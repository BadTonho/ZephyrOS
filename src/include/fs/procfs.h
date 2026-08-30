#ifndef PROCFS_H
#define PROCFS_H

#include "types.h"
#include "fs/vfs.h"

#define PROCFS_MAX_SNAPSHOT_SIZE (16U * 1024U)
#define PROCFS_SYS_CONTROL_MAX_INPUT 16U

typedef int (*procfs_read_callback_t)(char* buffer, uint32_t capacity,
                                      uint32_t* out_len, void* data);
typedef int (*procfs_write_callback_t)(const char* buffer, uint32_t len,
                                       void* data);

typedef enum {
    PROCFS_NODE_GLOBAL = 0,
    PROCFS_NODE_PROCESS_DIRECTORY,
    PROCFS_NODE_PROCESS_STATUS,
    PROCFS_NODE_PROCESS_CMDLINE,
    PROCFS_NODE_PROCESS_MAPS,
    PROCFS_NODE_SYS_DIRECTORY,
    PROCFS_NODE_SYS_KERNEL_DIRECTORY,
    PROCFS_NODE_SYS_CONTROL
} procfs_node_kind_t;

typedef struct {
    const char* name;
    uint32_t mode;
    procfs_read_callback_t read_proc;
    procfs_write_callback_t write_proc;
    void* data;
} proc_entry_t;

typedef struct {
    uint8_t* snapshot;
    uint32_t snapshot_size;
    uint32_t entry_index;
    uint32_t mount_slot;
    uint32_t mount_generation;
    uint8_t mount_acquired;
    procfs_node_kind_t node_kind;
    uint32_t process_pid;
    uint32_t process_generation;
    uint32_t control_index;
} procfs_file_context_t;

typedef struct {
    uint8_t registry;
    uint8_t lookup;
    uint8_t listing;
    uint8_t read;
    uint8_t permissions;
    uint8_t seek_eof;
    uint8_t callback_errors;
    uint8_t cleanup;
    uint8_t invariants;
    uint8_t global_nodes;
    uint8_t process_listing;
    uint8_t process_nodes;
    uint8_t format;
    uint8_t generation;
    uint32_t passed;
    uint32_t total;
    uint8_t control_nodes;
    uint8_t control_read;
    uint8_t control_write;
    uint8_t control_rollback;
    uint8_t control_privilege;
    uint8_t control_reset;
} procfs_test_result_t;

int procfs_init(void);
int procfs_is_ready(void);
int procfs_lookup(const char* canonical_path, vfs_lookup_result_t* result);
int procfs_open_file(const vfs_lookup_result_t* lookup, uint32_t mode,
                     vnode_t* vnode, file_t* file,
                     procfs_file_context_t* context);
int procfs_list(vfs_dir_entry_t* entries, uint32_t capacity,
                uint32_t* out_count);
int procfs_list_path(const char* canonical_path, vfs_dir_entry_t* entries,
                     uint32_t capacity, uint32_t* out_count);
int procfs_reset_controls(void);
int procfs_validate_state(void);
int procfs_self_test(procfs_test_result_t* result);

#endif
