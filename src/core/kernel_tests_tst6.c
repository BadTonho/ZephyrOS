#include "kernel_tests.h"

#include "core/app_package.h"
#include "core/log.h"
#include "core/update_runtime.h"
#include "memory/paging.h"
#include "process/process.h"

#define KERNEL_TEST_TST6_TAG "TST6"
#define TST6_PREFIX "qemu:tst6:"

typedef int (*tst6_case_fn)(const kernel_tests_runtime_t* runtime);

static uint32_t tst6_length(const char* text) {
    uint32_t length = 0U;

    if (!text) return 0U;
    while (text[length]) length++;
    return length;
}

static int tst6_equals(const char* value, uint32_t value_length,
                       const char* expected) {
    uint32_t expected_length = tst6_length(expected);

    if (!value || !expected || value_length != expected_length) return 0;
    for (uint32_t index = 0U; index < value_length; index++) {
        if (value[index] != expected[index]) return 0;
    }
    return 1;
}

static int tst6_suffix(const char* case_id, uint32_t case_length,
                       const char* suffix) {
    uint32_t prefix_length = tst6_length(TST6_PREFIX);
    uint32_t suffix_length = tst6_length(suffix);

    if (!case_id || case_length != prefix_length + suffix_length ||
        !tst6_equals(case_id, prefix_length, TST6_PREFIX)) return 0;
    return tst6_equals(case_id + prefix_length, suffix_length, suffix);
}

static int tst6_run_domain(const kernel_tests_runtime_t* runtime,
                           const char* phase, tst6_case_fn test) {
    int result;

    if (!test) return kernel_tests_report_phase(runtime, phase, ERR_NULL);
    result = test(runtime);
    if (result != OK) {
        LOG_ERROR_CODE(KERNEL_TEST_TST6_TAG, result, phase);
    }
    return kernel_tests_report_phase(runtime, phase, result);
}

static int tst6_check_ready(const kernel_tests_runtime_t* runtime) {
    int result;

    if (!paging_is_ready() || process_get_current() == 0) {
        return kernel_tests_report_phase(runtime, "tst6-preconditions",
                                         ERR_STATE);
    }
    result = kernel_tests_progress(runtime);
    return kernel_tests_report_phase(runtime, "tst6-preconditions", result);
}

static int tst6_run_platform(const kernel_tests_runtime_t* runtime) {
    return kernel_tests_run_platform(runtime);
}

static int tst6_run_network(const kernel_tests_runtime_t* runtime) {
    return kernel_tests_run_network(runtime);
}

static int tst6_run_storage(const kernel_tests_runtime_t* runtime) {
    return kernel_tests_run_storage_vfs(runtime);
}

static int tst6_run_memory(const kernel_tests_runtime_t* runtime) {
    return kernel_tests_run_memory_slab_with_runtime(runtime);
}

static int tst6_run_execution(const kernel_tests_runtime_t* runtime) {
    return kernel_tests_run_execution(runtime);
}

static int tst6_package_failure_contract(void) {
    app_package_diagnostic_t diagnostic;
    app_package_status_t status;

    if (!app_package_is_ready() ||
        app_package_run_diagnostics(&diagnostic) != OK ||
        app_package_get_status(&status) != OK ||
        app_package_is_mutation_active()) {
        LOG_ERROR_CODE(KERNEL_TEST_TST6_TAG, ERR_STATE, "fault-package");
        return ERR_STATE;
    }
    if (app_package_test_fail_after(0U) != ERR_INVALID) {
        LOG_ERROR_CODE(KERNEL_TEST_TST6_TAG, ERR_STATE, "fault-package");
        return ERR_STATE;
    }
    return OK;
}

static int tst6_update_failure_contract(void) {
    update_runtime_capabilities_t capabilities;
    update_runtime_status_t status;

    if (!update_runtime_is_ready() ||
        update_runtime_get_capabilities(&capabilities) != OK ||
        update_runtime_get_status(&status) != OK ||
        status.transaction_pending || status.recovery_pending) {
        LOG_ERROR_CODE(KERNEL_TEST_TST6_TAG, ERR_STATE, "fault-update");
        return ERR_STATE;
    }
    if (update_runtime_test_fail_after(0U) != ERR_INVALID) {
        LOG_ERROR_CODE(KERNEL_TEST_TST6_TAG, ERR_STATE, "fault-update");
        return ERR_STATE;
    }
    return OK;
}

static int tst6_run_matrix(const kernel_tests_runtime_t* runtime,
                           const char* suffix, uint32_t suffix_length) {
    if (tst6_equals(suffix, suffix_length, "baseline") ||
        tst6_equals(suffix, suffix_length, "minimal") ||
        tst6_equals(suffix, suffix_length, "audio") ||
        tst6_equals(suffix, suffix_length, "display") ||
        tst6_equals(suffix, suffix_length, "pci") ||
        tst6_equals(suffix, suffix_length, "usb-hid")) {
        return tst6_run_domain(runtime, "matrix-platform", tst6_run_platform);
    }
    if (tst6_equals(suffix, suffix_length, "network")) {
        return tst6_run_domain(runtime, "matrix-network", tst6_run_network);
    }
    if (tst6_equals(suffix, suffix_length, "usb-storage")) {
        return tst6_run_domain(runtime, "matrix-storage", tst6_run_storage);
    }
    LOG_ERROR_CODE(KERNEL_TEST_TST6_TAG, ERR_NOT_FOUND, "matrix");
    return ERR_NOT_FOUND;
}

