#include <stdint.h>
#include <stdio.h>

#include "core/arp.h"
#include "core/errors.h"
#include "core/ethernet.h"
#include "core/log.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/video.h"
#include "drivers/serial.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define ARP_ETHERTYPE 0x0806U
#define ARP_OPERATION_REQUEST 0x0001U
#define ARP_OPERATION_REPLY 0x0002U
#define ARP_PACKET_SIZE 28U
#define ARP_HARDWARE_ETHERNET 0x0001U
#define ARP_PROTOCOL_IPV4 0x0800U
#define ARP_BROADCAST_OCTET 0xFFU
#define ARP_OFFSET_HARDWARE_TYPE 0U
#define ARP_OFFSET_PROTOCOL_TYPE 2U
#define ARP_OFFSET_HARDWARE_SIZE 4U
#define ARP_OFFSET_PROTOCOL_SIZE 5U
#define ARP_OFFSET_OPERATION 6U
#define ARP_OFFSET_SENDER_MAC 8U
#define ARP_OFFSET_SENDER_IP 14U
#define ARP_OFFSET_TARGET_MAC 18U
#define ARP_OFFSET_TARGET_IP 24U

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
    printf("ZCOV_BEGIN|case=host:network:arp|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:network:arp|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:network:arp|value=0x%08X\n",
           (uint32_t)result);
}

static ethernet_protocol_handler_fn registered_handler;
static uint8_t transmitted_packet[ARP_PACKET_SIZE];
static uint16_t transmitted_length;
static uint16_t transmitted_ethertype;
static uint8_t transmitted_destination[ARP_MAC_ADDRESS_SIZE];
static int ethernet_send_result;
static uint32_t fake_ticks = 100U;
static uint32_t fake_frequency = 10U;

int ethernet_register_handler(uint16_t ethertype,
                              ethernet_protocol_handler_fn handler) {
    if (ethertype != ARP_ETHERTYPE || !handler) return ERR_INVALID;
    registered_handler = handler;
    return OK;
}

