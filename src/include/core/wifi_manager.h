#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "types.h"
#include "core/usb_manager.h"

#define WIFI_MANAGER_MAX_INTERFACES 8U
#define WIFI_PCI_BAR_COUNT 6U
#define WIFI_INTERFACE_ID_SIZE 20U
#define WIFI_INTERFACE_NAME_SIZE 32U
#define WIFI_PCI_CLASS_NETWORK 0x02U
#define WIFI_PCI_IRQ_UNKNOWN 0xFFU
#define WIFI_SCAN_RESULT_CAPACITY 8U
#define WIFI_SSID_MAX_LENGTH 32U
#define WIFI_BSSID_LENGTH 6U

typedef enum {
    WIFI_TRANSPORT_PCI = 0,
    WIFI_TRANSPORT_USB
} wifi_transport_t;

typedef enum {
    WIFI_INTERFACE_INVENTORIED = 0,
    WIFI_INTERFACE_UNSUPPORTED,
    WIFI_INTERFACE_READY,
    WIFI_INTERFACE_ERROR
} wifi_interface_state_t;

typedef struct {
    char id[WIFI_INTERFACE_ID_SIZE];
    char name[WIFI_INTERFACE_NAME_SIZE];
    wifi_transport_t transport;
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
    uint32_t bars[WIFI_PCI_BAR_COUNT];
    char usb_device_id[USB_DEVICE_ID_SIZE];
    uint8_t usb_port;
    uint8_t usb_address;
    uint16_t usb_revision;
    uint8_t usb_endpoint_count;
    wifi_interface_state_t state;
    int driver_error;
} wifi_interface_info_t;

typedef struct {
    uint8_t initialized;
    uint8_t partial;
    uint32_t interface_count;
    uint32_t candidate_count;
    uint32_t unsupported_count;
    uint32_t ready_count;
    uint32_t error_count;
    int last_error;
} wifi_manager_status_t;

typedef struct {
    uint8_t ssid_length;
    char ssid[WIFI_SSID_MAX_LENGTH + 1U];
    uint8_t bssid[WIFI_BSSID_LENGTH];
    uint8_t channel;
    uint8_t open_security;
} wifi_scan_result_t;

int wifi_manager_init(void);
int wifi_manager_refresh(void);
int wifi_manager_get_status(wifi_manager_status_t* out_status);
int wifi_manager_get_count(uint32_t* out_count);
int wifi_manager_get_interface(wifi_interface_info_t* out_info,
                               uint32_t index);
int wifi_manager_find(const char* id, wifi_interface_info_t* out_info);
int wifi_manager_validate_state(void);
int wifi_manager_scan(wifi_scan_result_t* results, uint32_t capacity,
                      uint32_t* out_count);
int wifi_manager_connect_open(const char* ssid);
const char* wifi_manager_state_name(wifi_interface_state_t state);

#endif
