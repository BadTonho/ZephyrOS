#include "fs/fs.h"
#include "fs/fat12.h"
#include "fs/fat32.h"
#include "core/video.h"
#include "core/log.h"
#include "core/errors.h"
#include "core/string.h"
#include "core/spinlock.h"

#define FS_DIR_BASE_NAME_LENGTH 8U
#define FS_DIR_FULL_NAME_LENGTH 11U
#define FS_DIR_ATTRIBUTE_OFFSET 11U
#define FS_DIR_CLUSTER_HIGH_OFFSET 20U
#define FS_DIR_CLUSTER_LOW_OFFSET 26U
#define FS_DIR_FILE_SIZE_OFFSET 28U
#define FS_DIR_LFN_ATTRIBUTE 0x0FU
#define FS_DIR_VOLUME_ATTRIBUTE 0x08U
#define FS_DIR_DIRECTORY_ATTRIBUTE 0x10U
#define FS_FIRST_DATA_CLUSTER 2U
#define FS_INVALID_CLUSTER 0xFFFFFFFFU
#define FS_FAT32_ENTRY_SIZE 4U

static uint8_t current_fs_type = FS_TYPE_NONE;
static uint32_t fs_generation;
static spinlock_t fs_operation_lock;

static uint32_t fs_resolve_dir_cluster(const char* dir_path);

static void fs_advance_generation_unlocked(void) {
    fs_generation++;
    if (fs_generation == 0U) fs_generation = 1U;
}

static int fs_stream_blocks_mutation_unlocked(void) {
    if (current_fs_type == FS_TYPE_FAT12 &&
        fat12_stream_is_active()) {
        LOG_WARN("FS", "Mutacao recusada durante escrita streaming");
        return 1;
    }
    return 0;
}

static int fs_write_file_in_dir_unlocked(const char* dir_path,
                                         const char* filename,
                                         const uint8_t* data,
                                         uint32_t size);

int fs_init(void) {
    LOG_INFO("FS", "Inicializando interface unificada de filesystem");
    spinlock_init(&fs_operation_lock);
    current_fs_type = FS_TYPE_NONE;

    int fat12_result = fat12_init();
    if (fat12_result == OK && fat12_get_fs()->initialized) {
        current_fs_type = FS_TYPE_FAT12;
        fs_advance_generation_unlocked();
        video_print("Sistema de arquivos: FAT12\n", 0x0A);
        LOG_INFO("FS", "Interface FAT12 inicializada com sucesso");
        return OK;
    }

    int fat32_result = fat32_init();
    if (fat32_result == OK && fat32_get_fs()->initialized) {
        current_fs_type = FS_TYPE_FAT32;
        fs_advance_generation_unlocked();
        video_print("Sistema de arquivos: FAT32\n", 0x0A);
        LOG_INFO("FS", "Interface FAT32 inicializada com sucesso");
        return OK;
    }

    fs_advance_generation_unlocked();
    LOG_ERROR("FS", "Nenhum sistema de arquivos montado");
    video_print("Nenhum sistema de arquivos encontrado!\n", 0x0C);
    if (fat12_result == ERR_DISK || fat32_result == ERR_DISK) return ERR_DISK;
    return ERR_NOT_FOUND;
}

int fs_read_file(const char* filename, uint8_t* buffer, uint32_t max_size) {
    int result;

    spinlock_acquire(&fs_operation_lock);
    if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_read_file(filename, buffer, max_size);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        result = fat32_read_file(filename, buffer, max_size);
    } else {
        result = ERR_NOT_FOUND;
    }
    spinlock_release(&fs_operation_lock);
    return result;
}

int fs_write_file(const char* filename, const uint8_t* data, uint32_t size) {
    int result;
    int mutated = 0;

    spinlock_acquire(&fs_operation_lock);
    if (fs_stream_blocks_mutation_unlocked()) {
        result = ERR_STATE;
    } else if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_write_file(filename, data, size);
        mutated = result >= 0;
    } else if (current_fs_type == FS_TYPE_FAT32) {
        result = fat32_write_file(filename, data, size);
        mutated = result >= 0;
    } else {
        result = ERR_NOT_FOUND;
    }
    if (mutated) fs_advance_generation_unlocked();
    spinlock_release(&fs_operation_lock);
    if (!mutated && result != OK) {
        LOG_ERROR("FS", "Escrita de arquivo nao foi concluida");
    }
    return result;
}

int fs_delete_file(const char* filename) {
    int result;
    int mutated = 0;

    spinlock_acquire(&fs_operation_lock);
    if (fs_stream_blocks_mutation_unlocked()) {
        result = ERR_STATE;
    } else if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_delete_file(filename);
        mutated = result >= 0;
    } else if (current_fs_type == FS_TYPE_FAT32) {
        result = fat32_delete_file(filename);
        mutated = result >= 0;
    } else {
        result = ERR_NOT_FOUND;
    }
    if (mutated) fs_advance_generation_unlocked();
    spinlock_release(&fs_operation_lock);
    return result;
}

