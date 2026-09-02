#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/recovery.h"
#include "core/string.h"
#include "core/video.h"
#include "drivers/serial.h"
#include "drivers/vesa.h"
#include "memory/paging.h"
#include "process/process.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define PAGING_TEST_PAGE_COUNT 96U
#define PAGING_TEST_USER_PHYSICAL 0x02000000U
#define PAGING_TEST_LAZY_ADDRESS (USER_LAUNCH_BASE + PAGE_SIZE)
#define PAGING_TEST_BUFFER_SIZE 32U

int paging_host_register_user_buffer(uint32_t address, void* pointer,
                                     uint32_t size);
extern page_directory_t* current_directory;

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
    printf("ZCOV_BEGIN|case=host:memory:paging|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:memory:paging|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:memory:paging|value=0x%08X\n",
           (uint32_t)result);
}

static uint8_t page_pool[PAGING_TEST_PAGE_COUNT][PAGE_SIZE]
    __attribute__((aligned(PAGE_SIZE)));
static uint8_t page_pool_used[PAGING_TEST_PAGE_COUNT];
static uint32_t pmm_allocations;
static uint32_t pmm_releases;
static uint32_t pmm_fail_after;
static uint32_t fake_ticks;
static uint32_t fake_vma_calls;
static int fake_vma_enabled;
static vesa_mode_t fake_vesa_mode;
static vesa_mode_t* fake_vesa_mode_pointer;
static uint32_t fake_vesa_disable_calls;
static uint32_t fake_framebuffer[PAGE_SIZE / sizeof(uint32_t)]
    __attribute__((aligned(PAGE_SIZE)));
static process_t fake_process;

static uint32_t pointer_address(const void* pointer) {
    union {
        const void* pointer;
        uintptr_t address;
    } value;

    value.pointer = pointer;
    return (uint32_t)value.address;
}

static void reset_pmm(void) {
    kmemset(page_pool_used, 0, sizeof(page_pool_used));
    pmm_allocations = 0U;
    pmm_releases = 0U;
    pmm_fail_after = 0xFFFFFFFFU;
}

static void* allocate_page(void) {
    if (pmm_allocations >= pmm_fail_after) return NULL;
    for (uint32_t index = 0U; index < PAGING_TEST_PAGE_COUNT; index++) {
        if (!page_pool_used[index]) {
            page_pool_used[index] = 1U;
            pmm_allocations++;
            return page_pool[index];
        }
    }
    return NULL;
}

void* pmm_alloc_page(void) {
    return allocate_page();
}

void* pmm_alloc_page_in_zone(memory_zone_t zone) {
    (void)zone;
    return allocate_page();
}

void pmm_free_page(void* address) {
    uintptr_t value = (uintptr_t)address;

    for (uint32_t index = 0U; index < PAGING_TEST_PAGE_COUNT; index++) {
        if (address == page_pool[index]) {
            if (page_pool_used[index]) {
                page_pool_used[index] = 0U;
                pmm_releases++;
            }
            return;
        }
    }
    if ((uint32_t)value != 0U) pmm_releases++;
}

uint32_t memory_get_total(void) {
    return MIN_PHYSICAL_MEMORY;
}

