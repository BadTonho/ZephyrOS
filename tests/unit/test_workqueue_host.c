#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/wait.h"
#include "core/workqueue.h"
#include "drivers/serial.h"
#include "process/process.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define WORKER_PID 40U
#define CURRENT_PID 41U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static process_t worker_process;
static process_t current_process;
static uint32_t current_pid = CURRENT_PID;
static uint32_t callback_calls;
static int callback_result;
static uint32_t yield_calls;
static uint32_t wake_calls;
static uint32_t signal_calls;
static uint32_t wait_calls;

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
    printf("ZCOV_BEGIN|case=host:core:workqueue|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:workqueue|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:workqueue|value=0x%08X\n",
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

uint32_t process_get_current_pid(void) {
    return current_pid;
}

process_t* process_get_current(void) {
    if (current_pid == worker_process.pid) return &worker_process;
    if (current_pid == current_process.pid) return &current_process;
    return 0;
}

void process_yield(void) {
    yield_calls++;
}

int init_waitqueue_head(wait_queue_head_t* queue, const char* owner) {
    if (!queue || !owner || !owner[0]) return ERR_NULL;
    kmemset(queue, 0, sizeof(*queue));
    queue->initialized = 1U;
    queue->available = 1U;
    queue->id = 1U;
    for (uint32_t index = 0U;
         index + 1U < WAIT_CHANNEL_OWNER_LENGTH && owner[index]; index++) {
        queue->owner[index] = owner[index];
    }
    return OK;
}

int wait_channel_reset(wait_channel_t* channel) {
    if (!channel) return ERR_NULL;
    channel->initialized = 0U;
    channel->available = 0U;
    channel->first = 0;
    channel->last = 0;
    channel->waiters = 0U;
    return OK;
}

int wait_channel_signal(wait_channel_t* channel) {
    if (!channel || !channel->initialized) return ERR_STATE;
    channel->condition++;
    signal_calls++;
    return OK;
}

int wake_up(wait_queue_head_t* queue, uint32_t* out_woken) {
    if (!queue || !out_woken) return ERR_NULL;
    wake_calls++;
    *out_woken = 0U;
    return OK;
}

int wake_up_all(wait_queue_head_t* queue, uint32_t* out_woken) {
    return wake_up(queue, out_woken);
}

int wait_event_timeout(wait_queue_head_t* queue, wait_condition_fn_t condition,
                       void* context, uint32_t timeout_ticks,
                       wait_reason_t* out_reason) {
    uint8_t ready = 0U;
    int result;

    if (!queue || !condition || !out_reason) return ERR_NULL;
    wait_calls++;
    result = condition(context, &ready);
    if (result != OK) return result;
    if (ready || timeout_ticks == WAIT_TIMEOUT_IMMEDIATE) {
        *out_reason = ready ? WAIT_REASON_EVENT : WAIT_REASON_TIMEOUT;
        return OK;
    }
    *out_reason = WAIT_REASON_TIMEOUT;
    return OK;
}

static int callback(void* context) {
    uint32_t* marker = (uint32_t*)context;

    callback_calls++;
    if (marker) (*marker)++;
    return callback_result;
}

static void fixture_reset(void) {
    kmemset(processes, 0, sizeof(processes));
    kmemset(&worker_process, 0, sizeof(worker_process));
    kmemset(&current_process, 0, sizeof(current_process));
    worker_process.pid = WORKER_PID;
    worker_process.state = PROCESS_STATE_RUNNING;
    current_process.pid = CURRENT_PID;
    current_process.state = PROCESS_STATE_RUNNING;
    processes[0] = &worker_process;
    processes[1] = &current_process;
    process_count = 2U;
    current_pid = CURRENT_PID;
    callback_calls = 0U;
    callback_result = OK;
    yield_calls = 0U;
    wake_calls = 0U;
    signal_calls = 0U;
    wait_calls = 0U;
}

static int check_preinitialization(void) {
    work_struct_t work;
    workqueue_stats_t stats;
    uint32_t executed = 0U;

    kmemset(&work, 0, sizeof(work));
    if (workqueue_get_stats(NULL) != ERR_NULL) return 1;
    if (workqueue_get_stats(&stats) != ERR_STATE) return 2;
    if (workqueue_dispatch(1U, 1U, &executed) != ERR_STATE) return 3;
    if (workqueue_dispatch(1U, 1U, NULL) != ERR_NULL) return 4;
    if (work_init(&work, "before", WORK_PRIORITY_NORMAL, callback, NULL) !=
        ERR_STATE) return 5;
    if (workqueue_self_test(NULL) != ERR_NULL) return 6;
    if (workqueue_power_set_quiescing(1U) != ERR_STATE) return 7;
    return 0;
}

static int check_initialization(void) {
    workqueue_stats_t stats;
    workqueue_self_test_result_t self_test;

    if (workqueue_init() != OK) return 1;
    if (workqueue_init() != OK) return 2;
    if (workqueue_get_stats(&stats) != OK) return 3;
    if (!stats.initialized || stats.capacity != WORKQUEUE_CAPACITY ||
        stats.registered != 1U) return 4;
    if (workqueue_validate_state() != OK) return 5;
    if (workqueue_self_test(&self_test) != OK) return 6;
    if (self_test.failed != 0U || self_test.passed != 12U) return 7;
    if (workqueue_priority_name(WORK_PRIORITY_HIGH)[0] != 'H' ||
        workqueue_priority_name((work_priority_t)99)[0] != 'I') return 8;
    if (workqueue_state_name(WORK_STATE_IDLE)[0] != 'I' ||
        workqueue_state_name((work_state_t)99)[0] != 'I') return 9;
    if (workqueue_context_name(WORK_CONTEXT_NONE)[0] != 'N' ||
        workqueue_context_name((work_context_t)99)[0] != 'N') return 10;
    return 0;
}

static int check_work_lifecycle(void) {
    work_struct_t work;
    work_info_t info[2];
    workqueue_stats_t stats;
    uint32_t marker = 0U;
    uint32_t executed = 0U;

    kmemset(&work, 0, sizeof(work));
    if (work_init(&work, "HostWork", WORK_PRIORITY_NORMAL, callback,
                  &marker) != OK) return 1;
    if (work_init(&work, "Duplicate", WORK_PRIORITY_NORMAL, callback,
                  NULL) != ERR_STATE) return 2;
    if (schedule_work(&work) != OK) return 3;
    if (schedule_work(&work) != OK) return 4;
    if (workqueue_copy_info(info, 2U, NULL) != ERR_NULL) return 5;
    if (workqueue_copy_info(info, 2U, &(uint32_t){0U}) != OK) return 6;
    if (work.state != WORK_STATE_READY || work.coalesced != 1U) return 7;
    if (workqueue_dispatch(1U, 1U, &executed) != ERR_STATE) return 8;
    if (workqueue_set_fallback(1U) != OK) return 9;
    if (workqueue_dispatch(0U, 1U, &executed) != OK || executed != 1U) {
        return 10;
    }
    if (callback_calls != 1U || marker != 1U || work.executed != 1U) return 11;
    callback_result = ERR_INVALID;
    if (schedule_work(&work) != OK) return 12;
    if (workqueue_dispatch(0U, 1U, &executed) != OK || executed != 1U) {
        return 13;
    }
    if (work.last_error != ERR_INVALID) return 14;
    callback_result = OK;
    if (schedule_delayed_work(&work, 5U) != OK) return 15;
    if (work.state != WORK_STATE_DELAYED) return 16;
    if (workqueue_copy_info(info, 2U, &(uint32_t){0U}) != OK) return 17;
    if (schedule_delayed_work(&work, 0U) != OK) return 18;
    if (work.state != WORK_STATE_READY) return 19;
    if (cancel_work(&work) != OK || work.state != WORK_STATE_IDLE) return 20;
    if (cancel_work(NULL) != ERR_NULL) return 21;
    if (workqueue_get_stats(&stats) != OK || stats.callback_errors == 0U) {
        return 22;
    }
    if (work_destroy(&work) != OK) return 23;
    if (work_destroy(&work) != ERR_STATE) return 24;
    if (workqueue_validate_state() != OK) return 25;
    return 0;
}

static int check_fallback_and_power(void) {
    work_struct_t work;
    uint8_t required = 0U;
    uint32_t marker = 0U;

    if (workqueue_needs_fallback(NULL) != ERR_NULL) return 1;
    if (workqueue_needs_fallback(&required) != OK || !required) return 2;
    if (workqueue_bind_worker(0U) != ERR_INVALID) return 3;
    if (workqueue_bind_worker(999U) != ERR_NOT_FOUND) return 4;
    if (workqueue_bind_worker(WORKER_PID) != OK) return 5;
    if (workqueue_needs_fallback(&required) != OK || required) return 6;
    worker_process.state = PROCESS_STATE_ZOMBIE;
    if (workqueue_needs_fallback(&required) != OK || !required) return 7;
    worker_process.state = PROCESS_STATE_RUNNING;
    if (workqueue_set_fallback(0U) != OK) return 8;
    if (workqueue_power_quiesce_until(99U) != ERR_TIMEOUT) return 9;
    kmemset(&work, 0, sizeof(work));
    if (work_init(&work, "Delayed", WORK_PRIORITY_HIGH, callback, &marker) !=
        OK) return 10;
    if (schedule_delayed_work(&work, 5U) != OK) return 11;
    if (workqueue_power_quiesce_until(200U) != OK) return 12;
    if (work.state != WORK_STATE_IDLE) return 13;
    if (workqueue_power_set_quiescing(0U) != OK) return 14;
    if (work_destroy(&work) != OK) return 15;
    if (workqueue_set_fallback(1U) != OK) return 16;
    return 0;
}

static int check_worker(void) {
    work_struct_t work;
    uint32_t marker = 0U;
    workqueue_stats_t stats;

    kmemset(&work, 0, sizeof(work));
    if (work_init(&work, "Worker", WORK_PRIORITY_HIGH, callback, &marker) !=
        OK) return 1;
    if (schedule_work(&work) != OK) return 2;
    current_pid = CURRENT_PID;
    workqueue_worker_main();
    if (callback_calls == 0U || marker == 0U || work.state != WORK_STATE_IDLE) {
        return 3;
    }
    if (yield_calls == 0U || wait_calls == 0U) return 4;
    if (workqueue_get_stats(&stats) != OK || stats.sleeps == 0U) return 5;
    if (work_destroy(&work) != OK) return 6;
    if (workqueue_validate_state() != OK) return 7;
    return 0;
}

static int check_final_state(void) {
    workqueue_stats_t stats;

    if (workqueue_set_fallback(0U) != OK) return 1;
    if (workqueue_get_stats(&stats) != OK) return 2;
    if (stats.registered != 1U || stats.ready_high || stats.ready_normal ||
        stats.delayed || stats.running) return 3;
    if (!wake_calls || !signal_calls) return 4;
    return workqueue_validate_state();
}

int main(void) {
    int result;

    fixture_reset();
    coverage_active = 1U;
    result = check_preinitialization();
    if (!result) result = check_initialization();
    if (!result) result = check_work_lifecycle();
    if (!result) result = check_fallback_and_power();
    if (!result) result = check_worker();
    if (!result) result = check_final_state();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        fprintf(stderr, "workqueue host failure: %d\n", result);
        return result;
    }
    printf("workqueue host: PASS\n");
    return 0;
}
