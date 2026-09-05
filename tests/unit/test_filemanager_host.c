#include <stdint.h>
#include <stdio.h>

#include "core/log.h"
#include "core/errors.h"
#include "drivers/vesa.h"
#include "fs/file_index.h"
#include "fs/fs.h"
#include "fs/storage.h"
#include "ui/filemanager_test.h"
#include "ui/taskbar.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static vesa_mode_t fixture_vesa_mode;

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
    return 0;
}

int taskbar_get_work_area(tb_rect_t* area) {
    (void)area;
    return ERR_UNAVAILABLE;
}

int storage_get_status(storage_status_t* out_status) {
    (void)out_status;
    return ERR_UNAVAILABLE;
}

int storage_get_mounted_at(uint8_t index, storage_volume_t* out_volume) {
    (void)index;
    (void)out_volume;
    return ERR_UNAVAILABLE;
}

int storage_find_volume(const char* id, storage_volume_t* out_volume) {
    (void)id;
    (void)out_volume;
    return ERR_UNAVAILABLE;
}

int storage_list_dir_long(const char* id, const char* path,
                          storage_long_dir_entry_t* entries,
                          uint32_t capacity, uint32_t* out_count) {
    (void)id;
    (void)path;
    (void)entries;
    (void)capacity;
    (void)out_count;
    return ERR_UNAVAILABLE;
}

int file_index_get_status(file_index_status_t* out_status) {
    (void)out_status;
    return ERR_UNAVAILABLE;
}

int file_index_search(const char* query, file_index_result_t* results,
                      uint32_t capacity,
                      file_index_search_status_t* out_status) {
    (void)query;
    (void)results;
    (void)capacity;
    (void)out_status;
    return ERR_UNAVAILABLE;
}

int fs_get_file_count_at(const char* dir_path) {
    (void)dir_path;
    return ERR_UNAVAILABLE;
}

int fs_get_file_info_at(const char* dir_path, int index, char* name_out,
                        uint32_t* size_out, uint8_t* attr_out) {
    (void)dir_path;
    (void)index;
    (void)name_out;
    (void)size_out;
    (void)attr_out;
    return ERR_UNAVAILABLE;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = fm_host_test_contracts();
    coverage_active = 0U;
    if (result != 0) printf("FILEMANAGER_HOST_FAIL:%d\n", result);
    coverage_emit(result);
    return result == 0 ? 0 : 1;
}
