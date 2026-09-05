#include <stdint.h>
#include <stdio.h>

#include "apps/shell_command_utils.h"
#include "apps/shell_introspection.h"
#include "core/errors.h"
#include "core/app_api.h"
#include "core/app_catalog.h"
#include "core/app_loader.h"
#include "core/app_package.h"
#include "core/log.h"
#include "core/string.h"
#include "core/net_buffer.h"
#include "core/net_socket.h"
#include "core/sk_buff.h"
#include "core/socket.h"
#include "core/syscall.h"
#include "core/timer.h"
#include "core/clock.h"
#include "core/keyboard.h"
#include "core/irq_deferred.h"
#include "core/input.h"
#include "core/recovery.h"
#include "core/device_manager.h"
#include "core/network_manager.h"
#include "core/power.h"
#include "core/usb_manager.h"
#include "core/wifi_manager.h"
#include "core/tls.h"
#include "core/video.h"
#include "core/wait.h"
#include "core/workqueue.h"
#include "core/memory.h"
#include "core/update.h"
#include "core/update_remote.h"
#include "apps/shell_runtime.h"
#include "drivers/idt.h"
#include "drivers/rtc.h"
#include "drivers/mouse.h"
#include "drivers/vesa.h"
#include "fs/vfs.h"
#include "fs/block_cache.h"
#include "fs/fs.h"
#include "fs/storage.h"
#include "fs/devfs.h"
#include "fs/procfs.h"
#include "memory/paging.h"
#include "memory/slab.h"
#include "process/process.h"
#include "process/signal.h"
#include "drivers/usb_hid.h"
#include "drivers/usb_msc.h"
#include "drivers/acpi.h"
#include "drivers/pci.h"
#include "fs/file_index.h"

void shell_dispatch_cmd_pwd(const char* arguments);
void shell_dispatch_cmd_cd(const char* arguments);
void shell_dispatch_cmd_mouse(const char* arguments);
void shell_dispatch_cmd_log(const char* arguments);
void shell_dispatch_cmd_timer(const char* arguments);
void shell_dispatch_cmd_clock(const char* arguments);
void shell_dispatch_cmd_irqstat(const char* arguments);
void shell_dispatch_cmd_wait(const char* arguments);
void shell_dispatch_cmd_wqinfo(const char* arguments);
void shell_dispatch_cmd_workq(const char* arguments);
void shell_dispatch_cmd_tls(const char* arguments);
void shell_dispatch_cmd_vfs(const char* arguments);
void shell_dispatch_cmd_mount(const char* arguments);
void shell_dispatch_cmd_devcheck(const char* arguments);
void shell_dispatch_cmd_devices(const char* arguments);
void shell_dispatch_cmd_device_info(const char* arguments);
void shell_dispatch_cmd_usb(const char* arguments);
void shell_dispatch_cmd_slabinfo(const char* arguments);
void shell_dispatch_cmd_slabtest(const char* arguments);
void shell_dispatch_cmd_cpu_usage(const char* arguments);
void shell_dispatch_cmd_pagefault(const char* arguments);
void shell_dispatch_cmd_vmamap(const char* arguments);
void shell_dispatch_cmd_schedcheck(const char* arguments);
void shell_dispatch_cmd_memcheck(const char* arguments);
void shell_dispatch_cmd_kmetrics(const char* arguments);
void shell_dispatch_cmd_device_scan(const char* arguments);
void shell_dispatch_cmd_power(const char* arguments);
void shell_dispatch_cmd_acpi(const char* arguments);
void shell_dispatch_cmd_kill(const char* arguments);
void shell_dispatch_cmd_sigtest(const char* arguments);
void shell_dispatch_cmd_proccheck(const char* arguments);
void shell_dispatch_cmd_health(const char* arguments);
void shell_diagnostics_reset(void);

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_OUTPUT_CAPACITY 4096U
#define HOST_PATH_CAPACITY 256U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static char video_output[HOST_OUTPUT_CAPACITY];
static uint32_t video_output_length;
static char fixture_cwd[HOST_PATH_CAPACITY];
static char fixture_last_path[HOST_PATH_CAPACITY];
static mouse_status_t fixture_mouse_status;
static int fixture_getcwd_result;
static int fixture_chdir_result;
static int fixture_mouse_status_result;
static int fixture_mouse_speed_result;
static int fixture_mouse_primary_result;
static int fixture_mouse_acceleration_result;
static uint32_t fixture_chdir_calls;
static uint32_t fixture_speed_calls;
static uint32_t fixture_primary_calls;
static uint32_t fixture_acceleration_calls;
static log_stats_t fixture_log_stats;
static log_record_t fixture_log_records[2];
static log_self_test_result_t fixture_log_test;
static int fixture_log_stats_result;
static int fixture_log_copy_result;
static int fixture_log_set_console_result;
static int fixture_log_set_buffer_result;
static int fixture_log_test_result;
static uint32_t fixture_log_copy_count;
static uint32_t fixture_log_clear_calls;
static uint32_t fixture_log_console_calls;
static uint32_t fixture_log_buffer_calls;
static log_level_t fixture_log_console_level;
static log_level_t fixture_log_buffer_level;
static timer_stats_t fixture_timer_stats;
static timer_info_t fixture_timer_records[2];
static timer_self_test_result_t fixture_timer_test;
static int fixture_timer_stats_result;
static int fixture_timer_copy_result;
static int fixture_timer_test_result;
static uint32_t fixture_timer_copy_count;
static uint32_t fixture_ticks;
static keyboard_metrics_t fixture_keyboard_metrics;
static ipc_stats_t fixture_ipc_stats;
static uint32_t fixture_ipc_pending_count;
static vesa_metrics_t fixture_vesa_metrics;
static vesa_mode_t fixture_vesa_mode;
static int fixture_vesa_backbuffer;
static paging_boot_stats_t fixture_paging_boot_stats;
static int fixture_paging_boot_result;
static clock_status_t fixture_clock_status;
static clock_self_test_result_t fixture_clock_test;
static int fixture_clock_status_result;
static int fixture_clock_ticks_result;
static int fixture_clock_utc_result;
static int fixture_clock_test_result;
static uint64_t fixture_clock_ticks;
static uint64_t fixture_clock_utc;
static rtc_status_t fixture_rtc_status;
static rtc_self_test_result_t fixture_rtc_test;
static int fixture_rtc_status_result;
static int fixture_rtc_test_result;
static irq_deferred_status_t fixture_irq_status;
static irq_deferred_irq_status_t fixture_irq_lines[IRQ_DEFERRED_IRQ_COUNT];
static idt_irq_status_t fixture_idt_lines[IDT_IRQ_LINE_COUNT];
static irq_deferred_self_test_result_t fixture_irq_test;
static int fixture_irq_status_result;
static int fixture_irq_line_result;
static int fixture_irq_test_result;
static int fixture_irq_validate_result;
static int fixture_idt_validate_result;
static wait_stats_t fixture_wait_stats;
static wait_queue_info_t fixture_wait_queues[2];
static wait_info_t fixture_waiters[2];
static wait_self_test_result_t fixture_wait_test;
static int fixture_wait_stats_result;
static int fixture_wait_info_result;
static int fixture_waiters_result;
static int fixture_wait_test_result;
static workqueue_stats_t fixture_workq_stats;
static work_info_t fixture_work_records[2];
static workqueue_self_test_result_t fixture_workq_test;
static int fixture_workq_stats_result;
static int fixture_workq_info_result;
static int fixture_workq_test_result;
static int fixture_workq_validate_result;
static int fixture_workq_probe_result;
static uint32_t fixture_wait_queue_count;
static uint32_t fixture_waiter_count;
static uint32_t fixture_work_info_count;
static tls_policy_t fixture_tls_policy;
static tls_status_t fixture_tls_status;
static tls_self_test_result_t fixture_tls_test;
static int fixture_tls_policy_result;
static int fixture_tls_status_result;
static int fixture_tls_capability_result;
static int fixture_tls_test_result;
static vfs_status_t fixture_vfs_status;
static vfs_descriptor_info_t fixture_vfs_descriptors[2];
static vfs_mount_info_t fixture_vfs_mounts[2];
static vfs_test_result_t fixture_vfs_test;
static int fixture_vfs_status_result;
static int fixture_vfs_descriptor_result;
static int fixture_vfs_mount_result;
static int fixture_vfs_test_result;
static uint32_t fixture_vfs_descriptor_count;
static uint32_t fixture_vfs_mount_count;
static devfs_test_result_t fixture_devfs_test;
static int fixture_devfs_test_result;
static device_info_t fixture_devices[2];
static uint32_t fixture_device_count;
static int fixture_device_count_result;
static int fixture_device_info_result;
static int fixture_device_find_result;
static int fixture_device_format_result;
static usb_manager_status_t fixture_usb_status;
static usb_controller_info_t fixture_usb_controllers[2];
static usb_port_info_t fixture_usb_ports[2];
static usb_device_info_t fixture_usb_devices[2];
static uint32_t fixture_usb_controller_count;
static uint32_t fixture_usb_port_count;
static uint32_t fixture_usb_device_count;
static int fixture_usb_status_result;
static int fixture_usb_count_result;
static int fixture_usb_info_result;
static int fixture_usb_port_count_result;
static int fixture_usb_port_result;
static int fixture_usb_device_count_result;
static int fixture_usb_device_result;
static int fixture_usb_find_result;
static int fixture_usb_format_result;
static usb_msc_info_t fixture_usb_msc[2];
static uint32_t fixture_usb_msc_count;
static int fixture_usb_msc_count_result;
static int fixture_usb_msc_info_result;
static usb_hid_info_t fixture_usb_hid[2];
static uint32_t fixture_usb_hid_count;
static int fixture_usb_hid_count_result;
static int fixture_usb_hid_info_result;
static int fixture_usb_hid_validate_result;
static int fixture_input_validate_result;
static int fixture_recovery_state_result;
static recovery_component_t fixture_recovery_usb;
static int fixture_recovery_mark_result;
static recovery_component_id_t fixture_recovery_last_component;
static recovery_state_t fixture_recovery_last_state;
static uint32_t fixture_recovery_mark_calls;
static int fixture_scan_pci_result;
static int fixture_scan_usb_refresh_result;
static int fixture_scan_usb_init_result;
static int fixture_scan_storage_result;
static int fixture_scan_mount_result;
static int fixture_scan_index_result;
static int fixture_scan_devices_result;
static int fixture_scan_network_result;
static int fixture_scan_wifi_refresh_result;
static int fixture_scan_wifi_init_result;
static uint32_t fixture_yield_calls;
static power_status_t fixture_power_status;
static int fixture_power_result;
static acpi_status_t fixture_acpi_status;
static acpi_power_info_t fixture_acpi_power;
static acpi_table_info_t fixture_acpi_tables[2];
static acpi_madt_info_t fixture_acpi_madt;
static uint32_t fixture_acpi_table_count;
static int fixture_acpi_status_result;
static int fixture_acpi_power_result;
static int fixture_acpi_table_count_result;
static int fixture_acpi_table_result;
static int fixture_acpi_madt_result;
static kmem_cache_info_t fixture_slab_info[2];
static uint32_t fixture_slab_count;
static int fixture_slab_info_result;
static int fixture_slab_self_test_result;
static scheduler_stats_t fixture_scheduler_stats;
static scheduler_validation_t fixture_scheduler_validation;
static int fixture_scheduler_stats_result;
static int fixture_scheduler_validation_result;
static page_fault_stats_t fixture_page_fault_stats;
static int fixture_page_fault_stats_result;
static process_t fixture_user_process;
static vm_area_info_t fixture_user_areas[2];
static uint32_t fixture_user_area_count;
static int fixture_process_lookup_result;
static int fixture_process_is_user_result;
static int fixture_process_vma_copy_result;
static memory_heap_stats_t fixture_memcheck_heap_stats;
static memory_pmm_stats_t fixture_memcheck_pmm_stats;
static memory_detailed_stats_t fixture_memcheck_detailed_stats;
static paging_user_stats_t fixture_memcheck_paging_stats;
static int fixture_memcheck_detailed_result;
static int fixture_memcheck_slab_validate_result;
static uint8_t fixture_memcheck_blocks[3][224];
static uint32_t fixture_memcheck_allocations;
static uint32_t fixture_memcheck_frees;
static uint32_t fixture_process_user_count;
static uint32_t fixture_process_zombie_count;
static int fixture_app_foreground_active;
static process_signal_self_test_t fixture_signal_test;
static int fixture_signal_test_result;
static int fixture_signal_send_result;
static uint32_t fixture_signal_send_pid;
static uint32_t fixture_signal_send_number;
static recovery_component_t fixture_recovery_components[RECOVERY_COMPONENT_COUNT];
static app_api_version_t fixture_app_api_version;
static update_capabilities_t fixture_update_capabilities;
static update_status_t fixture_update_status;
static update_remote_status_t fixture_update_remote_status;
static app_catalog_status_t fixture_app_catalog_status;
static net_buffer_stats_t fixture_net_buffer_stats;
static sk_buff_stats_t fixture_skb_stats;
static net_socket_status_t fixture_net_socket_status;
static socket_status_t fixture_socket_status;
static socket_self_test_result_t fixture_socket_test;
static block_cache_stats_t fixture_block_cache_stats;
static block_durability_status_t fixture_block_durability;
static process_signal_stats_t fixture_signal_stats;
static input_metrics_t fixture_input_metrics;
static process_user_fault_summary_t fixture_user_fault;
process_t* processes[MAX_PROCESSES];

static const char* fixture_recovery_names[RECOVERY_COMPONENT_COUNT] = {
    "VESA", "BACKBUFFER", "ATA", "FILESYSTEM", "AC97",
    "SYSTEM_PROCESS", "SHELL", "DESKTOP", "TASKBAR", "WM",
    "TASKMANAGER", "FILEMANAGER", "SETTINGS", "MEDIAPLAYER", "EDITOR",
    "GUITEST", "APP_LOADER", "ACPI", "DEVICES", "NETWORK", "POWER",
    "UPDATE", "SYSTEM_UPDATER", "APP_STORE", "STORAGE", "USB"
};

#define HOST_VFS_HANDLE_CAPACITY 4U
#define HOST_VFS_CONTENT_CAPACITY 64U

typedef struct {
    const char* path;
    const char* content;
    uint32_t size;
    uint8_t writable;
} fixture_vfs_file_t;

typedef struct {
    fixture_vfs_file_t file;
    uint32_t offset;
    uint32_t mode;
    uint8_t used;
} fixture_vfs_handle_t;

static fixture_vfs_handle_t fixture_vfs_handles[HOST_VFS_HANDLE_CAPACITY];
static char fixture_vfs_console_level[HOST_VFS_CONTENT_CAPACITY];
static char fixture_vfs_buffer_level[HOST_VFS_CONTENT_CAPACITY];
static uint32_t fixture_vfs_open_calls;
static uint32_t fixture_vfs_close_calls;
static uint32_t fixture_procfs_reset_calls;
static uint8_t fixture_vfs_sysfs_enabled;

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
    printf("ZCOV_BEGIN|case=host:shell:diagnostics|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:diagnostics|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:diagnostics|value=0x%08X\n",
           (uint32_t)result);
}

static void copy_text(char* output, uint32_t capacity, const char* input) {
    uint32_t index = 0U;

    if (!output || !capacity) return;
    if (!input) {
        output[0] = '\0';
        return;
    }
    while (input[index] && index + 1U < capacity) {
        output[index] = input[index];
        index++;
    }
    output[index] = '\0';
}

static void output_reset(void) {
    video_output_length = 0U;
    video_output[0] = '\0';
}

static void output_append(const char* text) {
    if (!text) return;
    while (*text && video_output_length + 1U < HOST_OUTPUT_CAPACITY) {
        video_output[video_output_length++] = *text++;
    }
    video_output[video_output_length] = '\0';
}

static int fixture_vfs_add_entry(vfs_dir_entry_t* entries, uint32_t capacity,
                                 uint32_t* count, const char* name,
                                 vfs_node_type_t type) {
    if (!entries || !count || !name) return ERR_NULL;
    if (*count >= capacity) return ERR_OVERFLOW;
    kmemset(&entries[*count], 0, sizeof(entries[*count]));
    copy_text(entries[*count].name, sizeof(entries[*count].name), name);
    entries[*count].type = type;
    (*count)++;
    return OK;
}

static int fixture_vfs_list(const char* path, vfs_dir_entry_t* entries,
                            uint32_t capacity, uint32_t* out_count) {
    uint32_t count = 0U;
    int result = OK;

    if (!path || !entries || !out_count) return ERR_NULL;
    if (!fixture_vfs_sysfs_enabled && path[0] == '/' && path[1] == 's' &&
        path[2] == 'y' && path[3] == 's' && path[4] == '/') {
        *out_count = 0U;
        return ERR_NOT_FOUND;
    }
    if (kstrcmp(path, "/proc") == 0) {
        result = fixture_vfs_add_entry(entries, capacity, &count, "1",
                                       VFS_NODE_DIRECTORY);
        if (result == OK) result = fixture_vfs_add_entry(
            entries, capacity, &count, "uptime", VFS_NODE_REGULAR);
        if (result == OK) result = fixture_vfs_add_entry(
            entries, capacity, &count, "meminfo", VFS_NODE_REGULAR);
        if (result == OK) result = fixture_vfs_add_entry(
            entries, capacity, &count, "cpuinfo", VFS_NODE_REGULAR);
        if (result == OK) result = fixture_vfs_add_entry(
            entries, capacity, &count, "version", VFS_NODE_REGULAR);
        if (result == OK) result = fixture_vfs_add_entry(
            entries, capacity, &count, "cmdline", VFS_NODE_REGULAR);
        if (result == OK) result = fixture_vfs_add_entry(
            entries, capacity, &count, "sys", VFS_NODE_DIRECTORY);
    } else if (kstrcmp(path, "/proc/sys") == 0) {
        result = fixture_vfs_add_entry(entries, capacity, &count, "kernel",
                                       VFS_NODE_DIRECTORY);
    } else if (kstrcmp(path, "/proc/sys/kernel") == 0) {
        result = fixture_vfs_add_entry(entries, capacity, &count,
                                       "console_log_level", VFS_NODE_REGULAR);
        if (result == OK) result = fixture_vfs_add_entry(
            entries, capacity, &count, "buffer_log_level", VFS_NODE_REGULAR);
    } else if (kstrcmp(path, "/sys") == 0) {
        result = fixture_vfs_add_entry(entries, capacity, &count, "bus",
                                       VFS_NODE_DIRECTORY);
        if (result == OK) result = fixture_vfs_add_entry(
            entries, capacity, &count, "class", VFS_NODE_DIRECTORY);
        if (result == OK) result = fixture_vfs_add_entry(
            entries, capacity, &count, "power", VFS_NODE_DIRECTORY);
    } else if (kstrcmp(path, "/sys/bus") == 0) {
        result = fixture_vfs_add_entry(entries, capacity, &count, "pci",
                                       VFS_NODE_DIRECTORY);
    } else if (kstrcmp(path, "/sys/bus/pci") == 0) {
        result = fixture_vfs_add_entry(entries, capacity, &count, "devices",
                                       VFS_NODE_DIRECTORY);
    } else if (kstrcmp(path, "/sys/bus/pci/devices") == 0) {
        result = fixture_vfs_add_entry(entries, capacity, &count, "00:03.0",
                                       VFS_NODE_DIRECTORY);
    } else if (kstrcmp(path, "/sys/bus/pci/devices/00:03.0") == 0) {
        result = fixture_vfs_add_entry(entries, capacity, &count, "vendor",
                                       VFS_NODE_REGULAR);
        if (result == OK) result = fixture_vfs_add_entry(
            entries, capacity, &count, "device", VFS_NODE_REGULAR);
        if (result == OK) result = fixture_vfs_add_entry(
            entries, capacity, &count, "class", VFS_NODE_REGULAR);
    } else if (kstrcmp(path, "/sys/class") == 0) {
        result = fixture_vfs_add_entry(entries, capacity, &count, "net",
                                       VFS_NODE_DIRECTORY);
        if (result == OK) result = fixture_vfs_add_entry(
            entries, capacity, &count, "block", VFS_NODE_DIRECTORY);
    } else if (kstrcmp(path, "/sys/class/net") == 0) {
        result = fixture_vfs_add_entry(entries, capacity, &count, "eth0",
                                       VFS_NODE_DIRECTORY);
    } else if (kstrcmp(path, "/sys/class/net/eth0") == 0) {
        result = fixture_vfs_add_entry(entries, capacity, &count, "address",
                                       VFS_NODE_REGULAR);
        if (result == OK) result = fixture_vfs_add_entry(
            entries, capacity, &count, "state", VFS_NODE_REGULAR);
    } else if (kstrcmp(path, "/sys/class/block") == 0) {
        result = fixture_vfs_add_entry(entries, capacity, &count, "hda",
                                       VFS_NODE_DIRECTORY);
    } else if (kstrcmp(path, "/sys/class/block/hda") == 0) {
        result = fixture_vfs_add_entry(entries, capacity, &count, "size",
                                       VFS_NODE_REGULAR);
        if (result == OK) result = fixture_vfs_add_entry(
            entries, capacity, &count, "readonly", VFS_NODE_REGULAR);
    } else if (kstrcmp(path, "/sys/power") == 0) {
        result = fixture_vfs_add_entry(entries, capacity, &count, "state",
                                       VFS_NODE_REGULAR);
    } else if (kstrcmp(path, "/proc/uptime") == 0 ||
               kstrcmp(path, "/proc/__missing__") == 0) {
        result = ERR_INVALID;
    } else {
        result = ERR_NOT_FOUND;
    }
    *out_count = result == OK ? count : 0U;
    return result;
}