int ethernet_send(const char* interface_id, const uint8_t* destination,
                  uint16_t ethertype, const uint8_t* payload,
                  uint16_t payload_length) {
    if (!interface_id || !destination || !payload ||
        ethertype != ARP_ETHERTYPE || payload_length != ARP_PACKET_SIZE) {
        return ERR_INVALID;
    }
    transmitted_ethertype = ethertype;
    transmitted_length = payload_length;
    kmemcpy(transmitted_destination, destination,
            sizeof(transmitted_destination));
    kmemcpy(transmitted_packet, payload, sizeof(transmitted_packet));
    return ethernet_send_result;
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

static int bytes_equal(const uint8_t* first, const uint8_t* second,
                       uint32_t length) {
    for (uint32_t index = 0U; index < length; index++) {
        if (first[index] != second[index]) return 0;
    }
    return 1;
}

static void build_packet(uint8_t* packet, uint16_t operation,
                         const uint8_t* sender_mac, uint32_t sender_ip,
                         const uint8_t* target_mac, uint32_t target_ip) {
    kmemset(packet, 0, ARP_PACKET_SIZE);
    write_u16(packet + ARP_OFFSET_HARDWARE_TYPE, ARP_HARDWARE_ETHERNET);
    write_u16(packet + ARP_OFFSET_PROTOCOL_TYPE, ARP_PROTOCOL_IPV4);
    packet[ARP_OFFSET_HARDWARE_SIZE] = 6U;
    packet[ARP_OFFSET_PROTOCOL_SIZE] = 4U;
    write_u16(packet + ARP_OFFSET_OPERATION, operation);
    kmemcpy(packet + ARP_OFFSET_SENDER_MAC, sender_mac,
            ARP_MAC_ADDRESS_SIZE);
    write_u32(packet + ARP_OFFSET_SENDER_IP, sender_ip);
    kmemcpy(packet + ARP_OFFSET_TARGET_MAC, target_mac,
            ARP_MAC_ADDRESS_SIZE);
    write_u32(packet + ARP_OFFSET_TARGET_IP, target_ip);
}

static ethernet_frame_view_t frame_for(uint8_t* packet,
                                       const char* interface_id,
                                       const uint8_t* destination,
                                       const uint8_t* source,
                                       ethernet_destination_t destination_type,
                                       uint16_t payload_length) {
    ethernet_frame_view_t frame;

    frame.interface_id = interface_id;
    frame.destination = destination;
    frame.source = source;
    frame.payload = packet;
    frame.payload_length = payload_length;
    frame.ethertype = ARP_ETHERTYPE;
    frame.destination_type = destination_type;
    return frame;
}

static int expect_status(uint32_t expected_entries,
                         uint32_t expected_incomplete,
                         uint32_t expected_resolved,
                         uint32_t expected_failed) {
    arp_status_t status;

    if (arp_get_status(&status) != OK) return 0;
    return status.cache_entries == expected_entries &&
           status.incomplete_entries == expected_incomplete &&
           status.resolved_entries == expected_resolved &&
           status.failed_entries == expected_failed;
}

static int check_arp(void) {
    const uint8_t local_mac[ARP_MAC_ADDRESS_SIZE] =
        {0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x03U};
    const uint8_t remote_mac[ARP_MAC_ADDRESS_SIZE] =
        {0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x0FU};
    const uint8_t group_mac[ARP_MAC_ADDRESS_SIZE] =
        {0x03U, 0x00U, 0x00U, 0x00U, 0x00U, 0x0FU};
    const uint8_t zero_mac[ARP_MAC_ADDRESS_SIZE] = {0U};
    const uint8_t broadcast_mac[ARP_MAC_ADDRESS_SIZE] =
        {ARP_BROADCAST_OCTET, ARP_BROADCAST_OCTET, ARP_BROADCAST_OCTET,
         ARP_BROADCAST_OCTET, ARP_BROADCAST_OCTET, ARP_BROADCAST_OCTET};
    const uint32_t local_ip = 0x0A000203U;
    const uint32_t remote_ip = 0x0A00020FU;
    uint8_t packet[ARP_PACKET_SIZE];
    uint8_t resolved_mac[ARP_MAC_ADDRESS_SIZE];
    uint8_t resolved;
    arp_cache_entry_info_t entry;
    arp_status_t status;
    ethernet_frame_view_t frame;

    ethernet_send_result = OK;
    fake_ticks = 100U;
    fake_frequency = 10U;
    registered_handler = NULL;
    transmitted_length = 0U;
    if (arp_get_status(NULL) != ERR_NULL || arp_get_cache_entry(0U, NULL) !=
            ERR_NULL || arp_get_cache_entry(ARP_CACHE_CAPACITY, &entry) !=
            ERR_INVALID || arp_validate_state() != OK ||
        arp_configure("eth0", local_mac, local_ip) != ERR_STATE) return 1;
    if (!arp_ipv4_is_valid(local_ip) || !arp_ipv4_is_valid(remote_ip) ||
        arp_ipv4_is_valid(0U) || arp_ipv4_is_valid(0xFFFFFFFFU) ||
        arp_ipv4_is_valid(0x7F000001U) || arp_ipv4_is_valid(0xE0000001U) ||
        kstrcmp(arp_entry_state_name(ARP_ENTRY_INCOMPLETE), "INCOMPLETE") != 0 ||
        kstrcmp(arp_entry_state_name(ARP_ENTRY_RESOLVED), "RESOLVED") != 0 ||
        kstrcmp(arp_entry_state_name(ARP_ENTRY_FAILED), "FAILED") != 0 ||
        kstrcmp(arp_entry_state_name((arp_entry_state_t)99), "INVALID") != 0) {
        return 2;
    }
    if (arp_init() != OK || arp_init() != OK || !registered_handler ||
        arp_configure(NULL, local_mac, local_ip) != ERR_NULL ||
        arp_configure("eth0", NULL, local_ip) != ERR_NULL ||
        arp_configure("eth0", local_mac, 0U) != ERR_INVALID ||
        arp_configure("eth0", zero_mac, local_ip) != ERR_INVALID ||
        arp_configure("eth0", group_mac, local_ip) != ERR_INVALID ||
        arp_configure("", local_mac, local_ip) != ERR_INVALID ||
        arp_configure("abcdefghijklmnopqrst", local_mac, local_ip) !=
            ERR_OVERFLOW) return 3;
    if (arp_configure("eth0", local_mac, local_ip) != OK ||
        arp_validate_state() != OK || !expect_status(0U, 0U, 0U, 0U)) {
        return 4;
    }
    if (arp_resolve(local_ip, resolved_mac, &resolved) != OK || !resolved ||
        kstrcmp(arp_entry_state_name(ARP_ENTRY_RESOLVED), "RESOLVED") != 0 ||
        kstrcmp(arp_entry_state_name(ARP_ENTRY_INCOMPLETE), "INCOMPLETE") != 0 ||
        !bytes_equal(resolved_mac, local_mac, ARP_MAC_ADDRESS_SIZE)) {
        return 5;
    }
    if (arp_resolve(remote_ip, resolved_mac, &resolved) != OK || resolved ||
        transmitted_length != ARP_PACKET_SIZE || transmitted_ethertype !=
            ARP_ETHERTYPE || !bytes_equal(transmitted_destination,
                                          broadcast_mac,
                                          ARP_MAC_ADDRESS_SIZE) ||
        transmitted_packet[ARP_OFFSET_OPERATION + 1U] !=
            ARP_OPERATION_REQUEST || !expect_status(1U, 1U, 0U, 0U)) {
        return 6;
    }
    if (arp_resolve(remote_ip, resolved_mac, &resolved) != OK || resolved ||
        arp_get_cache_entry(0U, &entry) != OK || !entry.used ||
        entry.state != ARP_ENTRY_INCOMPLETE || entry.attempts != 1U) {
        return 7;
    }
    fake_ticks = 110U;
    if (arp_maintain() != OK || arp_get_cache_entry(0U, &entry) != OK ||
        entry.attempts != 2U) return 8;
    fake_ticks = 120U;
    if (arp_maintain() != OK || arp_get_cache_entry(0U, &entry) != OK ||
        entry.attempts != 3U) return 9;
    fake_ticks = 130U;
    if (arp_maintain() != OK || arp_get_cache_entry(0U, &entry) != OK ||
        entry.state != ARP_ENTRY_FAILED || !expect_status(1U, 0U, 0U, 1U) ||
        arp_resolve(remote_ip, resolved_mac, &resolved) != ERR_TIMEOUT) {
        return 10;
    }
    if (arp_clear() != OK || arp_validate_state() != OK ||
        !expect_status(0U, 0U, 0U, 0U)) return 11;
    build_packet(packet, ARP_OPERATION_REQUEST, remote_mac, remote_ip,
                 zero_mac, local_ip);
    frame = frame_for(packet, "eth0", broadcast_mac, remote_mac,
                      ETHERNET_DESTINATION_BROADCAST, ARP_PACKET_SIZE);
    if (registered_handler(&frame) != OK ||
        transmitted_packet[ARP_OFFSET_OPERATION + 1U] != ARP_OPERATION_REPLY ||
        !bytes_equal(transmitted_destination, remote_mac,
                     ARP_MAC_ADDRESS_SIZE) ||
        arp_get_status(&status) != OK || status.rx_requests != 1U ||
        status.tx_replies != 1U || !expect_status(1U, 0U, 1U, 0U)) return 12;
    if (arp_resolve(remote_ip, resolved_mac, &resolved) != OK || !resolved ||
        !bytes_equal(resolved_mac, remote_mac, ARP_MAC_ADDRESS_SIZE) ||
        arp_clear() != OK) return 13;
    if (arp_resolve(remote_ip, resolved_mac, &resolved) != OK || resolved) {
        return 14;
    }
    build_packet(packet, ARP_OPERATION_REPLY, remote_mac, remote_ip,
                 local_mac, local_ip);
    frame = frame_for(packet, "eth0", local_mac, remote_mac,
                      ETHERNET_DESTINATION_LOCAL_UNICAST, ARP_PACKET_SIZE);
    if (registered_handler(&frame) != OK ||
        arp_resolve(remote_ip, resolved_mac, &resolved) != OK || !resolved ||
        !bytes_equal(resolved_mac, remote_mac, ARP_MAC_ADDRESS_SIZE)) return 15;
    if (registered_handler(NULL) != OK) return 16;
    frame.interface_id = "other0";
    if (registered_handler(&frame) != OK) return 17;
    frame.interface_id = "eth0";
    frame.payload_length = ARP_PACKET_SIZE - 1U;
    if (registered_handler(&frame) != OK) return 18;
    frame.payload_length = ARP_PACKET_SIZE;
    build_packet(packet, ARP_OPERATION_REQUEST, group_mac, remote_ip,
                 zero_mac, local_ip);
    if (registered_handler(&frame) != OK || arp_validate_state() != OK) {
        return 19;
    }
    if (arp_unconfigure() != OK || arp_clear() != ERR_STATE ||
        arp_validate_state() != OK || arp_get_status(&status) != OK ||
        status.configured || status.cache_entries != 0U) return 20;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    log_init();
    result = check_arp();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
