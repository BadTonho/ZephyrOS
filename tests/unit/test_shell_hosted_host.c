#include <stdint.h>
#include <stdio.h>

#include "apps/shell.h"
#include "apps/shell_runtime.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/video.h"
#include "drivers/mouse.h"
#include "ui/desktop.h"
#include "ui/wm.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static desktop_mode_t fake_desktop_mode;
static int fake_wm_active;
static int fake_wm_focused;
static int fake_wm_register_result;
static uint8_t fake_terminal_hosted;
static uint32_t fake_terminal_draw_calls;
static uint32_t fake_terminal_present_calls;
static uint32_t fake_terminal_take_dirty_calls;
static uint32_t fake_runtime_key_calls;
static uint32_t fake_shell_mouse_calls;
static int fake_shell_mouse_result;
static uint32_t fake_reset_modifiers_calls;
static uint32_t fake_prompt_calls;
static uint32_t fake_diagnostic_calls;
static uint32_t fake_desktop_active;
static const wm_hosted_app_t* registered_app;

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
    printf("ZCOV_BEGIN|case=host:shell:hosted|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:hosted|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:hosted|value=0x%08X\n",
           (uint32_t)result);
}

static void reset_fixture(void) {
    fake_desktop_mode = DESKTOP_MODE_SIMPLE;
    fake_wm_active = 0;
    fake_wm_focused = 0;
    fake_wm_register_result = OK;
    fake_terminal_hosted = 0U;
    fake_terminal_draw_calls = 0U;
    fake_terminal_present_calls = 0U;
    fake_terminal_take_dirty_calls = 0U;
    fake_runtime_key_calls = 0U;
    fake_shell_mouse_calls = 0U;
    fake_shell_mouse_result = 0;
    fake_reset_modifiers_calls = 0U;
    fake_prompt_calls = 0U;
    fake_diagnostic_calls = 0U;
    fake_desktop_active = 0U;
    registered_app = 0;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void desktop_set_active(int active) {
    fake_desktop_active = active ? 1U : 0U;
}

desktop_mode_t desktop_get_mode(void) {
    return fake_desktop_mode;
}

int wm_is_active(void) {
    return fake_wm_active;
}

void wm_set_active(int active) {
    fake_wm_active = active ? 1 : 0;
}

int wm_register_hosted_app(const wm_hosted_app_t* app) {
    if (!app) return ERR_NULL;
    registered_app = app;
    return fake_wm_register_result;
}

int wm_is_hosted_app_focused(wm_app_type_t app_type) {
    return fake_wm_focused && app_type == WM_APP_SHELL;
}

void video_terminal_set_hosted(int hosted) {
    fake_terminal_hosted = hosted ? 1U : 0U;
}

int video_terminal_is_hosted(void) {
    return fake_terminal_hosted;
}

int video_terminal_draw(int x, int y, int width, int height) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    fake_terminal_draw_calls++;
    return OK;
}

int video_terminal_present_hosted_dirty(void) {
    fake_terminal_present_calls++;
    return OK;
}

int video_terminal_take_hosted_dirty(void) {
    fake_terminal_take_dirty_calls++;
    return 1;
}

void shell_runtime_handle_terminal_key(uint8_t scancode) {
    (void)scancode;
    fake_runtime_key_calls++;
}

void shell_input_reset_modifiers(void) {
    fake_reset_modifiers_calls++;
}

int shell_handle_mouse(mouse_event_t* event) {
    (void)event;
    fake_shell_mouse_calls++;
    return fake_shell_mouse_result;
}

void shell_diagnostics_print_usb_fixture_report(void) {
    fake_diagnostic_calls++;
}

void shell_print_prompt(void) {
    fake_prompt_calls++;
}

static int test_unavailable_mode(void) {
    reset_fixture();
    shell_hosted_reset();
    if (shell_runtime_is_hosted_visible() != 0 ||
        shell_hosted_open() != ERR_UNAVAILABLE || fake_wm_active) return 1;
    return 0;
}

static int test_callbacks_and_close(void) {
    mouse_event_t event = {0};

    reset_fixture();
    fake_desktop_mode = DESKTOP_MODE_CLASSIC;
    fake_wm_focused = 1;
    if (shell_hosted_open() != OK || !registered_app || !fake_wm_active ||
        !fake_terminal_hosted || !shell_runtime_is_hosted_visible() ||
        fake_diagnostic_calls != 1U || fake_prompt_calls != 1U) return 2;
    if (shell_hosted_open() != OK || fake_diagnostic_calls != 1U ||
        fake_prompt_calls != 1U) return 3;

    registered_app->on_draw(1, 2, 3, 4);
    if (fake_terminal_draw_calls != 1U) return 4;
    registered_app->on_key(0x1EU);
    if (fake_runtime_key_calls != 1U || fake_terminal_present_calls != 1U) {
        return 5;
    }
    fake_wm_focused = 0;
    registered_app->on_key(0x30U);
    if (fake_runtime_key_calls != 2U || fake_terminal_present_calls != 1U) {
        return 6;
    }
    if (registered_app->on_mouse(0, 0, 0, 0, 0) != 0 ||
        fake_shell_mouse_calls != 0U) return 7;
    if (registered_app->on_mouse(&event, 0, 0, 0, 0) != 0 ||
        fake_shell_mouse_calls != 1U) return 8;
    fake_shell_mouse_result = 1;
    if (registered_app->on_mouse(&event, 0, 0, 0, 0) != 1 ||
        fake_shell_mouse_calls != 2U || fake_terminal_take_dirty_calls != 1U) {
        return 9;
    }
    registered_app->on_close();
    if (fake_reset_modifiers_calls != 1U || shell_runtime_is_hosted_visible()) {
        return 10;
    }
    return 0;
}

static int test_registration_failure(void) {
    reset_fixture();
    fake_desktop_mode = DESKTOP_MODE_CLASSIC;
    fake_wm_register_result = ERR_MEM;
    shell_hosted_reset();
    if (shell_hosted_open() != ERR_MEM || fake_wm_active ||
        fake_terminal_hosted || fake_desktop_active) return 11;
    return 0;
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    if (!result) result = test_unavailable_mode();
    if (!result) result = test_callbacks_and_close();
    if (!result) result = test_registration_failure();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        printf("SHELL_HOSTED_HOST_FAIL:%d\n", result);
        return result;
    }
    printf("SHELL_HOSTED_HOST_PASS\n");
    return 0;
}