static int fixture_vfs_get_file(const char* path, fixture_vfs_file_t* file) {
    if (!path || !file) return ERR_NULL;
    if (!fixture_vfs_sysfs_enabled && path[0] == '/' && path[1] == 's' &&
        path[2] == 'y' && path[3] == 's' && path[4] == '/') {
        return ERR_NOT_FOUND;
    }
    file->path = path;
    file->writable = 0U;
    if (kstrcmp(path, "/proc/uptime") == 0) {
        file->content = "uptime_ticks 120\n";
    } else if (kstrcmp(path, "/proc/meminfo") == 0) {
        file->content = "total_bytes 1048576\n";
    } else if (kstrcmp(path, "/proc/cpuinfo") == 0) {
        file->content = "processor 0\n";
    } else if (kstrcmp(path, "/proc/version") == 0) {
        file->content = "version ZephyrOS\n";
    } else if (kstrcmp(path, "/proc/cmdline") == 0) {
        file->content = "cmdline test\n";
    } else if (kstrcmp(path, "/proc/1/status") == 0) {
        file->content = "pid 1\nname init\nstate RUNNING\n";
    } else if (kstrcmp(path, "/proc/1/cmdline") == 0) {
        file->content = "cmdline init\n";
    } else if (kstrcmp(path, "/proc/1/maps") == 0) {
        file->content = "map 0x00400000-0x00401000\n";
    } else if (kstrcmp(path, "/proc/sys/kernel/console_log_level") == 0) {
        file->content = fixture_vfs_console_level;
        file->writable = 1U;
    } else if (kstrcmp(path, "/proc/sys/kernel/buffer_log_level") == 0) {
        file->content = fixture_vfs_buffer_level;
        file->writable = 1U;
    } else if (kstrcmp(path, "/sys/power/state") == 0) {
        file->content = "state S3,S5\n";
    } else if (kstrcmp(path, "/sys/bus/pci/devices/00:03.0/vendor") == 0) {
        file->content = "vendor 0x1234\n";
    } else if (kstrcmp(path, "/sys/bus/pci/devices/00:03.0/device") == 0) {
        file->content = "device 0x5678\n";
    } else if (kstrcmp(path, "/sys/bus/pci/devices/00:03.0/class") == 0) {
        file->content = "class network\n";
    } else if (kstrcmp(path, "/sys/class/net/eth0/address") == 0) {
        file->content = "address 00:11:22:33:44:55\n";
    } else if (kstrcmp(path, "/sys/class/net/eth0/state") == 0) {
        file->content = "state offline\n";
    } else if (kstrcmp(path, "/sys/class/block/hda/size") == 0) {
        file->content = "size 2048\n";
    } else if (kstrcmp(path, "/sys/class/block/hda/readonly") == 0) {
        file->content = "readonly 1\n";
    } else {
        return ERR_NOT_FOUND;
    }
    file->size = kstrlen(file->content);
    return OK;
}

