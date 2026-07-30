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

static int app_package_ready = 0;
static int app_package_mutation_active = 0;
static spinlock_t app_package_mutation_lock;
static app_package_action_result_t app_package_legacy_result;

typedef struct {
    uint8_t* data;
    const uint8_t* payload;
    uint32_t size;
    uint32_t payload_size;
    app_package_info_t info;
} app_package_install_context_t;

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

    if (fs_get_type() == FS_TYPE_NONE || !app_loader_is_ready()) {
        LOG_WARN("PKG", "Dependencias do servico de pacotes indisponiveis");
        LOG_ERROR("PKG", "Servico de pacotes nao foi inicializado");
        return ERR_UNAVAILABLE;
    }
    app_package_ready = 1;
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

int app_package_preflight_install(
    const char* alias, app_package_action_result_t* result_out) {
    app_package_install_context_t context;
    int result;

    if (!result_out) {
        LOG_ERROR("PKG", "Saida nula para preflight de instalacao");
        return ERR_NULL;
    }
    app_package_action_reset(result_out);
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
    result = app_package_mutation_begin(result_out);
    if (result == OK) {
        result = app_package_prepare_install(
            alias, 1, 1, 1, &context, result_out);
        if (result == OK) {
            result = app_package_commit_install(&context, result_out);
        }
        app_package_release_install(&context);
        app_package_mutation_end();
    }
    return result;
}

int app_package_install_file(const char* path, app_package_info_t* info_out) {
    app_package_install_context_t context;
    int result;

    if (!path || !info_out) {
        LOG_ERROR("PKG", "Instalacao recebeu argumento nulo");
        return ERR_NULL;
    }
    app_package_action_reset(&app_package_legacy_result);
    result = app_package_mutation_begin(&app_package_legacy_result);
    if (result == OK) {
        result = app_package_prepare_install(
            path, 0, 0, 1, &context, &app_package_legacy_result);
        if (result == OK) {
            result = app_package_commit_install(
                &context, &app_package_legacy_result);
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
    if (fs_delete_file_in_dir(directory, APP_PACKAGE_ENTRY_NAME) != OK ||
        fs_delete_file_in_dir(directory, APP_PACKAGE_METADATA_NAME) != OK ||
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
        default: return "UNKNOWN";
    }
}
