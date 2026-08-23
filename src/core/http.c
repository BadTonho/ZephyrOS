#include "core/http.h"
#include "core/dns.h"
#include "core/errors.h"
#include "core/ipv4.h"
#include "core/log.h"
#include "core/net_socket.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/tls.h"
#include "core/tls_client.h"

#define HTTP_SCHEME "http://"
#define HTTP_SCHEME_LENGTH 7U
#define HTTPS_SCHEME "https://"
#define HTTPS_SCHEME_LENGTH 8U
#define HTTP_REDIRECT_URL_MAX_LENGTH 4095U
#define HTTP_REDIRECT_PATH_BUFFER_SIZE (HTTP_REDIRECT_URL_MAX_LENGTH + 1U)
#define HTTP_REQUEST_METADATA_CAPACITY 512U
#define HTTP_REQUEST_CAPACITY (HTTP_REDIRECT_PATH_BUFFER_SIZE + \
                               HTTP_REQUEST_METADATA_CAPACITY)
#define HTTP_READ_CHUNK 512U
#define HTTP_TIMEOUT_SECONDS 45U
#define HTTP_MAX_SAFE_TICKS 0x7FFFFFFFU
#define HTTP_MAX_BODY_LIMIT 0x7FFFFFFFU

typedef enum {
    HTTP_CHUNK_STATE_SIZE = 0,
    HTTP_CHUNK_STATE_EXTENSION,
    HTTP_CHUNK_STATE_SIZE_LF,
    HTTP_CHUNK_STATE_DATA,
    HTTP_CHUNK_STATE_DATA_CR,
    HTTP_CHUNK_STATE_DATA_LF,
    HTTP_CHUNK_STATE_TRAILER_CR,
    HTTP_CHUNK_STATE_TRAILER_LF
} http_chunk_state_t;

typedef struct {
    uint16_t status_code;
    uint32_t content_length;
    uint8_t has_content_length;
    uint8_t chunked;
    uint8_t eof_framed;
    uint8_t no_body;
    uint8_t has_location;
    char location[HTTP_REDIRECT_PATH_BUFFER_SIZE];
} http_header_result_t;

static http_status_t http_status;
static net_socket_handle_t http_socket;
static uint8_t http_header_buffer[HTTP_HEADER_CAPACITY];
static uint8_t http_body_buffer[HTTP_BODY_CAPACITY];
static uint8_t http_request_buffer[HTTP_REQUEST_CAPACITY];
static uint8_t http_read_buffer[HTTP_READ_CHUNK];
static char http_parse_host_buffer[HTTP_HOST_BUFFER_SIZE];
static char http_parse_path_buffer[HTTP_PATH_BUFFER_SIZE];
static char http_redirect_path_buffer[HTTP_REDIRECT_PATH_BUFFER_SIZE];
static char http_request_path[HTTP_REDIRECT_PATH_BUFFER_SIZE];
static http_header_result_t http_header_result;
static char http_accept_buffer[HTTP_HEADER_VALUE_BUFFER_SIZE];
static char http_api_version_buffer[64];
static uint16_t http_request_length;
static uint16_t http_request_offset;
static uint32_t http_started_tick;
static http_body_sink_t http_body_sink;
static void* http_body_sink_context;
static uint32_t http_body_limit;
static uint8_t http_streaming;
static uint8_t http_require_https;
static uint8_t http_follow_redirects;
static uint8_t http_max_redirects;
static uint8_t http_chunked;
static http_chunk_state_t http_chunk_state;
static uint32_t http_chunk_remaining;
static uint8_t http_chunk_size_seen;

static void http_complete(void);

static char http_ascii_lower(char value) {
    if (value >= 'A' && value <= 'Z') {
        return (char)(value + ('a' - 'A'));
    }
    return value;
}

static uint8_t http_text_prefix_equal(const char* text,
                                      const char* expected) {
    while (*expected) {
        if (http_ascii_lower(*text) !=
            http_ascii_lower(*expected)) return 0;
        text++;
        expected++;
    }
    return 1;
}

static int http_copy_text(char* destination, uint32_t capacity,
                          const char* source, uint32_t length) {
    if (!destination || !source || !capacity) {
        LOG_ERROR("NET", "Texto HTTP nulo");
        return ERR_NULL;
    }
    if (length + 1U > capacity) {
        LOG_ERROR("NET", "Texto HTTP excede limite");
        return ERR_OVERFLOW;
    }
    for (uint32_t index = 0; index < length; index++) {
        destination[index] = source[index];
    }
    destination[length] = '\0';
    return OK;
}

static int http_validate_header_value(const char* value) {
    if (!value) return ERR_NULL;
    for (uint32_t index = 0U; value[index]; index++) {
        uint8_t raw = (uint8_t)value[index];

        if (raw < 0x21U || raw > 0x7EU) return ERR_INVALID;
    }
    return OK;
}

static uint8_t http_host_is_numeric(const char* host) {
    uint8_t dot = 0;

    if (!host || !*host) return 0;
    while (*host) {
        if (*host == '.') dot = 1;
        else if (*host < '0' || *host > '9') return 0;
        host++;
    }
    return dot;
}

static int http_parse_ipv4(const char* text, uint32_t* out_ip) {
    uint32_t address = 0;

    if (!text || !out_ip) {
        LOG_ERROR("NET", "IPv4 HTTP nulo");
        return ERR_NULL;
    }
    for (uint32_t octet = 0; octet < 4U; octet++) {
        uint32_t value = 0;
        uint8_t digits = 0;

        while (*text >= '0' && *text <= '9') {
            value = value * 10U + (uint32_t)(*text - '0');
            if (value > 255U || digits >= 3U) return ERR_INVALID;
            digits++;
            text++;
        }
        if (!digits) return ERR_INVALID;
        address = (address << 8U) | value;
        if (octet < 3U) {
            if (*text != '.') return ERR_INVALID;
            text++;
        }
    }
    if (*text || !ipv4_address_is_unicast(address)) return ERR_INVALID;
    *out_ip = address;
    return OK;
}

static int http_parse_port(const char* start, uint32_t length,
                           uint16_t* out_port) {
    uint32_t value = 0;

    if (!start || !out_port) {
        LOG_ERROR("NET", "Porta HTTP nula");
        return ERR_NULL;
    }
    if (!length || length > 5U) return ERR_INVALID;
    for (uint32_t index = 0; index < length; index++) {
        if (start[index] < '0' || start[index] > '9') {
            return ERR_INVALID;
        }
        value = value * 10U + (uint32_t)(start[index] - '0');
        if (value > 65535U) return ERR_INVALID;
    }
    if (!value) return ERR_INVALID;
    *out_port = (uint16_t)value;
    return OK;
}

static int http_validate_host(char* host) {
    uint32_t label_length = 0;

    if (!host) {
        LOG_ERROR("NET", "Host HTTP nulo");
        return ERR_NULL;
    }
    if (http_host_is_numeric(host)) {
        uint32_t address;

        return http_parse_ipv4(host, &address);
    }
    for (uint32_t index = 0; host[index]; index++) {
        char value = http_ascii_lower(host[index]);

        if (value == '.') {
            if (!label_length || host[index - 1U] == '-') {
                return ERR_INVALID;
            }
            label_length = 0;
        } else {
            if (!((value >= 'a' && value <= 'z') ||
                  (value >= '0' && value <= '9') || value == '-') ||
                (!label_length && value == '-')) return ERR_INVALID;
            label_length++;
            if (label_length > 63U) return ERR_INVALID;
        }
        host[index] = value;
    }
    return label_length && host[kstrlen(host) - 1U] != '-' ?
           OK : ERR_INVALID;
}

