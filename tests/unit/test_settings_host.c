#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/video.h"
#include "drivers/vesa.h"
#include "drivers/mouse.h"
#include "fs/storage.h"
#include "process/process.h"
#include "settings_test.h"
#include "ui/display.h"
#include "ui/gui.h"
#include "ui/taskbar.h"
#include "ui/wm.h"
#include "ui/taskbar.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

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
    printf("ZCOV_BEGIN|case=host:ui:settings-icons|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:ui:settings-icons|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:ui:settings-icons|value=0x%08X\n",
           (uint32_t)result);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void video_clear(void) {}
void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}
void video_print_at(int x, int y, const char* text, uint8_t color) {
    (void)x;
    (void)y;
    (void)text;
    (void)color;
}
void video_put_char_at(char character, uint8_t color, int x, int y) {
    (void)character;
    (void)color;
    (void)x;
    (void)y;
}
void video_draw_hline(int x, int y, int width, char character, uint8_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)character;
    (void)color;
}
void video_fill_rect(int x, int y, int width, int height, char character,
                     uint8_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)character;
    (void)color;
}
void video_draw_box(int x, int y, int width, int height, uint8_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)color;
}

void taskbar_draw(void) {}
int taskbar_get_work_area(tb_rect_t* area) {
    (void)area;
    return ERR_UNAVAILABLE;
}

void wm_request_hosted_redraw(wm_app_type_t app_type) {
    (void)app_type;
}

void mouse_invalidate_cursor(void) {}

vesa_mode_t* vesa_get_mode(void) {
    return 0;
}
int vesa_has_backbuffer(void) {
    return 0;
}
void vesa_clear(vesa_color_t color) {
    (void)color;
}
void vesa_draw_hline(uint32_t x, uint32_t y, uint32_t width,
                     vesa_color_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)color;
}
void vesa_frame_begin(void) {}
void vesa_frame_end(void) {}

int display_get_metrics(display_metrics_t* metrics) {
    (void)metrics;
    return ERR_UNAVAILABLE;
}

int storage_get_status(storage_status_t* status) {
    (void)status;
    return ERR_UNAVAILABLE;
}
int storage_get_disk_at(uint8_t index, storage_disk_t* disk) {
    (void)index;
    (void)disk;
    return ERR_UNAVAILABLE;
}
int storage_get_mounted_at(uint8_t index, storage_volume_t* volume) {
    (void)index;
    (void)volume;
    return ERR_UNAVAILABLE;
}
const char* storage_fs_name(storage_fs_type_t type) {
    (void)type;
    return "none";
}

uint32_t memory_get_total(void) { return 0U; }
uint32_t memory_get_free(void) { return 0U; }
uint32_t memory_get_used(void) { return 0U; }

process_t* processes[MAX_PROCESSES] = {0};

void gui_draw_scaled_window_frame(uint32_t x, uint32_t y, uint32_t width,
                                  uint32_t height, const char* title,
                                  int active) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)title;
    (void)active;
}

uint32_t display_scale_px(uint32_t value) {
    return value;
}

void gui_draw_scaled_text(uint32_t x, uint32_t y, const char* text,
                          uint32_t color) {
    (void)x;
    (void)y;
    (void)text;
    (void)color;
}
void gui_draw_rounded_rect(uint32_t x, uint32_t y, uint32_t width,
                           uint32_t height, uint32_t radius, uint32_t color) {
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
    result = settings_host_test_icon_editor();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != OK) {
        printf("settings-icons-host: FAIL code=%d\n", result);
        return 1;
    }
    printf("settings-icons-host: PASS\n");
    return 0;
}
