#include <stdint.h>
#include <stdio.h>

#include "core/crypto.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/update.h"
#include "core/update_trust.h"
#include "core/version.h"
#include "fs/fs.h"
#include "update_host.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_FS_FILE_CAPACITY 32U
#define HOST_FS_FILE_SIZE 4096U
#define HOST_PACKAGE_CAPACITY 2048U
#define HOST_ZUPD_HEADER_SIZE 128U
#define HOST_ZUPD_ENTRY_SIZE 128U
#define HOST_ZUPD_ENTRY_COUNT 3U
#define HOST_ZUPD_MANIFEST_OFFSET HOST_ZUPD_HEADER_SIZE
#define HOST_ZUPD_MANIFEST_SIZE (HOST_ZUPD_ENTRY_SIZE * HOST_ZUPD_ENTRY_COUNT)
#define HOST_ZUPD_PAYLOAD_OFFSET \
    (HOST_ZUPD_MANIFEST_OFFSET + HOST_ZUPD_MANIFEST_SIZE)
#define HOST_ZUPD_PAYLOAD_SIZE 12U
#define HOST_ZUPD_SIGNATURE_OFFSET \
    (HOST_ZUPD_PAYLOAD_OFFSET + HOST_ZUPD_PAYLOAD_SIZE)
#define HOST_ZUPD_TOTAL_SIZE (HOST_ZUPD_SIGNATURE_OFFSET + 64U)
#define HOST_ZUPD_PAYLOAD_SIZE_PER_ENTRY 4U
#define HOST_ZUPD_FILE "VALID.ZUP"
#define HOST_ZUPD_ENTRY_PATH_OFFSET 0U
#define HOST_ZUPD_ENTRY_PAYLOAD_OFFSET 64U
#define HOST_ZUPD_ENTRY_PAYLOAD_SIZE 68U
#define HOST_ZUPD_ENTRY_INSTALLED_SIZE 72U
#define HOST_ZUPD_ENTRY_OPERATION 76U
#define HOST_ZUPD_ENTRY_COMPRESSION 78U
#define HOST_ZUPD_ENTRY_TARGET_CLASS 80U
#define HOST_ZUPD_ENTRY_FLAGS 82U
#define HOST_ZUPD_ENTRY_PAYLOAD_HASH 84U
#define HOST_ZUPD_HEADER_FORMAT_VERSION 4U
#define HOST_ZUPD_HEADER_SIZE_OFFSET 6U
#define HOST_ZUPD_HEADER_ARCHITECTURE 8U
#define HOST_ZUPD_HEADER_TOTAL_SIZE 16U
#define HOST_ZUPD_HEADER_MANIFEST_OFFSET 20U
#define HOST_ZUPD_HEADER_MANIFEST_SIZE 24U
#define HOST_ZUPD_HEADER_PAYLOAD_OFFSET 28U
#define HOST_ZUPD_HEADER_PAYLOAD_SIZE 32U
#define HOST_ZUPD_HEADER_SIGNATURE_OFFSET 36U
#define HOST_ZUPD_HEADER_SIGNATURE_SIZE 40U
#define HOST_ZUPD_HEADER_SIGNATURE_ALGORITHM 42U
#define HOST_ZUPD_HEADER_HASH_ALGORITHM 44U
#define HOST_ZUPD_HEADER_ENTRY_COUNT 46U
#define HOST_ZUPD_HEADER_ENTRY_SIZE 48U
#define HOST_ZUPD_HEADER_KEY_ID 72U
#define HOST_ZUPD_HEADER_CONTENT_HASH 88U
#define HOST_ZUPD_UPDATE_OPERATION_REPLACE 1U
#define HOST_ZUPD_COMPRESSION_NONE 0U
#define HOST_ZUPD_TARGET_SYSTEM_FILE 1U
#define HOST_FS_ATTRIBUTE 0x26U

