#include "core/update_system.h"
#include "core/crypto.h"
#include "core/log.h"
#include "core/string.h"
#include "core/update_trust.h"
#include "core/http.h"
#include "core/update_remote_config.h"
#include "core/update_remote_github.h"
#include "fs/fs.h"
#include "process/process.h"

#define UPDATE_SYSTEM_DOMAIN "ZEPHYROS-SYSTEM-IMAGE-V1\0"
#define UPDATE_SYSTEM_DOMAIN_SIZE (sizeof(UPDATE_SYSTEM_DOMAIN) - 1U)
#define UPDATE_SYSTEM_COMPONENT_HASH_OFFSET 12U
#define UPDATE_SYSTEM_COMPONENT_OFFSET_OFFSET 4U
#define UPDATE_SYSTEM_COMPONENT_SIZE_OFFSET 8U

static int update_system_initialized;
static uint8_t update_system_header[UPDATE_SYSTEM_HEADER_SIZE];
static uint8_t update_system_signed_header[UPDATE_SYSTEM_HEADER_SIZE];
static uint8_t update_system_io_buffer[UPDATE_SYSTEM_IO_BUFFER_SIZE];

typedef struct {
    uint8_t header[UPDATE_SYSTEM_HEADER_SIZE];
    uint8_t signed_header[UPDATE_SYSTEM_HEADER_SIZE];
    uint32_t expected_size;
    uint32_t received;
    uint32_t payload_received;
    uint32_t image_size;
    crypto_ed25519_verify_ctx_t signature;
    crypto_sha256_ctx_t package_sha;
    uint8_t package_hash[CRYPTO_SHA256_SIZE];
    uint8_t expected_package_hash[CRYPTO_SHA256_SIZE];
    crypto_sha256_ctx_t image_sha;
    crypto_sha256_ctx_t component_sha[UPDATE_SYSTEM_COMPONENT_COUNT];
    uint8_t component_active[UPDATE_SYSTEM_COMPONENT_COUNT];
    uint8_t crypto_ready;
    char tag[UPDATE_REMOTE_TAG_SIZE];
    update_system_verification_t result;
} update_system_remote_stream_t;

static int any_nonzero(const uint8_t* data, uint32_t size);

static int update_system_remote_copy_text(char* output, uint32_t capacity,
                                          const char* input) {
    uint32_t length;

    if (!output || !capacity || !input) return ERR_NULL;
    length = kstrlen(input);
    if (!length || length >= capacity) return ERR_OVERFLOW;
    kmemcpy(output, input, length + 1U);
    return OK;
}

static int update_system_remote_http_options(
    const update_remote_options_t* options, http_request_options_t* output) {
    if (!output) return ERR_NULL;
    kmemset(output, 0, sizeof(*output));
    if (!options) return OK;
    output->accept = options->http_accept;
    output->api_version = options->http_api_version;
    output->require_https = options->http_require_https;
    output->follow_redirects = options->http_follow_redirects;
    output->max_redirects = options->http_max_redirects;
    return OK;
}

static int update_system_remote_wait_http(
    const update_remote_options_t* options, http_status_t* status) {
    if (!status) return ERR_NULL;
    while (1) {
        if (http_get_status(status) != OK) return ERR_STATE;
        if (status->state == HTTP_STATE_COMPLETE) return OK;
        if (status->state == HTTP_STATE_FAILED) {
            return status->last_error == OK ? ERR_STATE : status->last_error;
        }
        if (options && options->cancel_check &&
            options->cancel_check(options->cancel_context)) {
            http_reset();
            return ERR_TIMEOUT;
        }
        process_block(1U);
    }
}

static int update_system_remote_contains(const uint8_t* data, uint32_t length,
                                         const char* text) {
    uint32_t text_length;

    if (!data || !length || !text) return 0;
    text_length = kstrlen(text);
    if (!text_length || text_length > length) return 0;
    for (uint32_t offset = 0U; offset + text_length <= length; offset++) {
        uint32_t index;

        for (index = 0U; index < text_length; index++) {
            if (data[offset + index] != (uint8_t)text[index]) break;
        }
        if (index == text_length) return 1;
    }
    return 0;
}

static int update_system_remote_decimal(uint32_t value, char* output,
                                        uint32_t capacity) {
    char reverse[11];
    uint32_t length = 0U;

    if (!output || capacity < 2U) return ERR_NULL;
    do {
        if (length >= sizeof(reverse)) return ERR_OVERFLOW;
        reverse[length++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value);
    if (length + 1U > capacity) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < length; index++) {
        output[index] = reverse[length - index - 1U];
    }
    output[length] = '\0';
    return OK;
}

static char update_system_hex_digit(uint8_t value) {
    return value < 10U ? (char)('0' + value) : (char)('a' + value - 10U);
}

static int update_system_remote_descriptor_matches(
    const uint8_t* data, uint32_t length,
    const update_remote_github_release_t* release) {
    char size_text[12];
    char hash_text[CRYPTO_SHA256_SIZE * 2U + 1U];
    char tag_marker[UPDATE_REMOTE_TAG_SIZE + 12U];
    static const char tag_prefix[] = "\"tag\": \"";
    uint32_t tag_length;

    if (!data || !length || !release || !release->system_present ||
        !release->system.digest_present) return ERR_NULL;
    if (!update_system_remote_contains(data, length,
                                       "zephyros-release-v2") ||
        !update_system_remote_contains(data, length, "\"legacy\"") ||
        !update_system_remote_contains(data, length, "\"runtime\"") ||
        !update_system_remote_contains(data, length, "\"compatibility\"") ||
        !update_system_remote_contains(data, length, "\"system\"") ||
        !update_system_remote_contains(data, length, "\"system.zsys\"")) {
        return ERR_INVALID;
    }
    tag_length = kstrlen(release->tag);
    if (!tag_length || tag_length + sizeof(tag_prefix) + 1U >
            sizeof(tag_marker)) return ERR_OVERFLOW;
    kmemcpy(tag_marker, tag_prefix, sizeof(tag_prefix) - 1U);
    kmemcpy(tag_marker + sizeof(tag_prefix) - 1U,
            release->tag, tag_length);
    tag_marker[sizeof(tag_prefix) - 1U + tag_length] = '"';
    tag_marker[sizeof(tag_prefix) + tag_length] = '\0';
    if (!update_system_remote_contains(data, length, tag_marker)) {
        return ERR_INVALID;
    }
    if (update_system_remote_decimal(release->system.size, size_text,
                                     sizeof(size_text)) != OK ||
        !update_system_remote_contains(data, length, size_text)) {
        return ERR_INVALID;
    }
    if (release->system.digest_present) {
        for (uint32_t index = 0U; index < CRYPTO_SHA256_SIZE; index++) {
            hash_text[index * 2U] = update_system_hex_digit(
                release->system.digest[index] >> 4U);
            hash_text[index * 2U + 1U] = update_system_hex_digit(
                release->system.digest[index] & 0x0FU);
        }
        hash_text[sizeof(hash_text) - 1U] = '\0';
        if (!update_system_remote_contains(data, length, hash_text)) {
            return ERR_INVALID;
        }
    }
    return OK;
}

static uint16_t update_system_read_u16(const uint8_t* raw, uint32_t offset) {
    return (uint16_t)raw[offset] | ((uint16_t)raw[offset + 1U] << 8U);
}

