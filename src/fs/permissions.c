#include "fs/permissions.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "fs/fs.h"
#include "fs/storage.h"

typedef struct {
    char path[VFS_MAX_PATH];
    uint32_t uid;
    uint32_t gid;
    uint16_t mode;
    uint8_t type;
    uint8_t reserved;
} fs_permission_record_t;

typedef struct {
    char volume_id[STORAGE_ID_SIZE];
    uint32_t generation;
    uint32_t count;
    uint8_t loaded;
    uint8_t invalid;
    fs_permission_record_t records[FS_PERMISSION_MAX_RECORDS];
} fs_permission_volume_t;

static fs_permission_volume_t permission_volumes[STORAGE_MAX_MOUNTS];
static uint8_t permission_ready;

int fs_permissions_path_compare(const char* left, const char* right) {
    if (!left || !right) {
        if (left == right) return 0;
        return left ? 1 : -1;
    }
    while (*left && *right) {
        uint8_t left_value = (uint8_t)*left;
        uint8_t right_value = (uint8_t)*right;

        if (left_value >= (uint8_t)'A' && left_value <= (uint8_t)'Z') {
            left_value = (uint8_t)(left_value + ((uint8_t)'a' - (uint8_t)'A'));
        }
        if (right_value >= (uint8_t)'A' && right_value <= (uint8_t)'Z') {
            right_value = (uint8_t)(right_value + ((uint8_t)'a' - (uint8_t)'A'));
        }
        if (left_value != right_value) {
            return (int)left_value - (int)right_value;
        }
        left++;
        right++;
    }
    return (uint8_t)*left - (uint8_t)*right;
}

