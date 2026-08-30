#include "core/net_socket.h"
#include "core/errors.h"
#include "core/ipv4.h"
#include "core/log.h"
#include "core/net_buffer.h"
#include "core/string.h"
#include "core/tcp.h"
#include "core/timer.h"
#include "fs/vfs.h"

#define NET_SOCKET_HANDLE_SLOT_MASK 0xFFU
#define NET_SOCKET_HANDLE_GENERATION_SHIFT 8U
#define NET_SOCKET_HANDLE_GENERATION_MAX 0x00FFFFFFU
#define NET_SOCKET_EPHEMERAL_RANGE \
    ((uint32_t)TCP_EPHEMERAL_PORT_MAX - TCP_EPHEMERAL_PORT_MIN + 1U)

typedef struct {
    uint8_t active;
    uint8_t eof;
    uint8_t close_requested;
    uint32_t generation;
    net_socket_type_t type;
    net_socket_state_t state;
    tcp_connection_handle_t tcp_handle;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint16_t tx_head;
    uint16_t tx_tail;
    uint16_t tx_count;
    uint16_t rx_head;
    uint16_t rx_tail;
    uint16_t rx_count;
    int last_error;
    wait_queue_head_t wait_queue;
    uint8_t tx_buffer[NET_SOCKET_TX_CAPACITY];
    uint8_t rx_buffer[NET_SOCKET_RX_CAPACITY];
} net_socket_entry_t;

static net_socket_status_t net_socket_status;
static net_socket_entry_t net_sockets[NET_SOCKET_CAPACITY];
static net_socket_entry_t net_socket_test_fixture;
static uint32_t net_socket_generations[NET_SOCKET_CAPACITY];
static uint16_t net_socket_next_port;
static int32_t net_socket_find_handle(net_socket_handle_t handle);

typedef struct {
    net_socket_handle_t handle;
    net_socket_event_mask_t requested;
    net_socket_event_mask_t observed;
} net_socket_wait_context_t;

