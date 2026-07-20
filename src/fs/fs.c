#include "fs/fs.h"
#include "fs/fat12.h"
#include "fs/fat32.h"
#include "core/video.h"
#include "core/log.h"
#include "core/errors.h"
#include "core/string.h"

static uint8_t current_fs_type = FS_TYPE_NONE;

int fs_init(void) {
    current_fs_type = FS_TYPE_NONE;

    int fat12_result = fat12_init();
    if (fat12_result == OK && fat12_get_fs()->initialized) {
        current_fs_type = FS_TYPE_FAT12;
        video_print("Sistema de arquivos: FAT12\n", 0x0A);
        return OK;
    }

    int fat32_result = fat32_init();
    if (fat32_result == OK && fat32_get_fs()->initialized) {
        current_fs_type = FS_TYPE_FAT32;
        video_print("Sistema de arquivos: FAT32\n", 0x0A);
        return OK;
    }

    LOG_WARN("FS", "Nenhum sistema de arquivos montado");
    video_print("Nenhum sistema de arquivos encontrado!\n", 0x0C);
    if (fat12_result == ERR_DISK || fat32_result == ERR_DISK) return ERR_DISK;
    return ERR_NOT_FOUND;
}

int fs_read_file(const char* filename, uint8_t* buffer, uint32_t max_size) {
    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_read_file(filename, buffer, max_size);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_read_file(filename, buffer, max_size);
    }
    return ERR_NOT_FOUND;
}

int fs_write_file(const char* filename, const uint8_t* data, uint32_t size) {
    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_write_file(filename, data, size);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_write_file(filename, data, size);
    }
    return ERR_NOT_FOUND;
}

int fs_delete_file(const char* filename) {
    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_delete_file(filename);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_delete_file(filename);
    }
    return ERR_NOT_FOUND;
}

int fs_list_dir(void) {
    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_list_dir();
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_list_dir();
    }
    return ERR_NOT_FOUND;
}

int fs_get_file_count(void) {
    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_get_file_count();
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_get_file_count();
    }
    return 0;
}

int fs_get_file_info(int index, char* name_out, uint32_t* size_out, uint8_t* attr_out) {
    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_get_file_info(index, name_out, size_out, attr_out);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_get_file_info(index, name_out, size_out, attr_out);
    }
    return ERR_NOT_FOUND;
}

int fs_get_info(fs_info_t* info) {
    if (!info) return ERR_NULL;

    info->type = current_fs_type;

    if (current_fs_type == FS_TYPE_FAT12) {
        fat12_fs_t* fs = fat12_get_fs();
        info->bytes_per_sector = fs->bpb.bytes_per_sector;
        info->sectors_per_cluster = fs->bpb.sectors_per_cluster;
        info->total_sectors = fs->bpb.total_sectors;
        info->free_sectors = 0;
        info->total_clusters = 0;
        info->free_clusters = 0;
        kmemcpy(info->label, fs->bpb.volume_label, 11);
        info->label[11] = '\0';
    } else if (current_fs_type == FS_TYPE_FAT32) {
        fat32_fs_t* fs = fat32_get_fs();
        info->bytes_per_sector = fs->bpb.bytes_per_sector;
        info->sectors_per_cluster = fs->bpb.sectors_per_cluster;
        info->total_sectors = fs->bpb.total_sectors_large;
        info->free_sectors = 0;
        info->total_clusters = fs->bpb.sectors_per_fat * 512 / 4;
        info->free_clusters = 0;
        kmemcpy(info->label, fs->bpb.volume_label, 11);
        info->label[11] = '\0';
    } else {
        return ERR_NOT_FOUND;
    }

    return OK;
}

uint8_t fs_get_type(void) {
    return current_fs_type;
}

static uint32_t fs_resolve_dir_cluster(const char* dir_path) {
    if (!dir_path || dir_path[0] == '\0') {
        if (current_fs_type == FS_TYPE_FAT12) return 0;
        if (current_fs_type == FS_TYPE_FAT32) return fat32_get_fs()->root_cluster;
        return 0;
    }

    if (current_fs_type == FS_TYPE_FAT12) {
        return (uint32_t)fat12_resolve_path(dir_path);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_resolve_path(dir_path);
    }
    return 0;
}

int fs_read_file_at(const char* path, uint8_t* buffer, uint32_t max_size) {
    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_read_file_at(path, buffer, max_size);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_read_file_at(path, buffer, max_size);
    }
    return ERR_NOT_FOUND;
}

int fs_get_file_count_at(const char* dir_path) {
    uint32_t cluster = fs_resolve_dir_cluster(dir_path);

    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_get_file_count_at((uint16_t)cluster);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_get_file_count_at(cluster);
    }
    return 0;
}

int fs_get_file_info_at(const char* dir_path, int index, char* name_out, uint32_t* size_out, uint8_t* attr_out) {
    uint32_t cluster = fs_resolve_dir_cluster(dir_path);

    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_get_file_info_at((uint16_t)cluster, index, name_out, size_out, attr_out);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_get_file_info_at(cluster, index, name_out, size_out, attr_out);
    }
    return ERR_NOT_FOUND;
}

int fs_create_dir_entry(const char* dir_path, const char* name, uint8_t attributes) {
    uint32_t cluster = fs_resolve_dir_cluster(dir_path);

    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_create_dir_entry((uint16_t)cluster, name, attributes);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_create_dir_entry(cluster, name, attributes);
    }
    return ERR_NOT_FOUND;
}

int fs_write_file_in_dir(const char* dir_path, const char* filename, const uint8_t* data, uint32_t size) {
    uint32_t cluster = fs_resolve_dir_cluster(dir_path);

    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_write_file_in_dir((uint16_t)cluster, filename, data, size);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_write_file_in_dir(cluster, filename, data, size);
    }
    return ERR_NOT_FOUND;
}

int fs_delete_file_in_dir(const char* dir_path, const char* filename) {
    uint32_t cluster = fs_resolve_dir_cluster(dir_path);

    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_delete_file_in_dir((uint16_t)cluster, filename);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_delete_file_in_dir(cluster, filename);
    }
    return ERR_NOT_FOUND;
}
