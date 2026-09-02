#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/udp.h"
#include "drivers/serial.h"
#include "core/video.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U

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
    printf("ZCOV_BEGIN|case=host:network:udp|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:network:udp|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:network:udp|value=0x%08X\n",
           (uint32_t)result);
}

static ipv4_protocol_handler_fn registered_handler;
static ipv4_status_t ipv4_fixture_status;
static uint8_t transmitted_segment[IPV4_MAX_PAYLOAD_SIZE];
static uint16_t transmitted_length;
static uint8_t transmit_result;
static uint8_t broadcast_result;

int ipv4_register_handler(uint8_t protocol,
                          ipv4_protocol_handler_fn handler) {
    if (protocol != IPV4_PROTOCOL_UDP || !handler) return ERR_INVALID;
    registered_handler = handler;
    return OK;
}

int ipv4_get_status(ipv4_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = ipv4_fixture_status;
    return OK;
}

int ipv4_send(uint32_t destination_ip, uint8_t protocol,
              const uint8_t* payload, uint16_t payload_length,
              uint8_t* out_sent) {
    (void)destination_ip;
    if (!payload || !out_sent || protocol != IPV4_PROTOCOL_UDP ||
        payload_length > sizeof(transmitted_segment)) return ERR_INVALID;
    kmemcpy(transmitted_segment, payload, payload_length);
    transmitted_length = payload_length;
    *out_sent = transmit_result;
    return OK;
}

int ipv4_send_limited_broadcast(const char* interface_id,
                                uint32_t source_ip, uint8_t protocol,
                                const uint8_t* payload,
                                uint16_t payload_length,
                                uint8_t* out_sent) {
    (void)source_ip;
    if (!interface_id || !payload || !out_sent ||
        protocol != IPV4_PROTOCOL_UDP ||
        payload_length > sizeof(transmitted_segment)) return ERR_INVALID;
    kmemcpy(transmitted_segment, payload, payload_length);
    transmitted_length = payload_length;
    *out_sent = broadcast_result;
    return OK;
}

uint32_t timer_get_ticks(void) {
    return 100U;
}

