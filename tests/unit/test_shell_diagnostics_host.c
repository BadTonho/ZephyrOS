#include <stdint.h>
#include <stdio.h>

#include "apps/shell_command_utils.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/clock.h"
#include "core/irq_deferred.h"
#include "core/video.h"
#include "core/wait.h"
#include "core/workqueue.h"
#include "drivers/idt.h"
#include "drivers/rtc.h"
#include "drivers/mouse.h"
#include "fs/vfs.h"

void shell_dispatch_cmd_pwd(const char* arguments);
void shell_dispatch_cmd_cd(const char* arguments);
void shell_dispatch_cmd_mouse(const char* arguments);
void shell_dispatch_cmd_log(const char* arguments);
void shell_dispatch_cmd_timer(const char* arguments);
void shell_dispatch_cmd_clock(const char* arguments);
void shell_dispatch_cmd_irqstat(const char* arguments);
void shell_dispatch_cmd_wait(const char* arguments);
void shell_dispatch_cmd_wqinfo(const char* arguments);
void shell_dispatch_cmd_workq(const char* arguments);

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_OUTPUT_CAPACITY 4096U
#define HOST_PATH_CAPACITY 256U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static char video_output[HOST_OUTPUT_CAPACITY];
static uint32_t video_output_length;
static char fixture_cwd[HOST_PATH_CAPACITY];
static char fixture_last_path[HOST_PATH_CAPACITY];
static mouse_status_t fixture_mouse_status;
static int fixture_getcwd_result;
static int fixture_chdir_result;
static int fixture_mouse_status_result;
static int fixture_mouse_speed_result;
static int fixture_mouse_primary_result;
static int fixture_mouse_acceleration_result;
static uint32_t fixture_chdir_calls;
static uint32_t fixture_speed_calls;
static uint32_t fixture_primary_calls;
static uint32_t fixture_acceleration_calls;
static log_stats_t fixture_log_stats;
static log_record_t fixture_log_records[2];
static log_self_test_result_t fixture_log_test;
static int fixture_log_stats_result;
static int fixture_log_copy_result;
static int fixture_log_set_console_result;
static int fixture_log_set_buffer_result;
static int fixture_log_test_result;
static uint32_t fixture_log_copy_count;
static uint32_t fixture_log_clear_calls;
static uint32_t fixture_log_console_calls;
static uint32_t fixture_log_buffer_calls;
static log_level_t fixture_log_console_level;
static log_level_t fixture_log_buffer_level;
static timer_stats_t fixture_timer_stats;
static timer_info_t fixture_timer_records[2];
static timer_self_test_result_t fixture_timer_test;
static int fixture_timer_stats_result;
static int fixture_timer_copy_result;
static int fixture_timer_test_result;
static uint32_t fixture_timer_copy_count;
static clock_status_t fixture_clock_status;
static clock_self_test_result_t fixture_clock_test;
static int fixture_clock_status_result;
static int fixture_clock_ticks_result;
static int fixture_clock_utc_result;
static int fixture_clock_test_result;
static uint64_t fixture_clock_ticks;
static uint64_t fixture_clock_utc;
static rtc_status_t fixture_rtc_status;
static rtc_self_test_result_t fixture_rtc_test;
static int fixture_rtc_status_result;
static int fixture_rtc_test_result;
static irq_deferred_status_t fixture_irq_status;
static irq_deferred_irq_status_t fixture_irq_lines[IRQ_DEFERRED_IRQ_COUNT];
static idt_irq_status_t fixture_idt_lines[IDT_IRQ_LINE_COUNT];
static irq_deferred_self_test_result_t fixture_irq_test;
static int fixture_irq_status_result;
static int fixture_irq_line_result;
static int fixture_irq_test_result;
static int fixture_irq_validate_result;
static int fixture_idt_validate_result;
static wait_stats_t fixture_wait_stats;
static wait_queue_info_t fixture_wait_queues[2];
static wait_info_t fixture_waiters[2];
static wait_self_test_result_t fixture_wait_test;
static int fixture_wait_stats_result;
static int fixture_wait_info_result;
static int fixture_waiters_result;
static int fixture_wait_test_result;
static workqueue_stats_t fixture_workq_stats;
static work_info_t fixture_work_records[2];
static workqueue_self_test_result_t fixture_workq_test;
static int fixture_workq_stats_result;
static int fixture_workq_info_result;
static int fixture_workq_test_result;
static int fixture_workq_validate_result;
static int fixture_workq_probe_result;
static uint32_t fixture_wait_queue_count;
static uint32_t fixture_waiter_count;
static uint32_t fixture_work_info_count;

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
    printf("ZCOV_BEGIN|case=host:shell:diagnostics|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:diagnostics|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:diagnostics|value=0x%08X\n",
           (uint32_t)result);
}

static void copy_text(char* output, uint32_t capacity, const char* input) {
    uint32_t index = 0U;

    if (!output || !capacity) return;
    if (!input) {
        output[0] = '\0';
        return;
    }
    while (input[index] && index + 1U < capacity) {
        output[index] = input[index];
        index++;
    }
    output[index] = '\0';
}

static void output_reset(void) {
    video_output_length = 0U;
    video_output[0] = '\0';
}

static void output_append(const char* text) {
    if (!text) return;
    while (*text && video_output_length + 1U < HOST_OUTPUT_CAPACITY) {
        video_output[video_output_length++] = *text++;
    }
    video_output[video_output_length] = '\0';
}

