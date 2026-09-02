#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/icmp.h"
#include "core/ipv4.h"
#include "core/log.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/video.h"
#include "drivers/serial.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define ICMP_TYPE_ECHO_REPLY 0U
#define ICMP_TYPE_ECHO_REQUEST 8U
#define ICMP_HEADER_SIZE 8U
#define ICMP_PAYLOAD_SIZE ICMP_ECHO_MESSAGE_SIZE
#define ICMP_SOURCE_IP 0x0A00020FU
#define ICMP_LOCAL_IP 0x0A000203U
#define ICMP_TTL 64U

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
    printf("ZCOV_BEGIN|case=host:network:icmp|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:network:icmp|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:network:icmp|value=0x%08X\n",
           (uint32_t)result);
}

static ipv4_protocol_handler_fn registered_handler;
static timer_callback_fn timer_callback;
static void* timer_context;
static timer_owner_handle_t fixture_owner = 7U;
static timer_handle_t fixture_timer = 11U;
static timer_state_t fixture_timer_state = TIMER_STATE_IDLE;
static timer_mode_t fixture_timer_mode = TIMER_MODE_ONE_SHOT;
static ipv4_status_t ipv4_fixture_status;
static uint8_t transmitted_message[IPV4_MAX_PAYLOAD_SIZE];
static uint16_t transmitted_length;
static uint32_t transmitted_destination;
static uint8_t ipv4_send_sent;
static int ipv4_send_result;
static int timer_start_result;
static uint32_t fake_ticks = 100U;

int ipv4_register_handler(uint8_t protocol,
                          ipv4_protocol_handler_fn handler) {
    if (protocol != IPV4_PROTOCOL_ICMP || !handler) return ERR_INVALID;
    registered_handler = handler;
    return OK;
}

int ipv4_get_status(ipv4_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = ipv4_fixture_status;
    return OK;
}

uint8_t ipv4_address_is_unicast(uint32_t ip_address) {
    uint8_t first_octet = (uint8_t)(ip_address >> 24U);

    return ip_address != 0U && ip_address != IPV4_LIMITED_BROADCAST &&
           first_octet != 127U && first_octet < 224U;
}

int ipv4_send(uint32_t destination_ip, uint8_t protocol,
              const uint8_t* payload, uint16_t payload_length,
              uint8_t* out_sent) {
    if (protocol != IPV4_PROTOCOL_ICMP || !payload || !out_sent ||
        payload_length > sizeof(transmitted_message)) return ERR_INVALID;
    transmitted_destination = destination_ip;
    transmitted_length = payload_length;
    kmemcpy(transmitted_message, payload, payload_length);
    *out_sent = ipv4_send_sent;
    return ipv4_send_result;
}

int timer_owner_create(const char* name, timer_owner_handle_t* out_owner) {
    if (!name || !out_owner) return ERR_NULL;
    *out_owner = fixture_owner;
    return OK;
}

int timer_owner_destroy(timer_owner_handle_t owner) {
    return owner == fixture_owner ? OK : ERR_INVALID;
}

int timer_create(timer_owner_handle_t owner, const char* name,
                 timer_callback_fn callback, void* context,
                 timer_handle_t* out_timer) {
    if (owner != fixture_owner || !name || !callback || !out_timer) {
        return ERR_INVALID;
    }
    timer_callback = callback;
    timer_context = context;
    *out_timer = fixture_timer;
    fixture_timer_state = TIMER_STATE_IDLE;
    return OK;
}

int timer_start_once(timer_handle_t timer, uint32_t milliseconds) {
    if (timer != fixture_timer || !milliseconds) return ERR_INVALID;
    if (timer_start_result != OK) return timer_start_result;
    fixture_timer_mode = TIMER_MODE_ONE_SHOT;
    fixture_timer_state = TIMER_STATE_ARMED;
    return OK;
}

int timer_cancel(timer_handle_t timer) {
    if (timer != fixture_timer) return ERR_INVALID;
    fixture_timer_state = TIMER_STATE_IDLE;
    return OK;
}

