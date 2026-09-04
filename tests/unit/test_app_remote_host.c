#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/app_remote.h"
#include "core/app_remote_trust.h"
#include "core/crypto.h"
#include "core/errors.h"
#include "core/http.h"
#include "core/log.h"
#include "core/network_manager.h"
#include "core/string.h"
#include "fs/fs.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_FILE_CAPACITY 16384U
#define HOST_FILE_COUNT 64U
#define HOST_CATALOG_SIZE (APP_REMOTE_HEADER_SIZE + 2U * APP_REMOTE_ENTRY_SIZE + 64U)
#define HOST_PACKAGE_SIZE 4U

typedef struct {
    uint8_t present;
    uint8_t attributes;
    char path[FS_MAX_PATH];
    uint8_t data[HOST_FILE_CAPACITY];
    uint32_t size;
} host_file_t;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static host_file_t host_files[HOST_FILE_COUNT];
static uint8_t host_catalog[HOST_CATALOG_SIZE];
static uint32_t host_catalog_size;
static const uint8_t host_dependency_package[HOST_PACKAGE_SIZE] = {'D', 'E', 'P', '1'};
static const uint8_t host_application_package[HOST_PACKAGE_SIZE] = {'A', 'P', 'P', '1'};
static uint8_t host_network_ready;
static uint8_t host_package_ready;
static app_package_action_reason_t host_package_failure_reason;
static uint8_t host_installed_dependency;
static uint8_t host_installed_application;
static uint8_t host_application_updated;
static uint8_t host_http_pending;
static uint8_t host_http_request_count;
static http_status_t host_http_status;
static const uint8_t* host_http_body;
static uint32_t host_http_body_size;
static app_package_history_entry_t host_history;

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
    printf("ZCOV_BEGIN|case=host:core:app-remote|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:app-remote|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:app-remote|value=0x%08X\n",
           (uint32_t)result);
}

static void host_hash(const uint8_t* data, uint32_t size,
                      uint8_t hash[CRYPTO_SHA256_SIZE]) {
    uint32_t value = 2166136261U;

    for (uint32_t index = 0U; index < size; index++) {
        value ^= data[index];
        value *= 16777619U;
        value ^= value >> 13U;
    }
    for (uint32_t index = 0U; index < CRYPTO_SHA256_SIZE; index++) {
        uint32_t rotated = value ^ (index * 0x45D9F3BU);
        hash[index] = (uint8_t)(rotated >> ((index & 3U) * 8U));
    }
}