static int http_parse_url_ex(const char* url, char* host, char* path,
                             uint16_t* out_port, uint32_t* out_ip,
                             uint8_t* out_numeric, uint8_t* out_secure,
                             uint32_t url_limit, uint32_t path_capacity) {
    const char* authority;
    const char* cursor;
    const char* colon = NULL;
    uint32_t authority_length;
    uint32_t host_length;
    uint32_t path_length;

    if (!url || !host || !path || !out_port ||
        !out_ip || !out_numeric || !out_secure) {
        LOG_ERROR("NET", "Argumento nulo ao interpretar URL");
        return ERR_NULL;
    }
    if (kstrlen(url) > url_limit || path_capacity < 2U) return ERR_INVALID;
    if (http_text_prefix_equal(url, HTTPS_SCHEME)) {
        *out_secure = 1U;
        authority = url + HTTPS_SCHEME_LENGTH;
    } else if (http_text_prefix_equal(url, HTTP_SCHEME)) {
        *out_secure = 0U;
        authority = url + HTTP_SCHEME_LENGTH;
    } else {
        return ERR_INVALID;
    }
    cursor = authority;
    while (*cursor && *cursor != '/') {
        if (*cursor == ':' && !colon) colon = cursor;
        else if (*cursor == ':' || *cursor == '@' ||
                 *cursor == '#' || (uint8_t)*cursor <= 0x20U) {
            return ERR_INVALID;
        }
        cursor++;
    }
    authority_length = (uint32_t)(cursor - authority);
    host_length = colon ? (uint32_t)(colon - authority) :
                          authority_length;
    if (!host_length || host_length > HTTP_HOST_MAX_LENGTH ||
        http_copy_text(host, HTTP_HOST_BUFFER_SIZE,
                       authority, host_length) != OK ||
        http_validate_host(host) != OK) return ERR_INVALID;
    *out_port = *out_secure ? HTTPS_DEFAULT_PORT : HTTP_DEFAULT_PORT;
    if (colon && http_parse_port(
            colon + 1, (uint32_t)(cursor - colon - 1),
            out_port) != OK) return ERR_INVALID;
    path_length = *cursor ? kstrlen(cursor) : 1U;
    if (path_length + 1U > path_capacity) return ERR_OVERFLOW;
    if (*cursor) {
        for (uint32_t index = 0; index < path_length; index++) {
            if ((uint8_t)cursor[index] <= 0x20U ||
                (uint8_t)cursor[index] > 0x7EU ||
                cursor[index] == '#') return ERR_INVALID;
        }
        if (http_copy_text(path, path_capacity,
                           cursor, path_length) != OK) return ERR_OVERFLOW;
    } else {
        path[0] = '/';
        path[1] = '\0';
    }
    *out_numeric = http_host_is_numeric(host);
    *out_ip = 0;
    if (*out_numeric && http_parse_ipv4(host, out_ip) != OK) {
        return ERR_INVALID;
    }
    return OK;
}

static int http_parse_url(const char* url, char* host, char* path,
                          uint16_t* out_port, uint32_t* out_ip,
                          uint8_t* out_numeric, uint8_t* out_secure) {
    return http_parse_url_ex(url, host, path, out_port, out_ip,
                             out_numeric, out_secure,
                             HTTP_URL_MAX_LENGTH, HTTP_PATH_BUFFER_SIZE);
}

static int http_append_bytes(const char* text, uint16_t length) {
    if (!text) {
        LOG_ERROR("NET", "Texto nulo ao montar request HTTP");
        return ERR_NULL;
    }
    if ((uint32_t)http_request_length + length >=
        HTTP_REQUEST_CAPACITY) {
        LOG_ERROR("NET", "Request HTTP excede buffer");
        return ERR_OVERFLOW;
    }
    kmemcpy(http_request_buffer + http_request_length, text, length);
    http_request_length = (uint16_t)(http_request_length + length);
    return OK;
}

static int http_append_text(const char* text) {
    return http_append_bytes(text, (uint16_t)kstrlen(text));
}

static int http_append_port(uint16_t port) {
    char digits[6];
    uint8_t length = 0;
    uint16_t value = port;

    do {
        digits[length++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value);
    for (uint8_t index = 0; index < length / 2U; index++) {
        char swap = digits[index];

        digits[index] = digits[length - index - 1U];
        digits[length - index - 1U] = swap;
    }
    return http_append_bytes(digits, length);
}

static int http_build_request(void) {
    int result;

    http_request_length = 0;
    http_request_offset = 0;
    result = http_append_text("GET ");
    if (result == OK) result = http_append_text(http_request_path);
    if (result == OK) result = http_append_text(" HTTP/1.1\r\nHost: ");
    if (result == OK) result = http_append_text(http_status.host);
    if (result == OK &&
        ((!http_status.secure && http_status.port != HTTP_DEFAULT_PORT) ||
         (http_status.secure && http_status.port != HTTPS_DEFAULT_PORT))) {
        result = http_append_text(":");
        if (result == OK) result = http_append_port(http_status.port);
    }
    if (result == OK) {
        result = http_append_text("\r\nUser-Agent: ZephyrOS/0.1\r\nAccept: ");
    }
    if (result == OK) result = http_append_text(http_accept_buffer);
    if (result == OK && http_api_version_buffer[0]) {
        result = http_append_text("\r\nX-GitHub-Api-Version: ");
        if (result == OK) result = http_append_text(http_api_version_buffer);
    }
    if (result == OK) {
        result = http_append_text(
            "\r\nAccept-Encoding: identity\r\n"
            "Connection: close\r\n\r\n");
    }
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao montar request HTTP");
        return result;
    }
    return OK;
}

static uint8_t http_range_equal(const uint8_t* start,
                                uint32_t length,
                                const char* expected) {
    uint32_t expected_length = kstrlen(expected);

    if (length != expected_length) return 0;
    for (uint32_t index = 0; index < length; index++) {
        if (http_ascii_lower((char)start[index]) !=
            http_ascii_lower(expected[index])) return 0;
    }
    return 1;
}

static uint8_t http_header_value_equal(const uint8_t* data,
                                       uint32_t length,
                                       const char* expected) {
    uint32_t expected_length;

    if (!data || !expected) return 0U;
    while (length && (*data == ' ' || *data == '\t')) {
        data++;
        length--;
    }
    while (length &&
           (data[length - 1U] == ' ' || data[length - 1U] == '\t')) {
        length--;
    }
    expected_length = kstrlen(expected);
    if (length != expected_length) return 0U;
    for (uint32_t index = 0U; index < length; index++) {
        if (http_ascii_lower((char)data[index]) !=
            http_ascii_lower(expected[index])) return 0U;
    }
    return 1U;
}

static uint8_t http_is_token_character(uint8_t value) {
    if ((value >= '0' && value <= '9') ||
        (value >= 'A' && value <= 'Z') ||
        (value >= 'a' && value <= 'z')) return 1;
    return value == '!' || value == '#' || value == '$' ||
           value == '%' || value == '&' || value == '\'' ||
           value == '*' || value == '+' || value == '-' ||
           value == '.' || value == '^' || value == '_' ||
           value == '`' || value == '|' || value == '~';
}

