#include "core/update_remote.h"
#include "core/crypto.h"
#include "core/errors.h"
#include "core/http.h"
#include "core/log.h"
#include "core/network_manager.h"
#include "core/string.h"
#include "core/update_remote_config.h"
#include "core/update_trust.h"
#include "fs/fs.h"
#include "process/process.h"

#define UPDATE_REMOTE_FORMAT_VERSION 1U
#define UPDATE_REMOTE_ARCH_I386 1U
#define UPDATE_REMOTE_CHANNEL_STABLE 1U
#define UPDATE_REMOTE_RECORD_VERSION 1U
#define UPDATE_REMOTE_RECORD_HASH_OFFSET 480U
#define UPDATE_REMOTE_RECORD_RESERVED_OFFSET 108U
#define UPDATE_REMOTE_PHASE_CLEAN 0U
#define UPDATE_REMOTE_PHASE_DOWNLOADING 1U
#define UPDATE_REMOTE_SLOT_NONE 0xFFU
#define UPDATE_REMOTE_RECORD_MISSING (-1)
#define UPDATE_REMOTE_HTTP_OK 200U
#define UPDATE_REMOTE_WAIT_BLOCK_TICKS 1U
#define UPDATE_REMOTE_MAX_RETRIES 1U
#define UPDATE_REMOTE_RECORD_ATTRIBUTES \
    (FS_ATTRIBUTE_HIDDEN | FS_ATTRIBUTE_SYSTEM | FS_ATTRIBUTE_ARCHIVE)
#define UPDATE_REMOTE_PACKAGE_ATTRIBUTES UPDATE_REMOTE_RECORD_ATTRIBUTES

typedef struct {
    uint32_t sequence;
    uint8_t phase;
    uint8_t active_slot;
    uint8_t pending_slot;
    uint32_t manifest_generation;
    uint32_t package_size;
    uint8_t package_hash[32];
    uint8_t manifest_hash[32];
    update_version_t base_version;
    update_version_t target_version;
    uint32_t base_epoch;
    uint32_t target_epoch;
} update_remote_record_t;

typedef struct {
    crypto_sha256_ctx_t sha256;
    int write_error;
} update_remote_download_t;

static const char* update_remote_record_aliases[2] = {
    "ZUR0.STA", "ZUR1.STA"
};
static const char* update_remote_package_aliases[2] = {
    "ZUR0.ZUP", "ZUR1.ZUP"
};
static const uint8_t update_remote_domain[] =
    "ZEPHYROS-REMOTE-V1";

static update_remote_status_t update_remote_status;
static update_remote_record_t update_remote_record;
static int update_remote_record_slot = -1;
static uint8_t update_remote_manifest[UPDATE_REMOTE_MANIFEST_SIZE];
static uint8_t update_remote_manifest_hash[32];
static uint8_t update_remote_manifest_valid;
static uint8_t update_remote_cancelled;
static update_remote_download_t update_remote_download;
static crypto_ed25519_verify_ctx_t update_remote_manifest_verify;
static http_status_t update_remote_http;
static update_verification_t update_remote_verification;
static update_remote_record_t update_remote_record_candidates[2];
static uint8_t update_remote_record_raw[2][UPDATE_REMOTE_RECORD_SIZE];
static uint8_t update_remote_io_buffer[512];
static uint8_t update_remote_package_hash[32];
static char update_remote_package_url[UPDATE_REMOTE_URL_SIZE];

static int update_remote_transient_error(int error);

