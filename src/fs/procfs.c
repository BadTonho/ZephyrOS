#include "fs/procfs.h"
#include "fs/vfs_internal.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "core/timer.h"

typedef struct {
    proc_entry_t entry;
    uint32_t index;
} procfs_entry_snapshot_t;

static int procfs_uptime_read(char* buffer, uint32_t capacity,
                              uint32_t* out_len, void* data);
static int procfs_open(vnode_t* vnode, file_t* file);
static int procfs_read(file_t* file, void* buffer, uint32_t size,
                       uint32_t* bytes_read);
static int procfs_write(file_t* file, const void* buffer, uint32_t size,
                        uint32_t* bytes_written);
static int procfs_close(file_t* file);
static int procfs_lseek(file_t* file, int32_t offset, uint32_t whence,
                        uint32_t* position);
static int procfs_ioctl(file_t* file, uint32_t request, void* argument);
static int procfs_sync(file_t* file);
static int procfs_poll(file_t* file, uint32_t events, uint32_t* revents);
static int procfs_render_snapshot(const proc_entry_t* entry,
                                  procfs_file_context_t* context);

static const proc_entry_t procfs_entries[] = {
    {"uptime", VFS_MODE_READ, procfs_uptime_read, 0, 0}
};

static const file_operations_t procfs_operations = {
    procfs_open, procfs_read, procfs_write, procfs_close, procfs_lseek,
    procfs_ioctl, procfs_sync, procfs_poll
};

static spinlock_t procfs_lock;
static uint8_t procfs_ready;
static uint32_t procfs_active_snapshots;

static void procfs_test_count(procfs_test_result_t* result, uint8_t passed) {
    result->total++;
    if (passed) result->passed++;
}

