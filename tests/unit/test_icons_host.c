#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "drivers/vesa.h"
#include "fs/bmp.h"
#include "fs/fs.h"
#include "ui/icons.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_ALLOCATION_SIZE 4096U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t fake_fs_type;
static int fake_read_result;
static int fake_bmp_result;
static uint8_t fake_bad_dimensions;
static uint8_t fake_alloc_failure;
static int fake_draw_result;
static uint32_t fake_free_count;
static uint32_t fake_draw_count;
static uint32_t fake_log_count;
static uint8_t fake_allocation[HOST_ALLOCATION_SIZE];
static uint8_t fake_pixels[32U * 32U * 3U];
static vesa_mode_t fake_mode;

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
    printf("ZCOV_BEGIN|case=host:ui:icons|value=0x%08X\n", coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:ui:icons|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:ui:icons|value=0x%08X\n", (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "icons-host: falhou: %s\n", expression);
        (void)fflush(stderr);
        __builtin_trap();
    }
}

#define EXPECT(expression) expect_true((expression), #expression)

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
    fake_log_count++;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
    fake_log_count++;
}

void* kmalloc(uint32_t size) {
    if (fake_alloc_failure || !size || size > sizeof(fake_allocation)) return 0;
    memset(fake_allocation, 0, sizeof(fake_allocation));
    return fake_allocation;
}

void kfree(void* pointer) {
    if (pointer == fake_allocation) {
        memset(fake_allocation, 0, sizeof(fake_allocation));
    }
}

uint8_t fs_get_type(void) {
    return fake_fs_type;
}

int fs_read_file_at(const char* path, uint8_t* buffer, uint32_t max_size) {
    if (!path || !buffer || !max_size) return ERR_NULL;
    if (fake_read_result <= 0) return fake_read_result;
    memset(buffer, 0xA5, max_size < 4U ? max_size : 4U);
    return fake_read_result;
}

int bmp_load(const uint8_t* raw_data, uint32_t size, bmp_image_t* out) {
    if (!raw_data || !out) return ERR_NULL;
    if (!size) return ERR_INVALID;
    if (fake_bmp_result != OK) return fake_bmp_result;
    memset(out, 0, sizeof(*out));
    out->width = fake_bad_dimensions ? 16U : 32U;
    out->height = fake_bad_dimensions ? 16U : 32U;
    out->bpp = 24U;
    out->pixel_data = fake_pixels;
    out->pixel_data_size = sizeof(fake_pixels);
    out->initialized = 1U;
    return OK;
}

void bmp_free(bmp_image_t* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    fake_free_count++;
}

int bmp_draw_transparent_resized(bmp_image_t* image, int x, int y,
                                 uint32_t width, uint32_t height,
                                 vesa_color_t transparent_color) {
    (void)image;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)transparent_color;
    fake_draw_count++;
    return fake_draw_result;
}

vesa_mode_t* vesa_get_mode(void) {
    return &fake_mode;
}

uint32_t vesa_rgb(uint8_t red, uint8_t green, uint8_t blue) {
    return ((uint32_t)0xFFU << 24) | ((uint32_t)red << 16) |
           ((uint32_t)green << 8) | blue;
}

static void reset_fixture(void) {
    fake_fs_type = FS_TYPE_NONE;
    fake_read_result = 64;
    fake_bmp_result = OK;
    fake_bad_dimensions = 0U;
    fake_alloc_failure = 0U;
    fake_draw_result = OK;
    fake_free_count = 0U;
    fake_draw_count = 0U;
    fake_log_count = 0U;
    memset(&fake_mode, 0, sizeof(fake_mode));
    memset(fake_pixels, 0x5A, sizeof(fake_pixels));
}

