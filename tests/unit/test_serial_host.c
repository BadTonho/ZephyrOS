#include <stdint.h>
#include <stdio.h>

#include "core/log.h"
#include "drivers/serial.h"

#define HOST_COVERAGE_CAPACITY 128U
#define HOST_COVERAGE_LINE_SIZE 32U
#define SERIAL_LINE_STATUS_DATA_READY 0x01U
#define SERIAL_LINE_STATUS_TRANSMITTER_READY 0x20U

uint8_t serial_host_line_status;
uint8_t serial_host_data;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

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

static int expect(int condition, int code) {
    return condition ? 0 : code;
}

static int check_not_ready(void) {
    uint8_t byte = 0U;

    if (serial_is_ready() != 0U) return 10;
    if (serial_read_byte(NULL) != 0) return 11;
    if (serial_read_byte(&byte) != 0) return 12;
    if (serial_write_text(NULL, 1U) != 0U) return 13;
    if (serial_write_text("A", 1U) != 0U) return 14;
    if (serial_flush(1U) != 0U) return 15;
    return 0;
}

static int check_ready_and_receive(void) {
    uint8_t byte = 0U;

    serial_host_line_status = SERIAL_LINE_STATUS_TRANSMITTER_READY;
    serial_host_data = 0x5AU;
    serial_init();
    if (serial_is_ready() != 1U) return 20;
    if (serial_read_byte(NULL) != 0) return 21;
    if (serial_read_byte(&byte) != 0) return 22;
    serial_host_line_status |= SERIAL_LINE_STATUS_DATA_READY;
    if (serial_read_byte(&byte) != 1 || byte != 0x5AU) return 23;
    return 0;
}

static int check_transmit_filtering(void) {
    static const char filtered[] = {'A', '\n', '\x01'};
    static const char sequence[] = {'\x1B', '[', '2', 'J'};
    static const char broken_sequence[] = {'\x1B', 'x', 'B'};

    serial_host_line_status = 0U;
    if (serial_write_text(filtered, sizeof(filtered)) != sizeof(filtered)) {
        return 30;
    }
    if (serial_flush(0U) != 0U) return 31;
    serial_host_line_status = SERIAL_LINE_STATUS_TRANSMITTER_READY;
    if (serial_flush(1U) != 1U) return 32;
    if (serial_flush(SERIAL_TX_CAPACITY) != 1U) return 33;
    if (serial_write_text(sequence, sizeof(sequence)) != sizeof(sequence)) {
        return 34;
    }
    serial_host_line_status = 0U;
    if (serial_write_text(broken_sequence, sizeof(broken_sequence)) !=
        sizeof(broken_sequence)) return 35;
    serial_host_line_status = SERIAL_LINE_STATUS_TRANSMITTER_READY;
    if (serial_flush(SERIAL_TX_CAPACITY) != 1U) return 36;
    return expect(serial_flush(1U) == 0U, 37);
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:drivers:serial|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:serial|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:serial|value=0x%08X\n",
           (uint32_t)result);
}

int main(void) {
    int result;

    coverage_active = 1U;
    serial_host_line_status = 0U;
    serial_host_data = 0U;
    result = check_not_ready();
    if (!result) result = check_ready_and_receive();
    if (!result) result = check_transmit_filtering();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
