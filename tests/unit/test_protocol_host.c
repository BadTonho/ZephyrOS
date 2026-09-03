#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/test_protocol.h"
#include "drivers/serial.h"
#include "kernel_tests.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_RX_CAPACITY 4096U
#define HOST_TX_CAPACITY 16384U
#define HOST_PROTOCOL_TIMEOUT 64U
#define HOST_PROTOCOL_CRC_POLYNOMIAL 0xEDB88320U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t serial_ready;
static uint8_t rx_queue[HOST_RX_CAPACITY];
static uint32_t rx_head;
static uint32_t rx_tail;
static char tx_log[HOST_TX_CAPACITY];
static uint32_t tx_length;
static uint32_t fake_ticks;
static int fake_memory_result;
static uint32_t progress_calls;
static uint32_t phase_calls;

const kernel_tests_runtime_t* kernel_tests_active_runtime;

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

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

uint32_t timer_get_ticks(void) {
    return fake_ticks;
}

uint8_t serial_is_ready(void) {
    return serial_ready;
}

int serial_read_byte(uint8_t* out_byte) {
    if (!out_byte || !serial_ready || rx_tail == rx_head) return 0;
    *out_byte = rx_queue[rx_tail++];
    if (rx_tail == HOST_RX_CAPACITY) rx_tail = 0U;
    return 1;
}

uint32_t serial_write_text(const char* text, uint32_t length) {
    uint32_t available;

    if (!text || !serial_ready || tx_length >= HOST_TX_CAPACITY) return 0U;
    available = HOST_TX_CAPACITY - tx_length;
    if (length > available) length = available;
    memcpy(tx_log + tx_length, text, length);
    tx_length += length;
    return length;
}

uint32_t serial_flush(uint32_t budget) {
    return budget;
}

static int fake_runtime_run(const kernel_tests_runtime_t* runtime,
                            int result) {
    if (runtime && runtime->progress) {
        fake_ticks = 30U;
        progress_calls++;
        if (runtime->progress(runtime->context) != OK) return ERR_AGAIN;
    }
    if (runtime && runtime->report_phase) {
        phase_calls++;
        runtime->report_phase(runtime->context, "protocol-fixture", result);
    }
    return result;
}

int kernel_tests_run_memory_slab(void) {
    return fake_memory_result;
}

int kernel_tests_run_memory_slab_with_runtime(
    const kernel_tests_runtime_t* runtime) {
    return fake_runtime_run(runtime, fake_memory_result);
}

int kernel_tests_run_paging_vma(const kernel_tests_runtime_t* runtime) {
    return fake_runtime_run(runtime, OK);
}

int kernel_tests_run_execution(const kernel_tests_runtime_t* runtime) {
    return fake_runtime_run(runtime, OK);
}

int kernel_tests_run_storage_vfs(const kernel_tests_runtime_t* runtime) {
    return fake_runtime_run(runtime, OK);
}

int kernel_tests_run_network(const kernel_tests_runtime_t* runtime) {
    return fake_runtime_run(runtime, OK);
}

int kernel_tests_run_platform(const kernel_tests_runtime_t* runtime) {
    return fake_runtime_run(runtime, OK);
}

int kernel_tests_run_tst5_blackbox(const kernel_tests_runtime_t* runtime,
                                   const char* case_id,
                                   uint32_t case_length) {
    (void)case_id;
    (void)case_length;
    return fake_runtime_run(runtime, OK);
}

int kernel_tests_run_tst6(const kernel_tests_runtime_t* runtime,
                          const char* case_id, uint32_t case_length) {
    (void)case_id;
    (void)case_length;
    return fake_runtime_run(runtime, OK);
}

int kernel_tests_progress(const kernel_tests_runtime_t* runtime) {
    if (!runtime || !runtime->progress) return ERR_NULL;
    return runtime->progress(runtime->context);
}

int kernel_tests_phase_result(const char* phase, int result) {
    return phase ? result : ERR_NULL;
}

int kernel_tests_report_phase(const kernel_tests_runtime_t* runtime,
                              const char* phase, int result) {
    if (!runtime || !runtime->report_phase || !phase) return ERR_NULL;
    runtime->report_phase(runtime->context, phase, result);
    return result;
}

static uint32_t test_crc32(const char* text, uint32_t length) {
    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t index = 0U; index < length; index++) {
        crc ^= (uint8_t)text[index];
        for (uint32_t bit = 0U; bit < 8U; bit++) {
            crc = (crc >> 1U) ^
                  ((crc & 1U) ? HOST_PROTOCOL_CRC_POLYNOMIAL : 0U);
        }
    }
    return ~crc;
}

static uint32_t make_frame(char* output, uint32_t capacity,
                           const char* fields) {
    int prefix_length;
    int suffix_length;
    uint32_t crc;

    if (!output || !fields || capacity < 32U) return 0U;
    prefix_length = snprintf(output, capacity, "@@ZTEST/1 %s", fields);
    if (prefix_length <= 0 || (uint32_t)prefix_length >= capacity) return 0U;
    crc = test_crc32(output, (uint32_t)prefix_length);
    suffix_length = snprintf(output + prefix_length,
                             capacity - (uint32_t)prefix_length,
                             " crc=%08X\n", crc);
    if (suffix_length <= 0 ||
        (uint32_t)prefix_length + (uint32_t)suffix_length >= capacity) {
        return 0U;
    }
    return (uint32_t)prefix_length + (uint32_t)suffix_length;
}

