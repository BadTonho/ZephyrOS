#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/recovery.h"
#include "core/tls.h"
#include "core/update_remote.h"
#include "apps/shell_diagnostics_helpers.h"
#include "drivers/acpi.h"
#include "drivers/mouse.h"
#include "core/device_manager.h"
#include "core/usb_manager.h"
#include "fs/fs.h"
#include "fs/vfs.h"
#include "memory/paging.h"
#include "process/process.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static storage_fs_type_t host_fs_type;
static uint32_t host_free_pages;

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
    printf("ZCOV_BEGIN|case=host:shell:diagnostics-helpers|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:diagnostics-helpers|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:diagnostics-helpers|value=0x%08X\n",
           (uint32_t)result);
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

uint8_t fs_get_type(void) {
    return (uint8_t)host_fs_type;
}

uint32_t memory_get_free_pages(void) {
    return host_free_pages;
}

void shell_command_uppercase(char* text) {
    if (!text) return;
    while (*text) {
        if (*text >= 'a' && *text <= 'z') *text -= 'a' - 'A';
        text++;
    }
}

int shell_command_read_single_arg(const char* args, char* output,
                                  uint32_t capacity) {
    uint32_t length = 0U;

    if (!args || !output || capacity < 2U) return ERR_NULL;
    while (*args == ' ' || *args == '\t') args++;
    if (!*args) return ERR_INVALID;
    while (args[length] && args[length] != ' ' && args[length] != '\t') {
        if (length + 1U >= capacity) return ERR_OVERFLOW;
        output[length] = args[length];
        length++;
    }
    output[length] = '\0';
    while (args[length] == ' ' || args[length] == '\t') length++;
    if (args[length]) return ERR_INVALID;
    return OK;
}

int tls_capability_available(void) {
    return 1;
}

const char* tls_reason_name(tls_reason_t reason) {
    return reason == TLS_REASON_HANDSHAKE ? "handshake" : "reason";
}

static int host_check(int condition, const char* label) {
    if (condition) return 0;
    printf("SHELL_DIAGNOSTICS_FAIL:%s\n", label);
    return 1;
}

static int test_log_helpers(void) {
    log_level_t level = LOG_LEVEL_ERROR;
    int failures = 0;

    failures += host_check(strcmp(cmd_log_level_name(LOG_LEVEL_ERROR), "error") == 0,
                           "log_name_error");
    failures += host_check(strcmp(cmd_log_level_name(LOG_LEVEL_WARN), "warn") == 0,
                           "log_name_warn");
    failures += host_check(strcmp(cmd_log_level_name(LOG_LEVEL_INFO), "info") == 0,
                           "log_name_info");
    failures += host_check(strcmp(cmd_log_level_name(LOG_LEVEL_DEBUG), "debug") == 0,
                           "log_name_debug");
    failures += host_check(strcmp(cmd_log_level_name((log_level_t)99), "invalid") == 0,
                           "log_name_invalid");
    failures += host_check(cmd_log_parse_level("error", &level) &&
                           level == LOG_LEVEL_ERROR, "parse_error");
    failures += host_check(cmd_log_parse_level("warn", &level) &&
                           level == LOG_LEVEL_WARN, "parse_warn");
    failures += host_check(cmd_log_parse_level("info", &level) &&
                           level == LOG_LEVEL_INFO, "parse_info");
    failures += host_check(cmd_log_parse_level("debug", &level) &&
                           level == LOG_LEVEL_DEBUG, "parse_debug");
    failures += host_check(!cmd_log_parse_level("trace", &level), "parse_invalid");
    failures += host_check(!cmd_log_parse_level(NULL, &level), "parse_null_name");
    failures += host_check(!cmd_log_parse_level("info", NULL), "parse_null_output");
    failures += host_check(cmd_log_parse_tail_count("1") == 1U,
                           "tail_one");
    failures += host_check(cmd_log_parse_tail_count("32") == 32U,
                           "tail_max");
    failures += host_check(cmd_log_parse_tail_count("33") == 0U,
                           "tail_overflow");
    failures += host_check(cmd_log_parse_tail_count("1x") == 0U,
                           "tail_invalid");
    failures += host_check(cmd_log_parse_tail_count("") == 0U,
                           "tail_empty");
    failures += host_check(cmd_log_parse_tail_count(NULL) == 0U,
                           "tail_null");
    return failures;
}