static void fixture_reset(void) {
    output_reset();
    shell_diagnostics_reset();
    kmemset(fixture_vfs_handles, 0, sizeof(fixture_vfs_handles));
    copy_text(fixture_vfs_console_level,
              sizeof(fixture_vfs_console_level),
              "console_log_level info\n");
    copy_text(fixture_vfs_buffer_level,
              sizeof(fixture_vfs_buffer_level),
              "buffer_log_level debug\n");
    fixture_vfs_open_calls = 0U;
    fixture_vfs_close_calls = 0U;
    fixture_procfs_reset_calls = 0U;
    fixture_vfs_sysfs_enabled = 0U;
    copy_text(fixture_cwd, sizeof(fixture_cwd), "/home/test");
    fixture_last_path[0] = '\0';
    fixture_getcwd_result = OK;
    fixture_chdir_result = OK;
    fixture_mouse_status_result = OK;
    fixture_mouse_speed_result = OK;
    fixture_mouse_primary_result = OK;
    fixture_mouse_acceleration_result = OK;
    fixture_chdir_calls = 0U;
    fixture_speed_calls = 0U;
    fixture_primary_calls = 0U;
    fixture_acceleration_calls = 0U;
    fixture_log_stats_result = OK;
    fixture_log_copy_result = OK;
    fixture_log_set_console_result = OK;
    fixture_log_set_buffer_result = OK;
    fixture_log_test_result = OK;
    fixture_log_copy_count = 0U;
    fixture_log_clear_calls = 0U;
    fixture_log_console_calls = 0U;
    fixture_log_buffer_calls = 0U;
    fixture_log_console_level = LOG_LEVEL_INFO;
    fixture_log_buffer_level = LOG_LEVEL_DEBUG;
    fixture_timer_stats_result = OK;
    fixture_timer_copy_result = OK;
    fixture_timer_test_result = OK;
    fixture_timer_copy_count = 1U;
    fixture_ticks = 1000U;
    fixture_ipc_pending_count = 2U;
    fixture_vesa_backbuffer = 0;
    fixture_paging_boot_result = OK;
    fixture_clock_status_result = OK;
    fixture_clock_ticks_result = OK;
    fixture_clock_utc_result = OK;
    fixture_clock_test_result = OK;
    fixture_clock_ticks = 123456789ULL;
    fixture_clock_utc = 1735689600ULL;
    fixture_rtc_status_result = OK;
    fixture_rtc_test_result = OK;
    fixture_irq_status_result = OK;
    fixture_irq_line_result = OK;
    fixture_irq_test_result = OK;
    fixture_irq_validate_result = OK;
    fixture_idt_validate_result = OK;
    fixture_wait_stats_result = OK;
    fixture_wait_info_result = OK;
    fixture_waiters_result = OK;
    fixture_wait_test_result = OK;
    fixture_workq_stats_result = OK;
    fixture_workq_info_result = OK;
    fixture_workq_test_result = OK;
    fixture_workq_validate_result = OK;
    fixture_workq_probe_result = OK;
    fixture_wait_queue_count = 1U;
    fixture_waiter_count = 1U;
    fixture_work_info_count = 1U;
    fixture_tls_policy_result = OK;
    fixture_tls_status_result = OK;
    fixture_tls_capability_result = 1;
    fixture_tls_test_result = OK;
    fixture_vfs_status_result = OK;
    fixture_vfs_descriptor_result = OK;
    fixture_vfs_mount_result = OK;
    fixture_vfs_test_result = OK;
    fixture_vfs_descriptor_count = 1U;
    fixture_vfs_mount_count = 1U;
    fixture_devfs_test_result = OK;
    fixture_device_count = 2U;
    fixture_device_count_result = OK;
    fixture_device_info_result = OK;
    fixture_device_find_result = OK;
    fixture_device_format_result = OK;
    fixture_usb_controller_count = 1U;
    fixture_usb_port_count = 1U;
    fixture_usb_device_count = 1U;
    fixture_usb_status_result = OK;
    fixture_usb_count_result = OK;
    fixture_usb_info_result = OK;
    fixture_usb_port_count_result = OK;
    fixture_usb_port_result = OK;
    fixture_usb_device_count_result = OK;
    fixture_usb_device_result = OK;
    fixture_usb_find_result = OK;
    fixture_usb_format_result = OK;
    fixture_usb_msc_count = 1U;
    fixture_usb_msc_count_result = OK;
    fixture_usb_msc_info_result = OK;
    fixture_usb_hid_count = 1U;
    fixture_usb_hid_count_result = OK;
    fixture_usb_hid_info_result = OK;
    fixture_usb_hid_validate_result = OK;
    fixture_input_validate_result = OK;
    fixture_recovery_state_result = OK;
    fixture_recovery_mark_result = OK;
    fixture_recovery_last_component = RECOVERY_COMPONENT_COUNT;
    fixture_recovery_last_state = RECOVERY_STATE_UNKNOWN;
    fixture_recovery_mark_calls = 0U;
    fixture_scan_pci_result = OK;
    fixture_scan_usb_refresh_result = OK;
    fixture_scan_usb_init_result = OK;
    fixture_scan_storage_result = OK;
    fixture_scan_mount_result = OK;
    fixture_scan_index_result = OK;
    fixture_scan_devices_result = OK;
    fixture_scan_network_result = OK;
    fixture_scan_wifi_refresh_result = OK;
    fixture_scan_wifi_init_result = OK;
    fixture_yield_calls = 0U;
    fixture_power_result = OK;
    fixture_acpi_status_result = OK;
    fixture_acpi_power_result = OK;
    fixture_acpi_table_count_result = OK;
    fixture_acpi_table_result = OK;
    fixture_acpi_madt_result = OK;
    fixture_acpi_table_count = 2U;
    kmemset(&fixture_power_status, 0, sizeof(fixture_power_status));
    fixture_power_status.acpi_available = 1U;
    fixture_power_status.acpi_power_tables_available = 1U;
    fixture_power_status.acpi_pm1_control_available = 1U;
    fixture_power_status.acpi_mode_known = 1U;
    fixture_power_status.acpi_mode_enabled = 1U;
    fixture_power_status.acpi_s5_declared = 1U;
    fixture_power_status.acpi_mode_enable_available = 1U;
    fixture_power_status.acpi_s5_transition_ready = 1U;
    fixture_power_status.cpu_idle = POWER_CAPABILITY_AVAILABLE;
    fixture_power_status.hardware_poweroff = POWER_CAPABILITY_SIMULATED;
    fixture_power_status.reboot = POWER_CAPABILITY_AVAILABLE;
    fixture_power_status.service_state = POWER_SERVICE_READY;
    fixture_power_status.transaction_phase = POWER_TRANSACTION_IDLE;
    fixture_power_status.last_error = OK;
    fixture_power_status.reboot_acpi_reset_available = 1U;
    fixture_power_status.reboot_ps2_available = 1U;
    fixture_power_status.reboot_triple_fault_available = 1U;
    fixture_power_status.transaction_target = POWER_TRANSACTION_TARGET_NONE;
    fixture_power_status.notifiers_registered = 3U;
    fixture_power_status.notifiers_completed = 3U;
    fixture_power_status.sigterm_sent = 2U;
    fixture_power_status.sigkill_sent = 0U;
    fixture_power_status.processes_reaped = 2U;
    fixture_power_status.volumes_unmounted = 1U;
    fixture_power_status.optional_failures = 0U;
    fixture_power_status.quiescence_state = POWER_QUIESCENCE_READY;
    fixture_power_status.commit_started = 0U;
    fixture_power_status.transaction_degraded = 0U;
    for (uint32_t state = 0U; state < POWER_STATE_COUNT; state++) {
        fixture_power_status.states[state] = POWER_CAPABILITY_AVAILABLE;
    }
    fixture_power_status.states[POWER_STATE_S3] = POWER_CAPABILITY_SIMULATED;
    fixture_power_status.states[POWER_STATE_S4] = POWER_CAPABILITY_UNAVAILABLE;
    fixture_power_status.states[POWER_STATE_S5] = POWER_CAPABILITY_SIMULATED;
    kmemset(&fixture_acpi_status, 0, sizeof(fixture_acpi_status));
    fixture_acpi_status.initialized = 1U;
    fixture_acpi_status.available = 1U;
    fixture_acpi_status.revision = 2U;
    fixture_acpi_status.root_kind = ACPI_ROOT_XSDT;
    copy_text(fixture_acpi_status.oem_id, sizeof(fixture_acpi_status.oem_id),
              "ZEPHYR");
    fixture_acpi_status.rsdp_address = 0x000E0000U;
    fixture_acpi_status.root_address = 0x00100000U;
    fixture_acpi_status.root_entry_count = 4U;
    fixture_acpi_status.table_count = 2U;
    fixture_acpi_status.scan_ticks = 7U;
    fixture_acpi_status.fadt_present = 1U;
    fixture_acpi_status.dsdt_present = 1U;
    fixture_acpi_status.facs_present = 1U;
    fixture_acpi_status.fadt_address = 0x00101000U;
    fixture_acpi_status.dsdt_address = 0x00102000U;
    fixture_acpi_status.facs_address = 0x00103000U;
    fixture_acpi_status.rsdp_length = 36U;
    fixture_acpi_status.rsdp_checksum_valid = 1U;
    fixture_acpi_status.madt_present = 1U;
    fixture_acpi_status.madt_address = 0x00104000U;
    kmemset(&fixture_acpi_power, 0, sizeof(fixture_acpi_power));
    fixture_acpi_power.initialized = 1U;
    fixture_acpi_power.fadt_power_fields_present = 1U;
    fixture_acpi_power.pm1a_present = 1U;
    fixture_acpi_power.pm1a_readable = 1U;
    fixture_acpi_power.pm1_control_length = 2U;
    fixture_acpi_power.pm1a_control.address_space_id =
        ACPI_ADDRESS_SPACE_SYSTEM_IO;
    fixture_acpi_power.pm1a_control.register_bit_width = 16U;
    fixture_acpi_power.pm1a_control.access_size = 2U;
    fixture_acpi_power.pm1a_control.address = 0x404U;
    fixture_acpi_power.smi_command_port = 0xB2U;
    fixture_acpi_power.acpi_enable_value = 0xA0U;
    fixture_acpi_power.acpi_disable_value = 0xA1U;
    fixture_acpi_power.mode = ACPI_MODE_ENABLED;
    fixture_acpi_power.pm1a_value = 0x2000U;
    fixture_acpi_power.s5_state = ACPI_S5_DECLARED;
    fixture_acpi_power.s5_type_a = 5U;
    fixture_acpi_power.s5_type_b = 5U;
    fixture_acpi_power.mode_enable_available = 1U;
    fixture_acpi_power.s5_transition_ready = 1U;
    fixture_acpi_power.s5_candidates = 1U;
    fixture_acpi_power.reset_register_present = 1U;
    fixture_acpi_power.reset_register_valid = 1U;
    fixture_acpi_power.reset_register.address = 0xCF9U;
    fixture_acpi_power.reset_value = 6U;
    kmemset(fixture_acpi_tables, 0, sizeof(fixture_acpi_tables));
    copy_text(fixture_acpi_tables[0].signature,
              sizeof(fixture_acpi_tables[0].signature), "FADT");
    fixture_acpi_tables[0].physical_address = 0x00101000U;
    fixture_acpi_tables[0].length = 116U;
    fixture_acpi_tables[0].revision = 6U;
    fixture_acpi_tables[0].checksum_valid = 1U;
    copy_text(fixture_acpi_tables[1].signature,
              sizeof(fixture_acpi_tables[1].signature), "MADT");
    fixture_acpi_tables[1].physical_address = 0x00104000U;
    fixture_acpi_tables[1].length = 64U;
    fixture_acpi_tables[1].revision = 5U;
    fixture_acpi_tables[1].checksum_valid = 1U;
    kmemset(&fixture_acpi_madt, 0, sizeof(fixture_acpi_madt));
    fixture_acpi_madt.initialized = 1U;
    fixture_acpi_madt.present = 1U;
    fixture_acpi_madt.revision = 5U;
    fixture_acpi_madt.physical_address = 0x00104000U;
    fixture_acpi_madt.length = 64U;
    fixture_acpi_madt.entry_count = 3U;
    fixture_acpi_madt.local_apic_count = 1U;
    fixture_acpi_madt.enabled_processor_count = 1U;
    fixture_acpi_madt.io_apic_count = 1U;
    fixture_slab_count = 1U;
    fixture_slab_info_result = OK;
    fixture_slab_self_test_result = OK;
    fixture_scheduler_stats_result = OK;
    fixture_scheduler_validation_result = OK;
    fixture_page_fault_stats_result = OK;
    fixture_user_area_count = 2U;
    fixture_process_lookup_result = OK;
    fixture_process_is_user_result = 1;
    fixture_process_vma_copy_result = OK;
    fixture_memcheck_detailed_result = OK;
    fixture_memcheck_slab_validate_result = OK;
    fixture_memcheck_allocations = 0U;
    fixture_memcheck_frees = 0U;
    fixture_process_user_count = 0U;
    fixture_process_zombie_count = 0U;
    fixture_app_foreground_active = 0;
    kmemset(&fixture_signal_test, 0, sizeof(fixture_signal_test));
    fixture_signal_test.lifecycle = 1U;
    fixture_signal_test.actions = 1U;
    fixture_signal_test.blocking = 1U;
    fixture_signal_test.coalescing = 1U;
    fixture_signal_test.fatal_rules = 1U;
    fixture_signal_test.child_notification = 1U;
    fixture_signal_test.frame_rules = 1U;
    fixture_signal_test.invariants = 1U;
    fixture_signal_test_result = OK;
    fixture_signal_send_result = OK;
    fixture_signal_send_pid = 0U;
    fixture_signal_send_number = 0U;
    kmemset(&fixture_log_stats, 0, sizeof(fixture_log_stats));
    kmemset(fixture_log_records, 0, sizeof(fixture_log_records));
    kmemset(&fixture_log_test, 0, sizeof(fixture_log_test));
    fixture_log_stats.capacity = LOG_RECORD_CAPACITY;
    fixture_log_stats.occupancy = 2U;
    fixture_log_stats.console_level = LOG_LEVEL_INFO;
    fixture_log_stats.buffer_level = LOG_LEVEL_DEBUG;
    fixture_log_stats.next_sequence = 12U;
    fixture_log_stats.overwritten_records = 3U;
    fixture_log_stats.grouped_events = 4U;
    fixture_log_stats.truncated_events = 5U;
    fixture_log_stats.dropped_events = 6U;
    fixture_log_stats.clear_count = 7U;
    fixture_log_copy_count = 2U;
    fixture_log_records[0].sequence = 10U;
    fixture_log_records[0].first_tick = 100U;
    fixture_log_records[0].last_tick = 110U;
    fixture_log_records[0].level = LOG_LEVEL_ERROR;
    fixture_log_records[0].error_code = -4;
    fixture_log_records[0].occurrences = 2U;
    fixture_log_records[0].flags = LOG_RECORD_FLAG_HAS_ERROR_CODE;
    copy_text(fixture_log_records[0].module, sizeof(fixture_log_records[0].module),
              "TEST");
    copy_text(fixture_log_records[0].message,
              sizeof(fixture_log_records[0].message), "falha");
    fixture_log_records[1] = fixture_log_records[0];
    fixture_log_records[1].sequence = 11U;
    fixture_log_records[1].level = LOG_LEVEL_DEBUG;
    fixture_log_records[1].flags = 0U;
    fixture_log_test.passed = 8U;
    fixture_log_test.failed = 0U;
    fixture_log_test.order_and_metadata = 1U;
    fixture_log_test.wrap_and_overwrite = 1U;
    fixture_log_test.repetition_grouping = 1U;
    fixture_log_test.safe_truncation = 1U;
    fixture_log_test.optional_error_code = 1U;
    fixture_log_test.clear_behavior = 1U;
    fixture_log_test.text_serialization = 1U;
    fixture_log_test.level_filtering = 1U;
    kmemset(&fixture_timer_stats, 0, sizeof(fixture_timer_stats));
    kmemset(fixture_timer_records, 0, sizeof(fixture_timer_records));
    kmemset(&fixture_timer_test, 0, sizeof(fixture_timer_test));
    fixture_timer_stats.initialized = 1U;
    fixture_timer_stats.current_tick = 1000U;
    fixture_timer_stats.frequency = 100U;
    fixture_timer_stats.occupancy = 1U;
    fixture_timer_stats.capacity = TIMER_CAPACITY;
    fixture_timer_stats.owner_occupancy = 1U;
    fixture_timer_stats.owner_capacity = TIMER_OWNER_CAPACITY;
    fixture_timer_stats.armed = 1U;
    fixture_timer_stats.high_watermark = 2U;
    fixture_timer_stats.timers_created = 3U;
    fixture_timer_stats.timers_started = 4U;
    fixture_timer_stats.expirations = 5U;
    fixture_timer_stats.cancellations = 6U;
    fixture_timer_stats.timers_destroyed = 7U;
    fixture_timer_stats.callbacks = 8U;
    fixture_timer_stats.callback_errors = 1U;
    fixture_timer_stats.delayed_callbacks = 2U;
    fixture_timer_stats.missed_periods = 3U;
    fixture_timer_stats.invalid_operations = 4U;
    kmemset(&fixture_keyboard_metrics, 0, sizeof(fixture_keyboard_metrics));
    fixture_keyboard_metrics.queued = 2U;
    fixture_keyboard_metrics.capacity = 32U;
    fixture_keyboard_metrics.dropped = 3U;
    fixture_keyboard_metrics.processed = 4U;
    fixture_keyboard_metrics.peak_queued = 5U;
    kmemset(&fixture_ipc_stats, 0, sizeof(fixture_ipc_stats));
    fixture_ipc_stats.sent = 6U;
    fixture_ipc_stats.received = 7U;
    fixture_ipc_stats.failed = 1U;
    fixture_ipc_stats.queue_full = 2U;
    kmemset(&fixture_vesa_metrics, 0, sizeof(fixture_vesa_metrics));
    fixture_vesa_metrics.presentations = 3U;
    fixture_vesa_metrics.full_presentations = 2U;
    fixture_vesa_metrics.partial_presentations = 1U;
    fixture_vesa_metrics.bytes_copied = 4096U;
    fixture_vesa_metrics.last_copy_bytes = 1024U;
    fixture_vesa_metrics.last_copy_ticks = 4U;
    fixture_vesa_metrics.max_copy_ticks = 8U;
    kmemset(&fixture_vesa_mode, 0, sizeof(fixture_vesa_mode));
    fixture_vesa_mode.initialized = 1U;
    fixture_vesa_mode.width = VESA_WIDTH_640;
    fixture_vesa_mode.height = VESA_HEIGHT_480;
    fixture_vesa_mode.bpp = VESA_BPP_32;
    kmemset(&fixture_paging_boot_stats, 0, sizeof(fixture_paging_boot_stats));
    fixture_paging_boot_stats.identity_pages = 12U;
    fixture_paging_boot_stats.page_tables_created = 3U;
    fixture_paging_boot_stats.init_ticks = 9U;
    fixture_paging_boot_stats.initialized = 1U;
    fixture_timer_records[0].handle = 0x1234U;
    copy_text(fixture_timer_records[0].owner_name,
              sizeof(fixture_timer_records[0].owner_name), "SHELL");
    copy_text(fixture_timer_records[0].name,
              sizeof(fixture_timer_records[0].name), "heartbeat");
    fixture_timer_records[0].mode = TIMER_MODE_PERIODIC;
    fixture_timer_records[0].state = TIMER_STATE_ARMED;
    fixture_timer_records[0].deadline_tick = 1100U;
    fixture_timer_records[0].period_ticks = 100U;
    fixture_timer_records[0].executions = 9U;
    fixture_timer_records[0].delayed_callbacks = 1U;
    fixture_timer_records[0].missed_periods = 2U;
    fixture_timer_records[0].last_lateness_ticks = 3U;
    fixture_timer_records[0].last_error = -ERR_DISK;
    fixture_timer_test.conversion_and_limits = 1U;
    fixture_timer_test.one_shot = 1U;
    fixture_timer_test.periodic_no_drift = 1U;
    fixture_timer_test.periodic_coalescing = 1U;
    fixture_timer_test.cancel_armed = 1U;
    fixture_timer_test.cancel_pending = 1U;
    fixture_timer_test.owner_destruction = 1U;
    fixture_timer_test.stale_handles = 1U;
    fixture_timer_test.tick_wrap = 1U;
    fixture_timer_test.capacity = 1U;
    fixture_timer_test.callback_errors = 1U;
    fixture_timer_test.invariants = 1U;
    fixture_timer_test.passed = 11U;
    fixture_timer_test.failed = 0U;
    fixture_clock_status.initialized = 1U;
    fixture_clock_status.monotonic_available = 1U;
    fixture_clock_status.utc_available = 1U;
    fixture_clock_status.source = CLOCK_SOURCE_RTC;
    fixture_clock_status.frequency = 100U;
    fixture_clock_status.anchor_monotonic_ticks = 100000ULL;
    fixture_clock_status.anchor_unix_seconds = 1735689600ULL;
    fixture_clock_status.monotonic_wraps = 2ULL;
    fixture_clock_status.reads = 12U;
    fixture_clock_status.last_error = OK;
    fixture_clock_test.epoch_conversion = 1U;
    fixture_clock_test.leap_year_conversion = 1U;
    fixture_clock_test.invalid_date_rejected = 1U;
    fixture_clock_test.monotonic_rollover = 1U;
    fixture_clock_test.invariants = 1U;
    fixture_clock_test.passed = 5U;
    fixture_clock_test.failed = 0U;
    fixture_rtc_status.initialized = 1U;
    fixture_rtc_status.available = 1U;
    fixture_rtc_status.valid = 1U;
    fixture_rtc_status.utc.year = 2025U;
    fixture_rtc_status.utc.month = 1U;
    fixture_rtc_status.utc.day = 1U;
    fixture_rtc_status.utc.hour = 0U;
    fixture_rtc_status.utc.minute = 0U;
    fixture_rtc_status.utc.second = 0U;
    fixture_rtc_test.bcd_conversion = 1U;
    fixture_rtc_test.binary_conversion = 1U;
    fixture_rtc_test.twelve_hour_conversion = 1U;
    fixture_rtc_test.calendar_validation = 1U;
    fixture_rtc_test.invalid_dates_rejected = 1U;
    fixture_rtc_test.passed = 5U;
    fixture_rtc_test.failed = 0U;
    kmemset(&fixture_irq_status, 0, sizeof(fixture_irq_status));
    kmemset(fixture_irq_lines, 0, sizeof(fixture_irq_lines));
    kmemset(fixture_idt_lines, 0, sizeof(fixture_idt_lines));
    kmemset(&fixture_irq_test, 0, sizeof(fixture_irq_test));
    fixture_irq_status.initialized = 1U;
    fixture_irq_status.queued = 2U;
    fixture_irq_status.running = 1U;
    fixture_irq_status.capacity = IRQ_DEFERRED_USABLE_CAPACITY;
    fixture_irq_status.scheduled = 8U;
    fixture_irq_status.dispatched = 7U;
    fixture_irq_status.coalesced = 2U;
    fixture_irq_status.reruns = 1U;
    fixture_irq_status.cancelled = 3U;
    fixture_irq_status.peak_queued = 4U;
    fixture_irq_status.rejected = 1U;
    fixture_irq_status.context_errors = 2U;
    fixture_irq_status.last_error = ERR_INVALID;
    fixture_irq_lines[1].scheduled = 4U;
    fixture_irq_lines[1].dispatched = 3U;
    fixture_irq_lines[1].coalesced = 1U;
    fixture_irq_lines[1].rejected = 2U;
    fixture_irq_lines[1].cancelled = 1U;
    fixture_idt_lines[1].irq_line = 1U;
    fixture_idt_lines[1].registered_handlers = 2U;
    fixture_idt_lines[1].occurrences = 9U;
    fixture_irq_test.lifecycle = 1U;
    fixture_irq_test.coalescing = 1U;
    fixture_irq_test.rerun = 1U;
    fixture_irq_test.cancellation = 1U;
    fixture_irq_test.capacity = 1U;
    fixture_irq_test.attribution = 1U;
    fixture_irq_test.interrupt_context = 1U;
    fixture_irq_test.invariants = 1U;
    fixture_irq_test.passed = 8U;
    fixture_irq_test.failed = 0U;
    kmemset(&fixture_wait_stats, 0, sizeof(fixture_wait_stats));
    kmemset(fixture_wait_queues, 0, sizeof(fixture_wait_queues));
    kmemset(fixture_waiters, 0, sizeof(fixture_waiters));
    kmemset(&fixture_wait_test, 0, sizeof(fixture_wait_test));
    fixture_wait_stats.initialized = 1U;
    fixture_wait_stats.channels_active = 1U;
    fixture_wait_stats.active_waiters = 1U;
    fixture_wait_stats.peak_waiters = 2U;
    fixture_wait_stats.waits_started = 8U;
    fixture_wait_stats.event_wakes = 3U;
    fixture_wait_stats.timeout_wakes = 2U;
    fixture_wait_stats.cancellation_wakes = 1U;
    fixture_wait_stats.unavailable_wakes = 1U;
    fixture_wait_stats.invalid_operations = 2U;
    fixture_wait_stats.registry_capacity = WAIT_QUEUE_REGISTRY_CAPACITY;
    fixture_wait_stats.registry_peak = 4U;
    fixture_wait_stats.registration_rejections = 1U;
    fixture_wait_stats.wake_one_calls = 5U;
    fixture_wait_stats.wake_all_calls = 6U;
    fixture_wait_stats.context_errors = 1U;
    fixture_wait_stats.orphan_errors = 2U;
    fixture_wait_stats.signal_wakes = 3U;
    fixture_wait_queues[0].id = 7U;
    copy_text(fixture_wait_queues[0].owner,
              sizeof(fixture_wait_queues[0].owner), "timer");
    fixture_wait_queues[0].condition = 12U;
    fixture_wait_queues[0].waiters = 1U;
    fixture_wait_queues[0].peak_waiters = 2U;
    fixture_wait_queues[0].available = 1U;
    fixture_waiters[0].id = 23U;
    fixture_waiters[0].queue_id = 7U;
    fixture_waiters[0].queue_position = 0U;
    fixture_waiters[0].target = WAIT_TARGET_THREAD;
    copy_text(fixture_waiters[0].name, sizeof(fixture_waiters[0].name), "worker");
    copy_text(fixture_waiters[0].channel_owner,
              sizeof(fixture_waiters[0].channel_owner), "timer");
    fixture_waiters[0].reason = WAIT_REASON_SIGNAL;
    fixture_waiters[0].deadline_tick = 500U;
    fixture_waiters[0].remaining_ticks = 25U;
    fixture_waiters[0].deadline_active = 1U;
    fixture_waiters[0].active = 1U;
    fixture_wait_test.channel_lifecycle = 1U;
    fixture_wait_test.condition_signal = 1U;
    fixture_wait_test.availability = 1U;
    fixture_wait_test.accounting = 1U;
    fixture_wait_test.reasons = 1U;
    fixture_wait_test.limits = 1U;
    fixture_wait_test.reset = 1U;
    fixture_wait_test.fifo = 1U;
    fixture_wait_test.wake_all = 1U;
    fixture_wait_test.lost_wakeup = 1U;
    fixture_wait_test.condition_recheck = 1U;
    fixture_wait_test.process_thread = 1U;
    fixture_wait_test.interrupt_context = 1U;
    fixture_wait_test.invariants = 1U;
    fixture_wait_test.passed = 14U;
    fixture_wait_test.failed = 0U;
    kmemset(&fixture_workq_stats, 0, sizeof(fixture_workq_stats));
    kmemset(fixture_work_records, 0, sizeof(fixture_work_records));
    kmemset(&fixture_workq_test, 0, sizeof(fixture_workq_test));
    fixture_workq_stats.initialized = 1U;
    fixture_workq_stats.worker_bound = 1U;
    fixture_workq_stats.worker_active = 1U;
    fixture_workq_stats.fallback_active = 0U;
    fixture_workq_stats.worker_pid = 42U;
    fixture_workq_stats.execution_context = WORK_CONTEXT_KWORKER;
    fixture_workq_stats.capacity = WORKQUEUE_CAPACITY;
    fixture_workq_stats.registered = 1U;
    fixture_workq_stats.ready_high = 1U;
    fixture_workq_stats.ready_normal = 2U;
    fixture_workq_stats.delayed = 1U;
    fixture_workq_stats.running = 1U;
    fixture_workq_stats.scheduled = 8U;
    fixture_workq_stats.executed = 4U;
    fixture_workq_stats.coalesced = 2U;
    fixture_workq_stats.reruns = 1U;
    fixture_workq_stats.cancelled = 1U;
    fixture_workq_stats.rejected = 2U;
    fixture_workq_stats.callback_errors = 1U;
    fixture_workq_stats.context_errors = 1U;
    fixture_workq_stats.wake_errors = 1U;
    fixture_workq_stats.wakeups = 6U;
    fixture_workq_stats.sleeps = 3U;
    fixture_workq_stats.peak_pending = 4U;
    fixture_workq_stats.total_callback_ticks = 20U;
    fixture_workq_stats.max_callback_ticks = 8U;
    fixture_work_records[0].id = 0x01000005U;
    fixture_work_records[0].generation = 1U;
    copy_text(fixture_work_records[0].owner,
              sizeof(fixture_work_records[0].owner), "shell");
    fixture_work_records[0].priority = WORK_PRIORITY_HIGH;
    fixture_work_records[0].state = WORK_STATE_RUNNING;
    fixture_work_records[0].deadline_tick = 300U;
    fixture_work_records[0].remaining_ticks = 10U;
    fixture_work_records[0].scheduled = 5U;
    fixture_work_records[0].executed = 4U;
    fixture_work_records[0].coalesced = 1U;
    fixture_work_records[0].reruns = 2U;
    fixture_work_records[0].cancellations = 1U;
    fixture_work_records[0].callback_ticks = 20U;
    fixture_work_records[0].max_callback_ticks = 8U;
    fixture_work_records[0].last_error = -ERR_TIMEOUT;
    fixture_workq_test.lifecycle = 1U;
    fixture_workq_test.fifo = 1U;
    fixture_workq_test.priority = 1U;
    fixture_workq_test.delayed = 1U;
    fixture_workq_test.rollover = 1U;
    fixture_workq_test.promotion = 1U;
    fixture_workq_test.coalescing = 1U;
    fixture_workq_test.rerun = 1U;
    fixture_workq_test.cancellation = 1U;
    fixture_workq_test.capacity = 1U;
    fixture_workq_test.interrupt_context = 1U;
    fixture_workq_test.invariants = 1U;
    fixture_workq_test.passed = 12U;
    fixture_workq_test.failed = 0U;
    kmemset(&fixture_tls_policy, 0, sizeof(fixture_tls_policy));
    kmemset(&fixture_tls_status, 0, sizeof(fixture_tls_status));
    kmemset(&fixture_tls_test, 0, sizeof(fixture_tls_test));
    fixture_tls_policy.minimum_version = TLS_VERSION_1_3;
    fixture_tls_policy.require_static_ca = 1U;
    fixture_tls_policy.require_hostname_san = 1U;
    fixture_tls_policy.require_validity_window = 1U;
    fixture_tls_policy.allow_spki_pin = 1U;
    fixture_tls_policy.allow_http_fallback = 0U;
    fixture_tls_policy.trust_current_version = 4U;
    fixture_tls_policy.trust_next_version = 5U;
    fixture_tls_policy.trust_revocation_version = 3U;
    fixture_tls_status.initialized = 1U;
    fixture_tls_status.policy_ready = 1U;
    fixture_tls_status.handshake_available = 1U;
    fixture_tls_status.x509_available = 1U;
    fixture_tls_status.trusted_time_available = 1U;
    fixture_tls_status.static_ca_required = 1U;
    fixture_tls_status.spki_pinning_optional = 1U;
    fixture_tls_status.http_fallback_forbidden = 1U;
    fixture_tls_status.state = TLS_STATE_READY;
    fixture_tls_status.last_reason = TLS_REASON_NONE;
    fixture_tls_status.policy_checks = 10U;
    fixture_tls_status.policy_rejections = 2U;
    fixture_tls_status.last_error = OK;
    fixture_tls_status.entropy_available = 1U;
    fixture_tls_status.certificate_validation_available = 1U;
    fixture_tls_test.valid_identity = 1U;
    fixture_tls_test.time_unavailable = 1U;
    fixture_tls_test.certificate_future = 1U;
    fixture_tls_test.certificate_expired = 1U;
    fixture_tls_test.untrusted_chain = 1U;
    fixture_tls_test.san_mismatch = 1U;
    fixture_tls_test.pin_absent = 1U;
    fixture_tls_test.pin_match = 1U;
    fixture_tls_test.pin_mismatch = 1U;
    fixture_tls_test.trust_rotation = 1U;
    fixture_tls_test.trust_revocation = 1U;
    fixture_tls_test.invariants = 1U;
    fixture_tls_test.passed = 12U;
    fixture_tls_test.failed = 0U;
    kmemset(&fixture_vfs_status, 0, sizeof(fixture_vfs_status));
    kmemset(fixture_vfs_descriptors, 0, sizeof(fixture_vfs_descriptors));
    kmemset(fixture_vfs_mounts, 0, sizeof(fixture_vfs_mounts));
    kmemset(&fixture_vfs_test, 0, sizeof(fixture_vfs_test));
    fixture_vfs_status.initialized = 1U;
    fixture_vfs_status.descriptor_capacity = VFS_MAX_FDS;
    fixture_vfs_status.global_file_capacity = VFS_MAX_OPEN_FILES;
    fixture_vfs_status.global_files_used = 3U;
    fixture_vfs_status.processes_with_tables = 2U;
    fixture_vfs_status.descriptors_open = 4U;
    fixture_vfs_status.opens = 10U;
    fixture_vfs_status.reads = 11U;
    fixture_vfs_status.writes = 12U;
    fixture_vfs_status.seeks = 13U;
    fixture_vfs_status.closes = 14U;
    fixture_vfs_status.failures = 1U;
    fixture_vfs_status.mount_capacity = VFS_MAX_MOUNTS;
    fixture_vfs_status.mounts_active = 2U;
    fixture_vfs_status.lookups = 15U;
    fixture_vfs_status.chdirs = 2U;
    fixture_vfs_status.ioctls = 3U;
    fixture_vfs_status.device_capacity = 8U;
    fixture_vfs_status.devices_active = 4U;
    fixture_vfs_status.pipe_capacity = VFS_MAX_PIPES;
    fixture_vfs_status.pipes_active = 1U;
    fixture_vfs_status.pipe_reads = 5U;
    fixture_vfs_status.pipe_writes = 6U;
    fixture_vfs_descriptors[0].pid = 42U;
    fixture_vfs_descriptors[0].fd = 3;
    fixture_vfs_descriptors[0].type = VFS_NODE_REGULAR;
    fixture_vfs_descriptors[0].mode = VFS_MODE_READ;
    fixture_vfs_descriptors[0].offset = 7U;
    fixture_vfs_descriptors[0].size = 99U;
    copy_text(fixture_vfs_descriptors[0].path,
              sizeof(fixture_vfs_descriptors[0].path), "/etc/config");
    fixture_vfs_mounts[0].slot = 1U;
    fixture_vfs_mounts[0].generation = 4U;
    fixture_vfs_mounts[0].open_files = 2U;
    fixture_vfs_mounts[0].cwd_references = 1U;
    fixture_vfs_mounts[0].fs_type = STORAGE_FS_FAT32;
    fixture_vfs_mounts[0].used = 1U;
    fixture_vfs_mounts[0].pinned = 1U;
    fixture_vfs_mounts[0].read_only = 1U;
    copy_text(fixture_vfs_mounts[0].mount_point,
              sizeof(fixture_vfs_mounts[0].mount_point), "/");
    copy_text(fixture_vfs_mounts[0].volume_id,
              sizeof(fixture_vfs_mounts[0].volume_id), "ata0");
    fixture_vfs_mounts[0].kind = VFS_MOUNT_STORAGE;
    fixture_vfs_test.stdio = 1U;
    fixture_vfs_test.lifecycle = 1U;
    fixture_vfs_test.sequential_read = 1U;
    fixture_vfs_test.seek = 1U;
    fixture_vfs_test.permissions = 1U;
    fixture_vfs_test.eof = 1U;
    fixture_vfs_test.limits = 1U;
    fixture_vfs_test.table_capacity = 1U;
    fixture_vfs_test.closed_descriptor = 1U;
    fixture_vfs_test.isolation = 1U;
    fixture_vfs_test.cleanup = 1U;
    fixture_vfs_test.normalization = 1U;
    fixture_vfs_test.lookup = 1U;
    fixture_vfs_test.cwd = 1U;
    fixture_vfs_test.mount_busy = 1U;
    fixture_vfs_test.invariants = 1U;
    fixture_vfs_test.passed = 16U;
    fixture_vfs_test.total = 16U;
    fixture_vfs_test.directory_listing = 1U;
    fixture_vfs_test.devices = 1U;
    fixture_vfs_test.ioctl = 1U;
    fixture_vfs_test.pipes = 1U;
    fixture_vfs_test.procfs = 1U;
    fixture_vfs_test.sysfs = 1U;
    kmemset(&fixture_devfs_test, 0, sizeof(fixture_devfs_test));
    fixture_devfs_test.registry = 1U;
    fixture_devfs_test.null_device = 1U;
    fixture_devfs_test.zero_device = 1U;
    fixture_devfs_test.tty_device = 1U;
    fixture_devfs_test.speaker_device = 1U;
    fixture_devfs_test.block_device = 1U;
    fixture_devfs_test.permissions = 1U;
    fixture_devfs_test.cleanup = 1U;
    fixture_devfs_test.invariants = 1U;
    fixture_devfs_test.passed = 9U;
    fixture_devfs_test.total = 9U;
    kmemset(fixture_devices, 0, sizeof(fixture_devices));
    fixture_devices[0].kind = DEVICE_KIND_PCI;
    fixture_devices[0].status = DEVICE_STATUS_READY;
    fixture_devices[0].vendor_id = 0x1234U;
    fixture_devices[0].device_id = 0x5678U;
    fixture_devices[0].class_code = 0x02U;
    fixture_devices[0].subclass_code = 0x00U;
    fixture_devices[0].bus = 0U;
    fixture_devices[0].device = 3U;
    fixture_devices[0].function = 0U;
    fixture_devices[0].irq = 11U;
    fixture_devices[1].kind = DEVICE_KIND_ATA_PRIMARY;
    fixture_devices[1].status = DEVICE_STATUS_DEGRADED;
    fixture_devices[1].capacity_sectors = 2048U;
    fixture_devices[1].irq = DEVICE_IRQ_UNKNOWN;
    kmemset(&fixture_usb_status, 0, sizeof(fixture_usb_status));
    fixture_usb_status.initialized = 1U;
    fixture_usb_status.controller_count = 1U;
    fixture_usb_status.ehci_count = 1U;
    fixture_usb_status.ehci_ready_count = 1U;
    fixture_usb_status.dma_initialized = 1U;
    fixture_usb_status.irq_initialized = 1U;
    fixture_usb_status.transfer_available = 1U;
    fixture_usb_status.bulk_transfer_available = 1U;
    fixture_usb_status.high_speed_transfer_available = 1U;
    fixture_usb_status.port_count = 1U;
    fixture_usb_status.configured_device_count = 1U;
    fixture_usb_status.dma_td_capacity = 64U;
    fixture_usb_status.dma_td_in_use = 2U;
    fixture_usb_status.class_driver_active = 1U;
    fixture_usb_status.msc_device_count = 1U;
    fixture_usb_status.hid_device_count = 1U;
    fixture_usb_status.hid_active_count = 1U;
    fixture_usb_status.interrupt_transfer_available = 1U;
    kmemset(fixture_usb_controllers, 0, sizeof(fixture_usb_controllers));
    fixture_usb_controllers[0].model = USB_CONTROLLER_MODEL_EHCI;
    fixture_usb_controllers[0].state = USB_CONTROLLER_READY;
    fixture_usb_controllers[0].reason = USB_CONTROLLER_REASON_DRIVER_READY;
    fixture_usb_controllers[0].vendor_id = 0x8086U;
    fixture_usb_controllers[0].device_id = 0x24CDU;
    fixture_usb_controllers[0].class_code = USB_CONTROLLER_PCI_CLASS;
    fixture_usb_controllers[0].subclass_code = USB_CONTROLLER_PCI_SUBCLASS;
    fixture_usb_controllers[0].prog_if = USB_CONTROLLER_PROG_IF_EHCI;
    fixture_usb_controllers[0].revision = 1U;
    fixture_usb_controllers[0].bus = 0U;
    fixture_usb_controllers[0].device = 4U;
    fixture_usb_controllers[0].function = 0U;
    fixture_usb_controllers[0].irq = 11U;
    fixture_usb_controllers[0].bars[0] = 0x1000U;
    fixture_usb_controllers[0].ehci_initialized = 1U;
    fixture_usb_controllers[0].ehci_irq_registered = 1U;
    fixture_usb_controllers[0].ehci_dma_ready = 1U;
    fixture_usb_controllers[0].ehci_transfer_ready = 1U;
    fixture_usb_controllers[0].ehci_port_count = USB_EHCI_PORT_COUNT;
    fixture_usb_controllers[0].ehci_device_count = 1U;
    kmemset(fixture_usb_ports, 0, sizeof(fixture_usb_ports));
    copy_text(fixture_usb_ports[0].controller_id,
              sizeof(fixture_usb_ports[0].controller_id), "usb-00:04.0");
    fixture_usb_ports[0].controller_bus = 0U;
    fixture_usb_ports[0].controller_device = 4U;
    fixture_usb_ports[0].controller_model = USB_CONTROLLER_MODEL_EHCI;
    fixture_usb_ports[0].port_number = 1U;
    fixture_usb_ports[0].state = USB_PORT_CONFIGURED;
    fixture_usb_ports[0].reason = USB_PORT_REASON_NONE;
    fixture_usb_ports[0].speed = USB_DEVICE_SPEED_FULL;
    fixture_usb_ports[0].connected = 1U;
    fixture_usb_ports[0].enabled = 1U;
    fixture_usb_ports[0].usb_address = 5U;
    copy_text(fixture_usb_ports[0].device_id,
              sizeof(fixture_usb_ports[0].device_id), "usb-device-1");
    kmemset(fixture_usb_devices, 0, sizeof(fixture_usb_devices));
    copy_text(fixture_usb_devices[0].id, sizeof(fixture_usb_devices[0].id),
              "usb-device-1");
    copy_text(fixture_usb_devices[0].controller_id,
              sizeof(fixture_usb_devices[0].controller_id), "usb-00:04.0");
    fixture_usb_devices[0].controller_bus = 0U;
    fixture_usb_devices[0].controller_device = 4U;
    fixture_usb_devices[0].controller_function = 0U;
    fixture_usb_devices[0].port_number = 1U;
    fixture_usb_devices[0].state = USB_DEVICE_CONFIGURED;
    fixture_usb_devices[0].speed = USB_DEVICE_SPEED_HIGH;
    fixture_usb_devices[0].usb_address = 5U;
    fixture_usb_devices[0].controller_model = USB_CONTROLLER_MODEL_EHCI;
    fixture_usb_devices[0].vendor_id = 0x046DU;
    fixture_usb_devices[0].product_id = 0xC31CU;
    fixture_usb_devices[0].device_revision = 0x0100U;
    fixture_usb_devices[0].device_class = 0U;
    fixture_usb_devices[0].interface_class = 3U;
    fixture_usb_devices[0].max_packet_size0 = 64U;
    fixture_usb_devices[0].num_configurations = 1U;
    fixture_usb_devices[0].configuration_length = 34U;
    fixture_usb_devices[0].configuration_value = 1U;
    fixture_usb_devices[0].interface_number = 0U;
    fixture_usb_devices[0].endpoint_count = 1U;
    fixture_usb_devices[0].endpoints[0].address = 0x81U;
    fixture_usb_devices[0].endpoints[0].transfer_type = 3U;
    fixture_usb_devices[0].endpoints[0].max_packet = 8U;
    fixture_usb_devices[0].endpoints[0].interval = 10U;
    fixture_usb_devices[0].class_driver_active = 1U;
    fixture_usb_devices[0].device_descriptor_valid = 1U;
    fixture_usb_devices[0].configuration_descriptor_valid = 1U;
    fixture_usb_devices[0].interrupt_in_endpoint = 0x81U;
    fixture_usb_devices[0].interrupt_in_count = 1U;
    fixture_usb_devices[0].interrupt_in_max_packet = 8U;
    fixture_usb_devices[0].interrupt_interval = 10U;
    fixture_usb_devices[0].hid_driver_active = 1U;
    kmemset(fixture_usb_msc, 0, sizeof(fixture_usb_msc));
    copy_text(fixture_usb_msc[0].id, sizeof(fixture_usb_msc[0].id),
              "usb-device-1");
    copy_text(fixture_usb_msc[0].block_id, sizeof(fixture_usb_msc[0].block_id),
              "usbmsc0");
    copy_text(fixture_usb_msc[0].vendor, sizeof(fixture_usb_msc[0].vendor),
              "Zephyr");
    copy_text(fixture_usb_msc[0].product, sizeof(fixture_usb_msc[0].product),
              "TestDisk");
    copy_text(fixture_usb_msc[0].revision, sizeof(fixture_usb_msc[0].revision),
              "1.0");
    fixture_usb_msc[0].interface_number = 0U;
    fixture_usb_msc[0].bulk_in_endpoint = 0x81U;
    fixture_usb_msc[0].bulk_out_endpoint = 0x02U;
    fixture_usb_msc[0].bulk_in_max_packet = 512U;
    fixture_usb_msc[0].bulk_out_max_packet = 512U;
    fixture_usb_msc[0].sector_size = 512U;
    fixture_usb_msc[0].sector_count = 4096U;
    fixture_usb_msc[0].state = USB_MSC_READY;
    fixture_usb_msc[0].command_count = 4U;
    fixture_usb_msc[0].read_ops = 2U;
    fixture_usb_msc[0].reset_count = 1U;
    kmemset(fixture_usb_hid, 0, sizeof(fixture_usb_hid));
    copy_text(fixture_usb_hid[0].id, sizeof(fixture_usb_hid[0].id),
              "usb-device-1");
    copy_text(fixture_usb_hid[0].controller_id,
              sizeof(fixture_usb_hid[0].controller_id), "usb-00:04.0");
    fixture_usb_hid[0].kind = USB_HID_KIND_KEYBOARD;
    fixture_usb_hid[0].state = USB_HID_STATE_READY;
    fixture_usb_hid[0].interrupt_endpoint = 0x81U;
    fixture_usb_hid[0].max_packet = 8U;
    fixture_usb_hid[0].interval = 10U;
    fixture_usb_hid[0].active = 1U;
    fixture_usb_hid[0].report_count = 5U;
    fixture_usb_hid[0].malformed_count = 1U;
    fixture_usb_hid[0].timeout_count = 2U;
    fixture_usb_hid[0].error_count = 3U;
    fixture_usb_hid[0].dropped_count = 4U;
    fixture_usb_hid[0].cancel_count = 1U;
    fixture_recovery_usb.name = "USB";
    fixture_recovery_usb.state = RECOVERY_STATE_READY;
    fixture_recovery_usb.failures = 0U;
    fixture_recovery_usb.last_error = OK;
    fixture_recovery_usb.last_message = "ready";
    kmemset(fixture_recovery_components, 0,
            sizeof(fixture_recovery_components));
    for (uint32_t index = 0U; index < RECOVERY_COMPONENT_COUNT; index++) {
        fixture_recovery_components[index].name = fixture_recovery_names[index];
        fixture_recovery_components[index].state = RECOVERY_STATE_READY;
        fixture_recovery_components[index].last_error = OK;
        fixture_recovery_components[index].last_message = "ready";
    }
    fixture_recovery_components[RECOVERY_COMPONENT_USB] = fixture_recovery_usb;
    kmemset(&fixture_app_api_version, 0, sizeof(fixture_app_api_version));
    fixture_app_api_version.major = 1U;
    fixture_app_api_version.minor = 0U;
    kmemset(&fixture_update_capabilities, 0,
            sizeof(fixture_update_capabilities));
    fixture_update_capabilities.verifier_ready = 1U;
    fixture_update_capabilities.local_file_available = 1U;
    fixture_update_capabilities.apply_available = 1U;
    fixture_update_capabilities.rollback_available = 1U;
    fixture_update_capabilities.remote_available = 1U;
    fixture_update_capabilities.persistent_state_ready = 1U;
    fixture_update_capabilities.history_available = 1U;
    kmemset(&fixture_update_status, 0, sizeof(fixture_update_status));
    fixture_update_status.state_store = UPDATE_STORE_VALID;
    fixture_update_status.current_files = UPDATE_STORE_VALID;
    fixture_update_status.history_store = UPDATE_STORE_VALID;
    fixture_update_status.capabilities = fixture_update_capabilities;
    kmemset(&fixture_update_remote_status, 0,
            sizeof(fixture_update_remote_status));
    fixture_update_remote_status.state = UPDATE_REMOTE_STATE_READY;
    fixture_update_remote_status.reason = UPDATE_REMOTE_REASON_NONE;
    fixture_update_remote_status.cache_store = UPDATE_REMOTE_STORE_VALID;
    fixture_update_remote_status.initialized = 1U;
    fixture_update_remote_status.enabled = 1U;
    fixture_update_remote_status.network_ready = 1U;
    kmemset(&fixture_app_catalog_status, 0,
            sizeof(fixture_app_catalog_status));
    fixture_app_catalog_status.source_count = 1U;
    fixture_app_catalog_status.valid_source_count = 1U;
    fixture_app_catalog_status.entry_count = 1U;
    kmemset(&fixture_net_buffer_stats, 0, sizeof(fixture_net_buffer_stats));
    fixture_net_buffer_stats.initialized = 1U;
    fixture_net_buffer_stats.last_error = OK;
    kmemset(&fixture_skb_stats, 0, sizeof(fixture_skb_stats));
    fixture_skb_stats.initialized = 1U;
    fixture_skb_stats.last_error = OK;
    kmemset(&fixture_net_socket_status, 0,
            sizeof(fixture_net_socket_status));
    fixture_net_socket_status.initialized = 1U;
    fixture_net_socket_status.last_error = OK;
    kmemset(&fixture_socket_status, 0, sizeof(fixture_socket_status));
    fixture_socket_status.initialized = 1U;
    fixture_socket_status.last_error = OK;
    kmemset(&fixture_socket_test, 0, sizeof(fixture_socket_test));
    fixture_socket_test.lifecycle = 1U;
    fixture_socket_test.fd_mapping = 1U;
    fixture_socket_test.duplicate_bind = 1U;
    fixture_socket_test.unix_bind_connect = 1U;
    fixture_socket_test.unix_accept = 1U;
    fixture_socket_test.stream_io = 1U;
    fixture_socket_test.queue_full = 1U;
    fixture_socket_test.nonblocking = 1U;
    fixture_socket_test.close_wakeup = 1U;
    fixture_socket_test.eof = 1U;
    fixture_socket_test.cancellation = 1U;
    fixture_socket_test.invalid_inputs = 1U;
    fixture_socket_test.invariants = 1U;
    fixture_socket_test.passed = 13U;
    kmemset(&fixture_block_cache_stats, 0,
            sizeof(fixture_block_cache_stats));
    fixture_block_cache_stats.durability_state = BLOCK_DURABILITY_READY;
    fixture_block_cache_stats.last_error = OK;
    fixture_block_cache_stats.last_sync_error = OK;
    kmemset(&fixture_block_durability, 0,
            sizeof(fixture_block_durability));
    fixture_block_durability.state = BLOCK_DURABILITY_READY;
    fixture_block_durability.last_error = OK;
    kmemset(&fixture_signal_stats, 0, sizeof(fixture_signal_stats));
    fixture_signal_stats.initialized = 1U;
    fixture_signal_stats.last_error = OK;
    kmemset(&fixture_input_metrics, 0, sizeof(fixture_input_metrics));
    fixture_input_metrics.initialized = 1U;
    fixture_input_metrics.key_capacity = INPUT_KEY_QUEUE_CAPACITY;
    fixture_input_metrics.pointer_capacity = INPUT_POINTER_QUEUE_CAPACITY;
    kmemset(&fixture_user_fault, 0, sizeof(fixture_user_fault));
    kmemset(processes, 0, sizeof(processes));
    processes[0] = &fixture_user_process;
    kmemset(fixture_slab_info, 0, sizeof(fixture_slab_info));
    copy_text(fixture_slab_info[0].name, sizeof(fixture_slab_info[0].name),
              "shell-test");
    fixture_slab_info[0].object_size = 64U;
    fixture_slab_info[0].alignment = 8U;
    fixture_slab_info[0].object_stride = 64U;
    fixture_slab_info[0].objects_per_slab = 8U;
    fixture_slab_info[0].active_objects = 2U;
    fixture_slab_info[0].capacity = 8U;
    fixture_slab_info[0].slabs = 1U;
    fixture_slab_info[0].pages = 1U;
    fixture_slab_info[0].allocation_failures = 0U;
    fixture_slab_info[0].invalid_frees = 0U;
    fixture_slab_info[0].double_frees = 0U;
    fixture_slab_info[0].initialized = 1U;
    kmemset(&fixture_scheduler_stats, 0, sizeof(fixture_scheduler_stats));
    fixture_scheduler_stats.context_switches = 10U;
    fixture_scheduler_stats.cooperative_yields = 4U;
    fixture_scheduler_stats.user_preemptions = 3U;
    fixture_scheduler_stats.idle_fallbacks = 2U;
    fixture_scheduler_stats.user_quantum_ticks = 1U;
    fixture_scheduler_stats.idle_ticks = 20U;
    fixture_scheduler_stats.active_ticks = 80U;
    kmemset(&fixture_scheduler_validation, 0,
            sizeof(fixture_scheduler_validation));
    fixture_scheduler_validation.current_valid = 1U;
    fixture_scheduler_validation.idle_valid = 1U;
    fixture_scheduler_validation.pid_table_valid = 1U;
    fixture_scheduler_validation.state_table_valid = 1U;
    fixture_scheduler_validation.stack_table_valid = 1U;
    fixture_scheduler_validation.slab_table_valid = 1U;
    fixture_scheduler_validation.idle_accounting_valid = 1U;
    fixture_page_fault_stats.handled = 7U;
    fixture_page_fault_stats.invalid = 2U;
    kmemset(&fixture_user_process, 0, sizeof(fixture_user_process));
    fixture_user_process.pid = 42U;
    copy_text(fixture_user_process.name, sizeof(fixture_user_process.name),
              "user-fixture");
    fixture_user_process.state = PROCESS_STATE_READY;
    fixture_user_areas[0].start_addr = USER_CODE_BASE;
    fixture_user_areas[0].end_addr = USER_CODE_BASE + PAGE_SIZE;
    fixture_user_areas[0].flags = VM_READ | VM_EXEC;
    fixture_user_areas[1].start_addr = USER_STACK_BASE;
    fixture_user_areas[1].end_addr = USER_STACK_TOP;
    fixture_user_areas[1].flags = VM_READ | VM_WRITE;
    kmemset(&fixture_memcheck_heap_stats, 0,
            sizeof(fixture_memcheck_heap_stats));
    fixture_memcheck_heap_stats.total_bytes = 4096U;
    fixture_memcheck_heap_stats.used_bytes = 1024U;
    fixture_memcheck_heap_stats.free_bytes = 3072U;
    fixture_memcheck_heap_stats.allocated_blocks = 2U;
    fixture_memcheck_heap_stats.free_blocks = 3U;
    fixture_memcheck_heap_stats.largest_free_block = 2048U;
    fixture_memcheck_heap_stats.fragmentation_percent = 5U;
    fixture_memcheck_heap_stats.initialized = 1U;
    fixture_memcheck_heap_stats.valid = 1U;
    kmemset(&fixture_memcheck_pmm_stats, 0,
            sizeof(fixture_memcheck_pmm_stats));
    fixture_memcheck_pmm_stats.owned_pages = 50U;
    fixture_memcheck_pmm_stats.initialized = 1U;
    kmemset(&fixture_memcheck_detailed_stats, 0,
            sizeof(fixture_memcheck_detailed_stats));
    fixture_memcheck_detailed_stats.total_pages = 100U;
    fixture_memcheck_detailed_stats.zone_pages[MEMORY_ZONE_KERNEL] = 10U;
    fixture_memcheck_detailed_stats.zone_pages[MEMORY_ZONE_HEAP] = 20U;
    fixture_memcheck_detailed_stats.zone_pages[MEMORY_ZONE_SLAB] = 5U;
    fixture_memcheck_detailed_stats.zone_pages[MEMORY_ZONE_PROCESS] = 5U;
    fixture_memcheck_detailed_stats.zone_pages[MEMORY_ZONE_BUFFER] = 10U;
    fixture_memcheck_detailed_stats.zone_pages[MEMORY_ZONE_FREE] = 50U;
    fixture_memcheck_detailed_stats.free_runs = 10U;
    fixture_memcheck_detailed_stats.largest_free_run = 20U;
    fixture_memcheck_detailed_stats.isolated_free_pages = 5U;
    fixture_memcheck_detailed_stats.fragmentation_percent = 5U;
    fixture_memcheck_detailed_stats.initialized = 1U;
    fixture_memcheck_detailed_stats.valid = 1U;
    kmemset(&fixture_memcheck_paging_stats, 0,
            sizeof(fixture_memcheck_paging_stats));
    kmemset(&fixture_mouse_status, 0, sizeof(fixture_mouse_status));
    fixture_mouse_status.initialized = 1U;
    fixture_mouse_status.x = 12;
    fixture_mouse_status.y = 34;
    fixture_mouse_status.config.speed = 3U;
    fixture_mouse_status.config.primary_button = MOUSE_PRIMARY_LEFT;
}

