#ifndef BLOCK_CACHE_H
#define BLOCK_CACHE_H

#include "types.h"
#include "fs/block.h"

#define BLOCK_CACHE_CAPACITY 64U
#define BLOCK_CACHE_HASH_BUCKETS 64U
#define BLOCK_CACHE_BLOCK_SIZE BLOCK_SECTOR_SIZE
#define BLOCK_CACHE_INVALID_INDEX 0xFFFFFFFFU

typedef enum {
    BLOCK_CACHE_FREE = 0,
    BLOCK_CACHE_READING,
    BLOCK_CACHE_VALID,
    BLOCK_CACHE_DIRTY,
    BLOCK_CACHE_WRITEBACK,
    BLOCK_CACHE_ERROR
} block_cache_state_t;

typedef int (*block_cache_read_backend_t)(const char* device_id,
                                          uint32_t lba, uint8_t count,
                                          uint8_t* buffer);

typedef struct {
    uint32_t capacity;
    uint32_t block_size;
    uint32_t memory_bytes;
    uint32_t entries;
    uint32_t valid_entries;
    uint32_t reading_entries;
    uint32_t dirty_entries;
    uint32_t writeback_entries;
    uint32_t pinned_entries;
    uint32_t hits;
    uint32_t misses;
    uint32_t reads_avoided;
    uint32_t physical_reads;
    uint32_t evictions;
    uint32_t invalidations;
    uint32_t bypasses;
    uint32_t errors;
    uint32_t hit_rate_percent;
    int last_error;
} block_cache_stats_t;

int block_cache_init(void);
int block_cache_read(const block_device_t* device, uint32_t lba,
                     uint8_t count, uint8_t* buffer, uint32_t buffer_bytes,
                     block_cache_read_backend_t backend);
int block_cache_get_stats(block_cache_stats_t* out_stats);
int block_cache_clear(void);
int block_cache_invalidate_device(const char* device_id);
int block_cache_invalidate_range(const char* device_id, uint32_t lba,
                                 uint32_t sector_count);
int block_cache_validate_state(void);
int block_cache_self_test(void);

#endif