static void test_default_registry(void) {
    icon_registry_t* registry;

    icons_init();
    registry = icons_get_registry();
    EXPECT(registry != 0);
    EXPECT(registry->desktop[ICON_DESKTOP_SHELL].ch == 'S');
    EXPECT(registry->desktop[ICON_DESKTOP_EXPLORER].ch == 'E');
    EXPECT(registry->desktop[ICON_DESKTOP_TASKMGR].ch == 'T');
    EXPECT(registry->wm[ICON_WM_CLOSE].ch == 'x');
    EXPECT(registry->wm[ICON_WM_MINIMIZE].ch == '_');
    EXPECT(registry->fm[ICON_FM_FOLDER].ch == '[');
    EXPECT(registry->fm[ICON_FM_FILE].ch == '-');
    EXPECT(registry->tb[ICON_TB_START].ch == '>');
    EXPECT(icons_get_desktop(ICON_DESKTOP_COUNT) == 0);
    EXPECT(icons_get_wm(ICON_WM_COUNT) == 0);
    EXPECT(icons_get_fm(ICON_FM_COUNT) == 0);
    EXPECT(icons_get_tb(ICON_TB_COUNT) == 0);
    EXPECT(icons_get_desktop_bitmap_status(ICON_DESKTOP_COUNT) == ERR_INVALID);
    EXPECT(icons_draw_desktop_bitmap(ICON_DESKTOP_COUNT, 0, 0) == ERR_INVALID);
    EXPECT(icons_draw_desktop_bitmap_resized(ICON_DESKTOP_SHELL, 0, 0, 0) ==
           ERR_INVALID);
    EXPECT(icons_draw_desktop_bitmap_resized(ICON_DESKTOP_SHELL, -1, 0, 1) ==
           ERR_INVALID);
}

static void test_registry_mutation(void) {
    icons_set_desktop(ICON_DESKTOP_SHELL, 's', 1U, 2U);
    icons_set_wm(ICON_WM_CLOSE, 'c', 3U, 4U);
    icons_set_fm(ICON_FM_FOLDER, 'f', 5U, 6U);
    icons_set_tb(ICON_TB_START, 't', 7U, 8U);
    icons_set_desktop(ICON_DESKTOP_COUNT, 'x', 0U, 0U);
    icons_set_wm(ICON_WM_COUNT, 'x', 0U, 0U);
    icons_set_fm(ICON_FM_COUNT, 'x', 0U, 0U);
    icons_set_tb(ICON_TB_COUNT, 'x', 0U, 0U);
    EXPECT(icons_get_desktop(ICON_DESKTOP_SHELL)->ch == 's');
    EXPECT(icons_get_wm(ICON_WM_CLOSE)->color_selected == 4U);
    EXPECT(icons_get_fm(ICON_FM_FOLDER)->color == 5U);
    EXPECT(icons_get_tb(ICON_TB_START)->color_selected == 8U);
    icons_reset_defaults();
    EXPECT(icons_get_desktop(ICON_DESKTOP_SHELL)->ch == 'S');
}

static void test_bitmap_states(void) {
    fake_fs_type = FS_TYPE_FAT12;
    fake_read_result = 0;
    icons_init();
    EXPECT(icons_get_desktop_bitmap_status(ICON_DESKTOP_SHELL) == ERR_NOT_FOUND);

    fake_read_result = 64;
    fake_bmp_result = ERR_INVALID;
    icons_init();
    EXPECT(icons_get_desktop_bitmap_status(ICON_DESKTOP_EXPLORER) == ERR_INVALID);

    fake_bmp_result = OK;
    fake_bad_dimensions = 1U;
    icons_init();
    EXPECT(icons_get_desktop_bitmap_status(ICON_DESKTOP_TASKMGR) == ERR_INVALID);

    fake_bad_dimensions = 0U;
    fake_alloc_failure = 1U;
    icons_init();
    EXPECT(icons_get_desktop_bitmap_status(ICON_DESKTOP_SHELL) == ERR_MEM);

    fake_alloc_failure = 0U;
    icons_init();
    EXPECT(icons_get_desktop_bitmap_status(ICON_DESKTOP_SHELL) == OK);
    EXPECT(icons_get_desktop_bitmap_status(ICON_DESKTOP_EXPLORER) == OK);
    EXPECT(icons_get_desktop_bitmap_status(ICON_DESKTOP_TASKMGR) == OK);
    fake_mode.initialized = 1U;
    fake_mode.width = 64U;
    fake_mode.height = 64U;
    EXPECT(icons_draw_desktop_bitmap(ICON_DESKTOP_SHELL, 0, 0) == OK);
    EXPECT(icons_draw_desktop_bitmap_resized(ICON_DESKTOP_EXPLORER, 8, 8, 16U) ==
           OK);
    EXPECT(fake_draw_count == 2U);
    EXPECT(icons_draw_desktop_bitmap_resized(ICON_DESKTOP_SHELL, 49, 0, 16U) ==
           ERR_INVALID);
    fake_mode.initialized = 0U;
    EXPECT(icons_draw_desktop_bitmap(ICON_DESKTOP_SHELL, 0, 0) == ERR_INVALID);
}

int main(void) {
    reset_fixture();
    coverage_active = 1U;
    test_default_registry();
    test_registry_mutation();
    test_bitmap_states();
    coverage_active = 0U;
    coverage_emit(OK);
    return 0;
}
