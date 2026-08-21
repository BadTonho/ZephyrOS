#include "core/update_remote.h"
#include "core/crypto.h"
#include "core/errors.h"
#include "core/http.h"
#include "core/log.h"
#include "core/string.h"
#include "core/update_remote_config.h"
#include "process/process.h"

#define UPDATE_RELEASE_FORMAT "zephyros-release-v1"
#define UPDATE_RELEASE_TAG_MARKER "{tag}"
#define UPDATE_RELEASE_TAG_MARKER_SIZE 5U
#define UPDATE_RELEASE_JSON_KEY_SIZE 32U
#define UPDATE_RELEASE_SOURCE_COMMIT_SIZE UPDATE_REMOTE_SOURCE_COMMIT_SIZE
#define UPDATE_RELEASE_HASH_HEX_SIZE (CRYPTO_SHA256_SIZE * 2U)
#define UPDATE_RELEASE_HASH_TEXT_SIZE (UPDATE_RELEASE_HASH_HEX_SIZE + 1U)
#define UPDATE_RELEASE_WAIT_BLOCK_TICKS 1U
#define UPDATE_RELEASE_HTTP_OK 200U
#define UPDATE_RELEASE_HTTP_NOT_FOUND 404U
#define UPDATE_RELEASE_VERSION_COMPONENT_MAX 0xFFFFU
#define UPDATE_RELEASE_MANIFEST_SUFFIX ".zum"
#define UPDATE_RELEASE_PACKAGE_SUFFIX_ZUP ".zup"
#define UPDATE_RELEASE_PACKAGE_SUFFIX_UPD ".zephyrosupd"

typedef struct {
    const uint8_t* data;
    uint32_t length;
    uint32_t offset;
} update_release_json_t;

typedef struct {
    update_version_t minimum_version;
    update_version_t target_version;
    uint32_t base_epoch;
    uint32_t target_epoch;
} update_release_lock_t;

typedef struct {
    update_remote_release_t release;
    update_release_lock_t lock;
} update_release_descriptor_t;

typedef struct {
    uint8_t valid;
    uint8_t descriptor_hash[CRYPTO_SHA256_SIZE];
    update_remote_release_t release;
} update_release_selection_t;

/* O processo Shell possui apenas 4 KiB de stack; o descritor e o estado
 * HTTP sao grandes demais para permanecerem em frames durante o bloqueio. */
typedef struct {
    update_release_descriptor_t descriptor;
    uint8_t descriptor_hash[CRYPTO_SHA256_SIZE];
    http_status_t http_status;
    char descriptor_url[UPDATE_REMOTE_URL_SIZE];
    char text[UPDATE_REMOTE_RELEASE_NAME_SIZE];
    char source_commit[UPDATE_REMOTE_SOURCE_COMMIT_SIZE];
    char package_name[UPDATE_REMOTE_PATH_SIZE];
    char manifest_name[UPDATE_REMOTE_PATH_SIZE];
    char hash_text[UPDATE_RELEASE_HASH_TEXT_SIZE];
} update_release_workspace_t;

static update_release_selection_t update_release_selection;
static update_release_workspace_t update_release_workspace;

static int update_release_copy_text(char* output, uint32_t capacity,
                                    const char* input) {
    uint32_t length;

    if (!output || !capacity || !input) return ERR_NULL;
    length = kstrlen(input);
    if (length + 1U > capacity) return ERR_OVERFLOW;
    kmemcpy(output, input, length + 1U);
    return OK;
}

static void update_release_mark_cache_preserved(
    update_remote_result_t* result) {
    update_remote_status_t status;

    if (!result || result->cache_preserved ||
        update_remote_get_status(&status) != OK) return;
    result->cache_preserved = status.package_cached ? 1U : 0U;
}

static int update_release_reject(update_remote_reason_t reason, int error,
                                 const char* message,
                                 update_remote_result_t* result) {
    if (result) {
        result->reason = reason;
        update_release_mark_cache_preserved(result);
    }
    if (message) LOG_ERROR("UPDATE", message);
    return error;
}

static int update_release_channel_ready(update_remote_result_t* result) {
    update_remote_status_t status;
    int error;

    error = update_remote_get_status(&status);
    if (error != OK) {
        return update_release_reject(
            UPDATE_REMOTE_REASON_DISABLED, error,
            "Servico remoto nao esta inicializado", result);
    }
    if (!status.enabled) {
        return update_release_reject(
            UPDATE_REMOTE_REASON_DISABLED, ERR_UNAVAILABLE,
            "Distribuicao remota nao foi habilitada", result);
    }
    if (!status.network_ready) {
        return update_release_reject(
            UPDATE_REMOTE_REASON_NETWORK, ERR_UNAVAILABLE,
            "Rede nao esta configurada para Release remota", result);
    }
    return OK;
}