int timer_get_info(timer_handle_t timer, timer_info_t* out_info) {
    if (timer != fixture_timer || !out_info) return ERR_INVALID;
    kmemset(out_info, 0, sizeof(*out_info));
    out_info->handle = fixture_timer;
    out_info->owner = fixture_owner;
    out_info->mode = fixture_timer_mode;
    out_info->state = fixture_timer_state;
    return OK;
}

uint32_t timer_get_ticks(void) {
    return fake_ticks;
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

static uint16_t read_u16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

static void write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

static uint16_t checksum(const uint8_t* data, uint16_t length) {
    uint32_t sum = 0U;

    while (length >= 2U) {
        sum += read_u16(data);
        data += 2U;
        length -= 2U;
    }
    if (length) sum += (uint16_t)((uint16_t)data[0] << 8U);
    while (sum >> 16U) sum = (sum & 0xFFFFU) + (sum >> 16U);
    return (uint16_t)~sum;
}

static void finalize_packet(uint8_t* packet, uint8_t type,
                            uint16_t identifier, uint16_t sequence) {
    packet[0] = type;
    packet[1] = 0U;
    write_u16(packet + 2U, 0U);
    write_u16(packet + 4U, identifier);
    write_u16(packet + 6U, sequence);
    for (uint32_t index = ICMP_HEADER_SIZE; index < ICMP_PAYLOAD_SIZE;
         index++) {
        packet[index] = (uint8_t)(index ^ identifier ^ sequence);
    }
    write_u16(packet + 2U, checksum(packet, ICMP_PAYLOAD_SIZE));
}

static int invoke_packet(const uint8_t* payload, uint16_t payload_length,
                         uint32_t source_ip) {
    ipv4_packet_view_t packet;

    if (!registered_handler) return ERR_STATE;
    packet.interface_id = "eth0";
    packet.payload = payload;
    packet.payload_length = payload_length;
    packet.source_ip = source_ip;
    packet.destination_ip = ICMP_LOCAL_IP;
    packet.identification = 1U;
    packet.protocol = IPV4_PROTOCOL_ICMP;
    packet.ttl = ICMP_TTL;
    packet.delivery = IPV4_DELIVERY_LOCAL_UNICAST;
    return registered_handler(&packet);
}

static int bytes_equal(const uint8_t* first, const uint8_t* second,
                       uint32_t length) {
    for (uint32_t index = 0U; index < length; index++) {
        if (first[index] != second[index]) return 0;
    }
    return 1;
}

static int check_icmp(void) {
    uint8_t packet[ICMP_PAYLOAD_SIZE];
    uint8_t reply[ICMP_PAYLOAD_SIZE];
    icmp_status_t status;
    ipv4_status_t saved_ipv4;

    kmemset(&ipv4_fixture_status, 0, sizeof(ipv4_fixture_status));
    ipv4_fixture_status.initialized = 1U;
    ipv4_fixture_status.configured = 1U;
    ipv4_fixture_status.local_ip = ICMP_LOCAL_IP;
    ipv4_fixture_status.configuration_generation = 1U;
    registered_handler = NULL;
    timer_callback = NULL;
    timer_context = NULL;
    fixture_timer_state = TIMER_STATE_IDLE;
    fixture_timer_mode = TIMER_MODE_ONE_SHOT;
    ipv4_send_result = OK;
    ipv4_send_sent = 1U;
    timer_start_result = OK;
    fake_ticks = 100U;
    if (icmp_get_status(NULL) != ERR_NULL || icmp_validate_state() != OK ||
        kstrcmp(icmp_ping_state_name(ICMP_PING_IDLE), "IDLE") != 0 ||
        kstrcmp(icmp_ping_state_name(ICMP_PING_RESOLVING), "RESOLVING") != 0 ||
        kstrcmp(icmp_ping_state_name(ICMP_PING_WAITING_REPLY),
                "WAITING_REPLY") != 0 ||
        kstrcmp(icmp_ping_state_name(ICMP_PING_COMPLETE), "COMPLETE") != 0 ||
        kstrcmp(icmp_ping_state_name(ICMP_PING_FAILED), "FAILED") != 0 ||
        kstrcmp(icmp_ping_state_name((icmp_ping_state_t)99), "INVALID") != 0) {
        return 1;
    }
    if (icmp_ping_start(ICMP_SOURCE_IP, 1U, 1U) != ERR_STATE ||
        icmp_init() != OK || icmp_init() != OK || !registered_handler ||
        !timer_callback || timer_context == NULL ||
        icmp_get_status(&status) != OK || status.state != ICMP_PING_IDLE ||
        icmp_validate_state() != OK) return 2;
    if (icmp_ping_start(0U, 1U, 1U) != ERR_INVALID ||
        icmp_ping_start(0xE0000001U, 1U, 1U) != ERR_INVALID ||
        icmp_ping_start(ICMP_LOCAL_IP, 1U, 1U) != ERR_STATE ||
        icmp_ping_start(ICMP_SOURCE_IP, 0U, 1U) != ERR_INVALID ||
        icmp_ping_start(ICMP_SOURCE_IP, ICMP_PING_MAX_COUNT + 1U, 1U) !=
            ERR_INVALID || icmp_ping_start(ICMP_SOURCE_IP, 1U, 0U) !=
            ERR_INVALID) return 3;
    if (icmp_ping_start(ICMP_SOURCE_IP, 1U, 1U) != OK ||
        icmp_get_status(&status) != OK || status.state != ICMP_PING_RESOLVING ||
        icmp_maintain() != OK || status.state == ICMP_PING_COMPLETE ||
        transmitted_destination != ICMP_SOURCE_IP ||
        transmitted_length != ICMP_ECHO_MESSAGE_SIZE ||
        transmitted_message[0] != ICMP_TYPE_ECHO_REQUEST ||
        checksum(transmitted_message, transmitted_length) != 0U ||
        icmp_get_status(&status) != OK ||
        status.state != ICMP_PING_WAITING_REPLY || status.sent != 1U ||
        fixture_timer_state != TIMER_STATE_ARMED || icmp_validate_state() != OK) {
        return 4;
    }
    kmemcpy(reply, transmitted_message, sizeof(reply));
    reply[0] = ICMP_TYPE_ECHO_REPLY;
    write_u16(reply + 2U, 0U);
    write_u16(reply + 2U, checksum(reply, sizeof(reply)));
    fake_ticks = 107U;
    fixture_timer_state = TIMER_STATE_IDLE;
    if (invoke_packet(reply, sizeof(reply), ICMP_SOURCE_IP) != OK ||
        icmp_get_status(&status) != OK || status.state != ICMP_PING_COMPLETE ||
        status.received != 1U || status.last_event != ICMP_PING_EVENT_REPLY ||
        status.last_rtt_ticks != 7U || status.last_reply_ttl != ICMP_TTL ||
        icmp_validate_state() != OK) return 5;
    if (icmp_reset() != OK || icmp_get_status(&status) != OK ||
        status.state != ICMP_PING_IDLE || status.received != 0U ||
        fixture_timer_state != TIMER_STATE_IDLE || icmp_validate_state() != OK) {
        return 6;
    }
    if (icmp_ping_start(ICMP_SOURCE_IP, 2U, 1U) != OK ||
        icmp_maintain() != OK || status.state == ICMP_PING_COMPLETE ||
        icmp_get_status(&status) != OK || status.state != ICMP_PING_WAITING_REPLY ||
        !timer_callback || timer_callback(fixture_timer + 1U, timer_context) !=
            ERR_INVALID) return 7;
    fixture_timer_state = TIMER_STATE_IDLE;
    if (timer_callback(fixture_timer, timer_context) != OK ||
        icmp_get_status(&status) != OK || status.state != ICMP_PING_RESOLVING ||
        status.timeouts != 1U || status.last_event != ICMP_PING_EVENT_TIMEOUT ||
        icmp_validate_state() != OK || icmp_maintain() != OK) return 8;
    saved_ipv4 = ipv4_fixture_status;
    ipv4_fixture_status.configuration_generation = 2U;
    if (icmp_maintain() != ERR_STATE || icmp_get_status(&status) != OK ||
        status.state != ICMP_PING_FAILED || status.last_error != ERR_STATE) {
        return 9;
    }
    ipv4_fixture_status = saved_ipv4;
    if (icmp_reset() != OK) return 10;
    finalize_packet(packet, ICMP_TYPE_ECHO_REQUEST, 4U, 2U);
    ipv4_send_sent = 1U;
    finalize_packet(reply, ICMP_TYPE_ECHO_REPLY, 4U, 2U);
    if (invoke_packet(packet, sizeof(packet), ICMP_SOURCE_IP) != OK ||
        icmp_get_status(&status) != OK || status.echo_requests_rx != 1U ||
        status.echo_replies_tx != 1U || status.reply_pending ||
        !bytes_equal(transmitted_message, reply, sizeof(reply)) ||
        icmp_maintain() != OK) return 11;
    ipv4_send_sent = 0U;
    if (invoke_packet(packet, sizeof(packet), ICMP_SOURCE_IP) != OK ||
        invoke_packet(packet, sizeof(packet), ICMP_SOURCE_IP) != OK ||
        icmp_get_status(&status) != OK || status.pending_reply_drops != 1U ||
        !status.reply_pending || ipv4_send_sent != 0U) return 12;
    ipv4_send_sent = 1U;
    if (icmp_maintain() != OK || icmp_get_status(&status) != OK ||
        status.reply_pending) return 13;
    packet[2] ^= 0x01U;
    if (invoke_packet(packet, sizeof(packet), ICMP_SOURCE_IP) != OK ||
        icmp_get_status(&status) != OK || status.invalid_packets == 0U) return 14;
    if (invoke_packet(packet, ICMP_HEADER_SIZE - 1U, ICMP_SOURCE_IP) != OK ||
        icmp_get_status(&status) != OK || status.invalid_packets < 2U) return 15;
    finalize_packet(packet, ICMP_TYPE_ECHO_REQUEST, 4U, 2U);
    packet[1] = 1U;
    write_u16(packet + 2U, 0U);
    write_u16(packet + 2U, checksum(packet, sizeof(packet)));
    if (invoke_packet(packet, sizeof(packet), ICMP_SOURCE_IP) != OK ||
        icmp_get_status(&status) != OK || status.invalid_packets < 3U) return 16;
    finalize_packet(packet, 3U, 4U, 2U);
    if (invoke_packet(packet, sizeof(packet), ICMP_SOURCE_IP) != OK ||
        icmp_get_status(&status) != OK || status.ignored_packets == 0U) return 17;
    if (icmp_reset() != OK || icmp_get_status(&status) != OK ||
        status.state != ICMP_PING_IDLE || status.reply_pending ||
        icmp_validate_state() != OK) return 18;
    timer_start_result = ERR_TIMEOUT;
    if (icmp_ping_start(ICMP_SOURCE_IP, 1U, 1U) != OK ||
        icmp_maintain() != ERR_TIMEOUT || icmp_get_status(&status) != OK ||
        status.state != ICMP_PING_FAILED || status.last_error != ERR_TIMEOUT ||
        icmp_reset() != OK) return 19;
    ipv4_send_result = ERR_UNAVAILABLE;
    timer_start_result = OK;
    if (icmp_ping_start(ICMP_SOURCE_IP, 1U, 1U) != OK ||
        icmp_maintain() != ERR_UNAVAILABLE || icmp_get_status(&status) != OK ||
        status.state != ICMP_PING_FAILED || status.last_error != ERR_UNAVAILABLE ||
        icmp_reset() != OK) return 20;
    ipv4_send_result = OK;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    log_init();
    result = check_icmp();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
