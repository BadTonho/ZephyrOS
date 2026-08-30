#include "core/socket.h"
#include "core/errors.h"
#include "core/ipv4.h"
#include "core/log.h"
#include "core/net_socket.h"
#include "core/poll.h"
#include "core/sk_buff.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "fs/vfs.h"
#include "fs/vfs_internal.h"
#include "process/process.h"

#define SOCKET_WAIT_READ 1U
#define SOCKET_WAIT_WRITE 2U
#define SOCKET_WAIT_ACCEPT 3U
#define SOCKET_PATH_PREFIX "socket:"
#define SOCKET_TCP_IO_CHUNK_MAX 0xFFFFU

typedef struct {
    sk_buff_t* entries[SOCKET_QUEUE_CAPACITY];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    uint32_t bytes;
} socket_queue_t;

struct socket_ops {
    int (*bind)(socket_t* socket, const socket_address_t* address);
    int (*connect)(socket_t* socket, const socket_address_t* address);
    int (*listen)(socket_t* socket, uint16_t backlog);
    int (*accept)(socket_t* socket, int32_t* fd_out);
    int (*send)(socket_t* socket, const uint8_t* data,
                uint32_t length, uint32_t* out_sent);
    int (*recv)(socket_t* socket, uint8_t* buffer,
                uint32_t capacity, uint32_t* out_read,
                uint8_t* out_eof);
    int (*close)(socket_t* socket);
};

struct socket {
    uint8_t used;
    uint8_t nonblocking;
    uint8_t fd_attached;
    uint8_t listener_bound;
    uint32_t owner_pid;
    int32_t fd;
    socket_family_t family;
    socket_type_t type;
    socket_state_t state;
    const socket_ops_t* ops;
    uint32_t flags;
    socket_address_t local;
    socket_address_t remote;
    int last_error;
    net_socket_handle_t tcp_handle;
    socket_t* peer;
    socket_t* listener;
    socket_queue_t rx;
    socket_t* pending[SOCKET_UNIX_BACKLOG_MAX];
    uint8_t pending_head;
    uint8_t pending_tail;
    uint8_t pending_count;
    uint16_t backlog;
    wait_queue_head_t wait_queue;
};

typedef struct {
    socket_t* socket;
    uint8_t operation;
} socket_wait_context_t;

static socket_t socket_pool[SOCKET_CAPACITY];
static socket_status_t socket_status;
static spinlock_t socket_lock;
static uint8_t socket_lock_initialized;
static uint8_t socket_testing;
static uint8_t socket_test_buffer[SOCKET_QUEUE_BYTES];

static int socket_vfs_open(vnode_t* vnode, file_t* file);
static int socket_vfs_read(file_t* file, void* buffer, uint32_t size,
                           uint32_t* bytes_read);
static int socket_vfs_write(file_t* file, const void* buffer, uint32_t size,
                            uint32_t* bytes_written);
static int socket_vfs_poll(file_t* file, uint32_t events,
                           uint32_t* revents);
static int socket_vfs_close(file_t* file);
static int socket_vfs_lseek(file_t* file, int32_t offset, uint32_t whence,
                            uint32_t* position);
static int socket_vfs_ioctl(file_t* file, uint32_t request, void* argument);
static int socket_vfs_sync(file_t* file);

static const file_operations_t socket_vfs_operations = {
    socket_vfs_open, socket_vfs_read, socket_vfs_write, socket_vfs_close,
    socket_vfs_lseek, socket_vfs_ioctl, socket_vfs_sync, socket_vfs_poll
};

static void socket_record_failure_locked(int result) {
    socket_status.failures++;
    socket_status.last_error = result;
}

static void socket_wait_owner(uint32_t index, char* owner) {
    owner[0] = 's';
    owner[1] = 'o';
    owner[2] = 'c';
    owner[3] = 'k';
    owner[4] = 'e';
    owner[5] = 't';
    owner[6] = '-';
    owner[7] = (char)('0' + ((index + 1U) / 10U));
    owner[8] = (char)('0' + ((index + 1U) % 10U));
    owner[9] = '\0';
}

static int32_t socket_index_locked(const socket_t* socket) {
    if (!socket) return -1;
    for (uint32_t index = 0U; index < SOCKET_CAPACITY; index++) {
        if (&socket_pool[index] == socket && socket_pool[index].used) {
            return (int32_t)index;
        }
    }
    return -1;
}

static socket_t* socket_find_fd(int32_t fd) {
    uint32_t pid = process_get_current_pid();
    socket_t* result = 0;

    if (!socket_lock_initialized) return 0;
    if (fd < VFS_FD_FIRST_FILE || !pid) {
        if (socket_lock_initialized) {
            spinlock_acquire(&socket_lock);
            socket_status.stale_fds++;
            socket_status.last_error = ERR_INVALID;
            spinlock_release(&socket_lock);
        }
        return 0;
    }
    spinlock_acquire(&socket_lock);
    for (uint32_t index = 0U; index < SOCKET_CAPACITY; index++) {
        if (socket_pool[index].used && socket_pool[index].fd_attached &&
            socket_pool[index].owner_pid == pid &&
            socket_pool[index].fd == fd) {
            result = &socket_pool[index];
            break;
        }
    }
    spinlock_release(&socket_lock);
    if (!result) {
        spinlock_acquire(&socket_lock);
        socket_status.stale_fds++;
        socket_status.last_error = ERR_INVALID;
        spinlock_release(&socket_lock);
    }
    return result;
}

static void socket_address_clear(socket_address_t* address,
                                 socket_family_t family) {
    if (!address) return;
    kmemset(address, 0, sizeof(*address));
    address->family = family;
}

static uint32_t socket_unix_path_length(const char* path) {
    if (!path) return 0U;
    for (uint32_t index = 0U; index < SOCKET_UNIX_PATH_MAX; index++) {
        if (!path[index]) return index;
    }
    return SOCKET_UNIX_PATH_MAX;
}

static int socket_address_valid(const socket_address_t* address) {
    uint32_t length;

    if (!address) {
        LOG_WARN("SOCKET", "Endereco nulo no runtime de sockets");
        return ERR_NULL;
    }
    if (address->family == SOCKET_FAMILY_INET) {
        if (!address->value.ipv4.address || !address->value.ipv4.port ||
            !ipv4_address_is_unicast(address->value.ipv4.address)) {
            LOG_WARN("SOCKET", "Endereco IPv4 invalido");
            return ERR_INVALID;
        }
        return OK;
    }
    if (address->family != SOCKET_FAMILY_UNIX) {
        LOG_WARN("SOCKET", "Familia de endereco desconhecida");
        return ERR_INVALID;
    }
    length = socket_unix_path_length(address->value.local.path);
    if (!length) {
        LOG_WARN("SOCKET", "Caminho UNIX vazio");
        return ERR_INVALID;
    }
    if (length >= SOCKET_UNIX_PATH_MAX) {
        LOG_WARN("SOCKET", "Caminho UNIX excede o limite");
        return ERR_OVERFLOW;
    }
    return OK;
}

static void socket_copy_address(socket_address_t* destination,
                                const socket_address_t* source) {
    if (!destination || !source) return;
    kmemcpy(destination, source, sizeof(*destination));
}

static uint8_t socket_address_equal(const socket_address_t* first,
                                    const socket_address_t* second) {
    if (!first || !second || first->family != second->family) return 0U;
    if (first->family == SOCKET_FAMILY_INET) {
        return first->value.ipv4.address == second->value.ipv4.address &&
               first->value.ipv4.port == second->value.ipv4.port;
    }
    return kstrcmp(first->value.local.path, second->value.local.path) == 0;
}

static void socket_wake(socket_t* socket) {
    uint32_t woken = 0U;

    if (!socket || wake_up_all(&socket->wait_queue, &woken) != OK) {
        if (!socket_testing) LOG_ERROR("SOCKET", "Falha ao acordar socket");
    }
    (void)vfs_poll_notify();
}

static int socket_wait_condition(void* context, uint8_t* out_ready) {
    socket_wait_context_t* wait = (socket_wait_context_t*)context;
    socket_t* socket;

    if (!wait || !out_ready || !wait->socket) {
        LOG_ERROR("SOCKET", "Contexto invalido na espera de socket");
        return ERR_NULL;
    }
    socket = wait->socket;
    spinlock_acquire(&socket_lock);
    if (!socket->used) {
        *out_ready = 1U;
    } else if (wait->operation == SOCKET_WAIT_ACCEPT) {
        *out_ready = socket->state != SOCKET_STATE_LISTENING ||
                     socket->pending_count != 0U;
    } else if (wait->operation == SOCKET_WAIT_READ) {
        *out_ready = socket->rx.count != 0U ||
                     socket->state == SOCKET_STATE_EOF ||
                     socket->state == SOCKET_STATE_ERROR ||
                     socket->state == SOCKET_STATE_CLOSED;
    } else if (wait->operation == SOCKET_WAIT_WRITE) {
        if (socket->state == SOCKET_STATE_LISTENING) {
            *out_ready = socket->pending_count < socket->backlog;
        } else {
            *out_ready = socket->state != SOCKET_STATE_CONNECTED ||
                         !socket->peer ||
                         socket->peer->rx.bytes < SOCKET_QUEUE_BYTES;
        }
    } else {
        spinlock_release(&socket_lock);
        LOG_ERROR("SOCKET", "Operacao invalida na espera de socket");
        return ERR_INVALID;
    }
    spinlock_release(&socket_lock);
    return OK;
}

