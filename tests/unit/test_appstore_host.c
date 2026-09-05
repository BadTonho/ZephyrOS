#include <stdint.h>
#include <stdio.h>

#include "core/app_package.h"
#include "core/app_remote.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "ui/appstore_test.h"

#define HOST_COVERAGE_CAPACITY 2048U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static app_package_status_t fixture_package_status;
static int fixture_package_status_result;
static int fixture_provenance_available;
static int fixture_installed_trust;

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
    printf("ZCOV_BEGIN|case=host:ui:appstore|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:ui:appstore|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:ui:appstore|value=0x%08X\n",
           (uint32_t)result);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

int app_package_compare_versions(const char* left, const char* right,
                                 int* comparison_out) {
    if (!left || !right || !comparison_out) return ERR_NULL;
    *comparison_out = kstrcmp(left, right);
    return OK;
}

int app_package_get_status(app_package_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    if (fixture_package_status_result != OK) return fixture_package_status_result;
    *status_out = fixture_package_status;
    return OK;
}

int app_remote_is_provenance_available(void) {
    return fixture_provenance_available;
}

int app_remote_get_installed_trust(const char* id, const char* version) {
    (void)id;
    (void)version;
    return fixture_installed_trust;
}

int main(void) {
    int result;

    fixture_package_status_result = OK;
    fixture_provenance_available = 0;
    fixture_installed_trust = 0;
    coverage_active = 1U;
    result = appstore_host_test_contracts();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != OK) {
        printf("appstore-host: FAIL code=%d\n", result);
        return 1;
    }
    printf("appstore-host: PASS\n");
    return 0;
}
