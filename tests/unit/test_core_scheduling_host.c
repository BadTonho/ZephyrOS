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
    (void)pid;
    return 0;
}

uint32_t process_get_current_pid(void) {
    return 0U;
}

void process_yield(void) {
}

int process_wait(wait_channel_t* channel, uint32_t observed_condition,
                 uint32_t timeout_ticks, wait_reason_t* out_reason) {
    (void)channel;
    (void)observed_condition;
    (void)timeout_ticks;
    (void)out_reason;
    return ERR_UNAVAILABLE;
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

static int check_wait(void) {
    wait_self_test_result_t result;
    wait_stats_t stats;
    uint32_t remaining;
    uint8_t available;
    wait_queue_head_t queue;
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
    return 0;
}

static int check_workqueue(void) {
    workqueue_self_test_result_t self_test;
    workqueue_stats_t stats;
    work_struct_t work;
    uint32_t calls = 0U;
    uint32_t executed = 0U;
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
    if (workqueue_dispatch(1U, 0U, &executed) != OK || executed != 1U ||
        calls != 1U) return 30;
    if (workqueue_dispatch(1U, 1U, NULL) != ERR_NULL) return 31;
    if (work_destroy(&work) != OK || work_destroy(&work) != ERR_STATE) {
        return 32;
    }
    if (workqueue_validate_state() != OK) return 33;
    if (workqueue_get_stats(&stats) != OK || stats.running || stats.ready_high ||
        stats.ready_normal || stats.delayed) return 34;
    if (workqueue_set_fallback(0U) != OK) return 36;
    if (!workqueue_priority_name(WORK_PRIORITY_HIGH) ||
        !workqueue_state_name(WORK_STATE_IDLE) ||
        !workqueue_context_name(WORK_CONTEXT_NONE)) return 35;
    return 0;
}

static int check_irq_deferred(void) {
    irq_deferred_self_test_result_t self_test;
    irq_deferred_status_t status;
    irq_deferred_irq_status_t irq_status;
    irq_deferred_work_t work;
    uint32_t calls = 0U;
    uint32_t processed = 0U;

    if (irq_deferred_self_test(NULL) != ERR_NULL) return 40;
    if (irq_deferred_init() != OK ||
        irq_deferred_self_test(&self_test) != OK || self_test.failed ||
        self_test.passed != 8U) return 41;
    if (irq_deferred_get_status(NULL) != ERR_NULL) return 42;
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