static uint32_t update_system_read_u32(const uint8_t* raw, uint32_t offset) {
    return (uint32_t)raw[offset] |
           ((uint32_t)raw[offset + 1U] << 8U) |
           ((uint32_t)raw[offset + 2U] << 16U) |
           ((uint32_t)raw[offset + 3U] << 24U);
}

static int update_system_version_compare(const update_version_t* first,
                                         const update_version_t* second) {
    if (first->major != second->major) {
        return first->major < second->major ? -1 : 1;
    }
    if (first->minor != second->minor) {
        return first->minor < second->minor ? -1 : 1;
    }
    if (first->patch != second->patch) {
        return first->patch < second->patch ? -1 : 1;
    }
    return 0;
}

static int update_system_base_equal(const update_system_base_t* base,
                                    const update_version_t* version,
                                    uint32_t epoch) {
    return base && version &&
           update_system_version_compare(&base->version, version) == 0 &&
           base->epoch == epoch;
}

static int update_system_copy_fixed_text(const uint8_t* raw, uint32_t capacity,
                                         char* output, uint32_t output_capacity) {
    uint32_t length = 0U;

    if (!raw || !output || !capacity || !output_capacity) return ERR_NULL;
    while (length < capacity && raw[length]) length++;
    if (!length || length >= output_capacity) return ERR_INVALID;
    for (uint32_t index = 0U; index < length; index++) {
        uint8_t value = raw[index];

        if (value < 0x20U ||
            !((value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '.' ||
              value == '_' || value == '-')) {
            return ERR_INVALID;
        }
        output[index] = (char)value;
    }
    output[length] = '\0';
    for (uint32_t index = length + 1U; index < capacity; index++) {
        if (raw[index]) return ERR_INVALID;
    }
    return OK;
}

static int update_system_reject(update_system_reason_t reason, int error,
                                const char* message,
                                update_system_verification_t* result_out) {
    if (result_out) result_out->reason = reason;
    if (message) LOG_ERROR_CODE("UPDATE", error, message);
    return error;
}

static int update_system_hash_range(const char* path, uint32_t offset,
                                    uint32_t size,
                                    uint8_t hash[CRYPTO_SHA256_SIZE]) {
    crypto_sha256_ctx_t context;
    uint32_t position = 0U;
    int result;

    if (!path || !hash) return ERR_NULL;
    result = crypto_sha256_init(&context);
    while (result == OK && position < size) {
        uint32_t chunk = size - position;
        uint32_t bytes = 0U;

        if (chunk > sizeof(update_system_io_buffer)) {
            chunk = sizeof(update_system_io_buffer);
        }
        result = fs_read_file_range_at(
            path, offset + position, update_system_io_buffer, chunk, &bytes);
        if (result == OK && bytes != chunk) result = ERR_DISK;
        if (result == OK) {
            result = crypto_sha256_update(
                &context, update_system_io_buffer, bytes);
            position += bytes;
        }
    }
    if (result == OK) result = crypto_sha256_final(&context, hash);
    return result;
}

static int update_system_verify_signature(const char* path, uint32_t image_size) {
    crypto_ed25519_verify_ctx_t verify;
    uint32_t position = 0U;
    int result;

    kmemcpy(update_system_signed_header, update_system_header,
            UPDATE_SYSTEM_HEADER_SIZE);
    kmemset(update_system_signed_header + UPDATE_SYSTEM_SIGNATURE_OFFSET,
            0, UPDATE_SYSTEM_SIGNATURE_SIZE);
    result = crypto_ed25519_verify_init(
        &verify,
        update_system_header + UPDATE_SYSTEM_SIGNATURE_OFFSET,
        UPDATE_TRUST_PUBLIC_KEY);
    if (result == OK) {
        result = crypto_ed25519_verify_update(
            &verify, (const uint8_t*)UPDATE_SYSTEM_DOMAIN,
            UPDATE_SYSTEM_DOMAIN_SIZE);
    }
    if (result == OK) {
        result = crypto_ed25519_verify_update(
            &verify, update_system_signed_header,
            UPDATE_SYSTEM_HEADER_SIZE);
    }
    while (result == OK && position < image_size) {
        uint32_t chunk = image_size - position;
        uint32_t bytes = 0U;

        if (chunk > sizeof(update_system_io_buffer)) {
            chunk = sizeof(update_system_io_buffer);
        }
        result = fs_read_file_range_at(
            path, UPDATE_SYSTEM_PAYLOAD_OFFSET + position,
            update_system_io_buffer, chunk, &bytes);
        if (result == OK && bytes != chunk) result = ERR_DISK;
        if (result == OK) {
            result = crypto_ed25519_verify_update(
                &verify, update_system_io_buffer, bytes);
            position += bytes;
        }
    }
    if (result == OK) result = crypto_ed25519_verify_final(&verify);
    return result;
}

static int update_system_read_base(const uint8_t* raw, uint32_t offset,
                                   update_system_base_t* output) {
    if (!raw || !output) return ERR_NULL;
    output->version.major = update_system_read_u16(raw, offset);
    output->version.minor = update_system_read_u16(raw, offset + 2U);
    output->version.patch = update_system_read_u16(raw, offset + 4U);
    output->epoch = update_system_read_u32(raw, offset + 6U);
    return any_nonzero(raw + offset + 10U, 2U) ? ERR_INVALID : OK;
}

static int update_system_validate_components(
    const uint8_t* raw, uint32_t image_size,
    update_system_verification_t* result_out) {
    static const uint16_t expected_kinds[] = {
        UPDATE_SYSTEM_COMPONENT_BOOT,
        UPDATE_SYSTEM_COMPONENT_STAGE2,
        UPDATE_SYSTEM_COMPONENT_KERNEL,
    };
    uint32_t previous_end = 0U;

    if (!raw || !result_out) return ERR_NULL;
    if (update_system_read_u16(raw, UPDATE_SYSTEM_COMPONENT_COUNT_OFFSET) !=
            UPDATE_SYSTEM_COMPONENT_COUNT ||
        update_system_read_u16(raw, UPDATE_SYSTEM_COMPONENT_ENTRY_SIZE_OFFSET) !=
            UPDATE_SYSTEM_COMPONENT_ENTRY_SIZE ||
        update_system_read_u16(raw, UPDATE_SYSTEM_CHECKPOINT_ENTRY_SIZE_OFFSET) !=
            UPDATE_SYSTEM_CHECKPOINT_SIZE) {
        return ERR_INVALID;
    }
    result_out->component_count = UPDATE_SYSTEM_COMPONENT_COUNT;
    for (uint32_t index = 0U; index < UPDATE_SYSTEM_COMPONENT_COUNT; index++) {
        uint32_t offset = UPDATE_SYSTEM_COMPONENTS_OFFSET +
                          index * UPDATE_SYSTEM_COMPONENT_ENTRY_SIZE;
        update_system_component_t* component = &result_out->components[index];

        component->kind = update_system_read_u16(raw, offset);
        if (component->kind != expected_kinds[index] ||
            update_system_read_u16(raw, offset + 2U) != 0U ||
            any_nonzero(raw + offset + 44U,
                        UPDATE_SYSTEM_COMPONENT_ENTRY_SIZE - 44U)) {
            return ERR_INVALID;
        }
        component->offset = update_system_read_u32(
            raw, offset + UPDATE_SYSTEM_COMPONENT_OFFSET_OFFSET);
        component->size = update_system_read_u32(
            raw, offset + UPDATE_SYSTEM_COMPONENT_SIZE_OFFSET);
        kmemcpy(component->hash,
                raw + offset + UPDATE_SYSTEM_COMPONENT_HASH_OFFSET,
                sizeof(component->hash));
        if (!component->size || component->offset >= image_size ||
            component->size > image_size - component->offset ||
            component->offset != previous_end) {
            return ERR_INVALID;
        }
        if (index == 0U &&
            (component->offset != 0U || component->size != 512U)) {
            return ERR_INVALID;
        }
        if (index == 1U &&
            (component->offset != 512U || (component->size % 512U) != 0U)) {
            return ERR_INVALID;
        }
        previous_end = component->offset + component->size;
    }
    return OK;
}

static int any_nonzero(const uint8_t* data, uint32_t size) {
    if (!data) return 1;
    for (uint32_t index = 0U; index < size; index++) {
        if (data[index]) return 1;
    }
    return 0;
}

static int update_system_remote_compatible(
    update_system_verification_t* result_out) {
    update_version_t current_version;
    uint32_t current_epoch;
    int supported = 0;

    if (!result_out) return ERR_NULL;
    if (update_get_installed_version(&current_version, &current_epoch) != OK) {
        return ERR_STATE;
    }
    for (uint32_t index = 0U;
         index < result_out->compatibility.base_count; index++) {
        if (update_system_base_equal(
                &result_out->compatibility.supported_from[index],
                &current_version, current_epoch)) {
            supported = 1;
            break;
        }
    }
    if (!supported || update_system_version_compare(
            &current_version,
            &result_out->compatibility.minimum_updater.version) < 0 ||
        (update_system_version_compare(
             &current_version,
             &result_out->compatibility.minimum_updater.version) == 0 &&
         current_epoch < result_out->compatibility.minimum_updater.epoch)) {
        return ERR_INVALID;
    }
    if (update_system_version_compare(&result_out->target_version,
                                      &current_version) < 0 ||
        (update_system_version_compare(&result_out->target_version,
                                       &current_version) == 0 &&
         result_out->target_epoch < current_epoch)) {
        return ERR_INVALID;
    }
    return OK;
}

static int update_system_remote_begin(update_system_remote_stream_t* stream) {
    const uint8_t* raw;
    uint16_t flags;
    uint32_t image_size;
    int result;

    if (!stream) return ERR_NULL;
    raw = stream->header;
    if (raw[0] != 'Z' || raw[1] != 'S' || raw[2] != 'Y' || raw[3] != 'S' ||
        update_system_read_u16(raw, 4U) != UPDATE_SYSTEM_FORMAT_VERSION ||
        update_system_read_u16(raw, 6U) != UPDATE_SYSTEM_HEADER_SIZE ||
        update_system_read_u16(raw, 8U) != UPDATE_SYSTEM_ARCH_I386) {
        return update_system_reject(UPDATE_SYSTEM_REASON_FORMAT, ERR_INVALID,
                                    "Cabecalho remoto ZSYS invalido",
                                    &stream->result);
    }
    flags = update_system_read_u16(raw, 10U);
    image_size = update_system_read_u32(raw, 24U);
    if (flags & ~(UPDATE_SYSTEM_FLAG_REQUIRES_REBOOT |
                  UPDATE_SYSTEM_FLAG_BRIDGE_REQUIRED) ||
        update_system_read_u32(raw, 12U) != stream->expected_size ||
        update_system_read_u32(raw, 16U) != UPDATE_SYSTEM_PAYLOAD_OFFSET ||
        update_system_read_u32(raw, 20U) != image_size || !image_size ||
        image_size > UPDATE_SYSTEM_MAX_IMAGE_SIZE || image_size % 512U ||
        stream->expected_size != UPDATE_SYSTEM_HEADER_SIZE + image_size) {
        return update_system_reject(UPDATE_SYSTEM_REASON_SIZE, ERR_INVALID,
                                    "Tamanho remoto ZSYS invalido",
                                    &stream->result);
    }
    if (update_system_read_u32(raw, UPDATE_SYSTEM_SIGNATURE_SIZE_OFFSET) !=
            UPDATE_SYSTEM_SIGNATURE_SIZE ||
        update_system_read_u32(raw, UPDATE_SYSTEM_SIGNATURE_OFFSET_FIELD) !=
            UPDATE_SYSTEM_SIGNATURE_OFFSET ||
        any_nonzero(raw + UPDATE_SYSTEM_RESERVED_OFFSET,
                    UPDATE_SYSTEM_HEADER_SIZE - UPDATE_SYSTEM_RESERVED_OFFSET) ||
        !crypto_equal(raw + UPDATE_SYSTEM_KEY_ID_OFFSET,
                      UPDATE_TRUST_KEY_ID, UPDATE_SYSTEM_KEY_ID_SIZE)) {
        return update_system_reject(
            !crypto_equal(raw + UPDATE_SYSTEM_KEY_ID_OFFSET,
                          UPDATE_TRUST_KEY_ID, UPDATE_SYSTEM_KEY_ID_SIZE) ?
            UPDATE_SYSTEM_REASON_UNKNOWN_KEY : UPDATE_SYSTEM_REASON_FORMAT,
            ERR_INVALID, "Identidade ou assinatura remota ZSYS invalida",
            &stream->result);
    }
    if (update_system_copy_fixed_text(
            raw + UPDATE_SYSTEM_CHANNEL_OFFSET, UPDATE_SYSTEM_CHANNEL_SIZE,
            stream->result.compatibility.channel,
            sizeof(stream->result.compatibility.channel)) != OK ||
        kstrcmp(stream->result.compatibility.channel, "stable") != 0 ||
        update_system_copy_fixed_text(
            raw + UPDATE_SYSTEM_RELEASE_ID_OFFSET,
            UPDATE_SYSTEM_IDENTIFIER_SIZE, stream->result.release_id,
            sizeof(stream->result.release_id)) != OK ||
        update_system_copy_fixed_text(
            raw + UPDATE_SYSTEM_RELEASE_TAG_OFFSET,
            UPDATE_SYSTEM_IDENTIFIER_SIZE, stream->result.release_tag,
            sizeof(stream->result.release_tag)) != OK ||
        kstrcmp(stream->result.release_tag, stream->tag) != 0) {
        return update_system_reject(UPDATE_SYSTEM_REASON_PATH_POLICY,
                                    ERR_INVALID,
                                    "Identidade remota ZSYS diverge da tag",
                                    &stream->result);
    }
    stream->result.target_version.major = update_system_read_u16(
        raw, UPDATE_SYSTEM_TARGET_VERSION_OFFSET);
    stream->result.target_version.minor = update_system_read_u16(
        raw, UPDATE_SYSTEM_TARGET_VERSION_OFFSET + 2U);
    stream->result.target_version.patch = update_system_read_u16(
        raw, UPDATE_SYSTEM_TARGET_VERSION_OFFSET + 4U);
    stream->result.target_epoch = update_system_read_u32(
        raw, UPDATE_SYSTEM_TARGET_EPOCH_OFFSET);
    stream->result.compatibility.base_count = update_system_read_u16(
        raw, UPDATE_SYSTEM_BASE_COUNT_OFFSET);
    if (!stream->result.compatibility.base_count ||
        stream->result.compatibility.base_count > UPDATE_SYSTEM_MAX_BASES ||
        update_system_read_u16(raw, UPDATE_SYSTEM_BASE_ENTRY_SIZE_OFFSET) !=
            UPDATE_SYSTEM_BASE_ENTRY_SIZE) {
        return update_system_reject(UPDATE_SYSTEM_REASON_COMPATIBILITY,
                                    ERR_INVALID, "Origens remotas ZSYS invalidas",
                                    &stream->result);
    }
    for (uint32_t index = 0U;
         index < stream->result.compatibility.base_count; index++) {
        result = update_system_read_base(
            raw, UPDATE_SYSTEM_BASES_OFFSET + index *
                 UPDATE_SYSTEM_BASE_ENTRY_SIZE,
            &stream->result.compatibility.supported_from[index]);
        if (result != OK) {
            return update_system_reject(UPDATE_SYSTEM_REASON_FORMAT, result,
                                        "Padding de origem remota ZSYS invalido",
                                        &stream->result);
        }
        if (update_system_version_compare(
                &stream->result.target_version,
                &stream->result.compatibility.supported_from[index].version) < 0 ||
            (update_system_version_compare(
                 &stream->result.target_version,
                 &stream->result.compatibility.supported_from[index].version) == 0 &&
             stream->result.target_epoch <=
                 stream->result.compatibility.supported_from[index].epoch)) {
            return update_system_reject(
                UPDATE_SYSTEM_REASON_COMPATIBILITY, ERR_INVALID,
                "Alvo remoto ZSYS nao supera a origem declarada",
                &stream->result);
        }
    }
    if (any_nonzero(
            raw + UPDATE_SYSTEM_BASES_OFFSET +
                stream->result.compatibility.base_count *
                    UPDATE_SYSTEM_BASE_ENTRY_SIZE,
            UPDATE_SYSTEM_MIN_UPDATER_OFFSET -
                (UPDATE_SYSTEM_BASES_OFFSET +
                 stream->result.compatibility.base_count *
                    UPDATE_SYSTEM_BASE_ENTRY_SIZE))) {
        return update_system_reject(
            UPDATE_SYSTEM_REASON_FORMAT, ERR_INVALID,
            "Origens remotas ZSYS nao usadas nao estao zeradas",
            &stream->result);
    }
    result = update_system_read_base(
        raw, UPDATE_SYSTEM_MIN_UPDATER_OFFSET,
        &stream->result.compatibility.minimum_updater);
    if (result != OK) {
        return update_system_reject(UPDATE_SYSTEM_REASON_FORMAT, result,
                                    "min_updater remoto ZSYS invalido",
                                    &stream->result);
    }
    stream->result.compatibility.boot_abi = update_system_read_u32(
        raw, UPDATE_SYSTEM_BOOT_ABI_OFFSET);
    stream->result.compatibility.data_schema_from = update_system_read_u32(
        raw, UPDATE_SYSTEM_SCHEMA_FROM_OFFSET);
    stream->result.compatibility.data_schema_to = update_system_read_u32(
        raw, UPDATE_SYSTEM_SCHEMA_TO_OFFSET);
    stream->result.compatibility.route_kind = raw[UPDATE_SYSTEM_ROUTE_OFFSET];
    stream->result.compatibility.checkpoint_count =
        raw[UPDATE_SYSTEM_CHECKPOINT_COUNT_OFFSET];
    stream->result.compatibility.requires_reboot =
        (uint8_t)((flags & UPDATE_SYSTEM_FLAG_REQUIRES_REBOOT) != 0U);
    stream->result.compatibility.bridge_required =
        (uint8_t)((flags & UPDATE_SYSTEM_FLAG_BRIDGE_REQUIRED) != 0U);
    if (stream->result.compatibility.boot_abi != 1U ||
        stream->result.compatibility.data_schema_from >
            stream->result.compatibility.data_schema_to ||
        !stream->result.compatibility.requires_reboot ||
        stream->result.compatibility.bridge_required ||
        (stream->result.compatibility.route_kind != UPDATE_SYSTEM_ROUTE_DIRECT &&
         stream->result.compatibility.route_kind != UPDATE_SYSTEM_ROUTE_CHECKPOINT) ||
        stream->result.compatibility.checkpoint_count >
            UPDATE_SYSTEM_MAX_CHECKPOINTS ||
        (stream->result.compatibility.route_kind == UPDATE_SYSTEM_ROUTE_DIRECT &&
         stream->result.compatibility.checkpoint_count != 0U)) {
        return update_system_reject(UPDATE_SYSTEM_REASON_COMPATIBILITY,
                                    ERR_INVALID, "Compatibilidade remota ZSYS invalida",
                                    &stream->result);
    }
    for (uint32_t index = 0U;
         index < stream->result.compatibility.checkpoint_count; index++) {
        result = update_system_copy_fixed_text(
            raw + UPDATE_SYSTEM_CHECKPOINTS_OFFSET + index *
                 UPDATE_SYSTEM_CHECKPOINT_SIZE,
            UPDATE_SYSTEM_CHECKPOINT_SIZE,
            stream->result.compatibility.checkpoints[index],
            sizeof(stream->result.compatibility.checkpoints[index]));
        if (result != OK) {
            return update_system_reject(UPDATE_SYSTEM_REASON_PATH_POLICY,
                                        result, "Checkpoint remoto ZSYS invalido",
                                        &stream->result);
        }
    }
    if (any_nonzero(
            raw + UPDATE_SYSTEM_CHECKPOINTS_OFFSET +
                stream->result.compatibility.checkpoint_count *
                    UPDATE_SYSTEM_CHECKPOINT_SIZE,
            UPDATE_SYSTEM_KEY_ID_OFFSET -
                (UPDATE_SYSTEM_CHECKPOINTS_OFFSET +
                 stream->result.compatibility.checkpoint_count *
                    UPDATE_SYSTEM_CHECKPOINT_SIZE))) {
        return update_system_reject(
            UPDATE_SYSTEM_REASON_FORMAT, ERR_INVALID,
            "Checkpoints remotos ZSYS nao usados nao estao zerados",
            &stream->result);
    }
    result = update_system_validate_components(raw, image_size, &stream->result);
    if (result != OK) {
        return update_system_reject(UPDATE_SYSTEM_REASON_FORMAT, result,
                                    "Componentes remotos ZSYS invalidos",
                                    &stream->result);
    }
    result = update_system_remote_compatible(&stream->result);
    if (result != OK) {
        return update_system_reject(
            result == ERR_STATE ? UPDATE_SYSTEM_REASON_IO :
            UPDATE_SYSTEM_REASON_BASE_VERSION, result,
            "ZSYS remoto incompativel com o sistema atual",
            &stream->result);
    }
    kmemcpy(stream->result.image_hash,
            raw + UPDATE_SYSTEM_IMAGE_HASH_OFFSET, CRYPTO_SHA256_SIZE);
    stream->image_size = image_size;
    kmemcpy(stream->signed_header, raw, UPDATE_SYSTEM_HEADER_SIZE);
    kmemset(stream->signed_header + UPDATE_SYSTEM_SIGNATURE_OFFSET, 0,
            UPDATE_SYSTEM_SIGNATURE_SIZE);
    result = crypto_ed25519_verify_init(
        &stream->signature, raw + UPDATE_SYSTEM_SIGNATURE_OFFSET,
        UPDATE_TRUST_PUBLIC_KEY);
    if (result == OK) result = crypto_ed25519_verify_update(
        &stream->signature, (const uint8_t*)UPDATE_SYSTEM_DOMAIN,
        UPDATE_SYSTEM_DOMAIN_SIZE);
    if (result == OK) result = crypto_ed25519_verify_update(
        &stream->signature, stream->signed_header, UPDATE_SYSTEM_HEADER_SIZE);
    if (result == OK) result = crypto_sha256_init(&stream->image_sha);
    for (uint32_t index = 0U; result == OK &&
         index < stream->result.component_count; index++) {
        result = crypto_sha256_init(&stream->component_sha[index]);
        stream->component_active[index] = 1U;
    }
    if (result != OK) {
        return update_system_reject(UPDATE_SYSTEM_REASON_IO, result,
                                    "Falha ao iniciar verificacao remota ZSYS",
                                    &stream->result);
    }
    stream->crypto_ready = 1U;
    stream->result.header_valid = 1U;
    return OK;
}

static int update_system_remote_payload(update_system_remote_stream_t* stream,
                                        const uint8_t* data, uint32_t size) {
    uint32_t payload_start;
    uint32_t payload_end;
    int result;

    if (!stream || (!data && size)) return ERR_NULL;
    if (!size) return OK;
    if (!stream->crypto_ready ||
        stream->payload_received > stream->image_size ||
        size > stream->image_size - stream->payload_received) return ERR_OVERFLOW;
    result = crypto_ed25519_verify_update(&stream->signature, data, size);
    if (result == OK) result = crypto_sha256_update(&stream->image_sha, data, size);
    payload_start = stream->payload_received;
    payload_end = payload_start + size;
    for (uint32_t index = 0U;
         result == OK && index < stream->result.component_count; index++) {
        update_system_component_t* component = &stream->result.components[index];
        uint32_t start = component->offset > payload_start ?
            component->offset : payload_start;
        uint32_t end = component->offset + component->size < payload_end ?
            component->offset + component->size : payload_end;

        if (start < end) {
            result = crypto_sha256_update(
                &stream->component_sha[index], data + start - payload_start,
                end - start);
        }
    }
    if (result == OK) stream->payload_received += size;
    return result;
}

static int update_system_remote_sink(const uint8_t* data, uint32_t size,
                                     void* context) {
    update_system_remote_stream_t* stream =
        (update_system_remote_stream_t*)context;
    uint32_t consumed = 0U;

    if (!stream || (!data && size)) return ERR_NULL;
    if (crypto_sha256_update(&stream->package_sha, data, size) != OK) {
        return ERR_STATE;
    }
    while (consumed < size) {
        if (stream->received < UPDATE_SYSTEM_HEADER_SIZE) {
            uint32_t copy_size = UPDATE_SYSTEM_HEADER_SIZE - stream->received;
            if (copy_size > size - consumed) copy_size = size - consumed;
            kmemcpy(stream->header + stream->received, data + consumed, copy_size);
            stream->received += copy_size;
            consumed += copy_size;
            if (stream->received == UPDATE_SYSTEM_HEADER_SIZE) {
                int result = update_system_remote_begin(stream);
                if (result != OK) return result;
            }
        } else {
            uint32_t payload_size = size - consumed;
            int result = update_system_remote_payload(
                stream, data + consumed, payload_size);
            if (result != OK) return result;
            stream->received += payload_size;
            consumed += payload_size;
        }
    }
    return OK;
}

static int update_system_remote_finish(update_system_remote_stream_t* stream) {
    uint8_t hash[CRYPTO_SHA256_SIZE];
    int result;

    if (!stream || !stream->crypto_ready ||
        stream->received != stream->expected_size ||
        stream->payload_received != stream->image_size) return ERR_INVALID;
    result = crypto_sha256_final(&stream->package_sha, stream->package_hash);
    if (result == OK) result = crypto_ed25519_verify_final(&stream->signature);
    if (result == OK) result = crypto_sha256_final(&stream->image_sha, hash);
    if (result == OK && !crypto_equal(hash, stream->result.image_hash,
                                     CRYPTO_SHA256_SIZE)) result = ERR_INVALID;
    for (uint32_t index = 0U;
         result == OK && index < stream->result.component_count; index++) {
        result = crypto_sha256_final(&stream->component_sha[index], hash);
        if (result == OK && !crypto_equal(
                hash, stream->result.components[index].hash,
                CRYPTO_SHA256_SIZE)) result = ERR_INVALID;
    }
    if (result != OK) {
        stream->result.reason = result == ERR_INVALID ?
            UPDATE_SYSTEM_REASON_HASH : UPDATE_SYSTEM_REASON_SIGNATURE;
        return result;
    }
    stream->result.signature_valid = 1U;
    stream->result.image_valid = 1U;
    stream->result.image_size = stream->image_size;
    stream->result.reason = UPDATE_SYSTEM_REASON_NONE;
    stream->result.compatible = 1U;
    return OK;
}

static update_system_remote_stream_t update_system_remote_stream;
static update_remote_github_release_t update_system_remote_release;
static update_remote_result_t update_system_remote_result;

int update_system_check_tag(const char* tag,
                            const update_remote_options_t* options,
                            update_system_verification_t* result_out) {
    http_request_options_t http_options;
    http_status_t http_status;
    const uint8_t* descriptor = 0;
    uint32_t descriptor_size = 0U;
    uint8_t descriptor_hash[CRYPTO_SHA256_SIZE];
    int result;

    if (!tag || !result_out) {
        LOG_ERROR("UPDATE", "Tag ou resultado ZSYS remoto nulo");
        return ERR_NULL;
    }
    kmemset(result_out, 0, sizeof(*result_out));
    kmemset(&update_system_remote_release, 0,
            sizeof(update_system_remote_release));
    kmemset(&update_system_remote_result, 0,
            sizeof(update_system_remote_result));
    result = update_remote_github_query(
        tag, options, &update_system_remote_release,
        &update_system_remote_result);
    if (result != OK) {
        result_out->reason = UPDATE_SYSTEM_REASON_IO;
        LOG_ERROR_CODE("UPDATE", result,
                       "Falha ao descobrir Release GitHub para ZSYS");
        return result;
    }
    if (!update_system_remote_release.system_present ||
        !update_system_remote_release.system.url[0] ||
        update_system_remote_release.system.size < UPDATE_SYSTEM_HEADER_SIZE ||
        update_system_remote_release.system.size > UPDATE_SYSTEM_HEADER_SIZE +
            UPDATE_SYSTEM_MAX_IMAGE_SIZE ||
        !update_system_remote_release.system.digest_present) {
        result_out->reason = UPDATE_SYSTEM_REASON_FORMAT;
        LOG_ERROR("UPDATE", "Release remota nao publica system.zsys completo");
        return ERR_INVALID;
    }
    result = update_system_remote_http_options(options, &http_options);
    if (result == OK) result = http_get_start_ex(
        update_system_remote_release.descriptor.url, &http_options);
    if (result == OK) result = update_system_remote_wait_http(
        options, &http_status);
    if (result != OK) {
        result_out->reason = result == ERR_TIMEOUT ?
            UPDATE_SYSTEM_REASON_IO : UPDATE_SYSTEM_REASON_IO;
        LOG_ERROR_CODE("UPDATE", result,
                       "Falha ao obter release.json da Release v2");
        return result;
    }
    if (http_status.status_code != 200U ||
        !http_status.has_content_length ||
        http_status.content_length != http_status.body_length ||
        http_get_body(&descriptor, &descriptor_size) != OK ||
        (update_system_remote_release.descriptor.digest_present &&
         (crypto_sha256(descriptor, descriptor_size, descriptor_hash) != OK ||
          !crypto_equal(descriptor_hash,
                        update_system_remote_release.descriptor.digest,
                        CRYPTO_SHA256_SIZE))) ||
        update_system_remote_descriptor_matches(
            descriptor, descriptor_size, &update_system_remote_release) != OK) {
        result_out->reason = UPDATE_SYSTEM_REASON_FORMAT;
        LOG_ERROR("UPDATE", "release.json v2 diverge do asset system.zsys");
        return ERR_INVALID;
    }
    kmemset(&update_system_remote_stream, 0,
            sizeof(update_system_remote_stream));
    if (update_system_remote_copy_text(
            update_system_remote_stream.tag,
            sizeof(update_system_remote_stream.tag), tag) != OK) {
        result_out->reason = UPDATE_SYSTEM_REASON_PATH_POLICY;
        LOG_ERROR("UPDATE", "Tag remota ZSYS excede a politica de caminho");
        return ERR_INVALID;
    }
    update_system_remote_stream.expected_size =
        update_system_remote_release.system.size;
    kmemcpy(update_system_remote_stream.expected_package_hash,
            update_system_remote_release.system.digest, CRYPTO_SHA256_SIZE);
    result = crypto_sha256_init(&update_system_remote_stream.package_sha);
    if (result == OK) result = update_system_remote_http_options(
        options, &http_options);
    if (result == OK) result = http_get_stream_start_ex(
        update_system_remote_release.system.url,
        update_system_remote_release.system.size, update_system_remote_sink,
        &update_system_remote_stream, &http_options);
    if (result == OK) result = update_system_remote_wait_http(
        options, &http_status);
    if (result != OK) {
        *result_out = update_system_remote_stream.result;
        if (result_out->reason == UPDATE_SYSTEM_REASON_NONE) {
            result_out->reason = UPDATE_SYSTEM_REASON_IO;
        }
        LOG_ERROR_CODE("UPDATE", result, "Falha ao baixar system.zsys remoto");
        return result;
    }
    if (http_status.status_code != 200U ||
        !http_status.has_content_length ||
        http_status.content_length != update_system_remote_release.system.size ||
        http_status.body_length != update_system_remote_release.system.size) {
        result_out->reason = UPDATE_SYSTEM_REASON_SIZE;
        LOG_ERROR("UPDATE", "Tamanho de system.zsys remoto diverge da API");
        return ERR_INVALID;
    }
    result = update_system_remote_finish(&update_system_remote_stream);
    *result_out = update_system_remote_stream.result;
    if (result != OK ||
        !crypto_equal(update_system_remote_stream.package_hash,
                      update_system_remote_stream.expected_package_hash,
                      CRYPTO_SHA256_SIZE)) {
        result_out->reason = result == OK ? UPDATE_SYSTEM_REASON_HASH :
            result_out->reason;
        if (result_out->reason == UPDATE_SYSTEM_REASON_NONE) {
            result_out->reason = UPDATE_SYSTEM_REASON_HASH;
        }
        LOG_ERROR("UPDATE", "system.zsys remoto falhou na verificacao");
        return result == OK ? ERR_INVALID : result;
    }
    LOG_INFO("UPDATE", "system.zsys remoto verificado sem gravacao");
    return OK;
}

int update_system_init(void) {
    LOG_INFO("UPDATE", "Inicializando verificador ZSYS");
    update_system_initialized = 1;
    LOG_INFO("UPDATE", "Verificador ZSYS inicializado");
    return OK;
}

int update_system_is_ready(void) {
    return update_system_initialized;
}

static int update_system_verify_file_internal(
    const char* path, update_system_verification_t* result_out,
    uint8_t allow_current_slot) {
    update_version_t current_version;
    update_system_base_t current_base;
    uint32_t file_size = 0U;
    uint32_t image_size;
    uint32_t signature_size;
    uint32_t signature_offset;
    uint32_t target_epoch;
    uint8_t computed_hash[CRYPTO_SHA256_SIZE];
    int result;

    if (!path || !result_out) {
        LOG_ERROR("UPDATE", "Caminho ou resultado ZSYS nulo");
        return ERR_NULL;
    }
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = UPDATE_SYSTEM_REASON_FORMAT;
    if (!update_system_initialized) {
        return update_system_reject(UPDATE_SYSTEM_REASON_UNSUPPORTED,
                                     ERR_STATE,
                                     "Verificador ZSYS nao inicializado",
                                     result_out);
    }
    result = fs_get_root_file_info(path, &file_size, 0);
    if (result != OK) {
        return update_system_reject(UPDATE_SYSTEM_REASON_IO, result,
                                    "Falha ao localizar arquivo ZSYS",
                                    result_out);
    }
    if (file_size < UPDATE_SYSTEM_HEADER_SIZE ||
        file_size > UPDATE_SYSTEM_HEADER_SIZE + UPDATE_SYSTEM_MAX_IMAGE_SIZE) {
        return update_system_reject(UPDATE_SYSTEM_REASON_SIZE, ERR_INVALID,
                                    "Tamanho do arquivo ZSYS invalido",
                                    result_out);
    }
    {
        uint32_t bytes = 0U;
        result = fs_read_file_range_at(path, 0U, update_system_header,
                                       sizeof(update_system_header), &bytes);
        if (result != OK || bytes != sizeof(update_system_header)) {
            return update_system_reject(UPDATE_SYSTEM_REASON_IO,
                                        result == OK ? ERR_DISK : result,
                                        "Falha ao ler cabecalho ZSYS",
                                        result_out);
        }
    }
    if (update_system_header[0] != 'Z' || update_system_header[1] != 'S' ||
        update_system_header[2] != 'Y' || update_system_header[3] != 'S') {
        return update_system_reject(UPDATE_SYSTEM_REASON_FORMAT, ERR_INVALID,
                                    "Magic ZSYS invalido", result_out);
    }
    if (update_system_read_u16(update_system_header, 4U) !=
            UPDATE_SYSTEM_FORMAT_VERSION ||
        update_system_read_u16(update_system_header, 6U) !=
            UPDATE_SYSTEM_HEADER_SIZE) {
        return update_system_reject(UPDATE_SYSTEM_REASON_FORMAT, ERR_INVALID,
                                    "Versao ou tamanho do header ZSYS invalido",
                                    result_out);
    }
    if (update_system_read_u16(update_system_header, 8U) !=
        UPDATE_SYSTEM_ARCH_I386) {
        return update_system_reject(UPDATE_SYSTEM_REASON_ARCHITECTURE,
                                    ERR_INVALID, "Arquitetura ZSYS invalida",
                                    result_out);
    }
    image_size = update_system_read_u32(update_system_header, 24U);
    if (update_system_read_u32(update_system_header, 12U) != file_size ||
        update_system_read_u32(update_system_header, 16U) !=
            UPDATE_SYSTEM_PAYLOAD_OFFSET ||
        update_system_read_u32(update_system_header, 20U) != image_size ||
        !image_size || image_size > UPDATE_SYSTEM_MAX_IMAGE_SIZE ||
        (image_size % 512U) != 0U ||
        file_size != UPDATE_SYSTEM_HEADER_SIZE + image_size) {
        return update_system_reject(UPDATE_SYSTEM_REASON_SIZE, ERR_INVALID,
                                    "Regioes da imagem ZSYS divergem",
                                    result_out);
    }
    result_out->image_size = image_size;
    signature_size = update_system_read_u32(
        update_system_header, UPDATE_SYSTEM_SIGNATURE_SIZE_OFFSET);
    signature_offset = update_system_read_u32(
        update_system_header, UPDATE_SYSTEM_SIGNATURE_OFFSET_FIELD);
    if (signature_size != UPDATE_SYSTEM_SIGNATURE_SIZE ||
        signature_offset != UPDATE_SYSTEM_SIGNATURE_OFFSET ||
        any_nonzero(update_system_header + UPDATE_SYSTEM_RESERVED_OFFSET,
                    UPDATE_SYSTEM_HEADER_SIZE - UPDATE_SYSTEM_RESERVED_OFFSET)) {
        return update_system_reject(UPDATE_SYSTEM_REASON_FORMAT, ERR_INVALID,
                                    "Assinatura ou reservado ZSYS invalido",
                                    result_out);
    }
    if (!crypto_equal(update_system_header + UPDATE_SYSTEM_KEY_ID_OFFSET,
                      UPDATE_TRUST_KEY_ID, UPDATE_SYSTEM_KEY_ID_SIZE)) {
        return update_system_reject(UPDATE_SYSTEM_REASON_UNKNOWN_KEY,
                                    ERR_INVALID, "Key id ZSYS desconhecido",
                                    result_out);
    }
    if (update_system_copy_fixed_text(
            update_system_header + UPDATE_SYSTEM_CHANNEL_OFFSET,
            UPDATE_SYSTEM_CHANNEL_SIZE, result_out->compatibility.channel,
            sizeof(result_out->compatibility.channel)) != OK ||
        kstrcmp(result_out->compatibility.channel, "stable") != 0 ||
        update_system_copy_fixed_text(
            update_system_header + UPDATE_SYSTEM_RELEASE_ID_OFFSET,
            UPDATE_SYSTEM_IDENTIFIER_SIZE, result_out->release_id,
            sizeof(result_out->release_id)) != OK ||
        update_system_copy_fixed_text(
            update_system_header + UPDATE_SYSTEM_RELEASE_TAG_OFFSET,
            UPDATE_SYSTEM_IDENTIFIER_SIZE, result_out->release_tag,
            sizeof(result_out->release_tag)) != OK) {
        return update_system_reject(UPDATE_SYSTEM_REASON_PATH_POLICY,
                                    ERR_INVALID, "Identidade ZSYS invalida",
                                    result_out);
    }
    result_out->target_version.major = update_system_read_u16(
        update_system_header, UPDATE_SYSTEM_TARGET_VERSION_OFFSET);
    result_out->target_version.minor = update_system_read_u16(
        update_system_header, UPDATE_SYSTEM_TARGET_VERSION_OFFSET + 2U);
    result_out->target_version.patch = update_system_read_u16(
        update_system_header, UPDATE_SYSTEM_TARGET_VERSION_OFFSET + 4U);
    target_epoch = update_system_read_u32(
        update_system_header, UPDATE_SYSTEM_TARGET_EPOCH_OFFSET);
    result_out->target_epoch = target_epoch;
    result_out->compatibility.base_count = update_system_read_u16(
        update_system_header, UPDATE_SYSTEM_BASE_COUNT_OFFSET);
    if (!result_out->compatibility.base_count ||
        result_out->compatibility.base_count > UPDATE_SYSTEM_MAX_BASES ||
        update_system_read_u16(update_system_header,
                               UPDATE_SYSTEM_BASE_ENTRY_SIZE_OFFSET) !=
            UPDATE_SYSTEM_BASE_ENTRY_SIZE) {
        return update_system_reject(UPDATE_SYSTEM_REASON_COMPATIBILITY,
                                    ERR_INVALID, "Origens ZSYS invalidas",
                                    result_out);
    }
    for (uint32_t index = 0U; index < result_out->compatibility.base_count;
         index++) {
        result = update_system_read_base(
            update_system_header,
            UPDATE_SYSTEM_BASES_OFFSET + index * UPDATE_SYSTEM_BASE_ENTRY_SIZE,
            &result_out->compatibility.supported_from[index]);
        if (result != OK) {
            return update_system_reject(UPDATE_SYSTEM_REASON_FORMAT, result,
                                        "Base ZSYS possui padding invalido",
                                        result_out);
        }
        if (update_system_version_compare(
                &result_out->target_version,
                &result_out->compatibility.supported_from[index].version) < 0 ||
            (update_system_version_compare(
                 &result_out->target_version,
                 &result_out->compatibility.supported_from[index].version) == 0 &&
             target_epoch <= result_out->compatibility.supported_from[index].epoch)) {
            return update_system_reject(
                UPDATE_SYSTEM_REASON_COMPATIBILITY, ERR_INVALID,
                "Alvo ZSYS nao supera a origem declarada", result_out);
        }
    }
    if (any_nonzero(
            update_system_header + UPDATE_SYSTEM_BASES_OFFSET +
                result_out->compatibility.base_count *
                    UPDATE_SYSTEM_BASE_ENTRY_SIZE,
            UPDATE_SYSTEM_MIN_UPDATER_OFFSET -
                (UPDATE_SYSTEM_BASES_OFFSET +
                 result_out->compatibility.base_count *
                    UPDATE_SYSTEM_BASE_ENTRY_SIZE))) {
        return update_system_reject(UPDATE_SYSTEM_REASON_FORMAT, ERR_INVALID,
                                    "Origens ZSYS nao usadas nao estao zeradas",
                                    result_out);
    }
    result = update_system_read_base(
        update_system_header, UPDATE_SYSTEM_MIN_UPDATER_OFFSET,
        &result_out->compatibility.minimum_updater);
    if (result != OK) {
        return update_system_reject(UPDATE_SYSTEM_REASON_FORMAT, result,
                                    "min_updater ZSYS invalido", result_out);
    }
    result_out->compatibility.boot_abi = update_system_read_u32(
        update_system_header, UPDATE_SYSTEM_BOOT_ABI_OFFSET);
    result_out->compatibility.data_schema_from = update_system_read_u32(
        update_system_header, UPDATE_SYSTEM_SCHEMA_FROM_OFFSET);
    result_out->compatibility.data_schema_to = update_system_read_u32(
        update_system_header, UPDATE_SYSTEM_SCHEMA_TO_OFFSET);
    result_out->compatibility.route_kind =
        update_system_header[UPDATE_SYSTEM_ROUTE_OFFSET];
    result_out->compatibility.checkpoint_count =
        update_system_header[UPDATE_SYSTEM_CHECKPOINT_COUNT_OFFSET];
    result_out->compatibility.requires_reboot =
        (uint8_t)((update_system_read_u16(update_system_header, 10U) &
                   UPDATE_SYSTEM_FLAG_REQUIRES_REBOOT) != 0U);
    result_out->compatibility.bridge_required =
        (uint8_t)((update_system_read_u16(update_system_header, 10U) &
                   UPDATE_SYSTEM_FLAG_BRIDGE_REQUIRED) != 0U);
    if (update_system_read_u16(update_system_header, 10U) &
            ~(UPDATE_SYSTEM_FLAG_REQUIRES_REBOOT |
              UPDATE_SYSTEM_FLAG_BRIDGE_REQUIRED) ||
        result_out->compatibility.boot_abi != 1U ||
        result_out->compatibility.data_schema_from >
            result_out->compatibility.data_schema_to ||
        !result_out->compatibility.requires_reboot ||
        result_out->compatibility.bridge_required ||
        (result_out->compatibility.route_kind != UPDATE_SYSTEM_ROUTE_DIRECT &&
         result_out->compatibility.route_kind != UPDATE_SYSTEM_ROUTE_CHECKPOINT) ||
        result_out->compatibility.checkpoint_count > UPDATE_SYSTEM_MAX_CHECKPOINTS ||
        (result_out->compatibility.route_kind == UPDATE_SYSTEM_ROUTE_DIRECT &&
         result_out->compatibility.checkpoint_count != 0U)) {
        return update_system_reject(UPDATE_SYSTEM_REASON_COMPATIBILITY,
                                    ERR_INVALID, "Compatibilidade ZSYS invalida",
                                    result_out);
    }
    for (uint32_t index = 0U;
         index < result_out->compatibility.checkpoint_count; index++) {
        result = update_system_copy_fixed_text(
            update_system_header + UPDATE_SYSTEM_CHECKPOINTS_OFFSET +
                index * UPDATE_SYSTEM_CHECKPOINT_SIZE,
            UPDATE_SYSTEM_CHECKPOINT_SIZE,
            result_out->compatibility.checkpoints[index],
            sizeof(result_out->compatibility.checkpoints[index]));
        if (result != OK) {
            return update_system_reject(UPDATE_SYSTEM_REASON_PATH_POLICY,
                                        result, "Checkpoint ZSYS invalido",
                                        result_out);
        }
    }
    if (any_nonzero(
            update_system_header + UPDATE_SYSTEM_CHECKPOINTS_OFFSET +
                result_out->compatibility.checkpoint_count *
                    UPDATE_SYSTEM_CHECKPOINT_SIZE,
            UPDATE_SYSTEM_KEY_ID_OFFSET -
                (UPDATE_SYSTEM_CHECKPOINTS_OFFSET +
                 result_out->compatibility.checkpoint_count *
                    UPDATE_SYSTEM_CHECKPOINT_SIZE))) {
        return update_system_reject(UPDATE_SYSTEM_REASON_FORMAT, ERR_INVALID,
                                    "Checkpoints ZSYS nao usados nao estao zerados",
                                    result_out);
    }
    result = update_system_validate_components(
        update_system_header, image_size, result_out);
    if (result != OK) {
        return update_system_reject(UPDATE_SYSTEM_REASON_FORMAT, result,
                                    "Tabela de componentes ZSYS invalida",
                                    result_out);
    }
    result = update_get_installed_version(&current_version, &current_base.epoch);
    if (result != OK) {
        return update_system_reject(UPDATE_SYSTEM_REASON_COMPATIBILITY, result,
                                    "Versao instalada indisponivel para ZSYS",
                                    result_out);
    }
    current_base.version = current_version;
    result_out->compatible = 0U;
    for (uint32_t index = 0U; index < result_out->compatibility.base_count;
         index++) {
        if (update_system_base_equal(
                &result_out->compatibility.supported_from[index],
                &current_version, current_base.epoch)) {
            result_out->compatible = 1U;
            break;
        }
    }
    if (!result_out->compatible && allow_current_slot &&
        update_system_version_compare(&result_out->target_version,
                                      &current_version) == 0 &&
        target_epoch == current_base.epoch) {
        result_out->compatible = 1U;
    }
    if (!result_out->compatible ||
        update_system_version_compare(
            &current_version,
            &result_out->compatibility.minimum_updater.version) < 0 ||
        (update_system_version_compare(
             &current_version,
             &result_out->compatibility.minimum_updater.version) == 0 &&
         current_base.epoch < result_out->compatibility.minimum_updater.epoch)) {
        return update_system_reject(UPDATE_SYSTEM_REASON_BASE_VERSION,
                                    ERR_INVALID,
                                    "Versao atual nao e suportada pelo ZSYS",
                                    result_out);
    }
    if (update_system_version_compare(&result_out->target_version,
                                      &current_version) < 0 ||
        (update_system_version_compare(&result_out->target_version,
                                       &current_version) == 0 &&
         target_epoch < current_base.epoch)) {
        return update_system_reject(UPDATE_SYSTEM_REASON_DOWNGRADE, ERR_INVALID,
                                    "ZSYS representa downgrade", result_out);
    }
    result = update_system_verify_signature(path, image_size);
    if (result != OK) {
        return update_system_reject(UPDATE_SYSTEM_REASON_SIGNATURE, result,
                                    "Assinatura ZSYS invalida", result_out);
    }
    result_out->header_valid = 1U;
    result_out->signature_valid = 1U;
    result = update_system_hash_range(
        path, UPDATE_SYSTEM_PAYLOAD_OFFSET, image_size, computed_hash);
    if (result != OK) {
        return update_system_reject(UPDATE_SYSTEM_REASON_IO, result,
                                    "Falha ao calcular hash da imagem ZSYS",
                                    result_out);
    }
    kmemcpy(result_out->image_hash,
            update_system_header + UPDATE_SYSTEM_IMAGE_HASH_OFFSET,
            sizeof(result_out->image_hash));
    if (!crypto_equal(computed_hash, result_out->image_hash,
                      sizeof(computed_hash))) {
        return update_system_reject(UPDATE_SYSTEM_REASON_HASH, ERR_INVALID,
                                    "Hash da imagem ZSYS diverge", result_out);
    }
    for (uint32_t index = 0U; index < result_out->component_count; index++) {
        result = update_system_hash_range(
            path, UPDATE_SYSTEM_PAYLOAD_OFFSET +
                      result_out->components[index].offset,
            result_out->components[index].size, computed_hash);
        if (result != OK) {
            return update_system_reject(UPDATE_SYSTEM_REASON_IO, result,
                                        "Falha ao calcular hash de componente ZSYS",
                                        result_out);
        }
        if (!crypto_equal(computed_hash,
                          result_out->components[index].hash,
                          sizeof(computed_hash))) {
            return update_system_reject(UPDATE_SYSTEM_REASON_HASH, ERR_INVALID,
                                        "Hash de componente ZSYS diverge",
                                        result_out);
        }
    }
    result_out->image_valid = 1U;
    result_out->reason = UPDATE_SYSTEM_REASON_NONE;
    LOG_INFO("UPDATE", "ZSYS validado sem gravacao");
    return OK;
}

int update_system_verify_file(const char* path,
                              update_system_verification_t* result_out) {
    return update_system_verify_file_internal(path, result_out, 0U);
}

int update_system_verify_file_for_slot(
    const char* path, update_system_verification_t* result_out) {
    return update_system_verify_file_internal(path, result_out, 1U);
}

const char* update_system_reason_name(update_system_reason_t reason) {
    switch (reason) {
        case UPDATE_SYSTEM_REASON_NONE: return "NONE";
        case UPDATE_SYSTEM_REASON_FORMAT: return "FORMAT";
        case UPDATE_SYSTEM_REASON_SIZE: return "SIZE";
        case UPDATE_SYSTEM_REASON_HASH: return "HASH";
        case UPDATE_SYSTEM_REASON_UNKNOWN_KEY: return "UNKNOWN_KEY";
        case UPDATE_SYSTEM_REASON_SIGNATURE: return "SIGNATURE";
        case UPDATE_SYSTEM_REASON_ARCHITECTURE: return "ARCHITECTURE";
        case UPDATE_SYSTEM_REASON_BASE_VERSION: return "BASE_VERSION";
        case UPDATE_SYSTEM_REASON_DOWNGRADE: return "DOWNGRADE";
        case UPDATE_SYSTEM_REASON_COMPATIBILITY: return "COMPATIBILITY";
        case UPDATE_SYSTEM_REASON_PATH_POLICY: return "PATH_POLICY";
        case UPDATE_SYSTEM_REASON_IO: return "IO";
        case UPDATE_SYSTEM_REASON_UNSUPPORTED: return "UNSUPPORTED";
        default: return "UNKNOWN";
    }
}
