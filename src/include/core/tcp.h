#ifndef TCP_H
#define TCP_H

#include "types.h"

#define TCP_CONNECTION_CAPACITY 16U
#define TCP_HEADER_MIN_SIZE 20U
#define TCP_HEADER_MAX_SIZE 60U
#define TCP_LOCAL_MSS 536U
#define TCP_DEFAULT_RECEIVE_WINDOW 4096U
#define TCP_EPHEMERAL_PORT_MIN 49152U
#define TCP_EPHEMERAL_PORT_MAX 65535U

typedef uint32_t tcp_connection_handle_t;

typedef enum {
    TCP_STATE_CLOSED = 0,
    TCP_STATE_SYN_SENT,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT_1,
    TCP_STATE_FIN_WAIT_2,
    TCP_STATE_CLOSING,
    TCP_STATE_CLOSE_WAIT,
    TCP_STATE_LAST_ACK,
    TCP_STATE_TIME_WAIT,
    TCP_STATE_FAILED,
    TCP_STATE_LISTEN
} tcp_state_t;

typedef enum {
    TCP_EVENT_CONNECTED = 0,
    TCP_EVENT_DATA,
    TCP_EVENT_WRITABLE,
    TCP_EVENT_EOF,
    TCP_EVENT_CLOSED,
    TCP_EVENT_ERROR
} tcp_event_t;

typedef int (*tcp_event_fn)(tcp_connection_handle_t handle,
                            tcp_event_t event,
                            const uint8_t* data,
                            uint16_t length,
                            int error);

typedef struct {
    uint8_t used;
    tcp_connection_handle_t handle;
    tcp_state_t state;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t send_unacknowledged;
    uint32_t send_next;
    uint32_t receive_next;
    uint16_t local_window;
    uint16_t remote_window;
    uint16_t remote_mss;
    uint16_t pending_length;
    uint8_t retransmissions;
    uint32_t rto_milliseconds;
    uint32_t idle_seconds;
    int last_error;
} tcp_connection_info_t;

typedef struct {
    uint8_t initialized;
    uint8_t connection_count;
    uint32_t segments_rx;
    uint32_t segments_tx;
    uint32_t bytes_rx;
    uint32_t bytes_tx;
    uint32_t syn_tx;
    uint32_t syn_ack_rx;
    uint32_t fin_rx;
    uint32_t fin_tx;
    uint32_t resets_rx;
    uint32_t resets_tx;
    uint32_t retransmissions;
    uint32_t timeouts;
    uint32_t rx_invalid;
    uint32_t rx_checksum_errors;
    uint32_t rx_no_connection;
    uint32_t rx_duplicates;
    uint32_t rx_out_of_order;
    uint32_t callback_errors;
    uint32_t maintenance_cycles;
    int last_error;
} tcp_status_t;

int tcp_init(void);
int tcp_connect(uint16_t local_port, uint32_t remote_ip,
                uint16_t remote_port, uint16_t receive_window,
                tcp_event_fn callback,
                tcp_connection_handle_t* out_handle);
int tcp_send(tcp_connection_handle_t handle, const uint8_t* data,
             uint16_t length, uint16_t* out_accepted);
int tcp_set_receive_window(tcp_connection_handle_t handle,
                           uint16_t receive_window);
int tcp_close(tcp_connection_handle_t handle);
int tcp_abort(tcp_connection_handle_t handle);
int tcp_maintain(void);
int tcp_reset(void);
int tcp_get_status(tcp_status_t* out_status);
int tcp_get_connection_info(uint32_t index,
                            tcp_connection_info_t* out_info);
int tcp_validate_state(void);
const char* tcp_state_name(tcp_state_t state);

#endif
