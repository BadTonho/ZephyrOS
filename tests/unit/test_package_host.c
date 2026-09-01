#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/app_package.h"
#include "core/app_loader.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "fs/fs.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

static void __attribute__((no_instrument_function))
coverage_record(void* function) {
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
    printf("ZCOV_BEGIN|case=host:core:app-package|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:app-package|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:app-package|value=0x%08X\n",
           (uint32_t)result);
}

uint8_t fs_get_type(void) {
    return FS_TYPE_NONE;
}

void* kmalloc(uint32_t size) {
    (void)size;
    return 0;
}

void kfree(void* ptr) {
    (void)ptr;
}

int fs_read_file(const char* filename, uint8_t* buffer, uint32_t max_size) {
    (void)filename;
    (void)buffer;
    (void)max_size;
    return ERR_UNAVAILABLE;
}

int fs_read_file_at(const char* path, uint8_t* buffer, uint32_t max_size) {
    return fs_read_file(path, buffer, max_size);
}

int fs_get_file_count_at(const char* dir_path) {
    (void)dir_path;
    return 0;
}

int fs_get_file_info_at(const char* dir_path, int index, char* name_out,
                        uint32_t* size_out, uint8_t* attr_out) {
    (void)dir_path;
    (void)index;
    (void)name_out;
    (void)size_out;
    (void)attr_out;
    return ERR_NOT_FOUND;
}

int fs_get_info(fs_info_t* info) {
    (void)info;
    return ERR_UNAVAILABLE;
}

int fs_get_root_file_info(const char* filename, uint32_t* size_out,
                          uint8_t* attributes_out) {
    (void)filename;
    (void)size_out;
    (void)attributes_out;
    return ERR_NOT_FOUND;
}

int fs_create_dir_entry(const char* dir_path, const char* name,
                        uint8_t attributes) {
    (void)dir_path;
    (void)name;
    (void)attributes;
    return ERR_UNAVAILABLE;
}

int fs_write_file_in_dir(const char* dir_path, const char* filename,
                         const uint8_t* data, uint32_t size) {
    (void)dir_path;
    (void)filename;
    (void)data;
    (void)size;
    return ERR_UNAVAILABLE;
}

int fs_delete_file_in_dir(const char* dir_path, const char* filename) {
    (void)dir_path;
    (void)filename;
    return ERR_UNAVAILABLE;
}

int fs_atomic_delete_file_in_dir(const char* dir_path, const char* filename) {
    return fs_delete_file_in_dir(dir_path, filename);
}

int fs_atomic_write_root(const char* filename, const uint8_t* data,
                         uint32_t size, uint8_t attributes,
                         fs_atomic_mode_t mode) {
    (void)filename;
    (void)data;
    (void)size;
    (void)attributes;
    (void)mode;
    return ERR_UNAVAILABLE;
}

int fs_atomic_delete_root(const char* filename) {
    (void)filename;
    return ERR_UNAVAILABLE;
}

int fs_atomic_write_file_in_dir(const char* dir_path, const char* filename,
                                const uint8_t* data, uint32_t size,
                                uint8_t attributes, fs_atomic_mode_t mode) {
    (void)dir_path;
    (void)filename;
    (void)data;
    (void)size;
    (void)attributes;
    (void)mode;
    return ERR_UNAVAILABLE;
}

int app_loader_is_ready(void) {
    return 0;
}

int app_loader_is_foreground_active(void) {
    return 0;
}

int app_loader_validate_image(const uint8_t* image, uint32_t size,
                              app_image_header_t* header) {
    (void)image;
    (void)size;
    (void)header;
    return ERR_INVALID;
}

int app_loader_run_file_with_launch(const char* path,
                                    const app_launch_info_t* launch,
                                    uint32_t* pid_out) {
    (void)path;
    (void)launch;
    (void)pid_out;
    return ERR_UNAVAILABLE;
}

uint32_t timer_get_ticks(void) {
    return 100U;
}

uint8_t serial_is_ready(void) {
    return 1U;
}

uint32_t serial_write_text(const char* text, uint32_t length) {
    (void)text;
    return length;
}