static void net_socket_wait_owner(uint32_t index, char* owner) {
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

static net_socket_event_mask_t net_socket_entry_events(
    const net_socket_entry_t* socket) {
    net_socket_event_mask_t events = 0U;

    if (!socket || !socket->active) return NET_SOCKET_EVENT_CLOSED;
    if (socket->state == NET_SOCKET_STATE_CONNECTED) {
        events |= NET_SOCKET_EVENT_CONNECTED;
        if (socket->tx_count < NET_SOCKET_TX_CAPACITY) {
            events |= NET_SOCKET_EVENT_WRITABLE;
        }
    }
    if (socket->rx_count) events |= NET_SOCKET_EVENT_READABLE;
    if (socket->eof || socket->state == NET_SOCKET_STATE_EOF) {
        events |= NET_SOCKET_EVENT_EOF;
    }
    if (socket->state == NET_SOCKET_STATE_ERROR) {
        events |= NET_SOCKET_EVENT_ERROR;
    }
    return events;
}

static int net_socket_wait_condition(void* context, uint8_t* out_ready) {
    net_socket_wait_context_t* wait = (net_socket_wait_context_t*)context;
    int32_t index;

    if (!wait || !out_ready) {
        LOG_ERROR("NET", "Contexto nulo na condicao do socket");
        return ERR_NULL;
    }
    index = net_socket_find_handle(wait->handle);
    wait->observed = index < 0 ? NET_SOCKET_EVENT_CLOSED :
        net_socket_entry_events(&net_sockets[index]);
    *out_ready = (wait->observed & wait->requested) != 0U ||
                 (wait->observed & (NET_SOCKET_EVENT_EOF |
                                    NET_SOCKET_EVENT_ERROR |
                                    NET_SOCKET_EVENT_CLOSED)) != 0U;
    return OK;
}

static int net_socket_wake(uint32_t index, uint8_t all) {
    uint32_t woken = 0U;
    int result;

    result = all ? wake_up_all(&net_sockets[index].wait_queue, &woken) :
                   wake_up(&net_sockets[index].wait_queue, &woken);
    if (result != OK) {
        net_socket_status.wait_failures++;
        LOG_ERROR("NET", "Falha ao acordar espera de socket");
    }
    (void)vfs_poll_notify();
    return result;
}

static net_socket_handle_t net_socket_make_handle(uint32_t index) {
    return (net_sockets[index].generation <<
            NET_SOCKET_HANDLE_GENERATION_SHIFT) | (index + 1U);
}

static int32_t net_socket_find_handle(net_socket_handle_t handle) {
    uint32_t slot = handle & NET_SOCKET_HANDLE_SLOT_MASK;
    uint32_t generation =
        handle >> NET_SOCKET_HANDLE_GENERATION_SHIFT;

    if (!slot || slot > NET_SOCKET_CAPACITY || !generation) return -1;
    slot--;
    if (!net_sockets[slot].active ||
        net_sockets[slot].generation != generation) return -1;
    return (int32_t)slot;
}

static int32_t net_socket_find_tcp(tcp_connection_handle_t handle) {
    for (uint32_t index = 0; index < NET_SOCKET_CAPACITY; index++) {
        if (net_sockets[index].active &&
            net_sockets[index].tcp_handle == handle) {
            return (int32_t)index;
        }
    }
    return -1;
}

static uint8_t net_socket_port_used(uint16_t port) {
    for (uint32_t index = 0; index < NET_SOCKET_CAPACITY; index++) {
        if (net_sockets[index].active &&
            net_sockets[index].local_port == port) return 1;
    }
    return 0;
}

static uint16_t net_socket_allocate_port(void) {
    if (net_socket_next_port < TCP_EPHEMERAL_PORT_MIN) {
        net_socket_next_port = (uint16_t)(
            TCP_EPHEMERAL_PORT_MIN +
            (timer_get_ticks() % NET_SOCKET_EPHEMERAL_RANGE));
    }
    for (uint32_t attempt = 0;
         attempt < NET_SOCKET_EPHEMERAL_RANGE; attempt++) {
        uint16_t candidate = net_socket_next_port;

        net_socket_next_port++;
        if (net_socket_next_port < TCP_EPHEMERAL_PORT_MIN) {
            net_socket_next_port = TCP_EPHEMERAL_PORT_MIN;
        }
        if (!net_socket_port_used(candidate)) return candidate;
    }
    return 0;
}

static void net_socket_release(uint32_t index) {
    int result;

    if (!net_sockets[index].active) return;
    (void)vfs_poll_notify();
    result = wait_channel_set_available(&net_sockets[index].wait_queue, 0U);
    if (result != OK) {
        net_socket_status.wait_failures++;
        LOG_ERROR("NET", "Falha ao encerrar fila de espera do socket");
    }
    result = wait_channel_reset(&net_sockets[index].wait_queue);
    if (result != OK) {
        net_socket_status.wait_failures++;
        LOG_ERROR("NET", "Fila de socket permaneceu ocupada no fechamento");
        return;
    }
    kmemset(&net_sockets[index], 0, sizeof(net_sockets[index]));
    if (net_socket_status.active_count) {
        net_socket_status.active_count--;
    }
}

static int net_socket_start_close(uint32_t index) {
    net_socket_entry_t* socket = &net_sockets[index];
    int result = tcp_close(socket->tcp_handle);

    if (result == OK) return OK;
    socket->state = NET_SOCKET_STATE_ERROR;
    socket->last_error = result;
    socket->close_requested = 0U;
    net_socket_status.last_error = result;
    net_socket_wake(index, 1U);
    LOG_ERROR("NET", "TCP recusou fechamento do socket");
    return result;
}

static uint16_t net_socket_ring_write(uint8_t* buffer,
                                      uint16_t capacity,
                                      uint16_t* tail,
                                      uint16_t* count,
                                      const uint8_t* data,
                                      uint16_t length) {
    uint16_t written = 0;

    while (written < length && *count < capacity) {
        buffer[*tail] = data[written++];
        *tail = (uint16_t)((*tail + 1U) % capacity);
        (*count)++;
    }
    return written;
}

static uint16_t net_socket_ring_read(uint8_t* buffer,
                                     uint16_t capacity,
                                     uint16_t* head,
                                     uint16_t* count,
                                     uint8_t* destination,
                                     uint16_t length) {
    uint16_t read = 0;

    while (read < length && *count) {
        destination[read++] = buffer[*head];
        *head = (uint16_t)((*head + 1U) % capacity);
        (*count)--;
    }
    return read;
}

static int net_socket_tcp_event(tcp_connection_handle_t handle,
                                tcp_event_t event,
                                const uint8_t* data,
                                uint16_t length, int error) {
    int32_t index = net_socket_find_tcp(handle);
    net_socket_entry_t* socket;

    if (index < 0) {
        LOG_WARN("NET", "Evento TCP para socket inexistente");
        return ERR_NOT_FOUND;
    }
    socket = &net_sockets[index];
    if (event == TCP_EVENT_CONNECTED) {
        socket->state = NET_SOCKET_STATE_CONNECTED;
        net_socket_status.connects++;
        net_socket_wake((uint32_t)index, 1U);
    } else if (event == TCP_EVENT_WRITABLE) {
        net_socket_wake((uint32_t)index, 0U);
    } else if (event == TCP_EVENT_DATA) {
        uint16_t free_space = NET_SOCKET_RX_CAPACITY - socket->rx_count;

        if (!data || length > free_space ||
            net_socket_ring_write(
                socket->rx_buffer, NET_SOCKET_RX_CAPACITY,
                &socket->rx_tail, &socket->rx_count,
                data, length) != length) {
            net_socket_status.rx_overflows++;
            LOG_WARN("NET", "Ring RX do socket ficou cheio");
            return ERR_OVERFLOW;
        }
        net_buffer_note_copy(length);
        net_socket_status.bytes_received_tcp += length;
        net_socket_wake((uint32_t)index, 0U);
    } else if (event == TCP_EVENT_EOF) {
        socket->eof = 1;
        if (socket->state != NET_SOCKET_STATE_CLOSING) {
            socket->state = NET_SOCKET_STATE_EOF;
        }
        net_socket_wake((uint32_t)index, 1U);
    } else if (event == TCP_EVENT_ERROR) {
        socket->state = NET_SOCKET_STATE_ERROR;
        socket->last_error = error;
        net_socket_status.last_error = error;
        net_socket_wake((uint32_t)index, 1U);
    } else if (event == TCP_EVENT_CLOSED) {
        net_socket_status.closes++;
        net_socket_release((uint32_t)index);
    }
    return OK;
}

int net_socket_init(void) {
    tcp_status_t tcp;

    LOG_INFO("NET", "Inicializando sockets nativos");
    if (net_socket_status.initialized) {
        LOG_WARN("NET", "Sockets nativos ja estavam inicializados");
        LOG_INFO("NET", "Sockets nativos inicializados com sucesso");
        return OK;
    }
    if (net_buffer_init() != OK) {
        LOG_ERROR("NET", "Contrato de buffers indisponivel para sockets");
        return ERR_STATE;
    }
    if (tcp_get_status(&tcp) != OK || !tcp.initialized) {
        LOG_ERROR("NET", "TCP indisponivel para sockets nativos");
        return ERR_STATE;
    }
    kmemset(&net_socket_status, 0, sizeof(net_socket_status));
    kmemset(net_sockets, 0, sizeof(net_sockets));
    kmemset(net_socket_generations, 0,
            sizeof(net_socket_generations));
    net_socket_next_port = 0;
    net_socket_status.initialized = 1;
    net_socket_status.last_error = OK;
    LOG_INFO("NET", "Sockets nativos inicializados com sucesso");
    return OK;
}

int net_socket_open(net_socket_type_t type,
                    net_socket_handle_t* out_handle) {
    int32_t free_index = -1;
    uint32_t generation;
    char wait_owner[WAIT_CHANNEL_OWNER_LENGTH];
    int result;

    if (!out_handle) {
        LOG_ERROR("NET", "Destino nulo ao abrir socket");
        return ERR_NULL;
    }
    *out_handle = 0;
    if (!net_socket_status.initialized) {
        LOG_ERROR("NET", "Abertura de socket antes da inicializacao");
        return ERR_STATE;
    }
    if (type != NET_SOCKET_TYPE_STREAM) {
        LOG_ERROR("NET", "Tipo de socket nao suportado");
        return ERR_UNAVAILABLE;
    }
    for (uint32_t index = 0; index < NET_SOCKET_CAPACITY; index++) {
        if (!net_sockets[index].active) {
            free_index = (int32_t)index;
            break;
        }
    }
    if (free_index < 0) {
        LOG_ERROR("NET", "Tabela de sockets cheia");
        return ERR_OVERFLOW;
    }
    generation = net_socket_generations[free_index] + 1U;
    if (!generation ||
        generation > NET_SOCKET_HANDLE_GENERATION_MAX) generation = 1U;
    net_socket_generations[free_index] = generation;
    kmemset(&net_sockets[free_index], 0,
            sizeof(net_sockets[free_index]));
    net_sockets[free_index].active = 1;
    net_sockets[free_index].generation = generation;
    net_sockets[free_index].type = type;
    net_sockets[free_index].state = NET_SOCKET_STATE_OPEN;
    net_socket_wait_owner((uint32_t)free_index, wait_owner);
    result = init_waitqueue_head(&net_sockets[free_index].wait_queue,
                                 wait_owner);
    if (result != OK) {
        kmemset(&net_sockets[free_index], 0,
                sizeof(net_sockets[free_index]));
        LOG_ERROR("NET", "Falha ao criar fila de espera do socket");
        return result;
    }
    net_socket_status.active_count++;
    net_socket_status.opens++;
    *out_handle = net_socket_make_handle((uint32_t)free_index);
    return OK;
}

int net_socket_connect(net_socket_handle_t handle,
                       uint32_t remote_ip, uint16_t remote_port) {
    int32_t index;
    uint16_t local_port;
    tcp_connection_handle_t tcp_handle;
    int result;

    if (!net_socket_status.initialized) {
        LOG_ERROR("NET", "Conexao de socket antes da inicializacao");
        return ERR_STATE;
    }
    index = net_socket_find_handle(handle);
    if (index < 0) {
        net_socket_status.stale_handles++;
        LOG_ERROR("NET", "Handle de socket invalido na conexao");
        return ERR_INVALID;
    }
    if (net_sockets[index].state != NET_SOCKET_STATE_OPEN ||
        !remote_port || !ipv4_address_is_unicast(remote_ip)) {
        LOG_ERROR("NET", "Estado ou destino invalido para socket");
        return ERR_INVALID;
    }
    local_port = net_socket_allocate_port();
    if (!local_port) {
        LOG_ERROR("NET", "Portas efemeras de socket esgotadas");
        return ERR_OVERFLOW;
    }
    net_sockets[index].local_port = local_port;
    net_sockets[index].remote_ip = remote_ip;
    net_sockets[index].remote_port = remote_port;
    net_sockets[index].state = NET_SOCKET_STATE_CONNECTING;
    result = tcp_connect(local_port, remote_ip, remote_port,
                         NET_SOCKET_RX_CAPACITY,
                         net_socket_tcp_event, &tcp_handle);
    if (result != OK) {
        net_sockets[index].state = NET_SOCKET_STATE_ERROR;
        net_sockets[index].last_error = result;
        net_socket_wake((uint32_t)index, 1U);
        LOG_ERROR("NET", "TCP recusou conexao do socket");
        return result;
    }
    net_sockets[index].tcp_handle = tcp_handle;
    return OK;
}

int net_socket_send(net_socket_handle_t handle, const uint8_t* data,
                    uint16_t length, uint16_t* out_written) {
    int32_t index;
    uint16_t free_space;

    if (!data || !out_written) {
        LOG_ERROR("NET", "Argumento nulo no envio por socket");
        return ERR_NULL;
    }
    *out_written = 0;
    if (!net_socket_status.initialized) {
        LOG_ERROR("NET", "Envio por socket antes da inicializacao");
        return ERR_STATE;
    }
    index = net_socket_find_handle(handle);
    if (index < 0) {
        net_socket_status.stale_handles++;
        LOG_ERROR("NET", "Handle de socket invalido no envio");
        return ERR_INVALID;
    }
    if (net_sockets[index].state == NET_SOCKET_STATE_ERROR) {
        return net_sockets[index].last_error == OK ?
               ERR_STATE : net_sockets[index].last_error;
    }
    if (net_sockets[index].state != NET_SOCKET_STATE_CONNECTED ||
        !length) return OK;
    free_space = NET_SOCKET_TX_CAPACITY - net_sockets[index].tx_count;
    if (length > free_space) length = free_space;
    *out_written = net_socket_ring_write(
        net_sockets[index].tx_buffer, NET_SOCKET_TX_CAPACITY,
        &net_sockets[index].tx_tail, &net_sockets[index].tx_count,
        data, length);
    if (*out_written) net_buffer_note_copy(*out_written);
    net_socket_status.bytes_queued_tx += *out_written;
    return OK;
}

int net_socket_receive(net_socket_handle_t handle, uint8_t* buffer,
                       uint16_t capacity, uint16_t* out_read,
                       uint8_t* out_eof) {
    int32_t index;
    net_socket_entry_t* socket;

    if (!buffer || !out_read || !out_eof) {
        LOG_ERROR("NET", "Argumento nulo na recepcao por socket");
        return ERR_NULL;
    }
    *out_read = 0;
    *out_eof = 0;
    if (!net_socket_status.initialized) {
        LOG_ERROR("NET", "Recepcao por socket antes da inicializacao");
        return ERR_STATE;
    }
    index = net_socket_find_handle(handle);
    if (index < 0) {
        net_socket_status.stale_handles++;
        LOG_ERROR("NET", "Handle de socket invalido na recepcao");
        return ERR_INVALID;
    }
    socket = &net_sockets[index];
    if (socket->state == NET_SOCKET_STATE_ERROR) {
        return socket->last_error == OK ? ERR_STATE :
                                          socket->last_error;
    }
    if (!capacity) {
        *out_eof = socket->eof && !socket->rx_count;
        return OK;
    }
    *out_read = net_socket_ring_read(
        socket->rx_buffer, NET_SOCKET_RX_CAPACITY,
        &socket->rx_head, &socket->rx_count, buffer, capacity);
    if (*out_read) net_buffer_note_copy(*out_read);
    net_socket_status.bytes_read += *out_read;
    if (socket->tcp_handle) {
        tcp_set_receive_window(
            socket->tcp_handle,
            NET_SOCKET_RX_CAPACITY - socket->rx_count);
    }
    *out_eof = socket->eof && !socket->rx_count;
    return OK;
}

int net_socket_wait(net_socket_handle_t handle,
                    net_socket_event_mask_t events,
                    uint32_t timeout_ticks,
                    net_socket_event_mask_t* out_events,
                    wait_reason_t* out_reason) {
    net_socket_wait_context_t context;
    int32_t index;
    uint8_t ready = 0U;
    int result;

    if (!out_events || !out_reason) {
        LOG_ERROR("NET", "Destino nulo na espera por socket");
        return ERR_NULL;
    }
    *out_events = 0U;
    *out_reason = WAIT_REASON_NONE;
    if (!net_socket_status.initialized) {
        LOG_ERROR("NET", "Espera por socket antes da inicializacao");
        return ERR_STATE;
    }
    if (!events || (events & ~NET_SOCKET_EVENT_ALL) != 0U) {
        LOG_ERROR("NET", "Mascara invalida na espera por socket");
        return ERR_INVALID;
    }
    index = net_socket_find_handle(handle);
    if (index < 0) {
        net_socket_status.stale_handles++;
        LOG_ERROR("NET", "Handle invalido na espera por socket");
        return ERR_INVALID;
    }

    context.handle = handle;
    context.requested = events;
    context.observed = 0U;
    net_socket_status.wait_calls++;
    result = wait_event_timeout(&net_sockets[index].wait_queue,
                                net_socket_wait_condition, &context,
                                timeout_ticks, out_reason);
    if (result != OK) {
        net_socket_status.wait_failures++;
        LOG_ERROR("NET", "Espera por socket falhou");
        return result;
    }
    if (net_socket_wait_condition(&context, &ready) != OK) {
        net_socket_status.wait_failures++;
        LOG_ERROR("NET", "Estado final da espera por socket falhou");
        return ERR_STATE;
    }
    *out_events = context.observed;
    if (*out_reason == WAIT_REASON_TIMEOUT) net_socket_status.wait_timeouts++;
    else if (*out_reason == WAIT_REASON_CANCELLED) {
        net_socket_status.wait_cancellations++;
    } else if (*out_reason == WAIT_REASON_EVENT ||
               *out_reason == WAIT_REASON_DEVICE_UNAVAILABLE) {
        net_socket_status.wait_events++;
    }
    return OK;
}

int net_socket_close(net_socket_handle_t handle) {
    int32_t index;
    net_socket_entry_t* socket;

    if (!net_socket_status.initialized) {
        LOG_ERROR("NET", "Fechamento de socket antes da inicializacao");
        return ERR_STATE;
    }
    index = net_socket_find_handle(handle);
    if (index < 0) {
        net_socket_status.stale_handles++;
        LOG_ERROR("NET", "Handle de socket invalido no fechamento");
        return ERR_INVALID;
    }
    socket = &net_sockets[index];
    if (socket->state == NET_SOCKET_STATE_OPEN) {
        net_socket_status.closes++;
        net_socket_release((uint32_t)index);
        return OK;
    }
    if (socket->state == NET_SOCKET_STATE_CONNECTING ||
        socket->state == NET_SOCKET_STATE_ERROR) {
        if (socket->tcp_handle) tcp_abort(socket->tcp_handle);
        net_socket_status.closes++;
        net_socket_release((uint32_t)index);
        return OK;
    }
    socket->close_requested = 1;
    socket->state = NET_SOCKET_STATE_CLOSING;
    if (!socket->tx_count && socket->tcp_handle) {
        return net_socket_start_close((uint32_t)index);
    }
    return OK;
}

int net_socket_abort(net_socket_handle_t handle) {
    int32_t index;

    if (!net_socket_status.initialized) {
        LOG_ERROR("NET", "Aborto de socket antes da inicializacao");
        return ERR_STATE;
    }
    index = net_socket_find_handle(handle);
    if (index < 0) {
        net_socket_status.stale_handles++;
        LOG_ERROR("NET", "Handle de socket invalido no aborto");
        return ERR_INVALID;
    }
    if (net_sockets[index].tcp_handle) {
        tcp_abort(net_sockets[index].tcp_handle);
    }
    net_socket_status.aborts++;
    net_socket_release((uint32_t)index);
    return OK;
}

static int net_socket_drain(uint32_t index) {
    net_socket_entry_t* socket = &net_sockets[index];
    uint8_t chunk[TCP_LOCAL_MSS];
    uint16_t length;
    uint16_t accepted = 0;
    int result;

    if (!socket->tcp_handle || !socket->tx_count ||
        (socket->state != NET_SOCKET_STATE_CONNECTED &&
         socket->state != NET_SOCKET_STATE_CLOSING)) return OK;
    length = socket->tx_count;
    if (length > sizeof(chunk)) length = sizeof(chunk);
    for (uint16_t offset = 0; offset < length; offset++) {
        uint16_t position =
            (uint16_t)((socket->tx_head + offset) %
                       NET_SOCKET_TX_CAPACITY);
        chunk[offset] = socket->tx_buffer[position];
    }
    net_buffer_note_copy(length);
    result = tcp_send(socket->tcp_handle, chunk, length, &accepted);
    if (result != OK) {
        socket->state = NET_SOCKET_STATE_ERROR;
        socket->last_error = result;
        net_socket_status.last_error = result;
        net_socket_wake(index, 1U);
        LOG_WARN("NET", "TCP recusou fila TX do socket");
        return result;
    }
    socket->tx_head =
        (uint16_t)((socket->tx_head + accepted) %
                   NET_SOCKET_TX_CAPACITY);
    socket->tx_count -= accepted;
    net_socket_status.bytes_sent_tcp += accepted;
    if (accepted) net_socket_wake(index, 0U);
    return OK;
}

int net_socket_maintain(void) {
    if (!net_socket_status.initialized) {
        LOG_ERROR("NET", "Manutencao de socket antes da inicializacao");
        return ERR_STATE;
    }
    net_socket_status.maintenance_cycles++;
    for (uint32_t index = 0; index < NET_SOCKET_CAPACITY; index++) {
        net_socket_entry_t* socket = &net_sockets[index];

        if (!socket->active) continue;
        if (net_socket_drain(index) != OK) {
            net_socket_status.last_error = socket->last_error;
        }
        if (!socket->active) continue;
        if (socket->tcp_handle) {
            tcp_set_receive_window(
                socket->tcp_handle,
                NET_SOCKET_RX_CAPACITY - socket->rx_count);
        }
        if (socket->close_requested && !socket->tx_count &&
            socket->tcp_handle) {
            net_socket_start_close(index);
        }
    }
    return OK;
}

int net_socket_reset(void) {
    int result;

    if (!net_socket_status.initialized) {
        LOG_ERROR("NET", "Reset de socket antes da inicializacao");
        return ERR_STATE;
    }
    result = tcp_reset();
    if (result != OK) {
        LOG_ERROR("NET", "TCP recusou reset dos sockets");
        return result;
    }
    for (uint32_t index = 0U; index < NET_SOCKET_CAPACITY; index++) {
        if (net_sockets[index].active) net_socket_release(index);
    }
    net_socket_status.last_error = OK;
    LOG_INFO("NET", "Sockets nativos reiniciados");
    return OK;
}

int net_socket_get_status(net_socket_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("NET", "Destino nulo ao consultar sockets");
        return ERR_NULL;
    }
    *out_status = net_socket_status;
    return OK;
}

