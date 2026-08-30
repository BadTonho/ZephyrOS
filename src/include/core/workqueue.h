#ifndef WORKQUEUE_H
#define WORKQUEUE_H

#include "types.h"

#define WORKQUEUE_CAPACITY 64U
#define WORKQUEUE_OWNER_SIZE 24U
#define WORKQUEUE_HIGH_BUDGET 8U
#define WORKQUEUE_NORMAL_BUDGET 4U
#define WORKQUEUE_MAX_DELAY_TICKS 0x7FFFFFFFU
#define WORKQUEUE_ID_SLOT_BITS 8U
#define WORKQUEUE_ID_SLOT_MASK 0xFFU
#define WORKQUEUE_ID_GENERATION_MAX 0x00FFFFFFU

typedef enum {
    WORK_PRIORITY_HIGH = 0,
    WORK_PRIORITY_NORMAL,
    WORK_PRIORITY_COUNT
} work_priority_t;

typedef enum {
    WORK_STATE_IDLE = 0,
    WORK_STATE_READY,
    WORK_STATE_DELAYED,
    WORK_STATE_RUNNING
} work_state_t;

typedef enum {
    WORK_CONTEXT_NONE = 0,
    WORK_CONTEXT_KWORKER,
    WORK_CONTEXT_SYSTEM_FALLBACK
} work_context_t;

typedef int (*work_func_t)(void* context);

typedef struct work_struct work_struct_t;

struct work_struct {
    work_struct_t* previous;
    work_struct_t* next;
    work_func_t callback;
    void* context;
    char owner[WORKQUEUE_OWNER_SIZE];
    uint32_t id;
    uint32_t deadline_tick;
    uint32_t rerun_deadline_tick;
    uint32_t scheduled;
    uint32_t executed;
    uint32_t coalesced;
    uint32_t reruns;
    uint32_t cancellations;
    uint32_t callback_ticks;
    uint32_t max_callback_ticks;
    int last_error;
    work_priority_t priority;
    work_state_t state;
    uint8_t registry_slot;
    uint8_t initialized;
    uint8_t cancel_requested;
    uint8_t rerun_requested;
    uint8_t rerun_delayed;
};

typedef struct {
    uint8_t initialized;
    uint8_t worker_bound;
    uint8_t worker_active;
    uint8_t fallback_active;
    uint32_t worker_pid;
    work_context_t execution_context;
    uint32_t capacity;
    uint32_t registered;
    uint32_t ready_high;
    uint32_t ready_normal;
    uint32_t delayed;
    uint32_t running;
    uint32_t scheduled;
    uint32_t executed;
    uint32_t coalesced;
    uint32_t reruns;
    uint32_t cancelled;
    uint32_t rejected;
    uint32_t callback_errors;
    uint32_t context_errors;
    uint32_t invariant_errors;
    uint32_t wakeups;
    uint32_t wake_errors;
    uint32_t sleeps;
    uint32_t peak_pending;
    uint32_t total_callback_ticks;
    uint32_t max_callback_ticks;
    int last_error;
} workqueue_stats_t;

typedef struct {
    uint32_t id;
    uint32_t generation;
    char owner[WORKQUEUE_OWNER_SIZE];
    work_priority_t priority;
    work_state_t state;
    uint32_t deadline_tick;
    uint32_t remaining_ticks;
    uint32_t scheduled;
    uint32_t executed;
    uint32_t coalesced;
    uint32_t reruns;
    uint32_t cancellations;
    uint32_t callback_ticks;
    uint32_t max_callback_ticks;
    int last_error;
} work_info_t;

typedef struct {
    uint8_t lifecycle;
    uint8_t fifo;
    uint8_t priority;
    uint8_t delayed;
    uint8_t rollover;
    uint8_t promotion;
    uint8_t coalescing;
    uint8_t rerun;
    uint8_t cancellation;
    uint8_t capacity;
    uint8_t interrupt_context;
    uint8_t invariants;
    uint32_t passed;
    uint32_t failed;
} workqueue_self_test_result_t;

int workqueue_init(void);
int work_init(work_struct_t* work, const char* owner,
              work_priority_t priority,
              work_func_t callback, void* context);
int work_destroy(work_struct_t* work);
int schedule_work(work_struct_t* work);
int schedule_delayed_work(work_struct_t* work, uint32_t delay_ticks);
int cancel_work(work_struct_t* work);
int workqueue_dispatch(uint32_t high_budget, uint32_t normal_budget,
                       uint32_t* out_executed);
int workqueue_bind_worker(uint32_t pid);
int workqueue_set_fallback(uint8_t active);
int workqueue_needs_fallback(uint8_t* out_required);
void workqueue_worker_main(void);
int workqueue_copy_info(work_info_t* output,
                        uint32_t max_entries, uint32_t* out_count);
int workqueue_get_stats(workqueue_stats_t* out_stats);
int workqueue_validate_state(void);
int workqueue_self_test(workqueue_self_test_result_t* out_result);
int workqueue_probe_worker(uint32_t timeout_ticks);
int workqueue_power_quiesce_until(uint32_t deadline_tick);
int workqueue_power_set_quiescing(uint8_t active);
const char* workqueue_priority_name(work_priority_t priority);
const char* workqueue_state_name(work_state_t state);
const char* workqueue_context_name(work_context_t context);

#endif
