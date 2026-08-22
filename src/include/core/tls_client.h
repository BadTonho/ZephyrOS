#ifndef TLS_CLIENT_H
#define TLS_CLIENT_H

#include "types.h"
#include "core/tls.h"
#include "core/net_socket.h"

#define TLS_CLIENT_HOST_SIZE 254U

typedef enum {
    TLS_CLIENT_STATE_IDLE = 0,
    TLS_CLIENT_STATE_HANDSHAKING,
    TLS_CLIENT_STATE_READY,
    TLS_CLIENT_STATE_FAILED
} tls_client_state_t;

typedef struct {
    uint8_t initialized;
    uint8_t active;
    uint8_t handshake_complete;
    uint8_t x509_verified;
    uint8_t hostname_verified;
    tls_client_state_t state;
    tls_reason_t reason;
    uint16_t bearssl_error;
    uint16_t negotiated_version;
    uint16_t negotiated_suite;
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint32_t handshake_count;
    int last_error;
} tls_client_status_t;

int tls_client_init(void);
int tls_client_start(net_socket_handle_t socket, const char* hostname);
int tls_client_maintain(void);
int tls_client_send(const uint8_t* data, uint16_t length,
                    uint16_t* out_written);
int tls_client_receive(uint8_t* data, uint16_t capacity,
                       uint16_t* out_read, uint8_t* out_eof);
int tls_client_close(void);
int tls_client_get_status(tls_client_status_t* output);
int tls_client_validate_state(void);

#endif
