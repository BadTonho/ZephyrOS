#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/timer.h"
#include "core/video.h"
#include "drivers/mouse.h"
#include "drivers/vesa.h"
#include "ui/desktop.h"
#include "ui/display.h"
#include "ui/gui.h"
#include "ui/icons.h"
#include "ui/taskbar.h"
#include "ui/wm.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static vesa_mode_t fake_mode;
static display_metrics_t fake_metrics;
static tb_config_t fake_taskbar_config;
static tb_rect_t fake_work_area;
static icon_entry_t fake_wm_icons[ICON_WM_COUNT];
static uint32_t fake_ticks;
static uint32_t fake_draw_calls;
static uint32_t fake_app_draws;
static uint32_t fake_app_keys;
static uint32_t fake_app_mouse;
static uint32_t fake_app_closes;
static int fake_backbuffer;
static int fake_desktop_active;
static desktop_mode_t fake_desktop_mode;
static int fake_recovery_enabled;
static int fake_work_area_available;
static int fake_display_available;

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
    printf("ZCOV_BEGIN|case=host:ui:wm|value=0x%08X\n", coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:ui:wm|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:ui:wm|value=0x%08X\n", (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "wm-host: falhou: %s\n", expression);
        (void)fflush(stderr);
        __builtin_trap();
    }
}

#define EXPECT(expression) expect_true((expression), #expression)

static void reset_fixture(void) {
    fake_mode.width = 800U;
    fake_mode.height = 600U;
    fake_mode.bpp = 32U;
    fake_mode.pitch = 3200U;
    fake_mode.framebuffer = 0;
    fake_mode.initialized = 0U;
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
    fake_taskbar_config.position = TB_POS_BOTTOM;
    fake_taskbar_config.icon_size = TB_SIZE_MEDIUM;
    fake_taskbar_config.pinned = 1;
    fake_taskbar_config.custom_x = 0;
    fake_taskbar_config.custom_y = 0;
    fake_taskbar_config.width = 800;
    fake_taskbar_config.height = 24;
    fake_work_area.x = 0;
    fake_work_area.y = 0;
    fake_work_area.width = 800;
    fake_work_area.height = 576;
    fake_wm_icons[ICON_WM_CLOSE].ch = 'X';
    fake_wm_icons[ICON_WM_CLOSE].color = 0x0CU;
    fake_wm_icons[ICON_WM_CLOSE].color_selected = 0x04U;
    fake_wm_icons[ICON_WM_MINIMIZE].ch = '_';
    fake_wm_icons[ICON_WM_MINIMIZE].color = 0x0EU;
    fake_wm_icons[ICON_WM_MINIMIZE].color_selected = 0x06U;
    fake_wm_icons[ICON_WM_MAXIMIZE].ch = '+';
    fake_wm_icons[ICON_WM_MAXIMIZE].color = 0x0AU;
    fake_wm_icons[ICON_WM_MAXIMIZE].color_selected = 0x02U;
    fake_ticks = 0U;
    fake_draw_calls = 0U;
    fake_app_draws = 0U;
    fake_app_keys = 0U;
    fake_app_mouse = 0U;
    fake_app_closes = 0U;
    fake_backbuffer = 0;
    fake_desktop_active = 1;
    fake_desktop_mode = DESKTOP_MODE_SIMPLE;
    fake_recovery_enabled = 1;
    fake_work_area_available = 1;
    fake_display_available = 1;
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
    return component == RECOVERY_COMPONENT_WM && fake_recovery_enabled;
}

int recovery_mark_degraded(recovery_component_id_t component, int error_code,
                           const char* message) {
    (void)component;
    (void)error_code;
    (void)message;
    return OK;
}

vesa_mode_t* vesa_get_mode(void) { return &fake_mode; }
int vesa_has_backbuffer(void) { return fake_backbuffer; }
void vesa_frame_begin(void) { fake_draw_calls++; }
void vesa_frame_end(void) { fake_draw_calls++; }
void vesa_clear(vesa_color_t color) { (void)color; fake_draw_calls++; }
void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    vesa_color_t color) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
    fake_draw_calls++;
}
void vesa_draw_hline(uint32_t x, uint32_t y, uint32_t w, vesa_color_t color) {
    (void)x;
    (void)y;
    (void)w;
    (void)color;
    fake_draw_calls++;
}
void vesa_draw_vline(uint32_t x, uint32_t y, uint32_t h, vesa_color_t color) {
    (void)x;
    (void)y;
    (void)h;
    (void)color;
    fake_draw_calls++;
}
void vesa_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    vesa_color_t color) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
    fake_draw_calls++;
}
void vesa_draw_line(int x0, int y0, int x1, int y1, vesa_color_t color) {
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    (void)color;
    fake_draw_calls++;
}
void vesa_fill_circle(int cx, int cy, int r, vesa_color_t color) {
    (void)cx;
    (void)cy;
    (void)r;
    (void)color;
    fake_draw_calls++;
}
void vesa_set_clip_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}
void vesa_reset_clip_rect(void) {}

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