static int test_signal_and_mouse_helpers(void) {
    uint32_t value = 0U;
    uint8_t speed = 0U;
    const char* cursor;
    char token[8];
    int failures = 0;

    failures += host_check(cmd_signal_parse_uint("123", &value) == OK &&
                           value == 123U, "signal_uint");
    failures += host_check(cmd_signal_parse_uint("4294967295", &value) == OK &&
                           value == 0xFFFFFFFFU, "signal_uint_max");
    failures += host_check(cmd_signal_parse_uint("4294967296", &value) ==
                           ERR_OVERFLOW, "signal_uint_overflow");
    failures += host_check(cmd_signal_parse_uint("12x", &value) == ERR_INVALID,
                           "signal_uint_invalid");
    failures += host_check(cmd_signal_parse_uint(NULL, &value) == ERR_INVALID,
                           "signal_uint_null");
    failures += host_check(cmd_signal_parse_name("SIGINT", &value) == OK &&
                           value == APP_SIGNAL_INT, "signal_name");
    failures += host_check(cmd_signal_parse_name("-term", &value) == OK &&
                           value == APP_SIGNAL_TERM, "signal_name_dash");
    failures += host_check(cmd_signal_parse_name("8", &value) == ERR_INVALID,
                           "signal_number_invalid");
    failures += host_check(cmd_signal_parse_name("SIGUNKNOWN", &value) == ERR_INVALID,
                           "signal_name_invalid");
    failures += host_check(cmd_signal_parse_name("SIGINTLONGNAMEXX", &value) ==
                           ERR_OVERFLOW, "signal_name_overflow");
    failures += host_check(cmd_mouse_parse_speed("1", &speed) == OK && speed == 1U,
                           "mouse_speed_one");
    failures += host_check(cmd_mouse_parse_speed("10", &speed) == OK &&
                           speed == MOUSE_SPEED_MAX, "mouse_speed_max");
    failures += host_check(cmd_mouse_parse_speed("0", &speed) == ERR_INVALID,
                           "mouse_speed_zero");
    failures += host_check(cmd_mouse_parse_speed(NULL, &speed) == ERR_NULL,
                           "mouse_speed_null");
    cursor = "  speed rest";
    failures += host_check(cmd_mouse_read_token(&cursor, token, sizeof(token)) == OK &&
                           strcmp(token, "speed") == 0 &&
                           cmd_mouse_has_extra_args(cursor), "mouse_token");
    cursor = "";
    failures += host_check(cmd_mouse_read_token(&cursor, token, sizeof(token)) ==
                           ERR_INVALID, "mouse_token_empty");
    cursor = "12345678";
    failures += host_check(cmd_mouse_read_token(&cursor, token, sizeof(token)) ==
                           ERR_OVERFLOW, "mouse_token_overflow");
    failures += host_check(cmd_mouse_read_token(NULL, token, sizeof(token)) == ERR_NULL,
                           "mouse_token_null");
    failures += host_check(!cmd_mouse_has_extra_args(" \t"), "mouse_extra_empty");
    return failures;
}

