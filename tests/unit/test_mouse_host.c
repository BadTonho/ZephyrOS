#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/input.h"
#include "core/irq_deferred.h"
#include "core/log.h"
#include "drivers/idt.h"
#include "drivers/mouse.h"
#include "drivers/vesa.h"

#define HOST_COVERAGE_CAPACITY 128U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_PORT_COUNT 0x10000U
#define HOST_RESPONSE_CAPACITY 32U
#define HOST_FRAMEBUFFER_WIDTH 64U
#define HOST_FRAMEBUFFER_HEIGHT 48U
#define HOST_STATUS_OUTPUT_FULL 0x01U
#define HOST_STATUS_INPUT_FULL 0x02U
#define HOST_STATUS_AUX_DATA 0x20U
#define HOST_CONTROLLER_PORT 0x64U
#define HOST_DATA_PORT 0x60U
#define HOST_COMMAND_WRITE_MOUSE 0xD4U
#define HOST_COMMAND_READ_CONFIG 0x20U
#define HOST_COMMAND_WRITE_CONFIG 0x60U
#define HOST_COMMAND_GET_DEVICE_ID 0xF2U
#define HOST_RESPONSE_ACK 0xFAU
#define HOST_MOUSE_PACKET_STATUS 0x08U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t host_ports[HOST_PORT_COUNT];
static uint8_t host_responses[HOST_RESPONSE_CAPACITY];
static uint32_t host_response_head;
static uint32_t host_response_tail;
static uint8_t host_irq_pending;
static uint8_t host_irq_value;
static uint8_t host_input_busy;
static uint8_t host_bad_ack;
static uint8_t host_device_id;
static uint8_t host_last_command;
static uint8_t host_controller_config;
static uint32_t host_output_count;
static uint32_t host_schedule_count;
static uint32_t host_pointer_publish_count;
static uint32_t host_log_count;
static uint32_t host_pixel_writes;
static uint32_t host_frame_count;
static uint32_t host_flip_count;
static int host_pointer_register_result;
static int host_pointer_publish_result;
static int host_irq_register_result;
static int host_metrics_result;
static input_pointer_sink_t host_pointer_sink;
static isr_handler_t host_irq_handler;
static vesa_mode_t host_mode;
static vesa_color_t host_framebuffer[HOST_FRAMEBUFFER_WIDTH *
                                     HOST_FRAMEBUFFER_HEIGHT];

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
    host_log_count++;
}

uint8_t mouse_host_inb(uint16_t port) {
    if (port == HOST_CONTROLLER_PORT) {
        uint8_t status = host_input_busy ? HOST_STATUS_INPUT_FULL : 0U;

        if (host_irq_pending || host_response_head != host_response_tail) {
            status |= HOST_STATUS_OUTPUT_FULL;
        }
        if (host_irq_pending) status |= HOST_STATUS_AUX_DATA;
        return status;
    }
    if (port == HOST_DATA_PORT) {
        if (host_irq_pending) {
            host_irq_pending = 0U;
            return host_irq_value;
        }
        if (host_response_head != host_response_tail) {
            uint8_t value = host_responses[host_response_head];

            host_response_head =
                (host_response_head + 1U) % HOST_RESPONSE_CAPACITY;
            return value;
        }
        return 0U;
    }
    return host_ports[port];
}

static void host_response_push(uint8_t value) {
    if (host_response_tail - host_response_head >= HOST_RESPONSE_CAPACITY) {
        return;
    }
    host_responses[host_response_tail % HOST_RESPONSE_CAPACITY] = value;
    host_response_tail++;
}

void mouse_host_outb(uint16_t port, uint8_t value) {
    host_ports[port] = value;
    host_output_count++;
    if (port == HOST_CONTROLLER_PORT) {
        host_last_command = value;
        return;
    }
    if (port != HOST_DATA_PORT) return;
    if (host_last_command == HOST_COMMAND_READ_CONFIG) return;
    if (host_last_command == HOST_COMMAND_WRITE_CONFIG) {
        host_controller_config = value;
        return;
    }
    if (host_last_command != HOST_COMMAND_WRITE_MOUSE) return;
    if (host_bad_ack) {
        host_response_push(0U);
        host_bad_ack = 0U;
        return;
    }
    host_response_push(HOST_RESPONSE_ACK);
    if (value == HOST_COMMAND_GET_DEVICE_ID) {
        host_response_push(host_device_id);
    }
}

