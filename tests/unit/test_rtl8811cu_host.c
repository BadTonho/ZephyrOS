#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/usb_manager.h"
#include "drivers/rtl8811cu.h"
#include "fs/fs.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_FIRMWARE_SIZE 128U
#define HOST_FIRMWARE_HEADER_SIZE RTL8811CU_FIRMWARE_HEADER_SIZE

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t firmware_header[HOST_FIRMWARE_HEADER_SIZE];
static int fake_fs_info_result;
static int fake_fs_read_result;
static uint32_t fake_fs_size;
static uint32_t fake_fs_bytes_read;
static uint32_t host_log_count;

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
    printf("ZCOV_BEGIN|case=host:drivers:rtl8811cu|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:rtl8811cu|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:rtl8811cu|value=0x%08X\n",
           (uint32_t)result);
}

void kmemset(void* destination, uint8_t value, uint32_t size) {
    uint8_t* bytes = (uint8_t*)destination;

    if (!bytes) return;
    for (uint32_t index = 0U; index < size; index++) bytes[index] = value;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
    host_log_count++;
}

int fs_get_root_file_info(const char* filename, uint32_t* size_out,
                          uint8_t* attributes_out) {
    (void)filename;
    if (fake_fs_info_result != OK) return fake_fs_info_result;
    if (size_out) *size_out = fake_fs_size;
    if (attributes_out) *attributes_out = 0U;
    return OK;
}

int fs_read_file_range_at(const char* path, uint32_t offset,
                          uint8_t* buffer, uint32_t max_size,
                          uint32_t* bytes_read) {
    (void)path;
    (void)offset;
    if (fake_fs_read_result != OK) return fake_fs_read_result;
    if (!buffer || !bytes_read || max_size < HOST_FIRMWARE_HEADER_SIZE) {
        return ERR_INVALID;
    }
    for (uint32_t index = 0U; index < HOST_FIRMWARE_HEADER_SIZE; index++) {
        buffer[index] = firmware_header[index];
    }
    *bytes_read = fake_fs_bytes_read;
    return OK;
}

static void reset_firmware_fixture(void) {
    fake_fs_info_result = OK;
    fake_fs_read_result = OK;
    fake_fs_size = HOST_FIRMWARE_SIZE;
    fake_fs_bytes_read = HOST_FIRMWARE_HEADER_SIZE;
    host_log_count = 0U;
    kmemset(firmware_header, 0U, sizeof(firmware_header));
    firmware_header[0U] = 0x5AU;
    firmware_header[1U] = 0xA5U;
    firmware_header[4U] = 0x01U;
}

static usb_device_info_t valid_device(void) {
    usb_device_info_t device = {0};

    device.state = USB_DEVICE_CONFIGURED;
    device.speed = USB_DEVICE_SPEED_HIGH;
    device.controller_model = USB_CONTROLLER_MODEL_EHCI;
    device.vendor_id = RTL8811CU_VENDOR_ID;
    device.product_id = RTL8811CU_PRODUCT_ID;
    device.device_revision = RTL8811CU_DEVICE_REVISION;
    device.endpoint_count = 2U;
    device.bulk_in_endpoint = 0x81U;
    device.bulk_out_endpoint = 0x02U;
    device.bulk_in_count = 1U;
    device.bulk_out_count = 1U;
    device.bulk_in_max_packet = USB_ENDPOINT_MAX_PACKET_SIZE_HIGH;
    device.bulk_out_max_packet = USB_ENDPOINT_MAX_PACKET_SIZE_HIGH;
    device.device_descriptor_valid = 1U;
    device.configuration_descriptor_valid = 1U;
    return device;
}

