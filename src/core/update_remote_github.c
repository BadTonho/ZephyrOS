#include "core/update_remote_github.h"
#include "core/crypto.h"
#include "core/errors.h"
#include "core/http.h"
#include "core/log.h"
#include "core/string.h"
#include "core/update.h"
#include "core/update_system.h"
#include "core/update_remote_config.h"
#include "core/update_runtime.h"
#include "process/process.h"

#define UPDATE_GITHUB_HTTP_OK 200U
#define UPDATE_GITHUB_HTTP_NOT_FOUND 404U
#define UPDATE_GITHUB_WAIT_BLOCK_TICKS 1U
#define UPDATE_GITHUB_JSON_DEPTH 8U
#define UPDATE_GITHUB_KEY_SIZE 48U
#define UPDATE_GITHUB_DIGEST_TEXT_SIZE 72U
#define UPDATE_GITHUB_FINGERPRINT_SIZE 4096U
#define UPDATE_GITHUB_PUBLISHED_TEXT_SIZE 20U
#define UPDATE_GITHUB_PORT_MAX 65535U

typedef struct {
    const uint8_t* data;
    uint32_t length;
    uint32_t offset;
    uint8_t depth;
} update_github_json_t;

typedef struct {
    update_remote_github_asset_t asset;
    uint8_t name_found;
    uint8_t size_found;
    uint8_t url_found;
    uint8_t state_found;
    uint8_t digest_found;
    char state[16];
} update_github_asset_parse_t;

typedef struct {
    uint8_t response[UPDATE_REMOTE_GITHUB_RESPONSE_CAPACITY];
    uint32_t response_length;
    uint8_t fingerprint[UPDATE_GITHUB_FINGERPRINT_SIZE];
    http_status_t http;
    uint8_t cancelled;
    char url[UPDATE_REMOTE_URL_SIZE];
    char host[HTTP_HOST_BUFFER_SIZE];
    char api_host[HTTP_HOST_BUFFER_SIZE];
    update_github_asset_parse_t asset_parse;
} update_github_workspace_t;

static update_github_workspace_t update_github_workspace;

static void update_github_skip_space(update_github_json_t* json) {
    while (json && json->offset < json->length) {
        uint8_t value = json->data[json->offset];

        if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
            break;
        }
        json->offset++;
    }
}

