#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/input.h"
#include "core/log.h"
#include "core/usb_manager.h"
#include "drivers/uhci.h"
#include "drivers/usb_hid.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U
#define FAKE_DEVICE_CAPACITY (USB_HID_MAX_DEVICES + 1U)
#define FAKE_EVENT_CAPACITY 32U
#define INVALID_INDEX UINT32_MAX

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static usb_device_info_t fake_devices[FAKE_DEVICE_CAPACITY];
static uint32_t fake_device_count;
static int fake_manager_count_result;
static uint32_t fake_get_device_error_index;
static int fake_get_device_error_result;
static uint32_t fake_control_calls;
static uint32_t fake_control_fail_call;
static int fake_control_fail_result;
static int fake_submit_result;
static int fake_cancel_result;
static uint32_t fake_submit_count;
static uint32_t fake_cancel_count;
static uhci_interrupt_callback_t fake_callbacks[USB_HID_MAX_DEVICES];
static void* fake_contexts[USB_HID_MAX_DEVICES];
static input_key_event_t key_events[FAKE_EVENT_CAPACITY];
static input_pointer_event_t pointer_events[FAKE_EVENT_CAPACITY];
static uint32_t key_event_count;
static uint32_t pointer_event_count;
static int fake_key_result;
static int fake_pointer_result;

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

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
}

int input_publish_key(const input_key_event_t* event) {
    if (!event) return ERR_NULL;
    if (fake_key_result != OK) return fake_key_result;
    if (key_event_count >= FAKE_EVENT_CAPACITY) return ERR_OVERFLOW;
    key_events[key_event_count++] = *event;
    return OK;
}

int input_publish_pointer(const input_pointer_event_t* event) {
    if (!event) return ERR_NULL;
    if (fake_pointer_result != OK) return fake_pointer_result;
    if (pointer_event_count >= FAKE_EVENT_CAPACITY) return ERR_OVERFLOW;
    pointer_events[pointer_event_count++] = *event;
    return OK;
}

int usb_manager_get_device_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    if (fake_manager_count_result != OK) return fake_manager_count_result;
    *out_count = fake_device_count;
    return OK;
}

int usb_manager_get_device(uint32_t index, usb_device_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (index >= fake_device_count) return ERR_NOT_FOUND;
    if (index == fake_get_device_error_index) return fake_get_device_error_result;
    *out_info = fake_devices[index];
    return OK;
}

int uhci_control_request(const usb_device_info_t* device,
                         uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, uint16_t length,
                         uint8_t* data, uint16_t* out_length) {
    (void)device;
    (void)request_type;
    (void)request;
    (void)value;
    (void)index;
    (void)length;
    (void)data;
    fake_control_calls++;
    if (out_length) *out_length = 0U;
    if (fake_control_fail_call &&
        fake_control_calls == fake_control_fail_call) {
        return fake_control_fail_result;
    }
    return OK;
}

int uhci_interrupt_submit(const usb_device_info_t* device,
                          uint8_t endpoint_address, uint16_t max_packet,
                          uint8_t interval,
                          uhci_interrupt_callback_t callback,
                          void* context) {
    (void)device;
    (void)endpoint_address;
    (void)max_packet;
    (void)interval;
    if (fake_submit_result != OK) return fake_submit_result;
    if (fake_submit_count >= USB_HID_MAX_DEVICES) return ERR_OVERFLOW;
    fake_callbacks[fake_submit_count] = callback;
    fake_contexts[fake_submit_count] = context;
    fake_submit_count++;
    return OK;
}

int uhci_interrupt_cancel(const usb_device_info_t* device,
                          uint8_t endpoint_address) {
    (void)device;
    (void)endpoint_address;
    fake_cancel_count++;
    return fake_cancel_result;
}

static void copy_id(char* destination, uint32_t capacity, const char* value) {
    strncpy(destination, value, capacity - 1U);
    destination[capacity - 1U] = '\0';
}

static void reset_fixtures(void) {
    memset(fake_devices, 0, sizeof(fake_devices));
    memset(fake_callbacks, 0, sizeof(fake_callbacks));
    memset(fake_contexts, 0, sizeof(fake_contexts));
    memset(key_events, 0, sizeof(key_events));
    memset(pointer_events, 0, sizeof(pointer_events));
    fake_device_count = 0U;
    fake_manager_count_result = OK;
    fake_get_device_error_index = INVALID_INDEX;
    fake_get_device_error_result = ERR_DISK;
    fake_control_calls = 0U;
    fake_control_fail_call = 0U;
    fake_control_fail_result = ERR_INVALID;
    fake_submit_result = OK;
    fake_cancel_result = OK;
    fake_submit_count = 0U;
    fake_cancel_count = 0U;
    key_event_count = 0U;
    pointer_event_count = 0U;
    fake_key_result = OK;
    fake_pointer_result = OK;
}

