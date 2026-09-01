#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/timer.h"
#include "fs/devfs.h"
#include "fs/fs.h"
#include "fs/procfs.h"
#include "fs/storage.h"
#include "fs/sysfs.h"
#include "fs/vfs_internal.h"
#include "process/process.h"

#define HOST_VOLUME_CAPACITY 3U
#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
process_t* processes[MAX_PROCESSES];
uint32_t process_count;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static process_t fake_current;
static storage_volume_t fake_volumes[HOST_VOLUME_CAPACITY];
static uint8_t fake_volume_count;
static storage_status_t fake_storage_status;
static uint8_t fake_storage_initialized;
static uint8_t fake_quiescing;
static uint32_t fake_ticks = 100U;
static uint32_t fake_log_count;
static uint32_t fake_cursor_index;
static int fake_mount_result = OK;
static int fake_unmount_result = OK;

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
    printf("ZCOV_BEGIN|case=host:storage:vfs-path|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:storage:vfs-path|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:storage:vfs-path|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "vfs-path-host: falhou: %s\n", expression);
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
    fake_log_count++;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
    fake_log_count++;
}

uint32_t timer_get_ticks(void) {
    return fake_ticks++;
}

process_t* process_get_current(void) {
    return &fake_current;
}

int vfs_power_is_quiescing(void) {
    return fake_quiescing;
}

static void fill_volume(uint8_t index, const char* id, uint8_t boot,
                        storage_volume_role_t role, uint32_t generation) {
    storage_volume_t* volume = &fake_volumes[index];

    memset(volume, 0, sizeof(*volume));
    set_text(volume->id, sizeof(volume->id), id);
    set_text(volume->disk_id, sizeof(volume->disk_id), "hostdisk");
    volume->layout = STORAGE_LAYOUT_RAW;
    volume->fs_type = STORAGE_FS_FAT32;
    volume->state = STORAGE_VOLUME_MOUNTED;
    volume->mounted = 1U;
    volume->boot = boot;
    volume->pinned = boot;
    volume->generation = generation;
    volume->role = role;
    volume->bytes_per_sector = STORAGE_SECTOR_SIZE;
    volume->sectors_per_cluster = 1U;
    volume->sector_count = 4096U;
}

int storage_get_status(storage_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_storage_status;
    return OK;
}

int storage_get_mounted_at(uint8_t index, storage_volume_t* out_volume) {
    if (!out_volume) return ERR_NULL;
    if (!fake_storage_initialized || index >= fake_volume_count) {
        return ERR_NOT_FOUND;
    }
    *out_volume = fake_volumes[index];
    return OK;
}

int storage_find_volume(const char* id, storage_volume_t* out_volume) {
    if (!id || !out_volume) return ERR_NULL;
    for (uint8_t index = 0U; index < fake_volume_count; index++) {
        if (strcmp(id, fake_volumes[index].id) == 0) {
            *out_volume = fake_volumes[index];
            return OK;
        }
    }
    return ERR_NOT_FOUND;
}

int storage_mount(const char* id) {
    if (!id) return ERR_NULL;
    if (fake_mount_result != OK) return fake_mount_result;
    for (uint8_t index = 0U; index < fake_volume_count; index++) {
        if (strcmp(id, fake_volumes[index].id) == 0) {
            fake_volumes[index].mounted = 1U;
            fake_volumes[index].state = STORAGE_VOLUME_MOUNTED;
            fake_storage_status.mounted_count = fake_volume_count;
            return OK;
        }
    }
    return ERR_NOT_FOUND;
}

int storage_unmount(const char* id) {
    if (!id) return ERR_NULL;
    if (fake_unmount_result != OK) return fake_unmount_result;
    for (uint8_t index = 0U; index < fake_volume_count; index++) {
        if (strcmp(id, fake_volumes[index].id) == 0) {
            fake_volumes[index].mounted = 0U;
            fake_volumes[index].state = STORAGE_VOLUME_DETECTED;
            if (fake_storage_status.mounted_count) {
                fake_storage_status.mounted_count--;
            }
            return OK;
        }
    }
    return ERR_NOT_FOUND;
}

int storage_unmount_after_sync(const char* id) {
    return storage_unmount(id);
}

