#include <stdint.h>
#include <stdio.h>

#include "apps/shell_checks.h"
#include "apps/shell_job.h"
#include "core/app_loader.h"
#include "core/errors.h"
#include "core/keyboard.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/video.h"
#include "drivers/acpi.h"
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
static uint32_t fixture_user_fault_count;
static process_user_fault_summary_t fixture_last_user_fault;
static int fixture_last_user_fault_result = OK;
static int fixture_create_user_result = OK;
static uint32_t fixture_created_user_pid;
static uint32_t fixture_process_count;
static int fixture_foreground_active;
static int fixture_run_image_result = OK;
static uint32_t fixture_run_image_pid;
static recovery_component_t fixture_recovery[RECOVERY_COMPONENT_COUNT];
static uint32_t fixture_ticks;
static int fixture_job_active;
static int fixture_job_start_result = OK;
static uint32_t fixture_job_generation = 1U;
static int fixture_cancel_foreground_result = OK;
static uint8_t fixture_cancel_requested;
static keyboard_focus_cancel_filter_t fixture_cancel_filter;

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

int app_loader_cancel_foreground(uint32_t exit_code) {
    (void)exit_code;
    return fixture_cancel_foreground_result;
}

void app_loader_set_operation_generation(uint32_t generation) {
    fixture_job_generation = generation;
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

uint32_t process_get_user_fault_count(void) {
    return fixture_user_fault_count;
}

int process_get_last_user_fault(process_user_fault_summary_t* summary) {
    if (fixture_last_user_fault_result != OK) {
        return fixture_last_user_fault_result;
    }
    if (!summary) return ERR_NULL;
    *summary = fixture_last_user_fault;
    return OK;
}

int process_create_user_test(int trigger_fault, uint32_t* pid_out) {
    (void)trigger_fault;
    if (fixture_create_user_result != OK) return fixture_create_user_result;
    if (!pid_out) return ERR_NULL;
    *pid_out = fixture_created_user_pid;
    return OK;
}

uint32_t process_get_count(void) {
    return fixture_process_count;
}

int app_loader_is_foreground_active(void) {
    return fixture_foreground_active;
}

int app_loader_run_image(const char* name, const uint8_t* image,
                         uint32_t size, const app_launch_info_t* launch,
                         uint32_t* pid_out) {
    (void)name;
    (void)image;
    (void)size;
    (void)launch;
    if (fixture_run_image_result != OK) return fixture_run_image_result;
    if (!pid_out) return ERR_NULL;
    *pid_out = fixture_run_image_pid;
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

const recovery_component_t* recovery_get(recovery_component_id_t component) {
    if (component >= RECOVERY_COMPONENT_COUNT) return NULL;
    return &fixture_recovery[component];
}

uint32_t recovery_get_count(void) {
    return RECOVERY_COMPONENT_COUNT;
}

int recovery_is_available(recovery_component_id_t component) {
    const recovery_component_t* entry = recovery_get(component);

    return entry && entry->state == RECOVERY_STATE_READY;
}

int recovery_is_enabled(recovery_component_id_t component) {
    const recovery_component_t* entry = recovery_get(component);

    return entry && entry->state != RECOVERY_STATE_DISABLED;
}

uint32_t timer_get_ticks(void) {
    return fixture_ticks;
}

void shell_job_set_phase(shell_job_context_t* context, const char* phase) {
    uint32_t length;

    if (!context || !phase) return;
    length = kstrlen(phase);
    if (length >= SHELL_JOB_PHASE_SIZE) length = SHELL_JOB_PHASE_SIZE - 1U;
    kmemcpy(context->phase, phase, length);
    context->phase[length] = '\0';
}

void shell_job_set_progress(shell_job_context_t* context, uint32_t progress,
                            uint32_t total) {
    if (!context) return;
    context->progress = progress;
    context->total = total;
}

void shell_job_set_next_wake(shell_job_context_t* context,
                             uint32_t next_wake_tick) {
    if (!context) return;
    context->next_wake_tick = next_wake_tick;
    context->next_wake_active = 1U;
}

int shell_job_is_active(void) {
    return fixture_job_active;
}

int shell_job_start(const shell_job_definition_t* definition,
                    const char* arguments) {
    (void)definition;
    (void)arguments;
    if (fixture_job_start_result != OK) return fixture_job_start_result;
    fixture_job_active = 1;
    return OK;
}

uint32_t shell_job_get_generation(void) {
    return fixture_job_generation;
}

void shell_job_request_cancel(void) {
    fixture_cancel_requested = 1U;
}

void keyboard_set_focus_cancel_filter(keyboard_focus_cancel_filter_t filter) {
    fixture_cancel_filter = filter;
}

void shell_runtime_reset_input(void) {
}

void shell_runtime_finish_command(void) {
}

void shell_print_prompt(void) {
}

int process_cancel_user_test(uint32_t pid, uint32_t exit_code) {
    (void)pid;
    (void)exit_code;
    return fixture_cancel_foreground_result;
}

void shell_checks_host_set_recovery_state(recovery_component_id_t component,
                                          recovery_state_t state) {
    if (component < RECOVERY_COMPONENT_COUNT) {
        fixture_recovery[component].state = state;
    }
}

void shell_checks_host_set_ticks(uint32_t ticks) {
    fixture_ticks = ticks;
}

void shell_checks_host_set_job_fixture(int active, int start_result,
                                       uint32_t generation) {
    fixture_job_active = active;
    fixture_job_start_result = start_result;
    fixture_job_generation = generation;
}

void shell_checks_host_set_cancel_fixture(int result, uint8_t requested) {
    fixture_cancel_foreground_result = result;
    fixture_cancel_requested = requested;
}

int shell_checks_host_job_filter_installed(void) {
    return fixture_cancel_filter != NULL;
}

int shell_checks_host_job_active(void) {
    return fixture_job_active;
}

int shell_checks_host_cancel_requested(void) {
    return fixture_cancel_requested != 0U;
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

void shell_checks_host_set_fault_fixture(uint32_t count, uint32_t pid,
                                         uint32_t vector, uint32_t error,
                                         int result) {
    fixture_user_fault_count = count;
    fixture_last_user_fault.pid = pid;
    fixture_last_user_fault.vector = vector;
    fixture_last_user_fault.error = error;
    fixture_last_user_fault_result = result;
}

void shell_checks_host_set_user_create_fixture(int result, uint32_t pid) {
    fixture_create_user_result = result;
    fixture_created_user_pid = pid;
}

void shell_checks_host_set_process_count(uint32_t count) {
    fixture_process_count = count;
}

void shell_checks_host_set_foreground_fixture(int active) {
    fixture_foreground_active = active;
}

void shell_checks_host_set_run_image_fixture(int result, uint32_t pid) {
    fixture_run_image_result = result;
    fixture_run_image_pid = pid;
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