static int tst6_run_stress(const kernel_tests_runtime_t* runtime,
                           const char* suffix, uint32_t suffix_length) {
    if (tst6_equals(suffix, suffix_length, "kernel")) {
        if (tst6_run_domain(runtime, "stress-memory", tst6_run_memory) != OK) {
            LOG_ERROR_CODE(KERNEL_TEST_TST6_TAG, ERR_STATE, "stress");
            return ERR_STATE;
        }
        return tst6_run_domain(runtime, "stress-execution", tst6_run_execution);
    }
    if (tst6_equals(suffix, suffix_length, "storage")) {
        return tst6_run_domain(runtime, "stress-storage", tst6_run_storage);
    }
    if (tst6_equals(suffix, suffix_length, "network")) {
        return tst6_run_domain(runtime, "stress-network", tst6_run_network);
    }
    if (tst6_equals(suffix, suffix_length, "apps")) {
        if (tst6_run_domain(runtime, "stress-execution", tst6_run_execution) != OK) {
            LOG_ERROR_CODE(KERNEL_TEST_TST6_TAG, ERR_STATE, "stress");
            return ERR_STATE;
        }
        return tst6_run_domain(runtime, "stress-platform", tst6_run_platform);
    }
    LOG_ERROR_CODE(KERNEL_TEST_TST6_TAG, ERR_NOT_FOUND, "stress");
    return ERR_NOT_FOUND;
}

static int tst6_run_fault(const kernel_tests_runtime_t* runtime,
                          const char* suffix, uint32_t suffix_length) {
    int result;

    if (tst6_equals(suffix, suffix_length, "memory")) {
        return tst6_run_domain(runtime, "fault-memory", tst6_run_memory);
    }
    if (tst6_equals(suffix, suffix_length, "block") ||
        tst6_equals(suffix, suffix_length, "block-cache")) {
        return tst6_run_domain(runtime, "fault-storage", tst6_run_storage);
    }
    if (tst6_equals(suffix, suffix_length, "package")) {
        result = tst6_package_failure_contract();
        if (result != OK) {
            LOG_ERROR_CODE(KERNEL_TEST_TST6_TAG, result, "fault-package");
        }
        return kernel_tests_report_phase(runtime, "fault-package", result);
    }
    if (tst6_equals(suffix, suffix_length, "update")) {
        result = tst6_update_failure_contract();
        if (result != OK) {
            LOG_ERROR_CODE(KERNEL_TEST_TST6_TAG, result, "fault-update");
        }
        return kernel_tests_report_phase(runtime, "fault-update", result);
    }
    if (tst6_equals(suffix, suffix_length, "network")) {
        return tst6_run_domain(runtime, "fault-network", tst6_run_network);
    }
    if (tst6_equals(suffix, suffix_length, "process")) {
        return tst6_run_domain(runtime, "fault-process", tst6_run_execution);
    }
    if (tst6_equals(suffix, suffix_length, "recovery")) {
        return tst6_run_domain(runtime, "fault-recovery", tst6_run_storage);
    }
    LOG_ERROR_CODE(KERNEL_TEST_TST6_TAG, ERR_NOT_FOUND, "fault");
    return ERR_NOT_FOUND;
}

int kernel_tests_run_tst6(const kernel_tests_runtime_t* runtime,
                          const char* case_id, uint32_t case_length) {
    uint32_t prefix_length = tst6_length(TST6_PREFIX);
    const char* suffix;
    uint32_t suffix_length;
    int result;

    if (!case_id || case_length <= prefix_length ||
        !tst6_equals(case_id, prefix_length, TST6_PREFIX)) {
        LOG_ERROR_CODE(KERNEL_TEST_TST6_TAG, ERR_NOT_FOUND, "case");
        return ERR_NOT_FOUND;
    }
    result = tst6_check_ready(runtime);
    if (result != OK) {
        LOG_ERROR_CODE(KERNEL_TEST_TST6_TAG, result, "preconditions");
        return result;
    }
    suffix = case_id + prefix_length;
    suffix_length = case_length - prefix_length;
    if (tst6_suffix(case_id, case_length, "matrix:baseline") ||
        tst6_suffix(case_id, case_length, "matrix:minimal") ||
        tst6_suffix(case_id, case_length, "matrix:network") ||
        tst6_suffix(case_id, case_length, "matrix:usb-hid") ||
        tst6_suffix(case_id, case_length, "matrix:usb-storage") ||
        tst6_suffix(case_id, case_length, "matrix:audio") ||
        tst6_suffix(case_id, case_length, "matrix:display") ||
        tst6_suffix(case_id, case_length, "matrix:pci")) {
        return tst6_run_matrix(runtime, suffix + tst6_length("matrix:"),
                               suffix_length - tst6_length("matrix:"));
    }
    if (tst6_suffix(case_id, case_length, "stress:kernel") ||
        tst6_suffix(case_id, case_length, "stress:storage") ||
        tst6_suffix(case_id, case_length, "stress:network") ||
        tst6_suffix(case_id, case_length, "stress:apps")) {
        return tst6_run_stress(runtime, suffix + tst6_length("stress:"),
                               suffix_length - tst6_length("stress:"));
    }
    if (tst6_suffix(case_id, case_length, "fault:memory") ||
        tst6_suffix(case_id, case_length, "fault:block") ||
        tst6_suffix(case_id, case_length, "fault:block-cache") ||
        tst6_suffix(case_id, case_length, "fault:package") ||
        tst6_suffix(case_id, case_length, "fault:update") ||
        tst6_suffix(case_id, case_length, "fault:network") ||
        tst6_suffix(case_id, case_length, "fault:process") ||
        tst6_suffix(case_id, case_length, "fault:recovery")) {
        return tst6_run_fault(runtime, suffix + tst6_length("fault:"),
                              suffix_length - tst6_length("fault:"));
    }
    LOG_ERROR_CODE(KERNEL_TEST_TST6_TAG, ERR_NOT_FOUND, "case");
    return ERR_NOT_FOUND;
}