static uint32_t permission_crc32(const uint8_t* data, uint32_t size) {
    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t index = 0U; index < size; index++) {
        crc ^= data[index];
        for (uint32_t bit = 0U; bit < 8U; bit++) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

static uint16_t permission_read_u16(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t permission_read_u32(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void permission_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void permission_write_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static int permission_path_valid(const char* path) {
    uint32_t length;
    uint32_t component_start = 0U;

    if (!path || !path[0]) {
        LOG_WARN("FS_PERM", "Caminho de permissao vazio");
        return ERR_INVALID;
    }
    length = kstrlen(path);
    if (length >= VFS_MAX_PATH || path[0] == '/' ||
        path[length - 1U] == '/') return ERR_INVALID;
    for (uint32_t index = 0U; index <= length; index++) {
        if (index < length && (uint8_t)path[index] < 0x20U) {
            return ERR_INVALID;
        }
        if (index < length && path[index] == '\\') return ERR_INVALID;
        if (index == length || path[index] == '/') {
            uint32_t component_length = index - component_start;

            if (!component_length ||
                (component_length == 1U && path[component_start] == '.') ||
                (component_length == 2U && path[component_start] == '.' &&
                 path[component_start + 1U] == '.')) return ERR_INVALID;
            component_start = index + 1U;
        }
    }
    return OK;
}

static int permission_volume_slot(const char* volume_id, uint32_t generation,
                                  fs_permission_volume_t** output) {
    storage_volume_t volume;
    int free_slot = -1;
    int result;

    if (!volume_id || !output) {
        LOG_ERROR("FS_PERM", "Argumento nulo ao localizar volume");
        return ERR_NULL;
    }
    for (uint32_t index = 0U; index < STORAGE_MAX_MOUNTS; index++) {
        if (permission_volumes[index].loaded &&
            fs_permissions_path_compare(permission_volumes[index].volume_id,
                                         volume_id) == 0) {
            if (permission_volumes[index].generation != generation) {
                kmemset(&permission_volumes[index], 0,
                        sizeof(permission_volumes[index]));
            } else {
                *output = &permission_volumes[index];
                return permission_volumes[index].invalid ? ERR_STATE : OK;
            }
        }
        if (!permission_volumes[index].loaded && free_slot < 0) {
            free_slot = (int)index;
        }
    }
    if (free_slot < 0) {
        LOG_ERROR("FS_PERM", "Tabela de volumes de permissao cheia");
        return ERR_OVERFLOW;
    }
    *output = &permission_volumes[free_slot];
    kmemset(*output, 0, sizeof(**output));
    for (uint32_t index = 0U; index + 1U < STORAGE_ID_SIZE; index++) {
        (*output)->volume_id[index] = volume_id[index];
        if (!volume_id[index]) break;
    }
    (*output)->generation = generation;
    (*output)->loaded = 1U;
    result = storage_find_volume(volume_id, &volume);
    if (result != OK) {
        LOG_ERROR("FS_PERM", "Volume de permissao indisponivel");
        return result;
    }
    if (volume.fs_type != STORAGE_FS_FAT32 || volume.read_only) return OK;

    {
        uint32_t size = 0U;
        uint32_t file_size = 0U;
        uint32_t bytes_read = 0U;
        uint8_t attributes = 0U;
        uint8_t is_directory = 0U;
        uint8_t* data;

        result = storage_get_path_info(volume_id, FS_PERMISSION_SIDECAR_NAME,
                                       &size, &attributes, &is_directory);
        if (result == ERR_NOT_FOUND) return OK;
        if (result != OK || is_directory || size < FS_PERMISSION_SIDECAR_HEADER_SIZE ||
            size > FS_PERMISSION_SIDECAR_MAX_SIZE) {
            (*output)->invalid = 1U;
            return ERR_STATE;
        }
        file_size = size;
        data = (uint8_t*)kmalloc(size);
        if (!data) return ERR_MEM;
        result = storage_read_file_range(volume_id, FS_PERMISSION_SIDECAR_NAME,
                                         0U, data, file_size, &bytes_read);
        if (result == OK && bytes_read != file_size) result = ERR_STATE;
        if (result == OK) {
            uint32_t count = permission_read_u32(data + 8U);
            uint32_t record_size = permission_read_u32(data + 12U);
            uint32_t stored_crc = permission_read_u32(data + 16U);
            uint32_t payload_size;

            payload_size = count > FS_PERMISSION_MAX_RECORDS ? 0U :
                count * FS_PERMISSION_SIDECAR_RECORD_SIZE;
            if (permission_read_u32(data) != FS_PERMISSION_SIDECAR_MAGIC ||
                permission_read_u16(data + 4U) != FS_PERMISSION_SIDECAR_VERSION ||
                permission_read_u16(data + 6U) != FS_PERMISSION_SIDECAR_HEADER_SIZE ||
                record_size != FS_PERMISSION_SIDECAR_RECORD_SIZE ||
                count > FS_PERMISSION_MAX_RECORDS ||
                payload_size > size - FS_PERMISSION_SIDECAR_HEADER_SIZE ||
                payload_size != size - FS_PERMISSION_SIDECAR_HEADER_SIZE ||
                permission_crc32(data + FS_PERMISSION_SIDECAR_HEADER_SIZE,
                                 payload_size) != stored_crc) {
                (*output)->invalid = 1U;
                result = ERR_STATE;
            } else {
                (*output)->count = count;
                for (uint32_t index = 0U; index < count && result == OK; index++) {
                    const uint8_t* record = data + FS_PERMISSION_SIDECAR_HEADER_SIZE +
                        index * FS_PERMISSION_SIDECAR_RECORD_SIZE;
                    fs_permission_record_t* target = &(*output)->records[index];
                    uint32_t path_length = 0U;

                    while (path_length < VFS_MAX_PATH && record[path_length]) {
                        target->path[path_length] = (char)record[path_length];
                        path_length++;
                    }
                    if (path_length == 0U || path_length >= VFS_MAX_PATH ||
                        permission_path_valid(target->path) != OK ||
                        (index && fs_permissions_path_compare(
                            (*output)->records[index - 1U].path,
                            target->path) >= 0) ||
                        record[267U] != 0U) {
                        result = ERR_STATE;
                        break;
                    }
                    for (uint32_t offset = path_length + 1U;
                         offset < 256U; offset++) {
                        if (record[offset] != 0U) {
                            result = ERR_STATE;
                            break;
                        }
                    }
                    if (result != OK) break;
                    target->uid = permission_read_u32(record + 256U);
                    target->gid = permission_read_u32(record + 260U);
                    target->mode = permission_read_u16(record + 264U);
                    target->type = record[266U];
                    if ((target->mode & 0777U) != target->mode ||
                        target->type == VFS_NODE_NONE) result = ERR_STATE;
                }
                if (result != OK) (*output)->invalid = 1U;
            }
        }
        kfree(data);
    }
    return result;
}

static int permission_record_find(fs_permission_volume_t* volume,
                                  const char* path, uint32_t* index_out) {
    if (!volume || !path || !index_out) {
        LOG_ERROR("FS_PERM", "Argumento nulo ao buscar permissao");
        return ERR_NULL;
    }
    for (uint32_t index = 0U; index < volume->count; index++) {
        int compare = fs_permissions_path_compare(volume->records[index].path,
                                                  path);
        if (compare == 0) {
            *index_out = index;
            return OK;
        }
        if (compare > 0) break;
    }
    return ERR_NOT_FOUND;
}

static uint16_t permission_default_mode(const char* path, uint8_t is_directory) {
    uint32_t length;
    const char* base;

    if (is_directory) {
        return FS_PERMISSION_LEGACY_DIRECTORY;
    }
    length = path ? kstrlen(path) : 0U;
    base = path ? path : "";
    for (uint32_t index = 0U; index < length; index++) {
        if (path[index] == '/') base = path + index + 1U;
    }
    if (fs_permissions_path_compare(base, FS_PERMISSION_SIDECAR_NAME) == 0) {
        return 0U;
    }
    if (fs_permissions_path_compare(base, "APP.ZAP") == 0) return 0555U;
    if (fs_permissions_path_compare(base, "META.DAT") == 0 ||
        fs_permissions_path_compare(base, "AUTH.DAT") == 0) return 0444U;
    return FS_PERMISSION_LEGACY_FILE;
}

static uint16_t permission_new_mode(const char* path, uint8_t is_directory) {
    uint16_t mode = permission_default_mode(path, is_directory);

    if (is_directory) {
        if (path && kstrlen(path) > 5U &&
            (path[0] == 'A' || path[0] == 'a') &&
            (path[1] == 'P' || path[1] == 'p') &&
            (path[2] == 'P' || path[2] == 'p') &&
            (path[3] == 'S' || path[3] == 's') &&
            path[4] == '/') {
            for (uint32_t index = 5U; path[index]; index++) {
                if (path[index] == '/') return FS_PERMISSION_DIRECTORY_DEFAULT;
            }
            return FS_PERMISSION_LEGACY_DIRECTORY;
        }
        return FS_PERMISSION_DIRECTORY_DEFAULT;
    }
    if (!is_directory && mode == FS_PERMISSION_LEGACY_FILE) {
        return FS_PERMISSION_FILE_DEFAULT;
    }
    return mode;
}

static int permission_get_storage(const char* volume_id, uint32_t generation,
                                  const char* path, vfs_node_type_t type,
                                  fs_permission_info_t* output) {
    fs_permission_volume_t* volume;
    storage_volume_t storage_volume;
    uint32_t index;
    int result;

    if (!output) {
        LOG_ERROR("FS_PERM", "Saida de permissao ausente");
        return ERR_NULL;
    }
    (void)generation;
    kmemset(output, 0, sizeof(*output));
    result = permission_path_valid(path);
    if (result != OK) return result;
    result = storage_find_volume(volume_id, &storage_volume);
    if (result != OK) return result;
    result = permission_volume_slot(volume_id, storage_volume.generation,
                                    &volume);
    if (result != OK) return result;
    result = permission_record_find(volume, path, &index);
    if (result == OK) {
        output->uid = volume->records[index].uid;
        output->gid = volume->records[index].gid;
        output->mode = volume->records[index].mode;
        output->type = (vfs_node_type_t)volume->records[index].type;
        output->present = 1U;
        return OK;
    }
    if (result != ERR_NOT_FOUND) return result;
    output->uid = PROCESS_UID_ROOT;
    output->gid = PROCESS_GID_ROOT;
    output->mode = permission_default_mode(path, type == VFS_NODE_DIRECTORY);
    output->type = type;
    return OK;
}

static int permission_insert(fs_permission_volume_t* volume,
                             const char* path, vfs_node_type_t type,
                             uint32_t uid, uint32_t gid, uint16_t mode) {
    uint32_t position = 0U;

    if (!volume || !path) {
        LOG_ERROR("FS_PERM", "Destino de permissao ausente");
        return ERR_NULL;
    }
    while (position < volume->count &&
           fs_permissions_path_compare(volume->records[position].path,
                                       path) < 0) position++;
    if (position < volume->count &&
        fs_permissions_path_compare(volume->records[position].path, path) == 0) {
        volume->records[position].uid = uid;
        volume->records[position].gid = gid;
        volume->records[position].mode = mode;
        volume->records[position].type = (uint8_t)type;
        return OK;
    }
    if (volume->count >= FS_PERMISSION_MAX_RECORDS) {
        LOG_WARN("FS_PERM", "Limite de registros de permissao atingido");
        return ERR_OVERFLOW;
    }
    for (uint32_t index = volume->count; index > position; index--) {
        volume->records[index] = volume->records[index - 1U];
    }
    kmemset(&volume->records[position], 0,
            sizeof(volume->records[position]));
    for (uint32_t index = 0U; index + 1U < VFS_MAX_PATH && path[index]; index++) {
        volume->records[position].path[index] = path[index];
    }
    volume->records[position].uid = uid;
    volume->records[position].gid = gid;
    volume->records[position].mode = mode;
    volume->records[position].type = (uint8_t)type;
    volume->count++;
    return OK;
}

static int permission_serialize(const fs_permission_volume_t* volume,
                                uint8_t** data_out, uint32_t* size_out) {
    uint32_t size;
    uint8_t* data;

    if (!volume || !data_out || !size_out) {
        LOG_ERROR("FS_PERM", "Entrada de serializacao de permissao ausente");
        return ERR_NULL;
    }
    size = FS_PERMISSION_SIDECAR_HEADER_SIZE +
           volume->count * FS_PERMISSION_SIDECAR_RECORD_SIZE;
    data = (uint8_t*)kmalloc(size);
    if (!data) {
        LOG_ERROR("FS_PERM", "Memoria insuficiente para ZPERM.DAT");
        return ERR_MEM;
    }
    kmemset(data, 0U, size);
    permission_write_u32(data, FS_PERMISSION_SIDECAR_MAGIC);
    permission_write_u16(data + 4U, FS_PERMISSION_SIDECAR_VERSION);
    permission_write_u16(data + 6U, FS_PERMISSION_SIDECAR_HEADER_SIZE);
    permission_write_u32(data + 8U, volume->count);
    permission_write_u32(data + 12U, FS_PERMISSION_SIDECAR_RECORD_SIZE);
    for (uint32_t index = 0U; index < volume->count; index++) {
        uint8_t* record = data + FS_PERMISSION_SIDECAR_HEADER_SIZE +
            index * FS_PERMISSION_SIDECAR_RECORD_SIZE;
        const fs_permission_record_t* source = &volume->records[index];

        for (uint32_t offset = 0U; offset + 1U < VFS_MAX_PATH &&
             source->path[offset]; offset++) record[offset] =
            (uint8_t)source->path[offset];
        permission_write_u32(record + 256U, source->uid);
        permission_write_u32(record + 260U, source->gid);
        permission_write_u16(record + 264U, source->mode);
        record[266U] = source->type;
    }
    permission_write_u32(data + 16U,
                         permission_crc32(data + FS_PERMISSION_SIDECAR_HEADER_SIZE,
                                          size - FS_PERMISSION_SIDECAR_HEADER_SIZE));
    *data_out = data;
    *size_out = size;
    return OK;
}

static int permission_persist(fs_permission_volume_t* volume) {
    uint8_t* data = 0;
    uint32_t size = 0U;
    int result;

    result = permission_serialize(volume, &data, &size);
    if (result != OK) return result;
    result = storage_atomic_write_file(volume->volume_id,
                                       FS_PERMISSION_SIDECAR_NAME, data, size,
                                       FS_ATTRIBUTE_HIDDEN | FS_ATTRIBUTE_SYSTEM,
                                       STORAGE_ATOMIC_CREATE_OR_REPLACE);
    kfree(data);
    if (result != OK) LOG_ERROR("FS_PERM", "Falha ao persistir ZPERM.DAT");
    return result;
}

int fs_permissions_init(void) {
    kmemset(permission_volumes, 0, sizeof(permission_volumes));
    permission_ready = 1U;
    return OK;
}

int fs_permissions_prepare_volume(const char* volume_id) {
    storage_volume_t storage_volume;
    fs_permission_volume_t* volume;
    int result;

    if (!volume_id) {
        LOG_ERROR("FS_PERM", "Volume ausente ao preparar permissoes");
        return ERR_NULL;
    }
    if (!permission_ready) {
        LOG_ERROR("FS_PERM", "Modulo de permissoes nao inicializado");
        return ERR_STATE;
    }
    result = storage_find_volume(volume_id, &storage_volume);
    if (result != OK) return result;
    if (storage_volume.fs_type != STORAGE_FS_FAT32 ||
        storage_volume.read_only) return OK;
    result = permission_volume_slot(volume_id, storage_volume.generation,
                                    &volume);
    return result;
}

int fs_permissions_prepare_path(const char* volume_id,
                                const char* relative_path) {
    storage_volume_t storage_volume;
    fs_permission_volume_t* volume;
    uint32_t index;
    int result;

    if (!volume_id || !relative_path) return ERR_NULL;
    if (fs_permissions_path_compare(relative_path,
                                    FS_PERMISSION_SIDECAR_NAME) == 0) {
        return ERR_UNAVAILABLE;
    }
    result = permission_path_valid(relative_path);
    if (result != OK) return result;
    result = storage_find_volume(volume_id, &storage_volume);
    if (result != OK) return result;
    if (storage_volume.fs_type != STORAGE_FS_FAT32 ||
        storage_volume.read_only) return OK;
    result = permission_volume_slot(volume_id, storage_volume.generation,
                                    &volume);
    if (result != OK) return result;
    result = permission_record_find(volume, relative_path, &index);
    if (result == ERR_NOT_FOUND && volume->count >= FS_PERMISSION_MAX_RECORDS) {
        LOG_WARN("FS_PERM", "Limite de registros ZPERM.DAT atingido");
        return ERR_OVERFLOW;
    }
    return result == ERR_NOT_FOUND ? OK : result;
}

int fs_permissions_apply_lookup(vfs_lookup_result_t* lookup) {
    fs_permission_info_t info;
    int result;

    if (!lookup) {
        LOG_ERROR("FS_PERM", "Lookup ausente ao aplicar permissoes");
        return ERR_NULL;
    }
    if (!permission_ready) {
        LOG_ERROR("FS_PERM", "Lookup antes da inicializacao de permissoes");
        return ERR_STATE;
    }
    if (lookup->mount_kind == VFS_MOUNT_STORAGE && lookup->volume_id[0]) {
        if (lookup->relative_path[0] == '\0') {
            lookup->uid = PROCESS_UID_ROOT;
            lookup->gid = PROCESS_GID_ROOT;
            lookup->mode = FS_PERMISSION_LEGACY_DIRECTORY;
            lookup->permission_present = 1U;
            return OK;
        }
        result = permission_get_storage(lookup->volume_id,
                                        lookup->mount_generation,
                                        lookup->relative_path, lookup->type,
                                        &info);
        if (result != OK) return result;
        if (info.present && info.type != lookup->type) return ERR_STATE;
        lookup->uid = info.uid;
        lookup->gid = info.gid;
        lookup->mode = info.mode;
        lookup->permission_present = info.present;
        return OK;
    }
    lookup->uid = PROCESS_UID_ROOT;
    lookup->gid = PROCESS_GID_ROOT;
    lookup->permission_present = 1U;
    lookup->mode = lookup->type == VFS_NODE_DIRECTORY ? 0555U : 0444U;
    if (lookup->mount_kind == VFS_MOUNT_DEVFS) {
        lookup->mode = kstrcmp(lookup->canonical_path, "/dev/hda") == 0 ?
            0400U : 0666U;
    }
    return OK;
}

static int permission_bits_allow(const process_credentials_t* credentials,
                                 const fs_permission_info_t* info,
                                 uint32_t access) {
    uint16_t bits;
    uint16_t required = 0U;

    if (!credentials || !info) {
        LOG_ERROR("FS_PERM", "Credencial ou metadado de permissao ausente");
        return ERR_NULL;
    }
    if (credentials->uid == PROCESS_UID_ROOT) return OK;
    if (access & (FS_PERMISSION_ACCESS_READ | FS_PERMISSION_ACCESS_LIST)) {
        required |= 04U;
    }
    if (access & (FS_PERMISSION_ACCESS_WRITE | FS_PERMISSION_ACCESS_CREATE |
                  FS_PERMISSION_ACCESS_SYNC)) required |= 02U;
    if (access & (FS_PERMISSION_ACCESS_EXECUTE | FS_PERMISSION_ACCESS_CREATE)) {
        required |= 01U;
    }
    bits = credentials->uid == info->uid ? (info->mode >> 6U) & 07U :
           credentials->gid == info->gid ? (info->mode >> 3U) & 07U :
           info->mode & 07U;
    return (bits & required) == required ? OK : ERR_UNAVAILABLE;
}

int fs_permissions_check_lookup(const process_credentials_t* credentials,
                                const vfs_lookup_result_t* lookup,
                                uint32_t access) {
    fs_permission_info_t info;

    if (!credentials || !lookup) return ERR_NULL;
    if (!access || access & ~FS_PERMISSION_ACCESS_ALL) return ERR_INVALID;
    if (lookup->mount_kind == VFS_MOUNT_STORAGE &&
        lookup->relative_path[0] &&
        permission_path_valid(lookup->relative_path) != OK) return ERR_INVALID;
    if (lookup->mount_kind == VFS_MOUNT_STORAGE &&
        fs_permissions_path_compare(lookup->relative_path,
                                    FS_PERMISSION_SIDECAR_NAME) == 0) {
        LOG_WARN("FS_PERM", "Sidecar de permissoes reservado ao kernel");
        return ERR_UNAVAILABLE;
    }
    info.uid = lookup->uid;
    info.gid = lookup->gid;
    info.mode = lookup->mode;
    info.type = lookup->type;
    info.present = lookup->permission_present;
    if (lookup->type == VFS_NODE_DIRECTORY &&
        (access & (FS_PERMISSION_ACCESS_READ | FS_PERMISSION_ACCESS_WRITE))) {
        return ERR_INVALID;
    }
    if (lookup->type != VFS_NODE_DIRECTORY &&
        (access & (FS_PERMISSION_ACCESS_LIST | FS_PERMISSION_ACCESS_CREATE))) {
        return ERR_INVALID;
    }
    if (lookup->mount_kind == VFS_MOUNT_DEVFS) {
        if (kstrcmp(lookup->canonical_path, "/dev/hda") == 0 &&
            !process_credentials_has(credentials,
                                      PROCESS_CAPABILITY_DEVICE_BLOCK)) {
            LOG_WARN("FS_PERM", "Dispositivo de bloco sem capacidade");
            return ERR_UNAVAILABLE;
        }
        if (kstrcmp(lookup->canonical_path, "/dev/speaker") == 0 &&
            !process_credentials_has(credentials,
                                      PROCESS_CAPABILITY_DEVICE_AUDIO)) {
            LOG_WARN("FS_PERM", "Dispositivo de audio sem capacidade");
            return ERR_UNAVAILABLE;
        }
        if (kstrcmp(lookup->canonical_path, "/dev/hda") != 0 &&
            kstrcmp(lookup->canonical_path, "/dev/speaker") != 0 &&
            !process_credentials_has(credentials,
                                      PROCESS_CAPABILITY_DEVICE_BASIC)) {
            LOG_WARN("FS_PERM", "Dispositivo basico sem capacidade");
            return ERR_UNAVAILABLE;
        }
    }
    if ((access & (FS_PERMISSION_ACCESS_READ | FS_PERMISSION_ACCESS_LIST)) &&
        !process_credentials_has(credentials, PROCESS_CAPABILITY_FILE_READ)) {
        LOG_WARN("FS_PERM", "Leitura sem capacidade");
        return ERR_UNAVAILABLE;
    }
    if ((access & (FS_PERMISSION_ACCESS_WRITE | FS_PERMISSION_ACCESS_CREATE |
                   FS_PERMISSION_ACCESS_SYNC)) &&
        !process_credentials_has(credentials, PROCESS_CAPABILITY_FILE_WRITE)) {
        LOG_WARN("FS_PERM", "Escrita sem capacidade");
        return ERR_UNAVAILABLE;
    }
    if ((access & FS_PERMISSION_ACCESS_EXECUTE) &&
        !process_credentials_has(credentials, PROCESS_CAPABILITY_FILE_EXECUTE)) {
        LOG_WARN("FS_PERM", "Execucao sem capacidade");
        return ERR_UNAVAILABLE;
    }
    if ((access & FS_PERMISSION_ACCESS_IOCTL) &&
        lookup->mount_kind != VFS_MOUNT_DEVFS &&
        lookup->mount_kind != VFS_MOUNT_PROCFS) {
        LOG_WARN("FS_PERM", "IOCTL sem provider permitido");
        return ERR_UNAVAILABLE;
    }
    {
        int result = permission_bits_allow(credentials, &info, access);

        if (result != OK) LOG_WARN_CODE("FS_PERM", result,
                                        "Bits POSIX recusaram acesso");
        return result;
    }
}

int fs_permissions_check_traversal(const process_credentials_t* credentials,
                                    const vfs_lookup_result_t* lookup) {
    char parent[VFS_MAX_PATH];
    uint32_t length;
    int result;

    if (!credentials || !lookup) {
        LOG_ERROR("FS_PERM", "Contexto ausente na travessia VFS");
        return ERR_NULL;
    }
    if (lookup->mount_kind != VFS_MOUNT_STORAGE ||
        !lookup->relative_path[0]) return OK;
    if (permission_path_valid(lookup->relative_path) != OK) return ERR_INVALID;
    if (!process_credentials_has(credentials,
                                 PROCESS_CAPABILITY_FILE_EXECUTE)) {
        return ERR_UNAVAILABLE;
    }
    length = kstrlen(lookup->relative_path);
    for (uint32_t cursor = 0U; cursor < length; cursor++) {
        fs_permission_info_t info;
        uint32_t size = 0U;
        uint8_t attributes = 0U;
        uint8_t is_directory = 0U;

        if (lookup->relative_path[cursor] != '/') continue;
        if (!cursor || cursor + 1U >= length || cursor >= sizeof(parent)) {
            return ERR_INVALID;
        }
        for (uint32_t index = 0U; index < cursor; index++) {
            parent[index] = lookup->relative_path[index];
        }
        parent[cursor] = '\0';
        result = storage_get_path_info(lookup->volume_id, parent, &size,
                                       &attributes, &is_directory);
        if (result != OK) return result;
        if (!is_directory) return ERR_INVALID;
        result = permission_get_storage(lookup->volume_id,
                                        lookup->mount_generation, parent,
                                        VFS_NODE_DIRECTORY, &info);
        if (result != OK) return result;
        result = permission_bits_allow(credentials, &info,
                                       FS_PERMISSION_ACCESS_EXECUTE);
        if (result != OK) return result;
    }
    return OK;
}

int fs_permissions_check_create(const process_credentials_t* credentials,
                                const vfs_lookup_result_t* lookup) {
    char parent[VFS_MAX_PATH];
    uint32_t length;
    uint32_t slash = 0U;
    fs_permission_info_t info;
    int result;

    if (!credentials || !lookup) {
        LOG_ERROR("FS_PERM", "Contexto ausente na criacao VFS");
        return ERR_NULL;
    }
    if (lookup->mount_kind != VFS_MOUNT_STORAGE || lookup->read_only) {
        return ERR_UNAVAILABLE;
    }
    if (permission_path_valid(lookup->relative_path) != OK) return ERR_INVALID;
    if (lookup->type != VFS_NODE_REGULAR &&
        lookup->type != VFS_NODE_DIRECTORY) return ERR_INVALID;
    length = kstrlen(lookup->relative_path);
    if (!length || length >= VFS_MAX_PATH) return ERR_INVALID;
    for (uint32_t index = 0U; index < length; index++) {
        if (lookup->relative_path[index] == '/') slash = index;
    }
    if (slash >= sizeof(parent)) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < slash; index++) parent[index] =
        lookup->relative_path[index];
    parent[slash] = '\0';
    if (!parent[0]) {
        info.uid = PROCESS_UID_ROOT;
        info.gid = PROCESS_GID_ROOT;
        info.mode = FS_PERMISSION_LEGACY_DIRECTORY;
        info.type = VFS_NODE_DIRECTORY;
    } else {
        result = permission_get_storage(lookup->volume_id,
                                        lookup->mount_generation, parent,
                                        VFS_NODE_DIRECTORY, &info);
        if (result != OK) return result;
    }
    return permission_bits_allow(credentials, &info,
                                 FS_PERMISSION_ACCESS_WRITE |
                                 FS_PERMISSION_ACCESS_EXECUTE);
}

int fs_permissions_register_path(const char* volume_id,
                                 const char* relative_path,
                                 vfs_node_type_t type) {
    fs_permission_volume_t* volume;
    process_credentials_t credentials;
    storage_volume_t storage_volume;
    uint32_t generation;
    fs_permission_volume_t* backup = 0;
    fs_permission_info_t parent_info;
    char parent_path[VFS_MAX_PATH];
    uint32_t parent_length = 0U;
    uint32_t record_index;
    uint16_t mode;
    int result;

    if (!volume_id || !relative_path) {
        LOG_ERROR("FS_PERM", "Caminho ausente ao registrar permissao");
        return ERR_NULL;
    }
    result = storage_find_volume(volume_id, &storage_volume);
    if (result != OK) return result;
    if (storage_volume.fs_type != STORAGE_FS_FAT32 || storage_volume.read_only) {
        return OK;
    }
    generation = storage_volume.generation;
    result = permission_volume_slot(volume_id, generation, &volume);
    if (result != OK) return result;
    if (permission_path_valid(relative_path) != OK) return ERR_INVALID;
    result = permission_record_find(volume, relative_path, &record_index);
    if (result == OK) {
        return volume->records[record_index].type == (uint8_t)type ?
               OK : ERR_STATE;
    }
    if (result != ERR_NOT_FOUND) return result;
    backup = (fs_permission_volume_t*)kmalloc(sizeof(*backup));
    if (!backup) {
        LOG_ERROR("FS_PERM", "Memoria insuficiente para rollback de permissao");
        return ERR_MEM;
    }
    *backup = *volume;
    if (process_credentials_current(&credentials) != OK) {
        credentials.uid = PROCESS_UID_ROOT;
        credentials.gid = PROCESS_GID_ROOT;
    }
    for (uint32_t index = 0U; relative_path[index]; index++) {
        if (relative_path[index] == '/') parent_length = index;
    }
    if (parent_length > 0U && parent_length < sizeof(parent_path)) {
        for (uint32_t index = 0U; index < parent_length; index++) {
            parent_path[index] = relative_path[index];
        }
        parent_path[parent_length] = '\0';
        if (permission_get_storage(volume_id, generation, parent_path,
                                   VFS_NODE_DIRECTORY, &parent_info) == OK &&
            parent_info.present) {
            credentials.gid = parent_info.gid;
        }
    }
    mode = permission_new_mode(relative_path, type == VFS_NODE_DIRECTORY);
    result = permission_insert(volume, relative_path, type, credentials.uid,
                               credentials.gid, mode);
    if (result != OK) {
        kfree(backup);
        return result;
    }
    result = permission_persist(volume);
    if (result != OK) {
        *volume = *backup;
        kfree(backup);
        return result;
    }
    kfree(backup);
    return OK;
}

int fs_permissions_remove_path(const char* volume_id,
                               const char* relative_path) {
    fs_permission_volume_t* volume;
    storage_volume_t storage_volume;
    fs_permission_volume_t* backup = 0;
    uint32_t index;
    int result;

    if (!volume_id || !relative_path) {
        LOG_ERROR("FS_PERM", "Caminho ausente ao remover permissao");
        return ERR_NULL;
    }
    if (fs_permissions_path_compare(relative_path,
                                    FS_PERMISSION_SIDECAR_NAME) == 0) {
        return ERR_UNAVAILABLE;
    }
    result = permission_path_valid(relative_path);
    if (result != OK) return result;
    result = storage_find_volume(volume_id, &storage_volume);
    if (result != OK) return result;
    if (storage_volume.fs_type != STORAGE_FS_FAT32 || storage_volume.read_only) {
        return OK;
    }
    result = permission_volume_slot(volume_id, storage_volume.generation,
                                    &volume);
    if (result != OK) return result;
    result = permission_record_find(volume, relative_path, &index);
    if (result == ERR_NOT_FOUND) return OK;
    if (result != OK) return result;
    backup = (fs_permission_volume_t*)kmalloc(sizeof(*backup));
    if (!backup) {
        LOG_ERROR("FS_PERM", "Memoria insuficiente para rollback de remocao");
        return ERR_MEM;
    }
    *backup = *volume;
    for (; index + 1U < volume->count; index++) {
        volume->records[index] = volume->records[index + 1U];
    }
    volume->count--;
    result = permission_persist(volume);
    if (result != OK) *volume = *backup;
    kfree(backup);
    return result;
}

int fs_permissions_rename_path(const char* volume_id,
                               const char* relative_path,
                               const char* new_name) {
    fs_permission_volume_t* volume;
    storage_volume_t storage_volume;
    fs_permission_volume_t* backup = 0;
    uint32_t index;
    uint32_t slash = 0U;
    uint32_t target_index;
    char new_path[VFS_MAX_PATH];
    int result;

    if (!volume_id || !relative_path || !new_name) {
        LOG_ERROR("FS_PERM", "Caminho ausente ao renomear permissao");
        return ERR_NULL;
    }
    if (permission_path_valid(relative_path) != OK || !new_name[0]) {
        return ERR_INVALID;
    }
    for (uint32_t cursor = 0U; new_name[cursor]; cursor++) {
        if (new_name[cursor] == '/' || new_name[cursor] == '\\' ||
            (uint8_t)new_name[cursor] < 0x20U) return ERR_INVALID;
    }
    result = storage_find_volume(volume_id, &storage_volume);
    if (result != OK) return result;
    if (storage_volume.fs_type != STORAGE_FS_FAT32 || storage_volume.read_only) {
        return OK;
    }
    result = permission_volume_slot(volume_id, storage_volume.generation,
                                    &volume);
    if (result != OK) return result;
    result = permission_record_find(volume, relative_path, &index);
    if (result == ERR_NOT_FOUND) return OK;
    if (result != OK) return result;
    for (uint32_t cursor = 0U; relative_path[cursor]; cursor++) {
        if (relative_path[cursor] == '/') slash = cursor + 1U;
    }
    if (slash + kstrlen(new_name) >= sizeof(new_path)) return ERR_OVERFLOW;
    for (uint32_t cursor = 0U; cursor < slash; cursor++) new_path[cursor] =
        relative_path[cursor];
    for (uint32_t cursor = 0U; new_name[cursor]; cursor++) {
        new_path[slash + cursor] = new_name[cursor];
    }
    new_path[slash + kstrlen(new_name)] = '\0';
    if (permission_path_valid(new_path) != OK) return ERR_INVALID;
    if (fs_permissions_path_compare(new_path, relative_path) != 0 &&
        permission_record_find(volume, new_path, &target_index) == OK) {
        return ERR_STATE;
    }
    backup = (fs_permission_volume_t*)kmalloc(sizeof(*backup));
    if (!backup) {
        LOG_ERROR("FS_PERM", "Memoria insuficiente para rollback de renomeacao");
        return ERR_MEM;
    }
    *backup = *volume;
    for (uint32_t cursor = 0U; cursor + 1U < VFS_MAX_PATH; cursor++) {
        volume->records[index].path[cursor] = new_path[cursor];
        if (!new_path[cursor]) break;
    }
    for (uint32_t first = 1U; first < volume->count; first++) {
        fs_permission_record_t current = volume->records[first];
        uint32_t position = first;
        while (position > 0U && fs_permissions_path_compare(
                   volume->records[position - 1U].path, current.path) > 0) {
            volume->records[position] = volume->records[position - 1U];
            position--;
        }
        volume->records[position] = current;
    }
    result = permission_persist(volume);
    if (result != OK) *volume = *backup;
    kfree(backup);
    return result;
}

int fs_permissions_validate(void) {
    if (!permission_ready) {
        LOG_ERROR("FS_PERM", "Validacao antes da inicializacao");
        return ERR_STATE;
    }
    for (uint32_t volume = 0U; volume < STORAGE_MAX_MOUNTS; volume++) {
        fs_permission_volume_t* current = &permission_volumes[volume];
        if (!current->loaded) continue;
        if (current->invalid || current->count > FS_PERMISSION_MAX_RECORDS) {
            return ERR_STATE;
        }
        for (uint32_t index = 0U; index < current->count; index++) {
            if (permission_path_valid(current->records[index].path) != OK ||
                (index && fs_permissions_path_compare(
                    current->records[index - 1U].path,
                    current->records[index].path) >= 0)) {
                return ERR_STATE;
            }
        }
    }
    return OK;
}

const char* fs_permission_access_name(uint32_t access) {
    if (access == FS_PERMISSION_ACCESS_READ) return "read";
    if (access == FS_PERMISSION_ACCESS_WRITE) return "write";
    if (access == FS_PERMISSION_ACCESS_EXECUTE) return "execute";
    if (access == FS_PERMISSION_ACCESS_LIST) return "list";
    if (access == FS_PERMISSION_ACCESS_CREATE) return "create";
    if (access == FS_PERMISSION_ACCESS_SYNC) return "sync";
    if (access == FS_PERMISSION_ACCESS_IOCTL) return "ioctl";
    return "unknown";
}
