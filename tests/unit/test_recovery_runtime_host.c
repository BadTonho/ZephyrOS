#include <stdint.h>
#include <stdio.h>

#include "core/log.h"
#include "core/string.h"

void log_print(log_level_t level, const char* module, const char* message);

#define HOST_COVERAGE_CAPACITY 256U
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

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:boot:recovery-runtime|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:boot:recovery-runtime|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:boot:recovery-runtime|value=0x%08X\n",
           (uint32_t)result);
}

static int check_memory_primitives(void) {
    uint8_t source[32];
    uint8_t destination[32];

    for (uint32_t index = 0U; index < sizeof(source); index++) {
        source[index] = (uint8_t)(index * 3U + 1U);
        destination[index] = 0xCCU;
    }
    kmemset(destination, 0x5AU, sizeof(destination));
    for (uint32_t index = 0U; index < sizeof(destination); index++) {
        if (destination[index] != 0x5AU) return 1;
    }
    kmemcpy(destination, source, sizeof(destination));
    for (uint32_t index = 0U; index < sizeof(destination); index++) {
        if (destination[index] != source[index]) return 2;
    }
    kmemset(destination, 0U, 0U);
    if (destination[0] != source[0]) return 3;
    return 0;
}

static int check_string_primitives(void) {
    static const char empty[] = "";
    static const char short_text[] = "Recovery Runtime";
    static const char equal_text[] = "Recovery Runtime";
    static const char greater_text[] = "Recovery runtime";

    if (kstrlen(empty) != 0U) return 1;
    if (kstrlen(short_text) != 16U) return 2;
    if (kstrcmp(short_text, equal_text) != 0) return 3;
    if (kstrcmp(short_text, greater_text) >= 0) return 4;
    if (kstrcmp(greater_text, short_text) <= 0) return 5;
    return 0;
}

static int check_log_entry_points(void) {
    static const log_level_t levels[] = {
        LOG_LEVEL_ERROR, LOG_LEVEL_WARN, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG
    };

    for (uint32_t index = 0U; index < sizeof(levels) / sizeof(levels[0]);
         index++) {
        log_print(levels[index], "RECOVERY", "host diagnostic");
    }
    log_print(LOG_LEVEL_INFO, 0, 0);
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_memory_primitives();
    if (result == 0) result = check_string_primitives();
    if (result == 0) result = check_log_entry_points();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