static int http_parse_decimal(const uint8_t* start, uint32_t length,
                              uint32_t* out_value) {
    uint32_t value = 0;

    if (!start || !out_value) {
        LOG_ERROR("NET", "Numero HTTP nulo");
        return ERR_NULL;
    }
    while (length && (*start == ' ' || *start == '\t')) {
        start++;
        length--;
    }
    while (length && (start[length - 1U] == ' ' ||
                      start[length - 1U] == '\t')) length--;
    if (!length) return ERR_INVALID;
    for (uint32_t index = 0; index < length; index++) {
        if (start[index] < '0' || start[index] > '9') {
            return ERR_INVALID;
        }
        {
            uint32_t digit = (uint32_t)(start[index] - '0');

            if (value > (0xFFFFFFFFU - digit) / 10U) {
                return ERR_OVERFLOW;
            }
            value = value * 10U + digit;
        }
    }
    *out_value = value;
    return OK;
}

static int http_find_crlf(const uint8_t* data, uint32_t length,
                          uint32_t start, uint32_t* out_end) {
    if (!data || !out_end) {
        LOG_ERROR("NET", "Linha HTTP nula");
        return ERR_NULL;
    }
    for (uint32_t index = start; index + 1U < length; index++) {
        if (data[index] == '\r' && data[index + 1U] == '\n') {
            *out_end = index;
            return OK;
        }
    }
    return ERR_NOT_FOUND;
}

static int http_parse_status_line(const uint8_t* data,
                                  uint32_t length,
                                  uint16_t* out_status) {
    uint32_t offset;

    if (!data || !out_status) {
        LOG_ERROR("NET", "Status HTTP nulo");
        return ERR_NULL;
    }
    if (length < 12U ||
        !(http_range_equal(data, 8U, "HTTP/1.0") ||
          http_range_equal(data, 8U, "HTTP/1.1")) ||
        data[8] != ' ' || data[9] < '1' || data[9] > '5' ||
        data[10] < '0' || data[10] > '9' ||
        data[11] < '0' || data[11] > '9') return ERR_INVALID;
    offset = 12U;
    if (offset < length && data[offset] != ' ') return ERR_INVALID;
    for (uint32_t index = offset; index < length; index++) {
        if ((data[index] < 0x20U && data[index] != '\t') ||
            data[index] == 0x7FU) return ERR_INVALID;
    }
    *out_status = (uint16_t)(
        (data[9] - '0') * 100U +
        (data[10] - '0') * 10U + (data[11] - '0'));
    return OK;
}

static int http_parse_header_line(const uint8_t* data,
                                  uint32_t length,
                                  http_header_result_t* result) {
    uint32_t colon = 0;
    uint32_t value;
    uint32_t value_start;
    uint32_t value_length;

    if (!data || !result) {
        LOG_ERROR("NET", "Linha de header HTTP nula");
        return ERR_NULL;
    }
    for (colon = 0; colon < length && data[colon] != ':'; colon++) {
        if (!http_is_token_character(data[colon])) return ERR_INVALID;
    }
    if (!colon || colon >= length) return ERR_INVALID;
    for (uint32_t index = colon + 1U; index < length; index++) {
        if ((data[index] < 0x20U && data[index] != '\t') ||
            data[index] == 0x7FU) return ERR_INVALID;
    }
    if (http_range_equal(data, colon, "Transfer-Encoding")) {
        if (result->chunked || !http_header_value_equal(
                data + colon + 1U, length - colon - 1U, "chunked")) {
            return ERR_UNAVAILABLE;
        }
        result->chunked = 1U;
        return OK;
    }
    if (http_range_equal(data, colon, "Content-Encoding")) {
        return http_header_value_equal(
            data + colon + 1U, length - colon - 1U, "identity") ?
            OK : ERR_UNAVAILABLE;
    }
    if (http_range_equal(data, colon, "Location")) {
        if (result->has_location) return ERR_INVALID;
        value_start = colon + 1U;
        while (value_start < length &&
               (data[value_start] == ' ' || data[value_start] == '\t')) {
            value_start++;
        }
        value_length = length - value_start;
        while (value_length &&
               (data[value_start + value_length - 1U] == ' ' ||
                data[value_start + value_length - 1U] == '\t')) {
            value_length--;
        }
        if (!value_length || value_length > HTTP_REDIRECT_URL_MAX_LENGTH ||
            http_copy_text(result->location, sizeof(result->location),
                           (const char*)data + value_start,
                           value_length) != OK) {
            return ERR_INVALID;
        }
        result->has_location = 1U;
        return OK;
    }
    if (!http_range_equal(data, colon, "Content-Length")) return OK;
    if (http_parse_decimal(data + colon + 1U,
                           length - colon - 1U, &value) != OK) {
        return ERR_INVALID;
    }
    if (result->has_content_length &&
        result->content_length != value) return ERR_INVALID;
    result->has_content_length = 1;
    result->content_length = value;
    return OK;
}

static int http_parse_headers(const uint8_t* data, uint32_t length,
                              http_header_result_t* result) {
    uint32_t line_start = 0;
    uint32_t line_end;
    int parse_result;

    if (!data || !result) {
        LOG_ERROR("NET", "Headers HTTP nulos");
        return ERR_NULL;
    }
    kmemset(result, 0, sizeof(*result));
    if (http_find_crlf(data, length, 0U, &line_end) != OK ||
        http_parse_status_line(data, line_end,
                               &result->status_code) != OK ||
        result->status_code < HTTP_STATUS_MINIMUM ||
        result->status_code > HTTP_STATUS_MAXIMUM) return ERR_INVALID;
    line_start = line_end + 2U;
    while (line_start + 1U < length) {
        if (data[line_start] == '\r' &&
            data[line_start + 1U] == '\n') break;
        if (http_find_crlf(data, length, line_start, &line_end) != OK) {
            return ERR_INVALID;
        }
        parse_result = http_parse_header_line(
            data + line_start, line_end - line_start, result);
        if (parse_result != OK) return parse_result;
        line_start = line_end + 2U;
    }
    if (line_start + 2U != length) return ERR_INVALID;
    if (result->chunked && result->has_content_length) return ERR_INVALID;
    result->no_body = result->status_code == 204U ||
                      result->status_code == 304U;
    if (result->no_body) {
        if (result->chunked) return ERR_INVALID;
        result->has_content_length = 1;
        result->content_length = 0;
    } else if (!result->has_content_length && !result->chunked) {
        result->eof_framed = 1;
    }
    return OK;
}

static uint8_t http_socket_exists(void) {
    for (uint32_t index = 0; index < NET_SOCKET_CAPACITY; index++) {
        net_socket_info_t info;

        if (net_socket_get_info(index, &info) == OK &&
            info.used && info.handle == http_socket) return 1;
    }
    return 0;
}

static void http_release_socket(void) {
    tls_client_status_t tls;

    if (http_socket && http_socket_exists()) {
        net_socket_abort(http_socket);
    }
    if (tls_client_get_status(&tls) == OK && tls.active) tls_client_close();
    http_socket = 0;
}

