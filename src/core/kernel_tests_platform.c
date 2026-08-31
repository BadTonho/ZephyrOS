#include "kernel_tests.h"

#include "core/device_manager.h"
#include "core/input.h"
#include "core/irq_deferred.h"
#include "core/log.h"
#include "core/power.h"
#include "core/power_notifier.h"
#include "core/wifi_manager.h"
#include "drivers/acpi.h"
#include "drivers/idt.h"
#include "drivers/rng.h"
#include "drivers/rtc.h"
#include "core/clock.h"
#include "core/timer.h"
#include "core/usb_manager.h"

#define KERNEL_TEST_PLATFORM_TAG "TST4"

static int platform_check_log(void) {
    log_self_test_result_t result;

    if (log_self_test(&result) != OK || result.failed != 0U ||
        !result.order_and_metadata || !result.wrap_and_overwrite ||
        !result.repetition_grouping || !result.safe_truncation ||
        !result.optional_error_code || !result.clear_behavior ||
        !result.text_serialization || !result.level_filtering) {
        LOG_ERROR(KERNEL_TEST_PLATFORM_TAG,
                  "fase=platform-log resultado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_PLATFORM_TAG, ERR_STATE,
                       "fase=platform-log resultado invalido");
        return ERR_STATE;
    }
    return OK;
}

static int platform_check_timer(void) {
    timer_self_test_result_t result;

    if (timer_self_test(&result) != OK || result.failed != 0U ||
        !result.conversion_and_limits || !result.one_shot ||
        !result.periodic_no_drift || !result.periodic_coalescing ||
        !result.cancel_armed || !result.cancel_pending ||
        !result.owner_destruction || !result.stale_handles ||
        !result.tick_wrap || !result.capacity || !result.callback_errors ||
        !result.invariants || timer_validate_state() != OK) {
        LOG_ERROR(KERNEL_TEST_PLATFORM_TAG,
                  "fase=platform-timer resultado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_PLATFORM_TAG, ERR_STATE,
                       "fase=platform-timer resultado invalido");
        return ERR_STATE;
    }
    return OK;
}

static int platform_check_clock(void) {
    clock_self_test_result_t result;

    if (clock_self_test(&result) != OK || result.failed != 0U ||
        !result.epoch_conversion || !result.leap_year_conversion ||
        !result.invalid_date_rejected || !result.monotonic_rollover ||
        !result.invariants || !result.passed || clock_validate_state() != OK) {
        LOG_ERROR(KERNEL_TEST_PLATFORM_TAG,
                  "fase=platform-clock resultado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_PLATFORM_TAG, ERR_STATE,
                       "fase=platform-clock resultado invalido");
        return ERR_STATE;
    }
    return OK;
}

static int platform_check_irq(void) {
    irq_deferred_self_test_result_t result;

    if (irq_deferred_self_test(&result) != OK || result.failed != 0U ||
        !result.lifecycle || !result.coalescing || !result.rerun ||
        !result.cancellation || !result.capacity || !result.attribution ||
        !result.interrupt_context || !result.invariants ||
        irq_deferred_validate_state() != OK || idt_validate_irq_state() != OK) {
        LOG_ERROR(KERNEL_TEST_PLATFORM_TAG,
                  "fase=platform-irq resultado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_PLATFORM_TAG, ERR_STATE,
                       "fase=platform-irq resultado invalido");
        return ERR_STATE;
    }
    return OK;
}

static int platform_check_rtc(void) {
    rtc_self_test_result_t result;

    if (rtc_self_test(&result) != OK || result.failed != 0U ||
        !result.bcd_conversion || !result.binary_conversion ||
        !result.twelve_hour_conversion || !result.calendar_validation ||
        !result.invalid_dates_rejected || !result.passed ||
        rtc_validate_state() != OK) {
        LOG_ERROR(KERNEL_TEST_PLATFORM_TAG,
                  "fase=platform-rtc resultado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_PLATFORM_TAG, ERR_STATE,
                       "fase=platform-rtc resultado invalido");
        return ERR_STATE;
    }
    return OK;
}

static int platform_check_devices(void) {
    device_info_t info;
    device_text_t text;
    uint32_t count;
    int result;

    result = device_manager_get_count(&count);
    if (result != OK || count > DEVICE_MANAGER_MAX_DEVICES) {
        LOG_ERROR(KERNEL_TEST_PLATFORM_TAG,
                  "fase=platform-devices contagem invalida");
        LOG_ERROR_CODE(KERNEL_TEST_PLATFORM_TAG, ERR_STATE,
                       "fase=platform-devices contagem invalida");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < count; index++) {
        result = device_manager_get_info(index, &info);
        if (result != OK || info.kind > DEVICE_KIND_PC_SPEAKER ||
            info.status > DEVICE_STATUS_UNKNOWN ||
            device_manager_format_text(&info, &text) != OK || !text.id[0] ||
            device_manager_find(text.id, &info) != OK) {
            LOG_ERROR(KERNEL_TEST_PLATFORM_TAG,
                      "fase=platform-devices inventario invalido");
            LOG_ERROR_CODE(KERNEL_TEST_PLATFORM_TAG, ERR_STATE,
                           "fase=platform-devices inventario invalido");
            return ERR_STATE;
        }
    }
    if (device_manager_get_info(count, &info) != ERR_INVALID ||
        device_manager_get_info(count, 0) != ERR_NULL) {
        LOG_ERROR(KERNEL_TEST_PLATFORM_TAG,
                  "fase=platform-devices limites invalidos");
        LOG_ERROR_CODE(KERNEL_TEST_PLATFORM_TAG, ERR_STATE,
                       "fase=platform-devices limites invalidos");
        return ERR_STATE;
    }
    return OK;
}