int storage_get_path_info(const char* id, const char* path, uint32_t* out_size,
                          uint8_t* out_attributes, uint8_t* out_directory) {
    if (!id || !path || !out_size || !out_attributes || !out_directory) {
        return ERR_NULL;
    }
    if (strcmp(id, "system") != 0 && strcmp(id, "data") != 0 &&
        strcmp(id, "boot") != 0) return ERR_NOT_FOUND;
    *out_size = 0U;
    *out_attributes = 0U;
    *out_directory = 0U;
    if (!path[0] || strcmp(path, ".") == 0 || strcmp(path, "dir") == 0) {
        *out_directory = 1U;
        return OK;
    }
    if (strcmp(path, "file.txt") == 0 || strcmp(path, "etc/config") == 0) {
        *out_size = 17U;
        *out_attributes = FS_ATTRIBUTES_PRESERVE;
        return OK;
    }
    return ERR_NOT_FOUND;
}

int storage_dir_cursor_open_long(const char* id, const char* path,
                                 storage_long_dir_cursor_t* cursor) {
    if (!id || !path || !cursor) return ERR_NULL;
    if (strcmp(id, "system") != 0 || path[0] != '\0') return ERR_NOT_FOUND;
    memset(cursor, 0, sizeof(*cursor));
    cursor->active = 1U;
    fake_cursor_index = 0U;
    return OK;
}

int storage_dir_cursor_next_long(storage_long_dir_cursor_t* cursor,
                                 storage_long_dir_entry_t* out_entry,
                                 uint8_t* out_found, uint8_t* out_done) {
    if (!cursor || !out_entry || !out_found || !out_done) return ERR_NULL;
    *out_found = 0U;
    if (!cursor->active) {
        *out_done = 1U;
        return OK;
    }
    if (fake_cursor_index == 0U) {
        memset(out_entry, 0, sizeof(*out_entry));
        set_text(out_entry->name, sizeof(out_entry->name), "etc");
        out_entry->is_directory = 1U;
        *out_found = 1U;
        fake_cursor_index++;
        *out_done = 0U;
        return OK;
    }
    if (fake_cursor_index == 1U) {
        memset(out_entry, 0, sizeof(*out_entry));
        set_text(out_entry->name, sizeof(out_entry->name), "file.txt");
        out_entry->size = 17U;
        *out_found = 1U;
        fake_cursor_index++;
        *out_done = 0U;
        return OK;
    }
    cursor->active = 0U;
    *out_done = 1U;
    return OK;
}

int devfs_lookup(const char* path, vfs_lookup_result_t* result) {
    if (!path || !result) return ERR_NULL;
    if (strcmp(path, "/dev") == 0) {
        result->type = VFS_NODE_DIRECTORY;
        return OK;
    }
    if (strcmp(path, "/dev/null") == 0 || strcmp(path, "/dev/zero") == 0) {
        result->type = VFS_NODE_CHAR_DEVICE;
        return OK;
    }
    return ERR_NOT_FOUND;
}

int devfs_list(vfs_dir_entry_t* entries, uint32_t capacity,
               uint32_t* out_count) {
    if (!entries || !out_count) return ERR_NULL;
    if (capacity < 2U) return ERR_OVERFLOW;
    memset(entries, 0, sizeof(*entries) * 2U);
    set_text(entries[0].name, sizeof(entries[0].name), "null");
    set_text(entries[1].name, sizeof(entries[1].name), "zero");
    entries[0].type = VFS_NODE_CHAR_DEVICE;
    entries[1].type = VFS_NODE_CHAR_DEVICE;
    *out_count = 2U;
    return OK;
}

int procfs_lookup(const char* path, vfs_lookup_result_t* result) {
    if (!path || !result) return ERR_NULL;
    if (strcmp(path, "/proc") == 0) result->type = VFS_NODE_DIRECTORY;
    else if (strcmp(path, "/proc/uptime") == 0) result->type = VFS_NODE_REGULAR;
    else return ERR_NOT_FOUND;
    return OK;
}

int procfs_list_path(const char* path, vfs_dir_entry_t* entries,
                     uint32_t capacity, uint32_t* out_count) {
    if (!path || !entries || !out_count) return ERR_NULL;
    if (strcmp(path, "/proc") != 0) return ERR_NOT_FOUND;
    if (!capacity) return ERR_OVERFLOW;
    memset(entries, 0, sizeof(*entries));
    set_text(entries[0].name, sizeof(entries[0].name), "uptime");
    entries[0].type = VFS_NODE_REGULAR;
    *out_count = 1U;
    return OK;
}