int input_register_pointer_sink(input_pointer_sink_t sink) {
    if (host_pointer_register_result != OK) return host_pointer_register_result;
    host_pointer_sink = sink;
    return OK;
}

int input_publish_pointer(const input_pointer_event_t* event) {
    host_pointer_publish_count++;
    if (!event || !host_pointer_sink) return ERR_UNAVAILABLE;
    if (host_pointer_publish_result != OK) return host_pointer_publish_result;
    return host_pointer_sink(event);
}

int input_get_metrics(input_metrics_t* metrics) {
    if (!metrics) return ERR_NULL;
    if (host_metrics_result != OK) return host_metrics_result;
    metrics->pointer_queued = 0U;
    metrics->pointer_capacity = INPUT_POINTER_QUEUE_CAPACITY;
    return OK;
}

int irq_deferred_work_init(irq_deferred_work_t* work, const char* owner,
                           uint8_t irq_line, irq_deferred_callback_t callback,
                           void* context) {
    if (!work || !owner || !callback) return ERR_NULL;
    work->callback = callback;
    work->context = context;
    work->irq_line = irq_line;
    work->initialized = 1U;
    work->queued = 0U;
    work->running = 0U;
    return OK;
}

int irq_deferred_schedule(irq_deferred_work_t* work) {
    if (!work || !work->initialized) return ERR_STATE;
    work->queued = 1U;
    work->scheduled++;
    host_schedule_count++;
    return OK;
}

int idt_register_handler(uint8_t number, isr_handler_t handler) {
    if (number != 44U || !handler) return ERR_INVALID;
    if (host_irq_register_result != OK) return host_irq_register_result;
    host_irq_handler = handler;
    return OK;
}

vesa_mode_t* vesa_get_mode(void) {
    return &host_mode;
}

void vesa_put_pixel(uint32_t x, uint32_t y, vesa_color_t color) {
    if (x >= HOST_FRAMEBUFFER_WIDTH || y >= HOST_FRAMEBUFFER_HEIGHT) return;
    host_framebuffer[y * HOST_FRAMEBUFFER_WIDTH + x] = color;
    host_pixel_writes++;
}

vesa_color_t vesa_get_pixel(uint32_t x, uint32_t y) {
    vesa_color_t color = {0};

    if (x >= HOST_FRAMEBUFFER_WIDTH || y >= HOST_FRAMEBUFFER_HEIGHT) {
        return color;
    }
    return host_framebuffer[y * HOST_FRAMEBUFFER_WIDTH + x];
}

void vesa_frame_begin(void) {
    host_frame_count++;
}

void vesa_frame_end(void) {
    host_frame_count++;
}

void vesa_frame_mark_region(uint32_t x, uint32_t y, uint32_t width,
                            uint32_t height) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    host_frame_count++;
}

void vesa_flip_region(uint32_t x, uint32_t y, uint32_t width,
                      uint32_t height) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    host_flip_count++;
}

uint32_t vesa_rgb(uint8_t red, uint8_t green, uint8_t blue) {
    return ((uint32_t)red << 16U) | ((uint32_t)green << 8U) | blue;
}

static void host_reset_fixture(void) {
    for (uint32_t index = 0U; index < HOST_PORT_COUNT; index++) {
        host_ports[index] = 0U;
    }
    for (uint32_t index = 0U; index < HOST_FRAMEBUFFER_WIDTH *
         HOST_FRAMEBUFFER_HEIGHT; index++) {
        host_framebuffer[index].raw = 0x00112233U;
    }
    host_response_head = 0U;
    host_response_tail = 0U;
    host_response_push(0x04U);
    host_irq_pending = 0U;
    host_irq_value = 0U;
    host_input_busy = 0U;
    host_bad_ack = 0U;
    host_device_id = 0x03U;
    host_last_command = 0U;
    host_controller_config = 0U;
    host_output_count = 0U;
    host_schedule_count = 0U;
    host_pointer_publish_count = 0U;
    host_log_count = 0U;
    host_pixel_writes = 0U;
    host_frame_count = 0U;
    host_flip_count = 0U;
    host_pointer_register_result = OK;
    host_pointer_publish_result = OK;
    host_irq_register_result = OK;
    host_metrics_result = OK;
    host_pointer_sink = 0;
    host_irq_handler = 0;
    host_mode.width = HOST_FRAMEBUFFER_WIDTH;
    host_mode.height = HOST_FRAMEBUFFER_HEIGHT;
    host_mode.bpp = 32U;
    host_mode.pitch = HOST_FRAMEBUFFER_WIDTH * sizeof(uint32_t);
    host_mode.framebuffer = 0;
    host_mode.initialized = 1U;
}

