#include "core/update_remote_runtime.h"
#include "core/crypto.h"
#include "core/errors.h"
#include "core/http.h"
#include "core/log.h"
#include "core/network_manager.h"
#include "core/string.h"
#include "core/update_remote_config.h"
#include "core/update_remote_github.h"
#include "fs/fs.h"
#include "process/process.h"

#define RUNTIME_REMOTE_RECORD_MAGIC "ZRV2"
#define RUNTIME_REMOTE_RECORD_VERSION 1U
#define RUNTIME_REMOTE_RECORD_HASH_OFFSET 480U
#define RUNTIME_REMOTE_PHASE_CLEAN 0U
#define RUNTIME_REMOTE_PHASE_DOWNLOADING 1U
#define RUNTIME_REMOTE_SLOT_NONE 0xFFU
#define RUNTIME_REMOTE_HTTP_OK 200U
#define RUNTIME_REMOTE_WAIT_BLOCK_TICKS 1U
#define RUNTIME_REMOTE_ATTRIBUTES \
    (FS_ATTRIBUTE_HIDDEN | FS_ATTRIBUTE_SYSTEM | FS_ATTRIBUTE_ARCHIVE)
#define RUNTIME_REMOTE_RECORD_SEQ 8U
#define RUNTIME_REMOTE_RECORD_PHASE 12U
#define RUNTIME_REMOTE_RECORD_ACTIVE 13U
#define RUNTIME_REMOTE_RECORD_PENDING 14U
#define RUNTIME_REMOTE_RECORD_MODE 15U
#define RUNTIME_REMOTE_RECORD_ENTRIES 16U
#define RUNTIME_REMOTE_RECORD_MASK 18U
#define RUNTIME_REMOTE_RECORD_MANIFEST_HASH 20U
#define RUNTIME_REMOTE_RECORD_PACKAGE_HASH 52U
#define RUNTIME_REMOTE_RECORD_PACKAGE_SIZE 84U
#define RUNTIME_REMOTE_RECORD_TARGET_VERSION 88U
#define RUNTIME_REMOTE_RECORD_TARGET_EPOCH 94U

typedef struct {
    uint32_t sequence;
    uint8_t phase;
    uint8_t active_slot;
    uint8_t pending_slot;
    uint8_t mode;
    uint16_t entry_count;
    uint16_t asset_mask;
    uint8_t manifest_hash[32];
    uint8_t package_hash[32];
    uint32_t package_size;
    update_version_t target_version;
    uint32_t target_epoch;
} runtime_remote_record_t;

typedef struct {
    crypto_sha256_ctx_t sha;
    int write_error;
} runtime_remote_download_t;

typedef struct {
    char name[UPDATE_REMOTE_PATH_SIZE];
    uint32_t size;
    uint8_t hash[32];
} runtime_remote_descriptor_asset_t;

typedef struct {
    uint8_t github;
    uint8_t descriptor_present;
    char manifest_url[UPDATE_REMOTE_RUNTIME_URL_SIZE];
    char package_url[UPDATE_REMOTE_RUNTIME_URL_SIZE];
    char release_url[UPDATE_REMOTE_RUNTIME_URL_SIZE];
    char tag[UPDATE_REMOTE_TAG_SIZE];
    char release_id[UPDATE_REMOTE_RELEASE_ID_SIZE];
    char release_name[UPDATE_REMOTE_RELEASE_NAME_SIZE];
    runtime_remote_descriptor_asset_t manifest_asset;
    runtime_remote_descriptor_asset_t package_asset;
    runtime_remote_descriptor_asset_t assets[UPDATE_RUNTIME_MAX_ENTRIES];
    uint16_t asset_count;
    update_remote_github_release_t release;
} runtime_remote_source_t;

static const char* runtime_remote_record_aliases[2] = {
    "ZRV0.STA", "ZRV1.STA"
};
static const char* runtime_remote_manifest_aliases[2] = {
    "ZRV0.MAN", "ZRV1.MAN"
};
static const char* runtime_remote_package_aliases[2] = {
    "ZRV0.PKG", "ZRV1.PKG"
};
static const uint8_t runtime_remote_zero_hash[32] = {0};

static update_remote_runtime_status_t runtime_remote_status;
static runtime_remote_record_t runtime_remote_record;
static runtime_remote_record_t runtime_remote_previous_record;
static runtime_remote_record_t runtime_remote_recovery_record;
static uint8_t runtime_remote_previous_record_valid;
static uint8_t runtime_remote_recovery_record_valid;
static int runtime_remote_record_slot = -1;
static uint8_t runtime_remote_manifest_raw[UPDATE_RUNTIME_MANIFEST_SIZE];
static uint8_t runtime_remote_manifest_hash[32];
static uint8_t runtime_remote_manifest_valid;
static uint8_t runtime_remote_cancelled;
static uint32_t runtime_remote_progress_base;
static runtime_remote_download_t runtime_remote_transfer;
static http_status_t runtime_remote_http;
static uint8_t runtime_remote_descriptor[HTTP_BODY_CAPACITY];
static uint32_t runtime_remote_descriptor_length;
static runtime_remote_source_t runtime_remote_source;
static char runtime_remote_tag[UPDATE_REMOTE_TAG_SIZE];

static const update_remote_github_asset_t* runtime_remote_find_github_asset(
    const char* name);

static uint16_t runtime_remote_read_u16(const uint8_t* raw) {
    return (uint16_t)raw[0] | ((uint16_t)raw[1] << 8U);
}

static uint32_t runtime_remote_read_u32(const uint8_t* raw) {
    return (uint32_t)raw[0] |
           ((uint32_t)raw[1] << 8U) |
           ((uint32_t)raw[2] << 16U) |
           ((uint32_t)raw[3] << 24U);
}

static void runtime_remote_write_u16(uint8_t* raw, uint16_t value) {
    raw[0] = (uint8_t)value;
    raw[1] = (uint8_t)(value >> 8U);
}

static void runtime_remote_write_u32(uint8_t* raw, uint32_t value) {
    raw[0] = (uint8_t)value;
    raw[1] = (uint8_t)(value >> 8U);
    raw[2] = (uint8_t)(value >> 16U);
    raw[3] = (uint8_t)(value >> 24U);
}

static void runtime_remote_copy_text(char* output, uint32_t capacity,
                                     const char* input) {
    uint32_t index = 0U;

    if (!output || !capacity) return;
    if (!input) input = "";
    while (input[index] && index + 1U < capacity) {
        output[index] = input[index];
        index++;
    }
    output[index] = '\0';
}