static void net_socket_fill_info(uint32_t index,
                                 net_socket_info_t* out_info) {
    net_socket_entry_t* socket = &net_sockets[index];

    out_info->used = 1;
    out_info->handle = net_socket_make_handle(index);
    out_info->type = socket->type;
    out_info->state = socket->state;
    out_info->remote_ip = socket->remote_ip;
    out_info->local_port = socket->local_port;
    out_info->remote_port = socket->remote_port;
    out_info->tx_queued = socket->tx_count;
    out_info->rx_queued = socket->rx_count;
    out_info->eof = socket->eof;
    out_info->last_error = socket->last_error;
    out_info->waiters = socket->wait_queue.waiters;
}

int net_socket_get_info(uint32_t index, net_socket_info_t* out_info) {
    if (!out_info) {
        LOG_ERROR("NET", "Destino nulo ao consultar entrada de socket");
        return ERR_NULL;
    }
    if (index >= NET_SOCKET_CAPACITY) {
        LOG_ERROR("NET", "Indice de socket invalido");
        return ERR_INVALID;
    }
    kmemset(out_info, 0, sizeof(*out_info));
    if (net_sockets[index].active) {
        net_socket_fill_info(index, out_info);
    }
    return OK;
}

int net_socket_get_handle_info(net_socket_handle_t handle,
                               net_socket_info_t* out_info) {
    int32_t index;

    if (!out_info) {
        LOG_ERROR("NET", "Destino nulo ao consultar handle de socket");
        return ERR_NULL;
    }
    index = net_socket_find_handle(handle);
    if (index < 0) {
        net_socket_status.stale_handles++;
        LOG_ERROR("NET", "Handle de socket obsoleto");
        return ERR_INVALID;
    }
    kmemset(out_info, 0, sizeof(*out_info));
    net_socket_fill_info((uint32_t)index, out_info);
    return OK;
}