int fs_list_dir(void) {
    int result;

    spinlock_acquire(&fs_operation_lock);
    if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_list_dir();
    } else if (current_fs_type == FS_TYPE_FAT32) {
        result = fat32_list_dir();
    } else {
        result = ERR_NOT_FOUND;
    }
    spinlock_release(&fs_operation_lock);
    return result;
}

int fs_get_file_count(void) {
    int result;

    spinlock_acquire(&fs_operation_lock);
    if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_get_file_count();
    } else if (current_fs_type == FS_TYPE_FAT32) {
        result = fat32_get_file_count();
    } else {
        result = 0;
    }
    spinlock_release(&fs_operation_lock);
    return result;
}

int fs_get_file_info(int index, char* name_out, uint32_t* size_out, uint8_t* attr_out) {
    int result;

    spinlock_acquire(&fs_operation_lock);
    if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_get_file_info(index, name_out, size_out, attr_out);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        result = fat32_get_file_info(index, name_out, size_out, attr_out);
    } else {
        result = ERR_NOT_FOUND;
    }
    spinlock_release(&fs_operation_lock);
    return result;
}

int fs_get_info(fs_info_t* info) {
    int result = OK;

    if (!info) {
        LOG_ERROR("FS", "Destino nulo na consulta do filesystem");
        return ERR_NULL;
    }
    spinlock_acquire(&fs_operation_lock);

    info->type = current_fs_type;

    if (current_fs_type == FS_TYPE_FAT12) {
        fat12_fs_t* fs = fat12_get_fs();
        info->bytes_per_sector = fs->bpb.bytes_per_sector;
        info->sectors_per_cluster = fs->bpb.sectors_per_cluster;
        info->total_sectors = fs->bpb.total_sectors;
        if (info->total_sectors == 0) info->total_sectors = fs->bpb.large_sector_count;
        info->total_clusters = (info->total_sectors - fs->data_start) /
                               info->sectors_per_cluster;
        info->free_clusters = fat12_get_free_clusters();
        info->free_sectors = info->free_clusters * info->sectors_per_cluster;
        kmemcpy(info->label, fs->bpb.volume_label, 11);
        info->label[11] = '\0';
    } else if (current_fs_type == FS_TYPE_FAT32) {
        fat32_fs_t* fs = fat32_get_fs();
        info->bytes_per_sector = fs->bpb.bytes_per_sector;
        info->sectors_per_cluster = fs->bpb.sectors_per_cluster;
        info->total_sectors = fs->bpb.total_sectors_large;
        info->total_clusters = fs->total_clusters;
        info->free_clusters = fat32_get_free_clusters();
        info->free_sectors = info->free_clusters * info->sectors_per_cluster;
        kmemcpy(info->label, fs->bpb.volume_label, 11);
        info->label[11] = '\0';
    } else {
        result = ERR_NOT_FOUND;
    }

    spinlock_release(&fs_operation_lock);
    return result;
}

uint8_t fs_get_type(void) {
    return current_fs_type;
}

uint32_t fs_get_generation(void) {
    uint32_t generation;

    spinlock_acquire(&fs_operation_lock);
    generation = fs_generation;
    spinlock_release(&fs_operation_lock);
    return generation;
}

