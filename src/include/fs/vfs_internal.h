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
int vfs_stream_read(file_t* file, void* buffer, uint32_t size,
                    uint32_t* bytes_read);
int vfs_stream_write(file_t* file, const void* buffer, uint32_t size,
                     uint32_t* bytes_written);

#endif