static int socket_wait_local(socket_t* socket, uint8_t operation) {
    socket_wait_context_t context;
    wait_reason_t reason = WAIT_REASON_NONE;
    int result;

    context.socket = socket;
    context.operation = operation;
    result = wait_event(&socket->wait_queue, socket_wait_condition,
                        &context, &reason);
    if (result != OK) {
        LOG_ERROR("SOCKET", "Espera de socket falhou");
        return result;
    }
    if (reason == WAIT_REASON_CANCELLED || reason == WAIT_REASON_SIGNAL) {
        return ERR_CANCELLED;
    }
    if (reason != WAIT_REASON_EVENT) {
        LOG_ERROR("SOCKET", "Motivo invalido na espera de socket");
        return ERR_STATE;
    }
    return OK;
}

static void socket_drop_buffer(sk_buff_t* buffer) {
    if (!buffer) return;
    if (skb_complete(buffer, ERR_CANCELLED, NET_BUFFER_OWNER_NONE) != OK) {
        if (!socket_testing) LOG_WARN("SOCKET", "Descarte de SKB recusado");
    }
    if (skb_release(buffer) != OK && !socket_testing) {
        LOG_WARN("SOCKET", "Liberacao de SKB recusada");
    }
}

static void socket_queue_clear(socket_queue_t* queue) {
    sk_buff_t* buffer;

    if (!queue) return;
    while (queue->count) {
        buffer = queue->entries[queue->head];
        queue->entries[queue->head] = 0;
        queue->head = (uint8_t)((queue->head + 1U) % SOCKET_QUEUE_CAPACITY);
        queue->count--;
        if (buffer) socket_drop_buffer(buffer);
    }
    queue->bytes = 0U;
    queue->tail = queue->head;
}

static uint8_t socket_queue_push_locked(socket_t* receiver, sk_buff_t* buffer) {
    if (!receiver || !buffer || receiver->rx.count >= SOCKET_QUEUE_CAPACITY ||
        buffer->len > SOCKET_QUEUE_BYTES - receiver->rx.bytes) {
        return 0U;
    }
    receiver->rx.entries[receiver->rx.tail] = buffer;
    receiver->rx.tail = (uint8_t)((receiver->rx.tail + 1U) %
                                  SOCKET_QUEUE_CAPACITY);
    receiver->rx.count++;
    receiver->rx.bytes += buffer->len;
    return 1U;
}

static sk_buff_t* socket_queue_front_locked(socket_t* socket) {
    if (!socket || !socket->rx.count) return 0;
    return socket->rx.entries[socket->rx.head];
}

static sk_buff_t* socket_queue_pop_locked(socket_t* socket) {
    sk_buff_t* buffer;

    if (!socket || !socket->rx.count) return 0;
    buffer = socket->rx.entries[socket->rx.head];
    socket->rx.entries[socket->rx.head] = 0;
    socket->rx.head = (uint8_t)((socket->rx.head + 1U) %
                                SOCKET_QUEUE_CAPACITY);
    socket->rx.count--;
    return buffer;
}

static int socket_allocate(socket_family_t family, socket_t** out_socket) {
    int32_t free_index = -1;
    char owner[WAIT_CHANNEL_OWNER_LENGTH];
    int result;

    if (!out_socket) {
        LOG_ERROR("SOCKET", "Destino nulo ao alocar socket");
        return ERR_NULL;
    }
    *out_socket = 0;
    spinlock_acquire(&socket_lock);
    for (uint32_t index = 0U; index < SOCKET_CAPACITY; index++) {
        if (!socket_pool[index].used) {
            free_index = (int32_t)index;
            kmemset(&socket_pool[index], 0, sizeof(socket_t));
            socket_pool[index].used = 1U;
            socket_pool[index].fd = VFS_FD_INVALID;
            socket_pool[index].family = family;
            socket_pool[index].type = SOCKET_TYPE_STREAM;
            socket_pool[index].state = SOCKET_STATE_OPEN;
            socket_address_clear(&socket_pool[index].local, family);
            socket_address_clear(&socket_pool[index].remote, family);
            socket_status.active_count++;
            if (socket_status.active_count > socket_status.peak_count) {
                socket_status.peak_count = socket_status.active_count;
            }
            break;
        }
    }
    spinlock_release(&socket_lock);
    if (free_index < 0) {
        spinlock_acquire(&socket_lock);
        socket_record_failure_locked(ERR_OVERFLOW);
        spinlock_release(&socket_lock);
        LOG_WARN("SOCKET", "Pool de sockets cheio");
        return ERR_OVERFLOW;
    }
    socket_wait_owner((uint32_t)free_index, owner);
    result = init_waitqueue_head(&socket_pool[free_index].wait_queue, owner);
    if (result != OK) {
        spinlock_acquire(&socket_lock);
        socket_pool[free_index].used = 0U;
        if (socket_status.active_count) socket_status.active_count--;
        socket_record_failure_locked(result);
        spinlock_release(&socket_lock);
        LOG_ERROR("SOCKET", "Falha ao criar fila do socket");
        return result;
    }
    *out_socket = &socket_pool[free_index];
    return OK;
}

static void socket_destroy(socket_t* socket) {
    int32_t slot;
    uint32_t index;

    if (!socket) return;
    spinlock_acquire(&socket_lock);
    if (!socket->used) {
        spinlock_release(&socket_lock);
        return;
    }
    slot = socket_index_locked(socket);
    if (slot < 0) {
        spinlock_release(&socket_lock);
        return;
    }
    index = (uint32_t)slot;
    socket->fd_attached = 0U;
    socket->fd = VFS_FD_INVALID;
    socket->owner_pid = 0U;
    socket->state = SOCKET_STATE_CLOSED;
    socket_queue_clear(&socket->rx);
    wait_channel_set_available(&socket->wait_queue, 0U);
    if (wait_channel_reset(&socket->wait_queue) != OK && !socket_testing) {
        LOG_ERROR("SOCKET", "Fila de socket permaneceu ocupada");
    }
    socket->used = 0U;
    if (socket_status.active_count) socket_status.active_count--;
    kmemset(&socket_pool[index], 0, sizeof(socket_t));
    socket_pool[index].fd = VFS_FD_INVALID;
    spinlock_release(&socket_lock);
}

static uint8_t socket_pending_remove_locked(socket_t* listener,
                                            socket_t* child) {
    socket_t* retained[SOCKET_UNIX_BACKLOG_MAX];
    uint8_t count = 0U;
    uint8_t removed = 0U;

    if (!listener || !child) return 0U;
    while (listener->pending_count) {
        socket_t* current = listener->pending[listener->pending_head];
        listener->pending[listener->pending_head] = 0;
        listener->pending_head = (uint8_t)((listener->pending_head + 1U) %
                                           SOCKET_UNIX_BACKLOG_MAX);
        listener->pending_count--;
        if (current == child) removed = 1U;
        else retained[count++] = current;
    }
    for (uint8_t index = 0U; index < count; index++) {
        listener->pending[listener->pending_tail] = retained[index];
        listener->pending_tail = (uint8_t)((listener->pending_tail + 1U) %
                                           SOCKET_UNIX_BACKLOG_MAX);
        listener->pending_count++;
    }
    return removed;
}

static socket_t* socket_pending_pop_locked(socket_t* listener) {
    socket_t* child;

    if (!listener || !listener->pending_count) return 0;
    child = listener->pending[listener->pending_head];
    listener->pending[listener->pending_head] = 0;
    listener->pending_head = (uint8_t)((listener->pending_head + 1U) %
                                       SOCKET_UNIX_BACKLOG_MAX);
    listener->pending_count--;
    return child;
}

static int socket_unix_bind(socket_t* socket,
                            const socket_address_t* address) {
    int result;

    result = socket_address_valid(address);
    if (result != OK) {
        LOG_ERROR("SOCKET", "Endereco invalido no bind UNIX");
        return result;
    }
    if (address->family != SOCKET_FAMILY_UNIX) return ERR_INVALID;
    spinlock_acquire(&socket_lock);
    if (!socket->used || socket->state != SOCKET_STATE_OPEN) {
        spinlock_release(&socket_lock);
        LOG_ERROR("SOCKET", "Estado invalido no bind UNIX");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < SOCKET_CAPACITY; index++) {
        socket_t* other = &socket_pool[index];

        if (other != socket && other->used && other->listener_bound &&
            socket_address_equal(&other->local, address)) {
            spinlock_release(&socket_lock);
            LOG_WARN("SOCKET", "Caminho UNIX ja esta em uso");
            return ERR_INVALID;
        }
    }
    socket_copy_address(&socket->local, address);
    socket->listener_bound = 1U;
    socket->state = SOCKET_STATE_BOUND;
    socket_status.binds++;
    spinlock_release(&socket_lock);
    return OK;
}

