#ifndef USB_HID_H
#define USB_HID_H

#include "types.h"
#include "core/usb_manager.h"

#define USB_HID_MAX_DEVICES USB_MANAGER_MAX_DEVICES
#define USB_HID_KEYBOARD_REPORT_SIZE 8U
#define USB_HID_MOUSE_REPORT_MIN_SIZE 3U
#define USB_HID_MOUSE_REPORT_MAX_SIZE 4U

typedef enum {
    USB_HID_KIND_KEYBOARD = 0,
    USB_HID_KIND_MOUSE
} usb_hid_kind_t;

typedef enum {
    USB_HID_STATE_DISABLED = 0,
    USB_HID_STATE_READY,
    USB_HID_STATE_DEGRADED
} usb_hid_state_t;

typedef struct {
    char id[USB_DEVICE_ID_SIZE];
    char controller_id[USB_PORT_CONTROLLER_ID_SIZE];
    usb_hid_kind_t kind;
    usb_hid_state_t state;
    uint8_t interface_number;
    uint8_t interrupt_endpoint;
    uint16_t max_packet;
    uint8_t interval;
    uint8_t active;
    uint32_t report_count;
    uint32_t malformed_count;
    uint32_t timeout_count;
    uint32_t error_count;
    uint32_t dropped_count;
    uint32_t cancel_count;
    int last_error;
} usb_hid_info_t;

int usb_hid_init(void);
int usb_hid_refresh(void);
int usb_hid_get_count(uint32_t* out_count);
int usb_hid_get_at(uint32_t index, usb_hid_info_t* out_info);
int usb_hid_find(const char* id, usb_hid_info_t* out_info);
int usb_hid_is_active(const char* id);
int usb_hid_validate_state(void);
const char* usb_hid_kind_name(usb_hid_kind_t kind);
const char* usb_hid_state_name(usb_hid_state_t state);

#endif