static uint16_t fs_read_u16(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t fs_read_u32(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int fs_cursor_name(const uint8_t* raw, char* output) {
    uint32_t offset = 0;

    if (!raw || !output) {
        LOG_ERROR("FS", "Entrada nula ao converter nome do cursor");
        return ERR_NULL;
    }
    for (uint32_t index = 0;
         index < FS_DIR_BASE_NAME_LENGTH && raw[index] != ' '; index++) {
        output[offset++] = (char)raw[index];
    }
    if (raw[FS_DIR_BASE_NAME_LENGTH] != ' ') {
        output[offset++] = '.';
        for (uint32_t index = FS_DIR_BASE_NAME_LENGTH;
             index < FS_DIR_FULL_NAME_LENGTH && raw[index] != ' '; index++) {
            output[offset++] = (char)raw[index];
        }
    }
    output[offset] = '\0';
    return offset ? OK : ERR_INVALID;
}

static int fs_cursor_is_dot(const char* name) {
    return name && name[0] == '.' &&
           (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

static int fs_cursor_next_fat12(uint32_t cluster, uint32_t* out_next) {
    fat12_fs_t* fat = fat12_get_fs();
    uint32_t offset;
    uint16_t value;

    if (!out_next || !fat || !fat->fat) {
        LOG_ERROR("FS", "FAT12 indisponivel para cursor");
        return ERR_NULL;
    }
    offset = cluster + cluster / 2U;
    if (offset + 1U >= fat->bpb.sectors_per_fat * fat->bpb.bytes_per_sector) {
        return ERR_OVERFLOW;
    }
    value = fs_read_u16((const uint8_t*)fat->fat + offset);
    *out_next = cluster & 1U ? (uint32_t)(value >> 4) :
                              (uint32_t)(value & 0x0FFFU);
    return OK;
}

static int fs_cursor_next_fat32(uint32_t cluster, uint32_t* out_next) {
    fat32_fs_t* fat = fat32_get_fs();
    uint8_t sector[FS_DIR_SECTOR_SIZE];
    uint32_t offset;
    uint32_t sector_index;
    uint32_t entry_offset;

    if (!out_next || !fat) {
        LOG_ERROR("FS", "FAT32 indisponivel para cursor");
        return ERR_NULL;
    }
    offset = cluster * FS_FAT32_ENTRY_SIZE;
    sector_index = offset / FS_DIR_SECTOR_SIZE;
    entry_offset = offset % FS_DIR_SECTOR_SIZE;
    if (sector_index >= fat->bpb.sectors_per_fat ||
        ata_read_sectors(fat->fat_start + sector_index, 1, sector) != 0) {
        return ERR_DISK;
    }
    *out_next = fs_read_u32(sector + entry_offset) & 0x0FFFFFFFU;
    return OK;
}

static int fs_cursor_load_sector_unlocked(fs_dir_cursor_t* cursor) {
    uint32_t sectors_per_cluster;
    uint32_t next;
    uint32_t lba;

    if (!cursor) {
        LOG_ERROR("FS", "Cursor nulo ao carregar setor");
        return ERR_NULL;
    }
    if (cursor->fixed_root) {
        fat12_fs_t* fat = fat12_get_fs();
        uint32_t root_bytes = fat->bpb.root_entries * FS_DIR_ENTRY_SIZE;
        uint32_t root_sectors = (root_bytes + FS_DIR_SECTOR_SIZE - 1U) /
                                FS_DIR_SECTOR_SIZE;

        if (cursor->sector_index >= root_sectors) {
            cursor->done = 1;
            return OK;
        }
        kmemset(cursor->sector, 0, sizeof(cursor->sector));
        uint32_t copied = cursor->sector_index * FS_DIR_SECTOR_SIZE;
        uint32_t remaining = root_bytes - copied;
        uint32_t amount = remaining < FS_DIR_SECTOR_SIZE ?
                          remaining : FS_DIR_SECTOR_SIZE;
        kmemcpy(cursor->sector, (const uint8_t*)fat->root_dir + copied,
                amount);
        cursor->sector_loaded = 1;
        cursor->entry_index = 0;
        return OK;
    }

    sectors_per_cluster = cursor->fs_type == FS_TYPE_FAT12 ?
                          fat12_get_fs()->bpb.sectors_per_cluster :
                          fat32_get_fs()->bpb.sectors_per_cluster;
    if (cursor->sector_index >= sectors_per_cluster) {
        if (cursor->chain_steps++ >= FS_DIR_CHAIN_LIMIT) return ERR_OVERFLOW;
        int result = cursor->fs_type == FS_TYPE_FAT12 ?
                     fs_cursor_next_fat12(cursor->current_cluster, &next) :
                     fs_cursor_next_fat32(cursor->current_cluster, &next);
        if (result != OK) return result;
        if ((cursor->fs_type == FS_TYPE_FAT12 && next >= FAT12_CLUSTER_END) ||
            (cursor->fs_type == FS_TYPE_FAT32 && next >= FAT32_CLUSTER_END)) {
            cursor->done = 1;
            return OK;
        }
        if ((cursor->fs_type == FS_TYPE_FAT12 && next == FAT12_CLUSTER_BAD) ||
            (cursor->fs_type == FS_TYPE_FAT32 && next == FAT32_CLUSTER_BAD)) {
            return ERR_DISK;
        }
        if ((cursor->fs_type == FS_TYPE_FAT12 &&
             next >= FAT12_CLUSTER_RESERVED) ||
            (cursor->fs_type == FS_TYPE_FAT32 &&
             next >= FAT32_CLUSTER_RESERVED) ||
            next < FS_FIRST_DATA_CLUSTER) return ERR_INVALID;
        cursor->current_cluster = next;
        cursor->sector_index = 0;
        return OK;
    }

    if (cursor->fs_type == FS_TYPE_FAT12) {
        fat12_fs_t* fat = fat12_get_fs();
        uint32_t total_sectors = fat->bpb.total_sectors ?
                                 fat->bpb.total_sectors :
                                 fat->bpb.large_sector_count;
        uint32_t total_clusters =
            (total_sectors - fat->data_start) /
            fat->bpb.sectors_per_cluster;

        if (cursor->current_cluster < FS_FIRST_DATA_CLUSTER ||
            cursor->current_cluster >=
                total_clusters + FS_FIRST_DATA_CLUSTER) {
            return ERR_INVALID;
        }
        lba = fat->data_start +
              (cursor->current_cluster - FS_FIRST_DATA_CLUSTER) *
              fat->bpb.sectors_per_cluster + cursor->sector_index;
    } else {
        fat32_fs_t* fat = fat32_get_fs();
        if (cursor->current_cluster < FS_FIRST_DATA_CLUSTER ||
            cursor->current_cluster >=
                fat->total_clusters + FS_FIRST_DATA_CLUSTER) {
            return ERR_INVALID;
        }
        lba = fat->data_start +
              (cursor->current_cluster - FS_FIRST_DATA_CLUSTER) *
              fat->bpb.sectors_per_cluster + cursor->sector_index;
    }
    if (ata_read_sectors(lba, 1, cursor->sector) != 0) return ERR_DISK;
    cursor->sector_loaded = 1;
    cursor->entry_index = 0;
    return OK;
}

int fs_dir_cursor_open(const char* path, fs_dir_cursor_t* cursor) {
    uint32_t cluster;

    if (!path || !cursor) {
        LOG_ERROR("FS", "Argumento nulo ao abrir cursor de diretorio");
        return ERR_NULL;
    }
    if (kstrlen(path) >= FS_MAX_PATH) {
        LOG_ERROR("FS", "Caminho excede limite do cursor de diretorio");
        return ERR_OVERFLOW;
    }
    spinlock_acquire(&fs_operation_lock);
    if (current_fs_type == FS_TYPE_NONE) {
        spinlock_release(&fs_operation_lock);
        LOG_ERROR("FS", "Cursor solicitado sem filesystem montado");
        return ERR_UNAVAILABLE;
    }
    cluster = fs_resolve_dir_cluster(path);
    if ((current_fs_type == FS_TYPE_FAT12 && cluster == FS_INVALID_CLUSTER) ||
        (current_fs_type == FS_TYPE_FAT32 &&
         cluster < FS_FIRST_DATA_CLUSTER)) {
        spinlock_release(&fs_operation_lock);
        LOG_ERROR("FS", "Diretorio do cursor nao foi encontrado");
        return ERR_NOT_FOUND;
    }
    kmemset(cursor, 0, sizeof(*cursor));
    cursor->generation = fs_generation;
    cursor->directory_cluster = cluster;
    cursor->current_cluster = cluster;
    cursor->fs_type = current_fs_type;
    cursor->fixed_root = current_fs_type == FS_TYPE_FAT12 && cluster == 0U;
    cursor->active = 1;
    spinlock_release(&fs_operation_lock);
    return OK;
}

int fs_dir_cursor_next(fs_dir_cursor_t* cursor, fs_dir_entry_t* out_entry,
                       uint8_t* out_found, uint8_t* out_done) {
    int result;

    if (!cursor || !out_entry || !out_found || !out_done) {
        LOG_ERROR("FS", "Argumento nulo ao avancar cursor de diretorio");
        return ERR_NULL;
    }
    *out_found = 0;
    *out_done = cursor->done;
    spinlock_acquire(&fs_operation_lock);
    if (!cursor->active || cursor->fs_type != current_fs_type ||
        cursor->generation != fs_generation) {
        spinlock_release(&fs_operation_lock);
        LOG_ERROR("FS", "Cursor de diretorio ficou desatualizado");
        return ERR_STATE;
    }
    if (cursor->done) {
        spinlock_release(&fs_operation_lock);
        return OK;
    }
    if (!cursor->sector_loaded) {
        result = fs_cursor_load_sector_unlocked(cursor);
        if (result != OK) {
            spinlock_release(&fs_operation_lock);
            LOG_ERROR("FS", "Falha ao carregar setor do cursor");
            return result;
        }
        if (!cursor->sector_loaded || cursor->done) {
            *out_done = cursor->done;
            spinlock_release(&fs_operation_lock);
            return OK;
        }
    }

    while (cursor->entry_index <
           FS_DIR_SECTOR_SIZE / FS_DIR_ENTRY_SIZE) {
        const uint8_t* raw = cursor->sector +
                             cursor->entry_index++ * FS_DIR_ENTRY_SIZE;
        uint8_t attributes = raw[FS_DIR_ATTRIBUTE_OFFSET];

        if (raw[0] == 0x00U) {
            cursor->done = 1;
            *out_done = 1;
            spinlock_release(&fs_operation_lock);
            return OK;
        }
        if (raw[0] == 0xE5U || attributes == FS_DIR_LFN_ATTRIBUTE ||
            (attributes & FS_DIR_VOLUME_ATTRIBUTE)) continue;
        kmemset(out_entry, 0, sizeof(*out_entry));
        if (fs_cursor_name(raw, out_entry->name) != OK ||
            fs_cursor_is_dot(out_entry->name)) continue;
        out_entry->attributes = attributes;
        out_entry->is_directory =
            (attributes & FS_DIR_DIRECTORY_ATTRIBUTE) ? 1U : 0U;
        out_entry->cluster = fs_read_u16(raw + FS_DIR_CLUSTER_LOW_OFFSET);
        if (cursor->fs_type == FS_TYPE_FAT32) {
            out_entry->cluster |=
                (uint32_t)fs_read_u16(raw + FS_DIR_CLUSTER_HIGH_OFFSET) << 16;
        }
        out_entry->size = fs_read_u32(raw + FS_DIR_FILE_SIZE_OFFSET);
        *out_found = 1;
        spinlock_release(&fs_operation_lock);
        return OK;
    }
    cursor->sector_loaded = 0;
    cursor->sector_index++;
    cursor->entry_index = 0;
    spinlock_release(&fs_operation_lock);
    return OK;
}

static uint32_t fs_resolve_dir_cluster(const char* dir_path) {
    if (!dir_path || dir_path[0] == '\0') {
        if (current_fs_type == FS_TYPE_FAT12) return 0;
        if (current_fs_type == FS_TYPE_FAT32) return fat32_get_fs()->root_cluster;
        return 0;
    }

    if (current_fs_type == FS_TYPE_FAT12) {
        uint16_t c = fat12_resolve_path(dir_path);
        if (c == 0xFFFF) return 0xFFFFFFFF;
        return (uint32_t)c;
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_resolve_path(dir_path);
    }
    return 0;
}

int fs_read_file_at(const char* path, uint8_t* buffer, uint32_t max_size) {
    int result;

    spinlock_acquire(&fs_operation_lock);
    if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_read_file_at(path, buffer, max_size);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        result = fat32_read_file_at(path, buffer, max_size);
    } else {
        result = ERR_NOT_FOUND;
    }
    spinlock_release(&fs_operation_lock);
    return result;
}

int fs_read_file_range_at(const char* path, uint32_t offset,
                          uint8_t* buffer, uint32_t max_size,
                          uint32_t* bytes_read) {
    int result;

    if (!path || !bytes_read) {
        LOG_ERROR("FS", "Argumento nulo na leitura por faixa");
        return ERR_NULL;
    }
    if (max_size > 0 && !buffer) {
        LOG_ERROR("FS", "Buffer nulo na leitura por faixa");
        return ERR_NULL;
    }
    *bytes_read = 0;

    spinlock_acquire(&fs_operation_lock);
    if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_read_file_range_at(path, offset, buffer, max_size);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        result = fat32_read_file_range_at(path, offset, buffer, max_size);
    } else {
        spinlock_release(&fs_operation_lock);
        LOG_WARN("FS", "Leitura por faixa sem filesystem montado");
        return ERR_UNAVAILABLE;
    }
    spinlock_release(&fs_operation_lock);

    if (result < 0) {
        LOG_WARN("FS", "Arquivo nao encontrado ou leitura por faixa falhou");
        return ERR_NOT_FOUND;
    }

    *bytes_read = (uint32_t)result;
    return OK;
}

static int fs_write_file_at_unlocked(const char* path, const uint8_t* data,
                                     uint32_t size) {
    char dir_path[FS_MAX_PATH];
    char filename[FS_MAX_PATH];
    uint32_t length;
    int last_slash = -1;
    int result;

    if (!path) {
        LOG_ERROR("FS", "Caminho nulo na escrita por caminho");
        return ERR_NULL;
    }
    if (size > 0 && !data) {
        LOG_ERROR("FS", "Dados nulos na escrita por caminho");
        return ERR_NULL;
    }
    if (current_fs_type == FS_TYPE_NONE) {
        LOG_WARN("FS", "Escrita por caminho sem filesystem montado");
        return ERR_UNAVAILABLE;
    }

    length = kstrlen(path);
    if (length == 0) {
        LOG_ERROR("FS", "Caminho vazio na escrita por caminho");
        return ERR_INVALID;
    }
    if (length >= FS_MAX_PATH) {
        LOG_ERROR("FS", "Caminho excede o limite na escrita por caminho");
        return ERR_OVERFLOW;
    }

    for (uint32_t i = 0; i < length; i++) {
        if (path[i] == '/') last_slash = (int)i;
    }

    if (last_slash < 0) {
        result = fs_write_file_in_dir_unlocked("", path, data, size);
    } else {
        uint32_t dir_length = (uint32_t)last_slash;
        uint32_t filename_length = length - (uint32_t)last_slash - 1;

        if (filename_length == 0 || dir_length >= FS_MAX_PATH ||
            filename_length >= FS_MAX_PATH) {
            LOG_ERROR("FS", "Caminho invalido na escrita por caminho");
            return ERR_INVALID;
        }

        kmemcpy(dir_path, path, dir_length);
        dir_path[dir_length] = '\0';
        kmemcpy(filename, path + last_slash + 1, filename_length);
        filename[filename_length] = '\0';
        result = fs_write_file_in_dir_unlocked(
            dir_path, filename, data, size);
    }

    if (result < 0) {
        LOG_WARN("FS", "Escrita por caminho falhou");
        return ERR_DISK;
    }

    /* Drivers FAT retornam bytes gravados; esta camada publica errors.h. */
    return OK;
}

int fs_write_file_at(const char* path, const uint8_t* data, uint32_t size) {
    int result;

    spinlock_acquire(&fs_operation_lock);
    result = fs_stream_blocks_mutation_unlocked() ?
             ERR_STATE : fs_write_file_at_unlocked(path, data, size);
    if (result == OK) fs_advance_generation_unlocked();
    spinlock_release(&fs_operation_lock);
    return result;
}

int fs_get_file_count_at(const char* dir_path) {
    int result;

    spinlock_acquire(&fs_operation_lock);
    uint32_t cluster = fs_resolve_dir_cluster(dir_path);
    if (cluster == 0xFFFFFFFF) {
        spinlock_release(&fs_operation_lock);
        return 0;
    }

    if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_get_file_count_at((uint16_t)cluster);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        result = fat32_get_file_count_at(cluster);
    } else {
        result = 0;
    }
    spinlock_release(&fs_operation_lock);
    return result;
}

int fs_get_file_info_at(const char* dir_path, int index, char* name_out, uint32_t* size_out, uint8_t* attr_out) {
    int result;

    spinlock_acquire(&fs_operation_lock);
    uint32_t cluster = fs_resolve_dir_cluster(dir_path);
    if (cluster == 0xFFFFFFFF) {
        spinlock_release(&fs_operation_lock);
        return ERR_NOT_FOUND;
    }

    if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_get_file_info_at(
            (uint16_t)cluster, index, name_out, size_out, attr_out);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        result = fat32_get_file_info_at(
            cluster, index, name_out, size_out, attr_out);
    } else {
        result = ERR_NOT_FOUND;
    }
    spinlock_release(&fs_operation_lock);
    return result;
}

