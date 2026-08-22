                    #ifndef CLOCK_H
#define CLOCK_H

#include "types.h"

typedef enum {
    CLOCK_SOURCE_NONE = 0,
    CLOCK_SOURCE_RTC
} clock_source_t;

typedef struct {
    uint8_t initialized;
    uint8_t monotonic_available;
    uint8_t utc_available;
    clock_source_t source;
    uint32_t frequency;
    uint64_t anchor_monotonic_ticks;
    uint64_t anchor_unix_seconds;
    uint64_t monotonic_wraps;
    uint32_t reads;
    int last_error;
} clock_status_t;

typedef struct {
    uint8_t epoch_conversion;
    uint8_t leap_year_conversion;
    uint8_t invalid_date_rejected;
    uint8_t monotonic_rollover;
    uint8_t invariants;
    uint8_t passed;
    uint8_t failed;
} clock_self_test_result_t;

int clock_init(void);
int clock_get_utc(uint64_t* out_unix_seconds);
int clock_get_monotonic_ticks(uint64_t* out_ticks);
int clock_get_status(clock_status_t* out_status);
int clock_validate_state(void);
int clock_self_test(clock_self_test_result_t* out_result);
const char* clock_source_name(clock_source_t source);

#endif