static int platform_check_acpi_power(void) {
    acpi_status_t acpi;
    acpi_power_info_t acpi_power;
    power_status_t power;

    if (acpi_get_status(&acpi) != OK || acpi_get_power_info(&acpi_power) != OK ||
        power_get_status(&power) != OK ||
        !acpi.initialized || acpi.root_kind > ACPI_ROOT_XSDT ||
        acpi.table_count > ACPI_MAX_TABLES ||
        (acpi.available && !acpi.rsdp_checksum_valid) ||
        (acpi.fadt_present && !acpi.fadt_address) ||
        (acpi.dsdt_present && !acpi.dsdt_address) ||
        acpi_power.mode > ACPI_MODE_INCONSISTENT ||
        acpi_power.s5_state > ACPI_S5_AMBIGUOUS ||
        power.service_state < POWER_SERVICE_DISCOVERING ||
        power.service_state > POWER_SERVICE_UNAVAILABLE ||
        power.transaction_phase != POWER_TRANSACTION_IDLE ||
        power.transaction_target != POWER_TRANSACTION_TARGET_NONE ||
        power.commit_started || power.notifiers_registered == 0U ||
        power_notifier_validate_state() != OK) {
        LOG_ERROR(KERNEL_TEST_PLATFORM_TAG,
                  "fase=platform-acpi-power estado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_PLATFORM_TAG, ERR_STATE,
                       "fase=platform-acpi-power estado invalido");
        return ERR_STATE;
    }
    if (power.acpi_power_tables_available &&
        (!power.acpi_available || power.acpi_partial)) {
        LOG_ERROR(KERNEL_TEST_PLATFORM_TAG,
                  "fase=platform-acpi-power tabelas inconsistentes");
        LOG_ERROR_CODE(KERNEL_TEST_PLATFORM_TAG, ERR_STATE,
                       "fase=platform-acpi-power tabelas inconsistentes");
        return ERR_STATE;
    }
    return OK;
}

static int platform_check_optional_devices(void) {
    rng_status_t rng;
    wifi_manager_status_t wifi;
    int wifi_result;

    if (rng_get_status(&rng) != OK || rng_validate_state() != OK ||
        usb_manager_validate_state() != OK) {
        LOG_ERROR(KERNEL_TEST_PLATFORM_TAG,
                  "fase=platform-optional estado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_PLATFORM_TAG, ERR_STATE,
                       "fase=platform-optional estado invalido");
        return ERR_STATE;
    }
    wifi_result = wifi_manager_get_status(&wifi);
    if (wifi_result == ERR_STATE) {
        wifi_result = wifi_manager_init();
    }
    if ((wifi_result != OK && wifi_result != ERR_OVERFLOW) ||
        wifi_manager_get_status(&wifi) != OK ||
        wifi_manager_validate_state() != OK) {
        LOG_ERROR(KERNEL_TEST_PLATFORM_TAG,
                  "fase=platform-optional Wi-Fi indisponivel de forma incoerente");
        LOG_ERROR_CODE(KERNEL_TEST_PLATFORM_TAG, ERR_STATE,
                       "fase=platform-optional Wi-Fi indisponivel de forma incoerente");
        return ERR_STATE;
    }
    if (!rng.initialized &&
        (rng.rdrand_available || rng.words_generated != 0U)) {
        LOG_ERROR(KERNEL_TEST_PLATFORM_TAG,
                  "fase=platform-optional rng residual");
        return ERR_STATE;
    }
    return OK;
}

int kernel_tests_run_platform(const kernel_tests_runtime_t* runtime) {
    int result;

    LOG_INFO(KERNEL_TEST_PLATFORM_TAG, "fase=platform-preconditions inicio");
    if (input_validate_state() != OK || power_notifier_validate_state() != OK) {
        return kernel_tests_phase_result("fase=platform-preconditions",
                                         ERR_STATE);
    }
    result = kernel_tests_progress(runtime);
    if (result != OK) return result;
    result = platform_check_log();
    if (result != OK) {
        return kernel_tests_phase_result("fase=platform-log", result);
    }
    result = platform_check_timer();
    if (result != OK) {
        return kernel_tests_phase_result("fase=platform-timer", result);
    }
    result = platform_check_clock();
    if (result != OK) {
        return kernel_tests_phase_result("fase=platform-clock", result);
    }
    result = platform_check_irq();
    if (result != OK) {
        return kernel_tests_phase_result("fase=platform-irq", result);
    }
    result = platform_check_rtc();
    if (result != OK) {
        return kernel_tests_phase_result("fase=platform-rtc", result);
    }
    result = platform_check_devices();
    if (result != OK) {
        return kernel_tests_phase_result("fase=platform-devices", result);
    }
    result = platform_check_acpi_power();
    if (result != OK) {
        return kernel_tests_phase_result("fase=platform-acpi-power", result);
    }
    result = platform_check_optional_devices();
    if (result != OK) {
        return kernel_tests_phase_result("fase=platform-optional", result);
    }
    result = kernel_tests_progress(runtime);
    if (result != OK) return result;
    if (input_validate_state() != OK ||
        power_notifier_validate_state() != OK || idt_validate_irq_state() != OK) {
        return kernel_tests_phase_result("fase=platform-postconditions",
                                         ERR_STATE);
    }
    return kernel_tests_phase_result("fase=platform-postconditions", OK);
}