static void fixture_reset(void) {
    output_reset();
    copy_text(fixture_cwd, sizeof(fixture_cwd), "/home/test");
    fixture_last_path[0] = '\0';
    fixture_getcwd_result = OK;
    fixture_chdir_result = OK;
    fixture_mouse_status_result = OK;
    fixture_mouse_speed_result = OK;
    fixture_mouse_primary_result = OK;
    fixture_mouse_acceleration_result = OK;
    fixture_chdir_calls = 0U;
    fixture_speed_calls = 0U;
    fixture_primary_calls = 0U;
    fixture_acceleration_calls = 0U;
    fixture_log_stats_result = OK;
    fixture_log_copy_result = OK;
    fixture_log_set_console_result = OK;
    fixture_log_set_buffer_result = OK;
    fixture_log_test_result = OK;
    fixture_log_copy_count = 0U;
    fixture_log_clear_calls = 0U;
    fixture_log_console_calls = 0U;
    fixture_log_buffer_calls = 0U;
    fixture_log_console_level = LOG_LEVEL_INFO;
    fixture_log_buffer_level = LOG_LEVEL_DEBUG;
    fixture_timer_stats_result = OK;
    fixture_timer_copy_result = OK;
    fixture_timer_test_result = OK;
    fixture_timer_copy_count = 1U;
    fixture_clock_status_result = OK;
    fixture_clock_ticks_result = OK;
    fixture_clock_utc_result = OK;
    fixture_clock_test_result = OK;
    fixture_clock_ticks = 123456789ULL;
    fixture_clock_utc = 1735689600ULL;
    fixture_rtc_status_result = OK;
    fixture_rtc_test_result = OK;
    fixture_irq_status_result = OK;
    fixture_irq_line_result = OK;
    fixture_irq_test_result = OK;
    fixture_irq_validate_result = OK;
    fixture_idt_validate_result = OK;
    fixture_wait_stats_result = OK;
    fixture_wait_info_result = OK;
    fixture_waiters_result = OK;
    fixture_wait_test_result = OK;
    fixture_workq_stats_result = OK;
    fixture_workq_info_result = OK;
    fixture_workq_test_result = OK;
    fixture_workq_validate_result = OK;
    fixture_workq_probe_result = OK;
    fixture_wait_queue_count = 1U;
    fixture_waiter_count = 1U;
    fixture_work_info_count = 1U;
    kmemset(&fixture_log_stats, 0, sizeof(fixture_log_stats));
    kmemset(fixture_log_records, 0, sizeof(fixture_log_records));
    kmemset(&fixture_log_test, 0, sizeof(fixture_log_test));
    fixture_log_stats.capacity = LOG_RECORD_CAPACITY;
    fixture_log_stats.occupancy = 2U;
    fixture_log_stats.console_level = LOG_LEVEL_INFO;
    fixture_log_stats.buffer_level = LOG_LEVEL_DEBUG;
    fixture_log_stats.next_sequence = 12U;
    fixture_log_stats.overwritten_records = 3U;
    fixture_log_stats.grouped_events = 4U;
    fixture_log_stats.truncated_events = 5U;
    fixture_log_stats.dropped_events = 6U;
    fixture_log_stats.clear_count = 7U;
    fixture_log_copy_count = 2U;
    fixture_log_records[0].sequence = 10U;
    fixture_log_records[0].first_tick = 100U;
    fixture_log_records[0].last_tick = 110U;
    fixture_log_records[0].level = LOG_LEVEL_ERROR;
    fixture_log_records[0].error_code = -4;
    fixture_log_records[0].occurrences = 2U;
    fixture_log_records[0].flags = LOG_RECORD_FLAG_HAS_ERROR_CODE;
    copy_text(fixture_log_records[0].module, sizeof(fixture_log_records[0].module),
              "TEST");
    copy_text(fixture_log_records[0].message,
              sizeof(fixture_log_records[0].message), "falha");
    fixture_log_records[1] = fixture_log_records[0];
    fixture_log_records[1].sequence = 11U;
    fixture_log_records[1].level = LOG_LEVEL_DEBUG;
    fixture_log_records[1].flags = 0U;
    fixture_log_test.passed = 8U;
    fixture_log_test.failed = 0U;
    fixture_log_test.order_and_metadata = 1U;
    fixture_log_test.wrap_and_overwrite = 1U;
    fixture_log_test.repetition_grouping = 1U;
    fixture_log_test.safe_truncation = 1U;
    fixture_log_test.optional_error_code = 1U;
    fixture_log_test.clear_behavior = 1U;
    fixture_log_test.text_serialization = 1U;
    fixture_log_test.level_filtering = 1U;
    kmemset(&fixture_timer_stats, 0, sizeof(fixture_timer_stats));
    kmemset(fixture_timer_records, 0, sizeof(fixture_timer_records));
    kmemset(&fixture_timer_test, 0, sizeof(fixture_timer_test));
    fixture_timer_stats.initialized = 1U;
    fixture_timer_stats.current_tick = 1000U;
    fixture_timer_stats.frequency = 100U;
    fixture_timer_stats.occupancy = 1U;
    fixture_timer_stats.capacity = TIMER_CAPACITY;
    fixture_timer_stats.owner_occupancy = 1U;
    fixture_timer_stats.owner_capacity = TIMER_OWNER_CAPACITY;
    fixture_timer_stats.armed = 1U;
    fixture_timer_stats.high_watermark = 2U;
    fixture_timer_stats.timers_created = 3U;
    fixture_timer_stats.timers_started = 4U;
    fixture_timer_stats.expirations = 5U;
    fixture_timer_stats.cancellations = 6U;
    fixture_timer_stats.timers_destroyed = 7U;
    fixture_timer_stats.callbacks = 8U;
    fixture_timer_stats.callback_errors = 1U;
    fixture_timer_stats.delayed_callbacks = 2U;
    fixture_timer_stats.missed_periods = 3U;
    fixture_timer_stats.invalid_operations = 4U;
    fixture_timer_records[0].handle = 0x1234U;
    copy_text(fixture_timer_records[0].owner_name,
              sizeof(fixture_timer_records[0].owner_name), "SHELL");
    copy_text(fixture_timer_records[0].name,
              sizeof(fixture_timer_records[0].name), "heartbeat");
    fixture_timer_records[0].mode = TIMER_MODE_PERIODIC;
    fixture_timer_records[0].state = TIMER_STATE_ARMED;
    fixture_timer_records[0].deadline_tick = 1100U;
    fixture_timer_records[0].period_ticks = 100U;
    fixture_timer_records[0].executions = 9U;
    fixture_timer_records[0].delayed_callbacks = 1U;
    fixture_timer_records[0].missed_periods = 2U;
    fixture_timer_records[0].last_lateness_ticks = 3U;
    fixture_timer_records[0].last_error = -ERR_DISK;
    fixture_timer_test.conversion_and_limits = 1U;
    fixture_timer_test.one_shot = 1U;
    fixture_timer_test.periodic_no_drift = 1U;
    fixture_timer_test.periodic_coalescing = 1U;
    fixture_timer_test.cancel_armed = 1U;
    fixture_timer_test.cancel_pending = 1U;
    fixture_timer_test.owner_destruction = 1U;
    fixture_timer_test.stale_handles = 1U;
    fixture_timer_test.tick_wrap = 1U;
    fixture_timer_test.capacity = 1U;
    fixture_timer_test.callback_errors = 1U;
    fixture_timer_test.invariants = 1U;
    fixture_timer_test.passed = 11U;
    fixture_timer_test.failed = 0U;
    fixture_clock_status.initialized = 1U;
    fixture_clock_status.monotonic_available = 1U;
    fixture_clock_status.utc_available = 1U;
    fixture_clock_status.source = CLOCK_SOURCE_RTC;
    fixture_clock_status.frequency = 100U;
    fixture_clock_status.anchor_monotonic_ticks = 100000ULL;
    fixture_clock_status.anchor_unix_seconds = 1735689600ULL;
    fixture_clock_status.monotonic_wraps = 2ULL;
    fixture_clock_status.reads = 12U;
    fixture_clock_status.last_error = OK;
    fixture_clock_test.epoch_conversion = 1U;
    fixture_clock_test.leap_year_conversion = 1U;
    fixture_clock_test.invalid_date_rejected = 1U;
    fixture_clock_test.monotonic_rollover = 1U;
    fixture_clock_test.invariants = 1U;
    fixture_clock_test.passed = 5U;
    fixture_clock_test.failed = 0U;
    fixture_rtc_status.initialized = 1U;
    fixture_rtc_status.available = 1U;
    fixture_rtc_status.valid = 1U;
    fixture_rtc_status.utc.year = 2025U;
    fixture_rtc_status.utc.month = 1U;
    fixture_rtc_status.utc.day = 1U;
    fixture_rtc_status.utc.hour = 0U;
    fixture_rtc_status.utc.minute = 0U;
    fixture_rtc_status.utc.second = 0U;
    fixture_rtc_test.bcd_conversion = 1U;
    fixture_rtc_test.binary_conversion = 1U;
    fixture_rtc_test.twelve_hour_conversion = 1U;
    fixture_rtc_test.calendar_validation = 1U;
    fixture_rtc_test.invalid_dates_rejected = 1U;
    fixture_rtc_test.passed = 5U;
    fixture_rtc_test.failed = 0U;
    kmemset(&fixture_irq_status, 0, sizeof(fixture_irq_status));
    kmemset(fixture_irq_lines, 0, sizeof(fixture_irq_lines));
    kmemset(fixture_idt_lines, 0, sizeof(fixture_idt_lines));
    kmemset(&fixture_irq_test, 0, sizeof(fixture_irq_test));
    fixture_irq_status.initialized = 1U;
    fixture_irq_status.queued = 2U;
    fixture_irq_status.running = 1U;
    fixture_irq_status.capacity = IRQ_DEFERRED_USABLE_CAPACITY;
    fixture_irq_status.scheduled = 8U;
    fixture_irq_status.dispatched = 7U;
    fixture_irq_status.coalesced = 2U;
    fixture_irq_status.reruns = 1U;
    fixture_irq_status.cancelled = 3U;
    fixture_irq_status.peak_queued = 4U;
    fixture_irq_status.rejected = 1U;
    fixture_irq_status.context_errors = 2U;
    fixture_irq_status.last_error = ERR_INVALID;
    fixture_irq_lines[1].scheduled = 4U;
    fixture_irq_lines[1].dispatched = 3U;
    fixture_irq_lines[1].coalesced = 1U;
    fixture_irq_lines[1].rejected = 2U;
    fixture_irq_lines[1].cancelled = 1U;
    fixture_idt_lines[1].irq_line = 1U;
    fixture_idt_lines[1].registered_handlers = 2U;
    fixture_idt_lines[1].occurrences = 9U;
    fixture_irq_test.lifecycle = 1U;
    fixture_irq_test.coalescing = 1U;
    fixture_irq_test.rerun = 1U;
    fixture_irq_test.cancellation = 1U;
    fixture_irq_test.capacity = 1U;
    fixture_irq_test.attribution = 1U;
    fixture_irq_test.interrupt_context = 1U;
    fixture_irq_test.invariants = 1U;
    fixture_irq_test.passed = 8U;
    fixture_irq_test.failed = 0U;
    kmemset(&fixture_wait_stats, 0, sizeof(fixture_wait_stats));
    kmemset(fixture_wait_queues, 0, sizeof(fixture_wait_queues));
    kmemset(fixture_waiters, 0, sizeof(fixture_waiters));
    kmemset(&fixture_wait_test, 0, sizeof(fixture_wait_test));
    fixture_wait_stats.initialized = 1U;
    fixture_wait_stats.channels_active = 1U;
    fixture_wait_stats.active_waiters = 1U;
    fixture_wait_stats.peak_waiters = 2U;
    fixture_wait_stats.waits_started = 8U;
    fixture_wait_stats.event_wakes = 3U;
    fixture_wait_stats.timeout_wakes = 2U;
    fixture_wait_stats.cancellation_wakes = 1U;
    fixture_wait_stats.unavailable_wakes = 1U;
    fixture_wait_stats.invalid_operations = 2U;
    fixture_wait_stats.registry_capacity = WAIT_QUEUE_REGISTRY_CAPACITY;
    fixture_wait_stats.registry_peak = 4U;
    fixture_wait_stats.registration_rejections = 1U;
    fixture_wait_stats.wake_one_calls = 5U;
    fixture_wait_stats.wake_all_calls = 6U;
    fixture_wait_stats.context_errors = 1U;
    fixture_wait_stats.orphan_errors = 2U;
    fixture_wait_stats.signal_wakes = 3U;
    fixture_wait_queues[0].id = 7U;
    copy_text(fixture_wait_queues[0].owner,
              sizeof(fixture_wait_queues[0].owner), "timer");
    fixture_wait_queues[0].condition = 12U;
    fixture_wait_queues[0].waiters = 1U;
    fixture_wait_queues[0].peak_waiters = 2U;
    fixture_wait_queues[0].available = 1U;
    fixture_waiters[0].id = 23U;
    fixture_waiters[0].queue_id = 7U;
    fixture_waiters[0].queue_position = 0U;
    fixture_waiters[0].target = WAIT_TARGET_THREAD;
    copy_text(fixture_waiters[0].name, sizeof(fixture_waiters[0].name), "worker");
    copy_text(fixture_waiters[0].channel_owner,
              sizeof(fixture_waiters[0].channel_owner), "timer");
    fixture_waiters[0].reason = WAIT_REASON_SIGNAL;
    fixture_waiters[0].deadline_tick = 500U;
    fixture_waiters[0].remaining_ticks = 25U;
    fixture_waiters[0].deadline_active = 1U;
    fixture_waiters[0].active = 1U;
    fixture_wait_test.channel_lifecycle = 1U;
    fixture_wait_test.condition_signal = 1U;
    fixture_wait_test.availability = 1U;
    fixture_wait_test.accounting = 1U;
    fixture_wait_test.reasons = 1U;
    fixture_wait_test.limits = 1U;
    fixture_wait_test.reset = 1U;
    fixture_wait_test.fifo = 1U;
    fixture_wait_test.wake_all = 1U;
    fixture_wait_test.lost_wakeup = 1U;
    fixture_wait_test.condition_recheck = 1U;
    fixture_wait_test.process_thread = 1U;
    fixture_wait_test.interrupt_context = 1U;
    fixture_wait_test.invariants = 1U;
    fixture_wait_test.passed = 14U;
    fixture_wait_test.failed = 0U;
    kmemset(&fixture_workq_stats, 0, sizeof(fixture_workq_stats));
    kmemset(fixture_work_records, 0, sizeof(fixture_work_records));
    kmemset(&fixture_workq_test, 0, sizeof(fixture_workq_test));
    fixture_workq_stats.initialized = 1U;
    fixture_workq_stats.worker_bound = 1U;
    fixture_workq_stats.worker_active = 1U;
    fixture_workq_stats.fallback_active = 0U;
    fixture_workq_stats.worker_pid = 42U;
    fixture_workq_stats.execution_context = WORK_CONTEXT_KWORKER;
    fixture_workq_stats.capacity = WORKQUEUE_CAPACITY;
    fixture_workq_stats.registered = 1U;
    fixture_workq_stats.ready_high = 1U;
    fixture_workq_stats.ready_normal = 2U;
    fixture_workq_stats.delayed = 1U;
    fixture_workq_stats.running = 1U;
    fixture_workq_stats.scheduled = 8U;
    fixture_workq_stats.executed = 4U;
    fixture_workq_stats.coalesced = 2U;
    fixture_workq_stats.reruns = 1U;
    fixture_workq_stats.cancelled = 1U;
    fixture_workq_stats.rejected = 2U;
    fixture_workq_stats.callback_errors = 1U;
    fixture_workq_stats.context_errors = 1U;
    fixture_workq_stats.wake_errors = 1U;
    fixture_workq_stats.wakeups = 6U;
    fixture_workq_stats.sleeps = 3U;
    fixture_workq_stats.peak_pending = 4U;
    fixture_workq_stats.total_callback_ticks = 20U;
    fixture_workq_stats.max_callback_ticks = 8U;
    fixture_work_records[0].id = 0x01000005U;
    fixture_work_records[0].generation = 1U;
    copy_text(fixture_work_records[0].owner,
              sizeof(fixture_work_records[0].owner), "shell");
    fixture_work_records[0].priority = WORK_PRIORITY_HIGH;
    fixture_work_records[0].state = WORK_STATE_RUNNING;
    fixture_work_records[0].deadline_tick = 300U;
    fixture_work_records[0].remaining_ticks = 10U;
    fixture_work_records[0].scheduled = 5U;
    fixture_work_records[0].executed = 4U;
    fixture_work_records[0].coalesced = 1U;
    fixture_work_records[0].reruns = 2U;
    fixture_work_records[0].cancellations = 1U;
    fixture_work_records[0].callback_ticks = 20U;
    fixture_work_records[0].max_callback_ticks = 8U;
    fixture_work_records[0].last_error = -ERR_TIMEOUT;
    fixture_workq_test.lifecycle = 1U;
    fixture_workq_test.fifo = 1U;
    fixture_workq_test.priority = 1U;
    fixture_workq_test.delayed = 1U;
    fixture_workq_test.rollover = 1U;
    fixture_workq_test.promotion = 1U;
    fixture_workq_test.coalescing = 1U;
    fixture_workq_test.rerun = 1U;
    fixture_workq_test.cancellation = 1U;
    fixture_workq_test.capacity = 1U;
    fixture_workq_test.interrupt_context = 1U;
    fixture_workq_test.invariants = 1U;
    fixture_workq_test.passed = 12U;
    fixture_workq_test.failed = 0U;
    kmemset(&fixture_mouse_status, 0, sizeof(fixture_mouse_status));
    fixture_mouse_status.initialized = 1U;
    fixture_mouse_status.x = 12;
    fixture_mouse_status.y = 34;
    fixture_mouse_status.config.speed = 3U;
    fixture_mouse_status.config.primary_button = MOUSE_PRIMARY_LEFT;
}

