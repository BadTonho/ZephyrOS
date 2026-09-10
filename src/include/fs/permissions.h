#ifndef FS_PERMISSIONS_H
#define FS_PERMISSIONS_H

#include "types.h"
#include "fs/vfs.h"
#include "process/credentials.h"

#define FS_PERMISSION_FILE_DEFAULT 0664U
#define FS_PERMISSION_DIRECTORY_DEFAULT 0775U
#define FS_PERMISSION_LEGACY_FILE 0644U
#define FS_PERMISSION_LEGACY_DIRECTORY 0755U
#define FS_PERMISSION_MAX_RECORDS 128U
#define FS_PERMISSION_SIDECAR_NAME "ZPERM.DAT"
#define FS_PERMISSION_SIDECAR_MAGIC 0x5A504552U
#define FS_PERMISSION_SIDECAR_VERSION 1U
#define FS_PERMISSION_SIDECAR_HEADER_SIZE 20U
#define FS_PERMISSION_SIDECAR_RECORD_SIZE 268U
#define FS_PERMISSION_SIDECAR_MAX_SIZE \
    (FS_PERMISSION_SIDECAR_HEADER_SIZE + \
     FS_PERMISSION_MAX_RECORDS * FS_PERMISSION_SIDECAR_RECORD_SIZE)

typedef enum {
    FS_PERMISSION_ACCESS_READ = 1U << 0,
    FS_PERMISSION_ACCESS_WRITE = 1U << 1,
    FS_PERMISSION_ACCESS_EXECUTE = 1U << 2,
    FS_PERMISSION_ACCESS_LIST = 1U << 3,
    FS_PERMISSION_ACCESS_CREATE = 1U << 4,
    FS_PERMISSION_ACCESS_SYNC = 1U << 5,
    FS_PERMISSION_ACCESS_IOCTL = 1U << 6
} fs_permission_access_t;

#define FS_PERMISSION_ACCESS_ALL \
    (FS_PERMISSION_ACCESS_READ | FS_PERMISSION_ACCESS_WRITE | \
     FS_PERMISSION_ACCESS_EXECUTE | FS_PERMISSION_ACCESS_LIST | \
     FS_PERMISSION_ACCESS_CREATE | FS_PERMISSION_ACCESS_SYNC | \
     FS_PERMISSION_ACCESS_IOCTL)

typedef struct {
    uint32_t uid;
    uint32_t gid;
    uint16_t mode;
    vfs_node_type_t type;
    uint8_t present;
} fs_permission_info_t;

int fs_permissions_init(void);
int fs_permissions_path_compare(const char* left, const char* right);
int fs_permissions_prepare_volume(const char* volume_id);
int fs_permissions_prepare_path(const char* volume_id,
                                const char* relative_path);
int fs_permissions_apply_lookup(vfs_lookup_result_t* lookup);
int fs_permissions_check_lookup(const process_credentials_t* credentials,
                                const vfs_lookup_result_t* lookup,
                                uint32_t access);
int fs_permissions_check_traversal(const process_credentials_t* credentials,
                                    const vfs_lookup_result_t* lookup);
int fs_permissions_check_create(const process_credentials_t* credentials,
                                const vfs_lookup_result_t* lookup);
int fs_permissions_register_path(const char* volume_id,
                                 const char* relative_path,
                                 vfs_node_type_t type);
int fs_permissions_remove_path(const char* volume_id,
                               const char* relative_path);
int fs_permissions_rename_path(const char* volume_id,
                               const char* relative_path,
                               const char* new_name);
int fs_permissions_validate(void);
const char* fs_permission_access_name(uint32_t access);

#endif
