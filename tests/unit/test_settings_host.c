#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/video.h"
#include "drivers/vesa.h"
#include "drivers/mouse.h"
#include "fs/storage.h"
#include "process/process.h"
#include "settings_test.h"
#include "ui/display.h"
#include "ui/desktop.h"
#include "ui/gui.h"
#include "ui/icons.h"
#include "ui/taskbar.h"
#include "ui/wm.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static desktop_mode_t host_desktop_mode = DESKTOP_MODE_SIMPLE;
static uint8_t host_recovery_enabled = 1U;
static int host_mouse_result = OK;
static int host_display_result = OK;
static int host_storage_result = ERR_UNAVAILABLE;
static int host_wm_register_result = OK;
static tb_config_t host_taskbar_config = {
    TB_POS_BOTTOM, TB_SIZE_MEDIUM, 1, 0, 0, SCREEN_COLS, 1
};
static mouse_config_t host_mouse_config = {
    MOUSE_SPEED_DEFAULT, 0U, MOUSE_PRIMARY_LEFT
};
static display_metrics_t host_display_metrics = {
    DISPLAY_SCALE_NORMAL, 1U, 1U, 8U, 16U, 2U, 24U, 24U,
    72U, 24U, 16U, 24U, 32U, 720U, 450U, 1U
};
static vesa_mode_t host_vesa_mode = {0};
static uint8_t host_vesa_available;
static uint8_t host_backbuffer_available;
static storage_status_t host_storage_status = {0};
static storage_disk_t host_storage_disk = {0};
static storage_volume_t host_storage_volume = {0};
static icon_registry_t host_icon_registry = {
    {{'S', 7U, 15U}, {'E', 7U, 15U}, {'T', 7U, 15U}},
    {{'X', 7U, 15U}, {'_', 7U, 15U}, {'^', 7U, 15U}},
    {{'D', 7U, 15U}, {'F', 7U, 15U}},
    {{'W', 7U, 15U}}
};

void settings_host_fixture_simple(void) {
    host_desktop_mode = DESKTOP_MODE_SIMPLE;
    host_vesa_available = 0U;
    host_backbuffer_available = 0U;
}

void settings_host_fixture_classic(void) {
    host_desktop_mode = DESKTOP_MODE_CLASSIC;
    host_vesa_available = 1U;
    host_backbuffer_available = 1U;
    host_vesa_mode.width = 800U;
    host_vesa_mode.height = 600U;
    host_vesa_mode.bpp = 32U;
    host_vesa_mode.pitch = 3200U;
    host_vesa_mode.initialized = 1U;
}

void settings_host_fixture_storage(void) {
    host_storage_result = OK;
    host_storage_status.initialized = 1U;
    host_storage_status.disk_count = 1U;
    host_storage_status.volume_count = 1U;
    host_storage_status.mounted_count = 1U;
    host_storage_disk.id[0] = 'D';
    host_storage_disk.id[1] = '0';
    host_storage_disk.id[2] = '\0';
    host_storage_disk.sector_count = 128U;
    host_storage_volume.id[0] = 'V';
    host_storage_volume.id[1] = '0';
    host_storage_volume.id[2] = '\0';
    host_storage_volume.fs_type = STORAGE_FS_FAT32;
    host_storage_volume.boot = 1U;
}

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
tb_config_t* taskbar_get_config(void) { return &host_taskbar_config; }
void taskbar_set_position(tb_position_t position) {
    host_taskbar_config.position = position;
}
void taskbar_set_icon_size(tb_icon_size_t size) {
    host_taskbar_config.icon_size = size;
}
void taskbar_set_pinned(int pinned) { host_taskbar_config.pinned = pinned; }
int taskbar_get_work_area(tb_rect_t* area) {
    if (!area) return ERR_NULL;
    area->x = 0;
    area->y = 0;
    area->width = SCREEN_COLS * 8;
    area->height = SCREEN_ROWS * 10;
    return host_vesa_available ? OK : ERR_UNAVAILABLE;
}