static int socket_unix_listen(socket_t* socket, uint16_t backlog) {
    if (!socket || !backlog || backlog > SOCKET_UNIX_BACKLOG_MAX) {
        LOG_ERROR("SOCKET", "Backlog invalido no listen UNIX");
        return ERR_INVALID;
    }
    spinlock_acquire(&socket_lock);
    if (!socket->used || socket->family != SOCKET_FAMILY_UNIX ||
        socket->state != SOCKET_STATE_BOUND || !socket->listener_bound) {
        spinlock_release(&socket_lock);
        LOG_ERROR("SOCKET", "Estado invalido no listen UNIX");
        return ERR_STATE;
    }
    socket->backlog = backlog;
    socket->state = SOCKET_STATE_LISTENING;
    spinlock_release(&socket_lock);
    return OK;
}

static int socket_unix_connect(socket_t* socket,
                               const socket_address_t* address);
static int socket_unix_accept(socket_t* socket, int32_t* fd_out);
static int socket_unix_send(socket_t* socket, const uint8_t* data,
                            uint32_t length, uint32_t* out_sent);
static int socket_unix_recv(socket_t* socket, uint8_t* buffer,
                            uint32_t capacity, uint32_t* out_read,
                            uint8_t* out_eof);
static int socket_unix_close(socket_t* socket);

static const socket_ops_t socket_unix_ops = {
    socket_unix_bind, socket_unix_connect, socket_unix_listen,
    socket_unix_accept, socket_unix_send, socket_unix_recv,
    socket_unix_close
};

static int socket_tcp_unavailable(socket_t* socket,
                                  const socket_address_t* address) {
    (void)socket;
    (void)address;
    LOG_WARN("SOCKET", "Operacao TCP passiva indisponivel na NET2");
    return ERR_UNAVAILABLE;
}

static int socket_tcp_listen(socket_t* socket, uint16_t backlog) {
    (void)socket;
    (void)backlog;
    LOG_WARN("SOCKET", "LISTEN TCP passivo permanece fora da NET2");
    return ERR_UNAVAILABLE;
}

static int socket_tcp_accept(socket_t* socket, int32_t* fd_out) {
    (void)socket;
    (void)fd_out;
    LOG_WARN("SOCKET", "ACCEPT TCP passivo permanece fora da NET2");
    return ERR_UNAVAILABLE;
}

static int socket_tcp_connect(socket_t* socket,
                              const socket_address_t* address);
static int socket_tcp_send(socket_t* socket, const uint8_t* data,
                           uint32_t length, uint32_t* out_sent);
static int socket_tcp_recv(socket_t* socket, uint8_t* buffer,
                           uint32_t capacity, uint32_t* out_read,
                           uint8_t* out_eof);
static int socket_tcp_close(socket_t* socket);

static const socket_ops_t socket_tcp_ops = {
    socket_tcp_unavailable, socket_tcp_connect, socket_tcp_listen,
    socket_tcp_accept, socket_tcp_send, socket_tcp_recv, socket_tcp_close
};

static int socket_tcp_wait(socket_t* socket,
                           net_socket_event_mask_t events) {
    net_socket_event_mask_t observed = 0U;
    wait_reason_t reason = WAIT_REASON_NONE;
    net_socket_info_t info;
    int result;

    result = net_socket_wait(socket->tcp_handle, events,
                             WAIT_TIMEOUT_INFINITE, &observed, &reason);
    if (result != OK) {
        LOG_ERROR("SOCKET", "Espera do backend TCP falhou");
        return result;
    }
    if (reason == WAIT_REASON_CANCELLED || reason == WAIT_REASON_SIGNAL) {
        return ERR_CANCELLED;
    }
    if (net_socket_get_handle_info(socket->tcp_handle, &info) != OK) {
        LOG_ERROR("SOCKET", "Estado do backend TCP indisponivel");
        return ERR_STATE;
    }
    spinlock_acquire(&socket_lock);
    if (info.state == NET_SOCKET_STATE_CONNECTED) {
        socket->state = SOCKET_STATE_CONNECTED;
    } else if (info.state == NET_SOCKET_STATE_EOF) {
        socket->state = SOCKET_STATE_EOF;
    } else if (info.state == NET_SOCKET_STATE_ERROR) {
        socket->state = SOCKET_STATE_ERROR;
        socket->last_error = info.last_error;
    }
    spinlock_release(&socket_lock);
    if ((observed & NET_SOCKET_EVENT_ERROR) != 0U) {
        return info.last_error == OK ? ERR_STATE : info.last_error;
    }
    return OK;
}

static int socket_tcp_connect(socket_t* socket,
                              const socket_address_t* address) {
    net_socket_status_t status;
    net_socket_info_t info;
    int result;

    result = socket_address_valid(address);
    if (result != OK || address->family != SOCKET_FAMILY_INET) {
        LOG_ERROR("SOCKET", "Endereco invalido na conexao TCP");
        return result == OK ? ERR_INVALID : result;
    }
    result = net_socket_get_status(&status);
    if (result != OK || !status.initialized) {
        LOG_ERROR("SOCKET", "Backend TCP indisponivel");
        return ERR_STATE;
    }
    spinlock_acquire(&socket_lock);
    if (!socket->used || socket->state != SOCKET_STATE_OPEN) {
        spinlock_release(&socket_lock);
        LOG_ERROR("SOCKET", "Estado invalido na conexao TCP");
        return ERR_STATE;
    }
    socket_copy_address(&socket->remote, address);
    socket->state = SOCKET_STATE_CONNECTING;
    spinlock_release(&socket_lock);
    result = net_socket_connect(socket->tcp_handle,
                                address->value.ipv4.address,
                                address->value.ipv4.port);
    if (result != OK) {
        spinlock_acquire(&socket_lock);
        socket->state = SOCKET_STATE_ERROR;
        socket->last_error = result;
        socket_record_failure_locked(result);
        spinlock_release(&socket_lock);
        LOG_ERROR("SOCKET", "Backend TCP recusou a conexao generica");
        return result;
    }
    if (socket->nonblocking) return OK;
    result = socket_tcp_wait(socket, NET_SOCKET_EVENT_CONNECTED |
                             NET_SOCKET_EVENT_ERROR |
                             NET_SOCKET_EVENT_CLOSED);
    if (result != OK) {
        LOG_ERROR("SOCKET", "Conexao TCP generica nao foi concluida");
        return result;
    }
    if (net_socket_get_handle_info(socket->tcp_handle, &info) != OK) {
        LOG_ERROR("SOCKET", "TCP desapareceu durante conexao");
        return ERR_STATE;
    }
    return info.state == NET_SOCKET_STATE_CONNECTED ? OK :
           (info.last_error == OK ? ERR_STATE : info.last_error);
}

static int socket_tcp_send(socket_t* socket, const uint8_t* data,
                           uint32_t length, uint32_t* out_sent) {
    uint32_t sent = 0U;
    int result = OK;
    socket_state_t state;
    uint8_t nonblocking;

    if (!out_sent) {
        LOG_ERROR("SOCKET", "Destino nulo no envio TCP");
        return ERR_NULL;
    }
    *out_sent = 0U;
    if (length && !data) {
        LOG_ERROR("SOCKET", "Dados nulos no envio TCP");
        return ERR_NULL;
    }
    spinlock_acquire(&socket_lock);
    state = socket->state;
    nonblocking = socket->nonblocking;
    spinlock_release(&socket_lock);
    if (state == SOCKET_STATE_CONNECTING) {
        if (nonblocking) return ERR_AGAIN;
        result = socket_tcp_wait(socket, NET_SOCKET_EVENT_CONNECTED |
                                 NET_SOCKET_EVENT_ERROR |
                                 NET_SOCKET_EVENT_CLOSED);
        if (result != OK) {
            LOG_ERROR("SOCKET", "Envio aguardou uma conexao TCP invalida");
            return result;
        }
        state = socket->state;
    }
    if (state != SOCKET_STATE_CONNECTED) {
        LOG_ERROR("SOCKET", "Envio solicitado fora do estado conectado");
        return state == SOCKET_STATE_EOF ? ERR_UNAVAILABLE : ERR_STATE;
    }
    while (sent < length) {
        uint32_t remaining = length - sent;
        uint16_t request;
        uint16_t written = 0U;

        if (remaining > SOCKET_TCP_IO_CHUNK_MAX) {
            remaining = SOCKET_TCP_IO_CHUNK_MAX;
        }
        request = (uint16_t)remaining;
        result = net_socket_send(socket->tcp_handle, data + sent,
                                  request, &written);
        if (result != OK) break;
        if (written) {
            sent += written;
            continue;
        }
        if (socket->nonblocking) {
            result = ERR_AGAIN;
            break;
        }
        result = socket_tcp_wait(socket, NET_SOCKET_EVENT_WRITABLE |
                                 NET_SOCKET_EVENT_ERROR |
                                 NET_SOCKET_EVENT_CLOSED);
        if (result != OK) break;
    }
    *out_sent = sent;
    if (sent) {
        spinlock_acquire(&socket_lock);
        socket_status.sends++;
        socket_status.bytes_sent += sent;
        spinlock_release(&socket_lock);
        return OK;
    }
    return result;
}