void gui_draw_scaled_text(uint32_t x, uint32_t y, const char* text,
                          uint32_t color) {
    (void)x;
    (void)y;
    (void)text;
    (void)color;
    fake_draw_calls++;
}

int gui_measure_scaled_text(const char* text, uint32_t* width,
                            uint32_t* height) {
    if (!text || !width || !height) return ERR_NULL;
    *width = 64U;
    *height = 16U;
    return OK;
}

void taskbar_draw(void) { fake_draw_calls++; }
void taskbar_add_window(int window_id, const char* name) {
    (void)window_id;
    (void)name;
}
void taskbar_remove_window(int window_id) { (void)window_id; }
void taskbar_set_window_active(int window_id, int active) {
    (void)window_id;
    (void)active;
}
tb_config_t* taskbar_get_config(void) { return &fake_taskbar_config; }
int taskbar_get_work_area(tb_rect_t* area) {
    if (!area || !fake_work_area_available) return 0;
    *area = fake_work_area;
    return 1;
}

icon_entry_t* icons_get_wm(icon_wm_id_t id) {
    if (id < 0 || id >= ICON_WM_COUNT) return 0;
    return &fake_wm_icons[id];
}

int desktop_is_active(void) { return fake_desktop_active; }
desktop_mode_t desktop_get_mode(void) { return fake_desktop_mode; }
void desktop_draw_workspace(void) { fake_draw_calls++; }
void desktop_draw(void) { fake_draw_calls++; }

uint32_t timer_get_ticks(void) { return fake_ticks; }
void mouse_invalidate_cursor(void) { fake_draw_calls++; }

void video_put_char_at(char character, uint8_t color, int x, int y) {
    (void)character;
    (void)color;
    (void)x;
    (void)y;
    fake_draw_calls++;
}
void video_fill_rect(int x, int y, int width, int height, char character,
                     uint8_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)character;
    (void)color;
    fake_draw_calls++;
}
void video_draw_box(int x, int y, int width, int height, uint8_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)color;
    fake_draw_calls++;
}

static void app_on_draw(int x, int y, int width, int height) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    fake_app_draws++;
}

static void app_on_key(uint8_t scancode) {
    (void)scancode;
    fake_app_keys++;
}

static int app_on_mouse(mouse_event_t* event, int x, int y, int width,
                        int height) {
    (void)event;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    fake_app_mouse++;
    return 1;
}

static void app_on_close(void) { fake_app_closes++; }

static wm_hosted_app_t hosted_app(wm_app_type_t type, const char* title) {
    wm_hosted_app_t app;

    app.app_type = type;
    app.title = title;
    app.taskbar_label = title;
    app.min_width = 180;
    app.min_height = 128;
    app.default_width = 280;
    app.default_height = 180;
    app.key_redraw = WM_KEY_REDRAW_WINDOW_MANAGER;
    app.on_draw = app_on_draw;
    app.on_key = app_on_key;
    app.on_mouse = app_on_mouse;
    app.on_close = app_on_close;
    return app;
}

