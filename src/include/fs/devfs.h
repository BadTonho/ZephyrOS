#ifndef DEVFS_H
#define DEVFS_H

#include "types.h"
#include "fs/vfs.h"
#include "fs/block.h"

#define DEVFS_MAX_NODES 5U
#define DEVFS_SPEAKER_MIN_HZ 20U
#define DEVFS_SPEAKER_MAX_HZ 20000U
#define DEVFS_SPEAKER_MIN_MS 1U
#define DEVFS_SPEAKER_MAX_MS 2000U

typedef enum {
    DEVFS_NODE_NULL = 0,
    DEVFS_NODE_ZERO,
    DEVFS_NODE_TTY,
    DEVFS_NODE_SPEAKER,
    DEVFS_NODE_HDA
} devfs_node_id_t;

typedef struct {
    devfs_node_id_t node_id;
    uint32_t mount_slot;
    uint32_t mount_generation;
    char block_id[BLOCK_DEVICE_ID_SIZE];
    uint32_t capacity;
    uint8_t sector[BLOCK_SECTOR_SIZE];
} devfs_file_context_t;

typedef struct {
    char name[16];
    vfs_node_type_t type;
    uint8_t readable;
    uint8_t writable;
    uint8_t available;
} devfs_node_info_t;

typedef struct {
    uint32_t initialized;
    uint32_t capacity;
    uint32_t active;
    uint32_t opens;
    uint32_t reads;
    uint32_t writes;
    uint32_t seeks;
    uint32_t ioctls;
    uint32_t failures;
} devfs_status_t;

typedef struct {
    uint8_t registry;
    uint8_t null_device;
    uint8_t zero_device;
    uint8_t tty_device;
    uint8_t speaker_device;
    uint8_t block_device;
    uint8_t permissions;
    uint8_t cleanup;
    uint8_t invariants;
    uint32_t passed;
    uint32_t total;
} devfs_test_result_t;

int devfs_init(void);
int devfs_is_ready(void);
int devfs_lookup(const char* canonical_path, vfs_lookup_result_t* result);
int devfs_open_file(const vfs_lookup_result_t* lookup, uint32_t mode,
                    vnode_t* vnode, file_t* file,
                    devfs_file_context_t* context);
int devfs_list(vfs_dir_entry_t* entries, uint32_t capacity,
               uint32_t* out_count);
int devfs_copy_nodes(devfs_node_info_t* output, uint32_t capacity,
                     uint32_t* out_count);
int devfs_get_status(devfs_status_t* status);
int devfs_validate_state(void);
int devfs_self_test(devfs_test_result_t* result);

#endif
