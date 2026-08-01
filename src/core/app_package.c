#include "core/app_package.h"
#include "core/app_loader.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "fs/fs.h"

#define APP_PACKAGE_HEADER_SIZE ((uint32_t)sizeof(app_package_header_t))
#define APP_PACKAGE_MAX_FILE_SIZE \
    (APP_PACKAGE_HEADER_SIZE + APP_PACKAGE_MAX_MANIFEST_SIZE + \
     APP_IMAGE_MAX_FILE_SIZE)
#define APP_PACKAGE_READ_BUFFER_SIZE (APP_PACKAGE_MAX_FILE_SIZE + 1U)
#define APP_PACKAGE_CLUSTER_OVERHEAD 2U
#define APP_PACKAGE_CRC32_POLYNOMIAL 0xEDB88320U
#define APP_PACKAGE_ALIAS_SIZE 13U
#define APP_PACKAGE_SOURCE_PATH_SIZE 22U
#define APP_PACKAGE_TRANSACTION_SLOTS 2U
#define APP_PACKAGE_ROLLBACK_SLOTS APP_PACKAGE_MAX_ROLLBACKS
#define APP_PACKAGE_NO_BACKUP_SLOT 0xFFU
#define APP_PACKAGE_CONTROL_VERSION 1U
#define APP_PACKAGE_CONTROL_ATTRIBUTES \
    (FS_ATTRIBUTE_HIDDEN | FS_ATTRIBUTE_SYSTEM)
#define APP_PACKAGE_JOURNAL_NONE 0U
#define APP_PACKAGE_JOURNAL_PREPARED 1U
#define APP_PACKAGE_JOURNAL_REPLACING 2U
#define APP_PACKAGE_JOURNAL_COMMITTED 3U
#define APP_PACKAGE_BACKUP_APP_SIZE APP_IMAGE_MAX_FILE_SIZE
#define APP_PACKAGE_BACKUP_META_SIZE APP_PACKAGE_MAX_MANIFEST_SIZE

static int app_package_ready = 0;
static int app_package_mutation_active = 0;
static spinlock_t app_package_mutation_lock;
static app_package_action_result_t app_package_legacy_result;
static app_package_plan_t app_package_active_plan;
static char app_package_active_source_directory[13];

typedef struct {
    uint8_t* data;
    const uint8_t* payload;
    uint32_t size;
    uint32_t payload_size;
    app_package_info_t info;
} app_package_install_context_t;

typedef struct __attribute__((packed)) {
    uint8_t available;
    uint8_t slot;
    uint16_t reserved;
    char id[APP_PACKAGE_ID_SIZE];
    char version[APP_PACKAGE_VERSION_TEXT_SIZE];
} app_package_rollback_record_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t rollback_count;
    app_package_rollback_record_t rollbacks[APP_PACKAGE_ROLLBACK_SLOTS];
    uint32_t checksum;
} app_package_transaction_state_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint8_t phase;
    uint8_t operation;
    uint8_t slot;
    uint8_t backup_slot;
    uint8_t previous_backup_slot;
    uint16_t completed_files;
    app_package_plan_t plan;
    char backup_id[APP_PACKAGE_ID_SIZE];
    char backup_version[APP_PACKAGE_VERSION_TEXT_SIZE];
    uint32_t checksum;
} app_package_journal_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t count;
    uint32_t next_index;
    app_package_history_entry_t entries[APP_PACKAGE_HISTORY_MAX_ENTRIES];
    uint32_t checksum;
} app_package_history_store_t;

static const char* app_package_state_paths[APP_PACKAGE_TRANSACTION_SLOTS] = {
    "ZA40.STA", "ZA41.STA"
};
static const char* app_package_journal_paths[APP_PACKAGE_TRANSACTION_SLOTS] = {
    "ZA40.JRN", "ZA41.JRN"
};
static const char* app_package_history_paths[APP_PACKAGE_TRANSACTION_SLOTS] = {
    "ZA40.HIS", "ZA41.HIS"
};
static app_package_transaction_state_t app_package_transaction_state;
static app_package_journal_t app_package_journal;
static app_package_history_store_t app_package_history;
static int app_package_transaction_supported = 0;
static int app_package_transaction_pending = 0;
static int app_package_transaction_failed = 0;
static int app_package_history_available = 0;
static uint16_t app_package_fail_after = 0;
static int app_package_state_slot = -1;
static int app_package_journal_slot = -1;
static int app_package_history_slot = -1;
static uint8_t app_package_transaction_app_buffer[APP_PACKAGE_BACKUP_APP_SIZE];
static uint8_t app_package_transaction_meta_buffer[APP_PACKAGE_BACKUP_META_SIZE];

static uint32_t app_package_crc32(const uint8_t* content, uint32_t size);
static int app_package_transaction_init(void);
static void app_package_copy_string(char* destination, uint32_t capacity,
                                    const char* source);
static void app_package_history_append(
    app_package_history_operation_t operation,
    app_package_history_outcome_t outcome,
    app_package_action_reason_t reason, const char* id,
    const char* from_version, const char* to_version, uint32_t count);
static void app_package_discard_rollback(const char* id);

static int app_package_text_equals(const uint8_t* text, uint32_t length,
                                   const char* expected) {
    uint32_t expected_length = kstrlen(expected);

    if (length != expected_length) return 0;
    for (uint32_t index = 0; index < length; index++) {
        if (text[index] != (uint8_t)expected[index]) return 0;
    }
    return 1;
}

static int app_package_copy_text(char* destination, uint32_t capacity,
                                 const uint8_t* source, uint32_t length) {
    if (!destination || !source || capacity == 0 || length >= capacity) {
        LOG_ERROR("PKG", "Campo de manifesto invalido");
        return ERR_INVALID;
    }
    for (uint32_t index = 0; index < length; index++) {
        if (source[index] < 0x20U || source[index] > 0x7EU ||
            source[index] == '=') {
            LOG_ERROR("PKG", "Manifesto possui texto nao ASCII");
            return ERR_INVALID;
        }
        destination[index] = (char)source[index];
    }
    destination[length] = '\0';
    return OK;
}

static int app_package_id_is_valid(const char* id) {
    uint32_t length;

    if (!id) {
        LOG_ERROR("PKG", "ID de pacote nulo");
        return ERR_NULL;
    }
    length = kstrlen(id);
    if (length == 0 || length >= APP_PACKAGE_ID_SIZE) {
        LOG_ERROR("PKG", "ID de pacote fora do limite");
        return ERR_INVALID;
    }
    for (uint32_t index = 0; index < length; index++) {
        char character = id[index];
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '_')) {
            LOG_ERROR("PKG", "ID de pacote possui caractere invalido");
            return ERR_INVALID;
        }
    }
    return OK;
}

static void app_package_action_reset(app_package_action_result_t* result) {
    if (result) {
        kmemset(result, 0, sizeof(app_package_action_result_t));
        result->reason = APP_PACKAGE_ACTION_REASON_NONE;
    }
}

static int app_package_action_fail(app_package_action_result_t* result,
                                   app_package_action_reason_t reason,
                                   int error) {
    if (result) result->reason = reason;
    return error;
}

static int app_package_mutation_begin(
    app_package_action_result_t* result) {
    spinlock_acquire(&app_package_mutation_lock);
    if (app_package_mutation_active) {
        spinlock_release(&app_package_mutation_lock);
        LOG_WARN("PKG", "Outra mutacao de pacote esta em andamento");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_MUTATION_BUSY, ERR_STATE);
    }
    app_package_mutation_active = 1;
    spinlock_release(&app_package_mutation_lock);
    return OK;
}

static void app_package_mutation_end(void) {
    spinlock_acquire(&app_package_mutation_lock);
    app_package_mutation_active = 0;
    spinlock_release(&app_package_mutation_lock);
}

int app_package_is_mutation_active(void) {
    int active;

    spinlock_acquire(&app_package_mutation_lock);
    active = app_package_mutation_active;
    spinlock_release(&app_package_mutation_lock);
    return active;
}

static int app_package_alias_is_valid(const char* alias, char* id_out) {
    uint32_t length;
    uint32_t id_length;

    if (!alias || !id_out) {
        LOG_ERROR("PKG", "Alias ou destino de ID nulo");
        return ERR_NULL;
    }
    length = kstrlen(alias);
    if (length < 5U || length >= APP_PACKAGE_ALIAS_SIZE ||
        alias[length - 4U] != '.' || alias[length - 3U] != 'Z' ||
        alias[length - 2U] != 'P' || alias[length - 1U] != 'K') {
        LOG_ERROR("PKG", "Alias de pacote fora do formato ID.ZPK");
        return ERR_INVALID;
    }
    id_length = length - 4U;
    kmemcpy(id_out, alias, id_length);
    id_out[id_length] = '\0';
    return app_package_id_is_valid(id_out);
}

static int app_package_version_is_valid(const char* version) {
    uint32_t dots = 0;
    uint32_t digits = 0;

    if (!version || kstrlen(version) == 0) {
        LOG_ERROR("PKG", "Versao de pacote vazia");
        return ERR_INVALID;
    }
    for (uint32_t index = 0; version[index]; index++) {
        if (version[index] == '.') {
            if (digits == 0) {
                LOG_ERROR("PKG", "Versao de pacote invalida");
                return ERR_INVALID;
            }
            dots++;
            digits = 0;
        } else if (version[index] >= '0' && version[index] <= '9') {
            digits++;
        } else {
            LOG_ERROR("PKG", "Versao de pacote invalida");
            return ERR_INVALID;
        }
    }
    if (dots != 2U || digits == 0) {
        LOG_ERROR("PKG", "Versao de pacote deve usar MAJOR.MINOR.PATCH");
        return ERR_INVALID;
    }
    return OK;
}

int app_package_compare_versions(const char* left, const char* right,
                                 int* comparison_out) {
    const char* left_cursor;
    const char* right_cursor;

    if (!left || !right || !comparison_out) {
        LOG_ERROR("PKG", "Argumento nulo na comparacao de versao");
        return ERR_NULL;
    }
    if (app_package_version_is_valid(left) != OK ||
        app_package_version_is_valid(right) != OK) {
        LOG_ERROR("PKG", "Versao invalida na comparacao");
        return ERR_INVALID;
    }
    left_cursor = left;
    right_cursor = right;
    for (uint32_t part = 0; part < 3U; part++) {
        const char* left_end = left_cursor;
        const char* right_end = right_cursor;

        while (*left_end && *left_end != '.') left_end++;
        while (*right_end && *right_end != '.') right_end++;
        while (left_cursor + 1 < left_end && *left_cursor == '0') left_cursor++;
        while (right_cursor + 1 < right_end && *right_cursor == '0') right_cursor++;
        if ((left_end - left_cursor) != (right_end - right_cursor)) {
            *comparison_out = (left_end - left_cursor) >
                              (right_end - right_cursor) ? 1 : -1;
            return OK;
        }
        while (left_cursor < left_end) {
            if (*left_cursor != *right_cursor) {
                *comparison_out = *left_cursor > *right_cursor ? 1 : -1;
                return OK;
            }
            left_cursor++;
            right_cursor++;
        }
        left_cursor = *left_end == '.' ? left_end + 1 : left_end;
        right_cursor = *right_end == '.' ? right_end + 1 : right_end;
    }
    *comparison_out = 0;
    return OK;
}

static uint32_t app_package_crc32(const uint8_t* content, uint32_t size) {
    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t index = 0; index < size; index++) {
        crc ^= content[index];
        for (uint32_t bit = 0; bit < 8U; bit++) {
            crc = (crc & 1U) ? (crc >> 1) ^ APP_PACKAGE_CRC32_POLYNOMIAL :
                               (crc >> 1);
        }
    }
    return ~crc;
}

static int app_package_header_is_valid(const app_package_header_t* header) {
    if (!header) return 0;
    return header->magic[0] == 'Z' && header->magic[1] == 'P' &&
           header->magic[2] == 'K' && header->magic[3] == 'G' &&
           header->version == APP_PACKAGE_VERSION &&
           header->header_size == APP_PACKAGE_HEADER_SIZE &&
           header->architecture == APP_PACKAGE_ARCH_I386 &&
           header->flags == 0 && header->reserved == 0 &&
           header->manifest_size > 0 &&
           header->manifest_size <= APP_PACKAGE_MAX_MANIFEST_SIZE &&
           header->payload_size > 0 &&
           header->payload_size <= APP_IMAGE_MAX_FILE_SIZE;
}

static int app_package_parse_dependencies(const uint8_t* text, uint32_t length,
                                          app_package_info_t* info) {
    uint32_t start = 0;

    if (length == 0) return OK;
    while (start < length) {
        uint32_t end = start;
        int result;

        while (end < length && text[end] != ',') end++;
        if (info->dependency_count >= APP_PACKAGE_MAX_DEPENDENCIES ||
            end == start) {
            LOG_ERROR("PKG", "Dependencias de pacote invalidas");
            return ERR_INVALID;
        }
        result = app_package_copy_text(
            info->dependencies[info->dependency_count], APP_PACKAGE_ID_SIZE,
            text + start, end - start);
        if (result != OK) return result;
        result = app_package_id_is_valid(
            info->dependencies[info->dependency_count]);
        if (result != OK) return result;
        if (kstrcmp(info->dependencies[info->dependency_count], info->id) == 0) {
            LOG_ERROR("PKG", "Pacote depende de si mesmo");
            return ERR_INVALID;
        }
        for (uint32_t previous = 0; previous < info->dependency_count;
             previous++) {
            if (kstrcmp(info->dependencies[previous],
                        info->dependencies[info->dependency_count]) == 0) {
                LOG_ERROR("PKG", "Pacote possui dependencia duplicada");
                return ERR_INVALID;
            }
        }
        info->dependency_count++;
        start = end + 1U;
    }
    return OK;
}

static int app_package_parse_manifest(const uint8_t* manifest,
                                      uint32_t manifest_size,
                                      app_package_info_t* info) {
    static const char* keys[] = {
        "id", "name", "version", "api", "entry", "dependencies"
    };
    uint32_t cursor = 0;

    if (!manifest || !info || manifest_size == 0 ||
        manifest_size > APP_PACKAGE_MAX_MANIFEST_SIZE) {
        LOG_ERROR("PKG", "Manifesto de pacote invalido");
        return ERR_INVALID;
    }
    kmemset(info, 0, sizeof(app_package_info_t));
    for (uint32_t field = 0; field < 6U; field++) {
        uint32_t line_start = cursor;
        uint32_t equals = manifest_size;
        uint32_t line_end;
        int result;

        while (cursor < manifest_size && manifest[cursor] != '\n') {
            if (manifest[cursor] == '=' && equals == manifest_size) equals = cursor;
            cursor++;
        }
        if (cursor >= manifest_size || equals == manifest_size ||
            equals == line_start) {
            LOG_ERROR("PKG", "Linha de manifesto invalida");
            return ERR_INVALID;
        }
        line_end = cursor;
        cursor++;
        if (!app_package_text_equals(manifest + line_start, equals - line_start,
                                     keys[field])) {
            LOG_ERROR("PKG", "Chave de manifesto invalida");
            return ERR_INVALID;
        }
        if (field == 0U) {
            result = app_package_copy_text(info->id, APP_PACKAGE_ID_SIZE,
                                           manifest + equals + 1U,
                                           line_end - equals - 1U);
        } else if (field == 1U) {
            result = app_package_copy_text(info->name, APP_PACKAGE_NAME_SIZE,
                                           manifest + equals + 1U,
                                           line_end - equals - 1U);
        } else if (field == 2U) {
            result = app_package_copy_text(info->version,
                                           APP_PACKAGE_VERSION_TEXT_SIZE,
                                           manifest + equals + 1U,
                                           line_end - equals - 1U);
        } else if (field == 3U) {
            result = app_package_text_equals(manifest + equals + 1U,
                                             line_end - equals - 1U, "0.3") ?
                     OK : ERR_INVALID;
        } else if (field == 4U) {
            result = app_package_text_equals(manifest + equals + 1U,
                                             line_end - equals - 1U,
                                             APP_PACKAGE_ENTRY_NAME) ?
                     OK : ERR_INVALID;
        } else {
            result = app_package_parse_dependencies(manifest + equals + 1U,
                                                    line_end - equals - 1U,
                                                    info);
        }
        if (result != OK) {
            LOG_ERROR("PKG", "Valor de manifesto invalido");
            return result;
        }
    }
    if (cursor != manifest_size || app_package_id_is_valid(info->id) != OK ||
        info->name[0] == '\0' || app_package_version_is_valid(info->version) != OK) {
        LOG_ERROR("PKG", "Manifesto de pacote incompleto");
        return ERR_INVALID;
    }
    return OK;
}

