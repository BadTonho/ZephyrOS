#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/app_api.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/wait.h"
#include "drivers/ata.h"
#include "fs/devfs.h"
#include "fs/fs.h"
#include "fs/procfs.h"
#include "fs/storage.h"
#include "fs/sysfs.h"
#include "fs/vfs.h"
#include "fs/vfs_internal.h"
#include "memory/slab.h"
#include "process/process.h"
#include "process/thread.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_CACHE_COUNT 4U
#define HOST_CACHE_SLOTS 64U
#define HOST_CACHE_OBJECT_SIZE 512U
#define HOST_PAYLOAD_SIZE 16U

struct kmem_cache {
    uint32_t object_size;
    uint8_t used[HOST_CACHE_SLOTS];
    union {
        max_align_t alignment;
        uint8_t bytes[HOST_CACHE_SLOTS][HOST_CACHE_OBJECT_SIZE];
    } storage;
};

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static struct kmem_cache cache_pool[HOST_CACHE_COUNT];
static uint32_t cache_count;
static uint32_t wait_id;
static uint32_t fake_logs;
static uint32_t fake_console_writes;
static uint32_t fake_storage_writes;
static uint32_t fake_syncs;
static uint8_t fake_socket_value;
static ipc_msg_t fake_ipc_message;
static uint8_t fake_ipc_pending;
static process_t current_process;

process_t* processes[MAX_PROCESSES];
uint32_t process_count;

static void __attribute__((no_instrument_function)) coverage_record(
    void* function) {
    uintptr_t address = (uintptr_t)function;

    if (!coverage_active || !address) return;
    for (uint32_t index = 0U; index < coverage_count; index++) {
        if (coverage_addresses[index] == address) return;
    }
    if (coverage_count < HOST_COVERAGE_CAPACITY) {
        coverage_addresses[coverage_count++] = address;
    }
}

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(
    void* function, void* caller) {
    (void)caller;
    coverage_record(function);
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(
    void* function, void* caller) {
    (void)function;
    (void)caller;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:storage:vfs|value=0x%08X\n", coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:storage:vfs|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:storage:vfs|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "vfs-host: falhou: %s\n", expression);
        (void)fflush(stderr);
        __builtin_trap();
    }
}

#define EXPECT(expression) expect_true((expression), #expression)

