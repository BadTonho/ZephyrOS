#include <stdint.h>
#include <stdio.h>

#include "core/crypto.h"
#include "core/errors.h"
#include "core/http.h"
#include "core/log.h"
#include "core/string.h"
#include "core/update_remote_github.h"
#include "core/update_system.h"
#include "core/update_trust.h"
#include "fs/fs.h"
#include "process/process.h"
#include "update_system_host.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_IMAGE_SIZE 1536U
#define HOST_PACKAGE_SIZE (UPDATE_SYSTEM_HEADER_SIZE + HOST_IMAGE_SIZE)
#define HOST_DESCRIPTOR_CAPACITY 2048U
#define HOST_COMPONENT_HASH_OFFSET 12U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t host_package[HOST_PACKAGE_SIZE];
static uint8_t host_descriptor[HOST_DESCRIPTOR_CAPACITY];
static uint32_t host_descriptor_size;
static http_status_t host_http_status;
static uint8_t host_http_mode;
static update_remote_github_release_t host_release;
static uint32_t transfer_begin_count;
static uint32_t transfer_write_count;
static uint32_t transfer_finish_count;
static uint32_t transfer_abort_count;

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
    printf("ZCOV_BEGIN|case=host:core:update-system|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:update-system|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:update-system|value=0x%08X\n",
           (uint32_t)result);
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

static void host_set_text(uint8_t* data, uint32_t offset, uint32_t capacity,
                          const char* text) {
    uint32_t length = kstrlen(text);

    if (length >= capacity) length = capacity - 1U;
    kmemcpy(data + offset, text, length);
}

static int host_transfer_begin(const update_system_remote_asset_t* asset,
                               void* context) {
    (void)context;
    if (!asset || asset->package_size != HOST_PACKAGE_SIZE) return ERR_INVALID;
    transfer_begin_count++;
    return OK;
}

static int host_transfer_write(const uint8_t* data, uint32_t size,
                               void* context) {
    (void)context;
    if (!data || !size) return ERR_NULL;
    transfer_write_count += size;
    return OK;
}

static int host_transfer_finish(void* context) {
    (void)context;
    transfer_finish_count++;
    return OK;
}

static void host_transfer_abort(void* context) {
    (void)context;
    transfer_abort_count++;
}