static int app_package_decode(const uint8_t* data, uint32_t size,
                              app_package_info_t* info,
                              const uint8_t** payload_out,
                              uint32_t* payload_size_out) {
    app_package_header_t header;
    app_image_header_t image_header;
    uint32_t content_size;
    int result;

    if (!data || !info || size < APP_PACKAGE_HEADER_SIZE) {
        LOG_ERROR("PKG", "Pacote menor que o cabecalho");
        return ERR_INVALID;
    }
    kmemcpy(&header, data, APP_PACKAGE_HEADER_SIZE);
    if (!app_package_header_is_valid(&header)) {
        LOG_ERROR("PKG", "Cabecalho ZPKG invalido");
        return ERR_INVALID;
    }
    content_size = header.manifest_size + header.payload_size;
    if (content_size < header.manifest_size ||
        size != APP_PACKAGE_HEADER_SIZE + content_size ||
        app_package_crc32(data + APP_PACKAGE_HEADER_SIZE, content_size) !=
            header.content_crc32) {
        LOG_ERROR("PKG", "Tamanho ou CRC32 de pacote invalido");
        return ERR_INVALID;
    }
    result = app_package_parse_manifest(data + APP_PACKAGE_HEADER_SIZE,
                                        header.manifest_size, info);
    if (result != OK) return result;
    result = app_loader_validate_image(data + APP_PACKAGE_HEADER_SIZE +
                                       header.manifest_size,
                                       header.payload_size, &image_header);
    if (result != OK) {
        LOG_WARN("PKG", "Payload ZAPP invalido no pacote");
        return result;
    }
    if (payload_out) *payload_out = data + APP_PACKAGE_HEADER_SIZE +
                                    header.manifest_size;
    if (payload_size_out) *payload_size_out = header.payload_size;
    return OK;
}

static int app_package_read_file(const char* path, uint8_t** data_out,
                                 uint32_t* size_out) {
    uint8_t* data;
    int read_size;

    if (!path || !data_out || !size_out) {
        LOG_ERROR("PKG", "Argumento nulo ao ler pacote");
        return ERR_NULL;
    }
    data = (uint8_t*)kmalloc(APP_PACKAGE_READ_BUFFER_SIZE);
    if (!data) {
        LOG_ERROR("PKG", "Falha ao alocar buffer de pacote");
        return ERR_MEM;
    }
    read_size = fs_read_file_at(path, data, APP_PACKAGE_READ_BUFFER_SIZE);
    if (read_size < 0) {
        kfree(data);
        LOG_WARN("PKG", "Arquivo de pacote indisponivel");
        return fs_get_type() == FS_TYPE_NONE ? ERR_UNAVAILABLE : ERR_NOT_FOUND;
    }
    if ((uint32_t)read_size > APP_PACKAGE_MAX_FILE_SIZE) {
        kfree(data);
        LOG_WARN("PKG", "Pacote excede o limite de tamanho");
        return ERR_OVERFLOW;
    }
    *data_out = data;
    *size_out = (uint32_t)read_size;
    return OK;
}

static int app_package_find_entry(const char* directory, const char* name,
                                  uint8_t* attributes_out) {
    char found_name[13];
    int count;

    if (!directory || !name) {
        LOG_ERROR("PKG", "Consulta de pacote recebeu argumento nulo");
        return ERR_NULL;
    }
    count = fs_get_file_count_at(directory);
    for (int index = 0; index < count; index++) {
        uint8_t attributes = 0;
        if (fs_get_file_info_at(directory, index, found_name, 0, &attributes) == OK &&
            kstrcmp(found_name, name) == 0) {
            if (attributes_out) *attributes_out = attributes;
            return OK;
        }
    }
    return ERR_NOT_FOUND;
}

static int app_package_ensure_directory(const char* parent, const char* name) {
    uint8_t attributes = 0;
    int result = app_package_find_entry(parent, name, &attributes);

    if (result == OK) {
        if (attributes & APP_PACKAGE_DIRECTORY_ATTRIBUTE) return OK;
        LOG_ERROR("PKG", "Nome de diretorio de pacote ja e arquivo");
        return ERR_STATE;
    }
    if (result != ERR_NOT_FOUND) return result;
    result = fs_create_dir_entry(parent, name, APP_PACKAGE_DIRECTORY_ATTRIBUTE);
    if (result != OK) {
        LOG_ERROR("PKG", "Falha ao criar diretorio de pacote");
        return ERR_DISK;
    }
    return OK;
}

static int app_package_is_installed(const char* id) {
    uint8_t attributes = 0;
    int result = app_package_find_entry(APP_PACKAGE_DIRECTORY, id, &attributes);

    if (result != OK) return result;
    if (!(attributes & APP_PACKAGE_DIRECTORY_ATTRIBUTE)) {
        LOG_ERROR("PKG", "Registro de pacote nao e diretorio");
        return ERR_INVALID;
    }
    return OK;
}

static void app_package_add_blocker(app_package_action_result_t* result,
                                    const char* id) {
    uint32_t position = 0;
    uint32_t limit;

    if (!result || !id) return;
    while (position < result->blocker_count &&
           kstrcmp(result->blocker_ids[position], id) < 0) position++;
    if (position < result->blocker_count &&
        kstrcmp(result->blocker_ids[position], id) == 0) return;
    if (result->blocker_count < APP_PACKAGE_MAX_ACTION_BLOCKERS) {
        limit = result->blocker_count++;
    } else {
        result->blocker_overflow = 1U;
        if (position >= APP_PACKAGE_MAX_ACTION_BLOCKERS) return;
        limit = APP_PACKAGE_MAX_ACTION_BLOCKERS - 1U;
    }
    while (limit > position) {
        kmemcpy(result->blocker_ids[limit],
                result->blocker_ids[limit - 1U], APP_PACKAGE_ID_SIZE);
        limit--;
    }
    kmemset(result->blocker_ids[position], 0, APP_PACKAGE_ID_SIZE);
    kmemcpy(result->blocker_ids[position], id, kstrlen(id));
}

static int app_package_collect_missing(
    const app_package_info_t* info, app_package_action_result_t* result) {
    if (!info || !result) {
        LOG_ERROR("PKG", "Destino nulo ao verificar dependencias");
        return ERR_NULL;
    }
    for (uint32_t index = 0; index < info->dependency_count; index++) {
        if (app_package_is_installed(info->dependencies[index]) != OK) {
            app_package_add_blocker(result, info->dependencies[index]);
        }
    }
    if (result->blocker_count > 0U) {
        LOG_WARN("PKG", "Dependencia de pacote ausente");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_DEPENDENCY_MISSING,
            ERR_NOT_FOUND);
    }
    return OK;
}

static int app_package_evaluate_space(uint32_t payload_size,
                                      uint32_t metadata_size,
                                      uint32_t directory_count,
                                      const fs_info_t* fs_info,
                                      uint32_t* required_out) {
    uint32_t cluster_bytes;
    uint32_t required_clusters;

    if (!fs_info || !required_out || fs_info->bytes_per_sector == 0 ||
        fs_info->sectors_per_cluster == 0 ||
        fs_info->bytes_per_sector >
            0xFFFFFFFFU / fs_info->sectors_per_cluster) {
        LOG_ERROR("PKG", "Geometria invalida para calculo de espaco");
        return ERR_INVALID;
    }
    cluster_bytes = fs_info->bytes_per_sector * fs_info->sectors_per_cluster;
    if (cluster_bytes == 0 || payload_size > 0xFFFFFFFFU - metadata_size ||
        payload_size > 0xFFFFFFFFU - (cluster_bytes - 1U) ||
        metadata_size > 0xFFFFFFFFU - (cluster_bytes - 1U)) {
        LOG_ERROR("PKG", "Tamanho excede calculo de espaco");
        return ERR_OVERFLOW;
    }
    required_clusters = (payload_size + cluster_bytes - 1U) / cluster_bytes;
    if (required_clusters > 0xFFFFFFFFU -
            ((metadata_size + cluster_bytes - 1U) / cluster_bytes)) {
        LOG_ERROR("PKG", "Soma de clusters excede limite");
        return ERR_OVERFLOW;
    }
    required_clusters +=
        (metadata_size + cluster_bytes - 1U) / cluster_bytes;
    if (required_clusters > 0xFFFFFFFFU - directory_count ||
        required_clusters + directory_count >
            0xFFFFFFFFU - APP_PACKAGE_CLUSTER_OVERHEAD) {
        LOG_ERROR("PKG", "Overhead de clusters excede limite");
        return ERR_OVERFLOW;
    }
    required_clusters += directory_count;
    required_clusters += APP_PACKAGE_CLUSTER_OVERHEAD;
    *required_out = required_clusters;
    if (required_clusters > fs_info->free_clusters) {
        LOG_WARN("PKG", "Calculo detectou espaco insuficiente");
    }
    return required_clusters > fs_info->free_clusters ? ERR_DISK : OK;
}

static int app_package_check_space(uint32_t payload_size,
                                   uint32_t metadata_size,
                                   app_package_action_result_t* result) {
    fs_info_t fs_info;
    int check;

    if (!result) {
        LOG_ERROR("PKG", "Resultado nulo para calculo de espaco");
        return ERR_NULL;
    }
    if (fs_get_info(&fs_info) != OK) {
        LOG_WARN("PKG", "Espaco de pacote indisponivel");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_FILESYSTEM_UNAVAILABLE,
            ERR_UNAVAILABLE);
    }
    result->free_clusters = fs_info.free_clusters;
    check = app_package_evaluate_space(
        payload_size, metadata_size, 2U, &fs_info,
        &result->required_clusters);
    if (check == ERR_DISK) {
        LOG_WARN("PKG", "Espaco insuficiente para instalar pacote");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_INSUFFICIENT_SPACE, ERR_DISK);
    }
    if (check != OK) {
        LOG_ERROR("PKG", "Tamanho de pacote invalido para espaco");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_PACKAGE_INVALID, check);
    }
    return OK;
}

static void app_package_cleanup_partial(const char* id) {
    char directory[FS_MAX_PATH];

    if (!id) return;
    kmemcpy(directory, APP_PACKAGE_DIRECTORY, kstrlen(APP_PACKAGE_DIRECTORY));
    directory[kstrlen(APP_PACKAGE_DIRECTORY)] = '/';
    kmemcpy(directory + kstrlen(APP_PACKAGE_DIRECTORY) + 1U, id, kstrlen(id));
    directory[kstrlen(APP_PACKAGE_DIRECTORY) + 1U + kstrlen(id)] = '\0';
    fs_delete_file_in_dir(directory, APP_PACKAGE_ENTRY_NAME);
    fs_delete_file_in_dir(directory, APP_PACKAGE_METADATA_NAME);
    fs_delete_file_in_dir(APP_PACKAGE_DIRECTORY, id);
}

static int app_package_delete_installed_file(const char* directory,
                                             const char* filename) {
    if (fs_get_type() == FS_TYPE_FAT12) {
        return fs_atomic_delete_file_in_dir(directory, filename);
    }
    return fs_delete_file_in_dir(directory, filename);
}

static int app_package_read_metadata(const char* id, app_package_info_t* info) {
    char path[FS_MAX_PATH];
    uint8_t manifest[APP_PACKAGE_MAX_MANIFEST_SIZE];
    int size;
    uint32_t prefix_length = kstrlen(APP_PACKAGE_DIRECTORY);

    if (!id || !info) {
        LOG_ERROR("PKG", "Metadata de pacote recebeu argumento nulo");
        return ERR_NULL;
    }
    kmemcpy(path, APP_PACKAGE_DIRECTORY, prefix_length);
    path[prefix_length] = '/';
    kmemcpy(path + prefix_length + 1U, id, kstrlen(id));
    path[prefix_length + 1U + kstrlen(id)] = '/';
    kmemcpy(path + prefix_length + 2U + kstrlen(id), APP_PACKAGE_METADATA_NAME,
            kstrlen(APP_PACKAGE_METADATA_NAME));
    path[prefix_length + 2U + kstrlen(id) +
         kstrlen(APP_PACKAGE_METADATA_NAME)] = '\0';
    size = fs_read_file_at(path, manifest, APP_PACKAGE_MAX_MANIFEST_SIZE);
    if (size <= 0) {
        return ERR_NOT_FOUND;
    }
    return app_package_parse_manifest(manifest, (uint32_t)size, info);
}

static int app_package_collect_dependents(
    const char* id, app_package_action_result_t* result) {
    int count = app_package_get_installed_count();

    if (!id || !result) {
        LOG_ERROR("PKG", "Destino nulo ao consultar dependentes");
        return ERR_NULL;
    }
    for (int index = 0; index < count; index++) {
        app_package_info_t info;

        if (app_package_get_installed_info(index, &info) != OK) {
            LOG_WARN("PKG", "Falha ao consultar pacote dependente");
            return app_package_action_fail(
                result, APP_PACKAGE_ACTION_REASON_READ_ERROR, ERR_DISK);
        }
        if (kstrcmp(info.id, id) == 0) continue;
        for (uint32_t dependency = 0; dependency < info.dependency_count;
             dependency++) {
            if (kstrcmp(info.dependencies[dependency], id) == 0) {
                app_package_add_blocker(result, info.id);
                break;
            }
        }
    }
    if (result->blocker_count > 0U) {
        LOG_WARN("PKG", "Pacote possui dependente instalado");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_DEPENDENT_INSTALLED,
            ERR_STATE);
    }
    return OK;
}