static void set_text(char* destination, uint32_t capacity,
                     const char* source) {
    if (!destination || !capacity) return;
    if (!source) source = "";
    while (*source && capacity > 1U) {
        *destination++ = *source++;
        capacity--;
    }
    *destination = '\0';
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
    fake_logs++;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
    fake_logs++;
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

int app_api_console_write(const char* text, uint32_t size) {
    if (size && !text) return ERR_NULL;
    fake_console_writes += size;
    return OK;
}

static void cache_reset(struct kmem_cache* cache, uint32_t object_size) {
    kmemset(cache, 0, sizeof(*cache));
    cache->object_size = object_size;
}

kmem_cache_t* kmem_cache_create(const char* name, uint32_t object_size,
                                uint32_t alignment) {
    struct kmem_cache* cache;

    (void)name;
    (void)alignment;
    if (!object_size || object_size > HOST_CACHE_OBJECT_SIZE ||
        cache_count >= HOST_CACHE_COUNT) return 0;
    cache = &cache_pool[cache_count++];
    cache_reset(cache, object_size);
    return cache;
}

void* kmem_cache_alloc(kmem_cache_t* cache) {
    struct kmem_cache* host_cache = (struct kmem_cache*)cache;

    if (!host_cache || !host_cache->object_size) return 0;
    for (uint32_t index = 0U; index < HOST_CACHE_SLOTS; index++) {
        if (!host_cache->used[index]) {
            host_cache->used[index] = 1U;
            kmemset(host_cache->storage.bytes[index], 0,
                    host_cache->object_size);
            return host_cache->storage.bytes[index];
        }
    }
    return 0;
}

void kmem_cache_free(kmem_cache_t* cache, void* object) {
    struct kmem_cache* host_cache = (struct kmem_cache*)cache;

    if (!host_cache || !object) return;
    for (uint32_t index = 0U; index < HOST_CACHE_SLOTS; index++) {
        if (host_cache->storage.bytes[index] == object) {
            host_cache->used[index] = 0U;
            return;
        }
    }
}

int kmem_cache_destroy(kmem_cache_t* cache) {
    struct kmem_cache* host_cache = (struct kmem_cache*)cache;

    if (!host_cache) return ERR_NULL;
    for (uint32_t index = 0U; index < HOST_CACHE_SLOTS; index++) {
        if (host_cache->used[index]) return ERR_STATE;
    }
    cache_reset(host_cache, 0U);
    return OK;
}

int kmem_cache_validate(void) {
    for (uint32_t cache_index = 0U; cache_index < cache_count; cache_index++) {
        struct kmem_cache* cache = &cache_pool[cache_index];

        if (!cache->object_size || cache->object_size > HOST_CACHE_OBJECT_SIZE) {
            return ERR_STATE;
        }
    }
    return OK;
}

int kmem_cache_owns(const kmem_cache_t* cache, const void* object) {
    const struct kmem_cache* host_cache = (const struct kmem_cache*)cache;

    if (!host_cache || !object) return 0;
    for (uint32_t index = 0U; index < HOST_CACHE_SLOTS; index++) {
        if (host_cache->used[index] &&
            host_cache->storage.bytes[index] == object) return 1;
    }
    return 0;
}

void* kmalloc(uint32_t size) { return size ? malloc(size) : 0; }

void kfree(void* pointer) { free(pointer); }

void kmem_cache_get_stats(kmem_slab_stats_t* stats) {
    if (stats) kmemset(stats, 0, sizeof(*stats));
}

int wait_channel_init(wait_channel_t* channel, const char* owner) {
    if (!channel) return ERR_NULL;
    kmemset(channel, 0, sizeof(*channel));
    set_text(channel->owner, sizeof(channel->owner), owner);
    channel->id = ++wait_id;
    channel->initialized = 1U;
    channel->available = 1U;
    return OK;
}

int wait_channel_reset(wait_channel_t* channel) {
    if (!channel) return ERR_NULL;
    kmemset(channel, 0, sizeof(*channel));
    return OK;
}

int wait_channel_signal(wait_channel_t* channel) {
    if (!channel || !channel->initialized) return ERR_STATE;
    channel->condition++;
    return OK;
}

int wait_channel_set_available(wait_channel_t* channel, uint8_t available) {
    if (!channel || !channel->initialized) return ERR_STATE;
    channel->available = available ? 1U : 0U;
    return OK;
}

int wait_channel_get_condition(const wait_channel_t* channel,
                              uint32_t* out_condition) {
    if (!channel || !out_condition) return ERR_NULL;
    *out_condition = channel->condition;
    return OK;
}

int wait_channel_is_available(const wait_channel_t* channel,
                              uint8_t* out_available) {
    if (!channel || !out_available) return ERR_NULL;
    *out_available = channel->available;
    return OK;
}

int wait_event(wait_queue_head_t* queue, wait_condition_fn_t condition,
               void* context, wait_reason_t* out_reason) {
    uint8_t ready = 0U;

    if (!queue || !condition || !out_reason) return ERR_NULL;
    if (condition(context, &ready) != OK) return ERR_STATE;
    *out_reason = ready ? WAIT_REASON_EVENT : WAIT_REASON_TIMEOUT;
    return OK;
}

int wait_event_timeout(wait_queue_head_t* queue,
                       wait_condition_fn_t condition, void* context,
                       uint32_t timeout_ticks, wait_reason_t* out_reason) {
    (void)timeout_ticks;
    return wait_event(queue, condition, context, out_reason);
}

int wake_up_all(wait_queue_head_t* queue, uint32_t* out_woken) {
    if (!queue || !out_woken) return ERR_NULL;
    *out_woken = queue->waiters;
    queue->waiters = 0U;
    return OK;
}

int wake_up(wait_queue_head_t* queue, uint32_t* out_woken) {
    return wake_up_all(queue, out_woken);
}

int wait_validate_state(void) { return OK; }

int wait_get_stats(wait_stats_t* stats) {
    if (!stats) return ERR_NULL;
    kmemset(stats, 0, sizeof(*stats));
    stats->initialized = 1U;
    return OK;
}

int wait_deadline_remaining(uint32_t deadline_tick, uint32_t* out_remaining) {
    if (!out_remaining) return ERR_NULL;
    *out_remaining = deadline_tick;
    return OK;
}

int wait_deadline_remaining_active(uint32_t deadline_tick, uint8_t active,
                                   uint32_t* out_remaining) {
    if (!out_remaining) return ERR_NULL;
    *out_remaining = active ? deadline_tick : 0U;
    return OK;
}

process_t* process_get_current(void) { return &current_process; }

process_t* process_get_by_pid(uint32_t pid) {
    return pid == current_process.pid ? &current_process : 0;
}

thread_t* thread_get_current(void) { return 0; }

int ipc_current_has_pending(void) { return fake_ipc_pending ? 1 : 0; }

int ipc_receive(ipc_msg_t* message) {
    if (!message || !fake_ipc_pending) return 0;
    *message = fake_ipc_message;
    fake_ipc_pending = 0U;
    return 1;
}

int ipc_wait(uint32_t timeout_ticks, wait_reason_t* out_reason) {
    (void)timeout_ticks;
    if (!out_reason) return ERR_NULL;
    *out_reason = WAIT_REASON_CANCELLED;
    return OK;
}

int process_wait(wait_channel_t* channel, uint32_t observed_condition,
                 uint32_t timeout_ticks, wait_reason_t* out_reason) {
    (void)channel;
    (void)observed_condition;
    (void)timeout_ticks;
    if (!out_reason) return ERR_NULL;
    *out_reason = WAIT_REASON_TIMEOUT;
    return OK;
}

int process_wake_channel(wait_channel_t* channel, wait_wake_mode_t mode,
                         wait_reason_t reason, uint32_t* out_woken) {
    (void)mode;
    (void)reason;
    return wake_up_all(channel, out_woken);
}

void process_yield(void) {}

int storage_read_file_range(const char* id, const char* path,
                            uint32_t offset, uint8_t* buffer,
                            uint32_t max_size, uint32_t* out_read) {
    static const uint8_t payload[] = "vfs fixture data";
    uint32_t available;
    uint32_t amount;

    if (!id || !path || !out_read || (max_size && !buffer)) return ERR_NULL;
    if (kstrcmp(id, "system") != 0 || kstrcmp(path, "fixture") != 0) {
        return ERR_NOT_FOUND;
    }
    if (offset >= sizeof(payload) - 1U) {
        *out_read = 0U;
        return OK;
    }
    available = (uint32_t)(sizeof(payload) - 1U) - offset;
    amount = available < max_size ? available : max_size;
    if (amount) kmemcpy(buffer, payload + offset, amount);
    *out_read = amount;
    return OK;
}

int storage_write_file(const char* id, const char* path,
                       const uint8_t* data, uint32_t size,
                       uint8_t attributes) {
    if (!id || !path || (size && !data)) return ERR_NULL;
    (void)attributes;
    fake_storage_writes++;
    return kstrcmp(id, "system") == 0 && kstrcmp(path, "fixture") == 0 ?
           OK : ERR_NOT_FOUND;
}

int storage_sync_volume(const char* id) {
    if (!id) return ERR_NULL;
    fake_syncs++;
    return OK;
}

int storage_sync_all(void) {
    fake_syncs++;
    return OK;
}

int storage_get_path_info(const char* id, const char* path,
                          uint32_t* out_size, uint8_t* out_attributes,
                          uint8_t* out_is_directory) {
    if (!id || !path || !out_size || !out_attributes || !out_is_directory) {
        return ERR_NULL;
    }
    *out_size = 0U;
    *out_attributes = FS_ATTRIBUTE_ARCHIVE;
    *out_is_directory = 0U;
    return ERR_NOT_FOUND;
}

int storage_atomic_write_file(const char* id, const char* path,
                              const uint8_t* data, uint32_t size,
                              uint8_t attributes, storage_atomic_mode_t mode) {
    if (!id || !path || (size && !data)) return ERR_NULL;
    if (mode != STORAGE_ATOMIC_CREATE_OR_REPLACE &&
        mode != STORAGE_ATOMIC_REPLACE_ONLY) return ERR_INVALID;
    (void)attributes;
    return OK;
}

int storage_get_status(storage_status_t* status) {
    if (!status) return ERR_NULL;
    kmemset(status, 0, sizeof(*status));
    status->initialized = 1U;
    return OK;
}

static void lookup_init(vfs_lookup_result_t* result, const char* path) {
    kmemset(result, 0, sizeof(*result));
    set_text(result->canonical_path, sizeof(result->canonical_path), path);
    result->mount_slot = 0U;
    result->mount_generation = 1U;
    result->mount_kind = VFS_MOUNT_STORAGE;
    result->fs_type = STORAGE_FS_FAT32;
    set_text(result->mount_point, sizeof(result->mount_point), "/");
    set_text(result->volume_id, sizeof(result->volume_id), "system");
    set_text(result->relative_path, sizeof(result->relative_path), "fixture");
}

int vfs_path_init(void) { return OK; }

int vfs_path_validate_state(void) { return OK; }

void vfs_path_get_metrics(uint32_t* capacity, uint32_t* active,
                          uint32_t* lookups, uint32_t* chdirs) {
    if (capacity) *capacity = VFS_MAX_MOUNTS;
    if (active) *active = 1U;
    if (lookups) *lookups = 0U;
    if (chdirs) *chdirs = 0U;
}

int vfs_resolve_open_path(const char* path, uint32_t mode,
                          vfs_lookup_result_t* result) {
    if (!path || !result) return ERR_NULL;
    if (kstrcmp(path, "fixture") == 0 || kstrcmp(path, "/fixture") == 0) {
        lookup_init(result, "/fixture");
        result->type = VFS_NODE_REGULAR;
        result->size = 16U;
        return OK;
    }
    if (kstrcmp(path, "/readonly") == 0) {
        lookup_init(result, "/readonly");
        result->type = VFS_NODE_REGULAR;
        result->size = 4U;
        result->read_only = 1U;
        return OK;
    }
    if (kstrcmp(path, "/directory") == 0) {
        lookup_init(result, "/directory");
        result->type = VFS_NODE_DIRECTORY;
        return OK;
    }
    if (kstrcmp(path, "/dev/null") == 0) {
        lookup_init(result, "/dev/null");
        result->mount_kind = VFS_MOUNT_DEVFS;
        result->fs_type = STORAGE_FS_NONE;
        result->type = VFS_NODE_CHAR_DEVICE;
        set_text(result->volume_id, sizeof(result->volume_id), "devfs");
        return OK;
    }
    (void)mode;
    return ERR_NOT_FOUND;
}

int vfs_lookup(const char* path, vfs_lookup_result_t* result) {
    (void)path;
    (void)result;
    return ERR_NOT_FOUND;
}

int vfs_getcwd(char* path, uint32_t capacity) {
    (void)path;
    (void)capacity;
    return ERR_NOT_FOUND;
}

int vfs_fd_table_inherit_cwd(vfs_fd_table_t* table,
                             const vfs_fd_table_t* parent) {
    if (!table || !table->initialized) return ERR_STATE;
    set_text(table->cwd, sizeof(table->cwd),
             parent && parent->initialized && parent->cwd[0] ?
             parent->cwd : "/");
    return OK;
}

int vfs_chdir(const char* path) {
    if (!path) return ERR_NULL;
    set_text(current_process.fd_table.cwd,
             sizeof(current_process.fd_table.cwd), path);
    return OK;
}

int vfs_unmount_volume(const char* volume_id) {
    (void)volume_id;
    return ERR_STATE;
}

int vfs_list_dir(const char* path, vfs_dir_entry_t* entries,
                 uint32_t capacity, uint32_t* out_count) {
    if (!path || !entries || !out_count) return ERR_NULL;
    if (!capacity) return ERR_OVERFLOW;
    *out_count = 0U;
    return ERR_NOT_FOUND;
}

int vfs_mount_acquire(uint32_t slot, uint32_t generation) {
    return slot == 0U && generation == 1U ? OK : ERR_STATE;
}

void vfs_mount_release(uint32_t slot, uint32_t generation) {
    (void)slot;
    (void)generation;
}

int vfs_mount_validate_reference(uint32_t slot, uint32_t generation) {
    return slot == 0U && generation == 1U ? OK : ERR_STATE;
}

static int fake_device_open(vnode_t* vnode, file_t* file) {
    return vnode && file ? OK : ERR_NULL;
}

static int fake_device_read(file_t* file, void* buffer, uint32_t size,
                            uint32_t* bytes_read) {
    if (!file || !bytes_read || (size && !buffer)) return ERR_NULL;
    *bytes_read = 0U;
    return OK;
}

static int fake_device_write(file_t* file, const void* buffer, uint32_t size,
                             uint32_t* bytes_written) {
    if (!file || !bytes_written || (size && !buffer)) return ERR_NULL;
    *bytes_written = size;
    return OK;
}

static int fake_device_close(file_t* file) { return file ? OK : ERR_NULL; }

static int fake_device_lseek(file_t* file, int32_t offset, uint32_t whence,
                             uint32_t* position) {
    (void)offset;
    (void)whence;
    if (!file || !position) return ERR_NULL;
    *position = 0U;
    return ERR_UNAVAILABLE;
}

static int fake_device_ioctl(file_t* file, uint32_t request, void* argument) {
    (void)request;
    (void)argument;
    return file ? ERR_UNAVAILABLE : ERR_NULL;
}

static int fake_device_sync(file_t* file) { return file ? OK : ERR_NULL; }

static int fake_device_poll(file_t* file, uint32_t events,
                            uint32_t* revents) {
    (void)events;
    if (!file || !revents) return ERR_NULL;
    *revents = POLLOUT;
    return OK;
}

static const file_operations_t fake_device_operations = {
    fake_device_open, fake_device_read, fake_device_write,
    fake_device_close, fake_device_lseek, fake_device_ioctl,
    fake_device_sync, fake_device_poll
};

int devfs_init(void) { return OK; }
int procfs_init(void) { return OK; }
int sysfs_init(void) { return OK; }
int devfs_validate_state(void) { return OK; }
int procfs_validate_state(void) { return OK; }
int sysfs_validate_state(void) { return OK; }

int devfs_open_file(const vfs_lookup_result_t* lookup, uint32_t mode,
                    vnode_t* vnode, file_t* file,
                    devfs_file_context_t* context) {
    if (!lookup || !vnode || !file || !context) return ERR_NULL;
    kmemset(context, 0, sizeof(*context));
    context->node_id = DEVFS_NODE_NULL;
    context->mount_slot = lookup->mount_slot;
    context->mount_generation = lookup->mount_generation;
    vnode->type = lookup->type;
    vnode->operations = &fake_device_operations;
    vnode->private_data = context;
    set_text(vnode->path, sizeof(vnode->path), lookup->canonical_path);
    file->mode = mode;
    return fake_device_operations.open(vnode, file);
}

int devfs_get_status(devfs_status_t* status) {
    if (!status) return ERR_NULL;
    kmemset(status, 0, sizeof(*status));
    status->initialized = 1U;
    status->capacity = DEVFS_MAX_NODES;
    status->active = DEVFS_MAX_NODES;
    return OK;
}

int devfs_self_test(devfs_test_result_t* result) {
    if (!result) return ERR_NULL;
    kmemset(result, 0, sizeof(*result));
    result->passed = 1U;
    result->total = 1U;
    return OK;
}

int devfs_list(vfs_dir_entry_t* entries, uint32_t capacity,
               uint32_t* out_count) {
    if (!entries || !out_count) return ERR_NULL;
    if (capacity < DEVFS_MAX_NODES) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < DEVFS_MAX_NODES; index++) {
        kmemset(&entries[index], 0, sizeof(entries[index]));
        entries[index].type = VFS_NODE_CHAR_DEVICE;
    }
    *out_count = DEVFS_MAX_NODES;
    return OK;
}