static int update_release_validate_tag(const char* tag) {
    uint32_t length;

    if (!tag) return ERR_NULL;
    length = kstrlen(tag);
    if (!length || length >= UPDATE_REMOTE_TAG_SIZE) return ERR_INVALID;
    for (uint32_t index = 0U; index < length; index++) {
        char value = tag[index];
        int allowed = (value >= 'A' && value <= 'Z') ||
                      (value >= 'a' && value <= 'z') ||
                      (value >= '0' && value <= '9') ||
                      value == '.' || value == '_' || value == '-';

        if (!allowed) return ERR_INVALID;
    }
    return OK;
}

static int update_release_hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static int update_release_parse_hash(const char* text,
                                     uint8_t output[CRYPTO_SHA256_SIZE]) {
    if (!text || !output ||
        kstrlen(text) != UPDATE_RELEASE_HASH_HEX_SIZE) return ERR_INVALID;
    for (uint32_t index = 0U; index < CRYPTO_SHA256_SIZE; index++) {
        int high = update_release_hex_value(text[index * 2U]);
        int low = update_release_hex_value(text[index * 2U + 1U]);

        if (high < 0 || low < 0) return ERR_INVALID;
        output[index] = (uint8_t)((high << 4) | low);
    }
    return OK;
}

static int update_release_parse_version(const char* text,
                                        update_version_t* version) {
    uint32_t parts[3] = {0U, 0U, 0U};
    uint32_t part = 0U;
    uint8_t has_digit = 0U;
    uint32_t part_count = 0U;

    if (!text || !version) return ERR_NULL;
    for (uint32_t index = 0U; text[index]; index++) {
        char value = text[index];

        if (value >= '0' && value <= '9') {
            if (part > (UPDATE_RELEASE_VERSION_COMPONENT_MAX -
                        (uint32_t)(value - '0')) / 10U) {
                return ERR_OVERFLOW;
            }
            part = part * 10U + (uint32_t)(value - '0');
            has_digit = 1U;
        } else if (value == '.' && has_digit && part_count < 2U) {
            parts[part_count++] = part;
            part = 0U;
            has_digit = 0U;
        } else {
            return ERR_INVALID;
        }
    }
    if (!has_digit || part_count != 2U) return ERR_INVALID;
    parts[2] = part;
    version->major = (uint16_t)parts[0];
    version->minor = (uint16_t)parts[1];
    version->patch = (uint16_t)parts[2];
    return OK;
}

static int update_release_parse_u32(update_release_json_t* json,
                                    uint32_t* output) {
    uint32_t value = 0U;
    uint8_t has_digit = 0U;

    if (!json || !output) return ERR_NULL;
    if (json->offset < json->length && json->data[json->offset] == '0') {
        json->offset++;
        has_digit = 1U;
        if (json->offset < json->length &&
            json->data[json->offset] >= '0' &&
            json->data[json->offset] <= '9') {
            return ERR_INVALID;
        }
    }
    while (json->offset < json->length) {
        uint8_t raw = json->data[json->offset];
        uint32_t digit;

        if (raw < '0' || raw > '9') break;
        digit = (uint32_t)(raw - '0');
        if (value > (0xFFFFFFFFU - digit) / 10U) return ERR_OVERFLOW;
        value = value * 10U + digit;
        json->offset++;
        has_digit = 1U;
    }
    if (!has_digit) return ERR_INVALID;
    *output = value;
    return OK;
}

static void update_release_json_skip_space(update_release_json_t* json) {
    while (json && json->offset < json->length) {
        uint8_t value = json->data[json->offset];

        if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
            break;
        }
        json->offset++;
    }
}

static int update_release_json_expect(update_release_json_t* json,
                                      uint8_t expected) {
    if (!json) return ERR_NULL;
    update_release_json_skip_space(json);
    if (json->offset >= json->length ||
        json->data[json->offset] != expected) return ERR_INVALID;
    json->offset++;
    return OK;
}

