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
} power_status_t;

int power_init(void);
int power_get_status(power_status_t* out_status);
void power_shutdown(void) __attribute__((noreturn));
const char* power_capability_name(power_capability_t capability);

#endif
