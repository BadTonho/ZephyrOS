#include "core/power.h"
#include "core/power_notifier.h"
#include "core/errors.h"
#include "core/keyboard.h"
#include "core/log.h"
#include "core/network_manager.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/video.h"
#include "core/workqueue.h"
#include "drivers/ac97.h"
#include "drivers/acpi.h"
#include "drivers/idt.h"
#include "drivers/speaker.h"
#include "fs/storage.h"
#include "fs/vfs.h"
#include "process/process.h"

#define POWER_NOTIFICATION_BUDGET_TICKS 250U
#define POWER_SYNC_FLUSH_BUDGET_TICKS 1500U
#define POWER_QUIESCENCE_BUDGET_TICKS 250U
#define POWER_HARDWARE_COMMIT_BUDGET_TICKS 100U
#define POWER_TOTAL_BUDGET_TICKS 2100U
#define POWER_PROCESS_DEADLINE_GUARD_TICKS 8U

static power_status_t power_status;
static uint8_t power_initialized;
static uint8_t power_transaction_active;
static uint32_t power_transaction_deadline;
static uint32_t power_phase_deadline;

static int power_deadline_expired(uint32_t deadline_tick);

static int power_noop_notifier(uint32_t deadline_tick) {
    if ((int32_t)(timer_get_ticks() - deadline_tick) >= 0) {
        LOG_WARN("POWER", "Participante de energia recebeu prazo expirado");
        return ERR_TIMEOUT;
    }
    return OK;
}

static int power_notify_processes(uint32_t deadline_tick) {
    uint32_t sigterm = 0U;
    uint32_t sigkill = 0U;
    uint32_t reaped = 0U;
    uint32_t remaining;
    int result;

    result = process_power_set_quiescing(1U);
    if (result != OK) {
        LOG_ERROR_CODE("POWER", result,
                       "Falha ao bloquear criacao de processos");
        return result;
    }
    remaining = deadline_tick - timer_get_ticks();
    if (power_deadline_expired(deadline_tick) ||
        remaining <= POWER_PROCESS_DEADLINE_GUARD_TICKS) {
        LOG_WARN("POWER", "Prazo insuficiente para notificar processos");
        return ERR_TIMEOUT;
    }
    result = process_power_shutdown_users(
        deadline_tick - POWER_PROCESS_DEADLINE_GUARD_TICKS,
        &sigterm, &sigkill, &reaped);
    power_status.sigterm_sent = sigterm;
    power_status.sigkill_sent = sigkill;
    power_status.processes_reaped = reaped;
    if (result != OK) {
        LOG_ERROR_CODE("POWER", result,
                       "Falha ao encerrar processos ring3");
    }
    return result;
}

static int power_notify_workqueue(uint32_t deadline_tick) {
    if (power_deadline_expired(deadline_tick)) {
        LOG_WARN("POWER", "Prazo para bloquear workqueue expirou");
        return ERR_TIMEOUT;
    }
    {
        int result = workqueue_power_set_quiescing(1U);

        if (result != OK) {
            LOG_ERROR_CODE("POWER", result,
                           "Falha ao bloquear novos trabalhos");
        }
        return result;
    }
}

static int power_notify_vfs(uint32_t deadline_tick) {
    if (power_deadline_expired(deadline_tick)) {
        LOG_WARN("POWER", "Prazo para bloquear VFS expirou");
        return ERR_TIMEOUT;
    }
    {
        int result = vfs_power_set_quiescing(1U);

        if (result != OK) {
            LOG_ERROR_CODE("POWER", result,
                           "Falha ao bloquear novas operacoes VFS");
        }
        return result;
    }
}

static int power_quiesce_workqueue(uint32_t deadline_tick) {
    int result = workqueue_power_quiesce_until(deadline_tick);

    if (result != OK) {
        LOG_ERROR_CODE("POWER", result,
                       "Falha ao drenar workqueue");
    }
    return result;
}

static int power_quiesce_vfs(uint32_t deadline_tick) {
    uint32_t unmounted = 0U;
    int result;

    result = vfs_power_set_quiescing(1U);
    if (result != OK) {
        LOG_ERROR_CODE("POWER", result,
                       "Falha ao bloquear VFS para quiescencia");
        return result;
    }
    result = vfs_power_unmount_storage_until(deadline_tick, &unmounted);
    if (result == OK) {
        power_status.volumes_unmounted = unmounted;
    } else {
        LOG_ERROR_CODE("POWER", result,
                       "Falha ao liberar volumes de Storage");
    }
    return result;
}

