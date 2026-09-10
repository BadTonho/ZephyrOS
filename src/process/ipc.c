#include "process/process.h"
#include "core/errors.h"
#include "core/spinlock.h"
#include "core/log.h"
#include "core/string.h"
#include "process/resource.h"

#define IPC_EFLAGS_INTERRUPT_ENABLE (1U << 9U)

static spinlock_t ipc_lock;
static uint32_t focused_pid = 0;
static uint32_t focus_fallback_pid = 0;
static ipc_stats_t ipc_stats;
static int ipc_ready = 0;

typedef struct {
    uint32_t pid;
    uint32_t observed_generation;
    uint32_t observed_identity_generation;
} ipc_wait_context_t;

static uint32_t ipc_irq_save(void) {
#if defined(ZEPHYROS_HOST_TEST)
    return 0U;
#else
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
#endif
}

static void ipc_irq_restore(uint32_t flags) {
#if !defined(ZEPHYROS_HOST_TEST)
    if (flags & IPC_EFLAGS_INTERRUPT_ENABLE) {
        asm volatile("sti" : : : "memory");
    }
#else
    (void)flags;
#endif
}

static int ipc_wait_condition(void* context, uint8_t* out_ready) {
    ipc_wait_context_t* wait_context = (ipc_wait_context_t*)context;
    process_t* process;

    if (!wait_context || !wait_context->pid || !out_ready) {
        LOG_ERROR("IPC", "Contexto nulo na condicao de espera");
        return ERR_NULL;
    }
    spinlock_acquire(&ipc_lock);
    process = process_get_by_pid(wait_context->pid);
    *out_ready = !process;
    if (process) {
        *out_ready = process->state == PROCESS_STATE_UNUSED ||
                     process->state == PROCESS_STATE_ZOMBIE ||
                     process->event_generation !=
                         wait_context->observed_identity_generation ||
                     process->msg_head != process->msg_tail ||
                     process->ipc_wait_channel.condition !=
                         wait_context->observed_generation ||
                     (process->pending_signals & ~process->blocked_signals) != 0U;
    }
    spinlock_release(&ipc_lock);
    return OK;
}

void ipc_init(void) {
    if (ipc_ready) return;
    spinlock_init(&ipc_lock);
    focused_pid = 0;
    focus_fallback_pid = 0;
    ipc_stats.sent = 0;
    ipc_stats.received = 0;
    ipc_stats.failed = 0;
    ipc_stats.queue_full = 0;
    ipc_ready = 1;
    LOG_INFO("IPC", "Inter-Process Communication inicializado");
}

int ipc_is_ready(void) {
    return ipc_ready;
}

int ipc_send(uint32_t pid, ipc_msg_t* msg) {
    process_t* target;
    uint32_t next_head;
    uint32_t pending;
    uint32_t woken = 0U;
    uint32_t flags;
    int resource_result;
    int wake_result;

    if (!ipc_ready) {
        LOG_ERROR("IPC", "Envio antes da inicializacao");
        return 0;
    }
    if (!msg) {
        ipc_stats.failed++;
        LOG_ERROR("IPC", "Mensagem nula rejeitada");
        return 0;
    }
    if (msg->type <= IPC_MSG_NONE || msg->type > IPC_MSG_APP_REQUEST) {
        ipc_stats.failed++;
        LOG_ERROR("IPC", "Tipo de mensagem invalido");
        return 0;
    }

    flags = ipc_irq_save();
    spinlock_acquire(&ipc_lock);
    target = process_get_by_pid(pid);
    if (!target || (target->state != PROCESS_STATE_READY &&
                    target->state != PROCESS_STATE_RUNNING &&
                    target->state != PROCESS_STATE_BLOCKED)) {
        ipc_stats.failed++;
        spinlock_release(&ipc_lock);
        ipc_irq_restore(flags);
        LOG_WARN("IPC", "Processo de destino inexistente ou inativo");
        return 0;
    }

    if (target->msg_head >= target->msg_tail) {
        pending = target->msg_head - target->msg_tail;
    } else {
        pending = IPC_MSG_QUEUE_SIZE - target->msg_tail + target->msg_head;
    }
    resource_result = target->context.user_mode ?
        process_resource_check_ipc_pending(target, pending, 1U) : OK;
    if (resource_result != OK) {
        ipc_stats.failed++;
        spinlock_release(&ipc_lock);
        ipc_irq_restore(flags);
        LOG_WARN("IPC", "Quota de mensagens rejeitou envio");
        return 0;
    }
    next_head = (target->msg_head + 1) % IPC_MSG_QUEUE_SIZE;
    if (next_head == target->msg_tail) {
        ipc_stats.failed++;
        ipc_stats.queue_full++;
        process_resource_record_failure(
            target, PROCESS_RESOURCE_FAILURE_IPC_PENDING, ERR_OVERFLOW, 1U);
        spinlock_release(&ipc_lock);
        ipc_irq_restore(flags);
        LOG_WARN("IPC", "Fila de mensagens cheia");
        return 0;
    }

    target->msg_queue[target->msg_head] = *msg;
    target->msg_head = next_head;
    ipc_stats.sent++;

    wake_result = wake_up(&target->ipc_wait_channel, &woken);

    spinlock_release(&ipc_lock);
    ipc_irq_restore(flags);
    if (wake_result != OK) {
        LOG_WARN("IPC", "Falha ao acordar consumidor IPC");
    }
    (void)vfs_poll_notify();
    return 1;
}