uint32_t serial_flush(uint32_t budget) {
    return budget;
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void video_newline(void) {
}

static int expect_version(const char* left, const char* right, int expected) {
    int comparison = 99;

    if (app_package_compare_versions(left, right, &comparison) != OK) {
        return 0;
    }
    return comparison == expected;
}

static int test_names(void) {
    static const char* const reasons[] = {
        "NONE", "INVALID_ARGUMENT", "SOURCE_NOT_FOUND", "PACKAGE_INVALID",
        "ALIAS_MISMATCH", "DEPENDENCY_MISSING", "INSUFFICIENT_SPACE",
        "ALREADY_INSTALLED", "NOT_INSTALLED", "DEPENDENT_INSTALLED",
        "FILESYSTEM_UNAVAILABLE", "LOADER_UNAVAILABLE",
        "PACKAGE_SERVICE_UNAVAILABLE", "LOADER_BUSY", "MUTATION_BUSY",
        "READ_ERROR", "WRITE_ERROR", "UPDATE_NOT_AVAILABLE",
        "DOWNGRADE_REQUIRES_CONFIRM", "PLAN_INCOMPLETE", "PLAN_CYCLE",
        "PLAN_CONFLICT", "TRANSACTION_UNAVAILABLE", "TRANSACTION_PENDING",
        "ROLLBACK_UNAVAILABLE", "RECOVERY_FAILED", "HISTORY_UNAVAILABLE"
    };
    static const char* const operations[] = {
        "NONE", "INSTALL", "REMOVE", "UPDATE", "ROLLBACK", "RECOVERY"
    };
    static const char* const plan_actions[] = {
        "NONE", "INSTALL", "UPDATE"
    };
    static const char* const outcomes[] = {
        "NONE", "SUCCESS", "FAILED", "RECOVERED"
    };

    for (uint32_t index = 0U; index < sizeof(reasons) / sizeof(reasons[0]);
         index++) {
        if (strcmp(app_package_action_reason_name(
                       (app_package_action_reason_t)index), reasons[index]) != 0) {
            return 0;
        }
    }
    if (strcmp(app_package_action_reason_name((app_package_action_reason_t)99),
               "UNKNOWN") != 0) return 0;
    for (uint32_t index = 0U;
         index < sizeof(plan_actions) / sizeof(plan_actions[0]);
         index++) {
        if (strcmp(app_package_plan_action_name(
                       (app_package_plan_action_t)index), plan_actions[index]) != 0) {
            return 0;
        }
    }
    if (strcmp(app_package_plan_action_name((app_package_plan_action_t)99),
               "NONE") != 0) return 0;
    for (uint32_t index = 0U; index < sizeof(operations) / sizeof(operations[0]);
         index++) {
        if (strcmp(app_package_history_operation_name(
                       (app_package_history_operation_t)index),
                   operations[index]) != 0) return 0;
    }
    if (strcmp(app_package_history_operation_name(
                   (app_package_history_operation_t)99), "NONE") != 0) {
        return 0;
    }
    for (uint32_t index = 0U; index < sizeof(outcomes) / sizeof(outcomes[0]);
         index++) {
        if (strcmp(app_package_history_outcome_name(
                       (app_package_history_outcome_t)index), outcomes[index]) != 0) {
            return 0;
        }
    }
    return strcmp(app_package_history_outcome_name(
                      (app_package_history_outcome_t)99), "NONE") == 0;
}

int main(void) {
    app_package_status_t status;
    app_package_history_entry_t history;
    uint32_t history_count = 1U;
    int comparison;
    int result = 0;

    coverage_active = 1U;
    log_init();
    if (app_package_is_ready() != 0 || app_package_is_mutation_active() != 0) {
        result = 1;
    }
    if (!result && app_package_compare_versions(NULL, "1.0.0", &comparison) !=
                    ERR_NULL) result = 2;
    if (!result && app_package_compare_versions("1.0", "1.0.0", &comparison) !=
                    ERR_INVALID) result = 3;
    if (!result && !expect_version("1.2.3", "1.2.3", 0)) result = 4;
    if (!result && !expect_version("1.2.10", "1.2.3", 1)) result = 5;
    if (!result && !expect_version("01.002.0003", "1.2.3", 0)) result = 6;
    if (!result && !expect_version("0.0.0", "0.0.1", -1)) result = 7;
    if (!result && app_package_compare_versions("1.0.0", "1.0.0", NULL) !=
                    ERR_NULL) result = 8;
    if (!result && app_package_get_status(NULL) != ERR_NULL) result = 9;
    if (!result && (app_package_get_status(&status) != OK ||
                    status.operation_generation != 0U ||
                    status.rollback_count != 0U)) result = 10;
    if (!result && app_package_get_history_count(NULL) != ERR_NULL) result = 11;
    if (!result && app_package_get_history_count(&history_count) !=
                    ERR_UNAVAILABLE) result = 12;
    if (!result && app_package_get_history_entry(0U, NULL) != ERR_NULL) result = 13;
    if (!result && app_package_get_history_entry(0U, &history) !=
                    ERR_UNAVAILABLE) result = 14;
    if (!result && app_package_test_fail_after(0U) != ERR_INVALID) result = 15;
    if (!result && app_package_test_fail_after(33U) != ERR_INVALID) result = 16;
    if (!result && app_package_test_fail_after(1U) != OK) result = 17;
    if (!result && app_package_test_fail_after(32U) != OK) result = 18;
    if (!result && !test_names()) result = 19;
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