static int power_quiesce_audio(uint32_t deadline_tick) {
    if (power_deadline_expired(deadline_tick)) {
        LOG_WARN("POWER", "Prazo para parar audio expirou");
        return ERR_TIMEOUT;
    }
    speaker_off();
    ac97_stop();
    return OK;
}

static int power_quiesce_network(uint32_t deadline_tick) {
    int result;

    if (power_deadline_expired(deadline_tick)) {
        LOG_WARN("POWER", "Prazo para parar rede expirou");
        return ERR_TIMEOUT;
    }
    result = network_manager_quiesce();
    if (result != OK && result != ERR_UNAVAILABLE) {
        LOG_ERROR_CODE("POWER", result,
                       "Falha ao colocar rede em quiescencia");
    }
    return result;
}

static int power_quiesce_video(uint32_t deadline_tick) {
    int result;

    if (power_deadline_expired(deadline_tick)) {
        LOG_WARN("POWER", "Prazo para parar video expirou");
        return ERR_TIMEOUT;
    }
    result = video_power_quiesce();
    if (result != OK && result != ERR_UNAVAILABLE) {
        LOG_ERROR_CODE("POWER", result,
                       "Falha ao colocar video em quiescencia");
    }
    return result;
}

static int power_register_notifiers(void) {
    int result;

    result = power_notifier_init();
    if (result != OK) return result;
    result = power_notifier_register("processes", power_notify_processes,
                                    power_noop_notifier, 0U);
    if (result != OK) return result;
    result = power_notifier_register("workqueue", power_notify_workqueue,
                                    power_quiesce_workqueue, 0U);
    if (result != OK) return result;
    result = power_notifier_register("vfs-storage", power_notify_vfs,
                                    power_quiesce_vfs, 0U);
    if (result != OK) return result;
    result = power_notifier_register("audio", power_noop_notifier,
                                    power_quiesce_audio, 1U);
    if (result != OK) return result;
    result = power_notifier_register("network", power_noop_notifier,
                                    power_quiesce_network, 1U);
    if (result != OK) return result;
    result = power_notifier_register("video", power_noop_notifier,
                                    power_quiesce_video, 1U);
    if (result != OK) return result;
    return power_notifier_finalize();
}

static uint32_t power_irq_save(void) {
#if defined(ZEPHYROS_HOST_TEST)
    return 0U;
#else
    uint32_t flags;

    asm volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
#endif
}

static void power_irq_restore(uint32_t flags) {
#if !defined(ZEPHYROS_HOST_TEST)
    if (flags & (1U << 9U)) asm volatile("sti" : : : "memory");
#else
    (void)flags;
#endif
}

static int power_deadline_expired(uint32_t deadline_tick) {
    return (int32_t)(timer_get_ticks() - deadline_tick) >= 0;
}

static void power_record_failure(int result) {
    uint32_t flags;

    flags = power_irq_save();
    if (power_status.commit_started) {
        power_status.last_error = result;
        power_status.transaction_phase = POWER_TRANSACTION_TERMINAL;
        power_irq_restore(flags);
        LOG_ERROR_CODE("POWER", result,
                       "Falha apos commit; transacao permanece terminal");
        return;
    }
    power_irq_restore(flags);
    (void)process_power_set_quiescing(0U);
    (void)workqueue_power_set_quiescing(0U);
    (void)vfs_power_set_quiescing(0U);
    (void)network_manager_set_quiescing(0U);
    {
        flags = power_irq_save();
        power_status.last_error = result;
        power_status.transaction_phase = POWER_TRANSACTION_IDLE;
        power_status.quiescence_state = POWER_QUIESCENCE_UNKNOWN;
        power_transaction_active = 0U;
        power_irq_restore(flags);
    }
}

static void power_record_admission_error(int result) {
    uint32_t flags = power_irq_save();

    power_status.last_error = result;
    power_irq_restore(flags);
}