static void http_clear_session(http_state_t state) {
    http_status.state = state;
    http_status.url[0] = '\0';
    http_status.host[0] = '\0';
    http_status.path[0] = '\0';
    http_status.port = 0;
    http_status.resolved_ip = 0;
    http_status.status_code = 0;
    http_status.headers_length = 0;
    http_status.body_length = 0;
    http_status.body_limit = 0;
    http_status.content_length = 0;
    http_status.has_content_length = 0;
    http_status.eof_framed = 0;
    http_status.streaming = 0;
    http_status.secure = 0;
    http_status.tls_verified = 0;
    http_status.follow_redirects = http_follow_redirects;
    http_status.redirect_rejected = 0;
    http_status.redirect_count = 0;
    http_status.max_redirects = http_max_redirects;
    http_status.tls_reason = TLS_REASON_NONE;
    http_status.tls_error = 0;
    http_status.last_error = OK;
    http_request_length = 0;
    http_request_offset = 0;
    http_started_tick = 0;
    http_body_sink = 0;
    http_body_sink_context = 0;
    http_body_limit = 0;
    http_streaming = 0;
    http_request_path[0] = '\0';
    http_chunked = 0U;
    http_chunk_state = HTTP_CHUNK_STATE_SIZE;
    http_chunk_remaining = 0U;
    http_chunk_size_seen = 0U;
    kmemset(http_header_buffer, 0, sizeof(http_header_buffer));
    kmemset(http_body_buffer, 0, sizeof(http_body_buffer));
}

static int http_reject_unavailable_tls(void) {
    tls_status_t tls;

    http_status.secure = 1U;
    http_status.tls_verified = 0U;
    http_status.tls_reason = TLS_REASON_NONE;
    http_status.tls_error = 0U;
    if (tls_get_status(&tls) == OK) {
        http_status.tls_reason = tls.last_reason;
        http_status.tls_error = (uint16_t)tls.last_error;
    } else {
        LOG_ERROR("NET", "Nao foi possivel consultar estado TLS");
    }
    http_status.last_error = ERR_UNAVAILABLE;
    LOG_ERROR("NET", "HTTPS recusado: capacidade TLS indisponivel");
    return ERR_UNAVAILABLE;
}

static void http_cancel_resolution(void) {
    dns_status_t dns;

    if (http_status.state != HTTP_STATE_RESOLVING ||
        dns_get_status(&dns) != OK ||
        (dns.state != DNS_STATE_RESOLVING_ARP &&
         dns.state != DNS_STATE_WAITING_REPLY) ||
        kstrcmp(dns.query_name, http_status.host) != 0) return;
    if (dns_reset() != OK) {
        LOG_WARN("NET", "Consulta DNS do HTTP nao foi cancelada");
    }
}

static void http_fail(int error) {
    tls_client_status_t tls;

    if (error == ERR_TIMEOUT) http_status.timeouts++;
    else if (error == ERR_OVERFLOW) http_status.overflows++;
    else http_status.parse_errors++;
    if (http_status.secure && tls_client_get_status(&tls) == OK) {
        http_status.tls_reason = tls.reason;
        http_status.tls_error = tls.bearssl_error;
    }
    http_cancel_resolution();
    http_release_socket();
    http_status.state = HTTP_STATE_FAILED;
    http_status.last_error = error;
    http_status.event_generation++;
}

static int http_start_socket(uint32_t address) {
    int result;

    result = net_socket_open(NET_SOCKET_TYPE_STREAM, &http_socket);
    if (result == OK) {
        result = net_socket_connect(
            http_socket, address, http_status.port);
    }
    if (result != OK) {
        http_release_socket();
        LOG_ERROR("NET", "Falha ao abrir socket HTTP");
        return result;
    }
    http_status.resolved_ip = address;
    http_status.state = HTTP_STATE_CONNECTING;
    return OK;
}

static uint8_t http_headers_complete(void) {
    uint16_t length = http_status.headers_length;

    return length >= 4U &&
           http_header_buffer[length - 4U] == '\r' &&
           http_header_buffer[length - 3U] == '\n' &&
           http_header_buffer[length - 2U] == '\r' &&
           http_header_buffer[length - 1U] == '\n';
}

static uint8_t http_is_redirect_status(uint16_t status_code) {
    return (uint8_t)(status_code == 301U || status_code == 302U ||
                     status_code == 303U || status_code == 307U ||
                     status_code == 308U);
}

static int http_follow_redirect(const char* location) {
    uint32_t address = 0;
    uint16_t port = 0;
    uint8_t numeric = 0;
    uint8_t secure = 0;
    uint8_t next_redirect;
    uint8_t was_streaming = http_streaming;
    http_body_sink_t sink = http_body_sink;
    void* sink_context = http_body_sink_context;
    uint32_t body_limit = http_body_limit;
    int result;

    if (!location) return ERR_NULL;
    if (http_status.redirect_count >= http_max_redirects) {
        http_status.redirect_rejected = 1U;
        LOG_WARN("NET", "Limite de redirects HTTPS excedido");
        return ERR_OVERFLOW;
    }
    result = http_parse_url_ex(
        location, http_parse_host_buffer, http_redirect_path_buffer,
        &port, &address, &numeric, &secure, HTTP_REDIRECT_URL_MAX_LENGTH,
        sizeof(http_redirect_path_buffer));
    if (result != OK || !secure) {
        http_status.redirect_rejected = 1U;
        LOG_ERROR("NET", "Redirect HTTP nao e um destino HTTPS absoluto");
        return ERR_INVALID;
    }
    if (!tls_capability_available()) {
        return http_reject_unavailable_tls();
    }
    next_redirect = (uint8_t)(http_status.redirect_count + 1U);
    http_release_socket();
    http_clear_session(HTTP_STATE_RESOLVING);
    http_body_sink = sink;
    http_body_sink_context = sink_context;
    http_body_limit = body_limit;
    http_streaming = was_streaming;
    http_status.body_limit = body_limit;
    http_status.streaming = was_streaming;
    http_status.redirect_count = next_redirect;
    http_status.follow_redirects = http_follow_redirects;
    http_status.max_redirects = http_max_redirects;
    http_status.secure = 1U;
    http_status.requests_started++;
    if (kstrlen(location) >= sizeof(http_status.url)) {
        http_status.url[0] = '\0';
    } else {
        http_copy_text(http_status.url, sizeof(http_status.url),
                       location, kstrlen(location));
    }
    if (kstrlen(http_redirect_path_buffer) >= sizeof(http_status.path)) {
        http_status.path[0] = '\0';
    } else {
        http_copy_text(http_status.path, sizeof(http_status.path),
                       http_redirect_path_buffer,
                       kstrlen(http_redirect_path_buffer));
    }
    http_copy_text(http_status.host, sizeof(http_status.host),
                   http_parse_host_buffer, kstrlen(http_parse_host_buffer));
    if (http_copy_text(http_request_path, sizeof(http_request_path),
                       http_redirect_path_buffer,
                       kstrlen(http_redirect_path_buffer)) != OK) {
        LOG_ERROR("NET", "Caminho de redirect excede o buffer interno");
        return ERR_OVERFLOW;
    }
    http_status.port = port;
    http_started_tick = timer_get_ticks();
    if (numeric) return http_start_socket(address);
    result = dns_resolve(http_parse_host_buffer, &address, &numeric);
    if (result != OK) return result;
    if (numeric) return http_start_socket(address);
    return OK;
}

