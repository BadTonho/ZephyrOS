#include "core/power.h"
#include "core/errors.h"
#include "core/log.h"
#include "drivers/acpi.h"

static power_status_t power_status;
static int power_initialized = 0;

int power_init(void) {
    acpi_status_t acpi_status;

    LOG_INFO("POWER", "Inicializando diagnostico de energia");
    power_status.states[POWER_STATE_S0] = POWER_CAPABILITY_AVAILABLE;
    power_status.states[POWER_STATE_S1] = POWER_CAPABILITY_UNAVAILABLE;
    power_status.states[POWER_STATE_S2] = POWER_CAPABILITY_UNAVAILABLE;
    power_status.states[POWER_STATE_S3] = POWER_CAPABILITY_UNAVAILABLE;
    power_status.states[POWER_STATE_S4] = POWER_CAPABILITY_UNAVAILABLE;
    power_status.states[POWER_STATE_S5] = POWER_CAPABILITY_SIMULATED;
    power_status.cpu_idle = POWER_CAPABILITY_AVAILABLE;
    power_status.hardware_poweroff = POWER_CAPABILITY_UNAVAILABLE;
    power_status.reboot = POWER_CAPABILITY_AVAILABLE;
    power_status.acpi_available = 0;
    power_status.acpi_power_tables_available = 0;
    power_status.acpi_partial = 0;
    if (acpi_get_status(&acpi_status) == OK) {
        power_status.acpi_available = acpi_status.available;
        power_status.acpi_power_tables_available =
            acpi_status.fadt_present && acpi_status.dsdt_present;
        power_status.acpi_partial = acpi_status.partial;
    } else {
        LOG_WARN("POWER", "Estado ACPI indisponivel para diagnostico");
    }
    power_initialized = 1;
    LOG_INFO("POWER", "Diagnostico de energia inicializado com sucesso");
    return OK;
}

int power_get_status(power_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("POWER", "Destino nulo ao consultar energia");
        return ERR_NULL;
    }
    if (!power_initialized) {
        LOG_ERROR("POWER", "Consulta antes da inicializacao de energia");
        return ERR_STATE;
    }
    *out_status = power_status;
    return OK;
}

const char* power_capability_name(power_capability_t capability) {
    if (capability == POWER_CAPABILITY_AVAILABLE) return "DISPONIVEL";
    if (capability == POWER_CAPABILITY_SIMULATED) return "SIMULADO";
    return "INDISPONIVEL";
}