static void test_classic_windows(void) {
    mouse_event_t event;
    int first_id;
    int second_id;

    reset_fixture();
    wm_init();
    EXPECT(wm_get_config() != 0);
    wm_set_btn_position(WM_BTNS_LEFT);
    wm_set_btn_order(WM_BTN_CLOSE_MAX_MIN);
    wm_set_show_title(1);
    wm_set_border_style(1);
    EXPECT(wm_create_window(0, 0, 0, 20, 10, WM_APP_SHELL,
                            app_on_key, app_on_draw) == ERR_NULL);
    EXPECT(wm_create_window("bad", 0, 0, 5, 5, WM_APP_SHELL,
                            app_on_key, app_on_draw) == ERR_INVALID);
    first_id = wm_create_window("Classic window", 2, 2, 24, 8,
                                WM_APP_SHELL, app_on_key, app_on_draw);
    second_id = wm_create_window("Second", 8, 5, 20, 7, WM_APP_EXPLORER,
                                 app_on_key, app_on_draw);
    EXPECT(first_id == 0);
    EXPECT(second_id == 1);
    EXPECT(wm_get_window(first_id) != 0);
    EXPECT(wm_get_window(-1) == 0);
    EXPECT(wm_get_focused() != 0);
    EXPECT(wm_get_focused_id() == second_id);

    wm_set_active(1);
    EXPECT(wm_is_active() == 1);
    wm_draw_desktop();
    wm_draw_window(first_id);
    wm_draw_title_bar(wm_get_window(first_id));
    wm_set_btn_position(WM_BTNS_RIGHT);
    wm_draw_title_bar(wm_get_window(first_id));
    wm_set_btn_position(WM_BTNS_LEFT);
    wm_focus_window(first_id);
    wm_focus_next();
    wm_focus_prev();
    wm_move_window(first_id, 4, 4);
    wm_resize_window(first_id, 2, 2);
    EXPECT(wm_get_window(first_id)->width == WM_MIN_WIDTH);
    EXPECT(wm_get_window(first_id)->height == WM_MIN_HEIGHT);
    wm_maximize_window(first_id);
    wm_restore_window(first_id);
    wm_minimize_window(first_id);
    wm_restore_window(first_id);
    fake_ticks = 100U;
    wm_update_cpu_stats();
    EXPECT(wm_get_window(first_id)->cpu_ticks > 0U);

    EXPECT(wm_handle_key(0x0FU) == WM_RESULT_NONE);
    EXPECT(wm_handle_key(0x3BU) == WM_RESULT_NONE);
    EXPECT(wm_handle_key(0x3CU) == WM_RESULT_NONE);
    EXPECT(wm_handle_key(0x80U) == WM_RESULT_NONE);
    EXPECT(wm_handle_key(0x20U) == WM_RESULT_NONE);
    event.x = 10;
    event.y = 10;
    event.buttons = MOUSE_BTN_LEFT;
    event.changed = MOUSE_BTN_LEFT;
    event.event = MOUSE_EVENT_PRESS;
    event.wheel = 0;
    EXPECT(wm_handle_click(10 * 8, 10 * 16) == 1);
    EXPECT(wm_handle_mouse(&event) == 1);
    event.event = MOUSE_EVENT_RELEASE;
    event.buttons = 0;
    EXPECT(wm_handle_mouse(&event) == 0);
    EXPECT(wm_handle_mouse(0) == 0);

    wm_toggle_window(first_id);
    wm_restore_window(second_id);
    wm_focus_window(second_id);
    wm_close_focused();
    wm_destroy_window(-1);
    wm_set_active(0);
    EXPECT(wm_is_active() == 0);

    reset_fixture();
    wm_init();
    for (int index = 0; index < WM_MAX_WINDOWS; index++) {
        EXPECT(wm_create_window("limit", 0, 0, WM_MIN_WIDTH, WM_MIN_HEIGHT,
                                WM_APP_CUSTOM, app_on_key, app_on_draw) == index);
    }
    EXPECT(wm_create_window("overflow", 0, 0, WM_MIN_WIDTH, WM_MIN_HEIGHT,
                            WM_APP_CUSTOM, app_on_key, app_on_draw) ==
           ERR_OVERFLOW);
    fake_recovery_enabled = 0;
    EXPECT(wm_create_window("disabled", 0, 0, WM_MIN_WIDTH, WM_MIN_HEIGHT,
                            WM_APP_CUSTOM, app_on_key, app_on_draw) == ERR_STATE);
}

static void send_mouse(int x, int y, uint8_t event_type,
                       uint8_t buttons, uint8_t changed, int wheel) {
    mouse_event_t event;

    event.x = x;
    event.y = y;
    event.event = event_type;
    event.buttons = buttons;
    event.changed = changed;
    event.wheel = wheel;
    (void)wm_handle_mouse(&event);
}