static int http_finish_headers(void) {
    http_header_result_t* result = &http_header_result;
    int parse_result = http_parse_headers(
        http_header_buffer, http_status.headers_length, result);

    if (parse_result != OK) {
        if (parse_result == ERR_UNAVAILABLE) {
            LOG_WARN("NET", "Encoding HTTP nao suportado");
        } else {
            LOG_WARN("NET", "Resposta HTTP possui headers invalidos");
        }
        return parse_result;
    }
    http_status.status_code = result->status_code;
    http_status.content_length = result->content_length;
    http_status.has_content_length = result->has_content_length;
    http_status.eof_framed = result->eof_framed;
    http_chunked = result->chunked;
    if (http_is_redirect_status(result->status_code) &&
        http_follow_redirects) {
        if (!result->has_location) {
            http_status.redirect_rejected = 1U;
            LOG_ERROR("NET", "Resposta de redirect sem Location");
            return ERR_INVALID;
        }
        return http_follow_redirect(result->location);
    }
    if (result->has_content_length &&
        result->content_length > http_body_limit) {
        LOG_WARN("NET", "Corpo HTTP excede o limite da sessao");
        return ERR_OVERFLOW;
    }
    http_status.state = HTTP_STATE_RECEIVING_BODY;
    return OK;
}

static void http_complete(void) {
    if (http_socket && http_socket_exists()) {
        net_socket_close(http_socket);
    }
    tls_client_close();
    http_status.state = HTTP_STATE_COMPLETE;
    http_status.responses_rx++;
    http_status.last_error = OK;
    http_status.event_generation++;
}

static int http_consume_body(const uint8_t* data, uint16_t length) {
    uint32_t remaining = http_body_limit -
                         http_status.body_length;
    int result;

    if (!data && length != 0U) {
        LOG_ERROR("NET", "Corpo HTTP nulo");
        return ERR_NULL;
    }
    if (length > remaining) return ERR_OVERFLOW;
    if (http_status.has_content_length &&
        http_status.body_length + length >
            http_status.content_length) return ERR_INVALID;
    if (length) {
        if (http_streaming) {
            result = http_body_sink(
                data, (uint32_t)length, http_body_sink_context);
            if (result != OK) {
                LOG_ERROR("NET", "Consumidor do corpo HTTP recusou dados");
                return result;
            }
        } else {
            kmemcpy(http_body_buffer + http_status.body_length,
                    data, length);
        }
        http_status.body_length += length;
    }
    if (http_status.has_content_length &&
        http_status.body_length == http_status.content_length) {
        http_complete();
    }
    return OK;
}

static int http_chunk_error(int error, const char* message) {
    LOG_ERROR("NET", message);
    return error;
}

static int http_consume_chunked(const uint8_t* data, uint16_t length) {
    uint16_t offset = 0U;

    if (!data && length) {
        return http_chunk_error(ERR_NULL, "Corpo HTTP chunked nulo");
    }
    while (offset < length && http_status.state != HTTP_STATE_COMPLETE) {
        uint8_t value = data[offset];

        if (http_chunk_state == HTTP_CHUNK_STATE_SIZE) {
            uint8_t digit;

            if (value >= '0' && value <= '9') digit = (uint8_t)(value - '0');
            else if (value >= 'a' && value <= 'f') digit = (uint8_t)(value - 'a' + 10U);
            else if (value >= 'A' && value <= 'F') digit = (uint8_t)(value - 'A' + 10U);
            else digit = 16U;
            if (digit < 16U) {
                if (http_chunk_remaining >
                    (HTTP_MAX_BODY_LIMIT - digit) / 16U) {
                    return http_chunk_error(
                        ERR_OVERFLOW, "Tamanho de chunk HTTP excede limite");
                }
                http_chunk_remaining = http_chunk_remaining * 16U + digit;
                http_chunk_size_seen = 1U;
                offset++;
            } else if (value == ';' && http_chunk_size_seen) {
                http_chunk_state = HTTP_CHUNK_STATE_EXTENSION;
                offset++;
            } else if (value == '\r' && http_chunk_size_seen) {
                http_chunk_state = HTTP_CHUNK_STATE_SIZE_LF;
                offset++;
            } else {
                return http_chunk_error(
                    ERR_INVALID, "Tamanho de chunk HTTP invalido");
            }
        } else if (http_chunk_state == HTTP_CHUNK_STATE_EXTENSION) {
            if (value == '\r') {
                http_chunk_state = HTTP_CHUNK_STATE_SIZE_LF;
                offset++;
            } else if (value < 0x20U || value > 0x7EU) {
                return http_chunk_error(
                    ERR_INVALID, "Extensao de chunk HTTP invalida");
            } else {
                offset++;
            }
        } else if (http_chunk_state == HTTP_CHUNK_STATE_SIZE_LF) {
            if (value != '\n') {
                return http_chunk_error(
                    ERR_INVALID, "Termino de chunk HTTP invalido");
            }
            offset++;
            if (http_chunk_remaining >
                http_body_limit - http_status.body_length) {
                return http_chunk_error(
                    ERR_OVERFLOW, "Corpo HTTP chunked excede limite");
            }
            http_chunk_state = http_chunk_remaining ? HTTP_CHUNK_STATE_DATA :
                                                     HTTP_CHUNK_STATE_TRAILER_CR;
        } else if (http_chunk_state == HTTP_CHUNK_STATE_DATA) {
            uint16_t available = (uint16_t)(length - offset);
            uint16_t take = http_chunk_remaining < available ?
                            (uint16_t)http_chunk_remaining : available;
            int result;

            if (!take) {
                http_chunk_state = HTTP_CHUNK_STATE_DATA_CR;
                continue;
            }
            result = http_consume_body(data + offset, take);
            if (result != OK) return result;
            offset = (uint16_t)(offset + take);
            http_chunk_remaining -= take;
            if (!http_chunk_remaining) {
                http_chunk_state = HTTP_CHUNK_STATE_DATA_CR;
            }
        } else if (http_chunk_state == HTTP_CHUNK_STATE_DATA_CR) {
            if (value != '\r') {
                return http_chunk_error(
                    ERR_INVALID, "Separador de chunk HTTP invalido");
            }
            http_chunk_state = HTTP_CHUNK_STATE_DATA_LF;
            offset++;
        } else if (http_chunk_state == HTTP_CHUNK_STATE_DATA_LF) {
            if (value != '\n') {
                return http_chunk_error(
                    ERR_INVALID, "Separador de chunk HTTP invalido");
            }
            http_chunk_remaining = 0U;
            http_chunk_size_seen = 0U;
            http_chunk_state = HTTP_CHUNK_STATE_SIZE;
            offset++;
        } else if (http_chunk_state == HTTP_CHUNK_STATE_TRAILER_CR) {
            if (value != '\r') {
                return http_chunk_error(
                    ERR_UNAVAILABLE, "Trailers HTTP nao suportados");
            }
            http_chunk_state = HTTP_CHUNK_STATE_TRAILER_LF;
            offset++;
        } else if (http_chunk_state == HTTP_CHUNK_STATE_TRAILER_LF) {
            if (value != '\n') {
                return http_chunk_error(
                    ERR_INVALID, "Termino de corpo HTTP invalido");
            }
            offset++;
            http_complete();
        } else {
            return http_chunk_error(ERR_STATE, "Estado de chunk HTTP invalido");
        }
    }
    return OK;
}