typedef struct {
    uint8_t used;
    uint8_t attributes;
    char name[FS_MAX_FILENAME];
    uint32_t size;
    uint8_t data[HOST_FS_FILE_SIZE];
} host_fs_file_t;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t host_fs_type;
static host_fs_file_t host_fs_files[HOST_FS_FILE_CAPACITY];
static uint8_t host_fail_replace_once;
static uint8_t host_package[HOST_PACKAGE_CAPACITY];

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
    printf("ZCOV_BEGIN|case=host:core:update|value=0x%08X\n", coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:update|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:update|value=0x%08X\n",
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
    result = crypto_sha256_final(&context, hash);
    if (result != OK) return result;
    if (data && size == sizeof(UPDATE_TRUST_PUBLIC_KEY)) {
        uint8_t is_trust_key = 1U;

        for (uint32_t index = 0U; index < sizeof(UPDATE_TRUST_PUBLIC_KEY);
             index++) {
            if (data[index] != UPDATE_TRUST_PUBLIC_KEY[index]) {
                is_trust_key = 0U;
                break;
            }
        }
        if (is_trust_key) {
            for (uint32_t index = 0U; index < sizeof(UPDATE_TRUST_KEY_ID);
                 index++) {
                hash[index] = UPDATE_TRUST_KEY_ID[index];
            }
        }
    }
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

int crypto_self_test(void) {
    return OK;
}

static int host_fs_find(const char* name) {
    if (!name) return -1;
    for (uint32_t index = 0U; index < HOST_FS_FILE_CAPACITY; index++) {
        if (host_fs_files[index].used &&
            kstrcmp(host_fs_files[index].name, name) == 0) {
            return (int)index;
        }
    }
    return -1;
}

static void host_fs_reset(void) {
    kmemset(host_fs_files, 0, sizeof(host_fs_files));
    host_fs_type = FS_TYPE_NONE;
    host_fail_replace_once = 0U;
}

static int host_fs_put(const char* name, const uint8_t* data, uint32_t size,
                       uint8_t attributes) {
    int index = host_fs_find(name);

    if (!name || (size && !data) || size > HOST_FS_FILE_SIZE) {
        return ERR_INVALID;
    }
    if (index < 0) {
        for (uint32_t candidate = 0U; candidate < HOST_FS_FILE_CAPACITY;
             candidate++) {
            if (!host_fs_files[candidate].used) {
                index = (int)candidate;
                break;
            }
        }
    }
    if (index < 0) return ERR_UNAVAILABLE;
    host_fs_files[index].used = 1U;
    host_fs_files[index].attributes = attributes;
    kmemset(host_fs_files[index].name, 0, FS_MAX_FILENAME);
    kmemcpy(host_fs_files[index].name, name, kstrlen(name));
    host_fs_files[index].size = size;
    kmemset(host_fs_files[index].data, 0, HOST_FS_FILE_SIZE);
    if (size) kmemcpy(host_fs_files[index].data, data, size);
    return OK;
}

int fs_get_root_file_info(const char* filename, uint32_t* size_out,
                          uint8_t* attributes_out) {
    int index = host_fs_find(filename);

    if (index < 0) return ERR_NOT_FOUND;
    if (size_out) *size_out = host_fs_files[index].size;
    if (attributes_out) *attributes_out = host_fs_files[index].attributes;
    return OK;
}

int fs_read_file_range_at(const char* path, uint32_t offset,
                          uint8_t* buffer, uint32_t max_size,
                          uint32_t* bytes_read) {
    uint32_t available;
    uint32_t count;
    int index = host_fs_find(path);

    if (!bytes_read) return ERR_NULL;
    *bytes_read = 0U;
    if (index < 0) return ERR_NOT_FOUND;
    if (offset > host_fs_files[index].size) return ERR_INVALID;
    if (max_size && !buffer) return ERR_NULL;
    available = host_fs_files[index].size - offset;
    count = available < max_size ? available : max_size;
    if (count) kmemcpy(buffer, host_fs_files[index].data + offset, count);
    *bytes_read = count;
    return OK;
}

int fs_atomic_write_root(const char* filename, const uint8_t* data,
                         uint32_t size, uint8_t attributes,
                         fs_atomic_mode_t mode) {
    if (mode == FS_ATOMIC_REPLACE_ONLY && host_fs_find(filename) < 0) {
        return ERR_NOT_FOUND;
    }
    if (mode == FS_ATOMIC_REPLACE_ONLY && host_fail_replace_once) {
        host_fail_replace_once = 0U;
        return ERR_DISK;
    }
    return host_fs_put(filename, data, size, attributes);
}

int fs_atomic_delete_root(const char* filename) {
    int index = host_fs_find(filename);

    if (index < 0) return ERR_NOT_FOUND;
    host_fs_files[index].used = 0U;
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

static void host_write_u16(uint8_t* data, uint32_t offset, uint16_t value) {
    data[offset] = (uint8_t)value;
    data[offset + 1U] = (uint8_t)(value >> 8U);
}

static void host_write_u32(uint8_t* data, uint32_t offset, uint32_t value) {
    data[offset] = (uint8_t)value;
    data[offset + 1U] = (uint8_t)(value >> 8U);
    data[offset + 2U] = (uint8_t)(value >> 16U);
    data[offset + 3U] = (uint8_t)(value >> 24U);
}

static int host_bytes_equal(const uint8_t* first, const uint8_t* second,
                            uint32_t size) {
    for (uint32_t index = 0U; index < size; index++) {
        if (first[index] != second[index]) return 0;
    }
    return 1;
}

static int host_fs_matches(const char* name, const uint8_t* expected,
                           uint32_t size) {
    int index = host_fs_find(name);

    return index >= 0 && host_fs_files[index].size == size &&
           host_bytes_equal(host_fs_files[index].data, expected, size);
}

static void host_prepare_targets(uint8_t seed) {
    static const char* paths[HOST_ZUPD_ENTRY_COUNT] = {
        "EXPLORER.BMP", "SHELL.BMP", "TASKMGR.BMP"
    };
    uint8_t data[HOST_ZUPD_PAYLOAD_SIZE_PER_ENTRY];

    for (uint32_t index = 0U; index < HOST_ZUPD_ENTRY_COUNT; index++) {
        for (uint32_t byte = 0U; byte < sizeof(data); byte++) {
            data[byte] = (uint8_t)(seed + index * 0x10U + byte);
        }
        (void)host_fs_put(paths[index], data, sizeof(data), 0U);
    }
}

static void host_prepare_zupd(update_version_t base, uint32_t base_epoch,
                              update_version_t target, uint32_t target_epoch,
                              uint8_t seed) {
    static const char* paths[HOST_ZUPD_ENTRY_COUNT] = {
        "EXPLORER.BMP", "SHELL.BMP", "TASKMGR.BMP"
    };
    crypto_sha256_ctx_t content_hash;
    uint8_t hash[CRYPTO_SHA256_SIZE];
    uint32_t payload_offset = HOST_ZUPD_PAYLOAD_OFFSET;

    kmemset(host_package, 0, sizeof(host_package));
    kmemcpy(host_package, "ZUPD", 4U);
    host_write_u16(host_package, HOST_ZUPD_HEADER_FORMAT_VERSION, 1U);
    host_write_u16(host_package, HOST_ZUPD_HEADER_SIZE_OFFSET,
                   HOST_ZUPD_HEADER_SIZE);
    host_write_u32(host_package, HOST_ZUPD_HEADER_ARCHITECTURE, 1U);
    host_write_u32(host_package, HOST_ZUPD_HEADER_TOTAL_SIZE,
                   HOST_ZUPD_TOTAL_SIZE);
    host_write_u32(host_package, HOST_ZUPD_HEADER_MANIFEST_OFFSET,
                   HOST_ZUPD_MANIFEST_OFFSET);
    host_write_u32(host_package, HOST_ZUPD_HEADER_MANIFEST_SIZE,
                   HOST_ZUPD_MANIFEST_SIZE);
    host_write_u32(host_package, HOST_ZUPD_HEADER_PAYLOAD_OFFSET,
                   HOST_ZUPD_PAYLOAD_OFFSET);
    host_write_u32(host_package, HOST_ZUPD_HEADER_PAYLOAD_SIZE,
                   HOST_ZUPD_PAYLOAD_SIZE);
    host_write_u32(host_package, HOST_ZUPD_HEADER_SIGNATURE_OFFSET,
                   HOST_ZUPD_SIGNATURE_OFFSET);
    host_write_u16(host_package, HOST_ZUPD_HEADER_SIGNATURE_SIZE, 64U);
    host_write_u16(host_package, HOST_ZUPD_HEADER_SIGNATURE_ALGORITHM, 1U);
    host_write_u16(host_package, HOST_ZUPD_HEADER_HASH_ALGORITHM, 1U);
    host_write_u16(host_package, HOST_ZUPD_HEADER_ENTRY_COUNT,
                   HOST_ZUPD_ENTRY_COUNT);
    host_write_u16(host_package, HOST_ZUPD_HEADER_ENTRY_SIZE,
                   HOST_ZUPD_ENTRY_SIZE);
    host_write_u16(host_package, 52U, base.major);
    host_write_u16(host_package, 54U, base.minor);
    host_write_u16(host_package, 56U, base.patch);
    host_write_u16(host_package, 58U, target.major);
    host_write_u16(host_package, 60U, target.minor);
    host_write_u16(host_package, 62U, target.patch);
    host_write_u32(host_package, 64U, base_epoch);
    host_write_u32(host_package, 68U, target_epoch);
    kmemcpy(host_package + HOST_ZUPD_HEADER_KEY_ID, UPDATE_TRUST_KEY_ID,
            sizeof(UPDATE_TRUST_KEY_ID));

    for (uint32_t index = 0U; index < HOST_ZUPD_ENTRY_COUNT; index++) {
        uint8_t* entry = host_package + HOST_ZUPD_MANIFEST_OFFSET +
                         index * HOST_ZUPD_ENTRY_SIZE;
        uint8_t* payload = host_package + payload_offset;

        kmemcpy(entry + HOST_ZUPD_ENTRY_PATH_OFFSET, paths[index],
                kstrlen(paths[index]));
        host_write_u32(entry, HOST_ZUPD_ENTRY_PAYLOAD_OFFSET, payload_offset);
        host_write_u32(entry, HOST_ZUPD_ENTRY_PAYLOAD_SIZE,
                       HOST_ZUPD_PAYLOAD_SIZE_PER_ENTRY);
        host_write_u32(entry, HOST_ZUPD_ENTRY_INSTALLED_SIZE,
                       HOST_ZUPD_PAYLOAD_SIZE_PER_ENTRY);
        host_write_u16(entry, HOST_ZUPD_ENTRY_OPERATION,
                       HOST_ZUPD_UPDATE_OPERATION_REPLACE);
        host_write_u16(entry, HOST_ZUPD_ENTRY_COMPRESSION,
                       HOST_ZUPD_COMPRESSION_NONE);
        host_write_u16(entry, HOST_ZUPD_ENTRY_TARGET_CLASS,
                       HOST_ZUPD_TARGET_SYSTEM_FILE);
        for (uint32_t byte = 0U; byte < HOST_ZUPD_PAYLOAD_SIZE_PER_ENTRY;
             byte++) {
            payload[byte] = (uint8_t)(seed + index * 0x10U + byte);
        }
        (void)crypto_sha256(payload, HOST_ZUPD_PAYLOAD_SIZE_PER_ENTRY, hash);
        kmemcpy(entry + HOST_ZUPD_ENTRY_PAYLOAD_HASH, hash, sizeof(hash));
        payload_offset += HOST_ZUPD_PAYLOAD_SIZE_PER_ENTRY;
    }
    (void)crypto_sha256_init(&content_hash);
    (void)crypto_sha256_update(&content_hash,
                               host_package + HOST_ZUPD_MANIFEST_OFFSET,
                               HOST_ZUPD_MANIFEST_SIZE);
    (void)crypto_sha256_update(&content_hash,
                               host_package + HOST_ZUPD_PAYLOAD_OFFSET,
                               HOST_ZUPD_PAYLOAD_SIZE);
    (void)crypto_sha256_final(&content_hash, hash);
    kmemcpy(host_package + HOST_ZUPD_HEADER_CONTENT_HASH, hash, sizeof(hash));
    (void)host_fs_put(HOST_ZUPD_FILE, host_package, HOST_ZUPD_TOTAL_SIZE, 0U);
}

static int host_cancel(void* context) {
    (void)context;
    return 1;
}

static int host_check_fat12_flow(void) {
    const uint8_t old_data[HOST_ZUPD_PAYLOAD_SIZE_PER_ENTRY] = {
        0x10U, 0x11U, 0x12U, 0x13U
    };
    const uint8_t new_data[HOST_ZUPD_PAYLOAD_SIZE_PER_ENTRY] = {
        0x80U, 0x81U, 0x82U, 0x83U
    };
    update_version_t base = {
        ZEPHYROS_VERSION_MAJOR, ZEPHYROS_VERSION_MINOR,
        ZEPHYROS_VERSION_PATCH
    };
    update_version_t target = {0U, 2U, 0U};
    update_version_t next_target = {0U, 3U, 0U};
    update_version_t final_target = {0U, 4U, 0U};
    update_action_options_t options = {0U, 0, 0};
    update_action_result_t action;
    update_capabilities_t capabilities;
    update_status_t status;
    update_verification_t verification;
    update_history_entry_t history;
    uint32_t history_count = 0U;

    host_fs_reset();
    host_fs_type = FS_TYPE_FAT12;
    host_prepare_targets(0x10U);
    (void)host_fs_put("EXPLORER.BMP", old_data, sizeof(old_data), 0U);
    host_prepare_zupd(base, ZEPHYROS_VERSION_EPOCH, target, 1U, 0x80U);
    if (update_init() != OK || !update_is_ready()) return 201;
    if (update_get_capabilities(&capabilities) != OK ||
        !capabilities.apply_available || !capabilities.history_available ||
        !capabilities.persistent_state_ready) {
        return 202;
    }
    if (update_verify_file(HOST_ZUPD_FILE, &verification) != OK ||
        verification.entry_count != HOST_ZUPD_ENTRY_COUNT ||
        verification.target_version.patch != target.patch) {
        return 203;
    }
    options.dry_run = 1U;
    if (update_apply_file(HOST_ZUPD_FILE, &options, &action) != OK ||
        action.entry_count != HOST_ZUPD_ENTRY_COUNT ||
        !host_fs_matches("EXPLORER.BMP", old_data, sizeof(old_data))) {
        return 204;
    }
    options.dry_run = 0U;
    if (update_apply_file(HOST_ZUPD_FILE, &options, &action) != OK ||
        action.completed_entries != HOST_ZUPD_ENTRY_COUNT ||
        !action.reboot_required ||
        !host_fs_matches("EXPLORER.BMP", new_data, sizeof(new_data))) {
        return 205;
    }
    if (update_get_status(&status) != OK ||
        status.state_store != UPDATE_STORE_VALID ||
        status.current_files != UPDATE_STORE_VALID ||
        status.rollback_entry_count != HOST_ZUPD_ENTRY_COUNT ||
        !status.capabilities.rollback_available ||
        update_get_history_count(&history_count) != OK || history_count == 0U ||
        update_get_history_entry(0U, &history) != OK ||
        history.operation != UPDATE_HISTORY_OPERATION_APPLY) {
        return 206;
    }
    options.cancel_check = host_cancel;
    if (update_rollback(&options, &action) != ERR_STATE ||
        action.reason != UPDATE_ACTION_CANCELLED ||
        !host_fs_matches("EXPLORER.BMP", new_data, sizeof(new_data))) {
        return 207;
    }
    options.cancel_check = 0;
    if (update_rollback(&options, &action) != OK ||
        action.completed_entries != HOST_ZUPD_ENTRY_COUNT ||
        !host_fs_matches("EXPLORER.BMP", old_data, sizeof(old_data))) {
        return 208;
    }
    host_prepare_zupd(base, ZEPHYROS_VERSION_EPOCH, next_target, 2U, 0x90U);
    host_fail_replace_once = 1U;
    if (update_apply_file(HOST_ZUPD_FILE, 0, &action) != ERR_DISK ||
        action.reason != UPDATE_ACTION_IO ||
        !host_fs_matches("EXPLORER.BMP", old_data, sizeof(old_data)) ||
        update_get_status(&status) != OK || status.transaction_pending) {
        return 209;
    }
    host_prepare_zupd(base, ZEPHYROS_VERSION_EPOCH, final_target, 3U, 0xA0U);
    if (update_test_fail_after(1U) != OK ||
        update_apply_file(HOST_ZUPD_FILE, 0, &action) != ERR_STATE ||
        action.reason != UPDATE_ACTION_RECOVERY_PENDING ||
        update_get_status(&status) != OK || !status.transaction_pending ||
        !status.capabilities.recovery_pending) {
        return 210;
    }
    if (update_init() != OK ||
        update_get_status(&status) != OK || status.transaction_pending ||
        status.capabilities.recovery_pending ||
        !host_fs_matches("EXPLORER.BMP", old_data, sizeof(old_data))) {
        return 211;
    }
    options.cancel_check = host_cancel;
    if (update_apply_file(HOST_ZUPD_FILE, &options, &action) != ERR_STATE ||
        action.reason != UPDATE_ACTION_CANCELLED ||
        update_get_status(&status) != OK || status.transaction_pending) {
        return 212;
    }
    options.cancel_check = 0;
    host_prepare_zupd(base, ZEPHYROS_VERSION_EPOCH, next_target, 2U, 0xB0U);
    if (update_apply_file(HOST_ZUPD_FILE, &options, &action) != OK) return 213;
    host_fail_replace_once = 1U;
    if (update_rollback(&options, &action) != ERR_STATE ||
        action.reason != UPDATE_ACTION_RECOVERY_PENDING ||
        update_init() != OK || update_get_status(&status) != OK ||
        status.transaction_pending ||
        !host_fs_matches("EXPLORER.BMP", old_data, sizeof(old_data))) {
        return 214;
    }
    host_prepare_zupd(base, ZEPHYROS_VERSION_EPOCH, final_target, 3U, 0xC0U);
    if (update_apply_file(HOST_ZUPD_FILE, &options, &action) != OK) return 215;
    options.cancel_check = host_cancel;
    if (update_rollback(&options, &action) != ERR_STATE ||
        action.reason != UPDATE_ACTION_CANCELLED ||
        !host_fs_matches("EXPLORER.BMP", (const uint8_t[]){0xC0U, 0xC1U,
        0xC2U, 0xC3U}, HOST_ZUPD_PAYLOAD_SIZE_PER_ENTRY)) {
        return 216;
    }
    options.cancel_check = 0;
    if (update_rollback(&options, &action) != OK ||
        update_sync_runtime_state(&base, ZEPHYROS_VERSION_EPOCH) != OK ||
        update_get_status(&status) != OK || status.transaction_pending ||
        status.rollback_entry_count != 0U ||
        !host_fs_matches("EXPLORER.BMP", old_data, sizeof(old_data))) {
        return 217;
    }
    {
        uint8_t invalid_history[512U];

        kmemset(invalid_history, 0xA5U, sizeof(invalid_history));
        if (fs_atomic_delete_root("ZUPD1.HIS") != OK ||
            host_fs_put("ZUPD0.HIS", invalid_history,
                        sizeof(invalid_history), HOST_FS_ATTRIBUTE) != OK ||
            update_init() != OK || update_get_status(&status) != OK ||
            status.history_store != UPDATE_STORE_INVALID ||
            status.capabilities.history_available) {
            return 218;
        }
        if (fs_atomic_delete_root("ZUPD0.HIS") != OK ||
            update_init() != OK || update_get_status(&status) != OK ||
            status.history_store != UPDATE_STORE_EMPTY) {
            return 219;
        }
    }
    return OK;
}

int update_remote_capability_available(void) {
    return 0;
}

int update_runtime_init(void) {
    return ERR_UNAVAILABLE;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = update_host_test_contracts();
    if (result == OK) result = host_check_fat12_flow();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != 0) return result;
    puts("update-host: PASS");
    return 0;
}
