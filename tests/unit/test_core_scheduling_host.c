#include <stdint.h>
#include <stdio.h>

#include "core/irq_deferred.h"
#include "core/log.h"
#include "core/string.h"
#include "core/wait.h"
#include "core/workqueue.h"
#include "core/errors.h"
#include "drivers/serial.h"
#include "process/process.h"
#include "process/thread.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

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
    printf("ZCOV_BEGIN|case=host:core:scheduling|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:scheduling|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:scheduling|value=0x%08X\n",
           (uint32_t)result);
}

static uint32_t fake_tick = 100U;
static process_t fake_worker;
static uint32_t fake_current_pid;
static uint32_t probe_worker_pid;
static uint8_t probe_process_wait_dispatch;

uint32_t timer_get_ticks(void) {
    return fake_tick++;
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

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void video_newline(void) {
}

process_t* process_get_by_pid(uint32_t pid) {
    return pid == fake_worker.pid ? &fake_worker : 0;
}

uint32_t process_get_current_pid(void) {
    return fake_current_pid;
}

void process_yield(void) {
}

int process_wait(wait_channel_t* channel, uint32_t observed_condition,
                 uint32_t timeout_ticks, wait_reason_t* out_reason) {
    uint32_t executed = 0U;
    uint32_t previous_pid;
    int result;

    (void)channel;
    (void)observed_condition;
    (void)timeout_ticks;
    if (!probe_process_wait_dispatch) return ERR_UNAVAILABLE;
    previous_pid = fake_current_pid;
    fake_current_pid = probe_worker_pid;
    result = workqueue_dispatch(1U, 1U, &executed);
    fake_current_pid = previous_pid;
    if (result != OK) return result;
    if (executed != 1U) return ERR_STATE;
    if (out_reason) *out_reason = WAIT_REASON_EVENT;
    return OK;
}

thread_t* thread_get_current(void) {
    return 0;
}

int thread_wait(wait_channel_t* channel, uint32_t observed_condition,
                uint32_t timeout_ticks, wait_reason_t* out_reason) {
    (void)channel;
    (void)observed_condition;
    (void)timeout_ticks;
    (void)out_reason;
    return ERR_UNAVAILABLE;
}

static int work_callback(void* context) {
    uint32_t* calls = (uint32_t*)context;

    if (!calls) return ERR_NULL;
    (*calls)++;
    return OK;
}

static void deferred_callback(void* context) {
    uint32_t* calls = (uint32_t*)context;

    if (calls) (*calls)++;
}

static void deferred_notifier(void* context) {
    uint32_t* notifications = (uint32_t*)context;

    if (notifications) (*notifications)++;
}

static void wait_block(void* target, wait_queue_entry_t* entry) {
    (void)target;
    (void)entry;
}

static void wait_wake(void* target, wait_queue_entry_t* entry) {
    (void)target;
    (void)entry;
}

static void wait_yield(void* target) {
    (void)target;
}

static int wait_ready(void* context, uint8_t* out_ready) {
    (void)context;
    if (!out_ready) return ERR_NULL;
    *out_ready = 1U;
    return OK;
}

static int check_wait(void) {
    wait_self_test_result_t result;
    wait_stats_t stats;
    uint32_t remaining;
    uint8_t available;
    wait_queue_head_t queue;
    wait_queue_entry_t entry;
    wait_queue_info_t queue_info;
    wait_info_t waiter_info;
    wait_reason_t reason;
    uint32_t target = 1U;
    uint32_t count = 0U;
    uint32_t condition = 0U;
    char long_owner[WAIT_CHANNEL_OWNER_LENGTH + 1U];

    kmemset(&queue, 0, sizeof(queue));
    kmemset(long_owner, 'x', sizeof(long_owner));
    long_owner[sizeof(long_owner) - 1U] = '\0';
    wait_init();
    if (init_waitqueue_head(NULL, "invalid") != ERR_NULL) return 1;
    if (init_waitqueue_head(&queue, "") != ERR_INVALID) return 2;
    if (init_waitqueue_head(&queue, long_owner) != ERR_OVERFLOW) return 3;
    if (wait_self_test(NULL) != ERR_NULL) return 4;
    if (wait_self_test(&result) != OK || result.failed || result.passed != 14U) {
        return 5;
    }
    if (wait_get_stats(NULL) != ERR_NULL) return 6;
    if (wait_get_stats(&stats) != OK || stats.active_waiters != 0U) return 7;
    if (wait_deadline_remaining(10U, NULL) != ERR_NULL) return 8;
    if (wait_deadline_remaining_active(10U, 0U, &remaining) != OK ||
        remaining != 0U) return 9;
    if (wait_deadline_remaining_active(0xFFFFFFFFU, 1U, &remaining) != OK) {
        return 10;
    }
    if (wait_channel_is_available(NULL, &available) != ERR_STATE) return 11;
    if (wait_channel_reset(&queue) != ERR_STATE) return 12;
    if (wait_validate_state() != OK) return 13;
    if (!wait_reason_name(WAIT_REASON_TIMEOUT) ||
        !wait_wake_mode_name(WAIT_WAKE_ALL)) return 14;
    if (wait_channel_init(&queue, "host-channel") != OK) return 15;
    if (wait_channel_get_condition(&queue, &condition) != OK ||
        wait_queue_copy_info(NULL, 1U, &count) != ERR_NULL ||
        wait_queue_copy_info(&queue_info, 1U, &count) != OK || count != 1U ||
        queue_info.id != queue.id) return 16;
    kmemset(&entry, 0, sizeof(entry));
    if (wait_queue_entry_init(&entry, &target, "host-target",
                              WAIT_TARGET_THREAD, 77U, wait_block, wait_wake,
                              wait_yield) != OK ||
        wait_queue_block(&queue, &entry, condition, WAIT_TIMEOUT_INFINITE,
                         &reason) != OK || reason != WAIT_REASON_NONE) {
        return 17;
    }
    if (wait_queue_copy_waiters(NULL, 1U, &count) != ERR_NULL ||
        wait_queue_copy_waiters(&waiter_info, 1U, &count) != OK ||
        count != 1U || waiter_info.id != 77U ||
        waiter_info.queue_id != queue.id || waiter_info.active != 1U) {
        return 18;
    }
    wait_note_waiter(&queue);
    wait_note_wake(&queue, WAIT_REASON_EVENT);
    if (wait_event(&queue, wait_ready, NULL, &reason) != OK ||
        reason != WAIT_REASON_EVENT || wait_queue_remove(&entry,
        WAIT_REASON_CANCELLED) != OK || wait_channel_reset(&queue) != OK) {
        return 19;
    }
    return 0;
}

static int check_workqueue(void) {
    workqueue_self_test_result_t self_test;
    workqueue_stats_t stats;
    work_info_t infos[2];
    work_struct_t work;
    work_struct_t cancelled_work;
    work_struct_t quiesce_work;
    uint32_t calls = 0U;
    uint32_t cancelled_calls = 0U;
    uint32_t executed = 0U;
    uint32_t info_count = 0U;
    uint8_t fallback;

    if (workqueue_self_test(NULL) != ERR_NULL) return 20;
    if (workqueue_self_test(&self_test) != OK || self_test.failed ||
        self_test.passed != 12U) return 21;
    if (workqueue_init() != OK || workqueue_init() != OK) return 22;
    if (workqueue_get_stats(&stats) != OK || !stats.initialized) return 23;
    if (workqueue_needs_fallback(&fallback) != OK || !fallback) return 24;
    if (workqueue_bind_worker(0U) != ERR_INVALID) return 25;
    if (workqueue_set_fallback(1U) != OK) return 26;
    kmemset(&work, 0, sizeof(work));
    if (work_init(&work, "host-work", WORK_PRIORITY_HIGH,
                  work_callback, &calls) != OK) return 28;
    if (schedule_work(&work) != OK || schedule_work(&work) != OK) return 29;
    if (workqueue_copy_info(NULL, 1U, &info_count) != ERR_NULL ||
        workqueue_copy_info(NULL, 0U, &info_count) != OK ||
        info_count != 0U ||
        workqueue_copy_info(infos, 2U, &info_count) != OK ||
        info_count != 2U || infos[1].state != WORK_STATE_READY) return 30;
    kmemset(&cancelled_work, 0, sizeof(cancelled_work));
    if (work_init(&cancelled_work, "cancel-work", WORK_PRIORITY_NORMAL,
                  work_callback, &cancelled_calls) != OK ||
        schedule_delayed_work(&cancelled_work, 2U) != OK ||
        cancel_work(&cancelled_work) != OK || cancelled_work.state !=
        WORK_STATE_IDLE || cancelled_calls != 0U ||
        work_destroy(&cancelled_work) != OK) return 31;
    if (workqueue_dispatch(1U, 0U, &executed) != OK || executed != 1U ||
        calls != 1U) return 32;
    if (workqueue_dispatch(1U, 1U, NULL) != ERR_NULL) return 33;
    if (work_destroy(&work) != OK || work_destroy(&work) != ERR_STATE) {
        return 34;
    }
    if (workqueue_power_quiesce_until(0U) != ERR_TIMEOUT) return 35;
    kmemset(&quiesce_work, 0, sizeof(quiesce_work));
    if (work_init(&quiesce_work, "quiesce-work", WORK_PRIORITY_NORMAL,
                  work_callback, &calls) != OK) return 36;
    if (schedule_delayed_work(&quiesce_work, 4U) != OK) return 37;
    if (workqueue_power_set_quiescing(1U) != OK) return 38;
    if (workqueue_dispatch(1U, 1U, &executed) != OK || executed != 0U) {
        return 39;
    }
    if (workqueue_power_quiesce_until(fake_tick + 1000U) != OK) return 40;
    if (quiesce_work.state != WORK_STATE_IDLE) return 41;
    if (workqueue_power_set_quiescing(0U) != OK) return 42;
    if (work_destroy(&quiesce_work) != OK) return 43;
    if (workqueue_validate_state() != OK) return 37;
    if (workqueue_get_stats(&stats) != OK || stats.running || stats.ready_high ||
        stats.ready_normal || stats.delayed) return 44;
    fake_worker.pid = 123U;
    fake_worker.state = PROCESS_STATE_READY;
    probe_worker_pid = fake_worker.pid;
    if (workqueue_bind_worker(probe_worker_pid) != OK ||
        workqueue_needs_fallback(&fallback) != OK || fallback) return 45;
    probe_process_wait_dispatch = 1U;
    if (workqueue_probe_worker(10U) != OK) return 46;
    probe_process_wait_dispatch = 0U;
    if (workqueue_validate_state() != OK) return 47;
    if (workqueue_set_fallback(0U) != OK) return 45;
    if (!workqueue_priority_name(WORK_PRIORITY_HIGH) ||
        !workqueue_state_name(WORK_STATE_IDLE) ||
        !workqueue_context_name(WORK_CONTEXT_NONE)) return 46;
    return 0;
}

static int check_irq_deferred(void) {
    irq_deferred_self_test_result_t self_test;
    irq_deferred_status_t status;
    irq_deferred_irq_status_t irq_status;
    irq_deferred_work_t work;
    uint32_t calls = 0U;
    uint32_t notifications = 0U;
    uint32_t processed = 0U;

    if (irq_deferred_set_notifier(NULL, NULL) != ERR_STATE) return 40;
    if (irq_deferred_self_test(NULL) != ERR_NULL) return 40;
    if (irq_deferred_init() != OK ||
        irq_deferred_self_test(&self_test) != OK || self_test.failed ||
        self_test.passed != 8U) return 41;
    if (irq_deferred_set_notifier(deferred_notifier, &notifications) != OK ||
        irq_deferred_set_notifier(NULL, NULL) != OK ||
        irq_deferred_set_notifier(deferred_notifier, &notifications) != OK) {
        return 42;
    }
    if (irq_deferred_get_status(NULL) != ERR_NULL) return 43;
    if (irq_deferred_get_status(&status) != OK || !status.initialized) return 43;
    if (irq_deferred_get_irq_status(IRQ_DEFERRED_IRQ_COUNT, &irq_status) !=
        ERR_INVALID) return 44;
    if (irq_deferred_work_init(NULL, "host", 1U, deferred_callback, &calls) !=
        ERR_NULL) return 45;
    kmemset(&work, 0, sizeof(work));
    if (irq_deferred_work_init(&work, "host", 1U, deferred_callback, &calls) !=
        OK) return 46;
    if (irq_deferred_schedule(&work) != OK ||
        irq_deferred_schedule(&work) != OK) return 47;
    if (irq_deferred_dispatch(1U, &processed) != OK || processed != 1U ||
        calls != 1U) return 48;
    if (irq_deferred_cancel(&work) != ERR_NOT_FOUND) return 49;
    if (irq_deferred_validate_state() != OK) return 50;
    if (irq_deferred_get_status(&status) != OK || status.queued != 0U ||
        status.running != 0U) return 51;
    return 0;
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    log_init();
    result = check_wait();
    if (!result) result = check_workqueue();
    if (!result) result = check_irq_deferred();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
