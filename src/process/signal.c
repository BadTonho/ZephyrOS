#include "process/signal.h"
#include "process/process.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/syscall.h"
#include "memory/paging.h"

#define SIGNAL_EFLAGS_INTERRUPT_ENABLE (1U << 9U)
#define SIGNAL_FRAME_WORDS 2U
#define SIGNAL_FRAME_SIZE (SIGNAL_FRAME_WORDS * sizeof(uint32_t))

static process_signal_stats_t signal_stats;

static const uint8_t signal_trampoline[USER_SIGNAL_TRAMPOLINE_SIZE] = {
    0xB8U, APP_SYSCALL_SIGNAL_RETURN, 0x00U, 0x00U, 0x00U,
    0xCDU, 0x80U, 0x0FU, 0x0BU,
    0x90U, 0x90U, 0x90U, 0x90U, 0x90U, 0x90U, 0x90U
};

static uint32_t signal_irq_save(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static void signal_irq_restore(uint32_t flags) {
    if (flags & SIGNAL_EFLAGS_INTERRUPT_ENABLE) {
        asm volatile("sti" : : : "memory");
    }
}

static int signal_supported(uint32_t signal_number) {
    return signal_number == APP_SIGNAL_INT ||
           signal_number == APP_SIGNAL_KILL ||
           signal_number == APP_SIGNAL_SEGV ||
           signal_number == APP_SIGNAL_TERM ||
           signal_number == APP_SIGNAL_CHLD;
}

static int signal_unblockable(uint32_t signal_number) {
    return signal_number == APP_SIGNAL_KILL ||
           signal_number == APP_SIGNAL_SEGV;
}

static int signal_terminating(uint32_t signal_number) {
    return signal_number == APP_SIGNAL_INT ||
           signal_number == APP_SIGNAL_KILL ||
           signal_number == APP_SIGNAL_SEGV ||
           signal_number == APP_SIGNAL_TERM;
}

static int signal_process_active(const process_t* process) {
    return process && (process->state == PROCESS_STATE_READY ||
                       process->state == PROCESS_STATE_RUNNING ||
                       process->state == PROCESS_STATE_BLOCKED);
}

static int signal_handler_valid(const process_t* process,
                                uint32_t handler) {
    return process && process_is_user(process) && process->user_code_size &&
           handler >= USER_CODE_BASE &&
           handler - USER_CODE_BASE < process->user_code_size;
}

static uint32_t signal_select(uint32_t pending) {
    if (pending & APP_SIGNAL_BIT(APP_SIGNAL_KILL)) return APP_SIGNAL_KILL;
    if (pending & APP_SIGNAL_BIT(APP_SIGNAL_SEGV)) return APP_SIGNAL_SEGV;
    for (uint32_t signal_number = 1U;
         signal_number < PROCESS_SIGNAL_ACTION_COUNT; signal_number++) {
        if (pending & APP_SIGNAL_BIT(signal_number)) return signal_number;
    }
    return 0U;
}

static int signal_context_empty(const registers_t* regs) {
    const uint8_t* bytes = (const uint8_t*)regs;

    for (uint32_t index = 0U; index < sizeof(*regs); index++) {
        if (bytes[index]) return 0;
    }
    return 1;
}

static int signal_validate_process(const process_t* process) {
    if ((process->pending_signals & ~APP_SIGNAL_SUPPORTED_MASK) ||
        (process->blocked_signals & ~APP_SIGNAL_SUPPORTED_MASK)) {
        LOG_ERROR_CODE("PROC", (int32_t)process->pid,
                       "Bitmap de sinais contem bits desconhecidos");
        return ERR_STATE;
    }
    if (process->blocked_signals & APP_SIGNAL_UNBLOCKABLE_MASK) {
        LOG_ERROR_CODE("PROC", (int32_t)process->pid,
                       "Processo bloqueou sinal fatal");
        return ERR_STATE;
    }
    if (process->active_signal >= PROCESS_SIGNAL_ACTION_COUNT ||
        (process->active_signal &&
         !signal_supported(process->active_signal)) ||
        (process->last_signal && !signal_supported(process->last_signal)) ||
        (process->termination_signal &&
         !signal_terminating(process->termination_signal))) {
        LOG_ERROR_CODE("PROC", (int32_t)process->pid,
                       "Sinal ativo, entregue ou terminal invalido");
        return ERR_STATE;
    }
    if (process->signal_context_valid !=
        (process->active_signal != 0U)) {
        LOG_ERROR_CODE("PROC", (int32_t)process->pid,
                       "Contexto salvo diverge do sinal ativo");
        return ERR_STATE;
    }
    if (process->signal_context_valid && !process_is_user(process)) {
        LOG_ERROR_CODE("PROC", (int32_t)process->pid,
                       "Contexto de sinal pertence a processo ring0");
        return ERR_STATE;
    }
    if (!process->signal_context_valid &&
        (process->signal_saved_mask ||
         !signal_context_empty(&process->signal_saved_context))) {
        LOG_ERROR_CODE("PROC", (int32_t)process->pid,
                       "Contexto salvo orfao");
        return ERR_STATE;
    }
    if (process->signal_context_valid &&
        (((process->signal_saved_context.cs & 0x03U) != 0x03U) ||
         !(process->signal_saved_context.eflags &
           SIGNAL_EFLAGS_INTERRUPT_ENABLE))) {
        LOG_ERROR_CODE("PROC", (int32_t)process->pid,
                       "Contexto salvo sem retorno ring3 habilitado");
        return ERR_STATE;
    }
    if (process->pid != 0U && process->parent_pid == process->pid) {
        LOG_ERROR_CODE("PROC", (int32_t)process->pid,
                       "Processo e seu proprio pai");
        return ERR_STATE;
    }
    if (process->signal_context_valid > 1U ||
        process->signal_exit_notified > 1U ||
        process->cancel_pending > 1U) {
        LOG_ERROR_CODE("PROC", (int32_t)process->pid,
                       "Flags de ciclo de vida do processo invalidas");
        return ERR_STATE;
    }
    if (!process->cancel_pending && process->cancel_exit_code) {
        LOG_ERROR_CODE("PROC", (int32_t)process->pid,
                       "Codigo de cancelamento pendente orfao");
        return ERR_STATE;
    }
    if (process->state != PROCESS_STATE_ZOMBIE &&
        (process->termination_signal || process->signal_exit_notified)) {
        LOG_ERROR_CODE("PROC", (int32_t)process->pid,
                       "Estado vivo possui termino ou notificacao de saida");
        return ERR_STATE;
    }
    if (process->signal_actions[0].disposition !=
            APP_SIGNAL_DISPOSITION_DEFAULT ||
        process->signal_actions[0].handler ||
        process->signal_actions[0].mask) {
        LOG_ERROR_CODE("PROC", (int32_t)process->pid,
                       "Acao reservada de sinal nao esta vazia");
        return ERR_STATE;
    }
    if (process->state == PROCESS_STATE_ZOMBIE &&
        (process->pending_signals || process->blocked_signals ||
         process->active_signal || process->signal_context_valid ||
         process->cancel_pending || process->cancel_exit_code)) {
        LOG_ERROR_CODE("PROC", (int32_t)process->pid,
                       "Zombie reteve estado de sinal");
        return ERR_STATE;
    }
    if (process->parent_pid && !process_get_by_pid(process->parent_pid)) {
        LOG_ERROR_CODE("PROC", (int32_t)process->pid,
                       "Processo possui pai inexistente");
        return ERR_STATE;
    }
    for (uint32_t signal_number = 1U;
         signal_number < PROCESS_SIGNAL_ACTION_COUNT; signal_number++) {
        const app_signal_action_t* action =
            &process->signal_actions[signal_number];

        if ((!signal_supported(signal_number) &&
             (action->disposition != APP_SIGNAL_DISPOSITION_DEFAULT ||
              action->handler || action->mask)) ||
            action->disposition > APP_SIGNAL_DISPOSITION_HANDLER ||
            (action->mask & ~APP_SIGNAL_SUPPORTED_MASK) ||
            (signal_unblockable(signal_number) &&
             action->disposition != APP_SIGNAL_DISPOSITION_DEFAULT) ||
            (action->disposition == APP_SIGNAL_DISPOSITION_HANDLER &&
             !signal_handler_valid(process, action->handler)) ||
            (action->disposition != APP_SIGNAL_DISPOSITION_HANDLER &&
             action->handler != 0U)) {
            LOG_ERROR_CODE("PROC", (int32_t)process->pid,
                           "Tabela de acoes de sinais invalida");
            return ERR_STATE;
        }
    }
    return OK;
}

const char* process_signal_name(uint32_t signal_number) {
    switch (signal_number) {
        case APP_SIGNAL_INT: return "SIGINT";
        case APP_SIGNAL_KILL: return "SIGKILL";
        case APP_SIGNAL_SEGV: return "SIGSEGV";
        case APP_SIGNAL_TERM: return "SIGTERM";
        case APP_SIGNAL_CHLD: return "SIGCHLD";
        default: return "INVALID";
    }
}

int process_signal_init(void) {
    LOG_INFO("PROC", "Inicializando sinais de processos");
    kmemset(&signal_stats, 0, sizeof(signal_stats));
    signal_stats.initialized = 1U;
    signal_stats.last_error = OK;
    LOG_INFO("PROC", "Sinais de processos inicializados");
    return OK;
}

void process_signal_process_created(uint32_t pid, uint32_t parent_pid) {
    process_t* process = process_get_by_pid(pid);
    process_t* parent = process_get_by_pid(parent_pid);

    if (!process) {
        signal_stats.invariant_failures++;
        signal_stats.last_error = ERR_NOT_FOUND;
        LOG_ERROR("PROC", "Processo inexistente ao inicializar sinais");
        return;
    }
    process->parent_pid = parent && parent != process ? parent_pid : 0U;
    process->pending_signals = 0U;
    process->blocked_signals = 0U;
    process->active_signal = 0U;
    process->last_signal = 0U;
    process->termination_signal = 0U;
    process->last_child_pid = 0U;
    process->signal_context_valid = 0U;
    process->signal_exit_notified = 0U;
    process->cancel_exit_code = 0U;
    process->cancel_pending = 0U;
    for (uint32_t index = 0U;
         index < PROCESS_SIGNAL_ACTION_COUNT; index++) {
        process->signal_actions[index].disposition =
            APP_SIGNAL_DISPOSITION_DEFAULT;
        process->signal_actions[index].handler = 0U;
        process->signal_actions[index].mask = 0U;
    }
}

static int signal_ignored_by_action(process_t* target,
                                    uint32_t signal_number) {
    app_signal_action_t* action = &target->signal_actions[signal_number];

    if (action->disposition == APP_SIGNAL_DISPOSITION_IGNORE) return 1;
    return signal_number == APP_SIGNAL_CHLD &&
           action->disposition == APP_SIGNAL_DISPOSITION_DEFAULT;
}

int process_signal_send(uint32_t pid, uint32_t signal_number) {
    process_t* target;
    uint32_t bit;
    uint32_t flags;
    int wake_target = 0;
    int result;

    if (!signal_stats.initialized) {
        LOG_ERROR("PROC", "Envio de sinal antes da inicializacao");
        return ERR_STATE;
    }
    if (!signal_supported(signal_number)) {
        signal_stats.rejected++;
        signal_stats.last_error = ERR_INVALID;
        LOG_WARN("PROC", "Numero de sinal invalido");
        return ERR_INVALID;
    }
    target = process_get_by_pid(pid);
    if (!target || !signal_process_active(target)) {
        signal_stats.rejected++;
        signal_stats.last_error = ERR_NOT_FOUND;
        LOG_WARN("PROC", "Destino de sinal inexistente ou inativo");
        return ERR_NOT_FOUND;
    }
    if (!process_is_user(target) && signal_number != APP_SIGNAL_CHLD) {
        signal_stats.rejected++;
        signal_stats.last_error = ERR_UNAVAILABLE;
        LOG_WARN("PROC", "Sinal recusado para processo ring0");
        return ERR_UNAVAILABLE;
    }

    signal_stats.sent++;
    if (signal_ignored_by_action(target, signal_number)) {
        target->signal_ignored++;
        signal_stats.ignored++;
        return OK;
    }
    if (signal_unblockable(signal_number)) {
        result = process_terminate_user_signal(
            pid, signal_number, signal_number == APP_SIGNAL_SEGV);
        if (result != OK) {
            signal_stats.internal_failures++;
            signal_stats.last_error = result;
            LOG_ERROR_CODE("PROC", result, "Falha ao aplicar sinal fatal");
            return result;
        }
        signal_stats.delivered++;
        signal_stats.terminated++;
        return OK;
    }

    bit = APP_SIGNAL_BIT(signal_number);
    flags = signal_irq_save();
    if (target->pending_signals & bit) {
        signal_stats.coalesced++;
    } else {
        target->pending_signals |= bit;
    }
    if (target->blocked_signals & bit) {
        signal_stats.blocked++;
    } else if (target->state == PROCESS_STATE_BLOCKED &&
               target->wait_active && target->wait_entry.linked) {
        wake_target = 1;
    }
    signal_irq_restore(flags);

    if (wake_target) {
        result = wait_queue_remove(&target->wait_entry, WAIT_REASON_SIGNAL);
        if (result != OK) {
            signal_stats.internal_failures++;
            signal_stats.last_error = result;
            LOG_ERROR_CODE("PROC", result,
                           "Falha ao acordar processo para sinal");
            return result;
        }
        signal_stats.woken++;
    }
    return OK;
}

int process_signal_raise(uint32_t signal_number) {
    process_t* current = process_get_current();

    if (!current || !process_is_user(current)) {
        LOG_ERROR("PROC", "Sinal proprio sem processo ring3 atual");
        return ERR_STATE;
    }
    return process_signal_send(current->pid, signal_number);
}

int process_signal_action(uint32_t signal_number,
                          const app_signal_action_t* action,
                          app_signal_action_t* old_action) {
    process_t* current = process_get_current();
    uint32_t flags;

    if (!current || !process_is_user(current)) {
        LOG_ERROR("PROC", "Acao de sinal sem processo ring3 atual");
        return ERR_STATE;
    }
    if (!signal_supported(signal_number)) {
        LOG_WARN("PROC", "Acao solicitada para sinal invalido");
        return ERR_INVALID;
    }
    if (action && signal_unblockable(signal_number)) {
        LOG_WARN("PROC", "Acao recusada para sinal fatal");
        return ERR_UNAVAILABLE;
    }
    if (action && action->disposition > APP_SIGNAL_DISPOSITION_HANDLER) {
        LOG_WARN("PROC", "Disposicao de sinal invalida");
        return ERR_INVALID;
    }
    if (action && action->disposition == APP_SIGNAL_DISPOSITION_HANDLER &&
        !signal_handler_valid(current, action->handler)) {
        LOG_WARN("PROC", "Handler de sinal fora do codigo ring3");
        return ERR_INVALID;
    }
    if (action && action->disposition != APP_SIGNAL_DISPOSITION_HANDLER &&
        action->handler != 0U) {
        LOG_WARN("PROC", "Endereco informado para acao sem handler");
        return ERR_INVALID;
    }
    if (action && (action->mask & ~APP_SIGNAL_SUPPORTED_MASK)) {
        LOG_WARN("PROC", "Mascara da acao contem sinais desconhecidos");
        return ERR_INVALID;
    }

    flags = signal_irq_save();
    if (old_action) *old_action = current->signal_actions[signal_number];
    if (action) {
        current->signal_actions[signal_number] = *action;
        current->signal_actions[signal_number].mask &=
            ~APP_SIGNAL_UNBLOCKABLE_MASK;
    }
    signal_irq_restore(flags);
    return OK;
}

int process_signal_mask(uint32_t operation, uint32_t mask,
                        uint32_t* old_mask) {
    process_t* current = process_get_current();
    uint32_t flags;

    if (!current || !process_is_user(current)) {
        LOG_ERROR("PROC", "Mascara de sinal sem processo ring3 atual");
        return ERR_STATE;
    }
    if (operation > APP_SIGNAL_MASK_SET ||
        (mask & ~APP_SIGNAL_SUPPORTED_MASK)) {
        LOG_WARN("PROC", "Operacao ou mascara de sinais invalida");
        return ERR_INVALID;
    }
    mask &= ~APP_SIGNAL_UNBLOCKABLE_MASK;
    flags = signal_irq_save();
    if (old_mask) *old_mask = current->blocked_signals;
    if (operation == APP_SIGNAL_MASK_BLOCK) {
        current->blocked_signals |= mask;
    } else if (operation == APP_SIGNAL_MASK_UNBLOCK) {
        current->blocked_signals &= ~mask;
    } else {
        current->blocked_signals = mask;
    }
    signal_irq_restore(flags);
    return OK;
}

static int signal_terminate_current(registers_t* regs,
                                    uint32_t signal_number,
                                    int frame_failure) {
    process_t* current = process_get_current();
    int result;

    if (frame_failure) {
        signal_stats.frame_failures++;
        signal_number = APP_SIGNAL_SEGV;
    }
    result = process_terminate_user_signal(current->pid, signal_number,
                                           signal_number == APP_SIGNAL_SEGV);
    if (result != OK) {
        signal_stats.internal_failures++;
        signal_stats.last_error = result;
        return result;
    }
    signal_stats.delivered++;
    signal_stats.terminated++;
    result = process_prepare_user_termination(regs);
    if (result != OK) {
        signal_stats.internal_failures++;
        signal_stats.last_error = result;
    }
    return result;
}

static int signal_deliver_handler(process_t* current, registers_t* regs,
                                  uint32_t signal_number,
                                  const app_signal_action_t* action) {
    uint32_t frame[SIGNAL_FRAME_WORDS];
    uint32_t new_stack;
    int result;

    if (!signal_handler_valid(current, action->handler) ||
        regs->useresp < USER_STACK_BASE + SIGNAL_FRAME_SIZE ||
        regs->useresp > USER_STACK_TOP) {
        LOG_WARN("PROC", "Contexto ring3 invalido para handler de sinal");
        return signal_terminate_current(regs, APP_SIGNAL_SEGV, 1);
    }
    new_stack = regs->useresp - SIGNAL_FRAME_SIZE;
    result = paging_copy_to_user((void*)USER_SIGNAL_TRAMPOLINE_BASE,
                                 signal_trampoline,
                                 USER_SIGNAL_TRAMPOLINE_SIZE);
    if (result != OK) {
        LOG_ERROR_CODE("PROC", result,
                       "Falha ao restaurar trampoline de sinal");
        return signal_terminate_current(regs, APP_SIGNAL_SEGV, 1);
    }
    frame[0] = USER_SIGNAL_TRAMPOLINE_BASE;
    frame[1] = signal_number;
    result = paging_copy_to_user((void*)new_stack, frame, sizeof(frame));
    if (result != OK) {
        LOG_ERROR_CODE("PROC", result, "Falha ao montar frame de sinal");
        return signal_terminate_current(regs, APP_SIGNAL_SEGV, 1);
    }

    current->signal_saved_context = *regs;
    current->signal_saved_mask = current->blocked_signals;
    current->signal_context_valid = 1U;
    current->active_signal = signal_number;
    current->blocked_signals |= action->mask |
                                APP_SIGNAL_BIT(signal_number);
    current->blocked_signals &= ~APP_SIGNAL_UNBLOCKABLE_MASK;
    current->last_signal = signal_number;
    current->signal_delivered++;
    current->signal_caught++;
    signal_stats.delivered++;
    signal_stats.caught++;
    regs->eip = action->handler;
    regs->useresp = new_stack;
    regs->eax = signal_number;
    return OK;
}

int process_signal_prepare_user_return(registers_t* regs) {
    process_t* current = process_get_current();
    app_signal_action_t action;
    uint32_t deliverable;
    uint32_t signal_number;
    uint32_t flags;

    if (!regs) {
        LOG_ERROR("PROC", "Retorno de sinal sem registradores");
        return ERR_NULL;
    }
    if (!current || !process_is_user(current) ||
        (regs->cs & 0x03U) != 0x03U) return OK;
    if (current->cancel_pending) {
        return process_apply_pending_cancel(regs);
    }
    if (current->state == PROCESS_STATE_ZOMBIE) {
        return process_prepare_user_termination(regs);
    }
    if (current->state != PROCESS_STATE_RUNNING ||
        current->signal_context_valid) return OK;

    flags = signal_irq_save();
    deliverable = current->pending_signals & ~current->blocked_signals;
    signal_number = signal_select(deliverable);
    if (!signal_number) {
        signal_irq_restore(flags);
        return OK;
    }
    current->pending_signals &= ~APP_SIGNAL_BIT(signal_number);
    action = current->signal_actions[signal_number];
    signal_irq_restore(flags);

    if (action.disposition == APP_SIGNAL_DISPOSITION_IGNORE ||
        (signal_number == APP_SIGNAL_CHLD &&
         action.disposition == APP_SIGNAL_DISPOSITION_DEFAULT)) {
        current->signal_ignored++;
        current->last_signal = signal_number;
        signal_stats.ignored++;
        return OK;
    }
    if (action.disposition == APP_SIGNAL_DISPOSITION_DEFAULT) {
        return signal_terminate_current(regs, signal_number, 0);
    }
    return signal_deliver_handler(current, regs, signal_number, &action);
}

int process_signal_return(registers_t* regs) {
    process_t* current = process_get_current();
    uint32_t flags;

    if (!regs) {
        LOG_ERROR("PROC", "Sigreturn recebeu registradores nulos");
        return ERR_NULL;
    }
    if (!current || !process_is_user(current) ||
        !current->signal_context_valid || !current->active_signal) {
        LOG_WARN("PROC", "Sigreturn sem handler ativo");
        return ERR_STATE;
    }
    flags = signal_irq_save();
    *regs = current->signal_saved_context;
    current->blocked_signals = current->signal_saved_mask &
                               ~APP_SIGNAL_UNBLOCKABLE_MASK;
    current->signal_saved_mask = 0U;
    current->active_signal = 0U;
    current->signal_context_valid = 0U;
    kmemset(&current->signal_saved_context, 0,
            sizeof(current->signal_saved_context));
    signal_irq_restore(flags);
    return OK;
}

int process_signal_record_user_fault(registers_t* regs) {
    process_t* current = process_get_current();

    if (!regs || !current || !process_is_user(current) ||
        (regs->cs & 0x03U) != 0x03U) return ERR_STATE;
    signal_stats.user_faults++;
    if (process_terminate_user_signal(current->pid, APP_SIGNAL_SEGV, 1) != OK) {
        signal_stats.internal_failures++;
        signal_stats.last_error = ERR_STATE;
        return ERR_STATE;
    }
    signal_stats.delivered++;
    signal_stats.terminated++;
    return OK;
}

void process_signal_process_exited(uint32_t pid) {
    process_t* child = process_get_by_pid(pid);
    process_t* parent;

    if (!child || child->signal_exit_notified) return;
    child->signal_exit_notified = 1U;
    if (!child->parent_pid) return;
    parent = process_get_by_pid(child->parent_pid);
    if (!parent || parent == child) {
        child->parent_pid = 0U;
        return;
    }
    parent->last_child_pid = child->pid;
    signal_stats.child_notifications++;
    if (process_signal_send(parent->pid, APP_SIGNAL_CHLD) != OK) {
        signal_stats.internal_failures++;
        LOG_WARN("PROC", "Notificacao SIGCHLD nao foi entregue");
    }
}

void process_signal_process_destroyed(uint32_t pid) {
    for (uint32_t index = 0U; index < MAX_PROCESSES; index++) {
        if (processes[index].state != PROCESS_STATE_UNUSED &&
            processes[index].parent_pid == pid) {
            processes[index].parent_pid = 0U;
        }
    }
}

int process_signal_copy_info(process_signal_info_t* output,
                             uint32_t max_entries, uint32_t* out_count) {
    uint32_t count = 0U;
    uint32_t flags;

    if (!out_count || (max_entries && !output)) {
        LOG_ERROR("PROC", "Destino nulo para snapshot de sinais");
        return ERR_NULL;
    }
    flags = signal_irq_save();
    for (uint32_t index = 0U;
         index < MAX_PROCESSES && count < max_entries; index++) {
        process_t* process = &processes[index];
        process_signal_info_t* info;

        if (process->state == PROCESS_STATE_UNUSED) continue;
        info = &output[count++];
        info->pid = process->pid;
        info->parent_pid = process->parent_pid;
        info->pending = process->pending_signals;
        info->blocked = process->blocked_signals;
        info->active_signal = process->active_signal;
        info->last_delivered = process->last_signal;
        info->termination_signal = process->termination_signal;
        info->last_child_pid = process->last_child_pid;
        info->delivered = process->signal_delivered;
        info->caught = process->signal_caught;
        info->ignored = process->signal_ignored;
    }
    signal_irq_restore(flags);
    *out_count = count;
    return OK;
}

int process_signal_get_stats(process_signal_stats_t* out_stats) {
    uint32_t flags;

    if (!out_stats) {
        LOG_ERROR("PROC", "Destino nulo para metricas de sinais");
        return ERR_NULL;
    }
    flags = signal_irq_save();
    *out_stats = signal_stats;
    signal_irq_restore(flags);
    return signal_stats.initialized ? OK : ERR_STATE;
}

int process_signal_validate_state(void) {
    uint32_t flags;
    int result = OK;

    if (!signal_stats.initialized) return ERR_STATE;
    flags = signal_irq_save();
    for (uint32_t index = 0U; index < MAX_PROCESSES; index++) {
        process_t* process = &processes[index];

        if (process->state == PROCESS_STATE_UNUSED) continue;
        result = signal_validate_process(process);
        if (result != OK) break;
    }
    signal_irq_restore(flags);
    if (result != OK) {
        signal_stats.invariant_failures++;
        signal_stats.last_error = result;
        LOG_ERROR("PROC", "Invariantes de sinais inconsistentes");
    }
    return result;
}

int process_signal_self_test(process_signal_self_test_t* out_test) {
    app_signal_action_t handler_action;
    uint32_t sanitized_mask;

    if (!out_test) {
        LOG_ERROR("PROC", "Destino nulo para autoteste de sinais");
        return ERR_NULL;
    }
    kmemset(out_test, 0, sizeof(*out_test));
    handler_action.disposition = APP_SIGNAL_DISPOSITION_HANDLER;
    handler_action.handler = USER_CODE_BASE;
    handler_action.mask = APP_SIGNAL_BIT(APP_SIGNAL_TERM);
    sanitized_mask = (APP_SIGNAL_SUPPORTED_MASK |
                      APP_SIGNAL_UNBLOCKABLE_MASK) &
                     ~APP_SIGNAL_UNBLOCKABLE_MASK;
    out_test->lifecycle = signal_stats.initialized;
    out_test->actions = handler_action.disposition ==
                        APP_SIGNAL_DISPOSITION_HANDLER &&
                        handler_action.handler == USER_CODE_BASE;
    out_test->blocking = !(sanitized_mask & APP_SIGNAL_UNBLOCKABLE_MASK);
    out_test->coalescing =
        (APP_SIGNAL_BIT(APP_SIGNAL_INT) | APP_SIGNAL_BIT(APP_SIGNAL_INT)) ==
        APP_SIGNAL_BIT(APP_SIGNAL_INT);
    out_test->fatal_rules = signal_unblockable(APP_SIGNAL_KILL) &&
                            signal_unblockable(APP_SIGNAL_SEGV) &&
                            !signal_unblockable(APP_SIGNAL_TERM);
    out_test->child_notification = APP_SIGNAL_CHLD <
                                   PROCESS_SIGNAL_ACTION_COUNT;
    out_test->frame_rules = USER_SIGNAL_TRAMPOLINE_SIZE ==
                            sizeof(signal_trampoline) &&
                            SIGNAL_FRAME_SIZE == 8U;
    out_test->invariants = process_signal_validate_state() == OK;
    if (!out_test->lifecycle || !out_test->actions || !out_test->blocking ||
        !out_test->coalescing || !out_test->fatal_rules ||
        !out_test->child_notification || !out_test->frame_rules ||
        !out_test->invariants) {
        LOG_ERROR("PROC", "Autoteste de sinais encontrou falha");
        return ERR_STATE;
    }
    return OK;
}
