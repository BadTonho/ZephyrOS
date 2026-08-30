#ifndef THREAD_H
#define THREAD_H

#include "types.h"
#include "core/memory.h"
#include "core/wait.h"

#define MAX_THREADS 32
#define THREAD_STACK_PAGES 4U
#define THREAD_STACK_SIZE (THREAD_STACK_PAGES * PAGE_SIZE)
#define THREAD_NAME_LENGTH 32

typedef enum {
    THREAD_UNUSED = 0,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_FINISHED
} thread_state_t;

typedef struct {
    uint32_t id;
    char name[THREAD_NAME_LENGTH];
    thread_state_t state;
    uint32_t* stack;
    uint32_t esp;
    uint32_t eip;
    void (*entry)(void);
    uint32_t wait_ticks;
    wait_queue_entry_t wait_entry;
    wait_channel_t* wait_channel;
    uint32_t wait_condition;
    uint32_t wait_deadline;
    wait_reason_t wait_reason;
    uint8_t wait_deadline_active;
    uint8_t wait_active;
    uint32_t owner_pid;
} thread_t;

void thread_init(void);
int thread_is_ready(void);
thread_t* thread_create(const char* name, void (*entry)(void));
void thread_destroy(thread_t* thread);
void thread_yield(void);
void thread_block(uint32_t ticks);
void thread_block_indefinite(void);
void thread_unblock(thread_t* thread);
int thread_wait(wait_channel_t* channel, uint32_t observed_condition,
                uint32_t timeout_ticks, wait_reason_t* out_reason);
int thread_wake_channel(wait_channel_t* channel, wait_wake_mode_t mode,
                        wait_reason_t reason, uint32_t* out_woken);
int thread_cancel_wait(thread_t* thread);
int thread_copy_waiters(wait_info_t* output, uint32_t max_entries,
                        uint32_t* out_count);
thread_t* thread_get_current(void);
thread_t* thread_get_by_id(uint32_t id);
uint32_t thread_get_count(void);
void thread_scheduler_tick(void);
thread_t* thread_schedule_next(void);
int thread_run_self_test(void);
extern void thread_context_switch(uint32_t* previous_esp, uint32_t next_esp);

#endif