static int update_release_json_string(update_release_json_t* json,
                                      char* output, uint32_t capacity) {
    uint32_t written = 0U;

    if (!json || !output || !capacity) return ERR_NULL;
    output[0] = '\0';
    update_release_json_skip_space(json);
    if (json->offset >= json->length || json->data[json->offset++] != '"') {
        return ERR_INVALID;
    }
    while (json->offset < json->length) {
        uint8_t value = json->data[json->offset++];

        if (value == '"') {
            output[written] = '\0';
            return OK;
        }
        if (value == '\\') {
            if (json->offset >= json->length) return ERR_INVALID;
            value = json->data[json->offset++];
            if (value == '"' || value == '\\' || value == '/' ||
                value == 'b' || value == 'f' || value == 'n' ||
                value == 'r' || value == 't') {
                if (value == 'b' || value == 'f' || value == 'n' ||
                    value == 'r' || value == 't') {
                    return ERR_INVALID;
                }
            } else {
                return ERR_INVALID;
            }
        } else if (value < 0x20U) {
            return ERR_INVALID;
        }
        if (written + 1U >= capacity) return ERR_OVERFLOW;
        output[written++] = (char)value;
    }
    return ERR_INVALID;
}

static int update_release_json_key(update_release_json_t* json,
                                   const char* expected) {
    char key[UPDATE_RELEASE_JSON_KEY_SIZE];
    int result;

    result = update_release_json_string(json, key, sizeof(key));
    if (result != OK || kstrcmp(key, expected) != 0) return ERR_INVALID;
    return update_release_json_expect(json, ':');
}

static int update_release_validate_identifier(const char* text,
                                             uint32_t capacity) {
    uint32_t length;

    if (!text || capacity < 2U) return ERR_NULL;
    length = kstrlen(text);
    if (!length || length + 1U > capacity) return ERR_INVALID;
    for (uint32_t index = 0U; index < length; index++) {
        char value = text[index];
        int allowed = (value >= 'A' && value <= 'Z') ||
                      (value >= 'a' && value <= 'z') ||
                      (value >= '0' && value <= '9') ||
                      value == '.' || value == '_' || value == '-';

        if (!allowed) return ERR_INVALID;
    }
    return OK;
}

static int update_release_validate_source_commit(const char* text) {
    if (!text || kstrlen(text) != UPDATE_RELEASE_SOURCE_COMMIT_SIZE - 1U) {
        return ERR_INVALID;
    }
    for (uint32_t index = 0U;
         index < UPDATE_RELEASE_SOURCE_COMMIT_SIZE - 1U; index++) {
        char value = text[index];

        if (!((value >= '0' && value <= '9') ||
              (value >= 'a' && value <= 'f'))) return ERR_INVALID;
    }
    return OK;
}

static int update_release_has_suffix(const char* text, const char* suffix) {
    uint32_t text_length;
    uint32_t suffix_length;

    if (!text || !suffix) return 0;
    text_length = kstrlen(text);
    suffix_length = kstrlen(suffix);
    if (text_length < suffix_length) return 0;
    for (uint32_t index = 0U; index < suffix_length; index++) {
        char first = text[text_length - suffix_length + index];
        char second = suffix[index];

        if (first >= 'A' && first <= 'Z') first += 'a' - 'A';
        if (second >= 'A' && second <= 'Z') second += 'a' - 'A';
        if (first != second) return 0;
    }
    return 1;
}

static int update_release_validate_asset_name(const char* name,
                                              uint32_t capacity,
                                              int manifest) {
    uint32_t length;

    if (!name || capacity < 2U) return ERR_NULL;
    length = kstrlen(name);
    if (!length || length + 1U > capacity) return ERR_INVALID;
    for (uint32_t index = 0U; index < length; index++) {
        uint8_t value = (uint8_t)name[index];
        int allowed = (value >= 'A' && value <= 'Z') ||
                      (value >= 'a' && value <= 'z') ||
                      (value >= '0' && value <= '9') ||
                      value == '.' || value == '-' || value == '_';

        if (!allowed) return ERR_INVALID;
    }
    if (manifest) {
        return update_release_has_suffix(name,
                                         UPDATE_RELEASE_MANIFEST_SUFFIX) ?
               OK : ERR_INVALID;
    }
    return update_release_has_suffix(name, UPDATE_RELEASE_PACKAGE_SUFFIX_ZUP) ||
           update_release_has_suffix(name, UPDATE_RELEASE_PACKAGE_SUFFIX_UPD) ?
           OK : ERR_INVALID;
}

