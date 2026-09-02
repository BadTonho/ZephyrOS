#include <stdint.h>
#include <stdio.h>

#include "core/dhcp.h"
#include "core/errors.h"
#include "core/ipv4.h"
#include "core/log.h"
#include "core/string.h"
#include "core/udp.h"
#include "core/video.h"
#include "drivers/serial.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define DHCP_FIXED_SIZE 240U
#define DHCP_MAX_MESSAGE_SIZE 548U
#define DHCP_BOOTREPLY 2U
#define DHCP_HTYPE_ETHERNET 1U
#define DHCP_MAC_LENGTH 6U
#define DHCP_MAGIC_COOKIE 0x63825363U
#define DHCP_BROADCAST_FLAG 0x8000U
#define DHCP_OFFSET_OP 0U
#define DHCP_OFFSET_HTYPE 1U
#define DHCP_OFFSET_HLEN 2U
#define DHCP_OFFSET_XID 4U
#define DHCP_OFFSET_YIADDR 16U
#define DHCP_OFFSET_CHADDR 28U
#define DHCP_OFFSET_COOKIE 236U
#define DHCP_OFFSET_OPTIONS 240U
#define DHCP_OPTION_PAD 0U
#define DHCP_OPTION_SUBNET_MASK 1U
#define DHCP_OPTION_ROUTER 3U
#define DHCP_OPTION_DNS 6U
#define DHCP_OPTION_LEASE_TIME 51U
#define DHCP_OPTION_MESSAGE_TYPE 53U
#define DHCP_OPTION_SERVER_ID 54U
#define DHCP_OPTION_T1 58U
#define DHCP_OPTION_T2 59U
#define DHCP_OPTION_END 255U
#define DHCP_MESSAGE_OFFER 2U
#define DHCP_MESSAGE_ACK 5U
#define DHCP_MESSAGE_NAK 6U
#define DHCP_SERVER_IP 0x0A00020FU
#define DHCP_OFFER_IP 0x0A00022AU
#define DHCP_GATEWAY_IP 0x0A000201U
#define DHCP_DNS_IP 0x0A00020EU
#define DHCP_ENDPOINT 0xD601U
#define DHCP_INTERFACE "eth0"
#define DHCP_WRONG_INTERFACE "other0"
#define DHCP_LEASE_SECONDS 12U
#define DHCP_T1_SECONDS 6U
#define DHCP_T2_SECONDS 10U

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
    printf("ZCOV_BEGIN|case=host:network:dhcp|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:network:dhcp|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:network:dhcp|value=0x%08X\n",
           (uint32_t)result);
}

static udp_receive_fn registered_callback;
static udp_endpoint_handle_t bound_endpoint;
static int udp_bind_result;
static int udp_send_result;
static uint8_t udp_send_flag;
static uint8_t transmitted_payload[DHCP_MAX_MESSAGE_SIZE];
static uint16_t transmitted_length;
static uint32_t transmitted_destination;
static uint16_t transmitted_destination_port;
static uint8_t transmitted_broadcast;
static uint32_t fake_ticks;
static uint32_t fake_frequency;
static uint8_t fixture_mac[DHCP_MAC_LENGTH] =
    {0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x03U};

int udp_bind(uint16_t local_port, uint8_t flags, udp_receive_fn callback,
             udp_endpoint_handle_t* out_handle) {
    (void)flags;
    if (!local_port || !callback || !out_handle) return ERR_INVALID;
    if (udp_bind_result != OK) return udp_bind_result;
    registered_callback = callback;
    bound_endpoint = DHCP_ENDPOINT;
    *out_handle = bound_endpoint;
    return OK;
}

