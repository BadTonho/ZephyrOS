#include <stdint.h>
#include <stdio.h>

#include "apps/shell_checks.h"
#include "apps/shell_command_utils.h"
#include "apps/shell_job.h"
#include "core/app_loader.h"
#include "core/app_builtin.h"
#include "core/app_api.h"
#include "core/app_catalog.h"
#include "core/app_package.h"
#include "core/crypto.h"
#include "core/clock.h"
#include "core/device_manager.h"
#include "core/input.h"
#include "core/irq_deferred.h"
#include "core/net_socket.h"
#include "core/power.h"
#include "core/power_notifier.h"
#include "core/syscall.h"
#include "core/tls.h"
#include "core/wait.h"
#include "core/wifi_manager.h"
#include "core/workqueue.h"
#include "core/errors.h"
#include "core/keyboard.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/video.h"
#include "drivers/acpi.h"
#include "drivers/ata.h"
#include "drivers/ehci.h"
#include "drivers/idt.h"
#include "drivers/pci.h"
#include "drivers/rtc.h"
#include "core/usb_manager.h"
#include "drivers/usb_hid.h"
#include "drivers/usb_msc.h"
#include "fs/block.h"
#include "fs/block_cache.h"
#include "fs/file_index.h"
#include "fs/fs.h"
#include "fs/storage.h"
#include "fs/vfs.h"
#include "apps/shell_runtime.h"
#include "process/process.h"
#include "process/signal.h"
#include "process/thread.h"
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
static block_device_t fixture_block_device;
static block_cache_stats_t fixture_cache_stats;
static storage_volume_t fixture_volume;

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

int app_builtin_run_uptime(uint32_t* pid_out) {
    if (!pid_out) return ERR_NULL;
    *pid_out = 31U;
    return OK;
}

int app_builtin_run_mem(uint32_t* pid_out) {
    if (!pid_out) return ERR_NULL;
    *pid_out = 32U;
    return OK;
}

uint32_t process_get_current_pid(void) {
    return fixture_current_process.pid;
}

int video_terminal_is_active(void) { return 0; }

int process_take_user_test_result(uint32_t* pid_out, uint32_t* faulted_out) {
    (void)pid_out;
    (void)faulted_out;
    return ERR_NOT_FOUND;
}

int syscall_invoke_kernel(uint32_t number, uint32_t arg1, uint32_t arg2,
                          uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;
    if (number == APP_SYSCALL_INVALID) return ERR_INVALID;
    return ERR_UNAVAILABLE;
}

int fs_get_file_count(void) { return 0; }

int fs_get_file_info(int index, char* name, uint32_t* size,
                     uint8_t* attributes) {
    (void)index;
    (void)name;
    (void)size;
    (void)attributes;
    return ERR_NOT_FOUND;
}

int fs_write_file_at(const char* filename, const uint8_t* data,
                     uint32_t size) {
    (void)filename;
    (void)data;
    (void)size;
    return ERR_UNAVAILABLE;
}

int app_loader_build_launch_info(const char* arguments,
                                 app_launch_info_t* out_info) {
    (void)arguments;
    if (!out_info) return ERR_NULL;
    kmemset(out_info, 0, sizeof(*out_info));
    return ERR_UNAVAILABLE;
}

int app_loader_validate_image(const uint8_t* image, uint32_t size,
                              app_image_header_t* out_header) {
    (void)image;
    (void)size;
    if (out_header) kmemset(out_header, 0, sizeof(*out_header));
    return ERR_UNAVAILABLE;
}

int app_loader_run_file_with_launch(const char* filename,
                                    const app_launch_info_t* launch,
                                    uint32_t* pid_out) {
    (void)filename;
    (void)launch;
    if (!pid_out) return ERR_NULL;
    *pid_out = 0U;
    return ERR_UNAVAILABLE;
}

int app_loader_run_file(const char* path, uint32_t* pid_out) {
    (void)path;
    if (!pid_out) return ERR_NULL;
    *pid_out = 0U;
    return ERR_UNAVAILABLE;
}

int shell_job_generation_matches(uint32_t generation) {
    return generation == fixture_job_generation;
}

