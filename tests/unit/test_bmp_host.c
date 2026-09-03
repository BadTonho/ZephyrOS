#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "drivers/vesa.h"
#include "fs/bmp.h"

#define HOST_COVERAGE_CAPACITY 2048U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_ALLOC_COUNT 16U
#define HOST_ALLOC_SIZE 2048U
#define HOST_BMP_SIZE 512U
#define HOST_FRAMEBUFFER_WIDTH 16U
#define HOST_FRAMEBUFFER_HEIGHT 16U

typedef union {
    uint64_t alignment;
    uint8_t bytes[HOST_ALLOC_SIZE];
} host_alloc_block_t;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static host_alloc_block_t allocations[HOST_ALLOC_COUNT];
static uint8_t allocation_used[HOST_ALLOC_COUNT];
static uint32_t framebuffer[HOST_FRAMEBUFFER_WIDTH * HOST_FRAMEBUFFER_HEIGHT];
static vesa_mode_t video_mode;
static uint8_t video_mode_available;
static uint32_t pixel_writes;

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
    printf("ZCOV_BEGIN|case=host:storage:bmp|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:storage:bmp|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:storage:bmp|value=0x%08X\n",
           (uint32_t)result);
}

static int expect_true(int condition, const char* expression) {
    if (condition) return OK;
    fprintf(stderr, "bmp-host: falhou: %s\n", expression);
    return ERR_STATE;
}

