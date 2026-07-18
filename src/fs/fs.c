#include "fs.h"
#include "fat12.h"
#include "fat32.h"
#include "video.h"
#include "panic.h"

static uint8_t current_fs_type = FS_TYPE_NONE;

void fs_init(void) {
    fat12_init();
    if (fat12_get_fs()->initialized) {
        current_fs_type = FS_TYPE_FAT12;
        video_print("Sistema de arquivos: FAT12\n", 0x0A);
        return;
    }

    fat32_init();
    if (fat32_get_fs()->initialized) {
        current_fs_type = FS_TYPE_FAT32;
        video_print("Sistema de arquivos: FAT32\n", 0x0A);
        return;
    }

    video_print("Nenhum sistema de arquivos encontrado!\n", 0x0C);
}

int fs_read_file(const char* filename, uint8_t* buffer, uint32_t max_size) {
    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_read_file(filename, buffer, max_size);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_read_file(filename, buffer, max_size);
    }
    return -1;
}

int fs_write_file(const char* filename, const uint8_t* data, uint32_t size) {
    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_write_file(filename, data, size);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_write_file(filename, data, size);
    }
    return -1;
}

int fs_delete_file(const char* filename) {
    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_delete_file(filename);
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_delete_file(filename);
    }
    return -1;
}

int fs_list_dir(void) {
    if (current_fs_type == FS_TYPE_FAT12) {
        return fat12_list_dir();
    } else if (current_fs_type == FS_TYPE_FAT32) {
        return fat32_list_dir();
    }
    return -1;
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
    return -1;
}

int fs_get_info(fs_info_t* info) {
    if (!info) return -1;

    info->type = current_fs_type;

    if (current_fs_type == FS_TYPE_FAT12) {
        fat12_fs_t* fs = fat12_get_fs();
        info->bytes_per_sector = fs->bpb.bytes_per_sector;
        info->sectors_per_cluster = fs->bpb.sectors_per_cluster;
        info->total_sectors = fs->bpb.total_sectors;
        info->free_sectors = 0;
        info->total_clusters = 0;
        info->free_clusters = 0;
        memcpy(info->label, fs->bpb.volume_label, 11);
        info->label[11] = '\0';
    } else if (current_fs_type == FS_TYPE_FAT32) {
        fat32_fs_t* fs = fat32_get_fs();
        info->bytes_per_sector = fs->bpb.bytes_per_sector;
        info->sectors_per_cluster = fs->bpb.sectors_per_cluster;
        info->total_sectors = fs->bpb.total_sectors_large;
        info->free_sectors = 0;
        info->total_clusters = fs->bpb.sectors_per_fat * 512 / 4;
        info->free_clusters = 0;
        memcpy(info->label, fs->bpb.volume_label, 11);
        info->label[11] = '\0';
    } else {
        return -1;
    }

    return 0;
}

uint8_t fs_get_type(void) {
    return current_fs_type;
}