static int runtime_remote_tag_valid(const char* tag) {
    uint32_t length;

    if (!tag || !tag[0]) return 0;
    length = kstrlen(tag);
    if (length >= UPDATE_REMOTE_TAG_SIZE) return 0;
    for (uint32_t index = 0U; index < length; index++) {
        char value = tag[index];
        if (!((value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '.' ||
              value == '_' || value == '-')) return 0;
    }
    return 1;
}

static int runtime_remote_magic(const uint8_t* raw, const char* magic) {
    return raw && magic && raw[0] == (uint8_t)magic[0] &&
           raw[1] == (uint8_t)magic[1] && raw[2] == (uint8_t)magic[2] &&
           raw[3] == (uint8_t)magic[3];
}

static int runtime_remote_control_hash(uint8_t* raw) {
    uint8_t hash[32];
    int result = crypto_sha256(raw, RUNTIME_REMOTE_RECORD_HASH_OFFSET, hash);
    if (result == OK) kmemcpy(raw + RUNTIME_REMOTE_RECORD_HASH_OFFSET,
                              hash, sizeof(hash));
    return result;
}

static int runtime_remote_control_valid(const uint8_t* raw) {
    uint8_t hash[32];
    if (!raw || crypto_sha256(raw, RUNTIME_REMOTE_RECORD_HASH_OFFSET, hash) != OK) {
        return 0;
    }
    return crypto_equal(hash, raw + RUNTIME_REMOTE_RECORD_HASH_OFFSET, 32U);
}

static void runtime_remote_encode_record(uint8_t* raw,
                                         const runtime_remote_record_t* record) {
    kmemset(raw, 0, UPDATE_RUNTIME_CACHE_RECORD_SIZE);
    kmemcpy(raw, RUNTIME_REMOTE_RECORD_MAGIC, 4U);
    runtime_remote_write_u16(raw + 4U, RUNTIME_REMOTE_RECORD_VERSION);
    runtime_remote_write_u16(raw + 6U, UPDATE_RUNTIME_CACHE_RECORD_SIZE);
    runtime_remote_write_u32(raw + RUNTIME_REMOTE_RECORD_SEQ,
                             record->sequence);
    raw[RUNTIME_REMOTE_RECORD_PHASE] = record->phase;
    raw[RUNTIME_REMOTE_RECORD_ACTIVE] = record->active_slot;
    raw[RUNTIME_REMOTE_RECORD_PENDING] = record->pending_slot;
    raw[RUNTIME_REMOTE_RECORD_MODE] = record->mode;
    runtime_remote_write_u16(raw + RUNTIME_REMOTE_RECORD_ENTRIES,
                             record->entry_count);
    runtime_remote_write_u16(raw + RUNTIME_REMOTE_RECORD_MASK,
                             record->asset_mask);
    kmemcpy(raw + RUNTIME_REMOTE_RECORD_MANIFEST_HASH,
            record->manifest_hash, 32U);
    kmemcpy(raw + RUNTIME_REMOTE_RECORD_PACKAGE_HASH,
            record->package_hash, 32U);
    runtime_remote_write_u32(raw + RUNTIME_REMOTE_RECORD_PACKAGE_SIZE,
                             record->package_size);
    runtime_remote_write_u16(raw + RUNTIME_REMOTE_RECORD_TARGET_VERSION,
                             record->target_version.major);
    runtime_remote_write_u16(raw + RUNTIME_REMOTE_RECORD_TARGET_VERSION + 2U,
                             record->target_version.minor);
    runtime_remote_write_u16(raw + RUNTIME_REMOTE_RECORD_TARGET_VERSION + 4U,
                             record->target_version.patch);
    runtime_remote_write_u32(raw + RUNTIME_REMOTE_RECORD_TARGET_EPOCH,
                             record->target_epoch);
    runtime_remote_control_hash(raw);
}

static int runtime_remote_decode_record(const uint8_t* raw,
                                        runtime_remote_record_t* record) {
    if (!raw || !record || !runtime_remote_control_valid(raw) ||
        !runtime_remote_magic(raw, RUNTIME_REMOTE_RECORD_MAGIC) ||
        runtime_remote_read_u16(raw + 4U) != RUNTIME_REMOTE_RECORD_VERSION ||
        runtime_remote_read_u16(raw + 6U) != UPDATE_RUNTIME_CACHE_RECORD_SIZE ||
        runtime_remote_read_u16(raw + RUNTIME_REMOTE_RECORD_ENTRIES) >
            UPDATE_RUNTIME_MAX_ENTRIES || raw[RUNTIME_REMOTE_RECORD_PHASE] >
            RUNTIME_REMOTE_PHASE_DOWNLOADING ||
        (raw[RUNTIME_REMOTE_RECORD_ACTIVE] != RUNTIME_REMOTE_SLOT_NONE &&
         raw[RUNTIME_REMOTE_RECORD_ACTIVE] > 1U) ||
        (raw[RUNTIME_REMOTE_RECORD_PENDING] != RUNTIME_REMOTE_SLOT_NONE &&
         raw[RUNTIME_REMOTE_RECORD_PENDING] > 1U) ||
        raw[RUNTIME_REMOTE_RECORD_MODE] > UPDATE_REMOTE_RUNTIME_FETCH_FULL ||
        runtime_remote_read_u32(raw + RUNTIME_REMOTE_RECORD_PACKAGE_SIZE) >
            UPDATE_RUNTIME_PACKAGE_MAX_SIZE ||
        (runtime_remote_read_u16(raw + RUNTIME_REMOTE_RECORD_MASK) &
         (uint16_t)~((1U << UPDATE_RUNTIME_MAX_ENTRIES) - 1U))) {
        return ERR_INVALID;
    }
    kmemset(record, 0, sizeof(*record));
    record->sequence = runtime_remote_read_u32(raw + RUNTIME_REMOTE_RECORD_SEQ);
    record->phase = raw[RUNTIME_REMOTE_RECORD_PHASE];
    record->active_slot = raw[RUNTIME_REMOTE_RECORD_ACTIVE];
    record->pending_slot = raw[RUNTIME_REMOTE_RECORD_PENDING];
    record->mode = raw[RUNTIME_REMOTE_RECORD_MODE];
    record->entry_count = runtime_remote_read_u16(
        raw + RUNTIME_REMOTE_RECORD_ENTRIES);
    record->asset_mask = runtime_remote_read_u16(raw + RUNTIME_REMOTE_RECORD_MASK);
    kmemcpy(record->manifest_hash, raw + RUNTIME_REMOTE_RECORD_MANIFEST_HASH, 32U);
    kmemcpy(record->package_hash, raw + RUNTIME_REMOTE_RECORD_PACKAGE_HASH, 32U);
    record->package_size = runtime_remote_read_u32(
        raw + RUNTIME_REMOTE_RECORD_PACKAGE_SIZE);
    record->target_version.major = runtime_remote_read_u16(
        raw + RUNTIME_REMOTE_RECORD_TARGET_VERSION);
    record->target_version.minor = runtime_remote_read_u16(
        raw + RUNTIME_REMOTE_RECORD_TARGET_VERSION + 2U);
    record->target_version.patch = runtime_remote_read_u16(
        raw + RUNTIME_REMOTE_RECORD_TARGET_VERSION + 4U);
    record->target_epoch = runtime_remote_read_u32(
        raw + RUNTIME_REMOTE_RECORD_TARGET_EPOCH);
    if ((record->phase == RUNTIME_REMOTE_PHASE_CLEAN &&
         record->pending_slot != RUNTIME_REMOTE_SLOT_NONE &&
         record->pending_slot != record->active_slot) ||
        (record->phase == RUNTIME_REMOTE_PHASE_DOWNLOADING &&
         (record->pending_slot == RUNTIME_REMOTE_SLOT_NONE ||
          record->pending_slot == record->active_slot))) {
        return ERR_INVALID;
    }
    for (uint32_t reserved = 98U; reserved < RUNTIME_REMOTE_RECORD_HASH_OFFSET;
         reserved++) {
        if (raw[reserved]) return ERR_INVALID;
    }
    if (record->phase == RUNTIME_REMOTE_PHASE_CLEAN) {
        if ((record->active_slot == RUNTIME_REMOTE_SLOT_NONE &&
             (record->entry_count != 0U || record->asset_mask != 0U ||
              record->package_size != 0U ||
              !crypto_equal(record->manifest_hash, runtime_remote_zero_hash,
                            32U) ||
              !crypto_equal(record->package_hash, runtime_remote_zero_hash,
                            32U) || record->target_epoch != 0U ||
              record->target_version.major != 0U ||
              record->target_version.minor != 0U ||
              record->target_version.patch != 0U)) ||
            (record->active_slot <= 1U &&
             (record->entry_count == 0U ||
              (record->asset_mask &
               (uint16_t)~((1U << record->entry_count) - 1U)) ||
              crypto_equal(record->manifest_hash, runtime_remote_zero_hash,
                           32U) ||
              (record->mode == UPDATE_REMOTE_RUNTIME_FETCH_FULL &&
               (record->package_size == 0U ||
                crypto_equal(record->package_hash, runtime_remote_zero_hash,
                             32U) || record->asset_mask != 0U)) ||
              (record->mode == UPDATE_REMOTE_RUNTIME_FETCH_SELECTIVE &&
               (record->package_size != 0U ||
                !crypto_equal(record->package_hash, runtime_remote_zero_hash,
                              32U)))))) {
            return ERR_INVALID;
        }
    }
    return OK;
}

static int runtime_remote_write_record(void) {
    uint8_t raw[UPDATE_RUNTIME_CACHE_RECORD_SIZE];
    uint8_t slot;

    runtime_remote_record.sequence++;
    runtime_remote_encode_record(raw, &runtime_remote_record);
    slot = (uint8_t)(runtime_remote_record.sequence & 1U);
    runtime_remote_record_slot = slot;
    return fs_atomic_write_root(runtime_remote_record_aliases[slot], raw,
                                sizeof(raw), RUNTIME_REMOTE_ATTRIBUTES,
                                FS_ATOMIC_CREATE_OR_REPLACE);
}

static void runtime_remote_asset_alias(char* output, uint8_t slot,
                                       uint8_t index) {
    output[0] = 'Z';
    output[1] = 'R';
    output[2] = 'V';
    output[3] = (char)('0' + slot);
    output[4] = '.';
    output[5] = (char)('0' + index / 10U);
    output[6] = (char)('0' + index % 10U);
    output[7] = '\0';
}

static int runtime_remote_delete_root(const char* path) {
    int result = fs_atomic_delete_root(path);
    return result == ERR_NOT_FOUND ? OK : result;
}

static int runtime_remote_delete_slot(uint8_t slot) {
    char alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];
    int result = OK;

    if (runtime_remote_delete_root(runtime_remote_manifest_aliases[slot]) != OK) {
        result = ERR_DISK;
    }
    if (runtime_remote_delete_root(runtime_remote_package_aliases[slot]) != OK) {
        result = ERR_DISK;
    }
    for (uint8_t index = 0U; index < UPDATE_RUNTIME_MAX_ENTRIES; index++) {
        runtime_remote_asset_alias(alias, slot, index);
        if (runtime_remote_delete_root(alias) != OK) result = ERR_DISK;
    }
    return result;
}

static int runtime_remote_load_records(void) {
    runtime_remote_record_t selected;
    runtime_remote_record_t candidate;
    uint8_t raw[UPDATE_RUNTIME_CACHE_RECORD_SIZE];
    uint8_t found = 0U;
    uint8_t found_any = 0U;

    kmemset(&selected, 0, sizeof(selected));
    kmemset(&runtime_remote_recovery_record, 0,
            sizeof(runtime_remote_recovery_record));
    runtime_remote_recovery_record_valid = 0U;
    for (uint8_t slot = 0U; slot < 2U; slot++) {
        uint32_t file_size = 0U;
        uint32_t bytes = 0U;
        int info_result = fs_get_root_file_info(
            runtime_remote_record_aliases[slot], &file_size, 0);
        if (info_result == ERR_NOT_FOUND) continue;
        found_any = 1U;
        if (info_result != OK || file_size != sizeof(raw) ||
            fs_read_file_range_at(runtime_remote_record_aliases[slot], 0U,
                                  raw, sizeof(raw), &bytes) != OK ||
            bytes != sizeof(raw) ||
            runtime_remote_decode_record(raw, &candidate) != OK) {
            continue;
        }
        if (!found || candidate.sequence > selected.sequence) {
            if (found) {
                runtime_remote_recovery_record = selected;
                runtime_remote_recovery_record_valid = 1U;
            }
            selected = candidate;
            runtime_remote_record_slot = slot;
            found = 1U;
        } else if (!runtime_remote_recovery_record_valid ||
                   candidate.sequence > runtime_remote_recovery_record.sequence) {
            runtime_remote_recovery_record = candidate;
            runtime_remote_recovery_record_valid = 1U;
        }
    }
    if (!found && found_any) return ERR_INVALID;
    if (!found) {
        kmemset(&runtime_remote_record, 0, sizeof(runtime_remote_record));
        runtime_remote_record.active_slot = RUNTIME_REMOTE_SLOT_NONE;
        runtime_remote_record.pending_slot = RUNTIME_REMOTE_SLOT_NONE;
        runtime_remote_recovery_record_valid = 0U;
        return OK;
    }
    runtime_remote_record = selected;
    return OK;
}

static int runtime_remote_hash_file(const char* path, uint32_t expected_size,
                                    uint8_t hash[32]) {
    crypto_sha256_ctx_t sha;
    uint8_t buffer[1024];
    uint32_t offset = 0U;
    uint32_t bytes;
    int result = crypto_sha256_init(&sha);

    while (result == OK && offset < expected_size) {
        uint32_t chunk = expected_size - offset;
        if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
        result = fs_read_file_range_at(path, offset, buffer, chunk, &bytes);
        if (result == OK && bytes != chunk) result = ERR_DISK;
        if (result == OK) {
            result = crypto_sha256_update(&sha, buffer, bytes);
            if (result == OK) offset += bytes;
        }
    }
    if (result == OK) result = crypto_sha256_final(&sha, hash);
    return result;
}

static int runtime_remote_wait_http(const update_remote_options_t* options) {
    while (1) {
        if (http_get_status(&runtime_remote_http) != OK) return ERR_STATE;
        runtime_remote_status.http_status = runtime_remote_http.status_code;
        runtime_remote_status.bytes_received = runtime_remote_progress_base +
                                               runtime_remote_http.body_length;
        if (runtime_remote_http.state == HTTP_STATE_COMPLETE) return OK;
        if (runtime_remote_http.state == HTTP_STATE_FAILED) {
            return runtime_remote_http.last_error == OK ?
                   ERR_STATE : runtime_remote_http.last_error;
        }
        if (options && options->cancel_check &&
            options->cancel_check(options->cancel_context)) {
            runtime_remote_cancelled = 1U;
            http_reset();
            return ERR_TIMEOUT;
        }
        process_block(RUNTIME_REMOTE_WAIT_BLOCK_TICKS);
    }
}

static void runtime_remote_http_options(const update_remote_options_t* options,
                                        http_request_options_t* output) {
    kmemset(output, 0, sizeof(*output));
    if (options) {
        output->accept = options->http_accept;
        output->api_version = options->http_api_version;
        output->require_https = options->http_require_https;
        output->follow_redirects = options->http_follow_redirects;
        output->max_redirects = options->http_max_redirects;
    }
    if (runtime_remote_source.github) {
        output->require_https = 1U;
        output->follow_redirects = 1U;
        output->max_redirects = HTTP_MAX_REDIRECTS;
    }
}

static update_remote_runtime_reason_t runtime_remote_http_reason(void) {
    if (runtime_remote_http.tls_reason == TLS_REASON_TIME_UNAVAILABLE) {
        return UPDATE_REMOTE_RUNTIME_REASON_TIME;
    }
    if (runtime_remote_http.secure &&
        (!runtime_remote_http.tls_verified ||
         runtime_remote_http.tls_reason != TLS_REASON_NONE)) {
        return UPDATE_REMOTE_RUNTIME_REASON_TLS;
    }
    if (runtime_remote_http.status_code == 404U) {
        return UPDATE_REMOTE_RUNTIME_REASON_RELEASE_NOT_FOUND;
    }
    if (runtime_remote_http.last_error == ERR_TIMEOUT) {
        return UPDATE_REMOTE_RUNTIME_REASON_TIMEOUT;
    }
    if (runtime_remote_http.last_error == ERR_UNAVAILABLE) {
        return UPDATE_REMOTE_RUNTIME_REASON_DNS;
    }
    return UPDATE_REMOTE_RUNTIME_REASON_HTTP;
}