static int contains_text(const char* text) {
    uint32_t text_length;
    uint32_t output_length;

    if (!text) return 0;
    text_length = kstrlen(text);
    output_length = kstrlen(video_output);
    if (!text_length || text_length > output_length) return 0;
    for (uint32_t offset = 0U;
         offset + text_length <= output_length; offset++) {
        uint32_t index = 0U;

        while (index < text_length &&
               video_output[offset + index] == text[index]) index++;
        if (index == text_length) return 1;
    }
    return 0;
}

static int expect_text(const char* text) {
    if (kstrcmp(video_output, text) == 0) return 0;
    fprintf(stderr, "diagnostics-host: saida inesperada: %s\n", video_output);
    return 1;
}

static int expect_contains(const char* text) {
    if (contains_text(text)) return 0;
    fprintf(stderr, "diagnostics-host: trecho ausente: %s\n", text);
    return 1;
}

void video_print(const char* text, uint8_t color) {
    (void)color;
    output_append(text);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
}

int log_get_stats(log_stats_t* stats) {
    if (!stats) return ERR_NULL;
    if (fixture_log_stats_result != OK) return fixture_log_stats_result;
    *stats = fixture_log_stats;
    return OK;
}

int log_copy_recent(log_record_t* records, uint32_t max_records,
                    uint32_t* count) {
    if (!records || !count) return ERR_NULL;
    if (fixture_log_copy_result != OK) return fixture_log_copy_result;
    if (max_records < fixture_log_copy_count) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < fixture_log_copy_count; index++) {
        records[index] = fixture_log_records[index];
    }
    *count = fixture_log_copy_count;
    return OK;
}

