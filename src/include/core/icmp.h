#ifndef ICMP_H
#define ICMP_H

#include "types.h"

#define ICMP_ECHO_DATA_SIZE 32U
#define ICMP_ECHO_HEADER_SIZE 8U
#define ICMP_ECHO_MESSAGE_SIZE (ICMP_ECHO_HEADER_SIZE + ICMP_ECHO_DATA_SIZE)
#define ICMP_PING_DEFAULT_COUNT 4U
#define ICMP_PING_MAX_COUNT 10U
#define ICMP_PING_TIMEOUT_SECONDS 1U

typedef enum {
    ICMP_PING_IDLE = 0,
    ICMP_PING_RESOLVING,
    ICMP_PING_WAITING_REPLY,
    ICMP_PING_COMPLETE,
    ICMP_PING_FAILED
} icmp_ping_state_t;

typedef enum {
    ICMP_PING_EVENT_NONE = 0,
    ICMP_PING_EVENT_REPLY,
    ICMP_PING_EVENT_TIMEOUT
} icmp_ping_event_t;

typedef struct {
    uint8_t initialized;
    icmp_ping_state_t state;
    uint32_t target_ip;
    uint16_t identifier;
    uint16_t current_sequence;
    uint8_t requested_count;
    uint8_t sent;
    uint8_t received;
    uint8_t timeouts;
    uint32_t echo_requests_rx;
    uint32_t echo_requests_tx;
    uint32_t echo_replies_rx;
    uint32_t echo_replies_tx;
    uint32_t invalid_packets;
    uint32_t ignored_packets;
    uint32_t pending_reply_drops;
    uint8_t reply_pending;
    uint32_t rtt_min_ticks;
    uint32_t rtt_max_ticks;
    uint32_t rtt_total_ticks;
    uint32_t last_rtt_ticks;
    uint8_t last_reply_ttl;
    uint32_t event_generation;
    icmp_ping_event_t last_event;
    int last_error;
} icmp_status_t;

int icmp_init(void);
int icmp_ping_start(uint32_t target_ip, uint8_t count,
                    uint32_t timeout_seconds);
int icmp_maintain(void);
int icmp_reset(void);
int icmp_get_status(icmp_status_t* out_status);
int icmp_validate_state(void);
const char* icmp_ping_state_name(icmp_ping_state_t state);

#endif
