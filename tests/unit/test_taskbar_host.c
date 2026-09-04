#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "drivers/vesa.h"
#include "ui/desktop.h"
#include "ui/display.h"
#include "ui/gui.h"
#include "ui/taskbar.h"

#define HOST_COVERAGE_CAPACITY 2048U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static vesa_mode_t fake_mode;
static display_metrics_t fake_metrics;
static desktop_mode_t fake_desktop_mode;
static int fake_desktop_active;
static int fake_backbuffer;
static int fake_display_result;
static uint32_t fake_ticks;
static uint32_t fake_draw_calls;
static uint32_t fake_text_calls;
static uint32_t fake_frame_calls;

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
    printf("ZCOV_BEGIN|case=host:ui:taskbar|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:ui:taskbar|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:ui:taskbar|value=0x%08X\n",
           (uint32_t)result);
}

static void reset_fixture(void) {
    fake_mode.width = 800U;
    fake_mode.height = 600U;
    fake_mode.bpp = 32U;
    fake_mode.pitch = 3200U;
    fake_mode.framebuffer = NULL;
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
    fake_desktop_mode = DESKTOP_MODE_SIMPLE;
    fake_desktop_active = 0;
    fake_backbuffer = 0;
    fake_display_result = OK;
    fake_ticks = 0U;
    fake_draw_calls = 0U;
    fake_text_calls = 0U;
    fake_frame_calls = 0U;
    taskbar_init();
    taskbar_set_position(TB_POS_BOTTOM);
    taskbar_set_icon_size(TB_SIZE_MEDIUM);
    taskbar_set_pinned(1);
}

static int check(int condition) {
    return condition ? OK : ERR_STATE;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

vesa_mode_t* vesa_get_mode(void) { return &fake_mode; }
int vesa_has_backbuffer(void) { return fake_backbuffer; }
void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                    vesa_color_t color) {
    (void)x; (void)y; (void)width; (void)height; (void)color;
    fake_draw_calls++;
}
void vesa_frame_begin(void) { fake_frame_calls++; }
void vesa_frame_begin_region(uint32_t x, uint32_t y, uint32_t width,
                             uint32_t height) {
    (void)x; (void)y; (void)width; (void)height;
    fake_frame_calls++;
}
void vesa_frame_end(void) { fake_frame_calls++; }

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

desktop_mode_t desktop_get_mode(void) { return fake_desktop_mode; }
int desktop_is_active(void) { return fake_desktop_active; }
void desktop_draw(void) { fake_draw_calls++; }

void mouse_invalidate_cursor(void) { fake_draw_calls++; }
uint32_t timer_get_ticks(void) { return fake_ticks; }

void gui_draw_scaled_text(uint32_t x, uint32_t y, const char* text,
                          uint32_t color) {
    (void)x; (void)y; (void)text; (void)color;
    fake_text_calls++;
}
void gui_draw_flat_border(uint32_t x, uint32_t y, uint32_t width,
                          uint32_t height, uint32_t color) {
    (void)x; (void)y; (void)width; (void)height; (void)color;
    fake_draw_calls++;
}
void gui_draw_modern_button(uint32_t x, uint32_t y, uint32_t width,
                            uint32_t height, const char* text,
                            gui_button_state_t state) {
    (void)x; (void)y; (void)width; (void)height; (void)text; (void)state;
    fake_draw_calls++;
}
void gui_draw_rounded_rect(uint32_t x, uint32_t y, uint32_t width,
                           uint32_t height, uint32_t radius, uint32_t color) {
    (void)x; (void)y; (void)width; (void)height; (void)radius; (void)color;
    fake_draw_calls++;
}

void video_fill_rect(int x, int y, int width, int height, char character,
                     uint8_t color) {
    (void)x; (void)y; (void)width; (void)height; (void)character; (void)color;
    fake_draw_calls++;
}
void video_draw_box(int x, int y, int width, int height, uint8_t color) {
    (void)x; (void)y; (void)width; (void)height; (void)color;
    fake_draw_calls++;
}
void video_draw_hline(int x, int y, int width, char character,
                      uint8_t color) {
    (void)x; (void)y; (void)width; (void)character; (void)color;
    fake_draw_calls++;
}
void video_put_char_at(char character, uint8_t color, int x, int y) {
    (void)character; (void)color; (void)x; (void)y;
    fake_text_calls++;
}
void video_print_at(int x, int y, const char* text, uint8_t color) {
    (void)x; (void)y; (void)text; (void)color;
    fake_text_calls++;
}