void log_clear_buffer(void) {
    fixture_log_clear_calls++;
}

int log_set_console_level(log_level_t level) {
    fixture_log_console_calls++;
    if (fixture_log_set_console_result == OK) {
        fixture_log_console_level = level;
    }
    return fixture_log_set_console_result;
}

int log_set_buffer_level(log_level_t level) {
    fixture_log_buffer_calls++;
    if (fixture_log_set_buffer_result == OK) {
        fixture_log_buffer_level = level;
    }
    return fixture_log_set_buffer_result;
}

int log_self_test(log_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_log_test;
    return fixture_log_test_result;
}

const char* log_level_str(log_level_t level) {
    if (level == LOG_LEVEL_ERROR) return "ERROR";
    if (level == LOG_LEVEL_WARN) return "WARN";
    if (level == LOG_LEVEL_INFO) return "INFO";
    if (level == LOG_LEVEL_DEBUG) return "DEBUG";
    return "INVALID";
}

int timer_get_stats(timer_stats_t* stats) {
    if (!stats) return ERR_NULL;
    if (fixture_timer_stats_result != OK) return fixture_timer_stats_result;
    *stats = fixture_timer_stats;
    return OK;
}

int timer_copy_active(timer_info_t* output, uint32_t max_timers,
                      uint32_t* out_count) {
    if (!output || !out_count) return ERR_NULL;
    if (fixture_timer_copy_result != OK) return fixture_timer_copy_result;
    if (max_timers < fixture_timer_copy_count) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < fixture_timer_copy_count; index++) {
        output[index] = fixture_timer_records[index];
    }
    *out_count = fixture_timer_copy_count;
    return OK;
}