int net_socket_validate_state(void) {
    uint32_t active = 0;

    for (uint32_t index = 0; index < NET_SOCKET_CAPACITY; index++) {
        net_socket_entry_t* socket = &net_sockets[index];

        if (!socket->active) continue;
        active++;
        if (!socket->generation ||
            !socket->wait_queue.initialized ||
            !socket->wait_queue.available ||
            socket->type != NET_SOCKET_TYPE_STREAM ||
            socket->state > NET_SOCKET_STATE_ERROR ||
            socket->tx_count > NET_SOCKET_TX_CAPACITY ||
            socket->rx_count > NET_SOCKET_RX_CAPACITY ||
            socket->tx_head >= NET_SOCKET_TX_CAPACITY ||
            socket->tx_tail >= NET_SOCKET_TX_CAPACITY ||
            socket->rx_head >= NET_SOCKET_RX_CAPACITY ||
            socket->rx_tail >= NET_SOCKET_RX_CAPACITY ||
            (socket->state != NET_SOCKET_STATE_OPEN &&
             socket->state != NET_SOCKET_STATE_ERROR &&
             !socket->tcp_handle)) {
            LOG_ERROR("NET", "Entrada de socket inconsistente");
            return ERR_STATE;
        }
        for (uint32_t other = index + 1U;
             other < NET_SOCKET_CAPACITY; other++) {
            if (net_sockets[other].active &&
                socket->local_port &&
                net_sockets[other].local_port ==
                    socket->local_port) {
                LOG_ERROR("NET", "Porta local de socket duplicada");
                return ERR_STATE;
            }
        }
    }
    if (active != net_socket_status.active_count ||
        active > NET_SOCKET_CAPACITY ||
        (!net_socket_status.initialized && active) ||
        tcp_validate_state() != OK) {
        LOG_ERROR("NET", "Estado global de sockets inconsistente");
        return ERR_STATE;
    }
    return OK;
}