static int test_state_helpers(void) {
    update_remote_status_t remote;
    update_capabilities_t capabilities;
    update_status_t update_status;
    tls_status_t tls_status;
    vm_area_info_t area;
    memory_heap_stats_t heap_before;
    memory_heap_stats_t heap_after;
    memory_detailed_stats_t detail_before;
    memory_detailed_stats_t detail_after;
    memory_pmm_stats_t pmm;
    uint32_t free_pages = 5U;
    int failures = 0;

    failures += host_check(strcmp(shell_process_state_name(PROCESS_STATE_READY),
                                   "READY") == 0, "process_ready");
    failures += host_check(strcmp(shell_process_state_name(PROCESS_STATE_RUNNING),
                                   "RUNNING") == 0, "process_running");
    failures += host_check(strcmp(shell_process_state_name(PROCESS_STATE_BLOCKED),
                                   "BLOCKED") == 0, "process_blocked");
    failures += host_check(strcmp(shell_process_state_name(PROCESS_STATE_ZOMBIE),
                                   "ZOMBIE") == 0, "process_zombie");
    failures += host_check(strcmp(shell_process_state_name((process_state_t)99),
                                   "UNUSED") == 0, "process_invalid");
    failures += host_check(shell_diagnostics_health_state_color(RECOVERY_STATE_READY) == 0x0A,
                           "health_ready");
    failures += host_check(shell_diagnostics_health_state_color(RECOVERY_STATE_DEGRADED) == 0x0E,
                           "health_degraded");
    failures += host_check(shell_diagnostics_health_state_color(RECOVERY_STATE_DISABLED) == 0x0C,
                           "health_disabled");
    failures += host_check(shell_diagnostics_health_state_color(RECOVERY_STATE_UNKNOWN) == 0x0C,
                           "health_unknown");
    failures += host_check(cmd_device_status_color(DEVICE_STATUS_READY) == 0x0A,
                           "device_ready");
    failures += host_check(cmd_device_status_color(DEVICE_STATUS_DEGRADED) == 0x0E,
                           "device_degraded");
    failures += host_check(cmd_device_status_color(DEVICE_STATUS_DISABLED) == 0x0C,
                           "device_disabled");
    failures += host_check(cmd_device_status_color(DEVICE_STATUS_UNKNOWN) == 0x08,
                           "device_unknown");
    failures += host_check(cmd_usb_recovery_color(RECOVERY_STATE_READY) == 0x0A,
                           "usb_recovery_ready");
    failures += host_check(cmd_usb_state_color(USB_CONTROLLER_READY) == 0x0A,
                           "usb_state_ready");
    failures += host_check(cmd_usb_port_color(USB_PORT_CONFIGURED) == 0x0A,
                           "usb_port_configured");
    failures += host_check(strcmp(cmd_acpi_mode_name(ACPI_MODE_ENABLED), "HABILITADO") == 0,
                           "acpi_mode");
    failures += host_check(strcmp(cmd_acpi_s5_name(ACPI_S5_AMBIGUOUS), "AMBIGUO") == 0,
                           "acpi_s5");
    failures += host_check(strcmp(cmd_acpi_space_name(ACPI_ADDRESS_SPACE_SYSTEM_IO),
                                   "SYSTEM-IO") == 0, "acpi_space");
    failures += host_check(strcmp(cmd_vfs_node_name(VFS_NODE_SOCKET), "SOCKET") == 0,
                           "vfs_node");
    failures += host_check(cmd_health_update_local_state(&(update_capabilities_t){
                               .verifier_ready = 0U}) == RECOVERY_STATE_DISABLED,
                           "update_local_disabled");
    failures += host_check(cmd_health_update_local_state(&(update_capabilities_t){
                               .verifier_ready = 1U, .local_file_available = 0U}) ==
                           RECOVERY_STATE_DEGRADED, "update_local_degraded");
    capabilities.verifier_ready = 1U;
    capabilities.local_file_available = 1U;
    capabilities.apply_available = 1U;
    failures += host_check(cmd_health_update_local_state(&capabilities) == RECOVERY_STATE_READY,
                           "update_local_ready");
    failures += host_check(cmd_health_update_apply_state(&capabilities) == RECOVERY_STATE_READY,
                           "update_apply_ready");
    capabilities.apply_available = 0U;
    capabilities.recovery_pending = 1U;
    failures += host_check(cmd_health_update_apply_state(&capabilities) == RECOVERY_STATE_DEGRADED,
                           "update_apply_recovery");
    capabilities.recovery_pending = 0U;
    capabilities.persistent_state_ready = 0U;
    host_fs_type = FS_TYPE_FAT12;
    failures += host_check(cmd_health_update_apply_state(&capabilities) == RECOVERY_STATE_DEGRADED,
                           "update_apply_persistent");
    host_fs_type = FS_TYPE_NONE;
    failures += host_check(cmd_health_update_apply_state(&capabilities) == RECOVERY_STATE_DISABLED,
                           "update_apply_disabled");
    capabilities.history_available = 1U;
    failures += host_check(cmd_health_update_history_state(&capabilities, &update_status, 0) ==
                           RECOVERY_STATE_READY, "update_history_ready");
    capabilities.history_available = 0U;
    update_status.history_store = UPDATE_STORE_INVALID;
    failures += host_check(cmd_health_update_history_state(&capabilities, &update_status, 1) ==
                           RECOVERY_STATE_DEGRADED, "update_history_invalid");
    memset(&remote, 0, sizeof(remote));
    failures += host_check(cmd_health_remote_state_from_status(NULL) == RECOVERY_STATE_DISABLED,
                           "remote_null");
    failures += host_check(cmd_health_remote_state_from_status(&remote) == RECOVERY_STATE_DISABLED,
                           "remote_disabled");
    remote.enabled = 1U;
    remote.network_ready = 1U;
    remote.state = UPDATE_REMOTE_STATE_READY;
    remote.cache_store = UPDATE_REMOTE_STORE_VALID;
    failures += host_check(cmd_health_remote_state_from_status(&remote) == RECOVERY_STATE_READY,
                           "remote_ready");
    remote.network_ready = 0U;
    failures += host_check(cmd_health_remote_state_from_status(&remote) == RECOVERY_STATE_DEGRADED,
                           "remote_network");
    memset(&tls_status, 0, sizeof(tls_status));
    failures += host_check(strcmp(cmd_health_check_tls_detail(&tls_status),
                                   "tempo nao confiavel") == 0, "tls_time");
    tls_status.trusted_time_available = 1U;
    failures += host_check(strcmp(cmd_health_check_tls_detail(&tls_status),
                                   "handshake indisponivel") == 0, "tls_handshake");
    tls_status.handshake_available = 1U;
    failures += host_check(strcmp(cmd_health_check_tls_detail(&tls_status),
                                   "validacao X509 indisponivel") == 0, "tls_x509");
    tls_status.certificate_validation_available = 1U;
    failures += host_check(strcmp(cmd_health_check_tls_detail(&tls_status),
                                   "entropia indisponivel") == 0, "tls_entropy");
    tls_status.entropy_available = 1U;
    tls_status.last_reason = TLS_REASON_HANDSHAKE;
    failures += host_check(strcmp(cmd_health_check_tls_detail(&tls_status), "handshake") == 0,
                           "tls_reason");
    memset(&area, 0, sizeof(area));
    failures += host_check(strcmp(cmd_vmamap_area_type(NULL), "INVALID") == 0,
                           "vma_null");
    area.start_addr = USER_CODE_BASE;
    failures += host_check(strcmp(cmd_vmamap_area_type(&area), "CODE") == 0,
                           "vma_code");
    area.start_addr = USER_DATA_BASE;
    failures += host_check(strcmp(cmd_vmamap_area_type(&area), "DATA") == 0,
                           "vma_data");
    area.start_addr = USER_LAUNCH_BASE;
    failures += host_check(strcmp(cmd_vmamap_area_type(&area), "LAUNCH") == 0,
                           "vma_launch");
    area.start_addr = USER_STACK_BASE;
    failures += host_check(strcmp(cmd_vmamap_area_type(&area), "STACK") == 0,
                           "vma_stack");
    area.start_addr = 1U;
    area.flags = VM_ANONYMOUS;
    failures += host_check(strcmp(cmd_vmamap_area_type(&area), "ANON") == 0,
                           "vma_anon");
    area.flags = 0U;
    failures += host_check(strcmp(cmd_vmamap_area_type(&area), "OTHER") == 0,
                           "vma_other");
    memset(&heap_before, 0, sizeof(heap_before));
    heap_before.total_bytes = 100U;
    heap_before.free_bytes = 50U;
    heap_after = heap_before;
    failures += host_check(shell_memcheck_same_layout(&heap_before, &heap_after),
                           "memory_layout_equal");
    heap_after.free_bytes++;
    failures += host_check(!shell_memcheck_same_layout(&heap_before, &heap_after),
                           "memory_layout_changed");
    failures += host_check(!shell_memcheck_same_layout(NULL, &heap_after),
                           "memory_layout_null");
    memset(&detail_before, 0, sizeof(detail_before));
    detail_before.total_pages = 10U;
    detail_before.zone_pages[MEMORY_ZONE_KERNEL] = 1U;
    detail_before.zone_pages[MEMORY_ZONE_HEAP] = 1U;
    detail_before.zone_pages[MEMORY_ZONE_SLAB] = 1U;
    detail_before.zone_pages[MEMORY_ZONE_PROCESS] = 1U;
    detail_before.zone_pages[MEMORY_ZONE_BUFFER] = 1U;
    detail_before.zone_pages[MEMORY_ZONE_FREE] = 5U;
    detail_before.free_runs = 2U;
    detail_before.largest_free_run = 4U;
    detail_before.isolated_free_pages = 1U;
    detail_before.fragmentation_percent = 10U;
    detail_before.initialized = 1U;
    detail_before.valid = 1U;
    detail_after = detail_before;
    memset(&pmm, 0, sizeof(pmm));
    pmm.initialized = 1U;
    pmm.owned_pages = 3U;
    host_free_pages = free_pages;
    failures += host_check(shell_memcheck_same_memory_metrics(&detail_before,
                                                               &detail_after),
                           "memory_metrics_equal");
    failures += host_check(shell_memcheck_valid_memory_metrics(&detail_before, &pmm),
                           "memory_metrics_valid");
    detail_after.free_runs++;
    failures += host_check(!shell_memcheck_same_memory_metrics(&detail_before,
                                                                &detail_after),
                           "memory_metrics_changed");
    detail_before.fragmentation_percent = 101U;
    failures += host_check(!shell_memcheck_valid_memory_metrics(&detail_before, &pmm),
                           "memory_metrics_invalid");
    failures += host_check(shell_cpu_usage_percent(25U, 75U) == 25U,
                           "cpu_percent");
    failures += host_check(shell_cpu_usage_percent(0xFFFFFFFFU, 1U) <= 100U,
                           "cpu_percent_bound");
    failures += host_check(shell_kmetrics_delta(10U, 3U) == 7U,
                           "metrics_delta");
    failures += host_check(shell_kmetrics_delta(1U, 0xFFFFFFFFU) == 2U,
                           "metrics_delta_wrap");
    return failures;
}

