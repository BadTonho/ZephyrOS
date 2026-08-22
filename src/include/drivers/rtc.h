#ifndef RTC_H
#define RTC_H

#include "types.h"

#define RTC_YEAR_MIN 2000U
#define RTC_YEAR_MAX 2099U

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} rtc_datetime_t;

typedef struct {
    uint8_t initialized;
    uint8_t available;
    uint8_t valid;
    uint8_t bcd_mode;
    uint8_t twelve_hour_mode;
    uint8_t century_available;
    uint32_t reads;
    uint32_t stable_reads;
    uint32_t rejected_reads;
    rtc_datetime_t utc;
    int last_error;
} rtc_status_t;

typedef struct {
    uint8_t bcd_conversion;
    uint8_t binary_conversion;
    uint8_t twelve_hour_conversion;
    uint8_t calendar_validation;
    uint8_t invalid_dates_rejected;
    uint8_t passed;
    uint8_t failed;
} rtc_self_test_result_t;

int rtc_init(void);
int rtc_read_utc(rtc_datetime_t* out_datetime);
int rtc_get_status(rtc_status_t* out_status);
int rtc_validate_state(void);
int rtc_self_test(rtc_self_test_result_t* out_result);

#endif
