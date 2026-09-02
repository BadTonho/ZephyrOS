#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/usb_manager.h"
#include "drivers/ehci.h"
#include "drivers/pci.h"
#include "drivers/uhci.h"
#include "drivers/usb_hid.h"
#include "drivers/usb_msc.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U
#define FAKE_PCI_CAPACITY 3U
#define FAKE_USB_PORT_CAPACITY 2U
#define FAKE_USB_DEVICE_CAPACITY 2U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static pci_device_t fake_pci[FAKE_PCI_CAPACITY];
static uint8_t fake_pci_count;
static int fake_pci_count_result;
static int fake_pci_at_result;
static usb_uhci_status_t fake_uhci_status;
static usb_ehci_status_t fake_ehci_status;
static usb_port_info_t fake_uhci_ports[FAKE_USB_PORT_CAPACITY];
static usb_port_info_t fake_ehci_ports[FAKE_USB_PORT_CAPACITY];
static usb_device_info_t fake_uhci_devices[FAKE_USB_DEVICE_CAPACITY];
static usb_device_info_t fake_ehci_devices[FAKE_USB_DEVICE_CAPACITY];
static uint32_t fake_uhci_port_count;
static uint32_t fake_ehci_port_count;
static uint32_t fake_uhci_device_count;
static uint32_t fake_ehci_device_count;
static int fake_uhci_init_result;
static int fake_ehci_init_result;
static int fake_uhci_status_result;
static int fake_ehci_status_result;
static int fake_uhci_poll_result;
static int fake_ehci_poll_result;
static int fake_uhci_port_result;
static int fake_ehci_port_result;
static int fake_uhci_device_result;
static int fake_ehci_device_result;
static int fake_uhci_diag_result;
static uint32_t fake_uhci_poll_count;
static uint32_t fake_ehci_poll_count;
static uint32_t fake_msc_count;
static uint32_t fake_hid_count;
static int fake_msc_count_result;
static int fake_msc_init_result;
static int fake_msc_refresh_result;
static int fake_msc_get_at_result;
static int fake_msc_validate_result;
static int fake_hid_count_result;
static int fake_hid_init_result;
static int fake_hid_refresh_result;
static int fake_hid_get_at_result;
static int fake_hid_validate_result;
static usb_msc_info_t fake_msc_info;
static usb_hid_info_t fake_hid_info;
static recovery_component_t fake_recovery;
static int fake_recovery_get_available;
static int fake_recovery_mark_result;

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

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:core:usb-manager|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:usb-manager|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:usb-manager|value=0x%08X\n",
           (uint32_t)result);
}

int pci_get_device_count(uint8_t* out_count) {
    if (!out_count) return ERR_NULL;
    if (fake_pci_count_result != OK && fake_pci_count_result != ERR_OVERFLOW) {
        return fake_pci_count_result;
    }
    *out_count = fake_pci_count;
    return fake_pci_count_result;
}

int pci_get_device_at(uint8_t index, pci_device_t* out_device) {
    if (!out_device) return ERR_NULL;
    if (fake_pci_at_result != OK) return fake_pci_at_result;
    if (index >= fake_pci_count) return ERR_NOT_FOUND;
    *out_device = fake_pci[index];
    return OK;
}

int uhci_init(const pci_device_t* pci, const char* controller_id) {
    if (!pci || !controller_id) return ERR_NULL;
    return fake_uhci_init_result;
}

int uhci_poll(uint32_t budget, uint32_t* out_processed) {
    if (!out_processed) return ERR_NULL;
    *out_processed = budget < fake_uhci_poll_count ? budget : fake_uhci_poll_count;
    return fake_uhci_poll_result;
}

int uhci_get_status(uint8_t bus, uint8_t device, uint8_t function,
                    usb_uhci_status_t* out_status) {
    (void)bus;
    (void)device;
    (void)function;
    if (!out_status) return ERR_NULL;
    if (fake_uhci_status_result != OK) return fake_uhci_status_result;
    *out_status = fake_uhci_status;
    return OK;
}

int uhci_get_port_count(uint8_t bus, uint8_t device, uint8_t function,
                        uint32_t* out_count) {
    (void)bus;
    (void)device;
    (void)function;
    if (!out_count) return ERR_NULL;
    *out_count = fake_uhci_port_count;
    return OK;
}

int uhci_get_port(uint8_t bus, uint8_t device, uint8_t function,
                  uint32_t index, usb_port_info_t* out_info) {
    (void)bus;
    (void)device;
    (void)function;
    if (!out_info) return ERR_NULL;
    if (fake_uhci_port_result != OK) return fake_uhci_port_result;
    if (index >= fake_uhci_port_count) return ERR_NOT_FOUND;
    *out_info = fake_uhci_ports[index];
    return OK;
}