static int update_release_json_asset(update_release_json_t* json,
                                     char* name, uint32_t name_capacity,
                                     uint32_t* size_out,
                                     uint8_t hash[CRYPTO_SHA256_SIZE],
                                     int manifest) {
    char* hash_text = update_release_workspace.hash_text;
    int result;

    if (!json || !name || !name_capacity || !size_out || !hash) {
        return ERR_NULL;
    }
    hash_text[0] = '\0';
    result = update_release_json_expect(json, '{');
    if (result == OK) result = update_release_json_key(json, "name");
    if (result == OK) {
        result = update_release_json_string(json, name, name_capacity);
    }
    if (result == OK) {
        result = update_release_json_expect(json, ',');
    }
    if (result == OK) result = update_release_json_key(json, "size");
    if (result == OK) result = update_release_parse_u32(json, size_out);
    if (result == OK) result = update_release_json_expect(json, ',');
    if (result == OK) result = update_release_json_key(json, "sha256");
    if (result == OK) {
        result = update_release_json_string(
            json, hash_text, sizeof(update_release_workspace.hash_text));
    }
    if (result == OK) result = update_release_json_expect(json, '}');
    if (result != OK || update_release_validate_asset_name(
            name, name_capacity, manifest) != OK ||
        !*size_out || (manifest && *size_out != UPDATE_REMOTE_MANIFEST_SIZE) ||
        (!manifest && *size_out > ZUPD_MAX_TOTAL_SIZE) ||
        update_release_parse_hash(hash_text, hash) != OK) {
        return ERR_INVALID;
    }
    return OK;
}

static int update_release_build_manifest_url(const char* descriptor_url,
                                             const char* manifest_name,
                                             char* output);