int udp_send_limited_broadcast(udp_endpoint_handle_t handle,
                               const char* interface_id,
                               uint32_t source_ip, uint16_t destination_port,
                               const uint8_t* payload,
                               uint16_t payload_length, uint8_t* out_sent) {
    (void)source_ip;
    if (handle != bound_endpoint || !interface_id || !payload || !out_sent ||
        destination_port != DHCP_SERVER_PORT ||
        payload_length > sizeof(transmitted_payload)) return ERR_INVALID;
    if (udp_send_result != OK) return udp_send_result;
    kmemcpy(transmitted_payload, payload, payload_length);
    transmitted_length = payload_length;
    transmitted_destination = 0U;
    transmitted_destination_port = destination_port;
    transmitted_broadcast = 1U;
    *out_sent = udp_send_flag;
    return OK;
}

int udp_send(udp_endpoint_handle_t handle, uint32_t destination_ip,
             uint16_t destination_port, const uint8_t* payload,
             uint16_t payload_length, uint8_t* out_sent) {
    if (handle != bound_endpoint || !payload || !out_sent ||
        destination_port != DHCP_SERVER_PORT ||
        payload_length > sizeof(transmitted_payload)) return ERR_INVALID;
    if (udp_send_result != OK) return udp_send_result;
    kmemcpy(transmitted_payload, payload, payload_length);
    transmitted_length = payload_length;
    transmitted_destination = destination_ip;
    transmitted_destination_port = destination_port;
    transmitted_broadcast = 0U;
    *out_sent = udp_send_flag;
    return OK;
}

int udp_get_status(udp_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    kmemset(out_status, 0, sizeof(*out_status));
    out_status->initialized = 1U;
    return OK;
}

uint8_t ipv4_address_is_unicast(uint32_t ip_address) {
    uint8_t first_octet = (uint8_t)(ip_address >> 24U);

    if (!ip_address || ip_address == 0xFFFFFFFFU || first_octet == 127U ||
        first_octet >= 224U) return 0U;
    return 1U;
}

uint8_t ipv4_mask_is_valid(uint32_t subnet_mask) {
    uint32_t inverted;

    if (!subnet_mask) return 0U;
    inverted = ~subnet_mask;
    return (inverted & (inverted + 1U)) == 0U;
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

static uint32_t read_u32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) | data[3];
}

static uint16_t append_option(uint8_t* packet, uint16_t offset, uint8_t code,
                              const uint8_t* data, uint8_t length) {
    packet[offset++] = code;
    packet[offset++] = length;
    kmemcpy(packet + offset, data, length);
    return (uint16_t)(offset + length);
}

static uint16_t append_u32_option(uint8_t* packet, uint16_t offset,
                                  uint8_t code, uint32_t value) {
    uint8_t data[4];

    write_u32(data, value);
    return append_option(packet, offset, code, data, sizeof(data));
}

static uint16_t make_reply(uint8_t* packet, uint8_t message_type,
                           uint32_t address, uint8_t include_configuration) {
    const uint8_t mask_and_invalid[8] =
        {0U, 0U, 0U, 0U, 0xFFU, 0xFFU, 0xFFU, 0U};
    const uint8_t gateway_list[8] =
        {0U, 0U, 0U, 0U, 10U, 0U, 2U, 1U};
    const uint8_t dns_list[8] =
        {0U, 0U, 0U, 0U, 10U, 0U, 2U, 14U};
    uint16_t offset = DHCP_OFFSET_OPTIONS;
    uint32_t transaction_id = read_u32(transmitted_payload + DHCP_OFFSET_XID);

    kmemset(packet, 0, DHCP_MAX_MESSAGE_SIZE);
    packet[DHCP_OFFSET_OP] = DHCP_BOOTREPLY;
    packet[DHCP_OFFSET_HTYPE] = DHCP_HTYPE_ETHERNET;
    packet[DHCP_OFFSET_HLEN] = DHCP_MAC_LENGTH;
    write_u32(packet + DHCP_OFFSET_XID, transaction_id);
    write_u32(packet + DHCP_OFFSET_YIADDR, address);
    kmemcpy(packet + DHCP_OFFSET_CHADDR, fixture_mac, DHCP_MAC_LENGTH);
    write_u32(packet + DHCP_OFFSET_COOKIE, DHCP_MAGIC_COOKIE);
    offset = append_option(packet, offset, DHCP_OPTION_MESSAGE_TYPE,
                           &message_type, 1U);
    offset = append_u32_option(packet, offset, DHCP_OPTION_SERVER_ID,
                               DHCP_SERVER_IP);
    if (include_configuration) {
        offset = append_option(packet, offset, DHCP_OPTION_SUBNET_MASK,
                               mask_and_invalid + 4U, 4U);
        offset = append_option(packet, offset, DHCP_OPTION_ROUTER,
                               gateway_list, sizeof(gateway_list));
        offset = append_option(packet, offset, DHCP_OPTION_DNS, dns_list,
                               sizeof(dns_list));
        offset = append_u32_option(packet, offset, DHCP_OPTION_LEASE_TIME,
                                   DHCP_LEASE_SECONDS);
    }
    packet[offset++] = DHCP_OPTION_END;
    return offset;
}

