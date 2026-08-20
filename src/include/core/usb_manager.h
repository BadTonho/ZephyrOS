#ifndef USB_MANAGER_H
#define USB_MANAGER_H

#include "types.h"

#define USB_MANAGER_MAX_CONTROLLERS 8U
#define USB_CONTROLLER_BAR_COUNT 6U
#define USB_CONTROLLER_ID_SIZE 24U
#define USB_CONTROLLER_NAME_SIZE 32U
#define USB_CONTROLLER_LOCATION_SIZE 24U
#define USB_CONTROLLER_DETAIL_SIZE 64U
#define USB_PORT_CONTROLLER_ID_SIZE USB_CONTROLLER_ID_SIZE
#define USB_DEVICE_ID_SIZE 40U
#define USB_UHCI_PORT_COUNT 2U
#define USB_MANAGER_MAX_PORTS (USB_MANAGER_MAX_CONTROLLERS * USB_UHCI_PORT_COUNT)
#define USB_MANAGER_MAX_DEVICES (USB_MANAGER_MAX_PORTS)
#define USB_UHCI_FRAME_COUNT 1024U
#define USB_UHCI_TD_CAPACITY 64U
#define USB_UHCI_BUFFER_CAPACITY 8U
#define USB_UHCI_DESCRIPTOR_BUFFER_SIZE 512U
#define USB_UHCI_BULK_BUFFER_SIZE 1024U
#define USB_CONTROLLER_IRQ_UNKNOWN 0xFFU
#define USB_CONTROLLER_PCI_CLASS 0x0CU
#define USB_CONTROLLER_PCI_SUBCLASS 0x03U
#define USB_CONTROLLER_PROG_IF_UHCI 0x00U
#define USB_CONTROLLER_PROG_IF_EHCI 0x20U

typedef enum {
    USB_CONTROLLER_MODEL_UHCI = 0,
    USB_CONTROLLER_MODEL_EHCI,
    USB_CONTROLLER_MODEL_OTHER
} usb_controller_model_t;

typedef enum {
    USB_CONTROLLER_READY = 0,
    USB_CONTROLLER_DEGRADED,
    USB_CONTROLLER_DISABLED
} usb_controller_state_t;

typedef enum {
    USB_CONTROLLER_REASON_DRIVER_NOT_INITIALIZED = 0,
    USB_CONTROLLER_REASON_DRIVER_READY,
    USB_CONTROLLER_REASON_DRIVER_FAILURE,
    USB_CONTROLLER_REASON_PORT_FAILURE,
    USB_CONTROLLER_REASON_OUT_OF_SCOPE
} usb_controller_reason_t;

typedef enum {
    USB_UHCI_STATE_DISABLED = 0,
    USB_UHCI_STATE_READY,
    USB_UHCI_STATE_DEGRADED
} usb_uhci_state_t;

typedef enum {
    USB_PORT_EMPTY = 0,
    USB_PORT_RESETTING,
    USB_PORT_ENUMERATING,
    USB_PORT_CONFIGURED,
    USB_PORT_DEGRADED
} usb_port_state_t;

typedef enum {
    USB_PORT_REASON_NONE = 0,
    USB_PORT_REASON_NO_DEVICE,
    USB_PORT_REASON_RESET_TIMEOUT,
    USB_PORT_REASON_CONTROL_TIMEOUT,
    USB_PORT_REASON_INVALID_DESCRIPTOR,
    USB_PORT_REASON_UNSUPPORTED_SPEED,
    USB_PORT_REASON_UNSUPPORTED_LAYOUT,
    USB_PORT_REASON_DRIVER_FAILURE
} usb_port_reason_t;

typedef enum {
    USB_DEVICE_SPEED_LOW = 0,
    USB_DEVICE_SPEED_FULL
} usb_device_speed_t;

typedef enum {
    USB_DEVICE_CONFIGURED = 0,
    USB_DEVICE_DEGRADED
} usb_device_state_t;

typedef struct {
    usb_controller_model_t model;
    usb_controller_state_t state;
    usb_controller_reason_t reason;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass_code;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t irq;
    uint32_t bars[USB_CONTROLLER_BAR_COUNT];
    uint8_t uhci_initialized;
    uint8_t uhci_irq_registered;
    uint8_t uhci_dma_ready;
    uint8_t uhci_transfer_ready;
    uint8_t uhci_port_count;
    uint8_t uhci_device_count;
    uint8_t uhci_port_errors;
    int uhci_last_error;
} usb_controller_info_t;

typedef struct {
    uint8_t initialized;
    uint8_t partial;
    uint32_t controller_count;
    uint32_t uhci_count;
    uint32_t ehci_count;
    uint32_t other_count;
    uint8_t dma_initialized;
    uint8_t irq_initialized;
    uint8_t transfer_available;
    uint32_t uhci_ready_count;
    uint32_t port_count;
    uint32_t configured_device_count;
    uint32_t degraded_port_count;
    uint32_t dma_td_capacity;
    uint32_t dma_td_in_use;
    uint8_t class_driver_active;
    uint8_t hub_support_active;
    uint8_t hotplug_active;
    uint32_t msc_device_count;
    uint8_t bulk_transfer_available;
    int last_error;
} usb_manager_status_t;