int ipc_receive(ipc_msg_t* msg) {
    process_t* current = process_get_current();

    if (!ipc_ready) {
        LOG_ERROR("IPC", "Recebimento antes da inicializacao");
        return 0;
    }
    if (!msg) {
        ipc_stats.failed++;
        LOG_ERROR("IPC", "Buffer de recebimento nulo");
        return 0;
    }
    if (!current || (current->state != PROCESS_STATE_READY &&
                     current->state != PROCESS_STATE_RUNNING)) {
        ipc_stats.failed++;
        LOG_ERROR("IPC", "Recebimento sem processo atual");
        return 0;
    }

    spinlock_acquire(&ipc_lock);

    if (current->msg_head == current->msg_tail) {
        spinlock_release(&ipc_lock);
        return 0;
    }

    *msg = current->msg_queue[current->msg_tail];
    current->msg_tail = (current->msg_tail + 1) % IPC_MSG_QUEUE_SIZE;
    ipc_stats.received++;
    spinlock_release(&ipc_lock);
    return 1;
}

int ipc_current_has_pending(void) {
    process_t* current = process_get_current();
    int pending;

    if (!ipc_ready || !current) return 0;
    spinlock_acquire(&ipc_lock);
    pending = current->msg_head != current->msg_tail;
    spinlock_release(&ipc_lock);
    return pending;
}

int ipc_wait(uint32_t timeout_ticks, wait_reason_t* out_reason) {
    process_t* current = process_get_current();
    process_t* process;
    ipc_wait_context_t context;
    uint32_t pid;
    uint32_t generation;
    int result;

    if (!out_reason) {
        LOG_ERROR("IPC", "Destino nulo para resultado da espera IPC");
        return ERR_NULL;
    }
    *out_reason = WAIT_REASON_NONE;
    if (!ipc_ready) {
        LOG_ERROR("IPC", "Espera solicitada antes da inicializacao");
        return ERR_STATE;
    }
    if (!current) {
        LOG_ERROR("IPC", "Espera IPC sem processo atual");
        return ERR_STATE;
    }
    pid = current->pid;
    context.pid = pid;
    context.observed_generation = current->ipc_wait_generation;
    context.observed_identity_generation = current->event_generation;
    result = wait_event_timeout(&current->ipc_wait_channel,
                                ipc_wait_condition, &context,
                                timeout_ticks, out_reason);
    if (result != OK || *out_reason != WAIT_REASON_EVENT) return result;
    process = process_get_by_pid(pid);
    if (!process || process->state == PROCESS_STATE_UNUSED ||
        process->state == PROCESS_STATE_ZOMBIE ||
        process->event_generation != context.observed_identity_generation) {
        LOG_WARN("IPC", "IPC wait resumed for an obsolete process identity");
        return ERR_NOT_FOUND;
    }
    if (process->pending_signals & ~process->blocked_signals) {
        *out_reason = WAIT_REASON_SIGNAL;
        return OK;
    }
    if (wait_channel_get_condition(&process->ipc_wait_channel,
                                   &generation) != OK) {
        LOG_ERROR("IPC", "Falha ao confirmar geracao da espera IPC");
        return ERR_STATE;
    }
    process->ipc_wait_generation = generation;
    return OK;
}