static int deliver_packet(const uint8_t* payload, uint16_t length,
                          const char* interface_id, uint32_t source_ip,
                          uint16_t source_port, uint16_t destination_port) {
    udp_datagram_view_t datagram = {
        interface_id, payload, length, source_ip, 0xFFFFFFFFU, source_port,
        destination_port, IPV4_DELIVERY_LIMITED_BROADCAST
    };

    if (!registered_callback) return ERR_STATE;
    return registered_callback(&datagram);
}

static int expect_status(dhcp_state_t state, int last_error) {
    dhcp_status_t status;

    if (dhcp_get_status(&status) != OK) return 0;
    return status.state == state && status.last_error == last_error;
}

static int establish_bound(void) {
    uint8_t packet[DHCP_MAX_MESSAGE_SIZE];
    dhcp_event_t event;
    dhcp_lease_t lease;
    uint16_t length;

    udp_send_result = OK;
    udp_send_flag = 1U;
    if (dhcp_acquire(DHCP_INTERFACE, fixture_mac) != OK) return 0;
    length = make_reply(packet, DHCP_MESSAGE_OFFER, DHCP_OFFER_IP, 0U);
    if (deliver_packet(packet, length, DHCP_INTERFACE, DHCP_SERVER_IP,
                       DHCP_SERVER_PORT, DHCP_CLIENT_PORT) != OK) return 0;
    length = make_reply(packet, DHCP_MESSAGE_ACK, DHCP_OFFER_IP, 1U);
    if (deliver_packet(packet, length, DHCP_INTERFACE, DHCP_SERVER_IP,
                       DHCP_SERVER_PORT, DHCP_CLIENT_PORT) != OK ||
        dhcp_take_event(&event, &lease) != OK ||
        event != DHCP_EVENT_APPLY_LEASE ||
        dhcp_complete_event(event, OK) != OK) return 0;
    return expect_status(DHCP_STATE_BOUND, OK);
}