int fs_create_dir_entry(const char* dir_path, const char* name, uint8_t attributes) {
    int result;
    int mutated = 0;

    spinlock_acquire(&fs_operation_lock);
    if (fs_stream_blocks_mutation_unlocked()) {
        spinlock_release(&fs_operation_lock);
        return ERR_STATE;
    }
    uint32_t cluster = fs_resolve_dir_cluster(dir_path);
    if (cluster == 0xFFFFFFFF) {
        spinlock_release(&fs_operation_lock);
        return ERR_NOT_FOUND;
    }

    if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_create_dir_entry(
            (uint16_t)cluster, name, attributes);
        mutated = result >= 0;
    } else if (current_fs_type == FS_TYPE_FAT32) {
        result = fat32_create_dir_entry(cluster, name, attributes);
        mutated = result >= 0;
    } else {
        result = ERR_NOT_FOUND;
    }
    if (mutated) fs_advance_generation_unlocked();
    spinlock_release(&fs_operation_lock);
    return result;
}

static int fs_write_file_in_dir_unlocked(const char* dir_path,
                                         const char* filename,
                                         const uint8_t* data,
                                         uint32_t size) {
    uint32_t cluster = fs_resolve_dir_cluster(dir_path);
    if (cluster == 0xFFFFFFFF) {
        LOG_ERROR("FS", "Diretorio de destino nao encontrado");
        return ERR_NOT_FOUND;
    }

    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_write_file_in_dir((uint16_t)cluster, filename, data, size);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_write_file_in_dir(cluster, filename, data, size);
    }
    LOG_ERROR("FS", "Filesystem indisponivel para escrita");
    return ERR_NOT_FOUND;
}

