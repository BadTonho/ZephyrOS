#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "drivers/font.h"
#include "drivers/vesa.h"
#include "ui/display.h"
#include "ui/gui.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static vesa_mode_t fake_mode;
static display_metrics_t fake_metrics;
static font_face_t fake_face;
static uint8_t fake_glyph[FONT_HEIGHT];
static int fake_display_result;
static int fake_face_available;
static uint32_t fake_hline_calls;
static uint32_t fake_vline_calls;
static uint32_t fake_fill_calls;
static uint32_t fake_pixel_calls;
static uint32_t fake_glyph_calls;

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
    printf("ZCOV_BEGIN|case=host:gui:widgets|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:gui:widgets|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:gui:widgets|value=0x%08X\n",
           (uint32_t)result);
}

static void reset_fixture(void) {
    fake_mode.width = 320U;
    fake_mode.height = 240U;
    fake_mode.bpp = VESA_BPP_32;
    fake_mode.pitch = 1280U;
    fake_mode.framebuffer = 0;
    fake_mode.initialized = 1U;
    fake_metrics.scale = DISPLAY_SCALE_NORMAL;
    fake_metrics.factor_numerator = 1U;
    fake_metrics.factor_denominator = 1U;
    fake_metrics.font_width = FONT_WIDTH;
    fake_metrics.font_height = FONT_HEIGHT;
    fake_metrics.spacing = 8U;
    fake_metrics.taskbar_height = 20U;
    fake_metrics.taskbar_side_width = 20U;
    fake_metrics.button_min_width = 20U;
    fake_metrics.button_min_height = 20U;
    fake_metrics.icon_size = 32U;
    fake_metrics.title_bar_height = 20U;
    fake_metrics.row_height = 20U;
    fake_metrics.min_width = 320U;
    fake_metrics.min_height = 240U;
    fake_metrics.available = 1U;
    fake_face.glyphs = fake_glyph;
    fake_face.width = FONT_WIDTH;
    fake_face.height = FONT_HEIGHT;
    fake_face.row_stride = 1U;
    fake_face.glyph_stride = FONT_HEIGHT;
    fake_face.first_character = 32U;
    fake_face.last_character = 126U;
    for (uint32_t index = 0U; index < FONT_HEIGHT; index++) {
        fake_glyph[index] = 0xFFU;
    }
    fake_display_result = OK;
    fake_face_available = 1;
    fake_hline_calls = 0U;
    fake_vline_calls = 0U;
    fake_fill_calls = 0U;
    fake_pixel_calls = 0U;
    fake_glyph_calls = 0U;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

vesa_mode_t* vesa_get_mode(void) {
    return &fake_mode;
}

void vesa_put_pixel(uint32_t x, uint32_t y, vesa_color_t color) {
    (void)x;
    (void)y;
    (void)color;
    fake_pixel_calls++;
}

void vesa_draw_glyph8x16(uint32_t x, uint32_t y, const uint8_t* glyph,
                         vesa_color_t color) {
    (void)x;
    (void)y;
    (void)glyph;
    (void)color;
    fake_glyph_calls++;
}

void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                    vesa_color_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)color;
    fake_fill_calls++;
}

void vesa_draw_hline(uint32_t x, uint32_t y, uint32_t width,
                     vesa_color_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)color;
    fake_hline_calls++;
}

void vesa_draw_vline(uint32_t x, uint32_t y, uint32_t height,
                     vesa_color_t color) {
    (void)x;
    (void)y;
    (void)height;
    (void)color;
    fake_vline_calls++;
}

const uint8_t* font_get_glyph(char character) {
    return character == '?' ? 0 : fake_glyph;
}

int font_get_face(uint16_t width, uint16_t height, const font_face_t** face) {
    if (!face) return ERR_NULL;
    if (!fake_face_available || width != FONT_WIDTH || height != FONT_HEIGHT) {
        return ERR_NOT_FOUND;
    }
    *face = &fake_face;
    return OK;
}

int font_get_face_glyph(const font_face_t* face, char character,
                        const uint8_t** glyph) {
    if (!face || !glyph) return ERR_NULL;
    if (character == 'B') return ERR_NOT_FOUND;
    *glyph = fake_glyph;
    return OK;
}

int display_get_metrics(display_metrics_t* metrics) {
    if (!metrics) return ERR_NULL;
    if (fake_display_result != OK) return fake_display_result;
    *metrics = fake_metrics;
    return OK;
}

uint32_t display_scale_px(uint32_t base_value) {
    if (!fake_metrics.factor_denominator) return 0U;
    return base_value * fake_metrics.factor_numerator /
           fake_metrics.factor_denominator;
}

static int test_theme_contract(void) {
    gui_init();
    if (gui_get_theme() != GUI_THEME_MODERN_DARK ||
        gui_set_theme((gui_theme_t)-1) != ERR_INVALID ||
        gui_set_theme(GUI_THEME_CLASSIC) != ERR_UNAVAILABLE ||
        gui_set_theme(GUI_THEME_MODERN_DARK) != OK ||
        gui_get_theme() != GUI_THEME_MODERN_DARK) return 1;
    if (gui_theme_name(GUI_THEME_CLASSIC)[0] != 'C' ||
        gui_theme_name(GUI_THEME_MODERN_DARK)[0] != 'M' ||
        gui_theme_name(GUI_THEME_COUNT)[0] != 'D') return 2;
    return 0;
}

