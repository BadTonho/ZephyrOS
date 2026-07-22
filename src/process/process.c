#include "process/process.h"
#include "core/memory.h"
#include "core/video.h"
#include "core/panic.h"
#include "core/timer.h"
#include "core/log.h"
#include "core/errors.h"
#include "core/string.h"

process_t processes[MAX_PROCESSES];
static process_t* current_process = 0;
uint32_t process_count = 0;
static uint32_t next_pid = 1;
static int last_scheduled_idx = -1;

extern page_directory_t* current_directory;

static uint32_t process_allocate_pid(void) {
    uint32_t candidate;

    for (uint32_t attempt = 0; attempt < MAX_PROCESSES; attempt++) {
        candidate = next_pid;
        if (candidate == 0) candidate = 1;
        next_pid = candidate + 1;
        if (next_pid == 0) next_pid = 1;

        int in_use = 0;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].state != PROCESS_STATE_UNUSED &&
                processes[i].pid == candidate) {
                in_use = 1;
                break;
            }
        }
        if (!in_use) return candidate;
    }

    LOG_ERROR("PROC", "Nao foi possivel reservar um PID unico");
    return 0;
}

void process_init(void) {
    current_process = 0;
    process_count = 0;
    next_pid = 1;
    last_scheduled_idx = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        kmemset(&processes[i], 0, sizeof(process_t));
        processes[i].state = PROCESS_STATE_UNUSED;
    }
    scheduler_init();
    LOG_INFO("PROC", "Gerenciador de processos inicializado");
}

void process_bootstrap_idle(void) {
    process_t* proc = &processes[0];
    kmemset(proc, 0, sizeof(process_t));
    proc->pid = 0;
    
    const char* name = "System Idle";
    int i = 0;
    while (name[i] && i < PROCESS_NAME_LENGTH - 1) {
        proc->name[i] = name[i];
        i++;
    }
    proc->name[i] = '\0';
    
    proc->state = PROCESS_STATE_RUNNING;
    proc->total_ticks = 0;
    proc->wait_ticks = 0;
    
    proc->kernel_stack = 0; // Utiliza a stack inicial do boot
    proc->page_directory = current_directory;
    
    current_process = proc;
    process_count = 1;
    LOG_INFO("PROC", "Processo Idle inicializado");
}

