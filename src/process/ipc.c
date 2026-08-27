#include "process/process.h"
#include "core/errors.h"
#include "core/spinlock.h"
#include "core/log.h"
#include "core/string.h"

static spinlock_t ipc_lock;
static uint32_t focused_pid = 0;
static uint32_t focus_fallback_pid = 0;
static ipc_stats_t ipc_stats;
static int ipc_ready = 0;

typedef struct {
    process_t* process;
    uint32_t observed_generation;
} ipc_wait_context_t;

static int ipc_wait_condition(void* context, uint8_t* out_ready) {
    ipc_wait_context_t* wait_context = (ipc_wait_context_t*)context;
    process_t* process;

    if (!wait_context || !wait_context->process || !out_ready) {
        LOG_ERROR("IPC", "Contexto nulo na condicao de espera");
        return ERR_NULL;
    }
    process = wait_context->process;
    spinlock_acquire(&ipc_lock);
    *out_ready = process->msg_head != process->msg_tail ||
                 process->ipc_wait_channel.condition !=
                     wait_context->observed_generation;
    spinlock_release(&ipc_lock);
    return OK;
}

void ipc_init(void) {
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
    uint32_t woken = 0U;

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

    target = process_get_by_pid(pid);
    if (!target || (target->state != PROCESS_STATE_READY &&
                    target->state != PROCESS_STATE_RUNNING &&
                    target->state != PROCESS_STATE_BLOCKED)) {
        ipc_stats.failed++;
        LOG_WARN("IPC", "Processo de destino inexistente ou inativo");
        return 0;
    }

    spinlock_acquire(&ipc_lock);

    next_head = (target->msg_head + 1) % IPC_MSG_QUEUE_SIZE;
    if (next_head == target->msg_tail) {
        ipc_stats.failed++;
        ipc_stats.queue_full++;
        spinlock_release(&ipc_lock);
        LOG_WARN("IPC", "Fila de mensagens cheia");
        return 0;
    }

    target->msg_queue[target->msg_head] = *msg;
    target->msg_head = next_head;
    ipc_stats.sent++;

    spinlock_release(&ipc_lock);
    if (wake_up(&target->ipc_wait_channel, &woken) != OK) {
        LOG_WARN("IPC", "Falha ao acordar consumidor IPC");
    }
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

int ipc_wait(uint32_t timeout_ticks, wait_reason_t* out_reason) {
    process_t* current = process_get_current();
    ipc_wait_context_t context;
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
    context.process = current;
    context.observed_generation = current->ipc_wait_generation;
    result = wait_event_timeout(&current->ipc_wait_channel,
                                ipc_wait_condition, &context,
                                timeout_ticks, out_reason);
    if (result != OK || *out_reason != WAIT_REASON_EVENT) return result;
    if (wait_channel_get_condition(&current->ipc_wait_channel,
                                   &generation) != OK) {
        LOG_ERROR("IPC", "Falha ao confirmar geracao da espera IPC");
        return ERR_STATE;
    }
    current->ipc_wait_generation = generation;
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
        process_t* process = &processes[i];

        if (process->state == PROCESS_STATE_UNUSED) continue;
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