int fs_write_file_in_dir(const char* dir_path, const char* filename,
                         const uint8_t* data, uint32_t size) {
    uint32_t cluster;
    int result;
    int mutated = 0;

    spinlock_acquire(&fs_operation_lock);
    if (fs_stream_blocks_mutation_unlocked()) {
        result = ERR_STATE;
    } else if (current_fs_type == FS_TYPE_NONE) {
        result = ERR_NOT_FOUND;
    } else {
        cluster = fs_resolve_dir_cluster(dir_path);
        if ((current_fs_type == FS_TYPE_FAT12 &&
             cluster == FS_INVALID_CLUSTER) ||
            (current_fs_type == FS_TYPE_FAT32 &&
             cluster < FS_FIRST_DATA_CLUSTER)) {
            result = ERR_NOT_FOUND;
        } else if (current_fs_type == FS_TYPE_FAT12) {
            result = fat12_write_file_in_dir(
                (uint16_t)cluster, filename, data, size);
            mutated = result >= 0;
        } else {
            result = fat32_write_file_in_dir(cluster, filename, data, size);
            mutated = result >= 0;
        }
    }
    if (mutated) fs_advance_generation_unlocked();
    spinlock_release(&fs_operation_lock);
    if (!mutated && result != OK) {
        LOG_ERROR("FS", "Escrita em diretorio nao foi concluida");
    }
    return result;
}