static int update_release_parse_descriptor(
    const uint8_t* data, uint32_t length, const char* requested_tag,
    const char* descriptor_url, update_release_descriptor_t* descriptor,
    uint8_t descriptor_hash[CRYPTO_SHA256_SIZE],
    update_remote_reason_t* reason_out) {
    update_release_json_t json;
    char* text = update_release_workspace.text;
    char* source_commit = update_release_workspace.source_commit;
    char* package_name = update_release_workspace.package_name;
    char* manifest_name = update_release_workspace.manifest_name;
    const uint32_t text_capacity = sizeof(update_release_workspace.text);
    const uint32_t source_commit_capacity =
        sizeof(update_release_workspace.source_commit);
    const uint32_t package_capacity =
        sizeof(update_release_workspace.package_name);
    const uint32_t manifest_capacity =
        sizeof(update_release_workspace.manifest_name);
    uint32_t manifest_size = 0U;
    int result;

    if (!data || !requested_tag || !descriptor_url || !descriptor ||
        !descriptor_hash || !reason_out) return ERR_NULL;
    *reason_out = UPDATE_REMOTE_REASON_RELEASE_FORMAT;
    kmemset(descriptor, 0, sizeof(*descriptor));
    package_name[0] = '\0';
    manifest_name[0] = '\0';
    if (crypto_sha256(data, length, descriptor_hash) != OK) return ERR_STATE;
    json.data = data;
    json.length = length;
    json.offset = 0U;
    result = update_release_json_expect(&json, '{');
    if (result == OK) result = update_release_json_key(&json, "format");
    if (result == OK) result = update_release_json_string(
        &json, text, text_capacity);
    if (result == OK && kstrcmp(text, UPDATE_RELEASE_FORMAT) != 0) {
        result = ERR_INVALID;
    }
    if (result == OK) result = update_release_json_expect(&json, ',');
    if (result == OK) result = update_release_json_key(&json, "release_id");
    if (result == OK) result = update_release_json_string(
        &json, descriptor->release.release_id,
        sizeof(descriptor->release.release_id));
    if (result == OK && update_release_validate_identifier(
            descriptor->release.release_id,
            sizeof(descriptor->release.release_id)) != OK) {
        result = ERR_INVALID;
    }
    if (result == OK) result = update_release_json_expect(&json, ',');
    if (result == OK) result = update_release_json_key(&json, "release_name");
    if (result == OK) result = update_release_json_string(
        &json, descriptor->release.release_name,
        sizeof(descriptor->release.release_name));
    if (result == OK && (!descriptor->release.release_name[0] ||
                         kstrlen(descriptor->release.release_name) >=
                             UPDATE_REMOTE_RELEASE_NAME_SIZE)) {
        result = ERR_INVALID;
    }
    if (result == OK) result = update_release_json_expect(&json, ',');
    if (result == OK) result = update_release_json_key(&json, "channel");
    if (result == OK) result = update_release_json_string(
        &json, text, text_capacity);
    if (result == OK && kstrcmp(text, UPDATE_REMOTE_CHANNEL_NAME) != 0) {
        result = ERR_INVALID;
    }
    if (result == OK) result = update_release_json_expect(&json, ',');
    if (result == OK) result = update_release_json_key(&json, "source_commit");
    if (result == OK) result = update_release_json_string(
        &json, source_commit, source_commit_capacity);
    if (result == OK && update_release_validate_source_commit(source_commit) != OK) {
        result = ERR_INVALID;
    }
    if (result == OK) result = update_release_json_expect(&json, ',');
    if (result == OK) {
        result = update_release_json_key(&json, "tag");
        if (result != OK) *reason_out = UPDATE_REMOTE_REASON_RELEASE_TAG;
    }
    if (result == OK) result = update_release_json_string(
        &json, descriptor->release.tag,
        sizeof(descriptor->release.tag));
    if (result == OK && update_release_validate_tag(
            descriptor->release.tag) != OK) {
        *reason_out = UPDATE_REMOTE_REASON_RELEASE_TAG;
        result = ERR_INVALID;
    }
    if (result == OK && kstrcmp(descriptor->release.tag, requested_tag) != 0) {
        *reason_out = UPDATE_REMOTE_REASON_RELEASE_TAG;
        result = ERR_INVALID;
    }
    if (result == OK) result = update_release_json_expect(&json, ',');
    if (result == OK) result = update_release_json_key(&json, "version_lock");
    if (result == OK) result = update_release_json_expect(&json, '{');
    if (result == OK) result = update_release_json_key(
        &json, "minimum_version");
    if (result == OK) result = update_release_json_string(
        &json, text, text_capacity);
    if (result == OK) result = update_release_parse_version(
        text, &descriptor->lock.minimum_version);
    if (result == OK) result = update_release_json_expect(&json, ',');
    if (result == OK) result = update_release_json_key(
        &json, "target_version");
    if (result == OK) result = update_release_json_string(
        &json, text, text_capacity);
    if (result == OK) result = update_release_parse_version(
        text, &descriptor->lock.target_version);
    if (result == OK) result = update_release_json_expect(&json, ',');
    if (result == OK) result = update_release_json_key(&json, "base_epoch");
    if (result == OK) result = update_release_parse_u32(
        &json, &descriptor->lock.base_epoch);
    if (result == OK) result = update_release_json_expect(&json, ',');
    if (result == OK) result = update_release_json_key(&json, "target_epoch");
    if (result == OK) result = update_release_parse_u32(
        &json, &descriptor->lock.target_epoch);
    if (result == OK) result = update_release_json_expect(&json, '}');
    if (result == OK && descriptor->lock.target_epoch <
            descriptor->lock.base_epoch) {
        result = ERR_INVALID;
    }
    if (result == OK) result = update_release_json_expect(&json, ',');
    if (result == OK) result = update_release_json_key(&json, "assets");
    if (result == OK) result = update_release_json_expect(&json, '{');
    if (result == OK) {
        result = update_release_json_key(&json, "package");
        if (result != OK) *reason_out = UPDATE_REMOTE_REASON_RELEASE_ASSET;
    }
    if (result == OK) result = update_release_json_asset(
        &json, package_name, package_capacity,
        &descriptor->release.package_size,
        descriptor->release.package_hash, 0);
    if (result != OK && *reason_out == UPDATE_REMOTE_REASON_RELEASE_FORMAT) {
        *reason_out = UPDATE_REMOTE_REASON_RELEASE_ASSET;
    }
    if (result == OK) {
        result = update_release_json_expect(&json, ',');
        if (result != OK) *reason_out = UPDATE_REMOTE_REASON_RELEASE_ASSET;
    }
    if (result == OK) {
        result = update_release_json_key(&json, "manifest");
        if (result != OK) *reason_out = UPDATE_REMOTE_REASON_RELEASE_ASSET;
    }
    if (result == OK) result = update_release_json_asset(
        &json, manifest_name, manifest_capacity,
        &manifest_size,
        descriptor->release.manifest_hash, 1);
    if (result != OK && *reason_out == UPDATE_REMOTE_REASON_RELEASE_FORMAT) {
        *reason_out = UPDATE_REMOTE_REASON_RELEASE_ASSET;
    }
    if (result == OK) result = update_release_json_expect(&json, '}');
    if (result == OK) result = update_release_json_expect(&json, '}');
    update_release_json_skip_space(&json);
    if (result == OK && json.offset != json.length) result = ERR_INVALID;
    if (result != OK) {
        return result;
    }
    if (update_release_copy_text(
            descriptor->release.descriptor_url,
            sizeof(descriptor->release.descriptor_url), descriptor_url) != OK) {
        return ERR_OVERFLOW;
    }
    if (update_release_copy_text(
            descriptor->release.source_commit,
            sizeof(descriptor->release.source_commit), source_commit) != OK) {
        return ERR_OVERFLOW;
    }
    if (update_release_copy_text(
            descriptor->release.package_name,
            sizeof(descriptor->release.package_name), package_name) != OK) {
        return ERR_OVERFLOW;
    }
    if (descriptor->release.package_size > ZUPD_MAX_TOTAL_SIZE ||
        descriptor->release.package_size == 0U) {
        *reason_out = UPDATE_REMOTE_REASON_RELEASE_ASSET;
        return ERR_INVALID;
    }
    if (update_release_copy_text(
            descriptor->release.manifest_url,
            sizeof(descriptor->release.manifest_url), descriptor_url) != OK) {
        return ERR_OVERFLOW;
    }
    if (update_release_build_manifest_url(
            descriptor_url, manifest_name, descriptor->release.manifest_url) !=
        OK) {
        *reason_out = UPDATE_REMOTE_REASON_RELEASE_ASSET;
        return ERR_INVALID;
    }
    return OK;
}

