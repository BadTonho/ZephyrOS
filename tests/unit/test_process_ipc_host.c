#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/wait.h"
#include "drivers/serial.h"
#include "process/process.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U
#define IPC_FIXTURE_SENDER_PID 20U
#define IPC_FIXTURE_TARGET_PID 21U
#define IPC_FIXTURE_BLOCKED_PID 22U
#define IPC_FIXTURE_ZOMBIE_PID 23U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static process_t ipc_sender;
static process_t ipc_target;
static process_t ipc_blocked;
static process_t ipc_zombie;
static process_t* ipc_current;
static uint32_t wake_calls;
static uint32_t poll_calls;
static uint32_t wait_condition_calls;

process_t* processes[MAX_PROCESSES];
uint32_t process_count;

static void __attribute__((no_instrument_function)) coverage_record(
    void* function) {
    uintptr_t address = (uintptr_t)function;

    if (!coverage_active || !address) return;
    for (uint32_t index = 0U; index < coverage_count; index++) {
        if (coverage_addresses[index] == address) return;
    }
    if (coverage_count < HOST_COVERAGE_CAPACITY) {
        coverage_addresses[coverage_count++] = address;
    }
}

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(
    void* function, void* caller) {
    (void)caller;
    coverage_record(function);
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(
    void* function, void* caller) {
    (void)function;
    (void)caller;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:process:ipc|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:process:ipc|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:process:ipc|value=0x%08X\n",
           (uint32_t)result);
}

uint32_t timer_get_ticks(void) {
    return 100U;
}

uint32_t timer_get_frequency(void) {
    return 1000U;
}

uint8_t serial_is_ready(void) {
    return 1U;
}

uint32_t serial_write_text(const char* text, uint32_t length) {
    (void)text;
    return length;
}