static void make_keyboard(usb_device_info_t* device, const char* id) {
    memset(device, 0, sizeof(*device));
    copy_id(device->id, USB_DEVICE_ID_SIZE, id);
    copy_id(device->controller_id, USB_PORT_CONTROLLER_ID_SIZE, "uhci-0");
    device->controller_bus = 1U;
    device->controller_device = 2U;
    device->controller_function = 0U;
    device->port_number = 1U;
    device->state = USB_DEVICE_CONFIGURED;
    device->speed = USB_DEVICE_SPEED_FULL;
    device->usb_address = 5U;
    device->controller_model = USB_CONTROLLER_MODEL_UHCI;
    device->interface_number = 0U;
    device->interface_class = 0x03U;
    device->interface_subclass = 0x01U;
    device->interface_protocol = 0x01U;
    device->interrupt_in_endpoint = 0x81U;
    device->interrupt_in_count = 1U;
    device->interrupt_in_max_packet = USB_HID_KEYBOARD_REPORT_SIZE;
    device->interrupt_interval = 10U;
}

static void make_mouse(usb_device_info_t* device, const char* id) {
    make_keyboard(device, id);
    device->port_number = 2U;
    device->interface_protocol = 0x02U;
    device->interrupt_in_endpoint = 0x82U;
    device->interrupt_in_max_packet = USB_HID_MOUSE_REPORT_MAX_SIZE;
}

static int get_info(uint32_t index, usb_hid_info_t* out_info) {
    return usb_hid_get_at(index, out_info);
}

static int invoke_report(uint32_t index, int result, const uint8_t* report,
                         uint16_t length) {
    if (index >= fake_submit_count || !fake_callbacks[index]) return ERR_STATE;
    fake_callbacks[index](fake_contexts[index], result, report, length);
    return OK;
}

static int test_contract_before_init(void) {
    uint32_t count = 0U;
    usb_hid_info_t info;

    reset_fixtures();
    if (usb_hid_refresh() != ERR_STATE ||
        usb_hid_get_count(NULL) != ERR_NULL ||
        usb_hid_get_count(&count) != ERR_STATE ||
        usb_hid_get_at(0U, NULL) != ERR_NULL ||
        usb_hid_get_at(0U, &info) != ERR_STATE ||
        usb_hid_find(NULL, &info) != ERR_NULL ||
        usb_hid_find("missing", NULL) != ERR_NULL ||
        usb_hid_find("missing", &info) != ERR_STATE ||
        usb_hid_validate_state() != ERR_STATE || usb_hid_is_active(NULL)) {
        return 10;
    }
    if (strcmp(usb_hid_kind_name(USB_HID_KIND_KEYBOARD), "KEYBOARD") != 0 ||
        strcmp(usb_hid_kind_name(USB_HID_KIND_MOUSE), "MOUSE") != 0 ||
        strcmp(usb_hid_kind_name((usb_hid_kind_t)99), "UNKNOWN") != 0 ||
        strcmp(usb_hid_state_name(USB_HID_STATE_READY), "READY") != 0 ||
        strcmp(usb_hid_state_name(USB_HID_STATE_DEGRADED), "DEGRADED") != 0 ||
        strcmp(usb_hid_state_name(USB_HID_STATE_DISABLED), "DISABLED") != 0 ||
        strcmp(usb_hid_state_name((usb_hid_state_t)99), "DISABLED") != 0) {
        return 11;
    }
    return 0;
}