static int contains_text(const char* text) {
    uint32_t text_length;
    uint32_t output_length;

    if (!text) return 0;
    text_length = kstrlen(text);
    output_length = kstrlen(video_output);
    if (!text_length || text_length > output_length) return 0;
    for (uint32_t offset = 0U;
         offset + text_length <= output_length; offset++) {
        uint32_t index = 0U;

        while (index < text_length &&
               video_output[offset + index] == text[index]) index++;
        if (index == text_length) return 1;
    }
    return 0;
}

static int expect_text(const char* text) {
    if (kstrcmp(video_output, text) == 0) return 0;
    fprintf(stderr, "diagnostics-host: saida inesperada: atual=%s esperado=%s\n",
            video_output, text);
    return 1;
}

static int expect_contains(const char* text) {
    if (contains_text(text)) return 0;
    fprintf(stderr, "diagnostics-host: trecho ausente: %s\nsaida=%s\n",
            text, video_output);
    return 1;
}

static void prepare_health_fixture(void) {
    fixture_irq_status.rejected = 0U;
    fixture_irq_status.context_errors = 0U;
    fixture_wait_stats.registration_rejections = 0U;
    fixture_wait_stats.context_errors = 0U;
    fixture_wait_stats.orphan_errors = 0U;
    fixture_workq_stats.rejected = 0U;
    fixture_workq_stats.callback_errors = 0U;
    fixture_workq_stats.context_errors = 0U;
    fixture_workq_stats.wake_errors = 0U;
    fixture_workq_stats.invariant_errors = 0U;
    fixture_vfs_status.device_capacity = DEVFS_MAX_NODES;
    fixture_vfs_status.devices_active = DEVFS_MAX_NODES;
    fixture_socket_status.active_count = 0U;
    fixture_socket_status.failures = 0U;
    fixture_net_socket_status.wait_failures = 0U;
    fixture_block_cache_stats.dirty_entries = 0U;
    fixture_block_cache_stats.writeback_entries = 0U;
    fixture_net_buffer_stats.active_buffers = 0U;
    fixture_net_buffer_stats.invalid_transitions = 0U;
    fixture_net_buffer_stats.duplicate_completions = 0U;
    fixture_skb_stats.active_buffers = 0U;
    fixture_skb_stats.invalid_operations = 0U;
    fixture_socket_test.failed = 0U;
}

void video_print(const char* text, uint8_t color) {
    (void)color;
    output_append(text);
}

void video_begin_update(void) {
}

void video_end_update(void) {
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
}

int log_get_stats(log_stats_t* stats) {
    if (!stats) return ERR_NULL;
    if (fixture_log_stats_result != OK) return fixture_log_stats_result;
    *stats = fixture_log_stats;
    return OK;
}

int log_copy_recent(log_record_t* records, uint32_t max_records,
                    uint32_t* count) {
    if (!records || !count) return ERR_NULL;
    if (fixture_log_copy_result != OK) return fixture_log_copy_result;
    if (max_records < fixture_log_copy_count) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < fixture_log_copy_count; index++) {
        records[index] = fixture_log_records[index];
    }
    *count = fixture_log_copy_count;
    return OK;
}

void log_clear_buffer(void) {
    fixture_log_clear_calls++;
}

int log_set_console_level(log_level_t level) {
    fixture_log_console_calls++;
    if (fixture_log_set_console_result == OK) {
        fixture_log_console_level = level;
    }
    return fixture_log_set_console_result;
}

int log_set_buffer_level(log_level_t level) {
    fixture_log_buffer_calls++;
    if (fixture_log_set_buffer_result == OK) {
        fixture_log_buffer_level = level;
    }
    return fixture_log_set_buffer_result;
}

void log_set_level(log_level_t level) {
    fixture_log_buffer_level = level;
    fixture_log_console_level = level;
}

log_level_t log_get_console_level(void) {
    return fixture_log_console_level;
}

log_level_t log_get_buffer_level(void) {
    return fixture_log_buffer_level;
}

int log_self_test(log_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_log_test;
    return fixture_log_test_result;
}

const char* log_level_str(log_level_t level) {
    if (level == LOG_LEVEL_ERROR) return "ERROR";
    if (level == LOG_LEVEL_WARN) return "WARN";
    if (level == LOG_LEVEL_INFO) return "INFO";
    if (level == LOG_LEVEL_DEBUG) return "DEBUG";
    return "INVALID";
}

int timer_get_stats(timer_stats_t* stats) {
    if (!stats) return ERR_NULL;
    if (fixture_timer_stats_result != OK) return fixture_timer_stats_result;
    *stats = fixture_timer_stats;
    return OK;
}

int timer_copy_active(timer_info_t* output, uint32_t max_timers,
                      uint32_t* out_count) {
    if (!output || !out_count) return ERR_NULL;
    if (fixture_timer_copy_result != OK) return fixture_timer_copy_result;
    if (max_timers < fixture_timer_copy_count) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < fixture_timer_copy_count; index++) {
        output[index] = fixture_timer_records[index];
    }
    *out_count = fixture_timer_copy_count;
    return OK;
}

int timer_self_test(timer_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_timer_test;
    return fixture_timer_test_result;
}

const char* timer_mode_name(timer_mode_t mode) {
    return mode == TIMER_MODE_PERIODIC ? "periodico" : "one-shot";
}

const char* timer_state_name(timer_state_t state) {
    if (state == TIMER_STATE_ARMED) return "armado";
    if (state == TIMER_STATE_PENDING) return "pendente";
    return "ocioso";
}

int clock_get_status(clock_status_t* status) {
    if (!status) return ERR_NULL;
    if (fixture_clock_status_result != OK) return fixture_clock_status_result;
    *status = fixture_clock_status;
    return OK;
}

int clock_get_monotonic_ticks(uint64_t* ticks) {
    if (!ticks) return ERR_NULL;
    if (fixture_clock_ticks_result != OK) return fixture_clock_ticks_result;
    *ticks = fixture_clock_ticks;
    return OK;
}

int clock_get_utc(uint64_t* utc) {
    if (!utc) return ERR_NULL;
    if (fixture_clock_utc_result != OK) return fixture_clock_utc_result;
    *utc = fixture_clock_utc;
    return OK;
}

int clock_self_test(clock_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_clock_test;
    return fixture_clock_test_result;
}

const char* clock_source_name(clock_source_t source) {
    return source == CLOCK_SOURCE_RTC ? "RTC" : "nenhuma";
}

int rtc_get_status(rtc_status_t* status) {
    if (!status) return ERR_NULL;
    if (fixture_rtc_status_result != OK) return fixture_rtc_status_result;
    *status = fixture_rtc_status;
    return OK;
}

int rtc_self_test(rtc_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_rtc_test;
    return fixture_rtc_test_result;
}

int irq_deferred_get_status(irq_deferred_status_t* status) {
    if (!status) return ERR_NULL;
    if (fixture_irq_status_result != OK) return fixture_irq_status_result;
    *status = fixture_irq_status;
    return OK;
}

int irq_deferred_get_irq_status(uint8_t irq_line,
                                irq_deferred_irq_status_t* status) {
    if (!status) return ERR_NULL;
    if (irq_line >= IRQ_DEFERRED_IRQ_COUNT) return ERR_INVALID;
    if (fixture_irq_line_result != OK) return fixture_irq_line_result;
    *status = fixture_irq_lines[irq_line];
    return OK;
}

int irq_deferred_self_test(irq_deferred_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_irq_test;
    return fixture_irq_test_result;
}

int irq_deferred_validate_state(void) {
    return fixture_irq_validate_result;
}

int idt_get_irq_status(uint8_t irq_line, idt_irq_status_t* status) {
    if (!status) return ERR_NULL;
    if (irq_line >= IDT_IRQ_LINE_COUNT) return ERR_INVALID;
    if (fixture_idt_validate_result != OK) return fixture_idt_validate_result;
    *status = fixture_idt_lines[irq_line];
    return OK;
}

