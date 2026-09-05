#include <stdint.h>
#include <stdio.h>

#include "core/log.h"
#include "core/errors.h"
#include "core/string.h"
#include "drivers/vesa.h"
#include "fs/file_index.h"
#include "fs/fs.h"
#include "fs/storage.h"
#include "ui/desktop.h"
#include "ui/display.h"
#include "ui/filemanager.h"
#include "ui/filemanager_test.h"
#include "ui/gui.h"
#include "ui/icons.h"
#include "ui/taskbar.h"
#include "ui/wm.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static vesa_mode_t fixture_vesa_mode;
static int fixture_storage_ready;
static int fixture_data_present;
static int fixture_data_mounted;
static desktop_mode_t fixture_desktop_mode;
static icon_entry_t fixture_icons[ICON_FM_COUNT] = {
    {'D', 0x0BU, 0x70U},
    {'F', 0x07U, 0x70U}
};
static storage_volume_t fixture_volumes[2];
static storage_long_dir_entry_t fixture_data_entries[2];
static uint32_t fixture_data_entry_count;
static char fixture_root_names[16][FM_NAME_LEN];
static uint8_t fixture_root_attributes[16];
static uint32_t fixture_root_count;
static int fixture_index_ready;
static file_index_status_t fixture_index_status;
static file_index_result_t fixture_search_result;
static int fixture_search_result_code;
static int fixture_rename_result;
static int fixture_delete_result;
static uint8_t fixture_view_buffer[4096];

