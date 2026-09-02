#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "drivers/rtc.h"

#define HOST_COVERAGE_CAPACITY 128U
#define HOST_COVERAGE_LINE_SIZE 32U
#define RTC_INDEX_PORT 0x70U
#define RTC_DATA_PORT 0x71U
#define RTC_NMI_DISABLE 0x80U
#define RTC_REG_SECONDS 0x00U
#define RTC_REG_MINUTES 0x02U
#define RTC_REG_HOURS 0x04U
#define RTC_REG_DAY 0x07U
#define RTC_REG_MONTH 0x08U
#define RTC_REG_YEAR 0x09U
#define RTC_REG_STATUS_A 0x0AU
#define RTC_REG_STATUS_B 0x0BU
#define RTC_REG_CENTURY 0x32U
#define RTC_STATUS_A_UPDATE_IN_PROGRESS 0x80U
#define RTC_STATUS_B_24_HOUR 0x02U
#define RTC_STATUS_B_BINARY 0x04U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t cmos_registers[0x40U];
static uint8_t cmos_selected_register;
static uint8_t cmos_update_busy;

void rtc_host_outb(uint16_t port, uint8_t value) {
    if (port == RTC_INDEX_PORT) {
        cmos_selected_register = (uint8_t)(value & (uint8_t)~RTC_NMI_DISABLE);
    }
}

uint8_t rtc_host_inb(uint16_t port) {
    if (port != RTC_DATA_PORT || cmos_selected_register >= sizeof(cmos_registers)) {
        return 0U;
    }
    if (cmos_selected_register == RTC_REG_STATUS_A && cmos_update_busy) {
        return RTC_STATUS_A_UPDATE_IN_PROGRESS;
    }
    return cmos_registers[cmos_selected_register];
}

static void __attribute__((no_instrument_function)) coverage_record(
    void* function) {
    uintptr_t address = (uintptr_t)function;

    if (!coverage_active || !address) return;
    for (uint32_t index = 0U; index < coverage_count; index++) {
        if (coverage_addresses[index] == address) return;
    }
    if (coverage_count < HOST_COVERAGE_CAPACITY) {
        coverage_addresses[coverage_count++] = address;
    }
}

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(
    void* function, void* caller) {
    (void)caller;
    coverage_record(function);
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(
    void* function, void* caller) {
    (void)function;
    (void)caller;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:drivers:rtc-status|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:rtc-status|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:rtc-status|value=0x%08X\n",
           (uint32_t)result);
}

static int check_status(void) {
    rtc_status_t status;
    rtc_status_t second_status;

    if (rtc_get_status(NULL) != ERR_NULL) return 1;
    if (rtc_get_status(&status) != OK) return 2;
    if (status.initialized || status.available || status.valid ||
        status.reads || status.stable_reads || status.rejected_reads ||
        status.last_error != 0) return 3;
    if (rtc_get_status(&second_status) != OK) return 4;
    if (second_status.initialized != status.initialized ||
        second_status.available != status.available ||
        second_status.valid != status.valid ||
        second_status.reads != status.reads ||
        second_status.stable_reads != status.stable_reads ||
        second_status.rejected_reads != status.rejected_reads ||
        second_status.last_error != status.last_error) return 5;
    return 0;
}

static void set_bcd_snapshot(void) {
    memset(cmos_registers, 0, sizeof(cmos_registers));
    cmos_registers[RTC_REG_SECONDS] = 0x59U;
    cmos_registers[RTC_REG_MINUTES] = 0x58U;
    cmos_registers[RTC_REG_HOURS] = 0x23U;
    cmos_registers[RTC_REG_DAY] = 0x31U;
    cmos_registers[RTC_REG_MONTH] = 0x12U;
    cmos_registers[RTC_REG_YEAR] = 0x24U;
    cmos_registers[RTC_REG_CENTURY] = 0x20U;
    cmos_registers[RTC_REG_STATUS_B] = RTC_STATUS_B_24_HOUR;
    cmos_update_busy = 0U;
}

