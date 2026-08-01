#ifndef FS_H
#define FS_H

#include "types.h"

#define FS_TYPE_NONE   0
#define FS_TYPE_FAT12  1
#define FS_TYPE_FAT32  2

#define FS_MAX_FILENAME 256
#define FS_MAX_PATH     256
#define FS_MAX_ATOMIC_FILE_SIZE (64U * 1024U)
#define FS_MAX_STREAM_FILE_SIZE (128U * 1024U)

#define FS_ATTRIBUTE_HIDDEN     0x02U
#define FS_ATTRIBUTE_SYSTEM     0x04U
#define FS_ATTRIBUTE_ARCHIVE    0x20U
#define FS_ATTRIBUTES_PRESERVE  0xFFU

typedef enum {
    FS_ATOMIC_CREATE_OR_REPLACE = 0,
    FS_ATOMIC_REPLACE_ONLY = 1
} fs_atomic_mode_t;

typedef struct {
    uint32_t size;
    uint32_t cluster;
    uint8_t  attributes;
    char     name[13];
} fs_file_info_t;

typedef struct {
    uint32_t total_sectors;
    uint32_t free_sectors;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t total_clusters;
    uint32_t free_clusters;
    uint8_t  type;
    char     label[12];
} fs_info_t;

int      fs_init(void);
int      fs_read_file(const char* filename, uint8_t* buffer, uint32_t max_size);
int      fs_write_file(const char* filename, const uint8_t* data, uint32_t size);
int      fs_delete_file(const char* filename);
int      fs_list_dir(void);
int      fs_get_file_count(void);
int      fs_get_file_info(int index, char* name_out, uint32_t* size_out, uint8_t* attr_out);
int      fs_get_info(fs_info_t* info);
uint8_t  fs_get_type(void);

int      fs_read_file_at(const char* path, uint8_t* buffer, uint32_t max_size);
int      fs_read_file_range_at(const char* path, uint32_t offset,
                               uint8_t* buffer, uint32_t max_size,
                               uint32_t* bytes_read);
int      fs_write_file_at(const char* path, const uint8_t* data, uint32_t size);
int      fs_get_file_count_at(const char* dir_path);
int      fs_get_file_info_at(const char* dir_path, int index, char* name_out, uint32_t* size_out, uint8_t* attr_out);
int      fs_create_dir_entry(const char* dir_path, const char* name, uint8_t attributes);
int      fs_write_file_in_dir(const char* dir_path, const char* filename, const uint8_t* data, uint32_t size);
int      fs_delete_file_in_dir(const char* dir_path, const char* filename);
int      fs_get_root_file_info(const char* filename, uint32_t* size_out,
                               uint8_t* attributes_out);
int      fs_atomic_write_root(const char* filename, const uint8_t* data,
                              uint32_t size, uint8_t attributes,
                              fs_atomic_mode_t mode);
int      fs_atomic_delete_root(const char* filename);
int      fs_atomic_write_file_in_dir(const char* dir_path,
                                     const char* filename,
                                     const uint8_t* data, uint32_t size,
                                     uint8_t attributes,
                                     fs_atomic_mode_t mode);
int      fs_atomic_delete_file_in_dir(const char* dir_path,
                                      const char* filename);
int      fs_stream_begin_root(const char* filename, uint32_t expected_size,
                              uint8_t attributes);
int      fs_stream_write_root(const uint8_t* data, uint32_t size);
int      fs_stream_finish_root(void);
int      fs_stream_abort_root(void);
int      fs_stream_is_active(void);

#endif
