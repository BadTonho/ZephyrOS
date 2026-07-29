#ifndef UDP_H
#define UDP_H

#include "types.h"
#include "core/ipv4.h"

#define UDP_ENDPOINT_CAPACITY 16U
#define UDP_HEADER_SIZE 8U
#define UDP_MAX_PAYLOAD_SIZE (IPV4_MAX_PAYLOAD_SIZE - UDP_HEADER_SIZE)
#define UDP_BIND_ALLOW_BROADCAST 0x01U

typedef uint32_t udp_endpoint_handle_t;

typedef struct {
    const char* interface_id;
    const uint8_t* payload;
    uint16_t payload_length;
    uint32_t source_ip;
    uint32_t destination_ip;
    uint16_t source_port;
    uint16_t destination_port;
    ipv4_delivery_t delivery;
} udp_datagram_view_t;

typedef int (*udp_receive_fn)(const udp_datagram_view_t* datagram);

typedef struct {
    uint8_t initialized;
    uint8_t endpoint_count;
    uint32_t rx_datagrams;
    uint32_t tx_datagrams;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_broadcast;
    uint32_t tx_broadcast;
    uint32_t rx_invalid;
    uint32_t rx_length_errors;
    uint32_t rx_checksum_errors;
    uint32_t rx_no_listener;
    uint32_t rx_delivered;
    uint32_t rx_protocol_errors;
    uint32_t tx_errors;
    int last_error;
} udp_status_t;

int udp_init(void);
int udp_bind(uint16_t local_port, uint8_t flags,
             udp_receive_fn callback,
             udp_endpoint_handle_t* out_handle);
int udp_unbind(udp_endpoint_handle_t handle);
int udp_send(udp_endpoint_handle_t handle, uint32_t destination_ip,
             uint16_t destination_port, const uint8_t* payload,
             uint16_t payload_length, uint8_t* out_sent);
int udp_send_limited_broadcast(udp_endpoint_handle_t handle,
                               const char* interface_id,
                               uint32_t source_ip,
                               uint16_t destination_port,
                               const uint8_t* payload,
                               uint16_t payload_length,
                               uint8_t* out_sent);
int udp_get_status(udp_status_t* out_status);
int udp_validate_state(void);

#endif
