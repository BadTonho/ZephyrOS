#include "test_protocol_core.h"

#include <stdio.h>
#include <string.h>

#define TEST_EVENT_CAPACITY 32U
#define TEST_OUTPUT_CAPACITY 1024U

typedef struct {
    test_protocol_event_t events[TEST_EVENT_CAPACITY];
    uint32_t event_count;
    uint32_t run_count;
    int run_result;
    int set_reason_during_run;
    test_protocol_core_t* core;
    char last_case[TEST_PROTOCOL_CASE_CAPACITY];
    uint32_t last_iteration;
    uint32_t last_seed;
} fake_context_t;

static uint32_t test_crc32(const char* text, uint32_t length) {
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

static int fake_emit(const test_protocol_event_t* event, void* context) {
    fake_context_t* fake = (fake_context_t*)context;

    if (!fake || !event || fake->event_count >= TEST_EVENT_CAPACITY) {
        return ERR_OVERFLOW;
    }
    fake->events[fake->event_count++] = *event;
    return OK;
}

static int fake_run_case(void* context, const char* case_id,
                         uint32_t case_length, uint32_t iteration,
                         uint32_t seed) {
    fake_context_t* fake = (fake_context_t*)context;

    if (!fake || !case_id || case_length >= TEST_PROTOCOL_CASE_CAPACITY) {
        return ERR_INVALID;
    }
    fake->run_count++;
    memcpy(fake->last_case, case_id, case_length);
    fake->last_case[case_length] = '\0';
    fake->last_iteration = iteration;
    fake->last_seed = seed;
    if (fake->set_reason_during_run && fake->core &&
        test_protocol_core_set_case_reason(fake->core, "fixture_failure") != OK) {
        return ERR_STATE;
    }
    return fake->run_result;
}

static uint32_t make_frame(char* output, uint32_t capacity,
                           const char* fields) {
    int prefix_length;
    int frame_length;
    uint32_t crc;

    if (!output || !fields || capacity < 32U) return 0U;
    prefix_length = snprintf(output, capacity, "@@ZTEST/1 %s", fields);
    if (prefix_length <= 0 || (uint32_t)prefix_length >= capacity) return 0U;
    crc = test_crc32(output, (uint32_t)prefix_length);
    frame_length = snprintf(output + prefix_length,
                            capacity - (uint32_t)prefix_length,
                            " crc=%08X\n", crc);
    if (frame_length <= 0 ||
        (uint32_t)prefix_length + (uint32_t)frame_length >= capacity) {
        return 0U;
    }
    return (uint32_t)prefix_length + (uint32_t)frame_length;
}

static void clear_events(fake_context_t* fake) {
    if (fake) fake->event_count = 0U;
}

static int feed_frame(test_protocol_core_t* core, const char* frame,
                      uint32_t length) {
    int result = OK;

    for (uint32_t index = 0U; index < length; index++) {
        result = test_protocol_core_feed_byte(core, (uint8_t)frame[index]);
    }
    return result;
}

static int prepare_core(test_protocol_core_t* core, fake_context_t* fake) {
    test_protocol_core_callbacks_t callbacks;

    memset(fake, 0, sizeof(*fake));
    fake->run_result = OK;
    callbacks.emit = fake_emit;
    callbacks.run_case = fake_run_case;
    callbacks.context = fake;
    if (test_protocol_core_init(core, &callbacks) != OK) return ERR_STATE;
    fake->core = core;
    return OK;
}

static int expect_event(const fake_context_t* fake, uint32_t index,
                        test_protocol_event_type_t type, const char* reason) {
    const test_protocol_event_t* event;

    if (!fake || index >= fake->event_count) return 0;
    event = &fake->events[index];
    if (event->type != type) return 0;
    if (reason && strcmp(event->reason, reason) != 0) return 0;
    return 1;
}

static int test_handshake(void) {
    test_protocol_core_t core;
    fake_context_t fake;
    char frame[TEST_OUTPUT_CAPACITY];
    uint32_t length;

    if (prepare_core(&core, &fake) != OK) return 0;
    length = make_frame(frame, sizeof(frame), "cmd=HELLO run=hosttest seq=1");
    if (!length || feed_frame(&core, frame, length) != OK) return 0;
    if (fake.event_count != 0U) return 0;
    if (test_protocol_core_set_boot_ready(&core) != OK ||
        test_protocol_core_poll(&core, 10U) != OK) return 0;
    if (!expect_event(&fake, 0U, TEST_PROTOCOL_EVENT_READY, 0)) return 0;
    return fake.events[0].sequence == 1U &&
           strcmp(fake.events[0].run, "hosttest") == 0;
}

static int test_run_case(void) {
    test_protocol_core_t core;
    fake_context_t fake;
    char frame[TEST_OUTPUT_CAPACITY];
    uint32_t length;

    if (prepare_core(&core, &fake) != OK) return 0;
    length = make_frame(frame, sizeof(frame), "cmd=HELLO run=hosttest seq=1");
    if (!length || feed_frame(&core, frame, length) != OK) return 0;
    test_protocol_core_set_boot_ready(&core);
    test_protocol_core_poll(&core, 0U);
    clear_events(&fake);
    length = make_frame(frame, sizeof(frame),
                        "cmd=RUN case=qemu:tst2:boot-ready iteration=2 seed=9 seq=2");
    if (!length || feed_frame(&core, frame, length) != OK) return 0;
    if (fake.event_count != 2U ||
        !expect_event(&fake, 0U, TEST_PROTOCOL_EVENT_BEGIN, 0) ||
        !expect_event(&fake, 1U, TEST_PROTOCOL_EVENT_PASS, 0)) return 0;
    return fake.run_count == 1U && strcmp(fake.last_case,
           "qemu:tst2:boot-ready") == 0 && fake.last_iteration == 2U &&
           fake.last_seed == 9U;
}

static int test_errors(void) {
    test_protocol_core_t core;
    fake_context_t fake;
    char frame[TEST_OUTPUT_CAPACITY];
    uint32_t length;

    if (prepare_core(&core, &fake) != OK) return 0;
    length = make_frame(frame, sizeof(frame), "cmd=HELLO run=hosttest seq=1");
    if (!length || feed_frame(&core, frame, length) != OK) return 0;
    test_protocol_core_set_boot_ready(&core);
    test_protocol_core_poll(&core, 0U);
    clear_events(&fake);
    length = make_frame(frame, sizeof(frame), "cmd=PING run=hosttest seq=3");
    if (!length || feed_frame(&core, frame, length) != ERR_AGAIN ||
        !expect_event(&fake, 0U, TEST_PROTOCOL_EVENT_BLOCKED, "ERR_AGAIN")) {
        return 0;
    }
    clear_events(&fake);
    length = make_frame(frame, sizeof(frame), "cmd=PING run=hosttest seq=2");
    if (!length || feed_frame(&core, frame, length) != OK ||
        !expect_event(&fake, 0U, TEST_PROTOCOL_EVENT_HEARTBEAT, 0)) return 0;
    return 1;
}

static int test_invalid_crc_and_discard(void) {
    test_protocol_core_t core;
    fake_context_t fake;
    char frame[TEST_OUTPUT_CAPACITY];
    uint32_t length;

    if (prepare_core(&core, &fake) != OK) return 0;
    length = make_frame(frame, sizeof(frame), "cmd=HELLO run=hosttest seq=1");
    if (!length || feed_frame(&core, frame, length) != OK) return 0;
    test_protocol_core_set_boot_ready(&core);
    test_protocol_core_poll(&core, 0U);
    clear_events(&fake);
    frame[15] = frame[15] == 'A' ? 'B' : 'A';
    feed_frame(&core, frame, length);
    if (!expect_event(&fake, 0U, TEST_PROTOCOL_EVENT_BLOCKED, "ERR_INVALID")) {
        return 0;
    }
    clear_events(&fake);
    test_protocol_core_feed_byte(&core, 0U);
    test_protocol_core_feed_byte(&core, (uint8_t)'\n');
    if (fake.event_count != 0U) return 0;
    for (uint32_t index = 0U; index < TEST_PROTOCOL_FRAME_CAPACITY; index++) {
        test_protocol_core_feed_byte(&core, 'A');
    }
    test_protocol_core_feed_byte(&core, (uint8_t)'\n');
    return fake.event_count == 0U;
}

static int test_event_format(void) {
    test_protocol_event_t event;
    char output[TEST_OUTPUT_CAPACITY];
    uint32_t length;

    memset(&event, 0, sizeof(event));
    event.type = TEST_PROTOCOL_EVENT_PASS;
    event.has_case = 1U;
    event.sequence = 7U;
    event.iteration = 2U;
    event.seed = 9U;
    strcpy(event.run, "hosttest");
    strcpy(event.case_id, "qemu:tst2:boot-ready");
    length = test_protocol_core_format_event(&event, output, sizeof(output));
    if (!length || strncmp(output, "@@ZTEST/1 event=PASS", 20U) != 0 ||
        strstr(output, " crc=") == 0 || output[length - 1U] != '\n') {
        return 0;
    }
    return test_protocol_core_format_event(&event, output, 16U) == 0U;
}

static int test_public_api_contract(void) {
    test_protocol_core_t core;
    test_protocol_core_t uninitialized;
    fake_context_t fake;
    char frame[TEST_OUTPUT_CAPACITY];
    uint32_t length;

    memset(&uninitialized, 0, sizeof(uninitialized));
    if (test_protocol_core_is_active(&uninitialized) != 0U ||
        test_protocol_core_set_ticks(&uninitialized, 1U) != ERR_STATE ||
        test_protocol_core_set_boot_ready(&uninitialized) != ERR_STATE) {
        return 0;
    }
    if (prepare_core(&core, &fake) != OK ||
        test_protocol_core_is_active(&core) != 0U ||
        test_protocol_core_emit(&core, TEST_PROTOCOL_EVENT_PASS, 0) !=
        ERR_UNAVAILABLE || test_protocol_core_set_ticks(&core, 4U) != OK ||
        test_protocol_core_set_case_reason(&core, "fixture_failure") != OK ||
        test_protocol_core_set_case_reason(&core, "not valid") != OK) {
        return 0;
    }
    length = make_frame(frame, sizeof(frame), "cmd=HELLO run=hosttest seq=1");
    if (!length || feed_frame(&core, frame, length) != OK ||
        test_protocol_core_is_active(&core) == 0U ||
        test_protocol_core_set_boot_ready(&core) != OK ||
        test_protocol_core_poll(&core, 4U) != OK) {
        return 0;
    }
    clear_events(&fake);
    if (test_protocol_core_emit(&core, TEST_PROTOCOL_EVENT_BLOCKED,
                                "ERR_CANCELLED") != OK ||
        !expect_event(&fake, 0U, TEST_PROTOCOL_EVENT_BLOCKED,
                      "ERR_CANCELLED")) {
        return 0;
    }
    clear_events(&fake);
    fake.run_result = ERR_INVALID;
    fake.set_reason_during_run = 1;
    length = make_frame(frame, sizeof(frame),
                        "cmd=RUN case=qemu:tst2:boot-ready iteration=1 seed=4 seq=2");
    if (!length || feed_frame(&core, frame, length) != ERR_INVALID ||
        fake.event_count != 2U ||
        !expect_event(&fake, 1U, TEST_PROTOCOL_EVENT_FAIL,
                      "fixture_failure")) {
        return 0;
    }
    return 1;
}

static int run_tests(void) {
    int passed = 1;

    passed = test_handshake() && passed;
    printf("protocol_handshake %s\n", passed ? "OK" : "ERRO");
    passed = test_run_case() && passed;
    printf("protocol_run_case %s\n", passed ? "OK" : "ERRO");
    passed = test_errors() && passed;
    printf("protocol_errors %s\n", passed ? "OK" : "ERRO");
    passed = test_invalid_crc_and_discard() && passed;
    printf("protocol_invalid_input %s\n", passed ? "OK" : "ERRO");
    passed = test_event_format() && passed;
    printf("protocol_event_format %s\n", passed ? "OK" : "ERRO");
    passed = test_public_api_contract() && passed;
    printf("protocol_public_api %s\n", passed ? "OK" : "ERRO");
    return passed;
}

int main(void) {
    int passed = run_tests();

    printf("TST2 protocol core host test %s\n", passed ? "OK" : "ERRO");
    return passed ? 0 : 1;
}