process_t* process_create(const char* name, void (*entry_point)()) {
    process_t* proc = 0;

    if (!name || !entry_point) {
        LOG_ERROR("PROC", "Parametros invalidos ao criar processo");
        return 0;
    }
    if (!current_directory) {
        LOG_ERROR("PROC", "Paging indisponivel ao criar processo");
        return 0;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_STATE_UNUSED) {
            proc = &processes[i];
            break;
        }
    }

    if (!proc) {
        LOG_ERROR("PROC", "Limite de processos atingido");
        return 0;
    }

    kmemset(proc, 0, sizeof(process_t));

    int i = 0;
    while (name[i] && i < PROCESS_NAME_LENGTH - 1) {
        proc->name[i] = name[i];
        i++;
    }
    proc->name[i] = '\0';

    proc->pid = process_allocate_pid();
    if (!proc->pid) {
        LOG_ERROR("PROC", "Falha ao reservar PID do processo");
        kmemset(proc, 0, sizeof(process_t));
        proc->state = PROCESS_STATE_UNUSED;
        return 0;
    }
    /* O processo permanece invisivel ao scheduler ate o contexto estar pronto. */
    proc->total_ticks = 0;
    proc->wait_ticks = 0;

    proc->kernel_stack = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
    if (!proc->kernel_stack) {
        LOG_ERROR("PROC", "Falha ao alocar stack do processo");
        proc->state = PROCESS_STATE_UNUSED;
        return 0;
    }
    proc->kernel_stack_top = proc->kernel_stack + KERNEL_STACK_SIZE;

    proc->page_directory = paging_create_directory();
    if (!proc->page_directory) {
        LOG_ERROR("PROC", "Falha ao criar diretorio do processo");
        kfree((void*)proc->kernel_stack);
        proc->kernel_stack = 0;
        kmemset(proc, 0, sizeof(process_t));
        proc->state = PROCESS_STATE_UNUSED;
        return 0;
    }

    for (uint32_t addr = 0xB8000; addr < 0xC0000; addr += 0x1000) {
        page_entry_t* page = paging_get_page(addr, 1);
        if (!page) {
            LOG_ERROR("PROC", "Falha ao mapear VGA para o processo");
            paging_free_directory(proc->page_directory);
            proc->page_directory = 0;
            kfree((void*)proc->kernel_stack);
            proc->kernel_stack = 0;
            kmemset(proc, 0, sizeof(process_t));
            proc->state = PROCESS_STATE_UNUSED;
            return 0;
        }
        page->frame = addr / 0x1000;
        page->present = 1;
        page->rw = 1;
        page->user = 0;
    }

    uint32_t stack = proc->kernel_stack_top;
    uint32_t* stack_ptr = (uint32_t*)stack;

    stack_ptr--;
    *stack_ptr = 0x202;
    stack_ptr--;
    *stack_ptr = 0x08;
    stack_ptr--;
    *stack_ptr = (uint32_t)entry_point;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0x10;
    stack_ptr--;
    *stack_ptr = stack;
    stack_ptr--;
    *stack_ptr = 0x10;
    stack_ptr--;
    *stack_ptr = 0x10;
    stack_ptr--;
    *stack_ptr = 0x10;
    stack_ptr--;
    *stack_ptr = 0x10;

    proc->context.esp = (uint32_t)stack_ptr;
    proc->context.eip = (uint32_t)entry_point;
    proc->context.eflags = 0x202;
    proc->context.cs = 0x08;
    proc->context.ds = 0x10;
    proc->context.es = 0x10;
    proc->context.fs = 0x10;
    proc->context.gs = 0x10;
    proc->context.ss = 0x10;
    proc->context.cr3 = (uint32_t)proc->page_directory;

    proc->state = PROCESS_STATE_READY;
    process_count++;
    LOG_INFO("PROC", "Processo criado com sucesso");
    return proc;
}

static int process_pointer_valid(const process_t* proc) {
    uint32_t address;
    uint32_t start;
    uint32_t end;

    if (!proc) return 0;
    address = (uint32_t)proc;
    start = (uint32_t)&processes[0];
    end = start + sizeof(processes);
    return address >= start && address < end &&
           ((address - start) % sizeof(process_t)) == 0;
}

void process_destroy(process_t* proc) {
    if (!process_pointer_valid(proc)) {
        LOG_ERROR("PROC", "Ponteiro invalido ao destruir processo");
        return;
    }
    if (proc->state == PROCESS_STATE_UNUSED) {
        LOG_WARN("PROC", "Processo ja esta inutilizado");
        return;
    }
    if (proc->state < PROCESS_STATE_UNUSED ||
        proc->state > PROCESS_STATE_ZOMBIE) {
        LOG_ERROR("PROC", "Estado invalido ao destruir processo");
        return;
    }
    if (proc == current_process || proc->pid == 0) {
        LOG_WARN("PROC", "Destruicao do processo atual ou Idle bloqueada");
        return;
    }
    if (proc->page_directory && proc->page_directory == current_directory) {
        LOG_WARN("PROC", "Diretorio ativo impede destruicao do processo");
        return;
    }

    if (process_get_focus() == proc->pid) {
        process_set_focus(0);
    }
    proc->state = PROCESS_STATE_UNUSED;
    if (proc->kernel_stack) {
        kfree((void*)proc->kernel_stack);
        proc->kernel_stack = 0;
    }
    if (proc->page_directory) {
        paging_free_directory(proc->page_directory);
    }
    if (process_count > 0) process_count--;
    kmemset(proc, 0, sizeof(process_t));
    proc->state = PROCESS_STATE_UNUSED;
    LOG_INFO("PROC", "Processo destruido");
}

process_t* process_get_current(void) {
    return current_process;
}

uint32_t process_get_count(void) {
    return process_count;
}