static void net_socket_test_count(net_socket_self_test_result_t* result,
                                  uint8_t passed) {
    if (passed) result->passed++;
    else result->failed++;
}

typedef struct {
    uint8_t blocked;
    wait_reason_t reason;
} net_socket_test_waiter_t;

static void net_socket_test_block(void* target, wait_queue_entry_t* entry) {
    net_socket_test_waiter_t* waiter =
        (net_socket_test_waiter_t*)target;

    (void)entry;
    waiter->blocked = 1U;
}

static void net_socket_test_wake(void* target, wait_queue_entry_t* entry) {
    net_socket_test_waiter_t* waiter =
        (net_socket_test_waiter_t*)target;

    waiter->blocked = 0U;
    waiter->reason = entry->reason;
}

static void net_socket_test_yield(void* target) {
    (void)target;
}

int net_socket_self_test(net_socket_self_test_result_t* out_result) {
    wait_queue_head_t queue;
    wait_queue_entry_t entries[2];
    net_socket_test_waiter_t waiters[2];
    wait_reason_t reason = WAIT_REASON_NONE;
    uint32_t condition;
    uint32_t woken = 0U;

    if (!out_result) {
        LOG_ERROR("NET", "Destino nulo no autoteste de sockets");
        return ERR_NULL;
    }
    kmemset(out_result, 0, sizeof(*out_result));
    kmemset(&queue, 0, sizeof(queue));
    kmemset(entries, 0, sizeof(entries));
    kmemset(waiters, 0, sizeof(waiters));
    kmemset(&net_socket_test_fixture, 0,
            sizeof(net_socket_test_fixture));
    out_result->lifecycle =
        init_waitqueue_head(&queue, "socket-fixture") == OK;
    net_socket_test_count(out_result, out_result->lifecycle);
    if (!out_result->lifecycle) return ERR_STATE;

    net_socket_test_fixture.active = 1U;
    net_socket_test_fixture.state = NET_SOCKET_STATE_CONNECTED;
    net_socket_test_fixture.rx_count = 1U;
    out_result->event_mapping =
        (net_socket_entry_events(&net_socket_test_fixture) &
         (NET_SOCKET_EVENT_CONNECTED | NET_SOCKET_EVENT_READABLE)) ==
        (NET_SOCKET_EVENT_CONNECTED | NET_SOCKET_EVENT_READABLE);
    net_socket_test_fixture.state = NET_SOCKET_STATE_EOF;
    net_socket_test_fixture.eof = 1U;
    out_result->event_mapping = out_result->event_mapping &&
        (net_socket_entry_events(&net_socket_test_fixture) &
         NET_SOCKET_EVENT_EOF) != 0U;
    net_socket_test_fixture.state = NET_SOCKET_STATE_ERROR;
    out_result->event_mapping = out_result->event_mapping &&
        (net_socket_entry_events(&net_socket_test_fixture) &
         NET_SOCKET_EVENT_ERROR) != 0U;
    net_socket_test_fixture.active = 0U;
    out_result->event_mapping = out_result->event_mapping &&
        net_socket_entry_events(&net_socket_test_fixture) ==
            NET_SOCKET_EVENT_CLOSED;
    net_socket_test_count(out_result, out_result->event_mapping);

    for (uint32_t index = 0U; index < 2U; index++) {
        wait_queue_entry_init(&entries[index], &waiters[index],
                              "socket-test", WAIT_TARGET_PROCESS,
                              index + 1U, net_socket_test_block,
                              net_socket_test_wake,
                              net_socket_test_yield);
    }
    condition = queue.condition;
    wait_queue_block(&queue, &entries[0], condition,
                     WAIT_TIMEOUT_INFINITE, &reason);
    wait_queue_block(&queue, &entries[1], condition,
                     WAIT_TIMEOUT_INFINITE, &reason);
    out_result->readable_wake_one =
        wake_up(&queue, &woken) == OK && woken == 1U &&
        !waiters[0].blocked && waiters[1].blocked && queue.waiters == 1U;
    net_socket_test_count(out_result, out_result->readable_wake_one);
    out_result->terminal_wake_all = wake_up_all(&queue, &woken) == OK &&
        woken == 1U && !waiters[1].blocked && !queue.waiters;
    net_socket_test_count(out_result, out_result->terminal_wake_all);

    kmemset(&entries[0], 0, sizeof(entries[0]));
    kmemset(&waiters[0], 0, sizeof(waiters[0]));
    wait_queue_entry_init(&entries[0], &waiters[0], "socket-timeout",
                          WAIT_TARGET_PROCESS, 3U, net_socket_test_block,
                          net_socket_test_wake, net_socket_test_yield);
    condition = queue.condition;
    wait_queue_block(&queue, &entries[0], condition,
                     WAIT_TIMEOUT_INFINITE, &reason);
    out_result->timeout =
        wait_queue_remove(&entries[0], WAIT_REASON_TIMEOUT) == OK &&
        waiters[0].reason == WAIT_REASON_TIMEOUT && !queue.waiters;
    net_socket_test_count(out_result, out_result->timeout);
    kmemset(&entries[0], 0, sizeof(entries[0]));
    kmemset(&waiters[0], 0, sizeof(waiters[0]));
    wait_queue_entry_init(&entries[0], &waiters[0], "socket-cancel",
                          WAIT_TARGET_PROCESS, 4U, net_socket_test_block,
                          net_socket_test_wake, net_socket_test_yield);
    condition = queue.condition;
    wait_queue_block(&queue, &entries[0], condition,
                     WAIT_TIMEOUT_INFINITE, &reason);
    out_result->cancellation =
        wait_queue_remove(&entries[0], WAIT_REASON_CANCELLED) == OK &&
        waiters[0].reason == WAIT_REASON_CANCELLED && !queue.waiters;
    net_socket_test_count(out_result, out_result->cancellation);
    out_result->invariants =
        (!net_socket_status.initialized || net_socket_validate_state() == OK) &&
                             wait_validate_state() == OK;
    net_socket_test_count(out_result, out_result->invariants);
    if (queue.waiters) {
        wait_queue_wake_target(&queue, WAIT_TARGET_ANY, WAIT_WAKE_ALL,
                               WAIT_REASON_CANCELLED, &woken);
    }
    if (wait_channel_reset(&queue) != OK) {
        out_result->lifecycle = 0U;
        out_result->failed++;
    }
    return out_result->failed ? ERR_STATE : OK;
}

const char* net_socket_state_name(net_socket_state_t state) {
    if (state == NET_SOCKET_STATE_OPEN) return "OPEN";
    if (state == NET_SOCKET_STATE_CONNECTING) return "CONNECTING";
    if (state == NET_SOCKET_STATE_CONNECTED) return "CONNECTED";
    if (state == NET_SOCKET_STATE_CLOSING) return "CLOSING";
    if (state == NET_SOCKET_STATE_EOF) return "EOF";
    if (state == NET_SOCKET_STATE_ERROR) return "ERROR";
    return "DESCONHECIDO";
}