static int check_preconditions(void) {
    rtl8811cu_status_t status;
    rtl8811cu_probe_info_t probe;
    rtl8811cu_scan_result_t scan[RTL8811CU_SCAN_RESULT_CAPACITY];
    usb_device_info_t device = {0};
    uint32_t count = 7U;
    char long_ssid[RTL8811CU_SSID_MAX_LENGTH + 2U];

    for (uint32_t index = 0U; index <= RTL8811CU_SSID_MAX_LENGTH; index++) {
        long_ssid[index] = 'a';
    }
    long_ssid[RTL8811CU_SSID_MAX_LENGTH + 1U] = '\0';
    if (rtl8811cu_get_status(NULL) != ERR_NULL) return 1;
    if (rtl8811cu_get_status(&status) != ERR_STATE) return 2;
    if (rtl8811cu_validate_state() != ERR_STATE) return 3;
    if (rtl8811cu_probe(NULL, &probe) != ERR_NULL) return 4;
    if (rtl8811cu_probe(&device, NULL) != ERR_NULL) return 5;
    if (rtl8811cu_scan(NULL, 1U, &count) != ERR_NULL) return 6;
    if (rtl8811cu_scan(scan, 1U, NULL) != ERR_NULL) return 7;
    if (rtl8811cu_scan(scan, 0U, &count) != ERR_INVALID) return 8;
    if (rtl8811cu_scan(scan, RTL8811CU_SCAN_RESULT_CAPACITY + 1U, &count) !=
        ERR_INVALID) return 9;
    if (rtl8811cu_scan(scan, 1U, &count) != ERR_STATE) return 10;
    if (rtl8811cu_connect_open(NULL) != ERR_NULL) return 11;
    if (rtl8811cu_connect_open("") != ERR_INVALID) return 12;
    if (rtl8811cu_connect_open(long_ssid) != ERR_INVALID) return 13;
    if (rtl8811cu_connect_open("zephyr") != ERR_STATE) return 14;
    if (rtl8811cu_init(&device, NULL) != ERR_NULL) return 15;
    if (rtl8811cu_state_name(RTL8811CU_STATE_DISABLED) == NULL ||
        rtl8811cu_state_name(RTL8811CU_STATE_INVENTORIED) == NULL ||
        rtl8811cu_state_name(RTL8811CU_STATE_UNSUPPORTED) == NULL ||
        rtl8811cu_state_name(RTL8811CU_STATE_READY) == NULL ||
        rtl8811cu_state_name(RTL8811CU_STATE_ERROR) == NULL ||
        rtl8811cu_state_name((rtl8811cu_state_t)0xFFU) == NULL) {
        return 16;
    }
    return 0;
}

static int check_probe(void) {
    rtl8811cu_probe_info_t probe;
    usb_device_info_t device = valid_device();

    device.vendor_id = 0U;
    if (rtl8811cu_probe(&device, &probe) != ERR_NOT_FOUND ||
        probe.matched || probe.result != ERR_NOT_FOUND) return 1;
    device = valid_device();
    device.product_id = 0U;
    if (rtl8811cu_probe(&device, &probe) != ERR_NOT_FOUND ||
        probe.matched) return 2;
    device = valid_device();
    device.device_revision = 0U;
    if (rtl8811cu_probe(&device, &probe) != ERR_UNAVAILABLE ||
        !probe.matched || probe.revision_supported ||
        probe.result != ERR_UNAVAILABLE) return 3;
    device = valid_device();
    device.state = USB_DEVICE_DEGRADED;
    if (rtl8811cu_probe(&device, &probe) != ERR_STATE ||
        !probe.matched || !probe.revision_supported) return 4;
    device = valid_device();
    device.device_descriptor_valid = 0U;
    if (rtl8811cu_probe(&device, &probe) != ERR_STATE) {
        printf("RTL8811CU_DESCRIPTOR=%d\n",
               rtl8811cu_probe(&device, &probe));
        return 5;
    }
    device = valid_device();
    device.configuration_descriptor_valid = 0U;
    if (rtl8811cu_probe(&device, &probe) != ERR_STATE) return 6;
    device = valid_device();
    device.controller_model = USB_CONTROLLER_MODEL_UHCI;
    if (rtl8811cu_probe(&device, &probe) != ERR_UNAVAILABLE) return 7;
    device = valid_device();
    device.speed = USB_DEVICE_SPEED_FULL;
    if (rtl8811cu_probe(&device, &probe) != ERR_UNAVAILABLE) return 8;
    device = valid_device();
    device.bulk_in_endpoint = 0U;
    if (rtl8811cu_probe(&device, &probe) != ERR_INVALID) return 9;
    device = valid_device();
    device.bulk_out_endpoint = 0U;
    if (rtl8811cu_probe(&device, &probe) != ERR_INVALID) return 10;
    device = valid_device();
    device.bulk_in_max_packet = 0U;
    if (rtl8811cu_probe(&device, &probe) != ERR_INVALID) return 11;
    device = valid_device();
    device.bulk_out_max_packet = 0U;
    if (rtl8811cu_probe(&device, &probe) != ERR_INVALID) return 12;
    device = valid_device();
    device.bulk_in_max_packet = USB_ENDPOINT_MAX_PACKET_SIZE_HIGH + 1U;
    if (rtl8811cu_probe(&device, &probe) != ERR_INVALID) return 13;
    device = valid_device();
    device.bulk_out_max_packet = USB_ENDPOINT_MAX_PACKET_SIZE_HIGH + 1U;
    if (rtl8811cu_probe(&device, &probe) != ERR_INVALID) return 14;
    device = valid_device();
    if (rtl8811cu_probe(&device, &probe) != OK ||
        !probe.matched || !probe.revision_supported || !probe.configured ||
        probe.bulk_in_count != 1U || probe.bulk_out_count != 1U ||
        probe.endpoint_count != 2U || probe.result != OK) return 15;
    return 0;
}