int app_package_init(void) {
    LOG_INFO("PKG", "Inicializando servico de pacotes");
    app_package_ready = 0;
    spinlock_init(&app_package_mutation_lock);
    app_package_mutation_active = 0;
    kmemset(&app_package_legacy_result, 0,
            sizeof(app_package_legacy_result));
    kmemset(&app_package_transaction_state, 0,
            sizeof(app_package_transaction_state));
    kmemset(&app_package_journal, 0, sizeof(app_package_journal));
    kmemset(&app_package_history, 0, sizeof(app_package_history));
    app_package_transaction_supported = 0;
    app_package_transaction_pending = 0;
    app_package_transaction_failed = 0;
    app_package_history_available = 0;
    app_package_fail_after = 0;
    app_package_state_slot = -1;
    app_package_journal_slot = -1;
    app_package_history_slot = -1;

    if (fs_get_type() == FS_TYPE_NONE || !app_loader_is_ready()) {
        LOG_WARN("PKG", "Dependencias do servico de pacotes indisponiveis");
        LOG_ERROR("PKG", "Servico de pacotes nao foi inicializado");
        return ERR_UNAVAILABLE;
    }
    app_package_ready = 1;
    if (app_package_transaction_init() != OK) {
        LOG_WARN("PKG", "Transacao AS4 indisponivel; consultas permanecem ativas");
    }
    LOG_INFO("PKG", "Servico de pacotes inicializado");
    return OK;
}

int app_package_is_ready(void) {
    return app_package_ready;
}

int app_package_verify_file(const char* path, app_package_info_t* info_out) {
    uint8_t* data = 0;
    uint32_t size = 0;
    int result;

    if (!app_package_ready) {
        LOG_WARN("PKG", "Verificacao solicitada sem servico pronto");
        return ERR_UNAVAILABLE;
    }
    if (!path || !info_out) {
        LOG_ERROR("PKG", "Verificacao recebeu argumento nulo");
        return ERR_NULL;
    }
    result = app_package_read_file(path, &data, &size);
    if (result == OK) result = app_package_decode(data, size, info_out, 0, 0);
    if (data) kfree(data);
    if (result != OK) LOG_WARN("PKG", "Verificacao de pacote falhou");
    return result;
}

static int app_package_check_action_ready(
    app_package_action_result_t* result) {
    if (fs_get_type() == FS_TYPE_NONE) {
        LOG_WARN("PKG", "Filesystem indisponivel para acao de pacote");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_FILESYSTEM_UNAVAILABLE,
            ERR_UNAVAILABLE);
    }
    if (!app_loader_is_ready()) {
        LOG_WARN("PKG", "Loader indisponivel para acao de pacote");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_LOADER_UNAVAILABLE,
            ERR_UNAVAILABLE);
    }
    if (!app_package_ready) {
        LOG_WARN("PKG", "Servico indisponivel para acao de pacote");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_PACKAGE_SERVICE_UNAVAILABLE,
            ERR_UNAVAILABLE);
    }
    return OK;
}

static int app_package_check_transaction_for_mutation(
    app_package_action_result_t* result) {
    if (app_package_transaction_supported && app_package_transaction_failed) {
        LOG_WARN("PKG", "Mutacao bloqueada por recuperacao AS4 falha");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_RECOVERY_FAILED, ERR_STATE);
    }
    if (app_package_transaction_supported && app_package_transaction_pending) {
        LOG_WARN("PKG", "Mutacao bloqueada por journal AS4 pendente");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_TRANSACTION_PENDING, ERR_STATE);
    }
    return OK;
}

static int app_package_read_install_source(
    const char* path, int enforce_alias, app_package_install_context_t* context,
    app_package_action_result_t* result) {
    char expected_id[APP_PACKAGE_ID_SIZE];
    int read_result;

    if (!path || !context || !result) {
        LOG_ERROR("PKG", "Fonte ou destino nulo no preflight");
        return ERR_NULL;
    }
    if (enforce_alias &&
        app_package_alias_is_valid(path, expected_id) != OK) {
        LOG_ERROR("PKG", "Alias de instalacao invalido");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_INVALID_ARGUMENT, ERR_INVALID);
    }
    read_result = app_package_read_file(
        path, &context->data, &context->size);
    if (read_result != OK) {
        app_package_action_reason_t reason =
            read_result == ERR_NOT_FOUND ?
                APP_PACKAGE_ACTION_REASON_SOURCE_NOT_FOUND :
                APP_PACKAGE_ACTION_REASON_READ_ERROR;

        if (read_result == ERR_UNAVAILABLE) {
            reason = APP_PACKAGE_ACTION_REASON_FILESYSTEM_UNAVAILABLE;
        }
        return app_package_action_fail(result, reason, read_result);
    }
    read_result = app_package_decode(
        context->data, context->size, &context->info, &context->payload,
        &context->payload_size);
    if (read_result != OK) {
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_PACKAGE_INVALID, read_result);
    }
    result->info = context->info;
    if (enforce_alias && kstrcmp(expected_id, context->info.id) != 0) {
        LOG_WARN("PKG", "Alias nao corresponde ao ID do pacote");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_ALIAS_MISMATCH, ERR_INVALID);
    }
    return OK;
}

static int app_package_check_install_constraints(
    const app_package_install_context_t* context, int dependency_first,
    app_package_action_result_t* result) {
    int installed;
    int dependencies;

    if (!context || !result) {
        LOG_ERROR("PKG", "Contexto nulo nas restricoes de instalacao");
        return ERR_NULL;
    }
    installed = app_package_is_installed(context->info.id);
    if (installed != OK && installed != ERR_NOT_FOUND) {
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_READ_ERROR, installed);
    }
    if (!dependency_first && installed == OK) {
        LOG_WARN("PKG", "Pacote ja esta instalado");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_ALREADY_INSTALLED, ERR_STATE);
    }
    dependencies = app_package_collect_missing(&context->info, result);
    if (dependencies != OK) return dependencies;
    if (installed == OK) {
        LOG_WARN("PKG", "Pacote ja esta instalado");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_ALREADY_INSTALLED, ERR_STATE);
    }
    return app_package_check_space(
        context->payload_size,
        context->size - APP_PACKAGE_HEADER_SIZE - context->payload_size,
        result);
}

static int app_package_prepare_install(
    const char* path, int enforce_alias, int dependency_first,
    int mutation_owned, app_package_install_context_t* context,
    app_package_action_result_t* result) {
    int check;

    if (context) kmemset(context, 0, sizeof(app_package_install_context_t));
    if (!path || !context || !result) {
        LOG_ERROR("PKG", "Preflight de instalacao recebeu argumento nulo");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_INVALID_ARGUMENT, ERR_NULL);
    }
    check = app_package_check_action_ready(result);
    if (check != OK) return check;
    check = app_package_check_transaction_for_mutation(result);
    if (check != OK) return check;
    if (!mutation_owned && app_package_is_mutation_active()) {
        LOG_WARN("PKG", "Preflight bloqueado por mutacao ativa");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_MUTATION_BUSY, ERR_STATE);
    }
    if (app_loader_is_foreground_active()) {
        LOG_WARN("PKG", "Instalacao bloqueada por aplicativo em primeiro plano");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_LOADER_BUSY, ERR_STATE);
    }
    check = app_package_read_install_source(
        path, enforce_alias, context, result);
    if (check != OK) return check;
    return app_package_check_install_constraints(
        context, dependency_first, result);
}

static int app_package_commit_install(
    app_package_install_context_t* context,
    app_package_action_result_t* action_result) {
    char directory[FS_MAX_PATH];
    int package_directory_created = 0;
    int status;

    if (!context || !action_result) {
        LOG_ERROR("PKG", "Contexto nulo ao gravar pacote");
        return ERR_NULL;
    }
    status = app_package_ensure_directory("", APP_PACKAGE_DIRECTORY);
    if (status == OK) {
        status = fs_create_dir_entry(APP_PACKAGE_DIRECTORY, context->info.id,
                                     APP_PACKAGE_DIRECTORY_ATTRIBUTE);
        if (status == OK) {
            package_directory_created = 1;
        } else {
            LOG_ERROR("PKG", "Falha ao criar diretorio do pacote");
            status = ERR_DISK;
        }
    }
    if (status == OK) {
        kmemcpy(directory, APP_PACKAGE_DIRECTORY, kstrlen(APP_PACKAGE_DIRECTORY));
        directory[kstrlen(APP_PACKAGE_DIRECTORY)] = '/';
        kmemcpy(directory + kstrlen(APP_PACKAGE_DIRECTORY) + 1U,
                context->info.id, kstrlen(context->info.id));
        directory[kstrlen(APP_PACKAGE_DIRECTORY) + 1U +
                  kstrlen(context->info.id)] = '\0';
        status = fs_write_file_in_dir(
            directory, APP_PACKAGE_ENTRY_NAME, context->payload,
            context->payload_size);
        if (status >= 0) status = fs_write_file_in_dir(directory,
                                                        APP_PACKAGE_METADATA_NAME,
                                                        context->data +
                                                        APP_PACKAGE_HEADER_SIZE,
                                                        context->size -
                                                        APP_PACKAGE_HEADER_SIZE -
                                                        context->payload_size);
        if (status >= 0) status = OK;
        else status = ERR_DISK;
    }
    if (status != OK) {
        if (package_directory_created) {
            app_package_cleanup_partial(context->info.id);
        }
        LOG_WARN("PKG", "Instalacao de pacote falhou");
        return app_package_action_fail(
            action_result, APP_PACKAGE_ACTION_REASON_WRITE_ERROR, status);
    } else {
        LOG_INFO("PKG", "Pacote instalado com sucesso");
    }
    return OK;
}

static void app_package_release_install(
    app_package_install_context_t* context) {
    if (context && context->data) {
        kfree(context->data);
        context->data = 0;
    }
}

static int app_package_build_single_install_plan(
    const char* alias, app_package_plan_t* plan_out,
    app_package_action_result_t* result_out) {
    app_package_install_context_t context;
    int result;

    if (!alias || !plan_out || !result_out) return ERR_NULL;
    kmemset(&context, 0, sizeof(context));
    kmemset(plan_out, 0, sizeof(*plan_out));
    result = app_package_read_install_source(alias, 1, &context, result_out);
    if (result == OK) {
        app_package_plan_entry_t* entry = &plan_out->entries[0];

        app_package_copy_string(entry->id, sizeof(entry->id), context.info.id);
        app_package_copy_string(entry->alias, sizeof(entry->alias), alias);
        app_package_copy_string(entry->to_version, sizeof(entry->to_version),
                                context.info.version);
        entry->action = APP_PACKAGE_PLAN_ACTION_INSTALL;
        plan_out->entry_count = 1U;
        plan_out->target_index = 0U;
    }
    app_package_release_install(&context);
    return result;
}

int app_package_preflight_install(
    const char* alias, app_package_action_result_t* result_out) {
    app_package_install_context_t context;
    int result;

    if (!result_out) {
        LOG_ERROR("PKG", "Saida nula para preflight de instalacao");
        return ERR_NULL;
    }
    app_package_action_reset(result_out);
    if (app_package_transaction_supported) {
        result = app_package_build_single_install_plan(
            alias, &result_out->plan, result_out);
        return result == OK ?
            app_package_preflight_plan(&result_out->plan, result_out) : result;
    }
    result = app_package_prepare_install(
        alias, 1, 1, 0, &context, result_out);
    app_package_release_install(&context);
    return result;
}

int app_package_install_confirmed(
    const char* alias, app_package_action_result_t* result_out) {
    app_package_install_context_t context;
    int result;

    if (!result_out) {
        LOG_ERROR("PKG", "Saida nula para instalacao confirmada");
        return ERR_NULL;
    }
    app_package_action_reset(result_out);
    if (app_package_transaction_supported) {
        result = app_package_build_single_install_plan(
            alias, &result_out->plan, result_out);
        return result == OK ?
            app_package_apply_plan_confirmed(&result_out->plan, result_out) :
            result;
    }
    result = app_package_mutation_begin(result_out);
    if (result == OK) {
        result = app_package_prepare_install(
            alias, 1, 1, 1, &context, result_out);
        if (result == OK) {
            result = app_package_commit_install(&context, result_out);
            if (result == OK) {
                app_package_history_append(APP_PACKAGE_HISTORY_OPERATION_INSTALL,
                                           APP_PACKAGE_HISTORY_OUTCOME_SUCCESS,
                                           APP_PACKAGE_ACTION_REASON_NONE,
                                           context.info.id, "",
                                           context.info.version, 1U);
            }
        }
        app_package_release_install(&context);
        app_package_mutation_end();
    }
    return result;
}

int app_package_install_file(const char* path, app_package_info_t* info_out) {
    app_package_install_context_t context;
    uint32_t path_length;
    int result;

    if (!path || !info_out) {
        LOG_ERROR("PKG", "Instalacao recebeu argumento nulo");
        return ERR_NULL;
    }
    app_package_action_reset(&app_package_legacy_result);
    path_length = kstrlen(path);
    if (app_package_transaction_supported && path_length >= 4U &&
        path[path_length - 4U] == '.' && path[path_length - 3U] == 'Z' &&
        path[path_length - 2U] == 'P' && path[path_length - 1U] == 'K' &&
        path_length < APP_PACKAGE_ALIAS_SIZE) {
        result = app_package_build_single_install_plan(
            path, &app_package_legacy_result.plan, &app_package_legacy_result);
        if (result == OK) {
            result = app_package_apply_plan_confirmed(
                &app_package_legacy_result.plan, &app_package_legacy_result);
        }
        if (result == OK) *info_out = app_package_legacy_result.info;
        return result;
    }
    result = app_package_mutation_begin(&app_package_legacy_result);
    if (result == OK) {
        result = app_package_prepare_install(
            path, 0, 0, 1, &context, &app_package_legacy_result);
        if (result == OK) {
            result = app_package_commit_install(
                &context, &app_package_legacy_result);
            if (result == OK) {
                app_package_history_append(APP_PACKAGE_HISTORY_OPERATION_INSTALL,
                                           APP_PACKAGE_HISTORY_OUTCOME_SUCCESS,
                                           APP_PACKAGE_ACTION_REASON_NONE,
                                           context.info.id, "",
                                           context.info.version, 1U);
            }
        }
        if (result == OK) *info_out = context.info;
        app_package_release_install(&context);
        app_package_mutation_end();
    }
    return result;
}

int app_package_get_installed_count(void) {
    int count;
    int installed = 0;

    if (!app_package_ready) return 0;
    count = fs_get_file_count_at(APP_PACKAGE_DIRECTORY);
    for (int index = 0; index < count; index++) {
        char name[13];
        uint8_t attributes = 0;
        app_package_info_t info;
        if (fs_get_file_info_at(APP_PACKAGE_DIRECTORY, index, name, 0,
                                &attributes) == OK &&
            (attributes & APP_PACKAGE_DIRECTORY_ATTRIBUTE) &&
            app_package_read_metadata(name, &info) == OK) {
            installed++;
        }
    }
    return installed;
}

int app_package_get_installed_info(int index, app_package_info_t* info_out) {
    int count;
    int installed_index = 0;

    if (!app_package_ready) {
        LOG_WARN("PKG", "Listagem solicitada sem servico pronto");
        return ERR_UNAVAILABLE;
    }
    if (index < 0 || !info_out) {
        LOG_ERROR("PKG", "Indice ou destino de pacote invalido");
        return ERR_INVALID;
    }
    count = fs_get_file_count_at(APP_PACKAGE_DIRECTORY);
    for (int entry = 0; entry < count; entry++) {
        char name[13];
        uint8_t attributes = 0;
        app_package_info_t info;
        if (fs_get_file_info_at(APP_PACKAGE_DIRECTORY, entry, name, 0,
                                &attributes) != OK ||
            !(attributes & APP_PACKAGE_DIRECTORY_ATTRIBUTE) ||
            app_package_read_metadata(name, &info) != OK) continue;
        if (installed_index++ == index) {
            *info_out = info;
            return OK;
        }
    }
    LOG_WARN("PKG", "Indice de pacote instalado nao encontrado");
    return ERR_NOT_FOUND;
}

