#include "fs/vfs_internal.h"
#include "fs/devfs.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "process/process.h"

typedef struct {
    vfs_mount_info_t info;
} vfs_mount_entry_t;

static vfs_mount_entry_t vfs_mount_table[VFS_MAX_MOUNTS];
static spinlock_t vfs_mount_lock;
static uint32_t vfs_lookup_count;
static uint32_t vfs_chdir_count;
static uint8_t vfs_path_ready;
static uint32_t vfs_mount_cwd_references(const char* mount_point);

static int vfs_path_fail(int error, const char* message) {
    LOG_WARN("FS", message);
    return error;
}

static void vfs_path_copy(char* destination, uint32_t capacity,
                          const char* source) {
    uint32_t index = 0U;

    if (!destination || !capacity) return;
    while (source && source[index] && index + 1U < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int vfs_path_equal(const char* first, const char* second) {
    uint32_t index = 0U;

    if (!first || !second) return 0;
    while (first[index] && second[index] &&
           first[index] == second[index]) index++;
    return first[index] == second[index];
}

static int vfs_path_prefix(const char* path, const char* prefix) {
    uint32_t index = 0U;

    if (!path || !prefix) return 0;
    while (prefix[index] && path[index] == prefix[index]) index++;
    if (prefix[index]) return 0;
    if (index == 1U && prefix[0] == '/') return 1;
    return path[index] == '\0' || path[index] == '/';
}

static int vfs_path_append(char* path, uint32_t* length,
                           const char* text, uint32_t amount) {
    if (!path || !length || !text) {
        return vfs_path_fail(ERR_NULL, "Argumento nulo ao compor caminho");
    }
    if (*length + amount >= VFS_MAX_PATH) {
        return vfs_path_fail(ERR_OVERFLOW, "Caminho excedeu o limite");
    }
    for (uint32_t index = 0U; index < amount; index++) {
        path[(*length)++] = text[index];
    }
    path[*length] = '\0';
    return OK;
}

static int vfs_normalize_from(const char* path, const char* cwd,
                              char* output) {
    char combined[VFS_MAX_PATH];
    uint32_t combined_length = 0U;
    uint32_t segment_starts[VFS_MAX_PATH / 2U];
    uint32_t segment_count = 0U;
    uint32_t output_length = 1U;
    uint32_t cursor = 0U;

    if (!path || !cwd || !output) {
        return vfs_path_fail(ERR_NULL, "Argumento nulo ao normalizar caminho");
    }
    if (!path[0]) {
        return vfs_path_fail(ERR_INVALID, "Caminho vazio rejeitado");
    }
    if (kstrlen(path) >= VFS_MAX_PATH || kstrlen(cwd) >= VFS_MAX_PATH) {
        return vfs_path_fail(ERR_OVERFLOW, "Caminho longo rejeitado");
    }
    combined[0] = '\0';
    if (path[0] != '/' && path[0] != '\\') {
        uint32_t cwd_length = kstrlen(cwd);
        if (vfs_path_append(combined, &combined_length, cwd,
                            cwd_length) != OK) {
            return vfs_path_fail(ERR_OVERFLOW, "Cwd excedeu o limite");
        }
        if (combined_length > 1U && combined[combined_length - 1U] != '/') {
            if (vfs_path_append(combined, &combined_length, "/", 1U) != OK) {
                return vfs_path_fail(ERR_OVERFLOW,
                                     "Separador excedeu o limite");
            }
        }
    }
    if (vfs_path_append(combined, &combined_length, path,
                        kstrlen(path)) != OK) {
        return vfs_path_fail(ERR_OVERFLOW, "Caminho excedeu o limite");
    }
    output[0] = '/';
    output[1] = '\0';
    while (combined[cursor]) {
        uint32_t start;
        uint32_t length;

        while (combined[cursor] == '/' || combined[cursor] == '\\') cursor++;
        if (!combined[cursor]) break;
        start = cursor;
        while (combined[cursor] && combined[cursor] != '/' &&
               combined[cursor] != '\\') cursor++;
        length = cursor - start;
        if (length == 1U && combined[start] == '.') continue;
        if (length == 2U && combined[start] == '.' &&
            combined[start + 1U] == '.') {
            if (!segment_count) {
                return vfs_path_fail(ERR_INVALID,
                                     "Escape acima da raiz rejeitado");
            }
            output_length = segment_starts[--segment_count];
            output[output_length] = '\0';
            continue;
        }
        if (segment_count >= VFS_MAX_PATH / 2U) {
            return vfs_path_fail(ERR_OVERFLOW, "Caminho possui segmentos demais");
        }
        segment_starts[segment_count++] = output_length == 1U ? 1U :
                                         output_length;
        if (output_length > 1U) {
            if (output_length + 1U >= VFS_MAX_PATH) {
                return vfs_path_fail(ERR_OVERFLOW,
                                     "Caminho normalizado excedeu o limite");
            }
            output[output_length++] = '/';
        }
        if (output_length + length >= VFS_MAX_PATH) {
            return vfs_path_fail(ERR_OVERFLOW,
                                 "Componente excedeu o limite do caminho");
        }
        for (uint32_t index = 0U; index < length; index++) {
            output[output_length++] = combined[start + index];
        }
        output[output_length] = '\0';
    }
    return OK;
}

static int vfs_current_cwd(char* cwd) {
    process_t* current = process_get_current();

    if (!cwd) return vfs_path_fail(ERR_NULL, "Destino de cwd nulo");
    if (!current || !current->fd_table.initialized ||
        !current->fd_table.cwd[0]) {
        vfs_path_copy(cwd, VFS_MAX_PATH, "/");
        return current ? ERR_STATE : OK;
    }
    vfs_path_copy(cwd, VFS_MAX_PATH, current->fd_table.cwd);
    return OK;
}

static int vfs_find_mount_by_volume_unlocked(const char* volume_id) {
    for (uint32_t index = 0U; index < VFS_MAX_MOUNTS; index++) {
        if (vfs_mount_table[index].info.used &&
            vfs_path_equal(vfs_mount_table[index].info.volume_id,
                           volume_id)) return (int)index;
    }
    return -1;
}

static int vfs_find_mount_for_path_unlocked(const char* path) {
    int selected = -1;
    uint32_t selected_length = 0U;

    for (uint32_t index = 0U; index < VFS_MAX_MOUNTS; index++) {
        uint32_t length;

        if (!vfs_mount_table[index].info.used ||
            !vfs_path_prefix(path,
                             vfs_mount_table[index].info.mount_point)) continue;
        length = kstrlen(vfs_mount_table[index].info.mount_point);
        if (selected < 0 || length > selected_length) {
            selected = (int)index;
            selected_length = length;
        }
    }
    return selected;
}

static int vfs_legacy_prefix(const char* path, char* canonical) {
    char prefix[STORAGE_ID_SIZE];
    char mount_point[VFS_MAX_PATH];
    const char* relative;
    uint32_t separator = 0U;
    int mount_index = -1;

    while (path[separator] && path[separator] != ':' &&
           separator + 1U < sizeof(prefix)) separator++;
    if (path[separator] != ':') return ERR_NOT_FOUND;
    for (uint32_t index = 0U; index < separator; index++) {
        prefix[index] = path[index];
    }
    prefix[separator] = '\0';
    spinlock_acquire(&vfs_mount_lock);
    if (vfs_path_equal(prefix, "system")) {
        mount_index = vfs_find_mount_for_path_unlocked("/");
    } else if (vfs_path_equal(prefix, "legacy")) {
        for (uint32_t index = 0U; index < VFS_MAX_MOUNTS; index++) {
            if (vfs_mount_table[index].info.used &&
                (vfs_mount_table[index].info.fs_type == STORAGE_FS_FAT12 ||
                 vfs_path_equal(vfs_mount_table[index].info.mount_point,
                                "/mnt/boot"))) {
                mount_index = (int)index;
                break;
            }
        }
    } else {
        mount_index = vfs_find_mount_by_volume_unlocked(prefix);
    }
    if (mount_index >= 0) {
        vfs_path_copy(mount_point, sizeof(mount_point),
                      vfs_mount_table[mount_index].info.mount_point);
    }
    spinlock_release(&vfs_mount_lock);
    if (mount_index < 0) {
        LOG_WARN("FS", "Alias VFS nao encontrado");
        return ERR_NOT_FOUND;
    }
    relative = path + separator + 1U;
    while (*relative == '/' || *relative == '\\') relative++;
    if (!*relative) return vfs_normalize_from(mount_point, "/", canonical);
    {
        char joined[VFS_MAX_PATH];
        uint32_t length = 0U;

        joined[0] = '\0';
        if (vfs_path_append(joined, &length, mount_point,
                            kstrlen(mount_point)) != OK ||
            (length > 1U && vfs_path_append(joined, &length, "/", 1U) != OK) ||
            vfs_path_append(joined, &length, relative,
                            kstrlen(relative)) != OK) {
            return vfs_path_fail(ERR_OVERFLOW, "Alias VFS excedeu o limite");
        }
        return vfs_normalize_from(joined, "/", canonical);
    }
}

static int vfs_canonicalize(const char* path, char* canonical) {
    char cwd[VFS_MAX_PATH];
    int result;

    if (!path || !canonical) {
        return vfs_path_fail(ERR_NULL, "Destino canonico nulo");
    }
    if (path[0] != '/' && path[0] != '\\') {
        result = vfs_legacy_prefix(path, canonical);
        if (result != ERR_NOT_FOUND) return result;
    }
    if (vfs_current_cwd(cwd) != OK) {
        return vfs_path_fail(ERR_STATE, "Cwd atual invalido");
    }
    return vfs_normalize_from(path, cwd, canonical);
}

static void vfs_mount_fill(vfs_mount_info_t* info,
                           const storage_volume_t* volume,
                           const char* mount_point, uint32_t slot) {
    kmemset(info, 0, sizeof(*info));
    info->used = 1U;
    info->slot = slot;
    info->generation = volume->generation ? volume->generation : 1U;
    info->pinned = volume->pinned || volume->boot ||
                   vfs_path_equal(mount_point, "/");
    info->read_only = volume->read_only;
    info->fs_type = volume->fs_type;
    info->kind = VFS_MOUNT_STORAGE;
    vfs_path_copy(info->mount_point, VFS_MAX_PATH, mount_point);
    vfs_path_copy(info->volume_id, STORAGE_ID_SIZE, volume->id);
}

static void vfs_devfs_mount_fill(vfs_mount_info_t* info) {
    kmemset(info, 0, sizeof(*info));
    info->used = 1U;
    info->slot = VFS_MAX_STORAGE_MOUNTS;
    info->generation = 1U;
    info->pinned = 1U;
    info->kind = VFS_MOUNT_DEVFS;
    vfs_path_copy(info->mount_point, VFS_MAX_PATH, "/dev");
    vfs_path_copy(info->volume_id, STORAGE_ID_SIZE, "devfs");
}

static int vfs_desired_add(vfs_mount_info_t* desired, uint32_t* count,
                           const storage_volume_t* volume,
                           const char* mount_point) {
    if (*count >= VFS_MAX_MOUNTS) {
        return vfs_path_fail(ERR_OVERFLOW, "Tabela de montagens VFS cheia");
    }
    vfs_mount_fill(&desired[*count], volume, mount_point, *count);
    (*count)++;
    return OK;
}

int vfs_path_init(void) {
    LOG_INFO("FS", "Inicializando namespace de caminhos VFS");
    spinlock_init(&vfs_mount_lock);
    kmemset(vfs_mount_table, 0, sizeof(vfs_mount_table));
    vfs_devfs_mount_fill(
        &vfs_mount_table[VFS_MAX_STORAGE_MOUNTS].info);
    vfs_lookup_count = 0U;
    vfs_chdir_count = 0U;
    vfs_path_ready = 1U;
    LOG_INFO("FS", "Namespace de caminhos VFS inicializado");
    return OK;
}

int vfs_refresh_mounts(void) {
    storage_status_t storage_status;
    storage_volume_t volumes[VFS_MAX_STORAGE_MOUNTS];
    vfs_mount_info_t desired[VFS_MAX_MOUNTS];
    vfs_mount_entry_t updated[VFS_MAX_MOUNTS];
    uint8_t placed[VFS_MAX_MOUNTS];
    uint32_t volume_count = 0U;
    uint32_t desired_count = 0U;
    int root_index = -1;
    int boot_index = -1;

    if (!vfs_path_ready) return ERR_STATE;
    if (storage_get_status(&storage_status) != OK) {
        return vfs_path_fail(ERR_STATE,
                             "Status Storage indisponivel no refresh VFS");
    }
    if (!storage_status.initialized) {
        return vfs_path_fail(ERR_STATE,
                             "Storage nao inicializado no refresh VFS");
    }
    if (storage_status.mounted_count > VFS_MAX_STORAGE_MOUNTS) {
        return vfs_path_fail(ERR_OVERFLOW,
                             "Montagens Storage excedem capacidade VFS");
    }
    kmemset(volumes, 0, sizeof(volumes));
    kmemset(desired, 0, sizeof(desired));
    kmemset(updated, 0, sizeof(updated));
    kmemset(placed, 0, sizeof(placed));
    vfs_devfs_mount_fill(&desired[desired_count++]);
    while (volume_count < storage_status.mounted_count) {
        if (storage_get_mounted_at((uint8_t)volume_count,
                                   &volumes[volume_count]) != OK) {
            return vfs_path_fail(ERR_STATE,
                                 "Snapshot de montagem Storage inconsistente");
        }
        if (volumes[volume_count].role == STORAGE_VOLUME_ROLE_SYSTEM) {
            root_index = (int)volume_count;
        }
        if (volumes[volume_count].boot ||
            volumes[volume_count].role == STORAGE_VOLUME_ROLE_BOOT) {
            boot_index = (int)volume_count;
        }
        volume_count++;
    }
    if (root_index < 0) root_index = boot_index;
    if (root_index < 0 && volume_count) root_index = 0;
    if (root_index >= 0 &&
        vfs_desired_add(desired, &desired_count, &volumes[root_index],
                        "/") != OK) return ERR_OVERFLOW;
    if (boot_index >= 0 && boot_index != root_index &&
        vfs_desired_add(desired, &desired_count, &volumes[boot_index],
                        "/mnt/boot") != OK) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < volume_count; index++) {
        char point[VFS_MAX_PATH];
        uint32_t length = 0U;

        if ((int)index == root_index || (int)index == boot_index) continue;
        point[0] = '\0';
        if (vfs_path_append(point, &length, "/mnt/", 5U) != OK ||
            vfs_path_append(point, &length, volumes[index].id,
                            kstrlen(volumes[index].id)) != OK) {
            return ERR_OVERFLOW;
        }
        if (vfs_desired_add(desired, &desired_count, &volumes[index],
                            point) != OK) return ERR_OVERFLOW;
    }
    spinlock_acquire(&vfs_mount_lock);
    for (uint32_t old = 0U; old < VFS_MAX_MOUNTS; old++) {
        int match = -1;

        if (!vfs_mount_table[old].info.used) continue;
        for (uint32_t index = 0U; index < desired_count; index++) {
            if (!placed[index] &&
                vfs_path_equal(vfs_mount_table[old].info.volume_id,
                               desired[index].volume_id) &&
                vfs_path_equal(vfs_mount_table[old].info.mount_point,
                               desired[index].mount_point)) {
                match = (int)index;
                break;
            }
        }
        if (match < 0 &&
            (vfs_mount_table[old].info.open_files ||
             vfs_mount_cwd_references(
                 vfs_mount_table[old].info.mount_point))) {
            spinlock_release(&vfs_mount_lock);
            LOG_ERROR("FS", "Refresh removeria montagem VFS ocupada");
            return ERR_STATE;
        }
        if (match < 0) continue;
        desired[match].slot = old;
        desired[match].generation =
            vfs_mount_table[old].info.generation;
        desired[match].open_files =
            vfs_mount_table[old].info.open_files;
        updated[old].info = desired[match];
        placed[match] = 1U;
    }
    for (uint32_t index = 0U; index < desired_count; index++) {
        if (placed[index]) continue;
        for (uint32_t slot = 0U; slot < VFS_MAX_MOUNTS; slot++) {
            if (updated[slot].info.used) continue;
            desired[index].slot = slot;
            updated[slot].info = desired[index];
            placed[index] = 1U;
            break;
        }
        if (!placed[index]) {
            spinlock_release(&vfs_mount_lock);
            return ERR_OVERFLOW;
        }
    }
    kmemcpy(vfs_mount_table, updated, sizeof(vfs_mount_table));
    spinlock_release(&vfs_mount_lock);
    return desired_count ? OK : ERR_NOT_FOUND;
}

static int vfs_resolve_canonical(const char* canonical, uint32_t mode,
                                 vfs_lookup_result_t* result) {
    uint32_t mount_length;
    int mount_index;
    int query_result;
    uint8_t is_directory;

    if (vfs_path_equal(canonical, "/mnt")) {
        result->type = VFS_NODE_DIRECTORY;
        return OK;
    }
    spinlock_acquire(&vfs_mount_lock);
    mount_index = vfs_find_mount_for_path_unlocked(canonical);
    if (mount_index >= 0) {
        vfs_mount_info_t* mount = &vfs_mount_table[mount_index].info;

        result->mount_slot = mount->slot;
        result->mount_generation = mount->generation;
        result->fs_type = mount->fs_type;
        result->mount_kind = mount->kind;
        result->read_only = mount->read_only;
        vfs_path_copy(result->mount_point, VFS_MAX_PATH,
                      mount->mount_point);
        vfs_path_copy(result->volume_id, STORAGE_ID_SIZE,
                      mount->volume_id);
    }
    spinlock_release(&vfs_mount_lock);
    if (mount_index < 0) {
        return vfs_path_fail(ERR_NOT_FOUND, "Montagem para caminho ausente");
    }
    if (result->mount_kind == VFS_MOUNT_DEVFS) {
        return devfs_lookup(canonical, result);
    }
    mount_length = kstrlen(result->mount_point);
    if (vfs_path_equal(canonical, result->mount_point)) {
        result->type = VFS_NODE_DIRECTORY;
        return OK;
    }
    if (mount_length == 1U) mount_length = 0U;
    while (canonical[mount_length] == '/') mount_length++;
    vfs_path_copy(result->relative_path, VFS_MAX_PATH,
                  canonical + mount_length);
    query_result = storage_get_path_info(result->volume_id,
                                         result->relative_path,
                                         &result->size,
                                         &result->attributes,
                                         &is_directory);
    if (query_result == OK) {
        result->type = is_directory ? VFS_NODE_DIRECTORY : VFS_NODE_REGULAR;
        return OK;
    }
    if (query_result == ERR_NOT_FOUND && mode == VFS_MODE_WRITE) {
        result->type = VFS_NODE_REGULAR;
        return OK;
    }
    return query_result;
}

int vfs_resolve_open_path(const char* path, uint32_t mode,
                          vfs_lookup_result_t* result) {
    int status;

    if (!path || !result) {
        return vfs_path_fail(ERR_NULL, "Lookup VFS recebeu argumento nulo");
    }
    kmemset(result, 0, sizeof(*result));
    status = vfs_canonicalize(path, result->canonical_path);
    if (status != OK) return status;
    spinlock_acquire(&vfs_mount_lock);
    vfs_lookup_count++;
    spinlock_release(&vfs_mount_lock);
    return vfs_resolve_canonical(result->canonical_path, mode, result);
}

int vfs_lookup(const char* path, vfs_lookup_result_t* result) {
    return vfs_resolve_open_path(path, VFS_MODE_READ, result);
}

static int vfs_resolve_directory(const char* path, char* canonical,
                                 uint32_t* mount_slot,
                                 uint32_t* mount_generation) {
    char volume_id[STORAGE_ID_SIZE];
    char relative[VFS_MAX_PATH];
    uint32_t mount_length;
    uint32_t size;
    uint8_t attributes;
    uint8_t is_directory;
    int mount_index;
    int result;

    result = vfs_canonicalize(path, canonical);
    if (result != OK) return result;
    if (vfs_path_equal(canonical, "/mnt")) {
        *mount_slot = VFS_MAX_MOUNTS;
        *mount_generation = 0U;
        return OK;
    }
    spinlock_acquire(&vfs_mount_lock);
    mount_index = vfs_find_mount_for_path_unlocked(canonical);
    if (mount_index >= 0) {
        vfs_mount_info_t* mount = &vfs_mount_table[mount_index].info;

        *mount_slot = mount->slot;
        *mount_generation = mount->generation;
        mount_length = kstrlen(mount->mount_point);
        vfs_path_copy(volume_id, sizeof(volume_id), mount->volume_id);
        if (vfs_path_equal(canonical, mount->mount_point)) {
            spinlock_release(&vfs_mount_lock);
            return OK;
        }
        if (mount->kind == VFS_MOUNT_DEVFS) {
            spinlock_release(&vfs_mount_lock);
            return vfs_path_fail(ERR_INVALID,
                                 "No devfs nao e diretorio");
        }
    }
    spinlock_release(&vfs_mount_lock);
    if (mount_index < 0) {
        return vfs_path_fail(ERR_NOT_FOUND,
                             "Diretorio sem montagem correspondente");
    }
    if (mount_length == 1U) mount_length = 0U;
    while (canonical[mount_length] == '/') mount_length++;
    vfs_path_copy(relative, sizeof(relative), canonical + mount_length);
    result = storage_get_path_info(volume_id, relative, &size, &attributes,
                                   &is_directory);
    if (result != OK) return result;
    if (!is_directory) {
        return vfs_path_fail(ERR_INVALID, "Chdir recebeu arquivo regular");
    }
    return OK;
}

int vfs_chdir(const char* path) {
    process_t* current;
    char canonical[VFS_MAX_PATH];
    uint32_t mount_slot;
    uint32_t mount_generation;
    int result;

    if (!path) {
        LOG_ERROR("FS", "Caminho nulo em chdir VFS");
        return ERR_NULL;
    }
    current = process_get_current();
    if (!current || !current->fd_table.initialized) return ERR_STATE;
    result = vfs_resolve_directory(path, canonical, &mount_slot,
                                   &mount_generation);
    if (result != OK) return result;
    spinlock_acquire(&vfs_mount_lock);
    if (!vfs_path_equal(canonical, "/mnt") &&
        (mount_slot >= VFS_MAX_MOUNTS ||
         !vfs_mount_table[mount_slot].info.used ||
         vfs_mount_table[mount_slot].info.generation != mount_generation)) {
        spinlock_release(&vfs_mount_lock);
        return ERR_STATE;
    }
    vfs_path_copy(current->fd_table.cwd, VFS_MAX_PATH, canonical);
    vfs_chdir_count++;
    spinlock_release(&vfs_mount_lock);
    return OK;
}

int vfs_getcwd(char* path, uint32_t capacity) {
    process_t* current;
    uint32_t length;

    if (!path) return vfs_path_fail(ERR_NULL, "Destino de getcwd nulo");
    current = process_get_current();
    if (!current || !current->fd_table.initialized) {
        return vfs_path_fail(ERR_STATE, "Getcwd sem processo valido");
    }
    length = kstrlen(current->fd_table.cwd);
    if (!capacity || length + 1U > capacity) {
        return vfs_path_fail(ERR_OVERFLOW, "Buffer de getcwd insuficiente");
    }
    vfs_path_copy(path, capacity, current->fd_table.cwd);
    return OK;
}

static int vfs_dir_name_equal(const char* first, const char* second) {
    uint32_t index = 0U;

    if (!first || !second) return 0;
    while (first[index] && second[index]) {
        char a = first[index];
        char b = second[index];
        if (a >= 'a' && a <= 'z') a = (char)(a - ('a' - 'A'));
        if (b >= 'a' && b <= 'z') b = (char)(b - ('a' - 'A'));
        if (a != b) return 0;
        index++;
    }
    return first[index] == second[index];
}

static int vfs_dir_add_virtual(vfs_dir_entry_t* entries, uint32_t capacity,
                               uint32_t* count, const char* name) {
    if (!entries || !count || !name) {
        LOG_ERROR("FS", "Entrada virtual VFS recebeu destino nulo");
        return ERR_NULL;
    }
    for (uint32_t index = 0U; index < *count; index++) {
        if (vfs_dir_name_equal(entries[index].name, name)) {
            entries[index].type = VFS_NODE_DIRECTORY;
            entries[index].size = 0U;
            return OK;
        }
    }
    if (*count >= capacity) {
        LOG_ERROR("FS", "Entrada virtual excedeu capacidade da listagem");
        return ERR_OVERFLOW;
    }
    kmemset(&entries[*count], 0, sizeof(entries[*count]));
    vfs_path_copy(entries[*count].name, sizeof(entries[*count].name), name);
    entries[*count].type = VFS_NODE_DIRECTORY;
    (*count)++;
    return OK;
}

static int vfs_list_mnt(vfs_dir_entry_t* entries, uint32_t capacity,
                        uint32_t* out_count) {
    uint32_t count = 0U;

    spinlock_acquire(&vfs_mount_lock);
    for (uint32_t index = 0U; index < VFS_MAX_MOUNTS; index++) {
        const char* point;

        if (!vfs_mount_table[index].info.used ||
            vfs_mount_table[index].info.kind != VFS_MOUNT_STORAGE ||
            vfs_path_equal(vfs_mount_table[index].info.mount_point, "/")) {
            continue;
        }
        point = vfs_mount_table[index].info.mount_point;
        if (!vfs_path_prefix(point, "/mnt") || !point[4]) continue;
        if (count >= capacity) {
            spinlock_release(&vfs_mount_lock);
            LOG_ERROR("FS", "Listagem de montagens excedeu capacidade");
            return ERR_OVERFLOW;
        }
        kmemset(&entries[count], 0, sizeof(entries[count]));
        vfs_path_copy(entries[count].name, sizeof(entries[count].name),
                      point + 5U);
        entries[count].type = VFS_NODE_DIRECTORY;
        count++;
    }
    spinlock_release(&vfs_mount_lock);
    *out_count = count;
    return OK;
}

int vfs_list_dir(const char* path, vfs_dir_entry_t* entries,
                 uint32_t capacity, uint32_t* out_count) {
    storage_long_dir_cursor_t cursor;
    storage_long_dir_entry_t storage_entry;
    vfs_lookup_result_t lookup;
    uint32_t count = 0U;
    uint8_t found = 0U;
    uint8_t done = 0U;
    int result;

    if (!path || !entries || !out_count) {
        LOG_ERROR("FS", "Listagem VFS recebeu argumento nulo");
        return ERR_NULL;
    }
    *out_count = 0U;
    if (!capacity || capacity > VFS_MAX_DIR_ENTRIES) {
        return vfs_path_fail(ERR_OVERFLOW, "Capacidade de listagem invalida");
    }
    result = vfs_lookup(path, &lookup);
    if (result != OK) return result;
    if (lookup.type != VFS_NODE_DIRECTORY) {
        return vfs_path_fail(ERR_INVALID,
                             "Listagem VFS recebeu no que nao e diretorio");
    }
    if (vfs_path_equal(lookup.canonical_path, "/dev")) {
        return devfs_list(entries, capacity, out_count);
    }
    if (vfs_path_equal(lookup.canonical_path, "/mnt")) {
        return vfs_list_mnt(entries, capacity, out_count);
    }
    result = storage_dir_cursor_open_long(lookup.volume_id,
                                          lookup.relative_path, &cursor);
    if (result != OK) return result;
    while (!done) {
        result = storage_dir_cursor_next_long(&cursor, &storage_entry,
                                              &found, &done);
        if (result != OK) return result;
        if (!found) continue;
        if (count >= capacity) {
            return vfs_path_fail(ERR_OVERFLOW,
                                 "Diretorio excedeu capacidade de listagem");
        }
        kmemset(&entries[count], 0, sizeof(entries[count]));
        vfs_path_copy(entries[count].name, sizeof(entries[count].name),
                      storage_entry.name);
        entries[count].type = storage_entry.is_directory ?
                              VFS_NODE_DIRECTORY : VFS_NODE_REGULAR;
        entries[count].size = storage_entry.size;
        count++;
    }
    if (vfs_path_equal(lookup.canonical_path, "/")) {
        result = vfs_dir_add_virtual(entries, capacity, &count, "mnt");
        if (result == OK) {
            result = vfs_dir_add_virtual(entries, capacity, &count, "dev");
        }
        if (result != OK) return result;
    }
    *out_count = count;
    return OK;
}

int vfs_fd_table_inherit_cwd(vfs_fd_table_t* table,
                             const vfs_fd_table_t* parent) {
    if (!table || !table->initialized) {
        return vfs_path_fail(ERR_STATE, "Tabela invalida ao herdar cwd");
    }
    vfs_path_copy(table->cwd, VFS_MAX_PATH,
                  parent && parent->initialized && parent->cwd[0] ?
                  parent->cwd : "/");
    return OK;
}

int vfs_mount_acquire(uint32_t slot, uint32_t generation) {
    if (slot >= VFS_MAX_MOUNTS) {
        return vfs_path_fail(ERR_INVALID, "Slot de montagem invalido");
    }
    spinlock_acquire(&vfs_mount_lock);
    if (!vfs_mount_table[slot].info.used ||
        vfs_mount_table[slot].info.generation != generation) {
        spinlock_release(&vfs_mount_lock);
        return vfs_path_fail(ERR_STATE, "Geracao de montagem mudou");
    }
    vfs_mount_table[slot].info.open_files++;
    spinlock_release(&vfs_mount_lock);
    return OK;
}

void vfs_mount_release(uint32_t slot, uint32_t generation) {
    if (slot >= VFS_MAX_MOUNTS) return;
    spinlock_acquire(&vfs_mount_lock);
    if (vfs_mount_table[slot].info.used &&
        vfs_mount_table[slot].info.generation == generation &&
        vfs_mount_table[slot].info.open_files) {
        vfs_mount_table[slot].info.open_files--;
    }
    spinlock_release(&vfs_mount_lock);
}

int vfs_mount_validate_reference(uint32_t slot, uint32_t generation) {
    int valid;

    if (slot >= VFS_MAX_MOUNTS) {
        return vfs_path_fail(ERR_INVALID, "Referencia de montagem invalida");
    }
    spinlock_acquire(&vfs_mount_lock);
    valid = vfs_mount_table[slot].info.used &&
            vfs_mount_table[slot].info.generation == generation;
    spinlock_release(&vfs_mount_lock);
    return valid ? OK : ERR_STATE;
}

static uint32_t vfs_mount_cwd_references(const char* mount_point) {
    uint32_t references = 0U;

    for (uint32_t index = 0U; index < MAX_PROCESSES; index++) {
        if (processes[index] && processes[index]->fd_table.initialized &&
            vfs_path_prefix(processes[index]->fd_table.cwd, mount_point)) {
            references++;
        }
    }
    return references;
}

int vfs_copy_mounts(vfs_mount_info_t* output, uint32_t capacity,
                    uint32_t* out_count) {
    uint32_t count = 0U;

    if (!output || !out_count) {
        return vfs_path_fail(ERR_NULL, "Snapshot de montagens nulo");
    }
    spinlock_acquire(&vfs_mount_lock);
    for (uint32_t index = 0U; index < VFS_MAX_MOUNTS; index++) {
        if (!vfs_mount_table[index].info.used) continue;
        if (count >= capacity) {
            spinlock_release(&vfs_mount_lock);
            return vfs_path_fail(ERR_OVERFLOW,
                                 "Snapshot de montagens sem capacidade");
        }
        output[count] = vfs_mount_table[index].info;
        output[count].cwd_references = vfs_mount_cwd_references(
            output[count].mount_point);
        count++;
    }
    spinlock_release(&vfs_mount_lock);
    *out_count = count;
    return OK;
}

int vfs_mount_volume(const char* volume_id) {
    int result;

    if (!volume_id) return ERR_NULL;
    result = storage_mount(volume_id);
    if (result != OK) return result;
    result = vfs_refresh_mounts();
    if (result != OK) {
        (void)storage_unmount(volume_id);
        LOG_ERROR("FS", "Falha ao publicar montagem no namespace VFS");
    }
    return result;
}

int vfs_unmount_volume(const char* volume_id) {
    vfs_mount_info_t mount;
    storage_volume_t volume;
    int index;
    int result;

    if (!volume_id) return ERR_NULL;
    spinlock_acquire(&vfs_mount_lock);
    index = vfs_find_mount_by_volume_unlocked(volume_id);
    if (index >= 0) mount = vfs_mount_table[index].info;
    spinlock_release(&vfs_mount_lock);
    if (index < 0) return ERR_NOT_FOUND;
    if (mount.pinned || mount.open_files ||
        vfs_mount_cwd_references(mount.mount_point)) {
        LOG_WARN("FS", "Desmontagem VFS recusada para volume ocupado");
        return ERR_STATE;
    }
    result = storage_find_volume(volume_id, &volume);
    if (result == ERR_NOT_FOUND || (result == OK && !volume.mounted)) {
        LOG_WARN("FS", "Alias VFS obsoleto reconciliado na desmontagem");
        result = vfs_refresh_mounts();
        return result == ERR_NOT_FOUND ? OK : result;
    }
    if (result != OK) return result;
    result = storage_unmount(volume_id);
    if (result != OK) return result;
    result = vfs_refresh_mounts();
    return result == ERR_NOT_FOUND ? OK : result;
}

void vfs_path_get_metrics(uint32_t* capacity, uint32_t* active,
                          uint32_t* lookups, uint32_t* chdirs) {
    uint32_t count = 0U;

    spinlock_acquire(&vfs_mount_lock);
    for (uint32_t index = 0U; index < VFS_MAX_MOUNTS; index++) {
        if (vfs_mount_table[index].info.used) count++;
    }
    if (capacity) *capacity = VFS_MAX_MOUNTS;
    if (active) *active = count;
    if (lookups) *lookups = vfs_lookup_count;
    if (chdirs) *chdirs = vfs_chdir_count;
    spinlock_release(&vfs_mount_lock);
}

int vfs_path_validate_state(void) {
    uint8_t root_found = 0U;
    uint8_t devfs_found = 0U;
    char normalized[VFS_MAX_PATH];

    if (!vfs_path_ready) {
        return vfs_path_fail(ERR_STATE, "Namespace VFS nao inicializado");
    }
    if (vfs_normalize_from(
            "/mnt/usb-ms-00:04.0-p1-a1-l0p1/DOCS", "/", normalized) != OK ||
        !vfs_path_equal(normalized,
                        "/mnt/usb-ms-00:04.0-p1-a1-l0p1/DOCS")) {
        return vfs_path_fail(ERR_STATE,
                             "Caminho universal com volume USB invalido");
    }
    spinlock_acquire(&vfs_mount_lock);
    for (uint32_t index = 0U; index < VFS_MAX_MOUNTS; index++) {
        vfs_mount_info_t* info = &vfs_mount_table[index].info;

        if (!info->used) continue;
        if (info->slot != index || !info->mount_point[0] ||
            !info->volume_id[0] || info->mount_point[0] != '/') {
            spinlock_release(&vfs_mount_lock);
            return vfs_path_fail(ERR_STATE, "Montagem VFS inconsistente");
        }
        if (info->kind == VFS_MOUNT_DEVFS) {
            if (info->fs_type != STORAGE_FS_NONE || !info->pinned ||
                !vfs_path_equal(info->mount_point, "/dev") ||
                !vfs_path_equal(info->volume_id, "devfs")) {
                spinlock_release(&vfs_mount_lock);
                return vfs_path_fail(ERR_STATE, "Montagem devfs inconsistente");
            }
            devfs_found = 1U;
        } else if (info->kind != VFS_MOUNT_STORAGE ||
                   info->fs_type == STORAGE_FS_NONE) {
            spinlock_release(&vfs_mount_lock);
            return vfs_path_fail(ERR_STATE, "Montagem Storage inconsistente");
        }
        if (vfs_path_equal(info->mount_point, "/")) root_found = 1U;
        for (uint32_t other = index + 1U; other < VFS_MAX_MOUNTS; other++) {
            if (vfs_mount_table[other].info.used &&
                vfs_path_equal(info->mount_point,
                               vfs_mount_table[other].info.mount_point)) {
                spinlock_release(&vfs_mount_lock);
                return vfs_path_fail(ERR_STATE,
                                     "Pontos de montagem duplicados");
            }
        }
    }
    for (uint32_t index = 0U; index < MAX_PROCESSES; index++) {
        int cwd_mount;

        if (!processes[index] || !processes[index]->fd_table.initialized) continue;
        cwd_mount = vfs_find_mount_for_path_unlocked(
            processes[index]->fd_table.cwd);
        if (!processes[index]->fd_table.cwd[0] ||
            processes[index]->fd_table.cwd[0] != '/' ||
            vfs_normalize_from(processes[index]->fd_table.cwd, "/",
                               normalized) != OK ||
            !vfs_path_equal(processes[index]->fd_table.cwd, normalized) ||
            (!vfs_path_equal(normalized, "/mnt") &&
             cwd_mount < 0) ||
            (cwd_mount >= 0 &&
             vfs_mount_table[cwd_mount].info.kind == VFS_MOUNT_DEVFS &&
             !vfs_path_equal(normalized, "/dev"))) {
            spinlock_release(&vfs_mount_lock);
            return vfs_path_fail(ERR_STATE, "Cwd de processo inconsistente");
        }
    }
    spinlock_release(&vfs_mount_lock);
    return root_found && devfs_found ? OK : ERR_NOT_FOUND;
}
