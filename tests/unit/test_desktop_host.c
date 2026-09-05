#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/video.h"
#include "drivers/font.h"
#include "drivers/mouse.h"
#include "drivers/vesa.h"
#include "ui/desktop.h"
#include "ui/display.h"
#include "ui/gui.h"
#include "ui/icons.h"
#include "ui/taskbar.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static vesa_mode_t fake_mode;
static display_metrics_t fake_metrics;
static tb_config_t fake_taskbar_config;
static tb_rect_t fake_taskbar_bounds;
static tb_rect_t fake_work_area;
static icon_entry_t fake_icons[ICON_DESKTOP_COUNT];
static uint32_t fake_ticks;
static uint32_t fake_draw_calls;
static uint32_t fake_frame_calls;
static uint32_t fake_text_calls;
static int fake_backbuffer;
static int fake_bitmap_result;
static int fake_taskbar_key;
static int fake_taskbar_bounds_result;
static int fake_work_area_result;
static int fake_display_result;
static int fake_gui_measure_result;

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
    printf("ZCOV_BEGIN|case=host:ui:desktop|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:ui:desktop|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:ui:desktop|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "desktop-host: falhou: %s\n", expression);
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
    fake_taskbar_bounds.x = 0;
    fake_taskbar_bounds.y = 576;
    fake_taskbar_bounds.width = 800;
    fake_taskbar_bounds.height = 24;
    fake_work_area.x = 0;
    fake_work_area.y = 0;
    fake_work_area.width = 800;
    fake_work_area.height = 576;
    fake_icons[ICON_DESKTOP_SHELL].ch = 'S';
    fake_icons[ICON_DESKTOP_SHELL].color = 0x0FU;
    fake_icons[ICON_DESKTOP_SHELL].color_selected = 0x70U;
    fake_icons[ICON_DESKTOP_EXPLORER].ch = 'E';
    fake_icons[ICON_DESKTOP_EXPLORER].color = 0x0EU;
    fake_icons[ICON_DESKTOP_EXPLORER].color_selected = 0x71U;
    fake_icons[ICON_DESKTOP_TASKMGR].ch = 'T';
    fake_icons[ICON_DESKTOP_TASKMGR].color = 0x0DU;
    fake_icons[ICON_DESKTOP_TASKMGR].color_selected = 0x72U;
    fake_ticks = 0U;
    fake_draw_calls = 0U;
    fake_frame_calls = 0U;
    fake_text_calls = 0U;
    fake_backbuffer = 0;
    fake_bitmap_result = ERR_NOT_FOUND;
    fake_taskbar_key = 0;
    fake_taskbar_bounds_result = 1;
    fake_work_area_result = 1;
    fake_display_result = OK;
    fake_gui_measure_result = OK;
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

vesa_mode_t* vesa_get_mode(void) { return &fake_mode; }
int vesa_has_backbuffer(void) { return fake_backbuffer; }
void vesa_frame_begin(void) { fake_frame_calls++; }
void vesa_frame_begin_region(uint32_t x, uint32_t y, uint32_t width,
                             uint32_t height) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    fake_frame_calls++;
}
void vesa_frame_end(void) { fake_frame_calls++; }
void vesa_flip(void) { fake_frame_calls++; }
void vesa_clear(vesa_color_t color) {
    (void)color;
    fake_draw_calls++;
}
void vesa_draw_char(int x, int y, char c, vesa_color_t color, uint32_t scale) {
    (void)x;
    (void)y;
    (void)c;
    (void)color;
    (void)scale;
    fake_draw_calls++;
}

int display_get_metrics(display_metrics_t* metrics) {
    if (!metrics) return ERR_NULL;
    if (fake_display_result != OK) return fake_display_result;
    *metrics = fake_metrics;
    return OK;
}

uint32_t display_scale_px(uint32_t base_value) {
    return base_value * fake_metrics.factor_numerator /
           fake_metrics.factor_denominator;
}

