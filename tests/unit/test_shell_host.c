#include <stdint.h>
#include <stdio.h>

#include "apps/shell.h"
#include "apps/shell_input.h"
#include "apps/shell_job.h"
#include "apps/shell_runtime.h"
#include "apps/shell_command_utils.h"
#include "core/app_loader.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/video.h"
#include "drivers/mouse.h"
#include "apps/guitest.h"
#include "fs/fs.h"
#include "core/recovery.h"
#include "process/signal.h"
#include "apps/taskmanager.h"
#include "ui/appstore.h"
#include "ui/desktop.h"
#include "ui/filemanager.h"
#include "ui/settings.h"
#include "ui/taskbar.h"
#include "ui/updater.h"
#include "ui/wm.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_SHELL_WHEEL_SCROLL_LINES 3

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static int fake_terminal_active;
static int fake_wm_active;
static int fake_hosted_visible;
static int fake_desktop_active;
static int fake_fm_running;
static int fake_taskmgr_open;
static int fake_taskmgr_gui_open;
static int fake_settings_open;
static int fake_updater_open;
static int fake_appstore_open;
static int fake_guitest_active;
static int fake_input_blocked;
static int fake_job_blocked;
static int fake_loader_foreground;
static int fake_taskbar_config_result;
static uint32_t fake_input_init_calls;
static uint32_t fake_job_reset_calls;
static uint32_t fake_hosted_reset_calls;
static uint32_t fake_diagnostics_reset_calls;
static uint32_t fake_input_reset_calls;
static uint32_t fake_terminal_suspend_calls;
static uint32_t fake_terminal_scroll_calls;
static uint32_t fake_prompt_calls;
static uint32_t fake_terminal_begin_calls;
static uint32_t fake_taskbar_draw_calls;

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
    printf("ZCOV_BEGIN|case=host:shell:core|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:core|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:core|value=0x%08X\n",
           (uint32_t)result);
}

