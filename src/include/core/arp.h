#ifndef ARP_H
#define ARP_H

#include "types.h"

#define ARP_CACHE_CAPACITY 32U
#define ARP_INTERFACE_ID_SIZE 20U
#define ARP_MAC_ADDRESS_SIZE 6U

typedef enum {
    ARP_ENTRY_INCOMPLETE = 0,
    ARP_ENTRY_RESOLVED,
    ARP_ENTRY_FAILED
} arp_entry_state_t;

typedef struct {
    uint8_t initialized;
    uint8_t configured;
    char interface_id[ARP_INTERFACE_ID_SIZE];
    uint32_t local_ip;
    uint8_t local_mac[ARP_MAC_ADDRESS_SIZE];
    uint32_t cache_entries;
    uint32_t incomplete_entries;
    uint32_t resolved_entries;
    uint32_t failed_entries;
    uint32_t tx_requests;
    uint32_t tx_replies;
    uint32_t rx_requests;
    uint32_t rx_replies;
    uint32_t invalid_packets;
    uint32_t ignored_packets;
    uint32_t timeouts;
    int last_error;
} arp_status_t;

typedef struct {
    uint8_t used;
    uint32_t ip_address;
    uint8_t mac_address[ARP_MAC_ADDRESS_SIZE];
    arp_entry_state_t state;
    uint32_t age_seconds;
    uint8_t attempts;
} arp_cache_entry_info_t;

int arp_init(void);
int arp_configure(const char* interface_id, const uint8_t* local_mac,
                  uint32_t local_ip);
int arp_resolve(uint32_t ip_address, uint8_t* out_mac,
                uint8_t* out_resolved);
int arp_maintain(void);
int arp_clear(void);
int arp_get_status(arp_status_t* out_status);
int arp_get_cache_entry(uint32_t index,
                        arp_cache_entry_info_t* out_entry);
int arp_validate_state(void);
uint8_t arp_ipv4_is_valid(uint32_t ip_address);
const char* arp_entry_state_name(arp_entry_state_t state);

#endif
