#ifndef TEST_PROTOCOL_CORE_H
#define TEST_PROTOCOL_CORE_H

#include "core/errors.h"
#include "core/test_protocol.h"
#include "types.h"

#define TEST_PROTOCOL_CORE_MAX_FIELDS 16U

typedef enum {
    TEST_PROTOCOL_EVENT_NONE = 0,
    TEST_PROTOCOL_EVENT_READY,
    TEST_PROTOCOL_EVENT_HEARTBEAT,
    TEST_PROTOCOL_EVENT_BEGIN,
    TEST_PROTOCOL_EVENT_PASS,
    TEST_PROTOCOL_EVENT_FAIL,
    TEST_PROTOCOL_EVENT_SKIP,
    TEST_PROTOCOL_EVENT_BLOCKED,
    TEST_PROTOCOL_EVENT_PANIC,
    TEST_PROTOCOL_EVENT_TIMEOUT
} test_protocol_event_type_t;

typedef struct {
    test_protocol_event_type_t type;
    uint8_t has_case;
    uint32_t sequence;
    uint32_t iteration;
    uint32_t seed;
    uint32_t ticks;
    char run[TEST_PROTOCOL_RUN_CAPACITY];
    char case_id[TEST_PROTOCOL_CASE_CAPACITY];
    char reason[TEST_PROTOCOL_CASE_CAPACITY];
} test_protocol_event_t;

typedef int (*test_protocol_emit_fn)(const test_protocol_event_t* event,
                                     void* context);
typedef int (*test_protocol_run_case_fn)(void* context, const char* case_id,
                                         uint32_t case_length,
                                         uint32_t iteration, uint32_t seed);

typedef struct {
    test_protocol_emit_fn emit;
    test_protocol_run_case_fn run_case;
    void* context;
} test_protocol_core_callbacks_t;

typedef struct {
    uint8_t initialized;
    uint8_t session;
    uint8_t boot_ready;
    uint8_t ready_sent;
    uint8_t busy;
    uint8_t rx_discard;
    uint32_t host_sequence;
    uint32_t guest_sequence;
    uint32_t last_heartbeat;
    uint32_t current_ticks;
    uint32_t rx_length;
    char run[TEST_PROTOCOL_RUN_CAPACITY];
    char case_reason[TEST_PROTOCOL_CASE_CAPACITY];
    char rx[TEST_PROTOCOL_FRAME_CAPACITY];
    test_protocol_core_callbacks_t callbacks;
} test_protocol_core_t;

int test_protocol_core_init(test_protocol_core_t* core,
                            const test_protocol_core_callbacks_t* callbacks);
int test_protocol_core_feed_byte(test_protocol_core_t* core, uint8_t byte);
int test_protocol_core_poll(test_protocol_core_t* core, uint32_t ticks);
int test_protocol_core_set_ticks(test_protocol_core_t* core, uint32_t ticks);
int test_protocol_core_set_boot_ready(test_protocol_core_t* core);
int test_protocol_core_set_case_reason(test_protocol_core_t* core,
                                        const char* reason);
int test_protocol_core_emit(test_protocol_core_t* core,
                            test_protocol_event_type_t type,
                            const char* reason);
uint8_t test_protocol_core_is_active(const test_protocol_core_t* core);
uint32_t test_protocol_core_format_event(const test_protocol_event_t* event,
                                         char* output, uint32_t capacity);

#endif
