#ifndef VFS_H
#define VFS_H

#include "types.h"

#define VFS_MAX_FDS 32U
#define VFS_MAX_OPEN_FILES 32U
#define VFS_MAX_PATH 256U

#define VFS_FD_STDIN  0
#define VFS_FD_STDOUT 1
#define VFS_FD_STDERR 2
#define VFS_FD_FIRST_FILE 3
#define VFS_FD_INVALID (-1)

#define VFS_MODE_READ       1U
#define VFS_MODE_WRITE      2U
#define VFS_MODE_READ_WRITE 3U

#define VFS_SEEK_SET 0U
#define VFS_SEEK_CUR 1U
#define VFS_SEEK_END 2U

typedef enum {
    VFS_NODE_NONE = 0,
    VFS_NODE_REGULAR,
    VFS_NODE_STDIN,
    VFS_NODE_STDOUT,
    VFS_NODE_STDERR,
    VFS_NODE_TEST
} vfs_node_type_t;

typedef struct vfs_vnode vnode_t;
typedef struct vfs_file file_t;

typedef struct {
    int (*open)(vnode_t* vnode, file_t* file);
    int (*read)(file_t* file, void* buffer, uint32_t size,
                uint32_t* bytes_read);
    int (*write)(file_t* file, const void* buffer, uint32_t size,
                 uint32_t* bytes_written);
    int (*close)(file_t* file);
    int (*lseek)(file_t* file, int32_t offset, uint32_t whence,
                 uint32_t* position);
    int (*ioctl)(file_t* file, uint32_t request, void* argument);
} file_operations_t;

struct vfs_vnode {
    vfs_node_type_t type;
    const file_operations_t* operations;
    void* private_data;
    uint32_t size;
    char path[VFS_MAX_PATH];
};

struct vfs_file {
    vnode_t* vnode;
    uint32_t mode;
    uint32_t offset;
    uint32_t slot;
    uint32_t active_operations;
    uint8_t used;
    uint8_t persistent;
};

typedef struct {
    file_t* entries[VFS_MAX_FDS];
    file_t standard_files[3];
    uint8_t initialized;
} vfs_fd_table_t;

typedef struct {
    uint32_t initialized;
    uint32_t descriptor_capacity;
    uint32_t global_file_capacity;
    uint32_t global_files_used;
    uint32_t processes_with_tables;
    uint32_t descriptors_open;
    uint32_t opens;
    uint32_t reads;
    uint32_t writes;
    uint32_t seeks;
    uint32_t closes;
    uint32_t failures;
} vfs_status_t;

typedef struct {
    uint32_t pid;
    int32_t fd;
    vfs_node_type_t type;
    uint32_t mode;
    uint32_t offset;
    uint32_t size;
    char path[VFS_MAX_PATH];
} vfs_descriptor_info_t;

typedef struct {
    uint8_t stdio;
    uint8_t lifecycle;
    uint8_t sequential_read;
    uint8_t seek;
    uint8_t permissions;
    uint8_t eof;
    uint8_t limits;
    uint8_t table_capacity;
    uint8_t closed_descriptor;
    uint8_t isolation;
    uint8_t cleanup;
    uint8_t invariants;
    uint32_t passed;
    uint32_t total;
} vfs_test_result_t;

int vfs_init(void);
int vfs_is_ready(void);
int vfs_fd_table_init(vfs_fd_table_t* table);
int vfs_fd_table_release(vfs_fd_table_t* table);
int vfs_close_owner(uint32_t pid);
int vfs_open(const char* path, uint32_t mode, int32_t* fd_out);
int vfs_read(int32_t fd, void* buffer, uint32_t size,
             uint32_t* bytes_read);
int vfs_write(int32_t fd, const void* buffer, uint32_t size,
              uint32_t* bytes_written);
int vfs_close(int32_t fd);
int vfs_lseek(int32_t fd, int32_t offset, uint32_t whence,
              uint32_t* position);
int vfs_get_status(vfs_status_t* status);
int vfs_copy_descriptors(vfs_descriptor_info_t* output,
                         uint32_t capacity, uint32_t* out_count);
int vfs_validate_state(void);
int vfs_self_test(vfs_test_result_t* result);

#endif
