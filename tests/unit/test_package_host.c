#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/app_package.h"
#include "core/app_loader.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "fs/fs.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_FILE_CAPACITY 96U
#define HOST_FILE_BYTES 16384U
#define HOST_PACKAGE_BUFFER_SIZE \
    (sizeof(app_package_header_t) + APP_PACKAGE_MAX_MANIFEST_SIZE + \
     APP_IMAGE_MAX_FILE_SIZE + 1U)

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

typedef struct {
    char path[FS_MAX_PATH];
    uint8_t data[HOST_FILE_BYTES];
    uint32_t size;
    uint8_t attributes;
    uint8_t used;
} host_file_t;

static host_file_t host_files[HOST_FILE_CAPACITY];
static uint8_t host_fs_type;
static uint8_t host_loader_ready;
static uint8_t host_loader_foreground;
static int host_loader_run_result;
static int host_fs_info_result;
static int host_write_result;
static uint32_t host_next_pid;
static uint8_t host_package_buffer[HOST_PACKAGE_BUFFER_SIZE];
static uint8_t host_package_v2[HOST_PACKAGE_BUFFER_SIZE];
static uint8_t host_package_dependency[HOST_PACKAGE_BUFFER_SIZE];

static void __attribute__((no_instrument_function))
coverage_record(void* function) {
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
    printf("ZCOV_BEGIN|case=host:core:app-package|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:app-package|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:app-package|value=0x%08X\n",
           (uint32_t)result);
}

static int host_find_file(const char* path) {
    for (uint32_t index = 0U; index < HOST_FILE_CAPACITY; index++) {
        if (host_files[index].used && strcmp(host_files[index].path, path) == 0) {
            return (int)index;
        }
    }
    return -1;
}

static void host_copy_path(char* destination, const char* source) {
    size_t length = source ? strlen(source) : 0U;

    if (length >= FS_MAX_PATH) length = FS_MAX_PATH - 1U;
    if (length) memcpy(destination, source, length);
    destination[length] = '\0';
}

static int host_store_file(const char* path, const uint8_t* data,
                           uint32_t size, uint8_t attributes,
                           fs_atomic_mode_t mode) {
    int file_index;

    if (!path || (size && !data) || strlen(path) >= FS_MAX_PATH ||
        size > HOST_FILE_BYTES) return ERR_INVALID;
    file_index = host_find_file(path);
    if (mode == FS_ATOMIC_REPLACE_ONLY && file_index < 0) {
        return ERR_NOT_FOUND;
    }
    if (file_index < 0) {
        for (uint32_t index = 0U; index < HOST_FILE_CAPACITY; index++) {
            if (!host_files[index].used) {
                file_index = (int)index;
                break;
            }
        }
    }
    if (file_index < 0) return ERR_OVERFLOW;
    host_files[file_index].used = 1U;
    host_copy_path(host_files[file_index].path, path);
    if (size) memcpy(host_files[file_index].data, data, size);
    host_files[file_index].size = size;
    host_files[file_index].attributes = attributes;
    return OK;
}

static int host_remove_file(const char* path) {
    int file_index = host_find_file(path);

    if (file_index < 0) return ERR_NOT_FOUND;
    memset(&host_files[file_index], 0, sizeof(host_files[file_index]));
    return OK;
}

static int host_read_file(const char* path, uint8_t* buffer, uint32_t max_size) {
    int file_index;
    uint32_t copy_size;

    if (!path || (max_size && !buffer)) return ERR_NULL;
    file_index = host_find_file(path);
    if (file_index < 0) return -1;
    copy_size = host_files[file_index].size;
    if (copy_size > max_size) copy_size = max_size;
    if (copy_size) memcpy(buffer, host_files[file_index].data, copy_size);
    return (int)copy_size;
}

static int host_is_direct_child(const char* directory, const char* path) {
    const char* child;
    size_t directory_length;

    if (!directory || !path) return 0;
    directory_length = strlen(directory);
    if (directory_length == 0U) {
        if (path[0] == '\0') return 0;
        return strchr(path, '/') == 0;
    }
    if (strncmp(path, directory, directory_length) != 0 ||
        path[directory_length] != '/') return 0;
    child = path + directory_length + 1U;
    return child[0] != '\0' && strchr(child, '/') == 0;
}