static int runtime_remote_network_ready(void) {
    network_manager_status_t network;
    if (network_manager_get_status(&network) != OK) return 0;
    return network.http_available && network.ipv4_configured;
}

static int runtime_remote_fetch_manifest(const char* url,
                                         const update_remote_options_t* options) {
    const uint8_t* body;
    uint32_t length;
    http_request_options_t http_options;
    int result;

    runtime_remote_http_options(options, &http_options);
    runtime_remote_status.total_bytes = UPDATE_RUNTIME_MANIFEST_SIZE;
    runtime_remote_status.bytes_received = 0U;
    runtime_remote_progress_base = 0U;
    runtime_remote_cancelled = 0U;
    result = http_get_start_ex(url, &http_options);
    if (result == OK) result = runtime_remote_wait_http(options);
    if (result != OK) return result;
    if (runtime_remote_http.status_code != RUNTIME_REMOTE_HTTP_OK ||
        !runtime_remote_http.has_content_length ||
        runtime_remote_http.content_length != UPDATE_RUNTIME_MANIFEST_SIZE ||
        runtime_remote_http.body_length != UPDATE_RUNTIME_MANIFEST_SIZE ||
        http_get_body(&body, &length) != OK || length != UPDATE_RUNTIME_MANIFEST_SIZE) {
        return ERR_INVALID;
    }
    kmemcpy(runtime_remote_manifest_raw, body, length);
    runtime_remote_progress_base = UPDATE_RUNTIME_MANIFEST_SIZE;
    runtime_remote_status.bytes_received = UPDATE_RUNTIME_MANIFEST_SIZE;
    return OK;
}

static int runtime_remote_build_relative_url(const char* source,
                                             const char* name, char* output) {
    uint32_t prefix;
    uint32_t source_length;
    uint32_t name_length;

    if (!source || !name || !output) return ERR_NULL;
    source_length = kstrlen(source);
    name_length = kstrlen(name);
    prefix = source_length;
    while (prefix && source[prefix - 1U] != '/') prefix--;
    if (!prefix || prefix + name_length + 1U > UPDATE_REMOTE_RUNTIME_URL_SIZE) {
        return ERR_OVERFLOW;
    }
    for (uint32_t index = 0U; index < prefix; index++) output[index] = source[index];
    for (uint32_t index = 0U; index <= name_length; index++) {
        output[prefix + index] = name[index];
    }
    return OK;
}

typedef struct {
    const uint8_t* data;
    uint32_t length;
    uint32_t offset;
} runtime_remote_json_t;

static void runtime_remote_json_space(runtime_remote_json_t* json) {
    if (!json) return;
    while (json->offset < json->length &&
           (json->data[json->offset] == ' ' ||
            json->data[json->offset] == '\n' ||
            json->data[json->offset] == '\r' ||
            json->data[json->offset] == '\t')) {
        json->offset++;
    }
}

static int runtime_remote_json_expect(runtime_remote_json_t* json,
                                       uint8_t value) {
    if (!json) return ERR_NULL;
    runtime_remote_json_space(json);
    if (json->offset >= json->length || json->data[json->offset] != value) {
        return ERR_INVALID;
    }
    json->offset++;
    return OK;
}

static int runtime_remote_json_string(runtime_remote_json_t* json,
                                      char* output, uint32_t capacity) {
    uint32_t length = 0U;

    if (!json || !output || capacity < 2U) return ERR_NULL;
    if (runtime_remote_json_expect(json, '"') != OK) return ERR_INVALID;
    while (json->offset < json->length) {
        uint8_t value = json->data[json->offset++];

        if (value == '"') {
            if (!length || length + 1U > capacity) return ERR_INVALID;
            output[length] = '\0';
            return OK;
        }
        if (value == '\\') {
            if (json->offset >= json->length) return ERR_INVALID;
            value = json->data[json->offset++];
            if (value != '"' && value != '\\' && value != '/' &&
                value != 'b' && value != 'f' && value != 'n' &&
                value != 'r' && value != 't') return ERR_INVALID;
        } else if (value < 0x20U) {
            return ERR_INVALID;
        }
        if (length + 1U >= capacity) return ERR_OVERFLOW;
        output[length++] = (char)value;
    }
    return ERR_INVALID;
}

static int runtime_remote_json_key(runtime_remote_json_t* json,
                                   const char* expected) {
    char key[32];

    if (!json || !expected || runtime_remote_json_string(
            json, key, sizeof(key)) != OK ||
        kstrcmp(key, expected) != 0) return ERR_INVALID;
    return runtime_remote_json_expect(json, ':');
}

static int runtime_remote_json_number(runtime_remote_json_t* json,
                                      uint32_t* output) {
    uint32_t value = 0U;
    uint8_t found = 0U;

    if (!json || !output) return ERR_NULL;
    runtime_remote_json_space(json);
    while (json->offset < json->length) {
        uint8_t digit = json->data[json->offset];
        uint32_t numeric;

        if (digit < '0' || digit > '9') break;
        numeric = (uint32_t)(digit - '0');
        if (value > (0xFFFFFFFFU - numeric) / 10U) return ERR_OVERFLOW;
        value = value * 10U + numeric;
        json->offset++;
        found = 1U;
    }
    if (!found) return ERR_INVALID;
    *output = value;
    return OK;
}

static int runtime_remote_json_hash(const char* text, uint8_t output[32]) {
    if (!text || !output || kstrlen(text) != 64U) return ERR_INVALID;
    for (uint32_t index = 0U; index < 32U; index++) {
        uint8_t high = (uint8_t)text[index * 2U];
        uint8_t low = (uint8_t)text[index * 2U + 1U];
        uint8_t high_value;
        uint8_t low_value;

        if (high >= '0' && high <= '9') high_value = high - '0';
        else if (high >= 'a' && high <= 'f') high_value = high - 'a' + 10U;
        else if (high >= 'A' && high <= 'F') high_value = high - 'A' + 10U;
        else return ERR_INVALID;
        if (low >= '0' && low <= '9') low_value = low - '0';
        else if (low >= 'a' && low <= 'f') low_value = low - 'a' + 10U;
        else if (low >= 'A' && low <= 'F') low_value = low - 'A' + 10U;
        else return ERR_INVALID;
        output[index] = (uint8_t)((high_value << 4U) | low_value);
    }
    return OK;
}

static int runtime_remote_json_asset(runtime_remote_json_t* json,
                                     runtime_remote_descriptor_asset_t* asset) {
    char digest[65];

    if (!json || !asset) return ERR_NULL;
    kmemset(asset, 0, sizeof(*asset));
    if (runtime_remote_json_expect(json, '{') != OK ||
        runtime_remote_json_key(json, "name") != OK ||
        runtime_remote_json_string(json, asset->name, sizeof(asset->name)) != OK ||
        runtime_remote_json_expect(json, ',') != OK ||
        runtime_remote_json_key(json, "size") != OK ||
        runtime_remote_json_number(json, &asset->size) != OK ||
        runtime_remote_json_expect(json, ',') != OK ||
        runtime_remote_json_key(json, "sha256") != OK ||
        runtime_remote_json_string(json, digest, sizeof(digest)) != OK ||
        runtime_remote_json_expect(json, '}') != OK ||
        runtime_remote_json_hash(digest, asset->hash) != OK ||
        !asset->size) return ERR_INVALID;
    return OK;
}

static int runtime_remote_descriptor_parse(
    const uint8_t* data, uint32_t length, const char* requested_tag,
    runtime_remote_source_t* source) {
    runtime_remote_json_t json;
    char format[48];
    char release_id[UPDATE_REMOTE_RELEASE_ID_SIZE];
    char release_name[UPDATE_REMOTE_RELEASE_NAME_SIZE];
    char channel[16];
    char tag[UPDATE_REMOTE_TAG_SIZE];
    uint8_t seen[UPDATE_RUNTIME_MAX_ENTRIES];

    if (!data || !length || !requested_tag || !source) return ERR_NULL;
    kmemset(seen, 0, sizeof(seen));
    json.data = data;
    json.length = length;
    json.offset = 0U;
    if (runtime_remote_json_expect(&json, '{') != OK ||
        runtime_remote_json_key(&json, "format") != OK ||
        runtime_remote_json_string(&json, format, sizeof(format)) != OK ||
        kstrcmp(format, "zephyros-runtime-release-v2") != 0 ||
        runtime_remote_json_expect(&json, ',') != OK ||
        runtime_remote_json_key(&json, "release_id") != OK ||
        runtime_remote_json_string(&json, release_id, sizeof(release_id)) != OK ||
        runtime_remote_json_expect(&json, ',') != OK ||
        runtime_remote_json_key(&json, "release_name") != OK ||
        runtime_remote_json_string(&json, release_name, sizeof(release_name)) != OK ||
        runtime_remote_json_expect(&json, ',') != OK ||
        runtime_remote_json_key(&json, "channel") != OK ||
        runtime_remote_json_string(&json, channel, sizeof(channel)) != OK ||
        kstrcmp(channel, "stable") != 0 ||
        runtime_remote_json_expect(&json, ',') != OK ||
        runtime_remote_json_key(&json, "tag") != OK ||
        runtime_remote_json_string(&json, tag, sizeof(tag)) != OK ||
        kstrcmp(tag, requested_tag) != 0 ||
        runtime_remote_json_expect(&json, ',') != OK ||
        runtime_remote_json_key(&json, "runtime") != OK ||
        runtime_remote_json_expect(&json, '{') != OK ||
        runtime_remote_json_key(&json, "zum2") != OK ||
        runtime_remote_json_asset(&json, &source->manifest_asset) != OK ||
        runtime_remote_json_expect(&json, ',') != OK ||
        runtime_remote_json_key(&json, "zephyrosupd") != OK ||
        runtime_remote_json_asset(&json, &source->package_asset) != OK ||
        runtime_remote_json_expect(&json, ',') != OK ||
        runtime_remote_json_key(&json, "assets") != OK ||
        runtime_remote_json_expect(&json, '[') != OK) return ERR_INVALID;
    while (1) {
        runtime_remote_descriptor_asset_t* asset;
        int catalog;

        runtime_remote_json_space(&json);
        if (json.offset < json.length && json.data[json.offset] == ']') {
            json.offset++;
            break;
        }
        if (source->asset_count >= UPDATE_RUNTIME_MAX_ENTRIES) return ERR_OVERFLOW;
        asset = &source->assets[source->asset_count];
        if (runtime_remote_json_asset(&json, asset) != OK) return ERR_INVALID;
        catalog = -1;
        for (uint32_t index = 0U; index < UPDATE_RUNTIME_MAX_ENTRIES; index++) {
            static const char* const catalog_names[3] = {
                "EXPLORER.BMP", "SHELL.BMP", "TASKMGR.BMP"
            };
            if (index < 3U && kstrcmp(asset->name, catalog_names[index]) == 0) {
                catalog = (int)index;
                break;
            }
        }
        if (catalog < 0 || seen[catalog]) return ERR_INVALID;
        seen[catalog] = 1U;
        source->asset_count++;
        runtime_remote_json_space(&json);
        if (json.offset >= json.length) return ERR_INVALID;
        if (json.data[json.offset] == ']') {
            json.offset++;
            break;
        }
        if (runtime_remote_json_expect(&json, ',') != OK) return ERR_INVALID;
    }
    if (runtime_remote_json_expect(&json, '}') != OK ||
        runtime_remote_json_expect(&json, '}') != OK) return ERR_INVALID;
    runtime_remote_json_space(&json);
    if (json.offset != json.length ||
        kstrcmp(source->manifest_asset.name, "runtime.zum2") != 0 ||
        source->manifest_asset.size != UPDATE_RUNTIME_MANIFEST_SIZE ||
        kstrcmp(source->package_asset.name, UPDATE_REMOTE_RUNTIME_PACKAGE_NAME) != 0 ||
        source->package_asset.size > UPDATE_RUNTIME_PACKAGE_MAX_SIZE ||
        !release_id[0] || !release_name[0] || source->asset_count > 3U) {
        return ERR_INVALID;
    }
    runtime_remote_copy_text(source->tag, sizeof(source->tag), tag);
    runtime_remote_copy_text(source->release_id, sizeof(source->release_id),
                             release_id);
    runtime_remote_copy_text(source->release_name, sizeof(source->release_name),
                             release_name);
    return OK;
}

