#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "drivers/rng.h"

#define HOST_COVERAGE_CAPACITY 128U
#define HOST_COVERAGE_LINE_SIZE 32U

uint8_t rng_host_cpuid_available;
uint8_t rng_host_rdrand_available;
uint8_t rng_host_rdrand_ready;
uint32_t rng_host_word;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
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

static int check(int condition, int code) {
    return condition ? 0 : code;
}

static int check_initial_state(void) {
    rng_status_t status;

    if (rng_get_status(NULL) != ERR_NULL) return 10;
    if (rng_get_status(&status) != OK) return 11;
    if (status.initialized || status.cpuid_available ||
        status.rdrand_available || status.words_generated ||
        status.hardware_failures || status.last_error != 0) return 12;
    if (rng_validate_state() != OK) return 13;
    if (rng_get_bytes(NULL, 1U) != ERR_NULL) return 14;
    if (rng_get_bytes((uint8_t*)&status, 1U) != ERR_UNAVAILABLE) return 15;
    return 0;
}

static int check_initialization(void) {
    rng_status_t status;

    rng_host_cpuid_available = 0U;
    rng_host_rdrand_available = 1U;
    if (rng_init() != ERR_UNAVAILABLE) return 20;
    if (rng_get_status(&status) != OK || status.initialized ||
        status.cpuid_available || status.rdrand_available ||
        status.last_error != ERR_UNAVAILABLE || rng_validate_state() != OK) {
        return 21;
    }

    rng_host_cpuid_available = 1U;
    rng_host_rdrand_available = 0U;
    if (rng_init() != ERR_UNAVAILABLE) return 22;
    if (rng_get_status(&status) != OK || !status.cpuid_available ||
        status.initialized || status.rdrand_available ||
        status.last_error != ERR_UNAVAILABLE || rng_validate_state() != OK) {
        return 23;
    }

    rng_host_rdrand_available = 1U;
    rng_host_rdrand_ready = 1U;
    rng_host_word = 0x11223344U;
    if (rng_init() != OK) return 24;
    if (rng_get_status(&status) != OK || !status.initialized ||
        !status.cpuid_available || !status.rdrand_available ||
        status.words_generated || status.hardware_failures ||
        status.last_error != OK || rng_validate_state() != OK) return 25;
    return 0;
}

static int check_reads(void) {
    uint8_t bytes[7] = {0U};
    rng_status_t status;

    if (rng_get_bytes(NULL, 0U) != OK) return 30;
    if (rng_get_bytes(NULL, 1U) != ERR_NULL) return 31;
    if (rng_get_bytes(bytes, sizeof(bytes)) != OK) return 32;
    if (bytes[0] != 0x44U || bytes[1] != 0x33U || bytes[2] != 0x22U ||
        bytes[3] != 0x11U || bytes[4] != 0x45U || bytes[5] != 0x33U ||
        bytes[6] != 0x22U) return 33;
    if (rng_get_status(&status) != OK || status.words_generated != 2U ||
        status.hardware_failures || status.last_error != OK) return 34;
    if (rng_validate_state() != OK) return 35;

    rng_host_rdrand_ready = 0U;
    if (rng_get_bytes(bytes, 1U) != ERR_UNAVAILABLE) return 36;
    if (rng_get_status(&status) != OK || status.words_generated != 2U ||
        status.hardware_failures != 1U || status.last_error != ERR_UNAVAILABLE) {
        return 37;
    }
    if (rng_validate_state() != OK) return 38;
    return 0;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:drivers:rng|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:rng|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:rng|value=0x%08X\n",
           (uint32_t)result);
}

int main(void) {
    int result;

    coverage_active = 1U;
    rng_host_cpuid_available = 1U;
    rng_host_rdrand_available = 1U;
    rng_host_rdrand_ready = 1U;
    rng_host_word = 0x11223344U;
    result = check_initial_state();
    if (!result) result = check_initialization();
    if (!result) result = check_reads();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