int idt_validate_irq_state(void) {
    return fixture_idt_validate_result;
}

int wait_get_stats(wait_stats_t* stats) {
    if (!stats) return ERR_NULL;
    if (fixture_wait_stats_result != OK) return fixture_wait_stats_result;
    *stats = fixture_wait_stats;
    return OK;
}

int wait_queue_copy_info(wait_queue_info_t* output, uint32_t max_entries,
                         uint32_t* out_count) {
    if (!output || !out_count) return ERR_NULL;
    if (fixture_wait_info_result != OK) return fixture_wait_info_result;
    if (max_entries < fixture_wait_queue_count) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < fixture_wait_queue_count; index++) {
        output[index] = fixture_wait_queues[index];
    }
    *out_count = fixture_wait_queue_count;
    return OK;
}

int wait_queue_copy_waiters(wait_info_t* output, uint32_t max_entries,
                            uint32_t* out_count) {
    if (!output || !out_count) return ERR_NULL;
    if (fixture_waiters_result != OK) return fixture_waiters_result;
    if (max_entries < fixture_waiter_count) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < fixture_waiter_count; index++) {
        output[index] = fixture_waiters[index];
    }
    *out_count = fixture_waiter_count;
    return OK;
}

int wait_self_test(wait_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_wait_test;
    return fixture_wait_test_result;
}

int wait_validate_state(void) {
    return fixture_wait_stats_result;
}

const char* wait_reason_name(wait_reason_t reason) {
    if (reason == WAIT_REASON_EVENT) return "evento";
    if (reason == WAIT_REASON_TIMEOUT) return "timeout";
    if (reason == WAIT_REASON_CANCELLED) return "cancelado";
    if (reason == WAIT_REASON_DEVICE_UNAVAILABLE) return "indisponivel";
    if (reason == WAIT_REASON_SIGNAL) return "sinal";
    return "nenhum";
}

uint32_t timer_get_frequency(void) {
    return fixture_timer_stats.frequency;
}

uint32_t timer_get_ticks(void) {
    return fixture_ticks;
}

void keyboard_get_metrics(keyboard_metrics_t* metrics) {
    if (metrics) *metrics = fixture_keyboard_metrics;
}

void ipc_get_stats(ipc_stats_t* stats) {
    if (stats) *stats = fixture_ipc_stats;
}

uint32_t ipc_get_pending_count(void) {
    return fixture_ipc_pending_count;
}

int workqueue_get_stats(workqueue_stats_t* stats) {
    if (!stats) return ERR_NULL;
    if (fixture_workq_stats_result != OK) return fixture_workq_stats_result;
    *stats = fixture_workq_stats;
    return OK;
}

int workqueue_copy_info(work_info_t* output, uint32_t max_entries,
                        uint32_t* out_count) {
    if (!output || !out_count) return ERR_NULL;
    if (fixture_workq_info_result != OK) return fixture_workq_info_result;
    if (max_entries < fixture_work_info_count) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < fixture_work_info_count; index++) {
        output[index] = fixture_work_records[index];
    }
    *out_count = fixture_work_info_count;
    return OK;
}

int workqueue_self_test(workqueue_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_workq_test;
    return fixture_workq_test_result;
}

int workqueue_validate_state(void) {
    return fixture_workq_validate_result;
}

int workqueue_probe_worker(uint32_t timeout_ticks) {
    (void)timeout_ticks;
    return fixture_workq_probe_result;
}

const char* workqueue_priority_name(work_priority_t priority) {
    return priority == WORK_PRIORITY_HIGH ? "alta" : "normal";
}

const char* workqueue_state_name(work_state_t state) {
    if (state == WORK_STATE_READY) return "pronto";
    if (state == WORK_STATE_DELAYED) return "atrasado";
    if (state == WORK_STATE_RUNNING) return "executando";
    return "ocioso";
}

const char* workqueue_context_name(work_context_t context) {
    if (context == WORK_CONTEXT_KWORKER) return "kworker";
    if (context == WORK_CONTEXT_SYSTEM_FALLBACK) return "fallback";
    return "nenhum";
}

int tls_get_policy(tls_policy_t* policy) {
    if (!policy) return ERR_NULL;
    if (fixture_tls_policy_result != OK) return fixture_tls_policy_result;
    *policy = fixture_tls_policy;
    return OK;
}

int tls_get_status(tls_status_t* status) {
    if (!status) return ERR_NULL;
    if (fixture_tls_status_result != OK) return fixture_tls_status_result;
    *status = fixture_tls_status;
    return OK;
}

int tls_capability_available(void) {
    return fixture_tls_capability_result;
}

int tls_self_test(tls_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_tls_test;
    return fixture_tls_test_result;
}

const char* tls_state_name(tls_state_t state) {
    if (state == TLS_STATE_POLICY_ONLY) return "POLICY_ONLY";
    if (state == TLS_STATE_UNAVAILABLE) return "UNAVAILABLE";
    if (state == TLS_STATE_READY) return "READY";
    return "UNINITIALIZED";
}

const char* tls_reason_name(tls_reason_t reason) {
    if (reason == TLS_REASON_NONE) return "NONE";
    if (reason == TLS_REASON_TIME_UNAVAILABLE) return "TIME_UNAVAILABLE";
    if (reason == TLS_REASON_POLICY) return "POLICY";
    return "UNKNOWN";
}

int vfs_get_status(vfs_status_t* status) {
    if (!status) return ERR_NULL;
    if (fixture_vfs_status_result != OK) return fixture_vfs_status_result;
    *status = fixture_vfs_status;
    return OK;
}

int vfs_copy_descriptors(vfs_descriptor_info_t* output, uint32_t capacity,
                         uint32_t* out_count) {
    if (!output || !out_count) return ERR_NULL;
    if (fixture_vfs_descriptor_result != OK) return fixture_vfs_descriptor_result;
    if (capacity < fixture_vfs_descriptor_count) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < fixture_vfs_descriptor_count; index++) {
        output[index] = fixture_vfs_descriptors[index];
    }
    *out_count = fixture_vfs_descriptor_count;
    return OK;
}

int vfs_copy_mounts(vfs_mount_info_t* output, uint32_t capacity,
                    uint32_t* out_count) {
    if (!output || !out_count) return ERR_NULL;
    if (fixture_vfs_mount_result != OK) return fixture_vfs_mount_result;
    if (capacity < fixture_vfs_mount_count) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < fixture_vfs_mount_count; index++) {
        output[index] = fixture_vfs_mounts[index];
    }
    *out_count = fixture_vfs_mount_count;
    return OK;
}

int vfs_self_test(vfs_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_vfs_test;
    return fixture_vfs_test_result;
}

const char* storage_fs_name(storage_fs_type_t type) {
    if (type == STORAGE_FS_FAT12) return "FAT12";
    if (type == STORAGE_FS_FAT32) return "FAT32";
    return "UNKNOWN";
}

int devfs_self_test(devfs_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_devfs_test;
    return fixture_devfs_test_result;
}

int pci_init(void) {
    return fixture_scan_pci_result;
}

int usb_manager_refresh(void) {
    return fixture_scan_usb_refresh_result;
}

int usb_manager_init(void) {
    return fixture_scan_usb_init_result;
}

int storage_refresh(void) {
    return fixture_scan_storage_result;
}

int vfs_refresh_mounts(void) {
    return fixture_scan_mount_result;
}

int file_index_rebuild(void) {
    return fixture_scan_index_result;
}

int device_manager_refresh(void) {
    return fixture_scan_devices_result;
}

int network_manager_refresh(void) {
    return fixture_scan_network_result;
}

int wifi_manager_refresh(void) {
    return fixture_scan_wifi_refresh_result;
}

int wifi_manager_init(void) {
    return fixture_scan_wifi_init_result;
}

void process_yield(void) {
    fixture_yield_calls++;
}

int power_get_status(power_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    if (fixture_power_result != OK) return fixture_power_result;
    *out_status = fixture_power_status;
    return OK;
}

const char* power_capability_name(power_capability_t capability) {
    if (capability == POWER_CAPABILITY_AVAILABLE) return "DISPONIVEL";
    if (capability == POWER_CAPABILITY_SIMULATED) return "SIMULADO";
    return "INDISPONIVEL";
}

const char* power_service_state_name(power_service_state_t state) {
    if (state == POWER_SERVICE_DISCOVERING) return "DESCOBRINDO";
    if (state == POWER_SERVICE_READY) return "PRONTO";
    if (state == POWER_SERVICE_DEGRADED) return "DEGRADADO";
    if (state == POWER_SERVICE_UNAVAILABLE) return "INDISPONIVEL";
    return "DESCONHECIDO";
}

const char* power_transaction_phase_name(power_transaction_phase_t phase) {
    if (phase == POWER_TRANSACTION_ADMISSION) return "ADMISSION";
    if (phase == POWER_TRANSACTION_NOTIFICATION) return "NOTIFICATION";
    if (phase == POWER_TRANSACTION_SYNC_FLUSH) return "SYNC_FLUSH";
    if (phase == POWER_TRANSACTION_QUIESCENCE) return "QUIESCENCE";
    if (phase == POWER_TRANSACTION_HARDWARE_COMMIT) return "HARDWARE_COMMIT";
    if (phase == POWER_TRANSACTION_TERMINAL) return "TERMINAL";
    return "IDLE";
}

const char* power_transaction_target_name(power_transaction_target_t target) {
    if (target == POWER_TRANSACTION_TARGET_SHUTDOWN) return "SHUTDOWN";
    if (target == POWER_TRANSACTION_TARGET_REBOOT) return "REBOOT";
    return "NONE";
}

const char* power_quiescence_state_name(power_quiescence_state_t state) {
    if (state == POWER_QUIESCENCE_READY) return "READY";
    if (state == POWER_QUIESCENCE_DEGRADED) return "DEGRADED";
    if (state == POWER_QUIESCENCE_COMPLETE) return "COMPLETE";
    return "UNKNOWN";
}

int acpi_get_status(acpi_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    if (fixture_acpi_status_result != OK) return fixture_acpi_status_result;
    *out_status = fixture_acpi_status;
    return OK;
}

int acpi_get_power_info(acpi_power_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (fixture_acpi_power_result != OK) return fixture_acpi_power_result;
    *out_info = fixture_acpi_power;
    return OK;
}

int acpi_get_table_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    if (fixture_acpi_table_count_result != OK) {
        return fixture_acpi_table_count_result;
    }
    *out_count = fixture_acpi_table_count;
    return OK;
}

int acpi_get_table_at(uint32_t index, acpi_table_info_t* out_table) {
    if (!out_table) return ERR_NULL;
    if (fixture_acpi_table_result != OK) return fixture_acpi_table_result;
    if (index >= fixture_acpi_table_count || index >= 2U) return ERR_INVALID;
    *out_table = fixture_acpi_tables[index];
    return OK;
}

int acpi_get_madt_info(acpi_madt_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (fixture_acpi_madt_result != OK) return fixture_acpi_madt_result;
    *out_info = fixture_acpi_madt;
    return OK;
}

const char* acpi_root_kind_name(acpi_root_kind_t kind) {
    if (kind == ACPI_ROOT_RSDT) return "RSDT";
    if (kind == ACPI_ROOT_XSDT) return "XSDT";
    return "NONE";
}

int recovery_mark_ready(recovery_component_id_t component) {
    fixture_recovery_last_component = component;
    fixture_recovery_last_state = RECOVERY_STATE_READY;
    fixture_recovery_mark_calls++;
    return fixture_recovery_mark_result;
}

int recovery_mark_degraded(recovery_component_id_t component,
                           int error_code, const char* message) {
    (void)error_code;
    (void)message;
    fixture_recovery_last_component = component;
    fixture_recovery_last_state = RECOVERY_STATE_DEGRADED;
    fixture_recovery_mark_calls++;
    return fixture_recovery_mark_result;
}

int recovery_mark_disabled(recovery_component_id_t component,
                           int error_code, const char* message) {
    (void)error_code;
    (void)message;
    fixture_recovery_last_component = component;
    fixture_recovery_last_state = RECOVERY_STATE_DISABLED;
    fixture_recovery_mark_calls++;
    return fixture_recovery_mark_result;
}

int device_manager_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    if (fixture_device_count_result != OK) return fixture_device_count_result;
    *out_count = fixture_device_count;
    return OK;
}

int device_manager_get_info(uint32_t index, device_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (fixture_device_info_result != OK) return fixture_device_info_result;
    if (index >= fixture_device_count) return ERR_INVALID;
    *out_info = fixture_devices[index];
    return OK;
}

int device_manager_format_text(const device_info_t* info,
                               device_text_t* out_text) {
    if (!info || !out_text) return ERR_NULL;
    if (fixture_device_format_result != OK) return fixture_device_format_result;
    kmemset(out_text, 0, sizeof(*out_text));
    if (info->kind == DEVICE_KIND_PCI) {
        copy_text(out_text->id, sizeof(out_text->id), "pci-00:03.0");
        copy_text(out_text->name, sizeof(out_text->name), "Controlador de rede");
        copy_text(out_text->type, sizeof(out_text->type), "PCI");
        copy_text(out_text->location, sizeof(out_text->location),
                  "PCI 00:03.0");
        copy_text(out_text->detail, sizeof(out_text->detail),
                  "PCI dispositivo de teste");
    } else {
        copy_text(out_text->id, sizeof(out_text->id), "ata0");
        copy_text(out_text->name, sizeof(out_text->name), "ATA Primary");
        copy_text(out_text->type, sizeof(out_text->type), "STORAGE");
        copy_text(out_text->location, sizeof(out_text->location), "primary");
        copy_text(out_text->detail, sizeof(out_text->detail),
                  "2048 setores");
    }
    return OK;
}

int device_manager_find(const char* id, device_info_t* out_info) {
    device_text_t text;

    if (!id || !out_info) return ERR_NULL;
    if (fixture_device_find_result != OK) return fixture_device_find_result;
    for (uint32_t index = 0U; index < fixture_device_count; index++) {
        if (device_manager_format_text(&fixture_devices[index], &text) == OK &&
            kstrcmp(id, text.id) == 0) {
            *out_info = fixture_devices[index];
            return OK;
        }
    }
    return ERR_NOT_FOUND;
}

const char* device_manager_status_name(device_status_t status) {
    if (status == DEVICE_STATUS_READY) return "READY";
    if (status == DEVICE_STATUS_DEGRADED) return "DEGRADED";
    if (status == DEVICE_STATUS_DISABLED) return "DISABLED";
    return "UNKNOWN";
}

int vfs_list_dir(const char* path, vfs_dir_entry_t* entries,
                uint32_t capacity, uint32_t* out_count) {
    return fixture_vfs_list(path, entries, capacity, out_count);
}

int vfs_open(const char* path, uint32_t mode, int32_t* fd_out) {
    fixture_vfs_file_t file;

    if (!path || !fd_out) return ERR_NULL;
    *fd_out = VFS_FD_INVALID;
    if (mode & VFS_MODE_WRITE) {
        if (kstrcmp(path, "/proc/uptime") == 0 ||
            kstrcmp(path, "/sys/power/state") == 0) {
            return ERR_UNAVAILABLE;
        }
    }
    if (fixture_vfs_get_file(path, &file) != OK) {
        return ERR_NOT_FOUND;
    }
    if ((mode & VFS_MODE_WRITE) && !file.writable) {
        return ERR_UNAVAILABLE;
    }
    for (uint32_t index = 0U; index < HOST_VFS_HANDLE_CAPACITY; index++) {
        if (!fixture_vfs_handles[index].used) {
            fixture_vfs_handles[index].file = file;
            fixture_vfs_handles[index].offset = 0U;
            fixture_vfs_handles[index].mode = mode;
            fixture_vfs_handles[index].used = 1U;
            fixture_vfs_open_calls++;
            *fd_out = (int32_t)(VFS_FD_FIRST_FILE + index);
            return OK;
        }
    }
    return ERR_UNAVAILABLE;
}

int vfs_read(int32_t fd, void* buffer, uint32_t size,
             uint32_t* bytes_read) {
    uint32_t index;
    uint32_t available;
    uint32_t count;

    if (!buffer || !bytes_read || fd < VFS_FD_FIRST_FILE) return ERR_NULL;
    index = (uint32_t)(fd - VFS_FD_FIRST_FILE);
    if (index >= HOST_VFS_HANDLE_CAPACITY || !fixture_vfs_handles[index].used) {
        return ERR_INVALID;
    }
    if (!(fixture_vfs_handles[index].mode & VFS_MODE_READ)) {
        return ERR_UNAVAILABLE;
    }
    if (fixture_vfs_handles[index].offset > fixture_vfs_handles[index].file.size) {
        return ERR_STATE;
    }
    available = fixture_vfs_handles[index].file.size -
                fixture_vfs_handles[index].offset;
    count = size < available ? size : available;
    if (count) kmemcpy(buffer,
                       fixture_vfs_handles[index].file.content +
                           fixture_vfs_handles[index].offset,
                       count);
    fixture_vfs_handles[index].offset += count;
    *bytes_read = count;
    return OK;
}

static int fixture_vfs_bytes_equal(const void* left, const char* right,
                                   uint32_t size) {
    const uint8_t* bytes = (const uint8_t*)left;

    if (!left || !right) return 0;
    for (uint32_t index = 0U; index < size; index++) {
        if (bytes[index] != (uint8_t)right[index]) return 0;
    }
    return 1;
}

int vfs_write(int32_t fd, const void* buffer, uint32_t size,
              uint32_t* bytes_written) {
    uint32_t index;
    const char* path;

    if (!buffer || !bytes_written || fd < VFS_FD_FIRST_FILE) return ERR_NULL;
    index = (uint32_t)(fd - VFS_FD_FIRST_FILE);
    if (index >= HOST_VFS_HANDLE_CAPACITY || !fixture_vfs_handles[index].used) {
        fprintf(stderr, "write fd=%d size=%lu -> invalid\n", (int)fd,
                (unsigned long)size);
        return ERR_INVALID;
    }
    path = fixture_vfs_handles[index].file.path;
    if (!(fixture_vfs_handles[index].mode & VFS_MODE_WRITE)) {
        return ERR_UNAVAILABLE;
    }
    if (size != 6U || !fixture_vfs_bytes_equal(buffer, "debug\n", size)) {
        return ERR_INVALID;
    }
    if (kstrcmp(path, "/proc/sys/kernel/console_log_level") == 0) {
        copy_text(fixture_vfs_console_level,
                  sizeof(fixture_vfs_console_level),
                  "console_log_level debug\n");
    } else if (kstrcmp(path, "/proc/sys/kernel/buffer_log_level") == 0) {
        copy_text(fixture_vfs_buffer_level,
                  sizeof(fixture_vfs_buffer_level),
                  "buffer_log_level debug\n");
    } else {
        return ERR_UNAVAILABLE;
    }
    *bytes_written = size;
    return OK;
}

int vfs_lseek(int32_t fd, int32_t offset, uint32_t whence,
              uint32_t* position) {
    uint32_t index;
    int64_t target;

    if (!position || fd < VFS_FD_FIRST_FILE) return ERR_NULL;
    index = (uint32_t)(fd - VFS_FD_FIRST_FILE);
    if (index >= HOST_VFS_HANDLE_CAPACITY || !fixture_vfs_handles[index].used) {
        return ERR_INVALID;
    }
    if (whence == VFS_SEEK_SET) target = offset;
    else if (whence == VFS_SEEK_CUR) {
        target = (int64_t)fixture_vfs_handles[index].offset + offset;
    } else if (whence == VFS_SEEK_END) {
        target = (int64_t)fixture_vfs_handles[index].file.size + offset;
    } else return ERR_INVALID;
    if (target < 0 || (uint64_t)target > fixture_vfs_handles[index].file.size) {
        return ERR_INVALID;
    }
    fixture_vfs_handles[index].offset = (uint32_t)target;
    *position = (uint32_t)target;
    return OK;
}

int vfs_close(int32_t fd) {
    uint32_t index;

    if (fd < VFS_FD_FIRST_FILE) return ERR_INVALID;
    index = (uint32_t)(fd - VFS_FD_FIRST_FILE);
    if (index >= HOST_VFS_HANDLE_CAPACITY || !fixture_vfs_handles[index].used) {
        return ERR_INVALID;
    }
    fixture_vfs_handles[index].used = 0U;
    fixture_vfs_close_calls++;
    return OK;
}

int procfs_reset_controls(void) {
    copy_text(fixture_vfs_console_level,
              sizeof(fixture_vfs_console_level),
              "console_log_level info\n");
    copy_text(fixture_vfs_buffer_level,
              sizeof(fixture_vfs_buffer_level),
              "buffer_log_level debug\n");
    fixture_procfs_reset_calls++;
    return OK;
}

int usb_manager_get_status(usb_manager_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    if (fixture_usb_status_result != OK) return fixture_usb_status_result;
    *out_status = fixture_usb_status;
    return OK;
}

int usb_manager_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    if (fixture_usb_count_result != OK) return fixture_usb_count_result;
    *out_count = fixture_usb_controller_count;
    return OK;
}

int usb_manager_get_info(uint32_t index, usb_controller_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (fixture_usb_info_result != OK) return fixture_usb_info_result;
    if (index >= fixture_usb_controller_count) return ERR_INVALID;
    *out_info = fixture_usb_controllers[index];
    return OK;
}

int usb_manager_get_port_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    if (fixture_usb_port_count_result != OK) return fixture_usb_port_count_result;
    *out_count = fixture_usb_port_count;
    return OK;
}

int usb_manager_get_port(uint32_t index, usb_port_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (fixture_usb_port_result != OK) return fixture_usb_port_result;
    if (index >= fixture_usb_port_count) return ERR_INVALID;
    *out_info = fixture_usb_ports[index];
    return OK;
}

int usb_manager_get_device_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    if (fixture_usb_device_count_result != OK) {
        return fixture_usb_device_count_result;
    }
    *out_count = fixture_usb_device_count;
    return OK;
}

int usb_manager_get_device(uint32_t index, usb_device_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (fixture_usb_device_result != OK) return fixture_usb_device_result;
    if (index >= fixture_usb_device_count) return ERR_INVALID;
    *out_info = fixture_usb_devices[index];
    return OK;
}

int usb_manager_find(const char* id, usb_controller_info_t* out_info) {
    if (!id || !out_info) return ERR_NULL;
    if (fixture_usb_find_result != OK) return fixture_usb_find_result;
    if (kstrcmp(id, "usb-00:04.0") != 0) return ERR_NOT_FOUND;
    *out_info = fixture_usb_controllers[0];
    return OK;
}

