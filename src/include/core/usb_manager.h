#ifndef USB_MANAGER_H
#define USB_MANAGER_H

#include "types.h"

#define USB_MANAGER_MAX_CONTROLLERS 8U
#define USB_CONTROLLER_BAR_COUNT 6U
#define USB_CONTROLLER_ID_SIZE 24U
#define USB_CONTROLLER_NAME_SIZE 32U
#define USB_CONTROLLER_LOCATION_SIZE 24U
#define USB_CONTROLLER_DETAIL_SIZE 64U
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
    USB_CONTROLLER_REASON_OUT_OF_SCOPE
} usb_controller_reason_t;

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
    int last_error;
} usb_manager_status_t;

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
int usb_manager_format_text(const usb_controller_info_t* info,
                            usb_controller_text_t* out_text);
const char* usb_manager_model_name(usb_controller_model_t model);
const char* usb_manager_state_name(usb_controller_state_t state);
const char* usb_manager_reason_name(usb_controller_reason_t reason);

#endif
