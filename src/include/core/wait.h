#ifndef WAIT_H
#define WAIT_H

#include "types.h"

#define WAIT_CHANNEL_OWNER_LENGTH 24U
#define WAIT_INFO_NAME_LENGTH 32U
#define WAIT_QUEUE_REGISTRY_CAPACITY 128U
#define WAIT_TIMEOUT_IMMEDIATE 0U
#define WAIT_TIMEOUT_INFINITE 0xFFFFFFFFU
#define WAIT_MAX_TIMEOUT_TICKS 0x7FFFFFFFU

typedef enum {
    WAIT_REASON_NONE = 0,
    WAIT_REASON_EVENT,
    WAIT_REASON_TIMEOUT,
    WAIT_REASON_CANCELLED,
    WAIT_REASON_DEVICE_UNAVAILABLE,
    WAIT_REASON_SIGNAL
} wait_reason_t;

typedef enum {
    WAIT_WAKE_ONE = 0,
    WAIT_WAKE_ALL
} wait_wake_mode_t;

typedef enum {
    WAIT_TARGET_PROCESS = 0,
    WAIT_TARGET_THREAD,
    WAIT_TARGET_ANY
} wait_target_type_t;

typedef struct wait_queue_head wait_queue_head_t;
typedef struct wait_queue_entry wait_queue_entry_t;

typedef int (*wait_condition_fn_t)(void* context, uint8_t* out_ready);
typedef void (*wait_queue_transition_fn_t)(void* target,
                                           wait_queue_entry_t* entry);
typedef void (*wait_queue_yield_fn_t)(void* target);

struct wait_queue_entry {
    wait_queue_entry_t* previous;
    wait_queue_entry_t* next;
    wait_queue_head_t* queue;
    void* target;
    const char* target_name;
    wait_queue_transition_fn_t block;
    wait_queue_transition_fn_t wake;
    wait_queue_yield_fn_t yield;
    wait_target_type_t target_type;
    uint32_t target_id;
    uint32_t observed_condition;
    uint32_t deadline_tick;
    wait_reason_t reason;
    uint8_t deadline_active;
    uint8_t linked;
};

struct wait_queue_head {
    char owner[WAIT_CHANNEL_OWNER_LENGTH];
    wait_queue_entry_t* first;
    wait_queue_entry_t* last;
    uint32_t id;
    uint32_t condition;
    uint32_t waiters;
    uint32_t peak_waiters;
    uint8_t initialized;
    uint8_t available;
    uint8_t registry_slot;
};

typedef wait_queue_head_t wait_channel_t;

typedef struct {
    uint8_t initialized;
    uint32_t channels_active;
    uint32_t active_waiters;
    uint32_t peak_waiters;
    uint32_t waits_started;
    uint32_t event_wakes;
    uint32_t timeout_wakes;
    uint32_t cancellation_wakes;
    uint32_t unavailable_wakes;
    uint32_t invalid_operations;
    uint32_t registry_capacity;
    uint32_t registry_peak;
    uint32_t registration_rejections;
    uint32_t wake_one_calls;
    uint32_t wake_all_calls;
    uint32_t context_errors;
    uint32_t orphan_errors;
    uint32_t signal_wakes;
} wait_stats_t;

typedef struct {
    uint32_t id;
    char owner[WAIT_CHANNEL_OWNER_LENGTH];
    uint32_t condition;
    uint32_t waiters;
    uint32_t peak_waiters;
    uint8_t available;
} wait_queue_info_t;

typedef struct {
    uint32_t id;
    uint32_t queue_id;
    uint32_t queue_position;
    wait_target_type_t target;
    char name[WAIT_INFO_NAME_LENGTH];
    char channel_owner[WAIT_CHANNEL_OWNER_LENGTH];
    wait_reason_t reason;
    uint32_t deadline_tick;
    uint32_t remaining_ticks;
    uint8_t deadline_active;
    uint8_t active;
} wait_info_t;

typedef struct {
    uint8_t channel_lifecycle;
    uint8_t condition_signal;
    uint8_t availability;
    uint8_t accounting;
    uint8_t reasons;
    uint8_t limits;
    uint8_t reset;
    uint8_t fifo;
    uint8_t wake_all;
    uint8_t lost_wakeup;
    uint8_t condition_recheck;
    uint8_t process_thread;
    uint8_t interrupt_context;
    uint8_t invariants;
    uint32_t passed;
    uint32_t failed;
} wait_self_test_result_t;

void wait_init(void);
int init_waitqueue_head(wait_queue_head_t* queue, const char* owner);
int wait_event(wait_queue_head_t* queue, wait_condition_fn_t condition,
               void* context, wait_reason_t* out_reason);
int wait_event_timeout(wait_queue_head_t* queue,
                       wait_condition_fn_t condition, void* context,
                       uint32_t timeout_ticks, wait_reason_t* out_reason);
int wake_up(wait_queue_head_t* queue, uint32_t* out_woken);
int wake_up_all(wait_queue_head_t* queue, uint32_t* out_woken);
int wait_queue_entry_init(wait_queue_entry_t* entry, void* target,
                          const char* target_name,
                          wait_target_type_t target_type,
                          uint32_t target_id,
                          wait_queue_transition_fn_t block,
                          wait_queue_transition_fn_t wake,
                          wait_queue_yield_fn_t yield);
int wait_queue_block(wait_queue_head_t* queue, wait_queue_entry_t* entry,
                     uint32_t observed_condition, uint32_t timeout_ticks,
                     wait_reason_t* out_reason);
int wait_queue_remove(wait_queue_entry_t* entry, wait_reason_t reason);
int wait_queue_wake_target(wait_queue_head_t* queue,
                           wait_target_type_t target,
                           wait_wake_mode_t mode, wait_reason_t reason,
                           uint32_t* out_woken);
int wait_queue_copy_info(wait_queue_info_t* output,
                         uint32_t max_entries, uint32_t* out_count);
int wait_queue_copy_waiters(wait_info_t* output,
                            uint32_t max_entries, uint32_t* out_count);
int wait_validate_state(void);

int wait_channel_init(wait_channel_t* channel, const char* owner);
int wait_channel_reset(wait_channel_t* channel);
int wait_channel_signal(wait_channel_t* channel);
int wait_channel_get_condition(const wait_channel_t* channel,
                               uint32_t* out_condition);
int wait_channel_set_available(wait_channel_t* channel, uint8_t available);
int wait_channel_is_available(const wait_channel_t* channel,
                              uint8_t* out_available);
void wait_note_waiter(wait_channel_t* channel);
void wait_note_wake(wait_channel_t* channel, wait_reason_t reason);
int wait_deadline_remaining(uint32_t deadline_tick, uint32_t* out_remaining);
int wait_deadline_remaining_active(uint32_t deadline_tick, uint8_t active,
                                   uint32_t* out_remaining);
int wait_get_stats(wait_stats_t* out_stats);
int wait_self_test(wait_self_test_result_t* out_result);
const char* wait_reason_name(wait_reason_t reason);
const char* wait_wake_mode_name(wait_wake_mode_t mode);

#endif
