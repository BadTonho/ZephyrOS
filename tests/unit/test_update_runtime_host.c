#include <stdint.h>
#include <stdio.h>

#include "core/crypto.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/update.h"
#include "core/update_remote_runtime.h"
#include "core/update_runtime.h"
#include "fs/fs.h"
#include "update_runtime_host.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t host_file_present;
static uint8_t host_file_data[128U];
static uint32_t host_file_size;

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

static int host_file_selected(const char* path) {
    return host_file_present && path && kstrcmp(path, "SHELL.BMP") == 0;
}

int fs_get_root_file_info(const char* filename, uint32_t* size_out,
                          uint8_t* attributes_out) {
    if (!filename || !size_out) return ERR_NULL;
    if (!host_file_selected(filename)) return ERR_NOT_FOUND;
    *size_out = host_file_size;
    if (attributes_out) *attributes_out = FS_ATTRIBUTE_ARCHIVE;
    return OK;
}

int fs_read_file_range_at(const char* path, uint32_t offset,
                          uint8_t* buffer, uint32_t max_size,
                          uint32_t* bytes_read) {
    uint32_t available;

    if (bytes_read) *bytes_read = 0U;
    if (!path || !buffer || !bytes_read) return ERR_NULL;
    if (!host_file_selected(path) || offset > host_file_size) {
        return ERR_NOT_FOUND;
    }
    available = host_file_size - offset;
    if (max_size > available) return ERR_DISK;
    kmemcpy(buffer, host_file_data + offset, max_size);
    *bytes_read = max_size;
    return OK;
}

int fs_read_file_at(const char* path, uint8_t* buffer, uint32_t max_size) {
    if (!path || !buffer) return ERR_NULL;
    if (!host_file_selected(path) || host_file_size > max_size) {
        return ERR_NOT_FOUND;
    }
    kmemcpy(buffer, host_file_data, host_file_size);
    return OK;
}

int fs_atomic_write_root(const char* filename, const uint8_t* data,
                         uint32_t size, uint8_t attributes,
                         fs_atomic_mode_t mode) {
    (void)filename;
    (void)data;
    (void)size;
    (void)attributes;
    (void)mode;
    return ERR_UNAVAILABLE;
}

int fs_atomic_delete_root(const char* filename) {
    (void)filename;
    return ERR_NOT_FOUND;
}

uint8_t fs_get_type(void) {
    return FS_TYPE_NONE;
}

int fs_get_info(fs_info_t* info) {
    if (info) *info = (fs_info_t){0};
    return ERR_UNAVAILABLE;
}

int update_get_installed_version(update_version_t* version_out,
                                 uint32_t* epoch_out) {
    if (!version_out || !epoch_out) return ERR_NULL;
    *version_out = (update_version_t){1U, 2U, 3U};
    *epoch_out = 7U;
    return OK;
}

int update_get_status(update_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    *status_out = (update_status_t){0};
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
    return 0;
}

int update_remote_runtime_get_cache(update_runtime_cache_t* cache_out) {
    if (!cache_out) return ERR_NULL;
    *cache_out = (update_runtime_cache_t){0};
    return ERR_NOT_FOUND;
}

int main(void) {
    uint8_t expected_hash[CRYPTO_SHA256_SIZE];
    uint8_t matches = 0U;
    update_runtime_capabilities_t capabilities;
    int result;

    coverage_active = 1U;
    result = update_runtime_host_test_contracts();
    coverage_active = 0U;
    if (result == 0) {
        host_file_present = 1U;
        host_file_size = 4U;
        kmemcpy(host_file_data, "ABCD", 4U);
        crypto_sha256(host_file_data, host_file_size, expected_hash);
        result = update_runtime_file_matches("SHELL.BMP", 4U,
                                             expected_hash, &matches);
        if (result == OK && (!matches ||
            update_runtime_file_matches("SHELL.BMP", 5U, expected_hash,
                                        &matches) != OK || matches)) {
            result = 601;
        }
    }
    if (result == 0 && update_runtime_file_matches(0, 4U, expected_hash,
                                                    &matches) != ERR_NULL) {
        result = 602;
    }
    host_file_present = 0U;
    if (result == 0 && update_runtime_file_matches("SHELL.BMP", 4U,
                                                    expected_hash, &matches) !=
            OK) {
        result = 603;
    }
    if (result == 0 && update_runtime_get_capabilities(0) != ERR_NULL) {
        result = 604;
    }
    if (result == 0 && update_runtime_get_capabilities(&capabilities) !=
            ERR_STATE) {
        result = 605;
    }
    coverage_emit(result);
    if (result != 0) return result;
    puts("update-runtime-host: PASS");
    return 0;
}