void shell_job_note_stale_event(uint32_t generation) {
    (void)generation;
}

int shell_diagnostics_run_memcheck(shell_memcheck_result_t* result_out) {
    if (!result_out) return ERR_NULL;
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->heap_integrity = OK;
    result_out->coalescence = OK;
    result_out->pmm_guards = OK;
    result_out->user_directories = OK;
    result_out->slab_integrity = OK;
    result_out->memory_metrics = OK;
    return OK;
}

int thread_run_self_test(void) {
    return OK;
}

int block_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = 1U;
    return OK;
}

int block_get_at(uint32_t index, block_device_t* out_device) {
    if (!out_device) return ERR_NULL;
    if (index != 0U) return ERR_NOT_FOUND;
    *out_device = fixture_block_device;
    return OK;
}

int block_validate_state(void) {
    return OK;
}

int block_self_test(void) {
    return OK;
}

int block_get_stats(block_queue_stats_t* out_stats) {
    if (!out_stats) return ERR_NULL;
    kmemset(out_stats, 0, sizeof(*out_stats));
    return OK;
}

int power_shutdown_prepare(void) {
    return OK;
}

log_level_t log_get_level(void) { return LOG_LEVEL_INFO; }
log_level_t log_get_buffer_level(void) { return LOG_LEVEL_INFO; }
log_level_t log_get_console_level(void) { return LOG_LEVEL_INFO; }
void log_set_level(log_level_t level) { (void)level; }
int log_set_buffer_level(log_level_t level) { (void)level; return OK; }
int log_set_console_level(log_level_t level) { (void)level; return OK; }

int app_api_is_ready(void) { return 0; }
int app_api_file_is_ready(void) { return 0; }
int app_api_ipc_is_ready(void) { return 0; }
int syscall_is_ready(void) { return 0; }
int syscall_user_mode_is_enabled(void) { return 0; }
int idt_is_user_syscall_enabled(void) { return 0; }
int paging_is_ready(void) { return 0; }
int app_package_is_ready(void) { return 0; }
int app_catalog_is_ready(void) { return 0; }
int timer_validate_state(void) { return OK; }
int idt_validate_irq_state(void) { return OK; }
int irq_deferred_validate_state(void) { return OK; }
int workqueue_validate_state(void) { return OK; }
int wait_validate_state(void) { return OK; }
int vfs_validate_state(void) { return OK; }
int process_signal_validate_state(void) { return OK; }
int rtc_validate_state(void) { return OK; }
int clock_validate_state(void) { return OK; }
int tls_validate_state(void) { return OK; }

int net_socket_get_status(net_socket_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    kmemset(out_status, 0, sizeof(*out_status));
    return OK;
}

int net_socket_validate_state(void) { return OK; }

int app_api_get_version(app_api_version_t* version) {
    if (!version) return ERR_NULL;
    kmemset(version, 0, sizeof(*version));
    version->major = APP_API_VERSION_MAJOR;
    version->minor = APP_API_VERSION_MINOR;
    return OK;
}

int app_api_get_uptime(app_uptime_info_t* info) {
    if (!info) return ERR_NULL;
    kmemset(info, 0, sizeof(*info));
    return OK;
}

int app_api_get_memory_info(app_memory_info_t* info) {
    if (!info) return ERR_NULL;
    kmemset(info, 0, sizeof(*info));
    return OK;
}

int scheduler_validate_invariants(scheduler_validation_t* validation) {
    if (!validation) return ERR_NULL;
    kmemset(validation, 0, sizeof(*validation));
    validation->current_valid = 1;
    validation->idle_valid = 1;
    validation->pid_table_valid = 1;
    validation->state_table_valid = 1;
    validation->slab_table_valid = 1;
    validation->stack_table_valid = 1;
    validation->idle_accounting_valid = 1;
    return OK;
}

int process_stack_self_test(void) { return OK; }

int pci_get_device_count(uint8_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = 0U;
    return ERR_UNAVAILABLE;
}

int device_manager_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = 0U;
    return OK;
}