static void host_inject_byte(uint8_t value) {
    if (!host_irq_handler) return;
    host_irq_value = value;
    host_irq_pending = 1U;
    host_irq_handler(0);
}

static void host_inject_packet(uint8_t status, uint8_t dx, uint8_t dy,
                               uint8_t wheel) {
    host_inject_byte(status);
    host_inject_byte(dx);
    host_inject_byte(dy);
    if (mouse_has_wheel()) host_inject_byte(wheel);
}

static uint32_t callback_count;
static mouse_event_t callback_events[8];

static void mouse_callback(mouse_event_t* event) {
    if (callback_count < 8U) callback_events[callback_count] = *event;
    callback_count++;
}

static int check_before_init(void) {
    mouse_config_t config;
    mouse_status_t status;

    host_reset_fixture();
    if (mouse_has_wheel() != 0) return 1;
    if (mouse_set_speed(5U) != ERR_UNAVAILABLE) return 2;
    if (mouse_set_acceleration(1) != ERR_UNAVAILABLE) return 3;
    if (mouse_set_primary_button(MOUSE_PRIMARY_RIGHT) != ERR_UNAVAILABLE) {
        return 4;
    }
    if (mouse_get_config(0) != ERR_NULL || mouse_get_status(0) != ERR_NULL) {
        return 5;
    }
    if (mouse_get_config(&config) != OK ||
        config.speed != MOUSE_SPEED_DEFAULT ||
        config.primary_button != MOUSE_PRIMARY_LEFT) return 6;
    if (mouse_get_status(&status) != OK || status.initialized != 0U ||
        status.last_error != ERR_UNAVAILABLE) return 7;
    if (mouse_get_x() != 0 || mouse_get_y() != 0 ||
        mouse_get_buttons() != 0U) return 8;
    if (mouse_set_callback(mouse_callback) != 0) return 9;
    return 0;
}

