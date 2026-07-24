#include "core/app_package.h"
#include "core/app_loader.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "fs/fs.h"

#define APP_PACKAGE_HEADER_SIZE ((uint32_t)sizeof(app_package_header_t))
#define APP_PACKAGE_MAX_FILE_SIZE \
    (APP_PACKAGE_HEADER_SIZE + APP_PACKAGE_MAX_MANIFEST_SIZE + \
     APP_IMAGE_MAX_FILE_SIZE)
#define APP_PACKAGE_READ_BUFFER_SIZE (APP_PACKAGE_MAX_FILE_SIZE + 1U)
#define APP_PACKAGE_CLUSTER_OVERHEAD 2U
#define APP_PACKAGE_CRC32_POLYNOMIAL 0xEDB88320U

static int app_package_ready = 0;

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

static int app_package_require_dependencies(const app_package_info_t* info) {
    if (!info) {
        LOG_ERROR("PKG", "Info de pacote nula");
        return ERR_NULL;
    }
    for (uint32_t index = 0; index < info->dependency_count; index++) {
        if (app_package_is_installed(info->dependencies[index]) != OK) {
            LOG_WARN("PKG", "Dependencia de pacote ausente");
            return ERR_NOT_FOUND;
        }
    }
    return OK;
}

static int app_package_has_space(uint32_t payload_size, uint32_t metadata_size,
                                 uint32_t directory_count) {
    fs_info_t fs_info;
    uint32_t cluster_bytes;
    uint32_t required_clusters;

    if (fs_get_info(&fs_info) != OK || fs_info.bytes_per_sector == 0 ||
        fs_info.sectors_per_cluster == 0) {
        LOG_WARN("PKG", "Espaco de pacote indisponivel");
        return ERR_UNAVAILABLE;
    }
    cluster_bytes = fs_info.bytes_per_sector * fs_info.sectors_per_cluster;
    if (cluster_bytes == 0 || payload_size > 0xFFFFFFFFU - metadata_size ||
        payload_size > 0xFFFFFFFFU - (cluster_bytes - 1U) ||
        metadata_size > 0xFFFFFFFFU - (cluster_bytes - 1U)) {
        LOG_ERROR("PKG", "Tamanho de pacote invalido para espaco");
        return ERR_OVERFLOW;
    }
    required_clusters = (payload_size + cluster_bytes - 1U) / cluster_bytes;
    required_clusters += (metadata_size + cluster_bytes - 1U) / cluster_bytes;
    required_clusters += directory_count;
    if (required_clusters + APP_PACKAGE_CLUSTER_OVERHEAD > fs_info.free_clusters) {
        LOG_WARN("PKG", "Espaco insuficiente para instalar pacote");
        return ERR_DISK;
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
        LOG_WARN("PKG", "Metadata de pacote nao encontrada");
        return ERR_NOT_FOUND;
    }
    return app_package_parse_manifest(manifest, (uint32_t)size, info);
}

static int app_package_is_required_by_other(const char* id) {
    int count = app_package_get_installed_count();

    for (int index = 0; index < count; index++) {
        app_package_info_t info;
        if (app_package_get_installed_info(index, &info) != OK ||
            kstrcmp(info.id, id) == 0) continue;
        for (uint32_t dependency = 0; dependency < info.dependency_count;
             dependency++) {
            if (kstrcmp(info.dependencies[dependency], id) == 0) {
                LOG_WARN("PKG", "Pacote possui dependente instalado");
                return ERR_STATE;
            }
        }
    }
    return OK;
}

