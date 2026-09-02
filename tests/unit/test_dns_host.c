#include <stdint.h>
#include <stdio.h>

#include "core/dns.h"
#include "core/errors.h"
#include "core/ipv4.h"
#include "core/log.h"
#include "core/string.h"
#include "core/udp.h"
#include "core/video.h"
#include "drivers/serial.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define DNS_HEADER_SIZE 12U
#define DNS_TYPE_A 1U
#define DNS_TYPE_CNAME 5U
#define DNS_CLASS_IN 1U
#define DNS_FLAG_QUERY_RECURSION 0x0100U
#define DNS_FLAG_RESPONSE 0x8000U
#define DNS_FLAG_TRUNCATED 0x0200U
#define DNS_SERVER_PORT 53U
#define DNS_POINTER_OFFSET 12U
#define DNS_RECORD_HEADER_SIZE 10U
#define DNS_LOCAL_IP 0x0A000203U
#define DNS_SERVER_IP 0x0A00020FU
#define DNS_RESULT_IP 0x0A00022AU
#define DNS_CNAME_IP 0x0A00022BU
#define DNS_ENDPOINT 0xD501U

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
    printf("ZCOV_BEGIN|case=host:network:dns|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:network:dns|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:network:dns|value=0x%08X\n",
           (uint32_t)result);
}

static udp_receive_fn registered_callback;
static udp_endpoint_handle_t bound_endpoint;
static uint16_t bound_port;
static int udp_bind_result;
static int udp_send_result;
static uint8_t udp_send_flag;
static uint8_t transmitted_payload[DNS_MAX_PACKET_SIZE];
static uint16_t transmitted_length;
static uint32_t transmitted_destination;
static uint16_t transmitted_destination_port;
static uint32_t fake_ticks;
static uint32_t fake_frequency;

int udp_bind(uint16_t local_port, uint8_t flags, udp_receive_fn callback,
             udp_endpoint_handle_t* out_handle) {
    (void)flags;
    if (!local_port || !callback || !out_handle) return ERR_INVALID;
    if (udp_bind_result != OK) return udp_bind_result;
    registered_callback = callback;
    bound_port = local_port;
    bound_endpoint = DNS_ENDPOINT;
    *out_handle = bound_endpoint;
    return OK;
}

int udp_send(udp_endpoint_handle_t handle, uint32_t destination_ip,
             uint16_t destination_port, const uint8_t* payload,
             uint16_t payload_length, uint8_t* out_sent) {
    if (handle != bound_endpoint || !payload || !out_sent ||
        destination_port != DNS_SERVER_PORT ||
        payload_length > sizeof(transmitted_payload)) return ERR_INVALID;
    if (udp_send_result != OK) return udp_send_result;
    kmemcpy(transmitted_payload, payload, payload_length);
    transmitted_length = payload_length;
    transmitted_destination = destination_ip;
    transmitted_destination_port = destination_port;
    *out_sent = udp_send_flag;
    return OK;
}

uint8_t ipv4_address_is_unicast(uint32_t ip_address) {
    uint8_t first_octet = (uint8_t)(ip_address >> 24U);

    if (!ip_address || ip_address == 0xFFFFFFFFU || first_octet == 127U ||
        first_octet >= 224U) return 0U;
    return 1U;
}

uint32_t timer_get_ticks(void) {
    return fake_ticks;
}