int timer_self_test(timer_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_timer_test;
    return fixture_timer_test_result;
}

const char* timer_mode_name(timer_mode_t mode) {
    return mode == TIMER_MODE_PERIODIC ? "periodico" : "one-shot";
}

const char* timer_state_name(timer_state_t state) {
    if (state == TIMER_STATE_ARMED) return "armado";
    if (state == TIMER_STATE_PENDING) return "pendente";
    return "ocioso";
}

int clock_get_status(clock_status_t* status) {
    if (!status) return ERR_NULL;
    if (fixture_clock_status_result != OK) return fixture_clock_status_result;
    *status = fixture_clock_status;
    return OK;
}

int clock_get_monotonic_ticks(uint64_t* ticks) {
    if (!ticks) return ERR_NULL;
    if (fixture_clock_ticks_result != OK) return fixture_clock_ticks_result;
    *ticks = fixture_clock_ticks;
    return OK;
}

int clock_get_utc(uint64_t* utc) {
    if (!utc) return ERR_NULL;
    if (fixture_clock_utc_result != OK) return fixture_clock_utc_result;
    *utc = fixture_clock_utc;
    return OK;
}

int clock_self_test(clock_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_clock_test;
    return fixture_clock_test_result;
}

const char* clock_source_name(clock_source_t source) {
    return source == CLOCK_SOURCE_RTC ? "RTC" : "nenhuma";
}

int rtc_get_status(rtc_status_t* status) {
    if (!status) return ERR_NULL;
    if (fixture_rtc_status_result != OK) return fixture_rtc_status_result;
    *status = fixture_rtc_status;
    return OK;
}

int rtc_self_test(rtc_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_rtc_test;
    return fixture_rtc_test_result;
}

int irq_deferred_get_status(irq_deferred_status_t* status) {
    if (!status) return ERR_NULL;
    if (fixture_irq_status_result != OK) return fixture_irq_status_result;
    *status = fixture_irq_status;
    return OK;
}

int irq_deferred_get_irq_status(uint8_t irq_line,
                                irq_deferred_irq_status_t* status) {
    if (!status) return ERR_NULL;
    if (irq_line >= IRQ_DEFERRED_IRQ_COUNT) return ERR_INVALID;
    if (fixture_irq_line_result != OK) return fixture_irq_line_result;
    *status = fixture_irq_lines[irq_line];
    return OK;
}

int irq_deferred_self_test(irq_deferred_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_irq_test;
    return fixture_irq_test_result;
}

int irq_deferred_validate_state(void) {
    return fixture_irq_validate_result;
}

int idt_get_irq_status(uint8_t irq_line, idt_irq_status_t* status) {
    if (!status) return ERR_NULL;
    if (irq_line >= IDT_IRQ_LINE_COUNT) return ERR_INVALID;
    if (fixture_idt_validate_result != OK) return fixture_idt_validate_result;
    *status = fixture_idt_lines[irq_line];
    return OK;
}

int idt_validate_irq_state(void) {
    return fixture_idt_validate_result;
}

int wait_get_stats(wait_stats_t* stats) {
    if (!stats) return ERR_NULL;
    if (fixture_wait_stats_result != OK) return fixture_wait_stats_result;
    *stats = fixture_wait_stats;
    return OK;
}

int wait_queue_copy_info(wait_queue_info_t* output, uint32_t max_entries,
                         uint32_t* out_count) {
    if (!output || !out_count) return ERR_NULL;
    if (fixture_wait_info_result != OK) return fixture_wait_info_result;
    if (max_entries < fixture_wait_queue_count) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < fixture_wait_queue_count; index++) {
        output[index] = fixture_wait_queues[index];
    }
    *out_count = fixture_wait_queue_count;
    return OK;
}

int wait_queue_copy_waiters(wait_info_t* output, uint32_t max_entries,
                            uint32_t* out_count) {
    if (!output || !out_count) return ERR_NULL;
    if (fixture_waiters_result != OK) return fixture_waiters_result;
    if (max_entries < fixture_waiter_count) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < fixture_waiter_count; index++) {
        output[index] = fixture_waiters[index];
    }
    *out_count = fixture_waiter_count;
    return OK;
}

int wait_self_test(wait_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_wait_test;
    return fixture_wait_test_result;
}

int wait_validate_state(void) {
    return fixture_wait_stats_result;
}

const char* wait_reason_name(wait_reason_t reason) {
    if (reason == WAIT_REASON_EVENT) return "evento";
    if (reason == WAIT_REASON_TIMEOUT) return "timeout";
    if (reason == WAIT_REASON_CANCELLED) return "cancelado";
    if (reason == WAIT_REASON_DEVICE_UNAVAILABLE) return "indisponivel";
    if (reason == WAIT_REASON_SIGNAL) return "sinal";
    return "nenhum";
}

uint32_t timer_get_frequency(void) {
    return fixture_timer_stats.frequency;
}

int workqueue_get_stats(workqueue_stats_t* stats) {
    if (!stats) return ERR_NULL;
    if (fixture_workq_stats_result != OK) return fixture_workq_stats_result;
    *stats = fixture_workq_stats;
    return OK;
}

int workqueue_copy_info(work_info_t* output, uint32_t max_entries,
                        uint32_t* out_count) {
    if (!output || !out_count) return ERR_NULL;
    if (fixture_workq_info_result != OK) return fixture_workq_info_result;
    if (max_entries < fixture_work_info_count) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < fixture_work_info_count; index++) {
        output[index] = fixture_work_records[index];
    }
    *out_count = fixture_work_info_count;
    return OK;
}

int workqueue_self_test(workqueue_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_workq_test;
    return fixture_workq_test_result;
}

int workqueue_validate_state(void) {
    return fixture_workq_validate_result;
}

int workqueue_probe_worker(uint32_t timeout_ticks) {
    (void)timeout_ticks;
    return fixture_workq_probe_result;
}