int app_package_get_installed_info_by_id(const char* id,
                                         app_package_info_t* info_out) {
    int count;

    if (!app_package_ready) {
        LOG_WARN("PKG", "Consulta solicitada sem servico pronto");
        return ERR_UNAVAILABLE;
    }
    if (app_package_id_is_valid(id) != OK || !info_out) {
        LOG_ERROR("PKG", "Consulta de pacote recebeu ID invalido");
        return ERR_INVALID;
    }
    count = app_package_get_installed_count();
    for (int index = 0; index < count; index++) {
        app_package_info_t info;
        if (app_package_get_installed_info(index, &info) == OK &&
            kstrcmp(info.id, id) == 0) {
            *info_out = info;
            return OK;
        }
    }
    LOG_WARN("PKG", "Pacote instalado nao encontrado");
    return ERR_NOT_FOUND;
}

static int app_package_prepare_remove(
    const char* id, int require_metadata, int mutation_owned,
    app_package_action_result_t* result) {
    int status;

    if (!id || !result) {
        LOG_ERROR("PKG", "Preflight de remocao recebeu argumento nulo");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_INVALID_ARGUMENT, ERR_NULL);
    }
    status = app_package_check_action_ready(result);
    if (status != OK) return status;
    status = app_package_check_transaction_for_mutation(result);
    if (status != OK) return status;
    if (!mutation_owned && app_package_is_mutation_active()) {
        LOG_WARN("PKG", "Remocao bloqueada por mutacao ativa");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_MUTATION_BUSY, ERR_STATE);
    }
    if (app_loader_is_foreground_active()) {
        LOG_WARN("PKG", "Remocao bloqueada por aplicativo em primeiro plano");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_LOADER_BUSY, ERR_STATE);
    }
    if (app_package_id_is_valid(id) != OK) {
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_INVALID_ARGUMENT, ERR_INVALID);
    }
    status = app_package_is_installed(id);
    if (status == ERR_NOT_FOUND) {
        LOG_WARN("PKG", "Pacote solicitado nao esta instalado");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_NOT_INSTALLED, ERR_NOT_FOUND);
    }
    if (status != OK) {
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_READ_ERROR, status);
    }
    status = app_package_read_metadata(id, &result->info);
    if (status != OK && require_metadata) {
        LOG_WARN("PKG", "Metadata instalada nao pode ser lida");
        return app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_READ_ERROR, status);
    }
    return app_package_collect_dependents(id, result);
}

static int app_package_commit_remove(
    const char* id, app_package_action_result_t* action_result) {
    char directory[FS_MAX_PATH];

    if (!id || !action_result) {
        LOG_ERROR("PKG", "ID ou resultado nulo ao remover pacote");
        return ERR_NULL;
    }
    kmemcpy(directory, APP_PACKAGE_DIRECTORY, kstrlen(APP_PACKAGE_DIRECTORY));
    directory[kstrlen(APP_PACKAGE_DIRECTORY)] = '/';
    kmemcpy(directory + kstrlen(APP_PACKAGE_DIRECTORY) + 1U, id, kstrlen(id));
    directory[kstrlen(APP_PACKAGE_DIRECTORY) + 1U + kstrlen(id)] = '\0';
    if (app_package_delete_installed_file(directory,
                                          APP_PACKAGE_ENTRY_NAME) != OK ||
        app_package_delete_installed_file(directory,
                                          APP_PACKAGE_METADATA_NAME) != OK ||
        fs_delete_file_in_dir(APP_PACKAGE_DIRECTORY, id) != OK) {
        LOG_ERROR("PKG", "Falha ao remover registro de pacote");
        return app_package_action_fail(
            action_result, APP_PACKAGE_ACTION_REASON_WRITE_ERROR, ERR_DISK);
    }
    LOG_INFO("PKG", "Pacote removido com sucesso");
    return OK;
}

int app_package_preflight_remove(
    const char* id, app_package_action_result_t* result_out) {
    if (!result_out) {
        LOG_ERROR("PKG", "Saida nula para preflight de remocao");
        return ERR_NULL;
    }
    app_package_action_reset(result_out);
    return app_package_prepare_remove(id, 1, 0, result_out);
}

int app_package_remove_confirmed(
    const char* id, app_package_action_result_t* result_out) {
    int result;

    if (!result_out) {
        LOG_ERROR("PKG", "Saida nula para remocao confirmada");
        return ERR_NULL;
    }
    app_package_action_reset(result_out);
    result = app_package_mutation_begin(result_out);
    if (result == OK) {
        result = app_package_prepare_remove(id, 1, 1, result_out);
        if (result == OK) {
            result = app_package_commit_remove(id, result_out);
            if (result == OK) {
                app_package_discard_rollback(result_out->info.id);
                app_package_history_append(APP_PACKAGE_HISTORY_OPERATION_REMOVE,
                                           APP_PACKAGE_HISTORY_OUTCOME_SUCCESS,
                                           APP_PACKAGE_ACTION_REASON_NONE,
                                           result_out->info.id,
                                           result_out->info.version, "", 1U);
            }
        }
        app_package_mutation_end();
    }
    return result;
}

int app_package_remove(const char* id) {
    int result;

    if (!id) {
        LOG_ERROR("PKG", "Remocao recebeu ID nulo");
        return ERR_NULL;
    }
    app_package_action_reset(&app_package_legacy_result);
    result = app_package_mutation_begin(&app_package_legacy_result);
    if (result == OK) {
        result = app_package_prepare_remove(
            id, 0, 1, &app_package_legacy_result);
        if (result == OK) {
            result = app_package_commit_remove(
                id, &app_package_legacy_result);
            if (result == OK) {
                app_package_discard_rollback(
                    app_package_legacy_result.info.id);
                app_package_history_append(APP_PACKAGE_HISTORY_OPERATION_REMOVE,
                                           APP_PACKAGE_HISTORY_OUTCOME_SUCCESS,
                                           APP_PACKAGE_ACTION_REASON_NONE,
                                           app_package_legacy_result.info.id,
                                           app_package_legacy_result.info.version,
                                           "", 1U);
            }
        }
        app_package_mutation_end();
    }
    return result;
}

static void app_package_build_entry_path(char* path, const char* id) {
    uint32_t prefix_length = kstrlen(APP_PACKAGE_DIRECTORY);

    kmemcpy(path, APP_PACKAGE_DIRECTORY, prefix_length);
    path[prefix_length] = '/';
    kmemcpy(path + prefix_length + 1U, id, kstrlen(id));
    path[prefix_length + 1U + kstrlen(id)] = '/';
    kmemcpy(path + prefix_length + 2U + kstrlen(id),
            APP_PACKAGE_ENTRY_NAME, kstrlen(APP_PACKAGE_ENTRY_NAME));
    path[prefix_length + 2U + kstrlen(id) +
         kstrlen(APP_PACKAGE_ENTRY_NAME)] = '\0';
}

int app_package_run_installed(
    const char* id, const app_launch_info_t* launch, uint32_t* pid_out,
    app_package_action_result_t* result_out) {
    char path[FS_MAX_PATH];
    int result;

    if (!id || !pid_out || !result_out) {
        LOG_ERROR("PKG", "Execucao instalada recebeu argumento nulo");
        return ERR_NULL;
    }
    app_package_action_reset(result_out);
    result = app_package_check_action_ready(result_out);
    if (result != OK) return result;
    if (app_package_is_mutation_active()) {
        LOG_WARN("PKG", "Execucao bloqueada por mutacao ativa");
        return app_package_action_fail(
            result_out, APP_PACKAGE_ACTION_REASON_MUTATION_BUSY, ERR_STATE);
    }
    if (app_package_id_is_valid(id) != OK) {
        return app_package_action_fail(
            result_out, APP_PACKAGE_ACTION_REASON_INVALID_ARGUMENT,
            ERR_INVALID);
    }
    if (app_loader_is_foreground_active()) {
        LOG_WARN("PKG", "Execucao solicitada com loader ocupado");
        return app_package_action_fail(
            result_out, APP_PACKAGE_ACTION_REASON_LOADER_BUSY, ERR_STATE);
    }
    result = app_package_read_metadata(id, &result_out->info);
    if (result != OK) {
        LOG_WARN("PKG", "Pacote instalado nao encontrado para execucao");
        return app_package_action_fail(
            result_out, APP_PACKAGE_ACTION_REASON_NOT_INSTALLED,
            ERR_NOT_FOUND);
    }
    app_package_build_entry_path(path, id);
    result = app_loader_run_file_with_launch(path, launch, pid_out);
    if (result == OK) return OK;
    if (result == ERR_STATE) {
        return app_package_action_fail(
            result_out, APP_PACKAGE_ACTION_REASON_LOADER_BUSY, result);
    }
    if (result == ERR_UNAVAILABLE) {
        return app_package_action_fail(
            result_out, APP_PACKAGE_ACTION_REASON_LOADER_UNAVAILABLE, result);
    }
    return app_package_action_fail(
        result_out, result == ERR_INVALID || result == ERR_OVERFLOW ?
            APP_PACKAGE_ACTION_REASON_INVALID_ARGUMENT :
            APP_PACKAGE_ACTION_REASON_READ_ERROR,
        result);
}

static void app_package_copy_string(char* destination, uint32_t capacity,
                                    const char* source) {
    uint32_t length = source ? kstrlen(source) : 0U;

    if (!destination || capacity == 0U) return;
    if (length >= capacity) length = capacity - 1U;
    if (length) kmemcpy(destination, source, length);
    destination[length] = '\0';
}

static uint32_t app_package_record_checksum(const void* record,
                                            uint32_t size,
                                            uint32_t checksum_offset) {
    uint8_t* bytes = (uint8_t*)record;
    uint32_t checksum = 0;

    if (!record || checksum_offset + sizeof(uint32_t) > size) return 0U;
    for (uint32_t index = 0; index < size; index++) {
        uint8_t value = (index >= checksum_offset &&
                         index < checksum_offset + sizeof(uint32_t)) ?
                        0U : bytes[index];
        checksum ^= value;
        for (uint32_t bit = 0; bit < 8U; bit++) {
            checksum = (checksum & 1U) ?
                       (checksum >> 1) ^ APP_PACKAGE_CRC32_POLYNOMIAL :
                       (checksum >> 1);
        }
    }
    return ~checksum;
}

static int app_package_state_is_valid(const app_package_transaction_state_t* state) {
    uint32_t count = 0;

    if (!state || state->magic != 0x3453415AU ||
        state->version != APP_PACKAGE_CONTROL_VERSION ||
        state->rollback_count > APP_PACKAGE_ROLLBACK_SLOTS) return 0;
    for (uint32_t index = 0; index < APP_PACKAGE_ROLLBACK_SLOTS; index++) {
        const app_package_rollback_record_t* rollback =
            &state->rollbacks[index];

        if (rollback->available > 1U) return 0;
        if (!rollback->available) continue;
        if (rollback->slot >= APP_PACKAGE_ROLLBACK_SLOTS ||
            app_package_id_is_valid(rollback->id) != OK ||
            app_package_version_is_valid(rollback->version) != OK) {
            return 0;
        }
        for (uint32_t previous = 0; previous < index; previous++) {
            const app_package_rollback_record_t* other =
                &state->rollbacks[previous];

            if (other->available &&
                (other->slot == rollback->slot ||
                 kstrcmp(other->id, rollback->id) == 0)) return 0;
        }
        count++;
    }
    if (count != state->rollback_count) return 0;
    return state->checksum == app_package_record_checksum(
        state, sizeof(*state), (uint32_t)((uint8_t*)&state->checksum -
                                           (uint8_t*)state));
}

static int app_package_journal_is_valid(const app_package_journal_t* journal) {
    if (!journal || journal->magic != 0x34534A5AU ||
        journal->version != APP_PACKAGE_CONTROL_VERSION ||
        journal->phase > APP_PACKAGE_JOURNAL_COMMITTED || journal->slot > 1U ||
        journal->backup_slot >= APP_PACKAGE_ROLLBACK_SLOTS ||
        (journal->previous_backup_slot != APP_PACKAGE_NO_BACKUP_SLOT &&
         journal->previous_backup_slot >= APP_PACKAGE_ROLLBACK_SLOTS) ||
        journal->operation > APP_PACKAGE_HISTORY_OPERATION_ROLLBACK ||
        journal->plan.entry_count > APP_PACKAGE_MAX_PLAN_ENTRIES ||
        journal->plan.target_index >= APP_PACKAGE_MAX_PLAN_ENTRIES) return 0;
    return journal->checksum == app_package_record_checksum(
        journal, sizeof(*journal), (uint32_t)((uint8_t*)&journal->checksum -
                                               (uint8_t*)journal));
}

static int app_package_history_is_valid(const app_package_history_store_t* history) {
    if (!history || history->magic != 0x3453485AU ||
        history->version != APP_PACKAGE_CONTROL_VERSION ||
        history->count > APP_PACKAGE_HISTORY_MAX_ENTRIES ||
        history->next_index >= APP_PACKAGE_HISTORY_MAX_ENTRIES) return 0;
    return history->checksum == app_package_record_checksum(
        history, sizeof(*history), (uint32_t)((uint8_t*)&history->checksum -
                                               (uint8_t*)history));
}

static int app_package_read_control(const char* path, void* buffer,
                                    uint32_t expected_size) {
    int size;

    if (!path || !buffer || expected_size == 0U) return ERR_NULL;
    size = fs_read_file(path, (uint8_t*)buffer, expected_size);
    if (size < 0) return ERR_NOT_FOUND;
    return (uint32_t)size == expected_size ? OK : ERR_INVALID;
}

static int app_package_write_state(void) {
    int slot = app_package_state_slot < 0 ? 0 : 1 - app_package_state_slot;
    int result;

    app_package_transaction_state.magic = 0x3453415AU;
    app_package_transaction_state.version = APP_PACKAGE_CONTROL_VERSION;
    app_package_transaction_state.sequence++;
    app_package_transaction_state.checksum = app_package_record_checksum(
        &app_package_transaction_state, sizeof(app_package_transaction_state),
        (uint32_t)((uint8_t*)&app_package_transaction_state.checksum -
                   (uint8_t*)&app_package_transaction_state));
    result = fs_atomic_write_root(app_package_state_paths[slot],
                                  (const uint8_t*)&app_package_transaction_state,
                                  sizeof(app_package_transaction_state),
                                  APP_PACKAGE_CONTROL_ATTRIBUTES,
                                  FS_ATOMIC_CREATE_OR_REPLACE);
    if (result == OK) app_package_state_slot = slot;
    return result;
}

