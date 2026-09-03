#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/timer.h"
#include "fs/block_cache.h"
#include "fs/procfs.h"
#include "fs/vfs.h"
#include "memory/slab.h"
#include "process/process.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_ALLOC_SLOTS 16U
#define HOST_PROCESS_COUNT 6U
#define HOST_FD_COUNT 8U
#define HOST_GLOBAL_COUNT 5U
#define HOST_ALLOC_SLOT_SIZE (64U * 1024U)

typedef union {
    uint64_t alignment;
    uint8_t bytes[HOST_ALLOC_SLOT_SIZE];
} host_alloc_slot_t;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static host_alloc_slot_t allocation_slots[HOST_ALLOC_SLOTS];
static uint8_t allocation_used[HOST_ALLOC_SLOTS];
static process_t base_processes[HOST_PROCESS_COUNT];
static process_t temporary_process;
static uint8_t temporary_active;
static uint32_t next_generation = 100U;
static process_t* current_process;
static file_t fake_files[HOST_FD_COUNT];
static vnode_t fake_vnodes[HOST_FD_COUNT];
static procfs_file_context_t fake_contexts[HOST_FD_COUNT];
static uint8_t fake_fd_used[HOST_FD_COUNT];
static log_level_t fake_console_level = LOG_LEVEL_INFO;
static log_level_t fake_buffer_level = LOG_LEVEL_INFO;
static uint32_t fake_ticks = 640U;
static uint32_t fake_frequency = 100U;
static jmp_buf process_entry_jump;
static uint8_t process_entry_active;

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
    printf("ZCOV_BEGIN|case=host:storage:procfs|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:storage:procfs|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:storage:procfs|value=0x%08X\n",
           (uint32_t)result);
}

static int expect_true(int condition, const char* expression) {
    if (condition) return OK;
    fprintf(stderr, "procfs-host: falhou: %s\n", expression);
    return ERR_STATE;
}

