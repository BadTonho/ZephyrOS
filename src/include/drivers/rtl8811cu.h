#ifndef RTL8811CU_H
#define RTL8811CU_H

#include "types.h"
#include "core/ethernet.h"
#include "core/usb_manager.h"

#define RTL8811CU_VENDOR_ID 0x0BDAU
#define RTL8811CU_PRODUCT_ID 0xC811U
#define RTL8811CU_DEVICE_REVISION 0x0200U
#define RTL8811CU_FIRMWARE_FILE "RTL8811.BIN"
#define RTL8811CU_FIRMWARE_MAX_SIZE (128U * 1024U)
#define RTL8811CU_FIRMWARE_HEADER_SIZE 64U
#define RTL8811CU_SCAN_RESULT_CAPACITY 8U
#define RTL8811CU_SSID_MAX_LENGTH 32U
#define RTL8811CU_BSSID_LENGTH 6U

typedef enum {
    RTL8811CU_STATE_DISABLED = 0,
    RTL8811CU_STATE_INVENTORIED,
    RTL8811CU_STATE_UNSUPPORTED,
    RTL8811CU_STATE_READY,
    RTL8811CU_STATE_ERROR
} rtl8811cu_state_t;

typedef struct {
    uint8_t matched;
    uint8_t revision_supported;
    uint8_t configured;
    uint8_t bulk_in_count;
    uint8_t bulk_out_count;
    uint8_t interrupt_in_count;
    uint8_t endpoint_count;
    int result;
} rtl8811cu_probe_info_t;

typedef struct {
    uint8_t initialized;
    uint8_t firmware_available;
    uint8_t firmware_header_valid;
    uint8_t firmware_checksum_valid;
    uint8_t hardware_initialized;
    uint8_t tx_ready;
    uint8_t rx_ready;
    uint8_t link_up;
    uint32_t firmware_size;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t timeout_count;
    uint32_t recovery_count;
    int last_error;
    rtl8811cu_state_t state;
} rtl8811cu_status_t;

typedef struct {
    uint8_t ssid_length;
    char ssid[RTL8811CU_SSID_MAX_LENGTH + 1U];
    uint8_t bssid[RTL8811CU_BSSID_LENGTH];
    uint8_t channel;
    uint8_t open_security;
} rtl8811cu_scan_result_t;

int rtl8811cu_probe(const usb_device_info_t* device,
                    rtl8811cu_probe_info_t* out_probe);
int rtl8811cu_init(const usb_device_info_t* device,
                   ethernet_interface_t* out_interface);
int rtl8811cu_get_status(rtl8811cu_status_t* out_status);
int rtl8811cu_validate_state(void);
int rtl8811cu_scan(rtl8811cu_scan_result_t* results, uint32_t capacity,
                   uint32_t* out_count);
int rtl8811cu_connect_open(const char* ssid);
const char* rtl8811cu_state_name(rtl8811cu_state_t state);

#endif
