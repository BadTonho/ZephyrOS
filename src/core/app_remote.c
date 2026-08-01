#include "core/app_remote.h"
#include "core/app_remote_config.h"
#include "core/app_remote_trust.h"
#include "core/crypto.h"
#include "core/errors.h"
#include "core/http.h"
#include "core/log.h"
#include "core/network_manager.h"
#include "core/string.h"
#include "fs/fs.h"
#include "process/process.h"

#define APP_REMOTE_FORMAT_VERSION 1U
#define APP_REMOTE_CHANNEL_STABLE_TEST 1U
#define APP_REMOTE_HASH_SHA256 1U
#define APP_REMOTE_SIGNATURE_ED25519 1U
#define APP_REMOTE_HTTP_OK 200U
#define APP_REMOTE_WAIT_TICKS 1U
#define APP_REMOTE_RECORD_SIZE 512U
#define APP_REMOTE_RECORD_HASH_OFFSET 480U
#define APP_REMOTE_RECORD_VERSION 1U
#define APP_REMOTE_SLOT_NONE 0xFFU
#define APP_REMOTE_PHASE_CLEAN 0U
#define APP_REMOTE_PHASE_WRITING 1U
#define APP_REMOTE_DIRECTORY_ATTRIBUTE \
    (APP_PACKAGE_DIRECTORY_ATTRIBUTE | FS_ATTRIBUTE_HIDDEN | \
     FS_ATTRIBUTE_SYSTEM)
#define APP_REMOTE_FILE_ATTRIBUTE \
    (FS_ATTRIBUTE_HIDDEN | FS_ATTRIBUTE_SYSTEM | FS_ATTRIBUTE_ARCHIVE)
#define APP_REMOTE_PROVENANCE_MAX 32U
#define APP_REMOTE_PROVENANCE_VERSION 1U
#define APP_REMOTE_PROVENANCE_MAGIC 0x31505341U
#define APP_REMOTE_PROVENANCE_HASH_SIZE 32U

typedef struct {
    uint32_t sequence;
    uint32_t highest_generation;
    uint32_t cache_generation;
    uint8_t phase;
    uint8_t active_slot;
    uint8_t pending_slot;
    uint8_t cached_count;
    char target_id[APP_PACKAGE_ID_SIZE];
    char cached_ids[APP_REMOTE_MAX_ENTRIES][APP_PACKAGE_ID_SIZE];
    uint8_t catalog_hash[32];
} app_remote_record_t;

typedef struct {
    char id[APP_PACKAGE_ID_SIZE];
    char version[APP_PACKAGE_VERSION_TEXT_SIZE];
    uint8_t key_id[16];
    uint8_t app_hash[32];
    uint8_t metadata_hash[32];
    uint32_t generation;
    uint32_t sequence;
} app_remote_provenance_entry_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    uint32_t count;
    app_remote_provenance_entry_t entries[APP_REMOTE_PROVENANCE_MAX];
    uint8_t hash[APP_REMOTE_PROVENANCE_HASH_SIZE];
} app_remote_provenance_t;

typedef struct {
    app_package_plan_t* plan;
    uint8_t visiting[APP_REMOTE_MAX_ENTRIES];
    uint8_t visited[APP_REMOTE_MAX_ENTRIES];
    int update_target;
    int allow_downgrade;
    app_remote_reason_t reason;
} app_remote_plan_context_t;

static const char* app_remote_directories[2] = {
    "ASCACHE0", "ASCACHE1"
};
static const char* app_remote_record_paths[2] = {
    "ASR0.STA", "ASR1.STA"
};
static const char* app_remote_provenance_paths[2] = {
    "ASP0.DAT", "ASP1.DAT"
};
static const uint8_t app_remote_domain[] =
    "ZEPHYROS-APP-CATALOG-V1";

static app_remote_status_t app_remote_status;
static app_remote_entry_t app_remote_entries[APP_REMOTE_MAX_ENTRIES];
static app_remote_entry_t app_remote_candidate_entries[APP_REMOTE_MAX_ENTRIES];
static app_remote_record_t app_remote_record;
static app_remote_provenance_t app_remote_provenance;
static app_remote_result_t app_remote_internal_result;
static app_package_plan_t app_remote_active_plan;
/* A pilha do Shell possui 4 KiB. O status AS4 inclui ate 32 rollbacks e deve
   permanecer fora do caminho profundo de aplicacao FAT12. */
static app_package_status_t app_remote_package_status;
static char app_remote_pending_ids[APP_REMOTE_MAX_ENTRIES][APP_PACKAGE_ID_SIZE];
static uint8_t app_remote_pending_count;
static uint8_t app_remote_catalog[APP_REMOTE_MAX_CATALOG_SIZE];
static uint32_t app_remote_catalog_size;
static uint8_t app_remote_package_buffer[HTTP_BODY_CAPACITY];
static uint8_t app_remote_metadata_buffer[APP_PACKAGE_MAX_MANIFEST_SIZE];
static uint8_t app_remote_record_raw[2][APP_REMOTE_RECORD_SIZE];
static uint8_t app_remote_authenticated_cache_hash[32];
static uint8_t app_remote_authenticated_catalog_hash[32];
static uint8_t app_remote_candidate_catalog_hash[32];
static crypto_ed25519_verify_ctx_t app_remote_signature;
static http_status_t app_remote_http;
static int app_remote_record_slot = -1;
static int app_remote_provenance_slot = -1;
static uint8_t app_remote_cancel_requested;
static uint8_t app_remote_last_cancelled;
static uint8_t app_remote_fail_after;
static uint8_t app_remote_cache_signature_authenticated;
static uint8_t app_remote_catalog_signature_authenticated;
static uint8_t app_remote_provenance_reliable;
static uint8_t app_remote_provenance_trusted[APP_REMOTE_PROVENANCE_MAX];

