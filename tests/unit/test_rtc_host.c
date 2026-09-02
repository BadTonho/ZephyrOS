#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "drivers/rtc.h"

#define HOST_COVERAGE_CAPACITY 128U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

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

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_status();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
