#ifndef EHCI_H
#define EHCI_H

#include "types.h"
#include "core/usb_manager.h"
#include "core/irq_deferred.h"
#include "drivers/pci.h"

#define EHCI_IRQ_MAX 15U
#define EHCI_PCI_BAR_ADDRESS_MASK 0xFFFFFFF0U
#define EHCI_MMIO_REQUIRED_BYTES 0x1000U
#define EHCI_CAPABILITY_MIN_LENGTH 0x10U
#define EHCI_PORTSC_STRIDE 4U
#define EHCI_RESET_TIMEOUT_MS 100U
#define EHCI_CONTROL_TIMEOUT_MS 500U
#define EHCI_BULK_TIMEOUT_MS 1000U
#define EHCI_RECOVERY_TIMEOUT_MS 100U
#define EHCI_SET_ADDRESS_DELAY_MS 10U
#define EHCI_SYNC_BUFFER_SIZE 16384U
#define EHCI_INTERRUPT_BUFFER_SIZE 1024U
#define EHCI_DMA_PAGE_COUNT 8U
#define EHCI_QH_CAPACITY 3U
#define EHCI_QTD_CAPACITY 16U
#define EHCI_SYNC_QTD_CAPACITY 12U
#define EHCI_DEVICE_DESCRIPTOR_LENGTH 18U
#define EHCI_CONFIGURATION_HEADER_LENGTH 9U
#define EHCI_DESCRIPTOR_BUFFER_SIZE 4096U
#define EHCI_MAX_DESCRIPTOR_LENGTH EHCI_DESCRIPTOR_BUFFER_SIZE
#define EHCI_INTERRUPT_REQUEST_CAPACITY 1U

typedef struct {
    uint8_t initialized;
    uint8_t running;
    uint8_t irq_registered;
    uint8_t irq_pending;
    uint8_t dma_ready;
    uint8_t control_transfer_ready;
    uint8_t bulk_transfer_ready;
    uint8_t interrupt_transfer_ready;
    uint8_t port_count;
    uint8_t device_count;
    uint8_t port_errors;
    uint32_t capability_base;
    uint32_t operational_base;
    uint32_t async_list_phys;
    uint32_t qh_pool_phys;
    uint32_t qtd_pool_phys;
    uint32_t buffer_pool_phys;
    uint32_t qtd_capacity;
    uint32_t qtd_in_use;
    uint32_t buffer_capacity;
    uint32_t buffer_in_use;
    uint32_t irq_events;
    uint32_t timeout_count;
    uint32_t recovery_count;
    uint32_t control_transfer_count;
    uint32_t bulk_transfer_count;
    uint32_t interrupt_transfer_count;
    uint32_t interrupt_timeout_count;
    uint32_t interrupt_error_count;
    uint32_t interrupt_cancel_count;
    int last_error;
} usb_ehci_status_t;

int ehci_init(const pci_device_t* pci, const char* controller_id);
int ehci_poll(uint32_t budget, uint32_t* out_processed);
int ehci_get_status(uint8_t bus, uint8_t device, uint8_t function,
                    usb_ehci_status_t* out_status);
int ehci_get_port_count(uint8_t bus, uint8_t device, uint8_t function,
                        uint32_t* out_count);
int ehci_get_port(uint8_t bus, uint8_t device, uint8_t function,
                 uint32_t index, usb_port_info_t* out_info);
int ehci_get_device_count(uint8_t bus, uint8_t device, uint8_t function,
                          uint32_t* out_count);
int ehci_get_device(uint8_t bus, uint8_t device, uint8_t function,
                    uint32_t index, usb_device_info_t* out_info);
int ehci_validate_state(uint8_t bus, uint8_t device, uint8_t function);
int ehci_control_request(const usb_device_info_t* device,
                         uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, uint16_t length,
                         uint8_t* data, uint16_t* out_length);
int ehci_bulk_transfer(const usb_device_info_t* device,
                       uint8_t endpoint_address, uint8_t direction_in,
                       uint8_t* buffer, uint16_t length,
                       uint16_t* out_length);
int ehci_reset_bulk_toggles(const usb_device_info_t* device);
int ehci_interrupt_submit(const usb_device_info_t* device,
                          uint8_t endpoint_address, uint16_t max_packet,
                          uint8_t interval,
                          usb_interrupt_callback_t callback,
                          void* context);
int ehci_interrupt_cancel(const usb_device_info_t* device,
                          uint8_t endpoint_address);

#endif