static int socket_tcp_recv(socket_t* socket, uint8_t* buffer,
                           uint32_t capacity, uint32_t* out_read,
                           uint8_t* out_eof) {
    uint32_t total = 0U;
    int result = OK;
    socket_state_t state;
    uint8_t nonblocking;

    if (!out_read || !out_eof) {
        LOG_ERROR("SOCKET", "Destino nulo na recepcao TCP");
        return ERR_NULL;
    }
    *out_read = 0U;
    *out_eof = 0U;
    if (capacity && !buffer) {
        LOG_ERROR("SOCKET", "Buffer nulo na recepcao TCP");
        return ERR_NULL;
    }
    if (!capacity) return OK;
    spinlock_acquire(&socket_lock);
    state = socket->state;
    nonblocking = socket->nonblocking;
    spinlock_release(&socket_lock);
    if (state == SOCKET_STATE_CONNECTING) {
        if (nonblocking) return ERR_AGAIN;
        result = socket_tcp_wait(socket, NET_SOCKET_EVENT_CONNECTED |
                                 NET_SOCKET_EVENT_ERROR |
                                 NET_SOCKET_EVENT_CLOSED);
        if (result != OK) {
            LOG_ERROR("SOCKET", "Recepcao aguardou uma conexao TCP invalida");
            return result;
        }
        state = socket->state;
    }
    if (state == SOCKET_STATE_OPEN) {
        LOG_ERROR("SOCKET", "Recepcao solicitada antes da conexao TCP");
        return ERR_STATE;
    }
    if (state == SOCKET_STATE_ERROR) {
        LOG_ERROR("SOCKET", "Recepcao recusada pelo TCP");
        return socket->last_error == OK ? ERR_STATE : socket->last_error;
    }
    while (total < capacity) {
        uint32_t remaining = capacity - total;
        uint16_t request;
        uint16_t read = 0U;
        uint8_t eof = 0U;

        if (remaining > SOCKET_TCP_IO_CHUNK_MAX) {
            remaining = SOCKET_TCP_IO_CHUNK_MAX;
        }
        request = (uint16_t)remaining;
        result = net_socket_receive(socket->tcp_handle, buffer + total,
                                     request, &read, &eof);
        if (result != OK) break;
        total += read;
        if (eof || read) {
            *out_eof = eof;
            break;
        }
        if (socket->nonblocking) {
            result = ERR_AGAIN;
            break;
        }
        result = socket_tcp_wait(socket, NET_SOCKET_EVENT_READABLE |
                                 NET_SOCKET_EVENT_EOF |
                                 NET_SOCKET_EVENT_ERROR |
                                 NET_SOCKET_EVENT_CLOSED);
        if (result != OK) break;
    }
    *out_read = total;
    if (total) {
        spinlock_acquire(&socket_lock);
        socket_status.receives++;
        socket_status.bytes_received += total;
        spinlock_release(&socket_lock);
        return OK;
    }
    return result;
}

static int socket_tcp_close(socket_t* socket) {
    int result;
    net_socket_info_t info;

    if (!socket || !socket->tcp_handle) {
        if (socket) socket_destroy(socket);
        return OK;
    }
    result = net_socket_close(socket->tcp_handle);
    if (result == OK && net_socket_get_handle_info(
            socket->tcp_handle, &info) == OK && info.used) {
        net_socket_abort(socket->tcp_handle);
    } else if (result != OK && result != ERR_INVALID) {
        net_socket_abort(socket->tcp_handle);
    }
    if (result != OK && result != ERR_INVALID) {
        LOG_ERROR("SOCKET", "Fechamento do backend TCP falhou");
    }
    socket_destroy(socket);
    return result == ERR_INVALID ? OK : result;
}

static int socket_unix_connect(socket_t* socket,
                               const socket_address_t* address) {
    socket_t* listener = 0;
    socket_t* child = 0;
    int result;

    result = socket_address_valid(address);
    if (result != OK || address->family != SOCKET_FAMILY_UNIX) {
        LOG_ERROR("SOCKET", "Endereco invalido na conexao UNIX");
        return result == OK ? ERR_INVALID : result;
    }
    for (;;) {
        spinlock_acquire(&socket_lock);
        if (!socket->used || socket->state != SOCKET_STATE_OPEN) {
            spinlock_release(&socket_lock);
            LOG_ERROR("SOCKET", "Estado invalido na conexao UNIX");
            return ERR_STATE;
        }
        for (uint32_t index = 0U; index < SOCKET_CAPACITY; index++) {
            socket_t* candidate = &socket_pool[index];

            if (candidate->used && candidate->listener_bound &&
                candidate->state == SOCKET_STATE_LISTENING &&
                socket_address_equal(&candidate->local, address)) {
                listener = candidate;
                break;
            }
        }
        if (!listener) {
            spinlock_release(&socket_lock);
            LOG_WARN("SOCKET", "Listener UNIX nao encontrado");
            return ERR_NOT_FOUND;
        }
        if (listener->pending_count < listener->backlog) {
            spinlock_release(&socket_lock);
            break;
        }
        spinlock_release(&socket_lock);
        if (socket->nonblocking) return ERR_AGAIN;
        result = socket_wait_local(listener, SOCKET_WAIT_WRITE);
        if (result != OK) return result;
    }
    result = socket_allocate(SOCKET_FAMILY_UNIX, &child);
    if (result != OK) return result;
    child->ops = &socket_unix_ops;
    child->listener = listener;
    socket_copy_address(&child->local, address);
    socket_copy_address(&child->remote, address);
    spinlock_acquire(&socket_lock);
    if (!listener->used || listener->state != SOCKET_STATE_LISTENING ||
        listener->pending_count >= listener->backlog) {
        spinlock_release(&socket_lock);
        socket_destroy(child);
        return socket->nonblocking ? ERR_AGAIN : ERR_STATE;
    }
    listener->pending[listener->pending_tail] = child;
    listener->pending_tail = (uint8_t)((listener->pending_tail + 1U) %
                                       SOCKET_UNIX_BACKLOG_MAX);
    listener->pending_count++;
    child->peer = socket;
    socket->peer = child;
    socket_copy_address(&socket->remote, address);
    socket->state = SOCKET_STATE_CONNECTED;
    socket_status.connects++;
    spinlock_release(&socket_lock);
    socket_wake(listener);
    socket_wake(socket);
    return OK;
}

static int socket_unix_accept(socket_t* socket, int32_t* fd_out) {
    socket_t* child;
    int result;

    if (!fd_out) {
        LOG_ERROR("SOCKET", "Destino nulo no accept UNIX");
        return ERR_NULL;
    }
    *fd_out = VFS_FD_INVALID;
    for (;;) {
        spinlock_acquire(&socket_lock);
        if (!socket->used || socket->state != SOCKET_STATE_LISTENING) {
            spinlock_release(&socket_lock);
            LOG_ERROR("SOCKET", "Estado invalido no accept UNIX");
            return ERR_STATE;
        }
        child = socket_pending_pop_locked(socket);
        spinlock_release(&socket_lock);
        if (child) break;
        if (socket->nonblocking) return ERR_AGAIN;
        result = socket_wait_local(socket, SOCKET_WAIT_ACCEPT);
        if (result != OK) return result;
    }
    child->owner_pid = process_get_current_pid();
    result = vfs_open_socket(child, &socket_vfs_operations,
                             VFS_MODE_READ_WRITE, SOCKET_PATH_PREFIX "unix",
                             fd_out);
    if (result != OK) {
        spinlock_acquire(&socket_lock);
        if (child->peer) {
            child->peer->peer = 0;
            child->peer->state = SOCKET_STATE_ERROR;
            child->peer->last_error = result;
        }
        spinlock_release(&socket_lock);
        socket_wake(child->peer);
        socket_destroy(child);
        return result;
    }
    spinlock_acquire(&socket_lock);
    child->fd = *fd_out;
    child->fd_attached = 1U;
    child->state = SOCKET_STATE_CONNECTED;
    socket_status.accepts++;
    spinlock_release(&socket_lock);
    socket_wake(socket);
    if (child->peer) socket_wake(child->peer);
    return OK;
}

