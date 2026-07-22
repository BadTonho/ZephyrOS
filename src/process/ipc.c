#include "process/process.h"
#include "core/spinlock.h"
#include "core/log.h"
#include "core/string.h"

static spinlock_t ipc_lock;
static uint32_t focused_pid = 0;
static ipc_stats_t ipc_stats;
static int ipc_ready = 0;

void ipc_init(void) {
    spinlock_init(&ipc_lock);
    focused_pid = 0;
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
    int wake_target = 0;

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
    if (target->state == PROCESS_STATE_BLOCKED) {
        wake_target = 1;
    }

    spinlock_release(&ipc_lock);
    if (wake_target) process_unblock(target);
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

void ipc_get_stats(ipc_stats_t* stats) {
    if (!stats) {
        LOG_ERROR("IPC", "Buffer de estatisticas nulo");
        return;
    }

    spinlock_acquire(&ipc_lock);
    *stats = ipc_stats;
    spinlock_release(&ipc_lock);
}

void process_set_focus(uint32_t pid) {
    process_t* target = process_get_by_pid(pid);

    if (!target || (target->state != PROCESS_STATE_READY &&
                    target->state != PROCESS_STATE_RUNNING &&
                    target->state != PROCESS_STATE_BLOCKED)) {
        LOG_WARN("IPC", "Foco rejeitado para PID inexistente ou inativo");
        return;
    }

    focused_pid = target->pid;
    LOG_INFO("IPC", "Foco alterado");
}

uint32_t process_get_focus(void) {
    return focused_pid;
}