static int app_package_write_journal(void) {
    int slot = app_package_journal_slot < 0 ? 0 : 1 - app_package_journal_slot;
    int result;

    app_package_journal.magic = 0x34534A5AU;
    app_package_journal.version = APP_PACKAGE_CONTROL_VERSION;
    app_package_journal.checksum = app_package_record_checksum(
        &app_package_journal, sizeof(app_package_journal),
        (uint32_t)((uint8_t*)&app_package_journal.checksum -
                   (uint8_t*)&app_package_journal));
    result = fs_atomic_write_root(app_package_journal_paths[slot],
                                  (const uint8_t*)&app_package_journal,
                                  sizeof(app_package_journal),
                                  APP_PACKAGE_CONTROL_ATTRIBUTES,
                                  FS_ATOMIC_CREATE_OR_REPLACE);
    if (result == OK) app_package_journal_slot = slot;
    return result;
}

static int app_package_write_history(void) {
    int slot = app_package_history_slot < 0 ? 0 : 1 - app_package_history_slot;
    int result;

    app_package_history.magic = 0x3453485AU;
    app_package_history.version = APP_PACKAGE_CONTROL_VERSION;
    app_package_history.sequence++;
    app_package_history.checksum = app_package_record_checksum(
        &app_package_history, sizeof(app_package_history),
        (uint32_t)((uint8_t*)&app_package_history.checksum -
                   (uint8_t*)&app_package_history));
    result = fs_atomic_write_root(app_package_history_paths[slot],
                                  (const uint8_t*)&app_package_history,
                                  sizeof(app_package_history),
                                  APP_PACKAGE_CONTROL_ATTRIBUTES,
                                  FS_ATOMIC_CREATE_OR_REPLACE);
    if (result == OK) app_package_history_slot = slot;
    return result;
}

static void app_package_stage_path(uint8_t slot, uint32_t index,
                                   const char* extension, char output[13]) {
    static const char digits[] = "0123456789ABCDEF";

    output[0] = 'Z';
    output[1] = 'S';
    output[2] = digits[(slot >> 4U) & 0x0FU];
    output[3] = digits[slot & 0x0FU];
    output[4] = digits[index & 0x0FU];
    output[5] = '.';
    output[6] = extension[0];
    output[7] = extension[1];
    output[8] = extension[2];
    output[9] = '\0';
}

static void app_package_backup_path(uint8_t slot, const char* extension,
                                    char output[13]) {
    static const char digits[] = "0123456789ABCDEF";

    output[0] = 'Z';
    output[1] = 'B';
    output[2] = digits[(slot >> 4U) & 0x0FU];
    output[3] = digits[slot & 0x0FU];
    output[4] = '.';
    output[5] = extension[0];
    output[6] = extension[1];
    output[7] = extension[2];
    output[8] = '\0';
}

static int app_package_state_find_rollback(const char* id) {
    if (!id) return -1;
    for (uint32_t index = 0; index < APP_PACKAGE_ROLLBACK_SLOTS; index++) {
        const app_package_rollback_record_t* rollback =
            &app_package_transaction_state.rollbacks[index];

        if (rollback->available && kstrcmp(rollback->id, id) == 0) {
            return (int)index;
        }
    }
    return -1;
}

static int app_package_state_find_free_slot(void) {
    for (uint32_t slot = 0; slot < APP_PACKAGE_ROLLBACK_SLOTS; slot++) {
        int used = 0;

        for (uint32_t index = 0; index < APP_PACKAGE_ROLLBACK_SLOTS; index++) {
            const app_package_rollback_record_t* rollback =
                &app_package_transaction_state.rollbacks[index];

            if (rollback->available && rollback->slot == slot) {
                used = 1;
                break;
            }
        }
        if (!used) return (int)slot;
    }
    return -1;
}

static int app_package_state_store_rollback(const char* id,
                                             const char* version,
                                             uint8_t slot) {
    app_package_rollback_record_t* rollback;
    int index = app_package_state_find_rollback(id);

    if (!id || !version || slot >= APP_PACKAGE_ROLLBACK_SLOTS) return ERR_INVALID;
    if (index < 0) {
        for (uint32_t candidate = 0;
             candidate < APP_PACKAGE_ROLLBACK_SLOTS; candidate++) {
            if (!app_package_transaction_state.rollbacks[candidate].available) {
                index = (int)candidate;
                app_package_transaction_state.rollback_count++;
                break;
            }
        }
    }
    if (index < 0) return ERR_OVERFLOW;
    rollback = &app_package_transaction_state.rollbacks[index];
    kmemset(rollback, 0, sizeof(*rollback));
    rollback->available = 1U;
    rollback->slot = slot;
    app_package_copy_string(rollback->id, sizeof(rollback->id), id);
    app_package_copy_string(rollback->version, sizeof(rollback->version),
                            version);
    return OK;
}

static void app_package_state_remove_rollback(const char* id) {
    int index = app_package_state_find_rollback(id);

    if (index < 0) return;
    kmemset(&app_package_transaction_state.rollbacks[index], 0,
            sizeof(app_package_transaction_state.rollbacks[index]));
    if (app_package_transaction_state.rollback_count) {
        app_package_transaction_state.rollback_count--;
    }
}

static void app_package_build_directory(char* directory, const char* id) {
    uint32_t prefix = kstrlen(APP_PACKAGE_DIRECTORY);

    kmemcpy(directory, APP_PACKAGE_DIRECTORY, prefix);
    directory[prefix] = '/';
    kmemcpy(directory + prefix + 1U, id, kstrlen(id));
    directory[prefix + 1U + kstrlen(id)] = '\0';
}

static int app_package_read_installed_files(const char* id,
                                            uint32_t* app_size_out,
                                            uint32_t* meta_size_out) {
    char directory[FS_MAX_PATH];
    char path[FS_MAX_PATH];
    uint32_t directory_length;
    int app_size;
    int meta_size;

    if (!id || !app_size_out || !meta_size_out) return ERR_NULL;
    app_package_build_directory(directory, id);
    app_package_build_entry_path(path, id);
    app_size = fs_read_file_at(path, app_package_transaction_app_buffer,
                               sizeof(app_package_transaction_app_buffer));
    if (app_size <= 0) return ERR_NOT_FOUND;
    directory_length = kstrlen(directory);
    kmemcpy(path, directory, directory_length);
    path[directory_length] = '/';
    app_package_copy_string(path + directory_length + 1U,
                            sizeof(path) - directory_length - 1U,
                            APP_PACKAGE_METADATA_NAME);
    meta_size = fs_read_file_at(path, app_package_transaction_meta_buffer,
                                sizeof(app_package_transaction_meta_buffer));
    if (meta_size <= 0) return ERR_NOT_FOUND;
    *app_size_out = (uint32_t)app_size;
    *meta_size_out = (uint32_t)meta_size;
    return OK;
}

static int app_package_plan_contains_before(const app_package_plan_t* plan,
                                            uint32_t limit, const char* id) {
    for (uint32_t index = 0; index < limit; index++) {
        if (kstrcmp(plan->entries[index].id, id) == 0) return 1;
    }
    return 0;
}

static int app_package_build_source_path(const char* source_directory,
                                         const char* alias,
                                         char path[APP_PACKAGE_SOURCE_PATH_SIZE]) {
    uint32_t directory_length;
    uint32_t alias_length;

    if (!source_directory || !alias || !path) {
        LOG_ERROR("PKG", "Fonte de plano recebeu argumento nulo");
        return ERR_NULL;
    }
    directory_length = kstrlen(source_directory);
    alias_length = kstrlen(alias);
    if (!directory_length) {
        app_package_copy_string(path, APP_PACKAGE_SOURCE_PATH_SIZE, alias);
        return OK;
    }
    if (directory_length > 8U || alias_length > 12U ||
        directory_length + alias_length + 2U >
            APP_PACKAGE_SOURCE_PATH_SIZE) {
        LOG_ERROR("PKG", "Caminho da fonte de plano excede limite");
        return ERR_OVERFLOW;
    }
    for (uint32_t index = 0; index < directory_length; index++) {
        char value = source_directory[index];

        if (!((value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '_')) {
            LOG_ERROR("PKG", "Diretorio da fonte de plano e inseguro");
            return ERR_INVALID;
        }
    }
    kmemcpy(path, source_directory, directory_length);
    path[directory_length] = '/';
    app_package_copy_string(path + directory_length + 1U,
                            APP_PACKAGE_SOURCE_PATH_SIZE - directory_length -
                                1U,
                            alias);
    return OK;
}

static int app_package_validate_plan_entry(const app_package_plan_t* plan,
                                           uint32_t index,
                                           const char* source_directory,
                                           app_package_action_result_t* result,
                                           uint32_t* required_bytes) {
    app_package_install_context_t context;
    const app_package_plan_entry_t* item = &plan->entries[index];
    char source_path[APP_PACKAGE_SOURCE_PATH_SIZE];
    char expected_id[APP_PACKAGE_ID_SIZE];
    app_package_info_t installed;
    int installed_result;
    int version_comparison = 0;
    int status;

    kmemset(&context, 0, sizeof(context));
    if (app_package_alias_is_valid(item->alias, expected_id) != OK ||
        kstrcmp(expected_id, item->id) != 0) {
        LOG_WARN("PKG", "Alias do plano nao corresponde ao ID");
        status = app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_ALIAS_MISMATCH, ERR_INVALID);
        goto done;
    }
    status = app_package_build_source_path(source_directory, item->alias,
                                           source_path);
    if (status != OK) {
        status = app_package_action_fail(
            result, APP_PACKAGE_ACTION_REASON_INVALID_ARGUMENT, status);
        goto done;
    }
    status = app_package_read_install_source(source_path, 0, &context, result);
    if (status != OK) goto done;
    if (kstrcmp(context.info.id, item->id) != 0 ||
        kstrcmp(context.info.version, item->to_version) != 0) {
        status = app_package_action_fail(result,
                                         APP_PACKAGE_ACTION_REASON_PLAN_CONFLICT,
                                         ERR_STATE);
        goto done;
    }
    installed_result = app_package_get_installed_info_by_id(item->id, &installed);
    if (item->action == APP_PACKAGE_PLAN_ACTION_INSTALL) {
        if (installed_result == OK) {
            status = app_package_action_fail(result,
                                             APP_PACKAGE_ACTION_REASON_ALREADY_INSTALLED,
                                             ERR_STATE);
            goto done;
        }
        if (installed_result != ERR_NOT_FOUND) {
            status = app_package_action_fail(result,
                                             APP_PACKAGE_ACTION_REASON_READ_ERROR,
                                             installed_result);
            goto done;
        }
    } else if (item->action == APP_PACKAGE_PLAN_ACTION_UPDATE) {
        if (installed_result != OK || kstrcmp(installed.version,
                                               item->from_version) != 0) {
            status = app_package_action_fail(result,
                                             APP_PACKAGE_ACTION_REASON_PLAN_CONFLICT,
                                             ERR_STATE);
            goto done;
        }
        status = app_package_compare_versions(context.info.version,
                                              installed.version,
                                              &version_comparison);
        if (status != OK || version_comparison == 0 ||
            (version_comparison < 0 && !plan->allow_downgrade)) {
            status = app_package_action_fail(
                result, version_comparison < 0 ?
                APP_PACKAGE_ACTION_REASON_DOWNGRADE_REQUIRES_CONFIRM :
                APP_PACKAGE_ACTION_REASON_UPDATE_NOT_AVAILABLE, ERR_STATE);
            goto done;
        }
    } else {
        status = app_package_action_fail(result,
                                         APP_PACKAGE_ACTION_REASON_PLAN_CONFLICT,
                                         ERR_INVALID);
        goto done;
    }
    for (uint32_t dependency = 0; dependency < context.info.dependency_count;
         dependency++) {
        if (app_package_is_installed(context.info.dependencies[dependency]) != OK &&
            !app_package_plan_contains_before(plan, index,
                                              context.info.dependencies[dependency])) {
            app_package_add_blocker(result, context.info.dependencies[dependency]);
            status = app_package_action_fail(result,
                                             APP_PACKAGE_ACTION_REASON_DEPENDENCY_MISSING,
                                             ERR_STATE);
            goto done;
        }
    }
    if (required_bytes) {
        uint32_t manifest_size = context.size - APP_PACKAGE_HEADER_SIZE -
                                 context.payload_size;
        *required_bytes += 2U * (context.payload_size + manifest_size);
        if (item->action == APP_PACKAGE_PLAN_ACTION_UPDATE) {
            uint32_t app_size = 0;
            uint32_t meta_size = 0;
            status = app_package_read_installed_files(item->id, &app_size,
                                                       &meta_size);
            if (status != OK) {
                status = app_package_action_fail(result,
                                                 APP_PACKAGE_ACTION_REASON_READ_ERROR,
                                                 status);
                goto done;
            }
            *required_bytes += app_size + meta_size;
        }
    }
    result->info = context.info;
    status = OK;
done:
    app_package_release_install(&context);
    return status;
}

static int app_package_preflight_plan_internal(
    const app_package_plan_t* plan, const char* source_directory,
    int mutation_owned,
    app_package_action_result_t* result_out) {
    fs_info_t info;
    uint32_t required_bytes = 0U;
    uint32_t cluster_size;
    int result;

    if (!plan || !source_directory || !result_out || plan->entry_count == 0U ||
        plan->entry_count > APP_PACKAGE_MAX_PLAN_ENTRIES ||
        plan->target_index >= plan->entry_count) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_INVALID_ARGUMENT,
                                       ERR_INVALID);
    }
    result_out->plan = *plan;
    if (!app_package_transaction_supported) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_TRANSACTION_UNAVAILABLE,
                                       ERR_UNAVAILABLE);
    }
    if (app_package_transaction_failed) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_RECOVERY_FAILED,
                                       ERR_STATE);
    }
    if (app_package_transaction_pending) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_TRANSACTION_PENDING,
                                       ERR_STATE);
    }
    result = app_package_check_action_ready(result_out);
    if (result != OK) return result;
    if (!mutation_owned && app_package_is_mutation_active()) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_MUTATION_BUSY,
                                       ERR_STATE);
    }
    if (app_loader_is_foreground_active()) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_LOADER_BUSY,
                                       ERR_STATE);
    }
    for (uint32_t index = 0; index < plan->entry_count; index++) {
        result = app_package_validate_plan_entry(plan, index, source_directory,
                                                 result_out,
                                                 &required_bytes);
        if (result != OK) return result;
    }
    if (fs_get_info(&info) != OK || info.bytes_per_sector == 0U ||
        info.sectors_per_cluster == 0U) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_FILESYSTEM_UNAVAILABLE,
                                       ERR_DISK);
    }
    cluster_size = info.bytes_per_sector * info.sectors_per_cluster;
    result_out->required_clusters =
        (required_bytes + cluster_size - 1U) / cluster_size +
        8U + 2U * plan->entry_count;
    result_out->free_clusters = info.free_clusters;
    if (result_out->required_clusters > result_out->free_clusters) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_INSUFFICIENT_SPACE,
                                       ERR_DISK);
    }
    return OK;
}

int app_package_preflight_plan(const app_package_plan_t* plan,
                               app_package_action_result_t* result_out) {
    if (!result_out) {
        LOG_ERROR("PKG", "Saida nula no preflight de plano");
        return ERR_NULL;
    }
    if (plan == &result_out->plan) {
        result_out->reason = APP_PACKAGE_ACTION_REASON_NONE;
        kmemset(&result_out->info, 0, sizeof(result_out->info));
        kmemset(result_out->blocker_ids, 0, sizeof(result_out->blocker_ids));
        result_out->blocker_count = 0U;
        result_out->blocker_overflow = 0U;
        result_out->required_clusters = 0U;
        result_out->free_clusters = 0U;
    } else {
        app_package_action_reset(result_out);
    }
    return app_package_preflight_plan_internal(plan, "", 0, result_out);
}

