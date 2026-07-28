#ifndef NET_SOCKET_H
#define NET_SOCKET_H

#include "types.h"

#define NET_SOCKET_CAPACITY 16U
#define NET_SOCKET_TX_CAPACITY 2048U
#define NET_SOCKET_RX_CAPACITY 4096U

typedef uint32_t net_socket_handle_t;

typedef enum {
    NET_SOCKET_TYPE_STREAM = 1
} net_socket_type_t;

typedef enum {
    NET_SOCKET_STATE_OPEN = 0,
    NET_SOCKET_STATE_CONNECTING,
    NET_SOCKET_STATE_CONNECTED,
    NET_SOCKET_STATE_CLOSING,
    NET_SOCKET_STATE_EOF,
    NET_SOCKET_STATE_ERROR
} net_socket_state_t;

typedef struct {
    uint8_t used;
    net_socket_handle_t handle;
    net_socket_type_t type;
    net_socket_state_t state;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint16_t tx_queued;
    uint16_t rx_queued;
    uint8_t eof;
    int last_error;
} net_socket_info_t;

typedef struct {
    uint8_t initialized;
    uint8_t active_count;
    uint32_t opens;
    uint32_t connects;
    uint32_t closes;
    uint32_t aborts;
    uint32_t bytes_queued_tx;
    uint32_t bytes_sent_tcp;
    uint32_t bytes_received_tcp;
    uint32_t bytes_read;
    uint32_t rx_overflows;
    uint32_t stale_handles;
    uint32_t maintenance_cycles;
    int last_error;
} net_socket_status_t;

int net_socket_init(void);
int net_socket_open(net_socket_type_t type,
                    net_socket_handle_t* out_handle);
int net_socket_connect(net_socket_handle_t handle,
                       uint32_t remote_ip, uint16_t remote_port);
int net_socket_send(net_socket_handle_t handle, const uint8_t* data,
                    uint16_t length, uint16_t* out_written);
int net_socket_receive(net_socket_handle_t handle, uint8_t* buffer,
                       uint16_t capacity, uint16_t* out_read,
                       uint8_t* out_eof);
int net_socket_close(net_socket_handle_t handle);
int net_socket_abort(net_socket_handle_t handle);
int net_socket_maintain(void);
int net_socket_reset(void);
int net_socket_get_status(net_socket_status_t* out_status);
int net_socket_get_info(uint32_t index, net_socket_info_t* out_info);
int net_socket_get_handle_info(net_socket_handle_t handle,
                               net_socket_info_t* out_info);
int net_socket_validate_state(void);
const char* net_socket_state_name(net_socket_state_t state);

#endif
