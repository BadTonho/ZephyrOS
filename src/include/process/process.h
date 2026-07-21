#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"
#include "memory/paging.h"

#define MAX_PROCESSES 64
#define KERNEL_STACK_SIZE 4096
#define PROCESS_NAME_LENGTH 32

#define IPC_MSG_QUEUE_SIZE 32

typedef enum {
    IPC_MSG_NONE = 0,
    IPC_MSG_KEYBOARD,
    IPC_MSG_APP_REQUEST
} ipc_msg_type_t;

typedef enum {
    IPC_APP_OPEN_SHELL = 1,
    IPC_APP_OPEN_EXPLORER,
    IPC_APP_OPEN_TASKMANAGER,
    IPC_APP_OPEN_DESKTOP,
    IPC_APP_OPEN_SETTINGS,
    IPC_APP_OPEN_TASKMANAGER_GUI
} ipc_app_request_t;

typedef struct {
    ipc_msg_type_t type;
    uint32_t data1;
    uint32_t data2;
} ipc_msg_t;

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
    ipc_msg_t msg_queue[IPC_MSG_QUEUE_SIZE];
    uint32_t msg_head;
    uint32_t msg_tail;
} process_t;

void process_init(void);
void process_bootstrap_idle(void);
process_t* process_create(const char* name, void (*entry_point)());
void process_destroy(process_t* proc);
process_t* process_get_current(void);
uint32_t process_get_count(void);

void process_yield(void);
void process_block(uint32_t ticks);
void process_unblock(process_t* proc);
void ipc_init(void);

process_t* scheduler_schedule(void);
void scheduler_init(void);
void scheduler_tick(void);


int ipc_send(uint32_t pid, ipc_msg_t* msg);
int ipc_receive(ipc_msg_t* msg);
void process_set_focus(uint32_t pid);
uint32_t process_get_focus(void);

extern void process_context_switch(process_context_t* prev, process_context_t* next);

extern process_t processes[MAX_PROCESSES];
extern uint32_t process_count;

#endif