void taskbar_set_custom_position(int x, int y) {
    host_taskbar_config.custom_x = x;
    host_taskbar_config.custom_y = y;
}

void wm_request_hosted_redraw(wm_app_type_t app_type) {
    (void)app_type;
}
void wm_set_active(int active) { (void)active; }
int wm_register_hosted_app(const wm_hosted_app_t* app) {
    return app ? host_wm_register_result : ERR_NULL;
}
int wm_close_hosted_app(wm_app_type_t app_type) {
    (void)app_type;
    return OK;
}
void wm_set_btn_position(wm_btn_position_t position) { (void)position; }
void wm_set_btn_order(wm_btn_order_t order) { (void)order; }
void wm_set_show_title(int show) { (void)show; }
void wm_set_border_style(int style) { (void)style; }

desktop_mode_t desktop_get_mode(void) { return host_desktop_mode; }
void desktop_set_active(int active) { (void)active; }
void desktop_draw(void) {}

int recovery_is_enabled(recovery_component_id_t component) {
    (void)component;
    return host_recovery_enabled;
}

int mouse_get_config(mouse_config_t* config) {
    if (!config) return ERR_NULL;
    *config = host_mouse_config;
    return host_mouse_result == OK ? OK : host_mouse_result;
}
int mouse_set_speed(uint8_t speed) {
    if (host_mouse_result != OK) return host_mouse_result;
    host_mouse_config.speed = speed;
    return OK;
}
int mouse_set_acceleration(int enabled) {
    if (host_mouse_result != OK) return host_mouse_result;
    host_mouse_config.acceleration_enabled = enabled ? 1U : 0U;
    return OK;
}
int mouse_set_primary_button(mouse_primary_button_t button) {
    if (host_mouse_result != OK) return host_mouse_result;
    host_mouse_config.primary_button = button;
    return OK;
}

void mouse_invalidate_cursor(void) {}

vesa_mode_t* vesa_get_mode(void) {
    return host_vesa_available ? &host_vesa_mode : 0;
}
int vesa_has_backbuffer(void) {
    return host_backbuffer_available;
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
    if (!metrics) return ERR_NULL;
    if (host_display_result != OK) return host_display_result;
    *metrics = host_display_metrics;
    return OK;
}
int display_apply_scale(display_scale_t scale) {
    if (scale < DISPLAY_SCALE_SMALL || scale >= DISPLAY_SCALE_COUNT) {
        return ERR_INVALID;
    }
    host_display_metrics.scale = scale;
    return host_display_result;
}

int storage_get_status(storage_status_t* status) {
    if (!status) return ERR_NULL;
    if (host_storage_result != OK) return host_storage_result;
    *status = host_storage_status;
    return OK;
}
int storage_get_disk_at(uint8_t index, storage_disk_t* disk) {
    if (!disk) return ERR_NULL;
    if (index != 0U || host_storage_result != OK) return ERR_NOT_FOUND;
    *disk = host_storage_disk;
    return OK;
}
int storage_get_mounted_at(uint8_t index, storage_volume_t* volume) {
    if (!volume) return ERR_NULL;
    if (index != 0U || host_storage_result != OK) return ERR_NOT_FOUND;
    *volume = host_storage_volume;
    return OK;
}
const char* storage_fs_name(storage_fs_type_t type) {
    (void)type;
    return "none";
}

uint32_t memory_get_total(void) { return 0U; }
uint32_t memory_get_free(void) { return 0U; }
uint32_t memory_get_used(void) { return 0U; }

void power_reboot(void) {}

icon_registry_t* icons_get_registry(void) { return &host_icon_registry; }
void icons_reset_defaults(void) {}

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
    result += settings_host_test_contracts();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != OK) {
        printf("settings-icons-host: FAIL code=%d\n", result);
        return 1;
    }
    printf("settings-icons-host: PASS\n");
    return 0;
}