static int check_firmware_failures(usb_device_info_t* device,
                                   ethernet_interface_t* interface) {
    rtl8811cu_status_t status;

    reset_firmware_fixture();
    fake_fs_info_result = ERR_NOT_FOUND;
    if (rtl8811cu_init(device, interface) != ERR_NOT_FOUND ||
        rtl8811cu_get_status(&status) != OK ||
        status.state != RTL8811CU_STATE_ERROR ||
        status.last_error != ERR_NOT_FOUND ||
        status.firmware_available) return 1;
    reset_firmware_fixture();
    fake_fs_size = 0U;
    if (rtl8811cu_init(device, interface) != ERR_INVALID) return 2;
    reset_firmware_fixture();
    fake_fs_size = RTL8811CU_FIRMWARE_MAX_SIZE + 1U;
    if (rtl8811cu_init(device, interface) != ERR_INVALID) return 3;
    reset_firmware_fixture();
    fake_fs_size = HOST_FIRMWARE_HEADER_SIZE - 1U;
    if (rtl8811cu_init(device, interface) != ERR_INVALID) return 4;
    reset_firmware_fixture();
    fake_fs_read_result = ERR_DISK;
    if (rtl8811cu_init(device, interface) != ERR_DISK) return 5;
    reset_firmware_fixture();
    fake_fs_bytes_read = 0U;
    if (rtl8811cu_init(device, interface) != ERR_INVALID) return 6;
    reset_firmware_fixture();
    firmware_header[0U] = 0U;
    firmware_header[1U] = 0U;
    if (rtl8811cu_init(device, interface) != ERR_INVALID) return 7;
    reset_firmware_fixture();
    firmware_header[4U] = 0U;
    if (rtl8811cu_init(device, interface) != ERR_INVALID) return 8;
    return 0;
}

