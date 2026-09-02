#include <stdint.h>
#include <stdio.h>

#include "types.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U

void* memcpy(void* destination, const void* source, size_t length);
void* memmove(void* destination, const void* source, size_t length);
void* memset(void* destination, int value, size_t length);
int memcmp(const void* first, const void* second, size_t length);
size_t strlen(const char* text);

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
    printf("ZCOV_BEGIN|case=host:core:bearssl-compat|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:bearssl-compat|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:bearssl-compat|value=0x%08X\n",
           (uint32_t)result);
}

static int check_memory_copy(void) {
    uint8_t source[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
    uint8_t destination[8] = {0U};

    if (memcpy(destination, source, sizeof(source)) != destination) return 1;
    for (uint32_t index = 0U; index < sizeof(source); index++) {
        if (destination[index] != source[index]) return 2;
    }
    if (memcpy(destination, source, 0U) != destination) return 3;
    return 0;
}

static int check_memory_move(void) {
    char forward[8] = "abcdefg";
    char backward[8] = "abcdefg";

    if (memmove(forward + 1, forward, sizeof(forward) - 1U) != forward + 1U) {
        return 1;
    }
    if (forward[0] != 'a' || forward[1] != 'a' || forward[2] != 'b' ||
        forward[3] != 'c' || forward[4] != 'd' || forward[5] != 'e' ||
        forward[6] != 'f' || forward[7] != 'g') return 2;
    if (memmove(backward, backward + 1, sizeof(backward) - 1U) != backward) {
        return 3;
    }
    if (backward[0] != 'b' || backward[1] != 'c' || backward[2] != 'd' ||
        backward[3] != 'e' || backward[4] != 'f' || backward[5] != 'g' ||
        backward[6] != '\0') return 4;
    return 0;
}

static int check_memory_set_and_compare(void) {
    uint8_t first[6] = {0U};
    uint8_t second[6] = {0U};

    if (memset(first, 0xA5, sizeof(first)) != first) return 1;
    if (memset(second, 0xA5, sizeof(second)) != second) return 2;
    if (memcmp(first, second, sizeof(first)) != 0) return 3;
    second[4] = 0xA6U;
    if (memcmp(first, second, sizeof(first)) >= 0) return 4;
    if (memcmp(second, first, sizeof(first)) <= 0) return 5;
    if (memcmp(first, second, 0U) != 0) return 6;
    return 0;
}

static int check_lengths(void) {
    if (strlen(NULL) != 0U || strlen("") != 0U ||
        strlen("ZephyrOS") != 8U) return 1;
    return 0;
}

static int check_compatibility(void) {
    int result = check_memory_copy();

    if (result != 0) return result;
    result = check_memory_move();
    if (result != 0) return result + 10;
    result = check_memory_set_and_compare();
    if (result != 0) return result + 20;
    result = check_lengths();
    if (result != 0) return result + 30;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_compatibility();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