int gui_measure_scaled_text(const char* text, uint32_t* width,
                            uint32_t* height) {
    if (!text || !width || !height) return ERR_NULL;
    if (fake_gui_measure_result != OK) return fake_gui_measure_result;
    *width = 64U;
    *height = 16U;
    return OK;
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
    fake_text_calls++;
}

icon_entry_t* icons_get_desktop(icon_desktop_id_t id) {
    if (id < 0 || id >= ICON_DESKTOP_COUNT) return 0;
    return &fake_icons[id];
}

int icons_draw_desktop_bitmap_resized(icon_desktop_id_t id, int x, int y,
                                      uint32_t size) {
    (void)id;
    (void)x;
    (void)y;
    (void)size;
    return fake_bitmap_result;
}

tb_config_t* taskbar_get_config(void) { return &fake_taskbar_config; }

int taskbar_get_bounds(tb_rect_t* bounds) {
    if (!bounds || !fake_taskbar_bounds_result) return 0;
    *bounds = fake_taskbar_bounds;
    return 1;
}

int taskbar_get_work_area(tb_rect_t* area) {
    if (!area || !fake_work_area_result) return 0;
    *area = fake_work_area;
    return 1;
}

void taskbar_draw(void) { fake_draw_calls++; }

int taskbar_handle_config_key(uint8_t scancode) {
    return fake_taskbar_key && scancode == 0x40U;
}

void mouse_invalidate_cursor(void) { fake_draw_calls++; }
uint32_t timer_get_ticks(void) { return fake_ticks; }

void video_clear(void) { fake_draw_calls++; }
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
void video_put_char_at(char character, uint8_t color, int x, int y) {
    (void)character;
    (void)color;
    (void)x;
    (void)y;
    fake_draw_calls++;
}
void video_print_at(int x, int y, const char* text, uint8_t color) {
    (void)x;
    (void)y;
    (void)text;
    (void)color;
    fake_text_calls++;
}

static void test_simple_mode(void) {
    mouse_event_t event;

    reset_fixture();
    desktop_init();
    EXPECT(desktop_get_mode() == DESKTOP_MODE_SIMPLE);
    EXPECT(desktop_get_selected_app() == DESKTOP_APP_SHELL);
    EXPECT(desktop_is_active() == 0);
    EXPECT(desktop_handle_key(0x4DU) == 0);
    EXPECT(desktop_handle_mouse(0) == 0);
    EXPECT(desktop_handle_click(16, 32) == 0);
    EXPECT(desktop_set_mode(DESKTOP_MODE_MODERN) == ERR_UNAVAILABLE);
    EXPECT(desktop_set_mode((desktop_mode_t)99) == ERR_INVALID);
    EXPECT(desktop_set_mode(DESKTOP_MODE_CLASSIC) == ERR_NOT_FOUND);

    desktop_set_active(1);
    EXPECT(desktop_is_active() == 1);
    desktop_add_icon(0, DESKTOP_APP_FILE);
    for (int index = 0; index < DESKTOP_MAX_ICONS + 2; index++) {
        desktop_add_icon("Extra", DESKTOP_APP_FILE);
    }
    desktop_draw();
    desktop_draw_workspace();
    desktop_draw_icons();
    EXPECT(desktop_handle_click(16, 32) == DESKTOP_APP_SHELL);
    EXPECT(desktop_handle_click(0, 0) == 0);
    EXPECT(desktop_handle_key(0x48U) == 0);
    EXPECT(desktop_handle_key(0x50U) == 0);
    EXPECT(desktop_handle_key(0x4BU) == 0);
    EXPECT(desktop_handle_key(0x4DU) == 0);
    EXPECT(desktop_handle_key(0x1CU) == DESKTOP_APP_FILE);
    EXPECT(desktop_handle_key(0x80U) == 0);
    fake_taskbar_key = 1;
    EXPECT(desktop_handle_key(0x40U) == 1);
    fake_taskbar_key = 0;

    event.x = 0;
    event.y = 0;
    event.buttons = 0;
    event.event = MOUSE_EVENT_MOVE;
    event.changed = 0;
    event.wheel = 0;
    EXPECT(desktop_handle_mouse(&event) == 0);
    desktop_set_active(0);
    EXPECT(desktop_is_active() == 0);
}