int uhci_log_port_diagnostics(uint8_t bus, uint8_t device, uint8_t function) {
    (void)bus;
    (void)device;
    (void)function;
    return fake_uhci_diag_result;
}

int uhci_get_device_count(uint8_t bus, uint8_t device, uint8_t function,
                          uint32_t* out_count) {
    (void)bus;
    (void)device;
    (void)function;
    if (!out_count) return ERR_NULL;
    *out_count = fake_uhci_device_count;
    return OK;
}

int uhci_get_device(uint8_t bus, uint8_t device, uint8_t function,
                    uint32_t index, usb_device_info_t* out_info) {
    (void)bus;
    (void)device;
    (void)function;
    if (!out_info) return ERR_NULL;
    if (fake_uhci_device_result != OK) return fake_uhci_device_result;
    if (index >= fake_uhci_device_count) return ERR_NOT_FOUND;
    *out_info = fake_uhci_devices[index];
    return OK;
}

int uhci_validate_state(uint8_t bus, uint8_t device, uint8_t function) {
    (void)bus;
    (void)device;
    (void)function;
    return OK;
}

int ehci_init(const pci_device_t* pci, const char* controller_id) {
    if (!pci || !controller_id) return ERR_NULL;
    return fake_ehci_init_result;
}

int ehci_poll(uint32_t budget, uint32_t* out_processed) {
    if (!out_processed) return ERR_NULL;
    *out_processed = budget < fake_ehci_poll_count ? budget : fake_ehci_poll_count;
    return fake_ehci_poll_result;
}

int ehci_get_status(uint8_t bus, uint8_t device, uint8_t function,
                    usb_ehci_status_t* out_status) {
    (void)bus;
    (void)device;
    (void)function;
    if (!out_status) return ERR_NULL;
    if (fake_ehci_status_result != OK) return fake_ehci_status_result;
    *out_status = fake_ehci_status;
    return OK;
}

int ehci_get_port_count(uint8_t bus, uint8_t device, uint8_t function,
                        uint32_t* out_count) {
    (void)bus;
    (void)device;
    (void)function;
    if (!out_count) return ERR_NULL;
    *out_count = fake_ehci_port_count;
    return OK;
}

int ehci_get_port(uint8_t bus, uint8_t device, uint8_t function,
                  uint32_t index, usb_port_info_t* out_info) {
    (void)bus;
    (void)device;
    (void)function;
    if (!out_info) return ERR_NULL;
    if (fake_ehci_port_result != OK) return fake_ehci_port_result;
    if (index >= fake_ehci_port_count) return ERR_NOT_FOUND;
    *out_info = fake_ehci_ports[index];
    return OK;
}

int ehci_get_device_count(uint8_t bus, uint8_t device, uint8_t function,
                          uint32_t* out_count) {
    (void)bus;
    (void)device;
    (void)function;
    if (!out_count) return ERR_NULL;
    *out_count = fake_ehci_device_count;
    return OK;
}

int ehci_get_device(uint8_t bus, uint8_t device, uint8_t function,
                    uint32_t index, usb_device_info_t* out_info) {
    (void)bus;
    (void)device;
    (void)function;
    if (!out_info) return ERR_NULL;
    if (fake_ehci_device_result != OK) return fake_ehci_device_result;
    if (index >= fake_ehci_device_count) return ERR_NOT_FOUND;
    *out_info = fake_ehci_devices[index];
    return OK;
}

int ehci_validate_state(uint8_t bus, uint8_t device, uint8_t function) {
    (void)bus;
    (void)device;
    (void)function;
    return OK;
}

int usb_msc_init(void) {
    return fake_msc_init_result;
}

int usb_msc_refresh(void) {
    return fake_msc_refresh_result;
}

int usb_msc_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    if (fake_msc_count_result != OK) return fake_msc_count_result;
    *out_count = fake_msc_count;
    return OK;
}

int usb_msc_get_at(uint32_t index, usb_msc_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (fake_msc_get_at_result != OK) return fake_msc_get_at_result;
    if (index >= fake_msc_count) return ERR_NOT_FOUND;
    *out_info = fake_msc_info;
    return OK;
}

int usb_msc_find(const char* id, usb_msc_info_t* out_info) {
    if (!id || !out_info) return ERR_NULL;
    if (strcmp(id, fake_msc_info.id) != 0) return ERR_NOT_FOUND;
    *out_info = fake_msc_info;
    return OK;
}

int usb_msc_is_active(const char* id) {
    return id && fake_msc_count && strcmp(id, fake_msc_info.id) == 0 &&
           fake_msc_info.state == USB_MSC_READY;
}

int usb_msc_validate_state(void) {
    return fake_msc_validate_result;
}