static int check_layouts(void) {
    tb_rect_t bounds;
    tb_rect_t area;
    tb_config_t* config;

    reset_fixture();
    if (check(taskbar_get_bounds(NULL) == 0) != OK) return 1;
    if (check(taskbar_get_work_area(NULL) == 0) != OK) return 2;
    if (check(taskbar_get_bounds(&bounds) == 0) != OK) return 3;
    fake_desktop_mode = DESKTOP_MODE_CLASSIC;
    fake_backbuffer = 1;
    if (check(taskbar_get_bounds(&bounds) == 1) != OK) return 4;
    if (check(bounds.y == 576 && bounds.width == 800 && bounds.height == 24) != OK) {
        return 5;
    }
    if (check(taskbar_get_work_area(&area) == 1 && area.height == 576) != OK) {
        return 6;
    }

    taskbar_set_position(TB_POS_TOP);
    if (check(taskbar_get_bounds(&bounds) == 1 && bounds.y == 0) != OK) return 7;
    if (check(taskbar_get_work_area(&area) == 1 && area.y == 24) != OK) return 8;
    taskbar_set_position(TB_POS_LEFT);
    if (check(taskbar_get_bounds(&bounds) == 1 && bounds.width == 96 &&
              bounds.height == 600) != OK) {
        return 9;
    }
    if (check(taskbar_get_work_area(&area) == 1 && area.x == 96) != OK) return 10;
    taskbar_set_position(TB_POS_RIGHT);
    if (check(taskbar_get_bounds(&bounds) == 1 && bounds.x == 704) != OK) return 11;
    if (check(taskbar_get_work_area(&area) == 1 && area.width == 704) != OK) return 12;
    taskbar_set_custom_position(100, 20);
    config = taskbar_get_config();
    if (check(config && config->position == TB_POS_CUSTOM && config->custom_x == 100 &&
              config->custom_y == 20 && config->width == 40) != OK) return 13;
    if (check(taskbar_get_bounds(&bounds) == 1 && bounds.x == 800 - 320) != OK) return 14;
    fake_display_result = ERR_UNAVAILABLE;
    if (check(taskbar_get_bounds(&bounds) == 0 && taskbar_get_work_area(&area) == 0) != OK) {
        return 15;
    }
    taskbar_set_position((tb_position_t)-1);
    taskbar_set_icon_size((tb_icon_size_t)-1);
    taskbar_set_icon_size((tb_icon_size_t)99);
    return 0;
}

static int check_buttons(void) {
    reset_fixture();
    taskbar_add_app(TB_APP_SHELL, "Shell");
    taskbar_add_app(TB_APP_EXPLORER, "Explorer");
    taskbar_add_app(TB_APP_TASKMGR, "TaskMgr");
    taskbar_add_app(TB_APP_DESKTOP, "Desktop");
    taskbar_add_app(TB_APP_NONE, "None");
    taskbar_add_app(TB_APP_NONE, "None");
    taskbar_add_app(TB_APP_WINDOW, "WindowApp");
    taskbar_add_window(10, "One");
    taskbar_add_window(11, "Two");
    taskbar_add_window(12, "Three");
    taskbar_add_window(13, "Four");
    taskbar_add_window(-1, "Invalid");
    taskbar_add_window(14, NULL);
    taskbar_remove_app(TB_APP_EXPLORER);
    taskbar_remove_app((tb_app_type_t)99);
    taskbar_add_app(TB_APP_EXPLORER, "Explorer");
    taskbar_remove_window(11);
    taskbar_remove_window(999);
    taskbar_set_window_active(12, 0);
    taskbar_set_window_active(12, 1);
    taskbar_set_window_active(999, 1);
    if (check(taskbar_take_window_request() == -1) != OK) return 1;
    return 0;
}

