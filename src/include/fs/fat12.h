#ifndef FAT12_H
#define FAT12_H

#include "types.h"
#include "drivers/ata.h"

#define FAT12_CLUSTER_FREE      0x000
#define FAT12_CLUSTER_RESERVED  0xFF0
#define FAT12_CLUSTER_BAD       0xFF7
#define FAT12_CLUSTER_END       0xFF8

typedef struct {
    uint8_t  boot_jump[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors;
    uint8_t  media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t large_sector_count;
    uint8_t  drive_number;
    uint8_t  reserved;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     filesystem[8];
} __attribute__((packed)) fat12_bpb_t;

typedef struct {
    char     name[8];
    char     ext[3];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t modification_time;
    uint16_t modification_date;
    uint16_t cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat12_dir_entry_t;

typedef struct {
    fat12_bpb_t bpb;
    uint32_t fat_start;
    uint32_t root_start;
    uint32_t data_start;
    uint16_t* fat;
    fat12_dir_entry_t* root_dir;
    uint8_t* data_area;
    int initialized;
} fat12_fs_t;

int  fat12_init(void);
int  fat12_read_file(const char* filename, uint8_t* buffer, uint32_t max_size);
int  fat12_write_file(const char* filename, const uint8_t* data, uint32_t size);
int  fat12_delete_file(const char* filename);
int  fat12_list_dir(void);
fat12_fs_t* fat12_get_fs(void);

int  fat12_get_file_count(void);
int  fat12_get_file_info(int index, char* name_out, uint32_t* size_out, uint8_t* attr_out);

int  fat12_read_file_at(const char* path, uint8_t* buffer, uint32_t max_size);
int  fat12_get_file_count_at(uint16_t dir_cluster);
int  fat12_get_file_info_at(uint16_t dir_cluster, int index, char* name_out, uint32_t* size_out, uint8_t* attr_out);
uint16_t fat12_resolve_path(const char* path);
int  fat12_create_dir_entry(uint16_t dir_cluster, const char* name, uint8_t attributes);
int  fat12_write_file_in_dir(uint16_t dir_cluster, const char* filename, const uint8_t* data, uint32_t size);
int  fat12_delete_file_in_dir(uint16_t dir_cluster, const char* filename);

#endif