const char* workqueue_priority_name(work_priority_t priority) {
    return priority == WORK_PRIORITY_HIGH ? "alta" : "normal";
}

const char* workqueue_state_name(work_state_t state) {
    if (state == WORK_STATE_READY) return "pronto";
    if (state == WORK_STATE_DELAYED) return "atrasado";
    if (state == WORK_STATE_RUNNING) return "executando";
    return "ocioso";
}

const char* workqueue_context_name(work_context_t context) {
    if (context == WORK_CONTEXT_KWORKER) return "kworker";
    if (context == WORK_CONTEXT_SYSTEM_FALLBACK) return "fallback";
    return "nenhum";
}

int vfs_getcwd(char* path, uint32_t capacity) {
    if (!path || !capacity) return ERR_NULL;
    if (fixture_getcwd_result != OK) return fixture_getcwd_result;
    if (kstrlen(fixture_cwd) + 1U > capacity) return ERR_OVERFLOW;
    copy_text(path, capacity, fixture_cwd);
    return OK;
}

int vfs_chdir(const char* path) {
    if (!path) return ERR_NULL;
    fixture_chdir_calls++;
    copy_text(fixture_last_path, sizeof(fixture_last_path), path);
    return fixture_chdir_result;
}

int mouse_get_status(mouse_status_t* status) {
    if (!status) return ERR_NULL;
    if (fixture_mouse_status_result != OK) return fixture_mouse_status_result;
    *status = fixture_mouse_status;
    return OK;
}

int mouse_set_speed(uint8_t speed) {
    fixture_speed_calls++;
    if (fixture_mouse_speed_result == OK) fixture_mouse_status.config.speed = speed;
    return fixture_mouse_speed_result;
}

int mouse_set_primary_button(mouse_primary_button_t primary_button) {
    fixture_primary_calls++;
    if (fixture_mouse_primary_result == OK) {
        fixture_mouse_status.config.primary_button = primary_button;
    }
    return fixture_mouse_primary_result;
}

int mouse_set_acceleration(int enabled) {
    fixture_acceleration_calls++;
    if (fixture_mouse_acceleration_result == OK) {
        fixture_mouse_status.config.acceleration_enabled = enabled ? 1U : 0U;
    }
    return fixture_mouse_acceleration_result;
}

static int test_pwd(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_pwd("");
    failures += expect_text("/home/test\n");
    fixture_reset();
    shell_dispatch_cmd_pwd("extra");
    failures += expect_text("Uso: pwd\n");
    fixture_reset();
    fixture_getcwd_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_pwd(0);
    failures += expect_text("Erro: diretorio atual indisponivel.\n");
    return failures;
}

