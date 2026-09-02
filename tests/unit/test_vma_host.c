#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/video.h"
#include "drivers/serial.h"
#include "memory/paging.h"
#include "memory/vma.h"
#include "process/process.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define VMA_TEST_POOL_CAPACITY 32U
#define VMA_TEST_PAGE_CAPACITY 32U
#define VMA_TEST_IMAGE_SIZE 32U
#define VMA_TEST_DYNAMIC_PAGES 4U
#define VMA_TEST_FAULT_USER 0x04U
#define VMA_TEST_FAULT_WRITE 0x02U
#define VMA_TEST_FAULT_PRESENT 0x01U
#define VMA_TEST_FAULT_RESERVED 0x08U
#define VMA_TEST_FAULT_INSTRUCTION 0x10U

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
    printf("ZCOV_BEGIN|case=host:memory:vma|value=0x%08X\n", coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:memory:vma|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:memory:vma|value=0x%08X\n",
           (uint32_t)result);
}

static process_t* fake_current_process;
static page_directory_t fake_directory;
static page_entry_t fake_pages[VMA_TEST_PAGE_CAPACITY];
static uint32_t fake_page_addresses[VMA_TEST_PAGE_CAPACITY];
static uint8_t fake_page_used[VMA_TEST_PAGE_CAPACITY];
static uint8_t fake_physical_pages[VMA_TEST_PAGE_CAPACITY][PAGE_SIZE];
static uint8_t fake_physical_used[VMA_TEST_PAGE_CAPACITY];
static uint32_t fake_allocated_pages;
static int fake_map_result;

static vm_area_t fake_vma_pool[VMA_TEST_POOL_CAPACITY];
static uint8_t fake_vma_used[VMA_TEST_POOL_CAPACITY];

uint32_t timer_get_ticks(void) {
    return 0U;
}

static void reset_fixture(void) {
    fake_current_process = NULL;
    kmemset(&fake_directory, 0, sizeof(fake_directory));
    kmemset(fake_pages, 0, sizeof(fake_pages));
    kmemset(fake_page_addresses, 0, sizeof(fake_page_addresses));
    kmemset(fake_page_used, 0, sizeof(fake_page_used));
    kmemset(fake_physical_used, 0, sizeof(fake_physical_used));
    kmemset(fake_vma_pool, 0, sizeof(fake_vma_pool));
    kmemset(fake_vma_used, 0, sizeof(fake_vma_used));
    fake_allocated_pages = 0U;
    fake_map_result = OK;
}

static int check(int condition, const char* name) {
    if (condition) return OK;
    printf("vma-host failure: %s\n", name);
    return ERR_STATE;
}

static int check_result(int actual, int expected, const char* name) {
    return check(actual == expected, name);
}

process_t* process_get_current(void) {
    return fake_current_process;
}

int process_is_user(const process_t* process) {
    return process && process->context.user_mode != 0U;
}

void* kmalloc(uint32_t size) {
    if (size > sizeof(fake_vma_pool[0])) return NULL;
    for (uint32_t index = 0U; index < VMA_TEST_POOL_CAPACITY; index++) {
        if (!fake_vma_used[index]) {
            fake_vma_used[index] = 1U;
            return &fake_vma_pool[index];
        }
    }
    return NULL;
}

void kfree(void* pointer) {
    if (!pointer) return;
    for (uint32_t index = 0U; index < VMA_TEST_POOL_CAPACITY; index++) {
        if (pointer == &fake_vma_pool[index]) fake_vma_used[index] = 0U;
    }
}

page_directory_t* paging_get_current_directory(void) {
    return &fake_directory;
}

page_entry_t* paging_get_page_in_directory(page_directory_t* directory,
                                           uint32_t virtual_address,
                                           int create) {
    (void)create;
    if (directory != &fake_directory) return NULL;
    for (uint32_t index = 0U; index < VMA_TEST_PAGE_CAPACITY; index++) {
        if (fake_page_used[index] &&
            fake_page_addresses[index] == virtual_address) {
            return &fake_pages[index];
        }
    }
    return NULL;
}

