#include <stdint.h>
#include <stdio.h>

#include "core/spinlock.h"

#define HOST_COVERAGE_CAPACITY 64U
#define HOST_COVERAGE_LINE_SIZE 16U

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
    printf("ZCOV_BEGIN|case=host:core:spinlock|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:spinlock|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:spinlock|value=0x%08X\n",
           (uint32_t)result);
}

static int expect_true(int condition, const char* expression) {
    if (condition) return 0;
    fprintf(stderr, "spinlock-host: falhou: %s\n", expression);
    return 1;
}

#define EXPECT(expression) \
    do { \
        if (expect_true((expression), #expression)) failures++; \
    } while (0)

int main(void) {
    spinlock_t lock;
    int failures = 0;

    lock.lock = 0xFFFFFFFFU;
    coverage_active = 1U;
    spinlock_init(&lock);
    EXPECT(lock.lock == 0U);

    spinlock_acquire(&lock);
    EXPECT(lock.lock == 1U);
    spinlock_release(&lock);
    EXPECT(lock.lock == 0U);

    spinlock_acquire(&lock);
    spinlock_release(&lock);
    EXPECT(lock.lock == 0U);
    coverage_active = 0U;
    coverage_emit(failures);
    return failures;
}
