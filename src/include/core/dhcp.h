#ifndef DHCP_H
#define DHCP_H

#include "types.h"

#define DHCP_INTERFACE_ID_SIZE 20U
#define DHCP_MAC_ADDRESS_SIZE 6U
#define DHCP_CLIENT_PORT 68U
#define DHCP_SERVER_PORT 67U
#define DHCP_MAX_MESSAGE_SIZE 548U

typedef enum {
    DHCP_STATE_IDLE = 0,
    DHCP_STATE_SELECTING,
    DHCP_STATE_REQUESTING,
    DHCP_STATE_APPLYING,
    DHCP_STATE_BOUND,
    DHCP_STATE_RENEWING,
    DHCP_STATE_REBINDING,
    DHCP_STATE_FAILED,
    DHCP_STATE_EXPIRED
} dhcp_state_t;

typedef enum {
    DHCP_EVENT_NONE = 0,
    DHCP_EVENT_APPLY_LEASE,
    DHCP_EVENT_DROP_LEASE
} dhcp_event_t;

typedef struct {
    uint32_t address;
    uint32_t subnet_mask;
    uint32_t gateway;
    uint32_t dns_server;
    uint32_t server_identifier;
    uint32_t lease_seconds;
    uint32_t t1_seconds;
    uint32_t t2_seconds;
} dhcp_lease_t;

typedef struct {
    uint8_t initialized;
    dhcp_state_t state;
    char interface_id[DHCP_INTERFACE_ID_SIZE];
    uint8_t local_mac[DHCP_MAC_ADDRESS_SIZE];
    uint32_t transaction_id;
    dhcp_lease_t lease;
    uint32_t lease_remaining_seconds;
    uint32_t t1_remaining_seconds;
    uint32_t t2_remaining_seconds;
    uint8_t attempts;
    uint32_t discovers_tx;
    uint32_t offers_rx;
    uint32_t requests_tx;
    uint32_t acks_rx;
    uint32_t naks_rx;
    uint32_t releases_tx;
    uint32_t invalid_packets;
    uint32_t ignored_packets;
    uint32_t timeouts;
    uint32_t maintenance_cycles;
    dhcp_event_t pending_event;
    int last_error;
} dhcp_status_t;

int dhcp_init(void);
int dhcp_acquire(const char* interface_id, const uint8_t* local_mac);
int dhcp_renew(void);
int dhcp_release(uint8_t* out_sent);
int dhcp_reset(void);
int dhcp_maintain(void);
int dhcp_take_event(dhcp_event_t* out_event,
                    dhcp_lease_t* out_lease);
int dhcp_complete_event(dhcp_event_t event, int result);
int dhcp_get_status(dhcp_status_t* out_status);
int dhcp_validate_state(void);
const char* dhcp_state_name(dhcp_state_t state);

#endif