int paging_map_page_in_directory(page_directory_t* directory,
                                 uint32_t virtual_address,
                                 uint32_t physical,
                                 uint32_t flags) {
    (void)physical;
    if (directory != &fake_directory) return ERR_STATE;
    if (fake_map_result != OK) return fake_map_result;
    for (uint32_t index = 0U; index < VMA_TEST_PAGE_CAPACITY; index++) {
        if (!fake_page_used[index]) {
            fake_page_used[index] = 1U;
            fake_page_addresses[index] = virtual_address;
            fake_pages[index].present = (flags & PAGING_FLAG_PRESENT) != 0U;
            fake_pages[index].rw = (flags & PAGING_FLAG_WRITE) != 0U;
            fake_pages[index].user = (flags & PAGING_FLAG_USER) != 0U;
            return OK;
        }
    }
    return ERR_OVERFLOW;
}

int paging_unmap_user_page_in_directory(page_directory_t* directory,
                                        uint32_t virtual_address) {
    if (directory != &fake_directory) return ERR_STATE;
    for (uint32_t index = 0U; index < VMA_TEST_PAGE_CAPACITY; index++) {
        if (fake_page_used[index] &&
            fake_page_addresses[index] == virtual_address) {
            fake_page_used[index] = 0U;
            fake_pages[index].present = 0U;
            if (fake_allocated_pages) fake_allocated_pages--;
            return OK;
        }
    }
    return ERR_NOT_FOUND;
}

void* pmm_alloc_page_in_zone(memory_zone_t zone) {
    (void)zone;
    for (uint32_t index = 0U; index < VMA_TEST_PAGE_CAPACITY; index++) {
        if (!fake_physical_used[index]) {
            fake_physical_used[index] = 1U;
            fake_allocated_pages++;
            return fake_physical_pages[index];
        }
    }
    return NULL;
}

