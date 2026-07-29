#ifndef IPV4_H
#define IPV4_H

#include "types.h"

#define IPV4_INTERFACE_ID_SIZE 20U
#define IPV4_MAC_ADDRESS_SIZE 6U
#define IPV4_HEADER_SIZE 20U
#define IPV4_MTU 1500U
#define IPV4_MAX_PAYLOAD_SIZE (IPV4_MTU - IPV4_HEADER_SIZE)
#define IPV4_PROTOCOL_HANDLER_CAPACITY 4U
#define IPV4_PROTOCOL_ICMP 1U
#define IPV4_PROTOCOL_TCP 6U
#define IPV4_PROTOCOL_UDP 17U
#define IPV4_LIMITED_BROADCAST 0xFFFFFFFFU

typedef enum {
    IPV4_DELIVERY_LOCAL_UNICAST = 0,
    IPV4_DELIVERY_LIMITED_BROADCAST
} ipv4_delivery_t;

typedef struct {
    const char* interface_id;
    const uint8_t* payload;
    uint16_t payload_length;
    uint32_t source_ip;
    uint32_t destination_ip;
    uint16_t identification;
    uint8_t protocol;
    uint8_t ttl;
    ipv4_delivery_t delivery;
} ipv4_packet_view_t;

typedef int (*ipv4_protocol_handler_fn)(const ipv4_packet_view_t* packet);

typedef struct {
    uint8_t initialized;
    uint8_t configured;
    char interface_id[IPV4_INTERFACE_ID_SIZE];
    uint8_t local_mac[IPV4_MAC_ADDRESS_SIZE];
    uint32_t local_ip;
    uint32_t subnet_mask;
    uint32_t gateway;
    uint32_t configuration_generation;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t tx_direct;
    uint32_t tx_via_gateway;
    uint32_t rx_limited_broadcast;
    uint32_t tx_limited_broadcast;
    uint32_t rx_invalid;
    uint32_t rx_checksum_errors;
    uint32_t rx_ignored;
    uint32_t rx_options;
    uint32_t rx_fragments;
    uint32_t rx_unhandled;
    uint32_t rx_delivered;
    uint32_t rx_protocol_errors;
    uint8_t handler_count;
    uint16_t next_identification;
    int last_error;
} ipv4_status_t;

int ipv4_init(void);
int ipv4_configure(const char* interface_id, const uint8_t* local_mac,
                   uint32_t local_ip, uint32_t subnet_mask,
                   uint32_t gateway);
int ipv4_unconfigure(void);
int ipv4_register_handler(uint8_t protocol,
                          ipv4_protocol_handler_fn handler);
int ipv4_send(uint32_t destination_ip, uint8_t protocol,
              const uint8_t* payload, uint16_t payload_length,
              uint8_t* out_sent);
int ipv4_send_limited_broadcast(const char* interface_id,
                                uint32_t source_ip, uint8_t protocol,
                                const uint8_t* payload,
                                uint16_t payload_length,
                                uint8_t* out_sent);
int ipv4_get_status(ipv4_status_t* out_status);
int ipv4_validate_state(void);
uint8_t ipv4_address_is_unicast(uint32_t ip_address);
uint8_t ipv4_mask_is_valid(uint32_t subnet_mask);
const char* ipv4_protocol_name(uint8_t protocol);

#endif