static int host_file_index_at(const char* directory, int requested_index) {
    int current = 0;

    if (!directory || requested_index < 0) return -1;
    for (uint32_t index = 0U; index < HOST_FILE_CAPACITY; index++) {
        if (host_files[index].used &&
            host_is_direct_child(directory, host_files[index].path)) {
            if (current++ == requested_index) return (int)index;
        }
    }
    return -1;
}

static void host_copy_name(char* destination, const char* path) {
    const char* separator = strrchr(path, '/');
    const char* name = separator ? separator + 1U : path;
    size_t length = strlen(name);

    if (length >= 13U) length = 12U;
    memcpy(destination, name, length);
    destination[length] = '\0';
}

static void host_reset(void) {
    memset(host_files, 0, sizeof(host_files));
    memset(host_package_buffer, 0, sizeof(host_package_buffer));
    memset(host_package_v2, 0, sizeof(host_package_v2));
    memset(host_package_dependency, 0, sizeof(host_package_dependency));
    host_fs_type = FS_TYPE_NONE;
    host_loader_ready = 0U;
    host_loader_foreground = 0U;
    host_loader_run_result = ERR_UNAVAILABLE;
    host_fs_info_result = ERR_UNAVAILABLE;
    host_write_result = OK;
    host_next_pid = 100U;
}

uint8_t fs_get_type(void) {
    return host_fs_type;
}

void* kmalloc(uint32_t size) {
    return size <= sizeof(host_package_buffer) ? host_package_buffer : 0;
}

void kfree(void* ptr) {
    (void)ptr;
}

int fs_read_file(const char* filename, uint8_t* buffer, uint32_t max_size) {
    return host_read_file(filename, buffer, max_size);
}

int fs_read_file_at(const char* path, uint8_t* buffer, uint32_t max_size) {
    return host_read_file(path, buffer, max_size);
}

int fs_get_file_count_at(const char* dir_path) {
    int count = 0;

    if (!dir_path) return ERR_NULL;
    for (uint32_t index = 0U; index < HOST_FILE_CAPACITY; index++) {
        if (host_files[index].used &&
            host_is_direct_child(dir_path, host_files[index].path)) count++;
    }
    return count;
}

int fs_get_file_info_at(const char* dir_path, int index, char* name_out,
                        uint32_t* size_out, uint8_t* attr_out) {
    int file_index = host_file_index_at(dir_path, index);

    if (!dir_path || index < 0) return ERR_NULL;
    if (file_index < 0) return ERR_NOT_FOUND;
    if (name_out) host_copy_name(name_out, host_files[file_index].path);
    if (size_out) *size_out = host_files[file_index].size;
    if (attr_out) *attr_out = host_files[file_index].attributes;
    return OK;
}

int fs_get_info(fs_info_t* info) {
    if (!info) return ERR_NULL;
    if (host_fs_info_result != OK) return host_fs_info_result;
    memset(info, 0, sizeof(*info));
    info->bytes_per_sector = 512U;
    info->sectors_per_cluster = 1U;
    info->total_clusters = 4096U;
    info->free_clusters = 4096U;
    info->type = host_fs_type;
    return OK;
}

int fs_get_root_file_info(const char* filename, uint32_t* size_out,
                          uint8_t* attributes_out) {
    int file_index;

    if (!filename) return ERR_NULL;
    file_index = host_find_file(filename);
    if (file_index < 0 || strchr(filename, '/') != 0) return ERR_NOT_FOUND;
    if (size_out) *size_out = host_files[file_index].size;
    if (attributes_out) *attributes_out = host_files[file_index].attributes;
    return OK;
}

int fs_create_dir_entry(const char* dir_path, const char* name,
                        uint8_t attributes) {
    char path[FS_MAX_PATH];

    if (!dir_path || !name) return ERR_NULL;
    if (dir_path[0]) {
        snprintf(path, sizeof(path), "%s/%s", dir_path, name);
    } else {
        snprintf(path, sizeof(path), "%s", name);
    }
    if (host_find_file(path) >= 0) return ERR_STATE;
    return host_store_file(path, 0, 0U, attributes, FS_ATOMIC_CREATE_OR_REPLACE);
}

