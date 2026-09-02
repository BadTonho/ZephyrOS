#include <stdint.h>
#include <stdio.h>

#include "core/dns.h"
#include "core/errors.h"
#include "core/http.h"
#include "core/log.h"
#include "core/net_socket.h"
#include "core/string.h"
#include "core/tls.h"
#include "core/tls_client.h"
#include "core/video.h"
#include "drivers/serial.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HTTP_TEST_SOCKET 0x1001U
#define HTTP_TEST_IP 0x0A000202U
#define HTTP_TEST_ALT_IP 0x0A000203U
#define HTTP_TEST_RESPONSE_CAPACITY 8192U
#define HTTP_TEST_TRANSMIT_CAPACITY 8192U
#define HTTP_TEST_DRIVE_LIMIT 512U
#define HTTP_TEST_TIMEOUT_TICKS 45000U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

static void __attribute__((no_instrument_function)) coverage_record(
    void* function) {
    uintptr_t address = (uintptr_t)function;

    if (!coverage_active || !address) return;
    for (uint32_t index = 0U; index < coverage_count; index++) {
        if (coverage_addresses[index] == address) return;
    }
    if (coverage_count < HOST_COVERAGE_CAPACITY) {
        coverage_addresses[coverage_count++] = address;
    }
}

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(
    void* function, void* caller) {
    (void)caller;
    coverage_record(function);
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(
    void* function, void* caller) {
    (void)function;
    (void)caller;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:network:http|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:network:http|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:network:http|value=0x%08X\n",
           (uint32_t)result);
}

static net_socket_status_t fake_socket_status;
static net_socket_info_t fake_socket_info;
static int fake_socket_open_result;
static int fake_socket_connect_result;
static int fake_socket_send_result;
static int fake_socket_receive_result;
static int fake_socket_close_result;
static int fake_socket_abort_result;
static net_socket_state_t fake_connect_state;
static uint8_t fake_transmitted[HTTP_TEST_TRANSMIT_CAPACITY];
static uint16_t fake_transmitted_length;
static uint8_t fake_response[HTTP_TEST_RESPONSE_CAPACITY];
static uint16_t fake_response_length;
static uint16_t fake_response_offset;
static uint16_t fake_response_chunk;
static uint8_t fake_response_eof;

static dns_status_t fake_dns_status;
static int fake_dns_resolve_result;
static int fake_dns_reset_result;
static uint8_t fake_dns_resolved;

static tls_status_t fake_tls_status;
static tls_client_status_t fake_tls_client_status;
static int fake_tls_capability;
static int fake_tls_start_result;
static int fake_tls_maintain_result;
static int fake_tls_send_result;
static int fake_tls_receive_result;

static uint32_t fake_ticks;
static uint32_t fake_frequency;
static int fake_stack_result;
static uint32_t fake_stack_remaining;

static uint32_t sink_calls;
static uint32_t sink_bytes;
static int sink_result;

static int contains_text(const uint8_t* data, uint16_t length,
                          const char* text) {
    uint32_t text_length = kstrlen(text);

    if (!data || !text || text_length > length) return 0;
    for (uint16_t offset = 0U; offset <= length - text_length; offset++) {
        uint32_t index;

        for (index = 0U; index < text_length; index++) {
            if (data[offset + index] != (uint8_t)text[index]) break;
        }
        if (index == text_length) return 1;
    }
    return 0;
}

uint32_t timer_get_ticks(void) {
    return fake_ticks;
}

uint32_t timer_get_frequency(void) {
    return fake_frequency;
}

int process_stack_check_current(uint32_t* remaining_out) {
    if (remaining_out) *remaining_out = fake_stack_remaining;
    return fake_stack_result;
}

uint8_t serial_is_ready(void) {
    return 1U;
}

