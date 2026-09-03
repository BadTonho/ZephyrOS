#include <stdint.h>
#include <stdio.h>

#include "core/log.h"
#include "drivers/tss.h"

#define HOST_COVERAGE_CAPACITY 64U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint32_t flush_count;

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void tss_flush(void) {
    flush_count++;
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

static int check_before_init(void) {
    if (tss_is_ready() != 0) return 10;
    tss_set_kernel_stack(0U);
    tss_set_kernel_stack(0x12345000U);
    if (tss_is_ready() != 0) return 11;
    return 0;
}

static int check_initialization(void) {
    if (flush_count != 0U) return 20;
    tss_init();
    if (flush_count != 1U || tss_is_ready() != 1) return 21;
    tss_set_kernel_stack(0U);
    tss_set_kernel_stack(0xABCDE000U);
    if (tss_is_ready() != 1) return 22;
    tss_init();
    if (flush_count != 2U || tss_is_ready() != 1) return 23;
    return 0;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:drivers:tss|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:tss|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:tss|value=0x%08X\n",
           (uint32_t)result);
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_before_init();
    if (!result) result = check_initialization();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
