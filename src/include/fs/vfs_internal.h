#ifndef VFS_INTERNAL_H
#define VFS_INTERNAL_H

#include "types.h"
#include "fs/vfs.h"

int vfs_path_init(void);
int vfs_resolve_open_path(const char* path, uint32_t mode,
                          vfs_lookup_result_t* result);
int vfs_mount_acquire(uint32_t slot, uint32_t generation);
void vfs_mount_release(uint32_t slot, uint32_t generation);
int vfs_mount_validate_reference(uint32_t slot, uint32_t generation);
int vfs_path_validate_state(void);
void vfs_path_get_metrics(uint32_t* capacity, uint32_t* active,
                          uint32_t* lookups, uint32_t* chdirs);

#endif
