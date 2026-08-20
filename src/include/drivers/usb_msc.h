#ifndef USB_MSC_H
#define USB_MSC_H

#include "types.h"
#include "core/usb_manager.h"
#include "fs/block.h"

#define USB_MSC_MAX_DEVICES USB_MANAGER_MAX_DEVICES
#define USB_MSC_VENDOR_SIZE 9U
#define USB_MSC_PRODUCT_SIZE 17U
#define USB_MSC_REVISION_SIZE 5U

typedef enum {
    USB_MSC_DEGRADED = 0,
    USB_MSC_READY
} usb_msc_state_t;

typedef struct {
    char id[USB_DEVICE_ID_SIZE];
    char block_id[BLOCK_DEVICE_ID_SIZE];
    char vendor[USB_MSC_VENDOR_SIZE];
    char product[USB_MSC_PRODUCT_SIZE];
    char revision[USB_MSC_REVISION_SIZE];
    uint8_t lun;
    uint8_t interface_number;
    uint8_t bulk_in_endpoint;
    uint8_t bulk_out_endpoint;
    uint16_t bulk_in_max_packet;
    uint16_t bulk_out_max_packet;
    uint16_t sector_size;
    uint32_t sector_count;
    usb_msc_state_t state;
    uint32_t command_count;
    uint32_t read_ops;
    uint32_t reset_count;
    int last_error;
} usb_msc_info_t;

int usb_msc_init(void);
int usb_msc_refresh(void);
int usb_msc_get_count(uint32_t* out_count);
int usb_msc_get_at(uint32_t index, usb_msc_info_t* out_info);
int usb_msc_find(const char* id, usb_msc_info_t* out_info);
int usb_msc_is_active(const char* id);
int usb_msc_validate_state(void);
const char* usb_msc_state_name(usb_msc_state_t state);

#endif
