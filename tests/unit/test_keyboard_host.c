#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/input.h"
#include "core/irq_deferred.h"
#include "core/log.h"
#include "core/keyboard.h"
#include "process/process.h"

#define HOST_COVERAGE_CAPACITY 128U
#define HOST_COVERAGE_LINE_SIZE 32U
#define KEYBOARD_PORT_COUNT 0x10000U
#define KEYBOARD_DATA_PORT 0x60U
#define KEYBOARD_STATUS_PORT 0x64U
#define KEYBOARD_RESET_COMMAND 0xFEU
#define KEYBOARD_RESET_STATUS_BUSY 0x02U
#define KEYBOARD_QUEUE_CAPACITY 255U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t keyboard_ports[KEYBOARD_PORT_COUNT];
static uint32_t output_count;
static int fake_work_result;
static int fake_key_sink_result;
static int fake_handler_result;
static int fake_unmask_result;
static uint32_t work_init_count;
static uint32_t key_sink_count;
static uint32_t handler_count;
static uint32_t unmask_count;

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

uint8_t keyboard_host_inb(uint16_t port) {
    return keyboard_ports[port];
}

void keyboard_host_outb(uint16_t port, uint8_t value) {
    keyboard_ports[port] = value;
    output_count++;
}

int irq_deferred_work_init(irq_deferred_work_t* work, const char* owner,
                           uint8_t irq_line, irq_deferred_callback_t callback,
                           void* context) {
    (void)work;
    (void)owner;
    (void)irq_line;
    (void)callback;
    (void)context;
    work_init_count++;
    return fake_work_result;
}

int irq_deferred_schedule(irq_deferred_work_t* work) {
    (void)work;
    return OK;
}

int input_register_key_sink(input_key_sink_t sink) {
    (void)sink;
    key_sink_count++;
    return fake_key_sink_result;
}

int input_publish_key(const input_key_event_t* event) {
    (void)event;
    return OK;
}

int input_dispatch(uint32_t budget, uint32_t* out_processed) {
    (void)budget;
    if (out_processed) *out_processed = 0U;
    return OK;
}

int input_get_metrics(input_metrics_t* out_metrics) {
    if (!out_metrics) return ERR_NULL;
    out_metrics->key_queued = 0U;
    out_metrics->key_capacity = INPUT_KEY_QUEUE_CAPACITY;
    return OK;
}

int idt_register_handler(uint8_t number, isr_handler_t handler) {
    (void)number;
    (void)handler;
    handler_count++;
    return fake_handler_result;
}

int idt_unmask_irq(uint8_t irq_line) {
    (void)irq_line;
    unmask_count++;
    return fake_unmask_result;
}

process_t* process_get_by_pid(uint32_t pid) {
    (void)pid;
    return 0;
}

uint32_t process_get_focus(void) {
    return 0U;
}

int process_is_user(const process_t* process) {
    (void)process;
    return 0;
}

int process_signal_send(uint32_t pid, uint32_t signal_number) {
    (void)pid;
    (void)signal_number;
    return ERR_UNAVAILABLE;
}

int process_cancel_focused_user(uint32_t exit_code) {
    (void)exit_code;
    return ERR_UNAVAILABLE;
}

int ipc_send(uint32_t pid, ipc_msg_t* message) {
    (void)pid;
    (void)message;
    return ERR_UNAVAILABLE;
}

static int cancel_filter(uint8_t scancode) {
    return scancode == 0x1BU;
}

static int check_before_init(void) {
    keyboard_metrics_t metrics;

    keyboard_get_metrics(&metrics);
    if (metrics.capacity != KEYBOARD_QUEUE_CAPACITY || metrics.queued != 0U) {
        return 10;
    }
    if (keyboard_controller_reset_available() != 0U ||
        keyboard_controller_reset() != ERR_UNAVAILABLE) return 11;
    keyboard_set_focus_cancel_filter(cancel_filter);
    if (keyboard_scancode_to_ascii(0x10U) != 'q' ||
        keyboard_scancode_to_ascii_shifted(0x10U, 1U) != 'Q' ||
        keyboard_scancode_to_ascii_shifted(0x73U, 0U) != '/' ||
        keyboard_scancode_to_ascii_shifted(0x73U, 1U) != '?' ||
        keyboard_scancode_to_ascii_shifted(0x56U, 0U) != '\\' ||
        keyboard_scancode_to_ascii_shifted(0x56U, 1U) != '|' ||
        keyboard_scancode_to_ascii(0x80U) != 0) return 12;
    return 0;
}

static int check_initialization(void) {
    keyboard_metrics_t metrics;

    fake_work_result = OK;
    fake_key_sink_result = OK;
    fake_handler_result = OK;
    fake_unmask_result = OK;
    work_init_count = 0U;
    key_sink_count = 0U;
    handler_count = 0U;
    unmask_count = 0U;
    keyboard_init();
    if (keyboard_controller_reset_available() != 1U ||
        work_init_count != 1U || key_sink_count != 1U ||
        handler_count != 1U || unmask_count != 1U) return 20;
    keyboard_set_focus_cancel_filter(cancel_filter);
    keyboard_get_metrics(&metrics);
    if (metrics.capacity != KEYBOARD_QUEUE_CAPACITY || metrics.queued != 0U ||
        metrics.dropped != 0U || metrics.processed != 0U) return 21;
    return 0;
}

static int check_controller_reset(void) {
    output_count = 0U;
    keyboard_ports[KEYBOARD_STATUS_PORT] = 0U;
    if (keyboard_controller_reset() != ERR_TIMEOUT ||
        keyboard_ports[KEYBOARD_STATUS_PORT] != KEYBOARD_RESET_COMMAND ||
        output_count != 1U) return 30;
    keyboard_ports[KEYBOARD_STATUS_PORT] = KEYBOARD_RESET_STATUS_BUSY;
    return keyboard_controller_reset() == ERR_TIMEOUT ? 0 : 31;
}

static int check_failed_initialization(void) {
    fake_work_result = ERR_UNAVAILABLE;
    keyboard_init();
    if (keyboard_controller_reset_available() != 0U) return 40;
    fake_work_result = OK;
    keyboard_init();
    return keyboard_controller_reset_available() == 1U ? 0 : 41;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:drivers:keyboard|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:keyboard|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:keyboard|value=0x%08X\n",
           (uint32_t)result);
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_before_init();
    if (!result) result = check_initialization();
    if (!result) result = check_controller_reset();
    if (!result) result = check_failed_initialization();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