uint32_t timer_get_frequency(void) {
    return 1000U;
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

static uint32_t receive_calls;
static udp_datagram_view_t last_datagram;
static int receive_result;

static int receive_callback(const udp_datagram_view_t* datagram) {
    if (!datagram) return ERR_NULL;
    receive_calls++;
    last_datagram = *datagram;
    return receive_result;
}

static uint16_t fixture_checksum(const uint8_t* segment, uint16_t length) {
    uint32_t sum = 0U;

    sum += 0x0A00U;
    sum += 0x020FU;
    sum += 0x0A00U;
    sum += 0x0203U;
    sum += IPV4_PROTOCOL_UDP;
    sum += length;
    for (uint16_t offset = 0U; offset + 1U < length; offset += 2U) {
        sum += ((uint16_t)segment[offset] << 8U) | segment[offset + 1U];
    }
    if (length & 1U) sum += (uint16_t)segment[length - 1U] << 8U;
    while (sum >> 16U) sum = (sum & 0xFFFFU) + (sum >> 16U);
    return (uint16_t)~sum;
}

static void prepare_segment(uint16_t destination_port) {
    uint16_t checksum;

    transmitted_segment[0] = 0x12U;
    transmitted_segment[1] = 0x34U;
    transmitted_segment[2] = (uint8_t)(destination_port >> 8U);
    transmitted_segment[3] = (uint8_t)destination_port;
    transmitted_segment[4] = (uint8_t)(transmitted_length >> 8U);
    transmitted_segment[5] = (uint8_t)transmitted_length;
    transmitted_segment[6] = 0U;
    transmitted_segment[7] = 0U;
    checksum = fixture_checksum(transmitted_segment, transmitted_length);
    transmitted_segment[6] = (uint8_t)(checksum >> 8U);
    transmitted_segment[7] = (uint8_t)checksum;
}

static int invoke_segment(ipv4_delivery_t delivery) {
    ipv4_packet_view_t packet;

    if (!registered_handler) return ERR_STATE;
    packet.interface_id = "eth0";
    packet.payload = transmitted_segment;
    packet.payload_length = transmitted_length;
    packet.source_ip = 0x0A00020FU;
    packet.destination_ip = 0x0A000203U;
    packet.identification = 1U;
    packet.protocol = IPV4_PROTOCOL_UDP;
    packet.ttl = 64U;
    packet.delivery = delivery;
    return registered_handler(&packet);
}

static int check_udp(void) {
    udp_status_t status;
    udp_endpoint_handle_t endpoint;
    udp_endpoint_handle_t nonbroadcast;
    udp_endpoint_handle_t broadcast_endpoint;
    uint8_t payload[] = {'u', 'd', 'p'};
    uint8_t sent = 0U;

    kmemset(&ipv4_fixture_status, 0, sizeof(ipv4_fixture_status));
    ipv4_fixture_status.initialized = 1U;
    ipv4_fixture_status.configured = 1U;
    ipv4_fixture_status.local_ip = 0x0A000203U;
    transmit_result = 1U;
    broadcast_result = 1U;
    receive_result = OK;
    if (udp_get_status(NULL) != ERR_NULL || udp_init() != OK ||
        udp_init() != OK || !registered_handler ||
        udp_get_status(&status) != OK || !status.initialized ||
        udp_validate_state() != OK) return 1;
    if (udp_bind(0U, 0U, receive_callback, &endpoint) != ERR_INVALID ||
        udp_bind(4000U, 2U, receive_callback, &endpoint) != ERR_INVALID ||
        udp_bind(4000U, 0U, NULL, &endpoint) != ERR_NULL ||
        udp_bind(4000U, 0U, receive_callback, NULL) != ERR_NULL ||
        udp_bind(4000U, 0U, receive_callback, &endpoint) != OK ||
        udp_bind(4000U, 0U, receive_callback, &broadcast_endpoint) !=
            ERR_STATE) return 2;
    if (udp_send(0U, 0U, 53U, payload, sizeof(payload), &sent) !=
            ERR_INVALID ||
        udp_send(endpoint, 0U, 53U, payload, sizeof(payload), &sent) !=
            OK || sent != 1U || transmitted_length != UDP_HEADER_SIZE +
            sizeof(payload)) return 3;
    transmitted_length = UDP_HEADER_SIZE + sizeof(payload);
    prepare_segment(4000U);
    if (invoke_segment(IPV4_DELIVERY_LOCAL_UNICAST) != OK ||
        receive_calls != 1U || last_datagram.source_port != 0x1234U ||
        last_datagram.destination_port != 4000U ||
        last_datagram.payload_length != sizeof(payload) ||
        last_datagram.delivery != IPV4_DELIVERY_LOCAL_UNICAST) return 4;
    prepare_segment(5000U);
    if (invoke_segment(IPV4_DELIVERY_LOCAL_UNICAST) != OK ||
        udp_get_status(&status) != OK || status.rx_no_listener == 0U) return 5;
    transmitted_segment[6] ^= 0x01U;
    if (invoke_segment(IPV4_DELIVERY_LOCAL_UNICAST) != OK ||
        udp_get_status(&status) != OK || status.rx_checksum_errors == 0U) {
        return 6;
    }
    transmitted_length = UDP_HEADER_SIZE - 1U;
    if (invoke_segment(IPV4_DELIVERY_LOCAL_UNICAST) != OK ||
        udp_get_status(&status) != OK || status.rx_length_errors == 0U) {
        return 7;
    }
    transmitted_length = UDP_HEADER_SIZE + sizeof(payload);
    transmitted_segment[4] = 0U;
    transmitted_segment[5] = UDP_HEADER_SIZE - 1U;
    if (invoke_segment(IPV4_DELIVERY_LOCAL_UNICAST) != OK) return 8;
    transmitted_segment[4] = (uint8_t)(transmitted_length >> 8U);
    transmitted_segment[5] = (uint8_t)transmitted_length;
    receive_result = ERR_STATE;
    prepare_segment(4000U);
    if (invoke_segment(IPV4_DELIVERY_LOCAL_UNICAST) != ERR_STATE ||
        udp_get_status(&status) != OK || status.rx_protocol_errors == 0U) {
        return 9;
    }
    receive_result = OK;
    if (udp_bind(4001U, 0U, receive_callback, &nonbroadcast) != OK) {
        return 10;
    }
    prepare_segment(4001U);
    if (invoke_segment(IPV4_DELIVERY_LIMITED_BROADCAST) != OK ||
        udp_get_status(&status) != OK || status.rx_no_listener == 0U ||
        udp_bind(4002U, UDP_BIND_ALLOW_BROADCAST, receive_callback,
                 &broadcast_endpoint) != OK) return 10;
    if (udp_send_limited_broadcast(endpoint, "eth0", 0x0A000203U, 4000U,
                                    payload, sizeof(payload), &sent) !=
            ERR_INVALID ||
        udp_send_limited_broadcast(broadcast_endpoint, "eth0",
                                    0x0A000203U, 4000U, payload,
                                    sizeof(payload), &sent) != OK ||
        sent != 1U || udp_send_limited_broadcast(broadcast_endpoint, NULL,
            0x0A000203U, 4000U, payload, sizeof(payload), &sent) != ERR_NULL) {
        return 11;
    }
    if (udp_send(endpoint, 0x0A000204U, 53U, NULL, 1U, &sent) != ERR_NULL ||
        udp_send(endpoint, 0x0A000204U, 53U, payload,
                 UDP_MAX_PAYLOAD_SIZE + 1U, &sent) != ERR_INVALID ||
        udp_unbind(0U) != ERR_INVALID || udp_unbind(endpoint) != OK ||
        udp_unbind(endpoint) != ERR_INVALID || udp_unbind(nonbroadcast) != OK ||
        udp_unbind(broadcast_endpoint) != OK ||
        udp_unbind(0x0101U) != ERR_INVALID || udp_validate_state() != OK) {
        return 12;
    }
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    log_init();
    result = check_udp();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