static int test_keyboard_reports(void) {
    uint8_t first_report[USB_HID_KEYBOARD_REPORT_SIZE] = {0U};
    uint8_t repeated_report[USB_HID_KEYBOARD_REPORT_SIZE] = {0U};
    uint8_t modified_report[USB_HID_KEYBOARD_REPORT_SIZE] = {0U};
    uint8_t release_report[USB_HID_KEYBOARD_REPORT_SIZE] = {0U};
    uint8_t rollover_report[USB_HID_KEYBOARD_REPORT_SIZE] = {0U};
    uint8_t duplicate_report[USB_HID_KEYBOARD_REPORT_SIZE] = {0U};
    usb_hid_info_t info;

    reset_fixtures();
    make_keyboard(&fake_devices[0], "hid-keyboard");
    fake_device_count = 1U;
    first_report[2] = INPUT_USAGE_A;
    repeated_report[2] = INPUT_USAGE_A;
    modified_report[0] = 0x02U;
    modified_report[2] = INPUT_USAGE_A;
    rollover_report[2] = 0x01U;
    duplicate_report[2] = INPUT_USAGE_A;
    duplicate_report[3] = INPUT_USAGE_A;
    if (usb_hid_init() != OK || fake_control_calls != 2U ||
        fake_submit_count != 1U || usb_hid_get_count(NULL) != ERR_NULL ||
        usb_hid_get_count((uint32_t*)&fake_submit_count) != OK ||
        fake_submit_count != 1U || get_info(0U, &info) != OK ||
        info.kind != USB_HID_KIND_KEYBOARD || !info.active ||
        info.state != USB_HID_STATE_READY ||
        strcmp(info.id, "hid-keyboard") != 0 ||
        strcmp(info.controller_id, "uhci-0") != 0 ||
        !usb_hid_is_active("hid-keyboard") ||
        usb_hid_is_active("missing") || usb_hid_find("missing", &info) !=
        ERR_NOT_FOUND || usb_hid_validate_state() != OK) {
        return 20;
    }
    if (invoke_report(0U, OK, first_report, sizeof(first_report)) != OK ||
        key_event_count != 1U || key_events[0].usage != INPUT_USAGE_A ||
        !key_events[0].pressed ||
        key_events[0].source != INPUT_SOURCE_USB_HID) return 21;
    if (invoke_report(0U, OK, repeated_report, sizeof(repeated_report)) != OK ||
        key_event_count != 1U) return 22;
    if (invoke_report(0U, OK, modified_report, sizeof(modified_report)) != OK ||
        key_event_count != 2U || key_events[1].usage != INPUT_USAGE_LEFT_SHIFT ||
        !key_events[1].pressed) return 23;
    if (invoke_report(0U, OK, release_report, sizeof(release_report)) != OK ||
        key_event_count != 4U || key_events[2].usage != INPUT_USAGE_LEFT_SHIFT ||
        key_events[2].pressed || key_events[3].usage != INPUT_USAGE_A ||
        key_events[3].pressed) return 24;
    if (invoke_report(0U, OK, first_report, sizeof(first_report) - 1U) != OK ||
        get_info(0U, &info) != OK || info.malformed_count != 1U ||
        info.last_error != ERR_INVALID || info.state != USB_HID_STATE_DEGRADED) {
        return 25;
    }
    if (invoke_report(0U, OK, rollover_report, sizeof(rollover_report)) != OK ||
        invoke_report(0U, OK, duplicate_report, sizeof(duplicate_report)) != OK ||
        get_info(0U, &info) != OK || info.malformed_count != 3U) return 26;
    if (invoke_report(0U, OK, release_report, sizeof(release_report)) != OK ||
        get_info(0U, &info) != OK || info.state != USB_HID_STATE_READY ||
        info.last_error != OK || info.report_count != 8U) return 27;
    if (invoke_report(0U, ERR_TIMEOUT, NULL, 0U) != OK ||
        invoke_report(0U, ERR_INVALID, NULL, 0U) != OK ||
        get_info(0U, &info) != OK || info.timeout_count != 1U ||
        info.error_count != 1U || info.last_error != ERR_INVALID ||
        info.state != USB_HID_STATE_DEGRADED) return 28;
    if (invoke_report(0U, ERR_NOT_FOUND, NULL, 0U) != OK ||
        get_info(0U, &info) != OK || info.active ||
        info.state != USB_HID_STATE_DISABLED || info.last_error != ERR_NOT_FOUND ||
        info.cancel_count != 1U || usb_hid_is_active("hid-keyboard")) return 29;
    return 0;
}

