#include <stdint.h>
#include <stdio.h>

#include "apps/taskmanager_test.h"
#include "apps/shell_introspection.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "drivers/vesa.h"
#include "fs/vfs.h"
#include "process/process.h"
#include "ui/display.h"
#include "ui/gui.h"
#include "ui/taskbar.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static vesa_mode_t fixture_vesa_mode = {
    .width = 800U,
    .height = 600U,
    .bpp = 32U,
    .pitch = 3200U,
    .framebuffer = 0,
    .initialized = 1U
};

process_t* processes[MAX_PROCESSES];
uint32_t process_count;

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
    printf("ZCOV_BEGIN|case=host:shell:taskmanager|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:taskmanager|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:taskmanager|value=0x%08X\n",
           (uint32_t)result);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

uint32_t display_scale_px(uint32_t base_value) {
    return base_value;
}

int display_get_metrics(display_metrics_t* metrics) {
    if (!metrics) return ERR_NULL;
    metrics->scale = DISPLAY_SCALE_NORMAL;
    metrics->factor_numerator = 1U;
    metrics->factor_denominator = 1U;
    return OK;
}

vesa_mode_t* vesa_get_mode(void) {
    return &fixture_vesa_mode;
}

int vesa_has_backbuffer(void) {
    return 0;
}

int taskbar_get_work_area(tb_rect_t* area) {
    (void)area;
    return OK;
}

int vfs_open(const char* path, uint32_t mode, int32_t* fd_out) {
    (void)path;
    (void)mode;
    if (!fd_out) return ERR_NULL;
    *fd_out = VFS_FD_INVALID;
    return ERR_NOT_FOUND;
}

int vfs_read(int32_t fd, void* buffer, uint32_t size,
             uint32_t* bytes_read) {
    (void)fd;
    (void)buffer;
    (void)size;
    if (bytes_read) *bytes_read = 0U;
    return ERR_NOT_FOUND;
}

int vfs_close(int32_t fd) {
    (void)fd;
    return OK;
}

int vfs_list_dir(const char* path, vfs_dir_entry_t* entries,
                 uint32_t capacity, uint32_t* out_count) {
    (void)path;
    (void)entries;
    (void)capacity;
    if (!out_count) return ERR_NULL;
    *out_count = 0U;
    return OK;
}

uint32_t timer_get_ticks(void) {
    return 1U;
}

void vesa_draw_hline(uint32_t x, uint32_t y, uint32_t width,
                     vesa_color_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)color;
}

void vesa_draw_line(int x0, int y0, int x1, int y1, vesa_color_t color) {
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    (void)color;
}

uint32_t thread_get_count(void) {
    return 0U;
}

void video_put_char_at(char character, uint8_t color, int x, int y) {
    (void)character;
    (void)color;
    (void)x;
    (void)y;
}

void gui_draw_scaled_text(uint32_t x, uint32_t y, const char* text,
                          uint32_t color) {
    (void)x;
    (void)y;
    (void)text;
    (void)color;
}

void gui_draw_rounded_rect(uint32_t x, uint32_t y, uint32_t width,
                           uint32_t height, uint32_t radius,
                           uint32_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)radius;
    (void)color;
}

void gui_draw_flat_border(uint32_t x, uint32_t y, uint32_t width,
                          uint32_t height, uint32_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)color;
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
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = taskmgr_host_test_contracts();
    coverage_active = 0U;
    if (result != 0) printf("TASKMANAGER_HOST_FAIL:%d\n", result);
    coverage_emit(result);
    return result == 0 ? 0 : 1;
}
