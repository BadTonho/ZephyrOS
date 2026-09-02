#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "kernel_tests.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/timer.h"
#include "core/video.h"
#include "drivers/serial.h"
#include "memory/slab.h"

#define HOST_COVERAGE_CAPACITY 8192U
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
    printf("ZCOV_BEGIN|case=host:memory:slab-metadata|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:memory:slab-metadata|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:memory:slab-metadata|value=0x%08X\n",
           (uint32_t)result);
}

const kernel_tests_runtime_t* kernel_tests_active_runtime;

int kernel_tests_progress(const kernel_tests_runtime_t* runtime) {
    (void)runtime;
    return OK;
}

int paging_is_ready(void) {
    return 1;
}

void* pmm_alloc_pages_in_zone(uint32_t count, memory_zone_t zone) {
    (void)count;
    (void)zone;
    return 0;
}

void pmm_free_pages(void* address, uint32_t count) {
    (void)address;
    (void)count;
}

void memory_get_pmm_stats(memory_pmm_stats_t* stats) {
    if (!stats) return;
    stats->owned_pages = 0U;
    stats->allocation_failures = 0U;
    stats->invalid_frees = 0U;
    stats->initialized = 1U;
}

uint32_t timer_get_ticks(void) {
    static uint32_t tick = 100U;

    return tick++;
}

uint32_t timer_get_frequency(void) {
    return 1000U;
}

uint8_t serial_is_ready(void) {
    return 1U;
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

static int check_metadata(void) {
    kmem_cache_info_t info;
    kmem_slab_stats_t stats;
    kmem_cache_t* cache;

    if (kmem_cache_validate() != ERR_UNAVAILABLE) return 1;
    if (kmem_cache_init() != OK || kmem_cache_init() != OK) return 2;
    if (kmem_cache_get_count() != 0U) return 3;
    if (kmem_cache_get_info_at(KMEM_CACHE_MAX, &info) != ERR_INVALID) {
        return 4;
    }
    if (kmem_cache_get_info_at(0U, NULL) != ERR_NULL) return 5;
    if (kmem_cache_get_info_at(0U, &info) != OK || info.initialized) {
        return 6;
    }
    if (kmem_cache_create(NULL, 16U, 16U) != NULL ||
        kmem_cache_create("", 16U, 16U) != NULL ||
        kmem_cache_create("zero", 0U, 16U) != NULL ||
        kmem_cache_create("align", 16U, 3U) != NULL ||
        kmem_cache_create("large-align", 16U, PAGE_SIZE * 2U) != NULL) {
        return 7;
    }
    cache = kmem_cache_create("metadata", 24U, 8U);
    if (!cache || kmem_cache_get_count() != 1U) return 8;
    if (kmem_cache_create("metadata", 24U, 8U) != NULL) return 9;
    if (kmem_cache_get_info(NULL, &info) != ERR_INVALID ||
        kmem_cache_get_info(cache, NULL) != ERR_NULL ||
        kmem_cache_get_info(cache, &info) != OK) return 10;
    if (info.object_size != 24U || info.alignment != 8U ||
        info.object_stride != 24U || info.objects_per_slab == 0U ||
        info.capacity != 0U || info.slabs != 0U || info.pages != 0U ||
        !info.initialized) return 11;
    if (kmem_cache_get_info_at(0U, &info) != OK || !info.initialized ||
        kmem_cache_get_info_at(1U, &info) != OK || info.initialized) {
        return 12;
    }
    if (kmem_cache_owns(cache, NULL) || kmem_cache_owns(NULL, &info) ||
        kmem_cache_validate() != OK) return 13;
    kmem_cache_get_stats(NULL);
    kmem_cache_get_stats(&stats);
    if (!stats.initialized || !stats.valid || stats.caches != 1U ||
        stats.slabs != 0U || stats.active_objects != 0U ||
        stats.capacity != 0U) return 14;
    if (kmem_cache_destroy(NULL) != ERR_INVALID ||
        kmem_cache_destroy(cache) != OK || kmem_cache_get_count() != 0U ||
        kmem_cache_validate() != OK) return 15;
    if (kmem_cache_get_info_at(0U, &info) != OK || info.initialized) {
        return 16;
    }
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    log_init();
    result = check_metadata();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