int device_manager_get_info(uint32_t index, device_info_t* out_info) {
    (void)index;
    if (!out_info) return ERR_NULL;
    kmemset(out_info, 0, sizeof(*out_info));
    return ERR_UNAVAILABLE;
}

int device_manager_find(const char* id, device_info_t* out_info) {
    (void)id;
    if (!out_info) return ERR_NULL;
    kmemset(out_info, 0, sizeof(*out_info));
    return ERR_NOT_FOUND;
}

int device_manager_format_text(const device_info_t* info,
                               device_text_t* out_text) {
    (void)info;
    if (!out_text) return ERR_NULL;
    kmemset(out_text, 0, sizeof(*out_text));
    return ERR_UNAVAILABLE;
}

ata_device_t* ata_get_device(void) { return NULL; }
int ata_get_device_count(uint8_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = 0U;
    return OK;
}

int usb_manager_get_status(usb_manager_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    kmemset(out_status, 0, sizeof(*out_status));
    return ERR_UNAVAILABLE;
}

int usb_manager_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = 0U;
    return OK;
}

int usb_manager_get_info(uint32_t index, usb_controller_info_t* out_info) {
    (void)index;
    if (!out_info) return ERR_NULL;
    kmemset(out_info, 0, sizeof(*out_info));
    return ERR_UNAVAILABLE;
}

int usb_manager_find(const char* id, usb_controller_info_t* out_info) {
    (void)id;
    if (!out_info) return ERR_NULL;
    kmemset(out_info, 0, sizeof(*out_info));
    return ERR_NOT_FOUND;
}

int usb_manager_format_text(const usb_controller_info_t* info,
                            usb_controller_text_t* out_text) {
    (void)info;
    if (!out_text) return ERR_NULL;
    kmemset(out_text, 0, sizeof(*out_text));
    return ERR_UNAVAILABLE;
}

int usb_msc_validate_state(void) { return OK; }
int usb_hid_validate_state(void) { return OK; }
int input_validate_state(void) { return OK; }
int usb_manager_validate_state(void) { return OK; }
int usb_manager_get_uhci_status(uint32_t index, usb_uhci_status_t* out_status) {
    (void)index;
    if (!out_status) return ERR_NULL;
    kmemset(out_status, 0, sizeof(*out_status));
    return ERR_UNAVAILABLE;
}

int ehci_get_status(uint8_t bus, uint8_t device, uint8_t function,
                    usb_ehci_status_t* out_status) {
    (void)bus;
    (void)device;
    (void)function;
    if (!out_status) return ERR_NULL;
    kmemset(out_status, 0, sizeof(*out_status));
    return ERR_UNAVAILABLE;
}

int acpi_get_status(acpi_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    kmemset(out_status, 0, sizeof(*out_status));
    return ERR_UNAVAILABLE;
}

int acpi_get_power_info(acpi_power_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    kmemset(out_info, 0, sizeof(*out_info));
    return ERR_UNAVAILABLE;
}

int acpi_get_table_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = 0U;
    return OK;
}

int acpi_get_table_at(uint32_t index, acpi_table_info_t* out_table) {
    (void)index;
    if (!out_table) return ERR_NULL;
    kmemset(out_table, 0, sizeof(*out_table));
    return ERR_NOT_FOUND;
}

int acpi_get_madt_info(acpi_madt_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    kmemset(out_info, 0, sizeof(*out_info));
    return ERR_UNAVAILABLE;
}

int acpi_get_madt_entry_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = 0U;
    return OK;
}

int acpi_get_madt_entry_at(uint32_t index, acpi_madt_entry_t* out_entry) {
    (void)index;
    if (!out_entry) return ERR_NULL;
    kmemset(out_entry, 0, sizeof(*out_entry));
    return ERR_NOT_FOUND;
}

int power_get_status(power_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    kmemset(out_status, 0, sizeof(*out_status));
    return ERR_UNAVAILABLE;
}

uint8_t keyboard_controller_reset_available(void) { return 0U; }
int power_notifier_validate_state(void) { return OK; }