static int check_initialization_and_reads(void) {
    rtc_datetime_t datetime;
    rtc_status_t status;
    rtc_self_test_result_t self_test;

    set_bcd_snapshot();
    cmos_registers[RTC_REG_SECONDS] = 0x6AU;
    if (rtc_init() != ERR_INVALID) return 20;
    if (rtc_get_status(&status) != OK || !status.initialized ||
        status.available || status.valid || status.rejected_reads != 1U ||
        status.last_error != ERR_INVALID) return 21;

    set_bcd_snapshot();
    if (rtc_read_utc(&datetime) != OK || datetime.year != 2024U ||
        datetime.month != 12U || datetime.day != 31U ||
        datetime.hour != 23U || datetime.minute != 58U ||
        datetime.second != 59U) return 22;
    if (rtc_init() != OK) return 23;
    if (rtc_get_status(&status) != OK || !status.available || !status.valid ||
        !status.bcd_mode || status.twelve_hour_mode ||
        !status.century_available || status.reads != 2U ||
        status.stable_reads != 1U || status.last_error != OK) return 24;
    if (rtc_validate_state() != OK) return 25;
    if (rtc_self_test(&self_test) != OK || !self_test.bcd_conversion ||
        !self_test.binary_conversion || !self_test.twelve_hour_conversion ||
        !self_test.calendar_validation || !self_test.invalid_dates_rejected ||
        self_test.passed != 5U || self_test.failed != 0U) return 26;
    if (rtc_self_test(NULL) != ERR_NULL) return 27;
    return 0;
}

static int check_binary_and_invalid_data(void) {
    rtc_datetime_t datetime;
    rtc_status_t status;

    set_bcd_snapshot();
    cmos_registers[RTC_REG_STATUS_B] = RTC_STATUS_B_BINARY | RTC_STATUS_B_24_HOUR;
    cmos_registers[RTC_REG_SECONDS] = 59U;
    cmos_registers[RTC_REG_MINUTES] = 58U;
    cmos_registers[RTC_REG_HOURS] = 23U;
    cmos_registers[RTC_REG_DAY] = 29U;
    cmos_registers[RTC_REG_MONTH] = 2U;
    cmos_registers[RTC_REG_YEAR] = 24U;
    cmos_registers[RTC_REG_CENTURY] = 20U;
    if (rtc_read_utc(&datetime) != OK || datetime.year != 2024U ||
        datetime.month != 2U || datetime.day != 29U || datetime.hour != 23U ||
        rtc_get_status(&status) != OK || status.bcd_mode ||
        status.twelve_hour_mode || !status.century_available) return 30;

    cmos_registers[RTC_REG_DAY] = 30U;
    cmos_registers[RTC_REG_MONTH] = 4U;
    if (rtc_read_utc(&datetime) != OK || datetime.month != 4U ||
        datetime.day != 30U) return 31;
    cmos_registers[RTC_REG_DAY] = 31U;
    if (rtc_read_utc(&datetime) != ERR_INVALID) return 32;
    if (rtc_get_status(&status) != OK || status.last_error != ERR_INVALID ||
        status.rejected_reads < 1U || !status.valid || !status.available) return 33;
    return 0;
}

static int check_twelve_hour_and_timeout(void) {
    rtc_datetime_t datetime;
    rtc_status_t status;

    set_bcd_snapshot();
    cmos_registers[RTC_REG_STATUS_B] = 0U;
    cmos_registers[RTC_REG_HOURS] = 0x81U;
    cmos_registers[RTC_REG_DAY] = 1U;
    cmos_registers[RTC_REG_MONTH] = 1U;
    cmos_registers[RTC_REG_YEAR] = 0x24U;
    cmos_registers[RTC_REG_CENTURY] = 0x20U;
    if (rtc_read_utc(&datetime) != OK || datetime.hour != 13U ||
        datetime.minute != 58U || datetime.second != 59U) return 40;
    if (rtc_get_status(&status) != OK || !status.bcd_mode ||
        !status.twelve_hour_mode) return 41;

    cmos_update_busy = 1U;
    if (rtc_read_utc(&datetime) != ERR_TIMEOUT) return 42;
    cmos_update_busy = 0U;
    if (rtc_get_status(&status) != OK || status.last_error != ERR_TIMEOUT ||
        status.rejected_reads < 2U || !status.valid || !status.available) return 43;
    if (rtc_validate_state() != OK) return 44;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_status();
    if (!result) result = check_initialization_and_reads();
    if (!result) result = check_binary_and_invalid_data();
    if (!result) result = check_twelve_hour_and_timeout();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