int usb_hid_init(void) {
    return fake_hid_init_result;
}

int usb_hid_refresh(void) {
    return fake_hid_refresh_result;
}

int usb_hid_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    if (fake_hid_count_result != OK) return fake_hid_count_result;
    *out_count = fake_hid_count;
    return OK;
}

int usb_hid_get_at(uint32_t index, usb_hid_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (fake_hid_get_at_result != OK) return fake_hid_get_at_result;
    if (index >= fake_hid_count) return ERR_NOT_FOUND;
    *out_info = fake_hid_info;
    return OK;
}

int usb_hid_find(const char* id, usb_hid_info_t* out_info) {
    if (!id || !out_info) return ERR_NULL;
    if (strcmp(id, fake_hid_info.id) != 0) return ERR_NOT_FOUND;
    *out_info = fake_hid_info;
    return OK;
}

int usb_hid_is_active(const char* id) {
    return id && fake_hid_count && strcmp(id, fake_hid_info.id) == 0 &&
           fake_hid_info.active;
}

int usb_hid_validate_state(void) {
    return fake_hid_validate_result;
}

const recovery_component_t* recovery_get(recovery_component_id_t component) {
    if (!fake_recovery_get_available || component != RECOVERY_COMPONENT_USB) {
        return NULL;
    }
    return &fake_recovery;
}

int recovery_mark_ready(recovery_component_id_t component) {
    if (component != RECOVERY_COMPONENT_USB) return ERR_INVALID;
    fake_recovery.state = RECOVERY_STATE_READY;
    fake_recovery.last_error = OK;
    return fake_recovery_mark_result;
}

int recovery_mark_degraded(recovery_component_id_t component, int error_code,
                           const char* message) {
    if (component != RECOVERY_COMPONENT_USB) return ERR_INVALID;
    fake_recovery.state = RECOVERY_STATE_DEGRADED;
    fake_recovery.last_error = error_code;
    fake_recovery.last_message = message;
    return fake_recovery_mark_result;
}

int recovery_mark_disabled(recovery_component_id_t component, int error_code,
                           const char* message) {
    if (component != RECOVERY_COMPONENT_USB) return ERR_INVALID;
    fake_recovery.state = RECOVERY_STATE_DISABLED;
    fake_recovery.last_error = error_code;
    fake_recovery.last_message = message;
    return fake_recovery_mark_result;
}

static void set_controller(pci_device_t* device, uint8_t bus, uint8_t number,
                           uint8_t function, uint8_t prog_if) {
    memset(device, 0, sizeof(*device));
    device->vendor_id = 0x1234U;
    device->device_id = 0x5678U;
    device->class = USB_CONTROLLER_PCI_CLASS;
    device->subclass = USB_CONTROLLER_PCI_SUBCLASS;
    device->prog_if = prog_if;
    device->revision = 2U;
    device->irq = 11U;
    device->bus = bus;
    device->device = number;
    device->function = function;
    device->bar0 = 0x1000U;
    device->bar1 = 0x2000U;
    device->present = 1U;
}

static void set_device(usb_device_info_t* device, const char* id,
                       const char* controller_id, uint8_t bus,
                       uint8_t controller_device, uint8_t function,
                       uint8_t port, uint8_t address,
                       usb_controller_model_t model, usb_device_speed_t speed) {
    memset(device, 0, sizeof(*device));
    strncpy(device->id, id, sizeof(device->id) - 1U);
    strncpy(device->controller_id, controller_id,
            sizeof(device->controller_id) - 1U);
    device->controller_bus = bus;
    device->controller_device = controller_device;
    device->controller_function = function;
    device->port_number = port;
    device->state = USB_DEVICE_CONFIGURED;
    device->speed = speed;
    device->usb_address = address;
    device->controller_model = model;
    device->vendor_id = 0x1111U;
    device->product_id = 0x2222U;
    device->device_revision = 1U;
    device->device_class = 0U;
    device->device_subclass = 0U;
    device->device_protocol = 0U;
    device->max_packet_size0 = speed == USB_DEVICE_SPEED_HIGH ? 64U : 8U;
    device->num_configurations = 1U;
    device->configuration_length = 32U;
    device->configuration_value = 1U;
    device->device_descriptor_valid = 1U;
    device->configuration_descriptor_valid = 1U;
    device->endpoint_count = 1U;
    device->endpoints[0].address = 1U;
    device->endpoints[0].transfer_type = 2U;
    device->endpoints[0].max_packet = speed == USB_DEVICE_SPEED_HIGH ? 512U : 64U;
    device->endpoints[0].interval = 1U;
}