int shell_diagnostics_run_device_scan(shell_device_scan_result_t* scan) {
    if (!scan) return ERR_NULL;
    kmemset(scan, 0, sizeof(*scan));
    scan->network_result = ERR_UNAVAILABLE;
    scan->usb_result = ERR_UNAVAILABLE;
    scan->wifi_result = ERR_UNAVAILABLE;
    return ERR_UNAVAILABLE;
}

int wifi_manager_validate_state(void) { return OK; }
int shell_network_validate_for_checks(void) { return OK; }
int file_index_validate_state(void) { return OK; }
int file_index_self_test(void) { return OK; }

int app_package_run_diagnostics(app_package_diagnostic_t* diagnostic_out) {
    if (!diagnostic_out) return ERR_NULL;
    kmemset(diagnostic_out, 0, sizeof(*diagnostic_out));
    diagnostic_out->invalid_package = 1U;
    diagnostic_out->missing_dependency = 1U;
    diagnostic_out->insufficient_space = 1U;
    diagnostic_out->mutation_serialization = 1U;
    return OK;
}

int block_cache_get_stats(block_cache_stats_t* out_stats) {
    if (!out_stats) return ERR_NULL;
    *out_stats = fixture_cache_stats;
    return OK;
}

int block_cache_validate_state(void) {
    return OK;
}

int storage_find_volume(const char* id, storage_volume_t* out_volume) {
    if (!id || !out_volume) return ERR_NULL;
    *out_volume = fixture_volume;
    if (kstrcmp(id, fixture_volume.id) != 0) return ERR_NOT_FOUND;
    return OK;
}

int storage_mount(const char* id) {
    (void)id;
    return OK;
}

int storage_list_dir(const char* id, const char* path,
                     storage_dir_entry_t* entries, uint32_t capacity,
                     uint32_t* out_count) {
    (void)id;
    (void)path;
    (void)entries;
    (void)capacity;
    if (!out_count) return ERR_NULL;
    *out_count = 0U;
    return OK;
}

int storage_list_dir_long(const char* id, const char* path,
                          storage_long_dir_entry_t* entries,
                          uint32_t capacity, uint32_t* out_count) {
    (void)id;
    (void)path;
    (void)entries;
    (void)capacity;
    if (!out_count) return ERR_NULL;
    *out_count = 0U;
    return OK;
}

int storage_get_file_info(const char* id, const char* path,
                          uint32_t* out_size, uint8_t* out_attributes) {
    (void)id;
    (void)path;
    (void)out_size;
    (void)out_attributes;
    return ERR_NOT_FOUND;
}

int storage_read_file_range(const char* id, const char* path,
                            uint32_t offset, uint8_t* buffer,
                            uint32_t max_size, uint32_t* out_read) {
    (void)id;
    (void)path;
    (void)offset;
    (void)buffer;
    (void)max_size;
    (void)out_read;
    return ERR_NOT_FOUND;
}

int storage_write_file(const char* id, const char* path, const uint8_t* data,
                       uint32_t size, uint8_t attributes) {
    (void)id;
    (void)path;
    (void)data;
    (void)size;
    (void)attributes;
    return ERR_UNAVAILABLE;
}

int storage_delete_file(const char* id, const char* path) {
    (void)id;
    (void)path;
    return OK;
}

int storage_sync_volume(const char* id) {
    (void)id;
    return OK;
}

int storage_check(const char* id) {
    (void)id;
    return OK;
}

int crypto_sha256(const uint8_t* data, uint32_t size,
                  uint8_t hash[CRYPTO_SHA256_SIZE]) {
    (void)data;
    (void)size;
    if (!hash) return ERR_NULL;
    kmemset(hash, 0, CRYPTO_SHA256_SIZE);
    return OK;
}

int crypto_equal(const uint8_t* first, const uint8_t* second, uint32_t size) {
    if (!first || !second) return 0;
    for (uint32_t index = 0U; index < size; index++) {
        if (first[index] != second[index]) return 0;
    }
    return 1;
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

int process_reap_finished_user(void) {
    return OK;
}

int shell_command_args_equal(const char* args, const char* expected) {
    if (!args || !expected) return args == expected;
    return kstrcmp(args, expected) == 0;
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
