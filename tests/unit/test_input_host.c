#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/input.h"
#include "core/log.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_KEY_QUEUE_LIMIT (INPUT_KEY_QUEUE_CAPACITY - 1U)

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static input_key_event_t last_key;
static input_pointer_event_t last_pointer;
static uint32_t key_received;
static uint32_t pointer_received;
static int key_sink_result;
static int pointer_sink_result;

uint32_t timer_get_ticks(void) {
    return 100U;
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

static void __attribute__((no_instrument_function))
coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:core:input|value=0x%08X\n", coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:input|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:input|value=0x%08X\n", (uint32_t)result);
}

static int key_sink(const input_key_event_t* event) {
    if (!event) return ERR_NULL;
    last_key = *event;
    key_received++;
    return key_sink_result;
}

static int alternate_key_sink(const input_key_event_t* event) {
    (void)event;
    return OK;
}

static int pointer_sink(const input_pointer_event_t* event) {
    if (!event) return ERR_NULL;
    last_pointer = *event;
    pointer_received++;
    return pointer_sink_result;
}

static int alternate_pointer_sink(const input_pointer_event_t* event) {
    (void)event;
    return OK;
}

static input_key_event_t make_key(uint16_t usage, uint8_t pressed) {
    input_key_event_t event = {usage, pressed, 0U, INPUT_SOURCE_PS2};
    return event;
}

static input_pointer_event_t make_pointer(int32_t dx, int32_t dy,
                                          int8_t wheel, uint8_t buttons) {
    input_pointer_event_t event = {dx, dy, wheel, buttons, INPUT_SOURCE_PS2};
    return event;
}

static int test_uninitialized(void) {
    input_metrics_t metrics;
    uint32_t processed = 0U;

    if (input_publish_key(NULL) != ERR_NULL ||
        input_publish_pointer(NULL) != ERR_NULL ||
        input_register_key_sink(NULL) != ERR_NULL ||
        input_register_pointer_sink(NULL) != ERR_NULL ||
        input_dispatch(1U, &processed) != ERR_STATE ||
        input_dispatch(1U, NULL) != ERR_NULL ||
        input_get_metrics(NULL) != ERR_NULL ||
        input_get_metrics(&metrics) != ERR_STATE ||
        input_validate_state() != ERR_STATE) return 1;
    return 0;
}

static int test_registration_and_dispatch(void) {
    input_key_event_t key = make_key(INPUT_USAGE_A, 1U);
    input_pointer_event_t pointer = make_pointer(4, -3, 0, 1U);
    input_metrics_t metrics;
    uint32_t processed = 0U;

    if (input_init() != OK || input_init() != OK ||
        input_register_key_sink(key_sink) != OK ||
        input_register_key_sink(key_sink) != OK ||
        input_register_key_sink(alternate_key_sink) != ERR_STATE ||
        input_register_pointer_sink(pointer_sink) != OK ||
        input_register_pointer_sink(pointer_sink) != OK ||
        input_register_pointer_sink(alternate_pointer_sink) != ERR_STATE) {
        return 10;
    }
    if (input_publish_key(&key) != OK || input_publish_pointer(&pointer) != OK ||
        input_dispatch(0U, &processed) != OK || processed != 0U ||
        input_dispatch(2U, &processed) != OK || processed != 2U ||
        key_received != 1U || pointer_received != 1U ||
        last_key.usage != INPUT_USAGE_A || last_pointer.dx != 4 ||
        last_pointer.dy != -3) return 11;
    if (input_get_metrics(&metrics) != OK || metrics.key_queued != 0U ||
        metrics.pointer_queued != 0U || metrics.key_processed != 1U ||
        metrics.pointer_processed != 1U || input_validate_state() != OK) {
        return 12;
    }
    key_sink_result = ERR_CANCELLED;
    if (input_publish_key(&key) != OK || input_dispatch(1U, &processed) != OK ||
        processed != 0U || metrics.key_queued != 0U) {
        return 13;
    }
    key_sink_result = OK;
    if (input_dispatch(1U, &processed) != OK || processed != 1U) return 14;
    return 0;
}

static int test_pointer_coalescing_and_bounds(void) {
    input_pointer_event_t first = make_pointer(20000, -20000, 0, 2U);
    input_pointer_event_t second = make_pointer(20000, -20000, 0, 2U);
    input_pointer_event_t wheel = make_pointer(1, 2, 1, 2U);
    input_metrics_t metrics;
    uint32_t processed = 0U;

    if (input_publish_pointer(&first) != OK ||
        input_publish_pointer(&second) != OK ||
        input_publish_pointer(&wheel) != OK ||
        input_get_metrics(&metrics) != OK || metrics.pointer_queued != 2U ||
        metrics.pointer_published != 4U) return 20;
    if (input_dispatch(1U, &processed) != OK || processed != 1U ||
        last_pointer.dx != 32767 || last_pointer.dy != -32767 ||
        last_pointer.wheel != 0) return 20;
    if (input_dispatch(1U, &processed) != OK || processed != 1U ||
        last_pointer.wheel != 1) return 21;
    if (input_publish_pointer(&first) != OK ||
        input_publish_pointer(&second) != OK ||
        input_publish_pointer(&wheel) != OK) return 22;
    for (uint32_t index = 0U; index < INPUT_POINTER_QUEUE_CAPACITY - 3U;
         index++) {
        if (input_publish_pointer(&wheel) != OK) return 23;
    }
    if (input_publish_pointer(&wheel) != ERR_OVERFLOW ||
        input_get_metrics(&metrics) != OK || metrics.pointer_dropped != 1U ||
        metrics.pointer_queued != INPUT_POINTER_QUEUE_CAPACITY - 1U) return 24;
    if (input_dispatch(INPUT_POINTER_QUEUE_CAPACITY, &processed) != OK ||
        processed != INPUT_POINTER_QUEUE_CAPACITY - 1U ||
        input_validate_state() != OK) return 25;
    return 0;
}

static int test_key_queue_limit(void) {
    input_key_event_t key = make_key(INPUT_USAGE_B, 1U);
    input_metrics_t metrics;
    uint32_t processed = 0U;

    for (uint32_t index = 0U; index < HOST_KEY_QUEUE_LIMIT; index++) {
        if (input_publish_key(&key) != OK) return 30;
    }
    if (input_publish_key(&key) != ERR_OVERFLOW ||
        input_get_metrics(&metrics) != OK || metrics.key_queued != HOST_KEY_QUEUE_LIMIT ||
        metrics.key_dropped != 1U || input_dispatch(HOST_KEY_QUEUE_LIMIT + 1U,
                                                     &processed) != OK ||
        processed != HOST_KEY_QUEUE_LIMIT || input_validate_state() != OK) {
        return 31;
    }
    return 0;
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    log_init();
    if (!result) result = test_uninitialized();
    if (!result) result = test_registration_and_dispatch();
    if (!result) result = test_pointer_coalescing_and_bounds();
    if (!result) result = test_key_queue_limit();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        printf("INPUT_HOST_FAIL:%d\n", result);
        return result;
    }
    printf("INPUT_HOST_PASS\n");
    return 0;
}
