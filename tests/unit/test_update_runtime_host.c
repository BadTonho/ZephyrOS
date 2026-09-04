#include <stdint.h>
#include <stdio.h>

#include "core/crypto.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/update.h"
#include "core/update_remote_runtime.h"
#include "core/update_runtime.h"
#include "core/update_trust.h"
#include "fs/fs.h"
#include "update_runtime_host.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_FS_FILE_CAPACITY 48U
#define HOST_FS_FILE_SIZE 65536U
#define HOST_PACKAGE_SIZE 2048U
#define HOST_PACKAGE_FILE "RUNTIME.ZUP"
#define HOST_CACHE_MANIFEST "ZUM2.CAC"
#define HOST_CACHE_ASSET_PREFIX "CACHE."
#define HOST_CATALOG_COUNT 3U
#define HOST_PACKAGE_ENTRY_COUNT 3U
#define HOST_PACKAGE_HEADER_SIZE 128U
#define HOST_PACKAGE_ENTRY_SIZE 128U
#define HOST_PACKAGE_PAYLOAD_OFFSET \
    (HOST_PACKAGE_HEADER_SIZE + HOST_PACKAGE_ENTRY_COUNT * \
     HOST_PACKAGE_ENTRY_SIZE)
#define HOST_PACKAGE_PAYLOAD_SIZE 12U
#define HOST_PACKAGE_SIGNATURE_OFFSET \
    (HOST_PACKAGE_PAYLOAD_OFFSET + HOST_PACKAGE_PAYLOAD_SIZE)
#define HOST_PACKAGE_TOTAL_SIZE (HOST_PACKAGE_SIGNATURE_OFFSET + 64U)
#define HOST_PACKAGE_ENTRY_PAYLOAD_OFFSET 64U
#define HOST_PACKAGE_ENTRY_PAYLOAD_SIZE 68U
#define HOST_PACKAGE_ENTRY_PRESENT 72U
#define HOST_PACKAGE_ENTRY_OPERATION 73U
#define HOST_PACKAGE_ENTRY_HASH 76U
#define HOST_PACKAGE_HEADER_FORMAT 4U
#define HOST_PACKAGE_HEADER_SIZE_OFFSET 6U
#define HOST_PACKAGE_HEADER_ARCH 8U
#define HOST_PACKAGE_HEADER_TOTAL_SIZE 16U
#define HOST_PACKAGE_HEADER_PAYLOAD_OFFSET 20U
#define HOST_PACKAGE_HEADER_PAYLOAD_SIZE 24U
#define HOST_PACKAGE_HEADER_SIGNATURE_OFFSET 28U
#define HOST_PACKAGE_HEADER_SIGNATURE_SIZE 32U
#define HOST_PACKAGE_HEADER_GENERATION 36U
#define HOST_PACKAGE_HEADER_TARGET_VERSION 40U
#define HOST_PACKAGE_HEADER_TARGET_EPOCH 46U
#define HOST_PACKAGE_HEADER_ENTRY_COUNT 50U
#define HOST_PACKAGE_HEADER_ENTRY_SIZE 52U
#define HOST_PACKAGE_HEADER_KEY_ID 56U
#define HOST_PACKAGE_HEADER_BASE_VERSION 74U
#define HOST_PACKAGE_HEADER_BASE_EPOCH 80U

typedef struct {
    uint8_t used;
    uint8_t attributes;
    uint32_t size;
    char name[FS_MAX_FILENAME];
    uint8_t data[HOST_FS_FILE_SIZE];
} host_fs_file_t;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t host_fs_type;
static host_fs_file_t host_fs_files[HOST_FS_FILE_CAPACITY];
static uint8_t host_fail_active_write_once;
static uint8_t host_update_version_available = 1U;
static uint8_t host_legacy_transaction_pending;
static uint8_t host_cache_available;
static update_runtime_cache_t host_cache;
static uint8_t host_package[HOST_PACKAGE_SIZE];

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
    printf("ZCOV_BEGIN|case=host:core:update-runtime|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:update-runtime|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:update-runtime|value=0x%08X\n",
           (uint32_t)result);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

int crypto_sha256_init(crypto_sha256_ctx_t* context) {
    if (!context) return ERR_NULL;
    for (uint32_t index = 0U; index < 8U; index++) {
        context->state[index] = 0x9E3779B9U + index * 0x10001U;
    }
    context->total_size = 0U;
    context->buffer_size = 0U;
    return OK;
}