static int test_mouse_reports(void) {
    uint8_t short_report[USB_HID_MOUSE_REPORT_MIN_SIZE] = {1U, 5U, 253U};
    uint8_t wheel_report[USB_HID_MOUSE_REPORT_MAX_SIZE] = {15U, 254U, 4U, 7U};
    uint8_t invalid_report[USB_HID_MOUSE_REPORT_MAX_SIZE] = {0U};
    usb_hid_info_t info;

    reset_fixtures();
    make_mouse(&fake_devices[0], "hid-mouse");
    fake_device_count = 1U;
    if (usb_hid_init() != OK || fake_submit_count != 1U ||
        invoke_report(0U, OK, short_report, sizeof(short_report)) != OK ||
        invoke_report(0U, OK, wheel_report, sizeof(wheel_report)) != OK ||
        pointer_event_count != 2U || pointer_events[0].dx != 5 ||
        pointer_events[0].dy != 3 || pointer_events[0].wheel != 0 ||
        pointer_events[0].buttons != 1U || pointer_events[1].dx != -2 ||
        pointer_events[1].dy != -4 || pointer_events[1].wheel != 7 ||
        pointer_events[1].buttons != 7U ||
        pointer_events[0].source != INPUT_SOURCE_USB_HID) return 40;
    if (invoke_report(0U, OK, invalid_report,
                      USB_HID_MOUSE_REPORT_MIN_SIZE - 1U) != OK ||
        invoke_report(0U, OK, invalid_report,
                      USB_HID_MOUSE_REPORT_MAX_SIZE + 1U) != OK ||
        get_info(0U, &info) != OK || info.malformed_count != 2U ||
        info.state != USB_HID_STATE_DEGRADED) return 41;
    fake_pointer_result = ERR_OVERFLOW;
    if (invoke_report(0U, OK, short_report, sizeof(short_report)) != OK ||
        get_info(0U, &info) != OK || info.dropped_count != 1U ||
        info.state != USB_HID_STATE_READY || info.last_error != OK) return 42;
    fake_pointer_result = ERR_DISK;
    if (invoke_report(0U, OK, short_report, sizeof(short_report)) != OK ||
        get_info(0U, &info) != OK || info.error_count != 1U ||
        info.last_error != ERR_DISK || info.state != USB_HID_STATE_DEGRADED) {
        return 43;
    }
    if (invoke_report(0U, OK, short_report, sizeof(short_report)) != OK ||
        invoke_report(0U, ERR_TIMEOUT, NULL, 0U) != OK ||
        get_info(0U, &info) != OK || info.timeout_count != 1U) return 44;
    return 0;
}

static int test_refresh_and_removal(void) {
    usb_hid_info_t info;

    reset_fixtures();
    make_keyboard(&fake_devices[0], "hid-reconfigure");
    fake_device_count = 1U;
    if (usb_hid_init() != OK) return 50;
    fake_devices[0].interrupt_in_endpoint = 0x83U;
    fake_devices[0].interrupt_interval = 20U;
    if (usb_hid_refresh() != OK || fake_cancel_count != 1U ||
        fake_submit_count != 2U || get_info(0U, &info) != OK ||
        info.interrupt_endpoint != 0x83U || info.interval != 20U ||
        !info.active || info.last_error != OK) return 51;
    fake_device_count = 0U;
    fake_cancel_result = ERR_INVALID;
    if (usb_hid_refresh() != OK || fake_cancel_count != 2U ||
        get_info(0U, &info) != OK || info.active ||
        info.state != USB_HID_STATE_DISABLED || info.last_error != ERR_NOT_FOUND ||
        info.cancel_count != 1U || usb_hid_validate_state() != OK) return 52;
    if (invoke_report(0U, OK, NULL, 0U) != OK || usb_hid_find("missing", &info) !=
        ERR_NOT_FOUND) return 53;
    return 0;
}

static int test_activation_failures(void) {
    usb_hid_info_t info;
    uint32_t count = 0U;

    reset_fixtures();
    make_keyboard(&fake_devices[0], "hid-control-1");
    fake_device_count = 1U;
    fake_control_fail_call = 1U;
    if (usb_hid_init() != ERR_INVALID || get_info(0U, &info) != OK ||
        info.active || info.state != USB_HID_STATE_DEGRADED ||
        info.last_error != ERR_INVALID || info.error_count != 1U) return 60;
    fake_control_fail_call = 0U;
    fake_control_calls = 0U;
    if (usb_hid_refresh() != OK || !usb_hid_is_active("hid-control-1")) return 61;

    reset_fixtures();
    make_keyboard(&fake_devices[0], "hid-control-2");
    fake_device_count = 1U;
    fake_control_fail_call = 2U;
    if (usb_hid_init() != ERR_INVALID || get_info(0U, &info) != OK ||
        info.error_count != 1U || info.last_error != ERR_INVALID) return 62;

    reset_fixtures();
    make_keyboard(&fake_devices[0], "hid-submit");
    fake_device_count = 1U;
    fake_submit_result = ERR_UNAVAILABLE;
    if (usb_hid_init() != ERR_UNAVAILABLE || get_info(0U, &info) != OK ||
        info.active || info.error_count != 1U ||
        info.last_error != ERR_UNAVAILABLE) return 63;

    reset_fixtures();
    make_keyboard(&fake_devices[0], "hid-manager-error");
    fake_device_count = 1U;
    fake_manager_count_result = ERR_UNAVAILABLE;
    if (usb_hid_init() != ERR_STATE ||
        usb_hid_get_count(&count) != OK || count != 0U) return 64;

    reset_fixtures();
    make_keyboard(&fake_devices[0], "hid-device-error");
    fake_device_count = 1U;
    fake_get_device_error_index = 0U;
    if (usb_hid_init() != OK || usb_hid_get_count(&count) != OK || count != 0U ||
        usb_hid_validate_state() != OK) return 65;
    return 0;
}