static int runtime_remote_descriptor_sink(const uint8_t* data, uint32_t size,
                                          void* context) {
    (void)context;
    if (!data && size) return ERR_NULL;
    if (size > HTTP_BODY_CAPACITY - runtime_remote_descriptor_length) {
        return ERR_OVERFLOW;
    }
    if (size) kmemcpy(runtime_remote_descriptor + runtime_remote_descriptor_length,
                      data, size);
    runtime_remote_descriptor_length += size;
    return OK;
}

static int runtime_remote_fetch_descriptor(
    const char* url, const update_remote_options_t* options) {
    http_request_options_t http_options;
    int result;

    if (!url) return ERR_NULL;
    runtime_remote_http_options(options, &http_options);
    http_options.accept = "application/json";
    runtime_remote_progress_base = 0U;
    runtime_remote_descriptor_length = 0U;
    result = http_get_stream_start_ex(
        url, HTTP_BODY_CAPACITY, runtime_remote_descriptor_sink, 0,
        &http_options);
    if (result == OK) result = runtime_remote_wait_http(options);
    if (result != OK || runtime_remote_http.status_code != RUNTIME_REMOTE_HTTP_OK ||
        !runtime_remote_http.has_content_length ||
        runtime_remote_http.content_length != runtime_remote_descriptor_length ||
        !runtime_remote_descriptor_length) return ERR_INVALID;
    return OK;
}

static int runtime_remote_build_tag_url(const char* tag, char* output,
                                        uint32_t capacity) {
    const char* marker = "{tag}";
    uint32_t length = 0U;

    if (!tag || !output || capacity < 2U) return ERR_NULL;
    for (const char* cursor = UPDATE_REMOTE_RUNTIME_RELEASE_URL_TEMPLATE;
         *cursor;) {
        uint8_t matched = kstrlen(cursor) >= kstrlen(marker) &&
                          cursor[0] == marker[0] && cursor[1] == marker[1] &&
                          cursor[2] == marker[2] && cursor[3] == marker[3] &&
                          cursor[4] == marker[4];
        if (matched) {
            for (uint32_t index = 0U; tag[index]; index++) {
                if (length + 1U >= capacity) return ERR_OVERFLOW;
                output[length++] = tag[index];
            }
            cursor += 5U;
        } else {
            if (length + 1U >= capacity) return ERR_OVERFLOW;
            output[length++] = *cursor++;
        }
    }
    output[length] = '\0';
    return OK;
}

static int runtime_remote_source_for_tag(const char* tag,
                                         const update_remote_options_t* options,
                                         runtime_remote_source_t* source,
                                         update_remote_runtime_result_t* result_out) {
    update_remote_result_t github_result;
    char descriptor_url[UPDATE_REMOTE_RUNTIME_URL_SIZE];
    int result;

    kmemset(source, 0, sizeof(*source));
    runtime_remote_tag[0] = '\0';
    if (tag && tag[0]) {
        if (!runtime_remote_tag_valid(tag)) {
            result_out->reason = UPDATE_REMOTE_RUNTIME_REASON_RELEASE_TAG;
            return ERR_INVALID;
        }
        result = runtime_remote_build_tag_url(tag, descriptor_url,
                                              sizeof(descriptor_url));
        if (result == OK) {
            runtime_remote_copy_text(source->release_url,
                                     sizeof(source->release_url),
                                     descriptor_url);
            kmemset(&runtime_remote_http, 0, sizeof(runtime_remote_http));
            result = runtime_remote_fetch_descriptor(descriptor_url, options);
        }
        if (result == OK) {
            result = runtime_remote_descriptor_parse(
                runtime_remote_descriptor, runtime_remote_descriptor_length,
                tag, source);
            if (result != OK) {
                result_out->reason = UPDATE_REMOTE_RUNTIME_REASON_RELEASE_FORMAT;
                return ERR_INVALID;
            }
            source->github = 0U;
            source->descriptor_present = 1U;
            runtime_remote_copy_text(runtime_remote_tag,
                                     sizeof(runtime_remote_tag), tag);
            if (runtime_remote_build_relative_url(
                    descriptor_url, "runtime.zum2", source->manifest_url) != OK ||
                runtime_remote_build_relative_url(
                    descriptor_url, UPDATE_REMOTE_RUNTIME_PACKAGE_NAME,
                    source->package_url) != OK) {
                result_out->reason = UPDATE_REMOTE_RUNTIME_REASON_RELEASE_FORMAT;
                return ERR_OVERFLOW;
            }
            return OK;
        }
        /* O endpoint HTTP pode estar indisponivel; a mesma tag ainda pode
         * ser resolvida pela API HTTPS do GitHub. */
        source->release_url[0] = '\0';
        source->descriptor_present = 0U;
        kmemset(&github_result, 0, sizeof(github_result));
        result = update_remote_github_runtime_query(
            tag, options, &source->release, &github_result);
        if (result != OK) {
            result_out->http_status = github_result.http_status;
            result_out->tls_reason = github_result.tls_reason;
            result_out->tls_error = github_result.tls_error;
            result_out->reason = github_result.tls_reason ==
                TLS_REASON_TIME_UNAVAILABLE ?
                UPDATE_REMOTE_RUNTIME_REASON_TIME :
                github_result.reason ==
                UPDATE_REMOTE_REASON_RELEASE_NOT_FOUND ?
                UPDATE_REMOTE_RUNTIME_REASON_RELEASE_NOT_FOUND :
                github_result.reason == UPDATE_REMOTE_REASON_RELEASE_TAG ?
                UPDATE_REMOTE_RUNTIME_REASON_RELEASE_TAG :
                github_result.reason == UPDATE_REMOTE_REASON_RELEASE_ASSET ?
                UPDATE_REMOTE_RUNTIME_REASON_RELEASE_ASSET :
                github_result.reason == UPDATE_REMOTE_REASON_TLS ?
                UPDATE_REMOTE_RUNTIME_REASON_TLS :
                github_result.reason == UPDATE_REMOTE_REASON_TIMEOUT ?
                UPDATE_REMOTE_RUNTIME_REASON_TIMEOUT :
                UPDATE_REMOTE_RUNTIME_REASON_RELEASE_FORMAT;
            return result;
        }
        source->github = 1U;
        if (!source->release.runtime_manifest.url[0] ||
            !source->release.runtime_package.url[0]) {
            result_out->reason = UPDATE_REMOTE_RUNTIME_REASON_RELEASE_ASSET;
            return ERR_NOT_FOUND;
        }
        runtime_remote_copy_text(source->manifest_url,
                                 sizeof(source->manifest_url),
                                 source->release.runtime_manifest.url);
        runtime_remote_copy_text(source->package_url,
                                 sizeof(source->package_url),
                                 source->release.runtime_package.url);
        runtime_remote_copy_text(runtime_remote_tag,
                                 sizeof(runtime_remote_tag), tag);
        return OK;
    }
    runtime_remote_copy_text(source->manifest_url,
                             sizeof(source->manifest_url),
                             UPDATE_REMOTE_RUNTIME_MANIFEST_URL);
    return runtime_remote_build_relative_url(
        source->manifest_url, UPDATE_REMOTE_RUNTIME_PACKAGE_NAME,
        source->package_url);
}

static int runtime_remote_descriptor_matches_manifest(void) {
    uint8_t hash[32];
    uint16_t expected_assets = 0U;

    if (runtime_remote_source.descriptor_present &&
        (!runtime_remote_source.release_id[0] ||
         kstrcmp(runtime_remote_source.release_id,
                 runtime_remote_status.manifest.release_id) != 0)) {
        return ERR_INVALID;
    }

    if (runtime_remote_source.github) {
        const update_remote_github_asset_t* asset;

        /* O ID numerico da API GitHub e metadado externo. A tag exata ja foi
         * validada pela API e pelo campo release_tag assinado do ZUM2. */
        if (runtime_remote_source.release.runtime_manifest.size !=
                UPDATE_RUNTIME_MANIFEST_SIZE ||
            crypto_sha256(runtime_remote_manifest_raw,
                          UPDATE_RUNTIME_MANIFEST_SIZE, hash) != OK ||
            (runtime_remote_source.release.runtime_manifest.digest_present &&
             !crypto_equal(hash,
                 runtime_remote_source.release.runtime_manifest.digest, 32U)) ||
            runtime_remote_source.release.runtime_package.size !=
                runtime_remote_status.manifest.package_size ||
            (runtime_remote_source.release.runtime_package.digest_present &&
             !crypto_equal(runtime_remote_status.manifest.package_hash,
                 runtime_remote_source.release.runtime_package.digest, 32U))) {
            return ERR_INVALID;
        }
        for (uint32_t index = 0U;
             index < runtime_remote_status.manifest.entry_count; index++) {
            update_runtime_entry_t* entry =
                &runtime_remote_status.manifest.entries[index];
            if (!entry->target_present) continue;
            expected_assets++;
            asset = runtime_remote_find_github_asset(entry->asset_name);
            if (!asset || asset->size != entry->asset_size ||
                (asset->digest_present &&
                 !crypto_equal(asset->digest, entry->asset_hash, 32U))) {
                return ERR_INVALID;
            }
        }
        return runtime_remote_source.release.runtime_asset_count ==
               expected_assets ? OK : ERR_INVALID;
    }
    if (!runtime_remote_source.descriptor_present) return OK;
    if (runtime_remote_source.manifest_asset.size !=
            UPDATE_RUNTIME_MANIFEST_SIZE ||
        crypto_sha256(runtime_remote_manifest_raw,
                      UPDATE_RUNTIME_MANIFEST_SIZE, hash) != OK ||
        !crypto_equal(hash, runtime_remote_source.manifest_asset.hash, 32U) ||
        runtime_remote_source.package_asset.size !=
            runtime_remote_status.manifest.package_size ||
        !crypto_equal(runtime_remote_source.package_asset.hash,
                      runtime_remote_status.manifest.package_hash, 32U)) {
        return ERR_INVALID;
    }
    for (uint32_t index = 0U;
         index < runtime_remote_status.manifest.entry_count; index++) {
        update_runtime_entry_t* entry =
            &runtime_remote_status.manifest.entries[index];
        if (!entry->target_present) continue;
        expected_assets++;
        uint8_t found = 0U;
        for (uint32_t asset = 0U;
             asset < runtime_remote_source.asset_count; asset++) {
            runtime_remote_descriptor_asset_t* descriptor =
                &runtime_remote_source.assets[asset];
            if (kstrcmp(descriptor->name, entry->asset_name) == 0 &&
                descriptor->size == entry->asset_size &&
                crypto_equal(descriptor->hash, entry->asset_hash, 32U)) {
                found = 1U;
                break;
            }
        }
        if (!found) return ERR_INVALID;
    }
    return runtime_remote_source.asset_count == expected_assets ? OK : ERR_INVALID;
}

