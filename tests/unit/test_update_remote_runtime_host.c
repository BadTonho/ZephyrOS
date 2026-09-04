#include <stdint.h>
#include <stdio.h>

#include "core/crypto.h"
#include "core/errors.h"
#include "core/http.h"
#include "core/log.h"
#include "core/network_manager.h"
#include "core/string.h"
#include "core/update_remote_github.h"
#include "core/update_remote_runtime.h"
#include "core/update_runtime.h"
#include "fs/fs.h"
#include "update_remote_runtime_host.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static http_status_t host_http_status;
static uint8_t host_http_body[UPDATE_RUNTIME_MANIFEST_SIZE];

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
    printf("ZCOV_BEGIN|case=host:core:update-remote-runtime|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:update-remote-runtime|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:update-remote-runtime|value=0x%08X\n",
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

int crypto_equal(const uint8_t* first, const uint8_t* second, uint32_t size) {
    uint8_t different = 0U;

    if (!first || !second) return 0;
    for (uint32_t index = 0U; index < size; index++) {
        different |= (uint8_t)(first[index] ^ second[index]);
    }
    return different == 0U;
}

int fs_get_root_file_info(const char* filename, uint32_t* size_out,
                          uint8_t* attributes_out) {
    if (!filename || !size_out) return ERR_NULL;
    if (kstrcmp(filename, "HASH") != 0) return ERR_NOT_FOUND;
    *size_out = 4U;
    if (attributes_out) *attributes_out = FS_ATTRIBUTE_ARCHIVE;
    return OK;
}

int fs_read_file_range_at(const char* path, uint32_t offset,
                          uint8_t* buffer, uint32_t max_size,
                          uint32_t* bytes_read) {
    if (bytes_read) *bytes_read = 0U;
    if (!path || !buffer || !bytes_read) return ERR_NULL;
    if (kstrcmp(path, "HASH") != 0 || offset > 4U || max_size > 4U - offset) {
        return ERR_NOT_FOUND;
    }
    for (uint32_t index = 0U; index < max_size; index++) {
        buffer[index] = (uint8_t)"ABCD"[offset + index];
    }
    *bytes_read = max_size;
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

int fs_stream_begin_root(const char* filename, uint32_t expected_size,
                         uint8_t attributes) {
    (void)filename;
    (void)expected_size;
    (void)attributes;
    return OK;
}

int fs_stream_write_root(const uint8_t* data, uint32_t size) {
    return (!data && size) ? ERR_NULL : OK;
}

int fs_stream_finish_root(void) {
    return OK;
}

int fs_stream_abort_root(void) {
    return OK;
}

int network_manager_get_status(network_manager_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = (network_manager_status_t){0};
    out_status->http_available = 1U;
    out_status->ipv4_configured = 1U;
    return OK;
}

int http_get_start_ex(const char* url,
                      const http_request_options_t* options) {
    (void)url;
    (void)options;
    kmemset(&host_http_status, 0, sizeof(host_http_status));
    host_http_status.state = HTTP_STATE_COMPLETE;
    host_http_status.status_code = 200U;
    host_http_status.body_length = UPDATE_RUNTIME_MANIFEST_SIZE;
    host_http_status.content_length = UPDATE_RUNTIME_MANIFEST_SIZE;
    host_http_status.has_content_length = 1U;
    return OK;
}

int http_get_stream_start_ex(const char* url, uint32_t body_limit,
                             http_body_sink_t sink, void* context,
                             const http_request_options_t* options) {
    (void)url;
    (void)body_limit;
    (void)sink;
    (void)context;
    (void)options;
    return ERR_UNAVAILABLE;
}

int http_get_status(http_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = host_http_status;
    return OK;
}

int http_get_body(const uint8_t** out_body, uint32_t* out_length) {
    if (!out_body || !out_length) return ERR_NULL;
    *out_body = host_http_body;
    *out_length = UPDATE_RUNTIME_MANIFEST_SIZE;
    return OK;
}

int http_reset(void) {
    host_http_status.state = HTTP_STATE_IDLE;
    return OK;
}

void process_block(uint32_t ticks) {
    (void)ticks;
}

int update_runtime_is_ready(void) {
    return 1;
}

int update_runtime_parse_manifest(const uint8_t* raw, uint32_t size,
                                  update_runtime_manifest_t* output,
                                  update_runtime_reason_t* reason_out) {
    if (!raw || !output || !reason_out) return ERR_NULL;
    *reason_out = UPDATE_RUNTIME_REASON_FORMAT;
    if (size != UPDATE_RUNTIME_MANIFEST_SIZE || raw[0] != 0xA5U) {
        return ERR_INVALID;
    }
    kmemset(output, 0, sizeof(*output));
    output->target_version = (update_version_t){2U, 0U, 0U};
    output->target_epoch = 8U;
    output->base_count = 1U;
    output->base_versions[0] = (update_version_t){1U, 2U, 3U};
    output->base_epochs[0] = 7U;
    output->package_size = 64U;
    output->package_hash[0] = 2U;
    *reason_out = UPDATE_RUNTIME_REASON_NONE;
    return OK;
}

int update_runtime_get_installed_version(update_version_t* version_out,
                                          uint32_t* epoch_out) {
    if (!version_out || !epoch_out) return ERR_NULL;
    *version_out = (update_version_t){1U, 2U, 3U};
    *epoch_out = 7U;
    return OK;
}

int update_runtime_file_matches(const char* path, uint32_t expected_size,
                                const uint8_t expected_hash[32],
                                uint8_t* matches_out) {
    (void)path;
    (void)expected_size;
    (void)expected_hash;
    if (!matches_out) return ERR_NULL;
    *matches_out = 0U;
    return OK;
}

int update_runtime_verify_file_for_manifest(
    const char* path, const update_runtime_manifest_t* manifest,
    update_runtime_verification_t* result_out) {
    (void)path;
    (void)manifest;
    if (result_out) *result_out = (update_runtime_verification_t){0};
    return ERR_UNAVAILABLE;
}

int update_remote_github_runtime_query(
    const char* tag, const update_remote_options_t* options,
    update_remote_github_release_t* release_out,
    update_remote_result_t* result_out) {
    (void)tag;
    (void)options;
    if (release_out) *release_out = (update_remote_github_release_t){0};
    if (result_out) {
        *result_out = (update_remote_result_t){0};
        result_out->reason = UPDATE_REMOTE_REASON_RELEASE_NOT_FOUND;
    }
    return ERR_NOT_FOUND;
}

static int update_remote_runtime_host_test(void) {
    int result = update_remote_runtime_host_test_contracts();

    if (result == 0) {
        for (uint32_t index = 0U; index <= UPDATE_REMOTE_RUNTIME_STATE_FAILED;
             index++) {
            if (!update_remote_runtime_state_name(
                    (update_remote_runtime_state_t)index)) return 501;
        }
        for (uint32_t index = 0U; index <= UPDATE_REMOTE_RUNTIME_REASON_TIME;
             index++) {
            if (!update_remote_runtime_reason_name(
                    (update_remote_runtime_reason_t)index)) return 502;
        }
    }
    return result;
}

int main(void) {
    int result;

    host_http_status.state = HTTP_STATE_COMPLETE;
    coverage_active = 1U;
    result = update_remote_runtime_host_test();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != 0) return result;
    puts("update-remote-runtime-host: PASS");
    return 0;
}
