#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "drivers/vesa.h"
#include "ui/desktop.h"
#include "ui/display.h"
#include "ui/taskbar.h"
#include "ui/wm.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static vesa_mode_t fake_mode;
static uint8_t fake_mode_present;
static uint8_t fake_backbuffer;
static desktop_mode_t fake_desktop_mode;
static int fake_desktop_active;
static int fake_wm_active;
static int fake_reflow_result;
static uint32_t fake_reflow_calls;
static uint32_t fake_desktop_draw_calls;
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
    printf("ZCOV_BEGIN|case=host:gui:display|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:gui:display|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:gui:display|value=0x%08X\n",
           (uint32_t)result);
}

static void reset_fixture(void) {
    fake_mode.width = 800U;
    fake_mode.height = 600U;
    fake_mode.bpp = VESA_BPP_32;
    fake_mode.pitch = 3200U;
    fake_mode.framebuffer = 0;
    fake_mode.initialized = 1U;
    fake_mode_present = 1U;
    fake_backbuffer = 1U;
    fake_desktop_mode = DESKTOP_MODE_SIMPLE;
    fake_desktop_active = 0;
    fake_wm_active = 0;
    fake_reflow_result = OK;
    fake_reflow_calls = 0U;
    fake_desktop_draw_calls = 0U;
    fake_taskbar_draw_calls = 0U;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

vesa_mode_t* vesa_get_mode(void) {
    return fake_mode_present ? &fake_mode : 0;
}

int vesa_has_backbuffer(void) {
    return fake_backbuffer;
}

desktop_mode_t desktop_get_mode(void) {
    return fake_desktop_mode;
}

int desktop_is_active(void) {
    return fake_desktop_active;
}

void desktop_draw(void) {
    fake_desktop_draw_calls++;
}

int wm_is_active(void) {
    return fake_wm_active;
}

int wm_reflow_display(void) {
    fake_reflow_calls++;
    return fake_reflow_result;
}

void taskbar_draw(void) {
    fake_taskbar_draw_calls++;
}

static int test_uninitialized_and_names(void) {
    display_metrics_t metrics;
    display_scale_t scale;

    if (display_get_metrics(0) != ERR_NULL ||
        display_get_metrics(&metrics) != ERR_STATE ||
        display_parse_scale(0, &scale) != ERR_NULL ||
        display_parse_scale("normal", 0) != ERR_NULL ||
        display_scale_px(37U) != 37U) return 1;
    if (display_parse_scale("pequena", &scale) != OK ||
        scale != DISPLAY_SCALE_SMALL ||
        display_parse_scale("normal", &scale) != OK ||
        scale != DISPLAY_SCALE_NORMAL ||
        display_parse_scale("grande", &scale) != OK ||
        scale != DISPLAY_SCALE_LARGE ||
        display_parse_scale("invalida", &scale) != ERR_INVALID) return 2;
    if (kstrcmp(display_scale_name(DISPLAY_SCALE_SMALL), "pequena") != 0 ||
        kstrcmp(display_scale_name(DISPLAY_SCALE_NORMAL), "normal") != 0 ||
        kstrcmp(display_scale_name(DISPLAY_SCALE_LARGE), "grande") != 0 ||
        kstrcmp(display_scale_name(DISPLAY_SCALE_COUNT), "invalida") != 0) return 3;
    return 0;
}

static int test_initialization_and_limits(void) {
    display_metrics_t metrics;

    fake_backbuffer = 0U;
    if (display_init() != ERR_UNAVAILABLE ||
        display_get_metrics(&metrics) != OK || metrics.available != 0U ||
        display_apply_scale(DISPLAY_SCALE_SMALL) != ERR_OVERFLOW) return 4;
    fake_backbuffer = 1U;
    if (display_init() != OK || display_get_metrics(&metrics) != OK ||
        metrics.scale != DISPLAY_SCALE_NORMAL || !metrics.available ||
        display_apply_scale((display_scale_t)-1) != ERR_INVALID ||
        display_apply_scale(DISPLAY_SCALE_COUNT) != ERR_INVALID ||
        display_apply_scale(DISPLAY_SCALE_LARGE) != ERR_OVERFLOW) return 5;
    return 0;
}

static int test_scale_refresh_paths(void) {
    display_metrics_t metrics;

    fake_desktop_mode = DESKTOP_MODE_CLASSIC;
    fake_wm_active = 1;
    if (display_apply_scale(DISPLAY_SCALE_SMALL) != OK ||
        fake_reflow_calls != 1U || display_scale_px(10U) != 10U) return 6;
    fake_reflow_result = ERR_STATE;
    if (display_apply_scale(DISPLAY_SCALE_NORMAL) != ERR_STATE ||
        display_get_metrics(&metrics) != OK ||
        metrics.scale != DISPLAY_SCALE_SMALL || fake_reflow_calls != 3U) return 7;
    fake_reflow_result = OK;
    fake_wm_active = 0;
    fake_desktop_active = 1;
    if (display_apply_scale(DISPLAY_SCALE_NORMAL) != OK ||
        fake_desktop_draw_calls != 1U || display_scale_px(10U) != 13U) return 8;
    fake_desktop_active = 0;
    if (display_apply_scale(DISPLAY_SCALE_SMALL) != OK ||
        fake_taskbar_draw_calls != 1U) return 9;
    fake_mode.width = 1024U;
    fake_mode.height = 768U;
    if (display_apply_scale(DISPLAY_SCALE_LARGE) != OK ||
        display_scale_px(10U) != 15U) return 10;
    return 0;
}

int main(void) {
    int result = 0;

    reset_fixture();
    coverage_active = 1U;
    if (!result) result = test_uninitialized_and_names();
    if (!result) result = test_initialization_and_limits();
    if (!result) result = test_scale_refresh_paths();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        printf("DISPLAY_HOST_FAIL:%d\n", result);
        return result;
    }
    printf("DISPLAY_HOST_PASS\n");
    return 0;
}