static uint16_t app_remote_read_u16(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t app_remote_read_u32(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void app_remote_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void app_remote_write_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void app_remote_copy_text(char* output, uint32_t capacity,
                                 const char* input) {
    uint32_t index = 0U;

    if (!output || capacity == 0U) return;
    if (!input) input = "";
    while (input[index] && index + 1U < capacity) {
        output[index] = input[index];
        index++;
    }
    output[index] = '\0';
}

static int app_remote_fixed_text(const uint8_t* raw, uint32_t size,
                                 char* output, uint32_t capacity) {
    uint32_t end = size;

    if (!raw || !output || capacity == 0U) {
        LOG_ERROR("APPREMOTE", "Campo fixo recebeu argumento nulo");
        return ERR_NULL;
    }
    for (uint32_t index = 0; index < size; index++) {
        if (raw[index] == 0U) {
            end = index;
            break;
        }
        if (raw[index] < 0x20U || raw[index] > 0x7EU) return ERR_INVALID;
    }
    if (end == 0U || end == size || end >= capacity) return ERR_INVALID;
    for (uint32_t index = end + 1U; index < size; index++) {
        if (raw[index] != 0U) return ERR_INVALID;
    }
    for (uint32_t index = 0; index < end; index++) output[index] = (char)raw[index];
    output[end] = '\0';
    return OK;
}

static int app_remote_id_valid(const char* id) {
    uint32_t length = id ? kstrlen(id) : 0U;

    if (length == 0U || length >= APP_PACKAGE_ID_SIZE) return 0;
    for (uint32_t index = 0; index < length; index++) {
        char value = id[index];
        if (!((value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '_')) return 0;
    }
    return 1;
}

static int app_remote_path_valid(const char* path, const char* id) {
    uint32_t length = path ? kstrlen(path) : 0U;
    uint32_t id_length = id ? kstrlen(id) : 0U;
    uint32_t name_start;

    if (!path || !id || length < id_length + 5U || path[0] != '/' ||
        path[1] == '/' || path[length - 1U] == '/') return 0;
    for (uint32_t index = 0; index < length; index++) {
        char value = path[index];
        int allowed = (value >= 'A' && value <= 'Z') ||
                      (value >= 'a' && value <= 'z') ||
                      (value >= '0' && value <= '9') || value == '/' ||
                      value == '.' || value == '-' || value == '_';
        if (!allowed ||
            (value == '/' && index + 1U < length &&
             (path[index + 1U] == '/' ||
              (path[index + 1U] == '.' &&
               (index + 2U == length || path[index + 2U] == '/' ||
                (path[index + 2U] == '.' &&
                 (index + 3U == length || path[index + 3U] == '/'))))))) {
            return 0;
        }
    }
    name_start = length - id_length - 4U;
    if (name_start == 0U || path[name_start - 1U] != '/') return 0;
    for (uint32_t index = 0; index < id_length; index++) {
        if (path[name_start + index] != id[index]) return 0;
    }
    return kstrcmp(path + length - 4U, ".ZPK") == 0;
}

static int app_remote_key_revoked(const uint8_t key_id[16]) {
    for (uint32_t index = 0; index < APP_REMOTE_REVOKED_KEY_COUNT; index++) {
        if (crypto_equal(key_id, APP_REMOTE_REVOKED_KEY_IDS[index], 16U)) {
            return 1;
        }
    }
    return 0;
}

static int app_remote_network_ready(void) {
    network_manager_status_t network;

    if (network_manager_get_status(&network) != OK) return 0;
    return network.http_available && network.ipv4_configured;
}

static void app_remote_result_reset(app_remote_result_t* output) {
    if (output) kmemset(output, 0, sizeof(*output));
}

static void app_remote_result_status(app_remote_result_t* output) {
    if (!output) return;
    output->reason = app_remote_status.reason;
    output->http_status = app_remote_status.http_status;
    output->bytes_received = app_remote_status.bytes_received;
}

static int app_remote_fail(app_remote_reason_t reason, int error,
                           const char* message, app_remote_result_t* output) {
    app_remote_status.reason = reason;
    app_remote_status.state = APP_REMOTE_STATE_FAILED;
    app_remote_status.busy = 0U;
    if (message) LOG_ERROR("APPREMOTE", message);
    if (output) {
        output->reason = reason;
        output->http_status = app_remote_status.http_status;
        output->bytes_received = app_remote_status.bytes_received;
    }
    return error;
}

static int app_remote_begin(app_remote_result_t* output) {
    if (!app_remote_status.initialized) {
        return app_remote_fail(APP_REMOTE_REASON_CACHE_UNAVAILABLE, ERR_STATE,
                               "Servico remoto nao inicializado", output);
    }
    if (app_remote_status.busy) {
        if (output) output->reason = APP_REMOTE_REASON_MUTATION_BUSY;
        LOG_WARN("APPREMOTE", "Outra operacao remota esta ativa");
        return ERR_STATE;
    }
    app_remote_status.busy = 1U;
    app_remote_status.reason = APP_REMOTE_REASON_NONE;
    return OK;
}

static void app_remote_end_ready(void) {
    app_remote_status.busy = 0U;
    app_remote_status.reason = APP_REMOTE_REASON_NONE;
    app_remote_status.state = app_remote_status.catalog_available ?
                              APP_REMOTE_STATE_AVAILABLE :
                              APP_REMOTE_STATE_READY;
}

static int app_remote_wait_http(const app_remote_options_t* options) {
    while (1) {
        if (http_get_status(&app_remote_http) != OK) {
            LOG_ERROR("APPREMOTE", "Falha ao consultar HTTP");
            return ERR_STATE;
        }
        app_remote_status.http_status = app_remote_http.status_code;
        app_remote_status.bytes_received = app_remote_http.body_length;
        if (app_remote_http.state == HTTP_STATE_COMPLETE) return OK;
        if (app_remote_http.state == HTTP_STATE_FAILED) {
            LOG_WARN("APPREMOTE", "Transferencia HTTP falhou");
            return app_remote_http.last_error == OK ?
                   ERR_STATE : app_remote_http.last_error;
        }
        if (app_remote_cancel_requested ||
            (options && options->cancel_check &&
             options->cancel_check(options->cancel_context))) {
            http_reset();
            app_remote_cancel_requested = 0U;
            app_remote_last_cancelled = 1U;
            LOG_WARN("APPREMOTE", "Transferencia remota cancelada");
            return ERR_TIMEOUT;
        }
        process_block(APP_REMOTE_WAIT_TICKS);
    }
}

static int app_remote_http_get(const char* url,
                               const app_remote_options_t* options,
                               const uint8_t** body_out,
                               uint32_t* size_out) {
    int result;

    if (!url || !body_out || !size_out) {
        LOG_ERROR("APPREMOTE", "HTTP remoto recebeu argumento nulo");
        return ERR_NULL;
    }
    app_remote_cancel_requested = 0U;
    app_remote_last_cancelled = 0U;
    result = http_get_start(url);
    if (result == OK) result = app_remote_wait_http(options);
    if (result != OK) return result;
    if (app_remote_http.status_code != APP_REMOTE_HTTP_OK ||
        http_get_body(body_out, size_out) != OK) {
        LOG_ERROR("APPREMOTE", "Resposta HTTP remota invalida");
        return ERR_INVALID;
    }
    return OK;
}

static app_remote_reason_t app_remote_transport_reason(int error) {
    if (error == ERR_TIMEOUT) {
        return app_remote_last_cancelled ? APP_REMOTE_REASON_CANCELLED :
                                           APP_REMOTE_REASON_TIMEOUT;
    }
    return APP_REMOTE_REASON_HTTP;
}

static app_remote_reason_t app_remote_package_reason(
    app_package_action_reason_t reason) {
    switch (reason) {
        case APP_PACKAGE_ACTION_REASON_INSUFFICIENT_SPACE:
            return APP_REMOTE_REASON_SPACE;
        case APP_PACKAGE_ACTION_REASON_LOADER_BUSY:
        case APP_PACKAGE_ACTION_REASON_MUTATION_BUSY:
        case APP_PACKAGE_ACTION_REASON_TRANSACTION_PENDING:
            return APP_REMOTE_REASON_MUTATION_BUSY;
        case APP_PACKAGE_ACTION_REASON_READ_ERROR:
        case APP_PACKAGE_ACTION_REASON_WRITE_ERROR:
            return APP_REMOTE_REASON_IO;
        case APP_PACKAGE_ACTION_REASON_PACKAGE_INVALID:
        case APP_PACKAGE_ACTION_REASON_ALIAS_MISMATCH:
            return APP_REMOTE_REASON_PACKAGE_VERIFY;
        case APP_PACKAGE_ACTION_REASON_SOURCE_NOT_FOUND:
            return APP_REMOTE_REASON_CACHE_INVALID;
        case APP_PACKAGE_ACTION_REASON_FILESYSTEM_UNAVAILABLE:
        case APP_PACKAGE_ACTION_REASON_LOADER_UNAVAILABLE:
        case APP_PACKAGE_ACTION_REASON_PACKAGE_SERVICE_UNAVAILABLE:
        case APP_PACKAGE_ACTION_REASON_TRANSACTION_UNAVAILABLE:
            return APP_REMOTE_REASON_CACHE_UNAVAILABLE;
        case APP_PACKAGE_ACTION_REASON_DEPENDENCY_MISSING:
        case APP_PACKAGE_ACTION_REASON_PLAN_INCOMPLETE:
            return APP_REMOTE_REASON_PLAN_INCOMPLETE;
        case APP_PACKAGE_ACTION_REASON_PLAN_CYCLE:
            return APP_REMOTE_REASON_PLAN_CYCLE;
        default:
            return APP_REMOTE_REASON_PLAN_CONFLICT;
    }
}

static int app_remote_verify_signature(const uint8_t* raw,
                                       uint32_t signed_size) {
    const uint8_t* signature = raw + signed_size;
    int result = crypto_ed25519_verify_init(
        &app_remote_signature, signature, APP_REMOTE_TRUST_PUBLIC_KEY);

    if (result == OK) {
        result = crypto_ed25519_verify_update(
            &app_remote_signature, app_remote_domain,
            sizeof(app_remote_domain));
    }
    if (result == OK) {
        result = crypto_ed25519_verify_update(
            &app_remote_signature, raw, signed_size);
    }
    if (result == OK) result = crypto_ed25519_verify_final(&app_remote_signature);
    return result;
}

static int app_remote_decode_entry(const uint8_t* raw,
                                   app_remote_entry_t* entry,
                                   app_remote_reason_t* reason_out) {
    uint8_t dependency_count;
    int version_comparison;

    if (!raw || !entry || !reason_out) {
        LOG_ERROR("APPREMOTE", "Entrada ZAC1 recebeu argumento nulo");
        return ERR_NULL;
    }
    *reason_out = APP_REMOTE_REASON_CATALOG_FORMAT;
    kmemset(entry, 0, sizeof(*entry));
    if (app_remote_fixed_text(raw, 9U, entry->info.id,
                              sizeof(entry->info.id)) != OK ||
        app_remote_fixed_text(raw + 9U, 32U, entry->info.name,
                              sizeof(entry->info.name)) != OK ||
        app_remote_fixed_text(raw + 41U, 16U, entry->info.version,
                              sizeof(entry->info.version)) != OK) return ERR_INVALID;
    dependency_count = raw[57U];
    if (!app_remote_id_valid(entry->info.id) ||
        app_package_compare_versions(entry->info.version,
                                     entry->info.version,
                                     &version_comparison) != OK ||
        dependency_count > APP_PACKAGE_MAX_DEPENDENCIES ||
        raw[94U] != 0U || raw[95U] != 0U) return ERR_INVALID;
    entry->info.dependency_count = dependency_count;
    for (uint32_t index = 0; index < APP_PACKAGE_MAX_DEPENDENCIES; index++) {
        const uint8_t* dependency = raw + 58U + index * 9U;

        if (index < dependency_count) {
            if (app_remote_fixed_text(
                    dependency, 9U, entry->info.dependencies[index],
                    sizeof(entry->info.dependencies[index])) != OK ||
                !app_remote_id_valid(entry->info.dependencies[index])) {
                return ERR_INVALID;
            }
            for (uint32_t previous = 0; previous < index; previous++) {
                if (kstrcmp(entry->info.dependencies[previous],
                            entry->info.dependencies[index]) == 0) {
                    *reason_out = APP_REMOTE_REASON_PLAN_CONFLICT;
                    return ERR_INVALID;
                }
            }
        } else {
            for (uint32_t byte = 0; byte < 9U; byte++) {
                if (dependency[byte] != 0U) return ERR_INVALID;
            }
        }
    }
    entry->package_size = app_remote_read_u32(raw + 96U);
    kmemcpy(entry->package_hash, raw + 100U, 32U);
    if (app_remote_fixed_text(raw + 132U, APP_REMOTE_PATH_SIZE,
                              entry->package_path,
                              sizeof(entry->package_path)) != OK ||
        !app_remote_path_valid(entry->package_path, entry->info.id)) {
        *reason_out = APP_REMOTE_REASON_PATH;
        return ERR_INVALID;
    }
    if (entry->package_size == 0U ||
        entry->package_size > HTTP_BODY_CAPACITY) {
        *reason_out = APP_REMOTE_REASON_SIZE;
        return ERR_INVALID;
    }
    if (app_remote_read_u32(raw + 232U) != 0U) return ERR_INVALID;
    for (uint32_t index = 236U; index < APP_REMOTE_ENTRY_SIZE; index++) {
        if (raw[index] != 0U) return ERR_INVALID;
    }
    return OK;
}

static int app_remote_parse_catalog(const uint8_t* raw, uint32_t size,
                                    uint32_t minimum_generation,
                                    int signature_authenticated,
                                    app_remote_reason_t* reason_out) {
    uint32_t generation;
    uint16_t count;
    uint32_t signed_size;
    uint8_t entries_hash[32];
    uint8_t catalog_hash[32];

    if (!raw || !reason_out) {
        LOG_ERROR("APPREMOTE", "Parser ZAC1 recebeu argumento nulo");
        return ERR_NULL;
    }
    *reason_out = APP_REMOTE_REASON_CATALOG_FORMAT;
    if (size < APP_REMOTE_HEADER_SIZE + APP_REMOTE_SIGNATURE_SIZE ||
        raw[0] != 'Z' || raw[1] != 'A' || raw[2] != 'C' || raw[3] != '1' ||
        app_remote_read_u16(raw + 4U) != APP_REMOTE_FORMAT_VERSION ||
        app_remote_read_u16(raw + 6U) != APP_REMOTE_HEADER_SIZE ||
        app_remote_read_u16(raw + 8U) != APP_REMOTE_ENTRY_SIZE) return ERR_INVALID;
    count = app_remote_read_u16(raw + 10U);
    generation = app_remote_read_u32(raw + 12U);
    signed_size = APP_REMOTE_HEADER_SIZE + (uint32_t)count * APP_REMOTE_ENTRY_SIZE;
    if (count == 0U || count > APP_REMOTE_MAX_ENTRIES ||
        size != signed_size + APP_REMOTE_SIGNATURE_SIZE || generation == 0U ||
        app_remote_read_u16(raw + 16U) != APP_REMOTE_CHANNEL_STABLE_TEST ||
        app_remote_read_u16(raw + 18U) != APP_REMOTE_HASH_SHA256 ||
        app_remote_read_u16(raw + 20U) != APP_REMOTE_SIGNATURE_ED25519 ||
        app_remote_read_u16(raw + 22U) != 0U) return ERR_INVALID;
    for (uint32_t index = 72U; index < APP_REMOTE_HEADER_SIZE; index++) {
        if (raw[index] != 0U) return ERR_INVALID;
    }
    if (app_remote_key_revoked(raw + 24U)) {
        *reason_out = APP_REMOTE_REASON_REVOKED_KEY;
        return ERR_INVALID;
    }
    if (!crypto_equal(raw + 24U, APP_REMOTE_TRUST_KEY_ID, 16U)) {
        *reason_out = APP_REMOTE_REASON_UNKNOWN_KEY;
        return ERR_INVALID;
    }
    if (!signature_authenticated &&
        app_remote_verify_signature(raw, signed_size) != OK) {
        *reason_out = APP_REMOTE_REASON_SIGNATURE;
        return ERR_INVALID;
    }
    if (generation < minimum_generation) {
        *reason_out = APP_REMOTE_REASON_REPLAY;
        return ERR_INVALID;
    }
    if (generation == minimum_generation && minimum_generation > 0U) {
        if (crypto_sha256(raw, size, catalog_hash) != OK ||
            !crypto_equal(catalog_hash, app_remote_record.catalog_hash, 32U)) {
            *reason_out = APP_REMOTE_REASON_REPLAY;
            return ERR_INVALID;
        }
    }
    if (crypto_sha256(raw + APP_REMOTE_HEADER_SIZE,
                      (uint32_t)count * APP_REMOTE_ENTRY_SIZE,
                      entries_hash) != OK ||
        !crypto_equal(entries_hash, raw + 40U, 32U)) return ERR_INVALID;
    for (uint32_t index = 0; index < count; index++) {
        if (app_remote_decode_entry(
                raw + APP_REMOTE_HEADER_SIZE + index * APP_REMOTE_ENTRY_SIZE,
                &app_remote_candidate_entries[index], reason_out) != OK) {
            return ERR_INVALID;
        }
        if (index && kstrcmp(
                app_remote_candidate_entries[index - 1U].info.id,
                app_remote_candidate_entries[index].info.id) >= 0) {
            *reason_out = APP_REMOTE_REASON_DUPLICATE;
            return ERR_INVALID;
        }
    }
    kmemcpy(app_remote_entries, app_remote_candidate_entries,
            count * sizeof(app_remote_entries[0]));
    kmemset(app_remote_entries + count, 0, sizeof(app_remote_entries) -
            count * sizeof(app_remote_entries[0]));
    app_remote_status.generation = generation;
    app_remote_status.entry_count = count;
    kmemcpy(app_remote_status.key_id, raw + 24U, 16U);
    *reason_out = APP_REMOTE_REASON_NONE;
    return OK;
}

static int app_remote_find_index(const char* id) {
    if (!id) return -1;
    for (uint32_t index = 0; index < app_remote_status.entry_count; index++) {
        if (kstrcmp(app_remote_entries[index].info.id, id) == 0) return (int)index;
    }
    return -1;
}

static void app_remote_alias(const char* id, char alias[13]) {
    uint32_t length = kstrlen(id);

    kmemcpy(alias, id, length);
    alias[length] = '.';
    alias[length + 1U] = 'Z';
    alias[length + 2U] = 'P';
    alias[length + 3U] = 'K';
    alias[length + 4U] = '\0';
}

static void app_remote_classify_entries(void) {
    uint8_t catalog_hash[32];
    int catalog_matches_cache =
        app_remote_status.cache_state == APP_REMOTE_CACHE_VALID &&
        app_remote_record.active_slot <= 1U &&
        app_remote_status.generation == app_remote_record.cache_generation &&
        app_remote_catalog_size > 0U &&
        crypto_sha256(app_remote_catalog, app_remote_catalog_size,
                      catalog_hash) == OK &&
        crypto_equal(catalog_hash, app_remote_record.catalog_hash, 32U);

    for (uint32_t index = 0; index < app_remote_status.entry_count; index++) {
        app_remote_entry_t* entry = &app_remote_entries[index];
        app_package_info_t installed;
        int installed_result;
        int comparison = 0;

        installed_result = app_package_get_installed_info_by_id(
            entry->info.id, &installed);
        entry->installed = installed_result == OK;
        app_remote_copy_text(entry->installed_version,
                             sizeof(entry->installed_version),
                             entry->installed ? installed.version : "");
        entry->cached = 0U;
        if (catalog_matches_cache) {
            for (uint32_t cached = 0;
                 cached < app_remote_record.cached_count; cached++) {
                if (kstrcmp(app_remote_record.cached_ids[cached],
                            entry->info.id) == 0) {
                    entry->cached = 1U;
                    break;
                }
            }
        }
        entry->reason = APP_REMOTE_REASON_NONE;
        if (installed_result != OK && installed_result != ERR_NOT_FOUND) {
            entry->state = APP_REMOTE_ENTRY_BLOCKED;
            entry->reason = APP_REMOTE_REASON_IO;
        } else if (!entry->installed) {
            entry->state = entry->cached ? APP_REMOTE_ENTRY_CACHED :
                                           APP_REMOTE_ENTRY_AVAILABLE;
        } else if (app_package_compare_versions(
                       entry->info.version, installed.version,
                       &comparison) != OK) {
            entry->state = APP_REMOTE_ENTRY_BLOCKED;
            entry->reason = APP_REMOTE_REASON_PLAN_CONFLICT;
        } else if (comparison > 0) {
            entry->state = APP_REMOTE_ENTRY_UPDATE_AVAILABLE;
        } else if (comparison < 0) {
            entry->state = APP_REMOTE_ENTRY_DOWNGRADE;
        } else {
            entry->state = APP_REMOTE_ENTRY_SAME_VERSION;
        }
    }
}

static void app_remote_sort_dependencies(const app_package_info_t* info,
                                         char dependencies[4][9]) {
    for (uint32_t index = 0; index < info->dependency_count; index++) {
        app_remote_copy_text(dependencies[index], 9U,
                             info->dependencies[index]);
    }
    for (uint32_t index = 1; index < info->dependency_count; index++) {
        char item[9];
        uint32_t position = index;

        app_remote_copy_text(item, sizeof(item), dependencies[index]);
        while (position > 0U &&
               kstrcmp(dependencies[position - 1U], item) > 0) {
            app_remote_copy_text(dependencies[position], 9U,
                                 dependencies[position - 1U]);
            position--;
        }
        app_remote_copy_text(dependencies[position], 9U, item);
    }
}

static int app_remote_plan_append(app_remote_plan_context_t* context,
                                  uint32_t index, int target) {
    app_remote_entry_t* entry = &app_remote_entries[index];
    app_package_plan_entry_t* item;
    app_package_info_t installed;
    int installed_result = app_package_get_installed_info_by_id(
        entry->info.id, &installed);
    int has_installed = installed_result == OK;

    if (installed_result != OK && installed_result != ERR_NOT_FOUND) {
        LOG_WARN("APPREMOTE", "Estado instalado nao pode ser consultado");
        context->reason = APP_REMOTE_REASON_IO;
        return installed_result;
    }

    if (context->plan->entry_count >= APP_PACKAGE_MAX_PLAN_ENTRIES) {
        LOG_WARN("APPREMOTE", "Plano remoto excedeu o limite de entradas");
        context->reason = APP_REMOTE_REASON_PLAN_CONFLICT;
        return ERR_OVERFLOW;
    }
    item = &context->plan->entries[context->plan->entry_count];
    app_remote_copy_text(item->id, sizeof(item->id), entry->info.id);
    app_remote_alias(entry->info.id, item->alias);
    app_remote_copy_text(item->to_version, sizeof(item->to_version),
                         entry->info.version);
    if (target && context->update_target) {
        int comparison = 0;

        if (!has_installed || app_package_compare_versions(
                entry->info.version, installed.version, &comparison) != OK ||
            comparison == 0 || (comparison < 0 && !context->allow_downgrade)) {
            context->reason = APP_REMOTE_REASON_PLAN_CONFLICT;
            return ERR_STATE;
        }
        item->action = APP_PACKAGE_PLAN_ACTION_UPDATE;
        app_remote_copy_text(item->from_version, sizeof(item->from_version),
                             installed.version);
    } else {
        if (has_installed) {
            if (!target) return OK;
            LOG_WARN("APPREMOTE", "Instalacao remota ja esta instalada");
            context->reason = APP_REMOTE_REASON_PLAN_CONFLICT;
            return ERR_STATE;
        }
        item->action = APP_PACKAGE_PLAN_ACTION_INSTALL;
    }
    if (target) context->plan->target_index = context->plan->entry_count;
    context->plan->entry_count++;
    return OK;
}

static int app_remote_plan_visit(app_remote_plan_context_t* context,
                                 uint32_t index, int target) {
    char dependencies[APP_PACKAGE_MAX_DEPENDENCIES][APP_PACKAGE_ID_SIZE];
    app_remote_entry_t* entry = &app_remote_entries[index];

    if (context->visiting[index]) {
        LOG_WARN("APPREMOTE", "Ciclo detectado no plano remoto");
        context->reason = APP_REMOTE_REASON_PLAN_CYCLE;
        return ERR_STATE;
    }
    if (context->visited[index]) return OK;
    context->visiting[index] = 1U;
    kmemset(dependencies, 0, sizeof(dependencies));
    app_remote_sort_dependencies(&entry->info, dependencies);
    for (uint32_t dependency = 0;
         dependency < entry->info.dependency_count; dependency++) {
        int dependency_index;
        int installed_result = app_package_get_installed_info_by_id(
            dependencies[dependency],
            &app_remote_internal_result.package_action.info);

        if (installed_result == OK) continue;
        if (installed_result != ERR_NOT_FOUND) {
            LOG_WARN("APPREMOTE", "Dependencia instalada nao pode ser lida");
            context->reason = APP_REMOTE_REASON_IO;
            context->visiting[index] = 0U;
            return installed_result;
        }
        dependency_index = app_remote_find_index(dependencies[dependency]);
        if (dependency_index < 0) {
            context->reason = APP_REMOTE_REASON_PLAN_INCOMPLETE;
            context->visiting[index] = 0U;
            return ERR_NOT_FOUND;
        }
        if (app_remote_plan_visit(context, (uint32_t)dependency_index, 0) != OK) {
            context->visiting[index] = 0U;
            return ERR_STATE;
        }
    }
    context->visiting[index] = 0U;
    context->visited[index] = 1U;
    return app_remote_plan_append(context, index, target);
}

static int app_remote_build_plan_internal(const char* id, int update,
                                          int allow_downgrade,
                                          app_package_plan_t* plan,
                                          app_remote_reason_t* reason_out) {
    app_remote_plan_context_t context;
    int target_index;
    int result;

    if (!id || !plan || !reason_out) {
        LOG_ERROR("APPREMOTE", "Planejador remoto recebeu argumento nulo");
        return ERR_NULL;
    }
    target_index = app_remote_find_index(id);
    if (target_index < 0) {
        *reason_out = APP_REMOTE_REASON_PLAN_INCOMPLETE;
        return ERR_NOT_FOUND;
    }
    kmemset(plan, 0, sizeof(*plan));
    kmemset(&context, 0, sizeof(context));
    context.plan = plan;
    context.update_target = update;
    context.allow_downgrade = allow_downgrade;
    plan->allow_downgrade = allow_downgrade ? 1U : 0U;
    result = app_remote_plan_visit(&context, (uint32_t)target_index, 1);
    *reason_out = result == OK ? APP_REMOTE_REASON_NONE : context.reason;
    return result;
}

static void app_remote_record_encode(uint8_t raw[APP_REMOTE_RECORD_SIZE],
                                     const app_remote_record_t* record) {
    uint32_t offset = 33U;

    kmemset(raw, 0, APP_REMOTE_RECORD_SIZE);
    raw[0] = 'A'; raw[1] = 'S'; raw[2] = 'R'; raw[3] = '1';
    app_remote_write_u16(raw + 4U, APP_REMOTE_RECORD_VERSION);
    app_remote_write_u16(raw + 6U, APP_REMOTE_RECORD_SIZE);
    app_remote_write_u32(raw + 8U, record->sequence);
    app_remote_write_u32(raw + 12U, record->highest_generation);
    app_remote_write_u32(raw + 16U, record->cache_generation);
    raw[20U] = record->phase;
    raw[21U] = record->active_slot;
    raw[22U] = record->pending_slot;
    raw[23U] = record->cached_count;
    kmemcpy(raw + 24U, record->target_id, APP_PACKAGE_ID_SIZE);
    for (uint32_t index = 0; index < APP_REMOTE_MAX_ENTRIES; index++) {
        kmemcpy(raw + offset, record->cached_ids[index], APP_PACKAGE_ID_SIZE);
        offset += APP_PACKAGE_ID_SIZE;
    }
    kmemcpy(raw + offset, record->catalog_hash, 32U);
    crypto_sha256(raw, APP_REMOTE_RECORD_HASH_OFFSET,
                  raw + APP_REMOTE_RECORD_HASH_OFFSET);
}

static int app_remote_record_decode(const uint8_t* raw,
                                     app_remote_record_t* record) {
    uint8_t hash[32];
    uint32_t offset = 33U;
    int target_cached = 0;

    if (!raw || !record) {
        LOG_ERROR("APPREMOTE", "Registro remoto recebeu argumento nulo");
        return ERR_NULL;
    }
    if (raw[0] != 'A' || raw[1] != 'S' || raw[2] != 'R' || raw[3] != '1' ||
        app_remote_read_u16(raw + 4U) != APP_REMOTE_RECORD_VERSION ||
        app_remote_read_u16(raw + 6U) != APP_REMOTE_RECORD_SIZE ||
        crypto_sha256(raw, APP_REMOTE_RECORD_HASH_OFFSET, hash) != OK ||
        !crypto_equal(hash, raw + APP_REMOTE_RECORD_HASH_OFFSET, 32U)) {
        return ERR_INVALID;
    }
    kmemset(record, 0, sizeof(*record));
    record->sequence = app_remote_read_u32(raw + 8U);
    record->highest_generation = app_remote_read_u32(raw + 12U);
    record->cache_generation = app_remote_read_u32(raw + 16U);
    record->phase = raw[20U];
    record->active_slot = raw[21U];
    record->pending_slot = raw[22U];
    record->cached_count = raw[23U];
    kmemcpy(record->target_id, raw + 24U, APP_PACKAGE_ID_SIZE);
    if (record->phase > APP_REMOTE_PHASE_WRITING ||
        record->cached_count > APP_REMOTE_MAX_ENTRIES ||
        (record->active_slot != APP_REMOTE_SLOT_NONE && record->active_slot > 1U) ||
        (record->pending_slot != APP_REMOTE_SLOT_NONE && record->pending_slot > 1U) ||
        (record->phase == APP_REMOTE_PHASE_CLEAN &&
         record->pending_slot != APP_REMOTE_SLOT_NONE) ||
        (record->phase == APP_REMOTE_PHASE_WRITING &&
         (record->pending_slot > 1U ||
          record->pending_slot == record->active_slot)) ||
        raw[32U] != 0U) {
        return ERR_INVALID;
    }
    for (uint32_t index = 0; index < APP_REMOTE_MAX_ENTRIES; index++) {
        kmemcpy(record->cached_ids[index], raw + offset, APP_PACKAGE_ID_SIZE);
        if (index < record->cached_count) {
            if (raw[offset + APP_PACKAGE_ID_SIZE - 1U] != 0U ||
                !app_remote_id_valid(record->cached_ids[index])) {
                return ERR_INVALID;
            }
            for (uint32_t previous = 0; previous < index; previous++) {
                if (kstrcmp(record->cached_ids[previous],
                            record->cached_ids[index]) == 0) {
                    return ERR_INVALID;
                }
            }
            if (kstrcmp(record->cached_ids[index], record->target_id) == 0) {
                target_cached = 1;
            }
        } else {
            for (uint32_t byte = 0; byte < APP_PACKAGE_ID_SIZE; byte++) {
                if (raw[offset + byte] != 0U) return ERR_INVALID;
            }
        }
        offset += APP_PACKAGE_ID_SIZE;
    }
    if (record->active_slot == APP_REMOTE_SLOT_NONE) {
        for (uint32_t byte = 0; byte < APP_PACKAGE_ID_SIZE; byte++) {
            if (record->target_id[byte] != '\0') return ERR_INVALID;
        }
    } else {
        uint32_t target_length = kstrlen(record->target_id);

        for (uint32_t byte = target_length + 1U;
             byte < APP_PACKAGE_ID_SIZE; byte++) {
            if (record->target_id[byte] != '\0') return ERR_INVALID;
        }
    }
    if ((record->active_slot == APP_REMOTE_SLOT_NONE &&
         (record->cached_count != 0U || record->cache_generation != 0U ||
          record->target_id[0] != '\0')) ||
        (record->active_slot != APP_REMOTE_SLOT_NONE &&
         (!record->cached_count || !app_remote_id_valid(record->target_id) ||
          !target_cached))) {
        return ERR_INVALID;
    }
    kmemcpy(record->catalog_hash, raw + offset, 32U);
    return OK;
}

static int app_remote_write_record(void) {
    int slot = app_remote_record_slot < 0 ? 0 : 1 - app_remote_record_slot;
    int result;

    app_remote_record.sequence++;
    app_remote_record_encode(app_remote_record_raw[slot], &app_remote_record);
    result = fs_atomic_write_root(
        app_remote_record_paths[slot], app_remote_record_raw[slot],
        APP_REMOTE_RECORD_SIZE, APP_REMOTE_FILE_ATTRIBUTE,
        FS_ATOMIC_CREATE_OR_REPLACE);
    if (result == OK) app_remote_record_slot = slot;
    else LOG_ERROR("APPREMOTE", "Falha ao persistir estado remoto");
    return result;
}

static int app_remote_directory_exists(uint8_t slot) {
    int count;

    if (slot > 1U) return 0;
    count = fs_get_file_count_at("");
    for (int index = 0; index < count; index++) {
        char name[13];
        uint8_t attributes = 0U;

        if (fs_get_file_info_at("", index, name, 0, &attributes) != OK) {
            LOG_WARN("APPREMOTE", "Falha ao localizar slot de cache");
            continue;
        }
        if (kstrcmp(name, app_remote_directories[slot]) == 0) {
            return (attributes & APP_PACKAGE_DIRECTORY_ATTRIBUTE) != 0U;
        }
    }
    return 0;
}

static int app_remote_clear_directory(uint8_t slot) {
    const char* directory = app_remote_directories[slot];
    int count;

    if (!app_remote_directory_exists(slot)) return OK;
    count = fs_get_file_count_at(directory);
    while (count > 0) {
        char name[13];
        uint8_t attributes = 0U;

        if (fs_get_file_info_at(directory, 0, name, 0, &attributes) != OK ||
            (attributes & APP_PACKAGE_DIRECTORY_ATTRIBUTE) ||
            fs_atomic_delete_file_in_dir(directory, name) != OK) {
            LOG_ERROR("APPREMOTE", "Falha ao limpar slot de cache");
            return ERR_DISK;
        }
        count = fs_get_file_count_at(directory);
    }
    if (fs_delete_file_in_dir("", directory) != OK) {
        LOG_ERROR("APPREMOTE", "Falha ao remover diretorio de cache");
        return ERR_DISK;
    }
    return OK;
}

static int app_remote_ensure_directory(uint8_t slot) {
    const char* directory = app_remote_directories[slot];

    if (app_remote_directory_exists(slot)) return OK;
    if (fs_create_dir_entry("", directory,
                            APP_REMOTE_DIRECTORY_ATTRIBUTE) != OK) {
        LOG_ERROR("APPREMOTE", "Falha ao criar diretorio de cache");
        return ERR_DISK;
    }
    return OK;
}

static int app_remote_cache_path(uint8_t slot, const char* id,
                                 char path[FS_MAX_PATH]) {
    char alias[13];
    uint32_t directory_length;

    if (slot > 1U || !id || !path) {
        LOG_ERROR("APPREMOTE", "Caminho de cache recebeu argumento invalido");
        return ERR_NULL;
    }
    app_remote_alias(id, alias);
    directory_length = kstrlen(app_remote_directories[slot]);
    kmemcpy(path, app_remote_directories[slot], directory_length);
    path[directory_length] = '/';
    app_remote_copy_text(path + directory_length + 1U,
                         FS_MAX_PATH - directory_length - 1U, alias);
    return OK;
}

static int app_remote_hash_cached(uint8_t slot,
                                  const app_remote_entry_t* entry) {
    char path[FS_MAX_PATH];
    uint8_t hash[32];
    int size;

    if (!entry) {
        LOG_ERROR("APPREMOTE", "Hash de cache recebeu entrada nula");
        return ERR_NULL;
    }
    app_remote_cache_path(slot, entry->info.id, path);
    size = fs_read_file_at(path, app_remote_package_buffer,
                           sizeof(app_remote_package_buffer));
    if (size < 0 || (uint32_t)size != entry->package_size ||
        crypto_sha256(app_remote_package_buffer, (uint32_t)size, hash) != OK ||
        !crypto_equal(hash, entry->package_hash, 32U)) return ERR_INVALID;
    return OK;
}

static int app_remote_info_matches(const app_package_info_t* left,
                                   const app_package_info_t* right) {
    if (kstrcmp(left->id, right->id) != 0 ||
        kstrcmp(left->name, right->name) != 0 ||
        kstrcmp(left->version, right->version) != 0 ||
        left->dependency_count != right->dependency_count) return 0;
    for (uint32_t index = 0; index < left->dependency_count; index++) {
        if (kstrcmp(left->dependencies[index], right->dependencies[index]) != 0) {
            return 0;
        }
    }
    return 1;
}

static int app_remote_verify_cached_entry(uint8_t slot,
                                          const app_remote_entry_t* entry) {
    char path[FS_MAX_PATH];
    app_package_info_t verified;

    if (!entry) {
        LOG_ERROR("APPREMOTE", "Verificacao do cache recebeu entrada nula");
        return ERR_NULL;
    }
    if (app_remote_hash_cached(slot, entry) != OK) return ERR_INVALID;
    app_remote_cache_path(slot, entry->info.id, path);
    if (app_package_verify_file(path, &verified) != OK ||
        !app_remote_info_matches(&entry->info, &verified)) return ERR_INVALID;
    return OK;
}

static int app_remote_load_active_cache(void) {
    char path[FS_MAX_PATH];
    uint8_t hash[32];
    app_remote_reason_t reason;
    int signature_authenticated;
    int size;

    if (app_remote_record.active_slot == APP_REMOTE_SLOT_NONE) {
        app_remote_status.cache_state = APP_REMOTE_CACHE_EMPTY;
        return OK;
    }
    if (app_remote_record.active_slot > 1U) {
        LOG_ERROR("APPREMOTE", "Slot ativo do cache remoto e invalido");
        return ERR_INVALID;
    }
    app_remote_copy_text(path, sizeof(path),
                         app_remote_directories[app_remote_record.active_slot]);
    app_remote_copy_text(path + kstrlen(path),
                         sizeof(path) - kstrlen(path), "/PLAN.CAT");
    size = fs_read_file_at(path, app_remote_catalog, sizeof(app_remote_catalog));
    if (size <= 0 || crypto_sha256(app_remote_catalog, (uint32_t)size,
                                  hash) != OK ||
        !crypto_equal(hash, app_remote_record.catalog_hash, 32U)) {
        app_remote_status.cache_state = APP_REMOTE_CACHE_INVALID;
        return ERR_INVALID;
    }
    signature_authenticated = app_remote_cache_signature_authenticated &&
        crypto_equal(hash, app_remote_authenticated_cache_hash, 32U);
    if (app_remote_parse_catalog(app_remote_catalog, (uint32_t)size,
                                 app_remote_record.highest_generation,
                                 signature_authenticated, &reason) != OK ||
        app_remote_status.generation != app_remote_record.cache_generation) {
        app_remote_status.cache_state = APP_REMOTE_CACHE_INVALID;
        return ERR_INVALID;
    }
    kmemcpy(app_remote_authenticated_cache_hash, hash, sizeof(hash));
    app_remote_cache_signature_authenticated = 1U;
    app_remote_catalog_size = (uint32_t)size;
    for (uint32_t index = 0; index < app_remote_record.cached_count; index++) {
        int entry_index = app_remote_find_index(app_remote_record.cached_ids[index]);

        if (entry_index < 0 || app_remote_verify_cached_entry(
                app_remote_record.active_slot,
                &app_remote_entries[entry_index]) != OK) {
            app_remote_status.cache_state = APP_REMOTE_CACHE_INVALID;
            return ERR_INVALID;
        }
    }
    app_remote_status.catalog_available = 1U;
    app_remote_status.cache_target_available = 1U;
    app_remote_status.cache_state = APP_REMOTE_CACHE_VALID;
    app_remote_copy_text(app_remote_status.cache_target,
                         sizeof(app_remote_status.cache_target),
                         app_remote_record.target_id);
    app_remote_classify_entries();
    return OK;
}

static int app_remote_load_records(void) {
    app_remote_record_t candidate;
    int present = 0;

    for (uint32_t slot = 0; slot < 2U; slot++) {
        int size = fs_read_file(app_remote_record_paths[slot],
                                app_remote_record_raw[slot],
                                APP_REMOTE_RECORD_SIZE);
        if (size < 0) continue;
        present = 1;
        if ((uint32_t)size == APP_REMOTE_RECORD_SIZE &&
            app_remote_record_decode(app_remote_record_raw[slot], &candidate) == OK &&
            (app_remote_record_slot < 0 ||
             candidate.sequence > app_remote_record.sequence)) {
            app_remote_record = candidate;
            app_remote_record_slot = (int)slot;
        }
    }
    if (present && app_remote_record_slot < 0) {
        kmemset(&app_remote_record, 0, sizeof(app_remote_record));
        app_remote_record.active_slot = APP_REMOTE_SLOT_NONE;
        app_remote_record.pending_slot = APP_REMOTE_SLOT_NONE;
        LOG_ERROR("APPREMOTE", "Nenhum registro remoto redundante e valido");
        return ERR_INVALID;
    }
    if (app_remote_record_slot < 0) {
        kmemset(&app_remote_record, 0, sizeof(app_remote_record));
        app_remote_record.active_slot = APP_REMOTE_SLOT_NONE;
        app_remote_record.pending_slot = APP_REMOTE_SLOT_NONE;
    }
    app_remote_status.highest_generation = app_remote_record.highest_generation;
    return OK;
}

static void app_remote_provenance_hash(app_remote_provenance_t* store,
                                       uint8_t hash[32]) {
    crypto_sha256((const uint8_t*)store,
                  sizeof(*store) - APP_REMOTE_PROVENANCE_HASH_SIZE, hash);
}

static int app_remote_provenance_valid(app_remote_provenance_t* store) {
    uint8_t hash[32];

    if (store->magic != APP_REMOTE_PROVENANCE_MAGIC ||
        store->version != APP_REMOTE_PROVENANCE_VERSION ||
        store->size != sizeof(*store) ||
        store->count > APP_REMOTE_PROVENANCE_MAX) return 0;
    app_remote_provenance_hash(store, hash);
    return crypto_equal(hash, store->hash, 32U);
}

static int app_remote_build_installed_path(const char* id, const char* name,
                                           char path[FS_MAX_PATH]) {
    uint32_t length;

    if (!app_remote_id_valid(id) || !name || !path) {
        LOG_ERROR("APPREMOTE", "Caminho instalado recebeu argumento invalido");
        return ERR_INVALID;
    }
    app_remote_copy_text(path, FS_MAX_PATH, APP_PACKAGE_DIRECTORY);
    length = kstrlen(path);
    path[length++] = '/';
    path[length] = '\0';
    app_remote_copy_text(path + length, FS_MAX_PATH - length, id);
    length = kstrlen(path);
    path[length++] = '/';
    path[length] = '\0';
    if (length + kstrlen(name) + 1U > FS_MAX_PATH) {
        LOG_ERROR("APPREMOTE", "Caminho instalado excedeu limite");
        return ERR_OVERFLOW;
    }
    app_remote_copy_text(path + length, FS_MAX_PATH - length, name);
    return OK;
}

static int app_remote_hash_installed(const char* id, uint8_t app_hash[32],
                                     uint8_t metadata_hash[32]) {
    char path[FS_MAX_PATH];
    int size;

    if (!app_hash || !metadata_hash) {
        LOG_ERROR("APPREMOTE", "Hash instalado recebeu destino nulo");
        return ERR_NULL;
    }
    if (app_remote_build_installed_path(id, APP_PACKAGE_ENTRY_NAME, path) != OK) {
        return ERR_INVALID;
    }
    size = fs_read_file_at(path, app_remote_package_buffer,
                           sizeof(app_remote_package_buffer));
    if (size <= 0 || crypto_sha256(app_remote_package_buffer,
                                   (uint32_t)size, app_hash) != OK) {
        LOG_WARN("APPREMOTE", "APP.ZAP instalado nao pode ser autenticado");
        return ERR_INVALID;
    }
    if (app_remote_build_installed_path(
            id, APP_PACKAGE_METADATA_NAME, path) != OK) return ERR_INVALID;
    size = fs_read_file_at(path, app_remote_metadata_buffer,
                           sizeof(app_remote_metadata_buffer));
    if (size <= 0 || crypto_sha256(app_remote_metadata_buffer,
                                   (uint32_t)size, metadata_hash) != OK) {
        LOG_WARN("APPREMOTE", "META.DAT instalado nao pode ser autenticado");
        return ERR_INVALID;
    }
    return OK;
}

static void app_remote_load_provenance(void) {
    app_remote_provenance_t candidate;
    int present = 0;

    kmemset(&app_remote_provenance, 0, sizeof(app_remote_provenance));
    kmemset(app_remote_provenance_trusted, 0,
            sizeof(app_remote_provenance_trusted));
    app_remote_provenance_reliable = 0U;
    for (uint32_t slot = 0; slot < 2U; slot++) {
        int size = fs_read_file(app_remote_provenance_paths[slot],
                                (uint8_t*)&candidate, sizeof(candidate));
        if (size >= 0) present = 1;
        if (size == (int)sizeof(candidate) &&
            app_remote_provenance_valid(&candidate) &&
            (app_remote_provenance_slot < 0 ||
             candidate.sequence > app_remote_provenance.sequence)) {
            app_remote_provenance = candidate;
            app_remote_provenance_slot = (int)slot;
        }
    }
    if (app_remote_provenance_slot < 0) {
        kmemset(&app_remote_provenance, 0, sizeof(app_remote_provenance));
        app_remote_provenance.magic = APP_REMOTE_PROVENANCE_MAGIC;
        app_remote_provenance.version = APP_REMOTE_PROVENANCE_VERSION;
        app_remote_provenance.size = sizeof(app_remote_provenance);
        if (present) {
            LOG_WARN("APPREMOTE", "Procedencia instalada esta indisponivel");
            return;
        }
    }
    app_remote_provenance_reliable = 1U;
}

static int app_remote_write_provenance(void) {
    int slot = app_remote_provenance_slot < 0 ? 0 : 1 - app_remote_provenance_slot;
    int result;

    app_remote_provenance.sequence++;
    app_remote_provenance_hash(&app_remote_provenance,
                               app_remote_provenance.hash);
    result = fs_atomic_write_root(
        app_remote_provenance_paths[slot],
        (const uint8_t*)&app_remote_provenance,
        sizeof(app_remote_provenance), APP_REMOTE_FILE_ATTRIBUTE,
        FS_ATOMIC_CREATE_OR_REPLACE);
    if (result == OK) {
        app_remote_provenance_slot = slot;
        app_remote_provenance_reliable = 1U;
    } else {
        app_remote_provenance_reliable = 0U;
        LOG_WARN("APPREMOTE", "Procedencia remota nao foi persistida");
    }
    return result;
}

static int app_remote_record_provenance(const char* id, const char* version,
                                        uint32_t history_sequence) {
    app_remote_provenance_entry_t* entry = 0;
    uint8_t app_hash[32];
    uint8_t metadata_hash[32];

    if (app_remote_hash_installed(id, app_hash, metadata_hash) != OK) {
        app_remote_provenance_reliable = 0U;
        LOG_WARN("APPREMOTE", "Procedencia remota ficou como N/D");
        return ERR_INVALID;
    }

    for (uint32_t index = 0; index < APP_REMOTE_PROVENANCE_MAX; index++) {
        app_remote_provenance_entry_t* current =
            &app_remote_provenance.entries[index];
        if (kstrcmp(current->id, id) == 0 &&
            kstrcmp(current->version, version) == 0) {
            entry = current;
            break;
        }
        if (!entry && !current->id[0]) entry = current;
    }
    if (!entry) entry = &app_remote_provenance.entries[0];
    if (!entry->id[0] && app_remote_provenance.count < APP_REMOTE_PROVENANCE_MAX) {
        app_remote_provenance.count++;
    }
    kmemset(entry, 0, sizeof(*entry));
    app_remote_copy_text(entry->id, sizeof(entry->id), id);
    app_remote_copy_text(entry->version, sizeof(entry->version), version);
    kmemcpy(entry->key_id, APP_REMOTE_TRUST_KEY_ID, 16U);
    kmemcpy(entry->app_hash, app_hash, sizeof(entry->app_hash));
    kmemcpy(entry->metadata_hash, metadata_hash, sizeof(entry->metadata_hash));
    entry->generation = app_remote_record.cache_generation;
    entry->sequence = history_sequence;
    return OK;
}

static int app_remote_build_package_url(const char* catalog_url,
                                        const char* package_path,
                                        char output[APP_REMOTE_URL_SIZE]) {
    uint32_t prefix = 7U;
    uint32_t path_length;

    if (!catalog_url || !package_path || !output ||
        kstrlen(catalog_url) < 8U ||
        catalog_url[0] != 'h' || catalog_url[1] != 't' ||
        catalog_url[2] != 't' || catalog_url[3] != 'p' ||
        catalog_url[4] != ':' || catalog_url[5] != '/' ||
        catalog_url[6] != '/') {
        LOG_WARN("APPREMOTE", "URL base remota e invalida");
        return ERR_INVALID;
    }
    while (catalog_url[prefix] && catalog_url[prefix] != '/') prefix++;
    path_length = kstrlen(package_path);
    if (prefix + path_length + 1U > APP_REMOTE_URL_SIZE) return ERR_OVERFLOW;
    kmemcpy(output, catalog_url, prefix);
    app_remote_copy_text(output + prefix, APP_REMOTE_URL_SIZE - prefix,
                         package_path);
    return OK;
}

static int app_remote_validate_download(const app_remote_entry_t* entry,
                                        const uint8_t* data, uint32_t size) {
    uint8_t hash[32];

    if (!entry || !data) {
        LOG_ERROR("APPREMOTE", "Download remoto recebeu argumento nulo");
        return ERR_NULL;
    }
    if (size != entry->package_size ||
        crypto_sha256(data, size, hash) != OK ||
        !crypto_equal(hash, entry->package_hash, 32U)) return ERR_INVALID;
    return OK;
}

static int app_remote_write_cached_package(uint8_t slot,
                                           const app_remote_entry_t* entry,
                                           const uint8_t* data,
                                           uint32_t size,
                                           app_remote_reason_t* reason_out) {
    char alias[13];
    char path[FS_MAX_PATH];
    app_package_info_t verified;
    int result;

    if (!entry || !data || !reason_out) {
        LOG_ERROR("APPREMOTE", "Escrita do cache recebeu argumento nulo");
        return ERR_NULL;
    }
    *reason_out = APP_REMOTE_REASON_IO;
    app_remote_alias(entry->info.id, alias);
    result = fs_atomic_write_file_in_dir(
        app_remote_directories[slot], alias, data, size,
        APP_REMOTE_FILE_ATTRIBUTE, FS_ATOMIC_CREATE_OR_REPLACE);
    if (result != OK) return result;
    app_remote_cache_path(slot, entry->info.id, path);
    result = app_package_verify_file(path, &verified);
    if (result != OK) {
        *reason_out = APP_REMOTE_REASON_PACKAGE_VERIFY;
        return ERR_INVALID;
    }
    if (!app_remote_info_matches(&entry->info, &verified)) {
        *reason_out = APP_REMOTE_REASON_PACKAGE_MISMATCH;
        return ERR_INVALID;
    }
    *reason_out = APP_REMOTE_REASON_NONE;
    return OK;
}

static int app_remote_fetch_packages(uint8_t slot, const char* catalog_url,
                                     const app_package_plan_t* plan,
                                     const app_remote_options_t* options,
                                     app_remote_result_t* output) {
    char package_url[APP_REMOTE_URL_SIZE];

    for (uint32_t index = 0; index < plan->entry_count; index++) {
        int entry_index = app_remote_find_index(plan->entries[index].id);
        const uint8_t* body;
        uint32_t size;
        int result;

        if (entry_index < 0) return ERR_NOT_FOUND;
        result = app_remote_build_package_url(
            catalog_url, app_remote_entries[entry_index].package_path,
            package_url);
        if (result != OK) {
            output->reason = APP_REMOTE_REASON_PATH;
            return result;
        }
        app_remote_status.total_bytes +=
            app_remote_entries[entry_index].package_size;
        result = app_remote_http_get(package_url, options, &body, &size);
        if (result != OK) {
            output->reason = app_remote_transport_reason(result);
            return result;
        }
        if (app_remote_validate_download(
                &app_remote_entries[entry_index], body, size) != OK) {
            output->reason = APP_REMOTE_REASON_PACKAGE_HASH;
            return ERR_INVALID;
        }
        result = app_remote_write_cached_package(
            slot, &app_remote_entries[entry_index], body, size,
            &output->reason);
        if (result != OK) return result;
        app_remote_copy_text(app_remote_pending_ids[index],
                             APP_PACKAGE_ID_SIZE, plan->entries[index].id);
        app_remote_pending_count = (uint8_t)(index + 1U);
        if (app_remote_fail_after && index + 1U == app_remote_fail_after) {
            app_remote_fail_after = 0U;
            LOG_WARN("APPREMOTE", "Failpoint AS5 interrompeu cache pendente");
            return ERR_DISK;
        }
    }
    return OK;
}

static int app_remote_preflight_cache_space(
    const app_package_plan_t* plan, app_remote_result_t* output) {
    fs_info_t info;
    uint32_t cluster_size;
    uint32_t required = 4U;

    if (!plan || !output || fs_get_info(&info) != OK ||
        info.bytes_per_sector == 0U || info.sectors_per_cluster == 0U ||
        info.bytes_per_sector > 0xFFFFFFFFU / info.sectors_per_cluster) {
        LOG_ERROR("APPREMOTE", "Espaco do cache remoto nao pode ser calculado");
        return ERR_STATE;
    }
    cluster_size = info.bytes_per_sector * info.sectors_per_cluster;
    required += (app_remote_catalog_size + cluster_size - 1U) / cluster_size;
    required += (APP_REMOTE_RECORD_SIZE + cluster_size - 1U) / cluster_size;
    for (uint32_t index = 0; index < plan->entry_count; index++) {
        int entry_index = app_remote_find_index(plan->entries[index].id);
        uint32_t clusters;

        if (entry_index < 0) {
            LOG_WARN("APPREMOTE", "Plano referencia pacote remoto ausente");
            return ERR_NOT_FOUND;
        }
        clusters = (app_remote_entries[entry_index].package_size +
                    cluster_size - 1U) / cluster_size;
        if (required > 0xFFFFFFFFU - clusters) {
            LOG_ERROR("APPREMOTE", "Calculo de espaco remoto excedeu limite");
            return ERR_OVERFLOW;
        }
        required += clusters;
    }
    output->required_clusters = required;
    output->free_clusters = info.free_clusters;
    if (required > info.free_clusters) {
        output->reason = APP_REMOTE_REASON_SPACE;
        LOG_WARN("APPREMOTE", "Espaco insuficiente para o cache remoto");
        return ERR_DISK;
    }
    return OK;
}

static int app_remote_revalidate_pending_cache(
    uint8_t slot, const uint8_t authenticated_hash[32],
    uint32_t authenticated_generation, app_remote_result_t* output) {
    uint8_t current_hash[32];

    if (!authenticated_hash || !output) {
        LOG_ERROR("APPREMOTE", "Revalidacao pendente recebeu saida nula");
        return ERR_NULL;
    }
    app_remote_status.state = APP_REMOTE_STATE_VALIDATING;
    /* O check confirmado acabou de autenticar estes bytes. Comparar o
       digest evita repetir a verificacao Ed25519 longa sem aceitar mudanca. */
    if (authenticated_generation == 0U ||
        app_remote_status.generation != authenticated_generation ||
        app_remote_catalog_size < APP_REMOTE_HEADER_SIZE +
                                  APP_REMOTE_SIGNATURE_SIZE ||
        app_remote_read_u32(app_remote_catalog + 12U) !=
            authenticated_generation ||
        crypto_sha256(app_remote_catalog, app_remote_catalog_size,
                      current_hash) != OK ||
        !crypto_equal(current_hash, authenticated_hash, 32U)) {
        output->reason = APP_REMOTE_REASON_CATALOG_FORMAT;
        LOG_WARN("APPREMOTE", "Catalogo mudou antes da publicacao do cache");
        return ERR_INVALID;
    }
    for (uint32_t index = 0; index < app_remote_pending_count; index++) {
        int entry_index = app_remote_find_index(app_remote_pending_ids[index]);

        if (entry_index < 0 || app_remote_verify_cached_entry(
                slot, &app_remote_entries[entry_index]) != OK) {
            output->reason = APP_REMOTE_REASON_CACHE_INVALID;
            LOG_WARN("APPREMOTE", "Plano baixado falhou na revalidacao final");
            return ERR_INVALID;
        }
    }
    return OK;
}

static int app_remote_publish_cache(const char* id, const char* catalog_url,
                                    const app_remote_options_t* options,
                                    app_remote_result_t* output) {
    uint8_t slot = app_remote_record.active_slot == 0U ? 1U : 0U;
    uint8_t authenticated_hash[32];
    uint32_t authenticated_generation = app_remote_status.generation;
    app_remote_record_t writing_record;
    app_remote_reason_t plan_reason;
    app_package_info_t installed;
    int update = app_package_get_installed_info_by_id(id, &installed) == OK;
    int result;

    if (!id || !catalog_url || !output) {
        LOG_ERROR("APPREMOTE", "Publicacao do cache recebeu argumento nulo");
        return ERR_NULL;
    }
    result = app_remote_build_plan_internal(
        id, update, 1,
        &app_remote_active_plan, &plan_reason);
    if (result != OK) {
        output->reason = plan_reason;
        return result;
    }
    output->plan = app_remote_active_plan;
    result = app_remote_preflight_cache_space(&app_remote_active_plan, output);
    if (result != OK) return result;
    if (crypto_sha256(app_remote_catalog, app_remote_catalog_size,
                      authenticated_hash) != OK) {
        output->reason = APP_REMOTE_REASON_CATALOG_FORMAT;
        LOG_ERROR("APPREMOTE", "Hash do catalogo autenticado falhou");
        return ERR_INVALID;
    }
    app_remote_record.pending_slot = slot;
    app_remote_record.phase = APP_REMOTE_PHASE_WRITING;
    app_remote_pending_count = 0U;
    kmemset(app_remote_pending_ids, 0, sizeof(app_remote_pending_ids));
    if (app_remote_write_record() != OK) return ERR_DISK;
    app_remote_status.cache_pending = 1U;
    if (app_remote_clear_directory(slot) != OK) return ERR_DISK;
    if (app_remote_ensure_directory(slot) != OK) return ERR_DISK;
    result = fs_atomic_write_file_in_dir(
        app_remote_directories[slot], "PLAN.CAT", app_remote_catalog,
        app_remote_catalog_size, APP_REMOTE_FILE_ATTRIBUTE,
        FS_ATOMIC_CREATE_OR_REPLACE);
    if (result == OK) {
        result = app_remote_fetch_packages(
            slot, catalog_url, &app_remote_active_plan, options, output);
    }
    if (result == OK) {
        result = app_remote_revalidate_pending_cache(
            slot, authenticated_hash, authenticated_generation, output);
    }
    if (result != OK) return result;
    writing_record = app_remote_record;
    crypto_sha256(app_remote_catalog, app_remote_catalog_size,
                  app_remote_record.catalog_hash);
    app_remote_record.cached_count = app_remote_pending_count;
    kmemset(app_remote_record.cached_ids, 0,
            sizeof(app_remote_record.cached_ids));
    for (uint32_t index = 0; index < app_remote_pending_count; index++) {
        app_remote_copy_text(app_remote_record.cached_ids[index],
                             APP_PACKAGE_ID_SIZE,
                             app_remote_pending_ids[index]);
    }
    kmemset(app_remote_record.target_id, 0,
            sizeof(app_remote_record.target_id));
    app_remote_copy_text(app_remote_record.target_id,
                         sizeof(app_remote_record.target_id), id);
    app_remote_record.active_slot = slot;
    app_remote_record.pending_slot = APP_REMOTE_SLOT_NONE;
    app_remote_record.phase = APP_REMOTE_PHASE_CLEAN;
    app_remote_record.cache_generation = app_remote_status.generation;
    app_remote_record.highest_generation = app_remote_status.generation;
    if (app_remote_write_record() != OK) {
        app_remote_record = writing_record;
        LOG_ERROR("APPREMOTE", "Publicacao final do cache nao foi duravel");
        return ERR_DISK;
    }
    kmemcpy(app_remote_authenticated_cache_hash, authenticated_hash,
            sizeof(app_remote_authenticated_cache_hash));
    app_remote_cache_signature_authenticated = 1U;
    app_remote_status.cache_pending = 0U;
    app_remote_status.highest_generation = app_remote_record.highest_generation;
    app_remote_status.cache_state = APP_REMOTE_CACHE_VALID;
    app_remote_status.cache_target_available = 1U;
    app_remote_copy_text(app_remote_status.cache_target,
                         sizeof(app_remote_status.cache_target), id);
    output->cache_published = 1U;
    output->plan = app_remote_active_plan;
    return OK;
}

int app_remote_init(void) {
    int cache_recovery_ok = 1;

    LOG_INFO("APPREMOTE", "Inicializando repositorio remoto de aplicativos");
    kmemset(&app_remote_status, 0, sizeof(app_remote_status));
    kmemset(app_remote_entries, 0, sizeof(app_remote_entries));
    kmemset(&app_remote_record, 0, sizeof(app_remote_record));
    kmemset(app_remote_pending_ids, 0, sizeof(app_remote_pending_ids));
    app_remote_pending_count = 0U;
    app_remote_cache_signature_authenticated = 0U;
    app_remote_catalog_signature_authenticated = 0U;
    kmemset(app_remote_authenticated_cache_hash, 0,
            sizeof(app_remote_authenticated_cache_hash));
    kmemset(app_remote_authenticated_catalog_hash, 0,
            sizeof(app_remote_authenticated_catalog_hash));
    kmemset(app_remote_candidate_catalog_hash, 0,
            sizeof(app_remote_candidate_catalog_hash));
    app_remote_record_slot = -1;
    app_remote_provenance_slot = -1;
    app_remote_status.cache_state = fs_get_type() == FS_TYPE_FAT12 ?
                                    APP_REMOTE_CACHE_EMPTY :
                                    APP_REMOTE_CACHE_UNAVAILABLE;
    if (!app_package_is_ready()) {
        LOG_ERROR("APPREMOTE", "Servico de pacotes indisponivel");
        return ERR_UNAVAILABLE;
    }
    app_remote_status.initialized = 1U;
    app_remote_status.state = APP_REMOTE_STATE_DISABLED;
    app_remote_status.reason = APP_REMOTE_REASON_DISABLED;
    if (fs_get_type() == FS_TYPE_FAT12) {
        if (app_remote_load_records() != OK) {
            app_remote_status.cache_state = APP_REMOTE_CACHE_INVALID;
            app_remote_status.reason = APP_REMOTE_REASON_CACHE_INVALID;
            LOG_ERROR("APPREMOTE", "Registros do cache remoto invalidos");
        } else {
            app_remote_status.cache_pending =
                app_remote_record.phase == APP_REMOTE_PHASE_WRITING;
            if (app_remote_record.phase == APP_REMOTE_PHASE_WRITING &&
                app_remote_record.pending_slot <= 1U) {
                if (app_remote_clear_directory(
                        app_remote_record.pending_slot) != OK) {
                    cache_recovery_ok = 0;
                    LOG_ERROR("APPREMOTE", "Cache AS5 pendente nao foi removido");
                } else {
                    app_remote_record.pending_slot = APP_REMOTE_SLOT_NONE;
                    app_remote_record.phase = APP_REMOTE_PHASE_CLEAN;
                    if (app_remote_write_record() != OK) {
                        cache_recovery_ok = 0;
                        LOG_ERROR("APPREMOTE", "Recovery AS5 nao foi persistido");
                    } else {
                        app_remote_status.cache_pending = 0U;
                        LOG_INFO("APPREMOTE", "Cache AS5 pendente foi descartado");
                    }
                }
            }
            if (app_remote_load_active_cache() != OK &&
                app_remote_record.active_slot != APP_REMOTE_SLOT_NONE) {
                app_remote_status.reason = APP_REMOTE_REASON_CACHE_INVALID;
                LOG_WARN("APPREMOTE", "Cache remoto ativo foi recusado");
            }
            if (!cache_recovery_ok) {
                app_remote_status.cache_state = APP_REMOTE_CACHE_INVALID;
                app_remote_status.cache_target_available = 0U;
                app_remote_status.reason = APP_REMOTE_REASON_IO;
            }
            app_remote_load_provenance();
            if (app_remote_provenance_reliable) {
                app_remote_refresh_provenance();
            }
        }
    }
    LOG_INFO("APPREMOTE", "Repositorio remoto pronto e desabilitado");
    return OK;
}

int app_remote_enable(void) {
    if (!app_remote_status.initialized) {
        LOG_ERROR("APPREMOTE", "Habilitacao antes da inicializacao");
        return ERR_STATE;
    }
    if (app_remote_status.busy) {
        LOG_WARN("APPREMOTE", "Habilitacao recusada durante operacao");
        return ERR_STATE;
    }
    app_remote_status.enabled = 1U;
    app_remote_status.network_ready = app_remote_network_ready() ? 1U : 0U;
    app_remote_status.state = app_remote_status.catalog_available ?
                              APP_REMOTE_STATE_AVAILABLE : APP_REMOTE_STATE_READY;
    app_remote_status.reason = APP_REMOTE_REASON_NONE;
    LOG_INFO("APPREMOTE", "Repositorio remoto habilitado para a sessao");
    return OK;
}

int app_remote_disable(void) {
    if (!app_remote_status.initialized) {
        LOG_ERROR("APPREMOTE", "Desabilitacao antes da inicializacao");
        return ERR_STATE;
    }
    if (app_remote_status.busy) {
        LOG_WARN("APPREMOTE", "Desabilitacao recusada durante operacao");
        return ERR_STATE;
    }
    app_remote_status.enabled = 0U;
    app_remote_status.state = APP_REMOTE_STATE_DISABLED;
    app_remote_status.reason = APP_REMOTE_REASON_DISABLED;
    LOG_INFO("APPREMOTE", "Repositorio remoto desabilitado");
    return OK;
}

int app_remote_check(const char* catalog_url,
                     const app_remote_options_t* options,
                     app_remote_result_t* result_out) {
    const char* url = catalog_url && catalog_url[0] ?
                      catalog_url : APP_REMOTE_DEFAULT_CATALOG_URL;
    const uint8_t* body;
    uint32_t size;
    app_remote_reason_t reason;
    int signature_authenticated;
    int result;

    if (!result_out) {
        LOG_ERROR("APPREMOTE", "Consulta remota recebeu saida nula");
        return ERR_NULL;
    }
    app_remote_result_reset(result_out);
    if (app_remote_begin(result_out) != OK) return ERR_STATE;
    app_remote_status.network_ready = app_remote_network_ready() ? 1U : 0U;
    if (!app_remote_status.enabled || !app_remote_status.network_ready) {
        return app_remote_fail(!app_remote_status.enabled ?
                               APP_REMOTE_REASON_DISABLED :
                               APP_REMOTE_REASON_NETWORK, ERR_UNAVAILABLE,
                               "Repositorio remoto sem opt-in ou rede", result_out);
    }
    app_remote_status.state = APP_REMOTE_STATE_CHECKING;
    app_remote_status.total_bytes = APP_REMOTE_MAX_CATALOG_SIZE;
    result = app_remote_http_get(url, options, &body, &size);
    if (result != OK) {
        return app_remote_fail(app_remote_transport_reason(result), result,
                               "Consulta do catalogo remoto falhou", result_out);
    }
    if (crypto_sha256(body, size, app_remote_candidate_catalog_hash) != OK) {
        return app_remote_fail(APP_REMOTE_REASON_CATALOG_FORMAT, ERR_INVALID,
                               "Hash do catalogo remoto falhou", result_out);
    }
    signature_authenticated = app_remote_catalog_signature_authenticated &&
        crypto_equal(app_remote_candidate_catalog_hash,
                     app_remote_authenticated_catalog_hash, 32U);
    result = app_remote_parse_catalog(body, size,
                                      app_remote_record.highest_generation,
                                      signature_authenticated, &reason);
    if (result != OK) {
        return app_remote_fail(reason, result,
                               "Catalogo remoto recusado", result_out);
    }
    kmemcpy(app_remote_authenticated_catalog_hash,
            app_remote_candidate_catalog_hash,
            sizeof(app_remote_authenticated_catalog_hash));
    app_remote_catalog_signature_authenticated = 1U;
    kmemcpy(app_remote_catalog, body, size);
    app_remote_catalog_size = size;
    app_remote_status.catalog_available = 1U;
    app_remote_copy_text(app_remote_status.catalog_url,
                         sizeof(app_remote_status.catalog_url), url);
    app_remote_classify_entries();
    app_remote_end_ready();
    app_remote_result_status(result_out);
    LOG_INFO("APPREMOTE", "Catalogo remoto autenticado em memoria");
    return OK;
}

int app_remote_build_plan(const char* id, int update, int allow_downgrade,
                          app_package_plan_t* plan_out,
                          app_remote_result_t* result_out) {
    app_remote_reason_t reason;
    int result;

    if (!id || !plan_out || !result_out) {
        LOG_ERROR("APPREMOTE", "Plano remoto recebeu argumento nulo");
        return ERR_NULL;
    }
    app_remote_result_reset(result_out);
    if (app_remote_status.busy) {
        result_out->reason = APP_REMOTE_REASON_MUTATION_BUSY;
        LOG_WARN("APPREMOTE", "Plano remoto solicitado durante operacao");
        return ERR_STATE;
    }
    if (!app_remote_status.catalog_available) {
        return app_remote_fail(APP_REMOTE_REASON_CACHE_UNAVAILABLE,
                               ERR_UNAVAILABLE,
                               "Catalogo remoto nao esta disponivel", result_out);
    }
    result = app_remote_build_plan_internal(id, update, allow_downgrade,
                                            plan_out, &reason);
    result_out->reason = reason;
    if (result == OK) result_out->plan = *plan_out;
    else LOG_WARN("APPREMOTE", "Plano remoto foi recusado");
    return result;
}

int app_remote_fetch(const char* id, const char* catalog_url, int confirmed,
                     const app_remote_options_t* options,
                     app_remote_result_t* result_out) {
    app_package_info_t installed;
    int update;
    int result;

    if (!id || !result_out) {
        LOG_ERROR("APPREMOTE", "Fetch remoto recebeu argumento nulo");
        return ERR_NULL;
    }
    app_remote_result_reset(result_out);
    if (app_remote_status.busy) {
        result_out->reason = APP_REMOTE_REASON_MUTATION_BUSY;
        LOG_WARN("APPREMOTE", "Fetch recusado durante operacao remota");
        return ERR_STATE;
    }
    if (!confirmed) {
        if (fs_get_type() != FS_TYPE_FAT12) {
            return app_remote_fail(APP_REMOTE_REASON_CACHE_UNAVAILABLE,
                                   ERR_UNAVAILABLE,
                                   "Cache AS5 requer FAT12", result_out);
        }
        if (app_remote_status.cache_state == APP_REMOTE_CACHE_INVALID) {
            return app_remote_fail(APP_REMOTE_REASON_CACHE_INVALID, ERR_STATE,
                                   "Cache invalido requer limpeza explicita",
                                   result_out);
        }
        update = app_package_get_installed_info_by_id(id, &installed) == OK;
        result = app_remote_build_plan(id, update, 1,
                                       &result_out->plan, result_out);
        if (result == OK) {
            result = app_remote_preflight_cache_space(&result_out->plan,
                                                       result_out);
        }
        return result;
    }
    result = app_remote_check(catalog_url, options, result_out);
    if (result != OK) return result;
    if (app_remote_status.cache_state == APP_REMOTE_CACHE_INVALID) {
        return app_remote_fail(APP_REMOTE_REASON_CACHE_INVALID, ERR_STATE,
                               "Cache invalido requer limpeza explicita",
                               result_out);
    }
    if (fs_get_type() != FS_TYPE_FAT12) {
        return app_remote_fail(APP_REMOTE_REASON_CACHE_UNAVAILABLE,
                               ERR_UNAVAILABLE,
                               "Cache AS5 requer FAT12", result_out);
    }
    if (app_remote_begin(result_out) != OK) return ERR_STATE;
    app_remote_status.state = APP_REMOTE_STATE_DOWNLOADING;
    app_remote_status.total_bytes = app_remote_catalog_size;
    result_out->cache_preserved = app_remote_record.active_slot <= 1U;
    result = app_remote_publish_cache(
        id, app_remote_status.catalog_url, options, result_out);
    if (result != OK) {
        app_remote_reason_t reason = result_out->reason;

        if (app_remote_record.active_slot <= 1U) {
            (void)app_remote_load_active_cache();
        }
        if (reason == APP_REMOTE_REASON_NONE) {
            reason = result == ERR_TIMEOUT ?
                     app_remote_transport_reason(result) :
                     result == ERR_DISK ? APP_REMOTE_REASON_IO :
                     APP_REMOTE_REASON_PACKAGE_VERIFY;
        }
        return app_remote_fail(reason, result,
                               "Plano remoto nao foi publicado", result_out);
    }
    app_remote_classify_entries();
    app_remote_end_ready();
    LOG_INFO("APPREMOTE", "Plano remoto autenticado publicado no cache");
    return OK;
}

int app_remote_apply_cached(const char* id, int update, int confirmed,
                            const app_remote_options_t* options,
                            app_remote_result_t* result_out) {
    app_remote_reason_t reason;
    const char* directory;
    int result;

    if (!id || !result_out) {
        LOG_ERROR("APPREMOTE", "Aplicacao remota recebeu argumento nulo");
        return ERR_NULL;
    }
    app_remote_result_reset(result_out);
    if (app_remote_begin(result_out) != OK) return ERR_STATE;
    if (!app_remote_status.enabled) {
        return app_remote_fail(APP_REMOTE_REASON_DISABLED, ERR_UNAVAILABLE,
                               "Opt-in remoto necessario", result_out);
    }
    app_remote_status.state = APP_REMOTE_STATE_VALIDATING;
    if (app_remote_status.cache_state != APP_REMOTE_CACHE_VALID ||
        app_remote_record.active_slot > 1U ||
        kstrcmp(app_remote_record.target_id, id) != 0) {
        return app_remote_fail(APP_REMOTE_REASON_CACHE_UNAVAILABLE,
                               ERR_NOT_FOUND,
                               "Alvo remoto nao esta no cache ativo", result_out);
    }
    if (app_remote_load_active_cache() != OK) {
        return app_remote_fail(APP_REMOTE_REASON_CACHE_INVALID, ERR_INVALID,
                               "Cache remoto falhou na revalidacao", result_out);
    }
    result = app_remote_build_plan_internal(
        id, update, options && options->allow_downgrade,
        &app_remote_active_plan, &reason);
    if (result != OK) return app_remote_fail(reason, result,
                                              "Plano do cache foi recusado",
                                              result_out);
    directory = app_remote_directories[app_remote_record.active_slot];
    result = confirmed ? app_package_apply_plan_from_directory_confirmed(
                             &app_remote_active_plan, directory,
                             &result_out->package_action) :
                         app_package_preflight_plan_from_directory(
                             &app_remote_active_plan, directory,
                             &result_out->package_action);
    result_out->plan = app_remote_active_plan;
    if (result != OK) {
        app_remote_reason_t package_reason = app_remote_package_reason(
            result_out->package_action.reason);

        result_out->reason = package_reason;
        LOG_WARN("APPREMOTE", "Motor AS4 recusou plano remoto");
        return app_remote_fail(package_reason, result,
                               "Motor AS4 recusou plano remoto", result_out);
    }
    if (confirmed) {
        int provenance_result = OK;

        kmemset(&app_remote_package_status, 0,
                sizeof(app_remote_package_status));

        if (app_package_get_status(&app_remote_package_status) != OK ||
            !app_remote_package_status.history_available ||
            app_remote_package_status.last_history.sequence == 0U) {
            provenance_result = ERR_UNAVAILABLE;
        }

        for (uint32_t index = 0;
             index < app_remote_active_plan.entry_count; index++) {
            const app_package_plan_entry_t* item =
                &app_remote_active_plan.entries[index];
            if (provenance_result != OK || app_remote_record_provenance(
                    item->id, item->to_version,
                    app_remote_package_status.last_history.sequence) != OK) {
                provenance_result = ERR_INVALID;
            }
        }
        if (provenance_result == OK && app_remote_write_provenance() == OK) {
            app_remote_refresh_provenance();
        } else {
            app_remote_provenance_reliable = 0U;
        }
        app_remote_classify_entries();
        LOG_INFO("APPREMOTE", "Plano remoto aplicado pelo motor AS4");
    }
    app_remote_end_ready();
    return OK;
}

int app_remote_clear(int confirmed, app_remote_result_t* result_out) {
    if (!result_out) {
        LOG_ERROR("APPREMOTE", "Limpeza remota recebeu saida nula");
        return ERR_NULL;
    }
    app_remote_result_reset(result_out);
    if (!confirmed) return OK;
    if (app_remote_status.busy) {
        result_out->reason = APP_REMOTE_REASON_MUTATION_BUSY;
        LOG_WARN("APPREMOTE", "Cache ocupado por operacao remota");
        return ERR_STATE;
    }
    if (fs_get_type() != FS_TYPE_FAT12) {
        return app_remote_fail(APP_REMOTE_REASON_CACHE_UNAVAILABLE,
                               ERR_UNAVAILABLE,
                               "Limpeza do cache requer FAT12", result_out);
    }
    if (app_package_is_mutation_active()) {
        return app_remote_fail(APP_REMOTE_REASON_MUTATION_BUSY, ERR_STATE,
                               "Cache ocupado por outra mutacao", result_out);
    }
    for (uint8_t slot = 0U; slot < 2U; slot++) {
        if (app_remote_clear_directory(slot) != OK) {
            if (app_remote_record.active_slot > 1U ||
                app_remote_load_active_cache() != OK) {
                app_remote_status.cache_state = APP_REMOTE_CACHE_INVALID;
                app_remote_status.cache_target_available = 0U;
            }
            return app_remote_fail(APP_REMOTE_REASON_IO, ERR_DISK,
                                   "Falha ao limpar slot remoto", result_out);
        }
    }
    app_remote_record.active_slot = APP_REMOTE_SLOT_NONE;
    app_remote_record.pending_slot = APP_REMOTE_SLOT_NONE;
    app_remote_record.phase = APP_REMOTE_PHASE_CLEAN;
    app_remote_record.cache_generation = 0U;
    app_remote_record.cached_count = 0U;
    app_remote_cache_signature_authenticated = 0U;
    kmemset(app_remote_authenticated_cache_hash, 0,
            sizeof(app_remote_authenticated_cache_hash));
    kmemset(app_remote_record.target_id, 0,
            sizeof(app_remote_record.target_id));
    kmemset(app_remote_record.cached_ids, 0,
            sizeof(app_remote_record.cached_ids));
    if (app_remote_write_record() != OK) {
        return app_remote_fail(APP_REMOTE_REASON_IO, ERR_DISK,
                               "Estado limpo nao foi persistido", result_out);
    }
    app_remote_status.cache_state = APP_REMOTE_CACHE_EMPTY;
    app_remote_status.cache_target_available = 0U;
    app_remote_status.cache_pending = 0U;
    app_remote_status.cache_target[0] = '\0';
    app_remote_classify_entries();
    LOG_INFO("APPREMOTE", "Cache remoto limpo; geracao maxima preservada");
    return OK;
}

int app_remote_get_status(app_remote_status_t* status_out) {
    if (!status_out) {
        LOG_ERROR("APPREMOTE", "Consulta de status recebeu destino nulo");
        return ERR_NULL;
    }
    app_remote_status.network_ready = app_remote_network_ready() ? 1U : 0U;
    *status_out = app_remote_status;
    return app_remote_status.initialized ? OK : ERR_STATE;
}

int app_remote_get_count(uint32_t* count_out) {
    if (!count_out) {
        LOG_ERROR("APPREMOTE", "Contagem remota recebeu destino nulo");
        return ERR_NULL;
    }
    *count_out = app_remote_status.entry_count;
    return app_remote_status.catalog_available ? OK : ERR_UNAVAILABLE;
}

int app_remote_get_entry(uint32_t index, app_remote_entry_t* entry_out) {
    if (!entry_out) {
        LOG_ERROR("APPREMOTE", "Entrada remota recebeu destino nulo");
        return ERR_NULL;
    }
    if (!app_remote_status.catalog_available ||
        index >= app_remote_status.entry_count) return ERR_NOT_FOUND;
    *entry_out = app_remote_entries[index];
    return OK;
}

int app_remote_find_entry(const char* id, app_remote_entry_t* entry_out) {
    int index;

    if (!id || !entry_out) {
        LOG_ERROR("APPREMOTE", "Busca remota recebeu argumento nulo");
        return ERR_NULL;
    }
    index = app_remote_find_index(id);
    if (index < 0) return ERR_NOT_FOUND;
    *entry_out = app_remote_entries[index];
    return OK;
}

int app_remote_refresh_provenance(void) {
    uint32_t history_count = 0U;
    uint32_t newest_sequence = 0U;
    uint32_t oldest_sequence = 0U;

    if (!app_remote_status.initialized) {
        LOG_WARN("APPREMOTE", "Procedencia remota antes da inicializacao");
        return ERR_UNAVAILABLE;
    }
    if (app_remote_status.catalog_available) app_remote_classify_entries();
    if (!app_remote_provenance_reliable) {
        LOG_WARN("APPREMOTE", "Procedencia remota nao pode ser atualizada");
        return ERR_UNAVAILABLE;
    }
    kmemset(app_remote_provenance_trusted, 0,
            sizeof(app_remote_provenance_trusted));
    if (app_remote_provenance.count == 0U) return OK;
    if (app_package_get_history_count(&history_count) != OK ||
        history_count == 0U) {
        app_remote_provenance_reliable = 0U;
        LOG_WARN("APPREMOTE", "Historico AS4 nao valida procedencia remota");
        return ERR_UNAVAILABLE;
    }
    for (uint32_t history = 0; history < history_count; history++) {
        app_package_history_entry_t event;

        if (app_package_get_history_entry(history, &event) != OK) {
            app_remote_provenance_reliable = 0U;
            LOG_WARN("APPREMOTE", "Historico AS4 ficou indisponivel");
            return ERR_UNAVAILABLE;
        }
        if (history == 0U) newest_sequence = event.sequence;
        if (history + 1U == history_count) oldest_sequence = event.sequence;
    }
    for (uint32_t index = 0; index < APP_REMOTE_PROVENANCE_MAX; index++) {
        const app_remote_provenance_entry_t* entry =
            &app_remote_provenance.entries[index];
        app_package_info_t installed;
        uint8_t app_hash[32];
        uint8_t metadata_hash[32];
        int superseded = entry->sequence < oldest_sequence &&
                         newest_sequence > entry->sequence;

        if (!entry->id[0] ||
            app_package_get_installed_info_by_id(
                entry->id, &installed) != OK ||
            kstrcmp(entry->version, installed.version) != 0 ||
            !crypto_equal(entry->key_id, APP_REMOTE_TRUST_KEY_ID, 16U) ||
            app_remote_key_revoked(entry->key_id)) continue;
        for (uint32_t history = 0;
             history < history_count && !superseded; history++) {
            app_package_history_entry_t event;

            if (app_package_get_history_entry(history, &event) != OK) {
                superseded = 1;
                break;
            }
            if (event.sequence > entry->sequence &&
                kstrcmp(event.id, entry->id) == 0) superseded = 1;
        }
        if (superseded) continue;
        if (app_remote_hash_installed(
                entry->id, app_hash, metadata_hash) == OK &&
            crypto_equal(app_hash, entry->app_hash, 32U) &&
            crypto_equal(metadata_hash, entry->metadata_hash, 32U)) {
            app_remote_provenance_trusted[index] = 1U;
        }
    }
    return OK;
}

int app_remote_get_installed_trust(const char* id, const char* version) {
    if (!id || !version || !app_remote_provenance_reliable) return 0;
    for (uint32_t index = 0; index < APP_REMOTE_PROVENANCE_MAX; index++) {
        const app_remote_provenance_entry_t* entry =
            &app_remote_provenance.entries[index];
        if (app_remote_provenance_trusted[index] &&
            kstrcmp(entry->id, id) == 0 &&
            kstrcmp(entry->version, version) == 0 &&
            crypto_equal(entry->key_id, APP_REMOTE_TRUST_KEY_ID, 16U) &&
            !app_remote_key_revoked(entry->key_id)) return 1;
    }
    return 0;
}

int app_remote_is_provenance_available(void) {
    return app_remote_status.initialized && app_remote_provenance_reliable;
}

int app_remote_test_fail_after(uint8_t completed_files) {
    if (completed_files == 0U || completed_files > APP_REMOTE_MAX_ENTRIES) {
        LOG_ERROR("APPREMOTE", "Failpoint AS5 fora do intervalo");
        return ERR_INVALID;
    }
    app_remote_fail_after = completed_files;
    LOG_INFO("APPREMOTE", "Failpoint AS5 configurado");
    return OK;
}

void app_remote_request_cancel(void) {
    app_remote_cancel_requested = 1U;
}

const char* app_remote_state_name(app_remote_state_t state) {
    switch (state) {
        case APP_REMOTE_STATE_DISABLED: return "DISABLED";
        case APP_REMOTE_STATE_UNAVAILABLE: return "UNAVAILABLE";
        case APP_REMOTE_STATE_READY: return "READY";
        case APP_REMOTE_STATE_CHECKING: return "CHECKING";
        case APP_REMOTE_STATE_AVAILABLE: return "AVAILABLE";
        case APP_REMOTE_STATE_DOWNLOADING: return "DOWNLOADING";
        case APP_REMOTE_STATE_VALIDATING: return "VALIDATING";
        case APP_REMOTE_STATE_FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

const char* app_remote_reason_name(app_remote_reason_t reason) {
    static const char* names[] = {
        "NONE", "DISABLED", "NETWORK", "HTTP", "TIMEOUT",
        "CATALOG_FORMAT", "UNKNOWN_KEY", "REVOKED_KEY", "SIGNATURE",
        "REPLAY", "DUPLICATE", "PATH", "PLAN_INCOMPLETE", "PLAN_CYCLE",
        "PLAN_CONFLICT", "SIZE", "SPACE", "IO", "CANCELLED",
        "PACKAGE_HASH", "PACKAGE_VERIFY", "PACKAGE_MISMATCH",
        "CACHE_UNAVAILABLE", "CACHE_INVALID", "MUTATION_BUSY"
    };
    return (uint32_t)reason < sizeof(names) / sizeof(names[0]) ?
           names[reason] : "UNKNOWN";
}

const char* app_remote_cache_state_name(app_remote_cache_state_t state) {
    switch (state) {
        case APP_REMOTE_CACHE_UNAVAILABLE: return "UNAVAILABLE";
        case APP_REMOTE_CACHE_EMPTY: return "EMPTY";
        case APP_REMOTE_CACHE_VALID: return "VALID";
        case APP_REMOTE_CACHE_INVALID: return "INVALID";
        case APP_REMOTE_CACHE_STALE: return "STALE";
        default: return "UNKNOWN";
    }
}

const char* app_remote_entry_state_name(app_remote_entry_state_t state) {
    switch (state) {
        case APP_REMOTE_ENTRY_AVAILABLE: return "AVAILABLE";
        case APP_REMOTE_ENTRY_CACHED: return "CACHED";
        case APP_REMOTE_ENTRY_INSTALLED: return "INSTALLED";
        case APP_REMOTE_ENTRY_UPDATE_AVAILABLE: return "UPDATE_AVAILABLE";
        case APP_REMOTE_ENTRY_SAME_VERSION: return "SAME_VERSION";
        case APP_REMOTE_ENTRY_DOWNGRADE: return "DOWNGRADE";
        case APP_REMOTE_ENTRY_BLOCKED: return "BLOCKED";
        default: return "UNKNOWN";
    }
}
