#ifndef UHCI_H
#define UHCI_H

#include "types.h"
#include "core/usb_manager.h"
#include "drivers/pci.h"

#define UHCI_IO_SPACE_BYTES 0x20U
#define UHCI_IRQ_MAX 15U
#define UHCI_PCI_BAR_IO 0x01U
#define UHCI_PCI_BAR_ADDRESS_MASK 0xFFFFFFF0U
#define UHCI_PORT_STATUS_BASE 0x10U
#define UHCI_PORT_STATUS_STRIDE 0x02U
#define UHCI_RESET_TIMEOUT_MS 100U
#define UHCI_CONTROL_TIMEOUT_MS 250U
#define UHCI_SET_ADDRESS_DELAY_MS 2U
#define UHCI_RECOVERY_TIMEOUT_MS 50U
#define UHCI_POLL_BUDGET 4U

int uhci_init(const pci_device_t* pci, const char* controller_id);
int uhci_poll(uint32_t budget, uint32_t* out_processed);
int uhci_get_status(uint8_t bus, uint8_t device, uint8_t function,
                    usb_uhci_status_t* out_status);
int uhci_get_port_count(uint8_t bus, uint8_t device, uint8_t function,
                        uint32_t* out_count);
int uhci_get_port(uint8_t bus, uint8_t device, uint8_t function,
                 uint32_t index, usb_port_info_t* out_info);
int uhci_get_device_count(uint8_t bus, uint8_t device, uint8_t function,
                          uint32_t* out_count);
int uhci_get_device(uint8_t bus, uint8_t device, uint8_t function,
                    uint32_t index, usb_device_info_t* out_info);
int uhci_validate_state(uint8_t bus, uint8_t device, uint8_t function);

#endif
