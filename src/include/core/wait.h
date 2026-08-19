#ifndef WAIT_H
#define WAIT_H

#include "types.h"

#define WAIT_CHANNEL_OWNER_LENGTH 24U
#define WAIT_INFO_NAME_LENGTH 32U
#define WAIT_TIMEOUT_IMMEDIATE 0U
#define WAIT_TIMEOUT_INFINITE 0xFFFFFFFFU
#define WAIT_MAX_TIMEOUT_TICKS 0x7FFFFFFFU

typedef enum {
    WAIT_REASON_NONE = 0,
    WAIT_REASON_EVENT,
    WAIT_REASON_TIMEOUT,
    WAIT_REASON_CANCELLED,
    WAIT_REASON_DEVICE_UNAVAILABLE
} wait_reason_t;

typedef enum {
    WAIT_WAKE_ONE = 0,
    WAIT_WAKE_ALL
} wait_wake_mode_t;

typedef enum {
    WAIT_TARGET_PROCESS = 0,
    WAIT_TARGET_THREAD
} wait_target_type_t;

typedef struct {
    char owner[WAIT_CHANNEL_OWNER_LENGTH];
    uint32_t condition;
    uint32_t waiters;
    uint8_t initialized;
    uint8_t available;
} wait_channel_t;

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
} wait_stats_t;

typedef struct {
    uint32_t id;
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
    uint8_t invariants;
    uint32_t passed;
    uint32_t failed;
} wait_self_test_result_t;

void wait_init(void);
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
