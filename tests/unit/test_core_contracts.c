#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/clock.h"
#include "core/errors.h"
#include "core/log.h"
#include "test_protocol_core.h"
#include "drivers/rtc.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

static void __attribute__((no_instrument_function))
coverage_record(void* function) {
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
    printf("ZCOV_BEGIN|case=host:core:contracts|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:contracts|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:contracts|value=0x%08X\n",
           (uint32_t)result);
}

static uint32_t fake_tick = 100U;
static uint8_t fake_rtc_available;
static rtc_datetime_t fake_rtc_datetime;

#define PROTOCOL_EVENT_CAPACITY 16U

typedef struct {
    test_protocol_event_t events[PROTOCOL_EVENT_CAPACITY];
    uint32_t event_count;
    uint32_t run_count;
} protocol_test_context_t;

static int protocol_emit(const test_protocol_event_t* event, void* context) {
    protocol_test_context_t* test = (protocol_test_context_t*)context;

    if (!event || !test || test->event_count >= PROTOCOL_EVENT_CAPACITY) {
        return ERR_OVERFLOW;
    }
    test->events[test->event_count++] = *event;
    return OK;
}

static int protocol_run_case(void* context, const char* case_id,
                             uint32_t case_length, uint32_t iteration,
                             uint32_t seed) {
    protocol_test_context_t* test = (protocol_test_context_t*)context;
    const char expected[] = "qemu:tst4:memory-slab";

    if (!test || !case_id || case_length != sizeof(expected) - 1U ||
        iteration != 1U || seed != 42U) return ERR_INVALID;
    for (uint32_t index = 0U; index < case_length; index++) {
        if (case_id[index] != expected[index]) return ERR_INVALID;
    }
    test->run_count++;
    return OK;
}

static uint32_t protocol_crc(const char* text, uint32_t length) {
    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t index = 0U; index < length; index++) {
        crc ^= (uint8_t)text[index];
        for (uint32_t bit = 0U; bit < 8U; bit++) {
            crc = (crc >> 1U) ^
                  ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }
    return ~crc;
}

static int protocol_feed_frame(test_protocol_core_t* core, const char* frame,
                                uint32_t length) {
    int result = OK;

    for (uint32_t index = 0U; index < length; index++) {
        result = test_protocol_core_feed_byte(core, (uint8_t)frame[index]);
        if (result != OK && frame[index] != '\n') return result;
    }
    return result;
}

static uint32_t protocol_make_frame(char* output, uint32_t capacity,
                                    const char* fields) {
    int length;
    uint32_t crc;

    if (!output || !fields || capacity < 2U) return 0U;
    length = snprintf(output, capacity, "@@ZTEST/1 %s", fields);
    if (length < 0 || (uint32_t)length >= capacity) return 0U;
    crc = protocol_crc(output, (uint32_t)length);
    length += snprintf(output + length, capacity - (uint32_t)length,
                       " crc=%08X\n", crc);
    if (length < 0 || (uint32_t)length >= capacity) return 0U;
    return (uint32_t)length;
}