static void test_gui_windows(void) {
    wm_hosted_app_t shell;
    wm_hosted_app_t explorer;
    wm_config_t* config;
    int reflow_result;

    reset_fixture();
    fake_mode.initialized = 1U;
    fake_backbuffer = 1;
    fake_desktop_mode = DESKTOP_MODE_CLASSIC;
    wm_init();
    config = wm_get_config();
    EXPECT(config != 0);
    wm_set_active(1);
    EXPECT(wm_is_active() == 1);
    shell = hosted_app(WM_APP_SHELL, "Shell");
    explorer = hosted_app(WM_APP_EXPLORER, "Explorer");
    EXPECT(wm_register_hosted_app(0) == ERR_NULL);
    EXPECT(wm_register_hosted_app(&shell) == OK);
    EXPECT(wm_register_hosted_app(&shell) == OK);
    EXPECT(wm_is_hosted_app_focused(WM_APP_SHELL) == 1);
    EXPECT(wm_is_hosted_app_focused(WM_APP_EXPLORER) == 0);
    EXPECT(wm_register_hosted_app(&explorer) == OK);
    wm_request_hosted_redraw(WM_APP_SHELL);
    EXPECT(wm_reflow_display() == OK);

    wm_handle_key(0x38U);
    wm_handle_key(0x0FU);
    wm_handle_key(0x36U);
    wm_handle_key(0x0FU);
    wm_handle_key(0xB6U);
    wm_handle_key(0xB8U);
    wm_handle_key(0xE0U);
    wm_handle_key(0x4BU);
    wm_handle_key(0x3EU);
    wm_handle_key(0x43U);
    wm_handle_key(0x44U);
    wm_handle_key(0x38U);
    wm_handle_key(0x44U);
    wm_handle_key(0xB8U);
    wm_handle_key(0x38U);
    wm_handle_key(0x44U);
    wm_handle_key(0xB8U);
    wm_handle_key(0xAAU);
    wm_handle_key(0x2AU);
    wm_handle_key(0xAAU);
    wm_handle_key(0x20U);
    EXPECT(fake_app_keys > 0U);

    send_mouse(100, 100, MOUSE_EVENT_WHEEL, 0, 0, 1);
    send_mouse(100, 100, MOUSE_EVENT_MOVE, 0, 0, 0);
    send_mouse(100, 30, MOUSE_EVENT_PRESS, MOUSE_BTN_LEFT,
               MOUSE_BTN_LEFT, 0);
    send_mouse(180, 80, MOUSE_EVENT_MOVE, MOUSE_BTN_LEFT, 0, 0);
    send_mouse(180, 80, MOUSE_EVENT_RELEASE, 0, 0, 0);
    send_mouse(104, 100, MOUSE_EVENT_PRESS, MOUSE_BTN_LEFT,
               MOUSE_BTN_LEFT, 0);
    send_mouse(70, 100, MOUSE_EVENT_MOVE, MOUSE_BTN_LEFT, 0, 0);
    send_mouse(70, 100, MOUSE_EVENT_RELEASE, 0, 0, 0);
    send_mouse(100, 100, MOUSE_EVENT_PRESS, MOUSE_BTN_LEFT,
               MOUSE_BTN_LEFT, 0);
    send_mouse(140, 140, MOUSE_EVENT_MOVE, MOUSE_BTN_LEFT, 0, 0);
    send_mouse(140, 140, MOUSE_EVENT_RELEASE, 0, 0, 0);
    send_mouse(50, 30, MOUSE_EVENT_PRESS, MOUSE_BTN_LEFT,
               MOUSE_BTN_LEFT, 0);
    send_mouse(4, 4, MOUSE_EVENT_MOVE, MOUSE_BTN_LEFT, 0, 0);
    send_mouse(4, 4, MOUSE_EVENT_RELEASE, 0, 0, 0);
    send_mouse(24, 24, MOUSE_EVENT_PRESS, MOUSE_BTN_LEFT,
               MOUSE_BTN_LEFT, 0);
    send_mouse(40, 40, MOUSE_EVENT_MOVE, MOUSE_BTN_LEFT, 0, 0);
    send_mouse(40, 40, MOUSE_EVENT_RELEASE, 0, 0, 0);

    wm_toggle_window(100);
    wm_toggle_window(100);
    wm_request_hosted_redraw(WM_APP_EXPLORER);
    wm_update_cpu_stats();
    fake_work_area.width = 100U;
    reflow_result = wm_reflow_display();
    EXPECT(reflow_result == ERR_OVERFLOW);
    fake_work_area.width = 800U;
    fake_mode.width = 0U;
    fake_work_area_available = 0;
    reflow_result = wm_reflow_display();
    EXPECT(reflow_result == ERR_UNAVAILABLE);
    fake_mode.width = 800U;
    fake_work_area_available = 1;
    fake_mode.initialized = 0U;
    reflow_result = wm_reflow_display();
    EXPECT(reflow_result == ERR_UNAVAILABLE);
    fake_mode.initialized = 1U;
    EXPECT(wm_close_hosted_app(WM_APP_CUSTOM) == ERR_NOT_FOUND);
    if (!wm_is_active()) wm_set_active(1);
    EXPECT(wm_register_hosted_app(&shell) == OK);
    EXPECT(wm_close_hosted_app(WM_APP_SHELL) == OK);
    EXPECT(fake_app_closes > 0U);
    EXPECT(wm_close_hosted_app(WM_APP_SHELL) == ERR_NOT_FOUND);
    wm_set_active(0);
    EXPECT(wm_is_active() == 0);

    reset_fixture();
    fake_mode.initialized = 1U;
    fake_backbuffer = 1;
    fake_desktop_active = 0;
    fake_desktop_mode = DESKTOP_MODE_CLASSIC;
    wm_init();
    wm_set_active(1);
    EXPECT(wm_register_hosted_app(&explorer) == OK);
    wm_handle_key(0x38U);
    EXPECT(wm_handle_key(0x3EU) == WM_RESULT_EXIT);
    EXPECT(wm_is_active() == 0);
}

int main(void) {
    coverage_active = 1U;
    test_classic_windows();
    test_gui_windows();
    coverage_active = 0U;
    coverage_emit(OK);
    printf("wm-host: PASS\n");
    return 0;
}