static int socket_unix_send(socket_t* socket, const uint8_t* data,
                            uint32_t length, uint32_t* out_sent) {
    uint32_t sent = 0U;
    int result = OK;

    if (!out_sent) {
        LOG_ERROR("SOCKET", "Destino nulo no envio UNIX");
        return ERR_NULL;
    }
    *out_sent = 0U;
    if (length && !data) {
        LOG_ERROR("SOCKET", "Dados nulos no envio UNIX");
        return ERR_NULL;
    }
    while (sent < length) {
        socket_t* peer;
        uint32_t available;
        uint32_t chunk;
        sk_buff_t* buffer;

        spinlock_acquire(&socket_lock);
        peer = socket->peer;
        if (!socket->used || socket->state != SOCKET_STATE_CONNECTED ||
            !peer || !peer->used) {
            spinlock_release(&socket_lock);
            result = ERR_UNAVAILABLE;
            break;
        }
        available = SOCKET_QUEUE_BYTES - peer->rx.bytes;
        if (!available || peer->rx.count >= SOCKET_QUEUE_CAPACITY) {
            spinlock_release(&socket_lock);
            spinlock_acquire(&socket_lock);
            socket_status.queue_drops++;
            spinlock_release(&socket_lock);
            if (socket->nonblocking) {
                result = ERR_AGAIN;
                break;
            }
            result = socket_wait_local(socket, SOCKET_WAIT_WRITE);
            if (result != OK) break;
            continue;
        }
        chunk = length - sent;
        if (chunk > available) chunk = available;
        if (chunk > SK_BUFF_STORAGE_SIZE) chunk = SK_BUFF_STORAGE_SIZE;
        spinlock_release(&socket_lock);
        buffer = alloc_skb(chunk);
        if (!buffer) {
            result = ERR_MEM;
            break;
        }
        if (!skb_put(buffer, chunk)) {
            socket_drop_buffer(buffer);
            result = ERR_STATE;
            break;
        }
        kmemcpy(buffer->data, data + sent, chunk);
        net_buffer_note_copy(chunk);
        if (skb_transition(buffer, NET_BUFFER_STATE_QUEUED,
                           NET_BUFFER_OWNER_SOCKET) != OK) {
            socket_drop_buffer(buffer);
            result = ERR_STATE;
            break;
        }
        spinlock_acquire(&socket_lock);
        peer = socket->peer;
        if (!peer || !peer->used ||
            !socket_queue_push_locked(peer, buffer)) {
            spinlock_release(&socket_lock);
            spinlock_acquire(&socket_lock);
            socket_status.queue_drops++;
            spinlock_release(&socket_lock);
            socket_drop_buffer(buffer);
            if (socket->nonblocking) {
                result = ERR_AGAIN;
                break;
            }
            result = socket_wait_local(socket, SOCKET_WAIT_WRITE);
            if (result != OK) break;
            continue;
        }
        spinlock_release(&socket_lock);
        sent += chunk;
        socket_wake(peer);
    }
    *out_sent = sent;
    spinlock_acquire(&socket_lock);
    if (sent) {
        socket_status.sends++;
        socket_status.bytes_sent += sent;
    }
    spinlock_release(&socket_lock);
    if (sent) return OK;
    return result;
}

static int socket_unix_recv(socket_t* socket, uint8_t* buffer,
                            uint32_t capacity, uint32_t* out_read,
                            uint8_t* out_eof) {
    uint32_t total = 0U;
    int result = OK;

    if (!out_read || !out_eof) {
        LOG_ERROR("SOCKET", "Destino nulo na recepcao UNIX");
        return ERR_NULL;
    }
    *out_read = 0U;
    *out_eof = 0U;
    if (capacity && !buffer) {
        LOG_ERROR("SOCKET", "Buffer nulo na recepcao UNIX");
        return ERR_NULL;
    }
    if (!capacity) return OK;
    while (total < capacity) {
        sk_buff_t* packet;
        uint32_t chunk;
        socket_t* peer;

        spinlock_acquire(&socket_lock);
        packet = socket_queue_front_locked(socket);
        if (packet) {
            chunk = packet->len;
            if (chunk > capacity - total) chunk = capacity - total;
            kmemcpy(buffer + total, packet->data, chunk);
            if (socket->rx.bytes >= chunk) socket->rx.bytes -= chunk;
            total += chunk;
            if (!skb_pull(packet, chunk)) {
                spinlock_release(&socket_lock);
                result = ERR_STATE;
                break;
            }
            if (!packet->len) {
                packet = socket_queue_pop_locked(socket);
                if (skb_complete(packet, OK, NET_BUFFER_OWNER_SOCKET) != OK ||
                    skb_release(packet) != OK) {
                    spinlock_release(&socket_lock);
                    result = ERR_STATE;
                    break;
                }
            }
            net_buffer_note_copy(chunk);
            peer = socket->peer;
            spinlock_release(&socket_lock);
            socket_wake(peer);
            if (total) break;
        } else {
            *out_eof = socket->state == SOCKET_STATE_EOF ||
                       socket->state == SOCKET_STATE_CLOSED;
            if (socket->state == SOCKET_STATE_ERROR) {
                result = socket->last_error == OK ? ERR_STATE :
                         socket->last_error;
                spinlock_release(&socket_lock);
                break;
            }
            spinlock_release(&socket_lock);
            if (*out_eof) break;
            if (socket->nonblocking) {
                result = ERR_AGAIN;
                break;
            }
            result = socket_wait_local(socket, SOCKET_WAIT_READ);
            if (result != OK) break;
        }
    }
    *out_read = total;
    spinlock_acquire(&socket_lock);
    if (total) {
        socket_status.receives++;
        socket_status.bytes_received += total;
    }
    spinlock_release(&socket_lock);
    if (total) return OK;
    return result;
}

static int socket_unix_close(socket_t* socket) {
    socket_t* peer = 0;
    socket_t* unaccepted_peer = 0;
    socket_t* pending[SOCKET_UNIX_BACKLOG_MAX];
    uint8_t pending_count = 0U;

    spinlock_acquire(&socket_lock);
    if (socket->listener_bound) {
        while (socket->pending_count) {
            pending[pending_count++] = socket_pending_pop_locked(socket);
        }
        for (uint32_t index = 0U; index < SOCKET_CAPACITY; index++) {
            if (socket_pool[index].used &&
                socket_pool[index].listener == socket) {
                socket_pool[index].listener = 0;
            }
        }
        socket->listener_bound = 0U;
    }
    peer = socket->peer;
    if (peer) {
        peer->peer = 0;
        if (peer->state == SOCKET_STATE_CONNECTED) peer->state = SOCKET_STATE_EOF;
        if (peer->listener && !peer->fd_attached) {
            socket_pending_remove_locked(peer->listener, peer);
            unaccepted_peer = peer;
        }
    }
    if (socket->listener && !socket->fd_attached) {
        socket_pending_remove_locked(socket->listener, socket);
    }
    socket->peer = 0;
    socket->state = SOCKET_STATE_CLOSED;
    spinlock_release(&socket_lock);
    socket_wake(peer);
    socket_wake(socket);
    socket_wake(socket->listener);
    if (unaccepted_peer) socket_destroy(unaccepted_peer);
    for (uint8_t index = 0U; index < pending_count; index++) {
        socket_t* child = pending[index];

        if (child && child->peer) {
            child->peer->peer = 0;
            child->peer->state = SOCKET_STATE_ERROR;
            child->peer->last_error = ERR_UNAVAILABLE;
            socket_wake(child->peer);
        }
        socket_destroy(child);
    }
    socket_destroy(socket);
    return OK;
}

static int socket_vfs_open(vnode_t* vnode, file_t* file) {
    socket_t* socket;

    if (!vnode || !file || vnode->type != VFS_NODE_SOCKET ||
        !vnode->private_data) {
        LOG_ERROR("SOCKET", "Contexto invalido na abertura VFS");
        return ERR_NULL;
    }
    socket = (socket_t*)vnode->private_data;
    if (!socket->used) {
        LOG_ERROR("SOCKET", "Socket indisponivel na abertura VFS");
        return ERR_STATE;
    }
    return OK;
}

static int socket_poll_revents(socket_t* socket, uint32_t* revents) {
    socket_state_t state;
    socket_t* peer;
    net_socket_handle_t tcp_handle;
    net_socket_info_t tcp_info;
    int result;

    if (!socket || !revents) return ERR_NULL;
    *revents = 0U;
    spinlock_acquire(&socket_lock);
    if (!socket->used) {
        *revents = POLLHUP;
        spinlock_release(&socket_lock);
        return OK;
    }
    state = socket->state;
    peer = socket->peer;
    tcp_handle = socket->tcp_handle;
    if (socket->family == SOCKET_FAMILY_UNIX) {
        if (state == SOCKET_STATE_ERROR) *revents |= POLLERR;
        if (state == SOCKET_STATE_EOF || state == SOCKET_STATE_CLOSED) {
            if (socket->rx.count) *revents |= POLLIN;
            *revents |= POLLHUP;
        }
        if (state == SOCKET_STATE_LISTENING && socket->pending_count) {
            *revents |= POLLIN;
        }
        if (state == SOCKET_STATE_CONNECTED) {
            if (socket->rx.count) *revents |= POLLIN;
            if (!peer || !peer->used) {
                *revents |= POLLHUP | POLLERR;
            } else if (peer->rx.bytes < SOCKET_QUEUE_BYTES &&
                       peer->rx.count < SOCKET_QUEUE_CAPACITY) {
                *revents |= POLLOUT;
            }
        }
        spinlock_release(&socket_lock);
        return OK;
    }
    if (state == SOCKET_STATE_ERROR) *revents |= POLLERR;
    spinlock_release(&socket_lock);
    if (!tcp_handle) {
        *revents |= POLLHUP;
        return OK;
    }
    result = net_socket_get_handle_info(tcp_handle, &tcp_info);
    if (result != OK || !tcp_info.used) {
        *revents |= POLLHUP;
        return OK;
    }
    if (tcp_info.state == NET_SOCKET_STATE_CONNECTED) {
        if (tcp_info.rx_queued) *revents |= POLLIN;
        if (tcp_info.tx_queued < NET_SOCKET_TX_CAPACITY) {
            *revents |= POLLOUT;
        }
    } else if (tcp_info.state == NET_SOCKET_STATE_EOF) {
        if (tcp_info.rx_queued) *revents |= POLLIN;
        *revents |= POLLHUP;
    } else if (tcp_info.state == NET_SOCKET_STATE_ERROR) {
        *revents |= POLLERR;
    } else if (tcp_info.state == NET_SOCKET_STATE_CLOSING) {
        *revents |= POLLHUP;
    }
    return OK;
}