static int test_cd(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_cd("");
    if (fixture_chdir_calls != 1U || kstrcmp(fixture_last_path, "/") != 0) {
        fprintf(stderr, "diagnostics-host: cd padrao nao chamou raiz\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_cd("/var/log");
    if (fixture_chdir_calls != 1U ||
        kstrcmp(fixture_last_path, "/var/log") != 0 || video_output[0]) {
        fprintf(stderr, "diagnostics-host: cd valido inesperado\n");
        failures++;
    }
    fixture_reset();
    fixture_chdir_result = ERR_NOT_FOUND;
    shell_dispatch_cmd_cd("missing");
    failures += expect_text("Erro: cd recusado (codigo 4).\n");
    return failures;
}

static int test_mouse(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_mouse("");
    failures += expect_contains("Mouse PS/2:\n");
    failures += expect_contains("Posicao: 12,34\n");
    fixture_reset();
    fixture_mouse_status.initialized = 0U;
    fixture_mouse_status.config.acceleration_enabled = 1U;
    fixture_mouse_status.config.primary_button = MOUSE_PRIMARY_RIGHT;
    fixture_mouse_status.wheel_supported = 1U;
    shell_dispatch_cmd_mouse("speed 7");
    if (fixture_speed_calls != 1U || fixture_mouse_status.config.speed != 7U) {
        fprintf(stderr, "diagnostics-host: speed valido nao aplicado\n");
        failures++;
    }
    failures += expect_contains("Preferencia do mouse aplicada em RAM.\n");
    fixture_reset();
    shell_dispatch_cmd_mouse("primary right");
    if (fixture_primary_calls != 1U ||
        fixture_mouse_status.config.primary_button != MOUSE_PRIMARY_RIGHT) {
        fprintf(stderr, "diagnostics-host: primary right nao aplicado\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_mouse("primary left");
    if (fixture_primary_calls != 1U ||
        fixture_mouse_status.config.primary_button != MOUSE_PRIMARY_LEFT) {
        fprintf(stderr, "diagnostics-host: primary left nao aplicado\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_mouse("acceleration on");
    if (fixture_acceleration_calls != 1U ||
        !fixture_mouse_status.config.acceleration_enabled) {
        fprintf(stderr, "diagnostics-host: acceleration on nao aplicado\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_mouse("acceleration off");
    if (fixture_acceleration_calls != 1U ||
        fixture_mouse_status.config.acceleration_enabled) {
        fprintf(stderr, "diagnostics-host: acceleration off nao aplicado\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_mouse("speed 0");
    failures += expect_contains("preferencia invalida; estado preservado");
    fixture_reset();
    shell_dispatch_cmd_mouse("primary middle");
    failures += expect_contains("preferencia invalida; estado preservado");
    fixture_reset();
    shell_dispatch_cmd_mouse("unknown value");
    failures += expect_contains("preferencia invalida; estado preservado");
    fixture_reset();
    shell_dispatch_cmd_mouse("speed 4 extra");
    failures += expect_text("Uso: mouse | mouse speed <1-10> | mouse primary <left|right> | mouse acceleration <on|off>\n");
    fixture_reset();
    fixture_mouse_speed_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_mouse("speed 4");
    failures += expect_contains("driver de mouse indisponivel");
    fixture_reset();
    fixture_mouse_status_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_mouse(0);
    failures += expect_text("Erro: status do mouse indisponivel.\n");
    return failures;
}

static int test_log(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_log("status");
    failures += expect_contains("Log circular: 2/32 registros\n");
    failures += expect_contains("Niveis do log: console=info buffer=debug\n");
    fixture_reset();
    fixture_log_stats_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_log("");
    failures += expect_text("Erro: estatisticas do log indisponiveis.\n");
    fixture_reset();
    shell_dispatch_cmd_log("clear");
    if (fixture_log_clear_calls != 1U ||
        expect_text("Log circular limpo.\n")) failures++;
    fixture_reset();
    fixture_log_copy_count = 0U;
    shell_dispatch_cmd_log("tail");
    failures += expect_text("Log vazio.\n");
    fixture_reset();
    shell_dispatch_cmd_log("tail 2");
    failures += expect_contains("seq=10 ticks=100..110 [ERROR] [TEST] falha");
    failures += expect_contains("erro=-4");
    failures += expect_contains("seq=11 ticks=100..110 [DEBUG] [TEST] falha");
    fixture_reset();
    fixture_log_copy_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_log("tail 1");
    failures += expect_text("Erro: historico do log indisponivel.\n");
    fixture_reset();
    shell_dispatch_cmd_log("tail 33");
    failures += expect_contains("Uso: log [status|tail [1-16]|clear|level|check]");
    fixture_reset();
    shell_dispatch_cmd_log("level");
    failures += expect_contains("Niveis do log: console=info buffer=debug\n");
    fixture_reset();
    shell_dispatch_cmd_log("level console debug");
    if (fixture_log_console_calls != 1U ||
        fixture_log_console_level != LOG_LEVEL_DEBUG) {
        fprintf(stderr, "diagnostics-host: nivel console nao aplicado\n");
        failures++;
    }
    failures += expect_contains("Nivel console alterado para debug.\n");
    fixture_reset();
    fixture_log_set_buffer_result = ERR_INVALID;
    shell_dispatch_cmd_log("level buffer error");
    failures += expect_contains("Erro: o buffer deve ser tao detalhado quanto o console.\n");
    fixture_reset();
    shell_dispatch_cmd_log("level invalid info");
    failures += expect_contains("Uso: log [status|tail [1-16]|clear|level|check]");
    fixture_reset();
    shell_dispatch_cmd_log("check");
    failures += expect_contains("Resultado: OK (8 aprovados, 0 falhos)\n");
    fixture_reset();
    fixture_log_test_result = ERR_STATE;
    fixture_log_test.failed = 1U;
    shell_dispatch_cmd_log("check");
    failures += expect_contains("Resultado: ERRO (8 aprovados, 1 falhos)\n");
    fixture_reset();
    shell_dispatch_cmd_log("unknown");
    failures += expect_contains("Uso: log [status|tail [1-16]|clear|level|check]");
    return failures;
}

static int test_timer(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_timer("status");
    failures += expect_contains("Servico de timers: tick=1000 frequencia=100 Hz\n");
    failures += expect_contains("Proprietarios: 1/16  Timers: 1/32");
    failures += expect_contains("Callbacks: 8  Erros: 1  Atrasos: 2");
    fixture_reset();
    fixture_timer_stats_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_timer("");
    failures += expect_text("Erro: estatisticas de timer indisponiveis.\n");
    fixture_reset();
    fixture_timer_copy_count = 0U;
    shell_dispatch_cmd_timer("list");
    failures += expect_text("Nenhum timer criado.\n");
    fixture_reset();
    shell_dispatch_cmd_timer("list");
    failures += expect_contains("handle=0x00001234 owner=SHELL timer=heartbeat\n");
    failures += expect_contains("modo=periodico estado=armado prazo=1100 periodo=100");
    failures += expect_contains("execucoes=9 atrasos=1 perdidos=2 ultimo_atraso=3 erro=-3");
    fixture_reset();
    fixture_timer_copy_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_timer("list");
    failures += expect_text("Erro: lista de timers indisponivel.\n");
    fixture_reset();
    shell_dispatch_cmd_timer("check");
    failures += expect_contains("Autoteste de timers (tabelas privadas):\n");
    failures += expect_contains("Resultado: OK (11 aprovados, 0 falhos)\n");
    fixture_reset();
    fixture_timer_test_result = ERR_STATE;
    fixture_timer_test.failed = 1U;
    shell_dispatch_cmd_timer("check");
    failures += expect_contains("Resultado: ERRO (11 aprovados, 1 falhos)\n");
    fixture_reset();
    shell_dispatch_cmd_timer("invalid");
    failures += expect_text("Uso: timer [status|list|check]\n");
    return failures;
}

static int test_clock(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_clock("status");
    failures += expect_contains("Clock: inicializado=SIM fonte=RTC PIT=100 Hz UTC=READY wraps=2 reads=12 erro=0\n");
    failures += expect_contains("anchor_tick=100000 anchor_utc=1735689600\n");
    failures += expect_contains("monotono_ticks=123456789\n");
    failures += expect_contains("utc_unix_seconds=1735689600\n");
    failures += expect_contains("RTC=READY 2025-1-1 0:0:0\n");
    fixture_reset();
    fixture_clock_status.source = CLOCK_SOURCE_NONE;
    fixture_clock_status.utc_available = 0U;
    fixture_clock_ticks_result = ERR_UNAVAILABLE;
    fixture_clock_utc_result = ERR_UNAVAILABLE;
    fixture_rtc_status.valid = 0U;
    shell_dispatch_cmd_clock("");
    failures += expect_contains("fonte=nenhuma PIT=100 Hz UTC=UNAVAILABLE");
    failures += expect_contains("RTC=UNAVAILABLE\n");
    fixture_reset();
    fixture_clock_status_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_clock("status");
    failures += expect_text("Erro: estado do clock indisponivel.\n");
    fixture_reset();
    shell_dispatch_cmd_clock("check");
    failures += expect_contains("Autoteste RTC/clock:\n");
    failures += expect_contains("Resultado: OK\n");
    fixture_reset();
    fixture_rtc_test_result = ERR_STATE;
    fixture_clock_test.failed = 1U;
    shell_dispatch_cmd_clock("check");
    failures += expect_contains("Resultado: ERRO\n");
    fixture_reset();
    shell_dispatch_cmd_clock("invalid");
    failures += expect_text("Uso: clock [status|check]\n");
    return failures;
}

static int test_irqstat(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_irqstat("status");
    failures += expect_contains("Bottom-Half: fila=2/31 executando=1 pico=4\n");
    failures += expect_contains("Agendados=8 executados=7 coalescidos=2 reexecucoes=1\n");
    failures += expect_contains("Cancelados=3 rejeitados=1 contexto_invalido=2\n");
    fixture_reset();
    fixture_irq_status_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_irqstat("");
    failures += expect_text("Erro: fila Bottom-Half indisponivel.\n");
    fixture_reset();
    shell_dispatch_cmd_irqstat("list");
    failures += expect_contains("IRQs PIC ativas:\n  IRQ1 ocorrencias=9 handlers=2 BH=3/4 coalescidos=1 rejeitados=2\n");
    fixture_reset();
    fixture_irq_line_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_irqstat("list");
    failures += expect_text("IRQs PIC ativas:\nErro ao consultar linha IRQ.\n");
    fixture_reset();
    shell_dispatch_cmd_irqstat("check");
    failures += expect_contains("Autoteste de Bottom-Half (fila privada):\n");
    failures += expect_contains("  ciclo: OK\n  coalescencia: OK\n");
    failures += expect_contains("Resultado: OK\n");
    fixture_reset();
    fixture_irq_test_result = ERR_STATE;
    fixture_irq_test.invariants = 0U;
    fixture_irq_test.failed = 1U;
    shell_dispatch_cmd_irqstat("check");
    failures += expect_contains("  invariantes: ERRO\nResultado: ERRO\n");
    fixture_reset();
    fixture_irq_validate_result = ERR_STATE;
    shell_dispatch_cmd_irqstat("check");
    failures += expect_contains("Resultado: ERRO\n");
    fixture_reset();
    shell_dispatch_cmd_irqstat("invalid");
    failures += expect_text("Uso: irqstat [status|list|check]\n");
    return failures;
}

static int test_wait(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_wait("status");
    failures += expect_contains("Servico de espera: canais=1 waiters=1 pico=2\n");
    failures += expect_contains("Inicios=8 eventos=3 timeouts=2 cancelamentos=1 indisponiveis=1\n");
    failures += expect_contains("Operacoes invalidas=2 registro=1/128 pico=4 rejeicoes=1\n");
    failures += expect_contains("Wake um/todos=5/6 contexto/orfaos=1/2\n");
    fixture_reset();
    fixture_wait_stats_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_wait("");
    failures += expect_text("Erro: estatisticas de espera indisponiveis.\n");
    fixture_reset();
    fixture_waiter_count = 0U;
    shell_dispatch_cmd_wait("list");
    failures += expect_text("Nenhuma tarefa bloqueada por canal.\n");
    fixture_reset();
    shell_dispatch_cmd_wait("list");
    failures += expect_text("Tarefas bloqueadas por canal:\nthread=23 fila=7 pos=0 nome=worker canal=timer motivo=sinal restante=25\n");
    fixture_reset();
    fixture_waiters_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_wait("list");
    failures += expect_text("Erro: lista de esperas indisponivel.\n");
    fixture_reset();
    shell_dispatch_cmd_wait("check");
    failures += expect_contains("Autoteste de esperas (canal privado):\n");
    failures += expect_contains("  ciclo do canal: OK");
    failures += expect_contains("  wake all: OK");
    failures += expect_contains("Resultado: OK (14 aprovados, 0 falhos)\n");
    fixture_reset();
    fixture_wait_test_result = ERR_STATE;
    fixture_wait_test.invariants = 0U;
    fixture_wait_test.failed = 1U;
    shell_dispatch_cmd_wait("check");
    failures += expect_contains("  invariantes: ERRO\nResultado: ERRO (14 aprovados, 1 falhos)\n");
    fixture_reset();
    shell_dispatch_cmd_wait("invalid");
    failures += expect_text("Uso: wait [status|list|check]\n");
    return failures;
}

static int test_wqinfo(void) {
    int failures = 0;

    fixture_reset();
    fixture_waiter_count = 0U;
    shell_dispatch_cmd_wqinfo("");
    failures += expect_contains("Filas de espera registradas:\n  #7 timer estado=DISPONIVEL geracao=12 waiters/pico=1/2\n");
    failures += expect_contains("Nenhum waiter bloqueado.\n");
    fixture_reset();
    shell_dispatch_cmd_wqinfo("");
    failures += expect_contains("Ordem FIFO dos waiters:\nthread=23 fila=7 pos=0 nome=worker canal=timer motivo=sinal restante=25\n");
    fixture_reset();
    fixture_wait_info_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_wqinfo("");
    failures += expect_text("Erro: filas de espera indisponiveis.\n");
    fixture_reset();
    shell_dispatch_cmd_wqinfo("extra");
    failures += expect_text("Uso: wqinfo\n");
    return failures;
}

static int test_workq(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_workq("status");
    failures += expect_contains("Workqueue: contexto=kworker worker_pid=42 ativo=1 fallback=0\n");
    failures += expect_contains("Registro=1/64 prontos H/N=1/2 atrasados=1 executando=1 pico=4\n");
    failures += expect_contains("Agendados/executados=8/4 coalescidos=2 reexecucoes=1 cancelados=1\n");
    failures += expect_contains("Duracao ticks media/max=5/8 sub-tick=N/D\n");
    fixture_reset();
    fixture_workq_stats_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_workq("");
    failures += expect_text("Erro: workqueue indisponivel.\n");
    fixture_reset();
    fixture_work_info_count = 0U;
    shell_dispatch_cmd_workq("list");
    failures += expect_text("Trabalhos registrados:\n");
    fixture_reset();
    shell_dispatch_cmd_workq("list");
    failures += expect_contains("#16777221 shell geracao=1 prioridade=alta estado=executando restante=10 prazo=300 exec/agend=4/5 coalesc=1 erro=4294967288\n");
    failures += expect_contains("reexec/cancel=2/1 ticks total/max=20/8\n");
    fixture_reset();
    fixture_workq_info_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_workq("list");
    failures += expect_text("Erro: snapshot da workqueue indisponivel.\n");
    fixture_reset();
    shell_dispatch_cmd_workq("check");
    failures += expect_contains("Autoteste da workqueue (fixture privada):\n");
    failures += expect_contains("  ciclo: OK");
    failures += expect_contains("  interrupcoes habilitadas: OK");
    failures += expect_contains("Resultado: OK\n");
    fixture_reset();
    fixture_workq_test_result = ERR_STATE;
    fixture_workq_test.invariants = 0U;
    fixture_workq_test.failed = 1U;
    shell_dispatch_cmd_workq("check");
    failures += expect_contains("  invariantes: ERRO\nResultado: ERRO\n");
    fixture_reset();
    fixture_workq_validate_result = ERR_STATE;
    shell_dispatch_cmd_workq("check");
    failures += expect_contains("Resultado: ERRO\n");
    fixture_reset();
    fixture_workq_probe_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_workq("check");
    failures += expect_contains("wake Shell/kworker sem perda: ERRO");
    failures += expect_contains("Resultado: ERRO");
    fixture_reset();
    shell_dispatch_cmd_workq("invalid");
    failures += expect_text("Uso: workq [status|list|check]\n");
    return failures;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = test_pwd() + test_cd() + test_mouse() + test_log() +
             test_timer() + test_clock() + test_irqstat() + test_wait() +
             test_wqinfo() + test_workq();
    coverage_active = 0U;
    coverage_emit(result);
    return result ? 1 : 0;
}