static int update_release_build_descriptor_url(const char* tag,
                                               char* output) {
    const char* source = UPDATE_REMOTE_RELEASE_URL_TEMPLATE;
    uint32_t source_length;
    uint32_t tag_length;
    uint32_t marker_count = 0U;
    uint32_t marker_offset = 0U;
    uint32_t output_length;

    if (!tag || !output) return ERR_NULL;
    source_length = kstrlen(source);
    tag_length = kstrlen(tag);
    for (uint32_t index = 0U; index + UPDATE_RELEASE_TAG_MARKER_SIZE <=
         source_length; index++) {
        uint8_t matches = 1U;

        for (uint32_t marker = 0U; marker < UPDATE_RELEASE_TAG_MARKER_SIZE;
             marker++) {
            if (source[index + marker] !=
                UPDATE_RELEASE_TAG_MARKER[marker]) matches = 0U;
        }
        if (matches) {
            marker_count++;
            marker_offset = index;
        }
    }
    if (marker_count != 1U) return ERR_INVALID;
    output_length = source_length - UPDATE_RELEASE_TAG_MARKER_SIZE +
                    tag_length;
    if (output_length + 1U > UPDATE_REMOTE_URL_SIZE) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < marker_offset; index++) {
        output[index] = source[index];
    }
    for (uint32_t index = 0U; index < tag_length; index++) {
        output[marker_offset + index] = tag[index];
    }
    for (uint32_t index = marker_offset + UPDATE_RELEASE_TAG_MARKER_SIZE;
         index < source_length; index++) {
        output[index - UPDATE_RELEASE_TAG_MARKER_SIZE + tag_length] =
            source[index];
    }
    output[output_length] = '\0';
    return OK;
}

static int update_release_build_manifest_url(const char* descriptor_url,
                                             const char* manifest_name,
                                             char* output) {
    uint32_t url_length;
    uint32_t prefix_length = 0U;
    uint32_t name_length;

    if (!descriptor_url || !manifest_name || !output) return ERR_NULL;
    url_length = kstrlen(descriptor_url);
    name_length = kstrlen(manifest_name);
    for (uint32_t index = 0U; index < url_length; index++) {
        if (descriptor_url[index] == '/') prefix_length = index + 1U;
    }
    if (!prefix_length || prefix_length >= url_length ||
        prefix_length + name_length + 1U > UPDATE_REMOTE_URL_SIZE) {
        return ERR_INVALID;
    }
    for (uint32_t index = 0U; index < prefix_length; index++) {
        output[index] = descriptor_url[index];
    }
    for (uint32_t index = 0U; index <= name_length; index++) {
        output[prefix_length + index] = manifest_name[index];
    }
    return OK;
}

