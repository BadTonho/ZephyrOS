#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/video.h"
#include "video_test.h"
#include "drivers/font.h"
#include "drivers/vesa.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_TERMINAL_BUFFER_SIZE 16384U
#define HOST_FRAME_WIDTH 1024U
#define HOST_FRAME_HEIGHT 768U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static vesa_mode_t fake_mode;
static uint8_t fake_backbuffer;
static uint8_t fake_mode_available;
static uint8_t fake_glyph[FONT_HEIGHT];
static uint32_t fake_fill_calls;
static uint32_t fake_pixel_calls;
static uint32_t fake_glyph_calls;
static uint32_t fake_region_calls;
static uint32_t fake_frame_begin_calls;
static uint32_t fake_frame_end_calls;
static uint32_t fake_flip_calls;
static uint32_t fake_cursor_x;
static uint32_t fake_cursor_y;
static uint32_t fake_mouse_invalidations;

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
    printf("ZCOV_BEGIN|case=host:drivers:video|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:video|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:video|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "video-host: falhou: %s\n", expression);
        (void)fflush(stderr);
        __builtin_trap();
    }
}

#define EXPECT(expression) expect_true((expression), #expression)

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void mouse_invalidate_cursor(void) {
    fake_mouse_invalidations++;
}

void video_host_update_cursor(int x, int y) {
    fake_cursor_x = (uint32_t)x;
    fake_cursor_y = (uint32_t)y;
}

vesa_mode_t* vesa_get_mode(void) {
    return fake_mode_available ? &fake_mode : 0;
}

int vesa_has_backbuffer(void) {
    return fake_backbuffer ? 1 : 0;
}

void vesa_clear(vesa_color_t color) {
    (void)color;
    fake_fill_calls++;
}

void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t width,
                    uint32_t height, vesa_color_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)color;
    fake_fill_calls++;
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

void vesa_frame_mark_region(uint32_t x, uint32_t y, uint32_t width,
                            uint32_t height) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    fake_region_calls++;
}

void vesa_frame_begin_region(uint32_t x, uint32_t y, uint32_t width,
                             uint32_t height) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    fake_frame_begin_calls++;
}

void vesa_frame_end(void) {
    fake_frame_end_calls++;
}

void vesa_flip(void) {
    fake_flip_calls++;
}

void vesa_flip_region(uint32_t x, uint32_t y, uint32_t width,
                      uint32_t height) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    fake_flip_calls++;
}

void vesa_disable(void) {
    fake_mode.initialized = 0U;
    fake_backbuffer = 0U;
}

const uint8_t* font_get_glyph(char character) {
    if (character == '?') return 0;
    return fake_glyph;
}

static void reset_fixture(void) {
    memset(&fake_mode, 0, sizeof(fake_mode));
    memset(fake_glyph, 0xFF, sizeof(fake_glyph));
    fake_mode.width = HOST_FRAME_WIDTH;
    fake_mode.height = HOST_FRAME_HEIGHT;
    fake_mode.bpp = VESA_BPP_32;
    fake_mode.pitch = HOST_FRAME_WIDTH * 4U;
    fake_mode.initialized = 1U;
    fake_backbuffer = 1U;
    fake_mode_available = 1U;
    fake_fill_calls = 0U;
    fake_pixel_calls = 0U;
    fake_glyph_calls = 0U;
    fake_region_calls = 0U;
    fake_frame_begin_calls = 0U;
    fake_frame_end_calls = 0U;
    fake_flip_calls = 0U;
    fake_cursor_x = 0U;
    fake_cursor_y = 0U;
    fake_mouse_invalidations = 0U;
}

static void init_video_fixture(void) {
    fake_mode_available = 1U;
    fake_mode.initialized = 1U;
    fake_backbuffer = 1U;
    video_init();
}

static void test_output_and_drawing(void) {
    uint32_t fill_calls;
    uint32_t pixel_calls;

    init_video_fixture();
    EXPECT(video_get_cursor_x() == 0);
    EXPECT(video_get_cursor_y() == 0);
    video_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
    video_set_cursor(4, 3);
    EXPECT(video_get_cursor_x() == 4);
    EXPECT(video_get_cursor_y() == 3);
    video_put_char('A', 0x1FU);
    video_put_char('\n', 0x1FU);
    video_put_char('\t', 0x1FU);
    video_put_char('\r', 0x1FU);
    video_put_char('\b', 0x1FU);
    video_put_char('?', 0x1FU);
    video_print("hello", 0x2EU);
    video_print(0, 0x2EU);
    video_put_char_at('B', 0x3FU, 2, 2);
    video_put_char_at('B', 0x3FU, -1, 2);
    video_put_char_at('B', 0x3FU, SCREEN_COLS, 2);
    video_print_at(1, 1, "one\ntwo", 0x4FU);
    video_print_at(1, 1, 0, 0x4FU);
    video_fill_rect(2, 2, 2, 2, 'F', 0x5FU);
    video_draw_hline(2, 4, 3, 'H', 0x6FU);
    video_draw_vline(5, 2, 3, 'V', 0x7FU);
    video_draw_box(8, 2, 4, 3, 0x1FU);
    video_begin_update();
    video_begin_update();
    video_flush_updates();
    video_end_update();
    fill_calls = fake_fill_calls;
    pixel_calls = fake_pixel_calls;
    EXPECT(fill_calls > 0U);
    EXPECT(pixel_calls > 0U);
    EXPECT(fake_region_calls > 0U);
    EXPECT(fake_flip_calls > 0U);
    video_clear();
    EXPECT(fake_fill_calls > fill_calls);
    EXPECT(fake_cursor_x <= VGA_WIDTH - 1U);
    EXPECT(fake_cursor_y <= VGA_HEIGHT - 1U);
}

