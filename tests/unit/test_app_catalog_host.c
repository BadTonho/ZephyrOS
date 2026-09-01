#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/app_catalog.h"
#include "core/app_loader.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "fs/fs.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_MAX_FILES 32U
#define HOST_MAX_PACKAGES 32U
#define HOST_MAX_INSTALLED 16U

typedef struct {
    char name[APP_CATALOG_ALIAS_SIZE];
    uint32_t size;
    uint8_t attributes;
} host_file_t;

typedef struct {
    char alias[APP_CATALOG_ALIAS_SIZE];
    app_package_info_t info;
    int verify_result;
} host_package_t;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static host_file_t fake_files[HOST_MAX_FILES];
static uint32_t fake_file_count;
static host_package_t fake_packages[HOST_MAX_PACKAGES];
static uint32_t fake_package_count;
static app_package_info_t fake_installed[HOST_MAX_INSTALLED];
static uint32_t fake_installed_count;
static int fake_info_error_index = -1;
static int fake_installed_error_index = -1;
static uint8_t fake_fs_type;
static int fake_loader_ready;
static int fake_package_ready;
static uint8_t fake_transaction_pending;
static recovery_state_t fake_recovery_state;
static int fake_recovery_error;

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

static void __attribute__((no_instrument_function))
coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:core:app-catalog|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:app-catalog|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:app-catalog|value=0x%08X\n",
           (uint32_t)result);
}

