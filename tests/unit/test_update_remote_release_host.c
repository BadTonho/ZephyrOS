#include <stdint.h>
#include <stdio.h>

#include "core/crypto.h"
#include "core/errors.h"
#include "core/http.h"
#include "core/log.h"
#include "core/string.h"
#include "core/tls.h"
#include "core/update_remote.h"
#include "core/update_remote_github.h"
#include "update_remote_release_host.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U

const uint8_t update_remote_release_host_descriptor[] =
    "{\"format\":\"zephyros-release-v1\","
    "\"release_id\":\"ep6-alt\","
    "\"release_name\":\"EP6 fixture alternate\","
    "\"channel\":\"stable\","
    "\"source_commit\":\"1111111111111111111111111111111111111111\","
    "\"tag\":\"ep6-alt\","
    "\"version_lock\":{\"minimum_version\":\"0.1.0\","
    "\"target_version\":\"0.1.1\",\"base_epoch\":0,\"target_epoch\":0},"
    "\"assets\":{\"package\":{\"name\":\"APPLY.ZUP\",\"size\":9954,"
    "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"},"
    "\"manifest\":{\"name\":\"stable2.zum\",\"size\":256,"
    "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"}}}";

const uint32_t update_remote_release_host_descriptor_size =
    sizeof(update_remote_release_host_descriptor) - 1U;

const uint8_t* update_remote_release_host_http_body;
uint32_t update_remote_release_host_http_body_size;
uint8_t update_remote_release_host_http_mode;
uint16_t update_remote_release_host_http_status_code;
uint8_t update_remote_release_host_http_has_content_length;
uint32_t update_remote_release_host_http_content_length;
int update_remote_release_host_http_start_result;
int update_remote_release_host_http_status_result;
int update_remote_release_host_http_body_result;
int update_remote_release_host_http_error;
int update_remote_release_host_status_result;
uint8_t update_remote_release_host_enabled;
uint8_t update_remote_release_host_network_ready;
uint8_t update_remote_release_host_package_cached;
int update_remote_release_host_github_result;
uint8_t update_remote_release_host_cancel;

static http_status_t host_http_status;
static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

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
    printf("ZCOV_BEGIN|case=host:core:update-remote-release|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:update-remote-release|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:update-remote-release|value=0x%08X\n",
           (uint32_t)result);
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

int http_get_start_ex(const char* url, const http_request_options_t* options) {
    (void)url;
    (void)options;
    if (update_remote_release_host_http_start_result != OK) {
        return update_remote_release_host_http_start_result;
    }
    kmemset(&host_http_status, 0, sizeof(host_http_status));
    host_http_status.initialized = 1U;
    host_http_status.secure = 1U;
    host_http_status.tls_verified = 1U;
    host_http_status.tls_reason = TLS_REASON_NONE;
    host_http_status.state = update_remote_release_host_http_mode ==
                              UPDATE_REMOTE_RELEASE_HOST_HTTP_FAILED ?
                              HTTP_STATE_FAILED :
                              update_remote_release_host_http_mode ==
                              UPDATE_REMOTE_RELEASE_HOST_HTTP_WAIT ?
                              HTTP_STATE_RECEIVING_BODY : HTTP_STATE_COMPLETE;
    host_http_status.status_code =
        update_remote_release_host_http_mode ==
        UPDATE_REMOTE_RELEASE_HOST_HTTP_NOT_FOUND ? 404U :
        update_remote_release_host_http_status_code;
    host_http_status.body_length = update_remote_release_host_http_body_size;
    host_http_status.content_length =
        update_remote_release_host_http_content_length;
    host_http_status.has_content_length =
        update_remote_release_host_http_has_content_length;
    host_http_status.last_error = update_remote_release_host_http_error;
    return OK;
}

int http_get_status(http_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    if (update_remote_release_host_http_status_result != OK) {
        return update_remote_release_host_http_status_result;
    }
    *out_status = host_http_status;
    return OK;
}

int http_get_body(const uint8_t** out_body, uint32_t* out_length) {
    if (!out_body || !out_length) return ERR_NULL;
    if (update_remote_release_host_http_body_result != OK) {
        return update_remote_release_host_http_body_result;
    }
    *out_body = update_remote_release_host_http_body;
    *out_length = update_remote_release_host_http_body_size;
    return OK;
}

int http_reset(void) {
    host_http_status.state = HTTP_STATE_IDLE;
    return OK;
}

int update_remote_get_status(update_remote_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    if (update_remote_release_host_status_result != OK) {
        return update_remote_release_host_status_result;
    }
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->initialized = 1U;
    status_out->enabled = update_remote_release_host_enabled;
    status_out->network_ready = update_remote_release_host_network_ready;
    status_out->package_cached = update_remote_release_host_package_cached;
    status_out->state = status_out->enabled && status_out->network_ready ?
                        UPDATE_REMOTE_STATE_READY :
                        UPDATE_REMOTE_STATE_UNAVAILABLE;
    return OK;
}

int update_remote_check(const char* manifest_url,
                        const update_remote_options_t* options,
                        update_remote_result_t* result_out) {
    (void)manifest_url;
    (void)options;
    if (!result_out) return ERR_NULL;
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = UPDATE_REMOTE_REASON_NONE;
    return OK;
}

int update_remote_fetch(const char* manifest_url,
                        const update_remote_options_t* options,
                        update_remote_result_t* result_out) {
    return update_remote_check(manifest_url, options, result_out);
}

int update_remote_github_query(
    const char* tag, const update_remote_options_t* options,
    update_remote_github_release_t* release_out,
    update_remote_result_t* result_out) {
    (void)tag;
    (void)options;
    (void)release_out;
    (void)result_out;
    return update_remote_release_host_github_result;
}

int update_remote_release_host_cancel_check(void* context) {
    (void)context;
    return update_remote_release_host_cancel ? 1 : 0;
}

void process_block(uint32_t ticks) {
    (void)ticks;
}

int main(void) {
    int result;

    update_remote_release_host_http_body =
        update_remote_release_host_descriptor;
    update_remote_release_host_http_body_size =
        update_remote_release_host_descriptor_size;
    update_remote_release_host_http_mode =
        UPDATE_REMOTE_RELEASE_HOST_HTTP_COMPLETE;
    update_remote_release_host_http_status_code = 200U;
    update_remote_release_host_http_has_content_length = 1U;
    update_remote_release_host_http_content_length =
        update_remote_release_host_descriptor_size;
    update_remote_release_host_http_start_result = OK;
    update_remote_release_host_http_status_result = OK;
    update_remote_release_host_http_body_result = OK;
    update_remote_release_host_http_error = ERR_DISK;
    update_remote_release_host_status_result = OK;
    update_remote_release_host_enabled = 1U;
    update_remote_release_host_network_ready = 1U;
    update_remote_release_host_package_cached = 1U;
    update_remote_release_host_github_result = ERR_DISK;
    coverage_active = 1U;
    result = update_remote_release_host_test_contracts();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != OK) return result;
    puts("update-remote-release-host: PASS");
    return OK;
}
