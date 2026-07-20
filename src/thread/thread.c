#include "process/thread.h"
#include "core/memory.h"
#include "core/timer.h"
#include "core/log.h"
#include "core/string.h"

static thread_t threads[MAX_THREADS];
static thread_t* current_thread = 0;
static uint32_t thread_count = 0;
static uint32_t next_thread_id = 1;

void thread_init(void) {
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i].state = THREAD_UNUSED;
    }
    thread_count = 0;
}

thread_t* thread_create(const char* name, void (*entry)(void)) {
    thread_t* thread = 0;

    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].state == THREAD_UNUSED) {
            thread = &threads[i];
            break;
        }
    }

    if (!thread || !name || !entry) {
        LOG_ERROR("THRD", "Parametros invalidos ao criar thread");
        return 0;
    }

    kmemset(thread, 0, sizeof(thread_t));

    int i = 0;
    while (name[i] && i < 31) {
        thread->name[i] = name[i];
        i++;
    }
    thread->name[i] = '\0';

    thread->id = next_thread_id++;
    thread->state = THREAD_RUNNING;
    thread->entry = entry;
    thread->wait_ticks = 0;

    thread->stack = (uint32_t*)kmalloc(THREAD_STACK_SIZE);
    if (!thread->stack) {
        LOG_ERROR("THRD", "Falha ao alocar stack da thread");
        kmemset(thread, 0, sizeof(thread_t));
        thread->state = THREAD_UNUSED;
        return 0;
    }

    uint32_t stack_top = (uint32_t)thread->stack + THREAD_STACK_SIZE;
    uint32_t* stack_ptr = (uint32_t*)stack_top;

    stack_ptr--;
    *stack_ptr = 0x202;
    stack_ptr--;
    *stack_ptr = 0x08;
    stack_ptr--;
    *stack_ptr = (uint32_t)entry;
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
    *stack_ptr = stack_top;
    stack_ptr--;
    *stack_ptr = 0x10;
    stack_ptr--;
    *stack_ptr = 0x10;
    stack_ptr--;
    *stack_ptr = 0x10;
    stack_ptr--;
    *stack_ptr = 0x10;

    thread->esp = (uint32_t)stack_ptr;
    thread->eip = (uint32_t)entry;

    thread_count++;
    return thread;
}

void thread_destroy(thread_t* thread) {
    if (!thread || thread->state == THREAD_UNUSED) return;
    thread->state = THREAD_UNUSED;
    if (thread->stack) {
        kfree(thread->stack);
        thread->stack = 0;
    }
    if (thread_count > 0) thread_count--;
    thread->stack = 0;
}

thread_t* thread_schedule_next(void) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].state == THREAD_RUNNING && &threads[i] != current_thread) {
            return &threads[i];
        }
    }
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].state == THREAD_RUNNING) {
            return &threads[i];
        }
    }
    return current_thread;
}

void thread_yield(void) {
    thread_t* next = thread_schedule_next();
    if (next && next != current_thread) {
        thread_t* prev = current_thread;
        current_thread = next;
        current_thread->state = THREAD_RUNNING;

        if (prev && prev->state == THREAD_RUNNING) {
            prev->state = THREAD_RUNNING;
        }
    }
}

void thread_block(uint32_t ticks) {
    if (!current_thread) return;
    current_thread->state = THREAD_BLOCKED;
    current_thread->wait_ticks = ticks;
    thread_yield();
}

void thread_block_indefinite(void) {
    if (!current_thread) return;
    current_thread->state = THREAD_BLOCKED;
    current_thread->wait_ticks = 0;
    thread_yield();
}

void thread_unblock(thread_t* thread) {
    if (thread && thread->state == THREAD_BLOCKED) {
        thread->state = THREAD_RUNNING;
        thread->wait_ticks = 0;
    }
}

thread_t* thread_get_current(void) {
    return current_thread;
}

thread_t* thread_get_by_id(uint32_t id) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].id == id && threads[i].state != THREAD_UNUSED) {
            return &threads[i];
        }
    }
    return 0;
}

uint32_t thread_get_count(void) {
    return thread_count;
}

void thread_scheduler_tick(void) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].state == THREAD_BLOCKED) {
            if (threads[i].wait_ticks > 0) {
                threads[i].wait_ticks--;
                if (threads[i].wait_ticks == 0) {
                    threads[i].state = THREAD_RUNNING;
                }
            }
        }
    }
}