static int runtime_remote_manifest_check(const update_remote_options_t* options,
                                         update_remote_runtime_result_t* output) {
    update_runtime_reason_t reason;
    update_version_t installed;
    uint32_t installed_epoch;
    uint8_t supported = 0U;
    int result;

    (void)options;
    result = update_runtime_parse_manifest(runtime_remote_manifest_raw,
                                           UPDATE_RUNTIME_MANIFEST_SIZE,
                                           &runtime_remote_status.manifest,
                                           &reason);
    output->verification.reason = reason;
    if (result != OK) {
        output->reason = reason == UPDATE_RUNTIME_REASON_SIGNATURE ?
            UPDATE_REMOTE_RUNTIME_REASON_MANIFEST_SIGNATURE :
            reason == UPDATE_RUNTIME_REASON_UNKNOWN_KEY ?
            UPDATE_REMOTE_RUNTIME_REASON_UNKNOWN_KEY :
            UPDATE_REMOTE_RUNTIME_REASON_MANIFEST_FORMAT;
        return result;
    }
    if (runtime_remote_tag[0] &&
        kstrcmp(runtime_remote_status.manifest.release_tag,
                runtime_remote_tag) != 0) {
        output->reason = UPDATE_REMOTE_RUNTIME_REASON_RELEASE_TAG;
        return ERR_INVALID;
    }
    if (runtime_remote_descriptor_matches_manifest() != OK) {
        output->reason = UPDATE_REMOTE_RUNTIME_REASON_RELEASE_ASSET;
        return ERR_INVALID;
    }
    if (update_runtime_get_installed_version(&installed, &installed_epoch) != OK) {
        output->reason = UPDATE_REMOTE_RUNTIME_REASON_VERSION;
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < runtime_remote_status.manifest.base_count;
         index++) {
        update_version_t* base = &runtime_remote_status.manifest.base_versions[index];
        if (base->major == installed.major && base->minor == installed.minor &&
            base->patch == installed.patch &&
            runtime_remote_status.manifest.base_epochs[index] == installed_epoch) {
            supported = 1U;
            break;
        }
    }
    if (!supported || runtime_remote_status.manifest.target_epoch < installed_epoch ||
        (runtime_remote_status.manifest.target_epoch == installed_epoch &&
         (runtime_remote_status.manifest.target_version.major < installed.major ||
          (runtime_remote_status.manifest.target_version.major == installed.major &&
           runtime_remote_status.manifest.target_version.minor < installed.minor) ||
          (runtime_remote_status.manifest.target_version.major == installed.major &&
           runtime_remote_status.manifest.target_version.minor == installed.minor &&
           runtime_remote_status.manifest.target_version.patch <= installed.patch)))) {
        output->reason = UPDATE_REMOTE_RUNTIME_REASON_VERSION;
        return ERR_INVALID;
    }
    if (crypto_sha256(runtime_remote_manifest_raw,
                      UPDATE_RUNTIME_MANIFEST_SIZE,
                      runtime_remote_manifest_hash) != OK) return ERR_INVALID;
    runtime_remote_manifest_valid = 1U;
    runtime_remote_status.manifest_cached = 1U;
    runtime_remote_status.entry_count = runtime_remote_status.manifest.entry_count;
    runtime_remote_status.reused_entries = 0U;
    runtime_remote_status.missing_assets = 0U;
    for (uint32_t index = 0U; index < runtime_remote_status.manifest.entry_count;
         index++) {
        update_runtime_entry_t* entry = &runtime_remote_status.manifest.entries[index];
        uint8_t matches = 0U;
        if (entry->target_present && update_runtime_file_matches(
                entry->path, entry->target_size, entry->target_hash,
                &matches) == OK && matches) {
            runtime_remote_status.reused_entries++;
        } else if (entry->target_present) {
            runtime_remote_status.missing_assets++;
        }
    }
    output->manifest = runtime_remote_status.manifest;
    output->verification.manifest_valid = 1U;
    output->verification.compatible = 1U;
    output->verification.target_version = runtime_remote_status.manifest.target_version;
    output->verification.target_epoch = runtime_remote_status.manifest.target_epoch;
    output->verification.entry_count = runtime_remote_status.manifest.entry_count;
    return OK;
}

static int runtime_remote_manifest_commit_to_slot(uint8_t slot) {
    int result = fs_atomic_write_root(
        runtime_remote_manifest_aliases[slot], runtime_remote_manifest_raw,
        UPDATE_RUNTIME_MANIFEST_SIZE, RUNTIME_REMOTE_ATTRIBUTES,
        FS_ATOMIC_CREATE_OR_REPLACE);
    return result;
}

static int runtime_remote_stream_sink(const uint8_t* data, uint32_t size,
                                      void* context) {
    runtime_remote_download_t* download =
        (runtime_remote_download_t*)context;
    int result;

    if (!download || (!data && size)) return ERR_NULL;
    result = fs_stream_write_root(data, size);
    if (result == OK) result = crypto_sha256_update(&download->sha, data, size);
    if (result != OK) download->write_error = result;
    return result;
}

static int runtime_remote_download(const char* url, const char* alias,
                                   uint32_t size,
                                   const update_remote_options_t* options,
                                   uint8_t hash_out[32]) {
    http_request_options_t http_options;
    int result;

    kmemset(&runtime_remote_transfer, 0, sizeof(runtime_remote_transfer));
    runtime_remote_progress_base = runtime_remote_status.bytes_received;
    result = crypto_sha256_init(&runtime_remote_transfer.sha);
    if (result == OK) result = fs_stream_begin_root(alias, size,
                                                     RUNTIME_REMOTE_ATTRIBUTES);
    if (result == OK) {
        runtime_remote_http_options(options, &http_options);
        result = http_get_stream_start_ex(
            url, size, runtime_remote_stream_sink, &runtime_remote_transfer,
            &http_options);
    }
    if (result == OK) result = runtime_remote_wait_http(options);
    if (result == OK && (runtime_remote_http.status_code != RUNTIME_REMOTE_HTTP_OK ||
                         !runtime_remote_http.has_content_length ||
                         runtime_remote_http.content_length != size ||
                         runtime_remote_http.body_length != size)) {
        result = ERR_INVALID;
    }
    if (result == OK) result = fs_stream_finish_root();
    if (result == OK) result = crypto_sha256_final(&runtime_remote_transfer.sha,
                                                   hash_out);
    if (result != OK) {
        fs_stream_abort_root();
    } else {
        runtime_remote_progress_base = runtime_remote_status.bytes_received;
    }
    return result;
}

static void runtime_remote_prepare_progress(
    update_remote_runtime_fetch_mode_t mode) {
    uint32_t total = UPDATE_RUNTIME_MANIFEST_SIZE;

    runtime_remote_status.bytes_received = UPDATE_RUNTIME_MANIFEST_SIZE;
    if (mode == UPDATE_REMOTE_RUNTIME_FETCH_FULL) {
        total += runtime_remote_status.manifest.package_size;
    } else {
        for (uint32_t index = 0U;
             index < runtime_remote_status.manifest.entry_count; index++) {
            update_runtime_entry_t* entry =
                &runtime_remote_status.manifest.entries[index];
            uint8_t matches = 0U;

            if (!entry->target_present ||
                (update_runtime_file_matches(entry->path,
                                              entry->target_size,
                                              entry->target_hash,
                                              &matches) == OK && matches)) {
                continue;
            }
            total += entry->asset_size;
        }
    }
    runtime_remote_status.total_bytes = total;
    runtime_remote_progress_base = UPDATE_RUNTIME_MANIFEST_SIZE;
}

static int runtime_remote_validate_active(void) {
    uint8_t hash[32];
    uint32_t manifest_size = 0U;
    uint32_t bytes;
    update_runtime_reason_t reason;
    uint8_t ready = 1U;
    int result;

    runtime_remote_status.manifest_cached = 0U;
    runtime_remote_status.selective_ready = 0U;
    runtime_remote_status.package_cached = 0U;
    runtime_remote_status.bytes_received = 0U;
    runtime_remote_status.total_bytes = 0U;
    runtime_remote_status.entry_count = 0U;
    runtime_remote_status.reused_entries = 0U;
    runtime_remote_status.missing_assets = 0U;
    kmemset(&runtime_remote_status.manifest,
            0, sizeof(runtime_remote_status.manifest));
    runtime_remote_status.cached_manifest_alias[0] = '\0';
    runtime_remote_status.cached_package_alias[0] = '\0';
    if (runtime_remote_record.active_slot > 1U) return OK;
    result = fs_get_root_file_info(
        runtime_remote_manifest_aliases[runtime_remote_record.active_slot],
        &manifest_size, 0);
    if (result != OK || manifest_size != UPDATE_RUNTIME_MANIFEST_SIZE) {
        LOG_ERROR("UPDATE", "Tamanho do manifesto ativo runtime v2 invalido");
        return ERR_INVALID;
    }
    result = fs_read_file_range_at(
        runtime_remote_manifest_aliases[runtime_remote_record.active_slot], 0U,
        runtime_remote_manifest_raw, UPDATE_RUNTIME_MANIFEST_SIZE, &bytes);
    if (result != OK || bytes != UPDATE_RUNTIME_MANIFEST_SIZE ||
        update_runtime_parse_manifest(runtime_remote_manifest_raw, bytes,
                                       &runtime_remote_status.manifest,
                                       &reason) != OK ||
        crypto_sha256(runtime_remote_manifest_raw, bytes, hash) != OK ||
        !crypto_equal(hash, runtime_remote_record.manifest_hash, 32U)) {
        LOG_ERROR("UPDATE", "Manifesto ativo do runtime v2 e invalido");
        return ERR_INVALID;
    }
    if (runtime_remote_record.entry_count !=
            runtime_remote_status.manifest.entry_count ||
        (runtime_remote_record.mode == UPDATE_REMOTE_RUNTIME_FETCH_FULL &&
         runtime_remote_record.package_size !=
             runtime_remote_status.manifest.package_size) ||
        (runtime_remote_record.mode == UPDATE_REMOTE_RUNTIME_FETCH_SELECTIVE &&
         runtime_remote_record.package_size != 0U)) {
        LOG_ERROR("UPDATE", "Registro do cache runtime v2 diverge do manifesto");
        return ERR_INVALID;
    }
    runtime_remote_manifest_valid = 1U;
    runtime_remote_status.manifest_cached = 1U;
    runtime_remote_status.entry_count = runtime_remote_status.manifest.entry_count;
    runtime_remote_status.active_slot = runtime_remote_record.active_slot;
    runtime_remote_copy_text(
        runtime_remote_status.cached_manifest_alias,
        sizeof(runtime_remote_status.cached_manifest_alias),
        runtime_remote_manifest_aliases[runtime_remote_record.active_slot]);
    if (runtime_remote_record.mode == UPDATE_REMOTE_RUNTIME_FETCH_FULL) {
        uint32_t size = 0U;
        result = fs_get_root_file_info(
            runtime_remote_package_aliases[runtime_remote_record.active_slot],
            &size, 0);
        if (result != OK || size != runtime_remote_record.package_size ||
            runtime_remote_hash_file(
                runtime_remote_package_aliases[runtime_remote_record.active_slot],
                size, hash) != OK ||
            !crypto_equal(hash, runtime_remote_record.package_hash, 32U)) {
            return ERR_INVALID;
        }
        runtime_remote_status.package_cached = 1U;
        runtime_remote_copy_text(
            runtime_remote_status.cached_package_alias,
            sizeof(runtime_remote_status.cached_package_alias),
            runtime_remote_package_aliases[runtime_remote_record.active_slot]);
        runtime_remote_status.selective_ready = 0U;
        return OK;
    }
    for (uint32_t index = 0U; index < runtime_remote_status.manifest.entry_count;
         index++) {
        update_runtime_entry_t* entry = &runtime_remote_status.manifest.entries[index];
        uint8_t matches = 0U;
        char alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];

        if (!entry->target_present ||
            (update_runtime_file_matches(entry->path, entry->target_size,
                                          entry->target_hash, &matches) == OK &&
             matches)) continue;
        if (!(runtime_remote_record.asset_mask & (uint16_t)(1U << index))) {
            ready = 0U;
            continue;
        }
        runtime_remote_asset_alias(alias, runtime_remote_record.active_slot,
                                   (uint8_t)index);
        {
            uint32_t asset_size = 0U;
            if (fs_get_root_file_info(alias, &asset_size, 0) != OK ||
                asset_size != entry->asset_size ||
                runtime_remote_hash_file(alias, entry->asset_size, hash) != OK ||
                !crypto_equal(hash, entry->asset_hash, 32U)) ready = 0U;
        }
    }
    runtime_remote_status.selective_ready = ready;
    return ready ? OK : ERR_INVALID;
}

