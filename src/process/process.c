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

extern page_directory_t* current_directory;

void process_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        processes[i].state = PROCESS_STATE_UNUSED;
    }
    scheduler_init();
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
}

process_t* process_create(const char* name, void (*entry_point)()) {
    process_t* proc = 0;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_STATE_UNUSED) {
            proc = &processes[i];
            break;
        }
    }

    if (!proc || !name || !entry_point) {
        LOG_ERROR("PROC", "Parametros invalidos ao criar processo");
        return 0;
    }

    kmemset(proc, 0, sizeof(process_t));

    int i = 0;
    while (name[i] && i < PROCESS_NAME_LENGTH - 1) {
        proc->name[i] = name[i];
        i++;
    }
    proc->name[i] = '\0';

    proc->pid = next_pid++;
    proc->state = PROCESS_STATE_READY;
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
        if (page) {
            page->frame = addr / 0x1000;
            page->present = 1;
            page->rw = 1;
            page->user = 0;
        }
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

    process_count++;
    return proc;
}

void process_destroy(process_t* proc) {
    if (!proc || proc->state == PROCESS_STATE_UNUSED) return;
    proc->state = PROCESS_STATE_UNUSED;
    if (proc->kernel_stack) {
        kfree((void*)proc->kernel_stack);
        proc->kernel_stack = 0;
    }
    if (proc->page_directory) {
        paging_free_directory(proc->page_directory);
    }
    if (process_count > 0) process_count--;
    proc->kernel_stack = 0;
    proc->page_directory = 0;
}

process_t* process_get_current(void) {
    return current_process;
}

uint32_t process_get_count(void) {
    return process_count;
}

process_t* scheduler_schedule(void) {
    process_t* best = 0;
    uint32_t min_ticks = 0xFFFFFFFF;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_STATE_READY) {
            if (processes[i].total_ticks < min_ticks) {
                min_ticks = processes[i].total_ticks;
                best = &processes[i];
            }
        }
    }

    if (!best) {
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].state == PROCESS_STATE_READY) {
                best = &processes[i];
                break;
            }
        }
    }

    if (best) {
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].state == PROCESS_STATE_READY) {
                processes[i].total_ticks = 0;
            }
        }
        best->total_ticks = 1;
    }

    return best;
}

void process_yield(void) {
    if (!current_process) return;
    
    process_t* next = scheduler_schedule();
    if (next && next != current_process) {
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
    if (!current_process) return;
    current_process->state = PROCESS_STATE_BLOCKED;
    current_process->wait_ticks = ticks;
    process_yield();
}

void process_unblock(process_t* proc) {
    if (!proc) return;
    if (proc->state == PROCESS_STATE_BLOCKED) {
        proc->state = PROCESS_STATE_READY;
    }
}

void scheduler_init(void) {
    process_count = 0;
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