int fs_write_file_in_dir(const char* dir_path, const char* filename,
                         const uint8_t* data, uint32_t size) {
    char path[FS_MAX_PATH];

    if (!dir_path || !filename) return ERR_NULL;
    if (host_write_result != OK) return -1;
    snprintf(path, sizeof(path), "%s/%s", dir_path, filename);
    return host_store_file(path, data, size, FS_ATTRIBUTE_ARCHIVE,
                           FS_ATOMIC_CREATE_OR_REPLACE);
}

int fs_delete_file_in_dir(const char* dir_path, const char* filename) {
    char path[FS_MAX_PATH];

    if (!dir_path || !filename) return ERR_NULL;
    snprintf(path, sizeof(path), "%s/%s", dir_path, filename);
    return host_remove_file(path);
}

int fs_atomic_delete_file_in_dir(const char* dir_path, const char* filename) {
    return fs_delete_file_in_dir(dir_path, filename);
}

int fs_atomic_write_root(const char* filename, const uint8_t* data,
                         uint32_t size, uint8_t attributes,
                         fs_atomic_mode_t mode) {
    if (host_write_result != OK) return host_write_result;
    return host_store_file(filename, data, size, attributes, mode);
}

int fs_atomic_delete_root(const char* filename) {
    return host_remove_file(filename);
}

int fs_atomic_write_file_in_dir(const char* dir_path, const char* filename,
                                const uint8_t* data, uint32_t size,
                                uint8_t attributes, fs_atomic_mode_t mode) {
    char path[FS_MAX_PATH];

    if (!dir_path || !filename) return ERR_NULL;
    if (host_write_result != OK) return host_write_result;
    snprintf(path, sizeof(path), "%s/%s", dir_path, filename);
    return host_store_file(path, data, size, attributes, mode);
}

int app_loader_is_ready(void) {
    return host_loader_ready;
}

int app_loader_is_foreground_active(void) {
    return host_loader_foreground;
}

int app_loader_validate_image(const uint8_t* image, uint32_t size,
                              app_image_header_t* header) {
    uint32_t data_end;

    if (!image || !header) return ERR_NULL;
    if (size < APP_IMAGE_HEADER_SIZE || size > APP_IMAGE_MAX_FILE_SIZE) {
        return size > APP_IMAGE_MAX_FILE_SIZE ? ERR_OVERFLOW : ERR_INVALID;
    }
    memcpy(header, image, APP_IMAGE_HEADER_SIZE);
    if (memcmp(header->magic, "ZAPP", 4U) != 0 ||
        header->version != APP_IMAGE_VERSION ||
        header->architecture != APP_IMAGE_ARCH_I386 ||
        header->header_size != APP_IMAGE_HEADER_SIZE ||
        header->code_offset != APP_IMAGE_HEADER_SIZE ||
        header->code_size == 0U || header->code_size > APP_IMAGE_MAX_CODE_SIZE ||
        header->data_size > APP_IMAGE_MAX_DATA_SIZE ||
        header->stack_size != APP_IMAGE_STACK_SIZE ||
        header->flags != APP_IMAGE_FLAGS_NONE ||
        header->entry_offset >= header->code_size ||
        header->data_offset != header->code_offset + header->code_size) {
        return ERR_INVALID;
    }
    data_end = header->data_offset + header->data_size;
    return data_end == size ? OK : ERR_INVALID;
}

int app_loader_run_file_with_launch(const char* path,
                                    const app_launch_info_t* launch,
                                    uint32_t* pid_out) {
    (void)path;
    (void)launch;
    if (host_loader_run_result == OK && pid_out) *pid_out = host_next_pid++;
    return host_loader_run_result;
}

uint32_t timer_get_ticks(void) {
    return 100U;
}

uint8_t serial_is_ready(void) {
    return 1U;
}

uint32_t serial_write_text(const char* text, uint32_t length) {
    (void)text;
    return length;
}