static int http_consume_bytes(const uint8_t* data, uint16_t length) {
    uint16_t offset = 0;

    if (!data && length != 0U) {
        LOG_ERROR("NET", "Dados HTTP nulos");
        return ERR_NULL;
    }
    while (offset < length &&
           http_status.state != HTTP_STATE_COMPLETE) {
        if (http_status.state == HTTP_STATE_RECEIVING_HEADERS) {
            if (http_status.headers_length >= HTTP_HEADER_CAPACITY) {
                LOG_ERROR("NET", "Headers HTTP excederam limite");
                return ERR_OVERFLOW;
            }
            http_header_buffer[http_status.headers_length++] =
                data[offset++];
            if (http_headers_complete()) {
                int result = http_finish_headers();

                if (result != OK) return result;
            }
        } else if (http_status.state ==
                   HTTP_STATE_RECEIVING_BODY) {
            if (http_chunked) {
                return http_consume_chunked(data + offset,
                                            (uint16_t)(length - offset));
            }
            return http_consume_body(data + offset,
                                     length - offset);
        } else {
            return ERR_STATE;
        }
    }
    if (http_status.state == HTTP_STATE_RECEIVING_BODY &&
        http_status.has_content_length &&
        http_status.body_length == http_status.content_length) {
        http_complete();
    }
    return OK;
}

int http_init(void) {
    net_socket_status_t sockets;

    LOG_INFO("NET", "Inicializando cliente HTTP");
    if (http_status.initialized) {
        LOG_WARN("NET", "Cliente HTTP ja estava inicializado");
        LOG_INFO("NET", "Cliente HTTP inicializado com sucesso");
        return OK;
    }
    if (net_socket_get_status(&sockets) != OK ||
        !sockets.initialized) {
        LOG_ERROR("NET", "Sockets indisponiveis para HTTP");
        return ERR_STATE;
    }
    kmemset(&http_status, 0, sizeof(http_status));
    http_socket = 0;
    http_status.initialized = 1;
    http_status.state = HTTP_STATE_IDLE;
    http_status.last_error = OK;
    LOG_INFO("NET", "Cliente HTTP inicializado com sucesso");
    return OK;
}

static int http_prepare_options(const http_request_options_t* options) {
    const char* accept = options && options->accept ? options->accept : "*/*";
    const char* api_version = options && options->api_version ?
                              options->api_version : "";
    uint8_t max_redirects = options ? options->max_redirects : 0U;

    if (kstrlen(accept) > HTTP_HEADER_VALUE_MAX_LENGTH ||
        kstrlen(api_version) >= sizeof(http_api_version_buffer)) {
        LOG_ERROR("NET", "Header HTTP excede limite configuravel");
        return ERR_OVERFLOW;
    }
    if ((accept[0] && http_validate_header_value(accept) != OK) ||
        (api_version[0] && http_validate_header_value(api_version) != OK)) {
        LOG_ERROR("NET", "Header HTTP possui caracteres invalidos");
        return ERR_INVALID;
    }
    if (max_redirects > HTTP_MAX_REDIRECTS) {
        LOG_ERROR("NET", "Quantidade de redirects HTTP excede politica");
        return ERR_INVALID;
    }
    http_require_https = options ? options->require_https : 0U;
    http_follow_redirects = options ? options->follow_redirects : 0U;
    http_max_redirects = http_follow_redirects ?
                         (max_redirects ? max_redirects : HTTP_MAX_REDIRECTS) :
                         0U;
    if (http_copy_text(http_accept_buffer, sizeof(http_accept_buffer),
                       accept, kstrlen(accept)) != OK ||
        http_copy_text(http_api_version_buffer,
                       sizeof(http_api_version_buffer), api_version,
                       kstrlen(api_version)) != OK) {
        LOG_ERROR("NET", "Falha ao copiar opcoes HTTP");
        return ERR_OVERFLOW;
    }
    return OK;
}

static int http_get_start_internal(const char* url, uint32_t body_limit,
                                   http_body_sink_t sink, void* context,
                                   const http_request_options_t* options) {
    uint32_t address = 0;
    uint16_t port = 0;
    uint8_t numeric = 0;
    uint8_t secure = 0;
    uint8_t resolved = 0;
    int result;

    if (!url || body_limit == 0U || (sink == 0 && context != 0)) {
        LOG_ERROR("NET", "URL nula no GET HTTP");
        return ERR_NULL;
    }
    if (body_limit > HTTP_MAX_BODY_LIMIT) {
        LOG_ERROR("NET", "Limite de corpo HTTP invalido");
        return ERR_OVERFLOW;
    }
    if (!http_status.initialized) {
        LOG_ERROR("NET", "GET HTTP antes da inicializacao");
        return ERR_STATE;
    }
    if (http_status.state != HTTP_STATE_IDLE &&
        http_status.state != HTTP_STATE_COMPLETE &&
        http_status.state != HTTP_STATE_FAILED) {
        LOG_ERROR("NET", "Outra requisicao HTTP esta ativa");
        return ERR_STATE;
    }
    result = http_prepare_options(options);
    if (result != OK) return result;
    result = http_parse_url(
        url, http_parse_host_buffer, http_parse_path_buffer, &port,
        &address, &numeric, &secure);
    if (result != OK) {
        LOG_ERROR("NET", "URL HTTP invalida");
        return result;
    }
    if (http_require_https && !secure) {
        LOG_ERROR("NET", "Politica da requisicao exige HTTPS");
        return ERR_UNAVAILABLE;
    }
    if (secure && !tls_capability_available()) {
        http_release_socket();
        http_clear_session(HTTP_STATE_FAILED);
        http_status.event_generation++;
        return http_reject_unavailable_tls();
    }
    http_release_socket();
    http_clear_session(HTTP_STATE_RESOLVING);
    http_body_sink = sink;
    http_body_sink_context = context;
    http_body_limit = body_limit;
    http_streaming = sink ? 1U : 0U;
    http_status.body_limit = body_limit;
    http_status.streaming = http_streaming;
    http_copy_text(http_status.url, sizeof(http_status.url),
                   url, kstrlen(url));
    http_copy_text(http_status.host, sizeof(http_status.host),
                   http_parse_host_buffer,
                   kstrlen(http_parse_host_buffer));
    http_copy_text(http_status.path, sizeof(http_status.path),
                   http_parse_path_buffer,
                   kstrlen(http_parse_path_buffer));
    if (http_copy_text(http_request_path, sizeof(http_request_path),
                       http_parse_path_buffer,
                       kstrlen(http_parse_path_buffer)) != OK) {
        LOG_ERROR("NET", "Caminho HTTP excede o buffer de requisicao");
        http_fail(ERR_OVERFLOW);
        return ERR_OVERFLOW;
    }
    http_status.port = port;
    http_status.secure = secure;
    http_status.follow_redirects = http_follow_redirects;
    http_status.max_redirects = http_max_redirects;
    http_status.requests_started++;
    http_started_tick = timer_get_ticks();
    if (numeric) {
        result = http_start_socket(address);
        if (result != OK) http_fail(result);
        return result;
    }
    result = dns_resolve(
        http_parse_host_buffer, &address, &resolved);
    if (result != OK) {
        http_fail(result);
        LOG_ERROR("NET", "DNS recusou host HTTP");
        return result;
    }
    if (resolved) {
        result = http_start_socket(address);
        if (result != OK) http_fail(result);
        return result;
    }
    return OK;
}

int http_get_start(const char* url) {
    return http_get_start_internal(
        url, HTTP_BODY_CAPACITY, 0, 0, 0);
}

int http_get_stream_start(const char* url, uint32_t body_limit,
                          http_body_sink_t sink, void* context) {
    if (!sink) {
        LOG_ERROR("NET", "Consumidor nulo no GET HTTP streaming");
        return ERR_NULL;
    }
    return http_get_start_internal(url, body_limit, sink, context, 0);
}