int fs_delete_file_in_dir(const char* dir_path, const char* filename) {
    int result;
    int mutated = 0;

    spinlock_acquire(&fs_operation_lock);
    if (fs_stream_blocks_mutation_unlocked()) {
        spinlock_release(&fs_operation_lock);
        return ERR_STATE;
    }
    uint32_t cluster = fs_resolve_dir_cluster(dir_path);
    if (cluster == 0xFFFFFFFF) {
        spinlock_release(&fs_operation_lock);
        return ERR_NOT_FOUND;
    }

    if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_delete_file_in_dir((uint16_t)cluster, filename);
        mutated = result >= 0;
    } else if (current_fs_type == FS_TYPE_FAT32) {
        result = fat32_delete_file_in_dir(cluster, filename);
        mutated = result >= 0;
    } else {
        result = ERR_NOT_FOUND;
    }
    if (mutated) fs_advance_generation_unlocked();
    spinlock_release(&fs_operation_lock);
    return result;
}

int fs_rename_file_in_dir(const char* dir_path, const char* old_name,
                          const char* new_name) {
    uint32_t cluster;
    int result;

    if (!dir_path || !old_name || !new_name) {
        LOG_ERROR("FS", "Argumento nulo na renomeacao");
        return ERR_NULL;
    }
    spinlock_acquire(&fs_operation_lock);
    if (fs_stream_blocks_mutation_unlocked()) {
        result = ERR_STATE;
    } else {
        cluster = fs_resolve_dir_cluster(dir_path);
        if (cluster == FS_INVALID_CLUSTER) {
            result = ERR_NOT_FOUND;
        } else if (current_fs_type == FS_TYPE_FAT12) {
            result = fat12_rename_file_in_dir(
                (uint16_t)cluster, old_name, new_name);
        } else {
            result = ERR_UNAVAILABLE;
        }
    }
    if (result == OK) fs_advance_generation_unlocked();
    spinlock_release(&fs_operation_lock);
    if (result != OK) LOG_ERROR("FS", "Renomeacao de arquivo falhou");
    return result;
}

