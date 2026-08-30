#ifndef BLOCK_CACHE_H
#define BLOCK_CACHE_H

#include "types.h"
#include "fs/block.h"

#define BLOCK_CACHE_CAPACITY 64U
#define BLOCK_CACHE_HASH_BUCKETS 64U
#define BLOCK_CACHE_BLOCK_SIZE BLOCK_SECTOR_SIZE
#define BLOCK_CACHE_INVALID_INDEX 0xFFFFFFFFU
#define BLOCK_WRITEBACK_PERIOD_TICKS 250U
#define BLOCK_WRITEBACK_BUDGET 8U
#define BLOCK_WRITEBACK_PRESSURE_FREE_PAGES 32U
#define BLOCK_WRITEBACK_PRESSURE_DIRTY_PERCENT 75U

typedef enum {
    BLOCK_CACHE_FREE = 0,
    BLOCK_CACHE_READING,
    BLOCK_CACHE_VALID,
    BLOCK_CACHE_DIRTY,
    BLOCK_CACHE_WRITEBACK,
    BLOCK_CACHE_ERROR
} block_cache_state_t;

typedef enum {
    BLOCK_DURABILITY_READY = 0,
    BLOCK_DURABILITY_DEGRADED,
    BLOCK_DURABILITY_ERROR
} block_durability_state_t;

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
    uint32_t dirty_bytes;
    uint32_t writeback_attempts;
    uint32_t writeback_completed;
    uint32_t writeback_failures;
    uint32_t sync_operations;
    uint32_t flush_operations;
    uint32_t flush_unavailable;
    uint32_t degraded_syncs;
    uint32_t physical_writes;
    int last_sync_error;
    block_durability_state_t durability_state;
} block_cache_stats_t;

typedef struct {
    block_durability_state_t state;
    uint32_t devices_checked;
    uint32_t flush_supported;
    uint32_t flush_unavailable;
    int last_error;
} block_durability_status_t;

int block_cache_init(void);
int block_cache_read(const block_device_t* device, uint32_t lba,
                     uint8_t count, uint8_t* buffer, uint32_t buffer_bytes,
                     block_cache_read_backend_t backend);
int block_cache_write(const block_device_t* device, uint32_t lba,
                      uint32_t byte_offset, const uint8_t* buffer,
                      uint32_t size);
int block_cache_get_stats(block_cache_stats_t* out_stats);
int block_cache_get_durability_status(block_durability_status_t* out_status);
int block_cache_writeback_step(uint32_t budget, uint32_t* out_written);
int block_cache_sync_device(const char* device_id);
int block_cache_sync_device_until(const char* device_id,
                                  uint32_t deadline_tick);
int block_cache_sync_all(void);
int block_cache_sync_all_until(uint32_t deadline_tick);
int block_cache_clear(void);
int block_cache_invalidate_device(const char* device_id);
int block_cache_invalidate_range(const char* device_id, uint32_t lba,
                                 uint32_t sector_count);
int block_cache_validate_state(void);
int block_cache_self_test(void);

#endif