static int check_interface(usb_device_info_t* device,
                           ethernet_interface_t* interface) {
    rtl8811cu_status_t status;
    ethernet_driver_status_t driver_status;
    rtl8811cu_scan_result_t scan[RTL8811CU_SCAN_RESULT_CAPACITY];
    uint8_t buffer[32U];
    uint8_t pending = 1U;
    uint8_t received = 1U;
    uint16_t length = 1U;
    uint32_t count = 7U;
    char long_ssid[RTL8811CU_SSID_MAX_LENGTH + 2U];

    for (uint32_t index = 0U; index <= RTL8811CU_SSID_MAX_LENGTH; index++) {
        long_ssid[index] = 'b';
    }
    long_ssid[RTL8811CU_SSID_MAX_LENGTH + 1U] = '\0';
    reset_firmware_fixture();
    if (rtl8811cu_init(device, interface) != ERR_UNAVAILABLE) return 1;
    if (interface->get_driver_status == NULL ||
        interface->service_pending == NULL ||
        interface->rx_pending == NULL ||
        interface->receive_frame == NULL ||
        interface->send_frame == NULL ||
        interface->quiesce == NULL) return 2;
    if (rtl8811cu_get_status(&status) != OK ||
        status.state != RTL8811CU_STATE_ERROR ||
        status.firmware_available != 1U ||
        status.firmware_header_valid != 1U ||
        status.firmware_size != HOST_FIRMWARE_SIZE ||
        status.last_error != ERR_UNAVAILABLE) return 3;
    if (rtl8811cu_validate_state() != OK) return 4;
    if (interface->get_driver_status(interface->driver_context, NULL) !=
        ERR_NULL) {
        return 5;
    }
    if (interface->get_driver_status(interface->driver_context,
                                     &driver_status) != OK ||
        driver_status.initialized || driver_status.link_up ||
        driver_status.last_error != ERR_UNAVAILABLE) return 6;
    if (interface->service_pending(interface->driver_context) != OK) return 7;
    if (interface->rx_pending(interface->driver_context, NULL) != ERR_NULL) {
        return 8;
    }
    if (interface->rx_pending(interface->driver_context, &pending) !=
        ERR_UNAVAILABLE || pending != 0U) return 9;
    if (interface->receive_frame(interface->driver_context, NULL,
                                 sizeof(buffer), &length, &received) !=
        ERR_NULL) return 10;
    if (interface->receive_frame(interface->driver_context, buffer, 0U,
                                 &length, &received) != ERR_INVALID) return 11;
    if (interface->receive_frame(interface->driver_context, buffer,
                                 sizeof(buffer), &length, &received) !=
        ERR_UNAVAILABLE || length != 0U || received != 0U) return 12;
    if (interface->send_frame(interface->driver_context, NULL, 1U) !=
        ERR_INVALID) return 13;
    if (interface->send_frame(interface->driver_context, buffer, 0U) !=
        ERR_INVALID) return 14;
    if (interface->send_frame(interface->driver_context, buffer,
                               sizeof(buffer)) != ERR_UNAVAILABLE) return 15;
    if (interface->quiesce(interface->driver_context) != ERR_UNAVAILABLE) {
        return 16;
    }
    if (rtl8811cu_get_status(&status) != OK ||
        status.rx_errors != 1U || status.tx_errors != 1U ||
        status.last_error != ERR_UNAVAILABLE) return 17;
    if (rtl8811cu_scan(scan, 1U, &count) != ERR_UNAVAILABLE || count != 0U) {
        return 18;
    }
    if (rtl8811cu_scan(scan, RTL8811CU_SCAN_RESULT_CAPACITY, &count) !=
        ERR_UNAVAILABLE || count != 0U) return 19;
    if (rtl8811cu_connect_open(long_ssid) != ERR_INVALID) return 20;
    if (rtl8811cu_connect_open("zephyr") != ERR_UNAVAILABLE) return 21;
    if (interface->service_pending(interface->driver_context) != OK) return 22;
    if (rtl8811cu_state_name((rtl8811cu_state_t)0xFFU) == NULL) return 23;
    return 0;
}

static int check_init_states(void) {
    rtl8811cu_status_t status;
    ethernet_interface_t interface;
    usb_device_info_t device = valid_device();

    reset_firmware_fixture();
    device.vendor_id = 0U;
    if (rtl8811cu_init(&device, &interface) != ERR_NOT_FOUND ||
        rtl8811cu_get_status(&status) != OK ||
        status.state != RTL8811CU_STATE_DISABLED ||
        status.last_error != ERR_NOT_FOUND ||
        rtl8811cu_validate_state() != OK) return 1;
    reset_firmware_fixture();
    device = valid_device();
    device.device_revision = 0U;
    if (rtl8811cu_init(&device, &interface) != ERR_UNAVAILABLE ||
        rtl8811cu_get_status(&status) != OK ||
        status.state != RTL8811CU_STATE_UNSUPPORTED ||
        status.last_error != ERR_UNAVAILABLE ||
        rtl8811cu_validate_state() != OK) return 2;
    return 0;
}

int main(void) {
    int result;
    usb_device_info_t device;
    ethernet_interface_t interface;

    coverage_active = 1U;
    result = check_preconditions();
    if (result == 0) result = check_probe();
    if (result == 0) result = check_init_states();
    if (result == 0) {
        device = valid_device();
        result = check_firmware_failures(&device, &interface);
    }
    if (result == 0) {
        device = valid_device();
        result = check_interface(&device, &interface);
    }
    coverage_active = 0U;
    printf("RTL8811CU_HOST_RESULT=%d logs=%u\n", result, host_log_count);
    coverage_emit(result);
    return result;
}