int procfs_self_test(procfs_test_result_t* result) {
    if (!result) return ERR_NULL;
    kmemset(result, 0, sizeof(*result));
    result->passed = 1U;
    result->total = 1U;
    return OK;
}

int sysfs_self_test(sysfs_test_result_t* result) {
    if (!result) return ERR_NULL;
    kmemset(result, 0, sizeof(*result));
    result->passed = 1U;
    result->total = 1U;
    return OK;
}

static int fake_proc_open(const vfs_lookup_result_t* lookup, uint32_t mode,
                          vnode_t* vnode, file_t* file,
                          procfs_file_context_t* context) {
    (void)lookup;
    (void)mode;
    (void)vnode;
    (void)file;
    (void)context;
    return ERR_NOT_FOUND;
}

static int fake_sys_open(const vfs_lookup_result_t* lookup, uint32_t mode,
                         vnode_t* vnode, file_t* file,
                         sysfs_file_context_t* context) {
    (void)lookup;
    (void)mode;
    (void)vnode;
    (void)file;
    (void)context;
    return ERR_NOT_FOUND;
}

int procfs_open_file(const vfs_lookup_result_t* lookup, uint32_t mode,
                     vnode_t* vnode, file_t* file,
                     procfs_file_context_t* context) {
    return fake_proc_open(lookup, mode, vnode, file, context);
}

