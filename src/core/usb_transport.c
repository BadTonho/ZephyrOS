#include "core/usb_transport.h"
#include "core/errors.h"
#include "core/log.h"
#include "drivers/ehci.h"
#include "drivers/uhci.h"

static int usb_transport_is_ehci(const usb_device_info_t* device) {
    return device && device->controller_model == USB_CONTROLLER_MODEL_EHCI;
}

static int usb_transport_is_uhci(const usb_device_info_t* device) {
    return device && device->controller_model == USB_CONTROLLER_MODEL_UHCI;
}

int usb_transport_control_request(const usb_device_info_t* device,
                                  uint8_t request_type, uint8_t request,
                                  uint16_t value, uint16_t index,
                                  uint16_t length, uint8_t* data,
                                  uint16_t* out_length) {
    if (!device) {
        LOG_ERROR("USB", "Dispositivo nulo no transporte de controle");
        return ERR_NULL;
    }
    if (usb_transport_is_ehci(device)) {
        return ehci_control_request(device, request_type, request, value,
                                    index, length, data, out_length);
    }
    if (!usb_transport_is_uhci(device)) {
        LOG_ERROR("USB", "Controlador USB sem transporte selecionado");
        return ERR_UNAVAILABLE;
    }
    return uhci_control_request(device, request_type, request, value, index,
                                length, data, out_length);
}

int usb_transport_bulk_transfer(const usb_device_info_t* device,
                                uint8_t endpoint_address,
                                uint8_t direction_in, uint8_t* buffer,
                                uint16_t length, uint16_t* out_length) {
    if (!device || !buffer) {
        LOG_ERROR("USB", "Argumento nulo no transporte Bulk");
        return ERR_NULL;
    }
    if (usb_transport_is_ehci(device)) {
        return ehci_bulk_transfer(device, endpoint_address, direction_in,
                                  buffer, length, out_length);
    }
    if (!usb_transport_is_uhci(device)) {
        LOG_ERROR("USB", "Controlador USB sem transporte Bulk selecionado");
        return ERR_UNAVAILABLE;
    }
    return uhci_bulk_transfer(device, endpoint_address, direction_in, buffer,
                              length, out_length);
}

int usb_transport_reset_bulk_toggles(const usb_device_info_t* device) {
    if (!device) {
        LOG_ERROR("USB", "Dispositivo nulo ao resetar toggles USB");
        return ERR_NULL;
    }
    if (usb_transport_is_ehci(device)) return ehci_reset_bulk_toggles(device);
    if (!usb_transport_is_uhci(device)) {
        LOG_ERROR("USB", "Controlador USB sem transporte de toggles");
        return ERR_UNAVAILABLE;
    }
    return uhci_reset_bulk_toggles(device);
}

int usb_transport_interrupt_submit(const usb_device_info_t* device,
                                   uint8_t endpoint_address,
                                   uint16_t max_packet, uint8_t interval,
                                   usb_interrupt_callback_t callback,
                                   void* context) {
    if (!device || !callback) {
        LOG_ERROR("USB", "Argumento nulo no transporte Interrupt");
        return ERR_NULL;
    }
    if (usb_transport_is_ehci(device)) {
        return ehci_interrupt_submit(device, endpoint_address, max_packet,
                                     interval, callback, context);
    }
    if (!usb_transport_is_uhci(device)) {
        LOG_ERROR("USB", "Controlador USB sem transporte Interrupt");
        return ERR_UNAVAILABLE;
    }
    return uhci_interrupt_submit(device, endpoint_address, max_packet,
                                 interval, callback, context);
}

int usb_transport_interrupt_cancel(const usb_device_info_t* device,
                                   uint8_t endpoint_address) {
    if (!device) {
        LOG_ERROR("USB", "Dispositivo nulo ao cancelar Interrupt");
        return ERR_NULL;
    }
    if (usb_transport_is_ehci(device)) {
        return ehci_interrupt_cancel(device, endpoint_address);
    }
    if (!usb_transport_is_uhci(device)) {
        LOG_ERROR("USB", "Controlador USB sem transporte de cancelamento");
        return ERR_UNAVAILABLE;
    }
    return uhci_interrupt_cancel(device, endpoint_address);
}
