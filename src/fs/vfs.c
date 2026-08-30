#include "fs/vfs.h"
#include "fs/vfs_internal.h"
#include "fs/fs.h"
#include "fs/storage.h"
#include "fs/devfs.h"
#include "core/app_api.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "core/wait.h"
#include "memory/slab.h"
#include "process/process.h"

#define VFS_TEST_DATA_SIZE 8U
#define VFS_FILE_ACTIVE 1U
#define VFS_FILE_RESERVED 2U

typedef struct {
    const uint8_t* data;
    uint32_t size;
} vfs_test_context_t;

typedef struct {
    vfs_lookup_result_t lookup;
    devfs_file_context_t device;
    struct vfs_pipe* pipe;
    uint8_t pipe_reader;
    uint8_t pipe_writer;
} vfs_file_context_t;

typedef struct vfs_pipe {
    spinlock_t lock;
    wait_channel_t read_channel;
    wait_channel_t write_channel;
    uint8_t buffer[VFS_PIPE_BUFFER_SIZE];
    uint32_t read_offset;
    uint32_t write_offset;
    uint32_t bytes;
    uint32_t readers;
    uint32_t writers;
    uint32_t slot;
    uint8_t used;
} pipe_t;

static file_t* vfs_file_pool[VFS_MAX_OPEN_FILES];
static vnode_t* vfs_vnode_pool[VFS_MAX_OPEN_FILES];
static vfs_file_context_t vfs_file_contexts[VFS_MAX_OPEN_FILES];
static pipe_t vfs_pipe_pool[VFS_MAX_PIPES];
static vfs_lookup_result_t vfs_test_root_lookup;
static vfs_lookup_result_t vfs_test_virtual_lookup;
static vfs_dir_entry_t vfs_test_dir_entries[VFS_MAX_DIR_ENTRIES];
static vnode_t* vfs_stdio_nodes[3];
static kmem_cache_t* vfs_file_cache = 0;
static kmem_cache_t* vfs_vnode_cache = 0;
static spinlock_t vfs_lock;
static vfs_status_t vfs_metrics;
static int vfs_ready;

static int vfs_regular_open(vnode_t* vnode, file_t* file);
static int vfs_regular_read(file_t* file, void* buffer, uint32_t size,
                            uint32_t* bytes_read);
static int vfs_regular_write(file_t* file, const void* buffer, uint32_t size,
                             uint32_t* bytes_written);
static int vfs_regular_close(file_t* file);
static int vfs_regular_sync(file_t* file);
static int vfs_regular_lseek(file_t* file, int32_t offset, uint32_t whence,
                             uint32_t* position);
static int vfs_pipe_open(vnode_t* vnode, file_t* file);
static int vfs_pipe_read(file_t* file, void* buffer, uint32_t size,
                         uint32_t* bytes_read);
static int vfs_pipe_write(file_t* file, const void* buffer, uint32_t size,
                          uint32_t* bytes_written);
static int vfs_pipe_close(file_t* file);
static int vfs_pipe_lseek(file_t* file, int32_t offset, uint32_t whence,
                          uint32_t* position);
static int vfs_unsupported_open(vnode_t* vnode, file_t* file);
static int vfs_unsupported_read(file_t* file, void* buffer, uint32_t size,
                                uint32_t* bytes_read);
static int vfs_unsupported_write(file_t* file, const void* buffer,
                                 uint32_t size, uint32_t* bytes_written);
static int vfs_unsupported_close(file_t* file);
static int vfs_unsupported_lseek(file_t* file, int32_t offset,
                                 uint32_t whence, uint32_t* position);
static int vfs_unsupported_ioctl(file_t* file, uint32_t request,
                                 void* argument);
static int vfs_unsupported_sync(file_t* file);

static const file_operations_t vfs_regular_operations = {
    vfs_regular_open, vfs_regular_read, vfs_regular_write,
    vfs_regular_close, vfs_regular_lseek, vfs_unsupported_ioctl,
    vfs_regular_sync
};

static const file_operations_t vfs_stdin_operations = {
    vfs_unsupported_open, vfs_stream_read, vfs_unsupported_write,
    vfs_unsupported_close, vfs_unsupported_lseek, vfs_unsupported_ioctl,
    vfs_unsupported_sync
};

static const file_operations_t vfs_stdout_operations = {
    vfs_unsupported_open, vfs_unsupported_read, vfs_stream_write,
    vfs_unsupported_close, vfs_unsupported_lseek, vfs_unsupported_ioctl,
    vfs_unsupported_sync
};

static const file_operations_t vfs_pipe_operations = {
    vfs_pipe_open, vfs_pipe_read, vfs_pipe_write, vfs_pipe_close,
    vfs_pipe_lseek, vfs_unsupported_ioctl, vfs_unsupported_sync
};