static int test_path_helpers(void) {
    device_info_t info;
    device_text_t text;
    char path[128];
    uint32_t length = 0U;
    uint32_t pid = 0U;
    int failures = 0;

    failures += host_check(cmd_sysfs_append(path, sizeof(path), &length, "/sys/") == OK &&
                           strcmp(path, "/sys/") == 0, "sysfs_append");
    failures += host_check(cmd_sysfs_append(path, 5U, &length, "x") == ERR_OVERFLOW,
                           "sysfs_append_overflow");
    failures += host_check(cmd_sysfs_append(NULL, sizeof(path), &length, "x") == ERR_NULL,
                           "sysfs_append_null");
    memset(&info, 0, sizeof(info));
    memset(&text, 0, sizeof(text));
    strcpy(text.id, "pci-0000");
    info.kind = DEVICE_KIND_PCI;
    failures += host_check(cmd_sysfs_path_for_device(&info, &text, path, sizeof(path)) == OK &&
                           strcmp(path, "/sys/bus/pci/devices/0000") == 0,
                           "sysfs_device_pci");
    info.kind = DEVICE_KIND_ATA_PRIMARY;
    strcpy(text.id, "ata0");
    failures += host_check(cmd_sysfs_path_for_device(&info, &text, path, sizeof(path)) == OK &&
                           strcmp(path, "/sys/class/block/ata0") == 0,
                           "sysfs_device_ata");
    info.kind = DEVICE_KIND_AC97;
    failures += host_check(cmd_sysfs_path_for_device(&info, &text, path, sizeof(path)) ==
                           ERR_NOT_FOUND, "sysfs_device_other");
    failures += host_check(cmd_sysfs_path_for_device(NULL, &text, path, sizeof(path)) ==
                           ERR_NULL, "sysfs_device_null");
    failures += host_check(cmd_sysfs_path_for_id("net-eth0", path, sizeof(path)) == OK &&
                           strcmp(path, "/sys/class/net/net-eth0") == 0,
                           "sysfs_id_net");
    failures += host_check(cmd_sysfs_path_for_id("pci-0000", path, sizeof(path)) == OK &&
                           strcmp(path, "/sys/bus/pci/devices/0000") == 0,
                           "sysfs_id_pci");
    failures += host_check(cmd_sysfs_path_for_id("ata0", path, sizeof(path)) == OK &&
                           strcmp(path, "/sys/class/block/ata0") == 0,
                           "sysfs_id_ata");
    failures += host_check(cmd_sysfs_path_for_id("net-", path, sizeof(path)) == ERR_INVALID,
                           "sysfs_id_empty");
    failures += host_check(cmd_sysfs_path_for_id("usb0", path, sizeof(path)) == ERR_NOT_FOUND,
                           "sysfs_id_unknown");
    failures += host_check(cmd_sysfs_path_for_id(NULL, path, sizeof(path)) == ERR_NULL,
                           "sysfs_id_null");
    failures += host_check(cmd_proccheck_pid_path(path, sizeof(path), 12345U,
                                                  "/status") == OK &&
                           strcmp(path, "/proc/12345/status") == 0,
                           "proc_pid_path");
    failures += host_check(cmd_proccheck_pid_path(path, 8U, 12345U,
                                                  "/status") == ERR_OVERFLOW,
                           "proc_pid_overflow");
    failures += host_check(cmd_vmamap_parse_pid("12", &pid) == OK && pid == 12U,
                           "vmamap_pid");
    failures += host_check(cmd_vmamap_parse_pid("0", &pid) == ERR_INVALID,
                           "vmamap_pid_zero");
    failures += host_check(cmd_vmamap_parse_pid("999", &pid) == ERR_OVERFLOW,
                           "vmamap_pid_overflow");
    failures += host_check(cmd_vmamap_parse_pid(NULL, &pid) == ERR_INVALID,
                           "vmamap_pid_null_args");
    return failures;
}

int main(void) {
    int result;

    host_fs_type = FS_TYPE_NONE;
    coverage_active = 1U;
    result = test_log_helpers();
    result += test_signal_and_mouse_helpers();
    result += test_state_helpers();
    result += test_path_helpers();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != 0) return result;
    puts("shell-diagnostics-helpers-host: PASS");
    return 0;
}