int sysfs_lookup(const char* path, vfs_lookup_result_t* result) {
    if (!path || !result) return ERR_NULL;
    if (strcmp(path, "/sys") == 0) result->type = VFS_NODE_DIRECTORY;
    else if (strcmp(path, "/sys/power/state") == 0) result->type = VFS_NODE_REGULAR;
    else return ERR_NOT_FOUND;
    return OK;
}

int sysfs_list_path(const char* path, vfs_dir_entry_t* entries,
                    uint32_t capacity, uint32_t* out_count) {
    if (!path || !entries || !out_count) return ERR_NULL;
    if (strcmp(path, "/sys") != 0) return ERR_NOT_FOUND;
    if (!capacity) return ERR_OVERFLOW;
    memset(entries, 0, sizeof(*entries));
    set_text(entries[0].name, sizeof(entries[0].name), "power");
    entries[0].type = VFS_NODE_DIRECTORY;
    *out_count = 1U;
    return OK;
}

static void setup_storage(void) {
    memset(&fake_storage_status, 0, sizeof(fake_storage_status));
    fake_storage_initialized = 1U;
    fake_volume_count = HOST_VOLUME_CAPACITY;
    fake_storage_status.initialized = 1U;
    fake_storage_status.disk_count = 1U;
    fake_storage_status.volume_count = fake_volume_count;
    fake_storage_status.mounted_count = fake_volume_count;
    fill_volume(0U, "system", 0U, STORAGE_VOLUME_ROLE_SYSTEM, 11U);
    fill_volume(1U, "boot", 1U, STORAGE_VOLUME_ROLE_BOOT, 12U);
    fill_volume(2U, "data", 0U, STORAGE_VOLUME_ROLE_NONE, 13U);
}