static void set_port(usb_port_info_t* port, const char* controller_id,
                     uint8_t bus, uint8_t device, uint8_t function,
                     usb_controller_model_t model, uint8_t port_number,
                     const char* device_id, uint8_t configured) {
    memset(port, 0, sizeof(*port));
    strncpy(port->controller_id, controller_id,
            sizeof(port->controller_id) - 1U);
    port->controller_bus = bus;
    port->controller_device = device;
    port->controller_function = function;
    port->controller_model = model;
    port->port_number = port_number;
    port->state = configured ? USB_PORT_CONFIGURED : USB_PORT_EMPTY;
    port->reason = configured ? USB_PORT_REASON_NONE : USB_PORT_REASON_NO_DEVICE;
    port->speed = configured ? USB_DEVICE_SPEED_FULL : USB_DEVICE_SPEED_LOW;
    port->connected = configured;
    port->enabled = configured;
    port->usb_address = configured ? 7U : 0U;
    if (device_id) strncpy(port->device_id, device_id,
                           sizeof(port->device_id) - 1U);
}

static void reset_fixtures(void) {
    memset(fake_pci, 0, sizeof(fake_pci));
    memset(&fake_uhci_status, 0, sizeof(fake_uhci_status));
    memset(&fake_ehci_status, 0, sizeof(fake_ehci_status));
    memset(fake_uhci_ports, 0, sizeof(fake_uhci_ports));
    memset(fake_ehci_ports, 0, sizeof(fake_ehci_ports));
    memset(fake_uhci_devices, 0, sizeof(fake_uhci_devices));
    memset(fake_ehci_devices, 0, sizeof(fake_ehci_devices));
    memset(&fake_msc_info, 0, sizeof(fake_msc_info));
    memset(&fake_hid_info, 0, sizeof(fake_hid_info));
    memset(&fake_recovery, 0, sizeof(fake_recovery));
    set_controller(&fake_pci[0], 1U, 2U, 3U, USB_CONTROLLER_PROG_IF_UHCI);
    set_controller(&fake_pci[1], 2U, 4U, 0U, USB_CONTROLLER_PROG_IF_EHCI);
    set_controller(&fake_pci[2], 3U, 5U, 0U, 0x80U);
    set_device(&fake_uhci_devices[0], "usb-uhci-device", "usb-pci-01:02.3",
               1U, 2U, 3U, 1U, 7U, USB_CONTROLLER_MODEL_UHCI,
               USB_DEVICE_SPEED_FULL);
    set_device(&fake_ehci_devices[0], "usb-ehci-device", "usb-pci-02:04.0",
               2U, 4U, 0U, 1U, 8U, USB_CONTROLLER_MODEL_EHCI,
               USB_DEVICE_SPEED_HIGH);
    set_port(&fake_uhci_ports[0], "usb-pci-01:02.3", 1U, 2U, 3U,
             USB_CONTROLLER_MODEL_UHCI, 1U, "usb-uhci-device", 1U);
    set_port(&fake_uhci_ports[1], "usb-pci-01:02.3", 1U, 2U, 3U,
             USB_CONTROLLER_MODEL_UHCI, 2U, NULL, 0U);
    set_port(&fake_ehci_ports[0], "usb-pci-02:04.0", 2U, 4U, 0U,
             USB_CONTROLLER_MODEL_EHCI, 1U, "usb-ehci-device", 1U);
    fake_uhci_port_count = 2U;
    fake_ehci_port_count = 1U;
    fake_uhci_device_count = 1U;
    fake_ehci_device_count = 1U;
    fake_uhci_status.initialized = 1U;
    fake_uhci_status.running = 1U;
    fake_uhci_status.irq_registered = 1U;
    fake_uhci_status.dma_ready = 1U;
    fake_uhci_status.control_transfer_ready = 1U;
    fake_uhci_status.bulk_transfer_ready = 1U;
    fake_uhci_status.interrupt_transfer_ready = 1U;
    fake_uhci_status.port_count = 2U;
    fake_uhci_status.device_count = 1U;
    fake_uhci_status.td_capacity = 8U;
    fake_uhci_status.td_in_use = 2U;
    fake_uhci_status.buffer_capacity = 4U;
    fake_uhci_status.buffer_in_use = 1U;
    fake_ehci_status.initialized = 1U;
    fake_ehci_status.running = 1U;
    fake_ehci_status.irq_registered = 1U;
    fake_ehci_status.dma_ready = 1U;
    fake_ehci_status.control_transfer_ready = 1U;
    fake_ehci_status.bulk_transfer_ready = 1U;
    fake_ehci_status.interrupt_transfer_ready = 1U;
    fake_ehci_status.port_count = 1U;
    fake_ehci_status.device_count = 1U;
    fake_ehci_status.qtd_capacity = 8U;
    fake_ehci_status.qtd_in_use = 2U;
    fake_ehci_status.buffer_capacity = 4U;
    fake_ehci_status.buffer_in_use = 1U;
    strncpy(fake_msc_info.id, "usb-uhci-device", sizeof(fake_msc_info.id) - 1U);
    fake_msc_info.state = USB_MSC_READY;
    strncpy(fake_hid_info.id, "usb-ehci-device", sizeof(fake_hid_info.id) - 1U);
    fake_hid_info.state = USB_HID_STATE_READY;
    fake_hid_info.active = 1U;
    fake_recovery.name = "usb";
    fake_recovery.state = RECOVERY_STATE_UNKNOWN;
    fake_recovery.last_error = OK;
    fake_recovery_get_available = 1;
    fake_recovery_mark_result = OK;
    fake_pci_count = FAKE_PCI_CAPACITY;
    fake_pci_count_result = OK;
    fake_pci_at_result = OK;
    fake_uhci_init_result = OK;
    fake_ehci_init_result = OK;
    fake_uhci_status_result = OK;
    fake_ehci_status_result = OK;
    fake_uhci_poll_result = OK;
    fake_ehci_poll_result = OK;
    fake_uhci_port_result = OK;
    fake_ehci_port_result = OK;
    fake_uhci_device_result = OK;
    fake_ehci_device_result = OK;
    fake_uhci_diag_result = OK;
    fake_uhci_poll_count = 2U;
    fake_ehci_poll_count = 3U;
    fake_msc_count = 1U;
    fake_hid_count = 1U;
    fake_msc_count_result = OK;
    fake_msc_init_result = OK;
    fake_msc_refresh_result = OK;
    fake_msc_get_at_result = OK;
    fake_msc_validate_result = OK;
    fake_hid_count_result = OK;
    fake_hid_init_result = OK;
    fake_hid_refresh_result = OK;
    fake_hid_get_at_result = OK;
    fake_hid_validate_result = OK;
}