static void procfs_copy_text(char* destination, uint32_t capacity,
                             const char* source) {
    uint32_t index = 0U;

    if (!destination || !capacity) return;
    while (source && source[index] && index + 1U < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int procfs_path_is_root(const char* path) {
    return path && kstrcmp(path, "/proc") == 0;
}

static int procfs_path_is_uptime(const char* path) {
    return path && kstrcmp(path, "/proc/uptime") == 0;
}

static int procfs_append_char(char* buffer, uint32_t capacity,
                              uint32_t* length, char value) {
    if (!buffer || !length) {
        LOG_ERROR("PROCFS", "Destino nulo ao serializar procfs");
        return ERR_NULL;
    }
    if (*length >= capacity) {
        LOG_WARN("PROCFS", "Snapshot procfs excedeu a capacidade");
        return ERR_OVERFLOW;
    }
    buffer[*length] = value;
    (*length)++;
    return OK;
}

static int procfs_append_text(char* buffer, uint32_t capacity,
                              uint32_t* length, const char* text) {
    uint32_t index = 0U;
    int result;

    if (!text) {
        LOG_ERROR("PROCFS", "Texto nulo ao serializar procfs");
        return ERR_NULL;
    }
    while (text[index]) {
        result = procfs_append_char(buffer, capacity, length, text[index]);
        if (result != OK) return result;
        index++;
    }
    return OK;
}

static int procfs_append_decimal(char* buffer, uint32_t capacity,
                                 uint32_t* length, uint32_t value) {
    char digits[10];
    uint32_t count = 0U;
    int result;

    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value && count < sizeof(digits));
    while (count) {
        count--;
        result = procfs_append_char(buffer, capacity, length, digits[count]);
        if (result != OK) return result;
    }
    return OK;
}

static int procfs_uptime_read(char* buffer, uint32_t capacity,
                              uint32_t* out_len, void* data) {
    uint32_t length = 0U;
    int result;

    (void)data;
    if (!buffer || !out_len) {
        LOG_ERROR("PROCFS", "Destino nulo no callback de uptime");
        return ERR_NULL;
    }
    *out_len = 0U;
    if (!timer_get_frequency()) {
        LOG_WARN("PROCFS", "Frequencia do timer indisponivel");
        return ERR_UNAVAILABLE;
    }
    result = procfs_append_text(buffer, capacity, &length, "uptime_ticks ");
    if (result == OK) {
        result = procfs_append_decimal(buffer, capacity, &length,
                                       timer_get_ticks());
    }
    if (result == OK) result = procfs_append_char(buffer, capacity, &length,
                                                   '\n');
    if (result == OK) result = procfs_append_text(buffer, capacity, &length,
                                                   "frequency_hz ");
    if (result == OK) {
        result = procfs_append_decimal(buffer, capacity, &length,
                                       timer_get_frequency());
    }
    if (result == OK) result = procfs_append_char(buffer, capacity, &length,
                                                   '\n');
    if (result == OK) *out_len = length;
    return result;
}

static int procfs_entry_find(const char* canonical_path,
                             procfs_entry_snapshot_t* snapshot) {
    uint32_t index;

    if (!canonical_path || !snapshot) {
        LOG_ERROR("PROCFS", "Argumento nulo no lookup procfs");
        return ERR_NULL;
    }
    if (!procfs_path_is_uptime(canonical_path)) {
        LOG_WARN("PROCFS", "Entrada procfs inexistente");
        return ERR_NOT_FOUND;
    }
    spinlock_acquire(&procfs_lock);
    if (!procfs_ready) {
        spinlock_release(&procfs_lock);
        LOG_ERROR("PROCFS", "Lookup procfs antes da inicializacao");
        return ERR_STATE;
    }
    for (index = 0U; index < sizeof(procfs_entries) / sizeof(procfs_entries[0]);
         index++) {
        if (kstrcmp(procfs_entries[index].name, "uptime") == 0) {
            snapshot->entry = procfs_entries[index];
            snapshot->index = index;
            spinlock_release(&procfs_lock);
            return OK;
        }
    }
    spinlock_release(&procfs_lock);
    return ERR_NOT_FOUND;
}

int procfs_init(void) {
    LOG_INFO("PROCFS", "Inicializando procfs");
    spinlock_init(&procfs_lock);
    spinlock_acquire(&procfs_lock);
    procfs_active_snapshots = 0U;
    procfs_ready = 1U;
    spinlock_release(&procfs_lock);
    LOG_INFO("PROCFS", "Procfs inicializado em modo somente leitura");
    return OK;
}

int procfs_is_ready(void) {
    return procfs_ready;
}

int procfs_lookup(const char* canonical_path, vfs_lookup_result_t* result) {
    procfs_entry_snapshot_t snapshot;
    int status;

    if (!canonical_path || !result) {
        LOG_ERROR("PROCFS", "Destino nulo no lookup publico procfs");
        return ERR_NULL;
    }
    if (!procfs_ready) {
        LOG_ERROR("PROCFS", "Lookup publico procfs antes da inicializacao");
        return ERR_STATE;
    }
    if (procfs_path_is_root(canonical_path)) {
        result->type = VFS_NODE_DIRECTORY;
        result->size = 0U;
        result->attributes = 0U;
        result->read_only = 1U;
        result->relative_path[0] = '\0';
        return OK;
    }
    status = procfs_entry_find(canonical_path, &snapshot);
    if (status != OK) return status;
    result->type = VFS_NODE_REGULAR;
    result->size = 0U;
    result->attributes = 0U;
    result->read_only = 1U;
    procfs_copy_text(result->relative_path, VFS_MAX_PATH, "uptime");
    return OK;
}

static int procfs_render_snapshot(const proc_entry_t* entry,
                                  procfs_file_context_t* context) {
    uint32_t length = 0U;
    int result;

    if (!entry || !context || !entry->read_proc) {
        LOG_ERROR("PROCFS", "Callback procfs invalido");
        return ERR_INVALID;
    }
    context->snapshot = (uint8_t*)kmalloc(PROCFS_MAX_SNAPSHOT_SIZE);
    if (!context->snapshot) {
        LOG_ERROR("PROCFS", "Falha ao alocar snapshot procfs");
        return ERR_MEM;
    }
    result = entry->read_proc((char*)context->snapshot,
                              PROCFS_MAX_SNAPSHOT_SIZE, &length,
                              entry->data);
    if (result != OK || length > PROCFS_MAX_SNAPSHOT_SIZE) {
        if (result == OK) result = ERR_OVERFLOW;
        LOG_ERROR("PROCFS", "Snapshot procfs recusado pelo callback");
        kfree(context->snapshot);
        context->snapshot = 0;
        context->snapshot_size = 0U;
        return result;
    }
    context->snapshot_size = length;
    spinlock_acquire(&procfs_lock);
    procfs_active_snapshots++;
    spinlock_release(&procfs_lock);
    return OK;
}

int procfs_open_file(const vfs_lookup_result_t* lookup, uint32_t mode,
                     vnode_t* vnode, file_t* file,
                     procfs_file_context_t* context) {
    procfs_entry_snapshot_t snapshot;
    int result;

    if (!lookup || !vnode || !file || !context) {
        LOG_ERROR("PROCFS", "Argumento nulo na abertura procfs");
        return ERR_NULL;
    }
    if (lookup->mount_kind != VFS_MOUNT_PROCFS) {
        LOG_ERROR("PROCFS", "Montagem invalida na abertura procfs");
        return ERR_INVALID;
    }
    if (mode != VFS_MODE_READ) {
        LOG_WARN("PROCFS", "Abertura procfs com escrita recusada");
        return ERR_UNAVAILABLE;
    }
    result = procfs_entry_find(lookup->canonical_path, &snapshot);
    if (result != OK) return result;
    if (!(snapshot.entry.mode & VFS_MODE_READ)) {
        LOG_WARN("PROCFS", "Entry procfs sem permissao de leitura");
        return ERR_UNAVAILABLE;
    }
    result = procfs_render_snapshot(&snapshot.entry, context);
    if (result != OK) {
        LOG_ERROR("PROCFS", "Falha ao gerar snapshot procfs");
        return result;
    }
    context->entry_index = snapshot.index;
    context->mount_slot = lookup->mount_slot;
    context->mount_generation = lookup->mount_generation;
    context->mount_acquired = 1U;
    vnode->type = VFS_NODE_REGULAR;
    vnode->operations = &procfs_operations;
    vnode->private_data = context;
    vnode->size = context->snapshot_size;
    file->mode = mode;
    file->offset = 0U;
    return OK;
}

int procfs_list(vfs_dir_entry_t* entries, uint32_t capacity,
                uint32_t* out_count) {
    if (!entries || !out_count) {
        LOG_ERROR("PROCFS", "Destino nulo na listagem procfs");
        return ERR_NULL;
    }
    *out_count = 0U;
    if (!capacity) {
        LOG_ERROR("PROCFS", "Capacidade nula na listagem procfs");
        return ERR_OVERFLOW;
    }
    if (!procfs_ready) {
        LOG_ERROR("PROCFS", "Listagem procfs antes da inicializacao");
        return ERR_STATE;
    }
    kmemset(&entries[0], 0, sizeof(entries[0]));
    procfs_copy_text(entries[0].name, sizeof(entries[0].name), "uptime");
    entries[0].type = VFS_NODE_REGULAR;
    entries[0].size = 0U;
    *out_count = 1U;
    return OK;
}

static int procfs_read(file_t* file, void* buffer, uint32_t size,
                       uint32_t* bytes_read) {
    procfs_file_context_t* context;
    uint32_t available;

    if (!file || !file->vnode || !bytes_read) {
        LOG_ERROR("PROCFS", "Argumento nulo na leitura procfs");
        return ERR_NULL;
    }
    *bytes_read = 0U;
    if (!(file->mode & VFS_MODE_READ)) {
        LOG_WARN("PROCFS", "Leitura procfs recusada pelo modo");
        return ERR_UNAVAILABLE;
    }
    if (size && !buffer) {
        LOG_ERROR("PROCFS", "Buffer nulo na leitura procfs");
        return ERR_NULL;
    }
    context = (procfs_file_context_t*)file->vnode->private_data;
    if (!context || !context->snapshot || file->offset > context->snapshot_size) {
        LOG_ERROR("PROCFS", "Snapshot procfs invalido na leitura");
        return ERR_STATE;
    }
    available = context->snapshot_size - file->offset;
    if (size > available) size = available;
    if (size) kmemcpy(buffer, context->snapshot + file->offset, size);
    file->offset += size;
    *bytes_read = size;
    return OK;
}

static int procfs_write(file_t* file, const void* buffer, uint32_t size,
                        uint32_t* bytes_written) {
    (void)file;
    (void)buffer;
    (void)size;
    if (!bytes_written) {
        LOG_ERROR("PROCFS", "Contador nulo na escrita procfs");
        return ERR_NULL;
    }
    *bytes_written = 0U;
    LOG_WARN("PROCFS", "Escrita procfs recusada");
    return ERR_UNAVAILABLE;
}

static int procfs_open(vnode_t* vnode, file_t* file) {
    if (!vnode || !file) {
        LOG_ERROR("PROCFS", "Argumento nulo no open procfs");
        return ERR_NULL;
    }
    return OK;
}

static int procfs_close(file_t* file) {
    procfs_file_context_t* context;

    if (!file || !file->vnode) {
        LOG_ERROR("PROCFS", "Arquivo nulo no fechamento procfs");
        return ERR_NULL;
    }
    context = (procfs_file_context_t*)file->vnode->private_data;
    if (!context) {
        LOG_ERROR("PROCFS", "Contexto nulo no fechamento procfs");
        return ERR_STATE;
    }
    if (context->snapshot) {
        kfree(context->snapshot);
        context->snapshot = 0;
        context->snapshot_size = 0U;
        spinlock_acquire(&procfs_lock);
        if (procfs_active_snapshots) procfs_active_snapshots--;
        spinlock_release(&procfs_lock);
    }
    if (context->mount_acquired) {
        vfs_mount_release(context->mount_slot, context->mount_generation);
        context->mount_acquired = 0U;
    }
    return OK;
}

static int procfs_lseek(file_t* file, int32_t offset, uint32_t whence,
                        uint32_t* position) {
    procfs_file_context_t* context;
    uint32_t base;
    uint32_t target;

    if (!file || !file->vnode || !position) {
        LOG_ERROR("PROCFS", "Argumento nulo no seek procfs");
        return ERR_NULL;
    }
    *position = 0U;
    if (file->mode != VFS_MODE_READ) {
        LOG_WARN("PROCFS", "Seek procfs recusado pelo modo");
        return ERR_UNAVAILABLE;
    }
    context = (procfs_file_context_t*)file->vnode->private_data;
    if (!context || !context->snapshot) {
        LOG_ERROR("PROCFS", "Snapshot procfs invalido no seek");
        return ERR_STATE;
    }
    if (whence == VFS_SEEK_SET) base = 0U;
    else if (whence == VFS_SEEK_CUR) base = file->offset;
    else if (whence == VFS_SEEK_END) base = context->snapshot_size;
    else {
        LOG_WARN("PROCFS", "Origem de seek procfs invalida");
        return ERR_INVALID;
    }
    if (offset < 0) {
        uint32_t magnitude = (uint32_t)(-(offset + 1)) + 1U;
        if (magnitude > base) {
            LOG_WARN("PROCFS", "Seek procfs antes do inicio");
            return ERR_INVALID;
        }
        target = base - magnitude;
    } else {
        if (base > 0xFFFFFFFFU - (uint32_t)offset) {
            LOG_WARN("PROCFS", "Seek procfs excedeu o cursor");
            return ERR_OVERFLOW;
        }
        target = base + (uint32_t)offset;
    }
    if (target > context->snapshot_size) {
        LOG_WARN("PROCFS", "Seek procfs depois do snapshot");
        return ERR_INVALID;
    }
    file->offset = target;
    *position = target;
    return OK;
}

static int procfs_ioctl(file_t* file, uint32_t request, void* argument) {
    (void)file;
    (void)request;
    (void)argument;
    LOG_WARN("PROCFS", "Ioctl procfs nao suportado");
    return ERR_UNAVAILABLE;
}

static int procfs_sync(file_t* file) {
    (void)file;
    LOG_WARN("PROCFS", "Sync procfs nao suportado");
    return ERR_UNAVAILABLE;
}

static int procfs_poll(file_t* file, uint32_t events, uint32_t* revents) {
    (void)events;
    if (!file || !revents) {
        LOG_ERROR("PROCFS", "Argumento nulo no poll procfs");
        return ERR_NULL;
    }
    *revents = (file->mode & VFS_MODE_READ) ? POLLIN : POLLERR;
    return OK;
}

static int procfs_error_callback(char* buffer, uint32_t capacity,
                                 uint32_t* out_len, void* data) {
    (void)buffer;
    (void)capacity;
    (void)data;
    if (out_len) *out_len = 0U;
    LOG_WARN("PROCFS", "Callback de erro usado pelo autoteste");
    return ERR_INVALID;
}

static int procfs_overflow_callback(char* buffer, uint32_t capacity,
                                    uint32_t* out_len, void* data) {
    (void)buffer;
    (void)capacity;
    (void)data;
    if (out_len) *out_len = PROCFS_MAX_SNAPSHOT_SIZE + 1U;
    return OK;
}

static int procfs_compare_snapshot(const uint8_t* first, const uint8_t* second,
                                   uint32_t length) {
    uint32_t index;

    if (!first || !second) return 0;
    for (index = 0U; index < length; index++) {
        if (first[index] != second[index]) return 0;
    }
    return 1;
}

int procfs_validate_state(void) {
    uint32_t active;
    uint32_t entry_count = sizeof(procfs_entries) / sizeof(procfs_entries[0]);

    spinlock_acquire(&procfs_lock);
    active = procfs_active_snapshots;
    if (!procfs_ready || active > VFS_MAX_OPEN_FILES || entry_count != 1U ||
        !procfs_entries[0].name ||
        kstrcmp(procfs_entries[0].name, "uptime") != 0 ||
        procfs_entries[0].mode != VFS_MODE_READ ||
        !procfs_entries[0].read_proc || procfs_entries[0].write_proc) {
        spinlock_release(&procfs_lock);
        LOG_ERROR("PROCFS", "Invariantes procfs invalidas");
        return ERR_STATE;
    }
    spinlock_release(&procfs_lock);
    return OK;
}

int procfs_self_test(procfs_test_result_t* result) {
    vfs_lookup_result_t lookup;
    vfs_dir_entry_t entries[VFS_MAX_DIR_ENTRIES];
    uint8_t first[128];
    uint8_t second[128];
    procfs_file_context_t context;
    proc_entry_t error_entry;
    proc_entry_t overflow_entry;
    uint32_t count = 0U;
    uint32_t bytes = 0U;
    uint32_t position = 0U;
    uint32_t before_active;
    uint8_t close_ok = 1U;
    int32_t fd = VFS_FD_INVALID;
    int result_code;

    if (!result) {
        LOG_ERROR("PROCFS", "Destino nulo no autoteste procfs");
        return ERR_NULL;
    }
    kmemset(result, 0, sizeof(*result));
    kmemset(&context, 0, sizeof(context));
    result->registry = procfs_validate_state() == OK;
    procfs_test_count(result, result->registry);
    result->lookup = vfs_lookup("/proc", &lookup) == OK &&
                     lookup.type == VFS_NODE_DIRECTORY &&
                     vfs_lookup("/proc/uptime", &lookup) == OK &&
                     lookup.type == VFS_NODE_REGULAR &&
                     vfs_lookup("/proc/missing", &lookup) == ERR_NOT_FOUND;
    procfs_test_count(result, result->lookup);
    result->listing = vfs_list_dir("/proc", entries, VFS_MAX_DIR_ENTRIES,
                                   &count) == OK && count == 1U &&
                      kstrcmp(entries[0].name, "uptime") == 0;
    procfs_test_count(result, result->listing);
    spinlock_acquire(&procfs_lock);
    before_active = procfs_active_snapshots;
    spinlock_release(&procfs_lock);
    result_code = vfs_open("/proc/uptime", VFS_MODE_READ, &fd);
    if (result_code == OK) {
        result_code = vfs_read(fd, first, 8U, &bytes);
        result->read = result_code == OK && bytes > 0U;
        result_code = vfs_lseek(fd, 0, VFS_SEEK_SET, &position);
        if (result_code == OK) {
            result_code = vfs_read(fd, second, sizeof(second), &bytes);
        }
        result->read = result->read && result_code == OK && bytes > 0U;
        result->seek_eof = vfs_lseek(fd, 0, VFS_SEEK_END, &position) == OK &&
                           vfs_read(fd, first, sizeof(first), &bytes) == OK &&
                           bytes == 0U &&
                           vfs_lseek(fd, 1, VFS_SEEK_END, &position) ==
                           ERR_INVALID &&
                           vfs_lseek(fd, -1, VFS_SEEK_SET, &position) ==
                           ERR_INVALID;
        if (vfs_lseek(fd, 0, VFS_SEEK_SET, &position) != OK ||
            vfs_read(fd, first, sizeof(first), &bytes) != OK) {
            result->read = 0U;
        }
        result->read = result->read && bytes > 0U;
        if (vfs_lseek(fd, 0, VFS_SEEK_SET, &position) != OK ||
            vfs_read(fd, second, bytes, &count) != OK || count != bytes) {
            result->read = 0U;
        }
        result->read = result->read && procfs_compare_snapshot(first, second,
                                                                bytes);
        if (vfs_close(fd) != OK) close_ok = 0U;
        fd = VFS_FD_INVALID;
    }
    result->read = result->read && result_code == OK;
    procfs_test_count(result, result->read);
    procfs_test_count(result, result->seek_eof);
    result->permissions = vfs_open("/proc/uptime", VFS_MODE_WRITE, &fd) ==
                          ERR_UNAVAILABLE && fd == VFS_FD_INVALID;
    procfs_test_count(result, result->permissions);
    error_entry.name = "error";
    error_entry.mode = VFS_MODE_READ;
    error_entry.read_proc = procfs_error_callback;
    error_entry.write_proc = 0;
    error_entry.data = 0;
    overflow_entry.name = "overflow";
    overflow_entry.mode = VFS_MODE_READ;
    overflow_entry.read_proc = procfs_overflow_callback;
    overflow_entry.write_proc = 0;
    overflow_entry.data = 0;
    result->callback_errors = procfs_render_snapshot(&error_entry, &context) ==
                              ERR_INVALID && context.snapshot == 0 &&
                              procfs_render_snapshot(&overflow_entry, &context) ==
                              ERR_OVERFLOW && context.snapshot == 0;
    procfs_test_count(result, result->callback_errors);
    spinlock_acquire(&procfs_lock);
    result->cleanup = close_ok && procfs_active_snapshots == before_active;
    spinlock_release(&procfs_lock);
    procfs_test_count(result, result->cleanup);
    result->invariants = procfs_validate_state() == OK;
    procfs_test_count(result, result->invariants);
    if (fd != VFS_FD_INVALID) (void)vfs_close(fd);
    return result->passed == result->total ? OK : ERR_STATE;
}
