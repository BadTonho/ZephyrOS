#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/power.h"
#include "core/power_notifier.h"
#include "core/network_manager.h"
#include "core/workqueue.h"
#include "drivers/ac97.h"
#include "drivers/acpi.h"
#include "core/keyboard.h"
#include "fs/storage.h"
#include "fs/vfs.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint32_t fake_ticks = 100U;
static int fake_acpi_status_result = ERR_UNAVAILABLE;
static int fake_acpi_power_result = ERR_UNAVAILABLE;
static int fake_storage_sync_result = OK;
static int fake_storage_sync_until_result = OK;
static int fake_acpi_poweroff_result = ERR_DISK;
static uint8_t fake_vfs_quiescing;
static uint8_t fake_keyboard_reset_available;
static acpi_status_t fake_acpi_status;
static acpi_power_info_t fake_acpi_power_info;

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
    printf("ZCOV_BEGIN|case=host:core:power|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:power|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:power|value=0x%08X\n",
           (uint32_t)result);
}

uint32_t timer_get_ticks(void) {
    return fake_ticks++;
}

uint32_t timer_get_frequency(void) {
    return 1000U;
}

int serial_is_ready(void) {
    return 1;
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

uint8_t keyboard_controller_reset_available(void) {
    return fake_keyboard_reset_available;
}

int keyboard_controller_reset(void) {
    return ERR_UNAVAILABLE;
}

int acpi_get_status(acpi_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    if (fake_acpi_status_result != OK) return fake_acpi_status_result;
    *out_status = fake_acpi_status;
    return OK;
}

int acpi_get_power_info(acpi_power_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (fake_acpi_power_result != OK) return fake_acpi_power_result;
    *out_info = fake_acpi_power_info;
    return OK;
}

int acpi_reset(void) {
    return ERR_UNAVAILABLE;
}

int acpi_poweroff(void) {
    return fake_acpi_poweroff_result;
}

int process_power_set_quiescing(uint8_t active) {
    (void)active;
    return OK;
}

int process_power_shutdown_users(uint32_t deadline_tick,
                                 uint32_t* out_sigterm,
                                 uint32_t* out_sigkill,
                                 uint32_t* out_reaped) {
    if (!out_sigterm || !out_sigkill || !out_reaped) return ERR_NULL;
    if ((int32_t)(timer_get_ticks() - deadline_tick) >= 0) return ERR_TIMEOUT;
    *out_sigterm = 2U;
    *out_sigkill = 1U;
    *out_reaped = 3U;
    return OK;
}

int workqueue_power_set_quiescing(uint8_t active) {
    (void)active;
    return OK;
}

int workqueue_power_quiesce_until(uint32_t deadline_tick) {
    return (int32_t)(timer_get_ticks() - deadline_tick) < 0 ? OK : ERR_TIMEOUT;
}

int vfs_power_set_quiescing(uint8_t active) {
    fake_vfs_quiescing = active ? 1U : 0U;
    return OK;
}

int vfs_power_is_quiescing(void) {
    return fake_vfs_quiescing;
}

int vfs_power_unmount_storage_until(uint32_t deadline_tick,
                                    uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    if ((int32_t)(timer_get_ticks() - deadline_tick) >= 0) return ERR_TIMEOUT;
    *out_count = 1U;
    return OK;
}

int vfs_copy_mounts(vfs_mount_info_t* output, uint32_t capacity,
                    uint32_t* out_count) {
    if (!output || !out_count) return ERR_NULL;
    if (!capacity) return ERR_OVERFLOW;
    *out_count = 0U;
    return OK;
}

int storage_sync_all(void) {
    return fake_storage_sync_result;
}

int storage_sync_all_until(uint32_t deadline_tick) {
    if ((int32_t)(timer_get_ticks() - deadline_tick) >= 0) return ERR_TIMEOUT;
    return fake_storage_sync_until_result;
}

int network_manager_quiesce(void) {
    return OK;
}

int network_manager_set_quiescing(uint8_t active) {
    (void)active;
    return OK;
}

int video_power_quiesce(void) {
    return OK;
}

void speaker_off(void) {
}

void ac97_stop(void) {
}

static void reset_fixtures(void) {
    memset(&fake_acpi_status, 0, sizeof(fake_acpi_status));
    memset(&fake_acpi_power_info, 0, sizeof(fake_acpi_power_info));
    fake_ticks = 100U;
    fake_acpi_status_result = ERR_UNAVAILABLE;
    fake_acpi_power_result = ERR_UNAVAILABLE;
    fake_storage_sync_result = OK;
    fake_storage_sync_until_result = OK;
    fake_acpi_poweroff_result = ERR_DISK;
    fake_vfs_quiescing = 0U;
    fake_keyboard_reset_available = 0U;
}

static int test_preinitialization(void) {
    power_status_t status;

    if (power_get_status(NULL) != ERR_NULL) return 1;
    if (power_get_status(&status) != ERR_STATE) return 2;
    if (power_reboot() != ERR_STATE || system_reboot() != ERR_STATE ||
        power_shutdown_request() != ERR_STATE ||
        power_shutdown_prepare() != ERR_STATE) return 3;
    if (strcmp(power_capability_name(POWER_CAPABILITY_AVAILABLE),
               "DISPONIVEL") != 0 ||
        strcmp(power_capability_name(POWER_CAPABILITY_SIMULATED),
               "SIMULADO") != 0 ||
        strcmp(power_capability_name(POWER_CAPABILITY_UNAVAILABLE),
               "INDISPONIVEL") != 0) return 4;
    if (strcmp(power_service_state_name(POWER_SERVICE_UNKNOWN),
               "DESCONHECIDO") != 0 ||
        strcmp(power_service_state_name(POWER_SERVICE_DISCOVERING),
               "DESCOBRINDO") != 0 ||
        strcmp(power_service_state_name(POWER_SERVICE_READY), "PRONTO") != 0 ||
        strcmp(power_service_state_name(POWER_SERVICE_DEGRADED),
               "DEGRADADO") != 0 ||
        strcmp(power_service_state_name(POWER_SERVICE_UNAVAILABLE),
               "INDISPONIVEL") != 0) return 5;
    if (strcmp(power_transaction_phase_name(POWER_TRANSACTION_IDLE), "IDLE") != 0 ||
        strcmp(power_transaction_phase_name(POWER_TRANSACTION_ADMISSION),
               "ADMISSION") != 0 ||
        strcmp(power_transaction_phase_name(POWER_TRANSACTION_NOTIFICATION),
               "NOTIFICATION") != 0 ||
        strcmp(power_transaction_phase_name(POWER_TRANSACTION_SYNC_FLUSH),
               "SYNC_FLUSH") != 0 ||
        strcmp(power_transaction_phase_name(POWER_TRANSACTION_QUIESCENCE),
               "QUIESCENCE") != 0 ||
        strcmp(power_transaction_phase_name(POWER_TRANSACTION_HARDWARE_COMMIT),
               "HARDWARE_COMMIT") != 0 ||
        strcmp(power_transaction_phase_name(POWER_TRANSACTION_TERMINAL),
               "TERMINAL") != 0) return 6;
    if (strcmp(power_transaction_target_name(POWER_TRANSACTION_TARGET_NONE),
               "NONE") != 0 ||
        strcmp(power_transaction_target_name(POWER_TRANSACTION_TARGET_SHUTDOWN),
               "SHUTDOWN") != 0 ||
        strcmp(power_transaction_target_name(POWER_TRANSACTION_TARGET_REBOOT),
               "REBOOT") != 0) return 7;
    if (strcmp(power_quiescence_state_name(POWER_QUIESCENCE_UNKNOWN),
               "UNKNOWN") != 0 ||
        strcmp(power_quiescence_state_name(POWER_QUIESCENCE_READY), "READY") != 0 ||
        strcmp(power_quiescence_state_name(POWER_QUIESCENCE_DEGRADED),
               "DEGRADED") != 0 ||
        strcmp(power_quiescence_state_name(POWER_QUIESCENCE_COMPLETE),
               "COMPLETE") != 0) return 8;
    if (strcmp(power_service_state_name((power_service_state_t)99),
               "DESCONHECIDO") != 0 ||
        strcmp(power_transaction_phase_name((power_transaction_phase_t)99),
               "IDLE") != 0 ||
        strcmp(power_transaction_target_name((power_transaction_target_t)99),
               "NONE") != 0 ||
        strcmp(power_quiescence_state_name((power_quiescence_state_t)99),
               "UNKNOWN") != 0) return 9;
    return 0;
}

static int test_unavailable_acpi(void) {
    power_status_t status;

    reset_fixtures();
    if (power_init() != OK) return 20;
    if (power_get_status(&status) != OK ||
        status.service_state != POWER_SERVICE_DEGRADED ||
        status.hardware_poweroff != POWER_CAPABILITY_UNAVAILABLE ||
        status.reboot != POWER_CAPABILITY_AVAILABLE ||
        status.acpi_available || status.acpi_power_tables_available ||
        status.notifiers_registered != POWER_NOTIFIER_CAPACITY ||
        status.transaction_phase != POWER_TRANSACTION_IDLE ||
        power_notifier_validate_state() != OK) return 21;
    if (power_shutdown_request() != ERR_UNAVAILABLE) return 22;
    power_shutdown();
    if (power_shutdown_prepare() != OK) return 23;
    fake_vfs_quiescing = 1U;
    if (power_shutdown_prepare() != ERR_STATE) return 24;
    fake_vfs_quiescing = 0U;
    fake_storage_sync_result = ERR_DISK;
    if (power_shutdown_prepare() != ERR_DISK) return 25;
    return 0;
}

static int test_available_acpi_and_cleanup(void) {
    power_status_t status;

    reset_fixtures();
    fake_acpi_status_result = OK;
    fake_acpi_status.available = 1U;
    fake_acpi_status.fadt_present = 1U;
    fake_acpi_status.dsdt_present = 1U;
    fake_acpi_status.partial = 0U;
    fake_acpi_power_result = OK;
    fake_acpi_power_info.mode = ACPI_MODE_ENABLED;
    fake_acpi_power_info.s5_state = ACPI_S5_DECLARED;
    fake_acpi_power_info.pm1a_readable = 1U;
    fake_acpi_power_info.s5_transition_ready = 1U;
    fake_acpi_power_info.reset_register_present = 1U;
    fake_acpi_power_info.reset_register_valid = 1U;
    if (power_init() != OK || power_get_status(&status) != OK ||
        status.service_state != POWER_SERVICE_READY ||
        status.hardware_poweroff != POWER_CAPABILITY_AVAILABLE ||
        status.acpi_available != 1U ||
        status.acpi_power_tables_available != 1U ||
        status.acpi_mode_enabled != 1U || status.acpi_s5_declared != 1U ||
        status.acpi_s5_transition_ready != 1U ||
        status.reboot_acpi_reset_available != 1U) return 30;
    if (power_shutdown_request() != ERR_DISK) return 31;
    if (power_get_status(&status) != OK ||
        status.last_error != ERR_DISK ||
        status.transaction_phase != POWER_TRANSACTION_IDLE ||
        status.commit_started || status.quiescence_state !=
        POWER_QUIESCENCE_UNKNOWN || fake_vfs_quiescing) return 32;
    fake_storage_sync_until_result = ERR_TIMEOUT;
    if (power_shutdown_request() != ERR_TIMEOUT) return 33;
    if (power_get_status(&status) != OK ||
        status.transaction_phase != POWER_TRANSACTION_IDLE ||
        status.last_error != ERR_TIMEOUT || fake_vfs_quiescing) return 34;
    fake_storage_sync_until_result = OK;
    fake_acpi_poweroff_result = OK;
    if (power_init() != OK || power_shutdown_request() != OK) return 35;
    if (power_get_status(&status) != OK || !status.commit_started ||
        status.transaction_phase != POWER_TRANSACTION_TERMINAL ||
        status.transaction_target != POWER_TRANSACTION_TARGET_SHUTDOWN ||
        status.quiescence_state != POWER_QUIESCENCE_COMPLETE) return 36;
    return 0;
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    log_init();
    if (!result) result = test_preinitialization();
    if (!result) result = test_unavailable_acpi();
    if (!result) result = test_available_acpi_and_cleanup();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        printf("POWER_HOST_FAIL:%d\n", result);
        return result;
    }
    printf("POWER_HOST_PASS\n");
    return 0;
}