static int check_before_initialization(void) {
    usb_manager_status_t status;
    usb_controller_info_t controller;
    usb_port_info_t port;
    usb_device_info_t device;
    uint32_t count = 0U;
    uint32_t processed = 0U;

    if (usb_manager_get_status(NULL) != ERR_NULL ||
        usb_manager_get_status(&status) != ERR_STATE ||
        usb_manager_get_count(NULL) != ERR_NULL ||
        usb_manager_get_count(&count) != ERR_STATE ||
        usb_manager_get_info(0U, NULL) != ERR_NULL ||
        usb_manager_get_info(0U, &controller) != ERR_STATE ||
        usb_manager_find(NULL, &controller) != ERR_NULL ||
        usb_manager_find("usb-pci-01:02.3", NULL) != ERR_NULL ||
        usb_manager_find("usb-pci-01:02.3", &controller) != ERR_STATE ||
        usb_manager_poll(1U, NULL) != ERR_NULL ||
        usb_manager_poll(1U, &processed) != ERR_STATE ||
        usb_manager_get_uhci_status(0U, NULL) != ERR_NULL ||
        usb_manager_get_port_count(NULL) != ERR_NULL ||
        usb_manager_get_port_count(&count) != ERR_STATE ||
        usb_manager_get_port(0U, NULL) != ERR_NULL ||
        usb_manager_get_port(0U, &port) != ERR_STATE ||
        usb_manager_get_device_count(NULL) != ERR_NULL ||
        usb_manager_get_device_count(&count) != ERR_STATE ||
        usb_manager_get_device(0U, NULL) != ERR_NULL ||
        usb_manager_get_device(0U, &device) != ERR_STATE ||
        usb_manager_find_device(NULL, &device) != ERR_NULL ||
        usb_manager_find_device("usb-uhci-device", NULL) != ERR_NULL ||
        usb_manager_find_device("usb-uhci-device", &device) != ERR_STATE ||
        usb_manager_validate_state() != ERR_STATE) return 1;
    if (usb_manager_format_text(NULL, NULL) != ERR_NULL ||
        usb_manager_format_device_text(NULL, NULL) != ERR_NULL) return 2;
    if (strcmp(usb_manager_model_name(USB_CONTROLLER_MODEL_UHCI), "UHCI") != 0 ||
        strcmp(usb_manager_model_name(USB_CONTROLLER_MODEL_EHCI), "EHCI") != 0 ||
        strcmp(usb_manager_model_name(USB_CONTROLLER_MODEL_OTHER), "OUTRO") != 0 ||
        strcmp(usb_manager_state_name(USB_CONTROLLER_READY), "READY") != 0 ||
        strcmp(usb_manager_state_name(USB_CONTROLLER_DEGRADED), "DEGRADED") != 0 ||
        strcmp(usb_manager_state_name(USB_CONTROLLER_DISABLED), "DISABLED") != 0 ||
        strcmp(usb_manager_state_name((usb_controller_state_t)99U), "UNKNOWN") != 0 ||
        strcmp(usb_manager_reason_name(USB_CONTROLLER_REASON_DRIVER_NOT_INITIALIZED),
               "DRIVER_NOT_INITIALIZED") != 0 ||
        strcmp(usb_manager_reason_name(USB_CONTROLLER_REASON_DRIVER_READY),
               "DRIVER_READY") != 0 ||
        strcmp(usb_manager_reason_name(USB_CONTROLLER_REASON_DRIVER_FAILURE),
               "DRIVER_FAILURE") != 0 ||
        strcmp(usb_manager_reason_name(USB_CONTROLLER_REASON_PORT_FAILURE),
               "PORT_FAILURE") != 0 ||
        strcmp(usb_manager_reason_name(USB_CONTROLLER_REASON_OUT_OF_SCOPE),
               "OUT_OF_SCOPE") != 0 ||
        strcmp(usb_manager_reason_name((usb_controller_reason_t)99U), "UNKNOWN") != 0)
        return 3;
    if (strcmp(usb_manager_uhci_state_name(USB_UHCI_STATE_READY), "READY") != 0 ||
        strcmp(usb_manager_uhci_state_name(USB_UHCI_STATE_DEGRADED), "DEGRADED") != 0 ||
        strcmp(usb_manager_uhci_state_name(USB_UHCI_STATE_DISABLED), "DISABLED") != 0 ||
        strcmp(usb_manager_port_state_name(USB_PORT_EMPTY), "EMPTY") != 0 ||
        strcmp(usb_manager_port_state_name(USB_PORT_RESETTING), "RESETTING") != 0 ||
        strcmp(usb_manager_port_state_name(USB_PORT_ENUMERATING), "ENUMERATING") != 0 ||
        strcmp(usb_manager_port_state_name(USB_PORT_CONFIGURED), "CONFIGURED") != 0 ||
        strcmp(usb_manager_port_state_name(USB_PORT_DEGRADED), "DEGRADED") != 0 ||
        strcmp(usb_manager_port_state_name((usb_port_state_t)99U), "UNKNOWN") != 0)
        return 4;
    if (strcmp(usb_manager_port_reason_name(USB_PORT_REASON_NONE), "NONE") != 0 ||
        strcmp(usb_manager_port_reason_name(USB_PORT_REASON_NO_DEVICE), "NO_DEVICE") != 0 ||
        strcmp(usb_manager_port_reason_name(USB_PORT_REASON_RESET_TIMEOUT), "RESET_TIMEOUT") != 0 ||
        strcmp(usb_manager_port_reason_name(USB_PORT_REASON_CONTROL_TIMEOUT), "CONTROL_TIMEOUT") != 0 ||
        strcmp(usb_manager_port_reason_name(USB_PORT_REASON_INVALID_DESCRIPTOR), "INVALID_DESCRIPTOR") != 0 ||
        strcmp(usb_manager_port_reason_name(USB_PORT_REASON_UNSUPPORTED_SPEED), "UNSUPPORTED_SPEED") != 0 ||
        strcmp(usb_manager_port_reason_name(USB_PORT_REASON_UNSUPPORTED_LAYOUT), "UNSUPPORTED_LAYOUT") != 0 ||
        strcmp(usb_manager_port_reason_name(USB_PORT_REASON_DRIVER_FAILURE), "DRIVER_FAILURE") != 0 ||
        strcmp(usb_manager_port_reason_name((usb_port_reason_t)99U), "UNKNOWN") != 0 ||
        strcmp(usb_manager_speed_name(USB_DEVICE_SPEED_LOW), "LOW") != 0 ||
        strcmp(usb_manager_speed_name(USB_DEVICE_SPEED_HIGH), "HIGH") != 0 ||
        strcmp(usb_manager_speed_name(USB_DEVICE_SPEED_FULL), "FULL") != 0)
        return 5;
    return 0;
}