#define EXPECT(expression) \
    do { \
        if (expect_true((expression), #expression) != OK) failures++; \
    } while (0)

static void host_copy_text(char* destination, uint32_t capacity,
                           const char* source) {
    uint32_t index = 0U;

    if (!destination || !capacity) return;
    if (!source) source = "";
    while (source[index] && index + 1U < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int host_contains(const uint8_t* buffer, uint32_t length,
                         const char* text) {
    uint32_t text_length = kstrlen(text);

    if (!buffer || !text) return 0;
    if (text_length > length) return 0;
    for (uint32_t offset = 0U; offset + text_length <= length; offset++) {
        uint32_t index = 0U;

        while (index < text_length &&
               buffer[offset + index] == (uint8_t)text[index]) index++;
        if (index == text_length) return 1;
    }
    return 0;
}

static process_t* host_process_by_pid(uint32_t pid) {
    if (pid < HOST_PROCESS_COUNT) return &base_processes[pid];
    if (temporary_active && pid == temporary_process.pid) {
        return &temporary_process;
    }
    return 0;
}

static void host_snapshot_from_process(const process_t* process,
                                       process_snapshot_t* output) {
    kmemset(output, 0, sizeof(*output));
    output->pid = process->pid;
    output->generation = process->event_generation;
    host_copy_text(output->name, sizeof(output->name), process->name);
    output->state = process->state;
    output->parent_pid = process->parent_pid;
    output->threads = process->vma_count ? 2U : 1U;
    output->total_ticks = process->total_ticks;
    output->wait_ticks = process->wait_ticks;
    output->memory_bytes = process->vma_count * PAGE_SIZE;
    output->resident_pages = process->vma_count;
    output->image_bytes = process->user_code_size;
    output->exit_code = process->exit_code;
    output->faulted = process->faulted;
    output->user_mode = process->context.user_mode;
    output->vma_count = process->vma_count;
    host_copy_text(output->user_launch.raw_args,
                   sizeof(output->user_launch.raw_args),
                   process->user_launch.raw_args);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
}

void log_set_level(log_level_t level) {
    fake_console_level = level;
    fake_buffer_level = level;
}

log_level_t log_get_level(void) { return fake_buffer_level; }

int log_set_buffer_level(log_level_t level) {
    if (level > LOG_LEVEL_DEBUG) return ERR_INVALID;
    if (level < fake_console_level) return ERR_INVALID;
    fake_buffer_level = level;
    return OK;
}

int log_set_console_level(log_level_t level) {
    if (level > LOG_LEVEL_DEBUG) return ERR_INVALID;
    if (level > fake_buffer_level) return ERR_INVALID;
    fake_console_level = level;
    return OK;
}

log_level_t log_get_buffer_level(void) { return fake_buffer_level; }
log_level_t log_get_console_level(void) { return fake_console_level; }

uint32_t timer_get_ticks(void) { return fake_ticks; }
uint32_t timer_get_frequency(void) { return fake_frequency; }

uint32_t memory_get_total(void) { return 32U * 1024U * 1024U; }
uint32_t memory_get_free(void) { return 24U * 1024U * 1024U; }
uint32_t memory_get_used(void) { return 8U * 1024U * 1024U; }

void kmem_cache_get_stats(kmem_slab_stats_t* stats) {
    kmemset(stats, 0, sizeof(*stats));
    stats->caches = 2U;
    stats->slabs = 3U;
    stats->pages = 3U;
    stats->active_objects = 5U;
    stats->capacity = 24U;
    stats->initialized = 1U;
    stats->valid = 1U;
}

int block_cache_get_stats(block_cache_stats_t* stats) {
    if (!stats) return ERR_NULL;
    kmemset(stats, 0, sizeof(*stats));
    stats->capacity = BLOCK_CACHE_CAPACITY;
    stats->block_size = BLOCK_CACHE_BLOCK_SIZE;
    stats->memory_bytes = BLOCK_CACHE_CAPACITY * BLOCK_CACHE_BLOCK_SIZE;
    stats->entries = 2U;
    stats->valid_entries = 2U;
    stats->durability_state = BLOCK_DURABILITY_READY;
    return OK;
}

void* kmalloc(uint32_t size) {
    if (!size || size > HOST_ALLOC_SLOT_SIZE) return 0;
    for (uint32_t index = 0U; index < HOST_ALLOC_SLOTS; index++) {
        if (!allocation_used[index]) {
            allocation_used[index] = 1U;
            return allocation_slots[index].bytes;
        }
    }
    return 0;
}

void kfree(void* pointer) {
    if (!pointer) return;
    for (uint32_t index = 0U; index < HOST_ALLOC_SLOTS; index++) {
        if (pointer == allocation_slots[index].bytes) {
            allocation_used[index] = 0U;
            return;
        }
    }
}

process_t* process_get_current(void) { return current_process; }

int process_is_user(const process_t* process) {
    return process && process->context.user_mode;
}

int process_snapshot_copy(uint32_t pid, process_snapshot_t* output) {
    process_t* process;

    if (!output) return ERR_NULL;
    process = host_process_by_pid(pid);
    if (!process) return ERR_NOT_FOUND;
    host_snapshot_from_process(process, output);
    return OK;
}

int process_snapshot_list(process_snapshot_t* output, uint32_t capacity,
                          uint32_t* out_count) {
    if (!output || !out_count) return ERR_NULL;
    if (capacity < HOST_PROCESS_COUNT + (temporary_active ? 1U : 0U)) {
        return ERR_OVERFLOW;
    }
    for (uint32_t index = 0U; index < HOST_PROCESS_COUNT; index++) {
        host_snapshot_from_process(&base_processes[index], &output[index]);
    }
    *out_count = HOST_PROCESS_COUNT;
    if (temporary_active) {
        host_snapshot_from_process(&temporary_process,
                                   &output[*out_count]);
        (*out_count)++;
    }
    return OK;
}

int process_snapshot_copy_vmas(uint32_t pid, uint32_t generation,
                               vm_area_info_t* output, uint32_t capacity,
                               uint32_t* out_count) {
    process_t* process;

    if (!out_count) return ERR_NULL;
    process = host_process_by_pid(pid);
    if (!process) return ERR_NOT_FOUND;
    if (generation != process->event_generation) return ERR_AGAIN;
    if (process->vma_count > capacity) return ERR_OVERFLOW;
    if (process->vma_count && !output) return ERR_NULL;
    if (process->vma_count) {
        output[0].start_addr = 0x00400000U;
        output[0].end_addr = 0x00401000U;
        output[0].flags = VM_READ | VM_EXEC;
        output[0].offset = 0U;
    }
    *out_count = process->vma_count;
    return OK;
}

process_t* process_create(const char* name, void (*entry_point)()) {
    if (temporary_active) return 0;
    kmemset(&temporary_process, 0, sizeof(temporary_process));
    temporary_process.pid = HOST_PROCESS_COUNT;
    temporary_process.event_generation = next_generation++;
    temporary_process.state = PROCESS_STATE_READY;
    host_copy_text(temporary_process.name, sizeof(temporary_process.name), name);
    temporary_active = 1U;
    if (entry_point) {
        process_entry_active = 1U;
        if (setjmp(process_entry_jump) == 0) entry_point();
        process_entry_active = 0U;
    }
    return &temporary_process;
}

void process_destroy(process_t* process) {
    if (process == &temporary_process) temporary_active = 0U;
}

void process_yield(void) {
    if (process_entry_active) longjmp(process_entry_jump, 1);
}

int vfs_lookup(const char* path, vfs_lookup_result_t* result) {
    int status;

    if (!result) return ERR_NULL;
    status = procfs_lookup(path, result);
    if (status != OK) return status;
    host_copy_text(result->canonical_path, sizeof(result->canonical_path), path);
    host_copy_text(result->mount_point, sizeof(result->mount_point), "/proc");
    result->mount_kind = VFS_MOUNT_PROCFS;
    result->mount_slot = 1U;
    result->mount_generation = 1U;
    return OK;
}

int vfs_list_dir(const char* path, vfs_dir_entry_t* entries,
                uint32_t capacity, uint32_t* out_count) {
    return procfs_list_path(path, entries, capacity, out_count);
}

static int host_fd_index(int32_t fd) {
    int32_t index = fd - VFS_FD_FIRST_FILE;

    if (index < 0 || (uint32_t)index >= HOST_FD_COUNT ||
        !fake_fd_used[index]) return -1;
    return index;
}

int vfs_open(const char* path, uint32_t mode, int32_t* fd_out) {
    vfs_lookup_result_t lookup;
    int result;

    if (!path || !fd_out) return ERR_NULL;
    *fd_out = VFS_FD_INVALID;
    for (uint32_t index = 0U; index < HOST_FD_COUNT; index++) {
        if (!fake_fd_used[index]) {
            result = vfs_lookup(path, &lookup);
            if (result != OK || lookup.type != VFS_NODE_REGULAR) {
                return result == OK ? ERR_INVALID : result;
            }
            kmemset(&fake_files[index], 0, sizeof(fake_files[index]));
            kmemset(&fake_vnodes[index], 0, sizeof(fake_vnodes[index]));
            result = procfs_open_file(&lookup, mode, &fake_vnodes[index],
                                      &fake_files[index],
                                      &fake_contexts[index]);
            if (result != OK) return result;
            fake_files[index].vnode = &fake_vnodes[index];
            fake_files[index].slot = index;
            result = fake_vnodes[index].operations->open(
                &fake_vnodes[index], &fake_files[index]);
            if (result != OK) {
                (void)fake_vnodes[index].operations->close(&fake_files[index]);
                return result;
            }
            fake_fd_used[index] = 1U;
            *fd_out = VFS_FD_FIRST_FILE + (int32_t)index;
            return OK;
        }
    }
    return ERR_OVERFLOW;
}

int vfs_read(int32_t fd, void* buffer, uint32_t size, uint32_t* bytes_read) {
    int index = host_fd_index(fd);

    if (index < 0) return ERR_INVALID;
    return fake_files[index].vnode->operations->read(&fake_files[index], buffer,
                                                     size, bytes_read);
}

int vfs_write(int32_t fd, const void* buffer, uint32_t size,
              uint32_t* bytes_written) {
    int index = host_fd_index(fd);

    if (index < 0) return ERR_INVALID;
    return fake_files[index].vnode->operations->write(
        &fake_files[index], buffer, size, bytes_written);
}

int vfs_close(int32_t fd) {
    int index = host_fd_index(fd);
    int result;

    if (index < 0) return ERR_INVALID;
    result = fake_files[index].vnode->operations->close(&fake_files[index]);
    if (result == OK) fake_fd_used[index] = 0U;
    return result;
}

int vfs_lseek(int32_t fd, int32_t offset, uint32_t whence,
              uint32_t* position) {
    int index = host_fd_index(fd);

    if (index < 0) return ERR_INVALID;
    return fake_files[index].vnode->operations->lseek(
        &fake_files[index], offset, whence, position);
}

int vfs_ioctl(int32_t fd, uint32_t request, void* argument) {
    int index = host_fd_index(fd);

    if (index < 0) return ERR_INVALID;
    return fake_files[index].vnode->operations->ioctl(
        &fake_files[index], request, argument);
}

int vfs_fsync(int32_t fd) {
    int index = host_fd_index(fd);

    if (index < 0) return ERR_INVALID;
    return fake_files[index].vnode->operations->sync(&fake_files[index]);
}

int vfs_poll(pollfd_t* fds, uint32_t count, uint32_t timeout_ticks,
             uint32_t* out_ready) {
    int index;
    uint32_t revents = 0U;

    (void)timeout_ticks;
    if (!fds || !out_ready || !count) return ERR_NULL;
    *out_ready = 0U;
    index = host_fd_index(fds[0].fd);
    if (index < 0) {
        fds[0].revents = POLLNVAL;
        return ERR_INVALID;
    }
    if (fake_files[index].vnode->operations->poll(
            &fake_files[index], fds[0].events, &revents) != OK) {
        return ERR_STATE;
    }
    fds[0].revents = revents;
    if (revents) *out_ready = 1U;
    return OK;
}

void vfs_mount_release(uint32_t slot, uint32_t generation) {
    (void)slot;
    (void)generation;
}

static void host_init_processes(void) {
    for (uint32_t index = 0U; index < HOST_PROCESS_COUNT; index++) {
        kmemset(&base_processes[index], 0, sizeof(base_processes[index]));
        base_processes[index].pid = index;
        base_processes[index].event_generation = 10U + index;
        base_processes[index].state = index == 0U ? PROCESS_STATE_RUNNING :
                                       PROCESS_STATE_READY;
        base_processes[index].total_ticks = 20U + index;
        host_copy_text(base_processes[index].name,
                       sizeof(base_processes[index].name),
                       index == 0U ? "idle" : "worker");
    }
    base_processes[1].vma_count = 1U;
    current_process = &base_processes[0];
}

int main(void) {
    procfs_test_result_t self_test;
    vfs_dir_entry_t entries[VFS_MAX_DIR_ENTRIES];
    vfs_lookup_result_t lookup;
    pollfd_t poll_fd;
    uint8_t buffer[512];
    uint32_t bytes = 0U;
    uint32_t count = 0U;
    uint32_t position = 0U;
    uint32_t revents = 0U;
    int32_t fd = VFS_FD_INVALID;
    int failures = 0;

    host_init_processes();
    coverage_active = 1U;

    EXPECT(procfs_is_ready() == 0);
    EXPECT(procfs_lookup("/proc/uptime", &lookup) == ERR_STATE);
    EXPECT(procfs_init() == OK);
    EXPECT(procfs_is_ready() != 0);
    EXPECT(procfs_validate_state() == OK);
    EXPECT(procfs_lookup("/proc", &lookup) == OK);
    EXPECT(lookup.type == VFS_NODE_DIRECTORY);
    EXPECT(procfs_list(entries, VFS_MAX_DIR_ENTRIES, &count) == OK);
    EXPECT(count >= HOST_GLOBAL_COUNT + 1U);
    EXPECT(procfs_list(entries, 1U, &count) == ERR_OVERFLOW);
    EXPECT(procfs_list_path(0, entries, 1U, &count) == ERR_NULL);

    EXPECT(procfs_self_test(&self_test) == OK);
    EXPECT(self_test.passed == self_test.total);
    EXPECT(self_test.registry != 0U);
    EXPECT(self_test.process_nodes != 0U);
    EXPECT(self_test.control_rollback != 0U);
    EXPECT(self_test.invariants != 0U);

    EXPECT(vfs_open("/proc/1/maps", VFS_MODE_READ, &fd) == OK);
    if (fd != VFS_FD_INVALID) {
        int index = host_fd_index(fd);

        EXPECT(vfs_read(fd, buffer, sizeof(buffer), &bytes) == OK);
        EXPECT(bytes > 0U);
        EXPECT(host_contains(buffer, bytes, "vma_start"));
        EXPECT(vfs_lseek(fd, 0, VFS_SEEK_SET, &position) == OK);
        EXPECT(vfs_ioctl(fd, 0U, 0) == ERR_UNAVAILABLE);
        EXPECT(vfs_fsync(fd) == ERR_UNAVAILABLE);
        poll_fd.fd = fd;
        poll_fd.events = POLLIN;
        poll_fd.revents = 0U;
        EXPECT(vfs_poll(&poll_fd, 1U, 0U, &count) == OK);
        EXPECT((poll_fd.revents & POLLIN) != 0U);
        EXPECT(fake_files[index].vnode->operations->open(0,
                                                         &fake_files[index]) ==
               ERR_NULL);
        EXPECT(fake_files[index].vnode->operations->open(
                   fake_files[index].vnode, 0) == ERR_NULL);
        EXPECT(fake_files[index].vnode->operations->poll(0, 0, &revents) ==
               ERR_NULL);
        EXPECT(vfs_close(fd) == OK);
        fd = VFS_FD_INVALID;
    }

    EXPECT(vfs_lookup("/proc/uptime", &lookup) == OK);
    lookup.mount_kind = VFS_MOUNT_SYSFS;
    EXPECT(procfs_open_file(&lookup, VFS_MODE_READ, &fake_vnodes[0],
                            &fake_files[0], &fake_contexts[0]) == ERR_INVALID);
    EXPECT(procfs_list_path("/proc/sys", entries, 0U, &count) == ERR_OVERFLOW);
    EXPECT(procfs_list_path("/proc/0", entries, 2U, &count) == ERR_OVERFLOW);
    EXPECT(procfs_reset_controls() == OK);
    EXPECT(procfs_is_ready() != 0);
    EXPECT(procfs_validate_state() == OK);

    coverage_active = 0U;
    coverage_emit(failures ? ERR_STATE : OK);
    return failures ? 1 : 0;
}
