#include <stdint.h>
#include <stdio.h>

#include "core/crypto.h"
#include "core/errors.h"
#include "core/http.h"
#include "core/log.h"
#include "core/string.h"
#include "core/tls.h"
#include "core/update_remote_github.h"
#include "update_remote_github_host.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U

const uint8_t update_remote_github_host_release_json[] =
    "{\"tag_name\":\"v1.2.3\",\"id\":123,\"name\":\"Release\","
    "\"published_at\":\"2026-09-04T12:34:56Z\",\"draft\":false,"
    "\"prerelease\":false,\"assets\":["
    "{\"name\":\"release.json\",\"size\":512,"
    "\"browser_download_url\":\"https://github.com/BadTonho/ZephyrOS/releases/download/v1.2.3/release.json\","
    "\"state\":\"uploaded\",\"digest\":\"sha256:0000000000000000000000000000000000000000000000000000000000000000\"},"
    "{\"name\":\"release.zum\",\"size\":256,"
    "\"browser_download_url\":\"https://github.com/BadTonho/ZephyrOS/releases/download/v1.2.3/release.zum\","
    "\"state\":\"uploaded\",\"digest\":null},"
    "{\"name\":\"update.zephyrosupd\",\"size\":1024,"
    "\"browser_download_url\":\"https://objects.githubusercontent.com/update.zephyrosupd\","
    "\"state\":\"uploaded\"},"
    "{\"name\":\"system.zsys\",\"size\":2048,"
    "\"browser_download_url\":\"https://release-assets.githubusercontent.com/system.zsys\","
    "\"state\":\"uploaded\"},"
    "{\"name\":\"notes.txt\",\"size\":12,"
    "\"browser_download_url\":\"https://github.com/BadTonho/ZephyrOS/releases/download/v1.2.3/notes.txt\"}]}";

const uint32_t update_remote_github_host_release_json_size =
    sizeof(update_remote_github_host_release_json) - 1U;

const uint8_t update_remote_github_host_runtime_json[] =
    "{\"tag_name\":\"v1.2.3\",\"id\":123,\"name\":null,"
    "\"published_at\":\"2026-09-04T12:34:56Z\",\"draft\":false,"
    "\"prerelease\":false,\"assets\":["
    "{\"name\":\"runtime.zum2\",\"size\":4096,"
    "\"browser_download_url\":\"https://github.com/BadTonho/ZephyrOS/releases/download/v1.2.3/runtime.zum2\","
    "\"state\":\"uploaded\"},"
    "{\"name\":\"runtime.zephyrosupd\",\"size\":8192,"
    "\"browser_download_url\":\"https://objects.githubusercontent.com/runtime.zephyrosupd\"},"
    "{\"name\":\"EXPLORER.BMP\",\"size\":64,"
    "\"browser_download_url\":\"https://github.com/BadTonho/ZephyrOS/releases/download/v1.2.3/EXPLORER.BMP\"},"
    "{\"name\":\"SHELL.BMP\",\"size\":64,"
    "\"browser_download_url\":\"https://github.com/BadTonho/ZephyrOS/releases/download/v1.2.3/SHELL.BMP\"}]}";

const uint32_t update_remote_github_host_runtime_json_size =
    sizeof(update_remote_github_host_runtime_json) - 1U;

const uint8_t* update_remote_github_host_http_payload;
uint32_t update_remote_github_host_http_payload_size;
uint8_t update_remote_github_host_http_mode;
int update_remote_github_host_http_start_result;
int update_remote_github_host_http_status_result;
int update_remote_github_host_http_error;
uint8_t update_remote_github_host_cancel;

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
    printf("ZCOV_BEGIN|case=host:core:update-remote-github|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:update-remote-github|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:update-remote-github|value=0x%08X\n",
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

int http_get_stream_start_ex(const char* url, uint32_t body_limit,
                             http_body_sink_t sink, void* context,
                             const http_request_options_t* options) {
    (void)url;
    (void)body_limit;
    (void)options;
    if (update_remote_github_host_http_start_result != OK) {
        return update_remote_github_host_http_start_result;
    }
    if (!sink) return ERR_NULL;
    return sink(update_remote_github_host_http_payload,
                update_remote_github_host_http_payload_size, context);
}

int http_get_status(http_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    if (update_remote_github_host_http_status_result != OK) {
        return update_remote_github_host_http_status_result;
    }
    kmemset(out_status, 0, sizeof(*out_status));
    out_status->initialized = 1U;
    out_status->secure = 1U;
    out_status->tls_verified = 1U;
    out_status->tls_reason = TLS_REASON_NONE;
    out_status->body_length = update_remote_github_host_http_payload_size;
    if (update_remote_github_host_http_mode ==
        UPDATE_REMOTE_GITHUB_HOST_HTTP_FAILED) {
        out_status->state = HTTP_STATE_FAILED;
        out_status->last_error = update_remote_github_host_http_error;
    } else if (update_remote_github_host_http_mode ==
               UPDATE_REMOTE_GITHUB_HOST_HTTP_WAIT) {
        out_status->state = HTTP_STATE_RECEIVING_BODY;
    } else {
        out_status->state = HTTP_STATE_COMPLETE;
        out_status->status_code = update_remote_github_host_http_mode ==
                                  UPDATE_REMOTE_GITHUB_HOST_HTTP_NOT_FOUND ?
                                  404U : 200U;
    }
    return OK;
}

int http_reset(void) {
    return OK;
}

int update_remote_github_host_cancel_check(void* context) {
    (void)context;
    return update_remote_github_host_cancel ? 1 : 0;
}

void process_block(uint32_t ticks) {
    (void)ticks;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = update_remote_github_host_test_contracts();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != OK) return result;
    puts("update-remote-github-host: PASS");
    return OK;
}