int crypto_sha256_update(crypto_sha256_ctx_t* context, const uint8_t* data,
                         uint32_t size) {
    if (!context || (size && !data)) return ERR_NULL;
    for (uint32_t index = 0U; index < size; index++) {
        uint32_t slot = (uint32_t)((context->total_size + index) & 7U);
        context->state[slot] = (context->state[slot] << 5U) ^
                               (context->state[slot] >> 2U) ^ data[index];
    }
    context->total_size += size;
    return OK;
}

int crypto_sha256_final(crypto_sha256_ctx_t* context,
                        uint8_t hash[CRYPTO_SHA256_SIZE]) {
    if (!context || !hash) return ERR_NULL;
    for (uint32_t index = 0U; index < CRYPTO_SHA256_SIZE; index++) {
        uint32_t word = context->state[index / 4U] +
                        (uint32_t)context->total_size + index * 0x45D9F3BU;
        hash[index] = (uint8_t)(word >> ((index & 3U) * 8U));
    }
    return OK;
}

int crypto_sha256(const uint8_t* data, uint32_t size,
                  uint8_t hash[CRYPTO_SHA256_SIZE]) {
    crypto_sha256_ctx_t context;
    int result = crypto_sha256_init(&context);

    if (result != OK) return result;
    result = crypto_sha256_update(&context, data, size);
    if (result != OK) return result;
    return crypto_sha256_final(&context, hash);
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

int crypto_self_test(void) {
    return OK;
}

static int host_fs_find(const char* path) {
    if (!path) return -1;
    for (uint32_t index = 0U; index < HOST_FS_FILE_CAPACITY; index++) {
        if (host_fs_files[index].used &&
            kstrcmp(host_fs_files[index].name, path) == 0) return (int)index;
    }
    return -1;
}

static void host_fs_reset(void) {
    kmemset(host_fs_files, 0, sizeof(host_fs_files));
    host_fs_type = FS_TYPE_NONE;
    host_fail_active_write_once = 0U;
}

static int host_fs_put(const char* path, const uint8_t* data, uint32_t size,
                       uint8_t attributes) {
    int file_index = host_fs_find(path);

    if (!path || (size && !data) || size > HOST_FS_FILE_SIZE) return ERR_INVALID;
    if (file_index < 0) {
        for (uint32_t index = 0U; index < HOST_FS_FILE_CAPACITY; index++) {
            if (!host_fs_files[index].used) {
                file_index = (int)index;
                break;
            }
        }
    }
    if (file_index < 0) return ERR_UNAVAILABLE;
    host_fs_files[file_index].used = 1U;
    host_fs_files[file_index].attributes = attributes;
    kmemset(host_fs_files[file_index].name, 0, FS_MAX_FILENAME);
    kmemcpy(host_fs_files[file_index].name, path, kstrlen(path));
    host_fs_files[file_index].size = size;
    kmemset(host_fs_files[file_index].data, 0, HOST_FS_FILE_SIZE);
    if (size) kmemcpy(host_fs_files[file_index].data, data, size);
    return OK;
}

static int host_fs_matches(const char* path, const uint8_t* data,
                           uint32_t size) {
    int file_index = host_fs_find(path);

    if (file_index < 0 || host_fs_files[file_index].size != size) return 0;
    for (uint32_t index = 0U; index < size; index++) {
        if (host_fs_files[file_index].data[index] != data[index]) return 0;
    }
    return 1;
}

static void host_write_u16(uint8_t* raw, uint32_t offset, uint16_t value) {
    raw[offset] = (uint8_t)value;
    raw[offset + 1U] = (uint8_t)(value >> 8U);
}

static void host_write_u32(uint8_t* raw, uint32_t offset, uint32_t value) {
    raw[offset] = (uint8_t)value;
    raw[offset + 1U] = (uint8_t)(value >> 8U);
    raw[offset + 2U] = (uint8_t)(value >> 16U);
    raw[offset + 3U] = (uint8_t)(value >> 24U);
}

static void host_prepare_targets(uint8_t seed) {
    static const char* paths[HOST_CATALOG_COUNT] = {
        "EXPLORER.BMP", "SHELL.BMP", "TASKMGR.BMP"
    };
    uint8_t data[4U];

    for (uint32_t index = 0U; index < HOST_CATALOG_COUNT; index++) {
        for (uint32_t byte = 0U; byte < sizeof(data); byte++) {
            data[byte] = (uint8_t)(seed + index * 0x10U + byte);
        }
        (void)host_fs_put(paths[index], data, sizeof(data), FS_ATTRIBUTE_ARCHIVE);
    }
}

int fs_get_root_file_info(const char* filename, uint32_t* size_out,
                          uint8_t* attributes_out) {
    int file_index = host_fs_find(filename);

    if (!filename || !size_out) return ERR_NULL;
    if (file_index < 0) return ERR_NOT_FOUND;
    *size_out = host_fs_files[file_index].size;
    if (attributes_out) *attributes_out = host_fs_files[file_index].attributes;
    return OK;
}

int fs_read_file_range_at(const char* path, uint32_t offset,
                          uint8_t* buffer, uint32_t max_size,
                          uint32_t* bytes_read) {
    uint32_t available;
    int file_index = host_fs_find(path);

    if (bytes_read) *bytes_read = 0U;
    if (!path || !buffer || !bytes_read) return ERR_NULL;
    if (file_index < 0 || offset > host_fs_files[file_index].size) {
        return ERR_NOT_FOUND;
    }
    available = host_fs_files[file_index].size - offset;
    if (max_size > available) return ERR_DISK;
    kmemcpy(buffer, host_fs_files[file_index].data + offset, max_size);
    *bytes_read = max_size;
    return OK;
}

int fs_read_file_at(const char* path, uint8_t* buffer, uint32_t max_size) {
    int file_index = host_fs_find(path);

    if (!path || !buffer) return ERR_NULL;
    if (file_index < 0 || host_fs_files[file_index].size > max_size) {
        return ERR_NOT_FOUND;
    }
    kmemcpy(buffer, host_fs_files[file_index].data,
            host_fs_files[file_index].size);
    return (int)host_fs_files[file_index].size;
}

int fs_atomic_write_root(const char* filename, const uint8_t* data,
                         uint32_t size, uint8_t attributes,
                         fs_atomic_mode_t mode) {
    int file_index;

    if (!filename || (size && !data) || size > HOST_FS_FILE_SIZE) {
        return ERR_INVALID;
    }
    file_index = host_fs_find(filename);
    if (mode == FS_ATOMIC_REPLACE_ONLY && file_index < 0) return ERR_NOT_FOUND;
    if (host_fail_active_write_once &&
        (kstrcmp(filename, "EXPLORER.BMP") == 0 ||
         kstrcmp(filename, "SHELL.BMP") == 0 ||
         kstrcmp(filename, "TASKMGR.BMP") == 0)) {
        host_fail_active_write_once = 0U;
        return ERR_DISK;
    }
    return host_fs_put(filename, data, size, attributes);
}

int fs_atomic_delete_root(const char* filename) {
    int file_index = host_fs_find(filename);

    if (file_index < 0) return ERR_NOT_FOUND;
    host_fs_files[file_index].used = 0U;
    return OK;
}

uint8_t fs_get_type(void) {
    return host_fs_type;
}

int fs_get_info(fs_info_t* info) {
    if (!info) return ERR_NULL;
    if (host_fs_type != FS_TYPE_FAT12) {
        *info = (fs_info_t){0};
        return ERR_UNAVAILABLE;
    }
    *info = (fs_info_t){
        .total_sectors = 4096U,
        .free_sectors = 2048U,
        .bytes_per_sector = 512U,
        .sectors_per_cluster = 1U,
        .total_clusters = 4096U,
        .free_clusters = 2048U,
        .type = FS_TYPE_FAT12,
    };
    kmemcpy(info->label, "HOSTFAT12", 9U);
    return OK;
}

static void host_prepare_package(update_version_t base, uint32_t base_epoch,
                                 update_version_t target, uint32_t target_epoch,
                                 uint8_t seed) {
    static const char* paths[HOST_CATALOG_COUNT] = {
        "EXPLORER.BMP", "SHELL.BMP", "TASKMGR.BMP"
    };

    kmemset(host_package, 0, sizeof(host_package));
    kmemcpy(host_package, "ZUPD", 4U);
    host_write_u16(host_package, HOST_PACKAGE_HEADER_FORMAT,
                   UPDATE_RUNTIME_FORMAT_VERSION);
    host_write_u16(host_package, HOST_PACKAGE_HEADER_SIZE_OFFSET,
                   HOST_PACKAGE_HEADER_SIZE);
    host_write_u16(host_package, HOST_PACKAGE_HEADER_ARCH,
                   UPDATE_RUNTIME_ARCH_I386);
    host_write_u32(host_package, HOST_PACKAGE_HEADER_TOTAL_SIZE,
                   HOST_PACKAGE_TOTAL_SIZE);
    host_write_u32(host_package, HOST_PACKAGE_HEADER_PAYLOAD_OFFSET,
                   HOST_PACKAGE_PAYLOAD_OFFSET);
    host_write_u32(host_package, HOST_PACKAGE_HEADER_PAYLOAD_SIZE,
                   HOST_PACKAGE_PAYLOAD_SIZE);
    host_write_u32(host_package, HOST_PACKAGE_HEADER_SIGNATURE_OFFSET,
                   HOST_PACKAGE_SIGNATURE_OFFSET);
    host_write_u32(host_package, HOST_PACKAGE_HEADER_SIGNATURE_SIZE, 64U);
    host_write_u32(host_package, HOST_PACKAGE_HEADER_GENERATION, 3U);
    host_write_u16(host_package, HOST_PACKAGE_HEADER_TARGET_VERSION,
                   target.major);
    host_write_u16(host_package, HOST_PACKAGE_HEADER_TARGET_VERSION + 2U,
                   target.minor);
    host_write_u16(host_package, HOST_PACKAGE_HEADER_TARGET_VERSION + 4U,
                   target.patch);
    host_write_u32(host_package, HOST_PACKAGE_HEADER_TARGET_EPOCH,
                   target_epoch);
    host_write_u16(host_package, HOST_PACKAGE_HEADER_ENTRY_COUNT,
                   HOST_PACKAGE_ENTRY_COUNT);
    host_write_u16(host_package, HOST_PACKAGE_HEADER_ENTRY_SIZE,
                   HOST_PACKAGE_ENTRY_SIZE);
    kmemcpy(host_package + HOST_PACKAGE_HEADER_KEY_ID, UPDATE_TRUST_KEY_ID,
            UPDATE_RUNTIME_KEY_ID_SIZE);
    host_write_u16(host_package, HOST_PACKAGE_HEADER_BASE_VERSION,
                   base.major);
    host_write_u16(host_package, HOST_PACKAGE_HEADER_BASE_VERSION + 2U,
                   base.minor);
    host_write_u16(host_package, HOST_PACKAGE_HEADER_BASE_VERSION + 4U,
                   base.patch);
    host_write_u32(host_package, HOST_PACKAGE_HEADER_BASE_EPOCH, base_epoch);
    for (uint32_t index = 0U; index < HOST_CATALOG_COUNT; index++) {
        uint8_t* entry = host_package + HOST_PACKAGE_HEADER_SIZE +
                          index * HOST_PACKAGE_ENTRY_SIZE;
        uint8_t* payload = host_package + HOST_PACKAGE_PAYLOAD_OFFSET +
                           index * 4U;
        uint8_t hash[CRYPTO_SHA256_SIZE];

        kmemcpy(entry, paths[index], kstrlen(paths[index]));
        host_write_u32(entry, HOST_PACKAGE_ENTRY_PAYLOAD_OFFSET,
                       HOST_PACKAGE_PAYLOAD_OFFSET + index * 4U);
        host_write_u32(entry, HOST_PACKAGE_ENTRY_PAYLOAD_SIZE, 4U);
        entry[HOST_PACKAGE_ENTRY_PRESENT] = 1U;
        entry[HOST_PACKAGE_ENTRY_OPERATION] = UPDATE_RUNTIME_OPERATION_REPLACE;
        for (uint32_t byte = 0U; byte < 4U; byte++) {
            payload[byte] = (uint8_t)(seed + index * 0x10U + byte);
        }
        (void)crypto_sha256(payload, 4U, hash);
        kmemcpy(entry + HOST_PACKAGE_ENTRY_HASH, hash, sizeof(hash));
    }
    (void)host_fs_put(HOST_PACKAGE_FILE, host_package,
                      HOST_PACKAGE_TOTAL_SIZE, FS_ATTRIBUTE_ARCHIVE);
}

static void host_prepare_cache(update_version_t base, uint32_t base_epoch,
                               update_version_t target, uint32_t target_epoch,
                               uint8_t seed) {
    static const char* paths[HOST_CATALOG_COUNT] = {
        "EXPLORER.BMP", "SHELL.BMP", "TASKMGR.BMP"
    };

    kmemset(&host_cache, 0, sizeof(host_cache));
    host_cache.valid = 1U;
    host_cache.selective = 1U;
    host_cache.active_slot = 0U;
    host_cache.entry_count = HOST_CATALOG_COUNT;
    host_cache.manifest.generation = 4U;
    host_cache.manifest.target_version = target;
    host_cache.manifest.target_epoch = target_epoch;
    host_cache.manifest.base_count = 1U;
    host_cache.manifest.base_versions[0] = base;
    host_cache.manifest.base_epochs[0] = base_epoch;
    host_cache.manifest.entry_count = HOST_CATALOG_COUNT;
    kmemcpy(host_cache.manifest_alias, HOST_CACHE_MANIFEST,
            kstrlen(HOST_CACHE_MANIFEST) + 1U);
    for (uint32_t index = 0U; index < HOST_CATALOG_COUNT; index++) {
        uint8_t data[4U];
        uint8_t hash[CRYPTO_SHA256_SIZE];
        char alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];

        kmemcpy(host_cache.manifest.entries[index].path, paths[index],
                kstrlen(paths[index]) + 1U);
        host_cache.manifest.entries[index].target_present = 1U;
        host_cache.manifest.entries[index].allowed_operations =
            UPDATE_RUNTIME_OPERATION_CREATE | UPDATE_RUNTIME_OPERATION_REPLACE;
        host_cache.manifest.entries[index].target_size = sizeof(data);
        for (uint32_t byte = 0U; byte < sizeof(data); byte++) {
            data[byte] = (uint8_t)(seed + index * 0x10U + byte);
        }
        (void)crypto_sha256(data, sizeof(data), hash);
        kmemcpy(host_cache.manifest.entries[index].target_hash, hash,
                sizeof(hash));
        host_cache.manifest.entries[index].asset_size = sizeof(data);
        kmemcpy(host_cache.manifest.entries[index].asset_hash, hash,
                sizeof(hash));
        alias[0] = HOST_CACHE_ASSET_PREFIX[0];
        alias[1] = HOST_CACHE_ASSET_PREFIX[1];
        alias[2] = HOST_CACHE_ASSET_PREFIX[2];
        alias[3] = HOST_CACHE_ASSET_PREFIX[3];
        alias[4] = HOST_CACHE_ASSET_PREFIX[4];
        alias[5] = (char)('0' + index);
        alias[6] = '\0';
        kmemcpy(host_cache.asset_aliases[index], alias, kstrlen(alias) + 1U);
        (void)host_fs_put(alias, data, sizeof(data), FS_ATTRIBUTE_ARCHIVE);
    }
    host_cache_available = 1U;
}

