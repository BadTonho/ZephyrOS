#ifndef DNS_H
#define DNS_H

#include "types.h"

#define DNS_CACHE_CAPACITY 16U
#define DNS_NAME_MAX_LENGTH 253U
#define DNS_NAME_BUFFER_SIZE (DNS_NAME_MAX_LENGTH + 1U)
#define DNS_MAX_PACKET_SIZE 512U
#define DNS_SERVER_PORT 53U

typedef enum {
    DNS_STATE_IDLE = 0,
    DNS_STATE_RESOLVING_ARP,
    DNS_STATE_WAITING_REPLY,
    DNS_STATE_COMPLETE,
    DNS_STATE_FAILED
} dns_state_t;

typedef struct {
    uint8_t used;
    char name[DNS_NAME_BUFFER_SIZE];
    uint32_t address;
    uint32_t ttl_remaining_seconds;
    uint32_t age_seconds;
} dns_cache_entry_info_t;

typedef struct {
    uint8_t initialized;
    uint8_t configured;
    uint16_t local_port;
    uint32_t server_ip;
    dns_state_t state;
    char query_name[DNS_NAME_BUFFER_SIZE];
    char canonical_name[DNS_NAME_BUFFER_SIZE];
    uint32_t result_ip;
    uint16_t transaction_id;
    uint8_t attempts;
    uint8_t cname_depth;
    uint8_t cache_entries;
    uint32_t queries_tx;
    uint32_t replies_rx;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t timeouts;
    uint32_t invalid_packets;
    uint32_t ignored_packets;
    uint32_t maintenance_cycles;
    uint32_t event_generation;
    int last_error;
} dns_status_t;

int dns_init(void);
int dns_configure(uint32_t server_ip);
int dns_unconfigure(void);
int dns_resolve(const char* name, uint32_t* out_ip,
                uint8_t* out_resolved);
int dns_maintain(void);
int dns_reset(void);
int dns_clear(void);
int dns_get_status(dns_status_t* out_status);
int dns_get_cache_entry(uint32_t index,
                        dns_cache_entry_info_t* out_entry);
int dns_validate_state(void);
const char* dns_state_name(dns_state_t state);

#endif
