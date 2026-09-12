#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "driver_lifecycle_internal.h"

static int failures;

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active = 1U;

static void __attribute__((no_instrument_function)) coverage_record(
    void* function) {
    uintptr_t address = (uintptr_t)function;
    uint32_t index;

    if (!coverage_active || !address) return;
    for (index = 0U; index < coverage_count; index++) {
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

#define EXPECT(condition) do { \
    if (!(condition)) { \
        failures++; \
        printf("FAIL:%s:%d\n", #condition, __LINE__); \
    } \
} while (0)

static driver_lifecycle_resource_ids_t resources(uint32_t irq,
                                                 uint32_t dma,
                                                 uint32_t callback,
                                                 uint32_t work) {
    driver_lifecycle_resource_ids_t ids = {0};
    ids.irq = irq;
    ids.dma = dma;
    ids.callback = callback;
    ids.work = work;
    return ids;
}

static void test_transitions_and_idempotence(void) {
    driver_lifecycle_resource_ids_t ids = resources(5U, 9U, 11U, 12U);
    driver_lifecycle_snapshot_t snapshot;
    uint32_t generation = 0U;

    driver_lifecycle_init();
    EXPECT(driver_lifecycle_begin_probe("pci0", "root", "pci", "bridge",
                                        "00:00.0", DRIVER_LIFECYCLE_RESOURCE_IRQ |
                                        DRIVER_LIFECYCLE_RESOURCE_DMA, &ids, 0U,
                                        &generation) == OK);
    EXPECT(driver_lifecycle_begin_configure("pci0", generation) == ERR_STATE);
    EXPECT(driver_lifecycle_begin_reset("pci0", generation) == OK);
    EXPECT(driver_lifecycle_begin_configure("pci0", generation) == OK);
    EXPECT(driver_lifecycle_acquire("pci0", generation,
                                    DRIVER_LIFECYCLE_RESOURCE_IRQ |
                                    DRIVER_LIFECYCLE_RESOURCE_DMA, 0U) == OK);
    EXPECT(driver_lifecycle_release("pci0", generation,
                                    DRIVER_LIFECYCLE_RESOURCE_IO) == ERR_INVALID);
    EXPECT(driver_lifecycle_release("pci0", generation,
                                    DRIVER_LIFECYCLE_RESOURCE_DMA) == OK);
    EXPECT(driver_lifecycle_acquire("pci0", generation,
                                    DRIVER_LIFECYCLE_RESOURCE_DMA, 0U) == OK);
    EXPECT(driver_lifecycle_mark_registered("pci0", generation) == OK);
    EXPECT(driver_lifecycle_mark_ready("pci0", generation) == OK);
    EXPECT(driver_lifecycle_validate_ready("pci0", generation) == OK);
    EXPECT(driver_lifecycle_begin_probe("pci0", "root", "pci", "bridge",
                                        "00:00.0", 0U, 0, 0U,
                                        &generation) == OK);
    EXPECT(driver_lifecycle_snapshot("pci0", generation, &snapshot) == OK);
    EXPECT(snapshot.state == DRIVER_LIFECYCLE_READY);
    EXPECT(driver_lifecycle_validate_state() == OK);
}

static void test_resource_ownership_and_cleanup(void) {
    driver_lifecycle_resource_ids_t first = resources(7U, 15U, 21U, 22U);
    driver_lifecycle_resource_ids_t second = resources(7U, 16U, 23U, 24U);
    uint32_t first_generation = 0U;
    uint32_t second_generation = 0U;
    uint32_t required = DRIVER_LIFECYCLE_RESOURCE_IRQ |
                        DRIVER_LIFECYCLE_RESOURCE_DMA |
                        DRIVER_LIFECYCLE_RESOURCE_CALLBACK |
                        DRIVER_LIFECYCLE_RESOURCE_WORK;

    EXPECT(driver_lifecycle_publish("net0", "pci0", "pci", "ethernet",
                                    "0000:00:03.0", required, &first, 0U,
                                    OK, 0U, "ready") == OK);
    EXPECT(driver_lifecycle_begin_probe("net1", "pci0", "pci", "ethernet",
                                        "0000:00:04.0", required, &second, 0U,
                                        &second_generation) == OK);
    EXPECT(driver_lifecycle_begin_reset("net1", second_generation) == OK);
    EXPECT(driver_lifecycle_begin_configure("net1", second_generation) == OK);
    EXPECT(driver_lifecycle_acquire("net1", second_generation, required, 0U) ==
           ERR_UNAVAILABLE);
    EXPECT(driver_lifecycle_mark_failed("net1", second_generation,
                                        ERR_UNAVAILABLE, "resource conflict") == OK);
    EXPECT(driver_lifecycle_snapshot("net1", second_generation, 0) == ERR_STATE);
    EXPECT(driver_lifecycle_snapshot("net0", 1U, 0) == ERR_STATE);
    EXPECT(driver_lifecycle_snapshot("net0", 1U, &(driver_lifecycle_snapshot_t){0}) ==
           OK);
    EXPECT(driver_lifecycle_begin_quiesce("net0", 1U) == OK);
    EXPECT(driver_lifecycle_mark_quiesced("net0", 1U) == OK);
    EXPECT(driver_lifecycle_validate_callback("net0", 1U) == ERR_STATE);
    EXPECT(driver_lifecycle_mark_stopped("net0", 1U, OK, "stopped") == OK);
    EXPECT(driver_lifecycle_begin_probe("net0", "pci0", "pci", "ethernet",
                                        "0000:00:03.0", required, &first, 0U,
                                        &first_generation) == OK);
    EXPECT(first_generation != 1U);
    EXPECT(driver_lifecycle_validate_state() == OK);
}

static void test_shared_irq_and_stale_generation(void) {
    driver_lifecycle_resource_ids_t left = resources(9U, 0U, 31U, 0U);
    driver_lifecycle_resource_ids_t right = resources(9U, 0U, 32U, 0U);
    uint32_t left_generation = 0U;
    uint32_t right_generation = 0U;
    uint32_t required = DRIVER_LIFECYCLE_RESOURCE_IRQ |
                        DRIVER_LIFECYCLE_RESOURCE_CALLBACK;

    EXPECT(driver_lifecycle_publish("usb0", "pci0", "usb", "uhci", "00:1d.0",
                                    required, &left, DRIVER_LIFECYCLE_SHARED_IRQ,
                                    OK, 0U, "ready") == OK);
    EXPECT(driver_lifecycle_publish("usb1", "pci0", "usb", "ehci", "00:1d.1",
                                    required, &right, DRIVER_LIFECYCLE_SHARED_IRQ,
                                    OK, 0U, "ready") == OK);
    EXPECT(driver_lifecycle_snapshot("usb0", 1U,
                                     &(driver_lifecycle_snapshot_t){0}) == OK);
    EXPECT(driver_lifecycle_begin_quiesce("usb0", 1U) == OK);
    EXPECT(driver_lifecycle_mark_quiesced("usb0", 1U) == OK);
    EXPECT(driver_lifecycle_validate_ready("usb0", 1U) == ERR_UNAVAILABLE);
    EXPECT(driver_lifecycle_validate_callback("usb0", 1U) == ERR_STATE);
    EXPECT(driver_lifecycle_mark_stopped("usb0", 1U, OK, "stopped") == OK);
    EXPECT(driver_lifecycle_begin_probe("usb0", "pci0", "usb", "uhci",
                                        "00:1d.0", required, &left,
                                        DRIVER_LIFECYCLE_SHARED_IRQ,
                                        &left_generation) == OK);
    EXPECT(driver_lifecycle_begin_reset("usb0", left_generation) == OK);
    EXPECT(driver_lifecycle_begin_configure("usb0", left_generation) == OK);
    EXPECT(driver_lifecycle_acquire("usb0", left_generation, required,
                                    DRIVER_LIFECYCLE_SHARED_IRQ) == OK);
    EXPECT(driver_lifecycle_mark_registered("usb0", left_generation) == OK);
    EXPECT(driver_lifecycle_mark_ready("usb0", left_generation) == OK);
    EXPECT(left_generation != 1U);
    EXPECT(driver_lifecycle_validate_ready("usb0", 1U) == ERR_STATE);
    EXPECT(driver_lifecycle_validate_ready("usb0", left_generation) == OK);
    EXPECT(driver_lifecycle_begin_probe("broken", "root", "pci", "test",
                                        "00:02.0", 0U, 0, 0U,
                                        &right_generation) == OK);
    EXPECT(driver_lifecycle_mark_failed("broken", right_generation,
                                        ERR_DISK, "probe failure") == OK);
    EXPECT(driver_lifecycle_publish("optional", "root", "pci", "audio",
                                    "00:03.0", 0U, 0, 0U,
                                    ERR_UNAVAILABLE, 1U, "absent") ==
           ERR_UNAVAILABLE);
    {
        driver_lifecycle_state_t state = DRIVER_LIFECYCLE_UNREGISTERED;
        uint32_t optional_generation = 1U;
        EXPECT(driver_lifecycle_get_state("optional", optional_generation,
                                          &state) == OK);
        EXPECT(state == DRIVER_LIFECYCLE_DEGRADED);
    }
    EXPECT(driver_lifecycle_count() >= 4U);
    EXPECT(driver_lifecycle_validate_state() == OK);
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    uint32_t offset;
    uint32_t index;

    coverage_active = 0U;
    printf("ZCOV_BEGIN|case=host:drivers:lifecycle|value=0x%08X\n",
           coverage_count);
    for (offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:lifecycle|addresses=");
        for (index = offset; index < coverage_count &&
             index < offset + HOST_COVERAGE_LINE_SIZE; index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:lifecycle|value=0x%08X\n",
           (uint32_t)result);
}

int main(void) {
    test_transitions_and_idempotence();
    test_resource_ownership_and_cleanup();
    test_shared_irq_and_stale_generation();
    coverage_emit(failures ? 1 : 0);
    if (failures) return 1;
    puts("driver lifecycle host: PASS");
    return 0;
}