uint32_t serial_flush(uint32_t budget) {
    return budget;
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void video_newline(void) {
}

static int expect_version(const char* left, const char* right, int expected) {
    int comparison = 99;

    if (app_package_compare_versions(left, right, &comparison) != OK) {
        return 0;
    }
    return comparison == expected;
}

static int test_names(void) {
    static const char* const reasons[] = {
        "NONE", "INVALID_ARGUMENT", "SOURCE_NOT_FOUND", "PACKAGE_INVALID",
        "ALIAS_MISMATCH", "DEPENDENCY_MISSING", "INSUFFICIENT_SPACE",
        "ALREADY_INSTALLED", "NOT_INSTALLED", "DEPENDENT_INSTALLED",
        "FILESYSTEM_UNAVAILABLE", "LOADER_UNAVAILABLE",
        "PACKAGE_SERVICE_UNAVAILABLE", "LOADER_BUSY", "MUTATION_BUSY",
        "READ_ERROR", "WRITE_ERROR", "UPDATE_NOT_AVAILABLE",
        "DOWNGRADE_REQUIRES_CONFIRM", "PLAN_INCOMPLETE", "PLAN_CYCLE",
        "PLAN_CONFLICT", "TRANSACTION_UNAVAILABLE", "TRANSACTION_PENDING",
        "ROLLBACK_UNAVAILABLE", "RECOVERY_FAILED", "HISTORY_UNAVAILABLE"
    };
    static const char* const operations[] = {
        "NONE", "INSTALL", "REMOVE", "UPDATE", "ROLLBACK", "RECOVERY"
    };
    static const char* const plan_actions[] = {
        "NONE", "INSTALL", "UPDATE"
    };
    static const char* const outcomes[] = {
        "NONE", "SUCCESS", "FAILED", "RECOVERED"
    };

    for (uint32_t index = 0U; index < sizeof(reasons) / sizeof(reasons[0]);
         index++) {
        if (strcmp(app_package_action_reason_name(
                       (app_package_action_reason_t)index), reasons[index]) != 0) {
            return 0;
        }
    }
    if (strcmp(app_package_action_reason_name((app_package_action_reason_t)99),
               "UNKNOWN") != 0) return 0;
    for (uint32_t index = 0U;
         index < sizeof(plan_actions) / sizeof(plan_actions[0]);
         index++) {
        if (strcmp(app_package_plan_action_name(
                       (app_package_plan_action_t)index), plan_actions[index]) != 0) {
            return 0;
        }
    }
    if (strcmp(app_package_plan_action_name((app_package_plan_action_t)99),
               "NONE") != 0) return 0;
    for (uint32_t index = 0U; index < sizeof(operations) / sizeof(operations[0]);
         index++) {
        if (strcmp(app_package_history_operation_name(
                       (app_package_history_operation_t)index),
                   operations[index]) != 0) return 0;
    }
    if (strcmp(app_package_history_operation_name(
                   (app_package_history_operation_t)99), "NONE") != 0) {
        return 0;
    }
    for (uint32_t index = 0U; index < sizeof(outcomes) / sizeof(outcomes[0]);
         index++) {
        if (strcmp(app_package_history_outcome_name(
                       (app_package_history_outcome_t)index), outcomes[index]) != 0) {
            return 0;
        }
    }
    return strcmp(app_package_history_outcome_name(
                      (app_package_history_outcome_t)99), "NONE") == 0;
}

static uint32_t host_crc32(const uint8_t* data, uint32_t size) {
    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t index = 0U; index < size; index++) {
        crc ^= data[index];
        for (uint32_t bit = 0U; bit < 8U; bit++) {
            crc = (crc & 1U) ? (crc >> 1U) ^ 0xEDB88320U : (crc >> 1U);
        }
    }
    return ~crc;
}