int app_package_preflight_plan_from_directory(
    const app_package_plan_t* plan, const char* source_directory,
    app_package_action_result_t* result_out) {
    if (!source_directory || !result_out) {
        LOG_ERROR("PKG", "Diretorio ou saida nula no preflight remoto");
        return ERR_NULL;
    }
    app_package_action_reset(result_out);
    return app_package_preflight_plan_internal(
        plan, source_directory, 0, result_out);
}

static void app_package_remove_root_if_present(const char* path) {
    int result = fs_atomic_delete_root(path);

    if (result != OK && result != ERR_NOT_FOUND) {
        LOG_WARN("PKG", "Falha ao limpar arquivo transacional");
    }
}

static void app_package_cleanup_stage_files(uint8_t slot, uint32_t count) {
    char app_path[13];
    char meta_path[13];

    for (uint32_t index = 0; index < count; index++) {
        app_package_stage_path(slot, index, "ZAP", app_path);
        app_package_stage_path(slot, index, "MET", meta_path);
        app_package_remove_root_if_present(app_path);
        app_package_remove_root_if_present(meta_path);
    }
}

static void app_package_cleanup_backup_files(uint8_t slot) {
    char app_path[13];
    char meta_path[13];

    app_package_backup_path(slot, "ZAP", app_path);
    app_package_backup_path(slot, "MET", meta_path);
    app_package_remove_root_if_present(app_path);
    app_package_remove_root_if_present(meta_path);
}

static void app_package_discard_rollback(const char* id) {
    int index = app_package_state_find_rollback(id);
    uint8_t slot;

    if (index < 0) return;
    slot = app_package_transaction_state.rollbacks[index].slot;
    app_package_state_remove_rollback(id);
    if (app_package_write_state() != OK) {
        LOG_WARN("PKG", "Rollback removido nao pode ser persistido");
        return;
    }
    app_package_cleanup_backup_files(slot);
}

static int app_package_stage_entry(const app_package_plan_entry_t* item,
                                   const char* source_directory,
                                   uint8_t slot, uint32_t index,
                                   app_package_action_result_t* result_out) {
    app_package_install_context_t context;
    char source_path[APP_PACKAGE_SOURCE_PATH_SIZE];
    char app_path[13];
    char meta_path[13];
    uint32_t manifest_size;
    int result;

    kmemset(&context, 0, sizeof(context));
    result = app_package_build_source_path(source_directory, item->alias,
                                           source_path);
    if (result != OK) {
        return app_package_action_fail(
            result_out, APP_PACKAGE_ACTION_REASON_INVALID_ARGUMENT, result);
    }
    result = app_package_read_install_source(source_path, 0, &context,
                                             result_out);
    if (result != OK) goto done;
    if (kstrcmp(context.info.id, item->id) != 0 ||
        kstrcmp(context.info.version, item->to_version) != 0) {
        result = app_package_action_fail(result_out,
                                         APP_PACKAGE_ACTION_REASON_PLAN_CONFLICT,
                                         ERR_STATE);
        goto done;
    }
    manifest_size = context.size - APP_PACKAGE_HEADER_SIZE -
                    context.payload_size;
    app_package_stage_path(slot, index, "ZAP", app_path);
    app_package_stage_path(slot, index, "MET", meta_path);
    result = fs_atomic_write_root(app_path, context.payload, context.payload_size,
                                  APP_PACKAGE_CONTROL_ATTRIBUTES,
                                  FS_ATOMIC_CREATE_OR_REPLACE);
    if (result == OK) {
        result = fs_atomic_write_root(meta_path,
                                      context.data + APP_PACKAGE_HEADER_SIZE,
                                      manifest_size,
                                      APP_PACKAGE_CONTROL_ATTRIBUTES,
                                      FS_ATOMIC_CREATE_OR_REPLACE);
    }
    if (result != OK) {
        result = app_package_action_fail(result_out,
                                         APP_PACKAGE_ACTION_REASON_WRITE_ERROR,
                                         result);
    }
done:
    app_package_release_install(&context);
    return result;
}

static int app_package_stage_plan(const app_package_plan_t* plan,
                                  const char* source_directory,
                                  uint8_t slot,
                                  app_package_action_result_t* result_out) {
    for (uint32_t index = 0; index < plan->entry_count; index++) {
        int result = app_package_stage_entry(&plan->entries[index],
                                             source_directory, slot, index,
                                             result_out);
        if (result != OK) {
            app_package_cleanup_stage_files(slot, plan->entry_count);
            return result;
        }
    }
    return OK;
}

static int app_package_write_backup(const char* id, uint8_t slot,
                                    app_package_action_result_t* result_out) {
    char app_path[13];
    char meta_path[13];
    uint32_t app_size = 0;
    uint32_t meta_size = 0;
    int result = app_package_read_installed_files(id, &app_size, &meta_size);

    if (result != OK) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_READ_ERROR,
                                       result);
    }
    app_package_backup_path(slot, "ZAP", app_path);
    app_package_backup_path(slot, "MET", meta_path);
    result = fs_atomic_write_root(app_path, app_package_transaction_app_buffer,
                                  app_size, APP_PACKAGE_CONTROL_ATTRIBUTES,
                                  FS_ATOMIC_CREATE_OR_REPLACE);
    if (result == OK) {
        result = fs_atomic_write_root(meta_path,
                                      app_package_transaction_meta_buffer,
                                      meta_size, APP_PACKAGE_CONTROL_ATTRIBUTES,
                                      FS_ATOMIC_CREATE_OR_REPLACE);
    }
    if (result != OK) {
        app_package_cleanup_backup_files(slot);
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_WRITE_ERROR,
                                       result);
    }
    return OK;
}

static int app_package_read_root_file(const char* path, uint8_t* buffer,
                                      uint32_t capacity, uint32_t* size_out) {
    int size;

    if (!path || !buffer || !size_out) return ERR_NULL;
    size = fs_read_file(path, buffer, capacity);
    if (size <= 0) return ERR_NOT_FOUND;
    *size_out = (uint32_t)size;
    return OK;
}

static int app_package_apply_file(const char* directory, const char* filename,
                                  const char* stage_path, int replace_only,
                                  app_package_action_result_t* result_out) {
    uint8_t* buffer = kstrcmp(filename, APP_PACKAGE_ENTRY_NAME) == 0 ?
                      app_package_transaction_app_buffer :
                      app_package_transaction_meta_buffer;
    uint32_t capacity = kstrcmp(filename, APP_PACKAGE_ENTRY_NAME) == 0 ?
                        sizeof(app_package_transaction_app_buffer) :
                        sizeof(app_package_transaction_meta_buffer);
    uint32_t size = 0;
    int result = app_package_read_root_file(stage_path, buffer, capacity, &size);

    if (result == OK) {
        result = fs_atomic_write_file_in_dir(
            directory, filename, buffer, size, FS_ATTRIBUTE_ARCHIVE,
            replace_only ? FS_ATOMIC_REPLACE_ONLY :
                           FS_ATOMIC_CREATE_OR_REPLACE);
    }
    if (result != OK) {
        return app_package_action_fail(result_out,
                                       result == ERR_NOT_FOUND ?
                                       APP_PACKAGE_ACTION_REASON_READ_ERROR :
                                       APP_PACKAGE_ACTION_REASON_WRITE_ERROR,
                                       result);
    }
    return OK;
}

static int app_package_record_progress(app_package_action_result_t* result_out) {
    app_package_journal.phase = APP_PACKAGE_JOURNAL_REPLACING;
    app_package_journal.completed_files++;
    if (app_package_write_journal() != OK) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_WRITE_ERROR,
                                       ERR_DISK);
    }
    if (app_package_fail_after &&
        app_package_journal.completed_files == app_package_fail_after) {
        app_package_fail_after = 0;
        LOG_WARN("PKG", "Failpoint AS4 interrompeu transacao pendente");
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_WRITE_ERROR,
                                       ERR_DISK);
    }
    return OK;
}

static int app_package_apply_plan_entry(const app_package_plan_t* plan,
                                        uint32_t index,
                                        app_package_action_result_t* result_out) {
    const app_package_plan_entry_t* item = &plan->entries[index];
    char directory[FS_MAX_PATH];
    char app_stage[13];
    char meta_stage[13];
    int replace_only = item->action == APP_PACKAGE_PLAN_ACTION_UPDATE;
    int result;

    app_package_build_directory(directory, item->id);
    if (!replace_only) {
        result = app_package_ensure_directory("", APP_PACKAGE_DIRECTORY);
        if (result == OK) {
            result = fs_create_dir_entry(APP_PACKAGE_DIRECTORY, item->id,
                                         APP_PACKAGE_DIRECTORY_ATTRIBUTE);
        }
        if (result != OK) {
            return app_package_action_fail(result_out,
                                           APP_PACKAGE_ACTION_REASON_WRITE_ERROR,
                                           result);
        }
    }
    app_package_stage_path(app_package_journal.slot, index, "ZAP", app_stage);
    app_package_stage_path(app_package_journal.slot, index, "MET", meta_stage);
    result = app_package_apply_file(directory, APP_PACKAGE_ENTRY_NAME, app_stage,
                                    replace_only, result_out);
    if (result != OK) return result;
    result = app_package_record_progress(result_out);
    if (result != OK) return result;
    result = app_package_apply_file(directory, APP_PACKAGE_METADATA_NAME,
                                    meta_stage, replace_only, result_out);
    if (result != OK) return result;
    return app_package_record_progress(result_out);
}

static int app_package_apply_staged_plan(const app_package_plan_t* plan,
                                         app_package_action_result_t* result_out) {
    for (uint32_t index = 0; index < plan->entry_count; index++) {
        int result = app_package_apply_plan_entry(plan, index, result_out);
        if (result != OK) return result;
    }
    return OK;
}

static void app_package_history_append(app_package_history_operation_t operation,
                                       app_package_history_outcome_t outcome,
                                       app_package_action_reason_t reason,
                                       const char* id, const char* from_version,
                                       const char* to_version, uint32_t count) {
    app_package_history_entry_t* entry;

    if (!app_package_history_available) return;
    entry = &app_package_history.entries[app_package_history.next_index];
    kmemset(entry, 0, sizeof(*entry));
    entry->sequence = app_package_history.sequence + 1U;
    entry->operation = operation;
    entry->outcome = outcome;
    entry->reason = reason;
    entry->plan_entry_count = count;
    app_package_copy_string(entry->id, sizeof(entry->id), id);
    app_package_copy_string(entry->from_version, sizeof(entry->from_version),
                            from_version);
    app_package_copy_string(entry->to_version, sizeof(entry->to_version),
                            to_version);
    app_package_history.next_index =
        (app_package_history.next_index + 1U) % APP_PACKAGE_HISTORY_MAX_ENTRIES;
    if (app_package_history.count < APP_PACKAGE_HISTORY_MAX_ENTRIES) {
        app_package_history.count++;
    }
    if (app_package_write_history() != OK) {
        app_package_history_available = 0;
        LOG_WARN("PKG", "Historico AS4 nao pode ser persistido");
    }
}

static int app_package_restore_backup(const app_package_journal_t* journal) {
    char directory[FS_MAX_PATH];
    char app_path[13];
    char meta_path[13];
    uint32_t app_size = 0;
    uint32_t meta_size = 0;
    int result;

    if (!journal->backup_id[0]) return OK;
    app_package_backup_path(journal->backup_slot, "ZAP", app_path);
    app_package_backup_path(journal->backup_slot, "MET", meta_path);
    result = app_package_read_root_file(app_path, app_package_transaction_app_buffer,
                                        sizeof(app_package_transaction_app_buffer),
                                        &app_size);
    if (result != OK) return result;
    result = app_package_read_root_file(meta_path,
                                        app_package_transaction_meta_buffer,
                                        sizeof(app_package_transaction_meta_buffer),
                                        &meta_size);
    if (result != OK) return result;
    app_package_build_directory(directory, journal->backup_id);
    result = fs_atomic_write_file_in_dir(directory, APP_PACKAGE_ENTRY_NAME,
                                         app_package_transaction_app_buffer,
                                         app_size, FS_ATTRIBUTE_ARCHIVE,
                                         FS_ATOMIC_REPLACE_ONLY);
    if (result != OK) return result;
    return fs_atomic_write_file_in_dir(directory, APP_PACKAGE_METADATA_NAME,
                                       app_package_transaction_meta_buffer,
                                       meta_size, FS_ATTRIBUTE_ARCHIVE,
                                       FS_ATOMIC_REPLACE_ONLY);
}

static int app_package_recover_pending_journal(void) {
    int result = app_package_restore_backup(&app_package_journal);

    if (result != OK) {
        LOG_ERROR("PKG", "Recuperacao AS4 nao encontrou backup valido");
        return result;
    }
    for (uint32_t index = 0; index < app_package_journal.plan.entry_count;
         index++) {
        const app_package_plan_entry_t* item =
            &app_package_journal.plan.entries[index];
        if (item->action == APP_PACKAGE_PLAN_ACTION_INSTALL) {
            app_package_cleanup_partial(item->id);
        }
    }
    app_package_cleanup_stage_files(app_package_journal.slot,
                                    app_package_journal.plan.entry_count);
    if (app_package_journal.backup_id[0]) {
        app_package_cleanup_backup_files(app_package_journal.backup_slot);
    }
    app_package_remove_root_if_present(app_package_journal_paths[0]);
    app_package_remove_root_if_present(app_package_journal_paths[1]);
    app_package_transaction_pending = 0;
    app_package_history_append(APP_PACKAGE_HISTORY_OPERATION_RECOVERY,
                               APP_PACKAGE_HISTORY_OUTCOME_RECOVERED,
                               APP_PACKAGE_ACTION_REASON_NONE,
                               app_package_journal.backup_id,
                               app_package_journal.backup_version, "",
                               app_package_journal.plan.entry_count);
    kmemset(&app_package_journal, 0, sizeof(app_package_journal));
    LOG_INFO("PKG", "Recuperacao AS4 concluiu no estado anterior");
    return OK;
}

static int app_package_finalize_committed_journal(void) {
    const app_package_plan_entry_t* target;
    int result = OK;

    if (app_package_journal.plan.entry_count == 0U ||
        app_package_journal.plan.target_index >=
            app_package_journal.plan.entry_count) {
        LOG_ERROR("PKG", "Journal AS4 concluido sem plano valido");
        return ERR_INVALID;
    }
    target = &app_package_journal.plan.entries[
        app_package_journal.plan.target_index];
    if (app_package_journal.operation == APP_PACKAGE_HISTORY_OPERATION_UPDATE) {
        result = app_package_state_store_rollback(
            target->id, app_package_journal.backup_version,
            app_package_journal.backup_slot);
        if (result == OK) result = app_package_write_state();
        if (result != OK) {
            LOG_ERROR("PKG", "Nao foi possivel persistir rollback AS4");
            return result;
        }
        if (app_package_journal.previous_backup_slot <
            APP_PACKAGE_ROLLBACK_SLOTS &&
            app_package_journal.previous_backup_slot !=
                app_package_journal.backup_slot) {
            app_package_cleanup_backup_files(
                app_package_journal.previous_backup_slot);
        }
    } else if (app_package_journal.operation ==
               APP_PACKAGE_HISTORY_OPERATION_ROLLBACK) {
        app_package_state_remove_rollback(target->id);
        result = app_package_write_state();
        if (result != OK) {
            LOG_ERROR("PKG", "Nao foi possivel consumir rollback AS4");
            return result;
        }
        if (app_package_journal.previous_backup_slot <
            APP_PACKAGE_ROLLBACK_SLOTS) {
            app_package_cleanup_backup_files(
                app_package_journal.previous_backup_slot);
        }
        app_package_cleanup_backup_files(app_package_journal.backup_slot);
    }
    app_package_cleanup_stage_files(app_package_journal.slot,
                                    app_package_journal.plan.entry_count);
    app_package_remove_root_if_present(app_package_journal_paths[0]);
    app_package_remove_root_if_present(app_package_journal_paths[1]);
    app_package_transaction_pending = 0;
    kmemset(&app_package_journal, 0, sizeof(app_package_journal));
    return OK;
}

