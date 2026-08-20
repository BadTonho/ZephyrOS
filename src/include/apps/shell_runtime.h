#ifndef SHELL_RUNTIME_H
#define SHELL_RUNTIME_H

#include "types.h"
#include "core/app_loader.h"
#include "core/recovery.h"

typedef enum {
    SHELL_BUILTIN_APP_NONE,
    SHELL_BUILTIN_APP_UPTIME,
    SHELL_BUILTIN_APP_MEM
} shell_builtin_app_t;

typedef struct {
    int heap_integrity;
    int coalescence;
    int pmm_guards;
    int user_directories;
} shell_memcheck_result_t;

typedef struct {
    int pci_result;
    int devices_result;
    int usb_result;
    int storage_result;
    int network_result;
} shell_device_scan_result_t;

void shell_runtime_reset_input(void);
void shell_runtime_handle_terminal_key(uint8_t scancode);
void shell_runtime_suspend_terminal(void);
void shell_runtime_suspend_terminal_for_scene(void);
void shell_runtime_resume_terminal(void);
void shell_runtime_finish_command(void);
int shell_runtime_is_hosted_visible(void);
int shell_runtime_prepare_filemanager(void);

int shell_checks_input_blocked(void);
int shell_checks_handle_job_key(uint8_t scancode);
int shell_checks_handle_loader_result(const app_loader_result_t* result);
void shell_checks_report_user_test_result(void);
void shell_checks_run_app_inputtest(void);

const char* shell_core_builtin_app_name(shell_builtin_app_t app);
int shell_core_migrated_builtin_is_ready(void);
int shell_core_handle_loader_result(const app_loader_result_t* result);
void shell_core_reboot(void);
void shell_core_shutdown(const char* arguments);

int shell_diagnostics_run_memcheck(shell_memcheck_result_t* result_out);
int shell_diagnostics_run_device_scan(shell_device_scan_result_t* scan);
uint8_t shell_diagnostics_health_state_color(recovery_state_t state);
void shell_diagnostics_print_usb_fixture_report(void);
void shell_diagnostics_reset(void);
int shell_network_validate_for_checks(void);

void shell_hosted_reset(void);
int shell_hosted_open(void);
void shell_hosted_present_progress(void);

#endif
