#include "core/power.h"
#include "core/errors.h"
#include "core/log.h"
#include "drivers/acpi.h"
#include "drivers/ac97.h"
#include "drivers/speaker.h"
#include "fs/storage.h"

#define POWER_KBC_STATUS_PORT 0x64U
#define POWER_KBC_INPUT_FULL 0x02U
#define POWER_KBC_RESET_COMMAND 0xFEU
#define POWER_KBC_WAIT_LIMIT 1000000U
#define POWER_RESET_RETURN_WAIT 1000000U

static power_status_t power_status;
static int power_initialized = 0;

static void power_halt_fallback(void) __attribute__((noreturn));

static void power_halt_fallback(void) {
    asm volatile("cli");
    for (;;) asm volatile("hlt");
}

int power_init(void) {
    acpi_status_t acpi_status;
    acpi_power_info_t acpi_power_info;

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
    power_status.acpi_pm1_control_available = 0;
    power_status.acpi_mode_known = 0;
    power_status.acpi_mode_enabled = 0;
    power_status.acpi_s5_declared = 0;
    power_status.acpi_mode_enable_available = 0;
    power_status.acpi_s5_transition_ready = 0;
    if (acpi_get_status(&acpi_status) == OK) {
        power_status.acpi_available = acpi_status.available;
        power_status.acpi_power_tables_available =
            acpi_status.fadt_present && acpi_status.dsdt_present;
        power_status.acpi_partial = acpi_status.partial;
    } else {
        LOG_WARN("POWER", "Estado ACPI indisponivel para diagnostico");
    }
    if (acpi_get_power_info(&acpi_power_info) == OK) {
        power_status.acpi_pm1_control_available =
            acpi_power_info.pm1a_readable &&
            (!acpi_power_info.pm1b_present ||
             acpi_power_info.pm1b_readable);
        power_status.acpi_mode_known =
            acpi_power_info.mode == ACPI_MODE_ENABLED ||
            acpi_power_info.mode == ACPI_MODE_DISABLED;
        power_status.acpi_mode_enabled =
            acpi_power_info.mode == ACPI_MODE_ENABLED;
        power_status.acpi_s5_declared =
            acpi_power_info.s5_state == ACPI_S5_DECLARED;
        power_status.acpi_mode_enable_available =
            acpi_power_info.mode_enable_available;
        power_status.acpi_s5_transition_ready =
            acpi_power_info.s5_transition_ready;
        if (power_status.acpi_s5_transition_ready) {
            power_status.states[POWER_STATE_S5] =
                POWER_CAPABILITY_AVAILABLE;
            power_status.hardware_poweroff =
                POWER_CAPABILITY_AVAILABLE;
        }
    } else {
        LOG_WARN("POWER", "Snapshot de energia ACPI indisponivel");
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

int power_reboot(void) {
    uint32_t flags;

    if (!power_initialized) {
        LOG_ERROR("POWER", "Reinicio solicitado antes da inicializacao");
        return ERR_STATE;
    }
    if (power_status.reboot != POWER_CAPABILITY_AVAILABLE) {
        LOG_ERROR("POWER", "Controlador de reinicio indisponivel");
        return ERR_UNAVAILABLE;
    }
    LOG_INFO("POWER", "Solicitando reinicio pelo controlador 8042");
    for (uint32_t count = 0U; count < POWER_KBC_WAIT_LIMIT; count++) {
        uint8_t status;
        asm volatile("inb %1, %0" : "=a"(status) :
                     "Nd"((uint16_t)POWER_KBC_STATUS_PORT));
        if (!(status & POWER_KBC_INPUT_FULL)) {
            asm volatile("pushfl; popl %0" : "=r"(flags));
            asm volatile("cli");
            asm volatile("outb %0, %1" : :
                         "a"((uint8_t)POWER_KBC_RESET_COMMAND),
                         "Nd"((uint16_t)POWER_KBC_STATUS_PORT));
            for (uint32_t wait = 0U; wait < POWER_RESET_RETURN_WAIT; wait++) {
                asm volatile("pause");
            }
            if (flags & (1U << 9U)) asm volatile("sti");
            LOG_ERROR("POWER", "Controlador retornou apos solicitar reinicio");
            return ERR_TIMEOUT;
        }
    }
    LOG_ERROR("POWER", "Controlador ocupado ao solicitar reinicio");
    return ERR_TIMEOUT;
}

int power_shutdown_prepare(void) {
    int result;

    if (!power_initialized) {
        LOG_ERROR("POWER", "Preparacao de desligamento antes da inicializacao");
        return ERR_STATE;
    }
    result = storage_sync_all();
    if (result != OK) {
        LOG_ERROR_CODE("POWER", result,
                       "Desligamento recusado por falha de sincronizacao");
        return result;
    }
    LOG_INFO("POWER", "Sincronizacao concluida antes do desligamento");
    return OK;
}

void power_shutdown(void) {
    int result;

    LOG_INFO("POWER", "Iniciando desligamento terminal");
    speaker_off();
    ac97_stop();
    if (!power_initialized) {
        LOG_ERROR("POWER", "Servico nao inicializado; usando HLT");
        power_halt_fallback();
    }
    if (!power_status.acpi_s5_transition_ready) {
        LOG_WARN("POWER", "S5 ACPI indisponivel; usando HLT");
        power_halt_fallback();
    }

    result = acpi_enter_s5();
    if (result != OK) {
        LOG_ERROR("POWER", "S5 ACPI recusado; usando HLT");
    }
    power_halt_fallback();
}

const char* power_capability_name(power_capability_t capability) {
    if (capability == POWER_CAPABILITY_AVAILABLE) return "DISPONIVEL";
    if (capability == POWER_CAPABILITY_SIMULATED) return "SIMULADO";
    return "INDISPONIVEL";
}