void ipc_get_stats(ipc_stats_t* stats) {
    if (!stats) {
        LOG_ERROR("IPC", "Buffer de estatisticas nulo");
        return;
    }

    spinlock_acquire(&ipc_lock);
    *stats = ipc_stats;
    spinlock_release(&ipc_lock);
}

uint32_t ipc_get_pending_count(void) {
    uint32_t pending = 0;

    if (!ipc_ready) {
        LOG_WARN("IPC", "Consulta de fila antes da inicializacao");
        return 0;
    }

    spinlock_acquire(&ipc_lock);
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t* process = processes[i];

        if (!process || process->state == PROCESS_STATE_UNUSED ||
            process->state == PROCESS_STATE_ZOMBIE) continue;
        if (process->msg_head >= process->msg_tail) {
            pending += process->msg_head - process->msg_tail;
        } else {
            pending += IPC_MSG_QUEUE_SIZE - process->msg_tail +
                       process->msg_head;
        }
    }
    spinlock_release(&ipc_lock);
    return pending;
}

int ipc_get_pending_count_for_pid(uint32_t pid, uint32_t* pending) {
    process_t* process;

    if (!pending) {
        LOG_ERROR("IPC", "Destino de contagem pendente ausente");
        return ERR_NULL;
    }
    *pending = 0U;
    if (!ipc_ready) return OK;
    process = process_get_by_pid(pid);
    if (!process) {
        LOG_WARN("IPC", "Processo ausente ao consultar fila");
        return ERR_NOT_FOUND;
    }
    spinlock_acquire(&ipc_lock);
    if (process->msg_head >= process->msg_tail) {
        *pending = process->msg_head - process->msg_tail;
    } else {
        *pending = IPC_MSG_QUEUE_SIZE - process->msg_tail +
                   process->msg_head;
    }
    spinlock_release(&ipc_lock);
    return OK;
}

static int process_focus_target_is_valid(process_t* target) {
    return target && (target->state == PROCESS_STATE_READY ||
                      target->state == PROCESS_STATE_RUNNING ||
                      target->state == PROCESS_STATE_BLOCKED);
}

int process_set_focus(uint32_t pid) {
    process_t* target = process_get_by_pid(pid);

    if (!ipc_ready) {
        LOG_ERROR("IPC", "Foco alterado antes da inicializacao IPC");
        return ERR_STATE;
    }
    if (!process_focus_target_is_valid(target)) {
        LOG_WARN("IPC", "Foco rejeitado para PID inexistente ou inativo");
        return ERR_NOT_FOUND;
    }

    focused_pid = target->pid;
    LOG_DEBUG("IPC", "Foco alterado");
    return OK;
}

int process_set_focus_fallback(uint32_t pid) {
    process_t* target = process_get_by_pid(pid);

    if (!ipc_ready) {
        LOG_ERROR("IPC", "Fallback de foco definido antes da inicializacao");
        return ERR_STATE;
    }
    if (!process_focus_target_is_valid(target)) {
        LOG_ERROR("IPC", "Fallback de foco invalido");
        return ERR_NOT_FOUND;
    }

    focus_fallback_pid = target->pid;
    LOG_DEBUG("IPC", "Fallback de foco definido");
    return OK;
}

int process_restore_focus(void) {
    process_t* target = process_get_by_pid(focus_fallback_pid);

    if (!ipc_ready) {
        LOG_ERROR("IPC", "Restauracao de foco antes da inicializacao");
        return ERR_STATE;
    }
    if (!process_focus_target_is_valid(target)) {
        focused_pid = 0;
        LOG_WARN("IPC", "Fallback de foco indisponivel; foco limpo");
        return ERR_NOT_FOUND;
    }

    focused_pid = target->pid;
    LOG_DEBUG("IPC", "Foco restaurado para fallback");
    return OK;
}

uint32_t process_get_focus(void) {
    return focused_pid;
}