static void vfs_copy_text(char* destination, uint32_t capacity,
                          const char* source) {
    uint32_t index = 0U;

    if (!destination || capacity == 0U) return;
    while (source && source[index] && index + 1U < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int vfs_mode_valid(uint32_t mode) {
    return mode == VFS_MODE_READ || mode == VFS_MODE_WRITE ||
           mode == VFS_MODE_READ_WRITE;
}

static int vfs_initialize_stdio_nodes(void) {
    static const char* names[3] = {"stdin", "stdout", "stderr"};
    uint32_t index;

    for (index = 0U; index < 3U; index++) {
        vnode_t* vnode;

        spinlock_acquire(&vfs_lock);
        vnode = vfs_stdio_nodes[index];
        spinlock_release(&vfs_lock);
        if (vnode) continue;
        vnode = (vnode_t*)kmem_cache_alloc(vfs_vnode_cache);
        if (!vnode) {
            LOG_ERROR("FS", "Falha ao alocar vnode padrao VFS");
            return ERR_MEM;
        }
        vnode->type = index == 0U ? VFS_NODE_STDIN :
                      index == 1U ? VFS_NODE_STDOUT : VFS_NODE_STDERR;
        vnode->operations = index == 0U ?
            &vfs_stdin_operations : &vfs_stdout_operations;
        vfs_copy_text(vnode->path, VFS_MAX_PATH, names[index]);
        spinlock_acquire(&vfs_lock);
        if (!vfs_stdio_nodes[index]) vfs_stdio_nodes[index] = vnode;
        else {
            spinlock_release(&vfs_lock);
            kmem_cache_free(vfs_vnode_cache, vnode);
            continue;
        }
        spinlock_release(&vfs_lock);
    }
    return OK;
}

static int vfs_get_current_table(vfs_fd_table_t** table_out) {
    process_t* current;

    if (!table_out) return ERR_NULL;
    current = process_get_current();
    if (!current) {
        LOG_ERROR("FS", "Operacao VFS sem processo atual");
        return ERR_STATE;
    }
    if (!current->fd_table.initialized) {
        LOG_ERROR("FS", "Processo atual sem tabela de descritores");
        return ERR_STATE;
    }
    *table_out = &current->fd_table;
    return OK;
}

static int vfs_find_free_fd(vfs_fd_table_t* table) {
    uint32_t index;
    int fd = VFS_FD_INVALID;

    if (!table) return VFS_FD_INVALID;
    spinlock_acquire(&vfs_lock);
    for (index = VFS_FD_FIRST_FILE; index < VFS_MAX_FDS; index++) {
        if (!table->entries[index]) {
            fd = (int)index;
            break;
        }
    }
    spinlock_release(&vfs_lock);
    return fd;
}

static int vfs_allocate_file(file_t** file_out, vnode_t** vnode_out,
                             uint8_t state) {
    uint32_t index;
    file_t* file;
    vnode_t* vnode;

    if (!file_out || !vnode_out) {
        LOG_ERROR("FS", "Destinos nulos ao alocar arquivo VFS");
        return ERR_NULL;
    }
    for (index = 0U; index < VFS_MAX_OPEN_FILES; index++) {
        spinlock_acquire(&vfs_lock);
        if (vfs_file_pool[index] || vfs_vnode_pool[index]) {
            spinlock_release(&vfs_lock);
            continue;
        }
        spinlock_release(&vfs_lock);
        file = (file_t*)kmem_cache_alloc(vfs_file_cache);
        vnode = (vnode_t*)kmem_cache_alloc(vfs_vnode_cache);
        if (!file || !vnode) {
            if (file) kmem_cache_free(vfs_file_cache, file);
            if (vnode) kmem_cache_free(vfs_vnode_cache, vnode);
            vfs_metrics.failures++;
            LOG_ERROR("FS", "Falha ao alocar objetos VFS");
            return ERR_MEM;
        }
        file->used = state;
        file->slot = index;
        file->vnode = vnode;
        spinlock_acquire(&vfs_lock);
        if (vfs_file_pool[index] || vfs_vnode_pool[index]) {
            spinlock_release(&vfs_lock);
            kmem_cache_free(vfs_file_cache, file);
            kmem_cache_free(vfs_vnode_cache, vnode);
            continue;
        }
        kmemset(&vfs_file_contexts[index], 0,
                sizeof(vfs_file_context_t));
        vfs_file_pool[index] = file;
        vfs_vnode_pool[index] = vnode;
        *file_out = file;
        *vnode_out = vnode;
        spinlock_release(&vfs_lock);
        return OK;
    }
    vfs_metrics.failures++;
    LOG_WARN("FS", "Pool global de arquivos VFS cheio");
    return ERR_UNAVAILABLE;
}

static void vfs_release_file(file_t* file) {
    uint32_t slot;
    vnode_t* vnode;

    if (!file || file->persistent || !file->used) return;
    slot = file->slot;
    if (slot >= VFS_MAX_OPEN_FILES) return;
    spinlock_acquire(&vfs_lock);
    if (vfs_file_pool[slot] != file) {
        spinlock_release(&vfs_lock);
        LOG_ERROR("FS", "Arquivo VFS nao pertence ao slot informado");
        return;
    }
    vnode = vfs_vnode_pool[slot];
    vfs_file_pool[slot] = 0;
    vfs_vnode_pool[slot] = 0;
    kmemset(&vfs_file_contexts[slot], 0,
            sizeof(vfs_file_context_t));
    spinlock_release(&vfs_lock);
    kmem_cache_free(vfs_file_cache, file);
    if (vnode) kmem_cache_free(vfs_vnode_cache, vnode);
}

static int vfs_find_free_fd_pair(vfs_fd_table_t* table, int* read_fd,
                                 int* write_fd) {
    uint32_t index;

    if (!table || !read_fd || !write_fd) {
        LOG_ERROR("FS", "Parametros invalidos ao buscar par de descritores");
        return ERR_NULL;
    }
    *read_fd = VFS_FD_INVALID;
    *write_fd = VFS_FD_INVALID;
    spinlock_acquire(&vfs_lock);
    for (index = VFS_FD_FIRST_FILE; index < VFS_MAX_FDS; index++) {
        if (table->entries[index]) continue;
        if (*read_fd == VFS_FD_INVALID) *read_fd = (int)index;
        else {
            *write_fd = (int)index;
            break;
        }
    }
    spinlock_release(&vfs_lock);
    if (*read_fd == VFS_FD_INVALID || *write_fd == VFS_FD_INVALID) {
        *read_fd = VFS_FD_INVALID;
        *write_fd = VFS_FD_INVALID;
        LOG_WARN("FS", "Nao ha par de descritores livre para pipe");
        return ERR_UNAVAILABLE;
    }
    return OK;
}

static int vfs_pipe_allocate(pipe_t** pipe_out) {
    uint32_t index;
    int result;

    if (!pipe_out) return ERR_NULL;
    *pipe_out = 0;
    spinlock_acquire(&vfs_lock);
    for (index = 0U; index < VFS_MAX_PIPES; index++) {
        if (!vfs_pipe_pool[index].used) {
            kmemset(&vfs_pipe_pool[index], 0, sizeof(pipe_t));
            vfs_pipe_pool[index].slot = index;
            vfs_pipe_pool[index].used = 1U;
            spinlock_init(&vfs_pipe_pool[index].lock);
            *pipe_out = &vfs_pipe_pool[index];
            break;
        }
    }
    spinlock_release(&vfs_lock);
    if (!*pipe_out) {
        LOG_WARN("FS", "Pool global de pipes VFS cheio");
        return ERR_UNAVAILABLE;
    }

    result = wait_channel_init(&(*pipe_out)->read_channel, "VFS-pipe-r");
    if (result == OK) {
        result = wait_channel_init(&(*pipe_out)->write_channel,
                                   "VFS-pipe-w");
    }
    if (result != OK) {
        if ((*pipe_out)->read_channel.initialized) {
            if (wait_channel_reset(&(*pipe_out)->read_channel) != OK) {
                LOG_ERROR("FS", "Falha ao liberar espera de leitura do pipe");
            }
        }
        if ((*pipe_out)->write_channel.initialized) {
            if (wait_channel_reset(&(*pipe_out)->write_channel) != OK) {
                LOG_ERROR("FS", "Falha ao liberar espera de escrita do pipe");
            }
        }
        spinlock_acquire(&vfs_lock);
        kmemset(*pipe_out, 0, sizeof(pipe_t));
        spinlock_release(&vfs_lock);
        *pipe_out = 0;
        LOG_ERROR("FS", "Falha ao inicializar canais de espera do pipe");
        return result;
    }
    return OK;
}

static void vfs_pipe_release(pipe_t* pipe) {
    if (!pipe || !pipe->used) return;
    if (pipe->read_channel.initialized &&
        wait_channel_reset(&pipe->read_channel) != OK) {
        LOG_ERROR("FS", "Falha ao liberar canal de leitura do pipe");
    }
    if (pipe->write_channel.initialized &&
        wait_channel_reset(&pipe->write_channel) != OK) {
        LOG_ERROR("FS", "Falha ao liberar canal de escrita do pipe");
    }
    spinlock_acquire(&vfs_lock);
    kmemset(pipe, 0, sizeof(pipe_t));
    spinlock_release(&vfs_lock);
}

static pipe_t* vfs_pipe_from_file(file_t* file,
                                  vfs_file_context_t** context_out) {
    vfs_file_context_t* context;

    if (context_out) *context_out = 0;
    if (!file || !file->vnode || file->vnode->type != VFS_NODE_PIPE) {
        return 0;
    }
    context = (vfs_file_context_t*)file->vnode->private_data;
    if (!context || !context->pipe || !context->pipe->used) return 0;
    if (context_out) *context_out = context;
    return context->pipe;
}

static int vfs_pipe_read_ready(void* context, uint8_t* out_ready) {
    pipe_t* pipe = (pipe_t*)context;

    if (!pipe || !out_ready) {
        LOG_ERROR("FS", "Parametros invalidos ao consultar leitura de pipe");
        return ERR_NULL;
    }
    spinlock_acquire(&pipe->lock);
    *out_ready = pipe->bytes != 0U || pipe->writers == 0U;
    spinlock_release(&pipe->lock);
    return OK;
}

static int vfs_pipe_write_ready(void* context, uint8_t* out_ready) {
    pipe_t* pipe = (pipe_t*)context;

    if (!pipe || !out_ready) {
        LOG_ERROR("FS", "Parametros invalidos ao consultar escrita de pipe");
        return ERR_NULL;
    }
    spinlock_acquire(&pipe->lock);
    *out_ready = pipe->bytes < VFS_PIPE_BUFFER_SIZE || pipe->readers == 0U;
    spinlock_release(&pipe->lock);
    return OK;
}

static uint32_t vfs_pipe_copy_in(pipe_t* pipe, const uint8_t* data,
                                 uint32_t size) {
    uint32_t first;

    first = VFS_PIPE_BUFFER_SIZE - pipe->write_offset;
    if (first > size) first = size;
    kmemcpy(pipe->buffer + pipe->write_offset, data, first);
    if (first < size) kmemcpy(pipe->buffer, data + first, size - first);
    pipe->write_offset = (pipe->write_offset + size) % VFS_PIPE_BUFFER_SIZE;
    pipe->bytes += size;
    return size;
}

static uint32_t vfs_pipe_copy_out(pipe_t* pipe, uint8_t* data,
                                  uint32_t size) {
    uint32_t first;

    first = VFS_PIPE_BUFFER_SIZE - pipe->read_offset;
    if (first > size) first = size;
    kmemcpy(data, pipe->buffer + pipe->read_offset, first);
    if (first < size) kmemcpy(data + first, pipe->buffer, size - first);
    pipe->read_offset = (pipe->read_offset + size) % VFS_PIPE_BUFFER_SIZE;
    pipe->bytes -= size;
    return size;
}

static void vfs_pipe_wake_readers(pipe_t* pipe) {
    uint32_t woken = 0U;

    if (!pipe || wake_up_all(&pipe->read_channel, &woken) != OK) {
        LOG_ERROR("FS", "Falha ao acordar leitores do pipe");
    }
}

static void vfs_pipe_wake_writers(pipe_t* pipe) {
    uint32_t woken = 0U;

    if (!pipe || wake_up_all(&pipe->write_channel, &woken) != OK) {
        LOG_ERROR("FS", "Falha ao acordar escritores do pipe");
    }
}

static int vfs_pipe_open(vnode_t* vnode, file_t* file) {
    if (!vnode || !file || vnode->type != VFS_NODE_PIPE) {
        LOG_ERROR("FS", "Endpoint de pipe invalido na abertura");
        return ERR_NULL;
    }
    file->offset = 0U;
    return OK;
}

static int vfs_pipe_read(file_t* file, void* buffer, uint32_t size,
                         uint32_t* bytes_read) {
    vfs_file_context_t* context;
    pipe_t* pipe;
    wait_reason_t reason;
    uint32_t available;
    int result;

    if (!bytes_read) return ERR_NULL;
    *bytes_read = 0U;
    if (size && !buffer) return ERR_NULL;
    pipe = vfs_pipe_from_file(file, &context);
    if (!pipe || !context || !context->pipe_reader) return ERR_UNAVAILABLE;
    if (!size) return OK;
    while (*bytes_read == 0U) {
        spinlock_acquire(&pipe->lock);
        available = pipe->bytes;
        if (available) {
            if (available > size) available = size;
            vfs_pipe_copy_out(pipe, (uint8_t*)buffer, available);
            *bytes_read = available;
            spinlock_release(&pipe->lock);
            vfs_pipe_wake_writers(pipe);
            break;
        }
        if (pipe->writers == 0U) {
            spinlock_release(&pipe->lock);
            break;
        }
        spinlock_release(&pipe->lock);
        reason = WAIT_REASON_NONE;
        result = wait_event(&pipe->read_channel, vfs_pipe_read_ready,
                            pipe, &reason);
        if (result != OK) return result;
        if (reason == WAIT_REASON_SIGNAL || reason == WAIT_REASON_CANCELLED) {
            return OK;
        }
        if (reason != WAIT_REASON_EVENT) {
            LOG_ERROR("FS", "Motivo invalido na espera de leitura do pipe");
            return ERR_STATE;
        }
    }
    return OK;
}

static int vfs_pipe_write(file_t* file, const void* buffer, uint32_t size,
                          uint32_t* bytes_written) {
    vfs_file_context_t* context;
    pipe_t* pipe;
    wait_reason_t reason;
    uint32_t available;
    uint32_t remaining;
    int result;

    if (!bytes_written) return ERR_NULL;
    *bytes_written = 0U;
    if (size && !buffer) return ERR_NULL;
    pipe = vfs_pipe_from_file(file, &context);
    if (!pipe || !context || !context->pipe_writer) return ERR_UNAVAILABLE;
    while (*bytes_written < size) {
        spinlock_acquire(&pipe->lock);
        if (pipe->readers == 0U) {
            spinlock_release(&pipe->lock);
            LOG_WARN("FS", "Escrita recusada sem leitores no pipe");
            return ERR_UNAVAILABLE;
        }
        available = VFS_PIPE_BUFFER_SIZE - pipe->bytes;
        if (available) {
            remaining = size - *bytes_written;
            if (available > remaining) available = remaining;
            vfs_pipe_copy_in(pipe, (const uint8_t*)buffer + *bytes_written,
                             available);
            *bytes_written += available;
            spinlock_release(&pipe->lock);
            vfs_pipe_wake_readers(pipe);
            continue;
        }
        spinlock_release(&pipe->lock);
        reason = WAIT_REASON_NONE;
        result = wait_event(&pipe->write_channel, vfs_pipe_write_ready,
                            pipe, &reason);
        if (result != OK) return result;
        if (reason == WAIT_REASON_SIGNAL || reason == WAIT_REASON_CANCELLED) {
            return OK;
        }
        if (reason != WAIT_REASON_EVENT) {
            LOG_ERROR("FS", "Motivo invalido na espera de escrita do pipe");
            return ERR_STATE;
        }
    }
    return OK;
}

static int vfs_pipe_close(file_t* file) {
    vfs_file_context_t* context;
    pipe_t* pipe;
    uint8_t release = 0U;

    pipe = vfs_pipe_from_file(file, &context);
    if (!pipe || !context) {
        LOG_ERROR("FS", "Endpoint de pipe invalido no fechamento");
        return ERR_STATE;
    }
    spinlock_acquire(&pipe->lock);
    if (context->pipe_reader) {
        if (!pipe->readers) {
            spinlock_release(&pipe->lock);
            LOG_ERROR("FS", "Contagem de leitores do pipe inconsistente");
            return ERR_STATE;
        }
        pipe->readers--;
        context->pipe_reader = 0U;
    }
    if (context->pipe_writer) {
        if (!pipe->writers) {
            spinlock_release(&pipe->lock);
            LOG_ERROR("FS", "Contagem de escritores do pipe inconsistente");
            return ERR_STATE;
        }
        pipe->writers--;
        context->pipe_writer = 0U;
    }
    release = pipe->readers == 0U && pipe->writers == 0U;
    spinlock_release(&pipe->lock);
    vfs_pipe_wake_readers(pipe);
    vfs_pipe_wake_writers(pipe);
    if (release) vfs_pipe_release(pipe);
    return OK;
}

static int vfs_pipe_lseek(file_t* file, int32_t offset, uint32_t whence,
                          uint32_t* position) {
    (void)file;
    (void)offset;
    (void)whence;
    if (position) *position = 0U;
    LOG_WARN("FS", "Seek indisponivel em pipe");
    return ERR_UNAVAILABLE;
}

static int vfs_begin_operation(int32_t fd, file_t** file_out) {
    vfs_fd_table_t* table;
    file_t* file;
    int result;

    if (!vfs_ready) {
        LOG_ERROR("FS", "Operacao solicitada com VFS indisponivel");
        return ERR_UNAVAILABLE;
    }
    if (!file_out) {
        LOG_ERROR("FS", "Destino de operacao VFS nulo");
        return ERR_NULL;
    }
    if (fd < 0 || (uint32_t)fd >= VFS_MAX_FDS) {
        LOG_ERROR("FS", "Descritor VFS fora dos limites");
        return ERR_INVALID;
    }
    result = vfs_get_current_table(&table);
    if (result != OK) return result;
    spinlock_acquire(&vfs_lock);
    file = table->entries[fd];
    if (!file || !file->used || !file->vnode || !file->vnode->operations) {
        vfs_metrics.failures++;
        spinlock_release(&vfs_lock);
        LOG_ERROR("FS", "Descritor VFS fechado ou invalido");
        return ERR_INVALID;
    }
    if (file->active_operations != 0U) {
        vfs_metrics.failures++;
        spinlock_release(&vfs_lock);
        LOG_ERROR("FS", "Descritor VFS ocupado por outra operacao");
        return ERR_STATE;
    }
    file->active_operations = 1U;
    *file_out = file;
    spinlock_release(&vfs_lock);
    return OK;
}

static void vfs_end_operation(file_t* file) {
    if (!file) return;
    spinlock_acquire(&vfs_lock);
    file->active_operations = 0U;
    spinlock_release(&vfs_lock);
}

static int vfs_unsupported_open(vnode_t* vnode, file_t* file) {
    (void)vnode;
    (void)file;
    return ERR_UNAVAILABLE;
}

static int vfs_unsupported_read(file_t* file, void* buffer, uint32_t size,
                                uint32_t* bytes_read) {
    (void)file;
    (void)buffer;
    (void)size;
    if (bytes_read) *bytes_read = 0U;
    return ERR_UNAVAILABLE;
}

static int vfs_unsupported_write(file_t* file, const void* buffer,
                                 uint32_t size, uint32_t* bytes_written) {
    (void)file;
    (void)buffer;
    (void)size;
    if (bytes_written) *bytes_written = 0U;
    return ERR_UNAVAILABLE;
}

static int vfs_unsupported_close(file_t* file) {
    (void)file;
    return OK;
}

static int vfs_unsupported_lseek(file_t* file, int32_t offset,
                                 uint32_t whence, uint32_t* position) {
    (void)file;
    (void)offset;
    (void)whence;
    if (position) *position = 0U;
    return ERR_UNAVAILABLE;
}

static int vfs_unsupported_ioctl(file_t* file, uint32_t request,
                                 void* argument) {
    (void)file;
    (void)request;
    (void)argument;
    return ERR_UNAVAILABLE;
}

static int vfs_unsupported_sync(file_t* file) {
    (void)file;
    LOG_WARN("FS", "Sync nao suportado pelo objeto VFS");
    return ERR_UNAVAILABLE;
}

static int vfs_regular_open(vnode_t* vnode, file_t* file) {
    vfs_file_context_t* context;

    if (!vnode || !file) return ERR_NULL;
    context = (vfs_file_context_t*)vnode->private_data;
    if (!context || context->lookup.type != VFS_NODE_REGULAR) return ERR_STATE;
    vnode->size = context->lookup.size;
    file->offset = 0U;
    return OK;
}

static int vfs_regular_read(file_t* file, void* buffer, uint32_t size,
                            uint32_t* bytes_read) {
    vfs_file_context_t* context;
    int result;

    if (!file || !file->vnode || !bytes_read) return ERR_NULL;
    *bytes_read = 0U;
    if (!(file->mode & VFS_MODE_READ)) return ERR_UNAVAILABLE;
    context = (vfs_file_context_t*)file->vnode->private_data;
    if (!context || vfs_mount_validate_reference(
            context->lookup.mount_slot,
            context->lookup.mount_generation) != OK) return ERR_STATE;
    result = storage_read_file_range(context->lookup.volume_id,
                                     context->lookup.relative_path,
                                     file->offset, (uint8_t*)buffer,
                                     size, bytes_read);
    if (result != OK) return result;
    if (file->offset > 0xFFFFFFFFU - *bytes_read) return ERR_OVERFLOW;
    file->offset += *bytes_read;
    return OK;
}

static int vfs_regular_write(file_t* file, const void* buffer, uint32_t size,
                             uint32_t* bytes_written) {
    vfs_file_context_t* context;
    int result;

    if (!file || !file->vnode || !bytes_written) return ERR_NULL;
    *bytes_written = 0U;
    if (!(file->mode & VFS_MODE_WRITE)) return ERR_UNAVAILABLE;
    context = (vfs_file_context_t*)file->vnode->private_data;
    if (!context || vfs_mount_validate_reference(
            context->lookup.mount_slot,
            context->lookup.mount_generation) != OK) return ERR_STATE;
    if (context->lookup.fs_type != STORAGE_FS_FAT32 ||
        context->lookup.read_only) return ERR_UNAVAILABLE;
    result = storage_write_file(context->lookup.volume_id,
                                context->lookup.relative_path,
                                (const uint8_t*)buffer, size,
                                FS_ATTRIBUTE_ARCHIVE);
    if (result != OK) return result;
    file->vnode->size = size;
    file->offset = 0U;
    *bytes_written = size;
    return OK;
}

static int vfs_regular_close(file_t* file) {
    vfs_file_context_t* context;

    if (!file || !file->vnode) return ERR_NULL;
    context = (vfs_file_context_t*)file->vnode->private_data;
    if (context) {
        vfs_mount_release(context->lookup.mount_slot,
                          context->lookup.mount_generation);
    }
    return OK;
}

static int vfs_regular_sync(file_t* file) {
    vfs_file_context_t* context;

    if (!file || !file->vnode) {
        LOG_ERROR("FS", "Arquivo nulo no fsync VFS");
        return ERR_NULL;
    }
    context = (vfs_file_context_t*)file->vnode->private_data;
    if (!context || vfs_mount_validate_reference(
            context->lookup.mount_slot,
            context->lookup.mount_generation) != OK) {
        LOG_ERROR("FS", "Montagem invalida no fsync VFS");
        return ERR_STATE;
    }
    return storage_sync_volume(context->lookup.volume_id);
}

static int vfs_seek_base(file_t* file, uint32_t whence, uint32_t* base) {
    if (!file || !file->vnode || !base) return ERR_NULL;
    if (whence == VFS_SEEK_SET) *base = 0U;
    else if (whence == VFS_SEEK_CUR) *base = file->offset;
    else if (whence == VFS_SEEK_END) *base = file->vnode->size;
    else return ERR_INVALID;
    return OK;
}

static int vfs_regular_lseek(file_t* file, int32_t offset, uint32_t whence,
                             uint32_t* position) {
    uint32_t base;
    uint32_t target;
    int result;

    if (!file || !position) return ERR_NULL;
    *position = 0U;
    if (file->mode != VFS_MODE_READ) return ERR_UNAVAILABLE;
    result = vfs_seek_base(file, whence, &base);
    if (result != OK) return result;
    if (offset < 0) {
        uint32_t magnitude = (uint32_t)(-(offset + 1)) + 1U;
        if (magnitude > base) return ERR_INVALID;
        target = base - magnitude;
    } else {
        if (base > 0xFFFFFFFFU - (uint32_t)offset) return ERR_OVERFLOW;
        target = base + (uint32_t)offset;
    }
    if (target > file->vnode->size) return ERR_INVALID;
    file->offset = target;
    *position = target;
    return OK;
}

int vfs_stream_read(file_t* file, void* buffer, uint32_t size,
                    uint32_t* bytes_read) {
    uint8_t* output = (uint8_t*)buffer;
    wait_reason_t reason = WAIT_REASON_NONE;
    ipc_msg_t message;
    int result;

    (void)file;
    if (!bytes_read) {
        LOG_ERROR("FS", "Contador de leitura de fluxo nulo");
        return ERR_NULL;
    }
    *bytes_read = 0U;
    while (*bytes_read < size) {
        if (ipc_receive(&message)) {
            if (message.type == IPC_MSG_KEYBOARD) {
                output[*bytes_read] = (uint8_t)message.data1;
                (*bytes_read)++;
            }
            continue;
        }
        if (*bytes_read > 0U) return OK;
        asm volatile("sti" : : : "memory");
        result = ipc_wait(WAIT_TIMEOUT_INFINITE, &reason);
        if (result != OK) return result;
        if (reason == WAIT_REASON_SIGNAL) return OK;
        if (reason == WAIT_REASON_CANCELLED) return OK;
        if (reason != WAIT_REASON_EVENT) {
            LOG_ERROR("FS", "Motivo de despertar invalido no fluxo");
            return ERR_STATE;
        }
    }
    return OK;
}

int vfs_stream_write(file_t* file, const void* buffer, uint32_t size,
                     uint32_t* bytes_written) {
    int result;

    (void)file;
    if (!bytes_written) {
        LOG_ERROR("FS", "Contador de escrita de fluxo nulo");
        return ERR_NULL;
    }
    *bytes_written = 0U;
    result = app_api_console_write((const char*)buffer, size);
    if (result == OK) *bytes_written = size;
    return result;
}

int vfs_init(void) {
    LOG_INFO("FS", "Inicializando VFS");
    if (vfs_ready) {
        LOG_WARN("FS", "VFS ja estava inicializada");
        return OK;
    }
    spinlock_init(&vfs_lock);
    kmemset(vfs_file_pool, 0, sizeof(vfs_file_pool));
    kmemset(vfs_vnode_pool, 0, sizeof(vfs_vnode_pool));
    kmemset(vfs_file_contexts, 0, sizeof(vfs_file_contexts));
    kmemset(vfs_pipe_pool, 0, sizeof(vfs_pipe_pool));
    kmemset(vfs_stdio_nodes, 0, sizeof(vfs_stdio_nodes));
    kmemset(&vfs_metrics, 0, sizeof(vfs_metrics));
    vfs_file_cache = kmem_cache_create("vfs_file", sizeof(file_t), 8U);
    vfs_vnode_cache = kmem_cache_create("vfs_vnode", sizeof(vnode_t), 8U);
    if (!vfs_file_cache || !vfs_vnode_cache) {
        LOG_ERROR("FS", "Falha ao criar caches de objetos VFS");
        if (vfs_file_cache) kmem_cache_destroy(vfs_file_cache);
        vfs_file_cache = 0;
        if (vfs_vnode_cache) kmem_cache_destroy(vfs_vnode_cache);
        vfs_vnode_cache = 0;
        return ERR_MEM;
    }
    if (vfs_path_init() != OK) {
        LOG_ERROR("FS", "Falha ao inicializar caminhos VFS");
        kmem_cache_destroy(vfs_file_cache);
        kmem_cache_destroy(vfs_vnode_cache);
        vfs_file_cache = 0;
        vfs_vnode_cache = 0;
        return ERR_STATE;
    }
    if (devfs_init() != OK) {
        LOG_ERROR("FS", "Falha ao inicializar devfs");
        kmem_cache_destroy(vfs_file_cache);
        kmem_cache_destroy(vfs_vnode_cache);
        vfs_file_cache = 0;
        vfs_vnode_cache = 0;
        return ERR_STATE;
    }
    vfs_ready = 1;
    vfs_metrics.initialized = 1U;
    vfs_metrics.descriptor_capacity = VFS_MAX_FDS;
    vfs_metrics.global_file_capacity = VFS_MAX_OPEN_FILES;
    vfs_metrics.pipe_capacity = VFS_MAX_PIPES;
    LOG_INFO("FS", "VFS inicializada com sucesso");
    return OK;
}

int vfs_is_ready(void) {
    return vfs_ready;
}

int vfs_fd_table_init(vfs_fd_table_t* table) {
    uint32_t index;
    file_t* standard[3] = {0, 0, 0};

    if (!table) {
        LOG_ERROR("FS", "Tabela de descritores nula na inicializacao");
        return ERR_NULL;
    }
    if (table->initialized) {
        LOG_ERROR("FS", "Tabela de descritores ja inicializada");
        return ERR_STATE;
    }
    kmemset(table, 0, sizeof(vfs_fd_table_t));
    if (!vfs_ready) {
        LOG_ERROR("FS", "Tabela de descritores criada antes da VFS");
        return ERR_UNAVAILABLE;
    }
    if (vfs_initialize_stdio_nodes() != OK) return ERR_MEM;
    for (index = 0U; index < 3U; index++) {
        standard[index] = (file_t*)kmem_cache_alloc(vfs_file_cache);
        if (!standard[index]) {
            while (index > 0U) {
                index--;
                kmem_cache_free(vfs_file_cache, standard[index]);
            }
            LOG_ERROR("FS", "Falha ao alocar descritor padrao VFS");
            return ERR_MEM;
        }
    }
    spinlock_acquire(&vfs_lock);
    for (index = 0U; index < 3U; index++) {
        standard[index]->used = 1U;
        standard[index]->persistent = 1U;
        standard[index]->slot = VFS_MAX_OPEN_FILES + index;
        standard[index]->vnode = vfs_stdio_nodes[index];
        standard[index]->mode = index == VFS_FD_STDIN ?
            VFS_MODE_READ : VFS_MODE_WRITE;
        table->standard_files[index] = standard[index];
        table->entries[index] = standard[index];
    }
    table->initialized = 1U;
    vfs_copy_text(table->cwd, VFS_MAX_PATH, "/");
    spinlock_release(&vfs_lock);
    return OK;
}

int vfs_fd_table_release(vfs_fd_table_t* table) {
    uint32_t index;
    file_t* standard[3];

    if (!table) {
        LOG_ERROR("FS", "Tabela de descritores nula na liberacao");
        return ERR_NULL;
    }
    if (!table->initialized) return OK;
    for (index = 0U; index < 3U; index++) standard[index] = 0;
    spinlock_acquire(&vfs_lock);
    for (index = 0U; index < VFS_FD_FIRST_FILE; index++) {
        if (table->standard_files[index] &&
            table->standard_files[index]->active_operations != 0U) {
            spinlock_release(&vfs_lock);
            LOG_ERROR("FS", "Descritor padrao ativo durante liberacao");
            return ERR_STATE;
        }
    }
    for (index = 0U; index < 3U; index++) {
        standard[index] = table->standard_files[index];
    }
    spinlock_release(&vfs_lock);
    for (index = VFS_FD_FIRST_FILE; index < VFS_MAX_FDS; index++) {
        file_t* file;

        spinlock_acquire(&vfs_lock);
        file = table->entries[index];
        if (!file) {
            spinlock_release(&vfs_lock);
            continue;
        }
        if (file->active_operations != 0U) {
            spinlock_release(&vfs_lock);
            LOG_ERROR("FS", "Descritor ativo durante liberacao do processo");
            return ERR_STATE;
        }
        table->entries[index] = 0;
        spinlock_release(&vfs_lock);
        if (file->vnode && file->vnode->operations &&
            file->vnode->operations->close) {
            file->vnode->operations->close(file);
        }
        vfs_release_file(file);
    }
    spinlock_acquire(&vfs_lock);
    for (index = 0U; index < 3U; index++) {
        table->standard_files[index] = 0;
        table->entries[index] = 0;
    }
    kmemset(table->cwd, 0, sizeof(table->cwd));
    table->initialized = 0U;
    spinlock_release(&vfs_lock);
    for (index = 0U; index < 3U; index++) {
        if (standard[index]) kmem_cache_free(vfs_file_cache, standard[index]);
    }
    return OK;
}

int vfs_close_owner(uint32_t pid) {
    process_t* process;

    if (pid == 0U) {
        LOG_ERROR("FS", "PID invalido ao liberar descritores VFS");
        return ERR_INVALID;
    }
    process = process_get_by_pid(pid);
    if (!process) {
        LOG_ERROR("FS", "Processo nao encontrado ao liberar descritores VFS");
        return ERR_NOT_FOUND;
    }
    return vfs_fd_table_release(&process->fd_table);
}

int vfs_open(const char* path, uint32_t mode, int32_t* fd_out) {
    vfs_fd_table_t* table;
    file_t* file = 0;
    vnode_t* vnode = 0;
    vfs_file_context_t* context;
    uint32_t length;
    int fd;
    int result;

    if (!vfs_ready) {
        LOG_ERROR("FS", "Abertura solicitada com VFS indisponivel");
        return ERR_UNAVAILABLE;
    }
    if (!path || !fd_out) {
        LOG_ERROR("FS", "Argumento nulo na abertura VFS");
        return ERR_NULL;
    }
    *fd_out = VFS_FD_INVALID;
    length = kstrlen(path);
    if (length == 0U) {
        LOG_ERROR("FS", "Caminho vazio na abertura VFS");
        return ERR_INVALID;
    }
    if (length >= VFS_MAX_PATH) {
        LOG_ERROR("FS", "Caminho excede o limite da VFS");
        return ERR_OVERFLOW;
    }
    if (!vfs_mode_valid(mode)) {
        LOG_ERROR("FS", "Modo invalido na abertura VFS");
        return ERR_INVALID;
    }
    result = vfs_get_current_table(&table);
    if (result != OK) return result;
    fd = vfs_find_free_fd(table);
    if (fd == VFS_FD_INVALID) {
        LOG_WARN("FS", "Tabela de descritores do processo cheia");
        return ERR_UNAVAILABLE;
    }
    result = vfs_allocate_file(&file, &vnode, VFS_FILE_RESERVED);
    if (result != OK) return result;
    context = &vfs_file_contexts[file->slot];
    result = vfs_resolve_open_path(path, mode, &context->lookup);
    if (result != OK) {
        vfs_release_file(file);
        spinlock_acquire(&vfs_lock);
        vfs_metrics.failures++;
        spinlock_release(&vfs_lock);
        LOG_ERROR("FS", "Falha ao resolver caminho na abertura VFS");
        return result;
    }
    if (context->lookup.type == VFS_NODE_DIRECTORY) {
        vfs_release_file(file);
        LOG_ERROR("FS", "Diretorio nao pode ser aberto como arquivo");
        return ERR_INVALID;
    }
    result = vfs_mount_acquire(context->lookup.mount_slot,
                               context->lookup.mount_generation);
    if (result != OK) {
        vfs_release_file(file);
        LOG_ERROR("FS", "Montagem mudou durante abertura VFS");
        return result;
    }
    if (context->lookup.type == VFS_NODE_REGULAR) {
        vnode->type = VFS_NODE_REGULAR;
        vnode->operations = &vfs_regular_operations;
        vfs_copy_text(vnode->path, VFS_MAX_PATH,
                      context->lookup.canonical_path);
        vnode->private_data = context;
        file->mode = mode;
        result = vnode->operations->open(vnode, file);
    } else if (context->lookup.type == VFS_NODE_CHAR_DEVICE ||
               context->lookup.type == VFS_NODE_BLOCK_DEVICE) {
        result = devfs_open_file(&context->lookup, mode, vnode, file,
                                 &context->device);
    } else {
        result = ERR_INVALID;
    }
    if (result != OK) {
        vfs_mount_release(context->lookup.mount_slot,
                          context->lookup.mount_generation);
        vfs_release_file(file);
        spinlock_acquire(&vfs_lock);
        vfs_metrics.failures++;
        spinlock_release(&vfs_lock);
        LOG_ERROR("FS", "Falha ao abrir arquivo pela VFS");
        return result;
    }
    spinlock_acquire(&vfs_lock);
    if (table->entries[fd]) {
        spinlock_release(&vfs_lock);
        (void)vnode->operations->close(file);
        vfs_release_file(file);
        LOG_ERROR("FS", "Descritor VFS deixou de estar disponivel");
        return ERR_STATE;
    }
    file->used = VFS_FILE_ACTIVE;
    table->entries[fd] = file;
    vfs_metrics.opens++;
    spinlock_release(&vfs_lock);
    *fd_out = fd;
    return OK;
}

int vfs_pipe(int32_t fds[2]) {
    vfs_fd_table_t* table;
    pipe_t* pipe = 0;
    file_t* read_file = 0;
    file_t* write_file = 0;
    vnode_t* read_vnode = 0;
    vnode_t* write_vnode = 0;
    vfs_file_context_t* read_context;
    vfs_file_context_t* write_context;
    int read_fd;
    int write_fd;
    int result;

    if (!fds) {
        LOG_ERROR("FS", "Destino nulo na criacao de pipe VFS");
        return ERR_NULL;
    }
    fds[0] = VFS_FD_INVALID;
    fds[1] = VFS_FD_INVALID;
    if (!vfs_ready) {
        LOG_ERROR("FS", "Pipe solicitado com VFS indisponivel");
        return ERR_UNAVAILABLE;
    }
    result = vfs_get_current_table(&table);
    if (result != OK) return result;
    result = vfs_find_free_fd_pair(table, &read_fd, &write_fd);
    if (result != OK) {
        LOG_WARN("FS", "Tabela sem dois descritores para pipe");
        return result;
    }
    result = vfs_pipe_allocate(&pipe);
    if (result != OK) return result;
    result = vfs_allocate_file(&read_file, &read_vnode, VFS_FILE_RESERVED);
    if (result == OK) {
        result = vfs_allocate_file(&write_file, &write_vnode,
                                   VFS_FILE_RESERVED);
    }
    if (result != OK) {
        if (read_file) vfs_release_file(read_file);
        if (write_file) vfs_release_file(write_file);
        vfs_pipe_release(pipe);
        LOG_ERROR("FS", "Pool sem arquivos para endpoints de pipe");
        return result;
    }

    read_context = &vfs_file_contexts[read_file->slot];
    write_context = &vfs_file_contexts[write_file->slot];
    read_context->pipe = pipe;
    read_context->pipe_reader = 1U;
    write_context->pipe = pipe;
    write_context->pipe_writer = 1U;

    read_vnode->type = VFS_NODE_PIPE;
    read_vnode->operations = &vfs_pipe_operations;
    read_vnode->private_data = read_context;
    vfs_copy_text(read_vnode->path, VFS_MAX_PATH, "pipe:read");
    write_vnode->type = VFS_NODE_PIPE;
    write_vnode->operations = &vfs_pipe_operations;
    write_vnode->private_data = write_context;
    vfs_copy_text(write_vnode->path, VFS_MAX_PATH, "pipe:write");
    read_file->mode = VFS_MODE_READ;
    write_file->mode = VFS_MODE_WRITE;
    if (read_vnode->operations->open(read_vnode, read_file) != OK ||
        write_vnode->operations->open(write_vnode, write_file) != OK) {
        vfs_release_file(read_file);
        vfs_release_file(write_file);
        vfs_pipe_release(pipe);
        LOG_ERROR("FS", "Falha ao abrir endpoints de pipe");
        return ERR_STATE;
    }

    spinlock_acquire(&pipe->lock);
    pipe->readers = 1U;
    pipe->writers = 1U;
    spinlock_release(&pipe->lock);
    spinlock_acquire(&vfs_lock);
    if (table->entries[read_fd] || table->entries[write_fd]) {
        spinlock_release(&vfs_lock);
        vfs_pipe_close(read_file);
        vfs_pipe_close(write_file);
        vfs_release_file(read_file);
        vfs_release_file(write_file);
        LOG_ERROR("FS", "Descritores escolhidos para pipe foram ocupados");
        return ERR_STATE;
    }
    read_file->used = VFS_FILE_ACTIVE;
    write_file->used = VFS_FILE_ACTIVE;
    table->entries[read_fd] = read_file;
    table->entries[write_fd] = write_file;
    vfs_metrics.opens += 2U;
    spinlock_release(&vfs_lock);
    fds[0] = read_fd;
    fds[1] = write_fd;
    return OK;
}

int vfs_write_redirect(const char* path, const uint8_t* data, uint32_t size,
                       uint8_t append) {
    vfs_lookup_result_t lookup;
    uint8_t* file_data = 0;
    uint32_t existing_size = 0U;
    uint32_t total_size = size;
    uint32_t offset = 0U;
    uint32_t chunk_size;
    uint32_t read_size;
    uint8_t attributes = FS_ATTRIBUTE_ARCHIVE;
    uint8_t is_directory = 0U;
    uint8_t mount_acquired = 0U;
    int result;

    if (!vfs_ready) {
        LOG_ERROR("FS", "Redirecionamento solicitado com VFS indisponivel");
        return ERR_UNAVAILABLE;
    }
    if (!path || (size > 0U && !data)) {
        LOG_ERROR("FS", "Argumento nulo no redirecionamento VFS");
        return ERR_NULL;
    }
    if (size > VFS_REDIRECT_MAX_SIZE) {
        LOG_ERROR("FS", "Saida redirecionada excede o limite");
        return ERR_OVERFLOW;
    }
    if (append > 1U) {
        LOG_ERROR("FS", "Modo de redirecionamento VFS invalido");
        return ERR_INVALID;
    }
    result = vfs_resolve_open_path(path, VFS_MODE_WRITE, &lookup);
    if (result != OK) {
        LOG_ERROR("FS", "Destino invalido no redirecionamento VFS");
        return result;
    }
    if (lookup.type == VFS_NODE_DIRECTORY) {
        LOG_ERROR("FS", "Destino e diretorio no redirecionamento VFS");
        return ERR_INVALID;
    }
    if (lookup.type != VFS_NODE_REGULAR ||
        lookup.mount_kind != VFS_MOUNT_STORAGE ||
        lookup.fs_type != STORAGE_FS_FAT32 || lookup.read_only) {
        LOG_ERROR("FS", "Destino nao gravavel no redirecionamento VFS");
        return ERR_UNAVAILABLE;
    }
    result = vfs_mount_acquire(lookup.mount_slot, lookup.mount_generation);
    if (result != OK) {
        LOG_ERROR("FS", "Montagem indisponivel no redirecionamento VFS");
        return result;
    }
    mount_acquired = 1U;
    if (append) {
        result = storage_get_path_info(lookup.volume_id,
                                       lookup.relative_path,
                                       &existing_size, &attributes,
                                       &is_directory);
        if (result == ERR_NOT_FOUND) existing_size = 0U;
        else if (result != OK) goto cleanup;
        if (is_directory || existing_size > VFS_REDIRECT_MAX_SIZE - size) {
            result = is_directory ? ERR_INVALID : ERR_OVERFLOW;
            goto cleanup;
        }
        total_size = existing_size + size;
    }
    if (total_size > 0U) {
        file_data = (uint8_t*)kmalloc(total_size);
        if (!file_data) {
            result = ERR_MEM;
            goto cleanup;
        }
    }
    if (append && existing_size > 0U) {
        while (offset < existing_size) {
            chunk_size = existing_size - offset;
            if (chunk_size > APP_API_MAX_FILE_IO_SIZE) {
                chunk_size = APP_API_MAX_FILE_IO_SIZE;
            }
            read_size = 0U;
            result = storage_read_file_range(lookup.volume_id,
                                             lookup.relative_path, offset,
                                             file_data + offset, chunk_size,
                                             &read_size);
            if (result != OK || read_size != chunk_size) {
                if (result == OK) result = ERR_DISK;
                goto cleanup;
            }
            offset += read_size;
        }
    }
    if (size > 0U) kmemcpy(file_data + existing_size, data, size);
    result = storage_atomic_write_file(lookup.volume_id,
                                       lookup.relative_path, file_data,
                                       total_size, attributes,
                                       STORAGE_ATOMIC_CREATE_OR_REPLACE);
    if (result == OK) {
        spinlock_acquire(&vfs_lock);
        vfs_metrics.writes++;
        spinlock_release(&vfs_lock);
    }

cleanup:
    if (file_data) {
        kfree(file_data);
        file_data = 0;
    }
    if (mount_acquired) {
        vfs_mount_release(lookup.mount_slot, lookup.mount_generation);
    }
    if (result != OK) LOG_ERROR("FS", "Falha no redirecionamento VFS");
    return result;
}

int vfs_read(int32_t fd, void* buffer, uint32_t size,
             uint32_t* bytes_read) {
    file_t* file;
    int result;

    if (!bytes_read) {
        LOG_ERROR("FS", "Contador de leitura VFS nulo");
        return ERR_NULL;
    }
    *bytes_read = 0U;
    if (size > APP_API_MAX_FILE_IO_SIZE) {
        LOG_ERROR("FS", "Leitura VFS excede o limite");
        return ERR_OVERFLOW;
    }
    if (size > 0U && !buffer) {
        LOG_ERROR("FS", "Buffer de leitura VFS nulo");
        return ERR_NULL;
    }
    result = vfs_begin_operation(fd, &file);
    if (result != OK) return result;
    if (!(file->mode & VFS_MODE_READ)) result = ERR_UNAVAILABLE;
    else result = file->vnode->operations->read(file, buffer, size, bytes_read);
    vfs_end_operation(file);
    spinlock_acquire(&vfs_lock);
    if (result == OK) {
        vfs_metrics.reads++;
        if (file->vnode->type == VFS_NODE_PIPE) vfs_metrics.pipe_reads++;
    }
    else vfs_metrics.failures++;
    spinlock_release(&vfs_lock);
    if (result != OK) LOG_ERROR("FS", "Falha em leitura VFS");
    return result;
}

int vfs_write(int32_t fd, const void* buffer, uint32_t size,
              uint32_t* bytes_written) {
    file_t* file;
    int result;

    if (!bytes_written) {
        LOG_ERROR("FS", "Contador de escrita VFS nulo");
        return ERR_NULL;
    }
    *bytes_written = 0U;
    if (size > APP_API_MAX_FILE_IO_SIZE) {
        LOG_ERROR("FS", "Escrita VFS excede o limite");
        return ERR_OVERFLOW;
    }
    if (size > 0U && !buffer) {
        LOG_ERROR("FS", "Buffer de escrita VFS nulo");
        return ERR_NULL;
    }
    result = vfs_begin_operation(fd, &file);
    if (result != OK) return result;
    if (!(file->mode & VFS_MODE_WRITE)) result = ERR_UNAVAILABLE;
    else result = file->vnode->operations->write(file, buffer, size,
                                                  bytes_written);
    vfs_end_operation(file);
    spinlock_acquire(&vfs_lock);
    if (result == OK) {
        vfs_metrics.writes++;
        if (file->vnode->type == VFS_NODE_PIPE) vfs_metrics.pipe_writes++;
    }
    else vfs_metrics.failures++;
    spinlock_release(&vfs_lock);
    if (result != OK) LOG_ERROR("FS", "Falha em escrita VFS");
    return result;
}

int vfs_close(int32_t fd) {
    vfs_fd_table_t* table;
    file_t* file;
    int result;

    if (fd < 0 || (uint32_t)fd >= VFS_MAX_FDS) {
        LOG_ERROR("FS", "Descritor invalido no fechamento VFS");
        return ERR_INVALID;
    }
    if (fd < VFS_FD_FIRST_FILE) {
        LOG_ERROR("FS", "Descritor padrao reservado nao pode ser fechado");
        return ERR_UNAVAILABLE;
    }
    result = vfs_get_current_table(&table);
    if (result != OK) return result;
    spinlock_acquire(&vfs_lock);
    file = table->entries[fd];
    if (!file || !file->used || file->active_operations != 0U) {
        vfs_metrics.failures++;
        spinlock_release(&vfs_lock);
        LOG_ERROR("FS", "Descritor fechado, invalido ou ativo");
        return file && file->active_operations ? ERR_STATE : ERR_INVALID;
    }
    table->entries[fd] = 0;
    spinlock_release(&vfs_lock);
    result = file->vnode->operations->close(file);
    if (!file->persistent) vfs_release_file(file);
    spinlock_acquire(&vfs_lock);
    if (result == OK) vfs_metrics.closes++;
    else vfs_metrics.failures++;
    spinlock_release(&vfs_lock);
    if (result != OK) LOG_ERROR("FS", "Falha em fechamento VFS");
    return result;
}

int vfs_fsync(int32_t fd) {
    file_t* file;
    int result;

    result = vfs_begin_operation(fd, &file);
    if (result != OK) return result;
    if (!file->vnode->operations->sync) {
        result = ERR_UNAVAILABLE;
    } else {
        result = file->vnode->operations->sync(file);
    }
    vfs_end_operation(file);
    spinlock_acquire(&vfs_lock);
    if (result != OK) vfs_metrics.failures++;
    spinlock_release(&vfs_lock);
    if (result != OK) LOG_ERROR("FS", "Fsync VFS falhou");
    return result;
}

int vfs_sync(void) {
    int result;

    if (!vfs_ready) {
        LOG_ERROR("FS", "Sync global solicitado com VFS indisponivel");
        return ERR_UNAVAILABLE;
    }
    result = storage_sync_all();
    if (result != OK) LOG_ERROR("FS", "Sync global VFS falhou");
    return result;
}

int vfs_lseek(int32_t fd, int32_t offset, uint32_t whence,
              uint32_t* position) {
    file_t* file;
    int result;

    if (!position) {
        LOG_ERROR("FS", "Posicao de lseek VFS nula");
        return ERR_NULL;
    }
    *position = 0U;
    result = vfs_begin_operation(fd, &file);
    if (result != OK) return result;
    result = file->vnode->operations->lseek(file, offset, whence, position);
    vfs_end_operation(file);
    spinlock_acquire(&vfs_lock);
    if (result == OK) vfs_metrics.seeks++;
    else vfs_metrics.failures++;
    spinlock_release(&vfs_lock);
    if (result != OK) LOG_ERROR("FS", "Falha em lseek VFS");
    return result;
}

int vfs_ioctl(int32_t fd, uint32_t request, void* argument) {
    file_t* file;
    int result = vfs_begin_operation(fd, &file);

    if (result != OK) return result;
    result = file->vnode->operations->ioctl(file, request, argument);
    vfs_end_operation(file);
    spinlock_acquire(&vfs_lock);
    if (result == OK) {
        vfs_metrics.ioctls++;
    } else {
        vfs_metrics.failures++;
    }
    spinlock_release(&vfs_lock);
    if (result != OK) LOG_ERROR("FS", "Falha em ioctl VFS");
    return result;
}

int vfs_get_status(vfs_status_t* status) {
    uint32_t process_index;
    uint32_t fd;

    if (!status) {
        LOG_ERROR("FS", "Destino de status VFS nulo");
        return ERR_NULL;
    }
    if (!vfs_ready) {
        LOG_ERROR("FS", "Status solicitado com VFS indisponivel");
        return ERR_UNAVAILABLE;
    }
    spinlock_acquire(&vfs_lock);
    *status = vfs_metrics;
    status->pipes_active = 0U;
    for (fd = 0U; fd < VFS_MAX_PIPES; fd++) {
        if (vfs_pipe_pool[fd].used) status->pipes_active++;
    }
    for (fd = 0U; fd < VFS_MAX_OPEN_FILES; fd++) {
        if (vfs_file_pool[fd]) status->global_files_used++;
    }
    for (process_index = 0U; process_index < MAX_PROCESSES; process_index++) {
        if (!processes[process_index] ||
            !processes[process_index]->fd_table.initialized) continue;
        status->processes_with_tables++;
        for (fd = 0U; fd < VFS_MAX_FDS; fd++) {
            if (processes[process_index]->fd_table.entries[fd]) {
                status->descriptors_open++;
            }
        }
    }
    spinlock_release(&vfs_lock);
    vfs_path_get_metrics(&status->mount_capacity, &status->mounts_active,
                         &status->lookups, &status->chdirs);
    {
        devfs_status_t devfs_status;
        if (devfs_get_status(&devfs_status) == OK) {
            status->device_capacity = devfs_status.capacity;
            status->devices_active = devfs_status.active;
        }
    }
    return OK;
}

int vfs_copy_descriptors(vfs_descriptor_info_t* output,
                         uint32_t capacity, uint32_t* out_count) {
    process_t* process;
    uint32_t fd;
    uint32_t count = 0U;

    if (!out_count || (capacity > 0U && !output)) {
        LOG_ERROR("FS", "Destino de snapshot VFS nulo");
        return ERR_NULL;
    }
    if (!vfs_ready) {
        LOG_ERROR("FS", "Snapshot solicitado com VFS indisponivel");
        return ERR_UNAVAILABLE;
    }
    process = process_get_current();
    if (!process || !process->fd_table.initialized) return ERR_STATE;
    spinlock_acquire(&vfs_lock);
    for (fd = 0U; fd < VFS_MAX_FDS; fd++) {
        file_t* file = process->fd_table.entries[fd];
        if (!file) continue;
        if (count >= capacity) {
            spinlock_release(&vfs_lock);
            *out_count = count;
            return ERR_OVERFLOW;
        }
        output[count].pid = process->pid;
        output[count].fd = (int32_t)fd;
        output[count].type = file->vnode->type;
        output[count].mode = file->mode;
        output[count].offset = file->offset;
        output[count].size = file->vnode->size;
        vfs_copy_text(output[count].path, VFS_MAX_PATH,
                      file->vnode->path);
        count++;
    }
    spinlock_release(&vfs_lock);
    *out_count = count;
    return OK;
}

int vfs_open_socket(void* private_data, const file_operations_t* operations,
                    uint32_t mode, const char* path, int32_t* fd_out) {
    vfs_fd_table_t* table;
    file_t* file = 0;
    vnode_t* vnode = 0;
    uint32_t length;
    int fd;
    int result;

    if (!vfs_ready) {
        LOG_ERROR("FS", "Socket solicitado com VFS indisponivel");
        return ERR_UNAVAILABLE;
    }
    if (!private_data || !operations || !path || !fd_out) {
        LOG_ERROR("FS", "Argumento nulo na abertura de socket VFS");
        return ERR_NULL;
    }
    *fd_out = VFS_FD_INVALID;
    length = kstrlen(path);
    if (!length) {
        LOG_ERROR("FS", "Nome vazio na abertura de socket VFS");
        return ERR_INVALID;
    }
    if (length >= VFS_MAX_PATH) {
        LOG_ERROR("FS", "Nome de socket excede o limite da VFS");
        return ERR_OVERFLOW;
    }
    if (!vfs_mode_valid(mode)) {
        LOG_ERROR("FS", "Modo invalido na abertura de socket VFS");
        return ERR_INVALID;
    }
    result = vfs_get_current_table(&table);
    if (result != OK) return result;
    fd = vfs_find_free_fd(table);
    if (fd == VFS_FD_INVALID) {
        LOG_WARN("FS", "Tabela de descritores cheia para socket VFS");
        return ERR_UNAVAILABLE;
    }
    result = vfs_allocate_file(&file, &vnode, VFS_FILE_RESERVED);
    if (result != OK) return result;
    vnode->type = VFS_NODE_SOCKET;
    vnode->operations = operations;
    vnode->private_data = private_data;
    vfs_copy_text(vnode->path, VFS_MAX_PATH, path);
    file->mode = mode;
    result = operations->open ? operations->open(vnode, file) : OK;
    if (result != OK) {
        vfs_release_file(file);
        LOG_ERROR("FS", "Falha ao abrir socket pela VFS");
        return result;
    }
    spinlock_acquire(&vfs_lock);
    if (table->entries[fd]) {
        spinlock_release(&vfs_lock);
        if (operations->close) operations->close(file);
        vfs_release_file(file);
        LOG_ERROR("FS", "Descritor escolhido para socket foi ocupado");
        return ERR_STATE;
    }
    file->used = VFS_FILE_ACTIVE;
    table->entries[fd] = file;
    vfs_metrics.opens++;
    spinlock_release(&vfs_lock);
    *fd_out = fd;
    return OK;
}

int vfs_validate_state(void) {
    uint32_t process_index;
    uint32_t fd;

    if (!vfs_ready) {
        LOG_ERROR("FS", "Validacao VFS solicitada antes da inicializacao");
        return ERR_UNAVAILABLE;
    }
    if (kmem_cache_validate() != OK || vfs_path_validate_state() != OK) {
        LOG_ERROR("FS", "Invariantes dos objetos VFS invalidas");
        return ERR_STATE;
    }
    spinlock_acquire(&vfs_lock);
    for (process_index = 0U; process_index < MAX_PROCESSES; process_index++) {
        process_t* process = processes[process_index];
        if (!process || !process->fd_table.initialized) continue;
        for (fd = 0U; fd < VFS_FD_FIRST_FILE; fd++) {
            if (process->fd_table.entries[fd] !=
                    process->fd_table.standard_files[fd] ||
                !process->fd_table.standard_files[fd] ||
                !process->fd_table.standard_files[fd]->used ||
                !process->fd_table.standard_files[fd]->persistent ||
                process->fd_table.standard_files[fd]->vnode !=
                    vfs_stdio_nodes[fd] ||
                !kmem_cache_owns(vfs_file_cache,
                                 process->fd_table.standard_files[fd]) ||
                !kmem_cache_owns(vfs_vnode_cache, vfs_stdio_nodes[fd])) {
                spinlock_release(&vfs_lock);
                return ERR_STATE;
            }
        }
        for (fd = 0U; fd < VFS_MAX_FDS; fd++) {
            file_t* file = process->fd_table.entries[fd];
            uint32_t pool_index;

            if (!file) continue;
            if (!file->used || !file->vnode || !file->vnode->operations ||
                (fd >= VFS_FD_FIRST_FILE && file->persistent)) {
                spinlock_release(&vfs_lock);
                return ERR_STATE;
            }
            if (fd < VFS_FD_FIRST_FILE) continue;
            pool_index = file->slot;
            if (pool_index >= VFS_MAX_OPEN_FILES ||
                vfs_file_pool[pool_index] != file ||
                vfs_vnode_pool[pool_index] != file->vnode ||
                !kmem_cache_owns(vfs_file_cache, file) ||
                !kmem_cache_owns(vfs_vnode_cache, file->vnode)) {
                spinlock_release(&vfs_lock);
                return ERR_STATE;
            }
            if (file->vnode->type == VFS_NODE_REGULAR &&
                file->vnode->private_data !=
                    &vfs_file_contexts[pool_index]) {
                spinlock_release(&vfs_lock);
                return ERR_STATE;
            }
            if ((file->vnode->type == VFS_NODE_CHAR_DEVICE ||
                 file->vnode->type == VFS_NODE_BLOCK_DEVICE) &&
                file->vnode->private_data !=
                    &vfs_file_contexts[pool_index].device) {
                spinlock_release(&vfs_lock);
                return ERR_STATE;
            }
            if (file->vnode->type == VFS_NODE_PIPE) {
                vfs_file_context_t* context =
                    (vfs_file_context_t*)file->vnode->private_data;

                if (context != &vfs_file_contexts[pool_index] ||
                    !context->pipe || !context->pipe->used ||
                    (context->pipe_reader && context->pipe_writer) ||
                    (!context->pipe_reader && !context->pipe_writer) ||
                    (context->pipe_reader && file->mode != VFS_MODE_READ) ||
                    (context->pipe_writer && file->mode != VFS_MODE_WRITE)) {
                    spinlock_release(&vfs_lock);
                    return ERR_STATE;
                }
            }
            if (file->vnode->type == VFS_NODE_SOCKET &&
                (!file->vnode->private_data ||
                 !file->vnode->operations->open ||
                 !file->vnode->operations->read ||
                 !file->vnode->operations->write ||
                 !file->vnode->operations->close ||
                 !file->vnode->operations->lseek ||
                 !file->vnode->operations->sync)) {
                spinlock_release(&vfs_lock);
                return ERR_STATE;
            }
        }
    }
    for (fd = 0U; fd < VFS_MAX_OPEN_FILES; fd++) {
        file_t* file = vfs_file_pool[fd];
        uint32_t references = 0U;

        if (!file) {
            if (vfs_vnode_pool[fd]) {
                spinlock_release(&vfs_lock);
                return ERR_STATE;
            }
            continue;
        }
        if (!file->used || file->slot != fd || !file->vnode ||
            file->vnode != vfs_vnode_pool[fd]) {
            spinlock_release(&vfs_lock);
            return ERR_STATE;
        }
        for (process_index = 0U; process_index < MAX_PROCESSES;
             process_index++) {
            process_t* process = processes[process_index];
            uint32_t descriptor;

            if (!process || !process->fd_table.initialized) continue;
            for (descriptor = VFS_FD_FIRST_FILE;
                 descriptor < VFS_MAX_FDS; descriptor++) {
                if (process->fd_table.entries[descriptor] == file) {
                    references++;
                }
            }
        }
        if ((file->used == VFS_FILE_ACTIVE && references != 1U) ||
            (file->used == VFS_FILE_RESERVED && references != 0U) ||
            (!file->used && references != 0U) ||
            (file->used != VFS_FILE_ACTIVE &&
             file->used != VFS_FILE_RESERVED && file->used != 0U)) {
            spinlock_release(&vfs_lock);
            return ERR_STATE;
        }
    }
    for (fd = 0U; fd < VFS_MAX_PIPES; fd++) {
        pipe_t* pipe = &vfs_pipe_pool[fd];
        uint32_t readers = 0U;
        uint32_t writers = 0U;

        if (!pipe->used) continue;
        for (process_index = 0U; process_index < MAX_PROCESSES;
             process_index++) {
            process_t* process = processes[process_index];
            uint32_t descriptor;

            if (!process || !process->fd_table.initialized) continue;
            for (descriptor = VFS_FD_FIRST_FILE;
                 descriptor < VFS_MAX_FDS; descriptor++) {
                file_t* file = process->fd_table.entries[descriptor];
                vfs_file_context_t* context;

                if (!file || !file->vnode ||
                    file->vnode->type != VFS_NODE_PIPE) continue;
                context = (vfs_file_context_t*)file->vnode->private_data;
                if (!context || context->pipe != pipe) continue;
                if (context->pipe_reader) readers++;
                if (context->pipe_writer) writers++;
            }
        }
        if (pipe->readers != readers || pipe->writers != writers ||
            !pipe->read_channel.initialized ||
            !pipe->write_channel.initialized ||
            pipe->bytes > VFS_PIPE_BUFFER_SIZE) {
            spinlock_release(&vfs_lock);
            return ERR_STATE;
        }
    }
    spinlock_release(&vfs_lock);
    return devfs_validate_state();
}

static int vfs_test_read(file_t* file, void* buffer, uint32_t size,
                         uint32_t* bytes_read) {
    vfs_test_context_t* context;
    uint32_t available;

    if (!file || !file->vnode || !buffer || !bytes_read) return ERR_NULL;
    if (!(file->mode & VFS_MODE_READ)) return ERR_UNAVAILABLE;
    context = (vfs_test_context_t*)file->vnode->private_data;
    if (!context || file->offset > context->size) return ERR_STATE;
    available = context->size - file->offset;
    if (size > available) size = available;
    if (size) kmemcpy(buffer, context->data + file->offset, size);
    file->offset += size;
    *bytes_read = size;
    return OK;
}

static int vfs_test_write(file_t* file, const void* buffer, uint32_t size,
                          uint32_t* bytes_written) {
    if (!file || !bytes_written || (size > 0U && !buffer)) return ERR_NULL;
    *bytes_written = 0U;
    if (!(file->mode & VFS_MODE_WRITE)) return ERR_UNAVAILABLE;
    *bytes_written = size;
    return OK;
}

static const file_operations_t vfs_test_operations = {
    vfs_unsupported_open, vfs_test_read, vfs_test_write,
    vfs_unsupported_close, vfs_regular_lseek, vfs_unsupported_ioctl,
    vfs_unsupported_sync
};

static void vfs_test_count(vfs_test_result_t* result, uint8_t passed) {
    result->total++;
    if (passed) result->passed++;
}

static int vfs_test_dir_has(const char* name) {
    for (uint32_t index = 0U; index < VFS_MAX_DIR_ENTRIES; index++) {
        uint32_t offset = 0U;

        while (vfs_test_dir_entries[index].name[offset] && name[offset]) {
            char actual = vfs_test_dir_entries[index].name[offset];
            char expected = name[offset];

            if (actual >= 'a' && actual <= 'z') actual -= (char)('a' - 'A');
            if (expected >= 'a' && expected <= 'z') {
                expected -= (char)('a' - 'A');
            }
            if (actual != expected) break;
            offset++;
        }
        if (vfs_test_dir_entries[index].name[offset] == name[offset]) return 1;
    }
    return 0;
}

int vfs_self_test(vfs_test_result_t* result) {
    static const uint8_t test_data[VFS_TEST_DATA_SIZE] =
        {'Z', 'E', 'P', 'H', 'Y', 'R', 'O', 'S'};
    vfs_test_context_t context = {test_data, VFS_TEST_DATA_SIZE};
    vfs_fd_table_t isolated = {0};
    vfs_fd_table_t* table;
    file_t* file = 0;
    vnode_t* vnode = 0;
    uint8_t buffer[4];
    int32_t filled_fds[VFS_MAX_FDS - VFS_FD_FIRST_FILE];
    uint32_t bytes = 0U;
    uint32_t filled_count = 0U;
    uint32_t position = 0U;
    uint8_t fill_cleanup = 1U;
    uint8_t capacity_ready = 1U;
    int fd;
    int close_result;
    int result_code;
    char saved_cwd[VFS_MAX_PATH];
    char cwd[VFS_MAX_PATH];
    char alias[VFS_MAX_PATH];
    uint32_t dir_count = 0U;
    devfs_test_result_t device_result;

    if (!result) {
        LOG_ERROR("FS", "Destino de autoteste VFS nulo");
        return ERR_NULL;
    }
    if (!vfs_ready) {
        LOG_ERROR("FS", "Autoteste solicitado com VFS indisponivel");
        return ERR_UNAVAILABLE;
    }
    kmemset(result, 0, sizeof(vfs_test_result_t));
    result_code = vfs_get_current_table(&table);
    if (result_code != OK) return result_code;
    result->stdio = table->entries[0] == table->standard_files[0] &&
                    table->entries[1] == table->standard_files[1] &&
                    table->entries[2] == table->standard_files[2] &&
                    table->standard_files[0]->mode == VFS_MODE_READ &&
                    table->standard_files[1]->mode == VFS_MODE_WRITE &&
                    table->standard_files[2]->mode == VFS_MODE_WRITE &&
                    table->standard_files[0]->vnode->type == VFS_NODE_STDIN &&
                    table->standard_files[1]->vnode->type == VFS_NODE_STDOUT &&
                    table->standard_files[2]->vnode->type == VFS_NODE_STDERR;
    vfs_test_count(result, result->stdio);
    fd = vfs_find_free_fd(table);
    if (fd == VFS_FD_INVALID ||
        vfs_allocate_file(&file, &vnode, VFS_FILE_ACTIVE) != OK) {
        return ERR_UNAVAILABLE;
    }
    vnode->type = VFS_NODE_TEST;
    vnode->operations = &vfs_test_operations;
    vnode->private_data = &context;
    vnode->size = context.size;
    vfs_copy_text(vnode->path, VFS_MAX_PATH, "fixture:vfs");
    file->mode = VFS_MODE_READ;
    spinlock_acquire(&vfs_lock);
    table->entries[fd] = file;
    vfs_metrics.opens++;
    spinlock_release(&vfs_lock);
    result->lifecycle = 1U;
    vfs_test_count(result, result->lifecycle);
    result_code = vfs_read(fd, buffer, sizeof(buffer), &bytes);
    result->sequential_read = result_code == OK && bytes == sizeof(buffer) &&
                              buffer[0] == 'Z' && buffer[3] == 'H';
    vfs_test_count(result, result->sequential_read);
    result_code = vfs_lseek(fd, 2, VFS_SEEK_SET, &position);
    result->seek = result_code == OK && position == 2U;
    vfs_test_count(result, result->seek);
    result->permissions = vfs_write(fd, test_data, 1U, &bytes) ==
                          ERR_UNAVAILABLE;
    file->mode = VFS_MODE_WRITE;
    result->permissions = result->permissions &&
        vfs_lseek(fd, 0, VFS_SEEK_SET, &position) == ERR_UNAVAILABLE;
    file->mode = VFS_MODE_READ;
    vfs_test_count(result, result->permissions);
    vfs_lseek(fd, 0, VFS_SEEK_END, &position);
    result_code = vfs_read(fd, buffer, sizeof(buffer), &bytes);
    result->eof = result_code == OK && bytes == 0U;
    vfs_test_count(result, result->eof);
    result->limits = vfs_read(fd, buffer, APP_API_MAX_FILE_IO_SIZE + 1U,
                              &bytes) == ERR_OVERFLOW &&
                     vfs_lseek(fd, 1, VFS_SEEK_END, &position) == ERR_INVALID;
    vfs_test_count(result, result->limits);
    close_result = vfs_close(fd);
    result->closed_descriptor = close_result == OK &&
        vfs_read(fd, buffer, sizeof(buffer), &bytes) == ERR_INVALID;
    vfs_test_count(result, result->closed_descriptor);
    while ((fd = vfs_find_free_fd(table)) != VFS_FD_INVALID) {
        file = 0;
        vnode = 0;
        if (vfs_allocate_file(&file, &vnode, VFS_FILE_ACTIVE) != OK) {
            capacity_ready = 0U;
            break;
        }
        vnode->type = VFS_NODE_TEST;
        vnode->operations = &vfs_test_operations;
        vnode->private_data = &context;
        vnode->size = context.size;
        vfs_copy_text(vnode->path, VFS_MAX_PATH, "fixture:capacity");
        file->mode = VFS_MODE_READ;
        spinlock_acquire(&vfs_lock);
        table->entries[fd] = file;
        vfs_metrics.opens++;
        spinlock_release(&vfs_lock);
        filled_fds[filled_count++] = fd;
    }
    result->table_capacity = capacity_ready && filled_count > 0U &&
                             vfs_find_free_fd(table) == VFS_FD_INVALID &&
                             VFS_MAX_FDS == 32U &&
                             VFS_MAX_OPEN_FILES == 32U;
    vfs_test_count(result, result->table_capacity);
    while (filled_count > 0U) {
        filled_count--;
        if (vfs_close(filled_fds[filled_count]) != OK) fill_cleanup = 0U;
    }
    result_code = vfs_fd_table_init(&isolated);
    if (result_code == OK) {
        result_code = vfs_fd_table_inherit_cwd(&isolated, table);
    }
    result->isolation = result_code == OK && isolated.entries[3] == 0 &&
                        isolated.entries[0] == isolated.standard_files[0] &&
                        isolated.entries[0] != table->entries[0] &&
                        kstrcmp(isolated.cwd, table->cwd) == 0;
    vfs_test_count(result, result->isolation);
    file = 0;
    vnode = 0;
    if (result_code == OK &&
        vfs_allocate_file(&file, &vnode, VFS_FILE_ACTIVE) == OK) {
        vnode->type = VFS_NODE_TEST;
        vnode->operations = &vfs_test_operations;
        vnode->private_data = &context;
        vnode->size = context.size;
        vfs_copy_text(vnode->path, VFS_MAX_PATH, "fixture:cleanup");
        file->mode = VFS_MODE_READ;
        spinlock_acquire(&vfs_lock);
        isolated.entries[VFS_FD_FIRST_FILE] = file;
        spinlock_release(&vfs_lock);
    } else {
        fill_cleanup = 0U;
    }
    result_code = vfs_fd_table_release(&isolated);
    result->cleanup = fill_cleanup && result_code == OK &&
                      !isolated.initialized && !isolated.cwd[0];
    vfs_test_count(result, result->cleanup);
    result_code = vfs_getcwd(saved_cwd, sizeof(saved_cwd));
    result->normalization = result_code == OK &&
        vfs_lookup("/mnt/../", &vfs_test_root_lookup) == OK &&
        vfs_test_root_lookup.type == VFS_NODE_DIRECTORY &&
        kstrcmp(vfs_test_root_lookup.canonical_path, "/") == 0 &&
        vfs_lookup("//mnt/./", &vfs_test_virtual_lookup) == OK &&
        kstrcmp(vfs_test_virtual_lookup.canonical_path, "/mnt") == 0 &&
        vfs_lookup("/..", &vfs_test_virtual_lookup) == ERR_INVALID;
    vfs_test_count(result, result->normalization);
    vfs_copy_text(alias, sizeof(alias), "system:");
    result->lookup = vfs_lookup("/mnt", &vfs_test_virtual_lookup) == OK &&
                     vfs_test_virtual_lookup.type == VFS_NODE_DIRECTORY &&
                     vfs_lookup(alias, &vfs_test_virtual_lookup) == OK &&
                     kstrcmp(vfs_test_virtual_lookup.canonical_path,
                             "/") == 0 &&
                     vfs_lookup("/diretorio-vfs-inexistente",
                                &vfs_test_virtual_lookup) == ERR_NOT_FOUND;
    vfs_test_count(result, result->lookup);
    result->cwd = vfs_chdir("/mnt") == OK &&
                  vfs_getcwd(cwd, sizeof(cwd)) == OK &&
                  kstrcmp(cwd, "/mnt") == 0 &&
                  vfs_chdir(saved_cwd) == OK;
    vfs_test_count(result, result->cwd);
    result->mount_busy = vfs_test_root_lookup.volume_id[0] &&
        vfs_mount_acquire(vfs_test_root_lookup.mount_slot,
                          vfs_test_root_lookup.mount_generation) == OK;
    if (result->mount_busy) {
        result->mount_busy =
            vfs_unmount_volume(vfs_test_root_lookup.volume_id) == ERR_STATE &&
            vfs_mount_validate_reference(
                vfs_test_root_lookup.mount_slot,
                vfs_test_root_lookup.mount_generation + 1U) == ERR_STATE;
        vfs_mount_release(vfs_test_root_lookup.mount_slot,
                          vfs_test_root_lookup.mount_generation);
    }
    vfs_test_count(result, result->mount_busy);
    result->directory_listing =
        vfs_list_dir("/dev", vfs_test_dir_entries, VFS_MAX_DIR_ENTRIES,
                     &dir_count) == OK &&
        dir_count == DEVFS_MAX_NODES;
    if (result->directory_listing) {
        kmemset(vfs_test_dir_entries, 0, sizeof(vfs_test_dir_entries));
        result->directory_listing =
            vfs_list_dir("/", vfs_test_dir_entries, VFS_MAX_DIR_ENTRIES,
                         &dir_count) == OK &&
            vfs_test_dir_has("mnt") && vfs_test_dir_has("dev");
    }
    vfs_test_count(result, result->directory_listing);
    result->devices = devfs_self_test(&device_result) == OK;
    vfs_test_count(result, result->devices);
    {
        int32_t pipe_fds[2] = {VFS_FD_INVALID, VFS_FD_INVALID};
        uint8_t pipe_buffer[VFS_TEST_DATA_SIZE];
        uint32_t pipe_bytes = 0U;
        int pipe_result = vfs_pipe(pipe_fds);

        if (pipe_result == OK) {
            pipe_result = vfs_write(pipe_fds[1], test_data,
                                    sizeof(test_data), &pipe_bytes);
            if (pipe_result == OK && pipe_bytes == sizeof(test_data)) {
                pipe_result = vfs_close(pipe_fds[1]);
                pipe_fds[1] = VFS_FD_INVALID;
            }
            if (pipe_result == OK) {
                pipe_result = vfs_read(pipe_fds[0], pipe_buffer,
                                       sizeof(pipe_buffer), &pipe_bytes);
            }
            if (pipe_result == OK && pipe_bytes == sizeof(pipe_buffer)) {
                for (uint32_t index = 0U; index < sizeof(pipe_buffer); index++) {
                    if (pipe_buffer[index] != test_data[index]) {
                        pipe_result = ERR_STATE;
                        break;
                    }
                }
            } else if (pipe_result == OK) {
                pipe_result = ERR_STATE;
            }
            if (pipe_result == OK) {
                pipe_result = vfs_read(pipe_fds[0], pipe_buffer,
                                       sizeof(pipe_buffer), &pipe_bytes);
                if (pipe_result == OK && pipe_bytes != 0U) {
                    pipe_result = ERR_STATE;
                }
            }
            if (vfs_close(pipe_fds[0]) != OK) pipe_result = ERR_STATE;
            pipe_fds[0] = VFS_FD_INVALID;
        }
        if (pipe_fds[0] != VFS_FD_INVALID) (void)vfs_close(pipe_fds[0]);
        if (pipe_fds[1] != VFS_FD_INVALID) (void)vfs_close(pipe_fds[1]);
        if (pipe_result == OK) {
            int32_t no_reader_fds[2] = {VFS_FD_INVALID, VFS_FD_INVALID};
            uint32_t no_reader_bytes = 0U;

            pipe_result = vfs_pipe(no_reader_fds);
            if (pipe_result == OK) {
                if (vfs_close(no_reader_fds[0]) != OK) {
                    pipe_result = ERR_STATE;
                }
                no_reader_fds[0] = VFS_FD_INVALID;
            }
            if (pipe_result == OK) {
                pipe_result = vfs_write(no_reader_fds[1], test_data, 1U,
                                        &no_reader_bytes) == ERR_UNAVAILABLE ?
                              OK : ERR_STATE;
            }
            if (no_reader_fds[1] != VFS_FD_INVALID) {
                if (vfs_close(no_reader_fds[1]) != OK) pipe_result = ERR_STATE;
                no_reader_fds[1] = VFS_FD_INVALID;
            }
        }
        result->pipes = pipe_result == OK;
    }
    vfs_test_count(result, result->pipes);
    {
        int32_t speaker_fd = VFS_FD_INVALID;
        int ioctl_result = vfs_open("/dev/speaker", VFS_MODE_WRITE,
                                    &speaker_fd);
        if (ioctl_result == OK) {
            ioctl_result = vfs_ioctl(speaker_fd, APP_IOCTL_SPEAKER_STOP, 0);
            if (vfs_close(speaker_fd) != OK) ioctl_result = ERR_STATE;
        }
        result->ioctl = ioctl_result == OK;
    }
    vfs_test_count(result, result->ioctl);
    result->invariants = vfs_validate_state() == OK;
    vfs_test_count(result, result->invariants);
    return result->passed == result->total ? OK : ERR_STATE;
}
