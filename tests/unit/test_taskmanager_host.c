#include <stdint.h>
#include <stdio.h>

#include "apps/taskmanager_test.h"
#include "apps/shell_introspection.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/power.h"
#include "core/recovery.h"
#include "core/string.h"
#include "drivers/ata.h"
#include "drivers/vesa.h"
#include "fs/vfs.h"
#include "process/process.h"
#include "process/signal.h"
#include "process/thread.h"
#include "ui/display.h"
#include "ui/gui.h"
#include "ui/taskbar.h"
#include "ui/desktop.h"
#include "ui/wm.h"

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

void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t width,
                    uint32_t height, vesa_color_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)color;
}

int recovery_is_enabled(recovery_component_id_t component) {
    return component == RECOVERY_COMPONENT_TASKMANAGER;
}

uint32_t memory_get_total(void) {
    return 64U * 1024U * 1024U;
}

uint32_t memory_get_free(void) {
    return 48U * 1024U * 1024U;
}

uint32_t memory_get_used(void) {
    return 16U * 1024U * 1024U;
}

uint32_t memory_get_total_pages(void) {
    return 16384U;
}

uint32_t memory_get_free_pages(void) {
    return 12288U;
}

int memory_get_detailed_stats(memory_detailed_stats_t* stats) {
    if (!stats) return ERR_NULL;
    stats->total_pages = 16384U;
    stats->zone_pages[MEMORY_ZONE_KERNEL] = 128U;
    stats->zone_pages[MEMORY_ZONE_HEAP] = 512U;
    stats->zone_pages[MEMORY_ZONE_SLAB] = 256U;
    stats->zone_pages[MEMORY_ZONE_PROCESS] = 128U;
    stats->zone_pages[MEMORY_ZONE_BUFFER] = 64U;
    stats->zone_pages[MEMORY_ZONE_FREE] = 12288U;
    stats->free_runs = 4U;
    stats->largest_free_run = 8192U;
    stats->isolated_free_pages = 2U;
    stats->fragmentation_percent = 1U;
    stats->initialized = 1U;
    stats->valid = 1U;
    return OK;
}

static ata_device_t fixture_ata_device;

ata_device_t* ata_get_device(void) {
    return &fixture_ata_device;
}

uint32_t ata_get_read_ops(void) {
    return 3U;
}

uint32_t ata_get_write_ops(void) {
    return 1U;
}

void video_clear(void) {
}

void taskbar_draw(void) {
}

void taskbar_add_app(tb_app_type_t type, const char* name) {
    (void)type;
    (void)name;
}

int gui_measure_scaled_text(const char* text, uint32_t* width,
                            uint32_t* height) {
    if (!text || !width || !height) return ERR_NULL;
    *width = 8U;
    *height = 16U;
    return OK;
}

void vesa_frame_begin(void) {
}

void vesa_frame_begin_region(uint32_t x, uint32_t y, uint32_t width,
                             uint32_t height) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

void vesa_frame_end(void) {
}

void vesa_clear(vesa_color_t color) {
    (void)color;
}

void mouse_invalidate_cursor(void) {
}

void shell_print_prompt(void) {
}

void fm_run(void) {
}

int power_reboot(void) {
    return ERR_UNAVAILABLE;
}

int power_shutdown_request(void) {
    return ERR_UNAVAILABLE;
}

void video_print(const char* str, uint8_t color) {
    (void)str;
    (void)color;
}

void shell_command_print_num(uint32_t value) {
    (void)value;
}

void settings_open(void) {
}

void shell_handle_app_request(uint32_t request) {
    (void)request;
}

int taskbar_handle_config_key(uint8_t scancode) {
    (void)scancode;
    return 0;
}

int taskbar_handle_key(uint8_t scancode) {
    (void)scancode;
    return 0;
}

void process_destroy(process_t* proc) {
    (void)proc;
}

process_t* process_get_by_pid(uint32_t pid) {
    (void)pid;
    return NULL;
}

int process_is_user(const process_t* proc) {
    (void)proc;
    return 0;
}

int process_signal_send(uint32_t pid, uint32_t signal_number) {
    (void)pid;
    (void)signal_number;
    return OK;
}

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

void wm_request_hosted_redraw(wm_app_type_t app_type) {
    (void)app_type;
}

int wm_register_hosted_app(const wm_hosted_app_t* app) {
    (void)app;
    return OK;
}

desktop_mode_t desktop_get_mode(void) {
    return DESKTOP_MODE_CLASSIC;
}

void wm_set_active(int active) {
    (void)active;
}

static uint8_t fixture_ipc_exit_sent;
static uint32_t fixture_ipc_waits;
static uint32_t fixture_yield_count;

int ipc_receive(ipc_msg_t* message) {
    if (!message || fixture_ipc_exit_sent || fixture_ipc_waits < 6U) return 0;
    message->type = IPC_MSG_KEYBOARD;
    message->data1 = 0x01U;
    message->data2 = 0U;
    fixture_ipc_exit_sent = 1U;
    return 1;
}

int ipc_wait(uint32_t timeout_ticks, wait_reason_t* out_reason) {
    (void)timeout_ticks;
    if (out_reason) *out_reason = WAIT_REASON_NONE;
    fixture_ipc_waits++;
    return OK;
}

void process_yield(void) {
    fixture_yield_count++;
}

void taskbar_remove_app(tb_app_type_t app) {
    (void)app;
}

void desktop_set_active(int active) {
    (void)active;
}

int desktop_is_active(void) {
    return 0;
}

void desktop_draw(void) {
}

void video_terminal_begin(void) {
}

int wm_close_hosted_app(wm_app_type_t app_type) {
    (void)app_type;
    return OK;
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
    return fixture_ipc_waits + 1U;
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

thread_t* thread_get_by_id(uint32_t id) {
    (void)id;
    return NULL;
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
    if (result == 0 && fixture_yield_count == 0U) {
        result = 51;
    }
    if (result != 0) printf("TASKMANAGER_HOST_FAIL:%d\n", result);
    coverage_emit(result);
    return result == 0 ? 0 : 1;
}
