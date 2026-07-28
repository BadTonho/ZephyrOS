#ifndef ETHERNET_H
#define ETHERNET_H

#include "types.h"

#define ETHERNET_MAC_ADDRESS_SIZE 6U
#define ETHERNET_HEADER_SIZE 14U
#define ETHERNET_MIN_FRAME_SIZE 60U
#define ETHERNET_MAX_FRAME_SIZE 1518U
#define ETHERNET_MAX_PAYLOAD_SIZE 1500U
#define ETHERNET_RX_POLL_BUDGET 8U

typedef int (*ethernet_receive_frame_fn)(uint8_t* data, uint16_t capacity,
                                         uint16_t* out_length,
                                         uint8_t* out_received);
typedef int (*ethernet_send_frame_fn)(const uint8_t* data, uint16_t length);

typedef enum {
    ETHERNET_DESTINATION_UNKNOWN = 0,
    ETHERNET_DESTINATION_LOCAL_UNICAST,
    ETHERNET_DESTINATION_BROADCAST
} ethernet_destination_t;

typedef struct {
    uint8_t initialized;
    uint8_t mac_address[ETHERNET_MAC_ADDRESS_SIZE];
    ethernet_receive_frame_fn receive_frame;
    ethernet_send_frame_fn send_frame;
} ethernet_interface_t;

typedef struct {
    uint8_t initialized;
    uint8_t mac_address[ETHERNET_MAC_ADDRESS_SIZE];
    uint32_t polls;
    uint32_t rx_frames;
    uint32_t rx_unicast;
    uint32_t rx_broadcast;
    uint32_t rx_invalid;
    uint32_t rx_filtered;
    uint32_t rx_unhandled;
    uint32_t tx_frames;
    uint16_t last_frame_length;
    uint16_t last_ethertype;
    uint8_t last_source[ETHERNET_MAC_ADDRESS_SIZE];
    uint8_t last_destination[ETHERNET_MAC_ADDRESS_SIZE];
    ethernet_destination_t last_destination_type;
    int last_error;
} ethernet_status_t;

int ethernet_init(const ethernet_interface_t* interface);
int ethernet_poll(uint32_t budget, uint32_t* out_processed);
int ethernet_send(const uint8_t* destination, uint16_t ethertype,
                  const uint8_t* payload, uint16_t payload_length);
int ethernet_get_status(ethernet_status_t* out_status);

#endif