static uint32_t host_build_package(uint8_t* output, const char* id,
                                   const char* version,
                                   const char* dependencies) {
    char manifest[APP_PACKAGE_MAX_MANIFEST_SIZE];
    app_package_header_t package_header;
    app_image_header_t image_header;
    int manifest_size;
    uint32_t payload_size;

    manifest_size = snprintf(
        manifest, sizeof(manifest),
        "id=%s\nname=Host package\nversion=%s\napi=0.9\nentry=APP.ZAP\n"
        "dependencies=%s\n", id, version, dependencies);
    if (manifest_size <= 0 ||
        (uint32_t)manifest_size > APP_PACKAGE_MAX_MANIFEST_SIZE) return 0U;
    memset(&image_header, 0, sizeof(image_header));
    memcpy(image_header.magic, "ZAPP", 4U);
    image_header.version = APP_IMAGE_VERSION;
    image_header.architecture = APP_IMAGE_ARCH_I386;
    image_header.header_size = APP_IMAGE_HEADER_SIZE;
    image_header.code_offset = APP_IMAGE_HEADER_SIZE;
    image_header.code_size = 1U;
    image_header.data_offset = APP_IMAGE_HEADER_SIZE + 1U;
    image_header.data_size = 0U;
    image_header.entry_offset = 0U;
    image_header.stack_size = APP_IMAGE_STACK_SIZE;
    image_header.flags = APP_IMAGE_FLAGS_NONE;
    payload_size = APP_IMAGE_HEADER_SIZE + 1U;
    memset(&package_header, 0, sizeof(package_header));
    memcpy(package_header.magic, "ZPKG", 4U);
    package_header.version = APP_PACKAGE_VERSION;
    package_header.header_size = sizeof(app_package_header_t);
    package_header.architecture = APP_PACKAGE_ARCH_I386;
    package_header.manifest_size = (uint32_t)manifest_size;
    package_header.payload_size = payload_size;
    memcpy(output + sizeof(package_header), manifest, (size_t)manifest_size);
    memcpy(output + sizeof(package_header) + manifest_size,
           &image_header, sizeof(image_header));
    output[sizeof(package_header) + manifest_size + APP_IMAGE_HEADER_SIZE] =
        0xC3U;
    package_header.content_crc32 = host_crc32(
        output + sizeof(package_header),
        (uint32_t)manifest_size + payload_size);
    memcpy(output, &package_header, sizeof(package_header));
    return sizeof(package_header) + (uint32_t)manifest_size + payload_size;
}

static int host_prepare_package_files(void) {
    uint32_t package_size;
    uint32_t package_v2_size;
    uint32_t dependency_size;
    int result;

    host_reset();
    host_fs_type = FS_TYPE_FAT12;
    host_loader_ready = 1U;
    host_fs_info_result = OK;
    package_size = host_build_package(host_package_buffer, "DEMO", "1.0.0", "");
    package_v2_size = host_build_package(host_package_v2, "DEMO", "2.0.0", "");
    dependency_size = host_build_package(
        host_package_dependency, "DEPS", "1.0.0", "MISSING");
    if (!package_size || !package_v2_size || !dependency_size) return 0;
    result = host_store_file("DEMO.ZPK", host_package_buffer, package_size,
                             FS_ATTRIBUTE_ARCHIVE,
                             FS_ATOMIC_CREATE_OR_REPLACE);
    if (result != OK) return 0;
    result = host_store_file("SRC/DEMO.ZPK", host_package_v2,
                             package_v2_size, FS_ATTRIBUTE_ARCHIVE,
                             FS_ATOMIC_CREATE_OR_REPLACE);
    if (result != OK) return 0;
    result = host_store_file("DEPS.ZPK", host_package_dependency,
                             dependency_size, FS_ATTRIBUTE_ARCHIVE,
                             FS_ATOMIC_CREATE_OR_REPLACE);
    if (result != OK) return 0;
    host_package_buffer[0] = 'X';
    result = host_store_file("BAD.ZPK", host_package_buffer, package_size,
                             FS_ATTRIBUTE_ARCHIVE,
                             FS_ATOMIC_CREATE_OR_REPLACE);
    host_package_buffer[0] = 'Z';
    return result == OK;
}