static void host_copy(char* destination, uint32_t capacity,
                      const char* source) {
    uint32_t length = (uint32_t)strlen(source);

    if (length >= capacity) length = capacity - 1U;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static int host_file_find(const char* path) {
    if (!path) return -1;
    for (uint32_t index = 0U; index < HOST_FILE_COUNT; index++) {
        if (host_files[index].present && strcmp(host_files[index].path, path) == 0) {
            return (int)index;
        }
    }
    return -1;
}

static int host_file_set(const char* path, const uint8_t* data,
                         uint32_t size, uint8_t attributes) {
    int index = host_file_find(path);

    if (!path || (size && !data)) return ERR_NULL;
    if (size > HOST_FILE_CAPACITY) return ERR_OVERFLOW;
    if (index < 0) {
        for (uint32_t slot = 0U; slot < HOST_FILE_COUNT; slot++) {
            if (!host_files[slot].present) {
                index = (int)slot;
                host_files[slot].present = 1U;
                host_copy(host_files[slot].path, sizeof(host_files[slot].path),
                          path);
                break;
            }
        }
    }
    if (index < 0) return ERR_UNAVAILABLE;
    host_files[index].attributes = attributes;
    host_files[index].size = size;
    if (size) memcpy(host_files[index].data, data, size);
    return OK;
}

static int host_file_remove(const char* path) {
    int index = host_file_find(path);

    if (index < 0) return ERR_NOT_FOUND;
    host_files[index].present = 0U;
    host_files[index].size = 0U;
    return OK;
}

static int host_path_in_directory(const char* path, const char* directory) {
    uint32_t directory_length;
    const char* remainder;

    if (!path || !directory) return 0;
    if (!directory[0]) return strchr(path, '/') == NULL;
    directory_length = (uint32_t)strlen(directory);
    if (strncmp(path, directory, directory_length) != 0 ||
        path[directory_length] != '/') return 0;
    remainder = path + directory_length + 1U;
    return remainder[0] != '\0' && strchr(remainder, '/') == NULL;
}

static void host_reset(void) {
    memset(host_files, 0, sizeof(host_files));
    memset(host_catalog, 0, sizeof(host_catalog));
    host_catalog_size = 0U;
    host_network_ready = 1U;
    host_package_ready = 1U;
    host_package_failure_reason = APP_PACKAGE_ACTION_REASON_NONE;
    host_installed_dependency = 0U;
    host_installed_application = 0U;
    host_application_updated = 0U;
    host_http_pending = 0U;
    host_http_request_count = 0U;
    memset(&host_http_status, 0, sizeof(host_http_status));
    memset(&host_history, 0, sizeof(host_history));
    host_history.sequence = 7U;
    host_history.operation = APP_PACKAGE_HISTORY_OPERATION_INSTALL;
    host_history.outcome = APP_PACKAGE_HISTORY_OUTCOME_SUCCESS;
    host_copy(host_history.id, sizeof(host_history.id), "APP1");
    host_copy(host_history.to_version, sizeof(host_history.to_version), "2.0.0");
}

static void host_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void host_write_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void host_write_fixed(uint8_t* data, uint32_t size, const char* value) {
    memset(data, 0, size);
    memcpy(data, value, strlen(value));
}

static void host_write_entry(uint8_t* raw, const char* id, const char* name,
                             const char* version, const char* dependency,
                             const uint8_t* package, const char* path) {
    uint8_t package_hash[CRYPTO_SHA256_SIZE];

    memset(raw, 0, APP_REMOTE_ENTRY_SIZE);
    host_write_fixed(raw, 9U, id);
    host_write_fixed(raw + 9U, 32U, name);
    host_write_fixed(raw + 41U, 16U, version);
    if (dependency) {
        raw[57U] = 1U;
        host_write_fixed(raw + 58U, 9U, dependency);
    }
    host_write_u32(raw + 96U, HOST_PACKAGE_SIZE);
    host_hash(package, HOST_PACKAGE_SIZE, package_hash);
    memcpy(raw + 100U, package_hash, sizeof(package_hash));
    host_write_fixed(raw + 132U, APP_REMOTE_PATH_SIZE, path);
}

static void host_build_catalog(void) {
    uint8_t entries_hash[CRYPTO_SHA256_SIZE];

    memset(host_catalog, 0, sizeof(host_catalog));
    host_catalog_size = HOST_CATALOG_SIZE;
    host_catalog[0] = 'Z';
    host_catalog[1] = 'A';
    host_catalog[2] = 'C';
    host_catalog[3] = '1';
    host_write_u16(host_catalog + 4U, 1U);
    host_write_u16(host_catalog + 6U, APP_REMOTE_HEADER_SIZE);
    host_write_u16(host_catalog + 8U, APP_REMOTE_ENTRY_SIZE);
    host_write_u16(host_catalog + 10U, 2U);
    host_write_u32(host_catalog + 12U, 1U);
    host_write_u16(host_catalog + 16U, 1U);
    host_write_u16(host_catalog + 18U, 1U);
    host_write_u16(host_catalog + 20U, 1U);
    memcpy(host_catalog + 24U, APP_REMOTE_TRUST_KEY_ID,
           sizeof(APP_REMOTE_TRUST_KEY_ID));
    host_write_entry(host_catalog + APP_REMOTE_HEADER_SIZE,
                     "APP1", "Application", "2.0.0", "DEP1",
                     host_application_package, "/apps/APP1.ZPK");
    host_write_entry(host_catalog + APP_REMOTE_HEADER_SIZE + APP_REMOTE_ENTRY_SIZE,
                     "DEP1", "Dependency", "1.0.0", NULL,
                     host_dependency_package, "/apps/DEP1.ZPK");
    host_hash(host_catalog + APP_REMOTE_HEADER_SIZE,
              2U * APP_REMOTE_ENTRY_SIZE, entries_hash);
    memcpy(host_catalog + 40U, entries_hash, sizeof(entries_hash));
    memset(host_catalog + 72U, 0, APP_REMOTE_HEADER_SIZE - 72U);
}

static void host_install_files(void) {
    static const uint8_t application_data[] = {'I', 'N', 'S', 'T'};
    static const uint8_t metadata_data[] = {'M', 'E', 'T', 'A'};

    host_file_set("APPS/APP1/APP.ZAP", application_data,
                  sizeof(application_data), FS_ATTRIBUTE_ARCHIVE);
    host_file_set("APPS/APP1/META.DAT", metadata_data,
                  sizeof(metadata_data), FS_ATTRIBUTE_ARCHIVE);
    host_file_set("APPS/DEP1/APP.ZAP", host_dependency_package,
                  sizeof(host_dependency_package), FS_ATTRIBUTE_ARCHIVE);
    host_file_set("APPS/DEP1/META.DAT", metadata_data,
                  sizeof(metadata_data), FS_ATTRIBUTE_ARCHIVE);
}

static int host_parse_version(const char* text, uint32_t values[3]) {
    const char* cursor = text;

    for (uint32_t part = 0U; part < 3U; part++) {
        uint32_t value = 0U;
        uint32_t digits = 0U;

        while (*cursor >= '0' && *cursor <= '9') {
            value = value * 10U + (uint32_t)(*cursor - '0');
            cursor++;
            digits++;
        }
        if (!digits) return ERR_INVALID;
        values[part] = value;
        if (part < 2U) {
            if (*cursor != '.') return ERR_INVALID;
            cursor++;
        }
    }
    return *cursor == '\0' ? OK : ERR_INVALID;
}

static void host_fill_info(app_package_info_t* info, const char* id,
                           const char* version) {
    memset(info, 0, sizeof(*info));
    host_copy(info->id, sizeof(info->id), id);
    host_copy(info->name, sizeof(info->name),
              strcmp(id, "APP1") == 0 ? "Application" : "Dependency");
    host_copy(info->version, sizeof(info->version), version);
    if (strcmp(id, "APP1") == 0) {
        info->dependency_count = 1U;
        host_copy(info->dependencies[0], sizeof(info->dependencies[0]), "DEP1");
    }
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

int crypto_sha256_init(crypto_sha256_ctx_t* context) {
    if (!context) return ERR_NULL;
    memset(context, 0, sizeof(*context));
    return OK;
}

int crypto_sha256_update(crypto_sha256_ctx_t* context, const uint8_t* data,
                         uint32_t size) {
    if (!context || (size && !data)) return ERR_NULL;
    context->total_size += size;
    return OK;
}

int crypto_sha256_final(crypto_sha256_ctx_t* context,
                        uint8_t hash[CRYPTO_SHA256_SIZE]) {
    if (!context || !hash) return ERR_NULL;
    memset(hash, (int)(context->total_size & 0xFFU), CRYPTO_SHA256_SIZE);
    return OK;
}

int crypto_sha256(const uint8_t* data, uint32_t size,
                  uint8_t hash[CRYPTO_SHA256_SIZE]) {
    if (!hash || (size && !data)) return ERR_NULL;
    host_hash(data, size, hash);
    return OK;
}

int crypto_sha512_digest(const uint8_t* data, uint32_t size,
                         uint8_t hash[CRYPTO_SHA512_SIZE]) {
    if (!hash || (size && !data)) return ERR_NULL;
    for (uint32_t index = 0U; index < CRYPTO_SHA512_SIZE; index++) {
        hash[index] = (uint8_t)(index ^ (size ? data[index % size] : 0U));
    }
    return OK;
}

int crypto_equal(const uint8_t* first, const uint8_t* second, uint32_t size) {
    uint8_t different = 0U;

    if (!first || !second) return 0;
    for (uint32_t index = 0U; index < size; index++) {
        different |= (uint8_t)(first[index] ^ second[index]);
    }
    return different == 0U;
}

int crypto_ed25519_verify_init(
    crypto_ed25519_verify_ctx_t* context,
    const uint8_t signature[CRYPTO_ED25519_SIGNATURE_SIZE],
    const uint8_t public_key[CRYPTO_ED25519_PUBLIC_KEY_SIZE]) {
    if (!context || !signature || !public_key) return ERR_NULL;
    context->active = 1U;
    return OK;
}

int crypto_ed25519_verify_update(crypto_ed25519_verify_ctx_t* context,
                                 const uint8_t* data, uint32_t size) {
    if (!context || (size && !data)) return ERR_NULL;
    return context->active ? OK : ERR_STATE;
}

int crypto_ed25519_verify_final(crypto_ed25519_verify_ctx_t* context) {
    if (!context) return ERR_NULL;
    return context->active ? OK : ERR_STATE;
}

int network_manager_get_status(network_manager_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    memset(status_out, 0, sizeof(*status_out));
    status_out->http_available = host_network_ready;
    status_out->ipv4_configured = host_network_ready;
    return OK;
}

static void host_select_http_body(const char* url) {
    host_http_body = NULL;
    host_http_body_size = 0U;
    if (strstr(url, "stable.zac")) {
        host_http_body = host_catalog;
        host_http_body_size = host_catalog_size;
    } else if (strstr(url, "DEP1.ZPK")) {
        host_http_body = host_dependency_package;
        host_http_body_size = HOST_PACKAGE_SIZE;
    } else if (strstr(url, "APP1.ZPK")) {
        host_http_body = host_application_package;
        host_http_body_size = HOST_PACKAGE_SIZE;
    }
}

int http_get_start(const char* url) {
    if (!url) return ERR_NULL;
    host_http_request_count++;
    host_select_http_body(url);
    memset(&host_http_status, 0, sizeof(host_http_status));
    host_http_status.state = host_http_pending ? HTTP_STATE_RECEIVING_BODY :
                             host_http_body ? HTTP_STATE_COMPLETE :
                             HTTP_STATE_FAILED;
    host_http_status.status_code = host_http_body ? 200U : 404U;
    host_http_status.body_length = host_http_body_size;
    host_http_status.content_length = host_http_body_size;
    host_http_status.has_content_length = host_http_body ? 1U : 0U;
    host_http_status.last_error = host_http_body ? OK : ERR_NOT_FOUND;
    return OK;
}

int http_get_status(http_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    *status_out = host_http_status;
    return OK;
}

int http_get_body(const uint8_t** body_out, uint32_t* size_out) {
    if (!body_out || !size_out) return ERR_NULL;
    *body_out = host_http_body;
    *size_out = host_http_body_size;
    return host_http_body ? OK : ERR_NOT_FOUND;
}

int http_reset(void) {
    host_http_status.state = HTTP_STATE_IDLE;
    host_http_pending = 0U;
    return OK;
}

void process_block(uint32_t ticks) {
    (void)ticks;
}

uint8_t fs_get_type(void) {
    return FS_TYPE_FAT12;
}

int fs_get_file_count_at(const char* directory) {
    int count = 0;

    if (!directory) return ERR_NULL;
    for (uint32_t index = 0U; index < HOST_FILE_COUNT; index++) {
        if (host_files[index].present &&
            host_path_in_directory(host_files[index].path, directory)) count++;
    }
    return count;
}

int fs_get_file_info_at(const char* directory, int index, char* name_out,
                        uint32_t* size_out, uint8_t* attributes_out) {
    int current = 0;

    if (!directory || !name_out || !attributes_out) return ERR_NULL;
    for (uint32_t slot = 0U; slot < HOST_FILE_COUNT; slot++) {
        if (!host_files[slot].present ||
            !host_path_in_directory(host_files[slot].path, directory)) continue;
        if (current++ != index) continue;
        const char* name = strrchr(host_files[slot].path, '/');
        name = name ? name + 1 : host_files[slot].path;
        host_copy(name_out, 13U, name);
        if (size_out) *size_out = host_files[slot].size;
        *attributes_out = host_files[slot].attributes;
        return OK;
    }
    return ERR_NOT_FOUND;
}

int fs_read_file(const char* filename, uint8_t* buffer, uint32_t max_size) {
    int result = fs_read_file_at(filename, buffer, max_size);

    return result == ERR_NOT_FOUND ? -ERR_NOT_FOUND : result;
}

int fs_read_file_at(const char* path, uint8_t* buffer, uint32_t max_size) {
    int index = host_file_find(path);

    if (!path || !buffer) return ERR_NULL;
    if (index < 0) return ERR_NOT_FOUND;
    if (max_size < host_files[index].size) return ERR_OVERFLOW;
    if (host_files[index].size) {
        memcpy(buffer, host_files[index].data, host_files[index].size);
    }
    return (int)host_files[index].size;
}

int fs_get_root_file_info(const char* filename, uint32_t* size_out,
                          uint8_t* attributes_out) {
    int index;

    if (!filename || !size_out) return ERR_NULL;
    index = host_file_find(filename);
    if (index < 0) return ERR_NOT_FOUND;
    *size_out = host_files[index].size;
    if (attributes_out) *attributes_out = host_files[index].attributes;
    return OK;
}

int fs_atomic_write_root(const char* filename, const uint8_t* data,
                         uint32_t size, uint8_t attributes,
                         fs_atomic_mode_t mode) {
    if (mode == FS_ATOMIC_REPLACE_ONLY && host_file_find(filename) < 0) {
        return ERR_NOT_FOUND;
    }
    return host_file_set(filename, data, size, attributes);
}

int fs_atomic_delete_root(const char* filename) {
    return host_file_remove(filename);
}

static int host_join_path(char output[FS_MAX_PATH], const char* directory,
                          const char* name) {
    uint32_t directory_length;
    uint32_t name_length;

    if (!output || !directory || !name) return ERR_NULL;
    directory_length = (uint32_t)strlen(directory);
    name_length = (uint32_t)strlen(name);
    if (directory_length + name_length + 2U > FS_MAX_PATH) return ERR_OVERFLOW;
    memcpy(output, directory, directory_length);
    output[directory_length] = '/';
    memcpy(output + directory_length + 1U, name, name_length + 1U);
    return OK;
}

int fs_create_dir_entry(const char* directory, const char* name,
                        uint8_t attributes) {
    char path[FS_MAX_PATH];
    int result = host_join_path(path, directory, name);

    if (result != OK) return result;
    return host_file_set(path, NULL, 0U, attributes);
}

int fs_atomic_write_file_in_dir(const char* directory, const char* name,
                                const uint8_t* data, uint32_t size,
                                uint8_t attributes, fs_atomic_mode_t mode) {
    char path[FS_MAX_PATH];
    int result = host_join_path(path, directory, name);

    if (result != OK) return result;
    if (mode == FS_ATOMIC_REPLACE_ONLY && host_file_find(path) < 0) {
        return ERR_NOT_FOUND;
    }
    return host_file_set(path, data, size, attributes);
}

int fs_atomic_delete_file_in_dir(const char* directory, const char* name) {
    char path[FS_MAX_PATH];
    int result = host_join_path(path, directory, name);

    if (result != OK) return result;
    return host_file_remove(path);
}

int fs_write_file_in_dir(const char* directory, const char* name,
                         const uint8_t* data, uint32_t size) {
    return fs_atomic_write_file_in_dir(directory, name, data, size,
                                       FS_ATTRIBUTE_ARCHIVE,
                                       FS_ATOMIC_CREATE_OR_REPLACE);
}

int fs_get_info(fs_info_t* info_out) {
    if (!info_out) return ERR_NULL;
    memset(info_out, 0, sizeof(*info_out));
    info_out->bytes_per_sector = 512U;
    info_out->sectors_per_cluster = 1U;
    info_out->free_clusters = 4096U;
    info_out->total_clusters = 8192U;
    info_out->type = FS_TYPE_FAT12;
    return OK;
}

int fs_get_file_count(void) {
    return fs_get_file_count_at("");
}

int fs_write_file(const char* filename, const uint8_t* data, uint32_t size) {
    return fs_atomic_write_root(filename, data, size, FS_ATTRIBUTE_ARCHIVE,
                                 FS_ATOMIC_CREATE_OR_REPLACE);
}

int fs_delete_file(const char* filename) {
    return fs_atomic_delete_root(filename);
}

int app_package_is_ready(void) {
    return host_package_ready;
}

int app_package_compare_versions(const char* left, const char* right,
                                 int* comparison_out) {
    uint32_t left_values[3];
    uint32_t right_values[3];
    int result;

    if (!left || !right || !comparison_out) return ERR_NULL;
    result = host_parse_version(left, left_values);
    if (result != OK) return result;
    result = host_parse_version(right, right_values);
    if (result != OK) return result;
    *comparison_out = 0;
    for (uint32_t index = 0U; index < 3U; index++) {
        if (left_values[index] > right_values[index]) {
            *comparison_out = 1;
            break;
        }
        if (left_values[index] < right_values[index]) {
            *comparison_out = -1;
            break;
        }
    }
    return OK;
}

int app_package_get_installed_info_by_id(const char* id,
                                         app_package_info_t* info_out) {
    if (!id || !info_out) return ERR_NULL;
    if (strcmp(id, "DEP1") == 0 && host_installed_dependency) {
        host_fill_info(info_out, "DEP1", "1.0.0");
        return OK;
    }
    if (strcmp(id, "APP1") == 0 && host_installed_application) {
        host_fill_info(info_out, "APP1",
                       host_application_updated ? "2.0.0" : "1.0.0");
        return OK;
    }
    return ERR_NOT_FOUND;
}

int app_package_get_installed_count(void) {
    return (host_installed_dependency ? 1 : 0) +
           (host_installed_application ? 1 : 0);
}

int app_package_get_installed_info(int index, app_package_info_t* info_out) {
    if (!info_out || index < 0) return ERR_NULL;
    if (host_installed_dependency && index == 0) {
        host_fill_info(info_out, "DEP1", "1.0.0");
        return OK;
    }
    if (host_installed_application &&
        index == (host_installed_dependency ? 1 : 0)) {
        host_fill_info(info_out, "APP1",
                       host_application_updated ? "2.0.0" : "1.0.0");
        return OK;
    }
    return ERR_NOT_FOUND;
}

int app_package_verify_file(const char* path, app_package_info_t* info_out) {
    if (!path || !info_out) return ERR_NULL;
    if (strstr(path, "DEP1.ZPK") || strstr(path, "APPS/DEP1/APP.ZAP")) {
        host_fill_info(info_out, "DEP1", "1.0.0");
        return OK;
    }
    if (strstr(path, "APP1.ZPK") || strstr(path, "APPS/APP1/APP.ZAP")) {
        host_fill_info(info_out, "APP1", "2.0.0");
        return OK;
    }
    return ERR_NOT_FOUND;
}

int app_package_get_status(app_package_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    memset(status_out, 0, sizeof(*status_out));
    status_out->transaction_supported = 1U;
    status_out->history_available = 1U;
    status_out->last_history = host_history;
    return OK;
}

int app_package_get_history_count(uint32_t* count_out) {
    if (!count_out) return ERR_NULL;
    *count_out = 1U;
    return OK;
}

int app_package_get_history_entry(uint32_t newest_index,
                                  app_package_history_entry_t* entry_out) {
    if (!entry_out) return ERR_NULL;
    if (newest_index != 0U) return ERR_NOT_FOUND;
    *entry_out = host_history;
    return OK;
}

int app_package_preflight_plan_from_directory(
    const app_package_plan_t* plan, const char* source_directory,
    app_package_action_result_t* result_out) {
    if (!plan || !source_directory || !result_out) return ERR_NULL;
    memset(result_out, 0, sizeof(*result_out));
    result_out->plan = *plan;
    if (host_package_failure_reason != APP_PACKAGE_ACTION_REASON_NONE) {
        result_out->reason = host_package_failure_reason;
        return ERR_INVALID;
    }
    return OK;
}

int app_package_apply_plan_from_directory_confirmed(
    const app_package_plan_t* plan, const char* source_directory,
    app_package_action_result_t* result_out) {
    if (!plan || !source_directory || !result_out) return ERR_NULL;
    memset(result_out, 0, sizeof(*result_out));
    result_out->plan = *plan;
    if (host_package_failure_reason != APP_PACKAGE_ACTION_REASON_NONE) {
        result_out->reason = host_package_failure_reason;
        return ERR_INVALID;
    }
    host_installed_dependency = 1U;
    host_installed_application = 1U;
    host_application_updated = 1U;
    host_install_files();
    return OK;
}

int app_package_is_mutation_active(void) {
    return 0;
}

static int host_check(int condition, const char* label) {
    if (condition) return 0;
    printf("APP_REMOTE_FAIL:%s\n", label);
    return 1;
}

static int host_cancel(void* context) {
    (void)context;
    return 1;
}

static int host_test_contracts(void) {
    app_remote_result_t result;
    app_remote_status_t status;
    app_remote_entry_t entry;
    app_package_plan_t plan;
    app_remote_options_t options;
    uint32_t count = 0U;
    int comparison = 0;
    int package_result;
    int failures = 0;

    host_reset();
    host_build_catalog();
    failures += host_check(app_remote_get_status(NULL) == ERR_NULL,
                           "status_null");
    failures += host_check(app_remote_init() == OK, "init");
    failures += host_check(app_remote_state_name(APP_REMOTE_STATE_READY) != NULL,
                           "state_name");
    failures += host_check(app_remote_reason_name(APP_REMOTE_REASON_NETWORK) != NULL,
                           "reason_name");
    failures += host_check(app_remote_cache_state_name(APP_REMOTE_CACHE_EMPTY) != NULL,
                           "cache_name");
    failures += host_check(app_remote_entry_state_name(APP_REMOTE_ENTRY_AVAILABLE) != NULL,
                           "entry_name");
    failures += host_check(app_remote_get_status(&status) == OK &&
                           status.initialized && !status.enabled,
                           "initial_status");
    failures += host_check(app_remote_enable() == OK, "enable");
    failures += host_check(app_remote_get_count(&count) == ERR_UNAVAILABLE && count == 0U,
                           "empty_count");
    failures += host_check(app_remote_find_entry("APP1", &entry) == ERR_NOT_FOUND,
                           "empty_find");
    failures += host_check(app_remote_build_plan("APP1", 0, 0, &plan, &result) ==
                           ERR_UNAVAILABLE,
                           "empty_plan");
    failures += host_check(app_remote_check("http://repo/stable.zac", NULL,
                                            &result) == OK,
                           "check_catalog");
    failures += host_check(app_remote_get_count(&count) == OK && count == 2U,
                           "catalog_count");
    failures += host_check(app_remote_get_entry(0U, &entry) == OK &&
                           strcmp(entry.info.id, "APP1") == 0,
                           "catalog_entry");
    failures += host_check(app_remote_find_entry("APP1", &entry) == OK &&
                           entry.state == APP_REMOTE_ENTRY_AVAILABLE,
                           "catalog_find");
    failures += host_check(app_remote_build_plan("APP1", 0, 0, &plan, &result) == OK &&
                           plan.entry_count == 2U && plan.target_index == 1U &&
                           strcmp(plan.entries[0].id, "DEP1") == 0 &&
                           strcmp(plan.entries[1].alias, "APP1.ZPK") == 0,
                           "install_plan");
    failures += host_check(app_package_compare_versions("2.0.0", "1.0.0",
                                                        &comparison) == OK &&
                           comparison > 0,
                           "version_compare");
    failures += host_check(app_remote_fetch("APP1", "http://repo/stable.zac", 0,
                                            NULL, &result) == OK &&
                           result.required_clusters > 0U,
                           "fetch_preflight");
    failures += host_check(app_remote_apply_cached("APP1", 0, 0, NULL, &result) ==
                           ERR_NOT_FOUND &&
                           result.reason == APP_REMOTE_REASON_CACHE_UNAVAILABLE,
                           "apply_without_cache");
    failures += host_check(app_remote_check("http://repo/stable.zac", NULL,
                                            &result) == OK,
                           "check_before_publish");
    failures += host_check(app_remote_fetch("APP1", "http://repo/stable.zac", 1,
                                            NULL, &result) == OK &&
                           result.cache_published && result.plan.entry_count == 2U,
                           "fetch_publish");
    failures += host_check(app_remote_get_status(&status) == OK &&
                           status.cache_state == APP_REMOTE_CACHE_VALID &&
                           status.cache_target_available,
                           "published_status");
    host_installed_dependency = 1U;
    host_installed_application = 1U;
    host_install_files();
    failures += host_check(app_remote_apply_cached("APP1", 1, 0, NULL, &result) == OK &&
                           result.plan.entries[result.plan.target_index].action ==
                               APP_PACKAGE_PLAN_ACTION_UPDATE,
                           "cached_preflight");
    failures += host_check(app_remote_apply_cached("APP1", 1, 1, NULL, &result) == OK,
                           "cached_apply");
    failures += host_check(app_remote_refresh_provenance() == OK &&
                           app_remote_is_provenance_available() &&
                           app_remote_get_installed_trust("APP1", "2.0.0"),
                           "provenance");
    host_installed_dependency = 0U;
    host_installed_application = 0U;
    host_application_updated = 0U;
    failures += host_check(app_remote_check("http://repo/stable.zac", NULL,
                                            &result) == OK,
                           "refresh_before_package_reason");
    failures += host_check(app_remote_build_plan("APP1", 0, 0, &plan, &result) == OK,
                           "plan_before_package_reason");
    host_package_failure_reason = APP_PACKAGE_ACTION_REASON_PACKAGE_INVALID;
    package_result = app_remote_apply_cached("APP1", 0, 0, NULL, &result);
    failures += host_check(package_result == ERR_INVALID &&
                           result.reason == APP_REMOTE_REASON_PACKAGE_VERIFY,
                           "package_reason");
    host_package_failure_reason = APP_PACKAGE_ACTION_REASON_NONE;
    failures += host_check(app_remote_test_fail_after(0U) == ERR_INVALID,
                           "failpoint_invalid");
    failures += host_check(app_remote_clear(0, &result) == OK, "clear_dry_run");
    failures += host_check(app_remote_clear(1, &result) == OK, "clear_cache");
    failures += host_check(app_remote_disable() == OK, "disable");
    failures += host_check(app_remote_check("http://repo/stable.zac", NULL,
                                            &result) == ERR_UNAVAILABLE &&
                           result.reason == APP_REMOTE_REASON_DISABLED,
                           "disabled_check");
    failures += host_check(app_remote_enable() == OK, "re_enable");
    host_installed_dependency = 0U;
    host_installed_application = 0U;
    host_application_updated = 0U;
    options.cancel_check = host_cancel;
    options.cancel_context = NULL;
    options.allow_downgrade = 0U;
    host_http_pending = 1U;
    failures += host_check(app_remote_check("http://repo/stable.zac", &options,
                                            &result) == ERR_TIMEOUT &&
                           result.reason == APP_REMOTE_REASON_CANCELLED,
                           "cancel_check");
    host_http_pending = 0U;
    failures += host_check(app_remote_test_fail_after(1U) == OK, "failpoint_set");
    failures += host_check(app_remote_check("http://repo/stable.zac", NULL,
                                            &result) == OK, "check_before_failure");
    failures += host_check(app_remote_fetch("APP1", "http://repo/stable.zac", 1,
                                            NULL, &result) == ERR_DISK,
                           "publish_failure");
    failures += host_check(app_remote_init() == OK, "pending_recovery");
    failures += host_check(app_remote_get_status(&status) == OK &&
                           !status.cache_pending &&
                           status.cache_state == APP_REMOTE_CACHE_EMPTY,
                           "recovered_status");
    app_remote_request_cancel();
    return failures;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = host_test_contracts();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != 0) return result;
    puts("app-remote-host: PASS");
    return 0;
}
