#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "fs/fs.h"
#include "fs/permissions.h"
#include "fs/storage.h"
#include "process/process.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static process_t current_process;
static uint8_t fake_sidecar[FS_PERMISSION_SIDECAR_MAX_SIZE];
static uint32_t fake_sidecar_size;
static uint8_t fake_sidecar_present;

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
    printf("ZCOV_BEGIN|case=host:storage:permissions|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:storage:permissions|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:storage:permissions|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression, uint32_t line) {
    if (!condition) {
        fprintf(stderr, "permissions-host: falhou na linha %u: %s\n",
                line, expression);
        (void)fflush(stderr);
        __builtin_trap();
    }
}

#define EXPECT(expression) expect_true((expression), #expression, __LINE__)

static void write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void write_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint32_t crc32(const uint8_t* data, uint32_t size) {
    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t index = 0U; index < size; index++) {
        crc ^= data[index];
        for (uint32_t bit = 0U; bit < 8U; bit++) {
            crc = (crc >> 1U) ^
                  (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

static void sidecar_build(const char* first, const char* second) {
    const char* paths[2] = {first, second};
    uint32_t count = second ? 2U : 1U;

    memset(fake_sidecar, 0, sizeof(fake_sidecar));
    write_u32(fake_sidecar, FS_PERMISSION_SIDECAR_MAGIC);
    write_u16(fake_sidecar + 4U, FS_PERMISSION_SIDECAR_VERSION);
    write_u16(fake_sidecar + 6U, FS_PERMISSION_SIDECAR_HEADER_SIZE);
    write_u32(fake_sidecar + 8U, count);
    write_u32(fake_sidecar + 12U, FS_PERMISSION_SIDECAR_RECORD_SIZE);
    for (uint32_t index = 0U; index < count; index++) {
        uint8_t* record = fake_sidecar + FS_PERMISSION_SIDECAR_HEADER_SIZE +
            index * FS_PERMISSION_SIDECAR_RECORD_SIZE;
        uint32_t length = kstrlen(paths[index]);

        kmemcpy(record, paths[index], length);
        write_u32(record + 256U, PROCESS_UID_USER);
        write_u32(record + 260U, PROCESS_GID_USER);
        write_u16(record + 264U, 0664U);
        record[266U] = VFS_NODE_REGULAR;
    }
    fake_sidecar_size = FS_PERMISSION_SIDECAR_HEADER_SIZE +
        count * FS_PERMISSION_SIDECAR_RECORD_SIZE;
    write_u32(fake_sidecar + 16U,
              crc32(fake_sidecar + FS_PERMISSION_SIDECAR_HEADER_SIZE,
                    fake_sidecar_size - FS_PERMISSION_SIDECAR_HEADER_SIZE));
    fake_sidecar_present = 1U;
}

static void lookup_init(vfs_lookup_result_t* lookup, const char* path,
                        vfs_node_type_t type, vfs_mount_kind_t mount_kind) {
    kmemset(lookup, 0, sizeof(*lookup));
    lookup->type = type;
    lookup->mount_kind = mount_kind;
    lookup->fs_type = mount_kind == VFS_MOUNT_STORAGE ? STORAGE_FS_FAT32 :
                      STORAGE_FS_NONE;
    lookup->mount_generation = 1U;
    lookup->mount_slot = 0U;
    set_text(lookup->canonical_path, sizeof(lookup->canonical_path), path);
    set_text(lookup->relative_path, sizeof(lookup->relative_path),
             path[0] == '/' ? path + 1U : path);
    if (mount_kind == VFS_MOUNT_STORAGE) {
        set_text(lookup->volume_id, sizeof(lookup->volume_id), "system");
    }
}

void* kmalloc(uint32_t size) { return size ? malloc(size) : 0; }

void kfree(void* pointer) { free(pointer); }

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

process_t* process_get_current(void) { return &current_process; }

int storage_find_volume(const char* id, storage_volume_t* out_volume) {
    if (!id || !out_volume) return ERR_NULL;
    if (kstrcmp(id, "system") != 0) return ERR_NOT_FOUND;
    kmemset(out_volume, 0, sizeof(*out_volume));
    set_text(out_volume->id, sizeof(out_volume->id), "system");
    out_volume->fs_type = STORAGE_FS_FAT32;
    out_volume->mounted = 1U;
    out_volume->generation = 1U;
    return OK;
}

int storage_get_path_info(const char* id, const char* path,
                          uint32_t* out_size, uint8_t* out_attributes,
                          uint8_t* out_directory) {
    if (!id || !path || !out_size || !out_attributes || !out_directory) {
        return ERR_NULL;
    }
    if (kstrcmp(id, "system") != 0) return ERR_NOT_FOUND;
    if (kstrcmp(path, "new") == 0 || kstrcmp(path, "APPS") == 0 ||
        kstrcmp(path, "APPS/DEMO") == 0) {
        *out_size = 0U;
        *out_attributes = 0x10U;
        *out_directory = 1U;
        return OK;
    }
    if (kstrcmp(path, FS_PERMISSION_SIDECAR_NAME) != 0 ||
        !fake_sidecar_present) return ERR_NOT_FOUND;
    *out_size = fake_sidecar_size;
    *out_attributes = FS_ATTRIBUTE_HIDDEN | FS_ATTRIBUTE_SYSTEM;
    *out_directory = 0U;
    return OK;
}

int storage_read_file_range(const char* id, const char* path,
                            uint32_t offset, uint8_t* buffer,
                            uint32_t max_size, uint32_t* out_read) {
    uint32_t amount;

    if (!id || !path || !out_read || (max_size && !buffer)) return ERR_NULL;
    if (kstrcmp(id, "system") != 0 ||
        kstrcmp(path, FS_PERMISSION_SIDECAR_NAME) != 0 ||
        !fake_sidecar_present) return ERR_NOT_FOUND;
    if (offset >= fake_sidecar_size) {
        *out_read = 0U;
        return OK;
    }
    amount = fake_sidecar_size - offset;
    if (amount > max_size) amount = max_size;
    if (amount) kmemcpy(buffer, fake_sidecar + offset, amount);
    *out_read = amount;
    return OK;
}

int storage_atomic_write_file(const char* id, const char* path,
                              const uint8_t* data, uint32_t size,
                              uint8_t attributes, storage_atomic_mode_t mode) {
    if (!id || !path || !data || !size) return ERR_NULL;
    if (kstrcmp(id, "system") != 0 ||
        kstrcmp(path, FS_PERMISSION_SIDECAR_NAME) != 0 ||
        size > sizeof(fake_sidecar)) return ERR_INVALID;
    if (mode != STORAGE_ATOMIC_CREATE_OR_REPLACE &&
        mode != STORAGE_ATOMIC_REPLACE_ONLY) return ERR_INVALID;
    (void)attributes;
    kmemcpy(fake_sidecar, data, size);
    fake_sidecar_size = size;
    fake_sidecar_present = 1U;
    return OK;
}

static void set_user(void) {
    EXPECT(process_credentials_init_user(&current_process.credentials) == OK);
}

static void set_native(void) {
    EXPECT(process_credentials_init_native(&current_process.credentials) == OK);
}

int main(void) {
    vfs_lookup_result_t lookup;
    process_credentials_t user;

    coverage_active = 1U;
    kmemset(&current_process, 0, sizeof(current_process));
    current_process.pid = 7U;
    set_native();
    EXPECT(fs_permissions_init() == OK);
    EXPECT(fs_permissions_prepare_volume("system") == OK);
    EXPECT(fs_permissions_path_compare("ZPERM.DAT", "zperm.dat") == 0);
    EXPECT(fs_permissions_path_compare("APP.ZAP", "app.zap") == 0);
    fake_sidecar_present = 0U;

    lookup_init(&lookup, "/legacy.txt", VFS_NODE_REGULAR,
                VFS_MOUNT_STORAGE);
    EXPECT(fs_permissions_apply_lookup(&lookup) == OK);
    EXPECT(lookup.uid == PROCESS_UID_ROOT);
    EXPECT(lookup.gid == PROCESS_GID_ROOT);
    EXPECT(lookup.mode == FS_PERMISSION_LEGACY_FILE);
    EXPECT(lookup.permission_present == 0U);

    set_user();
    user = current_process.credentials;
    EXPECT(process_credentials_validate(&user) == OK);
    EXPECT(process_credentials_has(&user, PROCESS_CAPABILITY_FILE_READ) != 0);
    user.uid = PROCESS_UID_ROOT;
    EXPECT(process_credentials_validate(&user) == ERR_STATE);
    EXPECT(process_credentials_has(&user, PROCESS_CAPABILITY_FILE_READ) == 0);
    user = current_process.credentials;
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_READ) == OK);
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_WRITE) ==
           ERR_UNAVAILABLE);
    lookup.uid = 2000U;
    lookup.gid = 2001U;
    lookup.mode = 0604U;
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_READ) == OK);
    lookup.mode = 0600U;
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_READ) ==
           ERR_UNAVAILABLE);
    lookup.gid = PROCESS_GID_USER;
    lookup.mode = 0060U;
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_READ) == OK);

    EXPECT(fs_permissions_register_path("system", "new", VFS_NODE_DIRECTORY) ==
           OK);
    EXPECT(fs_permissions_register_path("system", "new/owned.txt",
                                        VFS_NODE_REGULAR) == OK);
    EXPECT(fs_permissions_register_path("system", "new/owned.txt",
                                        VFS_NODE_REGULAR) == OK);
    lookup_init(&lookup, "/new/owned.txt", VFS_NODE_REGULAR,
                VFS_MOUNT_STORAGE);
    EXPECT(fs_permissions_apply_lookup(&lookup) == OK);
    EXPECT(lookup.permission_present != 0U);
    EXPECT(lookup.uid == PROCESS_UID_USER);
    EXPECT(lookup.mode == FS_PERMISSION_FILE_DEFAULT);
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_LIST) ==
           ERR_INVALID);
    EXPECT(fs_permissions_check_create(&user, &lookup) == OK);
    EXPECT(fs_permissions_check_traversal(&user, &lookup) == OK);
    lookup_init(&lookup, "/new", VFS_NODE_DIRECTORY, VFS_MOUNT_STORAGE);
    EXPECT(fs_permissions_apply_lookup(&lookup) == OK);
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_READ) ==
           ERR_INVALID);
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_LIST |
                                       FS_PERMISSION_ACCESS_EXECUTE) == OK);

    EXPECT(fs_permissions_register_path("system", "APPS/DEMO",
                                        VFS_NODE_DIRECTORY) == OK);
    EXPECT(fs_permissions_register_path("system", "APPS/DEMO/APP.ZAP",
                                        VFS_NODE_REGULAR) == OK);
    EXPECT(fs_permissions_register_path("system", "APPS/DEMO/META.DAT",
                                        VFS_NODE_REGULAR) == OK);
    EXPECT(fs_permissions_register_path("system", "APPS/DEMO/AUTH.DAT",
                                        VFS_NODE_REGULAR) == OK);
    lookup_init(&lookup, "/APPS/DEMO/APP.ZAP", VFS_NODE_REGULAR,
                VFS_MOUNT_STORAGE);
    EXPECT(fs_permissions_apply_lookup(&lookup) == OK);
    EXPECT(lookup.mode == 0555U);
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_READ) == OK);
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_WRITE) ==
           ERR_UNAVAILABLE);
    lookup_init(&lookup, "/APPS/DEMO/META.DAT", VFS_NODE_REGULAR,
                VFS_MOUNT_STORAGE);
    EXPECT(fs_permissions_apply_lookup(&lookup) == OK);
    EXPECT(lookup.mode == 0444U);
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_WRITE) ==
           ERR_UNAVAILABLE);

    lookup_init(&lookup, "/dev/null", VFS_NODE_CHAR_DEVICE, VFS_MOUNT_DEVFS);
    EXPECT(fs_permissions_apply_lookup(&lookup) == OK);
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_READ) == OK);
    lookup_init(&lookup, "/dev/hda", VFS_NODE_BLOCK_DEVICE, VFS_MOUNT_DEVFS);
    EXPECT(fs_permissions_apply_lookup(&lookup) == OK);
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_READ) ==
           ERR_UNAVAILABLE);
    set_native();
    EXPECT(fs_permissions_check_lookup(&current_process.credentials, &lookup,
                                       FS_PERMISSION_ACCESS_READ) == OK);
    EXPECT(process_credentials_validate(&current_process.credentials) == OK);
    EXPECT(process_credentials_has(&current_process.credentials,
                                   PROCESS_CAPABILITY_PACKAGE_ADMIN) != 0);
    EXPECT(kstrcmp(process_capability_name(PROCESS_CAPABILITY_FILE_READ),
                   "file-read") == 0);
    EXPECT(kstrcmp(process_capability_name(0x80000000U), "unknown") == 0);

    set_user();
    lookup_init(&lookup, "/proc/status", VFS_NODE_REGULAR,
                VFS_MOUNT_PROCFS);
    EXPECT(fs_permissions_apply_lookup(&lookup) == OK);
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_READ) == OK);
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_WRITE) ==
           ERR_UNAVAILABLE);
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_IOCTL) == OK);
    EXPECT(kstrcmp(fs_permission_access_name(FS_PERMISSION_ACCESS_READ),
                   "read") == 0);
    EXPECT(kstrcmp(fs_permission_access_name(0x80000000U), "unknown") == 0);
    EXPECT(fs_permissions_register_path("system", "bad/name/", VFS_NODE_REGULAR) ==
           ERR_INVALID);
    EXPECT(fs_permissions_register_path("system", "../outside",
                                        VFS_NODE_REGULAR) == ERR_INVALID);
    EXPECT(fs_permissions_register_path("system", "a//b",
                                        VFS_NODE_REGULAR) == ERR_INVALID);
    EXPECT(fs_permissions_check_lookup(&user, &lookup, 0x80000000U) ==
           ERR_INVALID);
    lookup_init(&lookup, "/zperm.dat", VFS_NODE_REGULAR,
                VFS_MOUNT_STORAGE);
    EXPECT(fs_permissions_check_lookup(&user, &lookup,
                                       FS_PERMISSION_ACCESS_READ) ==
           ERR_UNAVAILABLE);

    fs_permissions_init();
    lookup_init(&lookup, "/new/owned.txt", VFS_NODE_REGULAR,
                VFS_MOUNT_STORAGE);
    EXPECT(fs_permissions_apply_lookup(&lookup) == OK);
    EXPECT(lookup.permission_present != 0U);
    fake_sidecar[0] ^= 1U;
    fs_permissions_init();
    EXPECT(fs_permissions_apply_lookup(&lookup) == ERR_STATE);
    sidecar_build("z.txt", "a.txt");
    fs_permissions_init();
    EXPECT(fs_permissions_apply_lookup(&lookup) == ERR_STATE);
    sidecar_build("valid.txt", 0);
    fake_sidecar_size = FS_PERMISSION_SIDECAR_HEADER_SIZE - 1U;
    fs_permissions_init();
    EXPECT(fs_permissions_apply_lookup(&lookup) == ERR_STATE);

    fake_sidecar_present = 0U;
    EXPECT(fs_permissions_init() == OK);
    set_user();
    for (uint32_t index = 0U; index < FS_PERMISSION_MAX_RECORDS; index++) {
        char path[16];

        (void)snprintf(path, sizeof(path), "f%03u", index);
        EXPECT(fs_permissions_register_path("system", path,
                                            VFS_NODE_REGULAR) == OK);
    }
    EXPECT(fs_permissions_prepare_path("system", "overflow") ==
           ERR_OVERFLOW);
    EXPECT(fs_permissions_register_path("system", "overflow",
                                        VFS_NODE_REGULAR) == ERR_OVERFLOW);
    EXPECT(fs_permissions_validate() == OK);

    EXPECT(fs_permissions_rename_path("system", "f000", "f001") ==
           ERR_STATE);
    EXPECT(fs_permissions_rename_path("system", "f000", "bad/name") ==
           ERR_INVALID);
    EXPECT(fs_permissions_rename_path("system", "f000", "renamed.txt") ==
           OK);
    EXPECT(fs_permissions_remove_path("system", "renamed.txt") == OK);
    EXPECT(fs_permissions_remove_path("system", "renamed.txt") == OK);
    EXPECT(fs_permissions_remove_path("system", "bad/name/") == ERR_INVALID);

    coverage_active = 0U;
    coverage_emit(OK);
    return 0;
}
