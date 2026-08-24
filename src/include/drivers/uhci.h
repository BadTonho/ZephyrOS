#ifndef UHCI_H
#define UHCI_H

#include "types.h"
#include "core/usb_manager.h"
#include "core/irq_deferred.h"
#include "drivers/pci.h"

#define UHCI_IO_SPACE_BYTES 0x20U
#define UHCI_IRQ_MAX 15U
#define UHCI_PCI_BAR_IO 0x01U
#define UHCI_PCI_BAR_ADDRESS_MASK 0xFFFFFFF0U
#define UHCI_PORT_STATUS_BASE 0x10U
#define UHCI_PORT_STATUS_STRIDE 0x02U
#define UHCI_RESET_TIMEOUT_MS 100U
#define UHCI_CONTROL_TIMEOUT_MS 250U
#define UHCI_SET_ADDRESS_DELAY_MS 10U
#define UHCI_RECOVERY_TIMEOUT_MS 50U
#define UHCI_POLL_BUDGET 4U
#define UHCI_BULK_TIMEOUT_MS 500U
#define UHCI_BULK_BUFFER_OFFSET 1024U
#define UHCI_INTERRUPT_TIMEOUT_MS 1000U

typedef usb_interrupt_callback_t uhci_interrupt_callback_t;

int uhci_init(const pci_device_t* pci, const char* controller_id);
int uhci_poll(uint32_t budget, uint32_t* out_processed);
int uhci_get_status(uint8_t bus, uint8_t device, uint8_t function,
                    usb_uhci_status_t* out_status);
int uhci_get_port_count(uint8_t bus, uint8_t device, uint8_t function,
                        uint32_t* out_count);
int uhci_get_port(uint8_t bus, uint8_t device, uint8_t function,
                 uint32_t index, usb_port_info_t* out_info);
int uhci_log_port_diagnostics(uint8_t bus, uint8_t device, uint8_t function);
int uhci_get_device_count(uint8_t bus, uint8_t device, uint8_t function,
                          uint32_t* out_count);
int uhci_get_device(uint8_t bus, uint8_t device, uint8_t function,
                    uint32_t index, usb_device_info_t* out_info);
int uhci_validate_state(uint8_t bus, uint8_t device, uint8_t function);
int uhci_control_request(const usb_device_info_t* device,
                         uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, uint16_t length,
                         uint8_t* data, uint16_t* out_length);
int uhci_bulk_transfer(const usb_device_info_t* device,
                       uint8_t endpoint_address, uint8_t direction_in,
                       uint8_t* buffer, uint16_t length,
                       uint16_t* out_length);
int uhci_reset_bulk_toggles(const usb_device_info_t* device);
int uhci_interrupt_submit(const usb_device_info_t* device,
                          uint8_t endpoint_address, uint16_t max_packet,
                          uint8_t interval,
                          uhci_interrupt_callback_t callback,
                          void* context);
int uhci_interrupt_cancel(const usb_device_info_t* device,
                          uint8_t endpoint_address);

#endif