static void reset_fixture(void) {
    fake_terminal_active = 0;
    fake_wm_active = 0;
    fake_hosted_visible = 0;
    fake_desktop_active = 0;
    fake_fm_running = 0;
    fake_taskmgr_open = 0;
    fake_taskmgr_gui_open = 0;
    fake_settings_open = 0;
    fake_updater_open = 0;
    fake_appstore_open = 0;
    fake_guitest_active = 0;
    fake_input_blocked = 0;
    fake_job_blocked = 0;
    fake_loader_foreground = 0;
    fake_taskbar_config_result = 0;
    fake_input_init_calls = 0U;
    fake_job_reset_calls = 0U;
    fake_hosted_reset_calls = 0U;
    fake_diagnostics_reset_calls = 0U;
    fake_input_reset_calls = 0U;
    fake_terminal_suspend_calls = 0U;
    fake_terminal_scroll_calls = 0U;
    fake_prompt_calls = 0U;
    fake_terminal_begin_calls = 0U;
    fake_taskbar_draw_calls = 0U;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void shell_input_init(void) {
    fake_input_init_calls++;
}

void shell_job_reset(void) {
    fake_job_reset_calls++;
}

void shell_hosted_reset(void) {
    fake_hosted_reset_calls++;
}

void shell_diagnostics_reset(void) {
    fake_diagnostics_reset_calls++;
}

void shell_input_reset(void) {
    fake_input_reset_calls++;
}

void shell_input_print_prompt(uint8_t wm_active) {
    (void)wm_active;
    fake_prompt_calls++;
}

int video_terminal_is_active(void) {
    return fake_terminal_active;
}

void video_terminal_suspend(void) {
    fake_terminal_suspend_calls++;
}

int video_terminal_scroll(int lines) {
    if (!lines) return 0;
    fake_terminal_scroll_calls++;
    return lines;
}

void video_terminal_begin(void) {
    fake_terminal_begin_calls++;
}

desktop_mode_t desktop_get_mode(void) {
    return DESKTOP_MODE_SIMPLE;
}

int desktop_is_active(void) {
    return fake_desktop_active;
}

void desktop_draw(void) {
}

int wm_is_active(void) {
    return fake_wm_active;
}

void wm_draw_all(void) {
}

int shell_runtime_is_hosted_visible(void) {
    return fake_hosted_visible;
}

int shell_checks_input_blocked(void) {
    return fake_input_blocked;
}

int shell_job_input_blocked(void) {
    return fake_job_blocked;
}

int app_loader_is_foreground_active(void) {
    return fake_loader_foreground;
}

int fm_is_running(void) {
    return fake_fm_running;
}

int taskmgr_is_open(void) {
    return fake_taskmgr_open;
}

int taskmgr_is_gui_open(void) {
    return fake_taskmgr_gui_open;
}

int settings_is_open(void) {
    return fake_settings_open;
}

int updater_is_open(void) {
    return fake_updater_open;
}

int appstore_is_open(void) {
    return fake_appstore_open;
}

int guitest_is_active(void) {
    return fake_guitest_active;
}

void appstore_draw(void) {
}

void updater_draw(void) {
}

void guitest_draw(void) {
}

void guitest_handle_key(uint8_t scancode) {
    (void)scancode;
}

void taskbar_draw(void) {
    fake_taskbar_draw_calls++;
}

void shell_input_resume_terminal(uint8_t wm_active) {
    (void)wm_active;
}

const char* shell_input_get_buffer(void) {
    return "";
}

shell_input_event_t shell_input_handle_key(uint8_t scancode,
                                           uint8_t wm_active,
                                           uint8_t input_blocked) {
    (void)scancode;
    (void)wm_active;
    (void)input_blocked;
    return SHELL_INPUT_EVENT_NONE;
}

void shell_input_cancel_extended(void) {
}

int fs_init(void) {
    return ERR_UNAVAILABLE;
}

uint8_t fs_get_type(void) {
    return FS_TYPE_NONE;
}

int recovery_is_enabled(recovery_component_id_t component) {
    (void)component;
    return 0;
}

int recovery_mark_ready(recovery_component_id_t component) {
    (void)component;
    return OK;
}

int recovery_mark_disabled(recovery_component_id_t component, int error_code,
                           const char* message) {
    (void)component;
    (void)error_code;
    (void)message;
    return OK;
}

void taskmgr_close(void) {
}

int taskmgr_open_gui(void) {
    return ERR_UNAVAILABLE;
}

void taskmgr_run(void) {
}

void taskmgr_handle_key(uint8_t scancode) {
    (void)scancode;
}

void taskmgr_gui_handle_key(uint8_t scancode) {
    (void)scancode;
}

void settings_close(void) {
}

void settings_open(void) {
}

int settings_handle_key(uint8_t scancode) {
    (void)scancode;
    return 0;
}

void updater_close(void) {
}

int updater_open(void) {
    return ERR_UNAVAILABLE;
}

void updater_handle_key(uint8_t scancode) {
    (void)scancode;
}

void appstore_close(void) {
}

int appstore_open(void) {
    return ERR_UNAVAILABLE;
}

void appstore_handle_key(uint8_t scancode) {
    (void)scancode;
}

void fm_run(void) {
}

void desktop_set_active(int active) {
    fake_desktop_active = active;
}

int desktop_handle_key(uint8_t scancode) {
    (void)scancode;
    return 0;
}

void wm_set_active(int active) {
    fake_wm_active = active;
}

int wm_handle_key(uint8_t scancode) {
    (void)scancode;
    return WM_RESULT_NONE;
}

int taskbar_handle_config_key(uint8_t scancode) {
    (void)scancode;
    return fake_taskbar_config_result;
}

int taskbar_handle_key(uint8_t scancode) {
    (void)scancode;
    return 0;
}

void shell_job_handle_key(uint8_t scancode) {
    (void)scancode;
}

int shell_job_is_active(void) {
    return 0;
}

void shell_hosted_present_progress(void) {
}

void shell_core_reboot(void) {
}

void shell_core_shutdown(const char* arguments) {
    (void)arguments;
}

void shell_command_print_num(uint32_t value) {
    (void)value;
}

int shell_checks_handle_job_key(uint8_t scancode) {
    (void)scancode;
    return 0;
}

int shell_checks_handle_loader_result(const app_loader_result_t* result) {
    (void)result;
    return 0;
}

void shell_checks_report_user_test_result(void) {
}

int shell_core_handle_loader_result(const app_loader_result_t* result) {
    (void)result;
    return 0;
}

int app_loader_take_finished_result(app_loader_result_t* result) {
    (void)result;
    return ERR_AGAIN;
}

const char* process_signal_name(uint32_t signal_number) {
    (void)signal_number;
    return "signal";
}

int shell_dispatch_execute(const char* input) {
    (void)input;
    return OK;
}

int shell_hosted_open(void) {
    return ERR_UNAVAILABLE;
}

void video_clear(void) {
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

static int test_init_and_mouse(void) {
    mouse_event_t event = {0};

    reset_fixture();
    shell_init();
    if (fake_input_init_calls != 1U || fake_job_reset_calls != 1U ||
        fake_hosted_reset_calls != 1U || fake_diagnostics_reset_calls != 1U) {
        return 1;
    }
    if (shell_handle_mouse(0) != 0 || shell_handle_mouse(&event) != 0) {
        return 2;
    }
    fake_terminal_active = 1;
    event.event = MOUSE_EVENT_WHEEL;
    event.wheel = 1;
    if (shell_handle_mouse(&event) != HOST_SHELL_WHEEL_SCROLL_LINES ||
        fake_terminal_scroll_calls != 1U) return 3;
    event.wheel = 0;
    if (shell_handle_mouse(&event) != 0 || fake_terminal_scroll_calls != 1U) {
        return 4;
    }
    return 0;
}

static int test_terminal_lifecycle(void) {
    reset_fixture();
    fake_terminal_active = 1;
    shell_runtime_suspend_terminal();
    if (fake_terminal_suspend_calls != 1U) return 5;
    fake_terminal_active = 0;
    shell_runtime_suspend_terminal();
    if (fake_terminal_suspend_calls != 1U) return 6;
    shell_runtime_finish_command();
    if (fake_input_reset_calls != 1U || fake_prompt_calls != 1U) return 7;
    return 0;
}

static int test_redraw_fallback(void) {
    reset_fixture();
    fake_taskbar_config_result = 9;
    shell_handle_key(0U);
    if (fake_terminal_begin_calls != 1U || fake_taskbar_draw_calls != 1U) {
        return 8;
    }
    return 0;
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    if (!result) result = test_init_and_mouse();
    if (!result) result = test_terminal_lifecycle();
    if (!result) result = test_redraw_fallback();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        printf("SHELL_HOST_FAIL:%d\n", result);
        return result;
    }
    printf("SHELL_HOST_PASS\n");
    return 0;
}
