#ifndef THREAD_H
#define THREAD_H

#include "types.h"

#define MAX_THREADS 32
#define THREAD_STACK_SIZE 4096

typedef enum {
    THREAD_UNUSED = 0,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_FINISHED
} thread_state_t;

typedef struct {
    uint32_t id;
    char name[32];
    thread_state_t state;
    uint32_t* stack;
    uint32_t esp;
    uint32_t eip;
    void (*entry)(void);
    uint32_t wait_ticks;
} thread_t;

void thread_init(void);
thread_t* thread_create(const char* name, void (*entry)(void));
void thread_destroy(thread_t* thread);
void thread_yield(void);
void thread_block(uint32_t ticks);
thread_t* thread_get_current(void);
thread_t* thread_get_by_id(uint32_t id);
uint32_t thread_get_count(void);
void thread_scheduler_tick(void);
thread_t* thread_schedule_next(void);

#endif