static int enqueue_frame(const char* frame, uint32_t length) {
    if (!frame || length >= HOST_RX_CAPACITY) return ERR_INVALID;
    for (uint32_t index = 0U; index < length; index++) {
        uint32_t next = rx_head + 1U;

        if (next == HOST_RX_CAPACITY) next = 0U;
        if (next == rx_tail) return ERR_OVERFLOW;
        rx_queue[rx_head] = (uint8_t)frame[index];
        rx_head = next;
    }
    return OK;
}

static int poll_input(void) {
    for (uint32_t attempt = 0U; attempt < HOST_PROTOCOL_TIMEOUT; attempt++) {
        test_protocol_poll();
        if (rx_tail == rx_head) return OK;
    }
    return ERR_TIMEOUT;
}

static int send_fields(const char* fields) {
    char frame[SERIAL_TX_CAPACITY / 4U];
    uint32_t length = make_frame(frame, sizeof(frame), fields);

    if (!length || enqueue_frame(frame, length) != OK) return ERR_INVALID;
    return poll_input();
}

static int tx_contains(const char* text) {
    if (!text) return 0;
    tx_log[tx_length < HOST_TX_CAPACITY ? tx_length : HOST_TX_CAPACITY - 1U] =
        '\0';
    return strstr(tx_log, text) != 0;
}

static void clear_tx(void) {
    tx_length = 0U;
    tx_log[0] = '\0';
}

static int check_uninitialized_contract(void) {
    serial_ready = 0U;
    if (test_protocol_is_active() != 0U) return 10;
    test_protocol_poll();
    test_protocol_set_boot_ready();
    test_protocol_panic("ignored");
    test_protocol_timeout("ignored");
    if (test_protocol_init() != ERR_UNAVAILABLE) return 11;
    serial_ready = 1U;
    if (test_protocol_init() != OK || test_protocol_is_active() != 0U) {
        return 12;
    }
    test_protocol_set_boot_ready();
    return 0;
}

static int check_handshake_and_events(void) {
    if (send_fields("cmd=HELLO run=protocolhost seq=1") != OK) return 20;
    if (test_protocol_is_active() != 1U || !tx_contains("event=READY")) {
        return 21;
    }
    clear_tx();
    if (send_fields("cmd=PING run=protocolhost seq=2") != OK ||
        !tx_contains("event=HEARTBEAT")) return 22;
    clear_tx();
    test_protocol_panic("panic_fixture");
    test_protocol_timeout("timeout_fixture");
    if (!tx_contains("event=PANIC") || !tx_contains("event=TIMEOUT")) {
        return 23;
    }
    return 0;
}

static int check_dispatch_and_failure(void) {
    int result;

    fake_memory_result = OK;
    clear_tx();
    if (send_fields("cmd=RUN case=qemu:tst2:boot-ready iteration=1 seed=3 seq=3") != OK ||
        !tx_contains("event=BEGIN") || !tx_contains("event=PASS")) return 30;
    clear_tx();
    if (send_fields("cmd=ABORT run=protocolhost seq=4") != OK ||
        !tx_contains("event=BLOCKED") || !tx_contains("ERR_CANCELLED")) {
        return 31;
    }
    clear_tx();
    fake_memory_result = ERR_INVALID;
    result = send_fields("cmd=RUN case=qemu:tst4:memory-slab iteration=2 seed=4 seq=5");
    if (result != OK) return 320;
    if (!tx_contains("event=HEARTBEAT")) return 321;
    if (!tx_contains("event=FAIL")) return 322;
    if (!tx_contains("protocol-fixture-ERR_INVALID")) return 323;
    if (progress_calls == 0U || phase_calls == 0U) return 33;
    return 0;
}

static int check_all_case_routes(void) {
    static const char* cases[] = {
        "qemu:tst4:paging-vma",
        "qemu:tst4:execution",
        "qemu:tst4:storage-vfs",
        "qemu:tst4:network",
        "qemu:tst4:platform",
        "qemu:tst5:shell",
        "qemu:tst6:matrix:baseline"
    };

    for (uint32_t index = 0U; index < sizeof(cases) / sizeof(cases[0]);
         index++) {
        char fields[192];

        snprintf(fields, sizeof(fields),
                 "cmd=RUN case=%s iteration=1 seed=5 seq=%u", cases[index],
                 6U + index);
        clear_tx();
        if (send_fields(fields) != OK || !tx_contains("event=PASS")) {
            return 40 + (int)index;
        }
    }
    clear_tx();
    if (send_fields("cmd=RUN case=unknown-case iteration=1 seed=6 seq=13") !=
            OK || !tx_contains("ERR_NOT_FOUND")) return 48;
    return 0;
}

static int check_before_ready_and_invalid(void) {
    serial_ready = 1U;
    if (test_protocol_init() != OK) return 50;
    if (send_fields("cmd=HELLO run=protocolhost seq=1") != OK) return 51;
    clear_tx();
    if (send_fields("cmd=RUN case=qemu:tst2:boot-ready iteration=1 seed=1 seq=2") !=
            OK || !tx_contains("ERR_AGAIN")) return 52;
    clear_tx();
    if (send_fields("cmd=PING run=protocolhost seq=3") != OK ||
        !tx_contains("ERR_AGAIN")) return 53;
    return 0;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:tst2:protocol-adapter|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:tst2:protocol-adapter|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:tst2:protocol-adapter|value=0x%08X\n",
           (uint32_t)result);
}

int main(void) {
    int result;

    coverage_active = 1U;
    rx_head = 0U;
    rx_tail = 0U;
    fake_ticks = 1U;
    fake_memory_result = OK;
    result = check_uninitialized_contract();
    if (!result) result = check_handshake_and_events();
    if (!result) result = check_dispatch_and_failure();
    if (!result) result = check_all_case_routes();
    if (!result) result = check_before_ready_and_invalid();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
