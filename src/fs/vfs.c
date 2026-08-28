#include "fs/vfs.h"
#include "fs/fs.h"
#include "core/app_api.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "process/process.h"

#define VFS_TEST_DATA_SIZE 8U

typedef struct {
    const uint8_t* data;
    uint32_t size;
} vfs_test_context_t;

static file_t vfs_file_pool[VFS_MAX_OPEN_FILES];
static vnode_t vfs_vnode_pool[VFS_MAX_OPEN_FILES];
static vnode_t vfs_stdio_nodes[3];
static spinlock_t vfs_lock;
static vfs_status_t vfs_metrics;
static int vfs_ready;

static int vfs_regular_open(vnode_t* vnode, file_t* file);
static int vfs_regular_read(file_t* file, void* buffer, uint32_t size,
                            uint32_t* bytes_read);
static int vfs_regular_write(file_t* file, const void* buffer, uint32_t size,
                             uint32_t* bytes_written);
static int vfs_regular_close(file_t* file);
static int vfs_regular_lseek(file_t* file, int32_t offset, uint32_t whence,
                             uint32_t* position);
static int vfs_stdin_read(file_t* file, void* buffer, uint32_t size,
                          uint32_t* bytes_read);
static int vfs_console_write(file_t* file, const void* buffer, uint32_t size,
                             uint32_t* bytes_written);
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

static const file_operations_t vfs_regular_operations = {
    vfs_regular_open, vfs_regular_read, vfs_regular_write,
    vfs_regular_close, vfs_regular_lseek, vfs_unsupported_ioctl
};

static const file_operations_t vfs_stdin_operations = {
    vfs_unsupported_open, vfs_stdin_read, vfs_unsupported_write,
    vfs_unsupported_close, vfs_unsupported_lseek, vfs_unsupported_ioctl
};