int http_get_start_ex(const char* url,
                      const http_request_options_t* options) {
    return http_get_start_internal(
        url, HTTP_BODY_CAPACITY, 0, 0, options);
}

int http_get_stream_start_ex(const char* url, uint32_t body_limit,
                             http_body_sink_t sink, void* context,
                             const http_request_options_t* options) {
    if (!sink) {
        LOG_ERROR("NET", "Consumidor nulo no GET HTTP streaming");
        return ERR_NULL;
    }
    return http_get_start_internal(
        url, body_limit, sink, context, options);
}

static int http_maintain_resolving(void) {
    dns_status_t dns;

    if (dns_get_status(&dns) != OK) {
        LOG_ERROR("NET", "Estado DNS indisponivel para HTTP");
        return ERR_STATE;
    }
    if (dns.state == DNS_STATE_COMPLETE &&
        dns.result_ip) return http_start_socket(dns.result_ip);
    if (dns.state == DNS_STATE_FAILED) {
        return dns.last_error == OK ? ERR_STATE : dns.last_error;
    }
    return OK;
}

static int http_maintain_connecting(void) {
    net_socket_info_t socket;
    int result = net_socket_get_handle_info(http_socket, &socket);

    if (result != OK) return result;
    if (socket.state == NET_SOCKET_STATE_ERROR) {
        LOG_ERROR("NET", "Socket falhou durante conexao HTTP");
        return socket.last_error == OK ? ERR_STATE :
                                         socket.last_error;
    }
    if (socket.state != NET_SOCKET_STATE_CONNECTED) return OK;
    if (http_status.secure) {
        result = tls_client_start(http_socket, http_status.host);
        if (result != OK) return result;
        http_status.state = HTTP_STATE_TLS_HANDSHAKING;
        return OK;
    }
    result = http_build_request();
    if (result != OK) return result;
    http_status.state = HTTP_STATE_SENDING;
    return OK;
}

static int http_maintain_tls(void) {
    tls_client_status_t tls;
    int result = tls_client_maintain();

    if (result != OK) {
        if (tls_client_get_status(&tls) == OK) {
            http_status.tls_reason = tls.reason;
            http_status.tls_error = tls.bearssl_error;
        }
        return result;
    }
    if (tls_client_get_status(&tls) != OK) return ERR_STATE;
    http_status.tls_reason = tls.reason;
    http_status.tls_error = tls.bearssl_error;
    if (!tls.handshake_complete) return OK;
    http_status.tls_verified =
        (uint8_t)(tls.x509_verified && tls.hostname_verified);
    if (!http_status.tls_verified) {
        LOG_ERROR("NET", "TLS terminou sem validacao X.509 completa");
        return ERR_UNAVAILABLE;
    }
    if (http_build_request() != OK) return ERR_OVERFLOW;
    http_status.state = HTTP_STATE_SENDING;
    return OK;
}

static int http_maintain_sending(void) {
    uint16_t written = 0;
    int result;

    if (http_request_offset >= http_request_length) {
        http_status.requests_tx++;
        http_status.state = HTTP_STATE_RECEIVING_HEADERS;
        return OK;
    }
    if (http_status.secure) {
        result = tls_client_send(
            http_request_buffer + http_request_offset,
            (uint16_t)(http_request_length - http_request_offset),
            &written);
    } else {
        result = net_socket_send(
            http_socket, http_request_buffer + http_request_offset,
            http_request_length - http_request_offset, &written);
    }
    if (result != OK) return result;
    http_request_offset = (uint16_t)(
        http_request_offset + written);
    if (http_request_offset == http_request_length) {
        http_status.requests_tx++;
        http_status.state = HTTP_STATE_RECEIVING_HEADERS;
    }
    return OK;
}

static int http_maintain_receiving(void) {
    uint16_t read = 0;
    uint8_t eof = 0;
    int result;

    if (http_status.secure) {
        result = tls_client_receive(
            http_read_buffer, sizeof(http_read_buffer), &read, &eof);
    } else {
        result = net_socket_receive(
            http_socket, http_read_buffer,
            sizeof(http_read_buffer), &read, &eof);
    }

    if (result != OK) return result;
    http_status.bytes_rx += read;
    if (read) {
        result = http_consume_bytes(http_read_buffer, read);
        if (result != OK) return result;
    }
    if (http_status.state == HTTP_STATE_COMPLETE) return OK;
    if (!eof) return OK;
    if (http_status.state == HTTP_STATE_RECEIVING_HEADERS) {
        LOG_ERROR("NET", "Conexao encerrou durante headers HTTP");
        return ERR_INVALID;
    }
    if (http_chunked) {
        LOG_ERROR("NET", "Conexao encerrou antes do fim chunked HTTP");
        return ERR_INVALID;
    }
    if (http_status.has_content_length) {
        if (http_status.body_length != http_status.content_length) {
            LOG_ERROR("NET", "Conexao encerrou antes do corpo HTTP");
            return ERR_INVALID;
        }
        http_complete();
        return OK;
    }
    http_complete();
    return OK;
}

int http_maintain(void) {
    uint32_t frequency;
    uint32_t timeout_ticks;
    int result = OK;

    if (!http_status.initialized) {
        LOG_ERROR("NET", "Manutencao HTTP antes da inicializacao");
        return ERR_STATE;
    }
    http_status.maintenance_cycles++;
    if (http_status.state == HTTP_STATE_IDLE ||
        http_status.state == HTTP_STATE_COMPLETE ||
        http_status.state == HTTP_STATE_FAILED) return OK;
    frequency = timer_get_frequency();
    if (!frequency ||
        HTTP_TIMEOUT_SECONDS > HTTP_MAX_SAFE_TICKS / frequency) {
        LOG_ERROR("NET", "Timer indisponivel para HTTP");
        return ERR_STATE;
    }
    timeout_ticks = frequency * HTTP_TIMEOUT_SECONDS;
    if ((uint32_t)(timer_get_ticks() - http_started_tick) >=
        timeout_ticks) {
        http_fail(ERR_TIMEOUT);
        return OK;
    }
    if (http_status.state == HTTP_STATE_RESOLVING) {
        result = http_maintain_resolving();
    } else if (http_status.state == HTTP_STATE_CONNECTING) {
        result = http_maintain_connecting();
    } else if (http_status.state == HTTP_STATE_TLS_HANDSHAKING) {
        result = http_maintain_tls();
    } else if (http_status.state == HTTP_STATE_SENDING) {
        result = http_maintain_sending();
    } else {
        result = http_maintain_receiving();
    }
    if (result != OK) {
        http_fail(result);
        /* A falha pertence a sessao; o polling continua operacional. */
    }
    return OK;
}

int http_reset(void) {
    if (!http_status.initialized) {
        LOG_ERROR("NET", "Reset HTTP antes da inicializacao");
        return ERR_STATE;
    }
    http_cancel_resolution();
    http_release_socket();
    http_clear_session(HTTP_STATE_IDLE);
    LOG_INFO("NET", "Cliente HTTP reiniciado");
    return OK;
}

int http_get_status(http_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("NET", "Destino nulo ao consultar HTTP");
        return ERR_NULL;
    }
    *out_status = http_status;
    return OK;
}