static int update_release_wait_http(const update_remote_options_t* options,
                                    http_status_t* status_out,
                                    uint8_t* cancelled_out) {
    if (!status_out || !cancelled_out) return ERR_NULL;
    *cancelled_out = 0U;
    while (1) {
        if (http_get_status(status_out) != OK) {
            LOG_ERROR("UPDATE", "Falha ao consultar descritor de Release");
            return ERR_STATE;
        }
        if (status_out->state == HTTP_STATE_COMPLETE) return OK;
        if (status_out->state == HTTP_STATE_FAILED) {
            return status_out->last_error == OK ?
                   ERR_STATE : status_out->last_error;
        }
        if (options && options->cancel_check &&
            options->cancel_check(options->cancel_context)) {
            *cancelled_out = 1U;
            http_reset();
            LOG_WARN("UPDATE", "Consulta de Release cancelada");
            return ERR_TIMEOUT;
        }
        process_block(UPDATE_RELEASE_WAIT_BLOCK_TICKS);
    }
}

static int update_release_fetch_descriptor(
    const char* descriptor_url, const update_remote_options_t* options,
    update_remote_result_t* result_out, const uint8_t** body_out,
    uint32_t* length_out, update_remote_reason_t* reason_out) {
    http_status_t* status = &update_release_workspace.http_status;
    uint8_t cancelled = 0U;
    int result;

    if (!descriptor_url || !result_out || !body_out || !length_out ||
        !reason_out) return ERR_NULL;
    *body_out = 0;
    *length_out = 0U;
    kmemset(status, 0, sizeof(*status));
    *reason_out = UPDATE_REMOTE_REASON_HTTP;
    result = http_get_start(descriptor_url);
    if (result == OK) result = update_release_wait_http(
        options, status, &cancelled);
    if (result != OK) {
        *reason_out = cancelled ? UPDATE_REMOTE_REASON_CANCELLED :
                     result == ERR_TIMEOUT ? UPDATE_REMOTE_REASON_TIMEOUT :
                     UPDATE_REMOTE_REASON_HTTP;
        return result;
    }
    result_out->http_status = status->status_code;
    result_out->bytes_received = status->body_length;
    if (status->status_code != UPDATE_RELEASE_HTTP_OK) {
        *reason_out = status->status_code == UPDATE_RELEASE_HTTP_NOT_FOUND ?
                      UPDATE_REMOTE_REASON_RELEASE_NOT_FOUND :
                      UPDATE_REMOTE_REASON_HTTP;
        return status->status_code == UPDATE_RELEASE_HTTP_NOT_FOUND ?
               ERR_NOT_FOUND : ERR_INVALID;
    }
    if (!status->has_content_length ||
        status->content_length != status->body_length ||
        !status->body_length || http_get_body(body_out, length_out) != OK) {
        *reason_out = UPDATE_REMOTE_REASON_RELEASE_FORMAT;
        return ERR_INVALID;
    }
    return OK;
}

static int update_release_resolve_descriptor(
    const char* tag, const update_remote_options_t* options,
    update_remote_result_t* result_out,
    update_release_descriptor_t* descriptor_out,
    uint8_t descriptor_hash[CRYPTO_SHA256_SIZE]) {
    char* descriptor_url = update_release_workspace.descriptor_url;
    const uint8_t* body = 0;
    uint32_t body_length = 0U;
    update_remote_reason_t reason;
    int result;

    result = update_release_build_descriptor_url(tag, descriptor_url);
    if (result != OK) {
        return update_release_reject(
            UPDATE_REMOTE_REASON_RELEASE_FORMAT, result,
            "Template de Release invalido", result_out);
    }
    result = update_release_fetch_descriptor(
        descriptor_url, options, result_out, &body, &body_length, &reason);
    if (result != OK) {
        return update_release_reject(
            reason, result, "Falha ao obter descritor de Release", result_out);
    }
    result = update_release_parse_descriptor(
        body, body_length, tag, descriptor_url, descriptor_out,
        descriptor_hash, &reason);
    if (result != OK) {
        return update_release_reject(
            reason, result, "Descritor de Release recusado", result_out);
    }
    return OK;
}

static int update_release_versions_equal(const update_version_t* first,
                                         const update_version_t* second) {
    return first && second && first->major == second->major &&
           first->minor == second->minor && first->patch == second->patch;
}

static const char* update_release_basename(const char* path) {
    const char* basename = path;

    if (!path) return 0;
    for (uint32_t index = 0U; path[index]; index++) {
        if (path[index] == '/') basename = path + index + 1U;
    }
    return basename;
}