static int socket_vfs_poll(file_t* file, uint32_t events,
                           uint32_t* revents) {
    socket_t* socket;

    (void)events;
    if (!file || !file->vnode || !revents) {
        LOG_ERROR("SOCKET", "Contexto nulo no readiness VFS de socket");
        return ERR_NULL;
    }
    socket = (socket_t*)file->vnode->private_data;
    if (!socket) {
        LOG_ERROR("SOCKET", "Socket ausente no readiness VFS");
        return ERR_STATE;
    }
    return socket_poll_revents(socket, revents);
}

static int socket_vfs_read(file_t* file, void* buffer, uint32_t size,
                           uint32_t* bytes_read) {
    socket_t* socket;
    uint8_t eof = 0U;

    if (!file || !file->vnode || !bytes_read) {
        LOG_ERROR("SOCKET", "Contexto nulo na leitura VFS de socket");
        return ERR_NULL;
    }
    if (!(file->mode & VFS_MODE_READ)) return ERR_UNAVAILABLE;
    socket = (socket_t*)file->vnode->private_data;
    if (!socket || !socket->ops || !socket->ops->recv) {
        LOG_ERROR("SOCKET", "Operacao de leitura VFS ausente");
        return ERR_STATE;
    }
    return socket->ops->recv(socket, (uint8_t*)buffer, size, bytes_read,
                             &eof);
}

static int socket_vfs_write(file_t* file, const void* buffer, uint32_t size,
                            uint32_t* bytes_written) {
    socket_t* socket;

    if (!file || !file->vnode || !bytes_written) {
        LOG_ERROR("SOCKET", "Contexto nulo na escrita VFS de socket");
        return ERR_NULL;
    }
    if (!(file->mode & VFS_MODE_WRITE)) return ERR_UNAVAILABLE;
    socket = (socket_t*)file->vnode->private_data;
    if (!socket || !socket->ops || !socket->ops->send) {
        LOG_ERROR("SOCKET", "Operacao de escrita VFS ausente");
        return ERR_STATE;
    }
    return socket->ops->send(socket, (const uint8_t*)buffer, size,
                             bytes_written);
}

static int socket_vfs_close(file_t* file) {
    socket_t* socket;
    int result;

    if (!file || !file->vnode) {
        LOG_ERROR("SOCKET", "Contexto nulo no fechamento VFS de socket");
        return ERR_NULL;
    }
    socket = (socket_t*)file->vnode->private_data;
    if (!socket || !socket->ops || !socket->ops->close) {
        LOG_ERROR("SOCKET", "Operacao de fechamento VFS ausente");
        return ERR_STATE;
    }
    result = socket->ops->close(socket);
    spinlock_acquire(&socket_lock);
    if (result == OK) socket_status.closes++;
    else socket_record_failure_locked(result);
    spinlock_release(&socket_lock);
    return result;
}

static int socket_vfs_lseek(file_t* file, int32_t offset, uint32_t whence,
                            uint32_t* position) {
    (void)file;
    (void)offset;
    (void)whence;
    if (position) *position = 0U;
    LOG_WARN("SOCKET", "Seek nao suportado por socket");
    return ERR_UNAVAILABLE;
}

static int socket_vfs_ioctl(file_t* file, uint32_t request, void* argument) {
    (void)file;
    (void)request;
    (void)argument;
    LOG_WARN("SOCKET", "Ioctl nao suportado por socket");
    return ERR_UNAVAILABLE;
}

static int socket_vfs_sync(file_t* file) {
    (void)file;
    LOG_WARN("SOCKET", "Sync nao suportado por socket");
    return ERR_UNAVAILABLE;
}

int socket_init(void) {
    if (!socket_lock_initialized) {
        spinlock_init(&socket_lock);
        socket_lock_initialized = 1U;
    }
    if (socket_status.initialized) return OK;
    if (!vfs_is_ready()) {
        LOG_ERROR("SOCKET", "VFS indisponivel para sockets genericos");
        return ERR_UNAVAILABLE;
    }
    if (skb_init() != OK) {
        LOG_ERROR("SOCKET", "SKB indisponivel para sockets genericos");
        return ERR_STATE;
    }
    spinlock_acquire(&socket_lock);
    kmemset(socket_pool, 0, sizeof(socket_pool));
    kmemset(&socket_status, 0, sizeof(socket_status));
    socket_status.initialized = 1U;
    socket_status.last_error = OK;
    spinlock_release(&socket_lock);
    LOG_INFO("SOCKET", "Sockets genericos inicializados");
    return OK;
}

int socket_create(socket_family_t family, socket_type_t type,
                  uint32_t flags, int32_t* fd_out) {
    socket_t* socket;
    net_socket_status_t status;
    int result;

    if (!fd_out) {
        LOG_ERROR("SOCKET", "Destino nulo na criacao de socket");
        return ERR_NULL;
    }
    *fd_out = VFS_FD_INVALID;
    if (!socket_status.initialized) {
        LOG_ERROR("SOCKET", "Criacao antes da inicializacao de sockets");
        return ERR_STATE;
    }
    if ((family != SOCKET_FAMILY_UNIX && family != SOCKET_FAMILY_INET) ||
        type != SOCKET_TYPE_STREAM ||
        (flags & ~SOCKET_FLAG_NONBLOCK) != 0U) {
        LOG_ERROR("SOCKET", "Familia, tipo ou flags invalidos");
        return ERR_INVALID;
    }
    if (family == SOCKET_FAMILY_INET &&
        (net_socket_get_status(&status) != OK || !status.initialized)) {
        LOG_ERROR("SOCKET", "TCP indisponivel para socket IPv4");
        return ERR_UNAVAILABLE;
    }
    result = socket_allocate(family, &socket);
    if (result != OK) return result;
    socket->nonblocking = (flags & SOCKET_FLAG_NONBLOCK) != 0U;
    socket->flags = flags;
    socket->ops = family == SOCKET_FAMILY_INET ?
                  &socket_tcp_ops : &socket_unix_ops;
    if (family == SOCKET_FAMILY_INET) {
        result = net_socket_open(NET_SOCKET_TYPE_STREAM,
                                 &socket->tcp_handle);
        if (result != OK) {
            socket_destroy(socket);
            return result;
        }
    }
    socket->owner_pid = process_get_current_pid();
    result = vfs_open_socket(socket, &socket_vfs_operations,
                             VFS_MODE_READ_WRITE,
                             family == SOCKET_FAMILY_INET ?
                             SOCKET_PATH_PREFIX "inet" :
                             SOCKET_PATH_PREFIX "unix", fd_out);
    if (result != OK) {
        if (socket->tcp_handle) net_socket_abort(socket->tcp_handle);
        socket_destroy(socket);
        return result;
    }
    socket->fd = *fd_out;
    socket->fd_attached = 1U;
    spinlock_acquire(&socket_lock);
    socket_status.creates++;
    spinlock_release(&socket_lock);
    return OK;
}

int socket_bind(int32_t fd, const socket_address_t* address) {
    socket_t* socket = socket_find_fd(fd);

    if (!socket) {
        LOG_ERROR("SOCKET", "FD invalido no bind");
        return ERR_INVALID;
    }
    return socket->ops->bind(socket, address);
}

int socket_connect(int32_t fd, const socket_address_t* address) {
    socket_t* socket = socket_find_fd(fd);

    if (!socket) {
        LOG_ERROR("SOCKET", "FD invalido na conexao");
        return ERR_INVALID;
    }
    return socket->ops->connect(socket, address);
}

int socket_listen(int32_t fd, uint16_t backlog) {
    socket_t* socket = socket_find_fd(fd);

    if (!socket) {
        LOG_ERROR("SOCKET", "FD invalido no listen");
        return ERR_INVALID;
    }
    return socket->ops->listen(socket, backlog);
}