static int test_unready_and_invalid(void) {
    app_package_action_result_t action;
    app_package_diagnostic_t diagnostic;
    app_package_history_entry_t history;
    app_package_info_t info;
    app_package_plan_t plan;
    app_package_status_t status;
    app_launch_info_t launch;
    uint32_t count = 0U;
    uint32_t pid = 0U;
    int package_error;

    memset(&action, 0, sizeof(action));
    memset(&diagnostic, 0, sizeof(diagnostic));
    memset(&history, 0, sizeof(history));
    memset(&info, 0, sizeof(info));
    memset(&plan, 0, sizeof(plan));
    memset(&status, 0, sizeof(status));
    memset(&launch, 0, sizeof(launch));
    if (app_package_verify_file("DEMO.ZPK", &info) != ERR_UNAVAILABLE ||
        app_package_get_installed_info(0, &info) != ERR_UNAVAILABLE ||
        app_package_get_installed_info_by_id("DEMO", &info) != ERR_UNAVAILABLE ||
        app_package_run_diagnostics(&diagnostic) != ERR_UNAVAILABLE ||
        app_package_init() != OK || !app_package_is_ready()) {
        return 0;
    }
    if (app_package_verify_file(0, &info) != ERR_NULL ||
        app_package_verify_file("DEMO.ZPK", 0) != ERR_NULL ||
        app_package_verify_file("MISSING.ZPK", &info) != ERR_NOT_FOUND ||
        app_package_verify_file("BAD.ZPK", &info) != ERR_INVALID ||
        app_package_get_status(0) != ERR_NULL ||
        app_package_get_history_count(0) != ERR_NULL ||
        app_package_get_history_count(&count) != OK || count != 0U ||
        app_package_get_history_entry(0U, 0) != ERR_NULL ||
        app_package_get_history_entry(0U, &history) != ERR_NOT_FOUND) {
        return 0;
    }
    if (app_package_get_installed_info(-1, &info) != ERR_INVALID ||
        app_package_get_installed_info(0, 0) != ERR_INVALID ||
        app_package_get_installed_info_by_id(0, &info) != ERR_INVALID ||
        app_package_get_installed_info_by_id("DEMO", 0) != ERR_INVALID ||
        app_package_get_installed_count() != 0 ||
        app_package_test_fail_after(0U) != ERR_INVALID ||
        app_package_test_fail_after(33U) != ERR_INVALID) {
        return 0;
    }
    if (app_package_preflight_install(0, &action) != ERR_NULL) {
        return 0;
    }
    if (app_package_preflight_install(".ZPK", &action) != ERR_INVALID) {
        return 0;
    }
    if (app_package_preflight_install("MISSING.ZPK", &action) != ERR_NOT_FOUND) {
        return 0;
    }
    package_error = app_package_preflight_install("DEPS.ZPK", &action);
    if (package_error != ERR_STATE) {
        return 0;
    }
    if (action.reason != APP_PACKAGE_ACTION_REASON_DEPENDENCY_MISSING ||
        action.blocker_count != 1U || strcmp(action.blocker_ids[0], "MISSING") != 0) {
        return 0;
    }
    if (app_package_install_confirmed(0, &action) != ERR_NULL ||
        app_package_install_file(0, &info) != ERR_NULL ||
        app_package_install_file("MISSING.ZPK", &info) != ERR_NOT_FOUND ||
        app_package_preflight_remove(0, &action) != ERR_NULL ||
        app_package_preflight_remove("MISSING", &action) != ERR_NOT_FOUND ||
        app_package_remove_confirmed(0, &action) != ERR_NULL ||
        app_package_remove(0) != ERR_NULL ||
        app_package_remove("MISSING") != ERR_NOT_FOUND ||
        app_package_run_installed(0, &launch, &pid, &action) != ERR_NULL ||
        app_package_run_installed("BAD", &launch, &pid, &action) != ERR_NOT_FOUND) {
        return 0;
    }
    if (app_package_preflight_plan(0, &action) != ERR_INVALID) {
        return 0;
    }
    if (app_package_preflight_plan(&plan, &action) != ERR_INVALID) {
        return 0;
    }
    if (app_package_preflight_plan_from_directory(0, "SRC", &action) != ERR_INVALID) {
        return 0;
    }
    if (app_package_preflight_plan_from_directory(&plan, 0, &action) != ERR_NULL) {
        return 0;
    }
    if (app_package_preflight_plan_from_directory(&plan, "SRC", 0) != ERR_NULL) {
        return 0;
    }
    if (app_package_apply_plan_confirmed(0, &action) != ERR_INVALID) {
        return 0;
    }
    if (app_package_apply_plan_confirmed(&plan, 0) != ERR_NULL) {
        return 0;
    }
    if (app_package_apply_plan_from_directory_confirmed(&plan, 0, &action) != ERR_NULL) {
        return 0;
    }
    if (app_package_apply_plan_from_directory_confirmed(0, "SRC", &action) != ERR_INVALID) {
        return 0;
    }
    if (app_package_preflight_rollback("bad", &action) != ERR_INVALID) {
        return 0;
    }
    if (app_package_preflight_rollback("DEMO", &action) != ERR_NOT_FOUND) {
        return 0;
    }
    if (app_package_rollback_confirmed(0, &action) != ERR_INVALID) {
        return 0;
    }
    if (app_package_rollback_confirmed("DEMO", 0) != ERR_NULL) {
        return 0;
    }
    if (app_package_run_diagnostics(&diagnostic) != OK ||
        !diagnostic.invalid_package || !diagnostic.missing_dependency ||
        !diagnostic.insufficient_space || !diagnostic.mutation_serialization ||
        !diagnostic.transaction_supported || !diagnostic.history_available ||
        app_package_get_status(&status) != OK || status.rollback_count != 0U ||
        app_package_is_mutation_active() != 0) {
        return 0;
    }
    return 1;
}