static int update_release_candidate_matches(
    const update_release_descriptor_t* descriptor,
    const update_remote_result_t* result) {
    const char* basename;

    if (!descriptor || !result) return 0;
    basename = update_release_basename(result->candidate.package_path);
    return basename && kstrcmp(basename, descriptor->release.package_name) == 0 &&
           result->candidate.package_size == descriptor->release.package_size &&
           crypto_equal(result->candidate.package_hash,
                        descriptor->release.package_hash,
                        CRYPTO_SHA256_SIZE) &&
           crypto_equal(result->manifest_hash,
                        descriptor->release.manifest_hash,
                        CRYPTO_SHA256_SIZE) &&
           update_release_versions_equal(
               &result->candidate.base_version,
               &descriptor->lock.minimum_version) &&
           update_release_versions_equal(
               &result->candidate.target_version,
               &descriptor->lock.target_version) &&
           result->candidate.base_epoch == descriptor->lock.base_epoch &&
           result->candidate.target_epoch == descriptor->lock.target_epoch;
}

int update_remote_release_check(const char* tag,
                                const update_remote_options_t* options,
                                update_remote_result_t* result_out) {
    update_remote_options_t defaults = {1U, 0, 0};
    int result;

    if (!result_out) {
        LOG_ERROR("UPDATE", "Destino nulo na consulta por tag");
        return ERR_NULL;
    }
    kmemset(result_out, 0, sizeof(*result_out));
    update_release_selection.valid = 0U;
    if (!options) options = &defaults;
    result = update_release_validate_tag(tag);
    if (result != OK) {
        return update_release_reject(
            UPDATE_REMOTE_REASON_RELEASE_TAG, result,
            "Tag de Release invalida", result_out);
    }
    result = update_release_channel_ready(result_out);
    if (result != OK) return result;
    result = update_release_resolve_descriptor(
        tag, options, result_out, &update_release_workspace.descriptor,
        update_release_workspace.descriptor_hash);
    if (result != OK) return result;
    result = update_remote_check(
        update_release_workspace.descriptor.release.manifest_url,
        options, result_out);
    if (result != OK) return result;
    if (!update_release_candidate_matches(
            &update_release_workspace.descriptor, result_out)) {
        return update_release_reject(
            UPDATE_REMOTE_REASON_RELEASE_ASSET, ERR_INVALID,
            "Descritor e manifesto assinado divergem", result_out);
    }
    update_release_selection.valid = 1U;
    kmemcpy(update_release_selection.descriptor_hash,
            update_release_workspace.descriptor_hash, CRYPTO_SHA256_SIZE);
    update_release_selection.release = update_release_workspace.descriptor.release;
    result_out->release = update_release_workspace.descriptor.release;
    LOG_INFO("UPDATE", "Release selecionada por tag exata");
    return OK;
}

int update_remote_release_fetch(const char* tag,
                                const update_remote_options_t* options,
                                update_remote_result_t* result_out) {
    update_remote_options_t defaults = {0U, 0, 0};
    int result;

    if (!result_out) {
        LOG_ERROR("UPDATE", "Destino nulo no download por tag");
        return ERR_NULL;
    }
    kmemset(result_out, 0, sizeof(*result_out));
    if (!options) options = &defaults;
    if (options->dry_run) return update_remote_release_check(
        tag, options, result_out);
    result = update_release_validate_tag(tag);
    if (result != OK) {
        return update_release_reject(
            UPDATE_REMOTE_REASON_RELEASE_TAG, result,
            "Tag de Release invalida", result_out);
    }
    if (!update_release_selection.valid ||
        kstrcmp(update_release_selection.release.tag, tag) != 0) {
        return update_release_reject(
            UPDATE_REMOTE_REASON_RELEASE_CHANGED, ERR_STATE,
            "Download por tag exige consulta previa", result_out);
    }
    result = update_release_channel_ready(result_out);
    if (result != OK) return result;
    result = update_release_resolve_descriptor(
        tag, options, result_out, &update_release_workspace.descriptor,
        update_release_workspace.descriptor_hash);
    if (result != OK) return result;
    if (!crypto_equal(update_release_workspace.descriptor_hash,
                      update_release_selection.descriptor_hash,
                      CRYPTO_SHA256_SIZE) ||
        kstrcmp(update_release_workspace.descriptor.release.manifest_url,
                update_release_selection.release.manifest_url) != 0) {
        return update_release_reject(
            UPDATE_REMOTE_REASON_RELEASE_CHANGED, ERR_STATE,
            "Descritor de Release mudou apos a consulta", result_out);
    }
    result = update_remote_fetch(
        update_release_selection.release.manifest_url, options, result_out);
    result_out->release = update_release_selection.release;
    if (result != OK) return result;
    LOG_INFO("UPDATE", "Release por tag publicada no cache U5");
    return OK;
}