static int check_initialization_and_events(void) {
    mouse_callback_t old_callback;
    mouse_config_t config;
    mouse_status_t status;
    uint32_t initial_x;
    uint32_t initial_y;

    host_reset_fixture();
    if (mouse_init() != OK || !host_irq_handler) return 20;
    if (host_controller_config != 0x06U) return 21;
    if (mouse_has_wheel() != 1) return 22;
    if (mouse_get_config(&config) != OK || config.speed != 3U ||
        config.acceleration_enabled != 0U ||
        config.primary_button != MOUSE_PRIMARY_LEFT) return 23;
    if (mouse_set_speed(0U) != ERR_INVALID ||
        mouse_set_speed(11U) != ERR_INVALID || mouse_set_speed(5U) != OK) {
        return 24;
    }
    if (mouse_set_acceleration(2) != ERR_INVALID ||
        mouse_set_acceleration(1) != OK ||
        mouse_set_primary_button((mouse_primary_button_t)2) != ERR_INVALID ||
        mouse_set_primary_button(MOUSE_PRIMARY_RIGHT) != OK) return 25;
    old_callback = mouse_set_callback(mouse_callback);
    if (old_callback != 0) return 26;
    initial_x = (uint32_t)mouse_get_x();
    initial_y = (uint32_t)mouse_get_y();
    if (initial_x != HOST_FRAMEBUFFER_WIDTH / 2U ||
        initial_y != HOST_FRAMEBUFFER_HEIGHT / 2U) return 27;
    mouse_process_events();
    host_inject_packet(HOST_MOUSE_PACKET_STATUS, 1U, 0U, 0U);
    host_inject_packet(HOST_MOUSE_PACKET_STATUS, 2U, 0U, 0U);
    mouse_process_events();
    callback_count = 0U;
    host_inject_byte(0U);
    host_inject_packet(HOST_MOUSE_PACKET_STATUS | MOUSE_BTN_LEFT, 3U, 2U,
                       1U);
    host_inject_packet(HOST_MOUSE_PACKET_STATUS | MOUSE_BTN_LEFT, 2U, 1U,
                       0U);
    mouse_process_events();
    if (callback_count < 3U || callback_events[0].event != MOUSE_EVENT_PRESS ||
        callback_events[0].buttons != MOUSE_BTN_RIGHT ||
        callback_events[1].event != MOUSE_EVENT_WHEEL ||
        callback_events[1].wheel != -1) return 28;
    if (mouse_get_buttons() != MOUSE_BTN_RIGHT ||
        mouse_get_x() <= (int)initial_x || mouse_get_y() >= (int)initial_y) {
        return 29;
    }
    host_inject_packet(HOST_MOUSE_PACKET_STATUS, 0U, 0U, 0U);
    mouse_process_events();
    if (mouse_get_buttons() != 0U || callback_count < 4U ||
        callback_events[callback_count - 1U].event != MOUSE_EVENT_RELEASE) {
        return 30;
    }
    if (mouse_get_status(&status) != OK || status.initialized != 1U ||
        status.raw_buttons != 0U || status.effective_buttons != 0U ||
        status.wheel_supported != 1U || status.last_error != ERR_INVALID) return 31;
    mouse_invalidate_cursor();
    if (host_flip_count == 0U || host_frame_count == 0U ||
        host_pixel_writes == 0U || host_schedule_count == 0U ||
        host_pointer_publish_count < 3U) return 32;
    return 0;
}

static int check_fallback_and_failures(void) {
    host_reset_fixture();
    host_device_id = 0U;
    if (mouse_init() != OK || mouse_has_wheel() != 0) return 40;
    host_reset_fixture();
    host_input_busy = 1U;
    if (mouse_init() != ERR_TIMEOUT) return 41;
    host_reset_fixture();
    host_bad_ack = 1U;
    if (mouse_init() != ERR_NOT_FOUND) return 42;
    host_reset_fixture();
    host_pointer_register_result = ERR_UNAVAILABLE;
    if (mouse_init() != ERR_UNAVAILABLE) return 43;
    host_reset_fixture();
    host_irq_register_result = ERR_UNAVAILABLE;
    if (mouse_init() != ERR_UNAVAILABLE) return 44;
    return 0;
}

static int check_input_failure_and_bounds(void) {
    mouse_status_t status;

    host_reset_fixture();
    if (mouse_init() != OK) return 50;
    host_metrics_result = ERR_UNAVAILABLE;
    mouse_process_events();
    host_metrics_result = OK;
    host_pointer_publish_result = ERR_OVERFLOW;
    host_inject_packet(HOST_MOUSE_PACKET_STATUS, 1U, 1U, 0U);
    mouse_process_events();
    if (mouse_get_status(&status) != OK || status.dropped_packets == 0U ||
        status.last_error != ERR_OVERFLOW) return 51;
    host_pointer_publish_result = OK;
    if (mouse_set_speed(MOUSE_SPEED_MAX) != OK ||
        mouse_set_acceleration(0) != OK ||
        mouse_set_primary_button(MOUSE_PRIMARY_LEFT) != OK) return 52;
    host_mode.initialized = 0U;
    mouse_process_events();
    host_mode.initialized = 1U;
    if (mouse_get_status(&status) != OK || status.initialized != 1U) return 53;
    return 0;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:drivers:mouse|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:mouse|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:mouse|value=0x%08X\n",
           (uint32_t)result);
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_before_init();
    if (!result) result = check_initialization_and_events();
    if (!result) result = check_fallback_and_failures();
    if (!result) result = check_input_failure_and_bounds();
    coverage_active = 0U;
    printf("MOUSE_HOST_RESULT=%d logs=%u outputs=%u publishes=%u\n", result,
           host_log_count, host_output_count, host_pointer_publish_count);
    coverage_emit(result);
    return result;
}