static int test_candidate_filters(void) {
    uint32_t count = 0U;
    usb_device_info_t* device = &fake_devices[0];
    uint32_t index;

    for (index = 0U; index < 12U; index++) {
        reset_fixtures();
        make_keyboard(device, "hid-invalid");
        if (index == 0U) device->state = USB_DEVICE_DEGRADED;
        if (index == 1U) device->controller_model = USB_CONTROLLER_MODEL_EHCI;
        if (index == 2U) device->interface_class = 0U;
        if (index == 3U) device->interface_subclass = 0U;
        if (index == 4U) device->interface_protocol = 0U;
        if (index == 5U) device->interrupt_in_count = 0U;
        if (index == 6U) device->interrupt_in_endpoint = 0x01U;
        if (index == 7U) device->interrupt_in_endpoint = 0x80U;
        if (index == 8U) device->interrupt_in_max_packet = 0U;
        if (index == 9U) device->interrupt_in_max_packet = 65U;
        if (index == 10U) device->interrupt_interval = 0U;
        if (index == 11U) device->interrupt_in_max_packet = 7U;
        fake_device_count = 1U;
        if (usb_hid_init() != OK || usb_hid_get_count(&count) != OK || count != 0U ||
            usb_hid_validate_state() != OK) return 70 + (int)index;
    }
    reset_fixtures();
    make_mouse(device, "hid-invalid-mouse");
    device->interrupt_in_max_packet = USB_HID_MOUSE_REPORT_MIN_SIZE - 1U;
    fake_device_count = 1U;
    if (usb_hid_init() != OK || usb_hid_get_count(&count) != OK || count != 0U) {
        return 82;
    }
    return 0;
}

static int test_capacity_and_public_limits(void) {
    uint32_t count = 0U;
    usb_hid_info_t info;

    reset_fixtures();
    for (uint32_t index = 0U; index < FAKE_DEVICE_CAPACITY; index++) {
        char id[USB_DEVICE_ID_SIZE];
        snprintf(id, sizeof(id), "hid-%u", (unsigned)index);
        make_keyboard(&fake_devices[index], id);
    }
    fake_device_count = FAKE_DEVICE_CAPACITY;
    if (usb_hid_init() != ERR_OVERFLOW || usb_hid_get_count(&count) != OK ||
        count != USB_HID_MAX_DEVICES || usb_hid_get_at(count, &info) != ERR_INVALID ||
        usb_hid_get_at(0U, NULL) != ERR_NULL || usb_hid_find(NULL, &info) != ERR_NULL ||
        usb_hid_find("absent", &info) != ERR_NOT_FOUND ||
        usb_hid_validate_state() != OK) return 90;
    return 0;
}

static int test_recovery_after_optional_failure(void) {
    uint32_t count = 0U;

    reset_fixtures();
    fake_device_count = 0U;
    if (usb_hid_init() != OK || usb_hid_get_count(&count) != OK || count != 0U ||
        usb_hid_refresh() != OK || usb_hid_validate_state() != OK) return 100;
    return 0;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:drivers:usb-hid|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:usb-hid|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:usb-hid|value=0x%08X\n",
           (uint32_t)result);
}

int main(void) {
    int result = 0;

    if (!result) result = test_contract_before_init();
    coverage_active = 1U;
    if (!result) result = test_keyboard_reports();
    if (!result) result = test_mouse_reports();
    if (!result) result = test_refresh_and_removal();
    if (!result) result = test_activation_failures();
    if (!result) result = test_candidate_filters();
    if (!result) result = test_capacity_and_public_limits();
    if (!result) result = test_recovery_after_optional_failure();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        printf("USB_HID_HOST_FAIL:%d\n", result);
        return result;
    }
    printf("USB_HID_HOST_PASS\n");
    return 0;
}