static int check_dhcp(void) {
    uint8_t packet[DHCP_MAX_MESSAGE_SIZE];
    uint8_t malformed[DHCP_MAX_MESSAGE_SIZE];
    uint8_t sent;
    uint8_t bad_mac[DHCP_MAC_LENGTH] = {1U, 0U, 0U, 0U, 0U, 1U};
    char long_interface[DHCP_INTERFACE_ID_SIZE + 1U];
    dhcp_status_t status;
    dhcp_event_t event;
    dhcp_lease_t lease;
    uint16_t length;

    fake_ticks = 100U;
    fake_frequency = 100U;
    udp_bind_result = ERR_UNAVAILABLE;
    udp_send_result = OK;
    udp_send_flag = 1U;
    registered_callback = NULL;
    bound_endpoint = 0U;
    transmitted_length = 0U;
    if (dhcp_get_status(NULL) != ERR_NULL ||
        dhcp_release(NULL) != ERR_NULL || dhcp_reset() != ERR_STATE ||
        dhcp_maintain() != ERR_STATE ||
        dhcp_acquire(DHCP_INTERFACE, fixture_mac) != ERR_STATE ||
        dhcp_renew() != ERR_STATE || dhcp_release(&sent) != ERR_STATE ||
        dhcp_take_event(&event, &lease) != ERR_STATE ||
        dhcp_complete_event(DHCP_EVENT_NONE, OK) != ERR_STATE ||
        dhcp_validate_state() != OK || dhcp_init() != ERR_UNAVAILABLE ||
        dhcp_validate_state() != OK) return 1;
    udp_bind_result = OK;
    if (dhcp_init() != OK || dhcp_init() != OK || !registered_callback ||
        bound_endpoint != DHCP_ENDPOINT ||
        kstrcmp(dhcp_state_name(DHCP_STATE_IDLE), "IDLE") != 0 ||
        kstrcmp(dhcp_state_name(DHCP_STATE_SELECTING), "SELECTING") != 0 ||
        kstrcmp(dhcp_state_name(DHCP_STATE_REQUESTING), "REQUESTING") != 0 ||
        kstrcmp(dhcp_state_name(DHCP_STATE_APPLYING), "APPLYING") != 0 ||
        kstrcmp(dhcp_state_name(DHCP_STATE_BOUND), "BOUND") != 0 ||
        kstrcmp(dhcp_state_name(DHCP_STATE_RENEWING), "RENEWING") != 0 ||
        kstrcmp(dhcp_state_name(DHCP_STATE_REBINDING), "REBINDING") != 0 ||
        kstrcmp(dhcp_state_name(DHCP_STATE_FAILED), "FAILED") != 0 ||
        kstrcmp(dhcp_state_name(DHCP_STATE_EXPIRED), "EXPIRED") != 0 ||
        kstrcmp(dhcp_state_name((dhcp_state_t)99), "DESCONHECIDO") != 0) {
        return 2;
    }
    for (uint32_t index = 0U; index < DHCP_INTERFACE_ID_SIZE; index++) {
        long_interface[index] = 'x';
    }
    long_interface[DHCP_INTERFACE_ID_SIZE] = '\0';
    if (dhcp_acquire(NULL, fixture_mac) != ERR_NULL ||
        dhcp_acquire(DHCP_INTERFACE, NULL) != ERR_NULL ||
        dhcp_acquire(DHCP_INTERFACE, bad_mac) != ERR_STATE ||
        dhcp_acquire("", fixture_mac) != ERR_INVALID ||
        dhcp_acquire(long_interface, fixture_mac) != ERR_OVERFLOW) return 3;
    if (dhcp_acquire(DHCP_INTERFACE, fixture_mac) != OK ||
        !expect_status(DHCP_STATE_SELECTING, OK) ||
        transmitted_broadcast != 1U || transmitted_length < DHCP_FIXED_SIZE ||
        transmitted_payload[DHCP_OFFSET_OP] != 1U ||
        transmitted_payload[DHCP_OFFSET_HTYPE] != DHCP_HTYPE_ETHERNET ||
        transmitted_payload[DHCP_OFFSET_HLEN] != DHCP_MAC_LENGTH ||
        read_u32(transmitted_payload + DHCP_OFFSET_COOKIE) != DHCP_MAGIC_COOKIE ||
        dhcp_acquire(DHCP_INTERFACE, fixture_mac) != ERR_STATE ||
        dhcp_get_status(&status) != OK || status.discovers_tx != 1U ||
        status.attempts != 1U || dhcp_validate_state() != OK) return 4;
    if (deliver_packet(NULL, 0U, NULL, DHCP_SERVER_IP, DHCP_SERVER_PORT,
                       DHCP_CLIENT_PORT) != OK ||
        deliver_packet(transmitted_payload, transmitted_length,
                       DHCP_WRONG_INTERFACE, DHCP_SERVER_IP,
                       DHCP_SERVER_PORT, DHCP_CLIENT_PORT) != OK ||
        dhcp_get_status(&status) != OK || status.invalid_packets != 1U ||
        status.ignored_packets != 1U) return 5;
    length = make_reply(packet, DHCP_MESSAGE_OFFER, DHCP_OFFER_IP, 0U);
    packet[DHCP_OFFSET_XID + 3U] ^= 1U;
    if (deliver_packet(packet, length, DHCP_INTERFACE, DHCP_SERVER_IP,
                       DHCP_SERVER_PORT, DHCP_CLIENT_PORT) != OK ||
        dhcp_get_status(&status) != OK || status.invalid_packets != 2U) {
        return 6;
    }
    length = make_reply(packet, DHCP_MESSAGE_OFFER, DHCP_OFFER_IP, 0U);
    if (deliver_packet(packet, length - 1U, DHCP_INTERFACE, DHCP_SERVER_IP,
                       DHCP_SERVER_PORT, DHCP_CLIENT_PORT) != OK ||
        dhcp_get_status(&status) != OK || status.invalid_packets != 3U) {
        return 7;
    }
    length = make_reply(packet, DHCP_MESSAGE_OFFER, DHCP_OFFER_IP, 0U);
    if (deliver_packet(packet, length, DHCP_INTERFACE, DHCP_SERVER_IP,
                       DHCP_SERVER_PORT + 1U, DHCP_CLIENT_PORT) != OK ||
        dhcp_get_status(&status) != OK || status.invalid_packets != 4U) {
        return 8;
    }
    length = make_reply(packet, DHCP_MESSAGE_OFFER, DHCP_OFFER_IP, 0U);
    if (deliver_packet(packet, length, DHCP_INTERFACE, DHCP_SERVER_IP,
                       DHCP_SERVER_PORT, DHCP_CLIENT_PORT) != OK ||
        !expect_status(DHCP_STATE_REQUESTING, OK) ||
        transmitted_broadcast != 1U || transmitted_length < DHCP_FIXED_SIZE ||
        dhcp_get_status(&status) != OK || status.offers_rx != 1U ||
        status.requests_tx != 1U) return 9;
    length = make_reply(packet, DHCP_MESSAGE_ACK, DHCP_OFFER_IP, 1U);
    if (deliver_packet(packet, length, DHCP_INTERFACE, DHCP_SERVER_IP,
                       DHCP_SERVER_PORT, DHCP_CLIENT_PORT) != OK ||
        !expect_status(DHCP_STATE_APPLYING, OK) ||
        dhcp_take_event(&event, &lease) != OK ||
        event != DHCP_EVENT_APPLY_LEASE || lease.address != DHCP_OFFER_IP ||
        lease.gateway != DHCP_GATEWAY_IP || lease.dns_server != DHCP_DNS_IP ||
        lease.lease_seconds != DHCP_LEASE_SECONDS ||
        lease.t1_seconds != DHCP_T1_SECONDS ||
        lease.t2_seconds != DHCP_T2_SECONDS ||
        dhcp_complete_event(DHCP_EVENT_NONE, OK) != ERR_INVALID ||
        dhcp_complete_event(event, OK) != OK ||
        !expect_status(DHCP_STATE_BOUND, OK) || dhcp_validate_state() != OK) {
        return 10;
    }
    fake_ticks = 200U;
    if (dhcp_get_status(&status) != OK || status.lease_remaining_seconds !=
            DHCP_LEASE_SECONDS - 1U || status.t1_remaining_seconds !=
            DHCP_T1_SECONDS - 1U || status.t2_remaining_seconds !=
            DHCP_T2_SECONDS - 1U || dhcp_release(NULL) != ERR_NULL) return 11;
    if (dhcp_renew() != OK || !expect_status(DHCP_STATE_RENEWING, OK) ||
        transmitted_broadcast != 0U || transmitted_destination != DHCP_SERVER_IP ||
        dhcp_get_status(&status) != OK || status.requests_tx != 2U) return 12;
    length = make_reply(packet, DHCP_MESSAGE_ACK, 0U, 1U);
    if (deliver_packet(packet, length, DHCP_INTERFACE, DHCP_SERVER_IP,
                       DHCP_SERVER_PORT, DHCP_CLIENT_PORT) != OK ||
        dhcp_take_event(&event, &lease) != OK ||
        event != DHCP_EVENT_APPLY_LEASE || lease.address != DHCP_OFFER_IP ||
        dhcp_complete_event(event, OK) != OK ||
        !expect_status(DHCP_STATE_BOUND, OK)) return 13;
    if (dhcp_release(&sent) != OK || !sent || transmitted_broadcast ||
        dhcp_take_event(&event, &lease) != OK ||
        event != DHCP_EVENT_DROP_LEASE || dhcp_complete_event(event, OK) != OK ||
        !expect_status(DHCP_STATE_IDLE, OK) || dhcp_release(&sent) != ERR_STATE) {
        return 14;
    }
    if (!establish_bound()) return 15;
    udp_send_result = ERR_TIMEOUT;
    if (dhcp_renew() != OK || !expect_status(DHCP_STATE_RENEWING,
                                               ERR_TIMEOUT)) return 16;
    udp_send_result = OK;
    if (dhcp_reset() != OK || !expect_status(DHCP_STATE_IDLE, OK)) return 17;
    if (!establish_bound()) return 18;
    length = make_reply(packet, DHCP_MESSAGE_NAK, 0U, 0U);
    if (dhcp_renew() != OK ||
        deliver_packet(packet, length, DHCP_INTERFACE, DHCP_SERVER_IP,
                       DHCP_SERVER_PORT, DHCP_CLIENT_PORT) != OK ||
        !expect_status(DHCP_STATE_APPLYING, OK) ||
        dhcp_get_status(&status) != OK || status.naks_rx == 0U ||
        dhcp_take_event(&event, &lease) != OK ||
        event != DHCP_EVENT_DROP_LEASE || dhcp_complete_event(event,
                                                               ERR_CANCELLED) != OK ||
        !expect_status(DHCP_STATE_FAILED, ERR_CANCELLED) ||
        dhcp_release(&sent) != OK ||
        !expect_status(DHCP_STATE_IDLE, ERR_CANCELLED) ||
        dhcp_reset() != OK) {
        return 19;
    }
    if (dhcp_acquire(DHCP_INTERFACE, fixture_mac) != OK) return 20;
    fake_ticks += 200U;
    if (dhcp_maintain() != OK) return 21;
    fake_ticks += 400U;
    if (dhcp_maintain() != OK) return 22;
    fake_ticks += 100U;
    if (dhcp_maintain() != OK) return 23;
    fake_ticks += 400U;
    if (dhcp_maintain() != OK) return 24;
    fake_ticks += 100U;
    if (dhcp_maintain() != OK ||
        !expect_status(DHCP_STATE_FAILED, ERR_TIMEOUT) ||
        dhcp_get_status(&status) != OK || status.timeouts == 0U ||
        dhcp_release(&sent) != OK) {
        return 25;
    }
    udp_send_flag = 0U;
    if (dhcp_acquire(DHCP_INTERFACE, fixture_mac) != ERR_STATE ||
        !expect_status(DHCP_STATE_FAILED, ERR_STATE) ||
        dhcp_release(&sent) != OK) return 26;
    udp_send_flag = 1U;
    fake_frequency = 0U;
    if (dhcp_acquire(DHCP_INTERFACE, fixture_mac) != ERR_STATE ||
        !expect_status(DHCP_STATE_FAILED, ERR_STATE) ||
        dhcp_release(&sent) != OK) return 27;
    fake_frequency = 100U;
    if (!establish_bound()) return 28;
    fake_ticks += 600U;
    if (dhcp_maintain() != OK || !expect_status(DHCP_STATE_RENEWING, OK) ||
        transmitted_broadcast != 0U) return 29;
    fake_ticks += 400U;
    if (dhcp_maintain() != OK || !expect_status(DHCP_STATE_REBINDING, OK) ||
        !transmitted_broadcast) return 30;
    fake_ticks += 400U;
    if (dhcp_maintain() != OK || !expect_status(DHCP_STATE_APPLYING, ERR_TIMEOUT) ||
        dhcp_take_event(&event, &lease) != OK ||
        event != DHCP_EVENT_DROP_LEASE || dhcp_complete_event(event, OK) != OK ||
        !expect_status(DHCP_STATE_EXPIRED, ERR_TIMEOUT) ||
        dhcp_validate_state() != OK) return 30;
    if (!establish_bound()) return 31;
    if (dhcp_renew() != OK) return 32;
    length = make_reply(packet, DHCP_MESSAGE_ACK, 0U, 1U);
    if (deliver_packet(packet, length, DHCP_INTERFACE, DHCP_SERVER_IP,
                       DHCP_SERVER_PORT, DHCP_CLIENT_PORT) != OK ||
        dhcp_take_event(&event, &lease) != OK ||
        dhcp_complete_event(event, ERR_UNAVAILABLE) != OK ||
        !expect_status(DHCP_STATE_BOUND, ERR_UNAVAILABLE) ||
        dhcp_reset() != OK) return 33;
    if (!establish_bound()) return 34;
    if (dhcp_renew() != OK) return 35;
    length = make_reply(packet, DHCP_MESSAGE_ACK, 0U, 1U);
    packet[DHCP_OFFSET_CHADDR] ^= 1U;
    if (deliver_packet(packet, length, DHCP_INTERFACE, DHCP_SERVER_IP,
                       DHCP_SERVER_PORT, DHCP_CLIENT_PORT) != OK ||
        dhcp_get_status(&status) != OK || status.invalid_packets == 0U ||
        dhcp_reset() != OK || dhcp_take_event(NULL, &lease) != ERR_NULL ||
        dhcp_take_event(&event, NULL) != ERR_NULL ||
        dhcp_complete_event(DHCP_EVENT_NONE, OK) != ERR_INVALID ||
        dhcp_validate_state() != OK) return 36;
    length = make_reply(packet, DHCP_MESSAGE_OFFER, DHCP_OFFER_IP, 0U);
    packet[DHCP_OFFSET_OPTIONS + 1U] = 2U;
    if (dhcp_acquire(DHCP_INTERFACE, fixture_mac) != OK ||
        deliver_packet(packet, length, DHCP_INTERFACE, DHCP_SERVER_IP,
                       DHCP_SERVER_PORT, DHCP_CLIENT_PORT) != OK ||
        dhcp_get_status(&status) != OK || status.invalid_packets == 0U ||
        dhcp_release(&sent) != OK || dhcp_validate_state() != OK) return 37;
    kmemcpy(malformed, transmitted_payload, transmitted_length);
    malformed[DHCP_OFFSET_COOKIE] ^= 1U;
    if (dhcp_acquire(DHCP_INTERFACE, fixture_mac) != OK ||
        deliver_packet(malformed, transmitted_length, DHCP_INTERFACE,
                       DHCP_SERVER_IP, DHCP_SERVER_PORT, DHCP_CLIENT_PORT) != OK ||
        dhcp_get_status(&status) != OK || status.invalid_packets == 0U ||
        dhcp_release(&sent) != OK || dhcp_validate_state() != OK) return 38;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    log_init();
    result = check_dhcp();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