static uint16_t update_remote_read_u16(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t update_remote_read_u32(const uint8_t* data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static void update_remote_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void update_remote_write_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void update_remote_copy_text(char* output, uint32_t capacity,
                                    const char* input) {
    uint32_t index = 0;

    if (!output || !capacity) return;
    if (!input) input = "";
    while (input[index] && index + 1U < capacity) {
        output[index] = input[index];
        index++;
    }
    output[index] = '\0';
}

static int update_remote_version_compare(const update_version_t* first,
                                         const update_version_t* second) {
    if (first->major != second->major) {
        return first->major > second->major ? 1 : -1;
    }
    if (first->minor != second->minor) {
        return first->minor > second->minor ? 1 : -1;
    }
    if (first->patch != second->patch) {
        return first->patch > second->patch ? 1 : -1;
    }
    return 0;
}

static void update_remote_result_copy(update_remote_result_t* output) {
    if (!output) return;
    output->reason = update_remote_status.reason;
    output->http_status = update_remote_status.http_status;
    output->retry_count = update_remote_status.retry_count;
    output->bytes_received = update_remote_status.bytes_received;
    output->candidate = update_remote_status.candidate;
    if (update_remote_manifest_valid) {
        kmemcpy(output->manifest_hash, update_remote_manifest_hash,
                CRYPTO_SHA256_SIZE);
    } else {
        kmemset(output->manifest_hash, 0, sizeof(output->manifest_hash));
    }
    update_remote_copy_text(
        output->cached_alias, sizeof(output->cached_alias),
        update_remote_status.cached_alias);
}

static int update_remote_reject(update_remote_reason_t reason, int error,
                                const char* message,
                                update_remote_result_t* output) {
    update_remote_status.state = UPDATE_REMOTE_STATE_FAILED;
    update_remote_status.reason = reason;
    update_remote_status.busy = 0U;
    if (message) LOG_ERROR("UPDATE", message);
    update_remote_result_copy(output);
    return error;
}

static int update_remote_begin(void) {
    if (!update_remote_status.initialized) {
        LOG_ERROR("UPDATE", "Servico remoto nao inicializado");
        return ERR_STATE;
    }
    if (update_remote_status.busy) {
        LOG_ERROR("UPDATE", "Outra operacao remota esta ativa");
        return ERR_STATE;
    }
    update_remote_status.busy = 1U;
    update_remote_status.operation_generation++;
    if (!update_remote_status.operation_generation) {
        update_remote_status.operation_generation = 1U;
    }
    return OK;
}

static void update_remote_end_ready(void) {
    update_remote_status.busy = 0U;
    update_remote_status.reason = UPDATE_REMOTE_REASON_NONE;
    update_remote_status.state = update_remote_status.manifest_cached ?
        UPDATE_REMOTE_STATE_AVAILABLE : UPDATE_REMOTE_STATE_READY;
}

static int update_remote_network_ready(void) {
    network_manager_status_t network;

    if (network_manager_get_status(&network) != OK) return 0;
    return network.http_available && network.ipv4_configured;
}

static void update_remote_refresh_network(void) {
    update_remote_status.network_ready =
        update_remote_network_ready() ? 1U : 0U;
    if (!update_remote_status.enabled) {
        update_remote_status.state = UPDATE_REMOTE_STATE_DISABLED;
    } else if (!update_remote_status.network_ready &&
               !update_remote_status.busy) {
        update_remote_status.state = UPDATE_REMOTE_STATE_UNAVAILABLE;
        update_remote_status.reason = UPDATE_REMOTE_REASON_NETWORK;
    }
}

static void update_remote_decode_version(update_version_t* version,
                                         const uint8_t* data) {
    version->major = update_remote_read_u16(data);
    version->minor = update_remote_read_u16(data + 2U);
    version->patch = update_remote_read_u16(data + 4U);
}

static void update_remote_encode_version(uint8_t* data,
                                         const update_version_t* version) {
    update_remote_write_u16(data, version->major);
    update_remote_write_u16(data + 2U, version->minor);
    update_remote_write_u16(data + 4U, version->patch);
}

static int update_remote_path_valid(const uint8_t* raw, char* path_out) {
    uint32_t end = UPDATE_REMOTE_PATH_SIZE;

    for (uint32_t index = 0; index < UPDATE_REMOTE_PATH_SIZE; index++) {
        if (!raw[index]) {
            end = index;
            break;
        }
    }
    if (end == 0U || end == UPDATE_REMOTE_PATH_SIZE || raw[0] != '/') {
        return 0;
    }
    for (uint32_t index = 0; index < end; index++) {
        uint8_t value = raw[index];
        int allowed = (value >= 'A' && value <= 'Z') ||
                      (value >= 'a' && value <= 'z') ||
                      (value >= '0' && value <= '9') ||
                      value == '/' || value == '.' ||
                      value == '-' || value == '_';

        if (!allowed || value == '\\' ||
            (value == '.' && index + 1U < end &&
             raw[index + 1U] == '.')) return 0;
        path_out[index] = (char)value;
    }
    path_out[end] = '\0';
    for (uint32_t index = end + 1U;
         index < UPDATE_REMOTE_PATH_SIZE; index++) {
        if (raw[index] != 0U) return 0;
    }
    return 1;
}

static int update_remote_verify_manifest_signature(const uint8_t* raw) {
    int result;

    result = crypto_ed25519_verify_init(
        &update_remote_manifest_verify,
        raw + UPDATE_REMOTE_SIGNED_SIZE,
        UPDATE_TRUST_PUBLIC_KEY);
    if (result != OK) return result;
    result = crypto_ed25519_verify_update(
        &update_remote_manifest_verify,
        update_remote_domain, sizeof(update_remote_domain));
    if (result != OK) return result;
    result = crypto_ed25519_verify_update(
        &update_remote_manifest_verify,
        raw, UPDATE_REMOTE_SIGNED_SIZE);
    if (result != OK) return result;
    return crypto_ed25519_verify_final(
        &update_remote_manifest_verify);
}

static int update_remote_manifest_versions_valid(
    const update_remote_candidate_t* candidate) {
    update_version_t installed;
    uint32_t installed_epoch;

    if (update_get_installed_version(
            &installed, &installed_epoch) != OK) return 0;
    if (update_remote_version_compare(
            &candidate->base_version, &installed) != 0 ||
        candidate->base_epoch != installed_epoch) return 0;
    if (candidate->target_epoch < candidate->base_epoch) return 0;
    return candidate->target_epoch > candidate->base_epoch ||
           update_remote_version_compare(
               &candidate->target_version,
               &candidate->base_version) > 0;
}

static int update_remote_parse_manifest(
    const uint8_t* raw, update_remote_candidate_t* candidate,
    update_remote_reason_t* reason_out) {
    if (!raw || !candidate || !reason_out) {
        LOG_ERROR("UPDATE", "Argumento nulo ao decodificar manifesto");
        return ERR_NULL;
    }
    *reason_out = UPDATE_REMOTE_REASON_MANIFEST_FORMAT;
    if (raw[0] != 'Z' || raw[1] != 'U' ||
        raw[2] != 'M' || raw[3] != '1' ||
        update_remote_read_u16(raw + 4U) !=
            UPDATE_REMOTE_FORMAT_VERSION ||
        update_remote_read_u16(raw + 6U) !=
            UPDATE_REMOTE_MANIFEST_SIZE ||
        update_remote_read_u16(raw + 8U) != UPDATE_REMOTE_ARCH_I386 ||
        update_remote_read_u16(raw + 10U) !=
            UPDATE_REMOTE_CHANNEL_STABLE ||
        update_remote_read_u32(raw + 88U) != 0U) {
        LOG_ERROR("UPDATE", "Layout do manifesto remoto invalido");
        return ERR_INVALID;
    }
    if (!crypto_equal(raw + 72U, UPDATE_TRUST_KEY_ID, 16U)) {
        *reason_out = UPDATE_REMOTE_REASON_UNKNOWN_KEY;
        LOG_ERROR("UPDATE", "Manifesto remoto usa chave desconhecida");
        return ERR_INVALID;
    }
    if (update_remote_verify_manifest_signature(raw) != OK) {
        *reason_out = UPDATE_REMOTE_REASON_MANIFEST_SIGNATURE;
        LOG_ERROR("UPDATE", "Assinatura do manifesto remoto invalida");
        return ERR_INVALID;
    }
    kmemset(candidate, 0, sizeof(*candidate));
    candidate->generation = update_remote_read_u32(raw + 12U);
    update_remote_decode_version(&candidate->base_version, raw + 16U);
    update_remote_decode_version(&candidate->target_version, raw + 22U);
    candidate->base_epoch = update_remote_read_u32(raw + 28U);
    candidate->target_epoch = update_remote_read_u32(raw + 32U);
    candidate->package_size = update_remote_read_u32(raw + 36U);
    kmemcpy(candidate->package_hash, raw + 40U, 32U);
    if (!candidate->package_size ||
        candidate->package_size > ZUPD_MAX_TOTAL_SIZE) {
        *reason_out = UPDATE_REMOTE_REASON_SIZE;
        LOG_ERROR("UPDATE", "Tamanho do pacote remoto invalido");
        return ERR_OVERFLOW;
    }
    if (!update_remote_path_valid(raw + 92U, candidate->package_path)) {
        LOG_ERROR("UPDATE", "Caminho relativo do pacote remoto invalido");
        return ERR_INVALID;
    }
    if (!update_remote_manifest_versions_valid(candidate)) {
        *reason_out = UPDATE_REMOTE_REASON_VERSION;
        LOG_ERROR("UPDATE", "Versoes do manifesto remoto incompativeis");
        return ERR_INVALID;
    }
    *reason_out = UPDATE_REMOTE_REASON_NONE;
    return OK;
}

static int update_remote_build_package_url(const char* manifest_url,
                                           const char* package_path,
                                           char* output) {
    uint32_t prefix;
    uint32_t path_length;

    if (!manifest_url || !package_path || !output) {
        LOG_ERROR("UPDATE", "Argumento nulo ao montar URL do pacote");
        return ERR_NULL;
    }
    if (kstrlen(manifest_url) < 8U ||
        manifest_url[0] != 'h' || manifest_url[1] != 't' ||
        manifest_url[2] != 't' || manifest_url[3] != 'p' ||
        manifest_url[4] != ':' || manifest_url[5] != '/' ||
        manifest_url[6] != '/') {
        LOG_ERROR("UPDATE", "URL do manifesto nao usa HTTP simples");
        return ERR_INVALID;
    }
    prefix = 7U;
    while (manifest_url[prefix] && manifest_url[prefix] != '/') {
        prefix++;
    }
    path_length = kstrlen(package_path);
    if (prefix + path_length + 1U > UPDATE_REMOTE_URL_SIZE) {
        LOG_ERROR("UPDATE", "URL resultante do pacote excede limite");
        return ERR_OVERFLOW;
    }
    for (uint32_t index = 0; index < prefix; index++) {
        output[index] = manifest_url[index];
    }
    for (uint32_t index = 0; index <= path_length; index++) {
        output[prefix + index] = package_path[index];
    }
    return OK;
}

static int update_remote_wait_http(const update_remote_options_t* options,
                                   http_status_t* status_out) {
    if (!status_out) {
        LOG_ERROR("UPDATE", "Destino nulo ao aguardar HTTP");
        return ERR_NULL;
    }
    while (1) {
        if (http_get_status(status_out) != OK) {
            LOG_ERROR("UPDATE", "Falha ao consultar estado HTTP");
            return ERR_STATE;
        }
        update_remote_status.http_status = status_out->status_code;
        update_remote_status.bytes_received = status_out->body_length;
        if (status_out->state == HTTP_STATE_COMPLETE) return OK;
        if (status_out->state == HTTP_STATE_FAILED) {
            LOG_WARN("UPDATE", "Operacao HTTP remota falhou");
            return status_out->last_error == OK ?
                   ERR_STATE : status_out->last_error;
        }
        if (options && options->cancel_check &&
            options->cancel_check(options->cancel_context)) {
            update_remote_cancelled = 1U;
            http_reset();
            LOG_WARN("UPDATE", "Operacao HTTP remota cancelada");
            return ERR_TIMEOUT;
        }
        process_block(UPDATE_REMOTE_WAIT_BLOCK_TICKS);
    }
}

static const char* update_remote_effective_url(const char* requested) {
    if (requested && requested[0]) return requested;
    if (update_remote_manifest_valid &&
        update_remote_status.manifest_url[0]) {
        return update_remote_status.manifest_url;
    }
    return UPDATE_REMOTE_DEFAULT_MANIFEST_URL;
}

static int update_remote_check_internal(
    const char* requested, const update_remote_options_t* options,
    update_remote_result_t* output) {
    const uint8_t* body;
    const char* url = update_remote_effective_url(requested);
    uint32_t length;
    update_remote_reason_t reason;
    int result;

    update_remote_refresh_network();
    if (!update_remote_status.enabled) {
        return update_remote_reject(
            UPDATE_REMOTE_REASON_DISABLED, ERR_UNAVAILABLE,
            "Distribuicao remota nao foi habilitada", output);
    }
    if (!update_remote_status.network_ready) {
        return update_remote_reject(
            UPDATE_REMOTE_REASON_NETWORK, ERR_UNAVAILABLE,
            "Rede nao esta configurada para Update remoto", output);
    }
    update_remote_status.state = UPDATE_REMOTE_STATE_CHECKING;
    update_remote_status.reason = UPDATE_REMOTE_REASON_NONE;
    update_remote_status.bytes_received = 0U;
    update_remote_status.total_bytes = UPDATE_REMOTE_MANIFEST_SIZE;
    for (uint8_t attempt = 0U;
         attempt <= UPDATE_REMOTE_MAX_RETRIES; attempt++) {
        update_remote_status.retry_count = attempt;
        update_remote_cancelled = 0U;
        result = http_get_start(url);
        if (result == OK) {
            result = update_remote_wait_http(
                options, &update_remote_http);
        }
        if (result == OK || update_remote_cancelled ||
            !update_remote_transient_error(result) ||
            attempt == UPDATE_REMOTE_MAX_RETRIES) break;
        LOG_WARN("UPDATE", "Consulta remota sera repetida do inicio");
    }
    if (result != OK) {
        reason = update_remote_cancelled ?
                 UPDATE_REMOTE_REASON_CANCELLED :
                 result == ERR_TIMEOUT ?
                 UPDATE_REMOTE_REASON_TIMEOUT : UPDATE_REMOTE_REASON_HTTP;
        return update_remote_reject(
            reason, result, "Consulta do manifesto remoto falhou", output);
    }
    if (update_remote_http.status_code != UPDATE_REMOTE_HTTP_OK ||
        !update_remote_http.has_content_length ||
        update_remote_http.content_length != UPDATE_REMOTE_MANIFEST_SIZE ||
        update_remote_http.body_length != UPDATE_REMOTE_MANIFEST_SIZE ||
        http_get_body(&body, &length) != OK ||
        length != UPDATE_REMOTE_MANIFEST_SIZE) {
        return update_remote_reject(
            UPDATE_REMOTE_REASON_HTTP, ERR_INVALID,
            "Resposta do manifesto remoto invalida", output);
    }
    result = update_remote_parse_manifest(
        body, &update_remote_status.candidate, &reason);
    if (result != OK) {
        return update_remote_reject(
            reason, result, "Manifesto remoto recusado", output);
    }
    kmemcpy(update_remote_manifest, body, sizeof(update_remote_manifest));
    if (crypto_sha256(
            body, sizeof(update_remote_manifest),
            update_remote_manifest_hash) != OK) {
        return update_remote_reject(
            UPDATE_REMOTE_REASON_MANIFEST_FORMAT, ERR_STATE,
            "Hash do manifesto remoto falhou", output);
    }
    update_remote_manifest_valid = 1U;
    update_remote_status.manifest_cached = 1U;
    update_remote_copy_text(
        update_remote_status.manifest_url,
        sizeof(update_remote_status.manifest_url), url);
    update_remote_end_ready();
    update_remote_result_copy(output);
    return OK;
}

static void update_remote_record_encode(
    uint8_t raw[UPDATE_REMOTE_RECORD_SIZE],
    const update_remote_record_t* record) {
    kmemset(raw, 0, UPDATE_REMOTE_RECORD_SIZE);
    raw[0] = 'Z';
    raw[1] = 'U';
    raw[2] = 'R';
    raw[3] = '1';
    update_remote_write_u16(raw + 4U, UPDATE_REMOTE_RECORD_VERSION);
    update_remote_write_u16(raw + 6U, UPDATE_REMOTE_RECORD_SIZE);
    update_remote_write_u32(raw + 8U, record->sequence);
    raw[12] = record->phase;
    raw[13] = record->active_slot;
    raw[14] = record->pending_slot;
    update_remote_write_u32(raw + 16U, record->manifest_generation);
    update_remote_write_u32(raw + 20U, record->package_size);
    kmemcpy(raw + 24U, record->package_hash, 32U);
    kmemcpy(raw + 56U, record->manifest_hash, 32U);
    update_remote_encode_version(raw + 88U, &record->base_version);
    update_remote_encode_version(raw + 94U, &record->target_version);
    update_remote_write_u32(raw + 100U, record->base_epoch);
    update_remote_write_u32(raw + 104U, record->target_epoch);
    crypto_sha256(raw, UPDATE_REMOTE_RECORD_HASH_OFFSET,
                  raw + UPDATE_REMOTE_RECORD_HASH_OFFSET);
}

static int update_remote_record_decode(
    const uint8_t raw[UPDATE_REMOTE_RECORD_SIZE],
    update_remote_record_t* record) {
    uint8_t hash[32];

    if (!raw || !record) {
        LOG_ERROR("UPDATE", "Argumento nulo ao decodificar cache remoto");
        return ERR_NULL;
    }
    if (raw[0] != 'Z' || raw[1] != 'U' ||
        raw[2] != 'R' || raw[3] != '1' ||
        update_remote_read_u16(raw + 4U) !=
            UPDATE_REMOTE_RECORD_VERSION ||
        update_remote_read_u16(raw + 6U) !=
            UPDATE_REMOTE_RECORD_SIZE ||
        (raw[12] != UPDATE_REMOTE_PHASE_CLEAN &&
         raw[12] != UPDATE_REMOTE_PHASE_DOWNLOADING) ||
        (raw[13] > 1U && raw[13] != UPDATE_REMOTE_SLOT_NONE) ||
        (raw[14] > 1U && raw[14] != UPDATE_REMOTE_SLOT_NONE)) {
        LOG_ERROR("UPDATE", "Cabecalho do cache remoto invalido");
        return ERR_INVALID;
    }
    if ((raw[12] == UPDATE_REMOTE_PHASE_CLEAN &&
         raw[14] != UPDATE_REMOTE_SLOT_NONE) ||
        (raw[12] == UPDATE_REMOTE_PHASE_DOWNLOADING &&
         raw[14] > 1U) ||
        (raw[14] <= 1U && raw[14] == raw[13])) {
        LOG_ERROR("UPDATE", "Fase e slots do cache remoto divergem");
        return ERR_INVALID;
    }
    for (uint32_t index = UPDATE_REMOTE_RECORD_RESERVED_OFFSET;
         index < UPDATE_REMOTE_RECORD_HASH_OFFSET; index++) {
        if (raw[index] != 0U) {
            LOG_ERROR("UPDATE", "Reservados do cache remoto nao zerados");
            return ERR_INVALID;
        }
    }
    if (crypto_sha256(raw, UPDATE_REMOTE_RECORD_HASH_OFFSET, hash) != OK ||
        !crypto_equal(
            hash, raw + UPDATE_REMOTE_RECORD_HASH_OFFSET, 32U)) {
        LOG_ERROR("UPDATE", "Hash do estado remoto invalido");
        return ERR_INVALID;
    }
    kmemset(record, 0, sizeof(*record));
    record->sequence = update_remote_read_u32(raw + 8U);
    record->phase = raw[12];
    record->active_slot = raw[13];
    record->pending_slot = raw[14];
    record->manifest_generation = update_remote_read_u32(raw + 16U);
    record->package_size = update_remote_read_u32(raw + 20U);
    kmemcpy(record->package_hash, raw + 24U, 32U);
    kmemcpy(record->manifest_hash, raw + 56U, 32U);
    update_remote_decode_version(&record->base_version, raw + 88U);
    update_remote_decode_version(&record->target_version, raw + 94U);
    record->base_epoch = update_remote_read_u32(raw + 100U);
    record->target_epoch = update_remote_read_u32(raw + 104U);
    if (record->active_slot == UPDATE_REMOTE_SLOT_NONE &&
        record->package_size != 0U) {
        LOG_ERROR("UPDATE", "Cache remoto vazio possui tamanho");
        return ERR_INVALID;
    }
    if (record->active_slot <= 1U && record->package_size == 0U) {
        LOG_ERROR("UPDATE", "Cache remoto ativo possui tamanho zero");
        return ERR_INVALID;
    }
    return OK;
}

static int update_remote_read_record(
    uint32_t slot, uint8_t raw[UPDATE_REMOTE_RECORD_SIZE],
    update_remote_record_t* record, uint8_t* exists) {
    uint32_t size;
    uint32_t read;

    *exists = 0U;
    if (fs_get_root_file_info(
            update_remote_record_aliases[slot], &size, 0) != OK) {
        return UPDATE_REMOTE_RECORD_MISSING;
    }
    *exists = 1U;
    if (size != UPDATE_REMOTE_RECORD_SIZE ||
        fs_read_file_range_at(
            update_remote_record_aliases[slot], 0U, raw,
            UPDATE_REMOTE_RECORD_SIZE, &read) != OK ||
        read != UPDATE_REMOTE_RECORD_SIZE) {
        LOG_ERROR("UPDATE", "Leitura do estado remoto redundante falhou");
        return ERR_INVALID;
    }
    return update_remote_record_decode(raw, record);
}

static int update_remote_load_records(void) {
    uint8_t exists[2];
    int results[2];
    int selected = -1;

    results[0] = update_remote_read_record(
        0U, update_remote_record_raw[0],
        &update_remote_record_candidates[0], &exists[0]);
    results[1] = update_remote_read_record(
        1U, update_remote_record_raw[1],
        &update_remote_record_candidates[1], &exists[1]);
    if (!exists[0] && !exists[1]) {
        kmemset(&update_remote_record, 0, sizeof(update_remote_record));
        update_remote_record.active_slot = UPDATE_REMOTE_SLOT_NONE;
        update_remote_record.pending_slot = UPDATE_REMOTE_SLOT_NONE;
        update_remote_record_slot = -1;
        update_remote_status.cache_store = UPDATE_REMOTE_STORE_EMPTY;
        return OK;
    }
    if (results[0] == OK && results[1] == OK &&
        update_remote_record_candidates[0].sequence ==
            update_remote_record_candidates[1].sequence &&
        !crypto_equal(
            update_remote_record_raw[0], update_remote_record_raw[1],
            UPDATE_REMOTE_RECORD_SIZE)) {
        LOG_ERROR("UPDATE", "Copias remotas empatadas divergem");
        return ERR_INVALID;
    }
    if (results[0] == OK) selected = 0;
    if (results[1] == OK &&
        (selected < 0 ||
         update_remote_record_candidates[1].sequence >
            update_remote_record_candidates[selected].sequence)) selected = 1;
    if (selected < 0) {
        LOG_ERROR("UPDATE", "Nenhuma copia valida do estado remoto");
        return ERR_INVALID;
    }
    update_remote_record = update_remote_record_candidates[selected];
    update_remote_record_slot = selected;
    update_remote_status.cache_store = UPDATE_REMOTE_STORE_VALID;
    return OK;
}

static int update_remote_write_record(void) {
    int slot = update_remote_record_slot == 0 ? 1 : 0;
    int result;

    update_remote_record.sequence++;
    update_remote_record_encode(
        update_remote_record_raw[0], &update_remote_record);
    result = fs_atomic_write_root(
        update_remote_record_aliases[slot], update_remote_record_raw[0],
        UPDATE_REMOTE_RECORD_SIZE,
        UPDATE_REMOTE_RECORD_ATTRIBUTES, FS_ATOMIC_CREATE_OR_REPLACE);
    if (result != OK) {
        update_remote_status.cache_store = UPDATE_REMOTE_STORE_INVALID;
        LOG_ERROR("UPDATE", "Falha ao persistir estado remoto redundante");
        return result;
    }
    update_remote_record_slot = slot;
    update_remote_status.cache_store = UPDATE_REMOTE_STORE_VALID;
    return OK;
}

static int update_remote_delete_root(const char* alias) {
    int result = fs_atomic_delete_root(alias);

    if (result == OK || result == ERR_NOT_FOUND) return OK;
    LOG_ERROR("UPDATE", "Arquivo interno remoto nao foi removido");
    return result;
}

static int update_remote_hash_file(const char* path, uint32_t size,
                                   uint8_t hash_out[32]) {
    crypto_sha256_ctx_t hash;
    uint32_t offset = 0U;

    if (crypto_sha256_init(&hash) != OK) {
        LOG_ERROR("UPDATE", "Falha ao iniciar hash do cache remoto");
        return ERR_STATE;
    }
    while (offset < size) {
        uint32_t chunk = size - offset;
        uint32_t read = 0U;

        if (chunk > sizeof(update_remote_io_buffer)) {
            chunk = sizeof(update_remote_io_buffer);
        }
        if (fs_read_file_range_at(
            path, offset, update_remote_io_buffer, chunk, &read) != OK ||
            read != chunk ||
            crypto_sha256_update(
                &hash, update_remote_io_buffer, chunk) != OK) {
            LOG_ERROR("UPDATE", "Falha ao hashear pacote remoto local");
            return ERR_DISK;
        }
        offset += chunk;
    }
    if (crypto_sha256_final(&hash, hash_out) != OK) {
        LOG_ERROR("UPDATE", "Falha ao finalizar hash do cache remoto");
        return ERR_STATE;
    }
    return OK;
}

static int update_remote_validate_cached_package(void) {
    uint8_t hash[32];
    uint32_t size;

    if (update_remote_record.active_slot == UPDATE_REMOTE_SLOT_NONE) {
        update_remote_status.package_cached = 0U;
        update_remote_status.cached_alias[0] = '\0';
        return OK;
    }
    if (fs_get_root_file_info(
            update_remote_package_aliases[
                update_remote_record.active_slot], &size, 0) != OK ||
        size != update_remote_record.package_size ||
        update_remote_hash_file(
            update_remote_package_aliases[
                update_remote_record.active_slot],
            size, hash) != OK ||
        !crypto_equal(hash, update_remote_record.package_hash, 32U)) {
        LOG_ERROR("UPDATE", "Pacote ativo do cache remoto e invalido");
        return ERR_INVALID;
    }
    update_remote_status.package_cached = 1U;
    update_remote_copy_text(
        update_remote_status.cached_alias,
        sizeof(update_remote_status.cached_alias),
        update_remote_package_aliases[
            update_remote_record.active_slot]);
    return OK;
}

static int update_remote_recover_cache(void) {
    if (update_remote_record.phase == UPDATE_REMOTE_PHASE_DOWNLOADING) {
        if (update_remote_record.pending_slot <= 1U) {
            if (update_remote_delete_root(
                update_remote_package_aliases[
                    update_remote_record.pending_slot]) != OK) {
                return ERR_DISK;
            }
        }
        update_remote_record.phase = UPDATE_REMOTE_PHASE_CLEAN;
        update_remote_record.pending_slot = UPDATE_REMOTE_SLOT_NONE;
        update_remote_status.pending_recovery = 1U;
        if (update_remote_write_record() != OK) return ERR_DISK;
        LOG_WARN("UPDATE", "Download remoto interrompido foi descartado");
    }
    for (uint32_t slot = 0U; slot < 2U; slot++) {
        if (slot != update_remote_record.active_slot &&
            update_remote_delete_root(
                update_remote_package_aliases[slot]) != OK) {
            return ERR_DISK;
        }
    }
    return update_remote_validate_cached_package();
}

static int update_remote_stream_sink(const uint8_t* data, uint32_t size,
                                     void* context) {
    update_remote_download_t* download =
        (update_remote_download_t*)context;
    int result;

    if (!download) {
        LOG_ERROR("UPDATE", "Contexto nulo no streaming remoto");
        return ERR_NULL;
    }
    result = fs_stream_write_root(data, size);
    if (result == OK) {
        result = crypto_sha256_update(&download->sha256, data, size);
    }
    if (result != OK) {
        download->write_error = result;
        LOG_ERROR("UPDATE", "Falha ao consumir bloco remoto");
    }
    return result;
}

static int update_remote_space_available(uint32_t package_size) {
    fs_info_t info;
    uint32_t cluster_size;
    uint32_t needed;

    if (fs_get_info(&info) != OK || info.type != FS_TYPE_FAT12) return 0;
    cluster_size = info.bytes_per_sector * info.sectors_per_cluster;
    if (!cluster_size) return 0;
    needed = (package_size + cluster_size - 1U) / cluster_size;
    return info.free_clusters >= needed + 2U;
}

static int update_remote_prepare_pending(uint8_t pending_slot) {
    if (update_remote_status.cache_store == UPDATE_REMOTE_STORE_INVALID) {
        if (update_remote_delete_root(
                update_remote_package_aliases[0]) != OK ||
            update_remote_delete_root(
                update_remote_package_aliases[1]) != OK) {
            LOG_ERROR("UPDATE", "Cache remoto invalido nao foi reiniciado");
            return ERR_DISK;
        }
        kmemset(&update_remote_record, 0, sizeof(update_remote_record));
        update_remote_record.active_slot = UPDATE_REMOTE_SLOT_NONE;
        update_remote_record.pending_slot = UPDATE_REMOTE_SLOT_NONE;
        update_remote_record_slot = -1;
    }
    if (update_remote_delete_root(
            update_remote_package_aliases[pending_slot]) != OK) {
        LOG_ERROR("UPDATE", "Slot pendente remoto nao foi preparado");
        return ERR_DISK;
    }
    update_remote_record.phase = UPDATE_REMOTE_PHASE_DOWNLOADING;
    update_remote_record.pending_slot = pending_slot;
    return update_remote_write_record();
}

static int update_remote_download_attempt(
    const char* package_url, const char* alias,
    const update_remote_options_t* options) {
    int result;

    kmemset(&update_remote_download, 0, sizeof(update_remote_download));
    update_remote_cancelled = 0U;
    result = crypto_sha256_init(&update_remote_download.sha256);
    if (result == OK) {
        result = fs_stream_begin_root(
            alias, update_remote_status.candidate.package_size,
            UPDATE_REMOTE_PACKAGE_ATTRIBUTES);
    }
    if (result == OK) {
        result = http_get_stream_start(
            package_url, update_remote_status.candidate.package_size,
            update_remote_stream_sink, &update_remote_download);
    }
    if (result == OK) {
        result = update_remote_wait_http(options, &update_remote_http);
    }
    if (result == OK &&
        (update_remote_http.status_code != UPDATE_REMOTE_HTTP_OK ||
         !update_remote_http.has_content_length ||
         update_remote_http.content_length !=
             update_remote_status.candidate.package_size ||
         update_remote_http.body_length !=
             update_remote_status.candidate.package_size)) {
        result = ERR_INVALID;
    }
    if (result == OK) result = fs_stream_finish_root();
    if (result == OK) {
        result = crypto_sha256_final(
            &update_remote_download.sha256,
            update_remote_package_hash);
    }
    if (result != OK) fs_stream_abort_root();
    return result;
}

static int update_remote_transient_error(int error) {
    if (update_remote_cancelled) return 0;
    return error == ERR_TIMEOUT || error == ERR_STATE ||
           error == ERR_NOT_FOUND;
}

static int update_remote_abort_pending(update_remote_reason_t reason,
                                       int error,
                                       update_remote_result_t* output) {
    uint8_t pending = update_remote_record.pending_slot;

    if (fs_stream_abort_root() != OK) {
        return update_remote_reject(
            UPDATE_REMOTE_REASON_IO, ERR_DISK,
            "Streaming remoto exige recuperacao no boot", output);
    }
    if (pending <= 1U) {
        if (update_remote_delete_root(
                update_remote_package_aliases[pending]) != OK) {
            return update_remote_reject(
                UPDATE_REMOTE_REASON_IO, ERR_DISK,
                "Slot remoto exige recuperacao no boot", output);
        }
    }
    update_remote_record.phase = UPDATE_REMOTE_PHASE_CLEAN;
    update_remote_record.pending_slot = UPDATE_REMOTE_SLOT_NONE;
    if (update_remote_write_record() != OK) {
        return update_remote_reject(
            UPDATE_REMOTE_REASON_IO, ERR_DISK,
            "Estado remoto exige recuperacao no boot", output);
    }
    if (output) {
        output->cache_preserved =
            update_remote_record.active_slot <= 1U ? 1U : 0U;
    }
    return update_remote_reject(
        reason, error, "Download remoto recusado", output);
}

static int update_remote_candidate_matches_package(
    const update_verification_t* verification) {
    const update_remote_candidate_t* candidate =
        &update_remote_status.candidate;

    return verification->total_size == candidate->package_size &&
           verification->base_epoch == candidate->base_epoch &&
           verification->target_epoch == candidate->target_epoch &&
           update_remote_version_compare(
               &verification->base_version,
               &candidate->base_version) == 0 &&
           update_remote_version_compare(
               &verification->target_version,
               &candidate->target_version) == 0;
}

static int update_remote_commit_package(
    uint8_t pending_slot, const uint8_t hash[32],
    update_remote_result_t* output) {
    update_remote_record_t previous_record = update_remote_record;
    uint8_t previous_slot = update_remote_record.active_slot;

    update_remote_record.phase = UPDATE_REMOTE_PHASE_CLEAN;
    update_remote_record.active_slot = pending_slot;
    update_remote_record.pending_slot = UPDATE_REMOTE_SLOT_NONE;
    update_remote_record.manifest_generation =
        update_remote_status.candidate.generation;
    update_remote_record.package_size =
        update_remote_status.candidate.package_size;
    kmemcpy(update_remote_record.package_hash, hash, 32U);
    kmemcpy(update_remote_record.manifest_hash,
            update_remote_manifest_hash, 32U);
    update_remote_record.base_version =
        update_remote_status.candidate.base_version;
    update_remote_record.target_version =
        update_remote_status.candidate.target_version;
    update_remote_record.base_epoch =
        update_remote_status.candidate.base_epoch;
    update_remote_record.target_epoch =
        update_remote_status.candidate.target_epoch;
    if (update_remote_write_record() != OK) {
        update_remote_record = previous_record;
        LOG_ERROR("UPDATE", "Commit do cache remoto nao foi persistido");
        return ERR_DISK;
    }
    if (previous_slot <= 1U && previous_slot != pending_slot) {
        if (update_remote_delete_root(
                update_remote_package_aliases[previous_slot]) != OK) {
            LOG_WARN("UPDATE", "Slot remoto anterior sera limpo no boot");
        }
    }
    update_remote_status.package_cached = 1U;
    update_remote_copy_text(
        update_remote_status.cached_alias,
        sizeof(update_remote_status.cached_alias),
        update_remote_package_aliases[pending_slot]);
    if (output) output->package_published = 1U;
    return OK;
}

static int update_remote_receive_package(
    const char* package_url, uint8_t pending_slot,
    const update_remote_options_t* options,
    update_remote_result_t* output) {
    int result;

    update_remote_status.state = UPDATE_REMOTE_STATE_DOWNLOADING;
    update_remote_status.total_bytes =
        update_remote_status.candidate.package_size;
    for (uint8_t attempt = 0U;
         attempt <= UPDATE_REMOTE_MAX_RETRIES; attempt++) {
        update_remote_status.retry_count = attempt;
        result = update_remote_download_attempt(
            package_url, update_remote_package_aliases[pending_slot],
            options);
        if (result == OK ||
            !update_remote_transient_error(result) ||
            attempt == UPDATE_REMOTE_MAX_RETRIES) break;
        LOG_WARN("UPDATE", "Transferencia remota sera repetida do inicio");
    }
    if (result != OK) {
        update_remote_reason_t reason =
            update_remote_cancelled ? UPDATE_REMOTE_REASON_CANCELLED :
            result == ERR_TIMEOUT ? UPDATE_REMOTE_REASON_TIMEOUT :
            update_remote_download.write_error ?
            UPDATE_REMOTE_REASON_IO : UPDATE_REMOTE_REASON_HTTP;
        return update_remote_abort_pending(reason, result, output);
    }
    if (!crypto_equal(
            update_remote_package_hash,
            update_remote_status.candidate.package_hash, 32U)) {
        return update_remote_abort_pending(
            UPDATE_REMOTE_REASON_PACKAGE_HASH, ERR_INVALID, output);
    }
    update_remote_status.state = UPDATE_REMOTE_STATE_VALIDATING;
    kmemset(
        &update_remote_verification, 0,
        sizeof(update_remote_verification));
    result = update_verify_file(
        update_remote_package_aliases[pending_slot],
        &update_remote_verification);
    output->verification_reason = update_remote_verification.reason;
    if (result != OK) {
        return update_remote_abort_pending(
            UPDATE_REMOTE_REASON_PACKAGE_VERIFY, result, output);
    }
    if (!update_remote_candidate_matches_package(
            &update_remote_verification)) {
        return update_remote_abort_pending(
            UPDATE_REMOTE_REASON_PACKAGE_MISMATCH, ERR_INVALID, output);
    }
    return OK;
}

int update_remote_init(void) {
    int result;

    LOG_INFO("UPDATE", "Inicializando distribuicao remota");
    kmemset(&update_remote_status, 0, sizeof(update_remote_status));
    kmemset(&update_remote_record, 0, sizeof(update_remote_record));
    update_remote_record.active_slot = UPDATE_REMOTE_SLOT_NONE;
    update_remote_record.pending_slot = UPDATE_REMOTE_SLOT_NONE;
    update_remote_record_slot = -1;
    update_remote_manifest_valid = 0U;
    update_remote_cancelled = 0U;
    update_remote_status.cache_store =
        fs_get_type() == FS_TYPE_FAT12 ?
        UPDATE_REMOTE_STORE_EMPTY : UPDATE_REMOTE_STORE_UNAVAILABLE;
    if (!update_is_ready()) {
        LOG_ERROR("UPDATE", "Verificador local indisponivel para remoto");
        return ERR_STATE;
    }
    if (fs_get_type() == FS_TYPE_FAT12) {
        result = update_remote_load_records();
        if (result == OK) result = update_remote_recover_cache();
        if (result != OK) {
            update_remote_status.cache_store = UPDATE_REMOTE_STORE_INVALID;
            LOG_WARN("UPDATE", "Cache remoto local esta degradado");
        }
    }
    update_remote_status.initialized = 1U;
    update_remote_status.state = UPDATE_REMOTE_STATE_DISABLED;
    update_remote_status.reason = UPDATE_REMOTE_REASON_DISABLED;
    update_remote_copy_text(
        update_remote_status.manifest_url,
        sizeof(update_remote_status.manifest_url),
        UPDATE_REMOTE_DEFAULT_MANIFEST_URL);
    LOG_INFO("UPDATE", "Distribuicao remota inicializada e desabilitada");
    return OK;
}

int update_remote_enable(void) {
    if (!update_remote_status.initialized) {
        LOG_ERROR("UPDATE", "Habilitacao remota antes da inicializacao");
        return ERR_STATE;
    }
    if (update_remote_status.busy) {
        LOG_ERROR("UPDATE", "Habilitacao remota durante operacao ativa");
        return ERR_STATE;
    }
    update_remote_status.enabled = 1U;
    update_remote_status.reason = UPDATE_REMOTE_REASON_NONE;
    update_remote_refresh_network();
    if (update_remote_status.network_ready) {
        update_remote_status.state = UPDATE_REMOTE_STATE_READY;
    }
    LOG_INFO("UPDATE", "Distribuicao remota habilitada nesta sessao");
    return OK;
}

int update_remote_disable(void) {
    if (!update_remote_status.initialized) {
        LOG_ERROR("UPDATE", "Desabilitacao remota antes da inicializacao");
        return ERR_STATE;
    }
    if (update_remote_status.busy) {
        LOG_ERROR("UPDATE", "Desabilitacao recusada durante transferencia");
        return ERR_STATE;
    }
    update_remote_status.enabled = 0U;
    update_remote_status.manifest_cached = 0U;
    update_remote_manifest_valid = 0U;
    update_remote_status.state = UPDATE_REMOTE_STATE_DISABLED;
    update_remote_status.reason = UPDATE_REMOTE_REASON_DISABLED;
    update_remote_status.bytes_received = 0U;
    update_remote_status.total_bytes = 0U;
    http_reset();
    LOG_INFO("UPDATE", "Distribuicao remota desabilitada");
    return OK;
}

int update_remote_check(const char* manifest_url,
                        const update_remote_options_t* options,
                        update_remote_result_t* result_out) {
    update_remote_options_t defaults = {1U, 0, 0};
    int result;

    if (!result_out) {
        LOG_ERROR("UPDATE", "Destino nulo na consulta remota");
        return ERR_NULL;
    }
    if (!options) options = &defaults;
    kmemset(result_out, 0, sizeof(*result_out));
    result = update_remote_begin();
    if (result != OK) return result;
    result = update_remote_check_internal(
        manifest_url, options, result_out);
    if (result == OK) update_remote_status.busy = 0U;
    return result;
}

int update_remote_fetch(const char* manifest_url,
                        const update_remote_options_t* options,
                        update_remote_result_t* result_out) {
    update_remote_options_t defaults = {0U, 0, 0};
    uint8_t expected_manifest_hash[32];
    const char* effective;
    uint8_t pending_slot;
    int result;

    if (!result_out) {
        LOG_ERROR("UPDATE", "Destino nulo no download remoto");
        return ERR_NULL;
    }
    if (!options) options = &defaults;
    kmemset(result_out, 0, sizeof(*result_out));
    if (options->dry_run) return update_remote_check(
        manifest_url, options, result_out);
    if (update_remote_begin() != OK) return ERR_STATE;
    if (!update_remote_manifest_valid) {
        return update_remote_reject(
            UPDATE_REMOTE_REASON_MANIFEST_FORMAT, ERR_STATE,
            "Download exige consulta previa do manifesto", result_out);
    }
    kmemcpy(expected_manifest_hash, update_remote_manifest_hash, 32U);
    effective = update_remote_effective_url(manifest_url);
    result = update_remote_check_internal(
        effective, options, result_out);
    if (result != OK) return result;
    update_remote_status.busy = 1U;
    if (!crypto_equal(
            expected_manifest_hash, update_remote_manifest_hash, 32U)) {
        return update_remote_reject(
            UPDATE_REMOTE_REASON_MANIFEST_FORMAT, ERR_STATE,
            "Manifesto mudou depois da confirmacao", result_out);
    }
    if (fs_get_type() != FS_TYPE_FAT12) {
        return update_remote_reject(
            UPDATE_REMOTE_REASON_CACHE, ERR_UNAVAILABLE,
            "Cache remoto requer FAT12", result_out);
    }
    if (!update_remote_space_available(
            update_remote_status.candidate.package_size)) {
        return update_remote_reject(
            UPDATE_REMOTE_REASON_SPACE, ERR_OVERFLOW,
            "Espaco insuficiente para cache remoto", result_out);
    }
    result = update_remote_build_package_url(
        effective, update_remote_status.candidate.package_path,
        update_remote_package_url);
    if (result != OK) {
        return update_remote_reject(
            UPDATE_REMOTE_REASON_MANIFEST_FORMAT, result,
            "Caminho de pacote remoto invalido", result_out);
    }
    pending_slot = update_remote_record.active_slot == 0U ? 1U : 0U;
    result_out->cache_preserved =
        update_remote_record.active_slot <= 1U ? 1U : 0U;
    if (update_remote_prepare_pending(pending_slot) != OK) {
        return update_remote_reject(
            UPDATE_REMOTE_REASON_IO, ERR_DISK,
            "Falha ao preparar cache remoto", result_out);
    }
    result = update_remote_receive_package(
        update_remote_package_url, pending_slot, options, result_out);
    if (result != OK) return result;
    if (update_remote_commit_package(
            pending_slot, update_remote_package_hash,
            result_out) != OK) {
        return update_remote_abort_pending(
            UPDATE_REMOTE_REASON_IO, ERR_DISK, result_out);
    }
    update_remote_end_ready();
    update_remote_result_copy(result_out);
    LOG_INFO("UPDATE", "Pacote remoto autenticado e publicado no cache");
    return OK;
}

int update_remote_clear(const update_remote_options_t* options,
                        update_remote_result_t* result_out) {
    update_remote_options_t defaults = {1U, 0, 0};
    int result = OK;

    if (!result_out) {
        LOG_ERROR("UPDATE", "Destino nulo ao limpar cache remoto");
        return ERR_NULL;
    }
    if (!options) options = &defaults;
    kmemset(result_out, 0, sizeof(*result_out));
    if (update_remote_begin() != OK) return ERR_STATE;
    if (fs_get_type() != FS_TYPE_FAT12) {
        return update_remote_reject(
            UPDATE_REMOTE_REASON_CACHE, ERR_UNAVAILABLE,
            "Limpeza de cache remoto requer FAT12", result_out);
    }
    if (options->dry_run) {
        result_out->cache_preserved =
            update_remote_status.package_cached;
        update_remote_end_ready();
        update_remote_result_copy(result_out);
        return OK;
    }
    for (uint32_t slot = 0U; slot < 2U; slot++) {
        if (update_remote_delete_root(
                update_remote_package_aliases[slot]) != OK ||
            update_remote_delete_root(
                update_remote_record_aliases[slot]) != OK) {
            result = ERR_DISK;
        }
    }
    if (result != OK) {
        return update_remote_reject(
            UPDATE_REMOTE_REASON_IO, result,
            "Falha ao limpar cache remoto", result_out);
    }
    kmemset(&update_remote_record, 0, sizeof(update_remote_record));
    update_remote_record.active_slot = UPDATE_REMOTE_SLOT_NONE;
    update_remote_record.pending_slot = UPDATE_REMOTE_SLOT_NONE;
    update_remote_record_slot = -1;
    update_remote_status.cache_store = UPDATE_REMOTE_STORE_EMPTY;
    update_remote_status.package_cached = 0U;
    update_remote_status.cached_alias[0] = '\0';
    update_remote_end_ready();
    update_remote_result_copy(result_out);
    LOG_INFO("UPDATE", "Cache remoto removido");
    return OK;
}

int update_remote_get_status(update_remote_status_t* status_out) {
    if (!status_out) {
        LOG_ERROR("UPDATE", "Destino nulo no status remoto");
        return ERR_NULL;
    }
    if (!update_remote_status.initialized) return ERR_STATE;
    update_remote_refresh_network();
    *status_out = update_remote_status;
    return OK;
}

int update_remote_get_cached_alias(char* alias_out, uint32_t capacity) {
    if (!alias_out || !capacity) {
        LOG_ERROR("UPDATE", "Destino nulo para alias do cache remoto");
        return ERR_NULL;
    }
    if (!update_remote_status.initialized ||
        !update_remote_status.package_cached) {
        LOG_WARN("UPDATE", "Nenhum pacote remoto esta armazenado");
        return ERR_NOT_FOUND;
    }
    if (kstrlen(update_remote_status.cached_alias) + 1U > capacity) {
        LOG_ERROR("UPDATE", "Destino pequeno para alias remoto");
        return ERR_OVERFLOW;
    }
    update_remote_copy_text(
        alias_out, capacity, update_remote_status.cached_alias);
    return OK;
}

int update_remote_capability_available(void) {
    if (!update_remote_status.initialized) return 0;
    update_remote_refresh_network();
    return update_remote_status.initialized &&
           update_remote_status.enabled &&
           update_remote_status.network_ready;
}

const char* update_remote_state_name(update_remote_state_t state) {
    if (state == UPDATE_REMOTE_STATE_DISABLED) return "DISABLED";
    if (state == UPDATE_REMOTE_STATE_UNAVAILABLE) return "UNAVAILABLE";
    if (state == UPDATE_REMOTE_STATE_READY) return "READY";
    if (state == UPDATE_REMOTE_STATE_CHECKING) return "CHECKING";
    if (state == UPDATE_REMOTE_STATE_AVAILABLE) return "AVAILABLE";
    if (state == UPDATE_REMOTE_STATE_DOWNLOADING) return "DOWNLOADING";
    if (state == UPDATE_REMOTE_STATE_VALIDATING) return "VALIDATING";
    if (state == UPDATE_REMOTE_STATE_FAILED) return "FAILED";
    return "UNKNOWN";
}

const char* update_remote_reason_name(update_remote_reason_t reason) {
    static const char* names[] = {
        "NONE", "DISABLED", "NETWORK", "HTTP", "TIMEOUT",
        "MANIFEST_FORMAT", "UNKNOWN_KEY", "MANIFEST_SIGNATURE",
        "VERSION", "SIZE", "SPACE", "IO", "CANCELLED",
        "PACKAGE_HASH", "PACKAGE_VERIFY", "PACKAGE_MISMATCH", "CACHE",
        "RELEASE_NOT_FOUND", "RELEASE_FORMAT", "RELEASE_TAG",
        "RELEASE_ASSET", "RELEASE_CHANGED"
    };

    if ((uint32_t)reason >= sizeof(names) / sizeof(names[0])) {
        return "UNKNOWN";
    }
    return names[reason];
}

const char* update_remote_store_name(update_remote_store_t state) {
    if (state == UPDATE_REMOTE_STORE_UNAVAILABLE) return "UNAVAILABLE";
    if (state == UPDATE_REMOTE_STORE_EMPTY) return "EMPTY";
    if (state == UPDATE_REMOTE_STORE_VALID) return "VALID";
    if (state == UPDATE_REMOTE_STORE_INVALID) return "INVALID";
    return "UNKNOWN";
}