static int power_phase_enter(power_transaction_phase_t phase,
                             uint32_t budget_ticks) {
    uint32_t remaining;
    uint32_t flags;
    uint32_t now = timer_get_ticks();

    if (!power_transaction_active ||
        (int32_t)(now - power_transaction_deadline) >= 0) {
        LOG_WARN("POWER", "Fase de energia sem transacao ou fora do prazo");
        return ERR_TIMEOUT;
    }
    remaining = power_transaction_deadline - now;
    if (remaining < budget_ticks) {
        LOG_WARN("POWER", "Fase de energia sem orcamento reservado");
        return ERR_TIMEOUT;
    }
    power_phase_deadline = now + budget_ticks;
    flags = power_irq_save();
    power_status.transaction_phase = phase;
    power_irq_restore(flags);
    return OK;
}

static int power_phase_expired(void) {
    return power_deadline_expired(power_phase_deadline) ||
           power_deadline_expired(power_transaction_deadline);
}

static int power_transaction_begin(uint8_t reboot) {
    uint32_t flags;
    uint32_t now;

    if (!power_initialized) {
        LOG_ERROR("POWER", "Transacao solicitada antes da inicializacao");
        power_record_admission_error(ERR_STATE);
        return ERR_STATE;
    }
    now = timer_get_ticks();
    flags = power_irq_save();
    if (power_transaction_active) {
        power_irq_restore(flags);
        LOG_WARN("POWER", "Transacao de energia concorrente recusada");
        power_record_admission_error(ERR_STATE);
        return ERR_STATE;
    }
    power_irq_restore(flags);
    if (reboot && power_status.reboot != POWER_CAPABILITY_AVAILABLE) {
        LOG_WARN("POWER", "Reinicio indisponivel para admissao");
        power_record_admission_error(ERR_UNAVAILABLE);
        return ERR_UNAVAILABLE;
    }
    if (!reboot &&
        power_status.hardware_poweroff != POWER_CAPABILITY_AVAILABLE) {
        LOG_WARN("POWER", "Desligamento S5 indisponivel para admissao");
        power_record_admission_error(ERR_UNAVAILABLE);
        return ERR_UNAVAILABLE;
    }
    flags = power_irq_save();
    if (power_transaction_active) {
        power_irq_restore(flags);
        LOG_WARN("POWER", "Transacao de energia concorrente recusada");
        power_record_admission_error(ERR_STATE);
        return ERR_STATE;
    }
    power_transaction_active = 1U;
    power_transaction_deadline = now + POWER_TOTAL_BUDGET_TICKS;
    power_phase_deadline = now;
    power_status.transaction_phase = POWER_TRANSACTION_ADMISSION;
    power_status.transaction_target = reboot ?
        POWER_TRANSACTION_TARGET_REBOOT : POWER_TRANSACTION_TARGET_SHUTDOWN;
    power_status.notifiers_completed = 0U;
    power_status.sigterm_sent = 0U;
    power_status.sigkill_sent = 0U;
    power_status.processes_reaped = 0U;
    power_status.volumes_unmounted = 0U;
    power_status.optional_failures = 0U;
    power_status.quiescence_state = POWER_QUIESCENCE_UNKNOWN;
    power_status.commit_started = 0U;
    power_status.transaction_degraded = 0U;
    power_status.last_error = OK;
    power_irq_restore(flags);
    return OK;
}

