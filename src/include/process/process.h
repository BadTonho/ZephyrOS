#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"
#include "core/app_api.h"
#include "core/wait.h"
#include "memory/paging.h"
#include "drivers/idt.h"

#define MAX_PROCESSES 64
#define KERNEL_STACK_SIZE 4096
#define PROCESS_NAME_LENGTH 32
#define PROCESS_EXIT_CANCELLED APP_EXIT_CANCELLED

#define IPC_MSG_QUEUE_SIZE 32
#define SCHEDULER_USER_QUANTUM_TICKS 1U

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
    IPC_APP_OPEN_TASKMANAGER_GUI,
    IPC_APP_OPEN_UPDATER,
    IPC_APP_OPEN_APP_STORE
} ipc_app_request_t;

typedef struct {
    ipc_msg_type_t type;
    uint32_t data1;
    uint32_t data2;
} ipc_msg_t;

typedef struct {
    uint32_t sent;
    uint32_t received;
    uint32_t failed;
    uint32_t queue_full;
} ipc_stats_t;

typedef struct {
    uint32_t context_switches;
    uint32_t cooperative_yields;
    uint32_t user_preemptions;
    uint32_t idle_fallbacks;
    uint32_t user_quantum_ticks;
} scheduler_stats_t;

typedef struct {
    uint32_t current_valid;
    uint32_t idle_valid;
    uint32_t pid_table_valid;
    uint32_t state_table_valid;
} scheduler_validation_t;

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
    uint32_t user_entry;
    uint32_t user_mode;
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
    uint32_t exit_code;
    uint32_t fault_vector;
    uint32_t fault_error;
    uint32_t fault_address;
    uint32_t faulted;
    uint32_t user_test;
    ipc_msg_t msg_queue[IPC_MSG_QUEUE_SIZE];
    uint32_t msg_head;
    uint32_t msg_tail;
    wait_channel_t ipc_wait_channel;
    wait_channel_t* wait_channel;
    uint32_t wait_condition;
    uint32_t wait_deadline;
    wait_reason_t wait_reason;
    uint8_t wait_deadline_active;
    uint8_t wait_active;
} process_t;

typedef struct {
    uint32_t pid;
    uint32_t vector;
    uint32_t error;
} process_user_fault_summary_t;

void process_init(void);
void process_bootstrap_idle(void);
process_t* process_create(const char* name, void (*entry_point)());
int process_create_user_image(const char* name, const uint8_t* code,
                              uint32_t code_size, const uint8_t* data,
                              uint32_t data_size, uint32_t entry_offset,
                              uint32_t stack_size, int diagnostic_test,
                              uint32_t* pid_out);
int process_create_user_image_suspended(const char* name,
                                        const uint8_t* code,
                                        uint32_t code_size,
                                        const uint8_t* data,
                                        uint32_t data_size,
                                        uint32_t entry_offset,
                                        uint32_t stack_size,
                                        uint32_t* pid_out);
int process_create_user_image_suspended_with_launch(
    const char* name, const uint8_t* code, uint32_t code_size,
    const uint8_t* data, uint32_t data_size, uint32_t entry_offset,
    uint32_t stack_size, const app_launch_info_t* launch,
    uint32_t* pid_out);
int process_create_user_test(int trigger_fault, uint32_t* pid_out);
int process_cancel_user_test(uint32_t pid, uint32_t exit_code);
int process_start_user(uint32_t pid);
void process_destroy(process_t* proc);
int process_reap_finished_user(void);
process_t* process_get_current(void);
uint32_t process_get_count(void);
process_t* process_get_by_pid(uint32_t pid);
uint32_t process_get_current_pid(void);
uint32_t process_get_event_generation(void);
uint32_t process_get_state_count(process_state_t state);
uint32_t process_get_user_count(void);
uint32_t process_get_user_fault_count(void);
int process_get_last_user_fault(process_user_fault_summary_t* summary);
int process_is_user(const process_t* proc);
int process_exit_current(uint32_t exit_code);
int process_handle_user_exception(registers_t* regs);
int process_prepare_user_termination(registers_t* regs);
void process_finish_user_termination(void);
int process_take_user_test_result(uint32_t* pid, uint32_t* faulted);

void process_yield(void);
void process_block(uint32_t ticks);
void process_unblock(process_t* proc);
int process_wait(wait_channel_t* channel, uint32_t observed_condition,
                 uint32_t timeout_ticks, wait_reason_t* out_reason);
int process_wake_channel(wait_channel_t* channel, wait_wake_mode_t mode,
                         wait_reason_t reason, uint32_t* out_woken);
int process_cancel_wait(process_t* proc);
int process_copy_waiters(wait_info_t* output, uint32_t max_entries,
                         uint32_t* out_count);
void ipc_init(void);
int ipc_is_ready(void);

process_t* scheduler_schedule(void);
void scheduler_init(void);
void scheduler_tick(void);
void scheduler_preempt_user(void);
void scheduler_get_stats(scheduler_stats_t* stats);
int scheduler_validate_invariants(scheduler_validation_t* validation);


int ipc_send(uint32_t pid, ipc_msg_t* msg);
int ipc_receive(ipc_msg_t* msg);
int ipc_wait(uint32_t timeout_ticks, wait_reason_t* out_reason);
void ipc_get_stats(ipc_stats_t* stats);
uint32_t ipc_get_pending_count(void);
int process_set_focus(uint32_t pid);
int process_set_focus_fallback(uint32_t pid);
int process_restore_focus(void);
int process_cancel_user(uint32_t pid, uint32_t exit_code);
int process_cancel_focused_user(uint32_t exit_code);
uint32_t process_get_focus(void);

extern void process_context_switch(process_context_t* prev, process_context_t* next);
extern void process_user_enter(void);
extern void process_user_termination_enter(void);

extern process_t processes[MAX_PROCESSES];
extern uint32_t process_count;

#endif
