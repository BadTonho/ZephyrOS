#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/video.h"
#include "drivers/serial.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define MEMORY_TEST_PAGE_COUNT 4U
#define MEMORY_TEST_INVALID_PAGE 0x00001234U
#define MEMORY_TEST_OUTSIDE_RAM MIN_PHYSICAL_MEMORY

int memory_host_init(mmap_entry_t* mmap, uint32_t mmap_count);

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint32_t panic_count;

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
    printf("ZCOV_BEGIN|case=host:memory:memory|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:memory:memory|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:memory:memory|value=0x%08X\n",
           (uint32_t)result);
}

static int check(int condition, const char* name) {
    if (condition) return OK;
    printf("memory-host failure: %s\n", name);
    return ERR_STATE;
}

static int check_result(int actual, int expected, const char* name) {
    return check(actual == expected, name);
}

static uint32_t fake_ticks;

uint32_t timer_get_ticks(void) {
    return fake_ticks++;
}

uint8_t serial_is_ready(void) {
    return 0U;
}

uint32_t serial_write_text(const char* text, uint32_t length) {
    (void)text;
    return length;
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void video_newline(void) {
}

void panic_memory(const char* message, uint32_t mmap_entries,
                  uint32_t total_memory, uint32_t free_memory,
                  uint32_t free_pages) {
    (void)message;
    (void)mmap_entries;
    (void)total_memory;
    (void)free_memory;
    (void)free_pages;
    panic_count++;
}

static mmap_entry_t memory_map[] = {
    {0U, 0U, MIN_PHYSICAL_MEMORY, 0U, 1U, 0U}
};

static int test_uninitialized_contract(void) {
    memory_detailed_stats_t detailed;

    memory_get_heap_stats(0);
    memory_get_pmm_stats(0);
    if (check_result(memory_get_detailed_stats(0), ERR_NULL,
                     "detailed stats null") != OK) return ERR_STATE;
    if (check_result(memory_get_detailed_stats(&detailed), ERR_STATE,
                     "detailed stats before init") != OK) return ERR_STATE;
    memory_init(0U);
    return check(panic_count == 1U, "null map reports panic");
}

static int test_initial_stats(void) {
    memory_detailed_stats_t detailed;
    memory_heap_stats_t heap;
    memory_pmm_stats_t pmm;

    if (check_result(memory_host_init(memory_map, 1U), OK,
                     "host memory init") != OK) return ERR_STATE;
    if (check(memory_get_total() == MIN_PHYSICAL_MEMORY,
              "total memory") != OK) return ERR_STATE;
    if (check(memory_get_total_pages() == MIN_PHYSICAL_MEMORY / PAGE_SIZE,
              "total pages") != OK) return ERR_STATE;
    if (check(memory_get_mmap_entries() == 1U, "mmap entries") != OK) {
        return ERR_STATE;
    }
    if (check(memory_get_free_pages() == 3072U, "free pages") != OK) {
        return ERR_STATE;
    }
    if (check(memory_get_free() == 3072U * PAGE_SIZE,
              "free memory") != OK) return ERR_STATE;
    if (check(memory_get_used() == 0x01400000U, "used memory") != OK) {
        return ERR_STATE;
    }
    memory_get_heap_stats(&heap);
    if (check(!heap.initialized && !heap.valid,
              "heap starts uninitialized") != OK) return ERR_STATE;
    memory_get_pmm_stats(&pmm);
    if (check(pmm.initialized && pmm.owned_pages == 0U,
              "pmm starts empty") != OK) return ERR_STATE;
    if (check_result(memory_get_detailed_stats(&detailed), OK,
                     "initial detailed stats") != OK) return ERR_STATE;
    if (check(detailed.valid && detailed.total_pages == 8192U,
              "detailed stats valid") != OK) return ERR_STATE;
    if (check(detailed.zone_pages[MEMORY_ZONE_KERNEL] == 4096U &&
              detailed.zone_pages[MEMORY_ZONE_HEAP] == 1024U &&
              detailed.zone_pages[MEMORY_ZONE_FREE] == 3072U,
              "initial zone relation") != OK) return ERR_STATE;
    return OK;
}

static int test_pmm_operations(void) {
    memory_detailed_stats_t before;
    memory_detailed_stats_t after;
    memory_pmm_stats_t pmm;
    void* pages[MEMORY_TEST_PAGE_COUNT] = {0};
    void* range;
    void* single_page;
    uint32_t invalid_before;
    uint32_t failures_before;

    if (memory_get_detailed_stats(&before) != OK) return ERR_STATE;
    memory_get_pmm_stats(&pmm);
    invalid_before = pmm.invalid_frees;
    failures_before = pmm.allocation_failures;
    if (check(!pmm_alloc_pages_in_zone(0U, MEMORY_ZONE_KERNEL),
              "zero page allocation rejected") != OK) return ERR_STATE;
    if (check(!pmm_alloc_pages_in_zone(1U, MEMORY_ZONE_HEAP),
              "heap allocation rejected") != OK) return ERR_STATE;
    if (check(!pmm_alloc_pages_in_zone(1U, MEMORY_ZONE_FREE),
              "free zone allocation rejected") != OK) return ERR_STATE;
    if (check(!pmm_alloc_pages(MIN_PHYSICAL_MEMORY / PAGE_SIZE + 1U),
              "oversized allocation rejected") != OK) return ERR_STATE;
    memory_get_pmm_stats(&pmm);
    if (check(pmm.allocation_failures >= failures_before + 4U,
              "allocation failures counted") != OK) return ERR_STATE;

    single_page = pmm_alloc_page();
    if (check(single_page != 0, "single page allocated") != OK) return ERR_STATE;
    pmm_free_page(single_page);
    pages[0] = pmm_alloc_page_in_zone(MEMORY_ZONE_KERNEL);
    pages[1] = pmm_alloc_page_in_zone(MEMORY_ZONE_SLAB);
    pages[2] = pmm_alloc_page_in_zone(MEMORY_ZONE_PROCESS);
    pages[3] = pmm_alloc_page_in_zone(MEMORY_ZONE_BUFFER);
    for (uint32_t index = 0U; index < MEMORY_TEST_PAGE_COUNT; index++) {
        if (check(pages[index] != 0, "zone page allocated") != OK) {
            return ERR_STATE;
        }
    }
    range = pmm_alloc_pages(2U);
    if (check(range != 0, "contiguous pages allocated") != OK) return ERR_STATE;
    memory_get_pmm_stats(&pmm);
    if (check(pmm.owned_pages == MEMORY_TEST_PAGE_COUNT + 2U,
              "owned pages counted") != OK) return ERR_STATE;
    if (memory_get_detailed_stats(&after) != OK) return ERR_STATE;
    if (check(after.zone_pages[MEMORY_ZONE_SLAB] == 1U &&
              after.zone_pages[MEMORY_ZONE_PROCESS] == 1U &&
              after.zone_pages[MEMORY_ZONE_BUFFER] == 1U,
              "dynamic zones counted") != OK) return ERR_STATE;

    pmm_free_pages(range, 2U);
    for (uint32_t index = 0U; index < MEMORY_TEST_PAGE_COUNT; index++) {
        pmm_free_page(pages[index]);
    }
    pmm_free_page(pages[0]);
    pmm_free_page((void*)(uintptr_t)MEMORY_TEST_INVALID_PAGE);
    pmm_free_pages((void*)(uintptr_t)MEMORY_TEST_OUTSIDE_RAM, 1U);
    pmm_free_pages((void*)(uintptr_t)PHYSICAL_IDENTITY_START, 0U);
    memory_get_pmm_stats(&pmm);
    if (check(pmm.owned_pages == 0U, "pmm cleanup") != OK) return ERR_STATE;
    if (check(pmm.invalid_frees >= invalid_before + 4U,
              "invalid frees counted") != OK) return ERR_STATE;
    if (memory_get_detailed_stats(&after) != OK) return ERR_STATE;
    if (check(after.zone_pages[MEMORY_ZONE_FREE] ==
                  before.zone_pages[MEMORY_ZONE_FREE],
              "free zone restored") != OK) return ERR_STATE;
    return check(after.valid && after.total_pages == before.total_pages,
                 "detailed stats restored");
}

static int test_heap_operations(void) {
    memory_heap_stats_t before;
    memory_heap_stats_t active;
    memory_heap_stats_t after;
    memory_pmm_stats_t pmm_before;
    memory_pmm_stats_t pmm_after;
    uint32_t failures_before;
    uint32_t invalid_before;
    uint32_t double_before;
    void* first;
    void* second;
    void* third;
    void* aligned;
    int outside = 0;

    memory_get_heap_stats(&before);
    memory_get_pmm_stats(&pmm_before);
    failures_before = before.allocation_failures;
    invalid_before = before.invalid_frees;
    double_before = before.double_frees;
    first = kmalloc(1U);
    second = kmalloc(33U);
    third = kmalloc(4096U);
    aligned = kmalloc_aligned(123U);
    if (check(first && second && third && aligned,
              "heap allocations") != OK) return ERR_STATE;
    if (check(((uintptr_t)first % 8U) == 0U &&
              ((uintptr_t)aligned % PAGE_SIZE) == 0U,
              "heap alignments") != OK) return ERR_STATE;
    ((uint8_t*)first)[0] = 0xA5U;
    ((uint8_t*)second)[32] = 0x5AU;
    ((uint8_t*)third)[4095] = 0x3CU;
    ((uint8_t*)aligned)[0] = 0xC3U;
    if (check(((uint8_t*)first)[0] == 0xA5U &&
              ((uint8_t*)second)[32] == 0x5AU &&
              ((uint8_t*)third)[4095] == 0x3CU &&
              ((uint8_t*)aligned)[0] == 0xC3U,
              "heap storage") != OK) return ERR_STATE;
    memory_get_heap_stats(&active);
    if (check(active.initialized && active.valid &&
              active.allocated_blocks == 4U && active.used_bytes > 0U,
              "active heap stats") != OK) return ERR_STATE;

    if (check(!kmalloc(0U) && !kmalloc(HEAP_SIZE),
              "invalid heap sizes") != OK) return ERR_STATE;
    if (check(!kmalloc_aligned(0U) && !kmalloc_aligned(HEAP_SIZE),
              "invalid aligned sizes") != OK) return ERR_STATE;
    kfree(&outside);
    kfree(second);
    kfree(first);
    kfree(second);
    kfree(third);
    kfree(aligned);
    kfree(aligned);
    kfree(0);
    memory_get_heap_stats(&after);
    if (check(after.valid && after.allocated_blocks == 0U &&
              after.free_blocks == 1U && after.used_bytes == 0U &&
              after.free_bytes == after.total_bytes &&
              after.largest_free_block == after.free_bytes &&
              after.fragmentation_percent == 0U,
              "heap cleanup") != OK) return ERR_STATE;
    if (check(after.allocation_failures >= failures_before + 4U,
              "heap allocation failures counted") != OK) return ERR_STATE;
    if (check(after.invalid_frees >= invalid_before + 1U,
              "heap invalid frees counted") != OK) return ERR_STATE;
    if (check(after.double_frees >= double_before + 2U,
              "heap double frees counted") != OK) return ERR_STATE;
    memory_get_pmm_stats(&pmm_after);
    if (check(pmm_after.owned_pages == pmm_before.owned_pages &&
              pmm_after.invalid_frees == pmm_before.invalid_frees,
              "heap leaves pmm unchanged") != OK) return ERR_STATE;
    return OK;
}

static int test_heap_reuse(void) {
    memory_heap_stats_t before;
    memory_heap_stats_t after;
    void* pointer;

    memory_get_heap_stats(&before);
    pointer = kmalloc(64U);
    if (check(pointer != 0, "heap reuse allocation") != OK) return ERR_STATE;
    kfree(pointer);
    memory_get_heap_stats(&after);
    return check(after.valid && after.allocated_blocks == 0U &&
                     after.free_blocks == before.free_blocks,
                 "heap reuse cleanup");
}

int main(void) {
    int result = OK;

    coverage_active = 1U;
    log_init();
    if (result == OK) result = test_uninitialized_contract();
    if (result == OK) result = test_initial_stats();
    if (result == OK) result = test_pmm_operations();
    if (result == OK) result = test_heap_operations();
    if (result == OK) result = test_heap_reuse();
    coverage_active = 0U;
    coverage_emit(result);
    return result == OK ? 0 : 1;
}