static int check_initialized_state(void) {
    usb_manager_status_t status;
    usb_controller_info_t controller;
    usb_controller_text_t controller_text;
    usb_device_info_t device;
    usb_device_text_t device_text;
    usb_port_info_t port;
    usb_uhci_status_t uhci_status;
    uint32_t count = 0U;
    uint32_t processed = 0U;

    if (usb_manager_init() != OK) return 10;
    if (usb_manager_get_status(&status) != OK || !status.initialized ||
        status.controller_count != 3U || status.uhci_count != 1U ||
        status.ehci_count != 1U || status.other_count != 1U ||
        status.uhci_ready_count != 1U || status.ehci_ready_count != 1U ||
        status.port_count != 3U || status.configured_device_count != 2U ||
        status.msc_device_count != 1U || status.hid_device_count != 1U ||
        status.hid_active_count != 1U || !status.class_driver_active ||
        !status.dma_initialized || !status.irq_initialized ||
        !status.transfer_available || !status.bulk_transfer_available ||
        !status.high_speed_transfer_available ||
        !status.interrupt_transfer_available || status.last_error != ERR_UNAVAILABLE)
        return 11;
    if (usb_manager_validate_state() != OK ||
        usb_manager_get_count(&count) != OK || count != 3U ||
        usb_manager_get_port_count(&count) != OK || count != 3U ||
        usb_manager_get_device_count(&count) != OK || count != 2U) return 12;
    if (usb_manager_get_info(0U, &controller) != OK ||
        controller.model != USB_CONTROLLER_MODEL_UHCI ||
        controller.state != USB_CONTROLLER_READY ||
        controller.reason != USB_CONTROLLER_REASON_DRIVER_READY ||
        controller.uhci_initialized != 1U ||
        usb_manager_get_info(1U, &controller) != OK ||
        controller.model != USB_CONTROLLER_MODEL_EHCI ||
        controller.state != USB_CONTROLLER_READY ||
        controller.ehci_initialized != 1U ||
        usb_manager_get_info(2U, &controller) != OK ||
        controller.model != USB_CONTROLLER_MODEL_OTHER ||
        controller.state != USB_CONTROLLER_DEGRADED ||
        controller.reason != USB_CONTROLLER_REASON_OUT_OF_SCOPE ||
        usb_manager_get_info(3U, &controller) != ERR_INVALID) return 13;
    if (usb_manager_find("USB-Pci-01-02.3", &controller) != OK ||
        controller.model != USB_CONTROLLER_MODEL_UHCI ||
        usb_manager_find("usb-pci-ff:ff.0", &controller) != ERR_NOT_FOUND ||
        usb_manager_get_info(2U, &controller) != OK) return 14;
    if (usb_manager_get_uhci_status(0U, &uhci_status) != OK ||
        uhci_status.td_capacity != 8U ||
        usb_manager_get_uhci_status(1U, &uhci_status) != ERR_UNAVAILABLE ||
        usb_manager_get_uhci_status(3U, &uhci_status) != ERR_INVALID) return 15;
    if (usb_manager_get_port(0U, &port) != OK ||
        port.state != USB_PORT_CONFIGURED ||
        strcmp(port.device_id, "usb-uhci-device") != 0 ||
        usb_manager_get_port(1U, &port) != OK || port.state != USB_PORT_EMPTY ||
        usb_manager_get_port(3U, &port) != ERR_INVALID) return 16;
    if (usb_manager_get_device(0U, &device) != OK ||
        !device.class_driver_active || device.hid_driver_active ||
        usb_manager_get_device(1U, &device) != OK ||
        device.class_driver_active || !device.hid_driver_active ||
        usb_manager_get_device(2U, &device) != ERR_INVALID ||
        usb_manager_find_device("USB-UHCI-DEVICE", &device) != OK ||
        strcmp(device.id, "usb-uhci-device") != 0 ||
        usb_manager_find_device("usb-unknown", &device) != ERR_NOT_FOUND) return 17;
    if (usb_manager_format_text(&controller, &controller_text) != OK) return 180;
    if (strcmp(controller_text.id, "usb-pci-03:05.0") != 0 ||
        strcmp(controller_text.name, "USB Controller desconhecido") != 0 ||
        strcmp(controller_text.detail, "Controlador fora do escopo") != 0 ||
        strcmp(controller_text.location, "PCI 03:05.0") != 0 ||
        usb_manager_format_device_text(&device, &device_text) != OK ||
        strcmp(device_text.detail, "Driver MSC ativo") != 0) return 18;
    device.class_driver_active = 1U;
    device.hid_driver_active = 1U;
    if (usb_manager_format_device_text(&device, &device_text) != OK ||
        strcmp(device_text.detail, "Drivers MSC e HID ativos") != 0) return 19;
    device.class_driver_active = 0U;
    if (usb_manager_format_device_text(&device, &device_text) != OK ||
        strcmp(device_text.detail, "Driver HID ativo") != 0) return 20;
    device.hid_driver_active = 0U;
    if (usb_manager_format_device_text(&device, &device_text) != OK ||
        strcmp(device_text.detail, "Configurado; sem driver de classe USB") != 0) return 21;
    if (usb_manager_poll(4U, &processed) != OK || processed != 4U ||
        usb_manager_poll(4U, NULL) != ERR_NULL) return 22;
    return 0;
}