uint32_t timer_get_frequency(void) {
    return fake_frequency;
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

static void write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

static void write_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static uint16_t read_u16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

static int append_name(uint8_t* packet, uint16_t* offset, const char* name) {
    const char* cursor = name;

    if (!packet || !offset || !name) return ERR_NULL;
    while (*cursor) {
        const char* label = cursor;
        uint8_t length = 0U;

        while (*cursor && *cursor != '.') {
            length++;
            cursor++;
        }
        if (!length || length > 63U) return ERR_INVALID;
        packet[(*offset)++] = length;
        for (uint8_t index = 0U; index < length; index++) {
            packet[(*offset)++] = (uint8_t)label[index];
        }
        if (*cursor == '.') cursor++;
    }
    packet[(*offset)++] = 0U;
    return OK;
}

static uint16_t begin_response(uint8_t* response, uint16_t flags,
                               uint16_t answer_count) {
    uint16_t offset = DNS_HEADER_SIZE;

    kmemset(response, 0, DNS_MAX_PACKET_SIZE);
    write_u16(response, read_u16(transmitted_payload));
    write_u16(response + 2U, flags);
    write_u16(response + 4U, 1U);
    write_u16(response + 6U, answer_count);
    kmemcpy(response + offset, transmitted_payload + DNS_HEADER_SIZE,
            transmitted_length - DNS_HEADER_SIZE);
    offset = transmitted_length;
    return offset;
}

static uint16_t append_a_record(uint8_t* response, uint16_t offset,
                                uint32_t address, uint32_t ttl) {
    response[offset++] = 0xC0U;
    response[offset++] = DNS_POINTER_OFFSET;
    write_u16(response + offset, DNS_TYPE_A);
    offset = (uint16_t)(offset + 2U);
    write_u16(response + offset, DNS_CLASS_IN);
    offset = (uint16_t)(offset + 2U);
    write_u32(response + offset, ttl);
    offset = (uint16_t)(offset + 4U);
    write_u16(response + offset, 4U);
    offset = (uint16_t)(offset + 2U);
    write_u32(response + offset, address);
    return (uint16_t)(offset + 4U);
}

static uint16_t append_cname_record(uint8_t* response, uint16_t offset,
                                    const char* name, uint32_t ttl) {
    uint16_t length_offset;
    uint16_t data_offset;

    response[offset++] = 0xC0U;
    response[offset++] = DNS_POINTER_OFFSET;
    write_u16(response + offset, DNS_TYPE_CNAME);
    offset = (uint16_t)(offset + 2U);
    write_u16(response + offset, DNS_CLASS_IN);
    offset = (uint16_t)(offset + 2U);
    write_u32(response + offset, ttl);
    offset = (uint16_t)(offset + 4U);
    length_offset = offset;
    data_offset = (uint16_t)(offset + 2U);
    offset = (uint16_t)(offset + 2U);
    append_name(response, &offset, name);
    write_u16(response + length_offset, (uint16_t)(offset - data_offset));
    return offset;
}

static uint16_t make_a_response(uint8_t* response, uint16_t flags,
                                uint32_t address, uint32_t ttl) {
    return append_a_record(response, begin_response(response, flags, 1U),
                           address, ttl);
}

static uint16_t make_cname_response(uint8_t* response, const char* name,
                                    uint32_t ttl) {
    return append_cname_record(
        response,
        begin_response(response, DNS_FLAG_RESPONSE | DNS_FLAG_QUERY_RECURSION,
                       1U),
        name, ttl);
}

static int deliver_packet(const uint8_t* payload, uint16_t length,
                          uint32_t source_ip, uint16_t source_port,
                          uint16_t destination_port) {
    udp_datagram_view_t datagram = {
        "eth0", payload, length, source_ip, DNS_LOCAL_IP, source_port,
        destination_port, IPV4_DELIVERY_LOCAL_UNICAST
    };

    if (!registered_callback) return ERR_STATE;
    return registered_callback(&datagram);
}

static int expect_status(dns_state_t state, int last_error,
                         uint32_t result_ip) {
    dns_status_t status;

    if (dns_get_status(&status) != OK) return 0;
    return status.state == state && status.last_error == last_error &&
           status.result_ip == result_ip;
}

static int check_dns(void) {
    uint8_t response[DNS_MAX_PACKET_SIZE];
    uint8_t malformed[DNS_HEADER_SIZE];
    char long_name[DNS_NAME_MAX_LENGTH + 2U];
    dns_status_t status;
    dns_cache_entry_info_t cache_entry;
    uint32_t result_ip;
    uint8_t resolved;
    uint16_t response_length;

    fake_ticks = 100U;
    fake_frequency = 1000U;
    udp_bind_result = ERR_UNAVAILABLE;
    udp_send_result = OK;
    udp_send_flag = 1U;
    registered_callback = NULL;
    bound_endpoint = 0U;
    bound_port = 0U;
    transmitted_length = 0U;
    if (dns_get_status(NULL) != ERR_NULL ||
        dns_get_cache_entry(0U, NULL) != ERR_NULL ||
        dns_get_cache_entry(DNS_CACHE_CAPACITY, &cache_entry) != ERR_INVALID ||
        dns_clear() != ERR_STATE || dns_reset() != ERR_STATE ||
        dns_unconfigure() != ERR_STATE || dns_maintain() != ERR_STATE ||
        dns_validate_state() != OK || dns_init() != ERR_UNAVAILABLE ||
        dns_validate_state() != OK) return 1;
    udp_bind_result = OK;
    if (dns_init() != OK || dns_init() != OK || !registered_callback ||
        bound_endpoint != DNS_ENDPOINT || !bound_port ||
        dns_configure(0U) != ERR_INVALID ||
        dns_configure(0xE0000001U) != ERR_INVALID ||
        dns_configure(DNS_SERVER_IP) != OK ||
        dns_configure(DNS_SERVER_IP) != OK || dns_validate_state() != OK) {
        return 2;
    }
    if (kstrcmp(dns_state_name(DNS_STATE_IDLE), "IDLE") != 0 ||
        kstrcmp(dns_state_name(DNS_STATE_RESOLVING_ARP), "RESOLVING_ARP") != 0 ||
        kstrcmp(dns_state_name(DNS_STATE_WAITING_REPLY), "WAITING_REPLY") != 0 ||
        kstrcmp(dns_state_name(DNS_STATE_COMPLETE), "COMPLETE") != 0 ||
        kstrcmp(dns_state_name(DNS_STATE_FAILED), "FAILED") != 0 ||
        kstrcmp(dns_state_name((dns_state_t)99), "DESCONHECIDO") != 0) {
        return 3;
    }
    for (uint32_t index = 0U; index < DNS_NAME_MAX_LENGTH + 1U; index++) {
        long_name[index] = 'a';
    }
    long_name[DNS_NAME_MAX_LENGTH + 1U] = '\0';
    if (dns_resolve(NULL, &result_ip, &resolved) != ERR_NULL ||
        dns_resolve("a.example", NULL, &resolved) != ERR_NULL ||
        dns_resolve("a.example", &result_ip, NULL) != ERR_NULL ||
        dns_resolve("", &result_ip, &resolved) != ERR_INVALID ||
        dns_resolve("-bad.example", &result_ip, &resolved) != ERR_INVALID ||
        dns_resolve("bad-.example", &result_ip, &resolved) != ERR_INVALID ||
        dns_resolve("a..example", &result_ip, &resolved) != ERR_INVALID ||
        dns_resolve(long_name, &result_ip, &resolved) != ERR_INVALID ||
        dns_get_cache_entry(0U, &cache_entry) != OK || cache_entry.used ||
        dns_validate_state() != OK) return 4;
    udp_send_flag = 0U;
    if (dns_resolve("Example.COM.", &result_ip, &resolved) != OK || resolved ||
        !expect_status(DNS_STATE_RESOLVING_ARP, OK, 0U) ||
        dns_maintain() != OK) return 5;
    udp_send_flag = 1U;
    if (dns_maintain() != OK) return 5;
    if (read_u16(transmitted_payload + 4U) != 1U ||
        transmitted_destination != DNS_SERVER_IP ||
        transmitted_destination_port != DNS_SERVER_PORT ||
        transmitted_length <= DNS_HEADER_SIZE ||
        dns_resolve("example.com", &result_ip, &resolved) != OK || resolved ||
        dns_resolve("other.example", &result_ip, &resolved) != ERR_STATE ||
        !expect_status(DNS_STATE_WAITING_REPLY, OK, 0U) ||
        dns_validate_state() != OK) return 6;
    response_length = make_a_response(
        response, DNS_FLAG_RESPONSE | DNS_FLAG_QUERY_RECURSION,
        DNS_RESULT_IP, 3U);
    if (deliver_packet(response, response_length, DNS_SERVER_IP,
                       DNS_SERVER_PORT, bound_port) != OK ||
        !expect_status(DNS_STATE_COMPLETE, OK, DNS_RESULT_IP) ||
        dns_get_status(&status) != OK || status.replies_rx != 1U ||
        status.cache_entries != 1U || status.cache_misses != 1U ||
        dns_get_cache_entry(0U, &cache_entry) != OK || !cache_entry.used ||
        kstrcmp(cache_entry.name, "example.com") != 0 ||
        cache_entry.address != DNS_RESULT_IP ||
        cache_entry.ttl_remaining_seconds != 3U ||
        dns_validate_state() != OK) return 7;
    fake_ticks = 110U;
    if (dns_resolve("EXAMPLE.COM", &result_ip, &resolved) != OK || !resolved ||
        result_ip != DNS_RESULT_IP || dns_get_status(&status) != OK ||
        status.cache_hits != 1U || status.state != DNS_STATE_COMPLETE ||
        dns_clear() != OK || dns_get_status(&status) != OK ||
        status.cache_entries != 0U || dns_validate_state() != OK) return 8;
    fake_ticks = 200U;
    if (dns_resolve("www.example.com", &result_ip, &resolved) != OK ||
        resolved || !expect_status(DNS_STATE_WAITING_REPLY, OK, 0U)) return 9;
    response_length = make_cname_response(response, "alias.example.com", 4U);
    if (deliver_packet(response, response_length, DNS_SERVER_IP,
                       DNS_SERVER_PORT, bound_port) != OK ||
        !expect_status(DNS_STATE_WAITING_REPLY, OK, 0U) ||
        dns_get_status(&status) != OK || status.cname_depth != 1U ||
        status.replies_rx != 2U ||
        transmitted_length <= DNS_HEADER_SIZE) return 10;
    response_length = make_a_response(
        response, DNS_FLAG_RESPONSE | DNS_FLAG_QUERY_RECURSION,
        DNS_CNAME_IP, 8U);
    if (deliver_packet(response, response_length, DNS_SERVER_IP,
                       DNS_SERVER_PORT, bound_port) != OK ||
        !expect_status(DNS_STATE_COMPLETE, OK, DNS_CNAME_IP) ||
        dns_get_status(&status) != OK || status.cache_entries != 1U ||
        status.cname_depth != 1U ||
        dns_get_cache_entry(0U, &cache_entry) != OK || !cache_entry.used ||
        cache_entry.ttl_remaining_seconds != 4U || dns_validate_state() != OK) {
        return 11;
    }
    if (dns_reset() != OK || dns_get_status(&status) != OK ||
        status.state != DNS_STATE_IDLE || status.query_name[0] ||
        dns_clear() != OK) return 12;
    fake_ticks = 400U;
    if (dns_resolve("expired.example", &result_ip, &resolved) != OK ||
        dns_maintain() != OK) return 13;
    response_length = make_a_response(
        response, DNS_FLAG_RESPONSE | DNS_FLAG_QUERY_RECURSION,
        DNS_RESULT_IP, 1U);
    if (deliver_packet(response, response_length, DNS_SERVER_IP,
                       DNS_SERVER_PORT, bound_port) != OK ||
        dns_get_cache_entry(0U, &cache_entry) != OK || !cache_entry.used) {
        return 14;
    }
    fake_ticks = 1501U;
    if (dns_get_cache_entry(0U, &cache_entry) != OK || cache_entry.used ||
        dns_resolve("expired.example", &result_ip, &resolved) != OK ||
        resolved || !expect_status(DNS_STATE_WAITING_REPLY, OK, 0U) ||
        dns_reset() != OK || dns_clear() != OK) return 15;
    fake_ticks = 500U;
    if (dns_resolve("invalid.example", &result_ip, &resolved) != OK) return 16;
    malformed[0] = transmitted_payload[0];
    malformed[1] = transmitted_payload[1];
    if (deliver_packet(NULL, 0U, DNS_SERVER_IP, DNS_SERVER_PORT, bound_port) !=
            OK || deliver_packet(malformed, sizeof(malformed), DNS_SERVER_IP,
                                 DNS_SERVER_PORT, bound_port) != OK ||
        !expect_status(DNS_STATE_FAILED, ERR_INVALID, 0U) ||
        dns_get_status(&status) != OK || status.invalid_packets == 0U ||
        dns_reset() != OK) return 17;
    if (dns_resolve("rcode.example", &result_ip, &resolved) != OK) return 18;
    response_length = begin_response(
        response, DNS_FLAG_RESPONSE | DNS_FLAG_QUERY_RECURSION | 3U, 0U);
    if (deliver_packet(response, response_length, DNS_SERVER_IP,
                       DNS_SERVER_PORT, bound_port) != OK ||
        !expect_status(DNS_STATE_FAILED, ERR_NOT_FOUND, 0U) ||
        dns_reset() != OK) return 19;
    if (dns_resolve("bad-address.example", &result_ip, &resolved) != OK) {
        return 20;
    }
    response_length = make_a_response(
        response, DNS_FLAG_RESPONSE | DNS_FLAG_QUERY_RECURSION,
        0xE0000001U, 1U);
    if (deliver_packet(response, response_length, DNS_SERVER_IP,
                       DNS_SERVER_PORT, bound_port) != OK ||
        !expect_status(DNS_STATE_FAILED, ERR_INVALID, 0U) ||
        dns_reset() != OK) return 21;
    if (dns_resolve("ignored.example", &result_ip, &resolved) != OK ||
        deliver_packet(response, response_length, DNS_SERVER_IP + 1U,
                       DNS_SERVER_PORT, bound_port) != OK ||
        deliver_packet(response, response_length, DNS_SERVER_IP,
                       DNS_SERVER_PORT + 1U, bound_port) != OK ||
        dns_get_status(&status) != OK || status.ignored_packets < 2U ||
        dns_reset() != OK) return 22;
    if (dns_resolve("timeout.example", &result_ip, &resolved) != OK) return 23;
    fake_ticks += 1000U;
    if (dns_maintain() != OK) return 24;
    fake_ticks += 1000U;
    if (dns_maintain() != OK) return 25;
    fake_ticks += 1000U;
    if (dns_maintain() != OK ||
        !expect_status(DNS_STATE_FAILED, ERR_TIMEOUT, 0U) ||
        dns_get_status(&status) != OK || status.timeouts == 0U ||
        dns_reset() != OK) return 26;
    fake_frequency = 0U;
    if (dns_resolve("no-clock.example", &result_ip, &resolved) != ERR_STATE) {
        return 27;
    }
    fake_frequency = 1000U;
    if (dns_resolve("maintenance.example", &result_ip, &resolved) != OK) {
        return 28;
    }
    fake_frequency = 0U;
    if (dns_maintain() != ERR_STATE || dns_reset() != OK) return 29;
    fake_frequency = 1000U;
    udp_send_result = ERR_UNAVAILABLE;
    if (dns_resolve("send-failure.example", &result_ip, &resolved) !=
            ERR_UNAVAILABLE ||
        !expect_status(DNS_STATE_FAILED, ERR_UNAVAILABLE, 0U) ||
        dns_reset() != OK) return 30;
    udp_send_result = OK;
    if (dns_unconfigure() != OK || dns_resolve("offline.example", &result_ip,
                                               &resolved) != ERR_STATE ||
        dns_clear() != OK || dns_reset() != OK ||
        dns_get_status(&status) != OK || status.configured ||
        status.state != DNS_STATE_IDLE || dns_validate_state() != OK) return 31;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    log_init();
    result = check_dns();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
