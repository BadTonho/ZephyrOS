#include <stdint.h>
#include <stdio.h>

#include "core/crypto.h"
#include "core/errors.h"
#include "core/http.h"
#include "core/log.h"
#include "core/network_manager.h"
#include "core/string.h"
#include "core/update.h"
#include "core/update_remote.h"
#include "update_remote_host.h"
#include "core/update_remote_runtime.h"
#include "fs/fs.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_FILE_CAPACITY 1024U
#define HOST_FILE_COUNT 8U

typedef struct {
    uint8_t present;
    char name[32];
    uint8_t data[HOST_FILE_CAPACITY];
    uint32_t size;
} host_file_t;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static host_file_t host_files[HOST_FILE_COUNT];
static uint8_t host_fs_type;
static uint8_t host_update_ready = 1U;
static uint8_t host_network_ready = 1U;
static int host_stream_index = -1;
static uint32_t host_stream_expected;
static http_status_t host_http_status;
static uint8_t host_http_body[UPDATE_REMOTE_MANIFEST_SIZE];
static uint32_t host_http_body_size;
static uint8_t host_http_pending;

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
    printf("ZCOV_BEGIN|case=host:core:update-remote|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:update-remote|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:update-remote|value=0x%08X\n",
           (uint32_t)result);
}

static int host_file_find(const char* name) {
    if (!name) return -1;
    for (uint32_t index = 0U; index < HOST_FILE_COUNT; index++) {
        if (host_files[index].present &&
            kstrcmp(host_files[index].name, name) == 0) return (int)index;
    }
    return -1;
}

static int host_file_open(const char* name) {
    int index = host_file_find(name);

    if (index >= 0) return index;
    for (uint32_t slot = 0U; slot < HOST_FILE_COUNT; slot++) {
        if (!host_files[slot].present) {
            host_files[slot].present = 1U;
            host_files[slot].size = 0U;
            kmemset(host_files[slot].name, 0, sizeof(host_files[slot].name));
            for (uint32_t offset = 0U;
                 offset + 1U < sizeof(host_files[slot].name) && name[offset];
                 offset++) host_files[slot].name[offset] = name[offset];
            return (int)slot;
        }
    }
    return -1;
}

void update_remote_host_set_fs_type(uint8_t type) {
    host_fs_type = type;
}

void update_remote_host_set_update_ready(uint8_t ready) {
    host_update_ready = ready;
}

void update_remote_host_set_network_ready(uint8_t ready) {
    host_network_ready = ready;
}