uint32_t timer_get_ticks(void) {
    return fake_ticks++;
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

vesa_mode_t* vesa_get_mode(void) {
    return fake_vesa_mode_pointer;
}

void vesa_disable(void) {
    fake_vesa_disable_calls++;
}

void video_disable_framebuffer(void) {
}

int recovery_mark_disabled(recovery_component_id_t component, int error_code,
                           const char* message) {
    (void)component;
    (void)error_code;
    (void)message;
    return OK;
}

process_t* process_get_current(void) {
    return &fake_process;
}

int process_is_user(const process_t* process) {
    return process && process->context.user_mode != 0U;
}

int process_vma_ensure_page(process_t* process, uint32_t address, int write) {
    uint32_t flags = PAGING_FLAG_PRESENT | PAGING_FLAG_USER;

    if (!process || !fake_vma_enabled) return ERR_UNAVAILABLE;
    fake_vma_calls++;
    if (write) flags |= PAGING_FLAG_WRITE;
    return paging_map_page_in_directory(paging_get_current_directory(),
                                         address, PAGING_TEST_USER_PHYSICAL,
                                         flags);
}

static int check(int condition, const char* name) {
    if (condition) return OK;
    printf("paging-host failure: %s\n", name);
    return ERR_STATE;
}

static int check_result(int actual, int expected, const char* name) {
    return check(actual == expected, name);
}

static int test_paging(void) {
    paging_boot_stats_t boot_stats;
    paging_user_stats_t user_stats;
    page_directory_t* kernel_dir;
    page_directory_t* user_dir;
    page_directory_t* temporary_dir;
    uint32_t page_count;
    uint32_t before_rejected;
    uint8_t user_buffer[PAGING_TEST_BUFFER_SIZE];
    uint8_t destination[PAGING_TEST_BUFFER_SIZE];
    const uint8_t source[] = "paging-copy";
    uint32_t before_allocations;
    uint32_t before_releases;
    int result;

    reset_pmm();
    fake_ticks = 100U;
    fake_vma_calls = 0U;
    fake_vma_enabled = 0;
    kmemset(&fake_vesa_mode, 0, sizeof(fake_vesa_mode));
    fake_vesa_mode.width = 1U;
    fake_vesa_mode.height = 1U;
    fake_vesa_mode.pitch = PAGE_SIZE;
    fake_vesa_mode.framebuffer = fake_framebuffer;
    fake_vesa_mode.initialized = 1U;
    fake_vesa_mode_pointer = &fake_vesa_mode;
    fake_vesa_disable_calls = 0U;
    kmemset(&fake_process, 0, sizeof(fake_process));
    fake_process.context.user_mode = 1U;
    kmemset(user_buffer, 0, sizeof(user_buffer));
    kmemset(destination, 0, sizeof(destination));

    if (check_result(paging_get_boot_stats(&boot_stats), ERR_STATE,
                     "boot stats before init") != OK) return ERR_STATE;
    if (check_result(paging_get_boot_stats(NULL), ERR_NULL,
                     "boot stats null") != OK) return ERR_STATE;
    if (check_result(paging_init(), OK, "paging init") != OK) return ERR_STATE;
    if (check_result(paging_init(), OK, "paging init idempotent") != OK) {
        return ERR_STATE;
    }
    if (check(paging_is_ready(), "paging ready") != OK) return ERR_STATE;
    if (check_result(paging_get_boot_stats(&boot_stats), OK,
                     "boot stats after init") != OK) return ERR_STATE;
    if (check(boot_stats.initialized && boot_stats.identity_pages > 0U &&
              boot_stats.page_tables_created > 0U,
              "boot stats values") != OK) return ERR_STATE;
    if (check(fake_vesa_disable_calls == 0U,
              "valid framebuffer mapping") != OK) return ERR_STATE;

    kernel_dir = paging_get_current_directory();
    if (check(kernel_dir != NULL, "current directory") != OK) return ERR_STATE;
    if (check(paging_get_page(0U, 0) != NULL,
              "current page lookup") != OK) return ERR_STATE;
    if (check(paging_get_page_in_directory(NULL, 0U, 0) == NULL,
              "page null directory") != OK) return ERR_STATE;
    if (check(paging_get_page_in_directory(kernel_dir, 0U, 2) == NULL,
              "page invalid create") != OK) return ERR_STATE;
    if (check_result(paging_map_page_in_directory(NULL, 0U, 0U, 0U),
                     ERR_NULL, "map null directory") != OK) return ERR_STATE;
    if (check_result(paging_map_page_in_directory(kernel_dir, 1U, 0U, 0U),
                     ERR_INVALID, "map unaligned") != OK) return ERR_STATE;
    if (check_result(paging_map_page_in_directory(kernel_dir, 0U, 0U, 8U),
                     ERR_INVALID, "map invalid flags") != OK) return ERR_STATE;
    if (check_result(paging_map_page(0U, 0U, 0U), OK, "map kernel page") != OK) {
        return ERR_STATE;
    }

    temporary_dir = paging_create_directory();
    if (check(temporary_dir != NULL, "temporary directory") != OK) {
        return ERR_STATE;
    }
    if (check_result(paging_map_page_in_directory(temporary_dir, 0U,
                                                  PAGE_SIZE, 0U), OK,
                     "map temporary page") != OK) return ERR_STATE;
    paging_free_directory(temporary_dir);

    user_dir = paging_create_user_directory();
    if (check(user_dir != NULL, "user directory") != OK) return ERR_STATE;
    paging_get_user_stats(&user_stats);
    if (check(user_stats.active_directories == 1U &&
              user_stats.directories_created == 1U,
              "user stats after create") != OK) return ERR_STATE;
    if (check_result(paging_map_page_in_directory(
                         user_dir, USER_DATA_BASE, PAGING_TEST_USER_PHYSICAL,
                         PAGING_FLAG_PRESENT | PAGING_FLAG_WRITE |
                         PAGING_FLAG_USER),
                     OK, "map user page") != OK) return ERR_STATE;
    if (check_result(paging_map_page_in_directory(
                         user_dir, USER_DATA_BASE, PAGING_TEST_USER_PHYSICAL,
                         PAGING_FLAG_PRESENT | PAGING_FLAG_USER),
                     ERR_STATE, "remap user page") != OK) return ERR_STATE;
    if (check_result(paging_map_page_in_directory(
                         user_dir, USER_SPACE_END, PAGING_TEST_USER_PHYSICAL,
                         PAGING_FLAG_PRESENT | PAGING_FLAG_USER),
                     ERR_INVALID, "user page outside range") != OK) {
        return ERR_STATE;
    }
    if (check_result(paging_get_user_page_count(user_dir, &page_count), OK,
                     "user page count") != OK ||
        check(page_count == 1U, "user page count value") != OK) {
        return ERR_STATE;
    }
    if (check_result(paging_get_user_page_count(NULL, &page_count), ERR_NULL,
                     "user page count null") != OK) return ERR_STATE;

    paging_switch_directory(user_dir);
    if (check(paging_get_current_directory() == user_dir,
              "switch user directory") != OK) return ERR_STATE;
    if (check_result(paging_validate_user_range(USER_DATA_BASE, PAGE_SIZE, 0),
                     OK, "validate readable page") != OK) return ERR_STATE;
    if (check_result(paging_validate_user_range(USER_DATA_BASE, PAGE_SIZE, 1),
                     OK, "validate writable page") != OK) return ERR_STATE;
    if (check_result(paging_validate_user_range(USER_DATA_BASE, 0U, 0),
                     ERR_NULL, "validate empty range") != OK) return ERR_STATE;
    if (check_result(paging_validate_user_range(USER_SPACE_END - 1U, 2U, 0),
                     ERR_INVALID, "validate overflowing range") != OK) {
        return ERR_STATE;
    }
    if (check_result(paging_map_page_in_directory(
                         user_dir, USER_CODE_BASE, PAGING_TEST_USER_PHYSICAL,
                         PAGING_FLAG_PRESENT | PAGING_FLAG_USER),
                     OK, "map readonly page") != OK) return ERR_STATE;
    if (check_result(paging_validate_user_range(USER_CODE_BASE, PAGE_SIZE, 1),
                     ERR_UNAVAILABLE, "reject readonly write") != OK) {
        return ERR_STATE;
    }

    fake_vma_enabled = 1;
    if (check_result(paging_validate_user_range(PAGING_TEST_LAZY_ADDRESS,
                                                PAGE_SIZE, 1), OK,
                     "lazy page materialization") != OK) return ERR_STATE;
    if (check(fake_vma_calls == 1U, "lazy page callback") != OK) return ERR_STATE;
    fake_vma_enabled = 0;
    if (check_result(paging_validate_user_range(USER_STACK_BASE, PAGE_SIZE, 0),
                     ERR_UNAVAILABLE, "missing page unavailable") != OK) {
        return ERR_STATE;
    }

    if (check_result(paging_host_register_user_buffer(
                         USER_DATA_BASE, user_buffer, sizeof(user_buffer)),
                     OK, "register user buffer") != OK) return ERR_STATE;
    if (check_result(paging_host_register_user_buffer(
                         USER_SPACE_END, user_buffer, sizeof(user_buffer)),
                     ERR_INVALID, "register invalid user buffer") != OK) {
        return ERR_STATE;
    }
    kmemcpy(user_buffer, source, sizeof(source));
    if (check_result(paging_copy_from_user(destination, user_buffer,
                                           sizeof(source)), OK,
                     "copy from user") != OK) return ERR_STATE;
    if (check(memcmp(destination, source, sizeof(source)) == 0,
              "copy from user data") != OK) return ERR_STATE;
    kmemset(destination, 0, sizeof(destination));
    if (check_result(paging_copy_to_user(user_buffer, source, sizeof(source)),
                     OK, "copy to user") != OK) return ERR_STATE;
    if (check(memcmp(user_buffer, source, sizeof(source)) == 0,
              "copy to user data") != OK) return ERR_STATE;
    if (check_result(paging_copy_from_user(NULL, user_buffer, sizeof(source)),
                     ERR_NULL, "copy null destination") != OK) return ERR_STATE;

    if (check_result(paging_unmap_user_page_in_directory(user_dir,
                                                         USER_CODE_BASE), OK,
                     "unmap readonly page") != OK) return ERR_STATE;
    if (check_result(paging_unmap_user_page_in_directory(user_dir,
                                                         USER_CODE_BASE),
                     ERR_NOT_FOUND, "unmap missing page") != OK) {
        return ERR_STATE;
    }
    if (check_result(paging_unmap_user_page_in_directory(user_dir,
                                                         USER_SPACE_END),
                     ERR_INVALID, "unmap outside range") != OK) {
        return ERR_STATE;
    }
    paging_switch_directory(kernel_dir);
    paging_get_user_stats(&user_stats);
    if (check(user_stats.active_pages == 2U, "user stats before release") != OK) {
        return ERR_STATE;
    }
    before_rejected = user_stats.rejected_releases;
    paging_free_user_directory(kernel_dir);
    paging_get_user_stats(&user_stats);
    if (check(user_stats.rejected_releases == before_rejected + 1U,
              "reject kernel directory release") != OK) return ERR_STATE;
    paging_free_user_directory(user_dir);
    paging_get_user_stats(&user_stats);
    if (check(user_stats.active_directories == 0U && user_stats.active_pages == 0U &&
              user_stats.directories_released == 1U,
              "user stats after release") != OK) return ERR_STATE;

    before_allocations = pmm_allocations;
    before_releases = pmm_releases;
    current_directory = NULL;
    paging_free_directory(kernel_dir);
    if (check(pmm_releases > before_releases &&
              pmm_allocations >= before_allocations,
              "kernel cleanup") != OK) return ERR_STATE;
    if (check(!paging_is_ready(), "paging not ready after cleanup") != OK) {
        return ERR_STATE;
    }

    pmm_fail_after = pmm_allocations + 1U;
    result = paging_init();
    if (check_result(result, ERR_MEM, "paging init allocation failure") != OK) {
        return ERR_STATE;
    }
    pmm_fail_after = 0xFFFFFFFFU;
    fake_vesa_mode.pitch = 0xFFFFFFFFU;
    fake_vesa_mode.height = 2U;
    if (check_result(paging_init(), OK, "paging init after recovery") != OK) {
        return ERR_STATE;
    }
    if (check(fake_vesa_disable_calls == 1U,
              "invalid framebuffer fallback") != OK) return ERR_STATE;
    kernel_dir = paging_get_current_directory();
    current_directory = NULL;
    paging_free_directory(kernel_dir);
    return OK;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = test_paging();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != OK) return 1;
    printf("paging-host: PASS\n");
    return 0;
}
