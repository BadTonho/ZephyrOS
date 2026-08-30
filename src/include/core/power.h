#ifndef POWER_H
#define POWER_H

#include "types.h"

typedef enum {
    POWER_STATE_S0 = 0,
    POWER_STATE_S1,
    POWER_STATE_S2,
    POWER_STATE_S3,
    POWER_STATE_S4,
    POWER_STATE_S5,
    POWER_STATE_COUNT
} power_state_t;

typedef enum {
    POWER_CAPABILITY_AVAILABLE = 0,
    POWER_CAPABILITY_SIMULATED,
    POWER_CAPABILITY_UNAVAILABLE
} power_capability_t;

typedef enum {
    POWER_SERVICE_UNKNOWN = 0,
    POWER_SERVICE_DISCOVERING,
    POWER_SERVICE_READY,
    POWER_SERVICE_DEGRADED,
    POWER_SERVICE_UNAVAILABLE
} power_service_state_t;

typedef enum {
    POWER_TRANSACTION_IDLE = 0,
    POWER_TRANSACTION_ADMISSION,
    POWER_TRANSACTION_NOTIFICATION,
    POWER_TRANSACTION_SYNC_FLUSH,
    POWER_TRANSACTION_QUIESCENCE,
    POWER_TRANSACTION_HARDWARE_COMMIT,
    POWER_TRANSACTION_TERMINAL
} power_transaction_phase_t;

typedef struct {
    power_capability_t states[POWER_STATE_COUNT];
    power_capability_t cpu_idle;
    power_capability_t hardware_poweroff;
    power_capability_t reboot;
    uint8_t acpi_available;
    uint8_t acpi_power_tables_available;
    uint8_t acpi_partial;
    uint8_t acpi_pm1_control_available;
    uint8_t acpi_mode_known;
    uint8_t acpi_mode_enabled;
    uint8_t acpi_s5_declared;
    uint8_t acpi_mode_enable_available;
    uint8_t acpi_s5_transition_ready;
    power_service_state_t service_state;
    power_transaction_phase_t transaction_phase;
    int last_error;
    uint8_t reboot_acpi_reset_available;
    uint8_t reboot_ps2_available;
    uint8_t reboot_triple_fault_available;
} power_status_t;

int power_init(void);
int power_get_status(power_status_t* out_status);
int system_reboot(void);
int power_reboot(void);
int power_shutdown_request(void);
void power_shutdown(void);
const char* power_capability_name(power_capability_t capability);
const char* power_service_state_name(power_service_state_t state);
const char* power_transaction_phase_name(power_transaction_phase_t phase);
int power_shutdown_prepare(void);

#endif