typedef struct {
    uint8_t initialized;
    uint8_t running;
    uint8_t irq_registered;
    uint8_t irq_pending;
    uint8_t dma_ready;
    uint8_t control_transfer_ready;
    uint8_t bulk_transfer_ready;
    uint8_t class_driver_active;
    uint8_t hub_support_active;
    uint8_t hotplug_active;
    uint8_t port_count;
    uint8_t device_count;
    uint8_t port_errors;
    uint32_t frame_list_phys;
    uint32_t queue_head_phys;
    uint32_t td_pool_phys;
    uint32_t buffer_pool_phys;
    uint32_t td_capacity;
    uint32_t td_in_use;
    uint32_t buffer_capacity;
    uint32_t buffer_in_use;
    uint32_t irq_events;
    uint32_t timeout_count;
    uint32_t recovery_count;
    uint32_t bulk_transfer_count;
    int last_error;
} usb_uhci_status_t;

typedef struct {
    char controller_id[USB_PORT_CONTROLLER_ID_SIZE];
    uint8_t controller_bus;
    uint8_t controller_device;
    uint8_t controller_function;
    uint8_t port_number;
    usb_port_state_t state;
    usb_port_reason_t reason;
    usb_device_speed_t speed;
    uint8_t connected;
    uint8_t enabled;
    uint8_t usb_address;
    char device_id[USB_DEVICE_ID_SIZE];
} usb_port_info_t;

typedef struct {
    char id[USB_DEVICE_ID_SIZE];
    char controller_id[USB_PORT_CONTROLLER_ID_SIZE];
    uint8_t controller_bus;
    uint8_t controller_device;
    uint8_t controller_function;
    uint8_t port_number;
    usb_device_state_t state;
    usb_device_speed_t speed;
    uint8_t usb_address;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t max_packet_size0;
    uint8_t num_configurations;
    uint16_t configuration_length;
    uint8_t configuration_value;
    uint8_t interface_number;
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint8_t endpoint_count;
    uint8_t bulk_in_endpoint;
    uint8_t bulk_out_endpoint;
    uint8_t bulk_in_count;
    uint8_t bulk_out_count;
    uint16_t bulk_in_max_packet;
    uint16_t bulk_out_max_packet;
    uint8_t class_driver_active;
    uint8_t hub_present;
    uint8_t device_descriptor_valid;
    uint8_t configuration_descriptor_valid;
} usb_device_info_t;

typedef struct {
    char id[USB_DEVICE_ID_SIZE];
    char controller_id[USB_PORT_CONTROLLER_ID_SIZE];
    char detail[USB_CONTROLLER_DETAIL_SIZE];
} usb_device_text_t;

typedef struct {
    char id[USB_CONTROLLER_ID_SIZE];
    char name[USB_CONTROLLER_NAME_SIZE];
    char location[USB_CONTROLLER_LOCATION_SIZE];
    char detail[USB_CONTROLLER_DETAIL_SIZE];
} usb_controller_text_t;

int usb_manager_init(void);
int usb_manager_refresh(void);
int usb_manager_get_status(usb_manager_status_t* out_status);
int usb_manager_get_count(uint32_t* out_count);
int usb_manager_get_info(uint32_t index, usb_controller_info_t* out_info);
int usb_manager_find(const char* id, usb_controller_info_t* out_info);
int usb_manager_poll(uint32_t budget, uint32_t* out_processed);
int usb_manager_get_uhci_status(uint32_t index, usb_uhci_status_t* out_status);
int usb_manager_get_port_count(uint32_t* out_count);
int usb_manager_get_port(uint32_t index, usb_port_info_t* out_info);
int usb_manager_get_device_count(uint32_t* out_count);
int usb_manager_get_device(uint32_t index, usb_device_info_t* out_info);
int usb_manager_find_device(const char* id, usb_device_info_t* out_info);
int usb_manager_format_device_text(const usb_device_info_t* info,
                                   usb_device_text_t* out_text);
int usb_manager_validate_state(void);
int usb_manager_format_text(const usb_controller_info_t* info,
                            usb_controller_text_t* out_text);
const char* usb_manager_model_name(usb_controller_model_t model);
const char* usb_manager_state_name(usb_controller_state_t state);
const char* usb_manager_reason_name(usb_controller_reason_t reason);
const char* usb_manager_uhci_state_name(usb_uhci_state_t state);
const char* usb_manager_port_state_name(usb_port_state_t state);
const char* usb_manager_port_reason_name(usb_port_reason_t reason);
const char* usb_manager_speed_name(usb_device_speed_t speed);

#endif