static int app_package_transaction_load_records(void) {
    app_package_transaction_state_t state;
    app_package_journal_t journal;
    app_package_history_store_t history;

    for (uint32_t index = 0; index < APP_PACKAGE_TRANSACTION_SLOTS; index++) {
        if (app_package_read_control(app_package_state_paths[index], &state,
                                     sizeof(state)) == OK &&
            app_package_state_is_valid(&state) &&
            (app_package_state_slot < 0 ||
             state.sequence > app_package_transaction_state.sequence)) {
            app_package_transaction_state = state;
            app_package_state_slot = (int)index;
        }
        if (app_package_read_control(app_package_journal_paths[index], &journal,
                                     sizeof(journal)) == OK &&
            app_package_journal_is_valid(&journal) &&
            (app_package_journal_slot < 0 ||
             journal.sequence > app_package_journal.sequence)) {
            app_package_journal = journal;
            app_package_journal_slot = (int)index;
        }
        if (app_package_read_control(app_package_history_paths[index], &history,
                                     sizeof(history)) == OK &&
            app_package_history_is_valid(&history) &&
            (app_package_history_slot < 0 ||
             history.sequence > app_package_history.sequence)) {
            app_package_history = history;
            app_package_history_slot = (int)index;
        }
    }
    app_package_history_available = 1;
    if (app_package_journal_slot < 0) {
        for (uint32_t index = 0; index < APP_PACKAGE_TRANSACTION_SLOTS;
             index++) {
            uint32_t size = 0;

            if (fs_get_root_file_info(app_package_journal_paths[index], &size,
                                      0) == OK) {
                LOG_ERROR("PKG", "Journal AS4 presente, mas invalido");
                app_package_transaction_failed = 1;
                return ERR_INVALID;
            }
        }
    }
    return OK;
}

static int app_package_transaction_init(void) {
    int result;

    app_package_transaction_supported = fs_get_type() == FS_TYPE_FAT12;
    if (!app_package_transaction_supported) {
        LOG_WARN("PKG", "AS4 requer atomicos FAT12; FAT32 fica somente leitura");
        return ERR_UNAVAILABLE;
    }
    result = app_package_transaction_load_records();
    if (result != OK) return result;
    if (app_package_journal_slot >= 0 &&
        app_package_journal.phase != APP_PACKAGE_JOURNAL_NONE) {
        app_package_transaction_pending = 1;
        if (app_package_journal.phase == APP_PACKAGE_JOURNAL_COMMITTED) {
            if (app_package_finalize_committed_journal() != OK) {
                app_package_transaction_failed = 1;
                LOG_ERROR("PKG", "Finalizacao AS4 apos reboot falhou");
                return ERR_DISK;
            }
        } else if (app_package_recover_pending_journal() != OK) {
            app_package_transaction_failed = 1;
            LOG_ERROR("PKG", "Recuperacao AS4 falhou; mutacoes bloqueadas");
            return ERR_DISK;
        }
    }
    return OK;
}

static int app_package_prepare_plan_journal(
    const app_package_plan_t* plan,
    app_package_action_result_t* result_out) {
    const app_package_plan_entry_t* target =
        &plan->entries[plan->target_index];
    int previous_index = app_package_state_find_rollback(target->id);

    kmemset(&app_package_journal, 0, sizeof(app_package_journal));
    app_package_journal.sequence = app_package_transaction_state.sequence + 1U;
    app_package_journal.phase = APP_PACKAGE_JOURNAL_PREPARED;
    app_package_journal.operation =
        target->action == APP_PACKAGE_PLAN_ACTION_UPDATE ?
        APP_PACKAGE_HISTORY_OPERATION_UPDATE :
        APP_PACKAGE_HISTORY_OPERATION_INSTALL;
    app_package_journal.slot = 0U;
    app_package_journal.previous_backup_slot = APP_PACKAGE_NO_BACKUP_SLOT;
    app_package_journal.plan = *plan;
    if (target->action == APP_PACKAGE_PLAN_ACTION_UPDATE) {
        int backup_slot = app_package_state_find_free_slot();

        if (backup_slot < 0) {
            LOG_WARN("PKG", "Nenhum slot de backup livre para o plano");
            return app_package_action_fail(
                result_out, APP_PACKAGE_ACTION_REASON_TRANSACTION_UNAVAILABLE,
                ERR_OVERFLOW);
        }
        app_package_journal.backup_slot = (uint8_t)backup_slot;
        if (previous_index >= 0) {
            app_package_journal.previous_backup_slot =
                app_package_transaction_state.rollbacks[previous_index].slot;
        }
        app_package_copy_string(app_package_journal.backup_id,
                                sizeof(app_package_journal.backup_id),
                                target->id);
        app_package_copy_string(app_package_journal.backup_version,
                                sizeof(app_package_journal.backup_version),
                                target->from_version);
    }
    return OK;
}

static int app_package_apply_plan_from_directory_internal(
    const app_package_plan_t* plan, const char* source_directory,
    app_package_action_result_t* result_out) {
    const app_package_plan_t* active_plan = &app_package_active_plan;
    const app_package_plan_entry_t* target;
    int result;

    if (!result_out) {
        LOG_ERROR("PKG", "Saida nula na aplicacao do plano");
        return ERR_NULL;
    }
    if (!plan || !source_directory) {
        app_package_action_reset(result_out);
        return app_package_action_fail(
            result_out, APP_PACKAGE_ACTION_REASON_INVALID_ARGUMENT,
            ERR_INVALID);
    }
    result = app_package_mutation_begin(result_out);
    if (result != OK) return result;
    /* A escrita FAT12 aprofunda a pilha; o plano ativo fica em estado estatico. */
    app_package_active_plan = *plan;
    app_package_copy_string(app_package_active_source_directory,
                            sizeof(app_package_active_source_directory),
                            source_directory);
    app_package_action_reset(result_out);
    result = app_package_preflight_plan_internal(
        active_plan, app_package_active_source_directory, 1, result_out);
    if (result != OK) goto done;
    target = &active_plan->entries[active_plan->target_index];
    result = app_package_prepare_plan_journal(active_plan, result_out);
    if (result != OK) goto done;
    result = app_package_stage_plan(active_plan,
                                    app_package_active_source_directory,
                                    app_package_journal.slot, result_out);
    if (result != OK) goto done;
    if (app_package_journal.backup_id[0]) {
        result = app_package_write_backup(app_package_journal.backup_id,
                                          app_package_journal.backup_slot,
                                          result_out);
        if (result != OK) {
            app_package_cleanup_stage_files(app_package_journal.slot,
                                            active_plan->entry_count);
            goto done;
        }
    }
    result = app_package_write_journal();
    if (result != OK) {
        app_package_cleanup_stage_files(app_package_journal.slot,
                                        active_plan->entry_count);
        if (app_package_journal.backup_id[0]) {
            app_package_cleanup_backup_files(app_package_journal.backup_slot);
        }
        result = app_package_action_fail(result_out,
                                         APP_PACKAGE_ACTION_REASON_WRITE_ERROR,
                                         result);
        goto done;
    }
    app_package_transaction_pending = 1;
    result = app_package_apply_staged_plan(active_plan, result_out);
    if (result != OK) goto done;
    app_package_journal.phase = APP_PACKAGE_JOURNAL_COMMITTED;
    result = app_package_write_journal();
    if (result != OK) {
        result = app_package_action_fail(result_out,
                                         APP_PACKAGE_ACTION_REASON_WRITE_ERROR,
                                         result);
        goto done;
    }
    app_package_history_append(target->action == APP_PACKAGE_PLAN_ACTION_UPDATE ?
                               APP_PACKAGE_HISTORY_OPERATION_UPDATE :
                               APP_PACKAGE_HISTORY_OPERATION_INSTALL,
                               APP_PACKAGE_HISTORY_OUTCOME_SUCCESS,
                               APP_PACKAGE_ACTION_REASON_NONE, target->id,
                               target->from_version, target->to_version,
                               active_plan->entry_count);
    result = app_package_finalize_committed_journal();
    if (result != OK) {
        result = app_package_action_fail(result_out,
                                         APP_PACKAGE_ACTION_REASON_WRITE_ERROR,
                                         result);
        goto done;
    }
done:
    app_package_mutation_end();
    return result;
}

int app_package_apply_plan_confirmed(const app_package_plan_t* plan,
                                     app_package_action_result_t* result_out) {
    return app_package_apply_plan_from_directory_internal(plan, "", result_out);
}

int app_package_apply_plan_from_directory_confirmed(
    const app_package_plan_t* plan, const char* source_directory,
    app_package_action_result_t* result_out) {
    if (!source_directory) {
        LOG_ERROR("PKG", "Diretorio nulo na aplicacao de plano remoto");
        return ERR_NULL;
    }
    return app_package_apply_plan_from_directory_internal(
        plan, source_directory, result_out);
}

static int app_package_read_backup_files(uint8_t slot, app_package_info_t* info,
                                         uint32_t* app_size_out,
                                         uint32_t* meta_size_out) {
    char app_path[13];
    char meta_path[13];
    app_image_header_t image;
    uint32_t app_size;
    uint32_t meta_size;
    int result;

    if (!info || !app_size_out || !meta_size_out) return ERR_NULL;
    app_package_backup_path(slot, "ZAP", app_path);
    app_package_backup_path(slot, "MET", meta_path);
    result = app_package_read_root_file(app_path,
                                        app_package_transaction_app_buffer,
                                        sizeof(app_package_transaction_app_buffer),
                                        &app_size);
    if (result != OK) return result;
    result = app_loader_validate_image(app_package_transaction_app_buffer,
                                       app_size, &image);
    if (result != OK) return result;
    result = app_package_read_root_file(meta_path,
                                        app_package_transaction_meta_buffer,
                                        sizeof(app_package_transaction_meta_buffer),
                                        &meta_size);
    if (result != OK) return result;
    result = app_package_parse_manifest(app_package_transaction_meta_buffer,
                                        meta_size, info);
    if (result != OK) return result;
    *app_size_out = app_size;
    *meta_size_out = meta_size;
    return OK;
}

static int app_package_stage_rollback(uint8_t source_slot, uint8_t stage_slot,
                                      app_package_action_result_t* result_out) {
    char source_app[13];
    char source_meta[13];
    char stage_app[13];
    char stage_meta[13];
    uint32_t app_size;
    uint32_t meta_size;
    app_package_info_t info;
    int result;

    result = app_package_read_backup_files(source_slot, &info, &app_size,
                                           &meta_size);
    if (result != OK) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_ROLLBACK_UNAVAILABLE,
                                       result);
    }
    app_package_backup_path(source_slot, "ZAP", source_app);
    app_package_backup_path(source_slot, "MET", source_meta);
    app_package_stage_path(stage_slot, 0U, "ZAP", stage_app);
    app_package_stage_path(stage_slot, 0U, "MET", stage_meta);
    result = fs_atomic_write_root(stage_app,
                                  app_package_transaction_app_buffer, app_size,
                                  APP_PACKAGE_CONTROL_ATTRIBUTES,
                                  FS_ATOMIC_CREATE_OR_REPLACE);
    if (result == OK) {
        result = fs_atomic_write_root(stage_meta,
                                      app_package_transaction_meta_buffer,
                                      meta_size,
                                      APP_PACKAGE_CONTROL_ATTRIBUTES,
                                      FS_ATOMIC_CREATE_OR_REPLACE);
    }
    if (result != OK) {
        app_package_cleanup_stage_files(stage_slot, 1U);
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_WRITE_ERROR,
                                       result);
    }
    (void)source_app;
    (void)source_meta;
    return OK;
}

static int app_package_prepare_rollback_internal(
    const char* id, int mutation_owned, app_package_action_result_t* result_out) {
    app_package_info_t backup;
    app_package_info_t installed;
    fs_info_t fs_info;
    uint32_t app_size;
    uint32_t meta_size;
    uint32_t installed_app_size;
    uint32_t installed_meta_size;
    uint32_t cluster_size;
    int rollback_index;
    int result;

    if (!id || !result_out || app_package_id_is_valid(id) != OK) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_INVALID_ARGUMENT,
                                       ERR_INVALID);
    }
    if (!app_package_transaction_supported) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_TRANSACTION_UNAVAILABLE,
                                       ERR_UNAVAILABLE);
    }
    if (app_package_transaction_failed) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_RECOVERY_FAILED,
                                       ERR_STATE);
    }
    if (app_package_transaction_pending) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_TRANSACTION_PENDING,
                                       ERR_STATE);
    }
    result = app_package_check_action_ready(result_out);
    if (result != OK) return result;
    if (!mutation_owned && app_package_is_mutation_active()) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_MUTATION_BUSY,
                                       ERR_STATE);
    }
    if (app_loader_is_foreground_active()) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_LOADER_BUSY,
                                       ERR_STATE);
    }
    rollback_index = app_package_state_find_rollback(id);
    if (rollback_index < 0) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_ROLLBACK_UNAVAILABLE,
                                       ERR_NOT_FOUND);
    }
    result = app_package_get_installed_info_by_id(id, &installed);
    if (result != OK) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_NOT_INSTALLED,
                                       result);
    }
    result = app_package_read_backup_files(
        app_package_transaction_state.rollbacks[rollback_index].slot,
        &backup, &app_size,
        &meta_size);
    if (result != OK || kstrcmp(backup.id, id) != 0 ||
        kstrcmp(backup.version,
                app_package_transaction_state.rollbacks[rollback_index].version) != 0) {
        LOG_ERROR("PKG", "Backup AS4 para rollback esta invalido");
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_ROLLBACK_UNAVAILABLE,
                                       ERR_INVALID);
    }
    result = app_package_read_installed_files(id, &installed_app_size,
                                              &installed_meta_size);
    if (result != OK || fs_get_info(&fs_info) != OK ||
        fs_info.bytes_per_sector == 0U || fs_info.sectors_per_cluster == 0U) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_READ_ERROR,
                                       result == OK ? ERR_DISK : result);
    }
    cluster_size = fs_info.bytes_per_sector * fs_info.sectors_per_cluster;
    result_out->required_clusters =
        (2U * (app_size + meta_size) + installed_app_size + installed_meta_size +
         cluster_size - 1U) / cluster_size + 10U;
    result_out->free_clusters = fs_info.free_clusters;
    if (result_out->required_clusters > result_out->free_clusters) {
        return app_package_action_fail(result_out,
                                       APP_PACKAGE_ACTION_REASON_INSUFFICIENT_SPACE,
                                       ERR_DISK);
    }
    result_out->info = backup;
    result_out->plan.entry_count = 1U;
    result_out->plan.target_index = 0U;
    result_out->plan.entries[0].action = APP_PACKAGE_PLAN_ACTION_UPDATE;
    app_package_copy_string(result_out->plan.entries[0].id,
                            sizeof(result_out->plan.entries[0].id), id);
    app_package_copy_string(result_out->plan.entries[0].from_version,
                            sizeof(result_out->plan.entries[0].from_version),
                            installed.version);
    app_package_copy_string(result_out->plan.entries[0].to_version,
                            sizeof(result_out->plan.entries[0].to_version),
                            backup.version);
    return OK;
}

