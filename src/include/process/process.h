#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"
#include "memory/paging.h"

#define MAX_PROCESSES 64
#define KERNEL_STACK_SIZE 4096
#define PROCESS_NAME_LENGTH 32

typedef enum {
    PROCESS_STATE_UNUSED = 0,
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_BLOCKED,
    PROCESS_STATE_ZOMBIE
} process_state_t;

typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip, eflags;
    uint32_t cs, ss, ds, es, fs, gs;
    uint32_t cr3;
} __attribute__((packed)) process_context_t;

typedef struct {
    uint32_t pid;
    char name[PROCESS_NAME_LENGTH];
    process_state_t state;
    process_context_t context;
    page_directory_t* page_directory;
    uint32_t kernel_stack;
    uint32_t kernel_stack_top;
    uint32_t* kernel_stack_ptr;
    uint32_t wait_ticks;
    uint32_t total_ticks;
    uint32_t next_pid;
} process_t;

void process_init(void);
process_t* process_create(const char* name, void (*entry_point)());
void process_destroy(process_t* proc);
process_t* process_get_current(void);
uint32_t process_get_count(void);

void process_yield(void);
void process_block(uint32_t ticks);
void process_unblock(process_t* proc);

process_t* scheduler_schedule(void);
void scheduler_init(void);
void scheduler_tick(void);

extern void process_context_switch(process_context_t* prev, process_context_t* next);

extern process_t processes[MAX_PROCESSES];
extern uint32_t process_count;

#endif