static int check_failure_recovery(void) {
    usb_manager_status_t status;

    fake_uhci_poll_result = ERR_TIMEOUT;
    if (usb_manager_poll(4U, &(uint32_t){0U}) != ERR_TIMEOUT) return 30;
    fake_uhci_poll_result = OK;
    fake_ehci_poll_result = ERR_TIMEOUT;
    if (usb_manager_poll(4U, &(uint32_t){0U}) != ERR_TIMEOUT) return 31;
    fake_ehci_poll_result = OK;
    fake_uhci_status_result = ERR_UNAVAILABLE;
    if (usb_manager_refresh() != OK || usb_manager_get_status(&status) != OK ||
        status.last_error != ERR_UNAVAILABLE || status.port_count != 1U ||
        status.configured_device_count != 1U) return 32;
    fake_uhci_status_result = OK;
    fake_ehci_status_result = ERR_UNAVAILABLE;
    if (usb_manager_refresh() != OK || usb_manager_get_status(&status) != OK ||
        status.last_error != ERR_UNAVAILABLE || status.port_count != 2U ||
        status.configured_device_count != 1U) return 33;
    fake_ehci_status_result = OK;
    fake_uhci_diag_result = ERR_UNAVAILABLE;
    if (usb_manager_refresh() != OK) return 34;
    fake_uhci_diag_result = OK;
    if (usb_manager_validate_state() != OK) return 35;
    fake_msc_count_result = ERR_UNAVAILABLE;
    fake_hid_count_result = ERR_UNAVAILABLE;
    if (usb_manager_refresh() != OK || usb_manager_validate_state() != OK) return 36;
    fake_msc_count_result = OK;
    fake_hid_count_result = OK;
    fake_msc_refresh_result = ERR_UNAVAILABLE;
    fake_hid_refresh_result = ERR_UNAVAILABLE;
    if (usb_manager_refresh() != OK || usb_manager_validate_state() != OK) return 37;
    fake_msc_refresh_result = OK;
    fake_hid_refresh_result = OK;
    return 0;
}