static void host_prepare_release(void) {
    uint8_t descriptor_hash[CRYPTO_SHA256_SIZE];
    uint8_t package_hash[CRYPTO_SHA256_SIZE];
    char hash_text[CRYPTO_SHA256_SIZE * 2U + 1U];
    const char* descriptor_template =
        "{\"format\":\"zephyros-release-v2\","
        "\"tag\": \"v2.0.0\",\"legacy\":{},\"runtime\":{},"
        "\"compatibility\":{},\"system\":{\"name\":\"system.zsys\","
        "\"size\":2560,\"sha256\":\"%s\"}}";
    uint32_t payload_offset = UPDATE_SYSTEM_PAYLOAD_OFFSET;

    kmemset(host_package, 0, sizeof(host_package));
    for (uint32_t index = 0U; index < HOST_IMAGE_SIZE; index++) {
        host_package[payload_offset + index] =
            (uint8_t)(index * 17U + index / 7U + 3U);
    }
    host_package[0] = 'Z';
    host_package[1] = 'S';
    host_package[2] = 'Y';
    host_package[3] = 'S';
    host_write_u16(host_package, 4U, UPDATE_SYSTEM_FORMAT_VERSION);
    host_write_u16(host_package, 6U, UPDATE_SYSTEM_HEADER_SIZE);
    host_write_u16(host_package, 8U, UPDATE_SYSTEM_ARCH_I386);
    host_write_u16(host_package, 10U, UPDATE_SYSTEM_FLAG_REQUIRES_REBOOT);
    host_write_u32(host_package, 12U, HOST_PACKAGE_SIZE);
    host_write_u32(host_package, 16U, UPDATE_SYSTEM_PAYLOAD_OFFSET);
    host_write_u32(host_package, 20U, HOST_IMAGE_SIZE);
    host_write_u32(host_package, 24U, HOST_IMAGE_SIZE);
    host_write_u16(host_package, UPDATE_SYSTEM_TARGET_VERSION_OFFSET, 2U);
    host_write_u16(host_package, UPDATE_SYSTEM_TARGET_VERSION_OFFSET + 2U, 0U);
    host_write_u16(host_package, UPDATE_SYSTEM_TARGET_VERSION_OFFSET + 4U, 0U);
    host_write_u32(host_package, UPDATE_SYSTEM_TARGET_EPOCH_OFFSET, 9U);
    host_write_u16(host_package, UPDATE_SYSTEM_BASE_COUNT_OFFSET, 1U);
    host_write_u16(host_package, UPDATE_SYSTEM_BASE_ENTRY_SIZE_OFFSET,
                   UPDATE_SYSTEM_BASE_ENTRY_SIZE);
    host_write_u16(host_package, UPDATE_SYSTEM_BASES_OFFSET, 1U);
    host_write_u32(host_package, UPDATE_SYSTEM_BASES_OFFSET + 6U, 7U);
    host_write_u16(host_package, UPDATE_SYSTEM_MIN_UPDATER_OFFSET, 1U);
    host_write_u32(host_package, UPDATE_SYSTEM_MIN_UPDATER_OFFSET + 6U, 7U);
    host_write_u32(host_package, UPDATE_SYSTEM_BOOT_ABI_OFFSET,
                   UPDATE_SYSTEM_BOOT_ABI_LEGACY);
    host_write_u32(host_package, UPDATE_SYSTEM_SCHEMA_FROM_OFFSET, 1U);
    host_write_u32(host_package, UPDATE_SYSTEM_SCHEMA_TO_OFFSET, 1U);
    host_package[UPDATE_SYSTEM_ROUTE_OFFSET] = UPDATE_SYSTEM_ROUTE_DIRECT;
    host_set_text(host_package, UPDATE_SYSTEM_CHANNEL_OFFSET,
                  UPDATE_SYSTEM_CHANNEL_SIZE, "stable");
    host_set_text(host_package, UPDATE_SYSTEM_RELEASE_ID_OFFSET,
                  UPDATE_SYSTEM_IDENTIFIER_SIZE, "release-2");
    host_set_text(host_package, UPDATE_SYSTEM_RELEASE_TAG_OFFSET,
                  UPDATE_SYSTEM_IDENTIFIER_SIZE, "v2.0.0");
    host_write_u16(host_package, UPDATE_SYSTEM_COMPONENT_COUNT_OFFSET,
                   UPDATE_SYSTEM_COMPONENT_COUNT);
    host_write_u16(host_package, UPDATE_SYSTEM_COMPONENT_ENTRY_SIZE_OFFSET,
                   UPDATE_SYSTEM_COMPONENT_ENTRY_SIZE);
    host_write_u16(host_package, UPDATE_SYSTEM_CHECKPOINT_ENTRY_SIZE_OFFSET,
                   UPDATE_SYSTEM_CHECKPOINT_SIZE);
    for (uint32_t index = 0U; index < UPDATE_SYSTEM_COMPONENT_COUNT; index++) {
        uint32_t offset = UPDATE_SYSTEM_COMPONENTS_OFFSET +
                          index * UPDATE_SYSTEM_COMPONENT_ENTRY_SIZE;
        uint8_t hash[CRYPTO_SHA256_SIZE];

        host_write_u16(host_package, offset, (uint16_t)(index + 1U));
        host_write_u32(host_package, offset + 4U, index * 512U);
        host_write_u32(host_package, offset + 8U, 512U);
        crypto_sha256(host_package + payload_offset + index * 512U, 512U, hash);
        kmemcpy(host_package + offset + HOST_COMPONENT_HASH_OFFSET,
                hash, sizeof(hash));
    }
    kmemcpy(host_package + UPDATE_SYSTEM_KEY_ID_OFFSET, UPDATE_TRUST_KEY_ID,
            UPDATE_SYSTEM_KEY_ID_SIZE);
    host_write_u32(host_package, UPDATE_SYSTEM_SIGNATURE_SIZE_OFFSET,
                   UPDATE_SYSTEM_SIGNATURE_SIZE);
    host_write_u32(host_package, UPDATE_SYSTEM_SIGNATURE_OFFSET_FIELD,
                   UPDATE_SYSTEM_SIGNATURE_OFFSET);
    crypto_sha256(host_package + payload_offset, HOST_IMAGE_SIZE,
                  host_package + UPDATE_SYSTEM_IMAGE_HASH_OFFSET);
    crypto_sha256(host_package, HOST_PACKAGE_SIZE, package_hash);

    kmemset(&host_release, 0, sizeof(host_release));
    kmemcpy(host_release.tag, "v2.0.0", 7U);
    kmemcpy(host_release.release_id, "release-2", 10U);
    kmemcpy(host_release.descriptor.name, "release.json", 13U);
    kmemcpy(host_release.descriptor.url, "https://fixture/release.json", 29U);
    host_release.descriptor.size = 0U;
    host_release.descriptor.digest_present = 1U;
    kmemcpy(host_release.system.name, "system.zsys", 12U);
    kmemcpy(host_release.system.url, "https://fixture/system.zsys", 28U);
    host_release.system.size = HOST_PACKAGE_SIZE;
    host_release.system.digest_present = 1U;
    kmemcpy(host_release.system.digest, package_hash, sizeof(package_hash));
    host_release.system_present = 1U;
    host_release.descriptor.size = 0U;
    host_release.descriptor.digest_present = 0U;

    host_descriptor_size = (uint32_t)snprintf(
        (char*)host_descriptor, sizeof(host_descriptor), descriptor_template,
        "0000000000000000000000000000000000000000000000000000000000000000");
    crypto_sha256(host_descriptor, host_descriptor_size, descriptor_hash);
    kmemcpy(host_release.descriptor.digest, descriptor_hash,
            sizeof(descriptor_hash));
    host_release.descriptor.digest_present = 1U;
    for (uint32_t index = 0U; index < CRYPTO_SHA256_SIZE; index++) {
        static const char digits[] = "0123456789abcdef";

        hash_text[index * 2U] = digits[host_release.system.digest[index] >> 4U];
        hash_text[index * 2U + 1U] =
            digits[host_release.system.digest[index] & 0x0FU];
    }
    hash_text[sizeof(hash_text) - 1U] = '\0';
    host_descriptor_size = (uint32_t)snprintf(
        (char*)host_descriptor, sizeof(host_descriptor), descriptor_template,
        hash_text);
    crypto_sha256(host_descriptor, host_descriptor_size, descriptor_hash);
    kmemcpy(host_release.descriptor.digest, descriptor_hash,
            sizeof(descriptor_hash));
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

int fs_get_root_file_info(const char* filename, uint32_t* size_out,
                          uint8_t* attributes_out) {
    if (!filename) return ERR_NULL;
    if (kstrcmp(filename, "system:/candidate.zsys") != 0) return ERR_NOT_FOUND;
    if (size_out) *size_out = HOST_PACKAGE_SIZE;
    if (attributes_out) *attributes_out = 0U;
    return OK;
}

int fs_read_file_range_at(const char* path, uint32_t offset, uint8_t* buffer,
                          uint32_t max_size, uint32_t* bytes_read) {
    if (!path || !buffer || !bytes_read) return ERR_NULL;
    if (kstrcmp(path, "system:/candidate.zsys") != 0) return ERR_NOT_FOUND;
    if (offset > HOST_PACKAGE_SIZE || max_size > HOST_PACKAGE_SIZE - offset) {
        return ERR_OVERFLOW;
    }
    kmemcpy(buffer, host_package + offset, max_size);
    *bytes_read = max_size;
    return OK;
}

int update_get_installed_version(update_version_t* version_out,
                                 uint32_t* epoch_out) {
    if (!version_out || !epoch_out) return ERR_NULL;
    version_out->major = 1U;
    version_out->minor = 0U;
    version_out->patch = 0U;
    *epoch_out = 7U;
    return OK;
}

int update_remote_github_query(
    const char* tag, const update_remote_options_t* options,
    update_remote_github_release_t* release_out,
    update_remote_result_t* result_out) {
    (void)options;
    if (!tag || !release_out || !result_out) return ERR_NULL;
    if (kstrcmp(tag, host_release.tag) != 0) return ERR_NOT_FOUND;
    *release_out = host_release;
    kmemset(result_out, 0, sizeof(*result_out));
    return OK;
}

int http_get_start_ex(const char* url, const http_request_options_t* options) {
    (void)options;
    if (!url) return ERR_NULL;
    if (kstrcmp(url, host_release.descriptor.url) != 0) return ERR_NOT_FOUND;
    host_http_mode = 1U;
    host_http_status.state = HTTP_STATE_COMPLETE;
    host_http_status.status_code = 200U;
    host_http_status.has_content_length = 1U;
    host_http_status.content_length = host_descriptor_size;
    host_http_status.body_length = host_descriptor_size;
    return OK;
}

int http_get_stream_start_ex(const char* url, uint32_t body_limit,
                             http_body_sink_t sink, void* context,
                             const http_request_options_t* options) {
    (void)options;
    if (!url || !sink) return ERR_NULL;
    if (kstrcmp(url, host_release.system.url) != 0) return ERR_NOT_FOUND;
    if (body_limit < HOST_PACKAGE_SIZE) return ERR_OVERFLOW;
    host_http_mode = 2U;
    if (sink(host_package, HOST_PACKAGE_SIZE, context) != OK) return ERR_DISK;
    host_http_status.state = HTTP_STATE_COMPLETE;
    host_http_status.status_code = 200U;
    host_http_status.has_content_length = 1U;
    host_http_status.content_length = HOST_PACKAGE_SIZE;
    host_http_status.body_length = HOST_PACKAGE_SIZE;
    return OK;
}

int http_get_status(http_status_t* out_status) {
    if (!out_status || !host_http_mode) return ERR_STATE;
    *out_status = host_http_status;
    return OK;
}

int http_get_body(const uint8_t** out_body, uint32_t* out_length) {
    if (!out_body || !out_length || host_http_mode != 1U) return ERR_STATE;
    *out_body = host_descriptor;
    *out_length = host_descriptor_size;
    return OK;
}

int http_reset(void) {
    host_http_mode = 0U;
    kmemset(&host_http_status, 0, sizeof(host_http_status));
    return OK;
}

void process_block(uint32_t ticks) {
    (void)ticks;
}

int main(void) {
    update_system_verification_t verification;
    update_system_remote_asset_t asset;
    update_system_transfer_t transfer = {
        host_transfer_begin, host_transfer_write, host_transfer_finish,
        host_transfer_abort, NULL
    };
    int result;

    host_prepare_release();
    coverage_active = 1U;
    result = update_system_host_test_contracts();
    if (result == OK) result = update_system_init();
    if (result == OK) result = update_system_verify_file(
        "system:/candidate.zsys", &verification);
    if (result == OK && (!verification.header_valid ||
                         !verification.signature_valid ||
                         !verification.image_valid || !verification.compatible ||
                         verification.component_count !=
                             UPDATE_SYSTEM_COMPONENT_COUNT)) {
        result = ERR_STATE;
    }
    if (result == OK) result = update_system_verify_file_for_slot(
        "system:/candidate.zsys", &verification);
    if (result == OK) result = update_system_transfer_tag(
        "v2.0.0", NULL, &transfer, &verification, &asset);
    coverage_active = 0U;
    coverage_emit(result);
    if (result != OK || transfer_begin_count != 1U ||
        transfer_write_count != HOST_PACKAGE_SIZE || transfer_finish_count != 1U ||
        transfer_abort_count != 0U || asset.package_size != HOST_PACKAGE_SIZE) {
        return result == OK ? ERR_STATE : result;
    }
    puts("update-system-host: PASS");
    return OK;
}