void pmm_free_page(void* address) {
    for (uint32_t index = 0U; index < VMA_TEST_PAGE_CAPACITY; index++) {
        if (address == fake_physical_pages[index]) {
            fake_physical_used[index] = 0U;
            if (fake_allocated_pages) fake_allocated_pages--;
        }
    }
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

static int test_preconditions(process_t* process) {
    uint32_t address = 0U;
    page_fault_stats_t stats;

    if (check_result(process_vma_get_page_fault_stats(NULL), ERR_NULL,
                     "stats null") != OK) return ERR_STATE;
    if (check_result(process_vma_get_page_fault_stats(&stats), OK,
                     "stats") != OK) return ERR_STATE;
    if (check_result(process_vma_register_image(NULL, 1U), ERR_INVALID,
                     "register null") != OK) return ERR_STATE;
    if (check_result(process_vma_register_image(process, 0U), ERR_INVALID,
                     "register zero") != OK) return ERR_STATE;
    if (check_result(process_vma_mmap(process, PAGE_SIZE, VM_READ,
                                      VM_ANONYMOUS, &address), ERR_STATE,
                     "mmap before current") != OK) return ERR_STATE;
    if (check_result(process_vma_ensure_page(process, USER_CODE_BASE, 0),
                     ERR_STATE, "ensure before current") != OK) return ERR_STATE;
    return check_result(process_vma_handle_page_fault(
                            process, USER_CODE_BASE, VMA_TEST_FAULT_USER),
                        ERR_STATE, "fault before current");
}

static int test_image_and_faults(process_t* process) {
    uint8_t code[VMA_TEST_IMAGE_SIZE];
    uint8_t data[VMA_TEST_IMAGE_SIZE];
    uint32_t address = 0U;
    page_fault_stats_t before;
    page_fault_stats_t after;

    for (uint32_t index = 0U; index < sizeof(code); index++) {
        code[index] = (uint8_t)(index + 1U);
        data[index] = (uint8_t)(0xA0U + index);
    }
    process->user_code_image = code;
    process->user_code_size = sizeof(code);
    process->user_data_image = data;
    process->user_data_size = sizeof(data);
    if (check_result(process_vma_register_image(process, sizeof(code)), OK,
                     "register image") != OK) return ERR_STATE;
    if (check(process->vma_count == 4U && process->vma_list != NULL,
              "fixed vmas") != OK) return ERR_STATE;
    if (check_result(process_vma_register_image(process, sizeof(code)),
                     ERR_STATE, "duplicate image") != OK) return ERR_STATE;
    if (check_result(process_vma_ensure_page(process, USER_CODE_BASE, 2),
                     ERR_INVALID, "ensure invalid write") != OK) return ERR_STATE;
    if (check_result(process_vma_ensure_page(process, USER_SPACE_START - 1U, 0),
                     ERR_INVALID, "ensure below user") != OK) return ERR_STATE;
    if (check_result(process_vma_ensure_page(process, USER_CODE_BASE, 0), OK,
                     "ensure code") != OK) return ERR_STATE;
    if (check_result(process_vma_ensure_page(process, USER_CODE_BASE, 0), OK,
                     "ensure present code") != OK) return ERR_STATE;
    if (check_result(process_vma_handle_page_fault(
                         process, USER_CODE_BASE, VMA_TEST_FAULT_USER),
                     ERR_INVALID, "fault present") != OK) return ERR_STATE;
    if (check_result(process_vma_handle_page_fault(
                         process, USER_CODE_BASE, VMA_TEST_FAULT_USER |
                         VMA_TEST_FAULT_RESERVED), ERR_INVALID,
                     "fault reserved") != OK) return ERR_STATE;
    if (check_result(process_vma_handle_page_fault(
                         process, USER_CODE_BASE, VMA_TEST_FAULT_USER |
                         VMA_TEST_FAULT_WRITE | VMA_TEST_FAULT_INSTRUCTION),
                     ERR_INVALID, "fault ambiguous") != OK) return ERR_STATE;
    if (check_result(process_vma_handle_page_fault(
                         process, USER_CODE_BASE, VMA_TEST_FAULT_USER |
                         VMA_TEST_FAULT_WRITE), ERR_INVALID,
                     "fault read-only") != OK) return ERR_STATE;
    if (check_result(process_vma_handle_page_fault(
                         process, USER_DATA_BASE, VMA_TEST_FAULT_USER |
                         VMA_TEST_FAULT_WRITE), OK, "fault data") != OK) {
        return ERR_STATE;
    }
    if (check_result(process_vma_handle_page_fault(
                         process, USER_LAUNCH_BASE, VMA_TEST_FAULT_USER), OK,
                     "fault launch") != OK) return ERR_STATE;
    if (check_result(process_vma_handle_page_fault(
                         process, USER_SPACE_END - PAGE_SIZE,
                         VMA_TEST_FAULT_USER), ERR_NOT_FOUND,
                     "fault without vma") != OK) return ERR_STATE;
    if (check_result(process_vma_get_page_fault_stats(&before), OK,
                     "stats before dynamic") != OK) return ERR_STATE;
    if (check_result(process_vma_mmap(process, VMA_TEST_DYNAMIC_PAGES * PAGE_SIZE,
                                      VM_READ | VM_WRITE, VM_ANONYMOUS,
                                      &address), OK, "mmap dynamic") != OK) {
        return ERR_STATE;
    }
    if (check_result(process_vma_handle_page_fault(
                         process, address + PAGE_SIZE,
                         VMA_TEST_FAULT_USER | VMA_TEST_FAULT_WRITE),
                     OK, "fault dynamic") != OK) return ERR_STATE;
    fake_map_result = ERR_UNAVAILABLE;
    if (check_result(process_vma_handle_page_fault(
                         process, address + 2U * PAGE_SIZE,
                         VMA_TEST_FAULT_USER), ERR_UNAVAILABLE,
                     "map unavailable") != OK) return ERR_STATE;
    fake_map_result = OK;
    if (check_result(process_vma_get_page_fault_stats(&after), OK,
                     "stats after dynamic") != OK) return ERR_STATE;
    if (check(after.handled > before.handled &&
              after.invalid > before.invalid, "fault statistics") != OK) {
        return ERR_STATE;
    }
    return OK;
}

static int test_mmap_limits_and_unmap(process_t* process) {
    uint32_t address = 0U;
    vm_area_info_t entries[VMA_TEST_POOL_CAPACITY];
    uint32_t count = 0U;
    int result;

    if (check_result(process_vma_mmap(process, PAGE_SIZE, VM_READ, VM_ANONYMOUS,
                                      NULL), ERR_NULL, "mmap null") != OK) {
        return ERR_STATE;
    }
    if (check_result(process_vma_mmap(process, PAGE_SIZE, 0x80U,
                                      VM_ANONYMOUS, &address), ERR_INVALID,
                     "mmap protection") != OK) return ERR_STATE;
    if (check_result(process_vma_mmap(process, PAGE_SIZE, VM_READ, VM_SHARED,
                                      &address), ERR_UNAVAILABLE,
                     "mmap flags") != OK) return ERR_STATE;
    if (check_result(process_vma_mmap(process, 0U, VM_READ, VM_ANONYMOUS,
                                      &address), ERR_INVALID, "mmap zero") != OK) {
        return ERR_STATE;
    }
    if (check_result(process_vma_mmap(process, 0xFFFFFFFFU, VM_READ,
                                      VM_ANONYMOUS, &address), ERR_OVERFLOW,
                     "mmap overflow") != OK) return ERR_STATE;
    if (check_result(process_vma_copy(process, entries, 0U, &count),
                     ERR_OVERFLOW, "copy capacity") != OK) return ERR_STATE;
    if (check_result(process_vma_copy(process, NULL, 1U, &count), ERR_NULL,
                     "copy null") != OK) return ERR_STATE;
    if (check_result(process_vma_mmap(process, VMA_TEST_DYNAMIC_PAGES * PAGE_SIZE,
                                      VM_READ | VM_WRITE, VM_ANONYMOUS,
                                      &address), OK, "mmap split") != OK) {
        return ERR_STATE;
    }
    if (check_result(process_vma_munmap(process, address + PAGE_SIZE, PAGE_SIZE),
                     OK, "munmap middle") != OK) return ERR_STATE;
    if (check_result(process_vma_munmap(process, address, PAGE_SIZE), OK,
                     "munmap left") != OK) return ERR_STATE;
    if (check_result(process_vma_munmap(process, address + 3U * PAGE_SIZE,
                                        PAGE_SIZE), OK, "munmap right") != OK) {
        return ERR_STATE;
    }
    if (check_result(process_vma_munmap(process, address + 2U * PAGE_SIZE,
                                        PAGE_SIZE), OK, "munmap full") != OK) {
        return ERR_STATE;
    }
    result = process_vma_munmap(process, address + 1U, PAGE_SIZE);
    if (check_result(result, ERR_INVALID, "munmap unaligned") != OK) return ERR_STATE;
    return check_result(process_vma_munmap(process, address, 0U), ERR_INVALID,
                        "munmap zero");
}

int main(void) {
    process_t process;
    uint32_t result = OK;
    uint32_t mapped_pages = 0U;

    reset_fixture();
    kmemset(&process, 0, sizeof(process));
    process.pid = 42U;
    process.page_directory = &fake_directory;
    process.context.user_mode = 1U;
    coverage_active = 1U;
    result = test_preconditions(&process);
    if (result == OK) {
        fake_current_process = &process;
        result = test_image_and_faults(&process);
    }
    if (result == OK) result = test_mmap_limits_and_unmap(&process);
    if (result == OK) {
        result = check_result(process_vma_copy(&process, NULL, 0U, NULL),
                              ERR_NULL, "copy all null");
    }
    process_vma_release(&process);
    for (uint32_t index = 0U; index < VMA_TEST_PAGE_CAPACITY; index++) {
        if (fake_page_used[index]) mapped_pages++;
    }
    if (result == OK) result = check(process.vma_list == NULL &&
                                     process.vma_count == 0U &&
                                     mapped_pages > 0U,
                                     "metadata release");
    kmemset(fake_page_used, 0, sizeof(fake_page_used));
    fake_allocated_pages = 0U;
    if (result == OK) result = check(fake_allocated_pages == 0U,
                                     "page fixture cleanup");
    coverage_active = 0U;
    coverage_emit((int)result);
    if (result == OK) {
        printf("vma-host: PASS\n");
        return 0;
    }
    printf("vma-host: FAIL (%u)\n", result);
    return 1;
}