int fs_get_root_file_info(const char* filename, uint32_t* size_out,
                          uint8_t* attributes_out) {
    int result;

    if (!filename) {
        LOG_ERROR("FS", "Nome nulo na consulta atomica");
        return ERR_NULL;
    }
    spinlock_acquire(&fs_operation_lock);
    if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_get_root_file_info(
            filename, size_out, attributes_out);
    } else {
        result = ERR_UNAVAILABLE;
    }
    spinlock_release(&fs_operation_lock);
    return result;
}

int fs_atomic_write_root(const char* filename, const uint8_t* data,
                         uint32_t size, uint8_t attributes,
                         fs_atomic_mode_t mode) {
    int result;

    if (!filename || !data) {
        LOG_ERROR("FS", "Argumento nulo na escrita atomica");
        return ERR_NULL;
    }
    if (size == 0U || size > FS_MAX_ATOMIC_FILE_SIZE) {
        LOG_ERROR("FS", "Tamanho invalido na escrita atomica");
        return ERR_OVERFLOW;
    }
    if (mode != FS_ATOMIC_CREATE_OR_REPLACE &&
        mode != FS_ATOMIC_REPLACE_ONLY) {
        LOG_ERROR("FS", "Modo invalido na escrita atomica");
        return ERR_INVALID;
    }

    spinlock_acquire(&fs_operation_lock);
    if (fs_stream_blocks_mutation_unlocked()) {
        result = ERR_STATE;
    } else if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_atomic_write_root(
            filename, data, size, attributes,
            mode == FS_ATOMIC_REPLACE_ONLY);
    } else {
        result = ERR_UNAVAILABLE;
    }
    if (result == OK) fs_advance_generation_unlocked();
    spinlock_release(&fs_operation_lock);
    if (result != OK) LOG_ERROR("FS", "Escrita atomica FAT12 falhou");
    return result;
}

int fs_atomic_delete_root(const char* filename) {
    int result;

    if (!filename) {
        LOG_ERROR("FS", "Nome nulo na exclusao atomica");
        return ERR_NULL;
    }
    spinlock_acquire(&fs_operation_lock);
    if (fs_stream_blocks_mutation_unlocked()) {
        result = ERR_STATE;
    } else if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_atomic_delete_root(filename);
    } else {
        result = ERR_UNAVAILABLE;
    }
    if (result == OK) fs_advance_generation_unlocked();
    spinlock_release(&fs_operation_lock);
    if (result != OK && result != ERR_NOT_FOUND) {
        LOG_ERROR("FS", "Exclusao atomica FAT12 falhou");
    }
    return result;
}

