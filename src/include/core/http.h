#ifndef HTTP_H
#define HTTP_H

#include "types.h"

#define HTTP_URL_MAX_LENGTH 511U
#define HTTP_URL_BUFFER_SIZE (HTTP_URL_MAX_LENGTH + 1U)
#define HTTP_HOST_MAX_LENGTH 253U
#define HTTP_HOST_BUFFER_SIZE (HTTP_HOST_MAX_LENGTH + 1U)
#define HTTP_PATH_MAX_LENGTH 255U
#define HTTP_PATH_BUFFER_SIZE (HTTP_PATH_MAX_LENGTH + 1U)
#define HTTP_HEADER_CAPACITY 4096U
#define HTTP_BODY_CAPACITY 16384U
#define HTTP_DEFAULT_PORT 80U
#define HTTP_STATUS_MINIMUM 200U
#define HTTP_STATUS_MAXIMUM 599U

typedef enum {
    HTTP_STATE_IDLE = 0,
    HTTP_STATE_RESOLVING,
    HTTP_STATE_CONNECTING,
    HTTP_STATE_SENDING,
    HTTP_STATE_RECEIVING_HEADERS,
    HTTP_STATE_RECEIVING_BODY,
    HTTP_STATE_COMPLETE,
    HTTP_STATE_FAILED
} http_state_t;

typedef int (*http_body_sink_t)(const uint8_t* data, uint32_t size,
                                void* context);

typedef struct {
    uint8_t initialized;
    http_state_t state;
    char url[HTTP_URL_BUFFER_SIZE];
    char host[HTTP_HOST_BUFFER_SIZE];
    char path[HTTP_PATH_BUFFER_SIZE];
    uint16_t port;
    uint32_t resolved_ip;
    uint16_t status_code;
    uint16_t headers_length;
    uint32_t body_length;
    uint32_t body_limit;
    uint32_t content_length;
    uint8_t has_content_length;
    uint8_t eof_framed;
    uint8_t streaming;
    uint32_t requests_started;
    uint32_t requests_tx;
    uint32_t responses_rx;
    uint32_t bytes_rx;
    uint32_t parse_errors;
    uint32_t overflows;
    uint32_t timeouts;
    uint32_t maintenance_cycles;
    uint32_t event_generation;
    int last_error;
} http_status_t;

int http_init(void);
int http_get_start(const char* url);
int http_get_stream_start(const char* url, uint32_t body_limit,
                          http_body_sink_t sink, void* context);
int http_maintain(void);
int http_reset(void);
int http_get_status(http_status_t* out_status);
int http_get_body(const uint8_t** out_body, uint32_t* out_length);
int http_validate_state(void);
const char* http_state_name(http_state_t state);

#endif
