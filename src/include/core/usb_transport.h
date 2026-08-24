#ifndef USB_TRANSPORT_H
#define USB_TRANSPORT_H

#include "types.h"
#include "core/usb_manager.h"

int usb_transport_control_request(const usb_device_info_t* device,
                                  uint8_t request_type, uint8_t request,
                                  uint16_t value, uint16_t index,
                                  uint16_t length, uint8_t* data,
                                  uint16_t* out_length);
int usb_transport_bulk_transfer(const usb_device_info_t* device,
                                uint8_t endpoint_address,
                                uint8_t direction_in, uint8_t* buffer,
                                uint16_t length, uint16_t* out_length);
int usb_transport_reset_bulk_toggles(const usb_device_info_t* device);
int usb_transport_interrupt_submit(const usb_device_info_t* device,
                                   uint8_t endpoint_address,
                                   uint16_t max_packet, uint8_t interval,
                                   usb_interrupt_callback_t callback,
                                   void* context);
int usb_transport_interrupt_cancel(const usb_device_info_t* device,
                                   uint8_t endpoint_address);

#endif