static int check_reinitialization_paths(void) {
    usb_manager_status_t status;

    fake_pci_count_result = ERR_DISK;
    if (usb_manager_refresh() != ERR_DISK ||
        usb_manager_get_status(&status) != OK) return 40;
    fake_pci_count_result = OK;
    if (usb_manager_init() != OK) return 41;
    fake_pci_count_result = ERR_OVERFLOW;
    if (usb_manager_init() != ERR_OVERFLOW ||
        usb_manager_get_status(&status) != OK || !status.partial ||
        status.last_error != ERR_OVERFLOW) return 42;
    fake_pci_count_result = OK;
    if (usb_manager_init() != OK || usb_manager_validate_state() != OK) return 43;
    fake_uhci_init_result = ERR_UNAVAILABLE;
    if (usb_manager_refresh() != OK || usb_manager_get_status(&status) != OK ||
        status.last_error != ERR_UNAVAILABLE) return 44;
    fake_uhci_init_result = OK;
    fake_ehci_init_result = ERR_UNAVAILABLE;
    if (usb_manager_refresh() != OK || usb_manager_get_status(&status) != OK ||
        status.last_error != ERR_UNAVAILABLE) return 45;
    fake_ehci_init_result = OK;
    if (usb_manager_refresh() != OK || usb_manager_validate_state() != OK) return 46;
    fake_pci_at_result = ERR_DISK;
    if (usb_manager_refresh() != ERR_DISK) return 47;
    fake_pci_at_result = OK;
    if (usb_manager_init() != OK) return 48;
    return 0;
}

static int check_optional_failures(void) {
    usb_manager_status_t status;

    fake_msc_init_result = ERR_UNAVAILABLE;
    fake_hid_init_result = ERR_UNAVAILABLE;
    if (usb_manager_init() != OK || usb_manager_get_status(&status) != OK ||
        status.msc_device_count != 1U || status.hid_device_count != 1U) return 50;
    fake_msc_init_result = OK;
    fake_hid_init_result = OK;
    fake_recovery_get_available = 0;
    if (usb_manager_refresh() != OK) return 51;
    fake_recovery_get_available = 1;
    fake_recovery_mark_result = ERR_STATE;
    if (usb_manager_refresh() != OK) return 52;
    fake_recovery_mark_result = OK;
    if (usb_manager_refresh() != OK || usb_manager_validate_state() != OK) return 53;
    return 0;
}

int main(void) {
    int result = 0;

    reset_fixtures();
    coverage_active = 1U;
    result = check_before_initialization();
    if (!result) result = check_initialized_state();
    if (!result) result = check_failure_recovery();
    if (!result) result = check_reinitialization_paths();
    if (!result) result = check_optional_failures();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) fprintf(stderr, "usb_manager_host_failure=%d\n", result);
    return result;
}