int socket_accept(int32_t fd, int32_t* fd_out) {
    socket_t* socket = socket_find_fd(fd);

    if (!socket) {
        LOG_ERROR("SOCKET", "FD invalido no accept");
        return ERR_INVALID;
    }
    return socket->ops->accept(socket, fd_out);
}

int socket_send(int32_t fd, const uint8_t* data,
                uint32_t length, uint32_t* out_sent) {
    socket_t* socket = socket_find_fd(fd);

    if (!socket) {
        LOG_ERROR("SOCKET", "FD invalido no envio");
        return ERR_INVALID;
    }
    return socket->ops->send(socket, data, length, out_sent);
}

int socket_recv(int32_t fd, uint8_t* buffer, uint32_t capacity,
                uint32_t* out_read, uint8_t* out_eof) {
    socket_t* socket = socket_find_fd(fd);

    if (!socket) {
        LOG_ERROR("SOCKET", "FD invalido na recepcao");
        return ERR_INVALID;
    }
    return socket->ops->recv(socket, buffer, capacity, out_read, out_eof);
}

int socket_set_nonblocking(int32_t fd, uint8_t enabled) {
    socket_t* socket = socket_find_fd(fd);

    if (!socket) {
        LOG_ERROR("SOCKET", "FD invalido ao ajustar bloqueio");
        return ERR_INVALID;
    }
    if (enabled > 1U) {
        LOG_ERROR("SOCKET", "Flag de bloqueio invalida");
        return ERR_INVALID;
    }
    spinlock_acquire(&socket_lock);
    socket->nonblocking = enabled;
    if (enabled) socket->flags |= SOCKET_FLAG_NONBLOCK;
    else socket->flags &= ~SOCKET_FLAG_NONBLOCK;
    spinlock_release(&socket_lock);
    return OK;
}

int socket_close(int32_t fd) {
    socket_t* socket = socket_find_fd(fd);

    if (!socket) {
        LOG_ERROR("SOCKET", "FD invalido no fechamento");
        return ERR_INVALID;
    }
    return vfs_close(fd);
}

static void socket_fill_info(socket_t* socket, uint32_t index,
                             socket_info_t* info) {
    if (!socket || !info) return;
    info->used = socket->used;
    info->fd = socket->fd;
    info->owner_pid = socket->owner_pid;
    info->family = socket->family;
    info->type = socket->type;
    info->state = socket->state;
    info->flags = socket->flags;
    info->local = socket->local;
    info->remote = socket->remote;
    info->rx_queued = socket->rx.bytes > 0xFFFFU ? 0xFFFFU :
                      (uint16_t)socket->rx.bytes;
    info->tx_queued = socket->peer && socket->peer->rx.bytes <= 0xFFFFU ?
                      (uint16_t)socket->peer->rx.bytes : 0U;
    info->backlog = socket->backlog;
    info->pending = socket->pending_count;
    info->last_error = socket->last_error;
    (void)index;
}

int socket_get_status(socket_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("SOCKET", "Destino nulo ao consultar sockets");
        return ERR_NULL;
    }
    if (!socket_lock_initialized) {
        LOG_ERROR("SOCKET", "Consulta de sockets antes da inicializacao");
        return ERR_STATE;
    }
    spinlock_acquire(&socket_lock);
    *out_status = socket_status;
    spinlock_release(&socket_lock);
    return OK;
}

int socket_get_info(uint32_t index, socket_info_t* out_info) {
    if (!out_info) {
        LOG_ERROR("SOCKET", "Destino nulo ao consultar entrada de socket");
        return ERR_NULL;
    }
    if (index >= SOCKET_CAPACITY) {
        LOG_ERROR("SOCKET", "Indice de socket invalido");
        return ERR_INVALID;
    }
    if (!socket_lock_initialized || !socket_status.initialized) {
        LOG_ERROR("SOCKET", "Consulta de entrada antes da inicializacao");
        return ERR_STATE;
    }
    kmemset(out_info, 0, sizeof(*out_info));
    spinlock_acquire(&socket_lock);
    socket_fill_info(&socket_pool[index], index, out_info);
    spinlock_release(&socket_lock);
    return OK;
}

int socket_validate_state(void) {
    uint32_t active = 0U;
    int result = OK;

    if (!socket_lock_initialized || !socket_status.initialized) {
        LOG_ERROR("SOCKET", "Validacao de sockets antes da inicializacao");
        return ERR_STATE;
    }
    if (vfs_validate_state() != OK || net_socket_validate_state() != OK ||
        skb_validate_state() != OK) {
        result = ERR_STATE;
    }
    spinlock_acquire(&socket_lock);
    for (uint32_t index = 0U; index < SOCKET_CAPACITY; index++) {
        socket_t* socket = &socket_pool[index];
        uint32_t pending_entries = 0U;

        if (!socket->used) continue;
        active++;
        if (!socket->wait_queue.initialized || !socket->wait_queue.available ||
            socket->family < SOCKET_FAMILY_UNIX ||
            socket->family > SOCKET_FAMILY_INET ||
            socket->type != SOCKET_TYPE_STREAM ||
            socket->state > SOCKET_STATE_CLOSED ||
            (socket->fd_attached && (!socket->owner_pid || socket->fd < 3)) ||
            (!socket->fd_attached &&
             (socket->fd != VFS_FD_INVALID || socket->owner_pid != 0U)) ||
            socket->rx.count > SOCKET_QUEUE_CAPACITY ||
            socket->rx.bytes > SOCKET_QUEUE_BYTES ||
            socket->rx.head >= SOCKET_QUEUE_CAPACITY ||
            socket->rx.tail >= SOCKET_QUEUE_CAPACITY ||
            socket->pending_head >= SOCKET_UNIX_BACKLOG_MAX ||
            socket->pending_tail >= SOCKET_UNIX_BACKLOG_MAX ||
            socket->pending_count > SOCKET_UNIX_BACKLOG_MAX ||
            socket->pending_count > socket->backlog ||
            (socket->listener_bound && socket->family != SOCKET_FAMILY_UNIX) ||
            (socket->listener_bound && socket->state != SOCKET_STATE_BOUND &&
             socket->state != SOCKET_STATE_LISTENING) ||
            (socket->state == SOCKET_STATE_LISTENING &&
             (!socket->listener_bound || !socket->backlog ||
              socket->backlog > SOCKET_UNIX_BACKLOG_MAX)) ||
            (socket->state != SOCKET_STATE_LISTENING &&
             socket->pending_count != 0U)) {
            result = ERR_STATE;
            break;
        }
        for (uint32_t other = index + 1U; other < SOCKET_CAPACITY; other++) {
            if (socket_pool[other].used && socket->listener_bound &&
                socket_pool[other].listener_bound &&
                socket_address_equal(&socket->local,
                                     &socket_pool[other].local)) {
                result = ERR_STATE;
            }
        }
        if (socket->peer && (!socket->peer->used ||
                             socket->peer->peer != socket)) {
            result = ERR_STATE;
        }
        if (socket->listener &&
            (!socket->listener->used ||
             socket->listener->family != SOCKET_FAMILY_UNIX ||
             !socket->listener->listener_bound)) {
            result = ERR_STATE;
        }
        for (uint32_t pending_index = 0U;
             pending_index < SOCKET_UNIX_BACKLOG_MAX; pending_index++) {
            socket_t* child = socket->pending[pending_index];

            if (!child) continue;
            pending_entries++;
            if (!socket->listener_bound || !child->used ||
                child->listener != socket || child->fd_attached ||
                !child->peer || child->peer->peer != child) {
                result = ERR_STATE;
            }
        }
        if (pending_entries != socket->pending_count) result = ERR_STATE;
    }
    if (active != socket_status.active_count || active > SOCKET_CAPACITY) {
        result = ERR_STATE;
    }
    spinlock_release(&socket_lock);
    if (result != OK) LOG_ERROR("SOCKET", "Estado global de sockets invalido");
    return result;
}

static void socket_test_count(socket_self_test_result_t* result,
                              uint8_t passed) {
    if (passed) result->passed++;
    else result->failed++;
}

typedef struct {
    uint8_t blocked;
    wait_reason_t reason;
} socket_test_waiter_t;

static void socket_test_block(void* target, wait_queue_entry_t* entry) {
    socket_test_waiter_t* waiter = (socket_test_waiter_t*)target;

    (void)entry;
    waiter->blocked = 1U;
}

static void socket_test_wake(void* target, wait_queue_entry_t* entry) {
    socket_test_waiter_t* waiter = (socket_test_waiter_t*)target;

    waiter->blocked = 0U;
    waiter->reason = entry->reason;
}

static void socket_test_yield(void* target) {
    (void)target;
}

