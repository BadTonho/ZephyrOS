#include "core/test_protocol.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/timer.h"
#include "drivers/serial.h"
#include "kernel_tests.h"
#include "test_coverage.h"
#include "test_protocol_core.h"

#define TEST_PROTOCOL_RX_BUDGET 32U

static test_protocol_core_t protocol_core;
static uint8_t protocol_initialized;
static uint8_t protocol_boot_ready;
static uint8_t protocol_emit_error_logged;
static char protocol_case_reason[TEST_PROTOCOL_CASE_CAPACITY];

static uint32_t protocol_length(const char* text);

static const char* protocol_error_name(int result) {
    if (result == ERR_NULL) return "ERR_NULL";
    if (result == ERR_MEM) return "ERR_MEM";
    if (result == ERR_DISK) return "ERR_DISK";
    if (result == ERR_NOT_FOUND) return "ERR_NOT_FOUND";
    if (result == ERR_OVERFLOW) return "ERR_OVERFLOW";
    if (result == ERR_INVALID) return "ERR_INVALID";
    if (result == ERR_STATE) return "ERR_STATE";
    if (result == ERR_TIMEOUT) return "ERR_TIMEOUT";
    if (result == ERR_UNAVAILABLE) return "ERR_UNAVAILABLE";
    if (result == ERR_CANCELLED) return "ERR_CANCELLED";
    if (result == ERR_AGAIN) return "ERR_AGAIN";
    return "ERR_UNKNOWN";
}

static void protocol_copy_text(char* destination, uint32_t capacity,
                               const char* text) {
    uint32_t length = 0U;

    if (!destination || capacity == 0U) return;
    if (!text) text = "ERR_STATE";
    while (text[length] && length + 1U < capacity) {
        destination[length] = text[length];
        length++;
    }
    destination[length] = '\0';
}

static void protocol_report_phase(void* context, const char* phase,
                                  int result) {
    uint32_t phase_length;
    uint32_t error_length;

    (void)context;
    if (result == OK || protocol_case_reason[0] || !phase) return;
    phase_length = protocol_length(phase);
    error_length = protocol_length(protocol_error_name(result));
    if (phase_length + 1U + error_length >= sizeof(protocol_case_reason)) {
        protocol_copy_text(protocol_case_reason, sizeof(protocol_case_reason),
                           "terminal:ERR_OVERFLOW");
        (void)test_protocol_core_set_case_reason(&protocol_core,
                                                 protocol_case_reason);
        return;
    }
    protocol_copy_text(protocol_case_reason, sizeof(protocol_case_reason), phase);
    protocol_case_reason[phase_length] = '-';
    protocol_copy_text(protocol_case_reason + phase_length + 1U,
                       sizeof(protocol_case_reason) - phase_length - 1U,
                       protocol_error_name(result));
    (void)test_protocol_core_set_case_reason(&protocol_core,
                                             protocol_case_reason);
}

static int protocol_test_progress(void* context) {
    uint32_t current_tick = timer_get_ticks();
    int result;

    (void)context;
    result = test_protocol_core_set_ticks(&protocol_core, current_tick);
    if (result != OK) return result;
    result = test_protocol_core_poll(&protocol_core, current_tick);
    serial_flush(SERIAL_TX_FLUSH_BUDGET);
    return result;
}

static uint32_t protocol_length(const char* text) {
    uint32_t length = 0U;

    if (!text) return 0U;
    while (text[length]) length++;
    return length;
}

static int protocol_token_equals(const char* value, uint32_t length,
                                 const char* expected) {
    uint32_t expected_length;

    if (!value || !expected) return 0;
    expected_length = protocol_length(expected);
    if (length != expected_length) return 0;
    for (uint32_t index = 0U; index < length; index++) {
        if (value[index] != expected[index]) return 0;
    }
    return 1;
}

static int protocol_emit_event(const test_protocol_event_t* event,
                               void* context) {
    char frame[TEST_PROTOCOL_FRAME_CAPACITY];
    uint32_t length;
    uint32_t written;

    (void)context;
    length = test_protocol_core_format_event(event, frame, sizeof(frame));
    if (!length) {
        if (!protocol_emit_error_logged) {
            LOG_ERROR("TEST", "Falha ao formatar evento ZTEST");
            protocol_emit_error_logged = 1U;
        }
        return ERR_INVALID;
    }
    written = serial_write_text(frame, length);
    serial_flush(SERIAL_TX_FLUSH_BUDGET);
    if (written != length) {
        if (!protocol_emit_error_logged) {
            LOG_ERROR("TEST", "Fila serial recusou evento ZTEST");
            protocol_emit_error_logged = 1U;
        }
        return ERR_AGAIN;
    }
    return OK;
}