static void host_copy(char* destination, uint32_t capacity,
                      const char* source) {
    size_t length = strlen(source);

    if (length >= capacity) length = capacity - 1U;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static void host_reset(void) {
    memset(fake_files, 0, sizeof(fake_files));
    memset(fake_packages, 0, sizeof(fake_packages));
    memset(fake_installed, 0, sizeof(fake_installed));
    fake_file_count = 0U;
    fake_package_count = 0U;
    fake_installed_count = 0U;
    fake_info_error_index = -1;
    fake_installed_error_index = -1;
    fake_fs_type = FS_TYPE_FAT32;
    fake_loader_ready = 1;
    fake_package_ready = 1;
    fake_transaction_pending = 0U;
    fake_recovery_state = RECOVERY_STATE_UNKNOWN;
    fake_recovery_error = OK;
}

static int host_add_file(const char* name, uint32_t size, uint8_t attributes) {
    if (fake_file_count >= HOST_MAX_FILES) return ERR_OVERFLOW;
    host_copy(fake_files[fake_file_count].name,
              sizeof(fake_files[fake_file_count].name), name);
    fake_files[fake_file_count].size = size;
    fake_files[fake_file_count].attributes = attributes;
    fake_file_count++;
    return OK;
}

static host_package_t* host_add_package(const char* alias, const char* id,
                                        const char* version, int result) {
    host_package_t* package;

    if (fake_package_count >= HOST_MAX_PACKAGES) return NULL;
    package = &fake_packages[fake_package_count++];
    host_copy(package->alias, sizeof(package->alias), alias);
    host_copy(package->info.id, sizeof(package->info.id), id);
    host_copy(package->info.name, sizeof(package->info.name), id);
    host_copy(package->info.version, sizeof(package->info.version), version);
    package->verify_result = result;
    return package;
}

static int host_add_installed(const char* id, const char* version) {
    app_package_info_t* info;

    if (fake_installed_count >= HOST_MAX_INSTALLED) return ERR_OVERFLOW;
    info = &fake_installed[fake_installed_count++];
    host_copy(info->id, sizeof(info->id), id);
    host_copy(info->name, sizeof(info->name), id);
    host_copy(info->version, sizeof(info->version), version);
    return OK;
}

static host_package_t* host_find_package(const char* alias) {
    for (uint32_t index = 0U; index < fake_package_count; index++) {
        if (strcmp(fake_packages[index].alias, alias) == 0) {
            return &fake_packages[index];
        }
    }
    return NULL;
}

static int host_parse_version(const char* text, uint32_t values[3]) {
    const char* cursor = text;

    for (uint32_t part = 0U; part < 3U; part++) {
        uint32_t value = 0U;
        uint32_t digits = 0U;

        while (*cursor >= '0' && *cursor <= '9') {
            uint32_t digit = (uint32_t)(*cursor - '0');
            if (value > (UINT32_MAX - digit) / 10U) return ERR_OVERFLOW;
            value = value * 10U + digit;
            cursor++;
            digits++;
        }
        if (!digits) return ERR_INVALID;
        values[part] = value;
        if (part < 2U) {
            if (*cursor != '.') return ERR_INVALID;
            cursor++;
        }
    }
    return *cursor == '\0' ? OK : ERR_INVALID;
}

uint8_t fs_get_type(void) {
    return fake_fs_type;
}

int fs_get_file_count_at(const char* dir_path) {
    if (!dir_path || strcmp(dir_path, "") != 0) return ERR_INVALID;
    return (int)fake_file_count;
}

int fs_get_file_info_at(const char* dir_path, int index, char* name_out,
                        uint32_t* size_out, uint8_t* attr_out) {
    if (!dir_path || !name_out || !size_out || !attr_out) return ERR_NULL;
    if (index < 0 || (uint32_t)index >= fake_file_count) return ERR_NOT_FOUND;
    if (index == fake_info_error_index) return ERR_DISK;
    host_copy(name_out, APP_CATALOG_ALIAS_SIZE, fake_files[index].name);
    *size_out = fake_files[index].size;
    *attr_out = fake_files[index].attributes;
    return OK;
}

int app_loader_is_ready(void) {
    return fake_loader_ready;
}

int app_package_is_ready(void) {
    return fake_package_ready;
}

int app_package_compare_versions(const char* left, const char* right,
                                 int* comparison_out) {
    uint32_t left_values[3];
    uint32_t right_values[3];
    int result;

    if (!left || !right || !comparison_out) return ERR_NULL;
    result = host_parse_version(left, left_values);
    if (result != OK) return result;
    result = host_parse_version(right, right_values);
    if (result != OK) return result;
    *comparison_out = 0;
    for (uint32_t index = 0U; index < 3U; index++) {
        if (left_values[index] > right_values[index]) {
            *comparison_out = 1;
            break;
        }
        if (left_values[index] < right_values[index]) {
            *comparison_out = -1;
            break;
        }
    }
    return OK;
}

int app_package_verify_file(const char* path, app_package_info_t* info_out) {
    host_package_t* package;

    if (!path || !info_out) return ERR_NULL;
    package = host_find_package(path);
    if (!package) return ERR_NOT_FOUND;
    if (package->verify_result != OK) return package->verify_result;
    *info_out = package->info;
    return OK;
}

int app_package_get_installed_count(void) {
    return (int)fake_installed_count;
}

int app_package_get_installed_info(int index, app_package_info_t* info_out) {
    if (!info_out) return ERR_NULL;
    if (index < 0 || (uint32_t)index >= fake_installed_count) {
        return ERR_NOT_FOUND;
    }
    if (index == fake_installed_error_index) return ERR_DISK;
    *info_out = fake_installed[index];
    return OK;
}

int app_package_get_status(app_package_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    memset(status_out, 0, sizeof(*status_out));
    status_out->transaction_pending = fake_transaction_pending;
    return OK;
}

int recovery_mark_ready(recovery_component_id_t component) {
    (void)component;
    fake_recovery_state = RECOVERY_STATE_READY;
    fake_recovery_error = OK;
    return OK;
}

int recovery_mark_degraded(recovery_component_id_t component, int error_code,
                           const char* message) {
    (void)component;
    (void)message;
    fake_recovery_state = RECOVERY_STATE_DEGRADED;
    fake_recovery_error = error_code;
    return OK;
}

int recovery_mark_disabled(recovery_component_id_t component, int error_code,
                           const char* message) {
    (void)component;
    (void)message;
    fake_recovery_state = RECOVERY_STATE_DISABLED;
    fake_recovery_error = error_code;
    return OK;
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

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void video_newline(void) {
}

static int test_uninitialized_contract(void) {
    app_catalog_status_t status;
    uint32_t count = 0U;

    if (app_catalog_is_ready()) return 1;
    if (app_catalog_refresh() != ERR_STATE) return 2;
    if (app_catalog_get_status(NULL) != ERR_NULL ||
        app_catalog_get_status(&status) != ERR_STATE) return 3;
    if (app_catalog_get_count(NULL) != ERR_NULL ||
        app_catalog_get_count(&count) != ERR_UNAVAILABLE) return 4;
    return 0;
}

static int test_dependency_states(void) {
    app_catalog_status_t status;

    host_reset();
    fake_fs_type = FS_TYPE_NONE;
    if (app_catalog_init() != ERR_UNAVAILABLE || app_catalog_is_ready()) return 10;
    if (app_catalog_get_status(&status) != OK ||
        status.reason != APP_CATALOG_REASON_FILESYSTEM_UNAVAILABLE ||
        fake_recovery_state != RECOVERY_STATE_DISABLED ||
        fake_recovery_error != ERR_UNAVAILABLE) return 11;
    fake_fs_type = FS_TYPE_FAT32;
    fake_loader_ready = 0;
    if (app_catalog_refresh() != ERR_UNAVAILABLE) return 12;
    if (app_catalog_get_status(&status) != OK ||
        status.reason != APP_CATALOG_REASON_LOADER_UNAVAILABLE) return 13;
    fake_loader_ready = 1;
    fake_package_ready = 0;
    if (app_catalog_refresh() != ERR_UNAVAILABLE) return 14;
    if (app_catalog_get_status(&status) != OK ||
        status.reason != APP_CATALOG_REASON_PACKAGE_SERVICE_UNAVAILABLE) return 15;
    return 0;
}

static int prepare_catalog(void) {
    host_reset();
    if (host_add_file("APP2.ZPK", 200U, 0U) != OK ||
        host_add_file("APP1.ZPK", 100U, 0U) != OK ||
        host_add_file("BAD.ZPK", 50U, 0U) != OK ||
        host_add_file("WRONG.ZPK", 50U, 0U) != OK ||
        host_add_file("README.TXT", 50U, 0U) != OK ||
        host_add_file("HIDDEN.ZPK", 50U, FS_ATTRIBUTE_HIDDEN) != OK ||
        host_add_file("DIR.ZPK", 50U, APP_PACKAGE_DIRECTORY_ATTRIBUTE) != OK) {
        return ERR_OVERFLOW;
    }
    if (!host_add_package("APP1.ZPK", "APP1", "2.0.0", OK) ||
        !host_add_package("APP2.ZPK", "APP2", "1.0.0", OK) ||
        !host_add_package("BAD.ZPK", "BAD", "1.0.0", ERR_INVALID) ||
        !host_add_package("WRONG.ZPK", "OTHER", "1.0.0", OK)) {
        return ERR_OVERFLOW;
    }
    if (host_add_installed("APP1", "1.0.0") != OK ||
        host_add_installed("ORPHAN", "1.0.0") != OK) return ERR_OVERFLOW;
    fake_packages[1].info.dependency_count = 1U;
    host_copy(fake_packages[1].info.dependencies[0], APP_PACKAGE_ID_SIZE, "APP1");
    return app_catalog_refresh();
}

static int test_catalog_entries(void) {
    app_catalog_status_t status;
    app_catalog_entry_t entry;
    uint32_t count = 0U;

    if (prepare_catalog() != OK) return 20;
    if (!app_catalog_is_ready() || app_catalog_get_status(&status) != OK ||
        status.source_count != 4U || status.valid_source_count != 2U ||
        status.invalid_source_count != 2U || status.installed_count != 2U ||
        status.entry_count != 5U || status.reason != APP_CATALOG_REASON_PACKAGE_INVALID) {
        return 21;
    }
    if (app_catalog_get_count(&count) != OK || count != 5U ||
        app_catalog_get_entry(5U, &entry) != ERR_NOT_FOUND ||
        app_catalog_get_entry(0U, NULL) != ERR_NULL) return 22;
    if (app_catalog_find_entry("APP1.ZPK", &entry) != OK ||
        entry.state != APP_CATALOG_STATE_UPDATE_AVAILABLE ||
        !(entry.capabilities & APP_CATALOG_CAPABILITY_UPDATE)) return 23;
    if (app_catalog_find_entry("APP1", &entry) != OK ||
        strcmp(entry.alias, "APP1.ZPK") != 0) return 24;
    if (app_catalog_find_entry("ORPHAN", &entry) != OK ||
        entry.state != APP_CATALOG_STATE_INSTALLED ||
        entry.capabilities != (APP_CATALOG_CAPABILITY_RUN |
                               APP_CATALOG_CAPABILITY_REMOVE)) return 25;
    if (app_catalog_find_entry("missing", &entry) != ERR_NOT_FOUND) return 26;
    if (app_catalog_state_name(APP_CATALOG_STATE_DOWNGRADE) == NULL ||
        strcmp(app_catalog_state_name((app_catalog_state_t)99), "UNKNOWN") != 0 ||
        strcmp(app_catalog_reason_name(APP_CATALOG_REASON_ALIAS_MISMATCH),
               "ALIAS_MISMATCH") != 0 ||
        strcmp(app_catalog_reason_name((app_catalog_reason_t)99), "UNKNOWN") != 0) {
        return 27;
    }
    return 0;
}

static int test_plans_and_invalid_sources(void) {
    app_package_plan_t plan;
    app_catalog_entry_t entry;

    if (app_catalog_build_install_plan(NULL, &plan) != ERR_NULL ||
        app_catalog_build_install_plan("missing", &plan) != ERR_NOT_FOUND ||
        app_catalog_build_install_plan("APP1", &plan) != ERR_STATE ||
        plan.reason != APP_PACKAGE_ACTION_REASON_PLAN_CONFLICT) return 30;
    if (app_catalog_build_install_plan("APP2", &plan) != OK ||
        plan.entry_count != 1U || plan.target_index != 0U ||
        strcmp(plan.entries[0].id, "APP2") != 0 ||
        plan.entries[0].action != APP_PACKAGE_PLAN_ACTION_INSTALL) return 31;
    if (app_catalog_build_update_plan("APP1", 0, &plan) != OK ||
        plan.entry_count != 1U || plan.entries[0].action != APP_PACKAGE_PLAN_ACTION_UPDATE ||
        strcmp(plan.entries[0].from_version, "1.0.0") != 0) return 32;
    host_copy(fake_packages[0].info.version, APP_PACKAGE_VERSION_TEXT_SIZE, "0.5.0");
    if (app_catalog_refresh() != OK ||
        app_catalog_build_update_plan("APP1", 0, &plan) != ERR_STATE ||
        plan.reason != APP_PACKAGE_ACTION_REASON_DOWNGRADE_REQUIRES_CONFIRM ||
        app_catalog_build_update_plan("APP1", 1, &plan) != OK) return 33;
    host_copy(fake_packages[0].info.version, APP_PACKAGE_VERSION_TEXT_SIZE, "2.0.0");
    if (app_catalog_refresh() != OK) return 34;
    fake_packages[0].info.version[0] = '1';
    if (app_catalog_refresh() != OK ||
        app_catalog_build_update_plan("APP1", 0, &plan) != ERR_STATE ||
        plan.reason != APP_PACKAGE_ACTION_REASON_UPDATE_NOT_AVAILABLE) return 35;
    host_copy(fake_packages[0].info.version, APP_PACKAGE_VERSION_TEXT_SIZE, "2.0.0");
    fake_packages[1].info.dependency_count = 1U;
    host_copy(fake_packages[1].info.dependencies[0], APP_PACKAGE_ID_SIZE, "MISSING");
    if (app_catalog_refresh() != OK ||
        app_catalog_find_entry("APP2", &entry) != OK ||
        entry.state != APP_CATALOG_STATE_BLOCKED || entry.missing_dependency_mask != 1U ||
        app_catalog_build_install_plan("APP2", &plan) != ERR_NOT_FOUND ||
        plan.reason != APP_PACKAGE_ACTION_REASON_PLAN_INCOMPLETE) return 36;
    return 0;
}

static int test_read_errors_and_limits(void) {
    app_catalog_status_t status;

    if (prepare_catalog() != OK) return 40;
    fake_info_error_index = 1;
    if (app_catalog_refresh() != OK || app_catalog_get_status(&status) != OK ||
        status.reason != APP_CATALOG_REASON_READ_ERROR) return 41;
    fake_info_error_index = -1;
    fake_installed_error_index = 0;
    if (app_catalog_refresh() != OK || app_catalog_get_status(&status) != OK ||
        status.reason != APP_CATALOG_REASON_READ_ERROR) return 42;
    fake_installed_error_index = -1;
    host_reset();
    for (uint32_t index = 0U; index < APP_CATALOG_MAX_SOURCES + 1U; index++) {
        char name[APP_CATALOG_ALIAS_SIZE];
        char id[APP_PACKAGE_ID_SIZE];

        (void)snprintf(id, sizeof(id), "P%02u", index);
        (void)snprintf(name, sizeof(name), "%s.ZPK", id);
        if (host_add_file(name, index, 0U) != OK ||
            !host_add_package(name, id, "1.0.0", OK)) return 43;
    }
    fake_installed_count = 0U;
    if (app_catalog_refresh() != OK || app_catalog_get_status(&status) != OK ||
        !status.source_overflow || status.reason != APP_CATALOG_REASON_SOURCE_LIMIT ||
        status.source_count != APP_CATALOG_MAX_SOURCES) return 44;
    return 0;
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    log_init();
    if (!result) result = test_uninitialized_contract();
    if (!result) result = test_dependency_states();
    if (!result) result = test_catalog_entries();
    if (!result) result = test_plans_and_invalid_sources();
    if (!result) result = test_read_errors_and_limits();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        printf("APP_CATALOG_HOST_FAIL:%d\n", result);
        return result;
    }
    printf("APP_CATALOG_HOST_PASS\n");
    return 0;
}