int sysfs_open_file(const vfs_lookup_result_t* lookup, uint32_t mode,
                    vnode_t* vnode, file_t* file,
                    sysfs_file_context_t* context) {
    return fake_sys_open(lookup, mode, vnode, file, context);
}

int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    (void)lba;
    (void)count;
    (void)buffer;
    return ERR_UNAVAILABLE;
}

static int fake_socket_open(vnode_t* vnode, file_t* file) {
    return vnode && file ? OK : ERR_NULL;
}

static int fake_socket_read(file_t* file, void* buffer, uint32_t size,
                            uint32_t* bytes_read) {
    if (!file || !bytes_read || (size && !buffer)) return ERR_NULL;
    if (size) ((uint8_t*)buffer)[0] = fake_socket_value;
    *bytes_read = size ? 1U : 0U;
    return OK;
}

static int fake_socket_write(file_t* file, const void* buffer, uint32_t size,
                             uint32_t* bytes_written) {
    if (!file || !bytes_written || (size && !buffer)) return ERR_NULL;
    *bytes_written = size;
    return OK;
}

static int fake_socket_close(file_t* file) { return file ? OK : ERR_NULL; }

static int fake_socket_lseek(file_t* file, int32_t offset, uint32_t whence,
                             uint32_t* position) {
    (void)offset;
    (void)whence;
    if (!file || !position) return ERR_NULL;
    *position = 0U;
    return OK;
}