#define EXPECT(expression) \
    do { \
        if (expect_true((expression), #expression) != OK) (*failures)++; \
    } while (0)

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

void* kmalloc(uint32_t size) {
    if (!size || size > HOST_ALLOC_SIZE) return 0;
    for (uint32_t index = 0U; index < HOST_ALLOC_COUNT; index++) {
        if (!allocation_used[index]) {
            allocation_used[index] = 1U;
            return allocations[index].bytes;
        }
    }
    return 0;
}

void kfree(void* pointer) {
    if (!pointer) return;
    for (uint32_t index = 0U; index < HOST_ALLOC_COUNT; index++) {
        if (pointer == allocations[index].bytes) allocation_used[index] = 0U;
    }
}

static void reset_allocations(void) {
    kmemset(allocation_used, 0, sizeof(allocation_used));
}

static uint32_t allocation_count(void) {
    uint32_t count = 0U;

    for (uint32_t index = 0U; index < HOST_ALLOC_COUNT; index++) {
        if (allocation_used[index]) count++;
    }
    return count;
}

vesa_mode_t* vesa_get_mode(void) {
    return video_mode_available ? &video_mode : 0;
}

void vesa_put_pixel(uint32_t x, uint32_t y, vesa_color_t color) {
    if (!video_mode_available || x >= HOST_FRAMEBUFFER_WIDTH ||
        y >= HOST_FRAMEBUFFER_HEIGHT) return;
    framebuffer[y * HOST_FRAMEBUFFER_WIDTH + x] = color.raw;
    pixel_writes++;
}

static void reset_video(void) {
    kmemset(framebuffer, 0, sizeof(framebuffer));
    kmemset(&video_mode, 0, sizeof(video_mode));
    video_mode.width = HOST_FRAMEBUFFER_WIDTH;
    video_mode.height = HOST_FRAMEBUFFER_HEIGHT;
    video_mode.bpp = VESA_BPP_32;
    video_mode.pitch = HOST_FRAMEBUFFER_WIDTH * sizeof(uint32_t);
    video_mode.framebuffer = framebuffer;
    video_mode.initialized = 1U;
    video_mode_available = 1U;
    pixel_writes = 0U;
}

static void put_u16(uint8_t* buffer, uint32_t offset, uint16_t value) {
    buffer[offset] = (uint8_t)value;
    buffer[offset + 1U] = (uint8_t)(value >> 8U);
}

static void put_u32(uint8_t* buffer, uint32_t offset, uint32_t value) {
    buffer[offset] = (uint8_t)value;
    buffer[offset + 1U] = (uint8_t)(value >> 8U);
    buffer[offset + 2U] = (uint8_t)(value >> 16U);
    buffer[offset + 3U] = (uint8_t)(value >> 24U);
}

static void put_i32(uint8_t* buffer, uint32_t offset, int32_t value) {
    put_u32(buffer, offset, (uint32_t)value);
}

static void put_text(uint8_t* buffer, uint32_t offset, const char* text) {
    for (uint32_t index = 0U; index < 4U; index++) {
        buffer[offset + index] = (uint8_t)text[index];
    }
}

static uint32_t bmp_row_size(uint32_t width, uint16_t bpp) {
    return ((width * bpp + 31U) / 32U) * 4U;
}

static uint32_t make_header(uint8_t* buffer, uint16_t bpp, uint32_t width,
                            int32_t height, uint32_t colors_used) {
    uint32_t entries = bpp <= 8U ? colors_used : 0U;
    uint32_t data_offset = 54U + entries * 4U;

    kmemset(buffer, 0, HOST_BMP_SIZE);
    buffer[0] = 'B';
    buffer[1] = 'M';
    put_u32(buffer, 2U, HOST_BMP_SIZE);
    put_u32(buffer, 10U, data_offset);
    put_u32(buffer, 14U, 40U);
    put_u32(buffer, 18U, width);
    put_i32(buffer, 22U, height);
    put_u16(buffer, 26U, 1U);
    put_u16(buffer, 28U, bpp);
    put_u32(buffer, 30U, 0U);
    put_u32(buffer, 34U, 0U);
    put_u32(buffer, 46U, colors_used);
    for (uint32_t index = 0U; index < entries &&
         data_offset + index * 4U + 4U <= HOST_BMP_SIZE; index++) {
        buffer[54U + index * 4U] = (uint8_t)(10U + index);
        buffer[55U + index * 4U] = (uint8_t)(20U + index);
        buffer[56U + index * 4U] = (uint8_t)(30U + index);
    }
    return data_offset;
}

static uint32_t make_24_bmp(uint8_t* buffer) {
    uint32_t data_offset = make_header(buffer, 24U, 2U, 2, 0U);
    uint32_t row_size = bmp_row_size(2U, 24U);

    buffer[data_offset] = 1U;
    buffer[data_offset + 1U] = 2U;
    buffer[data_offset + 2U] = 3U;
    buffer[data_offset + 3U] = 4U;
    buffer[data_offset + 4U] = 5U;
    buffer[data_offset + 5U] = 6U;
    buffer[data_offset + row_size] = 7U;
    buffer[data_offset + row_size + 1U] = 8U;
    buffer[data_offset + row_size + 2U] = 9U;
    buffer[data_offset + row_size + 3U] = 10U;
    buffer[data_offset + row_size + 4U] = 11U;
    buffer[data_offset + row_size + 5U] = 12U;
    return data_offset + row_size * 2U;
}

static uint32_t make_indexed_bmp(uint8_t* buffer, uint16_t bpp,
                                 uint32_t width, int32_t height,
                                 uint32_t colors_used) {
    uint32_t data_offset = make_header(buffer, bpp, width, height,
                                        colors_used);
    uint32_t row_size = bmp_row_size(width, bpp);

    if (bpp == 8U) {
        buffer[data_offset] = 0U;
        buffer[data_offset + 1U] = 1U;
    } else if (bpp == 4U) {
        buffer[data_offset] = 0x12U;
    } else {
        buffer[data_offset] = 0x80U;
    }
    return data_offset + row_size;
}

static vesa_color_t color(uint8_t blue, uint8_t green, uint8_t red) {
    vesa_color_t value;

    value.raw = 0U;
    value.channels.blue = blue;
    value.channels.green = green;
    value.channels.red = red;
    value.channels.alpha = 0xFFU;
    return value;
}

static void test_bmp_loading(int* failures) {
    uint8_t buffer[HOST_BMP_SIZE];
    bmp_image_t image;
    uint32_t size;

    size = make_24_bmp(buffer);
    EXPECT(bmp_load(buffer, size, &image) == OK);
    EXPECT(image.width == 2U && image.height == 2U && image.bpp == 24U);
    EXPECT(image.pixel_data_size == 16U && image.color_table == 0);
    EXPECT(image.pixel_data != 0 && allocation_count() == 1U);
    bmp_free(&image);
    EXPECT(allocation_count() == 0U);

    size = make_indexed_bmp(buffer, 8U, 2U, -1, 2U);
    EXPECT(bmp_load(buffer, size, &image) == OK);
    EXPECT(image.color_table_entries == 2U && image.pixel_data_size == 4U);
    bmp_free(&image);

    size = make_indexed_bmp(buffer, 4U, 3U, -1, 16U);
    EXPECT(bmp_load(buffer, size, &image) == OK);
    EXPECT(image.width == 3U && image.bpp == 4U);
    bmp_free(&image);

    size = make_indexed_bmp(buffer, 1U, 8U, -1, 2U);
    EXPECT(bmp_load(buffer, size, &image) == OK);
    EXPECT(image.width == 8U && image.bpp == 1U);
    bmp_free(&image);

    EXPECT(bmp_load(0, size, &image) == ERR_NULL);
    EXPECT(bmp_load(buffer, 0U, &image) == ERR_INVALID);
    EXPECT(bmp_load(buffer, size, 0) == ERR_NULL);

    buffer[0] = 'X';
    EXPECT(bmp_load(buffer, size, &image) == ERR_INVALID);
    size = make_24_bmp(buffer);
    put_u32(buffer, 14U, 39U);
    EXPECT(bmp_load(buffer, size, &image) == ERR_INVALID);
    size = make_24_bmp(buffer);
    put_u16(buffer, 26U, 2U);
    EXPECT(bmp_load(buffer, size, &image) == ERR_INVALID);
    size = make_24_bmp(buffer);
    put_i32(buffer, 18U, 0);
    EXPECT(bmp_load(buffer, size, &image) == ERR_INVALID);
    size = make_24_bmp(buffer);
    put_i32(buffer, 22U, 0);
    EXPECT(bmp_load(buffer, size, &image) == ERR_INVALID);
    size = make_24_bmp(buffer);
    put_u32(buffer, 30U, 1U);
    EXPECT(bmp_load(buffer, size, &image) == ERR_INVALID);
    size = make_24_bmp(buffer);
    put_u16(buffer, 28U, 16U);
    EXPECT(bmp_load(buffer, size, &image) == ERR_INVALID);

    size = make_indexed_bmp(buffer, 1U, 8U, -1, 3U);
    EXPECT(bmp_load(buffer, size, &image) == ERR_INVALID);
    size = make_indexed_bmp(buffer, 1U, 8U, -1, 2U);
    EXPECT(bmp_load(buffer, 54U, &image) == ERR_INVALID);
    size = make_24_bmp(buffer);
    put_u32(buffer, 10U, 53U);
    EXPECT(bmp_load(buffer, size, &image) == ERR_INVALID);
    size = make_24_bmp(buffer);
    put_u32(buffer, 10U, HOST_BMP_SIZE);
    EXPECT(bmp_load(buffer, size, &image) == ERR_INVALID);
    size = make_24_bmp(buffer);
    EXPECT(bmp_load(buffer, size - 1U, &image) == ERR_INVALID);

    size = make_header(buffer, 4U, 0x40000000U, 1, 2U);
    EXPECT(bmp_load(buffer, size, &image) == ERR_OVERFLOW);
    size = make_header(buffer, 24U, 1U, 0x7FFFFFFF, 0U);
    EXPECT(bmp_load(buffer, size, &image) == ERR_OVERFLOW);
}

static void test_bmp_allocation_failures(int* failures) {
    uint8_t buffer[HOST_BMP_SIZE];
    bmp_image_t image;
    uint32_t size = make_24_bmp(buffer);

    kmemset(allocation_used, 1, sizeof(allocation_used));
    EXPECT(bmp_load(buffer, size, &image) == ERR_MEM);
    reset_allocations();

    size = make_indexed_bmp(buffer, 8U, 2U, -1, 2U);
    kmemset(allocation_used, 1, sizeof(allocation_used));
    EXPECT(bmp_load(buffer, size, &image) == ERR_MEM);
    reset_allocations();
}

static void test_bmp_drawing(int* failures) {
    uint8_t buffer[HOST_BMP_SIZE];
    bmp_image_t image;
    vesa_color_t transparent;
    uint32_t size;

    reset_video();
    size = make_24_bmp(buffer);
    EXPECT(bmp_load(buffer, size, &image) == OK);
    bmp_draw(&image, 1, 1);
    EXPECT(framebuffer[1U * HOST_FRAMEBUFFER_WIDTH + 1U] ==
           color(7U, 8U, 9U).raw);
    EXPECT(framebuffer[2U * HOST_FRAMEBUFFER_WIDTH + 1U] ==
           color(1U, 2U, 3U).raw);
    transparent = color(1U, 2U, 3U);
    pixel_writes = 0U;
    bmp_draw_transparent(&image, 0, 0, transparent);
    EXPECT(pixel_writes == 3U);
    pixel_writes = 0U;
    EXPECT(bmp_draw_transparent_resized(&image, 0, 0, 4U, 4U,
                                        transparent) == OK);
    EXPECT(pixel_writes != 0U);
    bmp_draw_scaled(&image, 0, 0, 2U);
    bmp_draw_scaled(&image, 0, 0, 0U);
    bmp_free(&image);

    size = make_indexed_bmp(buffer, 8U, 2U, -1, 2U);
    EXPECT(bmp_load(buffer, size, &image) == OK);
    reset_video();
    bmp_draw(&image, 0, 0);
    EXPECT(framebuffer[0] == color(10U, 20U, 30U).raw);
    EXPECT(framebuffer[1] == color(11U, 21U, 31U).raw);
    bmp_free(&image);

    size = make_indexed_bmp(buffer, 4U, 3U, -1, 16U);
    EXPECT(bmp_load(buffer, size, &image) == OK);
    reset_video();
    bmp_draw(&image, 0, 0);
    EXPECT(framebuffer[0] == color(11U, 21U, 31U).raw);
    EXPECT(framebuffer[1] == color(12U, 22U, 32U).raw);
    bmp_free(&image);

    size = make_indexed_bmp(buffer, 1U, 8U, -1, 2U);
    EXPECT(bmp_load(buffer, size, &image) == OK);
    reset_video();
    bmp_draw(&image, 0, 0);
    EXPECT(framebuffer[0] == color(11U, 21U, 31U).raw);
    EXPECT(framebuffer[1] == color(10U, 20U, 30U).raw);
    bmp_free(&image);

    bmp_draw(0, 0, 0);
    kmemset(&image, 0, sizeof(image));
    bmp_draw(&image, 0, 0);
    bmp_draw_transparent(0, 0, 0, transparent);
    bmp_draw_scaled(0, 0, 0, 2U);
    EXPECT(bmp_draw_transparent_resized(0, 0, 0, 1U, 1U,
                                        transparent) == ERR_NULL);
    EXPECT(bmp_draw_transparent_resized(&image, 0, 0, 1U, 1U,
                                        transparent) == ERR_STATE);

    size = make_24_bmp(buffer);
    EXPECT(bmp_load(buffer, size, &image) == OK);
    video_mode.initialized = 0U;
    EXPECT(bmp_draw_transparent_resized(&image, 0, 0, 1U, 1U,
                                        transparent) == ERR_INVALID);
    reset_video();
    EXPECT(bmp_draw_transparent_resized(&image, -1, 0, 1U, 1U,
                                        transparent) == ERR_INVALID);
    EXPECT(bmp_draw_transparent_resized(&image, 0, -1, 1U, 1U,
                                        transparent) == ERR_INVALID);
    EXPECT(bmp_draw_transparent_resized(&image, 0, 0, 0U, 1U,
                                        transparent) == ERR_INVALID);
    EXPECT(bmp_draw_transparent_resized(&image, 0, 0, 1U, 0U,
                                        transparent) == ERR_INVALID);
    EXPECT(bmp_draw_transparent_resized(&image, 0, 0,
                                        HOST_FRAMEBUFFER_WIDTH + 1U, 1U,
                                        transparent) == ERR_INVALID);
    EXPECT(bmp_draw_transparent_resized(&image, 0, 0, 1U,
                                        HOST_FRAMEBUFFER_HEIGHT + 1U,
                                        transparent) == ERR_INVALID);
    bmp_free(&image);
    bmp_free(&image);
    bmp_free(0);
    video_mode_available = 0U;
}

int main(void) {
    int failures = 0;

    coverage_active = 1U;
    reset_allocations();
    reset_video();
    bmp_init();
    test_bmp_loading(&failures);
    test_bmp_allocation_failures(&failures);
    test_bmp_drawing(&failures);
    coverage_active = 0U;
    coverage_emit(failures ? ERR_STATE : OK);
    return failures ? 1 : 0;
}