int fs_atomic_write_file_in_dir(const char* dir_path, const char* filename,
                                const uint8_t* data, uint32_t size,
                                uint8_t attributes, fs_atomic_mode_t mode) {
    uint32_t cluster;
    int result;

    if (!dir_path || !filename || !data) {
        LOG_ERROR("FS", "Argumento nulo na escrita atomica em diretorio");
        return ERR_NULL;
    }
    if (size == 0U || size > FS_MAX_ATOMIC_FILE_SIZE) {
        LOG_ERROR("FS", "Tamanho invalido na escrita atomica em diretorio");
        return ERR_OVERFLOW;
    }
    if (mode != FS_ATOMIC_CREATE_OR_REPLACE &&
        mode != FS_ATOMIC_REPLACE_ONLY) {
        LOG_ERROR("FS", "Modo invalido na escrita atomica em diretorio");
        return ERR_INVALID;
    }
    spinlock_acquire(&fs_operation_lock);
    cluster = fs_resolve_dir_cluster(dir_path);
    if (fs_stream_blocks_mutation_unlocked()) {
        result = ERR_STATE;
    } else if (cluster == 0xFFFFFFFFU) {
        result = ERR_NOT_FOUND;
    } else if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_atomic_write_file_in_dir(
            (uint16_t)cluster, filename, data, size, attributes,
            mode == FS_ATOMIC_REPLACE_ONLY);
    } else {
        result = ERR_UNAVAILABLE;
    }
    if (result == OK) fs_advance_generation_unlocked();
    spinlock_release(&fs_operation_lock);
    if (result != OK) LOG_ERROR("FS", "Escrita atomica em diretorio falhou");
    return result;
}

int fs_atomic_delete_file_in_dir(const char* dir_path, const char* filename) {
    uint32_t cluster;
    int result;

    if (!dir_path || !filename) {
        LOG_ERROR("FS", "Argumento nulo na exclusao atomica em diretorio");
        return ERR_NULL;
    }
    spinlock_acquire(&fs_operation_lock);
    cluster = fs_resolve_dir_cluster(dir_path);
    if (fs_stream_blocks_mutation_unlocked()) {
        result = ERR_STATE;
    } else if (cluster == 0xFFFFFFFFU) {
        result = ERR_NOT_FOUND;
    } else if (current_fs_type == FS_TYPE_FAT12 && cluster == 0U) {
        result = fat12_atomic_delete_root(filename);
    } else if (current_fs_type == FS_TYPE_FAT12) {
        result = fat12_atomic_delete_file_in_dir((uint16_t)cluster, filename);
    } else {
        result = ERR_UNAVAILABLE;
    }
    if (result == OK) fs_advance_generation_unlocked();
    spinlock_release(&fs_operation_lock);
    if (result != OK && result != ERR_NOT_FOUND) {
        LOG_ERROR("FS", "Exclusao atomica em diretorio falhou");
    }
    return result;
}

int fs_stream_begin_root(const char* filename, uint32_t expected_size,
                         uint8_t attributes) {
    int result;

    if (!filename) {
        LOG_ERROR("FS", "Nome nulo no inicio streaming");
        return ERR_NULL;
    }
    if (!expected_size || expected_size > FS_MAX_STREAM_FILE_SIZE) {
        LOG_ERROR("FS", "Tamanho invalido no inicio streaming");
        return ERR_OVERFLOW;
    }
    spinlock_acquire(&fs_operation_lock);
    result = current_fs_type == FS_TYPE_FAT12 ?
             fat12_stream_begin_root(
                 filename, expected_size, attributes) :
             ERR_UNAVAILABLE;
    spinlock_release(&fs_operation_lock);
    if (result != OK) LOG_ERROR("FS", "Inicio streaming FAT12 falhou");
    return result;
}

int fs_stream_write_root(const uint8_t* data, uint32_t size) {
    int result;

    if (!data && size) {
        LOG_ERROR("FS", "Dados nulos na escrita streaming");
        return ERR_NULL;
    }
    spinlock_acquire(&fs_operation_lock);
    result = current_fs_type == FS_TYPE_FAT12 ?
             fat12_stream_write_root(data, size) : ERR_UNAVAILABLE;
    spinlock_release(&fs_operation_lock);
    if (result != OK) LOG_ERROR("FS", "Escrita streaming FAT12 falhou");
    return result;
}

int fs_stream_finish_root(void) {
    int result;

    spinlock_acquire(&fs_operation_lock);
    result = current_fs_type == FS_TYPE_FAT12 ?
             fat12_stream_finish_root() : ERR_UNAVAILABLE;
    if (result == OK) fs_advance_generation_unlocked();
    spinlock_release(&fs_operation_lock);
    if (result != OK) LOG_ERROR("FS", "Finalizacao streaming FAT12 falhou");
    return result;
}

int fs_stream_abort_root(void) {
    int result;

    spinlock_acquire(&fs_operation_lock);
    result = current_fs_type == FS_TYPE_FAT12 ?
             fat12_stream_abort_root() : ERR_UNAVAILABLE;
    spinlock_release(&fs_operation_lock);
    if (result != OK) LOG_ERROR("FS", "Cancelamento streaming FAT12 falhou");
    return result;
}

int fs_stream_is_active(void) {
    int active;

    spinlock_acquire(&fs_operation_lock);
    active = current_fs_type == FS_TYPE_FAT12 ?
             fat12_stream_is_active() : 0;
    spinlock_release(&fs_operation_lock);
    return active;
}