static int runtime_remote_prepare_pending(uint8_t slot) {
    if (runtime_remote_delete_slot(slot) != OK) return ERR_DISK;
    runtime_remote_record.phase = RUNTIME_REMOTE_PHASE_DOWNLOADING;
    runtime_remote_record.pending_slot = slot;
    return runtime_remote_write_record();
}

static int runtime_remote_abort_pending(update_remote_runtime_reason_t reason,
                                        int error,
                                        update_remote_runtime_result_t* output) {
    uint8_t pending = runtime_remote_record.pending_slot;
    int delete_result = OK;

    if (pending <= 1U) delete_result = runtime_remote_delete_slot(pending);
    if (delete_result != OK) {
        runtime_remote_status.pending_recovery = 1U;
        runtime_remote_status.reason = UPDATE_REMOTE_RUNTIME_REASON_IO;
        runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_FAILED;
        runtime_remote_status.busy = 0U;
        if (output) {
            output->reason = UPDATE_REMOTE_RUNTIME_REASON_IO;
            output->cache_preserved =
                runtime_remote_record.active_slot <= 1U ? 1U : 0U;
        }
        return ERR_DISK;
    }
    if (runtime_remote_previous_record_valid) {
        runtime_remote_record = runtime_remote_previous_record;
    }
    runtime_remote_record.phase = RUNTIME_REMOTE_PHASE_CLEAN;
    runtime_remote_record.pending_slot = RUNTIME_REMOTE_SLOT_NONE;
    if (runtime_remote_write_record() != OK) {
        runtime_remote_status.pending_recovery = 1U;
        runtime_remote_status.reason = UPDATE_REMOTE_RUNTIME_REASON_IO;
        runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_FAILED;
        runtime_remote_status.busy = 0U;
        if (output) {
            output->reason = UPDATE_REMOTE_RUNTIME_REASON_IO;
            output->cache_preserved =
                runtime_remote_record.active_slot <= 1U ? 1U : 0U;
        }
        return ERR_DISK;
    }
    runtime_remote_status.pending_recovery = 0U;
    runtime_remote_previous_record_valid = 0U;
    runtime_remote_status.active_slot = runtime_remote_record.active_slot;
    (void)runtime_remote_validate_active();
    runtime_remote_status.reason = reason;
    runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_FAILED;
    runtime_remote_status.busy = 0U;
    if (output) {
        output->reason = reason;
        output->cache_preserved =
            runtime_remote_record.active_slot <= 1U ? 1U : 0U;
    }
    LOG_ERROR("UPDATE", "Download runtime v2 recusado");
    return error;
}

static const update_remote_github_asset_t* runtime_remote_find_github_asset(
    const char* name) {
    if (!name) return 0;
    for (uint32_t index = 0U;
         index < runtime_remote_source.release.runtime_asset_count; index++) {
        if (kstrcmp(runtime_remote_source.release.runtime_assets[index].name,
                    name) == 0) {
            return &runtime_remote_source.release.runtime_assets[index];
        }
    }
    return 0;
}

static int runtime_remote_asset_url(const update_runtime_entry_t* entry,
                                    char* output, uint32_t capacity,
                                    uint32_t* size_out) {
    const update_remote_github_asset_t* github_asset;

    if (!entry || !output || !capacity || !size_out) return ERR_NULL;
    if (runtime_remote_source.github) {
        github_asset = runtime_remote_find_github_asset(entry->asset_name);
        if (!github_asset) return ERR_NOT_FOUND;
        runtime_remote_copy_text(output, capacity, github_asset->url);
        *size_out = github_asset->size;
        return github_asset->size == entry->asset_size ? OK : ERR_INVALID;
    }
    if (runtime_remote_build_relative_url(runtime_remote_source.manifest_url,
                                          entry->asset_name, output) != OK) {
        return ERR_OVERFLOW;
    }
    *size_out = entry->asset_size;
    return OK;
}

static void runtime_remote_copy_result(update_remote_runtime_result_t* output) {
    if (!output) return;
    output->reason = runtime_remote_status.reason;
    output->http_status = runtime_remote_status.http_status;
    output->bytes_received = runtime_remote_status.bytes_received;
    output->manifest = runtime_remote_status.manifest;
    runtime_remote_copy_text(
        output->cached_manifest_alias,
        sizeof(output->cached_manifest_alias),
        runtime_remote_status.cached_manifest_alias);
    runtime_remote_copy_text(
        output->cached_package_alias,
        sizeof(output->cached_package_alias),
        runtime_remote_status.cached_package_alias);
    output->tls_reason = runtime_remote_http.tls_reason;
    output->tls_error = runtime_remote_http.tls_error;
}

int update_remote_runtime_init(void) {
    LOG_INFO("UPDATE", "Inicializando transporte runtime v2");
    kmemset(&runtime_remote_status, 0, sizeof(runtime_remote_status));
    kmemset(&runtime_remote_record, 0, sizeof(runtime_remote_record));
    kmemset(&runtime_remote_source, 0, sizeof(runtime_remote_source));
    kmemset(&runtime_remote_recovery_record, 0,
            sizeof(runtime_remote_recovery_record));
    runtime_remote_record.active_slot = RUNTIME_REMOTE_SLOT_NONE;
    runtime_remote_record.pending_slot = RUNTIME_REMOTE_SLOT_NONE;
    runtime_remote_status.active_slot = RUNTIME_REMOTE_SLOT_NONE;
    runtime_remote_record_slot = -1;
    runtime_remote_manifest_valid = 0U;
    runtime_remote_progress_base = 0U;
    runtime_remote_previous_record_valid = 0U;
    runtime_remote_recovery_record_valid = 0U;
    if (!update_runtime_is_ready()) {
        LOG_ERROR("UPDATE", "Transporte runtime v2 requer runtime local pronto");
        return ERR_STATE;
    }
    runtime_remote_status.initialized = 1U;
    runtime_remote_status.enabled = 0U;
    runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_DISABLED;
    runtime_remote_status.reason = UPDATE_REMOTE_RUNTIME_REASON_DISABLED;
    runtime_remote_copy_text(runtime_remote_status.manifest_url,
                             sizeof(runtime_remote_status.manifest_url),
                             UPDATE_REMOTE_RUNTIME_MANIFEST_URL);
    if (fs_get_type() == FS_TYPE_FAT12) {
        if (runtime_remote_load_records() != OK) {
            runtime_remote_status.reason = UPDATE_REMOTE_RUNTIME_REASON_CACHE;
            runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_FAILED;
            LOG_ERROR("UPDATE", "Registros do cache runtime v2 invalidos");
            return ERR_STATE;
        }
        if (runtime_remote_record.pending_slot <= 1U) {
            if (runtime_remote_record.pending_slot !=
                runtime_remote_record.active_slot &&
                runtime_remote_delete_slot(runtime_remote_record.pending_slot) != OK) {
                runtime_remote_status.pending_recovery = 1U;
                runtime_remote_status.reason = UPDATE_REMOTE_RUNTIME_REASON_IO;
                runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_FAILED;
                LOG_ERROR("UPDATE", "Nao foi possivel limpar slot runtime v2 pendente");
                return ERR_DISK;
            }
            if (runtime_remote_record.pending_slot !=
                runtime_remote_record.active_slot) {
                if (runtime_remote_recovery_record_valid &&
                    runtime_remote_recovery_record.phase ==
                        RUNTIME_REMOTE_PHASE_CLEAN) {
                    runtime_remote_record = runtime_remote_recovery_record;
                } else {
                    kmemset(&runtime_remote_record, 0,
                            sizeof(runtime_remote_record));
                    runtime_remote_record.active_slot = RUNTIME_REMOTE_SLOT_NONE;
                }
            }
            runtime_remote_record.phase = RUNTIME_REMOTE_PHASE_CLEAN;
            runtime_remote_record.pending_slot = RUNTIME_REMOTE_SLOT_NONE;
            runtime_remote_status.pending_recovery = 1U;
            if (runtime_remote_write_record() != OK) {
                runtime_remote_status.reason = UPDATE_REMOTE_RUNTIME_REASON_IO;
                runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_FAILED;
                return ERR_DISK;
            }
            runtime_remote_status.pending_recovery = 0U;
            runtime_remote_recovery_record_valid = 0U;
        }
        runtime_remote_status.active_slot = runtime_remote_record.active_slot;
        if (runtime_remote_validate_active() != OK &&
            runtime_remote_record.active_slot <= 1U) {
            runtime_remote_status.reason = UPDATE_REMOTE_RUNTIME_REASON_CACHE;
            runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_FAILED;
            LOG_WARN("UPDATE", "Cache ativo runtime v2 esta degradado");
        }
    }
    LOG_INFO("UPDATE", "Transporte runtime v2 inicializado e desabilitado");
    return OK;
}

int update_remote_runtime_enable(void) {
    if (!runtime_remote_status.initialized) {
        LOG_ERROR("UPDATE", "Habilitacao runtime remoto antes da inicializacao");
        return ERR_STATE;
    }
    if (runtime_remote_status.pending_recovery) {
        LOG_ERROR("UPDATE", "Habilitacao runtime remoto bloqueada por recovery");
        return ERR_STATE;
    }
    runtime_remote_status.enabled = 1U;
    runtime_remote_status.reason = UPDATE_REMOTE_RUNTIME_REASON_NONE;
    runtime_remote_status.state = runtime_remote_network_ready() ?
        UPDATE_REMOTE_RUNTIME_STATE_READY : UPDATE_REMOTE_RUNTIME_STATE_UNAVAILABLE;
    runtime_remote_status.network_ready = runtime_remote_network_ready() ? 1U : 0U;
    LOG_INFO("UPDATE", "Transporte runtime v2 habilitado nesta sessao");
    return OK;
}

int update_remote_runtime_disable(void) {
    if (!runtime_remote_status.initialized) {
        LOG_ERROR("UPDATE", "Desabilitacao runtime remoto antes da inicializacao");
        return ERR_STATE;
    }
    if (runtime_remote_status.busy) {
        LOG_ERROR("UPDATE", "Desabilitacao runtime remoto durante operacao ativa");
        return ERR_STATE;
    }
    runtime_remote_status.enabled = 0U;
    runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_DISABLED;
    runtime_remote_status.reason = UPDATE_REMOTE_RUNTIME_REASON_DISABLED;
    http_reset();
    return OK;
}

