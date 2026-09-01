#include <stdint.h>
#include <stdio.h>

#include "core/arp.h"
#include "core/errors.h"
#include "core/ethernet.h"
#include "core/ipv4.h"
#include "core/log.h"
#include "drivers/serial.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define IPV4_INTERFACE "host-net0"
#define IPV4_LOCAL 0x0A00020FU
#define IPV4_GATEWAY 0x0A000202U
#define IPV4_MAC_0 0x02U
#define IPV4_ETHERTYPE 0x0800U
#define IPV4_VERSION_IHL 0x45U
#define IPV4_FLAG_DONT_FRAGMENT 0x4000U
#define IPV4_BROADCAST 0xFFFFFFFFU

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static ethernet_protocol_handler_fn registered_ipv4_handler;
static arp_status_t fake_arp_status;
static uint8_t ethernet_send_called;
static uint16_t ethernet_send_length;
static uint8_t ethernet_send_payload[IPV4_MTU];

static void __attribute__((no_instrument_function)) coverage_record(void* function) {
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
    printf("ZCOV_BEGIN|case=host:network:ipv4|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:network:ipv4|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:network:ipv4|value=0x%08X\n",
           (uint32_t)result);
}

uint32_t timer_get_ticks(void) {
    static uint32_t tick;
    return ++tick;
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

int ethernet_register_handler(uint16_t ethertype,
                              ethernet_protocol_handler_fn handler) {
    if (ethertype != IPV4_ETHERTYPE || !handler) return ERR_INVALID;
    registered_ipv4_handler = handler;
    return OK;
}

int ethernet_send(const char* interface_id, const uint8_t* destination,
                  uint16_t ethertype, const uint8_t* payload,
                  uint16_t payload_length) {
    (void)destination;
    if (!interface_id || !payload || ethertype != IPV4_ETHERTYPE ||
        payload_length > sizeof(ethernet_send_payload)) return ERR_INVALID;
    ethernet_send_called = 1U;
    ethernet_send_length = payload_length;
    for (uint16_t index = 0U; index < payload_length; index++) {
        ethernet_send_payload[index] = payload[index];
    }
    return OK;
}

int arp_configure(const char* interface_id, const uint8_t* local_mac,
                  uint32_t local_ip) {
    if (!interface_id || !local_mac) return ERR_NULL;
    fake_arp_status.configured = 1U;
    fake_arp_status.initialized = 1U;
    fake_arp_status.local_ip = local_ip;
    for (uint32_t index = 0U; index < ARP_MAC_ADDRESS_SIZE; index++) {
        fake_arp_status.local_mac[index] = local_mac[index];
    }
    for (uint32_t index = 0U; index < ARP_INTERFACE_ID_SIZE - 1U &&
         interface_id[index]; index++) {
        fake_arp_status.interface_id[index] = interface_id[index];
    }
    return OK;
}

int arp_clear(void) {
    return OK;
}

int arp_resolve(uint32_t ip_address, uint8_t* out_mac,
                uint8_t* out_resolved) {
    (void)ip_address;
    if (!out_mac || !out_resolved) return ERR_NULL;
    for (uint32_t index = 0U; index < ARP_MAC_ADDRESS_SIZE; index++) {
        out_mac[index] = (uint8_t)(0x10U + index);
    }
    *out_resolved = 1U;
    return OK;
}

int arp_get_status(arp_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_arp_status;
    return OK;
}

static int udp_handler(const ipv4_packet_view_t* packet) {
    return packet && (packet->delivery == IPV4_DELIVERY_LOCAL_UNICAST ||
                      packet->delivery == IPV4_DELIVERY_LIMITED_BROADCAST) &&
                   packet->payload_length == 3U && packet->payload[0] == 'u'
               ? OK : ERR_INVALID;
}

static int icmp_handler(const ipv4_packet_view_t* packet) {
    return packet ? OK : ERR_NULL;
}

static int tcp_handler(const ipv4_packet_view_t* packet) {
    return packet ? OK : ERR_INVALID;
}

static int raw_handler(const ipv4_packet_view_t* packet) {
    return packet ? OK : ERR_INVALID;
}

static uint16_t checksum(const uint8_t* data, uint16_t length) {
    uint32_t sum = 0U;

    while (length >= 2U) {
        sum += ((uint16_t)data[0] << 8U) | data[1];
        data += 2U;
        length -= 2U;
    }
    if (length) sum += (uint16_t)data[0] << 8U;
    while (sum >> 16U) sum = (sum & 0xFFFFU) + (sum >> 16U);
    return (uint16_t)~sum;
}

static void write_u16(uint8_t* output, uint16_t value) {
    output[0] = (uint8_t)(value >> 8U);
    output[1] = (uint8_t)value;
}

static void write_u32(uint8_t* output, uint32_t value) {
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

static uint16_t make_packet(uint8_t* packet, uint16_t capacity,
                            uint32_t source, uint32_t destination,
                            uint8_t protocol, const uint8_t* payload,
                            uint16_t payload_length) {
    uint16_t total = IPV4_HEADER_SIZE + payload_length;

    if (!packet || total > capacity) return 0U;
    for (uint16_t index = 0U; index < total; index++) packet[index] = 0U;
    packet[0] = IPV4_VERSION_IHL;
    write_u16(packet + 2U, total);
    write_u16(packet + 4U, 1U);
    write_u16(packet + 6U, IPV4_FLAG_DONT_FRAGMENT);
    packet[8] = 64U;
    packet[9] = protocol;
    write_u32(packet + 12U, source);
    write_u32(packet + 16U, destination);
    for (uint16_t index = 0U; index < payload_length; index++) {
        packet[IPV4_HEADER_SIZE + index] = payload[index];
    }
    write_u16(packet + 10U, checksum(packet, IPV4_HEADER_SIZE));
    return total;
}

static int deliver(uint8_t* packet, uint16_t length,
                   ethernet_destination_t destination_type,
                   const char* interface_id) {
    ethernet_frame_view_t frame = {
        interface_id, NULL, NULL, packet, length, IPV4_ETHERTYPE,
        destination_type
    };

    if (!registered_ipv4_handler) return ERR_STATE;
    return registered_ipv4_handler(&frame);
}

static int expect_initial_state(void) {
    ipv4_status_t status;
    uint8_t sent;

    if (ipv4_get_status(NULL) != ERR_NULL) return 10;
    if (ipv4_validate_state() != OK) return 11;
    if (ipv4_send(0x0A000201U, IPV4_PROTOCOL_UDP, NULL, 0U, &sent) !=
        ERR_STATE) return 12;
    if (ipv4_send(0x0A000201U, IPV4_PROTOCOL_UDP, NULL, 0U, NULL) != ERR_NULL) {
        return 13;
    }
    if (ipv4_get_status(&status) != OK || status.initialized) return 14;
    if (!ipv4_address_is_unicast(0x0A000201U) ||
        ipv4_address_is_unicast(0U) || ipv4_address_is_unicast(0xFFFFFFFFU) ||
        ipv4_address_is_unicast(0xE0000001U) ||
        ipv4_address_is_unicast(0x7F000001U)) return 15;
    if (!ipv4_mask_is_valid(0xFFFFFF00U) || ipv4_mask_is_valid(0U) ||
        ipv4_mask_is_valid(0xFF00FF00U)) return 16;
    if (ipv4_protocol_name(IPV4_PROTOCOL_ICMP)[0] != 'I' ||
        ipv4_protocol_name(99U)[0] != 'D') return 17;
    return 0;
}

static int expect_configuration(void) {
    uint8_t mac[IPV4_MAC_ADDRESS_SIZE] = {IPV4_MAC_0, 0, 0, 0, 0, 1};
    ipv4_status_t status;
    uint8_t sent;
    uint8_t payload[] = {'u', 'd', 'p'};

    if (ipv4_init() != OK || ipv4_init() != OK) return 30;
    if (ipv4_register_handler(IPV4_PROTOCOL_UDP, udp_handler) != OK) return 31;
    if (ipv4_register_handler(IPV4_PROTOCOL_UDP, udp_handler) != OK) return 32;
    if (ipv4_register_handler(IPV4_PROTOCOL_UDP, icmp_handler) != ERR_STATE) {
        return 33;
    }
    if (ipv4_register_handler(IPV4_PROTOCOL_ICMP, icmp_handler) != OK ||
        ipv4_register_handler(IPV4_PROTOCOL_TCP, tcp_handler) != OK ||
        ipv4_register_handler(99U, raw_handler) != OK) return 34;
    if (ipv4_register_handler(88U, raw_handler) != ERR_OVERFLOW) return 35;
    if (ipv4_register_handler(87U, NULL) != ERR_NULL) return 36;
    if (ipv4_configure(NULL, mac, IPV4_LOCAL, 0xFFFFFF00U, IPV4_GATEWAY) != ERR_NULL) {
        return 37;
    }
    if (ipv4_configure(IPV4_INTERFACE, NULL, IPV4_LOCAL, 0xFFFFFF00U,
                       IPV4_GATEWAY) != ERR_NULL) return 38;
    if (ipv4_configure(IPV4_INTERFACE, mac, 0xE0000001U, 0xFFFFFF00U,
                       IPV4_GATEWAY) != ERR_INVALID) return 39;
    if (ipv4_configure(IPV4_INTERFACE, mac, IPV4_LOCAL, 0xFF00FF00U,
                       IPV4_GATEWAY) != ERR_INVALID) return 40;
    if (ipv4_configure(IPV4_INTERFACE, mac, IPV4_LOCAL, 0xFFFFFF00U,
                       IPV4_GATEWAY) != OK) return 41;
    if (ipv4_configure(IPV4_INTERFACE, mac, IPV4_LOCAL, 0xFFFFFF00U,
                       IPV4_GATEWAY) != OK) return 42;
    if (ipv4_get_status(&status) != OK || !status.configured ||
        status.handler_count != 4U || status.configuration_generation != 1U) {
        return 43;
    }
    if (ipv4_validate_state() != OK) return 44;
    if (ipv4_send(IPV4_LOCAL, IPV4_PROTOCOL_UDP, payload, 3U, &sent) !=
        ERR_INVALID) return 45;
    if (ipv4_send(0x0A000203U, IPV4_PROTOCOL_UDP, payload, 3U, &sent) != OK ||
        !sent || !ethernet_send_called || ethernet_send_length != 23U) return 46;
    ethernet_send_called = 0U;
    if (ipv4_send(0xC0000201U, IPV4_PROTOCOL_UDP, payload, 3U, &sent) != OK ||
        !sent) return 47;
    if (ipv4_send(0x0A000203U, IPV4_PROTOCOL_UDP, payload,
                  IPV4_MAX_PAYLOAD_SIZE + 1U, &sent) != ERR_INVALID) return 48;
    if (ipv4_send(0x0A000203U, IPV4_PROTOCOL_UDP, NULL, 3U, &sent) != ERR_NULL) {
        return 49;
    }
    if (ipv4_send_limited_broadcast(IPV4_INTERFACE, IPV4_LOCAL,
                                    IPV4_PROTOCOL_UDP, payload, 3U,
                                    &sent) != OK || !sent) return 50;
    if (ipv4_send_limited_broadcast(IPV4_INTERFACE, IPV4_LOCAL,
                                    IPV4_PROTOCOL_TCP, payload, 3U,
                                    &sent) != ERR_INVALID) return 51;
    return 0;
}

static int expect_reception(void) {
    uint8_t packet[IPV4_MTU];
    uint8_t payload[] = {'u', 'd', 'p'};
    ipv4_status_t status;
    uint16_t length;

    length = make_packet(packet, sizeof(packet), 0x0A000201U, IPV4_LOCAL,
                         IPV4_PROTOCOL_UDP, payload, sizeof(payload));
    if (!length || deliver(packet, length, ETHERNET_DESTINATION_LOCAL_UNICAST,
                           IPV4_INTERFACE) != OK) return 60;
    packet[10] ^= 1U;
    if (deliver(packet, length, ETHERNET_DESTINATION_LOCAL_UNICAST,
                IPV4_INTERFACE) != OK) return 61;
    packet[10] ^= 1U;
    packet[0] = 0x46U;
    if (deliver(packet, length, ETHERNET_DESTINATION_LOCAL_UNICAST,
                IPV4_INTERFACE) != OK) return 62;
    packet[0] = IPV4_VERSION_IHL;
    packet[8] = 0U;
    if (deliver(packet, length, ETHERNET_DESTINATION_LOCAL_UNICAST,
                IPV4_INTERFACE) != OK) return 63;
    packet[8] = 64U;
    write_u16(packet + 6U, 1U);
    write_u16(packet + 10U, checksum(packet, IPV4_HEADER_SIZE));
    if (deliver(packet, length, ETHERNET_DESTINATION_LOCAL_UNICAST,
                IPV4_INTERFACE) != OK) return 64;
    write_u16(packet + 6U, 0U);
    write_u16(packet + 10U, checksum(packet, IPV4_HEADER_SIZE));
    write_u16(packet + 2U, IPV4_HEADER_SIZE - 1U);
    write_u16(packet + 10U, checksum(packet, IPV4_HEADER_SIZE));
    if (deliver(packet, length, ETHERNET_DESTINATION_LOCAL_UNICAST,
                IPV4_INTERFACE) != OK) return 65;
    length = make_packet(packet, sizeof(packet), 0x0A000201U, IPV4_LOCAL,
                         IPV4_PROTOCOL_UDP, payload, sizeof(payload));
    write_u16(packet + 2U, length + 1U);
    write_u16(packet + 10U, checksum(packet, IPV4_HEADER_SIZE));
    if (deliver(packet, length, ETHERNET_DESTINATION_LOCAL_UNICAST,
                IPV4_INTERFACE) != OK) return 66;
    length = make_packet(packet, sizeof(packet), 0x0A000201U, IPV4_LOCAL,
                         IPV4_PROTOCOL_UDP, payload, sizeof(payload));
    if (deliver(packet, length, ETHERNET_DESTINATION_LOCAL_UNICAST,
                "other0") != OK) return 67;
    if (deliver(packet, length, ETHERNET_DESTINATION_UNKNOWN,
                IPV4_INTERFACE) != OK) return 68;
    length = make_packet(packet, sizeof(packet), 0x0A000201U, IPV4_BROADCAST,
                         IPV4_PROTOCOL_UDP, payload, sizeof(payload));
    if (deliver(packet, length, ETHERNET_DESTINATION_BROADCAST,
                "other0") != OK) return 69;
    if (ipv4_get_status(&status) != OK || status.rx_packets < 2U ||
        status.rx_delivered != 2U || status.rx_checksum_errors == 0U ||
        status.rx_options == 0U || status.rx_fragments == 0U ||
        status.rx_ignored == 0U || status.rx_invalid == 0U) return 70;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = expect_initial_state();
    if (!result) result = expect_configuration();
    if (!result) result = expect_reception();
    if (!result && ipv4_unconfigure() != OK) result = 80;
    if (!result && ipv4_validate_state() != OK) result = 81;
    coverage_active = 0U;
    coverage_emit(result);
    if (result) printf("ipv4-host: FAIL code=%d\n", result);
    else printf("ipv4-host: PASS\n");
    return result;
}