static int update_github_validate_tag(const char* tag) {
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

static int update_github_expect(update_github_json_t* json, uint8_t expected) {
    if (!json) return ERR_NULL;
    update_github_skip_space(json);
    if (json->offset >= json->length ||
        json->data[json->offset] != expected) return ERR_INVALID;
    json->offset++;
    return OK;
}

static int update_github_hex_digit(uint8_t value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int update_github_parse_string(update_github_json_t* json,
                                      char* output, uint32_t capacity) {
    uint32_t length = 0U;

    if (!json || !output || !capacity) return ERR_NULL;
    update_github_skip_space(json);
    if (json->offset >= json->length || json->data[json->offset++] != '"') {
        return ERR_INVALID;
    }
    while (json->offset < json->length) {
        uint8_t value = json->data[json->offset++];
        uint8_t encoded[3];
        uint8_t encoded_length = 1U;

        if (value == '"') {
            if (length >= capacity) return ERR_OVERFLOW;
            output[length] = '\0';
            return OK;
        }
        if (value == '\\') {
            if (json->offset >= json->length) return ERR_INVALID;
            value = json->data[json->offset++];
            if (value == '"' || value == '\\' || value == '/' ||
                value == 'b' || value == 'f' || value == 'n' ||
                value == 'r' || value == 't') {
                value = value == 'b' ? '\b' : value == 'f' ? '\f' :
                        value == 'n' ? '\n' : value == 'r' ? '\r' :
                        value == 't' ? '\t' : value;
            } else if (value == 'u') {
                uint32_t codepoint = 0U;

                if (json->offset + 4U > json->length) return ERR_INVALID;
                for (uint32_t index = 0U; index < 4U; index++) {
                    int digit = update_github_hex_digit(
                        json->data[json->offset + index]);

                    if (digit < 0) return ERR_INVALID;
                    codepoint = (codepoint << 4U) | (uint32_t)digit;
                }
                json->offset += 4U;
                if (codepoint == 0U ||
                    (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
                    return ERR_INVALID;
                } else if (codepoint <= 0x7FU) {
                    value = (uint8_t)codepoint;
                    encoded[0] = value;
                } else if (codepoint <= 0x7FFU) {
                    encoded[0] = (uint8_t)(0xC0U | (codepoint >> 6U));
                    encoded[1] = (uint8_t)(0x80U | (codepoint & 0x3FU));
                    encoded_length = 2U;
                } else {
                    encoded[0] = (uint8_t)(0xE0U | (codepoint >> 12U));
                    encoded[1] = (uint8_t)(0x80U |
                                           ((codepoint >> 6U) & 0x3FU));
                    encoded[2] = (uint8_t)(0x80U | (codepoint & 0x3FU));
                    encoded_length = 3U;
                }
            } else {
                return ERR_INVALID;
            }
        } else if (value < 0x20U) {
            return ERR_INVALID;
        }
        if (encoded_length == 1U) encoded[0] = value;
        if (length + encoded_length >= capacity) return ERR_OVERFLOW;
        for (uint8_t index = 0U; index < encoded_length; index++) {
            output[length++] = (char)encoded[index];
        }
    }
    return ERR_INVALID;
}

static int update_github_parse_null(update_github_json_t* json) {
    static const char literal[] = "null";

    if (!json) return ERR_NULL;
    update_github_skip_space(json);
    for (uint32_t index = 0U; index < sizeof(literal) - 1U; index++) {
        if (json->offset >= json->length ||
            json->data[json->offset++] != literal[index]) return ERR_INVALID;
    }
    return OK;
}

static int update_github_parse_bool(update_github_json_t* json,
                                    uint8_t* output) {
    static const char true_text[] = "true";
    static const char false_text[] = "false";
    const char* text = 0;
    uint32_t length = 0U;

    if (!json || !output) return ERR_NULL;
    update_github_skip_space(json);
    if (json->offset + sizeof(true_text) - 1U <= json->length) {
        text = true_text;
        length = sizeof(true_text) - 1U;
        for (uint32_t index = 0U; index < length; index++) {
            if (json->data[json->offset + index] != text[index]) {
                text = 0;
                break;
            }
        }
        if (text) {
            json->offset += length;
            *output = 1U;
            return OK;
        }
    }
    if (json->offset + sizeof(false_text) - 1U > json->length) {
        return ERR_INVALID;
    }
    for (uint32_t index = 0U; index < sizeof(false_text) - 1U; index++) {
        if (json->data[json->offset + index] != false_text[index]) {
            return ERR_INVALID;
        }
    }
    json->offset += sizeof(false_text) - 1U;
    *output = 0U;
    return OK;
}

static int update_github_parse_u32(update_github_json_t* json,
                                   uint32_t* output) {
    uint32_t value = 0U;
    uint8_t digits = 0U;

    if (!json || !output) return ERR_NULL;
    update_github_skip_space(json);
    if (json->offset < json->length && json->data[json->offset] == '0') {
        json->offset++;
        digits = 1U;
        if (json->offset < json->length &&
            json->data[json->offset] >= '0' &&
            json->data[json->offset] <= '9') return ERR_INVALID;
    } else {
        while (json->offset < json->length) {
            uint8_t raw = json->data[json->offset];
            uint32_t digit;

            if (raw < '0' || raw > '9') break;
            digit = (uint32_t)(raw - '0');
            if (value > (0xFFFFFFFFU - digit) / 10U) return ERR_OVERFLOW;
            value = value * 10U + digit;
            json->offset++;
            digits = 1U;
        }
    }
    if (!digits) return ERR_INVALID;
    *output = value;
    return OK;
}

static int update_github_parse_number_text(update_github_json_t* json,
                                           char* output, uint32_t capacity) {
    uint32_t start;
    uint32_t length;

    if (!json || !output || !capacity) return ERR_NULL;
    update_github_skip_space(json);
    start = json->offset;
    if (json->offset < json->length && json->data[json->offset] == '-') {
        json->offset++;
    }
    while (json->offset < json->length &&
           json->data[json->offset] >= '0' &&
           json->data[json->offset] <= '9') json->offset++;
    length = json->offset - start;
    if (!length || length + 1U > capacity ||
        (length > 1U && json->data[start] == '0')) return ERR_INVALID;
    for (uint32_t index = 0U; index < length; index++) {
        uint8_t value = json->data[start + index];

        if (value < '0' || value > '9') return ERR_INVALID;
        output[index] = (char)value;
    }
    output[length] = '\0';
    return OK;
}

static int update_github_skip_string(update_github_json_t* json) {
    if (!json) return ERR_NULL;
    update_github_skip_space(json);
    if (json->offset >= json->length || json->data[json->offset++] != '"') {
        return ERR_INVALID;
    }
    while (json->offset < json->length) {
        uint8_t value = json->data[json->offset++];

        if (value == '"') return OK;
        if (value == '\\') {
            if (json->offset >= json->length) return ERR_INVALID;
            value = json->data[json->offset++];
            if (value != '"' && value != '\\' && value != '/' &&
                value != 'b' && value != 'f' && value != 'n' &&
                value != 'r' && value != 't' && value != 'u') return ERR_INVALID;
            if (value == 'u') {
                if (json->offset + 4U > json->length) return ERR_INVALID;
                for (uint32_t index = 0U; index < 4U; index++) {
                    if (update_github_hex_digit(
                            json->data[json->offset + index]) < 0) {
                        return ERR_INVALID;
                    }
                }
                json->offset += 4U;
            }
        } else if (value < 0x20U) {
            return ERR_INVALID;
        }
    }
    return ERR_INVALID;
}

static int update_github_skip_number(update_github_json_t* json) {
    uint8_t digits = 0U;

    if (!json) return ERR_NULL;
    update_github_skip_space(json);
    if (json->offset < json->length && json->data[json->offset] == '-') {
        json->offset++;
    }
    if (json->offset < json->length && json->data[json->offset] == '0') {
        json->offset++;
        digits = 1U;
        if (json->offset < json->length &&
            json->data[json->offset] >= '0' &&
            json->data[json->offset] <= '9') return ERR_INVALID;
    } else {
        while (json->offset < json->length &&
               json->data[json->offset] >= '0' &&
               json->data[json->offset] <= '9') {
            json->offset++;
            digits = 1U;
        }
    }
    if (!digits) return ERR_INVALID;
    if (json->offset < json->length && json->data[json->offset] == '.') {
        uint8_t fraction = 0U;

        json->offset++;
        while (json->offset < json->length &&
               json->data[json->offset] >= '0' &&
               json->data[json->offset] <= '9') {
            json->offset++;
            fraction = 1U;
        }
        if (!fraction) return ERR_INVALID;
    }
    if (json->offset < json->length &&
        (json->data[json->offset] == 'e' ||
         json->data[json->offset] == 'E')) {
        uint8_t exponent = 0U;

        json->offset++;
        if (json->offset < json->length &&
            (json->data[json->offset] == '+' ||
             json->data[json->offset] == '-')) json->offset++;
        while (json->offset < json->length &&
               json->data[json->offset] >= '0' &&
               json->data[json->offset] <= '9') {
            json->offset++;
            exponent = 1U;
        }
        if (!exponent) return ERR_INVALID;
    }
    return OK;
}

static int update_github_skip_value(update_github_json_t* json, uint8_t depth);

static int update_github_skip_object(update_github_json_t* json,
                                     uint8_t depth) {
    char key[UPDATE_GITHUB_KEY_SIZE];
    int result;

    if (depth > UPDATE_GITHUB_JSON_DEPTH) return ERR_OVERFLOW;
    result = update_github_expect(json, '{');
    if (result != OK) return result;
    update_github_skip_space(json);
    if (json->offset < json->length && json->data[json->offset] == '}') {
        json->offset++;
        return OK;
    }
    while (1) {
        result = update_github_parse_string(json, key, sizeof(key));
        if (result == OK) result = update_github_expect(json, ':');
        if (result == OK) result = update_github_skip_value(json, depth + 1U);
        if (result != OK) return result;
        update_github_skip_space(json);
        if (json->offset >= json->length) return ERR_INVALID;
        if (json->data[json->offset] == '}') {
            json->offset++;
            return OK;
        }
        if (update_github_expect(json, ',') != OK) return ERR_INVALID;
    }
}

static int update_github_skip_array(update_github_json_t* json,
                                    uint8_t depth) {
    int result;

    if (depth > UPDATE_GITHUB_JSON_DEPTH) return ERR_OVERFLOW;
    result = update_github_expect(json, '[');
    if (result != OK) return result;
    update_github_skip_space(json);
    if (json->offset < json->length && json->data[json->offset] == ']') {
        json->offset++;
        return OK;
    }
    while (1) {
        result = update_github_skip_value(json, depth + 1U);
        if (result != OK) return result;
        update_github_skip_space(json);
        if (json->offset >= json->length) return ERR_INVALID;
        if (json->data[json->offset] == ']') {
            json->offset++;
            return OK;
        }
        if (update_github_expect(json, ',') != OK) return ERR_INVALID;
    }
}

static int update_github_skip_value(update_github_json_t* json, uint8_t depth) {
    uint8_t boolean;

    if (!json) return ERR_NULL;
    update_github_skip_space(json);
    if (json->offset >= json->length) return ERR_INVALID;
    if (json->data[json->offset] == '"') {
        return update_github_skip_string(json);
    }
    if (json->data[json->offset] == '{') {
        return update_github_skip_object(json, depth);
    }
    if (json->data[json->offset] == '[') {
        return update_github_skip_array(json, depth);
    }
    if (json->data[json->offset] == 't' || json->data[json->offset] == 'f') {
        return update_github_parse_bool(json, &boolean);
    }
    if (json->data[json->offset] == 'n') {
        return update_github_parse_null(json);
    }
    if (json->data[json->offset] == '-' ||
        (json->data[json->offset] >= '0' &&
         json->data[json->offset] <= '9')) {
        return update_github_skip_number(json);
    }
    return ERR_INVALID;
}

static int update_github_parse_digest(const char* text,
                                      uint8_t digest[32]) {
    if (!text || !digest || kstrlen(text) != UPDATE_GITHUB_DIGEST_TEXT_SIZE - 1U ||
        text[0] != 's' || text[1] != 'h' || text[2] != 'a' ||
        text[3] != '2' || text[4] != '5' || text[5] != '6' || text[6] != ':') {
        return ERR_INVALID;
    }
    for (uint32_t index = 0U; index < 32U; index++) {
        char high = text[7U + index * 2U];
        char low = text[8U + index * 2U];
        int high_value = high >= '0' && high <= '9' ? high - '0' :
                         high >= 'a' && high <= 'f' ? high - 'a' + 10 : -1;
        int low_value = low >= '0' && low <= '9' ? low - '0' :
                        low >= 'a' && low <= 'f' ? low - 'a' + 10 : -1;

        if (high_value < 0 || low_value < 0) return ERR_INVALID;
        digest[index] = (uint8_t)((high_value << 4) | low_value);
    }
    return OK;
}

static int update_github_parse_asset(update_github_json_t* json,
                                     update_github_asset_parse_t* parsed) {
    char key[UPDATE_GITHUB_KEY_SIZE];
    char digest_text[UPDATE_GITHUB_DIGEST_TEXT_SIZE];
    int result;

    if (!json || !parsed) return ERR_NULL;
    kmemset(parsed, 0, sizeof(*parsed));
    result = update_github_expect(json, '{');
    if (result != OK) return result;
    update_github_skip_space(json);
    if (json->offset < json->length && json->data[json->offset] == '}') {
        json->offset++;
        return ERR_INVALID;
    }
    while (1) {
        result = update_github_parse_string(json, key, sizeof(key));
        if (result == OK) result = update_github_expect(json, ':');
        if (result != OK) return result;
        if (kstrcmp(key, "name") == 0) {
            if (parsed->name_found ||
                update_github_parse_string(json, parsed->asset.name,
                                           sizeof(parsed->asset.name)) != OK) {
                return ERR_INVALID;
            }
            parsed->name_found = 1U;
        } else if (kstrcmp(key, "size") == 0) {
            if (parsed->size_found ||
                update_github_parse_u32(json, &parsed->asset.size) != OK) {
                return ERR_INVALID;
            }
            parsed->size_found = 1U;
        } else if (kstrcmp(key, "browser_download_url") == 0) {
            if (parsed->url_found ||
                update_github_parse_string(json, parsed->asset.url,
                                           sizeof(parsed->asset.url)) != OK) {
                return ERR_INVALID;
            }
            parsed->url_found = 1U;
        } else if (kstrcmp(key, "state") == 0) {
            if (parsed->state_found ||
                update_github_parse_string(json, parsed->state,
                                           sizeof(parsed->state)) != OK) {
                return ERR_INVALID;
            }
            parsed->state_found = 1U;
        } else if (kstrcmp(key, "digest") == 0) {
            if (parsed->digest_found) return ERR_INVALID;
            digest_text[0] = '\0';
            update_github_skip_space(json);
            if (json->offset < json->length && json->data[json->offset] == 'n') {
                result = update_github_parse_null(json);
            } else {
                result = update_github_parse_string(
                    json, digest_text, sizeof(digest_text));
                if (result == OK) result = update_github_parse_digest(
                    digest_text, parsed->asset.digest);
            }
            if (result != OK) return result;
            parsed->digest_found = 1U;
            parsed->asset.digest_present = (uint8_t)(digest_text[0] != '\0');
        } else {
            result = update_github_skip_value(json, 1U);
        }
        if (result != OK) return result;
        update_github_skip_space(json);
        if (json->offset >= json->length) return ERR_INVALID;
        if (json->data[json->offset] == '}') {
            json->offset++;
            break;
        }
        if (update_github_expect(json, ',') != OK) return ERR_INVALID;
    }
    if (!parsed->name_found || !parsed->size_found || !parsed->url_found ||
        (parsed->state_found && kstrcmp(parsed->state, "uploaded") != 0)) {
        return ERR_INVALID;
    }
    return OK;
}

static int update_github_copy_asset(update_remote_github_asset_t* destination,
                                    const update_github_asset_parse_t* source) {
    if (!destination || !source) return ERR_NULL;
    *destination = source->asset;
    return OK;
}

static int update_github_assign_asset(update_remote_github_release_t* release,
                                      const update_github_asset_parse_t* parsed,
                                      uint8_t* descriptor_found,
                                      uint8_t* manifest_found,
                                      uint8_t* package_found,
                                      uint8_t* system_found,
                                      uint8_t* runtime_manifest_found,
                                      uint8_t* runtime_package_found,
                                      uint8_t runtime_only) {
    if (!release || !parsed || !descriptor_found || !manifest_found ||
        !package_found || !system_found || !runtime_manifest_found ||
        !runtime_package_found) return ERR_NULL;
    if (kstrcmp(parsed->asset.name, UPDATE_REMOTE_GITHUB_DESCRIPTOR_NAME) == 0) {
        if (*descriptor_found) return ERR_INVALID;
        *descriptor_found = 1U;
        return update_github_copy_asset(&release->descriptor, parsed);
    }
    if (kstrcmp(parsed->asset.name, UPDATE_REMOTE_GITHUB_MANIFEST_NAME) == 0) {
        if (*manifest_found) return ERR_INVALID;
        *manifest_found = 1U;
        return update_github_copy_asset(&release->manifest, parsed);
    }
    if (kstrcmp(parsed->asset.name, UPDATE_REMOTE_GITHUB_PACKAGE_NAME) == 0) {
        if (*package_found) return ERR_INVALID;
        *package_found = 1U;
        return update_github_copy_asset(&release->package, parsed);
    }
    if (!runtime_only &&
        kstrcmp(parsed->asset.name, UPDATE_REMOTE_GITHUB_SYSTEM_NAME) == 0) {
        if (*system_found) return ERR_INVALID;
        *system_found = 1U;
        release->system_present = 1U;
        return update_github_copy_asset(&release->system, parsed);
    }
    if (runtime_only && kstrcmp(parsed->asset.name,
                                UPDATE_REMOTE_GITHUB_RUNTIME_MANIFEST_NAME) == 0) {
        if (*runtime_manifest_found) return ERR_INVALID;
        *runtime_manifest_found = 1U;
        return update_github_copy_asset(&release->runtime_manifest, parsed);
    }
    if (runtime_only && kstrcmp(parsed->asset.name,
                                UPDATE_REMOTE_GITHUB_RUNTIME_PACKAGE_NAME) == 0) {
        if (*runtime_package_found) return ERR_INVALID;
        *runtime_package_found = 1U;
        return update_github_copy_asset(&release->runtime_package, parsed);
    }
    if (!runtime_only ||
        (kstrcmp(parsed->asset.name, "EXPLORER.BMP") != 0 &&
         kstrcmp(parsed->asset.name, "SHELL.BMP") != 0 &&
         kstrcmp(parsed->asset.name, "TASKMGR.BMP") != 0)) return OK;
    if (release->runtime_asset_count >= UPDATE_REMOTE_GITHUB_RUNTIME_ASSET_MAX) {
        return ERR_OVERFLOW;
    }
    for (uint32_t index = 0U; index < release->runtime_asset_count; index++) {
        if (kstrcmp(release->runtime_assets[index].name,
                    parsed->asset.name) == 0) return ERR_INVALID;
    }
    release->runtime_assets[release->runtime_asset_count++] = parsed->asset;
    return OK;
}

static int update_github_parse_assets(update_github_json_t* json,
                                      update_remote_github_release_t* release,
                                      uint8_t* descriptor_found,
                                      uint8_t* manifest_found,
                                      uint8_t* package_found,
                                      uint8_t* system_found,
                                      uint8_t* runtime_manifest_found,
                                      uint8_t* runtime_package_found,
                                      uint8_t runtime_only) {
    update_github_asset_parse_t* parsed =
        &update_github_workspace.asset_parse;
    int result;

    result = update_github_expect(json, '[');
    if (result != OK) return result;
    update_github_skip_space(json);
    if (json->offset < json->length && json->data[json->offset] == ']') {
        json->offset++;
        return ERR_INVALID;
    }
    while (1) {
        result = update_github_parse_asset(json, parsed);
        if (result == OK) result = update_github_assign_asset(
            release, parsed, descriptor_found, manifest_found, package_found,
            system_found, runtime_manifest_found, runtime_package_found,
            runtime_only);
        if (result != OK) return result;
        update_github_skip_space(json);
        if (json->offset >= json->length) return ERR_INVALID;
        if (json->data[json->offset] == ']') {
            json->offset++;
            return OK;
        }
        if (update_github_expect(json, ',') != OK) return ERR_INVALID;
    }
}

static int update_github_validate_published_at(const char* text) {
    uint32_t month;
    uint32_t day;
    uint32_t hour;
    uint32_t minute;
    uint32_t second;

    if (!text || kstrlen(text) != UPDATE_GITHUB_PUBLISHED_TEXT_SIZE ||
        text[4] != '-' ||
        text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
        text[16] != ':' || text[19] != 'Z') return ERR_INVALID;
    for (uint32_t index = 0U;
         index < UPDATE_GITHUB_PUBLISHED_TEXT_SIZE; index++) {
        if (index == 4U || index == 7U || index == 10U ||
            index == 13U || index == 16U || index == 19U) continue;
        if (text[index] < '0' || text[index] > '9') return ERR_INVALID;
    }
    month = (uint32_t)(text[5] - '0') * 10U + (uint32_t)(text[6] - '0');
    day = (uint32_t)(text[8] - '0') * 10U + (uint32_t)(text[9] - '0');
    hour = (uint32_t)(text[11] - '0') * 10U + (uint32_t)(text[12] - '0');
    minute = (uint32_t)(text[14] - '0') * 10U + (uint32_t)(text[15] - '0');
    second = (uint32_t)(text[17] - '0') * 10U + (uint32_t)(text[18] - '0');
    return month >= 1U && month <= 12U && day >= 1U && day <= 31U &&
           hour < 24U && minute < 60U && second <= 60U ? OK : ERR_INVALID;
}

static int update_github_parse_release(const uint8_t* data, uint32_t length,
                                       const char* requested_tag,
                                       uint8_t runtime_only,
                                       update_remote_github_release_t* release) {
    update_github_json_t json;
    char key[UPDATE_GITHUB_KEY_SIZE];
    uint8_t tag_found = 0U;
    uint8_t id_found = 0U;
    uint8_t name_found = 0U;
    uint8_t published_found = 0U;
    uint8_t draft_found = 0U;
    uint8_t prerelease_found = 0U;
    uint8_t assets_found = 0U;
    uint8_t descriptor_found = 0U;
    uint8_t manifest_found = 0U;
    uint8_t package_found = 0U;
    uint8_t system_found = 0U;
    uint8_t runtime_manifest_found = 0U;
    uint8_t runtime_package_found = 0U;
    uint8_t boolean;
    int result;

    if (!data || !length || !requested_tag || !release) return ERR_NULL;
    kmemset(release, 0, sizeof(*release));
    json.data = data;
    json.length = length;
    json.offset = 0U;
    json.depth = 0U;
    result = update_github_expect(&json, '{');
    if (result != OK) return result;
    update_github_skip_space(&json);
    if (json.offset < json.length && json.data[json.offset] == '}') return ERR_INVALID;
    while (1) {
        result = update_github_parse_string(&json, key, sizeof(key));
        if (result == OK) result = update_github_expect(&json, ':');
        if (result != OK) return result;
        if (kstrcmp(key, "tag_name") == 0) {
            if (tag_found || update_github_parse_string(
                    &json, release->tag, sizeof(release->tag)) != OK) return ERR_INVALID;
            tag_found = 1U;
        } else if (kstrcmp(key, "id") == 0) {
            if (id_found || update_github_parse_number_text(
                    &json, release->release_id, sizeof(release->release_id)) != OK) {
                return ERR_INVALID;
            }
            id_found = 1U;
        } else if (kstrcmp(key, "name") == 0) {
            if (name_found) return ERR_INVALID;
            update_github_skip_space(&json);
            if (json.offset < json.length && json.data[json.offset] == 'n') {
                result = update_github_parse_null(&json);
                release->release_name[0] = '\0';
            } else {
                result = update_github_parse_string(
                    &json, release->release_name, sizeof(release->release_name));
            }
            if (result != OK) return result;
            name_found = 1U;
        } else if (kstrcmp(key, "published_at") == 0) {
            if (published_found || update_github_parse_string(
                    &json, release->published_at,
                    sizeof(release->published_at)) != OK) return ERR_INVALID;
            published_found = 1U;
        } else if (kstrcmp(key, "draft") == 0) {
            if (draft_found || update_github_parse_bool(&json, &boolean) != OK ||
                boolean) return ERR_INVALID;
            draft_found = 1U;
        } else if (kstrcmp(key, "prerelease") == 0) {
            if (prerelease_found ||
                update_github_parse_bool(&json, &boolean) != OK || boolean) {
                return ERR_INVALID;
            }
            prerelease_found = 1U;
        } else if (kstrcmp(key, "assets") == 0) {
            if (assets_found || update_github_parse_assets(
                    &json, release, &descriptor_found, &manifest_found,
                    &package_found, &system_found, &runtime_manifest_found,
                    &runtime_package_found, runtime_only) != OK) return ERR_INVALID;
            assets_found = 1U;
        } else {
            result = update_github_skip_value(&json, 1U);
            if (result != OK) return result;
        }
        update_github_skip_space(&json);
        if (json.offset >= json.length) return ERR_INVALID;
        if (json.data[json.offset] == '}') {
            json.offset++;
            break;
        }
        if (update_github_expect(&json, ',') != OK) return ERR_INVALID;
    }
    update_github_skip_space(&json);
    if (json.offset != json.length || !tag_found || !id_found || !name_found ||
        !published_found || update_github_validate_published_at(
            release->published_at) != OK || !draft_found ||
        !prerelease_found || !assets_found || kstrcmp(release->tag, requested_tag) != 0) {
        return ERR_INVALID;
    }
    if (runtime_only) {
        if (!runtime_manifest_found || !runtime_package_found ||
            release->runtime_manifest.size != UPDATE_RUNTIME_MANIFEST_SIZE ||
            release->runtime_package.size == 0U ||
            release->runtime_package.size > UPDATE_RUNTIME_PACKAGE_MAX_SIZE ||
            !release->runtime_manifest.url[0] ||
            !release->runtime_package.url[0]) return ERR_INVALID;
    } else {
        if (!descriptor_found || !manifest_found || !package_found ||
            release->descriptor.size == 0U ||
            release->descriptor.size > HTTP_BODY_CAPACITY ||
            release->manifest.size != UPDATE_REMOTE_MANIFEST_SIZE ||
            release->package.size == 0U ||
            release->package.size > ZUPD_MAX_TOTAL_SIZE ||
            !release->descriptor.url[0] || !release->manifest.url[0] ||
            !release->package.url[0]) return ERR_INVALID;
        if (release->system_present &&
            (release->system.size < UPDATE_SYSTEM_HEADER_SIZE ||
             release->system.size > UPDATE_SYSTEM_HEADER_SIZE +
                 UPDATE_SYSTEM_MAX_IMAGE_SIZE ||
             !release->system.url[0])) return ERR_INVALID;
    }
    if (release->release_id[0] == '0' && release->release_id[1] == '\0') {
        return ERR_INVALID;
    }
    return OK;
}

static uint8_t update_github_ascii_equal(char first, char second) {
    if (first >= 'A' && first <= 'Z') first = (char)(first + 'a' - 'A');
    if (second >= 'A' && second <= 'Z') second = (char)(second + 'a' - 'A');
    return (uint8_t)(first == second);
}

static int update_github_host(const char* url, char* output, uint32_t capacity) {
    const char* start;
    uint32_t length = 0U;

    if (!url || !output || !capacity || kstrlen(url) < 9U ||
        url[0] != 'h' || url[1] != 't' || url[2] != 't' || url[3] != 'p' ||
        url[4] != 's' || url[5] != ':' || url[6] != '/' || url[7] != '/') {
        return ERR_INVALID;
    }
    start = url + 8U;
    while (start[length] && start[length] != '/' && start[length] != ':') {
        if (start[length] == '@' || (uint8_t)start[length] <= 0x20U) {
            return ERR_INVALID;
        }
        length++;
    }
    if (!length || length + 1U > capacity) return ERR_INVALID;
    for (uint32_t index = 0U; index < length; index++) {
        output[index] = start[index];
    }
    output[length] = '\0';
    return OK;
}

static int update_github_port(const char* url, uint16_t* output) {
    const char* cursor;
    const char* colon = 0;
    uint32_t value = 0U;

    if (!url || !output || kstrlen(url) < 9U) return ERR_INVALID;
    cursor = url + 8U;
    while (*cursor && *cursor != '/') {
        if (*cursor == ':') {
            if (colon) return ERR_INVALID;
            colon = cursor;
        }
        cursor++;
    }
    if (!colon) {
        *output = HTTPS_DEFAULT_PORT;
        return OK;
    }
    if (colon + 1 == cursor) return ERR_INVALID;
    for (const char* digit = colon + 1; digit < cursor; digit++) {
        uint32_t value_digit;

        if (*digit < '0' || *digit > '9') return ERR_INVALID;
        value_digit = (uint32_t)(*digit - '0');
        if (value > (UPDATE_GITHUB_PORT_MAX - value_digit) / 10U) {
            return ERR_INVALID;
        }
        value = value * 10U + value_digit;
    }
    if (!value) return ERR_INVALID;
    *output = (uint16_t)value;
    return OK;
}

static uint8_t update_github_host_equal(const char* first, const char* second) {
    uint32_t first_length;
    uint32_t second_length;

    if (!first || !second) return 0U;
    first_length = kstrlen(first);
    second_length = kstrlen(second);
    if (first_length != second_length) return 0U;
    for (uint32_t index = 0U; index < first_length; index++) {
        if (!update_github_ascii_equal(first[index], second[index])) return 0U;
    }
    return 1U;
}

static int update_github_validate_asset_url(const char* url) {
    uint16_t port;
    uint16_t api_port;

    update_github_workspace.host[0] = '\0';
    update_github_workspace.api_host[0] = '\0';
    if (update_github_host(url, update_github_workspace.host,
                           sizeof(update_github_workspace.host)) != OK ||
        update_github_host(UPDATE_REMOTE_GITHUB_API_URL,
                           update_github_workspace.api_host,
                           sizeof(update_github_workspace.api_host)) != OK ||
        update_github_port(url, &port) != OK ||
        update_github_port(UPDATE_REMOTE_GITHUB_API_URL,
                           &api_port) != OK) return ERR_INVALID;
    if (update_github_host_equal(update_github_workspace.host,
                                 update_github_workspace.api_host)) {
        if (port != api_port) return ERR_INVALID;
    } else if (port != HTTPS_DEFAULT_PORT) {
        return ERR_INVALID;
    }
    if (!update_github_host_equal(update_github_workspace.host,
                                  update_github_workspace.api_host) &&
        !update_github_host_equal(update_github_workspace.host, "github.com") &&
        !update_github_host_equal(update_github_workspace.host,
                                  "objects.githubusercontent.com") &&
        !update_github_host_equal(update_github_workspace.host,
                                  "release-assets.githubusercontent.com")) {
        return ERR_INVALID;
    }
    return OK;
}

static int update_github_append(uint8_t* output, uint32_t capacity,
                                uint32_t* length, const char* text) {
    uint32_t text_length;

    if (!output || !length || !text) return ERR_NULL;
    text_length = kstrlen(text);
    if (*length + text_length >= capacity) return ERR_OVERFLOW;
    kmemcpy(output + *length, text, text_length);
    *length += text_length;
    output[*length] = '\0';
    return OK;
}

static int update_github_append_u32(uint8_t* output, uint32_t capacity,
                                    uint32_t* length, uint32_t value) {
    char digits[11];
    uint8_t count = 0U;

    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value);
    for (uint8_t index = 0U; index < count / 2U; index++) {
        char swap = digits[index];
        digits[index] = digits[count - index - 1U];
        digits[count - index - 1U] = swap;
    }
    for (uint8_t index = 0U; index < count; index++) {
        char text[2] = {digits[index], '\0'};
        if (update_github_append(output, capacity, length, text) != OK) {
            return ERR_OVERFLOW;
        }
    }
    return OK;
}

static int update_github_append_digest(uint8_t* output, uint32_t capacity,
                                       uint32_t* length,
                                       const update_remote_github_asset_t* asset) {
    static const char hex[] = "0123456789abcdef";

    if (!asset) return ERR_NULL;
    if (!asset->digest_present) return update_github_append(
        output, capacity, length, "-");
    for (uint32_t index = 0U; index < 32U; index++) {
        char pair[3] = {hex[asset->digest[index] >> 4U],
                        hex[asset->digest[index] & 0x0FU], '\0'};
        if (update_github_append(output, capacity, length, pair) != OK) {
            return ERR_OVERFLOW;
        }
    }
    return OK;
}

static uint8_t update_github_marker_match(const char* source,
                                          const char* marker) {
    if (!source || !marker) return 0U;
    for (uint32_t index = 0U; marker[index]; index++) {
        if (!source[index] || source[index] != marker[index]) return 0U;
    }
    return 1U;
}

static int update_github_fingerprint(update_remote_github_release_t* release) {
    uint32_t length = 0U;
    const update_remote_github_asset_t* assets[4];
    uint32_t asset_count;

    if (!release) return ERR_NULL;
    assets[0] = &release->descriptor;
    assets[1] = &release->manifest;
    assets[2] = &release->package;
    asset_count = 3U;
    if (release->system_present) assets[asset_count++] = &release->system;
    kmemset(update_github_workspace.fingerprint, 0,
            sizeof(update_github_workspace.fingerprint));
    if (update_github_append(update_github_workspace.fingerprint,
                             sizeof(update_github_workspace.fingerprint),
                             &length, release->tag) != OK ||
        update_github_append(update_github_workspace.fingerprint,
                             sizeof(update_github_workspace.fingerprint),
                             &length, "\n") != OK ||
        update_github_append(update_github_workspace.fingerprint,
                             sizeof(update_github_workspace.fingerprint),
                             &length, release->release_id) != OK ||
        update_github_append(update_github_workspace.fingerprint,
                             sizeof(update_github_workspace.fingerprint),
                             &length, "\n") != OK ||
        update_github_append(update_github_workspace.fingerprint,
                             sizeof(update_github_workspace.fingerprint),
                             &length, release->release_name) != OK ||
        update_github_append(update_github_workspace.fingerprint,
                             sizeof(update_github_workspace.fingerprint),
                             &length, "\n") != OK ||
        update_github_append(update_github_workspace.fingerprint,
                             sizeof(update_github_workspace.fingerprint),
                             &length, release->published_at) != OK) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < asset_count; index++) {
        if (update_github_append(update_github_workspace.fingerprint,
                                 sizeof(update_github_workspace.fingerprint),
                                 &length, "\n") != OK ||
            update_github_append(update_github_workspace.fingerprint,
                                 sizeof(update_github_workspace.fingerprint),
                                 &length, assets[index]->name) != OK ||
            update_github_append(update_github_workspace.fingerprint,
                                 sizeof(update_github_workspace.fingerprint),
                                 &length, "|") != OK ||
            update_github_append_u32(update_github_workspace.fingerprint,
                                     sizeof(update_github_workspace.fingerprint),
                                     &length, assets[index]->size) != OK ||
            update_github_append(update_github_workspace.fingerprint,
                                 sizeof(update_github_workspace.fingerprint),
                                 &length, "|") != OK ||
            update_github_append(update_github_workspace.fingerprint,
                                 sizeof(update_github_workspace.fingerprint),
                                 &length, assets[index]->url) != OK ||
            update_github_append(update_github_workspace.fingerprint,
                                 sizeof(update_github_workspace.fingerprint),
                                 &length, "|") != OK ||
            update_github_append_digest(update_github_workspace.fingerprint,
                                        sizeof(update_github_workspace.fingerprint),
                                        &length, assets[index]) != OK) return ERR_OVERFLOW;
    }
    return crypto_sha256(update_github_workspace.fingerprint, length,
                         release->metadata_hash);
}

static int update_github_runtime_fingerprint(
    update_remote_github_release_t* release) {
    const update_remote_github_asset_t* assets[
        UPDATE_REMOTE_GITHUB_RUNTIME_ASSET_MAX + 2U];
    uint32_t asset_count;
    uint32_t length = 0U;

    if (!release) {
        return ERR_NULL;
    }
    asset_count = 2U + release->runtime_asset_count;
    assets[0] = &release->runtime_manifest;
    assets[1] = &release->runtime_package;
    for (uint32_t index = 0U; index < release->runtime_asset_count; index++) {
        assets[index + 2U] = &release->runtime_assets[index];
    }
    kmemset(update_github_workspace.fingerprint, 0,
            sizeof(update_github_workspace.fingerprint));
    if (update_github_append(update_github_workspace.fingerprint,
                             sizeof(update_github_workspace.fingerprint),
                             &length, release->tag) != OK ||
        update_github_append(update_github_workspace.fingerprint,
                             sizeof(update_github_workspace.fingerprint),
                             &length, "\n") != OK ||
        update_github_append(update_github_workspace.fingerprint,
                             sizeof(update_github_workspace.fingerprint),
                             &length, release->release_id) != OK ||
        update_github_append(update_github_workspace.fingerprint,
                             sizeof(update_github_workspace.fingerprint),
                             &length, "\n") != OK ||
        update_github_append(update_github_workspace.fingerprint,
                             sizeof(update_github_workspace.fingerprint),
                             &length, release->release_name) != OK ||
        update_github_append(update_github_workspace.fingerprint,
                             sizeof(update_github_workspace.fingerprint),
                             &length, "\n") != OK ||
        update_github_append(update_github_workspace.fingerprint,
                             sizeof(update_github_workspace.fingerprint),
                             &length, release->published_at) != OK) {
        return ERR_OVERFLOW;
    }
    for (uint32_t index = 0U; index < asset_count; index++) {
        if (update_github_append(update_github_workspace.fingerprint,
                                 sizeof(update_github_workspace.fingerprint),
                                 &length, "\n") != OK ||
            update_github_append(update_github_workspace.fingerprint,
                                 sizeof(update_github_workspace.fingerprint),
                                 &length, assets[index]->name) != OK ||
            update_github_append(update_github_workspace.fingerprint,
                                 sizeof(update_github_workspace.fingerprint),
                                 &length, "|") != OK ||
            update_github_append_u32(update_github_workspace.fingerprint,
                                     sizeof(update_github_workspace.fingerprint),
                                     &length, assets[index]->size) != OK ||
            update_github_append(update_github_workspace.fingerprint,
                                 sizeof(update_github_workspace.fingerprint),
                                 &length, "|") != OK ||
            update_github_append(update_github_workspace.fingerprint,
                                 sizeof(update_github_workspace.fingerprint),
                                 &length, assets[index]->url) != OK ||
            update_github_append(update_github_workspace.fingerprint,
                                 sizeof(update_github_workspace.fingerprint),
                                 &length, "|") != OK ||
            update_github_append_digest(update_github_workspace.fingerprint,
                                        sizeof(update_github_workspace.fingerprint),
                                        &length, assets[index]) != OK) {
            return ERR_OVERFLOW;
        }
    }
    return crypto_sha256(update_github_workspace.fingerprint, length,
                         release->metadata_hash);
}

static int update_github_build_url(const char* tag, char* output,
                                   uint32_t capacity) {
    const char* source = UPDATE_REMOTE_GITHUB_RELEASE_URL_TEMPLATE;
    const char* markers[] = {"{owner}", "{repo}", "{tag}"};
    const char* values[] = {UPDATE_REMOTE_GITHUB_OWNER,
                            UPDATE_REMOTE_GITHUB_REPOSITORY, tag};
    uint32_t length = 0U;
    uint32_t opening_braces = 0U;
    uint32_t closing_braces = 0U;

    if (!tag || !output || !capacity) return ERR_NULL;
    for (uint32_t index = 0U; source[index]; index++) {
        if (source[index] == '{') opening_braces++;
        if (source[index] == '}') closing_braces++;
    }
    if (opening_braces != 3U || closing_braces != 3U) return ERR_INVALID;
    for (uint32_t marker = 0U; marker < sizeof(markers) / sizeof(markers[0]); marker++) {
        uint32_t count = 0U;
        const char* scan = source;
        uint32_t marker_length = kstrlen(markers[marker]);

        while (*scan) {
            if (update_github_marker_match(scan, markers[marker])) {
                count++;
                scan += marker_length;
            } else {
                scan++;
            }
        }
        if (count != 1U) return ERR_INVALID;
    }
    for (uint32_t index = 0U; source[index]; index++) {
        if (source[index] == '{') {
            uint8_t known = 0U;
            uint32_t marker_length = 0U;

            for (uint32_t marker = 0U; marker < sizeof(markers) / sizeof(markers[0]); marker++) {
                if (update_github_marker_match(source + index,
                                               markers[marker])) {
                    known = 1U;
                    marker_length = kstrlen(markers[marker]);
                }
            }
            if (!known) return ERR_INVALID;
            index += marker_length - 1U;
        } else if (source[index] == '}') {
            return ERR_INVALID;
        }
    }
    if (update_github_append((uint8_t*)output, capacity, &length,
                             UPDATE_REMOTE_GITHUB_API_URL) != OK) return ERR_OVERFLOW;
    while (*source) {
        uint8_t replaced = 0U;

        for (uint32_t marker = 0U; marker < sizeof(markers) / sizeof(markers[0]); marker++) {
            uint32_t marker_length = kstrlen(markers[marker]);
            if (update_github_marker_match(source, markers[marker])) {
                if (update_github_append((uint8_t*)output, capacity, &length,
                                         values[marker]) != OK) return ERR_OVERFLOW;
                source += marker_length;
                replaced = 1U;
                break;
            }
        }
        if (!replaced) {
            char character[2] = {*source++, '\0'};
            if (update_github_append((uint8_t*)output, capacity, &length,
                                     character) != OK) return ERR_OVERFLOW;
        }
    }
    return update_github_validate_asset_url(output);
}

static int update_github_sink(const uint8_t* data, uint32_t size,
                              void* context) {
    uint32_t remaining;

    (void)context;
    if (!data && size) return ERR_NULL;
    remaining = UPDATE_REMOTE_GITHUB_RESPONSE_CAPACITY -
                update_github_workspace.response_length;
    if (size > remaining) return ERR_OVERFLOW;
    if (size) kmemcpy(update_github_workspace.response +
                      update_github_workspace.response_length, data, size);
    update_github_workspace.response_length += size;
    return OK;
}

static void update_github_copy_http_result(update_remote_result_t* result) {
    if (!result) return;
    result->http_status = update_github_workspace.http.status_code;
    result->bytes_received = update_github_workspace.http.body_length;
    result->secure = update_github_workspace.http.secure;
    result->tls_verified = update_github_workspace.http.tls_verified;
    result->redirect_count = update_github_workspace.http.redirect_count;
    result->tls_reason = update_github_workspace.http.tls_reason;
    result->tls_error = update_github_workspace.http.tls_error;
}

static update_remote_reason_t update_github_http_reason(void) {
    if (update_github_workspace.http.redirect_rejected) {
        return UPDATE_REMOTE_REASON_REDIRECT;
    }
    if (update_github_workspace.http.secure &&
        (!update_github_workspace.http.tls_verified ||
         update_github_workspace.http.tls_reason != TLS_REASON_NONE)) {
        return UPDATE_REMOTE_REASON_TLS;
    }
    if (update_github_workspace.http.status_code == UPDATE_GITHUB_HTTP_NOT_FOUND) {
        return UPDATE_REMOTE_REASON_RELEASE_NOT_FOUND;
    }
    return UPDATE_REMOTE_REASON_RELEASE_API;
}

static int update_github_wait(const update_remote_options_t* options) {
    while (1) {
        if (http_get_status(&update_github_workspace.http) != OK) {
            LOG_ERROR("UPDATE", "Falha ao consultar estado da API GitHub");
            return ERR_STATE;
        }
        if (update_github_workspace.http.state == HTTP_STATE_COMPLETE) return OK;
        if (update_github_workspace.http.state == HTTP_STATE_FAILED) {
            return update_github_workspace.http.last_error == OK ?
                   ERR_STATE : update_github_workspace.http.last_error;
        }
        if (options && options->cancel_check &&
            options->cancel_check(options->cancel_context)) {
            update_github_workspace.cancelled = 1U;
            http_reset();
            LOG_WARN("UPDATE", "Consulta da API GitHub cancelada");
            return ERR_TIMEOUT;
        }
        process_block(UPDATE_GITHUB_WAIT_BLOCK_TICKS);
    }
}

static int update_remote_github_query_mode(
    const char* tag, const update_remote_options_t* options,
    update_remote_github_release_t* release_out,
    update_remote_result_t* result_out, uint8_t runtime_only) {
    http_request_options_t http_options;
    int result;

    if (!tag || !release_out || !result_out) {
        LOG_ERROR("UPDATE", "Argumento nulo na consulta GitHub");
        return ERR_NULL;
    }
    kmemset(result_out, 0, sizeof(*result_out));
    kmemset(release_out, 0, sizeof(*release_out));
    kmemset(&update_github_workspace.http, 0,
            sizeof(update_github_workspace.http));
    update_github_workspace.response_length = 0U;
    update_github_workspace.cancelled = 0U;
    update_github_workspace.url[0] = '\0';
    update_github_workspace.host[0] = '\0';
    update_github_workspace.api_host[0] = '\0';
    kmemset(&update_github_workspace.asset_parse, 0,
            sizeof(update_github_workspace.asset_parse));
    result = update_github_validate_tag(tag);
    if (result != OK) {
        result_out->reason = UPDATE_REMOTE_REASON_RELEASE_TAG;
        LOG_ERROR("UPDATE", "Tag invalida na consulta GitHub");
        return result;
    }
    result = update_github_build_url(
        tag, update_github_workspace.url,
        sizeof(update_github_workspace.url));
    if (result != OK) {
        result_out->reason = UPDATE_REMOTE_REASON_RELEASE_API;
        LOG_ERROR("UPDATE", "Template da API GitHub invalido");
        return result;
    }
    http_options.accept = "application/vnd.github+json";
    http_options.api_version = UPDATE_REMOTE_GITHUB_API_VERSION;
    http_options.require_https = 1U;
    http_options.follow_redirects = 1U;
    http_options.max_redirects = HTTP_MAX_REDIRECTS;
    result = http_get_stream_start_ex(
        update_github_workspace.url, UPDATE_REMOTE_GITHUB_RESPONSE_CAPACITY,
        update_github_sink, 0, &http_options);
    if (result == OK) result = update_github_wait(options);
    update_github_copy_http_result(result_out);
    if (result != OK) {
        update_remote_reason_t reason = update_github_workspace.cancelled ?
            UPDATE_REMOTE_REASON_CANCELLED : update_github_http_reason();

        result_out->reason = reason;
        LOG_ERROR("UPDATE", "Falha no canal GitHub");
        return result;
    }
    if (update_github_workspace.http.status_code != UPDATE_GITHUB_HTTP_OK) {
        result_out->reason = update_github_http_reason();
        LOG_ERROR("UPDATE", "API GitHub retornou status recusado");
        return ERR_INVALID;
    }
    result = update_github_parse_release(
        update_github_workspace.response,
        update_github_workspace.response_length, tag, runtime_only, release_out);
    if (result == OK && runtime_only) {
        result = update_github_validate_asset_url(
            release_out->runtime_manifest.url);
        if (result == OK) result = update_github_validate_asset_url(
            release_out->runtime_package.url);
        for (uint32_t index = 0U;
             result == OK && index < release_out->runtime_asset_count; index++) {
            result = update_github_validate_asset_url(
                release_out->runtime_assets[index].url);
        }
        if (result == OK) result = update_github_runtime_fingerprint(release_out);
    } else if (result == OK) {
        result = update_github_validate_asset_url(release_out->descriptor.url);
        if (result == OK) result = update_github_validate_asset_url(
            release_out->manifest.url);
        if (result == OK) result = update_github_validate_asset_url(
            release_out->package.url);
        if (result == OK) result = update_github_fingerprint(release_out);
    }
    if (result != OK) {
        result_out->reason = UPDATE_REMOTE_REASON_RELEASE_API;
        LOG_ERROR("UPDATE", "Resposta GitHub nao possui Release segura");
        return result;
    }
    result_out->reason = UPDATE_REMOTE_REASON_NONE;
    LOG_INFO("UPDATE", "Release GitHub descoberta por tag exata");
    return OK;
}

int update_remote_github_query(
    const char* tag, const update_remote_options_t* options,
    update_remote_github_release_t* release_out,
    update_remote_result_t* result_out) {
    return update_remote_github_query_mode(tag, options, release_out,
                                           result_out, 0U);
}

int update_remote_github_runtime_query(
    const char* tag, const update_remote_options_t* options,
    update_remote_github_release_t* release_out,
    update_remote_result_t* result_out) {
    return update_remote_github_query_mode(tag, options, release_out,
                                           result_out, 1U);
}