int update_remote_runtime_check(
    const char* tag, const update_remote_options_t* options,
    update_remote_runtime_result_t* result_out) {
    update_remote_options_t defaults = {1U, 0, 0, 0, 0, 0, 0, 0};
    int result;

    if (!result_out) {
        LOG_ERROR("UPDATE", "Destino nulo no check runtime remoto");
        return ERR_NULL;
    }
    if (!options) options = &defaults;
    kmemset(result_out, 0, sizeof(*result_out));
    if (runtime_remote_status.pending_recovery) {
        result_out->reason = UPDATE_REMOTE_RUNTIME_REASON_IO;
        LOG_ERROR("UPDATE", "Check runtime remoto bloqueado por recovery pendente");
        return ERR_STATE;
    }
    if (!runtime_remote_status.initialized || !runtime_remote_status.enabled) {
        result_out->reason = UPDATE_REMOTE_RUNTIME_REASON_DISABLED;
        return ERR_UNAVAILABLE;
    }
    runtime_remote_status.network_ready = runtime_remote_network_ready() ? 1U : 0U;
    if (!runtime_remote_status.network_ready) {
        runtime_remote_status.reason = UPDATE_REMOTE_RUNTIME_REASON_NETWORK;
        runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_UNAVAILABLE;
        result_out->reason = runtime_remote_status.reason;
        return ERR_UNAVAILABLE;
    }
    if (runtime_remote_status.busy) {
        LOG_ERROR("UPDATE", "Check runtime remoto recusado durante operacao ativa");
        return ERR_STATE;
    }
    runtime_remote_status.busy = 1U;
    runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_CHECKING;
    result = runtime_remote_source_for_tag(tag, options, &runtime_remote_source,
                                            result_out);
    if (result == OK) {
        runtime_remote_copy_text(runtime_remote_status.manifest_url,
                                 sizeof(runtime_remote_status.manifest_url),
                                 runtime_remote_source.manifest_url);
        runtime_remote_copy_text(runtime_remote_status.release_url,
                                 sizeof(runtime_remote_status.release_url),
                                 runtime_remote_source.release_url);
        kmemset(&runtime_remote_http, 0, sizeof(runtime_remote_http));
        result = runtime_remote_fetch_manifest(runtime_remote_source.manifest_url,
                                               options);
    }
    if (result == OK) result = runtime_remote_manifest_check(options, result_out);
    if (result != OK) {
        if (!result_out->reason) result_out->reason =
            runtime_remote_cancelled ? UPDATE_REMOTE_RUNTIME_REASON_CANCELLED :
            runtime_remote_http_reason();
        runtime_remote_status.reason = result_out->reason;
        runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_FAILED;
        runtime_remote_status.busy = 0U;
        runtime_remote_copy_result(result_out);
        return result;
    }
    runtime_remote_status.reason = UPDATE_REMOTE_RUNTIME_REASON_NONE;
    runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_AVAILABLE;
    runtime_remote_status.busy = 0U;
    runtime_remote_copy_result(result_out);
    return OK;
}

int update_remote_runtime_fetch(
    const char* tag, update_remote_runtime_fetch_mode_t mode,
    const update_remote_options_t* options,
    update_remote_runtime_result_t* result_out) {
    update_remote_options_t defaults = {0U, 0, 0, 0, 0, 0, 0, 0};
    uint8_t expected_manifest_hash[32];
    uint8_t pending_slot;
    uint8_t previous_active;
    uint8_t hash[32];
    update_runtime_verification_t verification;
    int result;

    if (!result_out) {
        LOG_ERROR("UPDATE", "Destino nulo no fetch runtime remoto");
        return ERR_NULL;
    }
    if (!options) options = &defaults;
    kmemset(result_out, 0, sizeof(*result_out));
    if (options->dry_run) return update_remote_runtime_check(tag, options,
                                                             result_out);
    if (mode != UPDATE_REMOTE_RUNTIME_FETCH_SELECTIVE &&
        mode != UPDATE_REMOTE_RUNTIME_FETCH_FULL) {
        LOG_ERROR("UPDATE", "Modo de fetch runtime remoto invalido");
        return ERR_INVALID;
    }
    result = update_remote_runtime_check(tag, options, result_out);
    if (result != OK) return result;
    kmemcpy(expected_manifest_hash, runtime_remote_manifest_hash, 32U);
    if (fs_get_type() != FS_TYPE_FAT12) {
        result_out->reason = UPDATE_REMOTE_RUNTIME_REASON_CACHE;
        runtime_remote_status.reason = UPDATE_REMOTE_RUNTIME_REASON_CACHE;
        runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_UNAVAILABLE;
        runtime_remote_status.busy = 0U;
        LOG_ERROR("UPDATE", "Cache runtime remoto requer FAT12");
        return ERR_UNAVAILABLE;
    }
    runtime_remote_status.busy = 1U;
    runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_DOWNLOADING;
    runtime_remote_prepare_progress(mode);
    previous_active = runtime_remote_record.active_slot;
    runtime_remote_previous_record = runtime_remote_record;
    runtime_remote_previous_record_valid = 1U;
    pending_slot = runtime_remote_record.active_slot == 0U ? 1U : 0U;
    if (runtime_remote_prepare_pending(pending_slot) != OK) {
        return runtime_remote_abort_pending(
            UPDATE_REMOTE_RUNTIME_REASON_IO, ERR_DISK, result_out);
    }
    if (!crypto_equal(expected_manifest_hash, runtime_remote_manifest_hash, 32U)) {
        return runtime_remote_abort_pending(
            UPDATE_REMOTE_RUNTIME_REASON_MANIFEST_FORMAT, ERR_STATE,
            result_out);
    }
    if (runtime_remote_manifest_commit_to_slot(pending_slot) != OK) {
        return runtime_remote_abort_pending(
            UPDATE_REMOTE_RUNTIME_REASON_IO, ERR_DISK, result_out);
    }
    runtime_remote_record.mode = (uint8_t)mode;
    runtime_remote_record.entry_count = runtime_remote_status.manifest.entry_count;
    runtime_remote_record.asset_mask = 0U;
    runtime_remote_record.package_size = runtime_remote_status.manifest.package_size;
    kmemcpy(runtime_remote_record.manifest_hash, runtime_remote_manifest_hash, 32U);
    runtime_remote_record.target_version = runtime_remote_status.manifest.target_version;
    runtime_remote_record.target_epoch = runtime_remote_status.manifest.target_epoch;
    if (mode == UPDATE_REMOTE_RUNTIME_FETCH_FULL) {
        if (runtime_remote_download(
                runtime_remote_source.package_url,
                runtime_remote_package_aliases[pending_slot],
                runtime_remote_status.manifest.package_size, options, hash) != OK ||
            !crypto_equal(hash, runtime_remote_status.manifest.package_hash, 32U)) {
            return runtime_remote_abort_pending(
                runtime_remote_cancelled ? UPDATE_REMOTE_RUNTIME_REASON_CANCELLED :
                UPDATE_REMOTE_RUNTIME_REASON_PACKAGE_HASH,
                runtime_remote_cancelled ? ERR_TIMEOUT : ERR_INVALID,
                result_out);
        }
        runtime_remote_record.package_size = runtime_remote_status.manifest.package_size;
        kmemcpy(runtime_remote_record.package_hash, hash, 32U);
        runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_VALIDATING;
        kmemset(&verification, 0, sizeof(verification));
        if (update_runtime_verify_file_for_manifest(
                runtime_remote_package_aliases[pending_slot],
                &runtime_remote_status.manifest, &verification) != OK) {
            result_out->verification = verification;
            return runtime_remote_abort_pending(
                UPDATE_REMOTE_RUNTIME_REASON_PACKAGE_VERIFY, ERR_INVALID,
                result_out);
        }
        result_out->verification = verification;
        result_out->package_published = 1U;
    } else {
        runtime_remote_record.package_size = 0U;
        kmemset(runtime_remote_record.package_hash, 0,
                sizeof(runtime_remote_record.package_hash));
        for (uint32_t index = 0U;
             index < runtime_remote_status.manifest.entry_count; index++) {
            update_runtime_entry_t* entry = &runtime_remote_status.manifest.entries[index];
            uint8_t matches = 0U;
            char url[UPDATE_REMOTE_RUNTIME_URL_SIZE];
            char alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];
            uint32_t asset_size;

            if (!entry->target_present) {
                continue;
            }
            if (update_runtime_file_matches(entry->path, entry->target_size,
                                            entry->target_hash, &matches) == OK &&
                matches) {
                result_out->assets_reused++;
                continue;
            }
            result = runtime_remote_asset_url(entry, url, sizeof(url), &asset_size);
            if (result != OK) return runtime_remote_abort_pending(
                UPDATE_REMOTE_RUNTIME_REASON_RELEASE_ASSET, result, result_out);
            runtime_remote_asset_alias(alias, pending_slot, (uint8_t)index);
            if (runtime_remote_download(url, alias, asset_size, options, hash) != OK ||
                !crypto_equal(hash, entry->asset_hash, 32U)) {
                return runtime_remote_abort_pending(
                    runtime_remote_cancelled ? UPDATE_REMOTE_RUNTIME_REASON_CANCELLED :
                    UPDATE_REMOTE_RUNTIME_REASON_ASSET_HASH,
                    runtime_remote_cancelled ? ERR_TIMEOUT : ERR_INVALID,
                    result_out);
            }
            runtime_remote_record.asset_mask |= (uint16_t)(1U << index);
            result_out->assets_downloaded++;
            result_out->bytes_received += asset_size;
        }
        result_out->selective_published = 1U;
    }
    runtime_remote_record.active_slot = pending_slot;
    {
        uint32_t progress_total = runtime_remote_status.total_bytes;

        if (runtime_remote_validate_active() != OK) {
            runtime_remote_record.active_slot = previous_active;
            runtime_remote_status.active_slot = previous_active;
            return runtime_remote_abort_pending(
                UPDATE_REMOTE_RUNTIME_REASON_CACHE, ERR_INVALID, result_out);
        }
        runtime_remote_status.total_bytes = progress_total;
        runtime_remote_status.bytes_received = progress_total;
        runtime_remote_progress_base = progress_total;
    }
    runtime_remote_record.phase = RUNTIME_REMOTE_PHASE_CLEAN;
    runtime_remote_record.pending_slot = pending_slot;
    if (runtime_remote_write_record() != OK) return runtime_remote_abort_pending(
        UPDATE_REMOTE_RUNTIME_REASON_IO, ERR_DISK, result_out);
    runtime_remote_record.active_slot = pending_slot;
    runtime_remote_record.pending_slot = RUNTIME_REMOTE_SLOT_NONE;
    if (runtime_remote_write_record() != OK) {
        runtime_remote_record.active_slot = previous_active;
        runtime_remote_record.pending_slot = pending_slot;
        return runtime_remote_abort_pending(
            UPDATE_REMOTE_RUNTIME_REASON_IO, ERR_DISK, result_out);
    }
    if (previous_active <= 1U && previous_active != pending_slot) {
        runtime_remote_delete_slot(previous_active);
    }
    runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_AVAILABLE;
    runtime_remote_status.reason = UPDATE_REMOTE_RUNTIME_REASON_NONE;
    runtime_remote_status.busy = 0U;
    runtime_remote_status.pending_recovery = 0U;
    runtime_remote_previous_record_valid = 0U;
    runtime_remote_copy_result(result_out);
    result_out->manifest_published = 1U;
    LOG_INFO("UPDATE", "Cache runtime v2 publicado");
    return OK;
}