static int fake_socket_sync(file_t* file) { return file ? OK : ERR_NULL; }

static const file_operations_t fake_socket_operations = {
    fake_socket_open, fake_socket_read, fake_socket_write,
    fake_socket_close, fake_socket_lseek, fake_device_ioctl,
    fake_socket_sync, fake_device_poll
};

static const file_operations_t fake_socket_default_operations = {
    fake_socket_open, fake_socket_read, fake_socket_write,
    fake_socket_close, fake_socket_lseek, fake_device_ioctl,
    fake_socket_sync, 0
};

int main(void) {
    static const uint8_t payload[HOST_PAYLOAD_SIZE] = "vfs payload";
    uint8_t buffer[HOST_PAYLOAD_SIZE];
    vfs_fd_table_t inherited = {0};
    vfs_status_t status;
    vfs_descriptor_info_t descriptors[VFS_MAX_FDS];
    vfs_lookup_result_t lookup;
    pollfd_t poll_fds[2];
    fd_set_t read_set;
    fd_set_t write_set;
    uint32_t bytes;
    uint32_t ready;
    uint32_t descriptor_count;
    uint32_t position;
    int32_t fd;
    int32_t pipe_fds[2];
    int32_t socket_fd;
    uint8_t full_pipe_payload[VFS_PIPE_BUFFER_SIZE];
    char cwd[VFS_MAX_PATH];

    kmemset(&current_process, 0, sizeof(current_process));
    current_process.pid = 42U;
    processes[0] = &current_process;
    process_count = 1U;
    coverage_active = 1U;

    EXPECT(vfs_fd_table_init(0) == ERR_NULL);
    EXPECT(vfs_init() == OK);
    EXPECT(vfs_is_ready());
    EXPECT(vfs_fd_table_init(&current_process.fd_table) == OK);
    EXPECT(vfs_fd_table_init(&current_process.fd_table) == ERR_STATE);
    EXPECT(vfs_fd_table_inherit_cwd(&inherited, &current_process.fd_table) ==
           ERR_STATE);
    EXPECT(vfs_fd_table_init(&inherited) == OK);
    EXPECT(vfs_fd_table_inherit_cwd(&inherited, &current_process.fd_table) ==
           OK);
    EXPECT(kstrcmp(inherited.cwd, "/") == 0);

    EXPECT(vfs_open(0, VFS_MODE_READ, &fd) == ERR_NULL);
    EXPECT(vfs_open("fixture", 0U, &fd) == ERR_INVALID);
    EXPECT(vfs_open("/directory", VFS_MODE_READ, &fd) == ERR_INVALID);
    EXPECT(vfs_open("fixture", VFS_MODE_READ, &fd) == OK);
    EXPECT(fd >= VFS_FD_FIRST_FILE);
    EXPECT(vfs_read(fd, buffer, sizeof(buffer), &bytes) == OK);
    EXPECT(bytes == sizeof("vfs fixture data") - 1U);
    EXPECT(memcmp(buffer, "vfs fixture data", bytes) == 0);
    EXPECT(vfs_lseek(fd, 0, VFS_SEEK_SET, &position) == OK);
    EXPECT(position == 0U);
    EXPECT(vfs_lseek(fd, -1, VFS_SEEK_SET, &position) == ERR_INVALID);
    EXPECT(vfs_write(fd, payload, 1U, &bytes) == ERR_UNAVAILABLE);
    EXPECT(vfs_poll(&(pollfd_t){fd, POLLIN, 0U}, 1U, 0U, &ready) == OK);
    EXPECT(ready == 1U);
    EXPECT(vfs_fsync(fd) == OK);
    EXPECT(vfs_close(fd) == OK);
    EXPECT(vfs_close(fd) == ERR_INVALID);
    EXPECT(vfs_read(fd, buffer, 1U, &bytes) == ERR_INVALID);

    EXPECT(vfs_open("/readonly", VFS_MODE_READ_WRITE, &fd) == OK);
    EXPECT(vfs_write(fd, payload, 1U, &bytes) == ERR_UNAVAILABLE);
    EXPECT(vfs_close(fd) == OK);
    EXPECT(vfs_open("/dev/null", VFS_MODE_READ_WRITE, &fd) == OK);
    EXPECT(vfs_read(fd, buffer, 1U, &bytes) == OK && bytes == 0U);
    EXPECT(vfs_write(fd, payload, 1U, &bytes) == OK && bytes == 1U);
    EXPECT(vfs_ioctl(fd, 0U, 0) == ERR_UNAVAILABLE);
    EXPECT(vfs_close(fd) == OK);
    EXPECT(vfs_close(VFS_FD_STDIN) == ERR_UNAVAILABLE);
    EXPECT(vfs_write(VFS_FD_STDOUT, payload, 2U, &bytes) == OK);
    EXPECT(bytes == 2U && fake_console_writes == 2U);
    fake_ipc_message.type = IPC_MSG_KEYBOARD;
    fake_ipc_message.data1 = 'K';
    fake_ipc_message.data2 = 0U;
    fake_ipc_pending = 1U;
    EXPECT(vfs_read(VFS_FD_STDIN, buffer, 1U, &bytes) == OK);
    EXPECT(bytes == 1U && buffer[0] == 'K');
    EXPECT(vfs_read(VFS_FD_STDIN, buffer, 1U, &bytes) == OK);
    EXPECT(bytes == 0U);
    EXPECT(vfs_lseek(VFS_FD_STDOUT, 0, VFS_SEEK_SET, &position) ==
           ERR_UNAVAILABLE);
    EXPECT(vfs_ioctl(VFS_FD_STDOUT, 0U, 0) == ERR_UNAVAILABLE);
    EXPECT(vfs_fsync(VFS_FD_STDIN) == ERR_UNAVAILABLE);

    pipe_fds[0] = VFS_FD_INVALID;
    pipe_fds[1] = VFS_FD_INVALID;
    EXPECT(vfs_pipe(0) == ERR_NULL);
    EXPECT(vfs_pipe(pipe_fds) == OK);
    EXPECT(vfs_write(pipe_fds[1], payload, sizeof(payload), &bytes) == OK);
    EXPECT(bytes == sizeof(payload));
    EXPECT(vfs_poll(&(pollfd_t){pipe_fds[0], POLLIN, 0U}, 1U, 0U, &ready) ==
           OK && ready == 1U);
    EXPECT(vfs_lseek(pipe_fds[0], 0, VFS_SEEK_SET, &position) ==
           ERR_UNAVAILABLE);
    EXPECT(vfs_close(pipe_fds[1]) == OK);
    EXPECT(vfs_read(pipe_fds[0], buffer, sizeof(buffer), &bytes) == OK);
    EXPECT(bytes == sizeof(payload));
    EXPECT(memcmp(buffer, payload, sizeof(payload)) == 0);
    EXPECT(vfs_read(pipe_fds[0], buffer, sizeof(buffer), &bytes) == OK &&
           bytes == 0U);
    EXPECT(vfs_close(pipe_fds[0]) == OK);

    pipe_fds[0] = VFS_FD_INVALID;
    pipe_fds[1] = VFS_FD_INVALID;
    EXPECT(vfs_pipe(pipe_fds) == OK);
    EXPECT(vfs_read(pipe_fds[0], buffer, 1U, &bytes) == ERR_STATE);
    EXPECT(vfs_close(pipe_fds[0]) == OK);
    EXPECT(vfs_close(pipe_fds[1]) == OK);

    kmemset(full_pipe_payload, 'F', sizeof(full_pipe_payload));
    pipe_fds[0] = VFS_FD_INVALID;
    pipe_fds[1] = VFS_FD_INVALID;
    EXPECT(vfs_pipe(pipe_fds) == OK);
    EXPECT(vfs_write(pipe_fds[1], full_pipe_payload,
                     sizeof(full_pipe_payload), &bytes) == OK);
    EXPECT(bytes == VFS_PIPE_BUFFER_SIZE);
    EXPECT(vfs_write(pipe_fds[1], payload, 1U, &bytes) == ERR_STATE);
    EXPECT(bytes == 0U);
    EXPECT(vfs_close(pipe_fds[1]) == OK);
    EXPECT(vfs_close(pipe_fds[0]) == OK);

    socket_fd = VFS_FD_INVALID;
    EXPECT(vfs_open_socket(0, &fake_socket_operations, VFS_MODE_READ,
                           "socket", &socket_fd) == ERR_NULL);
    EXPECT(vfs_open_socket(&fake_socket_value, &fake_socket_operations,
                           VFS_MODE_READ, "socket", &socket_fd) == OK);
    EXPECT(vfs_read(socket_fd, buffer, 1U, &bytes) == OK && bytes == 1U);
    EXPECT(buffer[0] == fake_socket_value);
    EXPECT(vfs_copy_descriptors(descriptors, VFS_MAX_FDS, &descriptor_count) ==
           OK);
    EXPECT(descriptor_count >= 4U);
    EXPECT(vfs_copy_descriptors(descriptors, 1U, &descriptor_count) ==
           ERR_OVERFLOW);
    EXPECT(vfs_close(socket_fd) == OK);
    socket_fd = VFS_FD_INVALID;
    EXPECT(vfs_open_socket(&fake_socket_value,
                           &fake_socket_default_operations, VFS_MODE_READ,
                           "socket-default", &socket_fd) == OK);
    poll_fds[0].fd = socket_fd;
    poll_fds[0].events = POLLIN;
    poll_fds[0].revents = 0U;
    EXPECT(vfs_poll(poll_fds, 1U, 0U, &ready) == OK);
    EXPECT(ready == 1U && (poll_fds[0].revents & POLLERR));
    EXPECT(vfs_close(socket_fd) == OK);

    read_set.bits = 0U;
    write_set.bits = 0U;
    write_set.bits |= 1U << VFS_FD_STDOUT;
    poll_fds[0].fd = VFS_FD_STDOUT;
    poll_fds[0].events = POLLOUT;
    poll_fds[0].revents = 0U;
    EXPECT(vfs_select(VFS_MAX_FDS + 1U, &read_set, &write_set, 0, 0U,
                      &ready) == ERR_OVERFLOW);
    EXPECT(vfs_select(VFS_MAX_FDS, &read_set, &write_set, 0, 0U, &ready) == OK);
    EXPECT(ready >= 1U);
    EXPECT(vfs_poll(poll_fds, 1U, 0U, &ready) == OK);
    EXPECT(poll_fds[0].revents & POLLOUT);
    poll_fds[0].events = 0x8000U;
    EXPECT(vfs_poll(poll_fds, 1U, 0U, &ready) == ERR_INVALID);
    EXPECT(vfs_poll(poll_fds, POLL_MAX_FDS + 1U, 0U, &ready) == ERR_OVERFLOW);

    EXPECT(vfs_lookup("fixture", &lookup) == ERR_NOT_FOUND);
    EXPECT(vfs_getcwd(cwd, sizeof(cwd)) == ERR_NOT_FOUND);
    EXPECT(vfs_sync() == OK);
    EXPECT(vfs_power_set_quiescing(1U) == OK);
    EXPECT(vfs_power_is_quiescing());
    EXPECT(vfs_open("fixture", VFS_MODE_READ, &fd) == ERR_UNAVAILABLE);
    EXPECT(vfs_sync() == ERR_UNAVAILABLE);
    EXPECT(vfs_power_set_quiescing(0U) == OK);
    EXPECT(!vfs_power_is_quiescing());
    EXPECT(vfs_lookup(0, &lookup) == ERR_NOT_FOUND);
    EXPECT(vfs_write_redirect(0, payload, 1U, 0U) == ERR_NULL);
    EXPECT(vfs_write_redirect("fixture", payload, 1U, 0U) == OK);
    EXPECT(vfs_write_redirect("fixture", payload, 1U, 1U) == OK);
    EXPECT(vfs_write_redirect("fixture", payload, 1U, 2U) == ERR_INVALID);
    EXPECT(vfs_write_redirect("/readonly", payload, 1U, 0U) ==
           ERR_UNAVAILABLE);
    EXPECT(vfs_write_redirect("/directory", payload, 1U, 0U) == ERR_INVALID);
    EXPECT(vfs_write_redirect("fixture", payload, VFS_REDIRECT_MAX_SIZE + 1U,
                              0U) == ERR_OVERFLOW);

    EXPECT(vfs_get_status(&status) == OK);
    EXPECT(status.initialized && status.descriptor_capacity == VFS_MAX_FDS);
    EXPECT(status.global_files_used == 0U);
    EXPECT(vfs_validate_state() == OK);
    EXPECT(vfs_close_owner(0U) == ERR_INVALID);
    EXPECT(vfs_close_owner(999U) == ERR_NOT_FOUND);
    EXPECT(vfs_fd_table_release(&inherited) == OK);
    EXPECT(vfs_fd_table_release(&current_process.fd_table) == OK);
    EXPECT(!current_process.fd_table.initialized);

    EXPECT(fake_storage_writes == 0U);
    EXPECT(fake_syncs > 0U);
    EXPECT(fake_logs > 0U);
    coverage_active = 0U;
    coverage_emit(0);
    puts("vfs-host: PASS");
    return 0;
}
