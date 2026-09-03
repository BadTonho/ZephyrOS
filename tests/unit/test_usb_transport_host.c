#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/usb_transport.h"
#include "drivers/ehci.h"
#include "drivers/uhci.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

typedef struct {
    uint32_t control_calls;
    uint32_t bulk_calls;
    uint32_t reset_calls;
    uint32_t submit_calls;
    uint32_t cancel_calls;
    const usb_device_info_t* device;
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
    uint8_t endpoint_address;
    uint8_t direction_in;
    uint16_t max_packet;
    uint8_t interval;
    usb_interrupt_callback_t callback;
    void* context;
} transport_calls_t;

static transport_calls_t ehci_calls;
static transport_calls_t uhci_calls;

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

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:core:usb-transport|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:usb-transport|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:usb-transport|value=0x%08X\n",
           (uint32_t)result);
}

static void reset_calls(void) {
    ehci_calls = (transport_calls_t){0};
    uhci_calls = (transport_calls_t){0};
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

static void interrupt_callback(void* context, int result, const uint8_t* data,
                               uint16_t length) {
    (void)context;
    (void)result;
    (void)data;
    (void)length;
}

int ehci_control_request(const usb_device_info_t* device,
                         uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, uint16_t length,
                         uint8_t* data, uint16_t* out_length) {
    ehci_calls.control_calls++;
    ehci_calls.device = device;
    ehci_calls.request_type = request_type;
    ehci_calls.request = request;
    ehci_calls.value = value;
    ehci_calls.index = index;
    ehci_calls.length = length;
    if (out_length) *out_length = length;
    if (data) data[0] = request;
    return 11;
}

int ehci_bulk_transfer(const usb_device_info_t* device,
                       uint8_t endpoint_address, uint8_t direction_in,
                       uint8_t* buffer, uint16_t length,
                       uint16_t* out_length) {
    ehci_calls.bulk_calls++;
    ehci_calls.device = device;
    ehci_calls.endpoint_address = endpoint_address;
    ehci_calls.direction_in = direction_in;
    ehci_calls.length = length;
    if (out_length) *out_length = length;
    if (buffer) buffer[0] = endpoint_address;
    return 12;
}

int ehci_reset_bulk_toggles(const usb_device_info_t* device) {
    ehci_calls.reset_calls++;
    ehci_calls.device = device;
    return 13;
}

int ehci_interrupt_submit(const usb_device_info_t* device,
                          uint8_t endpoint_address, uint16_t max_packet,
                          uint8_t interval, usb_interrupt_callback_t callback,
                          void* context) {
    ehci_calls.submit_calls++;
    ehci_calls.device = device;
    ehci_calls.endpoint_address = endpoint_address;
    ehci_calls.max_packet = max_packet;
    ehci_calls.interval = interval;
    ehci_calls.callback = callback;
    ehci_calls.context = context;
    return 14;
}

int ehci_interrupt_cancel(const usb_device_info_t* device,
                          uint8_t endpoint_address) {
    ehci_calls.cancel_calls++;
    ehci_calls.device = device;
    ehci_calls.endpoint_address = endpoint_address;
    return 15;
}

int uhci_control_request(const usb_device_info_t* device,
                         uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, uint16_t length,
                         uint8_t* data, uint16_t* out_length) {
    uhci_calls.control_calls++;
    uhci_calls.device = device;
    uhci_calls.request_type = request_type;
    uhci_calls.request = request;
    uhci_calls.value = value;
    uhci_calls.index = index;
    uhci_calls.length = length;
    if (out_length) *out_length = length;
    if (data) data[0] = request;
    return 21;
}

int uhci_bulk_transfer(const usb_device_info_t* device,
                       uint8_t endpoint_address, uint8_t direction_in,
                       uint8_t* buffer, uint16_t length,
                       uint16_t* out_length) {
    uhci_calls.bulk_calls++;
    uhci_calls.device = device;
    uhci_calls.endpoint_address = endpoint_address;
    uhci_calls.direction_in = direction_in;
    uhci_calls.length = length;
    if (out_length) *out_length = length;
    if (buffer) buffer[0] = endpoint_address;
    return 22;
}

int uhci_reset_bulk_toggles(const usb_device_info_t* device) {
    uhci_calls.reset_calls++;
    uhci_calls.device = device;
    return 23;
}

int uhci_interrupt_submit(const usb_device_info_t* device,
                          uint8_t endpoint_address, uint16_t max_packet,
                          uint8_t interval, uhci_interrupt_callback_t callback,
                          void* context) {
    uhci_calls.submit_calls++;
    uhci_calls.device = device;
    uhci_calls.endpoint_address = endpoint_address;
    uhci_calls.max_packet = max_packet;
    uhci_calls.interval = interval;
    uhci_calls.callback = callback;
    uhci_calls.context = context;
    return 24;
}

int uhci_interrupt_cancel(const usb_device_info_t* device,
                          uint8_t endpoint_address) {
    uhci_calls.cancel_calls++;
    uhci_calls.device = device;
    uhci_calls.endpoint_address = endpoint_address;
    return 25;
}

static int test_invalid_and_unknown(void) {
    usb_device_info_t device = {0};
    uint8_t buffer[8] = {0};
    uint16_t out_length = 0U;
    int context = 1;

    if (usb_transport_control_request(0, 0U, 0U, 0U, 0U, 0U, buffer,
                                      &out_length) != ERR_NULL ||
        usb_transport_bulk_transfer(0, 0U, 0U, buffer, 0U, &out_length) !=
            ERR_NULL ||
        usb_transport_bulk_transfer(&device, 0U, 0U, 0, 0U, &out_length) !=
            ERR_NULL ||
        usb_transport_reset_bulk_toggles(0) != ERR_NULL ||
        usb_transport_interrupt_submit(0, 0U, 0U, 0U, interrupt_callback,
                                       &context) != ERR_NULL ||
        usb_transport_interrupt_submit(&device, 0U, 0U, 0U, 0, &context) !=
            ERR_NULL ||
        usb_transport_interrupt_cancel(0, 0U) != ERR_NULL) return 1;

    device.controller_model = USB_CONTROLLER_MODEL_OTHER;
    if (usb_transport_control_request(&device, 0U, 0U, 0U, 0U, 0U, buffer,
                                      &out_length) != ERR_UNAVAILABLE ||
        usb_transport_bulk_transfer(&device, 0U, 0U, buffer, 0U,
                                    &out_length) != ERR_UNAVAILABLE ||
        usb_transport_reset_bulk_toggles(&device) != ERR_UNAVAILABLE ||
        usb_transport_interrupt_submit(&device, 0U, 0U, 0U,
                                       interrupt_callback, &context) !=
            ERR_UNAVAILABLE ||
        usb_transport_interrupt_cancel(&device, 0U) != ERR_UNAVAILABLE) return 2;
    return 0;
}

static int test_ehci_dispatch(void) {
    usb_device_info_t device = {0};
    uint8_t buffer[8] = {0};
    uint16_t out_length = 0U;
    int context = 2;

    device.controller_model = USB_CONTROLLER_MODEL_EHCI;
    reset_calls();
    if (usb_transport_control_request(&device, 0x80U, 6U, 0x1234U, 2U, 4U,
                                      buffer, &out_length) != 11 ||
        out_length != 4U || buffer[0] != 6U || ehci_calls.control_calls != 1U ||
        ehci_calls.device != &device || ehci_calls.request_type != 0x80U ||
        ehci_calls.value != 0x1234U || ehci_calls.index != 2U ||
        ehci_calls.length != 4U) return 3;
    if (usb_transport_bulk_transfer(&device, 0x81U, 1U, buffer, 5U,
                                    &out_length) != 12 ||
        out_length != 5U || buffer[0] != 0x81U || ehci_calls.bulk_calls != 1U ||
        ehci_calls.endpoint_address != 0x81U || ehci_calls.direction_in != 1U ||
        ehci_calls.length != 5U) return 4;
    if (usb_transport_reset_bulk_toggles(&device) != 13 ||
        ehci_calls.reset_calls != 1U ||
        usb_transport_interrupt_submit(&device, 0x82U, 16U, 10U,
                                       interrupt_callback, &context) != 14 ||
        ehci_calls.submit_calls != 1U ||
        ehci_calls.callback != interrupt_callback ||
        ehci_calls.context != &context ||
        usb_transport_interrupt_cancel(&device, 0x82U) != 15 ||
        ehci_calls.cancel_calls != 1U || ehci_calls.endpoint_address != 0x82U)
        return 5;
    if (uhci_calls.control_calls != 0U || uhci_calls.bulk_calls != 0U ||
        uhci_calls.reset_calls != 0U || uhci_calls.submit_calls != 0U ||
        uhci_calls.cancel_calls != 0U) return 6;
    return 0;
}

static int test_uhci_dispatch(void) {
    usb_device_info_t device = {0};
    uint8_t buffer[8] = {0};
    uint16_t out_length = 0U;
    int context = 3;

    device.controller_model = USB_CONTROLLER_MODEL_UHCI;
    reset_calls();
    if (usb_transport_control_request(&device, 0x21U, 9U, 0x4321U, 3U, 6U,
                                      buffer, &out_length) != 21 ||
        out_length != 6U || buffer[0] != 9U || uhci_calls.control_calls != 1U ||
        uhci_calls.device != &device || uhci_calls.request_type != 0x21U ||
        uhci_calls.value != 0x4321U || uhci_calls.index != 3U ||
        uhci_calls.length != 6U) return 7;
    if (usb_transport_bulk_transfer(&device, 0x02U, 0U, buffer, 7U,
                                    &out_length) != 22 ||
        out_length != 7U || buffer[0] != 0x02U || uhci_calls.bulk_calls != 1U ||
        uhci_calls.endpoint_address != 0x02U || uhci_calls.direction_in != 0U ||
        uhci_calls.length != 7U) return 8;
    if (usb_transport_reset_bulk_toggles(&device) != 23 ||
        uhci_calls.reset_calls != 1U ||
        usb_transport_interrupt_submit(&device, 0x83U, 8U, 4U,
                                       interrupt_callback, &context) != 24 ||
        uhci_calls.submit_calls != 1U || uhci_calls.callback != interrupt_callback ||
        uhci_calls.context != &context ||
        usb_transport_interrupt_cancel(&device, 0x83U) != 25 ||
        uhci_calls.cancel_calls != 1U || uhci_calls.endpoint_address != 0x83U)
        return 9;
    if (ehci_calls.control_calls != 0U || ehci_calls.bulk_calls != 0U ||
        ehci_calls.reset_calls != 0U || ehci_calls.submit_calls != 0U ||
        ehci_calls.cancel_calls != 0U) return 10;
    return 0;
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    if (!result) result = test_invalid_and_unknown();
    if (!result) result = test_ehci_dispatch();
    if (!result) result = test_uhci_dispatch();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        printf("USB_TRANSPORT_HOST_FAIL:%d\n", result);
        return result;
    }
    printf("USB_TRANSPORT_HOST_PASS\n");
    return 0;
}
