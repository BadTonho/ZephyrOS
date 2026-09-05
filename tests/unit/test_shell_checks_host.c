#include <stdint.h>
#include <stdio.h>

#include "apps/shell_checks.h"
#include "core/app_loader.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/video.h"
#include "fs/fs.h"
#include "memory/paging.h"
#include "memory/vma.h"
#include "process/process.h"

#define HOST_COVERAGE_CAPACITY 2048U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t fixture_fs_type = FS_TYPE_FAT32;
static int fixture_loader_ready = 1;
static process_t fixture_current_process;
static uint32_t fixture_focus;
static uint32_t fixture_user_count;
static uint32_t fixture_zombie_count;
static paging_user_stats_t fixture_paging_stats;
static page_fault_stats_t fixture_fault_stats;
static int fixture_fault_stats_result = OK;
static const uint8_t* fixture_read_data;
static uint32_t fixture_read_size;
static int fixture_read_result;
static uint8_t fixture_read_mutate;
static uint32_t fixture_delete_successes;
static uint32_t fixture_delete_calls;

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
    printf("ZCOV_BEGIN|case=host:shell:checks|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:checks|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:checks|value=0x%08X\n",
           (uint32_t)result);
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void shell_command_print_num(uint32_t value) {
    (void)value;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

uint8_t fs_get_type(void) {
    return fixture_fs_type;
}

int app_loader_is_ready(void) {
    return fixture_loader_ready;
}

process_t* process_get_current(void) {
    return fixture_current_process.pid ? &fixture_current_process : NULL;
}

uint32_t process_get_focus(void) {
    return fixture_focus;
}

uint32_t process_get_user_count(void) {
    return fixture_user_count;
}

uint32_t process_get_state_count(process_state_t state) {
    return state == PROCESS_STATE_ZOMBIE ? fixture_zombie_count : 0U;
}

void paging_get_user_stats(paging_user_stats_t* stats) {
    if (stats) *stats = fixture_paging_stats;
}

int process_vma_get_page_fault_stats(page_fault_stats_t* stats) {
    if (fixture_fault_stats_result != OK) return fixture_fault_stats_result;
    if (!stats) return ERR_NULL;
    *stats = fixture_fault_stats;
    return OK;
}

int fs_read_file(const char* filename, uint8_t* buffer, uint32_t max_size) {
    (void)filename;
    if (fixture_read_result != 0) return fixture_read_result;
    if (!buffer || fixture_read_size > max_size || !fixture_read_data) {
        return ERR_INVALID;
    }
    kmemcpy(buffer, fixture_read_data, fixture_read_size);
    if (fixture_read_mutate && fixture_read_size > 0U) buffer[0] ^= 0xFFU;
    return (int)fixture_read_size;
}

int fs_delete_file(const char* filename) {
    (void)filename;
    fixture_delete_calls++;
    return fixture_delete_calls <= fixture_delete_successes ? OK :
           ERR_NOT_FOUND;
}

void shell_checks_host_set_environment(uint8_t fs_type, int loader_ready) {
    fixture_fs_type = fs_type;
    fixture_loader_ready = loader_ready;
}

void shell_checks_host_set_process_snapshot(uint32_t pid, uint32_t focus,
                                             uint32_t user_count,
                                             uint32_t zombie_count) {
    kmemset(&fixture_current_process, 0, sizeof(fixture_current_process));
    fixture_current_process.pid = pid;
    fixture_focus = focus;
    fixture_user_count = user_count;
    fixture_zombie_count = zombie_count;
}

void shell_checks_host_set_vma_snapshot(uint32_t active_pages,
                                         uint32_t active_directories,
                                         uint32_t handled, uint32_t invalid,
                                         int result) {
    kmemset(&fixture_paging_stats, 0, sizeof(fixture_paging_stats));
    fixture_paging_stats.active_pages = active_pages;
    fixture_paging_stats.active_directories = active_directories;
    fixture_fault_stats.handled = handled;
    fixture_fault_stats.invalid = invalid;
    fixture_fault_stats_result = result;
}

void shell_checks_host_set_image_fixture(const uint8_t* data, uint32_t size,
                                         int result, uint8_t mutate) {
    fixture_read_data = data;
    fixture_read_size = size;
    fixture_read_result = result;
    fixture_read_mutate = mutate;
}

void shell_checks_host_set_delete_fixture(uint32_t successes) {
    fixture_delete_successes = successes;
    fixture_delete_calls = 0U;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = shell_checks_host_test_contracts();
    coverage_active = 0U;
    if (result != 0) {
        printf("SHELL_CHECKS_HOST_FAIL:%d\n", result);
    }
    coverage_emit(result);
    return result == 0 ? 0 : 1;
}
