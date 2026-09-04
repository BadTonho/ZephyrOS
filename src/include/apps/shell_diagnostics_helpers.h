#ifndef SHELL_DIAGNOSTICS_HELPERS_H
#define SHELL_DIAGNOSTICS_HELPERS_H

#include "types.h"
#include "core/device_manager.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/recovery.h"
#include "core/tls.h"
#include "core/update.h"
#include "core/update_remote.h"
#include "core/usb_manager.h"
#include "drivers/acpi.h"
#include "fs/vfs.h"
#include "memory/paging.h"
#include "process/process.h"

const char* shell_process_state_name(process_state_t state);
uint8_t shell_diagnostics_health_state_color(recovery_state_t state);
recovery_state_t cmd_health_update_local_state(
    const update_capabilities_t* capabilities);
recovery_state_t cmd_health_update_apply_state(
    const update_capabilities_t* capabilities);
recovery_state_t cmd_health_update_history_state(
    const update_capabilities_t* capabilities,
    const update_status_t* status, int status_ready);
recovery_state_t cmd_health_remote_state_from_status(
    const update_remote_status_t* remote);
const char* cmd_health_check_tls_detail(const tls_status_t* status);
const char* cmd_log_level_name(log_level_t level);
int cmd_log_parse_level(const char* name, log_level_t* level);
uint32_t cmd_log_parse_tail_count(const char* text);
int cmd_signal_parse_uint(const char* text, uint32_t* output);
int cmd_signal_parse_name(const char* text, uint32_t* signal_number);
uint32_t shell_cpu_usage_percent(uint32_t value, uint32_t other);
uint8_t cmd_device_status_color(device_status_t status);
int cmd_sysfs_append(char* path, uint32_t capacity, uint32_t* length,
                     const char* text);
int cmd_sysfs_path_for_device(const device_info_t* info,
                              const device_text_t* text,
                              char* path, uint32_t capacity);
int cmd_sysfs_path_for_id(const char* id, char* path, uint32_t capacity);
int cmd_proccheck_pid_path(char* path, uint32_t capacity, uint32_t pid,
                           const char* suffix);
uint8_t cmd_usb_recovery_color(recovery_state_t state);
uint8_t cmd_usb_state_color(usb_controller_state_t state);
uint8_t cmd_usb_port_color(usb_port_state_t state);
const char* cmd_acpi_mode_name(acpi_mode_t mode);
const char* cmd_acpi_s5_name(acpi_s5_state_t state);
const char* cmd_acpi_space_name(uint8_t space_id);
uint32_t shell_kmetrics_delta(uint32_t current, uint32_t baseline);
int shell_memcheck_same_layout(const memory_heap_stats_t* before,
                               const memory_heap_stats_t* after);
int shell_memcheck_same_memory_metrics(
    const memory_detailed_stats_t* before,
    const memory_detailed_stats_t* after);
int shell_memcheck_valid_memory_metrics(
    const memory_detailed_stats_t* detailed,
    const memory_pmm_stats_t* pmm);
const char* cmd_vmamap_area_type(const vm_area_info_t* area);
int cmd_vmamap_parse_pid(const char* args, uint32_t* pid_out);
int cmd_mouse_read_token(const char** cursor, char* token, int token_size);
int cmd_mouse_has_extra_args(const char* cursor);
int cmd_mouse_parse_speed(const char* value, uint8_t* speed);
const char* cmd_vfs_node_name(vfs_node_type_t type);

#endif