static void fixture_copy(char* destination, const char* source) {
    uint32_t index = 0U;

    while (source && source[index] && index + 1U < FM_NAME_LEN) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int fixture_equal(const char* left, const char* right) {
    uint32_t index = 0U;

    while (left && right && left[index] && right[index]) {
        if (left[index] != right[index]) return 0;
        index++;
    }
    return left && right && left[index] == right[index];
}

static void fixture_reset(void) {
    fixture_storage_ready = 1;
    fixture_data_present = 1;
    fixture_data_mounted = 1;
    fixture_desktop_mode = DESKTOP_MODE_SIMPLE;
    kmemset(&fixture_vesa_mode, 0, sizeof(fixture_vesa_mode));
    fixture_vesa_mode.width = VESA_WIDTH_1024;
    fixture_vesa_mode.height = VESA_HEIGHT_768;
    fixture_vesa_mode.initialized = 1U;
    fixture_data_entry_count = 2U;
    fixture_root_count = 0U;
    fixture_index_ready = 1;
    fixture_search_result_code = OK;
    fixture_rename_result = OK;
    fixture_delete_result = OK;

    kmemset(fixture_volumes, 0, sizeof(fixture_volumes));
    fixture_copy(fixture_volumes[0].id, "C:");
    fixture_volumes[0].mounted = 1U;
    fixture_volumes[0].boot = 1U;
    fixture_volumes[0].generation = 10U;
    fixture_volumes[0].sector_count = 4096U;
    fixture_volumes[0].state = STORAGE_VOLUME_MOUNTED;
    fixture_copy(fixture_volumes[1].id, "DATA");
    fixture_volumes[1].mounted = 1U;
    fixture_volumes[1].generation = 7U;
    fixture_volumes[1].read_only = 1U;
    fixture_volumes[1].sector_count = 8192U;
    fixture_volumes[1].state = STORAGE_VOLUME_MOUNTED;

    kmemset(fixture_data_entries, 0, sizeof(fixture_data_entries));
    fixture_copy(fixture_data_entries[0].name, "notes.txt");
    fixture_data_entries[0].size = 12U;
    fixture_copy(fixture_data_entries[1].name, "docs");
    fixture_data_entries[1].attributes = 0x10U;
    fixture_data_entries[1].is_directory = 1U;

    kmemset(&fixture_index_status, 0, sizeof(fixture_index_status));
    fixture_index_status.state = FILE_INDEX_STATE_READY;
    fixture_index_status.initialized = 1U;
    fixture_index_status.event_generation = 4U;
    kmemset(&fixture_search_result, 0, sizeof(fixture_search_result));
    fixture_copy(fixture_search_result.entry.volume_id, "DATA");
    fixture_copy(fixture_search_result.entry.parent_path, "/docs");
    fixture_copy(fixture_search_result.entry.name, "notes.txt");
    fixture_search_result.entry.volume_generation = 7U;
    fixture_search_result.entry.size = 12U;
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
    printf("ZCOV_BEGIN|case=host:ui:filemanager|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:ui:filemanager|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:ui:filemanager|value=0x%08X\n",
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

vesa_mode_t* vesa_get_mode(void) {
    return &fixture_vesa_mode;
}

int vesa_has_backbuffer(void) {
    return 1;
}

void vesa_clear(vesa_color_t color) {
    (void)color;
}

void vesa_draw_char(int x, int y, char c, vesa_color_t color,
                    uint32_t scale) {
    (void)x;
    (void)y;
    (void)c;
    (void)color;
    (void)scale;
}

void vesa_frame_begin(void) {}

void vesa_frame_end(void) {}

void video_clear(void) {}

void video_put_char_at(char c, uint8_t color, int x, int y) {
    (void)c;
    (void)color;
    (void)x;
    (void)y;
}

void video_print_at(int x, int y, const char* str, uint8_t color) {
    (void)x;
    (void)y;
    (void)str;
    (void)color;
}

void video_fill_rect(int x, int y, int w, int h, char c, uint8_t color) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)c;
    (void)color;
}

void video_draw_hline(int x, int y, int w, char c, uint8_t color) {
    (void)x;
    (void)y;
    (void)w;
    (void)c;
    (void)color;
}

void video_draw_vline(int x, int y, int h, char c, uint8_t color) {
    (void)x;
    (void)y;
    (void)h;
    (void)c;
    (void)color;
}

void video_draw_box(int x, int y, int w, int h, uint8_t color) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
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

icon_entry_t* icons_get_fm(icon_fm_id_t id) {
    if ((uint32_t)id >= ICON_FM_COUNT) return 0;
    return &fixture_icons[id];
}

void taskbar_draw(void) {}

void mouse_invalidate_cursor(void) {}

void wm_request_hosted_redraw(wm_app_type_t app_type) {
    (void)app_type;
}

void* kmalloc(uint32_t size) {
    if (size > sizeof(fixture_view_buffer)) return 0;
    return fixture_view_buffer;
}

void kfree(void* ptr) {
    (void)ptr;
}

int fs_read_file_at(const char* path, uint8_t* buffer, uint32_t max_size) {
    (void)path;
    (void)buffer;
    (void)max_size;
    return ERR_UNAVAILABLE;
}

int storage_read_file_range(const char* id, const char* path,
                            uint32_t offset, uint8_t* buffer,
                            uint32_t max_size, uint32_t* out_read) {
    (void)id;
    (void)path;
    (void)offset;
    (void)buffer;
    (void)max_size;
    if (out_read) *out_read = 0U;
    return ERR_UNAVAILABLE;
}

int taskbar_get_work_area(tb_rect_t* area) {
    (void)area;
    return ERR_UNAVAILABLE;
}

int storage_get_status(storage_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    if (!fixture_storage_ready) return ERR_UNAVAILABLE;
    kmemset(out_status, 0, sizeof(*out_status));
    out_status->initialized = 1U;
    out_status->disk_count = 1U;
    out_status->volume_count = fixture_data_present ? 2U : 1U;
    out_status->mounted_count = fixture_data_mounted ? 2U : 1U;
    return OK;
}

int storage_get_mounted_at(uint8_t index, storage_volume_t* out_volume) {
    if (!out_volume) return ERR_NULL;
    if (!fixture_storage_ready) return ERR_UNAVAILABLE;
    if (index == 0U) {
        *out_volume = fixture_volumes[0];
        return OK;
    }
    if (index == 1U && fixture_data_mounted) {
        *out_volume = fixture_volumes[1];
        return OK;
    }
    return ERR_NOT_FOUND;
}

int storage_find_volume(const char* id, storage_volume_t* out_volume) {
    if (!id || !out_volume) return ERR_NULL;
    if (!fixture_storage_ready) return ERR_UNAVAILABLE;
    if (fixture_equal(id, fixture_volumes[0].id)) {
        *out_volume = fixture_volumes[0];
        return OK;
    }
    if (fixture_equal(id, fixture_volumes[1].id) && fixture_data_present) {
        *out_volume = fixture_volumes[1];
        out_volume->mounted = fixture_data_mounted ? 1U : 0U;
        return OK;
    }
    return ERR_NOT_FOUND;
}

int storage_find_system_volume(storage_volume_t* out_volume) {
    if (!out_volume) return ERR_NULL;
    if (!fixture_storage_ready) return ERR_UNAVAILABLE;
    *out_volume = fixture_volumes[0];
    return OK;
}

int storage_list_dir_long(const char* id, const char* path,
                          storage_long_dir_entry_t* entries,
                          uint32_t capacity, uint32_t* out_count) {
    (void)path;
    if (!id || !entries || !out_count) return ERR_NULL;
    if (!fixture_storage_ready || !fixture_data_mounted) {
        return ERR_UNAVAILABLE;
    }
    if (!fixture_equal(id, fixture_volumes[1].id)) return ERR_NOT_FOUND;
    if (capacity < fixture_data_entry_count) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < fixture_data_entry_count; index++) {
        entries[index] = fixture_data_entries[index];
    }
    *out_count = fixture_data_entry_count;
    return OK;
}