int update_remote_runtime_clear(const update_remote_options_t* options,
                                update_remote_runtime_result_t* result_out) {
    update_remote_options_t defaults = {1U, 0, 0, 0, 0, 0, 0, 0};
    int result = OK;

    if (!result_out) {
        LOG_ERROR("UPDATE", "Destino nulo na limpeza runtime remoto");
        return ERR_NULL;
    }
    if (!runtime_remote_status.initialized) {
        LOG_ERROR("UPDATE", "Limpeza runtime remoto antes da inicializacao");
        return ERR_STATE;
    }
    if (!options) options = &defaults;
    kmemset(result_out, 0, sizeof(*result_out));
    if (runtime_remote_status.busy) {
        LOG_ERROR("UPDATE", "Limpeza runtime remoto durante operacao ativa");
        return ERR_STATE;
    }
    if (options->dry_run) {
        result_out->cache_preserved =
            runtime_remote_record.active_slot <= 1U ? 1U : 0U;
        return OK;
    }
    for (uint8_t slot = 0U; slot < 2U; slot++) {
        if (runtime_remote_delete_slot(slot) != OK) result = ERR_DISK;
        if (runtime_remote_delete_root(runtime_remote_record_aliases[slot]) != OK) {
            result = ERR_DISK;
        }
    }
    if (result != OK) {
        result_out->reason = UPDATE_REMOTE_RUNTIME_REASON_IO;
        runtime_remote_status.pending_recovery = 1U;
        runtime_remote_status.reason = UPDATE_REMOTE_RUNTIME_REASON_IO;
        runtime_remote_status.state = UPDATE_REMOTE_RUNTIME_STATE_FAILED;
        LOG_ERROR("UPDATE", "Falha ao remover cache runtime remoto");
        return result;
    }
    kmemset(&runtime_remote_record, 0, sizeof(runtime_remote_record));
    runtime_remote_record.active_slot = RUNTIME_REMOTE_SLOT_NONE;
    runtime_remote_record.pending_slot = RUNTIME_REMOTE_SLOT_NONE;
    runtime_remote_record_slot = -1;
    runtime_remote_previous_record_valid = 0U;
    runtime_remote_manifest_valid = 0U;
    runtime_remote_status.active_slot = RUNTIME_REMOTE_SLOT_NONE;
    runtime_remote_status.pending_recovery = 0U;
    runtime_remote_status.manifest_cached = 0U;
    runtime_remote_status.selective_ready = 0U;
    runtime_remote_status.package_cached = 0U;
    runtime_remote_status.bytes_received = 0U;
    runtime_remote_status.total_bytes = 0U;
    runtime_remote_progress_base = 0U;
    runtime_remote_status.entry_count = 0U;
    runtime_remote_status.reused_entries = 0U;
    runtime_remote_status.missing_assets = 0U;
    kmemset(&runtime_remote_status.manifest,
            0, sizeof(runtime_remote_status.manifest));
    runtime_remote_status.cached_manifest_alias[0] = '\0';
    runtime_remote_status.cached_package_alias[0] = '\0';
    runtime_remote_status.reason = UPDATE_REMOTE_RUNTIME_REASON_NONE;
    runtime_remote_status.state = runtime_remote_status.enabled ?
        UPDATE_REMOTE_RUNTIME_STATE_READY : UPDATE_REMOTE_RUNTIME_STATE_DISABLED;
    return OK;
}

int update_remote_runtime_get_status(
    update_remote_runtime_status_t* status_out) {
    if (!status_out) {
        LOG_ERROR("UPDATE", "Destino nulo no status runtime remoto");
        return ERR_NULL;
    }
    if (!runtime_remote_status.initialized) {
        LOG_ERROR("UPDATE", "Status runtime remoto antes da inicializacao");
        return ERR_STATE;
    }
    runtime_remote_status.network_ready = runtime_remote_network_ready() ? 1U : 0U;
    *status_out = runtime_remote_status;
    return OK;
}

int update_remote_runtime_get_cache(update_runtime_cache_t* cache_out) {
    uint8_t hash[32];
    uint32_t manifest_size = 0U;
    uint32_t bytes;
    update_runtime_reason_t reason;
    uint8_t slot;

    if (!cache_out) {
        LOG_ERROR("UPDATE", "Destino nulo no cache runtime remoto");
        return ERR_NULL;
    }
    kmemset(cache_out, 0, sizeof(*cache_out));
    if (!runtime_remote_status.initialized ||
        runtime_remote_record.active_slot > 1U ||
        runtime_remote_validate_active() != OK) {
        LOG_ERROR("UPDATE", "Cache runtime remoto ausente ou invalido");
        return ERR_NOT_FOUND;
    }
    slot = runtime_remote_record.active_slot;
    cache_out->valid = 1U;
    cache_out->selective =
        runtime_remote_record.mode == UPDATE_REMOTE_RUNTIME_FETCH_SELECTIVE;
    cache_out->full_package =
        runtime_remote_record.mode == UPDATE_REMOTE_RUNTIME_FETCH_FULL;
    cache_out->active_slot = slot;
    runtime_remote_copy_text(cache_out->manifest_alias,
                             sizeof(cache_out->manifest_alias),
                             runtime_remote_manifest_aliases[slot]);
    runtime_remote_copy_text(cache_out->package_alias,
                             sizeof(cache_out->package_alias),
                             cache_out->full_package ?
                             runtime_remote_package_aliases[slot] : "");
    bytes = 0U;
    if (fs_get_root_file_info(runtime_remote_manifest_aliases[slot],
                              &manifest_size, 0) != OK ||
        manifest_size != UPDATE_RUNTIME_MANIFEST_SIZE ||
        fs_read_file_range_at(runtime_remote_manifest_aliases[slot], 0U,
                              runtime_remote_manifest_raw,
                              UPDATE_RUNTIME_MANIFEST_SIZE, &bytes) != OK ||
        bytes != UPDATE_RUNTIME_MANIFEST_SIZE ||
        update_runtime_parse_manifest(runtime_remote_manifest_raw, bytes,
                                       &cache_out->manifest, &reason) != OK ||
        crypto_sha256(runtime_remote_manifest_raw, bytes, hash) != OK ||
        !crypto_equal(hash, runtime_remote_record.manifest_hash, 32U)) {
        cache_out->valid = 0U;
        return ERR_INVALID;
    }
    cache_out->entry_count = cache_out->manifest.entry_count;
    cache_out->missing_assets = 0U;
    for (uint32_t index = 0U; index < cache_out->manifest.entry_count; index++) {
        update_runtime_entry_t* entry = &cache_out->manifest.entries[index];
        if (!entry->target_present) continue;
        if (runtime_remote_record.asset_mask & (uint16_t)(1U << index)) {
            runtime_remote_asset_alias(cache_out->asset_aliases[index], slot,
                                       (uint8_t)index);
        } else {
            uint8_t matches = 0U;
            if (update_runtime_file_matches(entry->path, entry->target_size,
                                            entry->target_hash,
                                            &matches) != OK || !matches) {
                cache_out->missing_assets++;
            }
        }
    }
    return OK;
}

int update_remote_runtime_get_cached_alias(char* alias_out,
                                           uint32_t capacity) {
    if (!alias_out || !capacity) {
        LOG_ERROR("UPDATE", "Buffer invalido para alias runtime remoto");
        return ERR_NULL;
    }
    if (!runtime_remote_status.initialized ||
        runtime_remote_record.active_slot > 1U ||
        runtime_remote_validate_active() != OK) {
        LOG_ERROR("UPDATE", "Alias runtime remoto indisponivel");
        return ERR_NOT_FOUND;
    }
    if (kstrlen(runtime_remote_manifest_aliases[runtime_remote_record.active_slot]) +
        1U > capacity) return ERR_OVERFLOW;
    runtime_remote_copy_text(alias_out, capacity,
                             runtime_remote_manifest_aliases[
                                 runtime_remote_record.active_slot]);
    return OK;
}

int update_remote_runtime_capability_available(void) {
    return runtime_remote_status.initialized && fs_get_type() == FS_TYPE_FAT12;
}

const char* update_remote_runtime_state_name(
    update_remote_runtime_state_t state) {
    switch (state) {
        case UPDATE_REMOTE_RUNTIME_STATE_DISABLED: return "DISABLED";
        case UPDATE_REMOTE_RUNTIME_STATE_UNAVAILABLE: return "UNAVAILABLE";
        case UPDATE_REMOTE_RUNTIME_STATE_READY: return "READY";
        case UPDATE_REMOTE_RUNTIME_STATE_CHECKING: return "CHECKING";
        case UPDATE_REMOTE_RUNTIME_STATE_AVAILABLE: return "AVAILABLE";
        case UPDATE_REMOTE_RUNTIME_STATE_DOWNLOADING: return "DOWNLOADING";
        case UPDATE_REMOTE_RUNTIME_STATE_VALIDATING: return "VALIDATING";
        case UPDATE_REMOTE_RUNTIME_STATE_FAILED: return "FAILED";
        default: return "INVALID_STATE";
    }
}

const char* update_remote_runtime_reason_name(
    update_remote_runtime_reason_t reason) {
    switch (reason) {
        case UPDATE_REMOTE_RUNTIME_REASON_NONE: return "NONE";
        case UPDATE_REMOTE_RUNTIME_REASON_DISABLED: return "DISABLED";
        case UPDATE_REMOTE_RUNTIME_REASON_NETWORK: return "NETWORK";
        case UPDATE_REMOTE_RUNTIME_REASON_HTTP: return "HTTP";
        case UPDATE_REMOTE_RUNTIME_REASON_TIMEOUT: return "TIMEOUT";
        case UPDATE_REMOTE_RUNTIME_REASON_DNS: return "DNS";
        case UPDATE_REMOTE_RUNTIME_REASON_TLS: return "TLS";
        case UPDATE_REMOTE_RUNTIME_REASON_RELEASE_NOT_FOUND: return "RELEASE_NOT_FOUND";
        case UPDATE_REMOTE_RUNTIME_REASON_RELEASE_FORMAT: return "RELEASE_FORMAT";
        case UPDATE_REMOTE_RUNTIME_REASON_RELEASE_TAG: return "RELEASE_TAG";
        case UPDATE_REMOTE_RUNTIME_REASON_RELEASE_ASSET: return "RELEASE_ASSET";
        case UPDATE_REMOTE_RUNTIME_REASON_MANIFEST_FORMAT: return "MANIFEST_FORMAT";
        case UPDATE_REMOTE_RUNTIME_REASON_MANIFEST_SIGNATURE: return "MANIFEST_SIGNATURE";
        case UPDATE_REMOTE_RUNTIME_REASON_UNKNOWN_KEY: return "UNKNOWN_KEY";
        case UPDATE_REMOTE_RUNTIME_REASON_VERSION: return "VERSION";
        case UPDATE_REMOTE_RUNTIME_REASON_SIZE: return "SIZE";
        case UPDATE_REMOTE_RUNTIME_REASON_ASSET_HASH: return "ASSET_HASH";
        case UPDATE_REMOTE_RUNTIME_REASON_PACKAGE_HASH: return "PACKAGE_HASH";
        case UPDATE_REMOTE_RUNTIME_REASON_PACKAGE_VERIFY: return "PACKAGE_VERIFY";
        case UPDATE_REMOTE_RUNTIME_REASON_CACHE: return "CACHE";
        case UPDATE_REMOTE_RUNTIME_REASON_IO: return "IO";
        case UPDATE_REMOTE_RUNTIME_REASON_CANCELLED: return "CANCELLED";
        case UPDATE_REMOTE_RUNTIME_REASON_TIME: return "TIME";
        default: return "INVALID_REASON";
    }
}