static int test_protocol_core_contract(void) {
    protocol_test_context_t test = {0};
    test_protocol_core_callbacks_t callbacks = {
        protocol_emit, protocol_run_case, &test
    };
    test_protocol_core_t core;
    char frame[256];
    uint32_t length;
    test_protocol_event_t formatted_event;
    char formatted[256];

    if (test_protocol_core_set_ticks(&core, 1U) != ERR_STATE) return 20;
    if (test_protocol_core_init(&core, &callbacks) != OK) return 21;
    if (test_protocol_core_is_active(&core)) return 22;
    if (test_protocol_core_set_case_reason(&core, "ERR_TIMEOUT") != OK) {
        return 23;
    }
    if (test_protocol_core_set_boot_ready(&core) != OK) return 24;
    if (test_protocol_core_poll(&core, 10U) != OK || test.event_count != 0U) {
        return 24;
    }
    length = protocol_make_frame(frame, sizeof(frame),
                                 "cmd=HELLO run=run-core seq=1");
    if (!length || protocol_feed_frame(&core, frame, length) != OK) return 25;
    if (test_protocol_core_poll(&core, 10U) != OK || test.event_count != 1U ||
        test.events[0].type != TEST_PROTOCOL_EVENT_READY ||
        !test_protocol_core_is_active(&core)) return 26;
    length = protocol_make_frame(frame, sizeof(frame),
                                 "cmd=PING run=run-core seq=2");
    if (!length || protocol_feed_frame(&core, frame, length) != OK ||
        test.event_count != 2U ||
        test.events[1].type != TEST_PROTOCOL_EVENT_HEARTBEAT) return 27;
    length = protocol_make_frame(frame, sizeof(frame),
                                 "cmd=RUN run=run-core seq=3 case=qemu:tst4:memory-slab iteration=1 seed=42");
    if (!length || protocol_feed_frame(&core, frame, length) != OK ||
        test.run_count != 1U || test.event_count != 4U ||
        test.events[2].type != TEST_PROTOCOL_EVENT_BEGIN ||
        test.events[3].type != TEST_PROTOCOL_EVENT_PASS) return 28;
    formatted_event = test.events[3];
    if (!test_protocol_core_format_event(&formatted_event, formatted,
                                         sizeof(formatted)) ||
        !strstr(formatted, "event=PASS") || !strstr(formatted, "crc=")) {
        return 29;
    }
    length = protocol_make_frame(frame, sizeof(frame),
                                 "cmd=ABORT run=run-core seq=4");
    if (!length || protocol_feed_frame(&core, frame, length) != OK ||
        test.event_count != 5U ||
        test.events[4].type != TEST_PROTOCOL_EVENT_BLOCKED ||
        strcmp(test.events[4].reason, "ERR_CANCELLED") != 0) return 30;
    frame[10] = frame[10] == 'A' ? 'B' : 'A';
    if (protocol_feed_frame(&core, frame, length) != ERR_INVALID ||
        test.event_count != 6U ||
        test.events[5].type != TEST_PROTOCOL_EVENT_BLOCKED) return 31;
    if (test_protocol_core_emit(&core, TEST_PROTOCOL_EVENT_NONE, 0) !=
        ERR_INVALID) return 32;
    return 0;
}

uint32_t timer_get_ticks(void) {
    return fake_tick++;
}

uint32_t timer_get_frequency(void) {
    return 1000U;
}

int rtc_read_utc(rtc_datetime_t* out_datetime) {
    if (!out_datetime) return ERR_NULL;
    if (!fake_rtc_available) return ERR_UNAVAILABLE;
    *out_datetime = fake_rtc_datetime;
    return OK;
}

int serial_is_ready(void) {
    return 1;
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

int main(void) {
    log_self_test_result_t log_result;
    clock_self_test_result_t clock_result;
    clock_status_t clock_status;
    uint64_t monotonic_ticks;
    char log_buffer[256];
    int result = 0;

    coverage_active = 1U;
    log_init();
    if (log_self_test(&log_result) != OK || log_result.failed != 0U ||
        log_result.passed != 8U) result = 1;
    if (!result && log_set_buffer_level((log_level_t)99) != ERR_INVALID) {
        result = 2;
    }
    if (!result && log_get_buffer(NULL, sizeof(log_buffer)) != ERR_NULL) {
        result = 3;
    }
    if (!result && log_get_stats(NULL) != ERR_NULL) result = 4;
    if (!result && log_get_buffer(log_buffer, sizeof(log_buffer)) < 0) {
        result = 5;
    }

    if (!result && clock_init() != ERR_UNAVAILABLE) result = 6;
    if (!result && (clock_get_status(&clock_status) != OK ||
        !clock_status.initialized || !clock_status.monotonic_available ||
        clock_status.utc_available)) result = 7;
    if (!result && clock_get_utc(&monotonic_ticks) != ERR_UNAVAILABLE) {
        result = 8;
    }
    if (!result && clock_get_monotonic_ticks(&monotonic_ticks) != OK) {
        result = 9;
    }
    if (!result && (clock_self_test(&clock_result) != OK ||
        clock_result.failed != 0U || clock_result.passed != 5U)) result = 10;
    if (!result && clock_validate_state() != OK) result = 11;
    if (!result) {
        uint64_t unix_seconds = 1U;

        fake_rtc_datetime.year = 1970U;
        fake_rtc_datetime.month = 1U;
        fake_rtc_datetime.day = 1U;
        fake_rtc_datetime.hour = 0U;
        fake_rtc_datetime.minute = 0U;
        fake_rtc_datetime.second = 0U;
        fake_rtc_available = 1U;
        fake_tick = 500U;
        if (clock_init() != OK || clock_get_utc(&unix_seconds) != OK ||
            unix_seconds != 0U || clock_validate_state() != OK) {
            result = 12;
        }
    }
    if (!result) result = test_protocol_core_contract();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