int file_index_get_status(file_index_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    if (!fixture_index_ready) return ERR_UNAVAILABLE;
    *out_status = fixture_index_status;
    return OK;
}

int file_index_search(const char* query, file_index_result_t* results,
                      uint32_t capacity,
                      file_index_search_status_t* out_status) {
    if (!query || !results || !out_status) return ERR_NULL;
    if (!fixture_index_ready) return ERR_UNAVAILABLE;
    if (capacity == 0U) return ERR_OVERFLOW;
    if (fixture_search_result_code != OK) return fixture_search_result_code;
    results[0] = fixture_search_result;
    kmemset(out_status, 0, sizeof(*out_status));
    out_status->total_matches = 1U;
    out_status->returned_matches = 1U;
    return OK;
}

int fs_get_file_count_at(const char* dir_path) {
    (void)dir_path;
    return (int)fixture_root_count;
}

int fs_get_file_info_at(const char* dir_path, int index, char* name_out,
                        uint32_t* size_out, uint8_t* attr_out) {
    (void)dir_path;
    if (index < 0 || (uint32_t)index >= fixture_root_count || !name_out ||
        !attr_out) return ERR_INVALID;
    fixture_copy(name_out, fixture_root_names[index]);
    if (size_out) *size_out = 0U;
    *attr_out = fixture_root_attributes[index];
    return OK;
}

int fs_create_dir_entry(const char* dir_path, const char* name,
                        uint8_t attributes) {
    (void)dir_path;
    if (!name) return ERR_NULL;
    if (fixture_root_count >= 16U) return ERR_OVERFLOW;
    fixture_copy(fixture_root_names[fixture_root_count], name);
    fixture_root_attributes[fixture_root_count] = attributes;
    fixture_root_count++;
    return OK;
}

int fs_rename_file_in_dir(const char* dir_path, const char* old_name,
                          const char* new_name) {
    (void)dir_path;
    (void)old_name;
    (void)new_name;
    return fixture_rename_result;
}

int fs_atomic_delete_file_in_dir(const char* dir_path, const char* name) {
    (void)dir_path;
    (void)name;
    return fixture_delete_result;
}

void taskbar_remove_app(tb_app_type_t type) {
    (void)type;
}

void desktop_set_active(int active) {
    (void)active;
}

void desktop_draw(void) {}

desktop_mode_t desktop_get_mode(void) {
    return fixture_desktop_mode;
}

int display_get_metrics(display_metrics_t* metrics) {
    if (!metrics) return ERR_NULL;
    kmemset(metrics, 0, sizeof(*metrics));
    metrics->font_width = 8U;
    metrics->font_height = 16U;
    metrics->scale = DISPLAY_SCALE_NORMAL;
    return OK;
}

int wm_close_hosted_app(wm_app_type_t app_type) {
    (void)app_type;
    return OK;
}

void filemanager_host_set_storage_fixture(int ready, int data_present,
                                           int data_mounted) {
    fixture_storage_ready = ready;
    fixture_data_present = data_present;
    fixture_data_mounted = data_mounted;
}

void filemanager_host_reset_fs_fixture(void) {
    fixture_root_count = 0U;
    kmemset(fixture_root_names, 0, sizeof(fixture_root_names));
    kmemset(fixture_root_attributes, 0, sizeof(fixture_root_attributes));
}

void filemanager_host_set_index_fixture(int ready, int result_code) {
    fixture_index_ready = ready;
    fixture_search_result_code = result_code;
}

void filemanager_host_set_rename_result(int result) {
    fixture_rename_result = result;
}

void filemanager_host_set_delete_result(int result) {
    fixture_delete_result = result;
}

void filemanager_host_set_desktop_mode(desktop_mode_t mode) {
    fixture_desktop_mode = mode;
}

int main(void) {
    int result;

    fixture_reset();
    coverage_active = 1U;
    result = fm_host_test_contracts();
    coverage_active = 0U;
    if (result != 0) printf("FILEMANAGER_HOST_FAIL:%d\n", result);
    coverage_emit(result);
    return result == 0 ? 0 : 1;
}