int http_get_body(const uint8_t** out_body, uint32_t* out_length) {
    if (!out_body || !out_length) {
        LOG_ERROR("NET", "Destino nulo ao consultar corpo HTTP");
        return ERR_NULL;
    }
    if (!http_status.initialized) {
        LOG_ERROR("NET", "Consulta de corpo HTTP antes da inicializacao");
        return ERR_STATE;
    }
    if (http_streaming) {
        LOG_WARN("NET", "Corpo HTTP streaming nao possui buffer consultavel");
        return ERR_UNAVAILABLE;
    }
    *out_body = http_body_buffer;
    *out_length = http_status.body_length;
    return OK;
}

static int http_validate_url_vector(void) {
    char host[HTTP_HOST_BUFFER_SIZE];
    char path[HTTP_PATH_BUFFER_SIZE];
    uint16_t port;
    uint32_t address;
    uint8_t numeric;
    uint8_t secure;

    if (http_parse_url("http://example.com:8080/a?b=1",
                       host, path, &port, &address, &numeric, &secure) != OK ||
        kstrcmp(host, "example.com") != 0 ||
        kstrcmp(path, "/a?b=1") != 0 || port != 8080U || numeric || secure) {
        LOG_ERROR("NET", "Vetor de URL HTTP falhou");
        return ERR_STATE;
    }
    if (http_parse_url("https://api.github.com/repos/a/b",
                       host, path, &port, &address, &numeric, &secure) != OK ||
        kstrcmp(host, "api.github.com") != 0 || port != HTTPS_DEFAULT_PORT ||
        !secure) {
        LOG_ERROR("NET", "Vetor de URL HTTPS falhou");
        return ERR_STATE;
    }
    return OK;
}

static int http_validate_header_vectors(void) {
    static const uint8_t valid[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n";
    static const uint8_t chunked[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    static const uint8_t compressed[] =
        "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n"
        "Content-Length: 3\r\n\r\n";
    static const uint8_t conflicting[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n"
        "Content-Length: 4\r\n\r\n";
    static const uint8_t conflicting_framing[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n"
        "Content-Length: 3\r\n\r\n";
    static http_header_result_t result;

    if (http_parse_headers(valid, sizeof(valid) - 1U, &result) != OK ||
        result.status_code != 200U ||
        !result.has_content_length || result.content_length != 3U) {
        LOG_ERROR("NET", "Vetor de headers HTTP falhou");
        return ERR_STATE;
    }
    if (http_parse_headers(chunked, sizeof(chunked) - 1U,
                           &result) != OK || !result.chunked ||
        result.has_content_length || result.eof_framed) {
        LOG_ERROR("NET", "Vetor chunked HTTP falhou");
        return ERR_STATE;
    }
    if (http_parse_headers(compressed, sizeof(compressed) - 1U,
                           &result) != ERR_UNAVAILABLE) {
        LOG_ERROR("NET", "Compressao HTTP foi aceita");
        return ERR_STATE;
    }
    if (http_parse_headers(conflicting, sizeof(conflicting) - 1U,
                           &result) != ERR_INVALID) {
        LOG_ERROR("NET", "Framing HTTP conflitante foi aceito");
        return ERR_STATE;
    }
    if (http_parse_headers(conflicting_framing,
                           sizeof(conflicting_framing) - 1U,
                           &result) != ERR_INVALID) {
        LOG_ERROR("NET", "Chunked HTTP com tamanho conflitou");
        return ERR_STATE;
    }
    return OK;
}

int http_validate_state(void) {
    tls_client_status_t tls;
    uint8_t tls_status_valid = 0U;

    if (http_validate_url_vector() != OK ||
        http_validate_header_vectors() != OK) return ERR_STATE;
    if (!http_status.initialized) {
        if (http_socket || http_status.state != HTTP_STATE_IDLE) {
            LOG_ERROR("NET", "HTTP ativo sem inicializacao");
            return ERR_STATE;
        }
        return OK;
    }
    if (tls_client_get_status(&tls) == OK) tls_status_valid = 1U;
    if (http_status.state > HTTP_STATE_TLS_HANDSHAKING ||
        http_status.url[HTTP_URL_MAX_LENGTH] != '\0' ||
        http_status.host[HTTP_HOST_MAX_LENGTH] != '\0' ||
        http_status.path[HTTP_PATH_MAX_LENGTH] != '\0' ||
        http_request_path[HTTP_REDIRECT_URL_MAX_LENGTH] != '\0' ||
        http_status.headers_length > HTTP_HEADER_CAPACITY ||
        http_chunk_state > HTTP_CHUNK_STATE_TRAILER_LF ||
        (http_chunked && http_status.has_content_length) ||
        http_status.body_length > http_status.body_limit ||
        http_status.body_limit != http_body_limit ||
        (http_status.streaming != http_streaming) ||
        (http_status.follow_redirects != http_follow_redirects) ||
        (http_status.max_redirects != http_max_redirects) ||
        (http_status.redirect_count > http_status.max_redirects) ||
        (http_streaming && !http_body_sink) ||
        (http_status.has_content_length &&
         (http_status.body_length > http_status.content_length ||
          http_status.content_length > http_status.body_limit)) ||
        (http_status.state == HTTP_STATE_COMPLETE &&
         (http_status.status_code < HTTP_STATUS_MINIMUM ||
          http_status.status_code > HTTP_STATUS_MAXIMUM)) ||
        ((http_status.state == HTTP_STATE_CONNECTING ||
          http_status.state == HTTP_STATE_TLS_HANDSHAKING ||
          http_status.state == HTTP_STATE_SENDING ||
          http_status.state == HTTP_STATE_RECEIVING_HEADERS ||
          http_status.state == HTTP_STATE_RECEIVING_BODY) &&
         !http_socket) ||
        (http_status.state == HTTP_STATE_TLS_HANDSHAKING &&
         !http_status.secure) ||
        (!http_status.secure && http_status.tls_verified) ||
        (http_status.secure &&
         (http_status.state == HTTP_STATE_TLS_HANDSHAKING ||
          http_status.state == HTTP_STATE_SENDING ||
          http_status.state == HTTP_STATE_RECEIVING_HEADERS ||
          http_status.state == HTTP_STATE_RECEIVING_BODY) &&
         (!tls_status_valid || !tls.active ||
          (http_status.state == HTTP_STATE_TLS_HANDSHAKING &&
           tls.state != TLS_CLIENT_STATE_HANDSHAKING) ||
          (http_status.state != HTTP_STATE_TLS_HANDSHAKING &&
           (!tls.handshake_complete || !http_status.tls_verified)))) ||
        (http_status.secure && http_status.state == HTTP_STATE_COMPLETE &&
         !http_status.tls_verified)) {
        LOG_ERROR("NET", "Estado HTTP inconsistente");
        return ERR_STATE;
    }
    return OK;
}

const char* http_state_name(http_state_t state) {
    if (state == HTTP_STATE_IDLE) return "IDLE";
    if (state == HTTP_STATE_RESOLVING) return "RESOLVING";
    if (state == HTTP_STATE_CONNECTING) return "CONNECTING";
    if (state == HTTP_STATE_SENDING) return "SENDING";
    if (state == HTTP_STATE_RECEIVING_HEADERS) {
        return "RECEIVING_HEADERS";
    }
    if (state == HTTP_STATE_RECEIVING_BODY) return "RECEIVING_BODY";
    if (state == HTTP_STATE_COMPLETE) return "COMPLETE";
    if (state == HTTP_STATE_FAILED) return "FAILED";
    if (state == HTTP_STATE_TLS_HANDSHAKING) return "TLS_HANDSHAKING";
    return "DESCONHECIDO";
}