static int test_text_and_measurement(void) {
    uint32_t width = 0U;
    uint32_t height = 0U;

    gui_draw_text(4U, 4U, 0, GUI_COLOR_TEXT);
    gui_draw_text(4U, 4U, "A\n?B", GUI_COLOR_TEXT);
    gui_draw_scaled_text(4U, 4U, 0, GUI_COLOR_TEXT);
    gui_draw_scaled_text(4U, 4U, "A\nB?", GUI_COLOR_TEXT);
    if (gui_measure_scaled_text(0, &width, &height) != ERR_NULL ||
        gui_measure_scaled_text("A", 0, &height) != ERR_NULL ||
        gui_measure_scaled_text("A", &width, 0) != ERR_NULL ||
        gui_measure_scaled_text("AB\nC", &width, &height) != OK ||
        width != 16U || height != 32U) return 3;

    fake_face_available = 0;
    gui_draw_scaled_text(4U, 4U, "AC", GUI_COLOR_TEXT);
    fake_metrics.available = 0U;
    gui_draw_scaled_text(4U, 4U, "A", GUI_COLOR_TEXT);
    fake_metrics.available = 1U;
    fake_display_result = ERR_STATE;
    if (gui_measure_scaled_text("A", &width, &height) != ERR_STATE) return 4;
    gui_draw_scaled_text(4U, 4U, "A", GUI_COLOR_TEXT);
    fake_display_result = OK;
    fake_face_available = 1;
    return 0;
}

static int test_shapes_and_controls(void) {
    gui_draw_panel(0U, 0U, 24U, 24U, GUI_COLOR_BG, 0);
    gui_draw_panel(0U, 0U, 2U, 2U, GUI_COLOR_BG, 1);
    gui_draw_rounded_rect(4U, 4U, 30U, 20U, 0U, GUI_MODERN_COLOR_WINDOW);
    gui_draw_rounded_rect(4U, 4U, 30U, 20U, 5U, GUI_MODERN_COLOR_WINDOW);
    gui_draw_flat_border(2U, 2U, 20U, 1U, GUI_COLOR_TEXT);
    gui_draw_flat_border(2U, 2U, 1U, 20U, GUI_COLOR_TEXT);
    gui_draw_flat_border(2U, 2U, 20U, 20U, GUI_COLOR_TEXT);
    gui_draw_vertical_gradient(2U, 2U, 20U, 1U, 0x00000000U, 0x00FFFFFFU);
    gui_draw_vertical_gradient(2U, 2U, 20U, 3U, 0x00000000U, 0x00FFFFFFU);
    gui_draw_button(4U, 4U, 40U, 20U, "OK", 0);
    gui_draw_button(4U, 4U, 40U, 20U, 0, 1);
    gui_draw_scaled_button(4U, 4U, 40U, 24U, "OK", 0);
    gui_draw_scaled_button(4U, 4U, 4U, 4U, "LONG", 1);
    gui_draw_scaled_button(4U, 4U, 40U, 24U, 0, 0);
    gui_draw_modern_button(4U, 4U, 50U, 24U, "OK", GUI_BUTTON_STATE_NORMAL);
    gui_draw_modern_button(4U, 4U, 50U, 24U, "OK", GUI_BUTTON_STATE_HOVER);
    gui_draw_modern_button(4U, 4U, 50U, 24U, "OK", GUI_BUTTON_STATE_PRESSED);
    gui_draw_modern_button(4U, 4U, 50U, 24U, 0, GUI_BUTTON_STATE_NORMAL);
    gui_draw_modern_button(4U, 4U, 50U, 24U, "TOO LONG", GUI_BUTTON_STATE_COUNT);
    if (!fake_hline_calls || !fake_vline_calls || !fake_fill_calls ||
        !fake_pixel_calls || !fake_glyph_calls) return 5;
    return 0;
}

static int test_window_paths(void) {
    gui_draw_scaled_window_frame(0U, 0U, 4U, 4U, "Tiny", 1);
    gui_draw_scaled_window_frame(10U, 10U, 100U, 80U, "Scaled", 1);
    gui_draw_scaled_window_frame(10U, 10U, 100U, 80U, 0, 0);
    fake_display_result = ERR_STATE;
    gui_draw_scaled_window_frame(10U, 10U, 100U, 80U, "Unavailable", 0);
    fake_display_result = OK;
    gui_draw_window_frame(0U, 0U, 4U, 4U, "Tiny", 0);
    gui_draw_window_frame(10U, 10U, 100U, 80U, "Window", 1);
    gui_draw_window_frame(10U, 10U, 100U, 80U, 0, 0);
    fake_mode.initialized = 0U;
    gui_draw_panel(0U, 0U, 20U, 20U, GUI_COLOR_BG, 0);
    fake_mode.initialized = 1U;
    return 0;
}

int main(void) {
    int result = 0;

    reset_fixture();
    coverage_active = 1U;
    if (!result) result = test_theme_contract();
    if (!result) result = test_text_and_measurement();
    if (!result) result = test_shapes_and_controls();
    if (!result) result = test_window_paths();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        printf("GUI_HOST_FAIL:%d\n", result);
        return result;
    }
    printf("GUI_HOST_PASS\n");
    return 0;
}
