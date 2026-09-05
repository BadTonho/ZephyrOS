#include <stdint.h>
#include <stdio.h>

#include "apps/guitest.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/video.h"
#include "drivers/mouse.h"
#include "drivers/speaker.h"
#include "drivers/vesa.h"
#include "ui/desktop.h"
#include "ui/display.h"
#include "ui/gui.h"
#include "ui/taskbar.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static vesa_mode_t fake_mode;
static display_metrics_t fake_metrics;
static tb_rect_t fake_work_area;
static int fake_guitest_enabled;
static int fake_vesa_enabled;
static int fake_display_available;
static int fake_work_area_available;
static desktop_mode_t fake_desktop_mode;
static uint32_t fake_draw_calls;
static uint32_t fake_beeps;

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
    printf("ZCOV_BEGIN|case=host:ui:guitest|value=0x%08X\n", coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:ui:guitest|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:ui:guitest|value=0x%08X\n", (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "guitest-host: falhou: %s\n", expression);
        (void)fflush(stderr);
        __builtin_trap();
    }
}

#define EXPECT(expression) expect_true((expression), #expression)

static void reset_fixture(void) {
    if (guitest_is_active()) guitest_close();
    fake_mode.width = 800U;
    fake_mode.height = 600U;
    fake_mode.bpp = 32U;
    fake_mode.pitch = 3200U;
    fake_mode.framebuffer = 0;
    fake_mode.initialized = 1U;
    fake_metrics.scale = DISPLAY_SCALE_NORMAL;
    fake_metrics.factor_numerator = 1U;
    fake_metrics.factor_denominator = 1U;
    fake_metrics.font_width = 8U;
    fake_metrics.font_height = 16U;
    fake_metrics.spacing = 4U;
    fake_metrics.taskbar_height = 24U;
    fake_metrics.taskbar_side_width = 96U;
    fake_metrics.button_min_width = 64U;
    fake_metrics.button_min_height = 20U;
    fake_metrics.icon_size = 32U;
    fake_metrics.title_bar_height = 20U;
    fake_metrics.row_height = 18U;
    fake_metrics.min_width = 320U;
    fake_metrics.min_height = 200U;
    fake_metrics.available = 1U;
    fake_work_area.x = 0;
    fake_work_area.y = 0;
    fake_work_area.width = 800;
    fake_work_area.height = 576;
    fake_guitest_enabled = 1;
    fake_vesa_enabled = 1;
    fake_display_available = 1;
    fake_work_area_available = 1;
    fake_desktop_mode = DESKTOP_MODE_CLASSIC;
    fake_draw_calls = 0U;
    fake_beeps = 0U;
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

int recovery_is_enabled(recovery_component_id_t component) {
    if (component == RECOVERY_COMPONENT_GUITEST) return fake_guitest_enabled;
    if (component == RECOVERY_COMPONENT_VESA) return fake_vesa_enabled;
    return 1;
}

vesa_mode_t* vesa_get_mode(void) { return &fake_mode; }
void vesa_frame_begin(void) { fake_draw_calls++; }
void vesa_frame_end(void) { fake_draw_calls++; }
void vesa_set_clip_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    fake_draw_calls++;
}
void vesa_reset_clip_rect(void) { fake_draw_calls++; }

void video_clear(void) { fake_draw_calls++; }
void mouse_invalidate_cursor(void) { fake_draw_calls++; }
void shell_print_prompt(void) { fake_draw_calls++; }
void taskbar_draw(void) { fake_draw_calls++; }
void speaker_beep(uint32_t frequency, uint32_t duration_ms) {
    (void)frequency;
    (void)duration_ms;
    fake_beeps++;
}

int display_get_metrics(display_metrics_t* metrics) {
    if (!metrics) return ERR_NULL;
    if (!fake_display_available) return ERR_UNAVAILABLE;
    *metrics = fake_metrics;
    return OK;
}

uint32_t display_scale_px(uint32_t base_value) {
    return base_value * fake_metrics.factor_numerator /
           fake_metrics.factor_denominator;
}

int taskbar_get_work_area(tb_rect_t* area) {
    if (!area) return 0;
    if (!fake_work_area_available) return 0;
    *area = fake_work_area;
    return 1;
}

desktop_mode_t desktop_get_mode(void) { return fake_desktop_mode; }

gui_theme_t gui_get_theme(void) { return GUI_THEME_MODERN_DARK; }
const char* gui_theme_name(gui_theme_t theme) {
    return theme == GUI_THEME_CLASSIC ? "Classic" : "Modern Dark";
}

void gui_draw_button(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     const char* text, int pressed) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)text;
    (void)pressed;
    fake_draw_calls++;
}

void gui_draw_flat_border(uint32_t x, uint32_t y, uint32_t width,
                          uint32_t height, uint32_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)color;
    fake_draw_calls++;
}

void gui_draw_modern_button(uint32_t x, uint32_t y, uint32_t width,
                            uint32_t height, const char* text,
                            gui_button_state_t state) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)text;
    (void)state;
    fake_draw_calls++;
}

void gui_draw_rounded_rect(uint32_t x, uint32_t y, uint32_t width,
                           uint32_t height, uint32_t radius, uint32_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)radius;
    (void)color;
    fake_draw_calls++;
}

void gui_draw_scaled_text(uint32_t x, uint32_t y, const char* text,
                          uint32_t color) {
    (void)x;
    (void)y;
    (void)text;
    (void)color;
    fake_draw_calls++;
}

