#ifndef SYSFS_H
#define SYSFS_H

#include "types.h"
#include "fs/block.h"
#include "fs/procfs.h"
#include "fs/vfs.h"

#define SYSFS_IDENTIFIER_SIZE BLOCK_DEVICE_ID_SIZE
#define SYSFS_PCI_ID_SIZE 8U
#define SYSFS_MAX_ATTRIBUTES 32U

typedef enum {
    SYSFS_NODE_ROOT = 0,
    SYSFS_NODE_BUS,
    SYSFS_NODE_BUS_PCI,
    SYSFS_NODE_PCI_DEVICES,
    SYSFS_NODE_PCI_DEVICE,
    SYSFS_NODE_CLASS,
    SYSFS_NODE_CLASS_NET,
    SYSFS_NODE_NET_DEVICE,
    SYSFS_NODE_CLASS_BLOCK,
    SYSFS_NODE_BLOCK_DEVICE,
    SYSFS_NODE_POWER,
    SYSFS_NODE_POWER_STATE,
    SYSFS_NODE_PCI_ATTRIBUTE,
    SYSFS_NODE_NET_ATTRIBUTE,
    SYSFS_NODE_BLOCK_ATTRIBUTE
} sysfs_node_kind_t;

typedef struct {
    uint8_t* snapshot;
    uint32_t snapshot_size;
    uint32_t mount_slot;
    uint32_t mount_generation;
    uint8_t mount_acquired;
    sysfs_node_kind_t node_kind;
    char identifier[SYSFS_IDENTIFIER_SIZE];
    char attribute[SYSFS_MAX_ATTRIBUTES];
    uint32_t generation;
} sysfs_file_context_t;

typedef struct {
    uint8_t registry;
    uint8_t lookup;
    uint8_t listing;
    uint8_t read;
    uint8_t permissions;
    uint8_t seek_eof;
    uint8_t format;
    uint8_t cleanup;
    uint8_t inventories;
    uint8_t power;
    uint32_t passed;
    uint32_t total;
} sysfs_test_result_t;

int sysfs_init(void);
int sysfs_is_ready(void);
int sysfs_lookup(const char* canonical_path, vfs_lookup_result_t* result);
int sysfs_open_file(const vfs_lookup_result_t* lookup, uint32_t mode,
                    vnode_t* vnode, file_t* file,
                    sysfs_file_context_t* context);
int sysfs_list(vfs_dir_entry_t* entries, uint32_t capacity,
               uint32_t* out_count);
int sysfs_list_path(const char* canonical_path, vfs_dir_entry_t* entries,
                    uint32_t capacity, uint32_t* out_count);
int sysfs_validate_state(void);
int sysfs_self_test(sysfs_test_result_t* result);

#endif