static int check_tui(void) {
    reset_fixture();
    taskbar_set_position(TB_POS_BOTTOM);
    taskbar_draw();
    fake_ticks = 50U;
    taskbar_update_clock();
    taskbar_draw_config_menu();
    if (check(taskbar_is_menu_open() == 1) != OK) return 1;
    if (check(taskbar_handle_config_key(0x80) == 0) != OK) return 2;
    taskbar_handle_config_key(0x48);
    taskbar_handle_config_key(0x50);
    for (int index = 0; index < 8; index++) {
        taskbar_handle_config_key(0x1C);
        taskbar_handle_config_key(0x50);
    }
    if (check(taskbar_handle_config_key(0x01) == 9) != OK) return 3;
    if (check(taskbar_handle_key(0x80) == 0) != OK) return 4;
    if (check(taskbar_handle_key(0x5B) == 1 && taskbar_is_menu_open() == 1) != OK) {
        return 5;
    }
    for (int index = 0; index < 9; index++) {
        if (index > 0) taskbar_handle_key(0x50);
        if (check(taskbar_handle_key(0x1C) >= 1) != OK) return 6;
        taskbar_handle_key(0x5B);
    }
    if (check(taskbar_handle_key(0x01) == 9) != OK) return 7;
    taskbar_set_position(TB_POS_BOTTOM);
    if (check(taskbar_handle_click(0, 0) == 0) != OK) return 8;
    if (check(taskbar_handle_click(16, 16 * 47) == 1) != OK) return 9;
    if (check(taskbar_handle_click(16, 16 * 19) == 7) != OK) return 10;
    taskbar_handle_key(0x5B);
    if (check(taskbar_handle_click(200, 200) == 1) != OK) return 11;
    taskbar_set_position(TB_POS_LEFT);
    taskbar_draw();
    taskbar_set_position(TB_POS_RIGHT);
    taskbar_draw();
    return check(fake_draw_calls > 0U && fake_text_calls > 0U);
}

static int check_gui(void) {
    tb_rect_t bounds;

    reset_fixture();
    fake_desktop_mode = DESKTOP_MODE_CLASSIC;
    fake_backbuffer = 1;
    fake_desktop_active = 1;
    taskbar_add_window(77, "Window");
    taskbar_draw();
    fake_ticks = 100U;
    taskbar_update_clock();
    taskbar_handle_key(0x5B);
    if (check(taskbar_is_menu_open() == 1) != OK) return 1;
    if (check(taskbar_handle_key(0x50) == 1) != OK) return 2;
    if (check(taskbar_handle_key(0x1C) == 2) != OK) return 3;
    if (check(taskbar_handle_click(0, 0) == 0) != OK) return 4;
    if (check(taskbar_get_bounds(&bounds) == 1) != OK) return 5;
    if (check(taskbar_handle_click(10, bounds.y + 4) == 1) != OK) return 6;
    taskbar_handle_key(0x01);
    if (check(taskbar_handle_click(170, bounds.y + 4) == TB_ACTION_WINDOW) != OK) {
        return 7;
    }
    if (check(taskbar_take_window_request() == 77) != OK) return 8;
    taskbar_handle_key(0x3B);
    taskbar_handle_config_key(0x50);
    taskbar_handle_config_key(0x1C);
    taskbar_handle_config_key(0x01);
    taskbar_set_position(TB_POS_TOP);
    taskbar_handle_key(0x5B);
    taskbar_handle_key(0x01);
    taskbar_set_position(TB_POS_LEFT);
    taskbar_handle_key(0x5B);
    taskbar_handle_key(0x01);
    taskbar_set_position(TB_POS_RIGHT);
    taskbar_handle_key(0x5B);
    taskbar_handle_key(0x01);
    taskbar_set_custom_position(0, 0);
    taskbar_handle_key(0x5B);
    taskbar_handle_key(0x01);
    return check(fake_frame_calls > 0U && fake_text_calls > 0U);
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_layouts();
    if (!result) result = check_buttons();
    if (!result) result = check_tui();
    if (!result) result = check_gui();
    coverage_active = 0U;
    coverage_emit(result);
    return result == OK ? 0 : result;
}