void gui_draw_scaled_window_frame(uint32_t x, uint32_t y, uint32_t w,
                                  uint32_t h, const char* title, int active) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)title;
    (void)active;
    fake_draw_calls++;
}

void gui_draw_text(uint32_t x, uint32_t y, const char* text, uint32_t color) {
    (void)x;
    (void)y;
    (void)text;
    (void)color;
    fake_draw_calls++;
}

void gui_draw_vertical_gradient(uint32_t x, uint32_t y, uint32_t width,
                                uint32_t height, uint32_t top_color,
                                uint32_t bottom_color) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)top_color;
    (void)bottom_color;
    fake_draw_calls++;
}

void gui_draw_window_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           const char* title, int active) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)title;
    (void)active;
    fake_draw_calls++;
}

static mouse_event_t mouse_event(int x, int y, uint8_t event, uint8_t buttons,
                                 uint8_t changed) {
    mouse_event_t result;

    result.x = x;
    result.y = y;
    result.event = event;
    result.buttons = buttons;
    result.changed = changed;
    result.wheel = 0;
    return result;
}

static void test_classic(void) {
    mouse_event_t event;

    reset_fixture();
    guitest_open();
    EXPECT(guitest_is_active());
    guitest_draw();
    guitest_handle_key(0x20U);
    guitest_handle_mouse(NULL);
    event = mouse_event(260, 210, MOUSE_EVENT_PRESS, MOUSE_BTN_LEFT,
                        MOUSE_BTN_LEFT);
    guitest_handle_mouse(&event);
    event = mouse_event(260, 210, MOUSE_EVENT_RELEASE, 0, MOUSE_BTN_LEFT);
    guitest_handle_mouse(&event);
    EXPECT(fake_beeps == 1U);
    event = mouse_event(580, 153, MOUSE_EVENT_RELEASE, 0, MOUSE_BTN_LEFT);
    guitest_handle_mouse(&event);
    EXPECT(!guitest_is_active());
    guitest_handle_key(0x01U);
    guitest_handle_mouse(&event);
}

static void test_modern(void) {
    mouse_event_t event;

    reset_fixture();
    guitest_open_modern();
    EXPECT(guitest_is_active());
    event = mouse_event(400, 400, MOUSE_EVENT_PRESS, MOUSE_BTN_LEFT,
                        MOUSE_BTN_LEFT);
    guitest_handle_mouse(&event);
    event = mouse_event(550, 510, MOUSE_EVENT_PRESS, MOUSE_BTN_LEFT,
                        MOUSE_BTN_LEFT);
    guitest_handle_mouse(&event);
    event = mouse_event(550, 510, MOUSE_EVENT_MOVE, MOUSE_BTN_LEFT, 0);
    guitest_handle_mouse(&event);
    event = mouse_event(400, 400, MOUSE_EVENT_MOVE, MOUSE_BTN_LEFT, 0);
    guitest_handle_mouse(&event);
    event = mouse_event(400, 400, MOUSE_EVENT_RELEASE, 0, MOUSE_BTN_LEFT);
    guitest_handle_mouse(&event);
    event = mouse_event(550, 510, MOUSE_EVENT_PRESS, MOUSE_BTN_LEFT,
                        MOUSE_BTN_LEFT);
    guitest_handle_mouse(&event);
    event = mouse_event(550, 510, MOUSE_EVENT_RELEASE, 0, MOUSE_BTN_LEFT);
    guitest_handle_mouse(&event);
    event = mouse_event(550, 510, MOUSE_EVENT_MOVE, 0, 0);
    guitest_handle_mouse(&event);
    event = mouse_event(400, 400, MOUSE_EVENT_MOVE, 0, 0);
    guitest_handle_mouse(&event);
    event = mouse_event(770, 10, MOUSE_EVENT_RELEASE, 0, MOUSE_BTN_LEFT);
    guitest_handle_mouse(&event);
    EXPECT(!guitest_is_active());

    guitest_open_modern();
    EXPECT(guitest_is_active());
    guitest_handle_key(0x01U);
    EXPECT(!guitest_is_active());
}

static void test_unavailable_paths(void) {
    reset_fixture();
    fake_guitest_enabled = 0;
    guitest_open();
    EXPECT(!guitest_is_active());
    fake_guitest_enabled = 1;
    fake_vesa_enabled = 0;
    guitest_open();
    EXPECT(!guitest_is_active());
    fake_vesa_enabled = 1;
    fake_mode.initialized = 0U;
    guitest_open_modern();
    EXPECT(!guitest_is_active());
    fake_mode.initialized = 1U;
    fake_display_available = 0;
    guitest_open_modern();
    EXPECT(!guitest_is_active());
    fake_display_available = 1;
    fake_work_area_available = 0;
    guitest_open_modern();
    EXPECT(!guitest_is_active());
    fake_work_area_available = 1;
    fake_desktop_mode = DESKTOP_MODE_SIMPLE;
    guitest_open_modern();
    EXPECT(!guitest_is_active());
    fake_desktop_mode = DESKTOP_MODE_CLASSIC;
    fake_mode.width = 40U;
    fake_work_area.width = 40;
    guitest_open_modern();
    EXPECT(!guitest_is_active());

    reset_fixture();
    guitest_open_modern();
    EXPECT(guitest_is_active());
    fake_guitest_enabled = 0;
    guitest_draw();
    EXPECT(!guitest_is_active());
}

int main(void) {
    coverage_active = 1U;
    test_classic();
    test_modern();
    test_unavailable_paths();
    coverage_active = 0U;
    coverage_emit(OK);
    printf("guitest-host: PASS\n");
    return 0;
}