static void test_terminal_history(void) {
    char snapshot[HOST_TERMINAL_BUFFER_SIZE];
    char small_snapshot[256];
    video_test_terminal_info_t info;
    uint32_t previous_generation;

    init_video_fixture();
    EXPECT(video_test_copy_terminal(0, sizeof(snapshot), &info) == ERR_INVALID);
    EXPECT(video_test_copy_terminal(snapshot, 0U, &info) == ERR_INVALID);
    EXPECT(video_test_copy_terminal(snapshot, 1U, &info) == ERR_OVERFLOW);
    video_terminal_begin();
    EXPECT(video_terminal_is_active() == 1);
    EXPECT(video_terminal_is_hosted() == 0);
    video_print("first line", 0x07U);
    video_newline();
    for (uint32_t line = 0U; line < 52U; line++) {
        video_print("history", 0x07U);
        video_newline();
    }
    EXPECT(video_test_copy_terminal(snapshot, sizeof(snapshot), &info) == OK);
    EXPECT(info.active == 1U);
    EXPECT(info.hosted == 0U);
    EXPECT(info.line_count > 45U);
    EXPECT(video_test_copy_terminal(small_snapshot, sizeof(small_snapshot),
                                   &info) == OK);
    EXPECT(info.truncated == 1U);
    EXPECT(video_terminal_scroll(0) == 0);
    EXPECT(video_terminal_scroll(1) == 1);
    EXPECT(video_terminal_is_scrolled() == 1);
    video_terminal_scroll_home();
    EXPECT(video_terminal_is_scrolled() == 1);
    video_terminal_scroll_end();
    EXPECT(video_terminal_is_scrolled() == 0);
    previous_generation = info.generation;
    video_terminal_clear();
    EXPECT(video_test_copy_terminal(snapshot, sizeof(snapshot), &info) == OK);
    EXPECT(info.line_count == 1U);
    EXPECT(info.generation > previous_generation);
    video_terminal_suspend();
    EXPECT(video_terminal_is_active() == 0);
    EXPECT(video_terminal_scroll(1) == 0);
    video_terminal_scroll_home();
    video_terminal_scroll_end();
}

static void test_hosted_terminal(void) {
    uint32_t glyph_calls;

    init_video_fixture();
    video_terminal_begin();
    video_print("hosted line one", 0x07U);
    video_newline();
    video_print("hosted line two", 0x07U);
    video_terminal_set_hosted(1);
    EXPECT(video_terminal_is_hosted() == 1);
    EXPECT(video_terminal_take_hosted_dirty() == 1);
    EXPECT(video_terminal_take_hosted_dirty() == 0);
    EXPECT(video_terminal_draw(-1, 0, 96, 96) == ERR_INVALID);
    EXPECT(video_terminal_draw(0, 0, 8, 8) == ERR_OVERFLOW);
    EXPECT(video_terminal_draw(0, 0, 96, 96) == OK);
    EXPECT(video_terminal_take_hosted_dirty() == 0);
    glyph_calls = fake_glyph_calls;
    EXPECT(glyph_calls > 0U);
    video_put_char('C', 0x1FU);
    EXPECT(video_terminal_present_hosted_dirty() == OK);
    EXPECT(video_terminal_take_hosted_dirty() == 0);
    EXPECT(video_terminal_draw(0, 0, 96, 96) == OK);
    video_put_char('D', 0x1FU);
    EXPECT(video_terminal_present_hosted_dirty() == OK);
    video_terminal_scroll_home();
    EXPECT(video_terminal_present_hosted_dirty() == OK);
    video_terminal_scroll_end();
    EXPECT(video_terminal_present_hosted_dirty() == OK);
    video_terminal_set_hosted(0);
    EXPECT(video_terminal_is_hosted() == 0);
    EXPECT(video_terminal_present_hosted_dirty() == OK);
}

static void test_hosted_failures_and_power(void) {
    init_video_fixture();
    video_terminal_set_hosted(1);
    fake_mode_available = 0U;
    EXPECT(video_terminal_draw(0, 0, 96, 96) == ERR_UNAVAILABLE);
    EXPECT(video_terminal_present_hosted_dirty() == ERR_STATE);
    fake_mode_available = 1U;
    fake_backbuffer = 0U;
    EXPECT(video_terminal_draw(0, 0, 96, 96) == ERR_UNAVAILABLE);
    EXPECT(video_terminal_present_hosted_dirty() == ERR_STATE);
    fake_backbuffer = 1U;
    video_terminal_set_hosted(0);
    EXPECT(video_terminal_draw(0, 0, 96, 96) == ERR_STATE);
    EXPECT(video_power_quiesce() == OK);
    EXPECT(fake_mode.initialized == 0U);
    init_video_fixture();
    fake_backbuffer = 0U;
    EXPECT(video_power_quiesce() == ERR_UNAVAILABLE);
    video_disable_framebuffer();
    EXPECT(fake_cursor_x <= VGA_WIDTH - 1U);
    init_video_fixture();
}

int main(void) {
    int result = OK;

    reset_fixture();
    coverage_active = 1U;
    test_output_and_drawing();
    test_terminal_history();
    test_hosted_terminal();
    test_hosted_failures_and_power();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        printf("VIDEO_HOST_FAIL:%d\n", result);
        return result;
    }
    printf("VIDEO_HOST_PASS\n");
    return 0;
}