process_t* process_get_by_pid(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state != PROCESS_STATE_UNUSED &&
            processes[i].pid == pid) {
            return &processes[i];
        }
    }

    LOG_DEBUG("PROC", "PID nao encontrado");
    return 0;
}

uint32_t process_get_current_pid(void) {
    if (!current_process) {
        LOG_WARN("PROC", "Processo atual indisponivel");
        return 0;
    }
    return current_process->pid;
}

uint32_t process_get_state_count(process_state_t state) {
    uint32_t count = 0;

    if (state < PROCESS_STATE_UNUSED || state > PROCESS_STATE_ZOMBIE) {
        LOG_ERROR("PROC", "Estado invalido ao contar processos");
        return 0;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == state) count++;
    }
    return count;
}

process_t* scheduler_schedule(void) {
    process_t* best = 0;

    for (int i = 1; i <= MAX_PROCESSES; i++) {
        int idx = (last_scheduled_idx + i) % MAX_PROCESSES;
        if (processes[idx].state == PROCESS_STATE_READY) {
            best = &processes[idx];
            last_scheduled_idx = idx;
            break;
        }
    }

    if (!best && processes[0].state != PROCESS_STATE_UNUSED &&
        processes[0].state != PROCESS_STATE_ZOMBIE) {
        if (processes[0].state != PROCESS_STATE_RUNNING) {
            processes[0].state = PROCESS_STATE_READY;
        }
        return &processes[0];
    }

    if (!best) LOG_WARN("PROC", "Nenhum processo pronto para escalonar");
    return best;
}

void process_yield(void) {
    /* O timer pode disparar antes do bootstrap do Idle; isso nao e falha. */
    if (!current_process) {
        return;
    }
    if (current_process->state < PROCESS_STATE_READY ||
        current_process->state > PROCESS_STATE_BLOCKED) {
        return;
    }
    
    process_t* next = scheduler_schedule();
    if (!next) return;
    if (next != current_process) {
        process_t* prev = current_process;
        current_process = next;
        current_process->state = PROCESS_STATE_RUNNING;

        if (prev && prev->state == PROCESS_STATE_RUNNING) {
            prev->state = PROCESS_STATE_READY;
        }

        process_context_switch(&prev->context, &next->context);
    }
}

void process_block(uint32_t ticks) {
    if (!current_process) {
        LOG_ERROR("PROC", "Tentativa de bloquear sem processo atual");
        return;
    }
    if (current_process->pid == 0 ||
        current_process->state != PROCESS_STATE_RUNNING) {
        LOG_WARN("PROC", "Estado invalido para bloquear processo");
        return;
    }
    if (ticks == 0) {
        LOG_WARN("PROC", "Bloqueio solicitado com duracao zero");
        return;
    }
    current_process->state = PROCESS_STATE_BLOCKED;
    current_process->wait_ticks = ticks;
    process_yield();
}

void process_unblock(process_t* proc) {
    if (!process_pointer_valid(proc)) {
        LOG_ERROR("PROC", "Ponteiro invalido ao desbloquear processo");
        return;
    }
    if (proc->state < PROCESS_STATE_UNUSED ||
        proc->state > PROCESS_STATE_ZOMBIE) {
        LOG_ERROR("PROC", "Estado invalido ao desbloquear processo");
        return;
    }
    if (proc->state == PROCESS_STATE_BLOCKED) {
        proc->state = PROCESS_STATE_READY;
        LOG_DEBUG("PROC", "Processo desbloqueado");
    } else {
        LOG_DEBUG("PROC", "Processo nao estava bloqueado");
    }
}

void scheduler_init(void) {
    process_count = 0;
    last_scheduled_idx = -1;
    LOG_INFO("PROC", "Scheduler round-robin inicializado");
}

void scheduler_tick(void) {
    if (!current_process) return;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_STATE_BLOCKED) {
            if (processes[i].wait_ticks > 0) {
                processes[i].wait_ticks--;
                if (processes[i].wait_ticks == 0) {
                    processes[i].state = PROCESS_STATE_READY;
                }
            }
        }
    }

    current_process->total_ticks++;
}