int app_package_init(void) {
    app_package_ready = 0;
    LOG_INFO("PKG", "Inicializando servico de pacotes");

    if (fs_get_type() == FS_TYPE_NONE || !app_loader_is_ready()) {
        LOG_WARN("PKG", "Dependencias do servico de pacotes indisponiveis");
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

int app_package_install_file(const char* path, app_package_info_t* info_out) {
    uint8_t* data = 0;
    const uint8_t* payload = 0;
    app_package_info_t info;
    char directory[FS_MAX_PATH];
    uint32_t size = 0;
    uint32_t payload_size = 0;
    int package_directory_created = 0;
    int result;

    if (!app_package_ready) {
        LOG_WARN("PKG", "Instalacao solicitada sem servico pronto");
        return ERR_UNAVAILABLE;
    }
    if (!path || !info_out) {
        LOG_ERROR("PKG", "Instalacao recebeu argumento nulo");
        return ERR_NULL;
    }
    kmemset(&info, 0, sizeof(info));
    if (app_loader_is_foreground_active()) {
        LOG_WARN("PKG", "Instalacao bloqueada por aplicativo em primeiro plano");
        return ERR_STATE;
    }
    result = app_package_read_file(path, &data, &size);
    if (result == OK) result = app_package_decode(data, size, &info, &payload,
                                                   &payload_size);
    if (result == OK) {
        result = app_package_is_installed(info.id);
        if (result == OK) {
            LOG_WARN("PKG", "Pacote ja esta instalado");
            result = ERR_STATE;
        } else if (result == ERR_NOT_FOUND) {
            result = OK;
        }
    }
    if (result == OK) result = app_package_require_dependencies(&info);
    if (result == OK) result = app_package_has_space(payload_size,
                                                     size - APP_PACKAGE_HEADER_SIZE -
                                                     payload_size, 2U);
    if (result == OK) result = app_package_ensure_directory("", APP_PACKAGE_DIRECTORY);
    if (result == OK) {
        result = fs_create_dir_entry(APP_PACKAGE_DIRECTORY, info.id,
                                     APP_PACKAGE_DIRECTORY_ATTRIBUTE);
        if (result == OK) {
            package_directory_created = 1;
        } else {
            LOG_ERROR("PKG", "Falha ao criar diretorio do pacote");
            result = ERR_DISK;
        }
    }
    if (result == OK) {
        kmemcpy(directory, APP_PACKAGE_DIRECTORY, kstrlen(APP_PACKAGE_DIRECTORY));
        directory[kstrlen(APP_PACKAGE_DIRECTORY)] = '/';
        kmemcpy(directory + kstrlen(APP_PACKAGE_DIRECTORY) + 1U, info.id,
                kstrlen(info.id));
        directory[kstrlen(APP_PACKAGE_DIRECTORY) + 1U + kstrlen(info.id)] = '\0';
        result = fs_write_file_in_dir(directory, APP_PACKAGE_ENTRY_NAME, payload,
                                      payload_size);
        if (result >= 0) result = fs_write_file_in_dir(directory,
                                                        APP_PACKAGE_METADATA_NAME,
                                                        data + APP_PACKAGE_HEADER_SIZE,
                                                        size - APP_PACKAGE_HEADER_SIZE -
                                                        payload_size);
        if (result >= 0) result = OK;
        else result = ERR_DISK;
    }
    if (result != OK) {
        if (package_directory_created) app_package_cleanup_partial(info.id);
        LOG_WARN("PKG", "Instalacao de pacote falhou");
    } else {
        *info_out = info;
        LOG_INFO("PKG", "Pacote instalado com sucesso");
    }
    if (data) kfree(data);
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

int app_package_remove(const char* id) {
    char directory[FS_MAX_PATH];
    int result;

    if (!app_package_ready) {
        LOG_WARN("PKG", "Remocao solicitada sem servico pronto");
        return ERR_UNAVAILABLE;
    }
    if (app_package_id_is_valid(id) != OK) {
        LOG_ERROR("PKG", "Remocao recebeu ID invalido");
        return ERR_INVALID;
    }
    result = app_package_is_installed(id);
    if (result == OK) result = app_package_is_required_by_other(id);
    if (result != OK) return result;
    kmemcpy(directory, APP_PACKAGE_DIRECTORY, kstrlen(APP_PACKAGE_DIRECTORY));
    directory[kstrlen(APP_PACKAGE_DIRECTORY)] = '/';
    kmemcpy(directory + kstrlen(APP_PACKAGE_DIRECTORY) + 1U, id, kstrlen(id));
    directory[kstrlen(APP_PACKAGE_DIRECTORY) + 1U + kstrlen(id)] = '\0';
    if (fs_delete_file_in_dir(directory, APP_PACKAGE_ENTRY_NAME) != OK ||
        fs_delete_file_in_dir(directory, APP_PACKAGE_METADATA_NAME) != OK ||
        fs_delete_file_in_dir(APP_PACKAGE_DIRECTORY, id) != OK) {
        LOG_ERROR("PKG", "Falha ao remover registro de pacote");
        return ERR_DISK;
    }
    LOG_INFO("PKG", "Pacote removido com sucesso");
    return OK;
}

int app_package_run_diagnostics(app_package_diagnostic_t* diagnostic_out) {
    app_package_info_t info;
    app_package_header_t invalid_header;
    fs_info_t fs_info;
    int installed_count;

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
    if (fs_get_info(&fs_info) == OK && fs_info.free_clusters < 0xFFFFFFFFU) {
        uint32_t requested_clusters = fs_info.free_clusters + 1U;
        diagnostic_out->insufficient_space = requested_clusters >
                                             fs_info.free_clusters;
    }
    return OK;
}