int usb_manager_format_text(const usb_controller_info_t* info,
                            usb_controller_text_t* out_text) {
    if (!info || !out_text) return ERR_NULL;
    if (fixture_usb_format_result != OK) return fixture_usb_format_result;
    kmemset(out_text, 0, sizeof(*out_text));
    copy_text(out_text->id, sizeof(out_text->id), "usb-00:04.0");
    copy_text(out_text->name, sizeof(out_text->name), "EHCI Test");
    copy_text(out_text->location, sizeof(out_text->location), "00:04.0");
    copy_text(out_text->detail, sizeof(out_text->detail),
              "EHCI USB controller");
    return OK;
}

const char* usb_manager_model_name(usb_controller_model_t model) {
    if (model == USB_CONTROLLER_MODEL_UHCI) return "UHCI";
    if (model == USB_CONTROLLER_MODEL_EHCI) return "EHCI";
    return "OTHER";
}

const char* usb_manager_state_name(usb_controller_state_t state) {
    if (state == USB_CONTROLLER_READY) return "READY";
    if (state == USB_CONTROLLER_DEGRADED) return "DEGRADED";
    return "DISABLED";
}

const char* usb_manager_reason_name(usb_controller_reason_t reason) {
    if (reason == USB_CONTROLLER_REASON_DRIVER_READY) return "DRIVER_READY";
    if (reason == USB_CONTROLLER_REASON_DRIVER_FAILURE) return "DRIVER_FAILURE";
    if (reason == USB_CONTROLLER_REASON_PORT_FAILURE) return "PORT_FAILURE";
    if (reason == USB_CONTROLLER_REASON_OUT_OF_SCOPE) return "OUT_OF_SCOPE";
    return "DRIVER_NOT_INITIALIZED";
}

const char* usb_manager_port_state_name(usb_port_state_t state) {
    if (state == USB_PORT_EMPTY) return "EMPTY";
    if (state == USB_PORT_RESETTING) return "RESETTING";
    if (state == USB_PORT_ENUMERATING) return "ENUMERATING";
    if (state == USB_PORT_CONFIGURED) return "CONFIGURED";
    return "DEGRADED";
}

const char* usb_manager_port_reason_name(usb_port_reason_t reason) {
    if (reason == USB_PORT_REASON_NONE) return "NONE";
    if (reason == USB_PORT_REASON_NO_DEVICE) return "NO_DEVICE";
    if (reason == USB_PORT_REASON_RESET_TIMEOUT) return "RESET_TIMEOUT";
    if (reason == USB_PORT_REASON_CONTROL_TIMEOUT) return "CONTROL_TIMEOUT";
    if (reason == USB_PORT_REASON_INVALID_DESCRIPTOR) return "INVALID_DESCRIPTOR";
    if (reason == USB_PORT_REASON_UNSUPPORTED_SPEED) return "UNSUPPORTED_SPEED";
    if (reason == USB_PORT_REASON_UNSUPPORTED_LAYOUT) return "UNSUPPORTED_LAYOUT";
    return "DRIVER_FAILURE";
}

const char* usb_manager_speed_name(usb_device_speed_t speed) {
    if (speed == USB_DEVICE_SPEED_LOW) return "LOW";
    if (speed == USB_DEVICE_SPEED_FULL) return "FULL";
    return "HIGH";
}

int usb_msc_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    if (fixture_usb_msc_count_result != OK) return fixture_usb_msc_count_result;
    *out_count = fixture_usb_msc_count;
    return OK;
}

int usb_msc_get_at(uint32_t index, usb_msc_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (fixture_usb_msc_info_result != OK) return fixture_usb_msc_info_result;
    if (index >= fixture_usb_msc_count) return ERR_INVALID;
    *out_info = fixture_usb_msc[index];
    return OK;
}

const char* usb_msc_state_name(usb_msc_state_t state) {
    return state == USB_MSC_READY ? "READY" : "DEGRADED";
}

int usb_hid_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    if (fixture_usb_hid_count_result != OK) return fixture_usb_hid_count_result;
    *out_count = fixture_usb_hid_count;
    return OK;
}

int usb_hid_get_at(uint32_t index, usb_hid_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (fixture_usb_hid_info_result != OK) return fixture_usb_hid_info_result;
    if (index >= fixture_usb_hid_count) return ERR_INVALID;
    *out_info = fixture_usb_hid[index];
    return OK;
}

int usb_hid_validate_state(void) {
    return fixture_usb_hid_validate_result;
}

const char* usb_hid_kind_name(usb_hid_kind_t kind) {
    return kind == USB_HID_KIND_KEYBOARD ? "KEYBOARD" : "MOUSE";
}

const char* usb_hid_state_name(usb_hid_state_t state) {
    if (state == USB_HID_STATE_READY) return "READY";
    if (state == USB_HID_STATE_DEGRADED) return "DEGRADED";
    return "DISABLED";
}

int input_validate_state(void) {
    return fixture_input_validate_result;
}

int app_loader_is_ready(void) {
    return 1;
}

uint32_t app_loader_get_foreground_pid(void) {
    return 0U;
}

int app_api_is_ready(void) {
    return 1;
}

int app_api_get_version(app_api_version_t* version) {
    if (!version) return ERR_NULL;
    *version = fixture_app_api_version;
    return OK;
}

int app_api_file_is_ready(void) {
    return 1;
}

int app_api_ipc_is_ready(void) {
    return 1;
}

int syscall_is_ready(void) {
    return 1;
}

int syscall_user_mode_is_enabled(void) {
    return 1;
}

int idt_is_user_syscall_enabled(void) {
    return 1;
}

const char* shell_core_builtin_app_name(shell_builtin_app_t app) {
    if (app == SHELL_BUILTIN_APP_UPTIME) return "uptime";
    if (app == SHELL_BUILTIN_APP_MEM) return "mem";
    return "unknown";
}

process_t* process_get_current(void) {
    return &fixture_user_process;
}

uint32_t process_get_current_pid(void) {
    return fixture_user_process.pid;
}

uint32_t process_get_count(void) {
    return 1U;
}

uint32_t thread_get_count(void) {
    return 1U;
}

uint32_t process_get_focus(void) {
    return fixture_user_process.pid;
}

uint32_t process_get_user_fault_count(void) {
    return 0U;
}

int process_get_last_user_fault(process_user_fault_summary_t* summary) {
    if (!summary) return ERR_NULL;
    *summary = fixture_user_fault;
    return ERR_NOT_FOUND;
}

int input_get_metrics(input_metrics_t* metrics) {
    if (!metrics) return ERR_NULL;
    *metrics = fixture_input_metrics;
    return OK;
}

uint32_t memory_get_total(void) {
    return 1024U * 1024U;
}

uint32_t memory_get_total_pages(void) {
    return 256U;
}

int paging_is_ready(void) {
    return 1;
}

int vfs_is_ready(void) {
    return 1;
}

int vfs_validate_state(void) {
    return OK;
}

void kmem_cache_get_stats(kmem_slab_stats_t* stats) {
    if (!stats) return;
    kmemset(stats, 0, sizeof(*stats));
    stats->initialized = 1U;
    stats->valid = 1U;
    stats->caches = 1U;
    stats->slabs = 1U;
    stats->active_objects = 1U;
    stats->capacity = 8U;
}

uint32_t recovery_get_count(void) {
    return RECOVERY_COMPONENT_COUNT;
}

int recovery_is_available(recovery_component_id_t component) {
    const recovery_component_t* entry = recovery_get(component);
    return entry && entry->state == RECOVERY_STATE_READY;
}

int app_package_is_ready(void) {
    return 1;
}

uint8_t fs_get_type(void) {
    return FS_TYPE_FAT12;
}

int update_get_capabilities(update_capabilities_t* capabilities) {
    if (!capabilities) return ERR_NULL;
    *capabilities = fixture_update_capabilities;
    return OK;
}

int update_get_status(update_status_t* status) {
    if (!status) return ERR_NULL;
    *status = fixture_update_status;
    return OK;
}

int update_remote_get_status(update_remote_status_t* status) {
    if (!status) return ERR_NULL;
    *status = fixture_update_remote_status;
    return OK;
}

const char* update_remote_reason_name(update_remote_reason_t reason) {
    if (reason == UPDATE_REMOTE_REASON_NONE) return "NONE";
    return "NETWORK";
}

int app_catalog_get_status(app_catalog_status_t* status) {
    if (!status) return ERR_NULL;
    *status = fixture_app_catalog_status;
    return OK;
}

int process_signal_get_stats(process_signal_stats_t* stats) {
    if (!stats) return ERR_NULL;
    *stats = fixture_signal_stats;
    return OK;
}

int process_signal_validate_state(void) {
    return OK;
}

int block_cache_get_stats(block_cache_stats_t* stats) {
    if (!stats) return ERR_NULL;
    *stats = fixture_block_cache_stats;
    return OK;
}

int block_cache_get_durability_status(block_durability_status_t* status) {
    if (!status) return ERR_NULL;
    *status = fixture_block_durability;
    return OK;
}

int net_buffer_get_stats(net_buffer_stats_t* stats) {
    if (!stats) return ERR_NULL;
    *stats = fixture_net_buffer_stats;
    return OK;
}

int net_buffer_validate_state(void) {
    return OK;
}

int skb_get_stats(sk_buff_stats_t* stats) {
    if (!stats) return ERR_NULL;
    *stats = fixture_skb_stats;
    return OK;
}

int skb_validate_state(void) {
    return OK;
}

int socket_get_status(socket_status_t* status) {
    if (!status) return ERR_NULL;
    *status = fixture_socket_status;
    return OK;
}

int socket_validate_state(void) {
    return OK;
}

int socket_self_test(socket_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_socket_test;
    return OK;
}

int net_socket_get_status(net_socket_status_t* status) {
    if (!status) return ERR_NULL;
    *status = fixture_net_socket_status;
    return OK;
}

int wifi_manager_get_status(wifi_manager_status_t* status) {
    if (!status) return ERR_NULL;
    kmemset(status, 0, sizeof(*status));
    status->initialized = 1U;
    return OK;
}

int wifi_manager_validate_state(void) {
    return OK;
}

const recovery_component_t* recovery_get(recovery_component_id_t component) {
    if (component >= RECOVERY_COMPONENT_COUNT ||
        fixture_recovery_state_result != OK) return 0;
    return &fixture_recovery_components[component];
}

const char* recovery_state_name(recovery_state_t state) {
    if (state == RECOVERY_STATE_READY) return "READY";
    if (state == RECOVERY_STATE_DEGRADED) return "DEGRADED";
    if (state == RECOVERY_STATE_DISABLED) return "DISABLED";
    return "UNKNOWN";
}

int kmem_cache_get_info_at(uint32_t index, kmem_cache_info_t* info) {
    if (!info) return ERR_NULL;
    if (fixture_slab_info_result != OK) return fixture_slab_info_result;
    if (index >= fixture_slab_count) return ERR_INVALID;
    *info = fixture_slab_info[index];
    return OK;
}

int kmem_cache_self_test(void) {
    return fixture_slab_self_test_result;
}

int kmem_cache_validate(void) {
    return fixture_memcheck_slab_validate_result;
}

void* kmalloc(uint32_t size) {
    uint32_t index = fixture_memcheck_allocations;

    if (size > sizeof(fixture_memcheck_blocks[0]) || index >= 3U) {
        return 0;
    }
    fixture_memcheck_allocations++;
    return fixture_memcheck_blocks[index];
}

void kfree(void* ptr) {
    if (ptr) fixture_memcheck_frees++;
}

void memory_get_heap_stats(memory_heap_stats_t* stats) {
    if (stats) *stats = fixture_memcheck_heap_stats;
}

void memory_get_pmm_stats(memory_pmm_stats_t* stats) {
    if (stats) *stats = fixture_memcheck_pmm_stats;
}

int memory_get_detailed_stats(memory_detailed_stats_t* stats) {
    if (!stats) return ERR_NULL;
    *stats = fixture_memcheck_detailed_stats;
    return fixture_memcheck_detailed_result;
}

uint32_t memory_get_free_pages(void) {
    return fixture_memcheck_detailed_stats.zone_pages[MEMORY_ZONE_FREE];
}

uint32_t memory_get_used(void) {
    return fixture_memcheck_heap_stats.used_bytes;
}

uint32_t memory_get_free(void) {
    return fixture_memcheck_heap_stats.free_bytes;
}

void paging_get_user_stats(paging_user_stats_t* stats) {
    if (stats) *stats = fixture_memcheck_paging_stats;
}

int paging_get_boot_stats(paging_boot_stats_t* stats) {
    if (!stats) return ERR_NULL;
    if (fixture_paging_boot_result != OK) return fixture_paging_boot_result;
    *stats = fixture_paging_boot_stats;
    return OK;
}

int vesa_has_backbuffer(void) {
    return fixture_vesa_backbuffer;
}

vesa_mode_t* vesa_get_mode(void) {
    return &fixture_vesa_mode;
}

void vesa_get_metrics(vesa_metrics_t* metrics) {
    if (metrics) *metrics = fixture_vesa_metrics;
}

uint32_t process_get_user_count(void) {
    return fixture_process_user_count;
}

uint32_t process_get_state_count(process_state_t state) {
    return state == PROCESS_STATE_ZOMBIE ? fixture_process_zombie_count : 0U;
}

int app_loader_is_foreground_active(void) {
    return fixture_app_foreground_active;
}

void scheduler_get_stats(scheduler_stats_t* stats) {
    if (stats) *stats = fixture_scheduler_stats;
}

int scheduler_validate_invariants(scheduler_validation_t* validation) {
    if (!validation) return ERR_NULL;
    *validation = fixture_scheduler_validation;
    return fixture_scheduler_validation_result;
}

int process_vma_get_page_fault_stats(page_fault_stats_t* stats) {
    if (!stats) return ERR_NULL;
    if (fixture_page_fault_stats_result != OK) {
        return fixture_page_fault_stats_result;
    }
    *stats = fixture_page_fault_stats;
    return OK;
}

process_t* process_get_by_pid(uint32_t pid) {
    if (fixture_process_lookup_result != OK ||
        pid != fixture_user_process.pid) {
        return 0;
    }
    return &fixture_user_process;
}

int process_is_user(const process_t* proc) {
    return proc == &fixture_user_process && fixture_process_is_user_result;
}

int process_signal_send(uint32_t pid, uint32_t signal_number) {
    fixture_signal_send_pid = pid;
    fixture_signal_send_number = signal_number;
    return fixture_signal_send_result;
}

int process_signal_self_test(process_signal_self_test_t* out_test) {
    if (!out_test) return ERR_NULL;
    *out_test = fixture_signal_test;
    return fixture_signal_test_result;
}

const char* process_signal_name(uint32_t signal_number) {
    if (signal_number == APP_SIGNAL_INT) return "SIGINT";
    if (signal_number == APP_SIGNAL_KILL) return "SIGKILL";
    if (signal_number == APP_SIGNAL_SEGV) return "SIGSEGV";
    if (signal_number == APP_SIGNAL_TERM) return "SIGTERM";
    if (signal_number == APP_SIGNAL_CHLD) return "SIGCHLD";
    return "INVALID";
}

int process_vma_copy(const process_t* proc, vm_area_info_t* output,
                     uint32_t capacity, uint32_t* out_count) {
    if (!proc || !output || !out_count) return ERR_NULL;
    if (fixture_process_vma_copy_result != OK) {
        return fixture_process_vma_copy_result;
    }
    if (capacity < fixture_user_area_count) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < fixture_user_area_count; index++) {
        output[index] = fixture_user_areas[index];
    }
    *out_count = fixture_user_area_count;
    return OK;
}

int vfs_getcwd(char* path, uint32_t capacity) {
    if (!path || !capacity) return ERR_NULL;
    if (fixture_getcwd_result != OK) return fixture_getcwd_result;
    if (kstrlen(fixture_cwd) + 1U > capacity) return ERR_OVERFLOW;
    copy_text(path, capacity, fixture_cwd);
    return OK;
}

int vfs_chdir(const char* path) {
    if (!path) return ERR_NULL;
    fixture_chdir_calls++;
    copy_text(fixture_last_path, sizeof(fixture_last_path), path);
    return fixture_chdir_result;
}

int mouse_get_status(mouse_status_t* status) {
    if (!status) return ERR_NULL;
    if (fixture_mouse_status_result != OK) return fixture_mouse_status_result;
    *status = fixture_mouse_status;
    return OK;
}

int mouse_set_speed(uint8_t speed) {
    fixture_speed_calls++;
    if (fixture_mouse_speed_result == OK) fixture_mouse_status.config.speed = speed;
    return fixture_mouse_speed_result;
}

int mouse_set_primary_button(mouse_primary_button_t primary_button) {
    fixture_primary_calls++;
    if (fixture_mouse_primary_result == OK) {
        fixture_mouse_status.config.primary_button = primary_button;
    }
    return fixture_mouse_primary_result;
}

int mouse_set_acceleration(int enabled) {
    fixture_acceleration_calls++;
    if (fixture_mouse_acceleration_result == OK) {
        fixture_mouse_status.config.acceleration_enabled = enabled ? 1U : 0U;
    }
    return fixture_mouse_acceleration_result;
}

static int test_pwd(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_pwd("");
    failures += expect_text("/home/test\n");
    fixture_reset();
    shell_dispatch_cmd_pwd("extra");
    failures += expect_text("Uso: pwd\n");
    fixture_reset();
    fixture_getcwd_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_pwd(0);
    failures += expect_text("Erro: diretorio atual indisponivel.\n");
    return failures;
}

