#ifndef LOG_H
#define LOG_H

#include "types.h"

typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN  = 1,
    LOG_LEVEL_INFO  = 2,
    LOG_LEVEL_DEBUG = 3
} log_level_t;

void log_init(void);
void log_set_level(log_level_t level);
log_level_t log_get_level(void);

void log_print(log_level_t level, const char* module, const char* msg);

void log_to_buffer(log_level_t level, const char* module, const char* msg);
int  log_get_buffer(char* out, int max_size);
void log_clear_buffer(void);

const char* log_level_str(log_level_t level);

#define LOG_ERROR(module, msg) log_print(LOG_LEVEL_ERROR, module, msg)
#define LOG_WARN(module, msg)  log_print(LOG_LEVEL_WARN,  module, msg)
#define LOG_INFO(module, msg)  log_print(LOG_LEVEL_INFO,  module, msg)
#define LOG_DEBUG(module, msg) log_print(LOG_LEVEL_DEBUG, module, msg)

#endif
