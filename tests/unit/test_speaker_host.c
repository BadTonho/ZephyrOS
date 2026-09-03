#include <stdint.h>
#include <stdio.h>

#include "core/log.h"
#include "drivers/speaker.h"

#define HOST_COVERAGE_CAPACITY 64U
#define HOST_COVERAGE_LINE_SIZE 32U
#define SPEAKER_PORT_COUNT 0x10000U
#define SPEAKER_PIT_COMMAND_PORT 0x43U
#define SPEAKER_PIT_CHANNEL_PORT 0x42U
#define SPEAKER_CONTROL_PORT 0x61U
#define SPEAKER_PIT_COMMAND 0xB6U
#define SPEAKER_ENABLED_MASK 0x03U

static uint8_t speaker_ports[SPEAKER_PORT_COUNT];
static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint32_t fake_ticks;
static uint32_t output_count;

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

uint8_t speaker_host_inb(uint16_t port) {
    return speaker_ports[port];
}

void speaker_host_outb(uint16_t port, uint8_t value) {
    speaker_ports[port] = value;
    output_count++;
}

uint32_t timer_get_ticks(void) {
    return fake_ticks++;
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

static int check_initialization(void) {
    speaker_ports[SPEAKER_CONTROL_PORT] = 0xFFU;
    output_count = 0U;
    speaker_init();
    if (speaker_ports[SPEAKER_CONTROL_PORT] != 0xFCU) return 10;
    return output_count == 1U ? 0 : 11;
}

static int check_zero_frequency(void) {
    speaker_ports[SPEAKER_CONTROL_PORT] = 0xFFU;
    output_count = 0U;
    speaker_beep(0U, 100U);
    if (speaker_ports[SPEAKER_CONTROL_PORT] != 0xFCU) return 20;
    return output_count == 1U ? 0 : 21;
}

static int check_beep(void) {
    uint32_t divisor;

    fake_ticks = 0U;
    speaker_ports[SPEAKER_CONTROL_PORT] = 0U;
    output_count = 0U;
    speaker_beep(440U, 1000U);
    divisor = 1193180U / 440U;
    if (speaker_ports[SPEAKER_PIT_COMMAND_PORT] != SPEAKER_PIT_COMMAND) {
        return 30;
    }
    if (speaker_ports[SPEAKER_PIT_CHANNEL_PORT] !=
        (uint8_t)((divisor >> 8) & 0xFFU)) return 31;
    if (speaker_ports[SPEAKER_CONTROL_PORT] != 0U) return 32;
    return output_count == 5U ? 0 : 33;
}

static int check_melody(void) {
    static const uint32_t frequencies[] = {0U, 880U};
    static const uint32_t durations[] = {1000U, 0U};

    fake_ticks = 0U;
    speaker_ports[SPEAKER_CONTROL_PORT] = 0U;
    output_count = 0U;
    speaker_play_melody(frequencies, durations, 2);
    if (speaker_ports[SPEAKER_CONTROL_PORT] != 0U) return 40;
    return output_count == 5U ? 0 : 41;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:drivers:speaker|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:speaker|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:speaker|value=0x%08X\n",
           (uint32_t)result);
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_initialization();
    if (!result) result = check_zero_frequency();
    if (!result) result = check_beep();
    if (!result) result = check_melody();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