static void host_fill_plan(app_package_plan_t* plan, const char* from_version,
                           const char* to_version) {
    memset(plan, 0, sizeof(*plan));
    plan->entry_count = 1U;
    plan->target_index = 0U;
    plan->entries[0].action = APP_PACKAGE_PLAN_ACTION_UPDATE;
    memcpy(plan->entries[0].id, "DEMO", 5U);
    memcpy(plan->entries[0].alias, "DEMO.ZPK", 9U);
    memcpy(plan->entries[0].from_version, from_version,
           strlen(from_version) + 1U);
    memcpy(plan->entries[0].to_version, to_version,
           strlen(to_version) + 1U);
}

static int test_install_update_rollback(void) {
    app_package_action_result_t action;
    app_package_info_t info;
    app_package_plan_t plan;
    app_package_status_t status;
    app_launch_info_t launch;
    uint32_t history_count;
    uint32_t pid = 0U;

    memset(&action, 0, sizeof(action));
    memset(&info, 0, sizeof(info));
    memset(&status, 0, sizeof(status));
    memset(&launch, 0, sizeof(launch));
    if (app_package_preflight_install("DEMO.ZPK", &action) != OK ||
        action.plan.entry_count != 1U ||
        app_package_install_confirmed("DEMO.ZPK", &action) != OK ||
        strcmp(action.info.id, "DEMO") != 0 ||
        app_package_get_installed_count() != 1 ||
        app_package_get_installed_info(0, &info) != OK ||
        strcmp(info.version, "1.0.0") != 0) return 0;
    if (app_package_preflight_install("DEMO.ZPK", &action) != ERR_STATE ||
        action.reason != APP_PACKAGE_ACTION_REASON_ALREADY_INSTALLED ||
        app_package_install_file("DEMO.ZPK", &info) != ERR_STATE) return 0;
    host_loader_run_result = OK;
    if (app_package_run_installed("DEMO", &launch, &pid, &action) != OK ||
        pid == 0U) return 0;
    host_loader_run_result = ERR_STATE;
    if (app_package_run_installed("DEMO", &launch, &pid, &action) != ERR_STATE ||
        action.reason != APP_PACKAGE_ACTION_REASON_LOADER_BUSY) return 0;
    host_loader_run_result = ERR_UNAVAILABLE;
    if (app_package_run_installed("DEMO", &launch, &pid, &action) != ERR_UNAVAILABLE ||
        action.reason != APP_PACKAGE_ACTION_REASON_LOADER_UNAVAILABLE) return 0;
    host_loader_run_result = ERR_DISK;
    if (app_package_run_installed("DEMO", &launch, &pid, &action) != ERR_DISK ||
        action.reason != APP_PACKAGE_ACTION_REASON_READ_ERROR) return 0;
    host_loader_run_result = OK;
    host_fill_plan(&plan, "1.0.0", "2.0.0");
    if (app_package_preflight_plan_from_directory(&plan, "SRC", &action) != OK ||
        app_package_test_fail_after(1U) != OK ||
        app_package_apply_plan_from_directory_confirmed(&plan, "SRC", &action) !=
            ERR_DISK || action.reason != APP_PACKAGE_ACTION_REASON_WRITE_ERROR ||
        app_package_is_mutation_active() != 0 ||
        app_package_get_status(&status) != OK || !status.transaction_pending ||
        app_package_preflight_plan(&plan, &action) != ERR_STATE ||
        action.reason != APP_PACKAGE_ACTION_REASON_TRANSACTION_PENDING) return 0;
    if (app_package_init() != OK ||
        app_package_get_status(&status) != OK || status.transaction_pending ||
        app_package_get_history_count(&history_count) != OK ||
        history_count < 2U) return 0;
    if (app_package_apply_plan_from_directory_confirmed(&plan, "SRC", &action) !=
            OK || app_package_get_installed_info_by_id("DEMO", &info) != OK ||
        strcmp(info.version, "2.0.0") != 0 ||
        app_package_get_status(&status) != OK || !status.rollback_available ||
        status.rollback_count != 1U) return 0;
    if (app_package_init() != OK ||
        app_package_get_status(&status) != OK || !status.rollback_available ||
        status.rollback_count != 1U) return 0;
    if (app_package_preflight_rollback("DEMO", &action) != OK ||
        strcmp(action.plan.entries[0].to_version, "1.0.0") != 0 ||
        app_package_rollback_confirmed("DEMO", &action) != OK ||
        app_package_get_installed_info_by_id("DEMO", &info) != OK ||
        strcmp(info.version, "1.0.0") != 0 ||
        app_package_get_status(&status) != OK || status.rollback_available ||
        status.transaction_pending || app_package_is_mutation_active() != 0) return 0;
    if (app_package_preflight_remove("DEMO", &action) != OK ||
        app_package_remove_confirmed("DEMO", &action) != OK ||
        app_package_get_installed_count() != 0 ||
        app_package_get_history_count(&history_count) != OK || history_count < 5U ||
        app_package_remove("DEMO") != ERR_NOT_FOUND) return 0;
    host_fs_type = FS_TYPE_FAT32;
    if (app_package_init() != OK ||
        app_package_preflight_install("DEMO.ZPK", &action) != OK) return 0;
    host_write_result = ERR_DISK;
    if (app_package_install_confirmed("DEMO.ZPK", &action) != ERR_DISK ||
        action.reason != APP_PACKAGE_ACTION_REASON_WRITE_ERROR ||
        app_package_get_installed_count() != 0 ||
        app_package_is_mutation_active() != 0) return 0;
    host_write_result = OK;
    if (app_package_install_confirmed("DEMO.ZPK", &action) != OK ||
        app_package_get_installed_count() != 1 ||
        app_package_remove("DEMO") != OK ||
        app_package_get_installed_count() != 0 ||
        app_package_is_mutation_active() != 0) return 0;
    return 1;
}

int main(void) {
    int comparison;
    int result = 0;

    if (!host_prepare_package_files()) result = 1;
    coverage_active = 1U;
    log_init();
    if (!result && app_package_compare_versions(NULL, "1.0.0", &comparison) !=
                    ERR_NULL) result = 2;
    if (!result && app_package_compare_versions("1.0", "1.0.0", &comparison) !=
                    ERR_INVALID) result = 3;
    if (!result && !expect_version("1.2.3", "1.2.3", 0)) result = 4;
    if (!result && !expect_version("1.2.10", "1.2.3", 1)) result = 5;
    if (!result && !expect_version("01.002.0003", "1.2.3", 0)) result = 6;
    if (!result && !expect_version("0.0.0", "0.0.1", -1)) result = 7;
    if (!result && app_package_compare_versions("1.0.0", "1.0.0", NULL) !=
                    ERR_NULL) result = 8;
    if (!result && !test_unready_and_invalid()) result = 9;
    if (!result && !test_install_update_rollback()) result = 10;
    if (!result && !test_names()) result = 11;
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