int socket_self_test(socket_self_test_result_t* out_result) {
    socket_status_t saved_status;
    socket_address_t address;
    socket_address_t invalid_address;
    wait_queue_head_t cancel_queue;
    wait_queue_entry_t cancel_entry;
    socket_test_waiter_t cancel_waiter;
    socket_info_t listener_info;
    int32_t duplicate_fd = VFS_FD_INVALID;
    int32_t invalid_fd = VFS_FD_INVALID;
    int32_t listener_fd = VFS_FD_INVALID;
    int32_t client_fd = VFS_FD_INVALID;
    int32_t server_fd = VFS_FD_INVALID;
    uint8_t* buffer = socket_test_buffer;
    uint32_t sent = 0U;
    uint32_t filled_sent = 0U;
    uint32_t read = 0U;
    uint8_t eof = 0U;
    uint8_t nonblocking_set = 0U;
    uint8_t mapping_found = 0U;
    uint8_t bind_result = 0U;
    int result = OK;

    if (!out_result) {
        LOG_ERROR("SOCKET", "Destino nulo no autoteste de sockets");
        return ERR_NULL;
    }
    if (socket_get_status(&saved_status) != OK || !saved_status.initialized) {
        LOG_ERROR("SOCKET", "Falha ao capturar metricas do autoteste");
        return ERR_STATE;
    }
    kmemset(out_result, 0, sizeof(*out_result));
    kmemset(&address, 0, sizeof(address));
    kmemset(&invalid_address, 0, sizeof(invalid_address));
    kmemset(&cancel_queue, 0, sizeof(cancel_queue));
    kmemset(&cancel_entry, 0, sizeof(cancel_entry));
    kmemset(&cancel_waiter, 0, sizeof(cancel_waiter));
    address.family = SOCKET_FAMILY_UNIX;
    address.value.local.path[0] = 'n';
    address.value.local.path[1] = 'e';
    address.value.local.path[2] = 't';
    address.value.local.path[3] = '2';
    address.value.local.path[4] = '-';
    address.value.local.path[5] = 'f';
    address.value.local.path[6] = 'i';
    address.value.local.path[7] = 'x';
    address.value.local.path[8] = 't';
    address.value.local.path[9] = 'u';
    address.value.local.path[10] = 'r';
    address.value.local.path[11] = 'e';
    address.value.local.path[12] = '\0';
    socket_testing = 1U;
    out_result->lifecycle = socket_create(SOCKET_FAMILY_UNIX,
                                           SOCKET_TYPE_STREAM,
                                           SOCKET_FLAG_NONBLOCK,
                                           &listener_fd) == OK;
    socket_test_count(out_result, out_result->lifecycle);
    if (out_result->lifecycle) {
        for (uint32_t index = 0U; index < SOCKET_CAPACITY; index++) {
            if (socket_get_info(index, &listener_info) == OK &&
                listener_info.used && listener_info.fd == listener_fd) {
                mapping_found = 1U;
                break;
            }
        }
        out_result->fd_mapping = mapping_found;
        socket_test_count(out_result, out_result->fd_mapping);
        bind_result = socket_bind(listener_fd, &address) == OK;
        out_result->duplicate_bind =
            socket_create(SOCKET_FAMILY_UNIX, SOCKET_TYPE_STREAM,
                          SOCKET_FLAG_NONBLOCK, &duplicate_fd) == OK &&
            socket_bind(duplicate_fd, &address) == ERR_INVALID;
        socket_test_count(out_result, out_result->duplicate_bind);
        if (duplicate_fd != VFS_FD_INVALID) socket_close(duplicate_fd);
        duplicate_fd = VFS_FD_INVALID;
        out_result->unix_bind_connect = bind_result &&
            socket_listen(listener_fd, SOCKET_UNIX_BACKLOG_MAX) == OK;
        socket_test_count(out_result, out_result->unix_bind_connect);
    }
    out_result->invalid_inputs =
        socket_bind(VFS_FD_INVALID, &address) == ERR_INVALID &&
        socket_set_nonblocking(VFS_FD_INVALID, 1U) == ERR_INVALID;
    invalid_address.family = (socket_family_t)0U;
    out_result->invalid_inputs = out_result->invalid_inputs &&
        socket_create(SOCKET_FAMILY_UNIX, SOCKET_TYPE_STREAM,
                      SOCKET_FLAG_NONBLOCK, &invalid_fd) == OK &&
        socket_bind(invalid_fd, &invalid_address) == ERR_INVALID;
    socket_test_count(out_result, out_result->invalid_inputs);
    if (invalid_fd != VFS_FD_INVALID) socket_close(invalid_fd);
    invalid_fd = VFS_FD_INVALID;
    if (out_result->unix_bind_connect) {
        out_result->unix_accept =
            socket_create(SOCKET_FAMILY_UNIX, SOCKET_TYPE_STREAM,
                          SOCKET_FLAG_NONBLOCK, &client_fd) == OK &&
            socket_connect(client_fd, &address) == OK &&
            socket_accept(listener_fd, &server_fd) == OK;
        socket_test_count(out_result, out_result->unix_accept);
    }
    if (out_result->unix_accept) {
        const uint8_t message[] = {'n', 'e', 't', '2', '-', 'o', 'k'};

        nonblocking_set = socket_set_nonblocking(server_fd, 1U) == OK;

        out_result->stream_io =
            socket_send(client_fd, message, sizeof(message), &sent) == OK &&
            sent == sizeof(message) &&
            socket_recv(server_fd, buffer, 3U, &read, &eof) == OK &&
            read == 3U && !eof &&
            socket_recv(server_fd, buffer, SOCKET_QUEUE_BYTES, &read, &eof) == OK &&
            read == sizeof(message) - 3U;
        socket_test_count(out_result, out_result->stream_io);
        kmemset(buffer, 'x', SOCKET_QUEUE_BYTES);
        out_result->nonblocking = nonblocking_set &&
            socket_recv(server_fd, buffer, 1U, &read, &eof) == ERR_AGAIN;
        socket_test_count(out_result, out_result->nonblocking);
        result = socket_send(client_fd, buffer, SOCKET_QUEUE_BYTES,
                             &filled_sent);
        out_result->queue_full = result == OK &&
            filled_sent == SOCKET_QUEUE_BYTES &&
            socket_send(client_fd, buffer, 1U, &sent) == ERR_AGAIN &&
            sent == 0U;
        socket_test_count(out_result, out_result->queue_full);
        if (result == OK && filled_sent == SOCKET_QUEUE_BYTES) {
            result = socket_close(server_fd);
            server_fd = VFS_FD_INVALID;
            out_result->eof = result == OK &&
                     socket_recv(client_fd, buffer, 1U, &read, &eof) == OK &&
                     !read && eof;
            result = out_result->eof ? OK : ERR_STATE;
        }
        out_result->close_wakeup = result == OK && out_result->eof;
        socket_test_count(out_result, out_result->close_wakeup);
    }
    if (server_fd != VFS_FD_INVALID) socket_close(server_fd);
    if (client_fd != VFS_FD_INVALID) socket_close(client_fd);
    if (listener_fd != VFS_FD_INVALID) socket_close(listener_fd);
    if (init_waitqueue_head(&cancel_queue, "socket-cancel") == OK &&
        wait_queue_entry_init(&cancel_entry, &cancel_waiter, "socket-test",
                              WAIT_TARGET_PROCESS, process_get_current_pid(),
                              socket_test_block,
                              socket_test_wake, socket_test_yield) == OK &&
        wait_queue_block(&cancel_queue, &cancel_entry, 0U,
                         WAIT_TIMEOUT_INFINITE, &cancel_waiter.reason) == OK &&
        cancel_waiter.blocked &&
        wait_queue_remove(&cancel_entry, WAIT_REASON_CANCELLED) == OK &&
        cancel_waiter.reason == WAIT_REASON_CANCELLED) {
        out_result->cancellation = 1U;
    }
    if (cancel_queue.initialized) wait_channel_reset(&cancel_queue);
    socket_test_count(out_result, out_result->cancellation);
    socket_testing = 0U;
    spinlock_acquire(&socket_lock);
    if (socket_status.active_count != saved_status.active_count) {
        result = ERR_STATE;
    }
    socket_status = saved_status;
    spinlock_release(&socket_lock);
    out_result->invariants = socket_validate_state() == OK;
    socket_test_count(out_result, out_result->invariants);
    if (result != OK || out_result->failed) {
        LOG_ERROR("SOCKET", "Autoteste de sockets falhou");
        return ERR_STATE;
    }
    return OK;
}

const char* socket_family_name(socket_family_t family) {
    if (family == SOCKET_FAMILY_UNIX) return "UNIX";
    if (family == SOCKET_FAMILY_INET) return "INET";
    return "UNKNOWN";
}

const char* socket_state_name(socket_state_t state) {
    if (state == SOCKET_STATE_OPEN) return "OPEN";
    if (state == SOCKET_STATE_BOUND) return "BOUND";
    if (state == SOCKET_STATE_LISTENING) return "LISTEN";
    if (state == SOCKET_STATE_CONNECTING) return "CONNECTING";
    if (state == SOCKET_STATE_CONNECTED) return "CONNECTED";
    if (state == SOCKET_STATE_EOF) return "EOF";
    if (state == SOCKET_STATE_ERROR) return "ERROR";
    if (state == SOCKET_STATE_CLOSED) return "CLOSED";
    return "UNKNOWN";
}