uint32_t serial_write_text(const char* text, uint32_t length) {
    (void)text;
    return length;
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void video_newline(void) {
}

uint8_t ipv4_address_is_unicast(uint32_t ip_address) {
    uint8_t first_octet = (uint8_t)(ip_address >> 24U);

    return (uint8_t)(ip_address && ip_address != 0xFFFFFFFFU &&
                     first_octet < 224U && first_octet != 127U);
}

static int fake_socket_receive_bytes(uint8_t* buffer, uint16_t capacity,
                                     uint16_t* out_read, uint8_t* out_eof,
                                     int result) {
    uint16_t remaining;
    uint16_t amount;

    if (!buffer || !out_read || !out_eof) return ERR_NULL;
    if (result != OK) return result;
    remaining = (uint16_t)(fake_response_length - fake_response_offset);
    amount = remaining < capacity ? remaining : capacity;
    if (fake_response_chunk && amount > fake_response_chunk) {
        amount = fake_response_chunk;
    }
    if (amount) {
        kmemcpy(buffer, fake_response + fake_response_offset, amount);
        fake_response_offset = (uint16_t)(fake_response_offset + amount);
    }
    *out_read = amount;
    *out_eof = (uint8_t)(fake_response_eof &&
                         fake_response_offset == fake_response_length);
    return OK;
}

static int fake_tls_send_bytes(const uint8_t* data, uint16_t length,
                               uint16_t* out_written, int result) {
    if (!data || !out_written) return ERR_NULL;
    if (result != OK) return result;
    if (length > sizeof(fake_transmitted)) return ERR_OVERFLOW;
    kmemcpy(fake_transmitted, data, length);
    fake_transmitted_length = length;
    *out_written = length;
    return OK;
}

int net_socket_init(void) {
    fake_socket_status.initialized = 1U;
    return OK;
}

int net_socket_open(net_socket_type_t type, net_socket_handle_t* out_handle) {
    if (!out_handle || type != NET_SOCKET_TYPE_STREAM) return ERR_INVALID;
    if (fake_socket_open_result != OK) return fake_socket_open_result;
    fake_socket_info.used = 1U;
    fake_socket_info.handle = HTTP_TEST_SOCKET;
    fake_socket_info.type = type;
    fake_socket_info.state = NET_SOCKET_STATE_OPEN;
    fake_socket_info.last_error = OK;
    fake_socket_status.active_count = 1U;
    fake_socket_status.opens++;
    *out_handle = HTTP_TEST_SOCKET;
    return OK;
}

int net_socket_connect(net_socket_handle_t handle, uint32_t remote_ip,
                       uint16_t remote_port) {
    if (handle != HTTP_TEST_SOCKET || !fake_socket_info.used ||
        !remote_ip || !remote_port) return ERR_INVALID;
    if (fake_socket_connect_result != OK) return fake_socket_connect_result;
    fake_socket_info.remote_ip = remote_ip;
    fake_socket_info.remote_port = remote_port;
    fake_socket_info.state = fake_connect_state;
    fake_socket_status.connects++;
    return OK;
}

int net_socket_send(net_socket_handle_t handle, const uint8_t* data,
                    uint16_t length, uint16_t* out_written) {
    if (handle != HTTP_TEST_SOCKET || !data || !out_written) return ERR_INVALID;
    if (fake_socket_send_result != OK) return fake_socket_send_result;
    if (length > sizeof(fake_transmitted)) return ERR_OVERFLOW;
    kmemcpy(fake_transmitted, data, length);
    fake_transmitted_length = length;
    *out_written = length;
    fake_socket_status.bytes_queued_tx += length;
    return OK;
}

int net_socket_receive(net_socket_handle_t handle, uint8_t* buffer,
                       uint16_t capacity, uint16_t* out_read,
                       uint8_t* out_eof) {
    if (handle != HTTP_TEST_SOCKET || !fake_socket_info.used) return ERR_INVALID;
    return fake_socket_receive_bytes(buffer, capacity, out_read, out_eof,
                                     fake_socket_receive_result);
}

int net_socket_wait(net_socket_handle_t handle, net_socket_event_mask_t events,
                    uint32_t timeout_ticks, net_socket_event_mask_t* out_events,
                    wait_reason_t* out_reason) {
    (void)handle;
    (void)events;
    (void)timeout_ticks;
    if (!out_events || !out_reason) return ERR_NULL;
    *out_events = 0U;
    *out_reason = WAIT_REASON_TIMEOUT;
    return OK;
}

int net_socket_close(net_socket_handle_t handle) {
    if (handle != HTTP_TEST_SOCKET || !fake_socket_info.used) return ERR_INVALID;
    if (fake_socket_close_result != OK) return fake_socket_close_result;
    fake_socket_info.used = 0U;
    fake_socket_info.state = NET_SOCKET_STATE_EOF;
    fake_socket_status.active_count = 0U;
    fake_socket_status.closes++;
    return OK;
}

int net_socket_abort(net_socket_handle_t handle) {
    if (handle != HTTP_TEST_SOCKET || !fake_socket_info.used) return ERR_INVALID;
    if (fake_socket_abort_result != OK) return fake_socket_abort_result;
    fake_socket_info.used = 0U;
    fake_socket_info.state = NET_SOCKET_STATE_EOF;
    fake_socket_status.active_count = 0U;
    fake_socket_status.aborts++;
    return OK;
}

int net_socket_maintain(void) {
    return OK;
}

int net_socket_reset(void) {
    kmemset(&fake_socket_info, 0, sizeof(fake_socket_info));
    fake_socket_status.active_count = 0U;
    return OK;
}

int net_socket_get_status(net_socket_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_socket_status;
    return OK;
}

int net_socket_get_info(uint32_t index, net_socket_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (index >= NET_SOCKET_CAPACITY) return ERR_INVALID;
    if (index == 0U) *out_info = fake_socket_info;
    else kmemset(out_info, 0, sizeof(*out_info));
    return OK;
}

int net_socket_get_handle_info(net_socket_handle_t handle,
                               net_socket_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (handle != HTTP_TEST_SOCKET || !fake_socket_info.used) return ERR_INVALID;
    *out_info = fake_socket_info;
    return OK;
}

int net_socket_validate_state(void) {
    return fake_socket_status.active_count == (fake_socket_info.used ? 1U : 0U) ?
           OK : ERR_STATE;
}

int net_socket_self_test(net_socket_self_test_result_t* out_result) {
    if (!out_result) return ERR_NULL;
    kmemset(out_result, 0, sizeof(*out_result));
    out_result->lifecycle = 1U;
    out_result->event_mapping = 1U;
    out_result->invariants = 1U;
    out_result->passed = 3U;
    return OK;
}

const char* net_socket_state_name(net_socket_state_t state) {
    if (state == NET_SOCKET_STATE_OPEN) return "OPEN";
    if (state == NET_SOCKET_STATE_CONNECTING) return "CONNECTING";
    if (state == NET_SOCKET_STATE_CONNECTED) return "CONNECTED";
    if (state == NET_SOCKET_STATE_CLOSING) return "CLOSING";
    if (state == NET_SOCKET_STATE_EOF) return "EOF";
    if (state == NET_SOCKET_STATE_ERROR) return "ERROR";
    return "UNKNOWN";
}

int dns_init(void) {
    fake_dns_status.initialized = 1U;
    return OK;
}

int dns_configure(uint32_t server_ip) {
    fake_dns_status.configured = server_ip != 0U;
    fake_dns_status.server_ip = server_ip;
    return fake_dns_status.configured ? OK : ERR_INVALID;
}

int dns_unconfigure(void) {
    fake_dns_status.configured = 0U;
    return OK;
}

int dns_resolve(const char* name, uint32_t* out_ip, uint8_t* out_resolved) {
    if (!name || !out_ip || !out_resolved) return ERR_NULL;
    if (fake_dns_resolve_result != OK) return fake_dns_resolve_result;
    {
        uint32_t length = kstrlen(name);

        if (length >= DNS_NAME_BUFFER_SIZE) length = DNS_NAME_MAX_LENGTH;
        kmemcpy(fake_dns_status.query_name, name, length);
        fake_dns_status.query_name[length] = '\0';
    }
    *out_ip = fake_dns_resolved ? HTTP_TEST_ALT_IP : 0U;
    *out_resolved = fake_dns_resolved;
    fake_dns_status.state = fake_dns_resolved ? DNS_STATE_COMPLETE :
                            DNS_STATE_RESOLVING_ARP;
    return OK;
}

int dns_maintain(void) {
    return OK;
}

int dns_reset(void) {
    if (fake_dns_reset_result != OK) return fake_dns_reset_result;
    fake_dns_status.state = DNS_STATE_IDLE;
    fake_dns_status.query_name[0] = '\0';
    return OK;
}

int dns_clear(void) {
    return dns_reset();
}

int dns_get_status(dns_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_dns_status;
    return OK;
}

int dns_get_cache_entry(uint32_t index, dns_cache_entry_info_t* out_entry) {
    if (!out_entry) return ERR_NULL;
    if (index >= DNS_CACHE_CAPACITY) return ERR_INVALID;
    kmemset(out_entry, 0, sizeof(*out_entry));
    return OK;
}

int dns_validate_state(void) {
    return OK;
}

const char* dns_state_name(dns_state_t state) {
    if (state == DNS_STATE_IDLE) return "IDLE";
    if (state == DNS_STATE_RESOLVING_ARP) return "RESOLVING_ARP";
    if (state == DNS_STATE_WAITING_REPLY) return "WAITING_REPLY";
    if (state == DNS_STATE_COMPLETE) return "COMPLETE";
    if (state == DNS_STATE_FAILED) return "FAILED";
    return "UNKNOWN";
}

int tls_init(void) {
    fake_tls_status.initialized = 1U;
    fake_tls_status.state = TLS_STATE_READY;
    return OK;
}

int tls_get_policy(tls_policy_t* out_policy) {
    if (!out_policy) return ERR_NULL;
    kmemset(out_policy, 0, sizeof(*out_policy));
    out_policy->minimum_version = TLS_VERSION_1_2;
    return OK;
}

int tls_policy_validate(const tls_peer_identity_t* identity,
                        tls_reason_t* out_reason) {
    if (!identity || !out_reason) return ERR_NULL;
    *out_reason = TLS_REASON_NONE;
    return OK;
}

int tls_get_status(tls_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_tls_status;
    return OK;
}

int tls_capability_available(void) {
    return fake_tls_capability;
}

int tls_validate_state(void) {
    return OK;
}

int tls_self_test(tls_self_test_result_t* out_result) {
    if (!out_result) return ERR_NULL;
    kmemset(out_result, 0, sizeof(*out_result));
    out_result->invariants = 1U;
    out_result->passed = 1U;
    return OK;
}

const char* tls_state_name(tls_state_t state) {
    if (state == TLS_STATE_UNINITIALIZED) return "UNINITIALIZED";
    if (state == TLS_STATE_POLICY_ONLY) return "POLICY_ONLY";
    if (state == TLS_STATE_UNAVAILABLE) return "UNAVAILABLE";
    if (state == TLS_STATE_READY) return "READY";
    return "UNKNOWN";
}

const char* tls_reason_name(tls_reason_t reason) {
    return reason == TLS_REASON_NONE ? "NONE" : "UNKNOWN";
}

int tls_client_init(void) {
    fake_tls_client_status.initialized = 1U;
    fake_tls_client_status.state = TLS_CLIENT_STATE_IDLE;
    return OK;
}

int tls_client_start(net_socket_handle_t socket, const char* hostname) {
    if (socket != HTTP_TEST_SOCKET || !hostname) return ERR_INVALID;
    if (fake_tls_start_result != OK) return fake_tls_start_result;
    fake_tls_client_status.active = 1U;
    fake_tls_client_status.state = TLS_CLIENT_STATE_HANDSHAKING;
    fake_tls_client_status.handshake_complete = 0U;
    return OK;
}

int tls_client_maintain(void) {
    if (fake_tls_maintain_result != OK) return fake_tls_maintain_result;
    fake_tls_client_status.handshake_complete = 1U;
    fake_tls_client_status.x509_verified = 1U;
    fake_tls_client_status.hostname_verified = 1U;
    fake_tls_client_status.active = 1U;
    fake_tls_client_status.state = TLS_CLIENT_STATE_READY;
    fake_tls_client_status.reason = TLS_REASON_NONE;
    return OK;
}

int tls_client_send(const uint8_t* data, uint16_t length,
                    uint16_t* out_written) {
    return fake_tls_send_bytes(data, length, out_written, fake_tls_send_result);
}

int tls_client_receive(uint8_t* data, uint16_t capacity, uint16_t* out_read,
                       uint8_t* out_eof) {
    return fake_socket_receive_bytes(data, capacity, out_read, out_eof,
                                     fake_tls_receive_result);
}

int tls_client_close(void) {
    fake_tls_client_status.active = 0U;
    fake_tls_client_status.state = TLS_CLIENT_STATE_IDLE;
    return OK;
}

int tls_client_get_status(tls_client_status_t* output) {
    if (!output) return ERR_NULL;
    *output = fake_tls_client_status;
    return OK;
}

int tls_client_validate_state(void) {
    return OK;
}

static void reset_fakes(void) {
    kmemset(&fake_socket_status, 0, sizeof(fake_socket_status));
    fake_socket_status.initialized = 1U;
    kmemset(&fake_socket_info, 0, sizeof(fake_socket_info));
    fake_socket_open_result = OK;
    fake_socket_connect_result = OK;
    fake_socket_send_result = OK;
    fake_socket_receive_result = OK;
    fake_socket_close_result = OK;
    fake_socket_abort_result = OK;
    fake_connect_state = NET_SOCKET_STATE_CONNECTED;
    fake_transmitted_length = 0U;
    fake_response_length = 0U;
    fake_response_offset = 0U;
    fake_response_chunk = 0U;
    fake_response_eof = 0U;
    kmemset(&fake_dns_status, 0, sizeof(fake_dns_status));
    fake_dns_status.initialized = 1U;
    fake_dns_resolve_result = OK;
    fake_dns_reset_result = OK;
    fake_dns_resolved = 0U;
    kmemset(&fake_tls_status, 0, sizeof(fake_tls_status));
    fake_tls_status.initialized = 1U;
    fake_tls_status.state = TLS_STATE_READY;
    fake_tls_capability = 0;
    fake_tls_start_result = OK;
    fake_tls_maintain_result = OK;
    fake_tls_send_result = OK;
    fake_tls_receive_result = OK;
    kmemset(&fake_tls_client_status, 0, sizeof(fake_tls_client_status));
    fake_tls_client_status.initialized = 1U;
    fake_tls_client_status.state = TLS_CLIENT_STATE_IDLE;
    fake_ticks = 100U;
    fake_frequency = 1000U;
    fake_stack_result = OK;
    fake_stack_remaining = 4096U;
    sink_calls = 0U;
    sink_bytes = 0U;
    sink_result = OK;
}

static void load_response(const char* response, uint8_t eof,
                          uint16_t chunk) {
    uint32_t length = kstrlen(response);

    kmemcpy(fake_response, response, length);
    fake_response_length = (uint16_t)length;
    fake_response_offset = 0U;
    fake_response_eof = eof;
    fake_response_chunk = chunk;
}

static int body_sink(const uint8_t* data, uint32_t size, void* context) {
    uint32_t* checksum = (uint32_t*)context;

    if (!data || !checksum) return ERR_NULL;
    sink_calls++;
    sink_bytes += size;
    for (uint32_t index = 0U; index < size; index++) {
        *checksum += data[index];
    }
    return sink_result;
}

static int drive_to_terminal(void) {
    http_status_t status;

    for (uint32_t iteration = 0U; iteration < HTTP_TEST_DRIVE_LIMIT;
         iteration++) {
        if (http_get_status(&status) != OK) return ERR_STATE;
        if (status.state == HTTP_STATE_COMPLETE ||
            status.state == HTTP_STATE_FAILED) return OK;
        if (http_maintain() != OK) return ERR_STATE;
    }
    return ERR_TIMEOUT;
}

static int check_contracts(void) {
    http_status_t status;
    const uint8_t* body;
    uint32_t body_length;
    http_request_options_t options;

    reset_fakes();
    if (http_get_status(NULL) != ERR_NULL || http_get_body(NULL, &body_length) != ERR_NULL ||
        http_get_body(&body, NULL) != ERR_NULL || http_maintain() != ERR_STATE ||
        http_reset() != ERR_STATE || http_validate_state() != OK ||
        kstrcmp(http_state_name(HTTP_STATE_IDLE), "IDLE") != 0 ||
        kstrcmp(http_state_name(HTTP_STATE_RECEIVING_BODY), "RECEIVING_BODY") != 0 ||
        kstrcmp(http_state_name((http_state_t)99), "DESCONHECIDO") != 0) return 1;
    fake_socket_status.initialized = 0U;
    if (http_init() != ERR_STATE) return 2;
    fake_socket_status.initialized = 1U;
    if (http_init() != OK || http_init() != OK || http_get_status(&status) != OK ||
        !status.initialized || status.state != HTTP_STATE_IDLE ||
        http_validate_state() != OK) return 3;
    if (http_get_start(NULL) != ERR_NULL ||
        http_get_stream_start("http://10.0.2.2/", 1U, NULL, NULL) != ERR_NULL ||
        http_get_stream_start_ex("http://10.0.2.2/", 1U, NULL, NULL, NULL) != ERR_NULL ||
        http_get_stream_start("http://10.0.2.2/", 0U, body_sink, &body_length) != ERR_NULL ||
        http_get_start("ftp://10.0.2.2/") != ERR_INVALID ||
        http_get_start("http://0.0.0.0/") != ERR_INVALID ||
        http_get_start("http://10.0.2.2:0/") != ERR_INVALID ||
        http_get_start("http://-bad.example/") != ERR_INVALID ||
        http_get_start("http://10.0.2.2/a#b") != ERR_INVALID) return 4;
    kmemset(&options, 0, sizeof(options));
    options.accept = "text/\nplain";
    if (http_get_start_ex("http://10.0.2.2/", &options) != ERR_INVALID) return 5;
    options.accept = "*/*";
    options.api_version = "v1";
    options.follow_redirects = 1U;
    options.max_redirects = HTTP_MAX_REDIRECTS + 1U;
    if (http_get_start_ex("http://10.0.2.2/", &options) != ERR_INVALID) return 6;
    options.max_redirects = 1U;
    options.require_https = 1U;
    if (http_get_start_ex("http://10.0.2.2/", &options) != ERR_UNAVAILABLE) return 7;
    if (http_get_body(&body, &body_length) != OK || body_length != 0U ||
        http_reset() != OK || http_get_body(NULL, &body_length) != ERR_NULL) return 8;
    return 0;
}

static int start_plain(const char* url) {
    http_status_t status;

    if (http_get_start(url) != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_CONNECTING) return 0;
    if (http_maintain() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_SENDING) return 0;
    if (http_maintain() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_RECEIVING_HEADERS) return 0;
    return 1;
}

static int check_plain_responses(void) {
    http_status_t status;
    const uint8_t* body;
    uint32_t body_length;
    uint32_t sink_checksum = 0U;

    reset_fakes();
    if (!start_plain("http://10.0.2.2:8080/path?q=1") ||
        fake_transmitted_length == 0U) return 1;
    load_response("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello", 0U, 0U);
    if (drive_to_terminal() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_COMPLETE || status.status_code != 200U ||
        status.body_length != 5U || status.responses_rx != 1U ||
        http_get_body(&body, &body_length) != OK || body_length != 5U ||
        kstrcmp((const char*)body, "hello") != 0 ||
        !contains_text(fake_transmitted, fake_transmitted_length,
                       "GET /path?q=1 HTTP/1.1") ||
        !contains_text(fake_transmitted, fake_transmitted_length,
                       "Host: 10.0.2.2:8080") ||
        http_validate_state() != OK) return 2;
    if (http_get_start("http://10.0.2.2/") != OK) return 3;
    load_response("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc", 0U, 1U);
    if (drive_to_terminal() != OK || http_get_status(&status) != OK ||
        status.body_length != 3U || http_reset() != OK) return 4;
    if (!start_plain("http://10.0.2.2/stream") ||
        http_get_stream_start("http://10.0.2.2/stream", 3U,
                              body_sink, &sink_checksum) != ERR_STATE) return 5;
    if (http_reset() != OK || !start_plain("http://10.0.2.2/stream")) return 6;
    if (http_reset() != OK ||
        http_get_stream_start("http://10.0.2.2/stream", 3U,
                              body_sink, &sink_checksum) != OK) return 7;
    load_response("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc", 0U, 0U);
    if (drive_to_terminal() != OK || http_get_status(&status) != OK ||
        !status.streaming || sink_calls != 1U || sink_bytes != 3U ||
        sink_checksum != (uint32_t)('a' + 'b' + 'c') ||
        http_get_body(&body, &body_length) != ERR_UNAVAILABLE ||
        http_reset() != OK) return 8;
    if (!start_plain("http://10.0.2.2/nobody")) return 9;
    load_response("HTTP/1.1 204 No Content\r\n\r\n", 0U, 0U);
    if (drive_to_terminal() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_COMPLETE || status.body_length != 0U ||
        http_reset() != OK) return 10;
    if (!start_plain("http://10.0.2.2/eof")) return 11;
    load_response("HTTP/1.1 200 OK\r\n\r\nabc", 1U, 0U);
    if (drive_to_terminal() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_COMPLETE || status.eof_framed != 1U ||
        status.body_length != 3U || http_reset() != OK) return 12;
    return 0;
}

static int check_chunked(void) {
    http_status_t status;
    const uint8_t* body;
    uint32_t body_length;

    reset_fakes();
    if (!start_plain("http://10.0.2.2/chunk")) return 1;
    load_response("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                  "2;ok=yes\r\nhe\r\n3\r\nllo\r\n0\r\n\r\n", 0U, 1U);
    if (drive_to_terminal() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_COMPLETE || status.body_length != 5U ||
        status.has_content_length || http_get_body(&body, &body_length) != OK ||
        body_length != 5U || kstrcmp((const char*)body, "hello") != 0 ||
        http_validate_state() != OK || http_reset() != OK) return 2;
    if (!start_plain("http://10.0.2.2/chunk-bad")) return 3;
    load_response("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                  "Z\r\n", 0U, 0U);
    if (drive_to_terminal() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_FAILED || status.last_error != ERR_INVALID ||
        http_reset() != OK) return 4;
    if (!start_plain("http://10.0.2.2/chunk-overflow")) return 5;
    load_response("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                  "4\r\nhell\r\n", 1U, 0U);
    if (drive_to_terminal() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_FAILED || status.last_error != ERR_INVALID ||
        http_reset() != OK) return 6;
    return 0;
}

static int check_dns_and_failures(void) {
    http_status_t status;

    reset_fakes();
    if (http_get_start("http://example.com/name") != OK ||
        http_get_status(&status) != OK || status.state != HTTP_STATE_RESOLVING ||
        http_reset() != OK || fake_dns_status.state != DNS_STATE_IDLE) return 1;
    fake_dns_resolved = 0U;
    if (http_get_start("http://example.com/name") != OK) return 2;
    fake_dns_status.state = DNS_STATE_COMPLETE;
    fake_dns_status.result_ip = HTTP_TEST_IP;
    if (http_maintain() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_CONNECTING || http_reset() != OK) return 3;
    fake_dns_resolve_result = ERR_NOT_FOUND;
    if (http_get_start("http://example.com/missing") != ERR_NOT_FOUND ||
        http_get_status(&status) != OK || status.state != HTTP_STATE_FAILED ||
        status.last_error != ERR_NOT_FOUND || http_reset() != OK) return 4;
    fake_dns_resolve_result = OK;
    fake_socket_open_result = ERR_UNAVAILABLE;
    if (http_get_start("http://10.0.2.2/open") != ERR_UNAVAILABLE ||
        http_get_status(&status) != OK || status.state != HTTP_STATE_FAILED ||
        http_reset() != OK) return 5;
    fake_socket_open_result = OK;
    fake_socket_connect_result = ERR_TIMEOUT;
    if (http_get_start("http://10.0.2.2/connect") != ERR_TIMEOUT ||
        http_get_status(&status) != OK || status.state != HTTP_STATE_FAILED ||
        http_reset() != OK) {
        http_get_status(&status);
        printf("HTTP_CONNECT state=%u error=%d used=%u\\n",
               status.state, status.last_error, fake_socket_info.used);
        return 6;
    }
    fake_socket_connect_result = OK;
    if (http_get_start("http://10.0.2.2/socket-error") != OK) return 7;
    fake_socket_info.state = NET_SOCKET_STATE_ERROR;
    fake_socket_info.last_error = ERR_DISK;
    if (http_maintain() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_FAILED || status.last_error != ERR_DISK ||
        http_reset() != OK) return 8;
    if (http_get_start("http://10.0.2.2/send-error") != OK ||
        http_maintain() != OK) return 9;
    fake_socket_send_result = ERR_DISK;
    if (http_maintain() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_FAILED || status.last_error != ERR_DISK ||
        http_reset() != OK) return 10;
    fake_socket_send_result = OK;
    if (!start_plain("http://10.0.2.2/receive-error")) return 11;
    fake_socket_receive_result = ERR_TIMEOUT;
    if (http_maintain() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_FAILED || status.last_error != ERR_TIMEOUT ||
        http_reset() != OK) return 12;
    fake_socket_receive_result = OK;
    if (!start_plain("http://10.0.2.2/timeout")) return 13;
    fake_ticks = HTTP_TEST_TIMEOUT_TICKS + 100U;
    if (http_maintain() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_FAILED || status.last_error != ERR_TIMEOUT ||
        status.timeouts == 0U || http_reset() != OK) return 14;
    if (!start_plain("http://10.0.2.2/timer")) return 15;
    fake_frequency = 0U;
    if (http_maintain() != ERR_STATE || http_reset() != OK) return 16;
    fake_frequency = 1000U;
    if (!start_plain("http://10.0.2.2/stack")) return 17;
    fake_stack_result = ERR_OVERFLOW;
    if (http_maintain() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_FAILED || status.last_error != ERR_OVERFLOW ||
        status.overflows != 1U || http_reset() != OK) return 18;
    return 0;
}

static int check_https_and_redirects(void) {
    http_status_t status;
    http_request_options_t options;
    const uint8_t* body;
    uint32_t body_length;

    reset_fakes();
    if (http_get_start("https://10.0.2.2/secure") != ERR_UNAVAILABLE ||
        http_get_status(&status) != OK || status.state != HTTP_STATE_FAILED ||
        status.last_error != ERR_UNAVAILABLE || http_reset() != OK) return 1;
    fake_tls_capability = 1;
    if (http_get_start("https://10.0.2.2/secure") != OK ||
        http_maintain() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_TLS_HANDSHAKING || http_maintain() != OK ||
        http_get_status(&status) != OK || status.state != HTTP_STATE_SENDING ||
        http_maintain() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_RECEIVING_HEADERS) return 2;
    load_response("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok", 0U, 0U);
    if (drive_to_terminal() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_COMPLETE || !status.secure ||
        !status.tls_verified || http_get_body(&body, &body_length) != OK ||
        body_length != 2U || kstrcmp((const char*)body, "ok") != 0 ||
        http_validate_state() != OK || http_reset() != OK) return 3;
    fake_tls_maintain_result = ERR_UNAVAILABLE;
    if (http_get_start("https://10.0.2.2/handshake") != OK ||
        http_maintain() != OK || http_maintain() != OK ||
        http_get_status(&status) != OK || status.state != HTTP_STATE_FAILED ||
        status.last_error != ERR_UNAVAILABLE || http_reset() != OK) return 4;
    fake_tls_maintain_result = OK;
    kmemset(&options, 0, sizeof(options));
    options.follow_redirects = 1U;
    options.max_redirects = 1U;
    if (http_get_start_ex("http://10.0.2.2/redirect", &options) != OK ||
        http_maintain() != OK || http_maintain() != OK ||
        http_get_status(&status) != OK || status.state != HTTP_STATE_RECEIVING_HEADERS) return 5;
    load_response("HTTP/1.1 302 Found\r\nLocation: https://10.0.2.3/next\r\n\r\n", 0U, 0U);
    if (http_maintain() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_CONNECTING || status.redirect_count != 1U ||
        status.requests_started < 2U) return 6;
    load_response("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok", 0U, 0U);
    if (drive_to_terminal() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_COMPLETE || status.redirect_count != 1U ||
        http_reset() != OK) return 7;
    options.max_redirects = 1U;
    if (http_get_start_ex("http://10.0.2.2/redirect-bad", &options) != OK ||
        http_maintain() != OK || http_maintain() != OK) return 8;
    load_response("HTTP/1.1 302 Found\r\nLocation: http://10.0.2.3/nope\r\n\r\n", 0U, 0U);
    if (drive_to_terminal() != OK || http_get_status(&status) != OK ||
        status.state != HTTP_STATE_FAILED || status.last_error != ERR_INVALID ||
        status.redirect_rejected != 1U || http_reset() != OK) return 9;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    log_init();
    result = check_contracts();
    if (result == 0) result = check_plain_responses();
    if (result == 0) result = check_chunked();
    if (result == 0) result = check_dns_and_failures();
    if (result == 0) result = check_https_and_redirects();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