static int test_cd(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_cd("");
    if (fixture_chdir_calls != 1U || kstrcmp(fixture_last_path, "/") != 0) {
        fprintf(stderr, "diagnostics-host: cd padrao nao chamou raiz\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_cd("/var/log");
    if (fixture_chdir_calls != 1U ||
        kstrcmp(fixture_last_path, "/var/log") != 0 || video_output[0]) {
        fprintf(stderr, "diagnostics-host: cd valido inesperado\n");
        failures++;
    }
    fixture_reset();
    fixture_chdir_result = ERR_NOT_FOUND;
    shell_dispatch_cmd_cd("missing");
    failures += expect_text("Erro: cd recusado (codigo 4).\n");
    return failures;
}

static int test_mouse(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_mouse("");
    failures += expect_contains("Mouse PS/2:\n");
    failures += expect_contains("Posicao: 12,34\n");
    fixture_reset();
    fixture_mouse_status.initialized = 0U;
    fixture_mouse_status.config.acceleration_enabled = 1U;
    fixture_mouse_status.config.primary_button = MOUSE_PRIMARY_RIGHT;
    fixture_mouse_status.wheel_supported = 1U;
    shell_dispatch_cmd_mouse("speed 7");
    if (fixture_speed_calls != 1U || fixture_mouse_status.config.speed != 7U) {
        fprintf(stderr, "diagnostics-host: speed valido nao aplicado\n");
        failures++;
    }
    failures += expect_contains("Preferencia do mouse aplicada em RAM.\n");
    fixture_reset();
    shell_dispatch_cmd_mouse("primary right");
    if (fixture_primary_calls != 1U ||
        fixture_mouse_status.config.primary_button != MOUSE_PRIMARY_RIGHT) {
        fprintf(stderr, "diagnostics-host: primary right nao aplicado\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_mouse("primary left");
    if (fixture_primary_calls != 1U ||
        fixture_mouse_status.config.primary_button != MOUSE_PRIMARY_LEFT) {
        fprintf(stderr, "diagnostics-host: primary left nao aplicado\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_mouse("acceleration on");
    if (fixture_acceleration_calls != 1U ||
        !fixture_mouse_status.config.acceleration_enabled) {
        fprintf(stderr, "diagnostics-host: acceleration on nao aplicado\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_mouse("acceleration off");
    if (fixture_acceleration_calls != 1U ||
        fixture_mouse_status.config.acceleration_enabled) {
        fprintf(stderr, "diagnostics-host: acceleration off nao aplicado\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_mouse("speed 0");
    failures += expect_contains("preferencia invalida; estado preservado");
    fixture_reset();
    shell_dispatch_cmd_mouse("primary middle");
    failures += expect_contains("preferencia invalida; estado preservado");
    fixture_reset();
    shell_dispatch_cmd_mouse("unknown value");
    failures += expect_contains("preferencia invalida; estado preservado");
    fixture_reset();
    shell_dispatch_cmd_mouse("speed 4 extra");
    failures += expect_text("Uso: mouse | mouse speed <1-10> | mouse primary <left|right> | mouse acceleration <on|off>\n");
    fixture_reset();
    fixture_mouse_speed_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_mouse("speed 4");
    failures += expect_contains("driver de mouse indisponivel");
    fixture_reset();
    fixture_mouse_status_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_mouse(0);
    failures += expect_text("Erro: status do mouse indisponivel.\n");
    return failures;
}

static int test_log(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_log("status");
    failures += expect_contains("Log circular: 2/32 registros\n");
    failures += expect_contains("Niveis do log: console=info buffer=debug\n");
    fixture_reset();
    fixture_log_stats_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_log("");
    failures += expect_text("Erro: estatisticas do log indisponiveis.\n");
    fixture_reset();
    shell_dispatch_cmd_log("clear");
    if (fixture_log_clear_calls != 1U ||
        expect_text("Log circular limpo.\n")) failures++;
    fixture_reset();
    fixture_log_copy_count = 0U;
    shell_dispatch_cmd_log("tail");
    failures += expect_text("Log vazio.\n");
    fixture_reset();
    shell_dispatch_cmd_log("tail 2");
    failures += expect_contains("seq=10 ticks=100..110 [ERROR] [TEST] falha");
    failures += expect_contains("erro=-4");
    failures += expect_contains("seq=11 ticks=100..110 [DEBUG] [TEST] falha");
    fixture_reset();
    fixture_log_copy_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_log("tail 1");
    failures += expect_text("Erro: historico do log indisponivel.\n");
    fixture_reset();
    shell_dispatch_cmd_log("tail 33");
    failures += expect_contains("Uso: log [status|tail [1-16]|clear|level|check]");
    fixture_reset();
    shell_dispatch_cmd_log("level");
    failures += expect_contains("Niveis do log: console=info buffer=debug\n");
    fixture_reset();
    shell_dispatch_cmd_log("level console debug");
    if (fixture_log_console_calls != 1U ||
        fixture_log_console_level != LOG_LEVEL_DEBUG) {
        fprintf(stderr, "diagnostics-host: nivel console nao aplicado\n");
        failures++;
    }
    failures += expect_contains("Nivel console alterado para debug.\n");
    fixture_reset();
    fixture_log_set_buffer_result = ERR_INVALID;
    shell_dispatch_cmd_log("level buffer error");
    failures += expect_contains("Erro: o buffer deve ser tao detalhado quanto o console.\n");
    fixture_reset();
    shell_dispatch_cmd_log("level invalid info");
    failures += expect_contains("Uso: log [status|tail [1-16]|clear|level|check]");
    fixture_reset();
    shell_dispatch_cmd_log("check");
    failures += expect_contains("Resultado: OK (8 aprovados, 0 falhos)\n");
    fixture_reset();
    fixture_log_test_result = ERR_STATE;
    fixture_log_test.failed = 1U;
    shell_dispatch_cmd_log("check");
    failures += expect_contains("Resultado: ERRO (8 aprovados, 1 falhos)\n");
    fixture_reset();
    shell_dispatch_cmd_log("unknown");
    failures += expect_contains("Uso: log [status|tail [1-16]|clear|level|check]");
    return failures;
}

static int test_timer(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_timer("status");
    failures += expect_contains("Servico de timers: tick=1000 frequencia=100 Hz\n");
    failures += expect_contains("Proprietarios: 1/16  Timers: 1/32");
    failures += expect_contains("Callbacks: 8  Erros: 1  Atrasos: 2");
    fixture_reset();
    fixture_timer_stats_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_timer("");
    failures += expect_text("Erro: estatisticas de timer indisponiveis.\n");
    fixture_reset();
    fixture_timer_copy_count = 0U;
    shell_dispatch_cmd_timer("list");
    failures += expect_text("Nenhum timer criado.\n");
    fixture_reset();
    shell_dispatch_cmd_timer("list");
    failures += expect_contains("handle=0x00001234 owner=SHELL timer=heartbeat\n");
    failures += expect_contains("modo=periodico estado=armado prazo=1100 periodo=100");
    failures += expect_contains("execucoes=9 atrasos=1 perdidos=2 ultimo_atraso=3 erro=-3");
    fixture_reset();
    fixture_timer_copy_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_timer("list");
    failures += expect_text("Erro: lista de timers indisponivel.\n");
    fixture_reset();
    shell_dispatch_cmd_timer("check");
    failures += expect_contains("Autoteste de timers (tabelas privadas):\n");
    failures += expect_contains("Resultado: OK (11 aprovados, 0 falhos)\n");
    fixture_reset();
    fixture_timer_test_result = ERR_STATE;
    fixture_timer_test.failed = 1U;
    shell_dispatch_cmd_timer("check");
    failures += expect_contains("Resultado: ERRO (11 aprovados, 1 falhos)\n");
    fixture_reset();
    shell_dispatch_cmd_timer("invalid");
    failures += expect_text("Uso: timer [status|list|check]\n");
    return failures;
}

static int test_clock(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_clock("status");
    failures += expect_contains("Clock: inicializado=SIM fonte=RTC PIT=100 Hz UTC=READY wraps=2 reads=12 erro=0\n");
    failures += expect_contains("anchor_tick=100000 anchor_utc=1735689600\n");
    failures += expect_contains("monotono_ticks=123456789\n");
    failures += expect_contains("utc_unix_seconds=1735689600\n");
    failures += expect_contains("RTC=READY 2025-1-1 0:0:0\n");
    fixture_reset();
    fixture_clock_status.source = CLOCK_SOURCE_NONE;
    fixture_clock_status.utc_available = 0U;
    fixture_clock_ticks_result = ERR_UNAVAILABLE;
    fixture_clock_utc_result = ERR_UNAVAILABLE;
    fixture_rtc_status.valid = 0U;
    shell_dispatch_cmd_clock("");
    failures += expect_contains("fonte=nenhuma PIT=100 Hz UTC=UNAVAILABLE");
    failures += expect_contains("RTC=UNAVAILABLE\n");
    fixture_reset();
    fixture_clock_status_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_clock("status");
    failures += expect_text("Erro: estado do clock indisponivel.\n");
    fixture_reset();
    shell_dispatch_cmd_clock("check");
    failures += expect_contains("Autoteste RTC/clock:\n");
    failures += expect_contains("Resultado: OK\n");
    fixture_reset();
    fixture_rtc_test_result = ERR_STATE;
    fixture_clock_test.failed = 1U;
    shell_dispatch_cmd_clock("check");
    failures += expect_contains("Resultado: ERRO\n");
    fixture_reset();
    shell_dispatch_cmd_clock("invalid");
    failures += expect_text("Uso: clock [status|check]\n");
    return failures;
}

static int test_irqstat(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_irqstat("status");
    failures += expect_contains("Bottom-Half: fila=2/31 executando=1 pico=4\n");
    failures += expect_contains("Agendados=8 executados=7 coalescidos=2 reexecucoes=1\n");
    failures += expect_contains("Cancelados=3 rejeitados=1 contexto_invalido=2\n");
    fixture_reset();
    fixture_irq_status_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_irqstat("");
    failures += expect_text("Erro: fila Bottom-Half indisponivel.\n");
    fixture_reset();
    shell_dispatch_cmd_irqstat("list");
    failures += expect_contains("IRQs PIC ativas:\n  IRQ1 ocorrencias=9 handlers=2 BH=3/4 coalescidos=1 rejeitados=2\n");
    fixture_reset();
    fixture_irq_line_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_irqstat("list");
    failures += expect_text("IRQs PIC ativas:\nErro ao consultar linha IRQ.\n");
    fixture_reset();
    shell_dispatch_cmd_irqstat("check");
    failures += expect_contains("Autoteste de Bottom-Half (fila privada):\n");
    failures += expect_contains("  ciclo: OK\n  coalescencia: OK\n");
    failures += expect_contains("Resultado: OK\n");
    fixture_reset();
    fixture_irq_test_result = ERR_STATE;
    fixture_irq_test.invariants = 0U;
    fixture_irq_test.failed = 1U;
    shell_dispatch_cmd_irqstat("check");
    failures += expect_contains("  invariantes: ERRO\nResultado: ERRO\n");
    fixture_reset();
    fixture_irq_validate_result = ERR_STATE;
    shell_dispatch_cmd_irqstat("check");
    failures += expect_contains("Resultado: ERRO\n");
    fixture_reset();
    shell_dispatch_cmd_irqstat("invalid");
    failures += expect_text("Uso: irqstat [status|list|check]\n");
    return failures;
}

static int test_wait(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_wait("status");
    failures += expect_contains("Servico de espera: canais=1 waiters=1 pico=2\n");
    failures += expect_contains("Inicios=8 eventos=3 timeouts=2 cancelamentos=1 indisponiveis=1\n");
    failures += expect_contains("Operacoes invalidas=2 registro=1/128 pico=4 rejeicoes=1\n");
    failures += expect_contains("Wake um/todos=5/6 contexto/orfaos=1/2\n");
    fixture_reset();
    fixture_wait_stats_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_wait("");
    failures += expect_text("Erro: estatisticas de espera indisponiveis.\n");
    fixture_reset();
    fixture_waiter_count = 0U;
    shell_dispatch_cmd_wait("list");
    failures += expect_text("Nenhuma tarefa bloqueada por canal.\n");
    fixture_reset();
    shell_dispatch_cmd_wait("list");
    failures += expect_text("Tarefas bloqueadas por canal:\nthread=23 fila=7 pos=0 nome=worker canal=timer motivo=sinal restante=25\n");
    fixture_reset();
    fixture_waiters_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_wait("list");
    failures += expect_text("Erro: lista de esperas indisponivel.\n");
    fixture_reset();
    shell_dispatch_cmd_wait("check");
    failures += expect_contains("Autoteste de esperas (canal privado):\n");
    failures += expect_contains("  ciclo do canal: OK");
    failures += expect_contains("  wake all: OK");
    failures += expect_contains("Resultado: OK (14 aprovados, 0 falhos)\n");
    fixture_reset();
    fixture_wait_test_result = ERR_STATE;
    fixture_wait_test.invariants = 0U;
    fixture_wait_test.failed = 1U;
    shell_dispatch_cmd_wait("check");
    failures += expect_contains("  invariantes: ERRO\nResultado: ERRO (14 aprovados, 1 falhos)\n");
    fixture_reset();
    shell_dispatch_cmd_wait("invalid");
    failures += expect_text("Uso: wait [status|list|check]\n");
    return failures;
}

static int test_wqinfo(void) {
    int failures = 0;

    fixture_reset();
    fixture_waiter_count = 0U;
    shell_dispatch_cmd_wqinfo("");
    failures += expect_contains("Filas de espera registradas:\n  #7 timer estado=DISPONIVEL geracao=12 waiters/pico=1/2\n");
    failures += expect_contains("Nenhum waiter bloqueado.\n");
    fixture_reset();
    shell_dispatch_cmd_wqinfo("");
    failures += expect_contains("Ordem FIFO dos waiters:\nthread=23 fila=7 pos=0 nome=worker canal=timer motivo=sinal restante=25\n");
    fixture_reset();
    fixture_wait_info_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_wqinfo("");
    failures += expect_text("Erro: filas de espera indisponiveis.\n");
    fixture_reset();
    shell_dispatch_cmd_wqinfo("extra");
    failures += expect_text("Uso: wqinfo\n");
    return failures;
}

static int test_workq(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_workq("status");
    failures += expect_contains("Workqueue: contexto=kworker worker_pid=42 ativo=1 fallback=0\n");
    failures += expect_contains("Registro=1/64 prontos H/N=1/2 atrasados=1 executando=1 pico=4\n");
    failures += expect_contains("Agendados/executados=8/4 coalescidos=2 reexecucoes=1 cancelados=1\n");
    failures += expect_contains("Duracao ticks media/max=5/8 sub-tick=N/D\n");
    fixture_reset();
    fixture_workq_stats_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_workq("");
    failures += expect_text("Erro: workqueue indisponivel.\n");
    fixture_reset();
    fixture_work_info_count = 0U;
    shell_dispatch_cmd_workq("list");
    failures += expect_text("Trabalhos registrados:\n");
    fixture_reset();
    shell_dispatch_cmd_workq("list");
    failures += expect_contains("#16777221 shell geracao=1 prioridade=alta estado=executando restante=10 prazo=300 exec/agend=4/5 coalesc=1 erro=4294967288\n");
    failures += expect_contains("reexec/cancel=2/1 ticks total/max=20/8\n");
    fixture_reset();
    fixture_workq_info_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_workq("list");
    failures += expect_text("Erro: snapshot da workqueue indisponivel.\n");
    fixture_reset();
    shell_dispatch_cmd_workq("check");
    failures += expect_contains("Autoteste da workqueue (fixture privada):\n");
    failures += expect_contains("  ciclo: OK");
    failures += expect_contains("  interrupcoes habilitadas: OK");
    failures += expect_contains("Resultado: OK\n");
    fixture_reset();
    fixture_workq_test_result = ERR_STATE;
    fixture_workq_test.invariants = 0U;
    fixture_workq_test.failed = 1U;
    shell_dispatch_cmd_workq("check");
    failures += expect_contains("  invariantes: ERRO\nResultado: ERRO\n");
    fixture_reset();
    fixture_workq_validate_result = ERR_STATE;
    shell_dispatch_cmd_workq("check");
    failures += expect_contains("Resultado: ERRO\n");
    fixture_reset();
    fixture_workq_probe_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_workq("check");
    failures += expect_contains("wake Shell/kworker sem perda: ERRO");
    failures += expect_contains("Resultado: ERRO");
    fixture_reset();
    shell_dispatch_cmd_workq("invalid");
    failures += expect_text("Uso: workq [status|list|check]\n");
    return failures;
}

static int test_tls(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_tls("status");
    failures += expect_contains("TLS: estado=READY tempo=TRUSTED capability=HTTPS_READY entropy=RDRAND x509=READY\n");
    failures += expect_contains("min_version=0x0304 CA=REQUIRED SAN=REQUIRED validity=REQUIRED pin=OPTIONAL");
    failures += expect_contains("trust=current/next=4/5 revoked<3 fallback_http=FORBIDDEN");
    failures += expect_contains("handshake=AVAILABLE x509=AVAILABLE checks=10 rejects=2 last_reason=NONE");
    fixture_reset();
    fixture_tls_status.state = TLS_STATE_UNAVAILABLE;
    fixture_tls_status.trusted_time_available = 0U;
    fixture_tls_status.entropy_available = 0U;
    fixture_tls_status.certificate_validation_available = 0U;
    fixture_tls_status.handshake_available = 0U;
    fixture_tls_status.x509_available = 0U;
    fixture_tls_status.last_reason = TLS_REASON_TIME_UNAVAILABLE;
    fixture_tls_capability_result = 0;
    shell_dispatch_cmd_tls("");
    failures += expect_contains("TLS: estado=UNAVAILABLE tempo=UNAVAILABLE capability=HTTPS_UNAVAILABLE entropy=UNAVAILABLE x509=UNAVAILABLE\n");
    failures += expect_contains("handshake=DISABLED x509=DEFERRED checks=10 rejects=2 last_reason=TIME_UNAVAILABLE");
    fixture_reset();
    fixture_tls_policy_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_tls("status");
    failures += expect_text("Erro: politica TLS indisponivel.\n");
    fixture_reset();
    fixture_tls_status_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_tls("status");
    failures += expect_text("Erro: politica TLS indisponivel.\n");
    fixture_reset();
    shell_dispatch_cmd_tls("check");
    failures += expect_contains("Autoteste da politica TLS:\n");
    failures += expect_contains("  identidade valida: OK\n");
    failures += expect_contains("  invariantes: OK\nResultado: OK\n");
    fixture_reset();
    fixture_tls_test_result = ERR_STATE;
    fixture_tls_test.invariants = 0U;
    fixture_tls_test.failed = 1U;
    shell_dispatch_cmd_tls("check");
    failures += expect_contains("  invariantes: ERRO");
    failures += expect_contains("Resultado: ERRO");
    fixture_reset();
    shell_dispatch_cmd_tls("invalid");
    failures += expect_text("Uso: tls [status|check]\n");
    return failures;
}

static int test_vfs(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_vfs("status");
    failures += expect_contains("VFS:\n  Estado: READY\n");
    failures += expect_contains("Arquivos globais: 3/32  processos: 2  descritores: 4\n");
    failures += expect_contains("open/read/write/seek/close/falhas: 10/11/12/13/14/1\n");
    failures += expect_contains("montagens: 2/7  lookups/chdir: 15/2  ioctl: 3\n");
    failures += expect_contains("Descritores do processo atual:");
    failures += expect_contains("fd=3 tipo=FILE modo=1 offset=7 path=/etc/config");
    fixture_reset();
    fixture_vfs_status.initialized = 0U;
    shell_dispatch_cmd_vfs("");
    failures += expect_contains("Estado: DISABLED\n");
    fixture_reset();
    fixture_vfs_descriptor_count = 0U;
    shell_dispatch_cmd_vfs("status");
    failures += expect_contains("Descritores do processo atual:\n");
    fixture_reset();
    fixture_vfs_status_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_vfs("status");
    failures += expect_text("Erro: estado VFS indisponivel.\n");
    fixture_reset();
    fixture_vfs_descriptor_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_vfs("status");
    failures += expect_text("Erro: estado VFS indisponivel.\n");
    fixture_reset();
    shell_dispatch_cmd_vfs("test");
    failures += expect_contains("VFS Test: OK\n  Casos aprovados: 16/16\n");
    failures += expect_contains("  Pipes: OK\n  Procfs: OK\n  Sysfs: OK\n");
    fixture_reset();
    fixture_vfs_test_result = ERR_STATE;
    fixture_vfs_test.pipes = 0U;
    fixture_vfs_test.total = 17U;
    shell_dispatch_cmd_vfs("test");
    failures += expect_contains("VFS Test: ERRO\n  Casos aprovados: 16/17\n");
    failures += expect_contains("  Pipes: ERRO\n");
    fixture_reset();
    shell_dispatch_cmd_vfs("invalid");
    failures += expect_text("Uso: vfs status|test\n");
    fixture_reset();
    shell_dispatch_cmd_mount("");
    failures += expect_text("Montagens VFS:\n  / -> ata0 tipo=FAT32 acesso=RO geracao=4 refs=3\n");
    fixture_reset();
    fixture_vfs_mount_count = 0U;
    shell_dispatch_cmd_mount("");
    failures += expect_text("Montagens VFS:\n");
    fixture_reset();
    fixture_vfs_mount_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_mount("");
    failures += expect_text("Erro: montagens VFS indisponiveis.\n");
    fixture_reset();
    shell_dispatch_cmd_mount("extra");
    failures += expect_text("Uso: mount\n");
    return failures;
}

static int test_devcheck(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_devcheck("");
    failures += expect_contains("DevCheck: recusas de permissao abaixo sao esperadas.\n");
    failures += expect_contains("DevCheck: OK\n  Casos aprovados: 9/9\n");
    fixture_reset();
    fixture_devfs_test_result = ERR_STATE;
    fixture_devfs_test.invariants = 0U;
    fixture_devfs_test.passed = 8U;
    shell_dispatch_cmd_devcheck("");
    failures += expect_contains("DevCheck: ERRO\n  Casos aprovados: 8/9\n");
    fixture_reset();
    shell_dispatch_cmd_devcheck("extra");
    failures += expect_text("Uso: devcheck\n");
    return failures;
}

static int test_devices_and_usb(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_devices("-v");
    failures += expect_contains("Dispositivos detectados:\n");
    failures += expect_contains("pci-00:03.0");
    failures += expect_contains("ata0");
    failures += expect_contains("2048 setores");
    fixture_reset();
    shell_dispatch_cmd_devices("extra");
    failures += expect_text("Uso: devices [-v]\n");
    fixture_reset();
    fixture_device_count_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_devices("");
    failures += expect_text("Erro: inventario de dispositivos indisponivel.\n");
    fixture_reset();
    shell_dispatch_cmd_device_info("pci-00:03.0");
    failures += expect_contains("Dispositivo:\n  ID: pci-00:03.0\n");
    failures += expect_contains("Estado: READY");
    fixture_reset();
    shell_dispatch_cmd_device_info("missing");
    failures += expect_text("Erro: dispositivo nao encontrado.\n");
    fixture_reset();
    shell_dispatch_cmd_device_info("");
    failures += expect_text("Uso: device-info <id>\n");

    fixture_reset();
    fixture_vfs_sysfs_enabled = 1U;
    shell_dispatch_cmd_devices("-v");
    failures += expect_contains("Dispositivos detectados:\n");
    failures += expect_contains("sysfs=OK");
    failures += expect_contains("    sysfs:\n");
    failures += expect_contains("vendor 0x1234\n");
    failures += expect_contains("device 0x5678\n");
    failures += expect_contains("class network\n");
    if (fixture_vfs_open_calls != fixture_vfs_close_calls) {
        fprintf(stderr, "diagnostics-host: devices deixou arquivo sysfs aberto\n");
        failures++;
    }

    fixture_reset();
    fixture_vfs_sysfs_enabled = 1U;
    shell_dispatch_cmd_device_info("pci-00:03.0");
    failures += expect_contains("Dispositivo (sysfs):\n");
    failures += expect_contains("Caminho: /sys/bus/pci/devices/00:03.0\n");
    failures += expect_contains("Atributos:\n");
    failures += expect_contains("vendor 0x1234\n");
    failures += expect_contains("device 0x5678\n");
    failures += expect_contains("class network\n");
    if (fixture_vfs_open_calls != fixture_vfs_close_calls) {
        fprintf(stderr, "diagnostics-host: device-info deixou arquivo sysfs aberto\n");
        failures++;
    }

    fixture_reset();
    shell_dispatch_cmd_usb("status");
    failures += expect_contains("USB:\n  Servico: READY");
    failures += expect_contains("Controladores: 1");
    failures += expect_contains("High-speed: READY");
    fixture_reset();
    shell_dispatch_cmd_usb("list");
    failures += expect_contains("Controladores USB detectados:\n");
    failures += expect_contains("usb-00:04.0");
    failures += expect_contains("EHCI USB controller");
    fixture_reset();
    shell_dispatch_cmd_usb("ports");
    failures += expect_contains("Portas USB:\n");
    failures += expect_contains("usb-00:04.0 p1  CONFIGURED");
    fixture_reset();
    shell_dispatch_cmd_usb("devices");
    failures += expect_contains("Dispositivos USB configurados:\n");
    failures += expect_contains("ID: usb-device-1");
    failures += expect_contains("DeviceDesc: OK");
    failures += expect_contains("MSC e HID ativos");
    fixture_reset();
    shell_dispatch_cmd_usb("storage");
    failures += expect_contains("USB Mass Storage: 1 dispositivo(s)\n");
    failures += expect_contains("ID: usb-device-1");
    failures += expect_contains("Estado: READY");
    fixture_reset();
    shell_dispatch_cmd_usb("hid status");
    failures += expect_contains("USB HID Boot: 1 registro(s)\n");
    failures += expect_contains("KEYBOARD  Estado: READY");
    fixture_reset();
    shell_dispatch_cmd_usb("hid check");
    failures += expect_text("USB HID check: OK\n");
    fixture_reset();
    shell_dispatch_cmd_usb("device usb-00:04.0");
    failures += expect_contains("Controlador USB:\n  ID: usb-00:04.0");
    failures += expect_contains("Modelo: EHCI");
    fixture_reset();
    shell_dispatch_cmd_usb("device missing");
    failures += expect_text("Erro: controlador USB nao encontrado.\n");
    fixture_reset();
    shell_dispatch_cmd_usb("invalid");
    failures += expect_contains("Uso: usb status|list|ports|devices|storage|hid|device <id>\n");
    fixture_reset();
    fixture_usb_status_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_usb("status");
    failures += expect_text("Erro: inventario USB indisponivel.\n");
    fixture_reset();
    fixture_usb_count_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_usb("list");
    failures += expect_text("Erro: inventario USB indisponivel.\n");
    fixture_reset();
    fixture_usb_port_count_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_usb("ports");
    failures += expect_text("Erro: portas USB indisponiveis.\n");
    fixture_reset();
    fixture_usb_device_count_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_usb("devices");
    failures += expect_text("Erro: dispositivos USB indisponiveis.\n");
    fixture_reset();
    fixture_usb_msc_count_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_usb("storage");
    failures += expect_text("Erro: inventario USB MSC indisponivel.\n");
    fixture_reset();
    fixture_usb_hid_count_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_usb("hid");
    failures += expect_text("Erro: inventario USB HID indisponivel.\n");
    fixture_reset();
    fixture_usb_hid_validate_result = ERR_STATE;
    shell_dispatch_cmd_usb("hid check");
    failures += expect_contains("USB HID check: FALHOU hid=");
    return failures;
}

static int test_device_scan(void) {
    int failures = 0;
    shell_device_scan_result_t scan;

    fixture_reset();
    if (shell_diagnostics_run_device_scan(&scan) != OK ||
        scan.pci_result != OK || scan.devices_result != OK ||
        scan.usb_result != OK || scan.storage_result != OK ||
        scan.network_result != OK || scan.wifi_result != OK ||
        fixture_yield_calls != 6U ||
        fixture_recovery_last_component != RECOVERY_COMPONENT_DEVICES ||
        fixture_recovery_last_state != RECOVERY_STATE_READY) {
        fprintf(stderr, "diagnostics-host: device-scan valido inesperado\n");
        failures++;
    }
    if (shell_diagnostics_run_device_scan(0) != ERR_NULL) {
        fprintf(stderr, "diagnostics-host: device-scan aceitou resultado nulo\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_device_scan("");
    failures += expect_text("Varredura PCI concluida; inventario atualizado.\n");

    fixture_reset();
    fixture_scan_usb_refresh_result = ERR_STATE;
    fixture_scan_wifi_refresh_result = ERR_STATE;
    if (shell_diagnostics_run_device_scan(&scan) != OK ||
        scan.usb_result != OK || scan.wifi_result != OK) {
        fprintf(stderr, "diagnostics-host: device-scan nao recuperou managers\n");
        failures++;
    }

    fixture_reset();
    fixture_scan_pci_result = ERR_OVERFLOW;
    fixture_scan_devices_result = ERR_OVERFLOW;
    if (shell_diagnostics_run_device_scan(&scan) != ERR_OVERFLOW ||
        fixture_recovery_last_state != RECOVERY_STATE_DEGRADED) {
        fprintf(stderr, "diagnostics-host: device-scan parcial nao publicado\n");
        failures++;
    }
    fixture_reset();
    fixture_scan_pci_result = ERR_OVERFLOW;
    fixture_scan_devices_result = ERR_OVERFLOW;
    shell_dispatch_cmd_device_scan("");
    failures += expect_text("Varredura PCI/USB parcial; inventario atualizado.\n");

    fixture_reset();
    fixture_scan_network_result = ERR_UNAVAILABLE;
    fixture_scan_wifi_refresh_result = ERR_UNAVAILABLE;
    fixture_scan_storage_result = ERR_DISK;
    shell_dispatch_cmd_device_scan("");
    failures += expect_contains("Aviso: inventario de rede indisponivel.\n");
    failures += expect_contains("Aviso: inventario Wi-Fi indisponivel.\n");
    failures += expect_contains("Aviso: inventario de storage degradado.\n");
    failures += expect_contains("Varredura PCI concluida; inventario atualizado.\n");

    fixture_reset();
    fixture_scan_pci_result = ERR_DISK;
    shell_dispatch_cmd_device_scan("");
    failures += expect_text("Erro: varredura PCI indisponivel.\n");
    if (fixture_recovery_last_state != RECOVERY_STATE_DISABLED) {
        fprintf(stderr, "diagnostics-host: falha PCI nao desabilitou Devices\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_device_scan("extra");
    failures += expect_text("Uso: device-scan\n");
    return failures;
}

static int test_power(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_power("status");
    failures += expect_contains("Energia:\n  ACPI: DETECTADO");
    failures += expect_contains("Tabelas de energia ACPI: VALIDADA");
    failures += expect_contains("Snapshot ACPI: COMPLETO");
    failures += expect_contains("Modo ACPI: HABILITADO");
    failures += expect_contains("Servico: PRONTO");
    failures += expect_contains("Fase: IDLE");
    failures += expect_contains("Notificadores: 3/3");
    failures += expect_contains("Quiescencia: READY");
    fixture_reset();
    fixture_power_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_power("status");
    failures += expect_text("Erro: diagnostico de energia indisponivel.\n");
    fixture_reset();
    fixture_power_status.acpi_available = 0U;
    fixture_power_status.acpi_power_tables_available = 0U;
    fixture_power_status.acpi_mode_known = 0U;
    fixture_power_status.hardware_poweroff = POWER_CAPABILITY_UNAVAILABLE;
    fixture_power_status.reboot = POWER_CAPABILITY_UNAVAILABLE;
    fixture_power_status.service_state = POWER_SERVICE_UNAVAILABLE;
    fixture_power_status.transaction_phase = POWER_TRANSACTION_TERMINAL;
    fixture_power_status.transaction_target = POWER_TRANSACTION_TARGET_SHUTDOWN;
    fixture_power_status.quiescence_state = POWER_QUIESCENCE_DEGRADED;
    fixture_power_status.commit_started = 1U;
    fixture_power_status.transaction_degraded = 1U;
    shell_dispatch_cmd_power("status");
    failures += expect_contains("ACPI: INDISPONIVEL");
    failures += expect_contains("Modo ACPI: DESCONHECIDO");
    failures += expect_contains("Servico: INDISPONIVEL");
    failures += expect_contains("Fase: TERMINAL");
    failures += expect_contains("Alvo da transacao: SHUTDOWN");
    failures += expect_contains("Transacao degradada: SIM");
    fixture_reset();
    shell_dispatch_cmd_power("");
    failures += expect_text("Uso: power status\n");
    return failures;
}

static int test_acpi(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_acpi("status");
    failures += expect_contains("ACPI:\n  Estado: PRONTO");
    failures += expect_contains("OEM: ZEPHYR revisao=2");
    failures += expect_contains("FADT: VALIDA em 0x00101000");
    failures += expect_contains("MADT: VALIDA em 0x00104000");
    failures += expect_contains("FADT energia: PRESENTE");
    failures += expect_contains("Modo ACPI: HABILITADO");
    failures += expect_contains("_S5_: DECLARADO tipo_a=5 tipo_b=5");
    fixture_reset();
    fixture_acpi_power_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_acpi("status");
    failures += expect_contains("Energia ACPI: INDISPONIVEL");
    fixture_reset();
    fixture_acpi_status.available = 0U;
    fixture_acpi_status.partial = 1U;
    shell_dispatch_cmd_acpi("status");
    failures += expect_contains("ACPI:\n  Estado: INDISPONIVEL");
    failures += expect_contains("RSDP: nao encontrado ou invalido");
    fixture_reset();
    fixture_acpi_status_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_acpi("status");
    failures += expect_text("Erro: diagnostico ACPI indisponivel.\n");

    fixture_reset();
    shell_dispatch_cmd_acpi("tables");
    failures += expect_contains("ACPI tables:\n");
    failures += expect_contains("[0] FADT addr=0x00101000 length=116 revision=6 checksum=OK");
    failures += expect_contains("[1] MADT addr=0x00104000 length=64 revision=5 checksum=OK");
    failures += expect_contains("MADT addr=0x00104000 entries=3 processors=1 local_apic=1 io_apic=1 skipped=0");
    fixture_reset();
    fixture_acpi_status.available = 0U;
    shell_dispatch_cmd_acpi("tables");
    failures += expect_contains("Estado: INDISPONIVEL");
    failures += expect_contains("RSDP: nao encontrada");
    fixture_reset();
    fixture_acpi_table_count_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_acpi("tables");
    failures += expect_contains("Falha ao consultar inventario");
    fixture_reset();
    fixture_acpi_table_result = ERR_STATE;
    shell_dispatch_cmd_acpi("tables");
    failures += expect_contains("Falha ao copiar tabela");
    fixture_reset();
    fixture_acpi_madt_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_acpi("tables");
    failures += expect_contains("MADT: consulta indisponivel");
    fixture_reset();
    shell_dispatch_cmd_acpi("invalid");
    failures += expect_text("Uso: acpi status|tables\n");
    return failures;
}

static int test_slab(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_slabinfo("");
    failures += expect_contains("Caches SLAB:\n");
    failures += expect_contains("  shell-test obj=64 alinh=8 ativos=2/8 slabs=1 paginas=1 falhas=0\n");
    fixture_reset();
    fixture_slab_info[0].initialized = 0U;
    shell_dispatch_cmd_slabinfo("");
    failures += expect_text("Caches SLAB:\n");
    fixture_reset();
    fixture_slab_count = 0U;
    shell_dispatch_cmd_slabinfo("");
    failures += expect_text("Caches SLAB:\n");
    fixture_reset();
    shell_dispatch_cmd_slabinfo("extra");
    failures += expect_text("Uso: slabinfo\n");
    fixture_reset();
    shell_dispatch_cmd_slabtest("");
    failures += expect_text("SLABTest: OK\n");
    fixture_reset();
    fixture_slab_self_test_result = ERR_STATE;
    shell_dispatch_cmd_slabtest("");
    failures += expect_text("SLABTest: ERRO\n");
    fixture_reset();
    shell_dispatch_cmd_slabtest("extra");
    failures += expect_text("Uso: slabtest\n");
    return failures;
}

static int test_cpu_usage(void) {
    int failures = 0;

    fixture_reset();
    shell_diagnostics_reset();
    shell_dispatch_cmd_cpu_usage("usage reset");
    failures += expect_text("Linha-base de CPU capturada.\n");
    fixture_scheduler_stats.active_ticks = 100U;
    fixture_scheduler_stats.idle_ticks = 30U;
    output_reset();
    shell_dispatch_cmd_cpu_usage("usage");
    failures += expect_contains("CPU usage (PIT):\n");
    failures += expect_contains("ticks_ativos=20 ticks_idle=10 ticks_total=30\n");
    failures += expect_contains("percentual_ativo=66% percentual_idle=33%\n");
    fixture_reset();
    shell_dispatch_cmd_cpu_usage("");
    failures += expect_text("Uso: cpu usage [reset]\n");
    fixture_reset();
    shell_dispatch_cmd_cpu_usage("other");
    failures += expect_text("Uso: cpu usage [reset]\n");
    return failures;
}

static int test_pagefault_and_vmamap(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_pagefault("status");
    failures += expect_text("PageFault: tratadas=7 invalidas=2\n");
    fixture_reset();
    shell_dispatch_cmd_pagefault("");
    failures += expect_text("Uso: pagefault status\n");
    fixture_reset();
    fixture_page_fault_stats_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_pagefault("status");
    failures += expect_text("PageFault indisponivel.\n");

    fixture_reset();
    shell_dispatch_cmd_vmamap("42");
    failures += expect_contains("VMAMap PID=42 (user-fixture):\n");
    failures += expect_contains("  CODE R-X 0x00800000-0x00801000 paginas=1\n");
    failures += expect_contains("  STACK RW- 0x00C00000-0x00C01000 paginas=1\n");
    failures += expect_contains("  total=2 VMA(s)\n");
    fixture_reset();
    shell_dispatch_cmd_vmamap("");
    failures += expect_text("Uso: vmamap <pid>\n");
    fixture_reset();
    shell_dispatch_cmd_vmamap("43");
    failures += expect_text("Erro: PID nao encontrado.\n");
    fixture_reset();
    fixture_process_is_user_result = 0;
    shell_dispatch_cmd_vmamap("42");
    failures += expect_text("Erro: processo sem mapa ring 3.\n");
    fixture_reset();
    fixture_process_vma_copy_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_vmamap("42");
    failures += expect_text("Erro: mapa virtual indisponivel.\n");
    return failures;
}

static int test_schedcheck(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_schedcheck("");
    failures += expect_text("SchedCheck:\n  estado_atual OK\n  idle OK\n  tabela_pid OK\n  estados OK\n  tabela_slab OK\n  stacks OK\n  contabilidade_idle OK\n  resultado OK\n");
    fixture_reset();
    shell_dispatch_cmd_schedcheck("extra");
    failures += expect_text("Uso: schedcheck\n");
    fixture_reset();
    fixture_scheduler_validation.current_valid = 0U;
    fixture_scheduler_validation_result = ERR_STATE;
    shell_dispatch_cmd_schedcheck("");
    failures += expect_contains("  estado_atual ERRO\n");
    failures += expect_contains("  resultado ERRO\n");
    return failures;
}

static int test_memcheck(void) {
    int failures = 0;
    shell_memcheck_result_t result;

    fixture_reset();
    shell_dispatch_cmd_memcheck("");
    failures += expect_contains("MemCheck:\n");
    failures += expect_contains("  heap_integridade OK\n");
    failures += expect_contains("  coalescencia OK\n");
    failures += expect_contains("  pmm_guardas OK\n");
    failures += expect_contains("  diretorios_user OK\n");
    failures += expect_contains("  slab_integridade OK\n");
    failures += expect_contains("  memoria_detalhada OK\n");
    failures += expect_contains("  resultado OK\n");
    if (fixture_memcheck_allocations != 3U ||
        fixture_memcheck_frees != 3U) {
        fprintf(stderr, "diagnostics-host: memcheck nao restaurou blocos\n");
        failures++;
    }

    fixture_reset();
    shell_dispatch_cmd_memcheck("extra");
    failures += expect_text("Uso: memcheck\n");
    fixture_reset();
    fixture_process_user_count = 1U;
    shell_dispatch_cmd_memcheck("");
    failures += expect_text("MemCheck indisponivel: processo ring 3 ou zumbi pendente.\n");
    fixture_reset();
    fixture_app_foreground_active = 1;
    shell_dispatch_cmd_memcheck("");
    failures += expect_text("MemCheck indisponivel: processo ring 3 ou zumbi pendente.\n");

    fixture_reset();
    fixture_memcheck_slab_validate_result = ERR_STATE;
    result.heap_integrity = 0;
    if (shell_diagnostics_run_memcheck(&result) != ERR_STATE ||
        result.heap_integrity != 1 || result.coalescence != 1 ||
        result.pmm_guards != 1 || result.user_directories != 1 ||
        result.slab_integrity != 0 || result.memory_metrics != 1) {
        fprintf(stderr, "diagnostics-host: memcheck nao propagou falha SLAB\n");
        failures++;
    }
    if (fixture_memcheck_allocations != 3U ||
        fixture_memcheck_frees != 3U) {
        fprintf(stderr, "diagnostics-host: memcheck falho nao limpou blocos\n");
        failures++;
    }
    if (shell_diagnostics_run_memcheck(0) != ERR_NULL) {
        fprintf(stderr, "diagnostics-host: memcheck aceitou resultado nulo\n");
        failures++;
    }
    return failures;
}

static int test_kmetrics(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_kmetrics("");
    failures += expect_contains("Metricas K1 (desde boot):\n");
    failures += expect_contains("  PIT: ticks=1000 frequencia=100 Hz\n");
    failures += expect_contains("  Scheduler: trocas=10");
    failures += expect_contains("  Filas: teclado=2/32");
    failures += expect_contains("  Memoria PMM: usada=1 KB livre=3 KB paginas_livres=50 proprias=50");
    failures += expect_contains("  Paging boot: paginas=12 tabelas=3 ticks=9 modo=blocos\n");
    failures += expect_contains("  Heap: usado=1 KB livre=3 KB total=4 KB");
    failures += expect_contains("  VESA: N/D\n");

    fixture_reset();
    shell_dispatch_cmd_kmetrics("invalid");
    failures += expect_text("Uso: kmetrics [reset]\n");

    fixture_reset();
    shell_dispatch_cmd_kmetrics("reset");
    failures += expect_text("Linha-base K1 capturada.\n");
    fixture_ticks = 1020U;
    fixture_keyboard_metrics.queued = 4U;
    fixture_keyboard_metrics.dropped = 5U;
    fixture_keyboard_metrics.processed = 7U;
    fixture_ipc_pending_count = 3U;
    fixture_ipc_stats.sent = 9U;
    fixture_ipc_stats.received = 10U;
    fixture_ipc_stats.failed = 2U;
    fixture_ipc_stats.queue_full = 4U;
    fixture_scheduler_stats.context_switches = 15U;
    fixture_scheduler_stats.cooperative_yields = 8U;
    fixture_scheduler_stats.user_preemptions = 6U;
    fixture_scheduler_stats.idle_fallbacks = 4U;
    fixture_scheduler_stats.idle_ticks = 25U;
    fixture_scheduler_stats.active_ticks = 95U;
    fixture_memcheck_pmm_stats.allocation_failures = 3U;
    fixture_memcheck_pmm_stats.invalid_frees = 2U;
    fixture_memcheck_heap_stats.allocation_failures = 4U;
    fixture_memcheck_heap_stats.invalid_frees = 1U;
    fixture_memcheck_heap_stats.double_frees = 2U;
    fixture_memcheck_paging_stats.directories_created = 5U;
    fixture_memcheck_paging_stats.directories_released = 2U;
    fixture_memcheck_paging_stats.rejected_releases = 1U;
    fixture_vesa_backbuffer = 1;
    fixture_vesa_metrics.presentations = 5U;
    fixture_vesa_metrics.full_presentations = 3U;
    fixture_vesa_metrics.partial_presentations = 2U;
    fixture_vesa_metrics.bytes_copied = 8192U;
    fixture_vesa_metrics.last_copy_bytes = 2048U;
    fixture_vesa_metrics.last_copy_ticks = 6U;
    fixture_vesa_metrics.max_copy_ticks = 12U;
    output_reset();
    shell_dispatch_cmd_kmetrics("");
    failures += expect_contains("Metricas K1 (desde reset):\n");
    failures += expect_contains("  PIT: ticks=20 frequencia=100 Hz\n");
    failures += expect_contains("  Scheduler: trocas=5");
    failures += expect_contains("  Filas: teclado=4/32 descartes=2 processados=3");
    failures += expect_contains("  IPC pendentes=3");
    failures += expect_contains("  Memoria PMM: usada=1 KB livre=3 KB paginas_livres=50 proprias=50 falhas=3 rejeicoes=2");
    failures += expect_contains("  Paging boot: paginas=12 tabelas=3 ticks=9 modo=blocos\n");
    failures += expect_contains("  VESA: apresentacoes=2 completas=1 parciais=1 bytes=4096 media_bytes=2048");

    fixture_reset();
    fixture_paging_boot_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_kmetrics("");
    failures += expect_contains("Metricas K1 (desde boot):\n");

    return failures;
}

static int test_signal_commands(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_sigtest("");
    failures += expect_contains("Autoteste de sinais (fixture privada):\n");
    failures += expect_contains("  ciclo: OK\n");
    failures += expect_contains("  SIGKILL/SIGSEGV fatais: OK\n");
    failures += expect_contains("  invariantes: OK\n");
    failures += expect_contains("Resultado: OK\n");

    fixture_reset();
    fixture_signal_test_result = ERR_STATE;
    shell_dispatch_cmd_sigtest("");
    failures += expect_contains("Resultado: ERRO\n");

    fixture_reset();
    shell_dispatch_cmd_sigtest("extra");
    failures += expect_text("Uso: sigtest\n");

    fixture_reset();
    shell_dispatch_cmd_kill("TERM 42");
    failures += expect_contains("Sinal SIGTERM enviado ao PID 42.\n");
    if (fixture_signal_send_pid != 42U ||
        fixture_signal_send_number != APP_SIGNAL_TERM) {
        fprintf(stderr, "diagnostics-host: kill nao encaminhou sinal\n");
        failures++;
    }

    fixture_reset();
    fixture_signal_send_result = ERR_STATE;
    shell_dispatch_cmd_kill("KILL 42");
    failures += expect_text("Erro: sinal nao entregue.\n");
    if (fixture_signal_send_number != APP_SIGNAL_KILL) {
        fprintf(stderr, "diagnostics-host: kill perdeu codigo do sinal\n");
        failures++;
    }

    fixture_reset();
    shell_dispatch_cmd_kill("TERM 99");
    failures += expect_text(
        "Uso: kill -2|-9|-11|-15|-17|INT|KILL|SEGV|TERM|CHLD PID\n");

    fixture_reset();
    shell_dispatch_cmd_kill("");
    failures += expect_text(
        "Uso: kill -2|-9|-11|-15|-17|INT|KILL|SEGV|TERM|CHLD PID\n");

    return failures;
}

static int test_proccheck(void) {
    int failures = 0;

    fixture_reset();
    fixture_vfs_sysfs_enabled = 1U;
    shell_dispatch_cmd_proccheck("");
    failures += expect_contains("PROC5 introspeccao: OK");
    failures += expect_contains(" testes=");
    failures += expect_contains(" aprovados=");
    if (fixture_vfs_open_calls != fixture_vfs_close_calls ||
        fixture_procfs_reset_calls != 2U) {
        fprintf(stderr, "diagnostics-host: proccheck deixou VFS ou controles ativos\n");
        failures++;
    }

    fixture_reset();
    shell_dispatch_cmd_proccheck("extra");
    failures += expect_text("Uso: proccheck\n");
    return failures;
}

static int test_health(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_health("invalid");
    failures += expect_text("Uso: health [summary|check]\n");

    fixture_reset();
    prepare_health_fixture();
    shell_dispatch_cmd_health("check");
    failures += expect_contains("Health check: OK\n");

    fixture_reset();
    prepare_health_fixture();
    shell_dispatch_cmd_health("summary");
    failures += expect_contains("Resumo do health:\n");
    failures += expect_contains("Componentes: READY=26");
    failures += expect_contains("Update: READY");
    failures += expect_contains("Kernel: proc=");

    fixture_reset();
    prepare_health_fixture();
    fixture_recovery_components[RECOVERY_COMPONENT_NETWORK].state =
        RECOVERY_STATE_DEGRADED;
    fixture_recovery_components[RECOVERY_COMPONENT_NETWORK].failures = 2U;
    fixture_recovery_components[RECOVERY_COMPONENT_NETWORK].last_error =
        ERR_UNAVAILABLE;
    shell_dispatch_cmd_health("summary");
    failures += expect_contains("NETWORK: DEGRADED erro=9 falhas=2\n");

    fixture_reset();
    prepare_health_fixture();
    shell_dispatch_cmd_health("");
    failures += expect_contains("Estado dos componentes:\n");
    failures += expect_contains("VESA: READY");
    failures += expect_contains("Capacidades de Update:\n");
    failures += expect_contains("Estado do kernel:\n");

    fixture_reset();
    prepare_health_fixture();
    fixture_vfs_status_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_health("check");
    failures += expect_contains("VFS: UNKNOWN erro=");
    return failures;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = test_pwd() + test_cd() + test_mouse() + test_log() +
             test_timer() + test_clock() + test_irqstat() + test_wait() +
             test_wqinfo() + test_workq() + test_tls() + test_vfs();
    result += test_devcheck() + test_devices_and_usb();
    result += test_device_scan();
    result += test_power();
    result += test_acpi();
    result += test_slab();
    result += test_cpu_usage();
    result += test_pagefault_and_vmamap();
    result += test_schedcheck();
    result += test_memcheck();
    result += test_kmetrics();
    result += test_signal_commands();
    result += test_proccheck();
    result += test_health();
    coverage_active = 0U;
    coverage_emit(result);
    return result ? 1 : 0;
}