uint32_t serial_flush(uint32_t budget) {
    (void)budget;
    return 0U;
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void video_newline(void) {
}

process_t* process_get_by_pid(uint32_t pid) {
    for (uint32_t index = 0U; index < MAX_PROCESSES; index++) {
        if (processes[index] && processes[index]->pid == pid) {
            return processes[index];
        }
    }
    return 0;
}

process_t* process_get_current(void) {
    return ipc_current;
}

int wake_up(wait_queue_head_t* queue, uint32_t* out_woken) {
    if (!queue || !out_woken) return ERR_NULL;
    wake_calls++;
    *out_woken = 0U;
    return OK;
}

int vfs_poll_notify(void) {
    poll_calls++;
    return OK;
}

int wait_channel_get_condition(const wait_channel_t* channel,
                               uint32_t* out_condition) {
    if (!channel || !out_condition) return ERR_NULL;
    *out_condition = channel->condition;
    return OK;
}

int wait_event_timeout(wait_queue_head_t* queue, wait_condition_fn_t condition,
                       void* context, uint32_t timeout_ticks,
                       wait_reason_t* out_reason) {
    uint8_t ready = 0U;
    int result;

    if (!queue || !condition || !out_reason) return ERR_NULL;
    wait_condition_calls++;
    result = condition(context, &ready);
    if (result != OK) return result;
    if (!ready || timeout_ticks == WAIT_TIMEOUT_IMMEDIATE) {
        *out_reason = WAIT_REASON_TIMEOUT;
        return OK;
    }
    queue->condition++;
    *out_reason = WAIT_REASON_EVENT;
    return OK;
}

static void fixture_reset(void) {
    kmemset(processes, 0, sizeof(processes));
    kmemset(&ipc_sender, 0, sizeof(ipc_sender));
    kmemset(&ipc_target, 0, sizeof(ipc_target));
    kmemset(&ipc_blocked, 0, sizeof(ipc_blocked));
    kmemset(&ipc_zombie, 0, sizeof(ipc_zombie));
    ipc_sender.pid = IPC_FIXTURE_SENDER_PID;
    ipc_sender.state = PROCESS_STATE_READY;
    ipc_target.pid = IPC_FIXTURE_TARGET_PID;
    ipc_target.state = PROCESS_STATE_RUNNING;
    ipc_blocked.pid = IPC_FIXTURE_BLOCKED_PID;
    ipc_blocked.state = PROCESS_STATE_BLOCKED;
    ipc_zombie.pid = IPC_FIXTURE_ZOMBIE_PID;
    ipc_zombie.state = PROCESS_STATE_ZOMBIE;
    processes[0] = &ipc_sender;
    processes[1] = &ipc_target;
    processes[2] = &ipc_blocked;
    processes[3] = &ipc_zombie;
    process_count = 4U;
    ipc_current = &ipc_sender;
    wake_calls = 0U;
    poll_calls = 0U;
    wait_condition_calls = 0U;
}

static ipc_msg_t valid_message(uint32_t value) {
    ipc_msg_t message;

    message.type = IPC_MSG_KEYBOARD;
    message.data1 = value;
    message.data2 = value + 1U;
    return message;
}

static int check_preinitialization(void) {
    ipc_msg_t message = valid_message(1U);
    wait_reason_t reason = WAIT_REASON_EVENT;

    if (ipc_is_ready() != 0) return 1;
    if (ipc_send(IPC_FIXTURE_TARGET_PID, &message) != 0) return 2;
    if (ipc_receive(&message) != 0) return 3;
    if (ipc_wait(1U, &reason) != ERR_STATE) return 4;
    if (reason != WAIT_REASON_NONE) return 5;
    if (process_set_focus(IPC_FIXTURE_TARGET_PID) != ERR_STATE) return 6;
    if (process_set_focus_fallback(IPC_FIXTURE_TARGET_PID) != ERR_STATE) return 7;
    if (process_restore_focus() != ERR_STATE) return 8;
    return 0;
}

static int check_initialization(void) {
    ipc_stats_t stats;

    ipc_init();
    if (ipc_is_ready() != 1) return 1;
    ipc_get_stats(&stats);
    if (stats.sent || stats.received || stats.failed || stats.queue_full) {
        return 2;
    }
    if (ipc_get_pending_count() != 0U) return 3;
    if (process_get_focus() != 0U) return 4;
    ipc_get_stats(NULL);
    return 0;
}

static int check_invalid_messages(void) {
    ipc_msg_t message = valid_message(2U);
    ipc_stats_t stats;

    if (ipc_send(IPC_FIXTURE_TARGET_PID, NULL) != 0) return 1;
    message.type = IPC_MSG_NONE;
    if (ipc_send(IPC_FIXTURE_TARGET_PID, &message) != 0) return 2;
    message.type = (ipc_msg_type_t)99;
    if (ipc_send(IPC_FIXTURE_TARGET_PID, &message) != 0) return 3;
    message = valid_message(3U);
    if (ipc_send(999U, &message) != 0) return 4;
    if (ipc_send(IPC_FIXTURE_ZOMBIE_PID, &message) != 0) return 5;
    if (ipc_receive(NULL) != 0) return 6;
    if (wake_calls || poll_calls) return 7;
    ipc_get_stats(&stats);
    if (stats.failed != 6U) return 8;
    return 0;
}

static int check_send_receive(void) {
    ipc_msg_t sent = valid_message(10U);
    ipc_msg_t received;

    if (ipc_send(IPC_FIXTURE_TARGET_PID, &sent) != 1) return 1;
    if (wake_calls != 1U || poll_calls != 1U) return 2;
    if (ipc_get_pending_count() != 1U) return 3;
    if (ipc_current_has_pending() != 0) return 4;
    ipc_current = &ipc_target;
    if (ipc_current_has_pending() != 1) return 5;
    if (ipc_receive(&received) != 1) return 6;
    if (received.type != sent.type || received.data1 != sent.data1 ||
        received.data2 != sent.data2) return 7;
    if (ipc_receive(&received) != 0) return 8;
    if (ipc_get_pending_count() != 0U) return 9;
    ipc_current = &ipc_sender;
    return 0;
}

static int check_queue_limit(void) {
    ipc_msg_t message = valid_message(20U);
    ipc_stats_t before;
    ipc_stats_t after;

    ipc_get_stats(&before);
    for (uint32_t index = 0U; index < IPC_MSG_QUEUE_SIZE - 1U; index++) {
        message.data1 = index;
        if (ipc_send(IPC_FIXTURE_TARGET_PID, &message) != 1) return 1;
    }
    if (ipc_send(IPC_FIXTURE_TARGET_PID, &message) != 0) return 2;
    ipc_get_stats(&after);
    if (after.sent != before.sent + IPC_MSG_QUEUE_SIZE - 1U) return 3;
    if (after.failed != before.failed + 1U) return 4;
    if (after.queue_full != before.queue_full + 1U) return 5;
    if (ipc_get_pending_count() != IPC_MSG_QUEUE_SIZE - 1U) return 6;
    ipc_target.msg_head = 0U;
    ipc_target.msg_tail = 0U;
    if (ipc_get_pending_count() != 0U) return 7;
    return 0;
}

static int check_wait(void) {
    ipc_msg_t message = valid_message(30U);
    wait_reason_t reason = WAIT_REASON_NONE;
    uint32_t previous_generation;

    if (ipc_wait(1U, NULL) != ERR_NULL) return 1;
    ipc_current = NULL;
    if (ipc_wait(1U, &reason) != ERR_STATE) return 2;
    ipc_current = &ipc_target;
    if (ipc_wait(WAIT_TIMEOUT_IMMEDIATE, &reason) != OK) return 3;
    if (reason != WAIT_REASON_TIMEOUT) return 4;
    if (ipc_send(IPC_FIXTURE_TARGET_PID, &message) != 1) return 5;
    previous_generation = ipc_target.ipc_wait_generation;
    if (ipc_wait(10U, &reason) != OK) return 6;
    if (reason != WAIT_REASON_EVENT) return 7;
    if (ipc_target.ipc_wait_generation == previous_generation) return 8;
    ipc_target.pending_signals = 1U;
    ipc_target.blocked_signals = 0U;
    if (ipc_wait(10U, &reason) != OK) return 9;
    if (reason != WAIT_REASON_SIGNAL) return 10;
    ipc_target.pending_signals = 0U;
    ipc_target.msg_head = 0U;
    ipc_target.msg_tail = 0U;
    ipc_current = &ipc_sender;
    if (wait_condition_calls < 3U) return 11;
    return 0;
}

static int check_focus(void) {
    if (process_set_focus(IPC_FIXTURE_TARGET_PID) != OK) return 1;
    if (process_get_focus() != IPC_FIXTURE_TARGET_PID) return 2;
    if (process_set_focus(IPC_FIXTURE_BLOCKED_PID) != OK) return 3;
    if (process_get_focus() != IPC_FIXTURE_BLOCKED_PID) return 4;
    if (process_set_focus(IPC_FIXTURE_ZOMBIE_PID) != ERR_NOT_FOUND) return 5;
    if (process_set_focus(999U) != ERR_NOT_FOUND) return 6;
    if (process_set_focus_fallback(IPC_FIXTURE_TARGET_PID) != OK) return 7;
    if (process_restore_focus() != OK) return 8;
    if (process_get_focus() != IPC_FIXTURE_TARGET_PID) return 9;
    ipc_target.state = PROCESS_STATE_ZOMBIE;
    if (process_restore_focus() != ERR_NOT_FOUND) return 10;
    if (process_get_focus() != 0U) return 11;
    if (process_set_focus_fallback(IPC_FIXTURE_ZOMBIE_PID) != ERR_NOT_FOUND) {
        return 12;
    }
    return 0;
}

static int check_final_state(void) {
    ipc_stats_t stats;

    ipc_get_stats(&stats);
    if (stats.received != 1U) return 1;
    if (stats.queue_full != 1U) return 2;
    if (ipc_get_pending_count() != 0U) return 3;
    if (ipc_current_has_pending() != 0) return 4;
    if (process_count != 4U) return 5;
    return 0;
}

int main(void) {
    int result;

    fixture_reset();
    coverage_active = 1U;
    result = check_preinitialization();
    if (!result) result = check_initialization();
    if (!result) result = check_invalid_messages();
    if (!result) result = check_send_receive();
    if (!result) result = check_queue_limit();
    if (!result) result = check_wait();
    if (!result) result = check_focus();
    if (!result) result = check_final_state();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        fprintf(stderr, "process ipc host failure: %d\n", result);
        return result;
    }
    printf("process ipc host: PASS\n");
    return 0;
}