static int power_run_precommit(void) {
    uint32_t completed = 0U;
    uint32_t optional_failures = 0U;
    int result;

    result = power_phase_enter(POWER_TRANSACTION_NOTIFICATION,
                               POWER_NOTIFICATION_BUDGET_TICKS);
    if (result != OK) return result;
    result = power_notifier_notify(power_phase_deadline, &completed,
                                   &optional_failures);
    if (result != OK) return result;
    if (power_phase_expired()) {
        LOG_WARN("POWER", "Prazo de notificacao de energia expirou");
        return ERR_TIMEOUT;
    }
    power_status.notifiers_completed = completed;
    power_status.optional_failures += optional_failures;
    if (optional_failures) power_status.transaction_degraded = 1U;

    result = power_phase_enter(POWER_TRANSACTION_SYNC_FLUSH,
                               POWER_SYNC_FLUSH_BUDGET_TICKS);
    if (result != OK) return result;
    result = storage_sync_all_until(power_phase_deadline);
    if (result == ERR_STATE) {
        vfs_mount_info_t mounts[VFS_MAX_MOUNTS];
        uint32_t count = 0U;
        uint8_t has_storage = 0U;

        if (vfs_copy_mounts(mounts, VFS_MAX_MOUNTS, &count) != OK) {
            return ERR_STATE;
        }
        for (uint32_t index = 0U; index < count; index++) {
            if (mounts[index].kind == VFS_MOUNT_STORAGE) {
                has_storage = 1U;
                break;
            }
        }
        if (has_storage) return result;
        result = OK;
    }
    if (result != OK) return result;
    if (power_phase_expired()) {
        LOG_WARN("POWER", "Prazo de sync/flush de energia expirou");
        return ERR_TIMEOUT;
    }

    result = power_phase_enter(POWER_TRANSACTION_QUIESCENCE,
                               POWER_QUIESCENCE_BUDGET_TICKS);
    if (result != OK) return result;
    result = power_notifier_quiesce(power_phase_deadline, &completed,
                                    &optional_failures);
    if (result != OK) return result;
    power_status.notifiers_completed += completed;
    power_status.optional_failures += optional_failures;
    power_status.transaction_degraded = optional_failures ? 1U :
                                        power_status.transaction_degraded;
    if (power_phase_expired()) {
        LOG_WARN("POWER", "Prazo de quiescencia de energia expirou");
        return ERR_TIMEOUT;
    }
    power_status.quiescence_state = power_status.transaction_degraded ?
        POWER_QUIESCENCE_DEGRADED : POWER_QUIESCENCE_COMPLETE;
    return OK;
}

static int power_transaction_hardware_phase(void) {
    return power_phase_enter(POWER_TRANSACTION_HARDWARE_COMMIT,
                             POWER_HARDWARE_COMMIT_BUDGET_TICKS);
}

static void power_mark_terminal(void) {
    uint32_t flags = power_irq_save();

    power_status.transaction_phase = POWER_TRANSACTION_TERMINAL;
    power_irq_restore(flags);
}

static void power_mark_commit_started(void) {
    uint32_t flags = power_irq_save();

    power_status.commit_started = 1U;
    power_irq_restore(flags);
}

static void power_terminal_halt(void) __attribute__((noreturn));

static void power_trigger_triple_fault(void) __attribute__((noreturn));

static void power_trigger_triple_fault(void) {
#if defined(ZEPHYROS_HOST_TEST)
    __builtin_trap();
#else
    idt_ptr_t null_idt;

    null_idt.limit = 0U;
    null_idt.base = 0U;
    asm volatile("cli\n\tlidt %0\n\tint3" : : "m"(null_idt) : "memory");
    for (;;) asm volatile("hlt");
#endif
}

static int power_reboot_commit(void) {
    int result;

    if (power_status.reboot_acpi_reset_available) {
        power_mark_commit_started();
        result = acpi_reset();
        if (result == OK) {
            LOG_ERROR("POWER", "RESET_REG ACPI retornou sem reiniciar");
        } else {
            LOG_ERROR_CODE("POWER", result, "RESET_REG ACPI nao reiniciou");
        }
    }
    if (power_status.reboot_ps2_available) {
        power_mark_commit_started();
        result = keyboard_controller_reset();
        if (result == OK) {
            LOG_ERROR("POWER", "Reset PS/2 retornou sem reiniciar");
        } else {
            LOG_ERROR_CODE("POWER", result, "Reset PS/2 nao reiniciou");
        }
    }
    if (!power_status.reboot_triple_fault_available) {
        LOG_ERROR("POWER", "Nenhum metodo de reinicio esta disponivel");
        if (power_status.commit_started) {
            power_mark_terminal();
            power_terminal_halt();
        }
        return ERR_UNAVAILABLE;
    }
    power_mark_commit_started();
    power_mark_terminal();
    LOG_INFO("POWER", "Usando triple fault como ultimo metodo de reinicio");
    power_trigger_triple_fault();
}

static void power_terminal_halt(void) {
#if defined(ZEPHYROS_HOST_TEST)
    __builtin_trap();
#else
    for (;;) asm volatile("cli\n\thlt");
#endif
}

