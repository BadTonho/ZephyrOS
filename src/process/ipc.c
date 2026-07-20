#include "process/process.h"
#include "core/spinlock.h"
#include "core/log.h"
#include "core/string.h"

static spinlock_t ipc_lock;
static uint32_t focused_pid = 0;

void ipc_init(void) {
    spinlock_init(&ipc_lock);
    LOG_INFO("IPC", "Inter-Process Communication inicializado");
}

int ipc_send(uint32_t pid, ipc_msg_t* msg) {
    if (pid >= MAX_PROCESSES) return 0;
    
    spinlock_acquire(&ipc_lock);
    
    process_t* target = &processes[pid];
    if (target->state == PROCESS_STATE_UNUSED || target->state == PROCESS_STATE_ZOMBIE) {
        spinlock_release(&ipc_lock);
        return 0;
    }
    
    uint32_t next_head = (target->msg_head + 1) % IPC_MSG_QUEUE_SIZE;
    if (next_head == target->msg_tail) {
        // Fila cheia
        spinlock_release(&ipc_lock);
        return 0;
    }
    
    target->msg_queue[target->msg_head] = *msg;
    target->msg_head = next_head;
    
    // Se o processo estava bloqueado esperando input, podemos acorda-lo
    // (A implementar no futuro)
    
    spinlock_release(&ipc_lock);
    return 1;
}

int ipc_receive(ipc_msg_t* msg) {
    process_t* current = process_get_current();
    if (!current) return 0;
    
    spinlock_acquire(&ipc_lock);
    
    if (current->msg_head == current->msg_tail) {
        // Fila vazia
        spinlock_release(&ipc_lock);
        return 0;
    }
    
    *msg = current->msg_queue[current->msg_tail];
    current->msg_tail = (current->msg_tail + 1) % IPC_MSG_QUEUE_SIZE;
    
    spinlock_release(&ipc_lock);
    return 1;
}

void process_set_focus(uint32_t pid) {
    if (pid < MAX_PROCESSES) {
        focused_pid = pid;
        LOG_INFO("IPC", "Foco alterado");
    }
}

uint32_t process_get_focus(void) {
    return focused_pid;
}