int update_get_installed_version(update_version_t* version_out,
                                 uint32_t* epoch_out) {
    if (!version_out || !epoch_out) return ERR_NULL;
    if (!host_update_version_available) return ERR_STATE;
    *version_out = (update_version_t){1U, 2U, 3U};
    *epoch_out = 7U;
    return OK;
}

int update_get_status(update_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    *status_out = (update_status_t){
        .transaction_pending = host_legacy_transaction_pending
    };
    return OK;
}

int update_is_ready(void) {
    return 1;
}

int update_sync_runtime_state(const update_version_t* installed_version,
                              uint32_t installed_epoch) {
    (void)installed_version;
    (void)installed_epoch;
    return OK;
}

int update_remote_runtime_capability_available(void) {
    return host_cache_available;
}

int update_remote_runtime_get_cache(update_runtime_cache_t* cache_out) {
    if (!cache_out) return ERR_NULL;
    if (!host_cache_available) {
        *cache_out = (update_runtime_cache_t){0};
        return ERR_NOT_FOUND;
    }
    *cache_out = host_cache;
    return OK;
}

static int host_cancel(void* context) {
    (void)context;
    return 1;
}

static int host_check_runtime_flow(void) {
    static const uint8_t base_data[4U] = {0x10U, 0x11U, 0x12U, 0x13U};
    static const uint8_t package_data[4U] = {0x80U, 0x81U, 0x82U, 0x83U};
    static const uint8_t second_data[4U] = {0x90U, 0x91U, 0x92U, 0x93U};
    static const uint8_t cache_data[4U] = {0xD0U, 0xD1U, 0xD2U, 0xD3U};
    update_version_t base = {1U, 2U, 3U};
    update_version_t first = {1U, 3U, 0U};
    update_version_t second = {1U, 4U, 0U};
    update_version_t cached = {1U, 5U, 0U};
    update_runtime_action_options_t options = {0U, 0, 0};
    update_runtime_action_result_t action;
    update_runtime_capabilities_t capabilities;
    update_runtime_status_t status;
    update_runtime_verification_t verification;
    uint8_t hash[CRYPTO_SHA256_SIZE];
    uint8_t matches = 0U;
    int result;

    host_fs_reset();
    host_fs_type = FS_TYPE_FAT12;
    host_cache_available = 0U;
    host_update_version_available = 1U;
    host_legacy_transaction_pending = 0U;
    host_prepare_targets(0x10U);
    host_prepare_package(base, 7U, first, 8U, 0x80U);
    result = update_runtime_init();
    if (result != OK || !update_runtime_is_ready()) return 701;
    if (update_runtime_get_capabilities(&capabilities) != OK ||
        !capabilities.verifier_ready || !capabilities.local_file_available ||
        !capabilities.persistent_state_ready || !capabilities.apply_available ||
        capabilities.rollback_available || capabilities.cache_available) {
        return 702;
    }
    if (update_runtime_get_status(&status) != OK || !status.state_valid ||
        status.entry_count != HOST_CATALOG_COUNT || status.rollback_available ||
        update_runtime_get_installed_version(&base, &status.installed_epoch) !=
            OK) return 703;
    if (update_runtime_verify_file(HOST_PACKAGE_FILE, &verification) != OK ||
        verification.package_valid != 1U || verification.compatible != 1U ||
        verification.entry_count != HOST_PACKAGE_ENTRY_COUNT ||
        verification.changed_entries != HOST_CATALOG_COUNT) return 704;

    (void)crypto_sha256(host_package, HOST_PACKAGE_TOTAL_SIZE, hash);
    host_prepare_cache((update_version_t){1U, 2U, 3U}, 7U, first, 8U, 0x80U);
    host_cache.manifest.generation = 3U;
    host_cache.manifest.package_size = HOST_PACKAGE_TOTAL_SIZE;
    kmemcpy(host_cache.manifest.package_hash, hash, sizeof(hash));
    if (update_runtime_verify_file_for_manifest(HOST_PACKAGE_FILE,
                                                &host_cache.manifest,
                                                &verification) != OK) return 705;
    host_cache_available = 0U;

    host_package[HOST_PACKAGE_PAYLOAD_OFFSET] ^= 0x01U;
    (void)host_fs_put(HOST_PACKAGE_FILE, host_package,
                      HOST_PACKAGE_TOTAL_SIZE, FS_ATTRIBUTE_ARCHIVE);
    if (update_runtime_verify_file(HOST_PACKAGE_FILE, &verification) !=
            ERR_INVALID || verification.reason != UPDATE_RUNTIME_REASON_HASH) {
        return 706;
    }
    host_prepare_package((update_version_t){1U, 2U, 3U}, 7U, first, 8U, 0x80U);

    options.dry_run = 1U;
    if (update_runtime_apply_file(HOST_PACKAGE_FILE, &options, &action) != OK ||
        action.completed_entries != 0U ||
        !host_fs_matches("EXPLORER.BMP", base_data, sizeof(base_data))) {
        return 707;
    }
    options.dry_run = 0U;
    if (update_runtime_apply_file(HOST_PACKAGE_FILE, &options, &action) != OK ||
        action.completed_entries != HOST_CATALOG_COUNT ||
        action.reason != UPDATE_RUNTIME_REASON_NONE ||
        !host_fs_matches("EXPLORER.BMP", package_data, sizeof(package_data))) {
        return 708;
    }
    (void)crypto_sha256(package_data, sizeof(package_data), hash);
    if (update_runtime_file_matches("EXPLORER.BMP", sizeof(package_data), hash,
                                    &matches) != OK || !matches) return 709;
    if (update_runtime_file_matches("EXPLORER.BMP", 3U, hash, &matches) != OK ||
        matches || update_runtime_file_matches(0, 4U, hash, &matches) !=
            ERR_NULL) return 710;
    if (update_runtime_get_cache(0) != ERR_NULL ||
        update_runtime_apply_cached(&options, &action) != ERR_NOT_FOUND ||
        action.reason != UPDATE_RUNTIME_REASON_CACHE) return 711;

    options.cancel_check = host_cancel;
    if (update_runtime_rollback(&options, &action) != ERR_TIMEOUT ||
        action.reason != UPDATE_RUNTIME_REASON_CANCELLED ||
        action.recovery_pending ||
        !host_fs_matches("EXPLORER.BMP", package_data, sizeof(package_data))) {
        return 712;
    }
    options.cancel_check = 0;
    if (update_runtime_rollback(&options, &action) != OK ||
        !host_fs_matches("EXPLORER.BMP", base_data, sizeof(base_data))) {
        return 713;
    }

    host_prepare_package(base, 7U, second, 9U, 0x90U);
    if (update_runtime_test_fail_after(1U) != OK ||
        update_runtime_apply_file(HOST_PACKAGE_FILE, &options, &action) !=
            ERR_STATE || action.reason != UPDATE_RUNTIME_REASON_JOURNAL ||
        !action.recovery_pending || update_runtime_is_ready() != 1) return 714;
    if (update_runtime_init() != OK ||
        !host_fs_matches("EXPLORER.BMP", base_data, sizeof(base_data))) {
        return 715;
    }

    host_fail_active_write_once = 1U;
    if (update_runtime_apply_file(HOST_PACKAGE_FILE, &options, &action) !=
            ERR_DISK || action.reason != UPDATE_RUNTIME_REASON_IO ||
        !action.recovery_pending || update_runtime_init() != OK ||
        !host_fs_matches("EXPLORER.BMP", base_data, sizeof(base_data))) {
        return 716;
    }
    if (update_runtime_apply_file(HOST_PACKAGE_FILE, &options, &action) != OK ||
        !host_fs_matches("EXPLORER.BMP", second_data, sizeof(second_data))) {
        return 717;
    }
    host_fail_active_write_once = 1U;
    result = update_runtime_rollback(&options, &action);
    int recovery_result = update_runtime_init();
    int restored = host_fs_matches("EXPLORER.BMP", base_data,
                                   sizeof(second_data));
    if (result != ERR_DISK ||
        action.reason != UPDATE_RUNTIME_REASON_IO ||
        !action.recovery_pending || recovery_result != OK || !restored) {
        return 718;
    }
    if (update_runtime_rollback(&options, &action) != ERR_NOT_FOUND ||
        action.reason != UPDATE_RUNTIME_REASON_CACHE) {
        return 719;
    }

    host_prepare_cache(base, 7U, cached, 10U, 0xD0U);
    if (update_runtime_get_cache(&host_cache) != OK ||
        !host_cache.valid || !host_cache.selective ||
        update_runtime_get_capabilities(&capabilities) != OK ||
        !capabilities.cache_available ||
        update_runtime_apply_cached(&options, &action) != OK ||
        !host_fs_matches("EXPLORER.BMP", cache_data, sizeof(cache_data))) {
        return 720;
    }
    if (update_runtime_rollback(&options, &action) != OK ||
        !host_fs_matches("EXPLORER.BMP", base_data, sizeof(base_data))) {
        return 721;
    }
    if (update_runtime_apply_file(0, &options, &action) != ERR_NULL ||
        update_runtime_apply_file(HOST_PACKAGE_FILE, 0, 0) != ERR_NULL ||
        update_runtime_rollback(0, 0) != ERR_NULL) return 722;

    host_cache_available = 0U;
    host_fs_reset();
    host_fs_type = FS_TYPE_NONE;
    if (update_runtime_init() != OK || update_runtime_get_capabilities(&capabilities) !=
            OK || capabilities.local_file_available ||
        capabilities.persistent_state_ready || !capabilities.apply_available ||
        update_runtime_get_status(&status) != OK || !status.state_valid) return 723;
    host_fs_reset();
    host_fs_type = FS_TYPE_FAT12;
    host_update_version_available = 0U;
    host_legacy_transaction_pending = 0U;
    if (update_runtime_init() != OK) return 724;
    host_fs_reset();
    host_fs_type = FS_TYPE_FAT12;
    host_legacy_transaction_pending = 1U;
    if (update_runtime_init() != ERR_STATE) return 725;
    return OK;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = update_runtime_host_test_contracts();
    if (result == 0) result = host_check_runtime_flow();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != 0) return result;
    puts("update-runtime-host: PASS");
    return 0;
}
