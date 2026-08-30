#ifndef SOCKET_H
#define SOCKET_H

#include "types.h"

#define SOCKET_CAPACITY 16U
#define SOCKET_UNIX_PATH_MAX 108U
#define SOCKET_UNIX_BACKLOG_MAX 4U
#define SOCKET_QUEUE_CAPACITY 8U
#define SOCKET_QUEUE_BYTES 4096U
#define SOCKET_FLAG_NONBLOCK 0x01U

typedef struct socket socket_t;
typedef struct socket_ops socket_ops_t;

typedef enum {
    SOCKET_FAMILY_UNIX = 1,
    SOCKET_FAMILY_INET = 2
} socket_family_t;

typedef enum {
    SOCKET_TYPE_STREAM = 1
} socket_type_t;

typedef enum {
    SOCKET_STATE_OPEN = 0,
    SOCKET_STATE_BOUND,
    SOCKET_STATE_LISTENING,
    SOCKET_STATE_CONNECTING,
    SOCKET_STATE_CONNECTED,
    SOCKET_STATE_EOF,
    SOCKET_STATE_ERROR,
    SOCKET_STATE_CLOSED
} socket_state_t;

typedef struct {
    socket_family_t family;
    union {
        struct {
            uint32_t address;
            uint16_t port;
        } ipv4;
        struct {
            char path[SOCKET_UNIX_PATH_MAX];
        } local;
    } value;
} socket_address_t;

typedef struct {
    uint8_t used;
    int32_t fd;
    uint32_t owner_pid;
    socket_family_t family;
    socket_type_t type;
    socket_state_t state;
    uint32_t flags;
    socket_address_t local;
    socket_address_t remote;
    uint16_t rx_queued;
    uint16_t tx_queued;
    uint16_t backlog;
    uint16_t pending;
    int last_error;
} socket_info_t;

typedef struct {
    uint8_t initialized;
    uint16_t active_count;
    uint16_t peak_count;
    uint32_t creates;
    uint32_t closes;
    uint32_t binds;
    uint32_t connects;
    uint32_t accepts;
    uint32_t sends;
    uint32_t receives;
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint32_t queue_drops;
    uint32_t stale_fds;
    uint32_t failures;
    int last_error;
} socket_status_t;

typedef struct {
    uint8_t lifecycle;
    uint8_t fd_mapping;
    uint8_t duplicate_bind;
    uint8_t unix_bind_connect;
    uint8_t unix_accept;
    uint8_t stream_io;
    uint8_t queue_full;
    uint8_t nonblocking;
    uint8_t close_wakeup;
    uint8_t eof;
    uint8_t cancellation;
    uint8_t invalid_inputs;
    uint8_t invariants;
    uint32_t passed;
    uint32_t failed;
} socket_self_test_result_t;

int socket_init(void);
int socket_create(socket_family_t family, socket_type_t type,
                  uint32_t flags, int32_t* fd_out);
int socket_bind(int32_t fd, const socket_address_t* address);
int socket_connect(int32_t fd, const socket_address_t* address);
int socket_listen(int32_t fd, uint16_t backlog);
int socket_accept(int32_t fd, int32_t* fd_out);
int socket_send(int32_t fd, const uint8_t* data,
                uint32_t length, uint32_t* out_sent);
int socket_recv(int32_t fd, uint8_t* buffer,
                uint32_t capacity, uint32_t* out_read,
                uint8_t* out_eof);
int socket_set_nonblocking(int32_t fd, uint8_t enabled);
int socket_close(int32_t fd);
int socket_get_status(socket_status_t* out_status);
int socket_get_info(uint32_t index, socket_info_t* out_info);
int socket_validate_state(void);
int socket_self_test(socket_self_test_result_t* out_result);
const char* socket_family_name(socket_family_t family);
const char* socket_state_name(socket_state_t state);

#endif