void update_remote_host_set_http_pending(uint8_t pending) {
    host_http_pending = pending;
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

int fs_get_root_file_info(const char* filename, uint32_t* size_out,
                          uint8_t* attributes_out) {
    int index;

    if (!filename || !size_out) return ERR_NULL;
    index = host_file_find(filename);
    if (index < 0) return ERR_NOT_FOUND;
    *size_out = host_files[index].size;
    if (attributes_out) *attributes_out = FS_ATTRIBUTE_ARCHIVE;
    return OK;
}

int fs_read_file_range_at(const char* path, uint32_t offset,
                          uint8_t* buffer, uint32_t max_size,
                          uint32_t* bytes_read) {
    int index;

    if (bytes_read) *bytes_read = 0U;
    if (!path || !buffer || !bytes_read) return ERR_NULL;
    index = host_file_find(path);
    if (index < 0 || offset > host_files[index].size ||
        max_size > host_files[index].size - offset) return ERR_DISK;
    kmemcpy(buffer, host_files[index].data + offset, max_size);
    *bytes_read = max_size;
    return OK;
}

int fs_atomic_write_root(const char* filename, const uint8_t* data,
                         uint32_t size, uint8_t attributes,
                         fs_atomic_mode_t mode) {
    int index;

    (void)attributes;
    (void)mode;
    if (!filename || (size && !data)) return ERR_NULL;
    if (size > HOST_FILE_CAPACITY) return ERR_OVERFLOW;
    index = host_file_open(filename);
    if (index < 0) return ERR_UNAVAILABLE;
    if (size) kmemcpy(host_files[index].data, data, size);
    host_files[index].size = size;
    return OK;
}

int fs_atomic_delete_root(const char* filename) {
    int index = host_file_find(filename);

    if (index < 0) return ERR_NOT_FOUND;
    host_files[index].present = 0U;
    host_files[index].size = 0U;
    return OK;
}

uint8_t fs_get_type(void) {
    return host_fs_type;
}

int fs_get_info(fs_info_t* info) {
    if (!info) return ERR_NULL;
    kmemset(info, 0, sizeof(*info));
    if (host_fs_type != FS_TYPE_FAT12) return ERR_UNAVAILABLE;
    info->type = FS_TYPE_FAT12;
    info->bytes_per_sector = 512U;
    info->sectors_per_cluster = 1U;
    info->free_clusters = 128U;
    return OK;
}

int fs_stream_begin_root(const char* filename, uint32_t expected_size,
                         uint8_t attributes) {
    int index;

    (void)attributes;
    if (!filename || expected_size > HOST_FILE_CAPACITY) return ERR_INVALID;
    index = host_file_open(filename);
    if (index < 0) return ERR_UNAVAILABLE;
    host_files[index].size = 0U;
    host_stream_index = index;
    host_stream_expected = expected_size;
    return OK;
}

int fs_stream_write_root(const uint8_t* data, uint32_t size) {
    if (host_stream_index < 0 || (size && !data)) return ERR_STATE;
    if (host_files[host_stream_index].size + size > HOST_FILE_CAPACITY ||
        host_files[host_stream_index].size + size > host_stream_expected) {
        return ERR_OVERFLOW;
    }
    kmemcpy(host_files[host_stream_index].data +
                host_files[host_stream_index].size, data, size);
    host_files[host_stream_index].size += size;
    return OK;
}

int fs_stream_finish_root(void) {
    if (host_stream_index < 0 ||
        host_files[host_stream_index].size != host_stream_expected) return ERR_STATE;
    host_stream_index = -1;
    return OK;
}

int fs_stream_abort_root(void) {
    if (host_stream_index >= 0) {
        host_files[host_stream_index].size = 0U;
        host_stream_index = -1;
    }
    return OK;
}

int fs_stream_is_active(void) {
    return host_stream_index >= 0;
}

int network_manager_get_status(network_manager_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = (network_manager_status_t){0};
    out_status->http_available = host_network_ready;
    out_status->ipv4_configured = host_network_ready;
    return OK;
}

int http_get_start_ex(const char* url, const http_request_options_t* options) {
    (void)url;
    (void)options;
    kmemset(&host_http_status, 0, sizeof(host_http_status));
    host_http_status.state = host_http_pending ?
                             HTTP_STATE_RECEIVING_BODY : HTTP_STATE_COMPLETE;
    host_http_status.status_code = 200U;
    host_http_status.body_length = host_http_body_size;
    host_http_status.content_length = host_http_body_size;
    host_http_status.has_content_length = 1U;
    return OK;
}

int http_get_stream_start_ex(const char* url, uint32_t body_limit,
                             http_body_sink_t sink, void* context,
                             const http_request_options_t* options) {
    int result;

    (void)url;
    (void)options;
    if (!sink || body_limit < host_http_body_size) return ERR_INVALID;
    result = sink(host_http_body, host_http_body_size, context);
    if (result != OK) return result;
    host_http_status.state = HTTP_STATE_COMPLETE;
    host_http_status.status_code = 200U;
    host_http_status.body_length = host_http_body_size;
    host_http_status.content_length = host_http_body_size;
    host_http_status.has_content_length = 1U;
    return OK;
}

int http_get_status(http_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = host_http_status;
    return OK;
}

int http_get_body(const uint8_t** out_body, uint32_t* out_length) {
    if (!out_body || !out_length) return ERR_NULL;
    *out_body = host_http_body;
    *out_length = host_http_body_size;
    return OK;
}

int http_reset(void) {
    host_http_status.state = HTTP_STATE_IDLE;
    return OK;
}

void process_block(uint32_t ticks) {
    (void)ticks;
}

int update_is_ready(void) {
    return host_update_ready;
}

int update_get_installed_version(update_version_t* version_out,
                                 uint32_t* epoch_out) {
    if (!version_out || !epoch_out) return ERR_NULL;
    *version_out = (update_version_t){1U, 2U, 3U};
    *epoch_out = 7U;
    return OK;
}

int update_verify_file(const char* path, update_verification_t* result_out) {
    (void)path;
    if (!result_out) return ERR_NULL;
    *result_out = (update_verification_t){
        ZUPD_REASON_NONE, {1U, 2U, 3U}, {1U, 2U, 4U}, 7U, 8U,
        7U, 0U};
    return OK;
}

int update_remote_runtime_init(void) {
    return OK;
}

int update_remote_runtime_enable(void) {
    return OK;
}

int update_remote_runtime_disable(void) {
    return OK;
}

int main(void) {
    int result;

    kmemcpy(host_http_body, "REMOTE!", 7U);
    host_http_body_size = 7U;
    coverage_active = 1U;
    result = update_remote_host_test_contracts();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != 0) return result;
    puts("update-remote-host: PASS");
    return 0;
}