static const file_operations_t vfs_stdout_operations = {
    vfs_unsupported_open, vfs_unsupported_read, vfs_console_write,
    vfs_unsupported_close, vfs_unsupported_lseek, vfs_unsupported_ioctl
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

static int vfs_allocate_file(file_t** file_out, vnode_t** vnode_out) {
    uint32_t index;

    if (!file_out || !vnode_out) return ERR_NULL;
    spinlock_acquire(&vfs_lock);
    for (index = 0U; index < VFS_MAX_OPEN_FILES; index++) {
        if (!vfs_file_pool[index].used) {
            kmemset(&vfs_file_pool[index], 0, sizeof(file_t));
            kmemset(&vfs_vnode_pool[index], 0, sizeof(vnode_t));
            vfs_file_pool[index].used = 1U;
            vfs_file_pool[index].slot = index;
            vfs_file_pool[index].vnode = &vfs_vnode_pool[index];
            *file_out = &vfs_file_pool[index];
            *vnode_out = &vfs_vnode_pool[index];
            spinlock_release(&vfs_lock);
            return OK;
        }
    }
    vfs_metrics.failures++;
    spinlock_release(&vfs_lock);
    LOG_WARN("FS", "Pool global de arquivos VFS cheio");
    return ERR_UNAVAILABLE;
}

static void vfs_release_file(file_t* file) {
    uint32_t slot;

    if (!file || file->persistent || !file->used) return;
    slot = file->slot;
    if (slot >= VFS_MAX_OPEN_FILES) return;
    spinlock_acquire(&vfs_lock);
    kmemset(&vfs_file_pool[slot], 0, sizeof(file_t));
    kmemset(&vfs_vnode_pool[slot], 0, sizeof(vnode_t));
    spinlock_release(&vfs_lock);
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

static int vfs_regular_open(vnode_t* vnode, file_t* file) {
    uint32_t size = 0U;
    int result;

    if (!vnode || !file) return ERR_NULL;
    if (fs_get_type() == FS_TYPE_NONE) return ERR_UNAVAILABLE;
    if (file->mode & VFS_MODE_READ) {
        result = fs_read_file_range_at(vnode->path, 0U, 0, 0U, &size);
        if (result != OK) return result;
    }
    vnode->size = size;
    file->offset = 0U;
    return OK;
}

static int vfs_regular_read(file_t* file, void* buffer, uint32_t size,
                            uint32_t* bytes_read) {
    int result;

    if (!file || !file->vnode || !bytes_read) return ERR_NULL;
    *bytes_read = 0U;
    if (!(file->mode & VFS_MODE_READ)) return ERR_UNAVAILABLE;
    result = fs_read_file_range_at(file->vnode->path, file->offset,
                                   (uint8_t*)buffer, size, bytes_read);
    if (result != OK) return result;
    if (file->offset > 0xFFFFFFFFU - *bytes_read) return ERR_OVERFLOW;
    file->offset += *bytes_read;
    return OK;
}

static int vfs_regular_write(file_t* file, const void* buffer, uint32_t size,
                             uint32_t* bytes_written) {
    int result;

    if (!file || !file->vnode || !bytes_written) return ERR_NULL;
    *bytes_written = 0U;
    if (!(file->mode & VFS_MODE_WRITE)) return ERR_UNAVAILABLE;
    result = fs_write_file_at(file->vnode->path, (const uint8_t*)buffer, size);
    if (result != OK) return result;
    file->vnode->size = size;
    file->offset = 0U;
    *bytes_written = size;
    return OK;
}

static int vfs_regular_close(file_t* file) {
    if (!file) return ERR_NULL;
    return OK;
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

static int vfs_stdin_read(file_t* file, void* buffer, uint32_t size,
                          uint32_t* bytes_read) {
    uint8_t* output = (uint8_t*)buffer;
    wait_reason_t reason = WAIT_REASON_NONE;
    ipc_msg_t message;
    int result;

    (void)file;
    if (!bytes_read) return ERR_NULL;
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
        if (reason != WAIT_REASON_EVENT) return ERR_STATE;
    }
    return OK;
}

static int vfs_console_write(file_t* file, const void* buffer, uint32_t size,
                             uint32_t* bytes_written) {
    int result;

    (void)file;
    if (!bytes_written) return ERR_NULL;
    *bytes_written = 0U;
    result = app_api_console_write((const char*)buffer, size);
    if (result == OK) *bytes_written = size;
    return result;
}

int vfs_init(void) {
    static const char* names[3] = {"stdin", "stdout", "stderr"};
    uint32_t index;

    LOG_INFO("FS", "Inicializando VFS");
    if (vfs_ready) {
        LOG_WARN("FS", "VFS ja estava inicializada");
        return OK;
    }
    spinlock_init(&vfs_lock);
    kmemset(vfs_file_pool, 0, sizeof(vfs_file_pool));
    kmemset(vfs_vnode_pool, 0, sizeof(vfs_vnode_pool));
    kmemset(vfs_stdio_nodes, 0, sizeof(vfs_stdio_nodes));
    kmemset(&vfs_metrics, 0, sizeof(vfs_metrics));
    for (index = 0U; index < 3U; index++) {
        vfs_stdio_nodes[index].type = index == 0U ? VFS_NODE_STDIN :
                                      index == 1U ? VFS_NODE_STDOUT :
                                                    VFS_NODE_STDERR;
        vfs_stdio_nodes[index].operations = index == 0U ?
            &vfs_stdin_operations : &vfs_stdout_operations;
        vfs_copy_text(vfs_stdio_nodes[index].path, VFS_MAX_PATH, names[index]);
    }
    vfs_ready = 1;
    vfs_metrics.initialized = 1U;
    vfs_metrics.descriptor_capacity = VFS_MAX_FDS;
    vfs_metrics.global_file_capacity = VFS_MAX_OPEN_FILES;
    LOG_INFO("FS", "VFS inicializada com sucesso");
    return OK;
}

int vfs_is_ready(void) {
    return vfs_ready;
}

int vfs_fd_table_init(vfs_fd_table_t* table) {
    uint32_t index;

    if (!table) {
        LOG_ERROR("FS", "Tabela de descritores nula na inicializacao");
        return ERR_NULL;
    }
    kmemset(table, 0, sizeof(vfs_fd_table_t));
    if (!vfs_ready) {
        LOG_ERROR("FS", "Tabela de descritores criada antes da VFS");
        return ERR_UNAVAILABLE;
    }
    spinlock_acquire(&vfs_lock);
    for (index = 0U; index < 3U; index++) {
        table->standard_files[index].used = 1U;
        table->standard_files[index].persistent = 1U;
        table->standard_files[index].slot = index;
        table->standard_files[index].vnode = &vfs_stdio_nodes[index];
        table->standard_files[index].mode = index == VFS_FD_STDIN ?
            VFS_MODE_READ : VFS_MODE_WRITE;
        table->entries[index] = &table->standard_files[index];
    }
    table->initialized = 1U;
    spinlock_release(&vfs_lock);
    return OK;
}

int vfs_fd_table_release(vfs_fd_table_t* table) {
    uint32_t index;

    if (!table) {
        LOG_ERROR("FS", "Tabela de descritores nula na liberacao");
        return ERR_NULL;
    }
    if (!table->initialized) return OK;
    spinlock_acquire(&vfs_lock);
    for (index = 0U; index < VFS_FD_FIRST_FILE; index++) {
        if (table->standard_files[index].active_operations != 0U) {
            spinlock_release(&vfs_lock);
            LOG_ERROR("FS", "Descritor padrao ativo durante liberacao");
            return ERR_STATE;
        }
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
    table->entries[VFS_FD_STDIN] = 0;
    table->entries[VFS_FD_STDOUT] = 0;
    table->entries[VFS_FD_STDERR] = 0;
    kmemset(table->standard_files, 0, sizeof(table->standard_files));
    table->initialized = 0U;
    spinlock_release(&vfs_lock);
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
    result = vfs_allocate_file(&file, &vnode);
    if (result != OK) return result;
    vnode->type = VFS_NODE_REGULAR;
    vnode->operations = &vfs_regular_operations;
    vfs_copy_text(vnode->path, VFS_MAX_PATH, path);
    file->mode = mode;
    result = vnode->operations->open(vnode, file);
    if (result != OK) {
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
        vfs_release_file(file);
        LOG_ERROR("FS", "Descritor VFS deixou de estar disponivel");
        return ERR_STATE;
    }
    table->entries[fd] = file;
    vfs_metrics.opens++;
    spinlock_release(&vfs_lock);
    *fd_out = fd;
    return OK;
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
    result = file->vnode->operations->read(file, buffer, size, bytes_read);
    vfs_end_operation(file);
    spinlock_acquire(&vfs_lock);
    if (result == OK) vfs_metrics.reads++;
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
    result = file->vnode->operations->write(file, buffer, size, bytes_written);
    vfs_end_operation(file);
    spinlock_acquire(&vfs_lock);
    if (result == OK) vfs_metrics.writes++;
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
    for (fd = 0U; fd < VFS_MAX_OPEN_FILES; fd++) {
        if (vfs_file_pool[fd].used) status->global_files_used++;
    }
    for (process_index = 0U; process_index < MAX_PROCESSES; process_index++) {
        if (!processes[process_index].fd_table.initialized) continue;
        status->processes_with_tables++;
        for (fd = 0U; fd < VFS_MAX_FDS; fd++) {
            if (processes[process_index].fd_table.entries[fd]) {
                status->descriptors_open++;
            }
        }
    }
    spinlock_release(&vfs_lock);
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

int vfs_validate_state(void) {
    uint32_t process_index;
    uint32_t fd;

    if (!vfs_ready) return ERR_UNAVAILABLE;
    spinlock_acquire(&vfs_lock);
    for (process_index = 0U; process_index < MAX_PROCESSES; process_index++) {
        process_t* process = &processes[process_index];
        if (!process->fd_table.initialized) continue;
        for (fd = 0U; fd < VFS_FD_FIRST_FILE; fd++) {
            if (process->fd_table.entries[fd] !=
                    &process->fd_table.standard_files[fd] ||
                !process->fd_table.standard_files[fd].used ||
                !process->fd_table.standard_files[fd].persistent ||
                process->fd_table.standard_files[fd].vnode !=
                    &vfs_stdio_nodes[fd]) {
                spinlock_release(&vfs_lock);
                return ERR_STATE;
            }
        }
        for (fd = 0U; fd < VFS_MAX_FDS; fd++) {
            file_t* file = process->fd_table.entries[fd];
            uint32_t pool_index;
            uint8_t pool_member = 0U;

            if (!file) continue;
            if (!file->used || !file->vnode || !file->vnode->operations ||
                (fd >= VFS_FD_FIRST_FILE && file->persistent)) {
                spinlock_release(&vfs_lock);
                return ERR_STATE;
            }
            if (fd < VFS_FD_FIRST_FILE) continue;
            for (pool_index = 0U; pool_index < VFS_MAX_OPEN_FILES;
                 pool_index++) {
                if (file == &vfs_file_pool[pool_index]) {
                    pool_member = 1U;
                    break;
                }
            }
            if (!pool_member) {
                spinlock_release(&vfs_lock);
                return ERR_STATE;
            }
        }
    }
    for (fd = 0U; fd < VFS_MAX_OPEN_FILES; fd++) {
        file_t* file = &vfs_file_pool[fd];
        uint32_t references = 0U;

        if (file->used && (file->slot != fd || !file->vnode ||
                          file->vnode != &vfs_vnode_pool[fd])) {
            spinlock_release(&vfs_lock);
            return ERR_STATE;
        }
        for (process_index = 0U; process_index < MAX_PROCESSES;
             process_index++) {
            process_t* process = &processes[process_index];
            uint32_t descriptor;

            if (!process->fd_table.initialized) continue;
            for (descriptor = VFS_FD_FIRST_FILE;
                 descriptor < VFS_MAX_FDS; descriptor++) {
                if (process->fd_table.entries[descriptor] == file) {
                    references++;
                }
            }
        }
        if ((file->used && references != 1U) ||
            (!file->used && references != 0U)) {
            spinlock_release(&vfs_lock);
            return ERR_STATE;
        }
    }
    spinlock_release(&vfs_lock);
    return OK;
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
    vfs_unsupported_close, vfs_regular_lseek, vfs_unsupported_ioctl
};

static void vfs_test_count(vfs_test_result_t* result, uint8_t passed) {
    result->total++;
    if (passed) result->passed++;
}

int vfs_self_test(vfs_test_result_t* result) {
    static const uint8_t test_data[VFS_TEST_DATA_SIZE] =
        {'Z', 'E', 'P', 'H', 'Y', 'R', 'O', 'S'};
    vfs_test_context_t context = {test_data, VFS_TEST_DATA_SIZE};
    vfs_fd_table_t isolated;
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
    result->stdio = table->entries[0] == &table->standard_files[0] &&
                    table->entries[1] == &table->standard_files[1] &&
                    table->entries[2] == &table->standard_files[2] &&
                    table->standard_files[0].mode == VFS_MODE_READ &&
                    table->standard_files[1].mode == VFS_MODE_WRITE &&
                    table->standard_files[2].mode == VFS_MODE_WRITE &&
                    table->standard_files[0].vnode->type == VFS_NODE_STDIN &&
                    table->standard_files[1].vnode->type == VFS_NODE_STDOUT &&
                    table->standard_files[2].vnode->type == VFS_NODE_STDERR;
    vfs_test_count(result, result->stdio);
    fd = vfs_find_free_fd(table);
    if (fd == VFS_FD_INVALID ||
        vfs_allocate_file(&file, &vnode) != OK) return ERR_UNAVAILABLE;
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
        if (vfs_allocate_file(&file, &vnode) != OK) {
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
    result->isolation = result_code == OK && isolated.entries[3] == 0 &&
                        isolated.entries[0] == &isolated.standard_files[0] &&
                        isolated.entries[0] != table->entries[0];
    vfs_test_count(result, result->isolation);
    file = 0;
    vnode = 0;
    if (result_code == OK && vfs_allocate_file(&file, &vnode) == OK) {
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
                      !isolated.initialized && file && !file->used;
    vfs_test_count(result, result->cleanup);
    result->invariants = vfs_validate_state() == OK;
    vfs_test_count(result, result->invariants);
    return result->passed == result->total ? OK : ERR_STATE;
}
