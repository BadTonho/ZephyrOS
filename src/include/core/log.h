#ifndef LOG_H
#define LOG_H

#include "types.h"

typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN  = 1,
    LOG_LEVEL_INFO  = 2,
    LOG_LEVEL_DEBUG = 3
} log_level_t;

#define LOG_RECORD_CAPACITY 32U
#define LOG_MODULE_CAPACITY 16U
#define LOG_MESSAGE_CAPACITY 80U

#define LOG_RECORD_FLAG_HAS_ERROR_CODE    0x01U
#define LOG_RECORD_FLAG_MODULE_TRUNCATED  0x02U
#define LOG_RECORD_FLAG_MESSAGE_TRUNCATED 0x04U

typedef struct {
    uint32_t sequence;
    uint32_t first_tick;
    uint32_t last_tick;
    log_level_t level;
    int32_t error_code;
    uint32_t occurrences;
    uint8_t flags;
    char module[LOG_MODULE_CAPACITY];
    char message[LOG_MESSAGE_CAPACITY];
} log_record_t;

typedef struct {
    uint32_t occupancy;
    uint32_t capacity;
    log_level_t buffer_level;
    log_level_t console_level;
    uint32_t next_sequence;
    uint32_t overwritten_records;
    uint32_t grouped_events;
    uint32_t truncated_events;
    uint32_t dropped_events;
    uint32_t clear_count;
} log_stats_t;

typedef struct {
    uint32_t passed;
    uint32_t failed;
    uint8_t order_and_metadata;
    uint8_t wrap_and_overwrite;
    uint8_t repetition_grouping;
    uint8_t safe_truncation;
    uint8_t optional_error_code;
    uint8_t clear_behavior;
    uint8_t text_serialization;
    uint8_t level_filtering;
} log_self_test_result_t;

void log_init(void);
void log_set_level(log_level_t level);
log_level_t log_get_level(void);
int log_set_buffer_level(log_level_t level);
int log_set_console_level(log_level_t level);
log_level_t log_get_buffer_level(void);
log_level_t log_get_console_level(void);

void log_print(log_level_t level, const char* module, const char* msg);
void log_print_code(log_level_t level, const char* module,
                    int32_t error_code, const char* msg);

void log_to_buffer(log_level_t level, const char* module, const char* msg);
int  log_get_buffer(char* out, int max_size);
void log_clear_buffer(void);
int log_get_stats(log_stats_t* out_stats);
int log_copy_recent(log_record_t* out_records, uint32_t max_records,
                    uint32_t* out_count);
int log_self_test(log_self_test_result_t* out_result);

const char* log_level_str(log_level_t level);

#define LOG_ERROR(module, msg) log_print(LOG_LEVEL_ERROR, module, msg)
#define LOG_WARN(module, msg)  log_print(LOG_LEVEL_WARN,  module, msg)
#define LOG_INFO(module, msg)  log_print(LOG_LEVEL_INFO,  module, msg)
#define LOG_DEBUG(module, msg) log_print(LOG_LEVEL_DEBUG, module, msg)
#define LOG_ERROR_CODE(module, code, msg) \
    log_print_code(LOG_LEVEL_ERROR, module, code, msg)
#define LOG_WARN_CODE(module, code, msg) \
    log_print_code(LOG_LEVEL_WARN, module, code, msg)

#endif
