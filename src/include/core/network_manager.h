#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "types.h"
#include "core/ethernet.h"

#define NETWORK_MANAGER_MAX_INTERFACES 4U
#define NETWORK_PCI_BAR_COUNT 6U
#define NETWORK_INTERFACE_ID_SIZE 20U
#define NETWORK_INTERFACE_NAME_SIZE 40U
#define NETWORK_DRIVER_NAME_SIZE 24U
#define NETWORK_IRQ_UNKNOWN 0xFFU
#define NETWORK_MAC_ADDRESS_SIZE 6U

typedef enum {
    NETWORK_ADAPTER_UNKNOWN = 0,
    NETWORK_ADAPTER_E1000,
    NETWORK_ADAPTER_RTL8139
} network_adapter_model_t;

typedef enum {
    NETWORK_INTERFACE_DRIVER_MISSING = 0,
    NETWORK_INTERFACE_UNSUPPORTED,
    NETWORK_INTERFACE_ACTIVE,
    NETWORK_INTERFACE_DRIVER_ERROR
} network_interface_state_t;

typedef enum {
    NETWORK_LINK_UNKNOWN = 0,
    NETWORK_LINK_DOWN,
    NETWORK_LINK_UP
} network_link_state_t;

typedef struct {
    network_adapter_model_t model;
    network_interface_state_t state;
    network_link_state_t link;
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
    uint32_t bars[NETWORK_PCI_BAR_COUNT];
    uint8_t mac_address[NETWORK_MAC_ADDRESS_SIZE];
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_errors;
    uint32_t tx_errors;
    uint32_t rx_dropped;
    uint8_t rx_queue_depth;
    uint8_t rx_queue_high_water;
    uint32_t rx_queue_dropped;
    uint32_t rx_interrupts;
    int driver_error;
} network_interface_info_t;

typedef struct {
    uint8_t initialized;
    uint8_t partial;
    uint8_t packet_io_available;
    uint8_t ethernet_available;
    uint8_t ipv4_available;
    uint32_t interface_count;
    uint32_t recognized_count;
    uint32_t active_count;
    int last_error;
} network_manager_status_t;

typedef struct {
    char id[NETWORK_INTERFACE_ID_SIZE];
    char name[NETWORK_INTERFACE_NAME_SIZE];
    char driver[NETWORK_DRIVER_NAME_SIZE];
} network_interface_text_t;

typedef struct {
    uint32_t processed_now;
    uint8_t driver_queue_depth;
    uint8_t driver_queue_high_water;
    uint32_t driver_queue_dropped;
    uint32_t driver_rx_interrupts;
    ethernet_status_t layer;
} network_ethernet_diagnostic_t;

int network_manager_init(void);
int network_manager_refresh(void);
int network_manager_poll(uint32_t* out_processed);
int network_manager_get_status(network_manager_status_t* out_status);
int network_manager_get_count(uint32_t* out_count);
int network_manager_get_interface(uint32_t index,
                                  network_interface_info_t* out_info);
int network_manager_find(const char* id, network_interface_info_t* out_info);
int network_manager_send_diagnostic(const char* id);
int network_manager_get_ethernet_diagnostic(
    const char* id, network_ethernet_diagnostic_t* out_diagnostic);
int network_manager_format_text(const network_interface_info_t* info,
                                network_interface_text_t* out_text);
const char* network_manager_model_name(network_adapter_model_t model);
const char* network_manager_interface_state_name(
    network_interface_state_t state);
const char* network_manager_link_state_name(network_link_state_t state);

#endif
