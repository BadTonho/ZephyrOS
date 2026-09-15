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
    int slab_integrity;
    int memory_metrics;
} shell_memcheck_result_t;

typedef struct {
    int pci_result;
    int devices_result;
    int usb_result;
    int storage_result;
    int network_result;
    int wifi_result;
} shell_device_scan_result_t;

typedef enum {
    SHELL_LIFECYCLE_LAYER_NONE = 0,
    SHELL_LIFECYCLE_LAYER_DISPATCHER,
    SHELL_LIFECYCLE_LAYER_JOB,
    SHELL_LIFECYCLE_LAYER_SCENE,
    SHELL_LIFECYCLE_LAYER_FOCUS,
    SHELL_LIFECYCLE_LAYER_VIDEO,
    SHELL_LIFECYCLE_LAYER_INPUT,
    SHELL_LIFECYCLE_LAYER_LOADER
} shell_lifecycle_layer_t;

typedef enum {
    SHELL_LIFECYCLE_PROMPT_HIDDEN = 0,
    SHELL_LIFECYCLE_PROMPT_REQUESTED,
    SHELL_LIFECYCLE_PROMPT_VISIBLE,
    SHELL_LIFECYCLE_PROMPT_BLOCKED
} shell_lifecycle_prompt_state_t;

typedef struct {
    uint32_t generation;
    uint32_t finalization_requests;
    uint32_t finalizations;
    uint32_t duplicate_finalizations;
    uint32_t prompt_requests;
    uint32_t prompt_reconciliations;
    uint32_t prompt_rendered;
    uint32_t prompt_blocked;
    uint32_t prompt_missing;
    uint32_t prompt_duplicates;
    uint32_t input_blocked_events;
    uint32_t last_error;
    shell_lifecycle_layer_t last_layer;
    shell_lifecycle_prompt_state_t prompt_state;
    uint8_t operation_active;
    uint8_t input_blocked;
    uint8_t terminal_active;
    uint8_t hosted_visible;
    uint8_t focus_shell;
    uint8_t scene_active;
    uint8_t job_active;
    uint8_t loader_active;
} shell_lifecycle_status_t;

void shell_runtime_reset_input(void);
void shell_runtime_handle_terminal_key(uint8_t scancode);
void shell_runtime_suspend_terminal(void);
void shell_runtime_suspend_terminal_for_scene(void);
void shell_runtime_resume_terminal(void);
void shell_runtime_finish_command(void);
void shell_runtime_begin_operation(shell_lifecycle_layer_t layer);
void shell_runtime_note_lifecycle_layer(shell_lifecycle_layer_t layer);
void shell_runtime_note_lifecycle_error(int error_code);
void shell_runtime_note_lifecycle_input_blocked(void);
int shell_runtime_get_lifecycle_status(shell_lifecycle_status_t* status_out);
void shell_runtime_reset_lifecycle_status(void);
int shell_runtime_is_hosted_visible(void);
int shell_runtime_prepare_filemanager(void);

int shell_checks_input_blocked(void);
int shell_checks_should_cancel_focused_user(uint8_t scancode);
int shell_checks_handle_job_key(uint8_t scancode);
int shell_checks_handle_loader_result(const app_loader_result_t* result);
void shell_checks_report_user_test_result(void);
void shell_checks_run_app_inputtest(uint8_t use_tty);

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

#ifdef ZEPHYROS_HOST_TEST
int shell_network_checks_host_test_contracts(void);
int shell_network_host_test_contracts(void);
int shell_packages_host_test_contracts(void);
#endif

void shell_hosted_reset(void);
void shell_runtime_close_hosted(void);
int shell_hosted_open(void);
void shell_hosted_present_progress(void);

#endif
