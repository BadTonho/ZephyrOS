#ifndef STORAGE_H
#define STORAGE_H

#include "types.h"
#include "fs/block.h"

#define STORAGE_MAX_DISKS BLOCK_MAX_DEVICES
#define STORAGE_MAX_VOLUMES 16U
#define STORAGE_MAX_MOUNTS 4U
#define STORAGE_ID_SIZE BLOCK_DEVICE_ID_SIZE
#define STORAGE_DISK_ID_SIZE BLOCK_DEVICE_ID_SIZE
#define STORAGE_LABEL_SIZE 12U
#define STORAGE_MODEL_SIZE 41U
#define STORAGE_NAME_SIZE 13U
#define STORAGE_MAX_PATH 256U
#define STORAGE_MAX_DIR_ENTRIES 64U
#define STORAGE_MAX_CHAIN_STEPS 4096U
#define STORAGE_MAX_VIEW_BYTES 4095U
#define STORAGE_SECTOR_SIZE 512U

typedef enum {
    STORAGE_LAYOUT_RAW = 0,
    STORAGE_LAYOUT_MBR
} storage_layout_t;

typedef enum {
    STORAGE_FS_NONE = 0,
    STORAGE_FS_FAT12,
    STORAGE_FS_FAT32
} storage_fs_type_t;

typedef enum {
    STORAGE_VOLUME_DETECTED = 0,
    STORAGE_VOLUME_INVALID,
    STORAGE_VOLUME_UNSUPPORTED,
    STORAGE_VOLUME_MOUNTED
} storage_volume_state_t;

typedef enum {
    STORAGE_DISK_ATA = 0,
    STORAGE_DISK_USB_MSC
} storage_disk_kind_t;

typedef struct {
    char id[STORAGE_DISK_ID_SIZE];
    char model[STORAGE_MODEL_SIZE];
    storage_disk_kind_t kind;
    uint16_t sector_size;
    uint8_t read_only;
    uint8_t slot;
    uint8_t channel;
    uint8_t slave;
    uint32_t sector_count;
    uint32_t read_ops;
    uint32_t write_ops;
    int last_error;
} storage_disk_t;

typedef struct {
    char id[STORAGE_ID_SIZE];
    char disk_id[STORAGE_DISK_ID_SIZE];
    char label[STORAGE_LABEL_SIZE];
    storage_layout_t layout;
    storage_fs_type_t fs_type;
    storage_volume_state_t state;
    uint8_t disk_slot;
    uint8_t partition_index;
    uint8_t partition_type;
    uint8_t mounted;
    uint8_t boot;
    uint8_t read_only;
    uint8_t pinned;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint32_t start_lba;
    uint32_t sector_count;
    uint32_t generation;
    int last_error;
} storage_volume_t;

typedef struct {
    char name[STORAGE_NAME_SIZE];
    uint32_t size;
    uint32_t cluster;
    uint8_t attributes;
    uint8_t is_directory;
} storage_dir_entry_t;

typedef struct {
    char volume_id[STORAGE_ID_SIZE];
    uint32_t volume_generation;
    uint32_t directory_cluster;
    uint32_t current_cluster;
    uint32_t sector_index;
    uint32_t entry_index;
    uint32_t chain_steps;
    storage_fs_type_t fs_type;
    uint8_t fixed_root;
    uint8_t sector_loaded;
    uint8_t active;
    uint8_t done;
    uint8_t sector[STORAGE_SECTOR_SIZE];
} storage_dir_cursor_t;

typedef struct {
    uint8_t initialized;
    uint8_t disk_count;
    uint8_t volume_count;
    uint8_t mounted_count;
    int last_error;
} storage_status_t;

int storage_init(void);
int storage_refresh(void);
int storage_get_status(storage_status_t* out_status);
int storage_get_disk_at(uint8_t index, storage_disk_t* out_disk);
int storage_find_disk(const char* id, storage_disk_t* out_disk);
int storage_get_volume_at(uint8_t index, storage_volume_t* out_volume);
int storage_get_mounted_at(uint8_t index, storage_volume_t* out_volume);
int storage_find_volume(const char* id, storage_volume_t* out_volume);
int storage_mount(const char* id);
int storage_unmount(const char* id);
int storage_list_dir(const char* id, const char* path,
                     storage_dir_entry_t* entries, uint32_t capacity,
                     uint32_t* out_count);
int storage_dir_cursor_open(const char* id, const char* path,
                            storage_dir_cursor_t* cursor);
int storage_dir_cursor_next(storage_dir_cursor_t* cursor,
                            storage_dir_entry_t* out_entry,
                            uint8_t* out_found, uint8_t* out_done);
int storage_read_file_range(const char* id, const char* path,
                            uint32_t offset, uint8_t* buffer,
                            uint32_t max_size, uint32_t* out_read);
const char* storage_fs_name(storage_fs_type_t type);
const char* storage_volume_state_name(storage_volume_state_t state);

#endif
