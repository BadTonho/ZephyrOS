#ifndef TIMER_H
#define TIMER_H

#include "types.h"
#include "drivers/idt.h"

#define TIMER_CAPACITY 32U
#define TIMER_OWNER_CAPACITY 16U
#define TIMER_NAME_CAPACITY 16U
#define TIMER_DISPATCH_BUDGET 8U
#define TIMER_MAX_INTERVAL_TICKS 0x7FFFFFFFU

typedef uint32_t timer_handle_t;
typedef uint32_t timer_owner_handle_t;

typedef enum {
    TIMER_STATE_IDLE = 0,
    TIMER_STATE_ARMED,
    TIMER_STATE_PENDING
} timer_state_t;

typedef enum {
    TIMER_MODE_ONE_SHOT = 0,
    TIMER_MODE_PERIODIC
} timer_mode_t;

typedef int (*timer_callback_fn)(timer_handle_t handle, void* context);

typedef struct {
    timer_handle_t handle;
    timer_owner_handle_t owner;
    char owner_name[TIMER_NAME_CAPACITY];
    char name[TIMER_NAME_CAPACITY];
    timer_mode_t mode;
    timer_state_t state;
    uint32_t deadline_tick;
    uint32_t period_ticks;
    uint32_t executions;
    uint32_t delayed_callbacks;
    uint32_t missed_periods;
    uint32_t last_lateness_ticks;
    int last_error;
} timer_info_t;

typedef struct {
    uint8_t initialized;
    uint32_t current_tick;
    uint32_t frequency;
    uint32_t occupancy;
    uint32_t capacity;
    uint32_t owner_occupancy;
    uint32_t owner_capacity;
    uint32_t armed;
    uint32_t pending;
    uint32_t high_watermark;
    uint32_t owners_created;
    uint32_t owners_destroyed;
    uint32_t timers_created;
    uint32_t timers_started;
    uint32_t expirations;
    uint32_t cancellations;
    uint32_t timers_destroyed;
    uint32_t callbacks;
    uint32_t callback_errors;
    uint32_t delayed_callbacks;
    uint32_t missed_periods;
    uint32_t invalid_operations;
} timer_stats_t;

typedef struct {
    uint8_t conversion_and_limits;
    uint8_t one_shot;
    uint8_t periodic_no_drift;
    uint8_t periodic_coalescing;
    uint8_t cancel_armed;
    uint8_t cancel_pending;
    uint8_t owner_destruction;
    uint8_t stale_handles;
    uint8_t tick_wrap;
    uint8_t capacity;
    uint8_t callback_errors;
    uint8_t invariants;
    uint32_t passed;
    uint32_t failed;
} timer_self_test_result_t;

int timer_init(uint32_t freq);
void timer_handler(registers_t* regs);
uint32_t timer_get_ticks(void);
uint32_t timer_get_frequency(void);
int timer_owner_create(const char* name, timer_owner_handle_t* out_owner);
int timer_owner_destroy(timer_owner_handle_t owner);
int timer_create(timer_owner_handle_t owner, const char* name,
                 timer_callback_fn callback, void* context,
                 timer_handle_t* out_timer);
int timer_start_once(timer_handle_t timer, uint32_t milliseconds);
int timer_start_periodic(timer_handle_t timer, uint32_t milliseconds);
int timer_cancel(timer_handle_t timer);
int timer_get_info(timer_handle_t timer, timer_info_t* out_info);
int timer_destroy(timer_handle_t timer);
int timer_get_stats(timer_stats_t* out_stats);
int timer_copy_active(timer_info_t* output, uint32_t max_timers,
                      uint32_t* out_count);
int timer_dispatch_pending(uint32_t budget, uint32_t* out_dispatched);
int timer_validate_state(void);
int timer_self_test(timer_self_test_result_t* out_result);
const char* timer_state_name(timer_state_t state);
const char* timer_mode_name(timer_mode_t mode);

#endif
