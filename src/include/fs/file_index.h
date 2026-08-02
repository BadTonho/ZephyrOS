#ifndef FILE_INDEX_H
#define FILE_INDEX_H

#include "types.h"
#include "fs/fs.h"
#include "fs/storage.h"

#define FILE_INDEX_MAX_ENTRIES    512U
#define FILE_INDEX_MAX_DIRECTORIES 128U
#define FILE_INDEX_MAX_DEPTH       16U
#define FILE_INDEX_MAX_SOURCES     STORAGE_MAX_MOUNTS
#define FILE_INDEX_MAX_RESULTS     64U
#define FILE_INDEX_QUERY_SIZE      64U

typedef enum {
    FILE_INDEX_STATE_UNINITIALIZED = 0,
    FILE_INDEX_STATE_EMPTY,
    FILE_INDEX_STATE_BUILDING,
    FILE_INDEX_STATE_READY,
    FILE_INDEX_STATE_CANCELLED,
    FILE_INDEX_STATE_FAILED
} file_index_state_t;

typedef struct {
    char volume_id[STORAGE_ID_SIZE];
    char parent_path[FS_MAX_PATH];
    char name[STORAGE_NAME_SIZE];
    uint32_t volume_generation;
    uint32_t size;
    uint8_t attributes;
    uint8_t is_directory;
    uint8_t boot;
} file_index_entry_t;

typedef enum {
    FILE_INDEX_RESULT_AVAILABLE = 0,
    FILE_INDEX_RESULT_VOLUME_MISSING,
    FILE_INDEX_RESULT_STALE
} file_index_result_availability_t;

typedef struct {
    file_index_entry_t entry;
    file_index_result_availability_t availability;
} file_index_result_t;

typedef struct {
    file_index_state_t state;
    uint32_t active_entries;
    uint32_t candidate_entries;
    uint32_t directories_scanned;
    uint32_t scan_steps;
    uint32_t source_count;
    uint32_t sources_completed;
    uint32_t event_generation;
    uint32_t memory_bytes;
    uint8_t initialized;
    uint8_t partial;
    uint8_t stale;
    uint8_t automatic_suspended;
    int last_error;
} file_index_status_t;

typedef struct {
    uint32_t total_matches;
    uint32_t returned_matches;
    uint8_t partial;
    uint8_t stale;
    uint8_t building;
    uint8_t cancelled;
    uint8_t volume_missing;
    uint8_t result_stale;
    int last_error;
} file_index_search_status_t;

int file_index_init(void);
int file_index_poll(uint32_t budget, uint32_t* out_steps);
int file_index_rebuild(void);
int file_index_cancel(void);
int file_index_get_status(file_index_status_t* out_status);
int file_index_search(const char* query, file_index_result_t* results,
                      uint32_t capacity,
                      file_index_search_status_t* out_status);
int file_index_validate_state(void);
int file_index_self_test(void);
const char* file_index_state_name(file_index_state_t state);

#endif