int power_init(void) {
    acpi_status_t acpi_status;
    acpi_power_info_t acpi_power_info;
    int acpi_result;

    LOG_INFO("POWER", "Inicializando coordenador de energia");
    kmemset(&power_status, 0, sizeof(power_status));
    power_status.service_state = POWER_SERVICE_DISCOVERING;
    power_status.transaction_phase = POWER_TRANSACTION_IDLE;
    power_status.last_error = OK;
    power_status.states[POWER_STATE_S0] = POWER_CAPABILITY_AVAILABLE;
    power_status.states[POWER_STATE_S1] = POWER_CAPABILITY_UNAVAILABLE;
    power_status.states[POWER_STATE_S2] = POWER_CAPABILITY_UNAVAILABLE;
    power_status.states[POWER_STATE_S3] = POWER_CAPABILITY_UNAVAILABLE;
    power_status.states[POWER_STATE_S4] = POWER_CAPABILITY_UNAVAILABLE;
    power_status.states[POWER_STATE_S5] = POWER_CAPABILITY_SIMULATED;
    power_status.cpu_idle = POWER_CAPABILITY_AVAILABLE;
    power_status.hardware_poweroff = POWER_CAPABILITY_UNAVAILABLE;
    power_status.reboot = POWER_CAPABILITY_AVAILABLE;
    power_status.reboot_triple_fault_available = 1U;
    power_status.reboot_ps2_available = keyboard_controller_reset_available();
    power_status.acpi_available = 0U;
    power_status.acpi_power_tables_available = 0U;
    power_status.acpi_partial = 0U;
    power_status.acpi_pm1_control_available = 0U;
    power_status.acpi_mode_known = 0U;
    power_status.acpi_mode_enabled = 0U;
    power_status.acpi_s5_declared = 0U;
    power_status.acpi_mode_enable_available = 0U;
    power_status.acpi_s5_transition_ready = 0U;
    power_initialized = 0U;
    power_transaction_active = 0U;

    if (power_register_notifiers() != OK) {
        power_status.service_state = POWER_SERVICE_UNAVAILABLE;
        LOG_ERROR("POWER", "Falha ao registrar cadeia de energia");
        return ERR_STATE;
    }
    if (power_notifier_get_count(&power_status.notifiers_registered) != OK) {
        power_status.service_state = POWER_SERVICE_UNAVAILABLE;
        LOG_ERROR("POWER", "Falha ao consultar cadeia de energia");
        return ERR_STATE;
    }
    if (power_notifier_validate_state() != OK) {
        power_status.service_state = POWER_SERVICE_UNAVAILABLE;
        LOG_ERROR("POWER", "Cadeia de energia invalida");
        return ERR_STATE;
    }

    acpi_result = acpi_get_status(&acpi_status);
    if (acpi_result == OK) {
        power_status.acpi_available = acpi_status.available;
        power_status.acpi_power_tables_available =
            acpi_status.fadt_present && acpi_status.dsdt_present;
        power_status.acpi_partial = acpi_status.partial;
    } else {
        LOG_WARN("POWER", "Estado ACPI indisponivel para energia");
    }
    acpi_result = acpi_get_power_info(&acpi_power_info);
    if (acpi_result == OK) {
        power_status.reboot_acpi_reset_available =
            acpi_power_info.reset_register_present &&
            acpi_power_info.reset_register_valid;
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
    power_status.service_state =
        power_status.hardware_poweroff == POWER_CAPABILITY_AVAILABLE ?
        POWER_SERVICE_READY : POWER_SERVICE_DEGRADED;
    power_initialized = 1U;
    LOG_INFO("POWER", "Coordenador de energia inicializado");
    return OK;
}

int power_get_status(power_status_t* out_status) {
    uint32_t flags;

    if (!out_status) {
        LOG_ERROR("POWER", "Destino nulo ao consultar energia");
        return ERR_NULL;
    }
    flags = power_irq_save();
    if (!power_initialized) {
        power_irq_restore(flags);
        LOG_ERROR("POWER", "Consulta antes da inicializacao de energia");
        return ERR_STATE;
    }
    *out_status = power_status;
    power_irq_restore(flags);
    return OK;
}

int system_reboot(void) {
    int result;

    result = power_transaction_begin(1U);
    if (result != OK) return result;
    result = power_run_precommit();
    if (result != OK) {
        power_record_failure(result);
        LOG_ERROR_CODE("POWER", result, "Reinicio abortado antes do commit");
        return result;
    }
    result = power_transaction_hardware_phase();
    if (result != OK) {
        power_record_failure(result);
        LOG_ERROR_CODE("POWER", result, "Reinicio excedeu o prazo de commit");
        return result;
    }
    result = power_reboot_commit();
    if (result != OK) {
        if (power_status.commit_started) {
            power_mark_terminal();
            power_terminal_halt();
        }
        power_record_failure(result);
        LOG_ERROR_CODE("POWER", result, "Reinicio sem metodo terminal valido");
    }
    return result;
}

int power_reboot(void) {
    return system_reboot();
}

int power_shutdown_request(void) {
    int result;

    result = power_transaction_begin(0U);
    if (result != OK) return result;
    result = power_run_precommit();
    if (result != OK) {
        power_record_failure(result);
        LOG_ERROR_CODE("POWER", result,
                       "Desligamento abortado antes do commit");
        return result;
    }
    result = power_transaction_hardware_phase();
    if (result != OK) {
        power_record_failure(result);
        LOG_ERROR_CODE("POWER", result,
                       "Desligamento excedeu o prazo de commit");
        return result;
    }
    result = acpi_poweroff();
    if (result != OK) {
        LOG_ERROR_CODE("POWER", result, "S5 nao concluiu o desligamento");
        power_record_failure(result);
        return result;
    }
    power_mark_commit_started();
    power_mark_terminal();
    return result;
}

void power_shutdown(void) {
    int result = power_shutdown_request();

    if (result != OK) {
        LOG_ERROR_CODE("POWER", result,
                       "Desligamento recusado; sistema permanece ativo");
    }
}

int power_shutdown_prepare(void) {
    uint32_t flags;
    int result;

    if (!power_initialized) {
        LOG_ERROR("POWER", "Preparacao de desligamento antes da inicializacao");
        return ERR_STATE;
    }
    flags = power_irq_save();
    if (power_transaction_active) {
        power_irq_restore(flags);
        LOG_WARN("POWER", "Preparacao diagnostica recusada durante transacao");
        return ERR_STATE;
    }
    power_irq_restore(flags);
    if (vfs_power_is_quiescing()) {
        LOG_WARN("POWER", "Preparacao diagnostica recusada durante quiescencia");
        return ERR_STATE;
    }
    result = storage_sync_all();
    if (result != OK) {
        LOG_ERROR_CODE("POWER", result,
                       "Diagnostico de desligamento falhou no sync");
        return result;
    }
    LOG_INFO("POWER", "Sincronizacao diagnostica concluida");
    return OK;
}

const char* power_capability_name(power_capability_t capability) {
    if (capability == POWER_CAPABILITY_AVAILABLE) return "DISPONIVEL";
    if (capability == POWER_CAPABILITY_SIMULATED) return "SIMULADO";
    return "INDISPONIVEL";
}

const char* power_service_state_name(power_service_state_t state) {
    if (state == POWER_SERVICE_DISCOVERING) return "DESCOBRINDO";
    if (state == POWER_SERVICE_READY) return "PRONTO";
    if (state == POWER_SERVICE_DEGRADED) return "DEGRADADO";
    if (state == POWER_SERVICE_UNAVAILABLE) return "INDISPONIVEL";
    return "DESCONHECIDO";
}

const char* power_transaction_phase_name(power_transaction_phase_t phase) {
    if (phase == POWER_TRANSACTION_ADMISSION) return "ADMISSION";
    if (phase == POWER_TRANSACTION_NOTIFICATION) return "NOTIFICATION";
    if (phase == POWER_TRANSACTION_SYNC_FLUSH) return "SYNC_FLUSH";
    if (phase == POWER_TRANSACTION_QUIESCENCE) return "QUIESCENCE";
    if (phase == POWER_TRANSACTION_HARDWARE_COMMIT) {
        return "HARDWARE_COMMIT";
    }
    if (phase == POWER_TRANSACTION_TERMINAL) return "TERMINAL";
    return "IDLE";
}

const char* power_transaction_target_name(power_transaction_target_t target) {
    if (target == POWER_TRANSACTION_TARGET_SHUTDOWN) return "SHUTDOWN";
    if (target == POWER_TRANSACTION_TARGET_REBOOT) return "REBOOT";
    return "NONE";
}

const char* power_quiescence_state_name(power_quiescence_state_t state) {
    if (state == POWER_QUIESCENCE_READY) return "READY";
    if (state == POWER_QUIESCENCE_DEGRADED) return "DEGRADED";
    if (state == POWER_QUIESCENCE_COMPLETE) return "COMPLETE";
    return "UNKNOWN";
}