static void test_classic_mode_and_mouse(void) {
    mouse_event_t event;

    reset_fixture();
    fake_mode.initialized = 1U;
    fake_backbuffer = 1;
    desktop_init();
    EXPECT(desktop_get_mode() == DESKTOP_MODE_CLASSIC);
    EXPECT(desktop_set_mode(DESKTOP_MODE_CLASSIC) == OK);
    desktop_set_active(1);
    desktop_draw();
    desktop_draw_workspace();
    desktop_draw_icons();
    EXPECT(fake_frame_calls > 0U);
    EXPECT(desktop_handle_click(16, 32) == 0);

    event.x = 30;
    event.y = 30;
    event.buttons = MOUSE_BTN_LEFT;
    event.event = MOUSE_EVENT_PRESS;
    event.changed = MOUSE_BTN_LEFT;
    event.wheel = 0;
    EXPECT(desktop_handle_mouse(&event) == 0);
    event.x = 40;
    event.y = 40;
    event.event = MOUSE_EVENT_MOVE;
    EXPECT(desktop_handle_mouse(&event) == 0);
    event.event = MOUSE_EVENT_RELEASE;
    event.buttons = 0;
    EXPECT(desktop_handle_mouse(&event) == 0);

    event.x = 30;
    event.y = 30;
    event.buttons = MOUSE_BTN_LEFT;
    event.event = MOUSE_EVENT_PRESS;
    event.changed = MOUSE_BTN_LEFT;
    EXPECT(desktop_handle_mouse(&event) == 0);
    fake_ticks = 10U;
    event.event = MOUSE_EVENT_RELEASE;
    event.buttons = 0;
    EXPECT(desktop_handle_mouse(&event) == 0);
    event.event = MOUSE_EVENT_PRESS;
    event.buttons = MOUSE_BTN_LEFT;
    EXPECT(desktop_handle_mouse(&event) == 0);
    fake_ticks = 20U;
    event.event = MOUSE_EVENT_RELEASE;
    event.buttons = 0;
    EXPECT(desktop_handle_mouse(&event) == DESKTOP_APP_SHELL);

    event.x = 700;
    event.y = 500;
    event.event = MOUSE_EVENT_PRESS;
    event.changed = MOUSE_BTN_LEFT;
    event.buttons = MOUSE_BTN_LEFT;
    EXPECT(desktop_handle_mouse(&event) == 0);
    EXPECT(desktop_handle_mouse(0) == 0);
    desktop_set_active(0);
    EXPECT(desktop_handle_mouse(&event) == 0);

    fake_taskbar_config.position = TB_POS_CUSTOM;
    fake_taskbar_bounds.x = 24;
    fake_taskbar_bounds.y = 24;
    fake_taskbar_bounds.width = 112;
    fake_taskbar_bounds.height = 96;
    desktop_set_active(1);
    EXPECT(desktop_set_mode(DESKTOP_MODE_CLASSIC) == OK);
    desktop_draw_workspace();
    fake_taskbar_bounds_result = 0;
    desktop_draw_workspace();
    fake_work_area_result = 0;
    desktop_draw_workspace();
    fake_work_area_result = 1;
    fake_display_result = ERR_STATE;
    desktop_draw_workspace();
    fake_display_result = OK;
    fake_mode.initialized = 0U;
    EXPECT(desktop_set_mode(DESKTOP_MODE_CLASSIC) == ERR_NOT_FOUND);
    desktop_set_active(0);
}

int main(void) {
    coverage_active = 1U;
    test_simple_mode();
    test_classic_mode_and_mouse();
    coverage_active = 0U;
    coverage_emit(OK);
    printf("desktop-host: PASS\n");
    return 0;
}
