#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "drivers/font.h"
#include "drivers/vesa.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_BUFFER_SIZE 65536U
#define HOST_BOOT_HANDLE 1U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static vesa_boot_info_t fake_boot;
static uint32_t fake_framebuffer[HOST_BUFFER_SIZE / sizeof(uint32_t)];
static uint8_t fake_backbuffer[HOST_BUFFER_SIZE];
static uint8_t fake_glyph[FONT_HEIGHT];
static uint32_t fake_ticks;
static uint8_t fake_alloc_failure;
static uint32_t fake_free_count;

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
    printf("ZCOV_BEGIN|case=host:drivers:vesa|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:vesa|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:vesa|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "vesa-host: falhou: %s\n", expression);
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

void* kmalloc(uint32_t size) {
    if (fake_alloc_failure || !size || size > sizeof(fake_backbuffer)) {
        return 0;
    }
    memset(fake_backbuffer, 0, sizeof(fake_backbuffer));
    return fake_backbuffer;
}

void kfree(void* pointer) {
    if (pointer == fake_backbuffer) {
        fake_free_count++;
        memset(fake_backbuffer, 0, sizeof(fake_backbuffer));
    }
}

void kmemcpy(void* destination, const void* source, uint32_t size) {
    memcpy(destination, source, size);
}

uint32_t timer_get_ticks(void) {
    return fake_ticks++;
}

const uint8_t* font_get_glyph(char character) {
    if (character == '?') return 0;
    return fake_glyph;
}

vesa_boot_info_t* vesa_host_boot_info(uint32_t address) {
    return address == HOST_BOOT_HANDLE ? &fake_boot : 0;
}

uint32_t* vesa_host_framebuffer(uint32_t address) {
    return address == HOST_BOOT_HANDLE ? fake_framebuffer : 0;
}

static void reset_fixture(void) {
    memset(&fake_boot, 0, sizeof(fake_boot));
    memset(fake_framebuffer, 0, sizeof(fake_framebuffer));
    memset(fake_backbuffer, 0, sizeof(fake_backbuffer));
    memset(fake_glyph, 0xFF, sizeof(fake_glyph));
    fake_boot.framebuffer_addr = HOST_BOOT_HANDLE;
    fake_boot.pitch = 64U * 4U;
    fake_boot.width = 64U;
    fake_boot.height = 48U;
    fake_boot.bpp = VESA_BPP_32;
    fake_boot.initialized = 1U;
    fake_ticks = 1U;
    fake_alloc_failure = 0U;
    fake_free_count = 0U;
    vesa_disable();
}

static void test_invalid_initialization(void) {
    vesa_mode_t* mode;

    vesa_init(0U);
    mode = vesa_get_mode();
    EXPECT(mode != 0);
    EXPECT(mode->initialized == 0U);
    EXPECT(vesa_init_backbuffer() == ERR_NOT_FOUND);

    vesa_init(2U);
    EXPECT(mode->initialized == 0U);

    fake_boot.initialized = 0U;
    vesa_init(HOST_BOOT_HANDLE);
    EXPECT(mode->initialized == 0U);
    fake_boot.initialized = 1U;

    fake_boot.framebuffer_addr = 0U;
    vesa_init(HOST_BOOT_HANDLE);
    EXPECT(mode->initialized == 0U);
    fake_boot.framebuffer_addr = HOST_BOOT_HANDLE;

    fake_boot.width = 0U;
    vesa_init(HOST_BOOT_HANDLE);
    EXPECT(mode->initialized == 0U);
    fake_boot.width = 64U;

    fake_boot.height = 0U;
    vesa_init(HOST_BOOT_HANDLE);
    EXPECT(mode->initialized == 0U);
    fake_boot.height = 48U;

    fake_boot.pitch = 0U;
    vesa_init(HOST_BOOT_HANDLE);
    EXPECT(mode->initialized == 0U);
    fake_boot.pitch = 64U * 4U;

    fake_boot.bpp = VESA_BPP_16;
    vesa_init(HOST_BOOT_HANDLE);
    EXPECT(mode->initialized == 0U);
    fake_boot.bpp = VESA_BPP_32;

    fake_boot.framebuffer_addr = 2U;
    vesa_init(HOST_BOOT_HANDLE);
    EXPECT(mode->initialized == 0U);
    fake_boot.framebuffer_addr = HOST_BOOT_HANDLE;
}

static void test_32bpp_drawing(void) {
    vesa_color_t color;
    vesa_color_t pixel;
    vesa_metrics_t metrics;
    uint8_t bitmap[1] = {0x81U};
    uint32_t free_count;

    vesa_init(HOST_BOOT_HANDLE);
    EXPECT(vesa_get_mode()->initialized == 1U);
    EXPECT(vesa_init_backbuffer() == OK);
    EXPECT(vesa_has_backbuffer() == 1);
    free_count = fake_free_count;
    EXPECT(vesa_init_backbuffer() == OK);
    EXPECT(fake_free_count == free_count + 1U);

    color.raw = vesa_rgba(0x11U, 0x22U, 0x33U, 0x44U);
    EXPECT(color.raw == vesa_rgba(0x11U, 0x22U, 0x33U, 0x44U));
    EXPECT(vesa_rgb(0x11U, 0x22U, 0x33U) != 0U);
    vesa_clear(color);
    vesa_put_pixel(4U, 5U, color);
    pixel = vesa_get_pixel(4U, 5U);
    EXPECT(pixel.raw == color.raw);
    EXPECT(vesa_get_pixel(64U, 0U).raw == 0U);

    vesa_set_clip_rect(4U, 5U, 8U, 8U);
    vesa_put_pixel(3U, 5U, (vesa_color_t){.raw = 0xA5A5A5A5U});
    EXPECT(vesa_get_pixel(3U, 5U).raw == color.raw);
    vesa_fill_rect(0U, 0U, 64U, 48U, color);
    vesa_reset_clip_rect();
    vesa_set_clip_rect(63U, 47U, 8U, 8U);
    vesa_fill_rect(0U, 0U, 64U, 48U, color);
    vesa_reset_clip_rect();

    vesa_draw_bitmap(2, 2, bitmap, 8U, 1U, color);
    EXPECT(vesa_get_pixel(2U, 2U).raw == color.raw);
    EXPECT(vesa_get_pixel(9U, 2U).raw == color.raw);
    vesa_draw_circle(16, 16, 4, color);
    vesa_draw_circle(16, 16, -1, color);
    vesa_draw_string(0, 0, "A?", color, 1U);

    vesa_frame_begin_region(2U, 2U, 4U, 4U);
    vesa_put_pixel(3U, 3U, color);
    vesa_frame_mark_region(3U, 3U, 2U, 2U);
    vesa_flip();
    vesa_flip_region(0U, 0U, 4U, 4U);
    vesa_frame_end();
    vesa_frame_end();
    vesa_flip();
    vesa_flip_region(1U, 1U, 2U, 2U);
    EXPECT(fake_framebuffer[3U + 3U * 64U] == color.raw);

    vesa_get_metrics(0);
    vesa_get_metrics(&metrics);
    EXPECT(metrics.presentations >= 2U);
    EXPECT(metrics.full_presentations >= 1U);
    EXPECT(metrics.partial_presentations >= 1U);
    EXPECT(metrics.bytes_copied > 0U);
    EXPECT(metrics.last_copy_bytes > 0U);
    vesa_set_mode(800U, 600U, VESA_BPP_32);
}

static void test_24bpp_and_cleanup(void) {
    vesa_color_t color;
    vesa_color_t pixel;

    vesa_disable();
    EXPECT(vesa_has_backbuffer() == 0);
    EXPECT(vesa_get_mode()->initialized == 0U);

    fake_boot.pitch = 64U * 3U;
    fake_boot.bpp = VESA_BPP_24;
    vesa_init(HOST_BOOT_HANDLE);
    EXPECT(vesa_get_mode()->bpp == VESA_BPP_24);
    EXPECT(vesa_init_backbuffer() == OK);
    color.raw = vesa_rgba(0xAAU, 0xBBU, 0xCCU, 0xDDU);
    vesa_clear(color);
    vesa_put_pixel(7U, 8U, color);
    pixel = vesa_get_pixel(7U, 8U);
    EXPECT(pixel.channels.red == color.channels.red);
    EXPECT(pixel.channels.green == color.channels.green);
    EXPECT(pixel.channels.blue == color.channels.blue);
    vesa_draw_glyph8x16(0U, 0U, fake_glyph, color);
    vesa_flip();
    vesa_disable();
    EXPECT(vesa_has_backbuffer() == 0);
    EXPECT(vesa_get_mode()->initialized == 0U);
}

static void test_allocation_failure(void) {
    fake_boot.pitch = 64U * 4U;
    fake_boot.bpp = VESA_BPP_32;
    vesa_init(HOST_BOOT_HANDLE);
    fake_alloc_failure = 1U;
    EXPECT(vesa_init_backbuffer() == ERR_MEM);
    EXPECT(vesa_has_backbuffer() == 0);
    fake_alloc_failure = 0U;
}

int main(void) {
    int result = OK;

    reset_fixture();
    coverage_active = 1U;
    test_invalid_initialization();
    test_32bpp_drawing();
    test_24bpp_and_cleanup();
    test_allocation_failure();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