int main(void) {
    char cwd[VFS_MAX_PATH];
    vfs_lookup_result_t lookup;
    vfs_mount_info_t mounts[VFS_MAX_MOUNTS];
    vfs_dir_entry_t entries[VFS_MAX_DIR_ENTRIES];
    vfs_fd_table_t inherited;
    uint32_t count;
    uint32_t capacity;
    uint32_t active;
    uint32_t lookups;
    uint32_t chdirs;
    uint32_t unmounted;

    coverage_active = 1U;
    memset(&fake_current, 0, sizeof(fake_current));
    fake_current.fd_table.initialized = 1U;
    set_text(fake_current.fd_table.cwd, sizeof(fake_current.fd_table.cwd), "/mnt");
    processes[0] = &fake_current;
    process_count = 1U;

    EXPECT(vfs_path_validate_state() == ERR_STATE);
    EXPECT(vfs_refresh_mounts() == ERR_STATE);
    EXPECT(vfs_path_init() == OK);
    EXPECT(vfs_path_validate_state() == ERR_NOT_FOUND);

    setup_storage();
    EXPECT(vfs_refresh_mounts() == OK);
    EXPECT(vfs_path_validate_state() == OK);
    vfs_path_get_metrics(&capacity, &active, &lookups, &chdirs);
    EXPECT(capacity == VFS_MAX_MOUNTS);
    EXPECT(active == 6U);
    EXPECT(lookups == 0U);
    EXPECT(chdirs == 0U);

    EXPECT(vfs_lookup("/", &lookup) == OK);
    EXPECT(lookup.type == VFS_NODE_DIRECTORY);
    EXPECT(strcmp(lookup.volume_id, "system") == 0);
    EXPECT(vfs_lookup("/mnt", &lookup) == OK);
    EXPECT(lookup.type == VFS_NODE_DIRECTORY);
    EXPECT(vfs_lookup("/dev", &lookup) == OK);
    EXPECT(lookup.type == VFS_NODE_DIRECTORY);
    EXPECT(vfs_lookup("/dev/null", &lookup) == OK);
    EXPECT(lookup.type == VFS_NODE_CHAR_DEVICE);
    EXPECT(vfs_lookup("/proc/uptime", &lookup) == OK);
    EXPECT(lookup.type == VFS_NODE_REGULAR);
    EXPECT(vfs_lookup("/sys/power/state", &lookup) == OK);
    EXPECT(lookup.type == VFS_NODE_REGULAR);
    EXPECT(vfs_lookup("system:/etc/config", &lookup) == OK);
    EXPECT(strcmp(lookup.canonical_path, "/etc/config") == 0);
    EXPECT(vfs_lookup("/../escape", &lookup) == ERR_INVALID);
    EXPECT(vfs_lookup("/missing", &lookup) == ERR_NOT_FOUND);
    EXPECT(vfs_resolve_open_path("/new", VFS_MODE_WRITE, &lookup) == OK);
    EXPECT(lookup.type == VFS_NODE_REGULAR);

    EXPECT(vfs_chdir("/mnt/data/dir") == OK);
    EXPECT(vfs_getcwd(cwd, sizeof(cwd)) == OK);
    EXPECT(strcmp(cwd, "/mnt/data/dir") == 0);
    EXPECT(vfs_lookup("../../../file.txt", &lookup) == OK);
    EXPECT(strcmp(lookup.canonical_path, "/file.txt") == 0);
    EXPECT(vfs_chdir("/file.txt") == ERR_INVALID);
    EXPECT(vfs_chdir("../../../../") == ERR_INVALID);
    EXPECT(vfs_getcwd(cwd, 2U) == ERR_OVERFLOW);

    EXPECT(vfs_list_dir("/", entries, VFS_MAX_DIR_ENTRIES, &count) == OK);
    EXPECT(count >= 6U);
    EXPECT(vfs_list_dir("/dev", entries, 2U, &count) == OK);
    EXPECT(count == 2U);
    EXPECT(vfs_list_dir("/proc", entries, 2U, &count) == OK);
    EXPECT(count == 1U);
    EXPECT(vfs_list_dir("/sys", entries, 2U, &count) == OK);
    EXPECT(count == 1U);
    EXPECT(vfs_list_dir("/mnt", entries, VFS_MAX_DIR_ENTRIES, &count) == OK);
    EXPECT(count == 2U);
    EXPECT(vfs_list_dir("/file.txt", entries, 2U, &count) == ERR_INVALID);
    EXPECT(vfs_list_dir("/", entries, 0U, &count) == ERR_OVERFLOW);

    EXPECT(vfs_mount_volume("data") == OK);

    EXPECT(vfs_copy_mounts(mounts, VFS_MAX_MOUNTS, &count) == OK);
    EXPECT(count == 6U);
    EXPECT(vfs_copy_mounts(mounts, 1U, &count) == ERR_OVERFLOW);
    EXPECT(vfs_mount_acquire(VFS_MAX_MOUNTS, 1U) == ERR_INVALID);
    EXPECT(vfs_mount_acquire(VFS_MAX_STORAGE_MOUNTS, 1U) == OK);
    EXPECT(vfs_mount_validate_reference(VFS_MAX_STORAGE_MOUNTS, 1U) == OK);
    EXPECT(vfs_mount_validate_reference(VFS_MAX_STORAGE_MOUNTS, 2U) == ERR_STATE);
    vfs_mount_release(VFS_MAX_STORAGE_MOUNTS, 1U);
    EXPECT(vfs_mount_validate_reference(VFS_MAX_MOUNTS, 1U) == ERR_INVALID);

    memset(&inherited, 0, sizeof(inherited));
    EXPECT(vfs_fd_table_inherit_cwd(&inherited, &fake_current.fd_table) == ERR_STATE);
    inherited.initialized = 1U;
    EXPECT(vfs_fd_table_inherit_cwd(&inherited, &fake_current.fd_table) == OK);
    EXPECT(strcmp(inherited.cwd, "/mnt/data/dir") == 0);
    EXPECT(vfs_fd_table_inherit_cwd(&inherited, 0) == OK);
    EXPECT(strcmp(inherited.cwd, "/") == 0);

    EXPECT(vfs_chdir("/") == OK);
    EXPECT(vfs_power_unmount_storage_until(fake_ticks - 1U, &unmounted) == ERR_TIMEOUT);
    EXPECT(vfs_power_unmount_storage_until(fake_ticks + 100U, 0) == ERR_NULL);
    EXPECT(vfs_chdir("/") == OK);
    EXPECT(vfs_power_unmount_storage_until(fake_ticks + 100U, &unmounted) == OK);
    EXPECT(unmounted == 1U);
    EXPECT(vfs_path_validate_state() == OK);

    fake_quiescing = 1U;
    EXPECT(vfs_lookup("/", &lookup) == ERR_UNAVAILABLE);
    EXPECT(vfs_chdir("/") == ERR_UNAVAILABLE);
    EXPECT(vfs_list_dir("/", entries, VFS_MAX_DIR_ENTRIES, &count) == ERR_UNAVAILABLE);
    fake_quiescing = 0U;
    EXPECT(fake_log_count > 0U);

    coverage_active = 0U;
    coverage_emit(0);
    puts("vfs-path-host: PASS");
    return 0;
}