static int protocol_dispatch_case(void* context, const char* case_id,
                                  uint32_t case_length, uint32_t iteration,
                                  uint32_t seed) {
    static const char boot_case[] = "qemu:tst2:boot-ready";
    static const char memory_slab_case[] = "qemu:tst4:memory-slab";
    static const char paging_vma_case[] = "qemu:tst4:paging-vma";
    static const char execution_case[] = "qemu:tst4:execution";
    static const char storage_vfs_case[] = "qemu:tst4:storage-vfs";
    static const char network_case[] = "qemu:tst4:network";
    static const char platform_case[] = "qemu:tst4:platform";
    static const char tst5_prefix[] = "qemu:tst5:";
    static const char tst6_prefix[] = "qemu:tst6:";
    kernel_tests_runtime_t runtime;

    (void)context;
    (void)iteration;
    (void)seed;
    runtime.progress = protocol_test_progress;
    runtime.context = 0;
    runtime.report_phase = protocol_report_phase;
    protocol_case_reason[0] = '\0';
    if (protocol_token_equals(case_id, case_length, boot_case)) {
        if (protocol_boot_ready) return OK;
        LOG_WARN("TEST", "Caso de boot solicitado antes do estado READY");
        return ERR_AGAIN;
    }
    if (protocol_token_equals(case_id, case_length, memory_slab_case)) {
        if (!protocol_boot_ready) {
            LOG_WARN("TEST", "Autoteste de memoria solicitado antes do READY");
            return ERR_STATE;
        }
        return kernel_tests_run_memory_slab_with_runtime(&runtime);
    }
    if (protocol_token_equals(case_id, case_length, paging_vma_case)) {
        if (!protocol_boot_ready) return ERR_STATE;
        return kernel_tests_run_paging_vma(&runtime);
    }
    if (protocol_token_equals(case_id, case_length, execution_case)) {
        if (!protocol_boot_ready) return ERR_STATE;
        return kernel_tests_run_execution(&runtime);
    }
    if (protocol_token_equals(case_id, case_length, storage_vfs_case)) {
        if (!protocol_boot_ready) return ERR_STATE;
        return kernel_tests_run_storage_vfs(&runtime);
    }
    if (protocol_token_equals(case_id, case_length, network_case)) {
        if (!protocol_boot_ready) return ERR_STATE;
        return kernel_tests_run_network(&runtime);
    }
    if (protocol_token_equals(case_id, case_length, platform_case)) {
        if (!protocol_boot_ready) return ERR_STATE;
        return kernel_tests_run_platform(&runtime);
    }
    if (case_length > protocol_length(tst5_prefix) &&
        protocol_token_equals(case_id, protocol_length(tst5_prefix), tst5_prefix)) {
        if (!protocol_boot_ready) return ERR_STATE;
        return kernel_tests_run_tst5_blackbox(&runtime, case_id, case_length);
    }
    if (case_length > protocol_length(tst6_prefix) &&
        protocol_token_equals(case_id, protocol_length(tst6_prefix), tst6_prefix)) {
        if (!protocol_boot_ready) return ERR_STATE;
        return kernel_tests_run_tst6(&runtime, case_id, case_length);
    }
    return ERR_NOT_FOUND;
}

static int protocol_run_case(void* context, const char* case_id,
                             uint32_t case_length, uint32_t iteration,
                             uint32_t seed) {
    int result;

#if defined(ZEPHYROS_TEST_COVERAGE)
    test_coverage_begin_case(case_id, case_length);
#endif
    result = protocol_dispatch_case(context, case_id, case_length,
                                    iteration, seed);
#if defined(ZEPHYROS_TEST_COVERAGE)
    test_coverage_end_case(result);
#endif
    return result;
}

static void protocol_receive(void) {
    uint8_t byte;
    uint32_t received = 0U;

    while (received++ < TEST_PROTOCOL_RX_BUDGET && serial_read_byte(&byte)) {
        test_protocol_core_feed_byte(&protocol_core, byte);
    }
}

int test_protocol_init(void) {
    test_protocol_core_callbacks_t callbacks;

    if (!serial_is_ready()) {
        LOG_ERROR("TEST", "Protocolo ZTEST sem canal serial");
        return ERR_UNAVAILABLE;
    }
    callbacks.emit = protocol_emit_event;
    callbacks.run_case = protocol_run_case;
    callbacks.context = 0;
    if (test_protocol_core_init(&protocol_core, &callbacks) != OK) {
        LOG_ERROR("TEST", "Falha ao inicializar nucleo do protocolo ZTEST");
        return ERR_STATE;
    }
    protocol_initialized = 1U;
    protocol_boot_ready = 0U;
    protocol_emit_error_logged = 0U;
    protocol_case_reason[0] = '\0';
    return OK;
}

void test_protocol_set_boot_ready(void) {
    if (!protocol_initialized) return;
    protocol_boot_ready = 1U;
    test_protocol_core_set_boot_ready(&protocol_core);
}

void test_protocol_poll(void) {
    uint32_t current_tick;

    if (!protocol_initialized) return;
    current_tick = timer_get_ticks();
    test_protocol_core_set_ticks(&protocol_core, current_tick);
    protocol_receive();
    test_protocol_core_poll(&protocol_core, current_tick);
    serial_flush(SERIAL_TX_FLUSH_BUDGET);
}

void test_protocol_panic(const char* reason) {
    if (!protocol_initialized || !test_protocol_is_active()) return;
    test_protocol_core_emit(&protocol_core, TEST_PROTOCOL_EVENT_PANIC, reason);
    serial_flush(SERIAL_TX_CAPACITY);
}

void test_protocol_timeout(const char* reason) {
    if (!protocol_initialized || !test_protocol_is_active()) return;
    test_protocol_core_emit(&protocol_core, TEST_PROTOCOL_EVENT_TIMEOUT, reason);
    serial_flush(SERIAL_TX_CAPACITY);
}

uint8_t test_protocol_is_active(void) {
    return test_protocol_core_is_active(&protocol_core);
}
