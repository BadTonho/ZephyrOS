#ifndef FAT32_H
#define FAT32_H

#include "types.h"
#include "drivers/ata.h"

#define FAT32_CLUSTER_FREE      0x00000000
#define FAT32_CLUSTER_RESERVED  0x0FFFFFF0
#define FAT32_CLUSTER_BAD       0x0FFFFFF7
#define FAT32_CLUSTER_END       0x0FFFFFF8

#pragma pack(push, 1)
typedef struct {
    uint8_t  boot_jump[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_small;
    uint8_t  media_type;
    uint16_t sectors_per_fat_small;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_large;
    uint32_t sectors_per_fat;
    uint16_t extended_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved_nt;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     filesystem[8];
} __attribute__((packed)) fat32_bpb_t;

typedef struct {
    char     name[8];
    char     ext[3];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  creation_time_tenth;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t modification_time;
    uint16_t modification_date;
    uint16_t cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat32_dir_entry_t;
#pragma pack(pop)

typedef struct {
    fat32_bpb_t bpb;
    uint32_t fat_start;
    uint32_t data_start;
    uint32_t* fat;
    uint32_t* root_cache;
    uint32_t root_cluster;
    uint32_t cluster_size;
    uint32_t total_clusters;
    int initialized;
} fat32_fs_t;

int  fat32_init(void);
int  fat32_read_file(const char* filename, uint8_t* buffer, uint32_t max_size);
int  fat32_write_file(const char* filename, const uint8_t* data, uint32_t size);
int  fat32_delete_file(const char* filename);
int  fat32_list_dir(void);
int  fat32_get_file_count(void);
int  fat32_get_file_info(int index, char* name_out, uint32_t* size_out, uint8_t* attr_out);
fat32_fs_t* fat32_get_fs(void);

int  fat32_read_file_at(const char* path, uint8_t* buffer, uint32_t max_size);
int  fat32_get_file_count_at(uint32_t dir_cluster);
int  fat32_get_file_info_at(uint32_t dir_cluster, int index, char* name_out, uint32_t* size_out, uint8_t* attr_out);
uint32_t fat32_resolve_path(const char* path);
int  fat32_create_dir_entry(uint32_t dir_cluster, const char* name, uint8_t attributes);
int  fat32_write_file_in_dir(uint32_t dir_cluster, const char* filename, const uint8_t* data, uint32_t size);
int  fat32_delete_file_in_dir(uint32_t dir_cluster, const char* filename);

#endif