int app_package_preflight_rollback(const char* id,
                                   app_package_action_result_t* result_out) {
    if (!result_out) {
        LOG_ERROR("PKG", "Saida nula no preflight de rollback");
        return ERR_NULL;
    }
    app_package_action_reset(result_out);
    return app_package_prepare_rollback_internal(id, 0, result_out);
}

int app_package_rollback_confirmed(const char* id,
                                   app_package_action_result_t* result_out) {
    const app_package_plan_t* plan = &app_package_active_plan;
    uint8_t source_slot;
    uint8_t stage_slot;
    int source_index;
    int backup_slot;
    int result;

    if (!result_out) {
        LOG_ERROR("PKG", "Saida nula no rollback confirmado");
        return ERR_NULL;
    }
    app_package_action_reset(result_out);
    result = app_package_mutation_begin(result_out);
    if (result != OK) return result;
    result = app_package_prepare_rollback_internal(id, 1, result_out);
    if (result != OK) goto done;
    app_package_active_plan = result_out->plan;
    source_index = app_package_state_find_rollback(id);
    backup_slot = app_package_state_find_free_slot();
    if (source_index < 0 || backup_slot < 0) {
        result = app_package_action_fail(
            result_out, APP_PACKAGE_ACTION_REASON_TRANSACTION_UNAVAILABLE,
            ERR_OVERFLOW);
        goto done;
    }
    source_slot = app_package_transaction_state.rollbacks[source_index].slot;
    stage_slot = 0U;
    kmemset(&app_package_journal, 0, sizeof(app_package_journal));
    app_package_journal.sequence = app_package_transaction_state.sequence + 1U;
    app_package_journal.phase = APP_PACKAGE_JOURNAL_PREPARED;
    app_package_journal.operation = APP_PACKAGE_HISTORY_OPERATION_ROLLBACK;
    app_package_journal.slot = stage_slot;
    app_package_journal.backup_slot = (uint8_t)backup_slot;
    app_package_journal.previous_backup_slot = source_slot;
    app_package_journal.plan = *plan;
    app_package_copy_string(app_package_journal.backup_id,
                            sizeof(app_package_journal.backup_id), id);
    app_package_copy_string(app_package_journal.backup_version,
                            sizeof(app_package_journal.backup_version),
                            plan->entries[0].from_version);
    result = app_package_stage_rollback(source_slot, stage_slot, result_out);
    if (result != OK) goto done;
    result = app_package_write_backup(id, app_package_journal.backup_slot,
                                      result_out);
    if (result != OK) {
        app_package_cleanup_stage_files(stage_slot, 1U);
        goto done;
    }
    result = app_package_write_journal();
    if (result != OK) {
        app_package_cleanup_stage_files(stage_slot, 1U);
        app_package_cleanup_backup_files(app_package_journal.backup_slot);
        result = app_package_action_fail(result_out,
                                         APP_PACKAGE_ACTION_REASON_WRITE_ERROR,
                                         result);
        goto done;
    }
    app_package_transaction_pending = 1;
    result = app_package_apply_staged_plan(plan, result_out);
    if (result != OK) goto done;
    app_package_journal.phase = APP_PACKAGE_JOURNAL_COMMITTED;
    result = app_package_write_journal();
    if (result != OK) {
        result = app_package_action_fail(result_out,
                                         APP_PACKAGE_ACTION_REASON_WRITE_ERROR,
                                         result);
        goto done;
    }
    app_package_history_append(APP_PACKAGE_HISTORY_OPERATION_ROLLBACK,
                               APP_PACKAGE_HISTORY_OUTCOME_SUCCESS,
                               APP_PACKAGE_ACTION_REASON_NONE, id,
                               plan->entries[0].from_version,
                               plan->entries[0].to_version, 1U);
    result = app_package_finalize_committed_journal();
    if (result != OK) {
        result = app_package_action_fail(result_out,
                                         APP_PACKAGE_ACTION_REASON_WRITE_ERROR,
                                         result);
        goto done;
    }
done:
    app_package_mutation_end();
    return result;
}

int app_package_get_status(app_package_status_t* status_out) {
    uint32_t count;

    if (!status_out) {
        LOG_ERROR("PKG", "Saida nula na consulta de status AS4");
        return ERR_NULL;
    }
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->transaction_supported = app_package_transaction_supported;
    status_out->transaction_pending = app_package_transaction_pending ||
                                      app_package_transaction_failed;
    status_out->history_available = app_package_history_available;
    for (uint32_t index = 0; index < APP_PACKAGE_ROLLBACK_SLOTS; index++) {
        const app_package_rollback_record_t* rollback =
            &app_package_transaction_state.rollbacks[index];

        if (!rollback->available ||
            status_out->rollback_count >= APP_PACKAGE_MAX_ROLLBACKS) {
            continue;
        }
        app_package_copy_string(
            status_out->rollbacks[status_out->rollback_count].id,
            sizeof(status_out->rollbacks[status_out->rollback_count].id),
            rollback->id);
        app_package_copy_string(
            status_out->rollbacks[status_out->rollback_count].version,
            sizeof(status_out->rollbacks[status_out->rollback_count].version),
            rollback->version);
        if (!status_out->rollback_count) {
            app_package_copy_string(status_out->rollback_id,
                                    sizeof(status_out->rollback_id),
                                    rollback->id);
            app_package_copy_string(status_out->rollback_version,
                                    sizeof(status_out->rollback_version),
                                    rollback->version);
        }
        status_out->rollback_count++;
    }
    status_out->rollback_available = status_out->rollback_count > 0U;
    count = app_package_history.count;
    if (count > 0U) {
        uint32_t newest = (app_package_history.next_index +
                           APP_PACKAGE_HISTORY_MAX_ENTRIES - 1U) %
                          APP_PACKAGE_HISTORY_MAX_ENTRIES;
        status_out->last_history = app_package_history.entries[newest];
    }
    return OK;
}

int app_package_get_history_count(uint32_t* count_out) {
    if (!count_out) {
        LOG_ERROR("PKG", "Saida nula na contagem de historico AS4");
        return ERR_NULL;
    }
    if (!app_package_history_available) return ERR_UNAVAILABLE;
    *count_out = app_package_history.count;
    return OK;
}

int app_package_get_history_entry(uint32_t newest_index,
                                  app_package_history_entry_t* entry_out) {
    uint32_t slot;

    if (!entry_out) {
        LOG_ERROR("PKG", "Saida nula na leitura de historico AS4");
        return ERR_NULL;
    }
    if (!app_package_history_available) return ERR_UNAVAILABLE;
    if (newest_index >= app_package_history.count) return ERR_NOT_FOUND;
    slot = (app_package_history.next_index + APP_PACKAGE_HISTORY_MAX_ENTRIES -
            1U - newest_index) % APP_PACKAGE_HISTORY_MAX_ENTRIES;
    *entry_out = app_package_history.entries[slot];
    return OK;
}

int app_package_test_fail_after(uint16_t completed_files) {
    if (completed_files == 0U || completed_files > 32U) {
        LOG_ERROR("PKG", "Failpoint AS4 fora do intervalo");
        return ERR_INVALID;
    }
    app_package_fail_after = completed_files;
    LOG_INFO("PKG", "Failpoint AS4 configurado");
    return OK;
}

int app_package_run_diagnostics(app_package_diagnostic_t* diagnostic_out) {
    app_package_info_t info;
    app_package_header_t invalid_header;
    fs_info_t fs_info;
    uint32_t required_clusters = 0;
    int installed_count;
    int first_lock;
    int second_lock;

    if (!app_package_ready || !diagnostic_out) {
        LOG_ERROR("PKG", "Diagnostico de pacote indisponivel");
        return ERR_UNAVAILABLE;
    }
    kmemset(diagnostic_out, 0, sizeof(app_package_diagnostic_t));
    kmemset(&invalid_header, 0, sizeof(invalid_header));
    diagnostic_out->invalid_package = !app_package_header_is_valid(&invalid_header);
    kmemset(&info, 0, sizeof(info));
    kmemcpy(info.id, "MISSING", 7);
    diagnostic_out->missing_dependency = 1;
    installed_count = app_package_get_installed_count();
    for (int index = 0; index < installed_count; index++) {
        app_package_info_t installed;
        if (app_package_get_installed_info(index, &installed) == OK &&
            kstrcmp(installed.id, info.id) == 0) {
            diagnostic_out->missing_dependency = 0;
            break;
        }
    }
    if (fs_get_info(&fs_info) == OK) {
        fs_info.free_clusters = 0;
        diagnostic_out->insufficient_space =
            app_package_evaluate_space(
                1U, 1U, 2U, &fs_info, &required_clusters) == ERR_DISK &&
            required_clusters > fs_info.free_clusters;
    }
    app_package_action_reset(&app_package_legacy_result);
    first_lock = app_package_mutation_begin(&app_package_legacy_result);
    second_lock = first_lock == OK ?
        app_package_mutation_begin(&app_package_legacy_result) : first_lock;
    diagnostic_out->mutation_serialization =
        first_lock == OK && second_lock == ERR_STATE &&
        app_package_legacy_result.reason ==
            APP_PACKAGE_ACTION_REASON_MUTATION_BUSY;
    if (first_lock == OK) app_package_mutation_end();
    diagnostic_out->transaction_supported = app_package_transaction_supported;
    diagnostic_out->transaction_pending = app_package_transaction_pending ||
                                          app_package_transaction_failed;
    diagnostic_out->rollback_available =
        app_package_transaction_state.rollback_count > 0U;
    diagnostic_out->history_available = app_package_history_available;
    return OK;
}

const char* app_package_action_reason_name(
    app_package_action_reason_t reason) {
    switch (reason) {
        case APP_PACKAGE_ACTION_REASON_NONE: return "NONE";
        case APP_PACKAGE_ACTION_REASON_INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case APP_PACKAGE_ACTION_REASON_SOURCE_NOT_FOUND:
            return "SOURCE_NOT_FOUND";
        case APP_PACKAGE_ACTION_REASON_PACKAGE_INVALID:
            return "PACKAGE_INVALID";
        case APP_PACKAGE_ACTION_REASON_ALIAS_MISMATCH:
            return "ALIAS_MISMATCH";
        case APP_PACKAGE_ACTION_REASON_DEPENDENCY_MISSING:
            return "DEPENDENCY_MISSING";
        case APP_PACKAGE_ACTION_REASON_INSUFFICIENT_SPACE:
            return "INSUFFICIENT_SPACE";
        case APP_PACKAGE_ACTION_REASON_ALREADY_INSTALLED:
            return "ALREADY_INSTALLED";
        case APP_PACKAGE_ACTION_REASON_NOT_INSTALLED:
            return "NOT_INSTALLED";
        case APP_PACKAGE_ACTION_REASON_DEPENDENT_INSTALLED:
            return "DEPENDENT_INSTALLED";
        case APP_PACKAGE_ACTION_REASON_FILESYSTEM_UNAVAILABLE:
            return "FILESYSTEM_UNAVAILABLE";
        case APP_PACKAGE_ACTION_REASON_LOADER_UNAVAILABLE:
            return "LOADER_UNAVAILABLE";
        case APP_PACKAGE_ACTION_REASON_PACKAGE_SERVICE_UNAVAILABLE:
            return "PACKAGE_SERVICE_UNAVAILABLE";
        case APP_PACKAGE_ACTION_REASON_LOADER_BUSY: return "LOADER_BUSY";
        case APP_PACKAGE_ACTION_REASON_MUTATION_BUSY: return "MUTATION_BUSY";
        case APP_PACKAGE_ACTION_REASON_READ_ERROR: return "READ_ERROR";
        case APP_PACKAGE_ACTION_REASON_WRITE_ERROR: return "WRITE_ERROR";
        case APP_PACKAGE_ACTION_REASON_UPDATE_NOT_AVAILABLE:
            return "UPDATE_NOT_AVAILABLE";
        case APP_PACKAGE_ACTION_REASON_DOWNGRADE_REQUIRES_CONFIRM:
            return "DOWNGRADE_REQUIRES_CONFIRM";
        case APP_PACKAGE_ACTION_REASON_PLAN_INCOMPLETE:
            return "PLAN_INCOMPLETE";
        case APP_PACKAGE_ACTION_REASON_PLAN_CYCLE: return "PLAN_CYCLE";
        case APP_PACKAGE_ACTION_REASON_PLAN_CONFLICT: return "PLAN_CONFLICT";
        case APP_PACKAGE_ACTION_REASON_TRANSACTION_UNAVAILABLE:
            return "TRANSACTION_UNAVAILABLE";
        case APP_PACKAGE_ACTION_REASON_TRANSACTION_PENDING:
            return "TRANSACTION_PENDING";
        case APP_PACKAGE_ACTION_REASON_ROLLBACK_UNAVAILABLE:
            return "ROLLBACK_UNAVAILABLE";
        case APP_PACKAGE_ACTION_REASON_RECOVERY_FAILED:
            return "RECOVERY_FAILED";
        case APP_PACKAGE_ACTION_REASON_HISTORY_UNAVAILABLE:
            return "HISTORY_UNAVAILABLE";
        default: return "UNKNOWN";
    }
}

const char* app_package_plan_action_name(app_package_plan_action_t action) {
    switch (action) {
        case APP_PACKAGE_PLAN_ACTION_INSTALL: return "INSTALL";
        case APP_PACKAGE_PLAN_ACTION_UPDATE: return "UPDATE";
        default: return "NONE";
    }
}

const char* app_package_history_operation_name(
    app_package_history_operation_t operation) {
    switch (operation) {
        case APP_PACKAGE_HISTORY_OPERATION_INSTALL: return "INSTALL";
        case APP_PACKAGE_HISTORY_OPERATION_REMOVE: return "REMOVE";
        case APP_PACKAGE_HISTORY_OPERATION_UPDATE: return "UPDATE";
        case APP_PACKAGE_HISTORY_OPERATION_ROLLBACK: return "ROLLBACK";
        case APP_PACKAGE_HISTORY_OPERATION_RECOVERY: return "RECOVERY";
        default: return "NONE";
    }
}

const char* app_package_history_outcome_name(
    app_package_history_outcome_t outcome) {
    switch (outcome) {
        case APP_PACKAGE_HISTORY_OUTCOME_SUCCESS: return "SUCCESS";
        case APP_PACKAGE_HISTORY_OUTCOME_FAILED: return "FAILED";
        case APP_PACKAGE_HISTORY_OUTCOME_RECOVERED: return "RECOVERED";
        default: return "NONE";
    }
}
