#ifndef ETHERNET_H
#define ETHERNET_H

#include "types.h"

#define ETHERNET_INTERFACE_CAPACITY 4U
#define ETHERNET_INTERFACE_ID_SIZE 20U
#define ETHERNET_MAC_ADDRESS_SIZE 6U
#define ETHERNET_HEADER_SIZE 14U
#define ETHERNET_MIN_FRAME_SIZE 60U
#define ETHERNET_MAX_FRAME_SIZE 1518U
#define ETHERNET_MAX_PAYLOAD_SIZE 1500U
#define ETHERNET_RX_POLL_BUDGET 8U
#define ETHERNET_PROTOCOL_HANDLER_CAPACITY 4U

typedef struct {
    uint8_t initialized;
    uint8_t link_up;
    uint8_t rx_queue_depth;
    uint8_t rx_queue_high_water;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_errors;
    uint32_t tx_errors;
    uint32_t rx_dropped;
    uint32_t rx_queue_dropped;
    uint32_t rx_interrupts;
    int last_error;
} ethernet_driver_status_t;

typedef int (*ethernet_get_driver_status_fn)(
    void* driver_context, ethernet_driver_status_t* out_status);
typedef int (*ethernet_receive_frame_fn)(
    void* driver_context, uint8_t* data, uint16_t capacity,
    uint16_t* out_length, uint8_t* out_received);
typedef int (*ethernet_send_frame_fn)(
    void* driver_context, const uint8_t* data, uint16_t length);
typedef int (*ethernet_rx_pending_fn)(
    void* driver_context, uint8_t* out_pending);

typedef enum {
    ETHERNET_DESTINATION_UNKNOWN = 0,
    ETHERNET_DESTINATION_LOCAL_UNICAST,
    ETHERNET_DESTINATION_BROADCAST
} ethernet_destination_t;

typedef struct {
    const char* interface_id;
    const uint8_t* destination;
    const uint8_t* source;
    const uint8_t* payload;
    uint16_t payload_length;
    uint16_t ethertype;
    ethernet_destination_t destination_type;
} ethernet_frame_view_t;

typedef int (*ethernet_protocol_handler_fn)(
    const ethernet_frame_view_t* frame);

typedef struct {
    uint8_t initialized;
    char interface_id[ETHERNET_INTERFACE_ID_SIZE];
    uint8_t mac_address[ETHERNET_MAC_ADDRESS_SIZE];
    void* driver_context;
    ethernet_get_driver_status_fn get_driver_status;
    ethernet_rx_pending_fn rx_pending;
    ethernet_receive_frame_fn receive_frame;
    ethernet_send_frame_fn send_frame;
} ethernet_interface_t;

typedef struct {
    uint8_t attached;
    char interface_id[ETHERNET_INTERFACE_ID_SIZE];
    uint8_t mac_address[ETHERNET_MAC_ADDRESS_SIZE];
    ethernet_driver_status_t driver;
    uint32_t polls;
    uint32_t poll_errors;
    uint32_t rx_frames;
    uint32_t rx_unicast;
    uint32_t rx_broadcast;
    uint32_t rx_invalid;
    uint32_t rx_filtered;
    uint32_t rx_delivered;
    uint32_t rx_unhandled;
    uint32_t rx_protocol_errors;
    uint32_t tx_frames;
    uint16_t last_frame_length;
    uint16_t last_ethertype;
    uint8_t last_source[ETHERNET_MAC_ADDRESS_SIZE];
    uint8_t last_destination[ETHERNET_MAC_ADDRESS_SIZE];
    ethernet_destination_t last_destination_type;
    int last_error;
} ethernet_interface_status_t;

typedef struct {
    uint8_t initialized;
    uint8_t interface_count;
    uint8_t handler_count;
    uint32_t polls;
    uint32_t poll_errors;
    uint32_t rx_frames;
    uint32_t rx_delivered;
    uint32_t tx_frames;
    int last_error;
} ethernet_status_t;

int ethernet_init(void);
int ethernet_attach_interface(const ethernet_interface_t* interface);
int ethernet_register_handler(uint16_t ethertype,
                              ethernet_protocol_handler_fn handler);
int ethernet_poll(uint32_t budget, uint32_t* out_processed);
int ethernet_send(const char* interface_id, const uint8_t* destination,
                  uint16_t ethertype, const uint8_t* payload,
                  uint16_t payload_length);
int ethernet_get_status(ethernet_status_t* out_status);
int ethernet_get_interface_status(
    const char* interface_id, ethernet_interface_status_t* out_status);
int ethernet_validate_state(void);

#endif
