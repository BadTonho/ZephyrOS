#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/ipv4.h"
#include "core/log.h"
#include "core/string.h"
#include "core/tcp.h"
#include "core/video.h"
#include "drivers/serial.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define TCP_HEADER_SIZE 20U
#define TCP_SYN_SIZE 24U
#define TCP_MAX_SEGMENT_SIZE 1024U
#define TCP_SERVER_IP 0x0A00020FU
#define TCP_LOCAL_IP 0x0A000202U
#define TCP_SERVER_PORT 80U
#define TCP_CLIENT_PORT 49152U
#define TCP_OTHER_PORT 49153U
#define TCP_TEST_MSS 1460U
#define TCP_FLAG_FIN 0x01U
#define TCP_FLAG_SYN 0x02U
#define TCP_FLAG_RST 0x04U
#define TCP_FLAG_PSH 0x08U
#define TCP_FLAG_ACK 0x10U
#define TCP_OPTION_MSS 2U
#define TCP_OPTION_MSS_LENGTH 4U
#define TCP_OFFSET_SOURCE_PORT 0U
#define TCP_OFFSET_DESTINATION_PORT 2U
#define TCP_OFFSET_SEQUENCE 4U
#define TCP_OFFSET_ACKNOWLEDGMENT 8U
#define TCP_OFFSET_DATA 12U
#define TCP_OFFSET_FLAGS 13U
#define TCP_OFFSET_WINDOW 14U
#define TCP_OFFSET_CHECKSUM 16U
#define TCP_OFFSET_URGENT 18U

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
    printf("ZCOV_BEGIN|case=host:network:tcp|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:network:tcp|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:network:tcp|value=0x%08X\n",
           (uint32_t)result);
}

static ipv4_protocol_handler_fn registered_handler;
static int register_result;
static int send_result;
static uint8_t send_flag = 1U;
static uint8_t transmitted[TCP_MAX_SEGMENT_SIZE];
static uint16_t transmitted_length;
static uint32_t transmitted_destination;
static uint8_t transmitted_protocol;
static uint32_t fake_ticks;
static uint32_t fake_frequency = 1000U;
static ipv4_status_t fake_ipv4_status = {
    .initialized = 1U,
    .configured = 1U,
    .interface_id = "eth0",
    .local_mac = {0x02U, 0U, 0U, 0U, 0U, 2U},
    .local_ip = TCP_LOCAL_IP,
    .subnet_mask = 0xFFFFFF00U,
    .gateway = 0x0A000201U,
    .configuration_generation = 1U
};

int ipv4_register_handler(uint8_t protocol, ipv4_protocol_handler_fn handler) {
    if (protocol != IPV4_PROTOCOL_TCP || !handler) return ERR_INVALID;
    if (register_result != OK) return register_result;
    registered_handler = handler;
    return OK;
}

int ipv4_get_status(ipv4_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_ipv4_status;
    return OK;
}

int ipv4_send(uint32_t destination_ip, uint8_t protocol,
              const uint8_t* payload, uint16_t payload_length,
              uint8_t* out_sent) {
    if (!payload || !out_sent || payload_length > sizeof(transmitted)) {
        return ERR_NULL;
    }
    *out_sent = 0U;
    if (send_result != OK) return send_result;
    kmemcpy(transmitted, payload, payload_length);
    transmitted_length = payload_length;
    transmitted_destination = destination_ip;
    transmitted_protocol = protocol;
    *out_sent = send_flag;
    return OK;
}

uint8_t ipv4_address_is_unicast(uint32_t ip_address) {
    uint8_t first_octet = (uint8_t)(ip_address >> 24U);

    return ip_address && ip_address != 0xFFFFFFFFU && first_octet < 224U;
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

static uint32_t callback_connected;
static uint32_t callback_data;
static uint32_t callback_writable;
static uint32_t callback_eof;
static uint32_t callback_closed;
static uint32_t callback_error;
static uint16_t callback_data_length;
static int callback_result;
static int callback_error_code;

static int tcp_callback(tcp_connection_handle_t handle, tcp_event_t event,
                        const uint8_t* data, uint16_t length, int error) {
    (void)handle;
    if (event == TCP_EVENT_CONNECTED) callback_connected++;
    if (event == TCP_EVENT_DATA) {
        callback_data++;
        callback_data_length = length;
        if (!data || length != 5U) return ERR_INVALID;
    }
    if (event == TCP_EVENT_WRITABLE) callback_writable++;
    if (event == TCP_EVENT_EOF) callback_eof++;
    if (event == TCP_EVENT_CLOSED) callback_closed++;
    if (event == TCP_EVENT_ERROR) {
        callback_error++;
        callback_error_code = error;
    }
    return callback_result;
}

static uint16_t read_u16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

static uint32_t read_u32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) | data[3];
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

static uint32_t add_bytes(uint32_t sum, const uint8_t* data,
                          uint16_t length) {
    while (length >= 2U) {
        sum += read_u16(data);
        data += 2U;
        length -= 2U;
    }
    if (length) sum += (uint16_t)data[0] << 8U;
    return sum;
}

static uint16_t checksum(uint32_t source_ip, uint32_t destination_ip,
                         const uint8_t* segment, uint16_t length) {
    uint32_t sum = (uint16_t)(source_ip >> 16U);

    sum += (uint16_t)source_ip;
    sum += (uint16_t)(destination_ip >> 16U);
    sum += (uint16_t)destination_ip;
    sum += IPV4_PROTOCOL_TCP;
    sum += length;
    sum = add_bytes(sum, segment, length);
    while (sum >> 16U) sum = (sum & 0xFFFFU) + (sum >> 16U);
    return (uint16_t)~sum;
}

static uint16_t make_segment(uint8_t* segment, uint16_t source_port,
                             uint16_t destination_port, uint32_t sequence,
                             uint32_t acknowledgment, uint16_t flags,
                             uint16_t window, const uint8_t* payload,
                             uint16_t payload_length, uint16_t mss) {
    uint16_t header_length = mss ? TCP_SYN_SIZE : TCP_HEADER_SIZE;
    uint16_t length = header_length + payload_length;
    uint16_t value;

    kmemset(segment, 0, TCP_MAX_SEGMENT_SIZE);
    write_u16(segment + TCP_OFFSET_SOURCE_PORT, source_port);
    write_u16(segment + TCP_OFFSET_DESTINATION_PORT, destination_port);
    write_u32(segment + TCP_OFFSET_SEQUENCE, sequence);
    write_u32(segment + TCP_OFFSET_ACKNOWLEDGMENT, acknowledgment);
    segment[TCP_OFFSET_DATA] = (uint8_t)((header_length / 4U) << 4U);
    segment[TCP_OFFSET_FLAGS] = (uint8_t)flags;
    write_u16(segment + TCP_OFFSET_WINDOW, window);
    if (mss) {
        segment[TCP_HEADER_SIZE] = TCP_OPTION_MSS;
        segment[TCP_HEADER_SIZE + 1U] = TCP_OPTION_MSS_LENGTH;
        write_u16(segment + TCP_HEADER_SIZE + 2U, mss);
    }
    if (payload_length) kmemcpy(segment + header_length, payload, payload_length);
    value = checksum(TCP_SERVER_IP, TCP_LOCAL_IP, segment, length);
    write_u16(segment + TCP_OFFSET_CHECKSUM, value);
    return length;
}

static int deliver(const uint8_t* segment, uint16_t length,
                   uint32_t source_ip, uint32_t destination_ip) {
    ipv4_packet_view_t packet = {
        "eth0", segment, length, source_ip, destination_ip, 1U,
        IPV4_PROTOCOL_TCP, 64U, IPV4_DELIVERY_LOCAL_UNICAST
    };

    if (!registered_handler) return ERR_STATE;
    return registered_handler(&packet);
}

static int expect_info(uint32_t index, tcp_state_t state,
                       tcp_connection_handle_t handle) {
    tcp_connection_info_t info;

    if (tcp_get_connection_info(index, &info) != OK) return 0;
    return info.used && info.handle == handle && info.state == state;
}

static int establish(tcp_connection_handle_t* out_handle,
                     uint16_t local_port, uint16_t window) {
    uint8_t segment[TCP_MAX_SEGMENT_SIZE];
    uint32_t sequence;
    uint32_t acknowledgment;
    uint16_t length;

    if (tcp_connect(local_port, TCP_SERVER_IP, TCP_SERVER_PORT, window,
                    tcp_callback, out_handle) != OK || !*out_handle ||
        transmitted_protocol != IPV4_PROTOCOL_TCP ||
        read_u16(transmitted + TCP_OFFSET_SOURCE_PORT) != local_port ||
        !(transmitted[TCP_OFFSET_FLAGS] & TCP_FLAG_SYN)) return 0;
    sequence = read_u32(transmitted + TCP_OFFSET_SEQUENCE);
    acknowledgment = sequence + 1U;
    length = make_segment(segment, TCP_SERVER_PORT, local_port, 900U,
                          acknowledgment, TCP_FLAG_SYN | TCP_FLAG_ACK,
                          4096U, NULL, 0U, TCP_TEST_MSS);
    if (deliver(segment, length, TCP_SERVER_IP, TCP_LOCAL_IP) != OK ||
        !expect_info(local_port == TCP_CLIENT_PORT ? 0U : 1U,
                     TCP_STATE_ESTABLISHED, *out_handle)) return 0;
    return callback_connected > 0U && callback_writable > 0U;
}

static int check_tcp(void) {
    uint8_t segment[TCP_MAX_SEGMENT_SIZE];
    uint8_t payload[5] = {'h', 'e', 'l', 'l', 'o'};
    uint8_t large_payload[TCP_LOCAL_MSS + 1U];
    tcp_connection_handle_t handle;
    tcp_connection_handle_t second_handle;
    tcp_connection_handle_t handles[TCP_CONNECTION_CAPACITY];
    tcp_status_t status;
    tcp_connection_info_t info;
    uint16_t length;
    uint16_t accepted;
    uint32_t client_sequence;

    fake_ticks = 100U;
    register_result = ERR_UNAVAILABLE;
    registered_handler = NULL;
    send_result = OK;
    send_flag = 1U;
    callback_result = OK;
    if (tcp_get_status(NULL) != ERR_NULL ||
        tcp_get_connection_info(0U, NULL) != ERR_NULL ||
        tcp_get_connection_info(TCP_CONNECTION_CAPACITY, &info) != ERR_INVALID ||
        tcp_init() != ERR_UNAVAILABLE || tcp_validate_state() != OK ||
        tcp_connect(TCP_CLIENT_PORT, TCP_SERVER_IP, TCP_SERVER_PORT, 512U,
                    tcp_callback, &handle) != ERR_STATE ||
        tcp_send(0U, payload, sizeof(payload), &accepted) != ERR_STATE ||
        tcp_close(0U) != ERR_STATE || tcp_abort(0U) != ERR_STATE ||
        tcp_maintain() != ERR_STATE || tcp_reset() != ERR_STATE) return 1;
    register_result = OK;
    if (tcp_init() != OK || tcp_init() != OK || !registered_handler ||
        tcp_state_name(TCP_STATE_CLOSED)[0] != 'C' ||
        kstrcmp(tcp_state_name(TCP_STATE_SYN_SENT), "SYN_SENT") != 0 ||
        kstrcmp(tcp_state_name(TCP_STATE_ESTABLISHED), "ESTABLISHED") != 0 ||
        kstrcmp(tcp_state_name(TCP_STATE_FIN_WAIT_1), "FIN_WAIT_1") != 0 ||
        kstrcmp(tcp_state_name(TCP_STATE_FIN_WAIT_2), "FIN_WAIT_2") != 0 ||
        kstrcmp(tcp_state_name(TCP_STATE_CLOSING), "CLOSING") != 0 ||
        kstrcmp(tcp_state_name(TCP_STATE_CLOSE_WAIT), "CLOSE_WAIT") != 0 ||
        kstrcmp(tcp_state_name(TCP_STATE_LAST_ACK), "LAST_ACK") != 0 ||
        kstrcmp(tcp_state_name(TCP_STATE_TIME_WAIT), "TIME_WAIT") != 0 ||
        kstrcmp(tcp_state_name(TCP_STATE_FAILED), "FAILED") != 0 ||
        kstrcmp(tcp_state_name(TCP_STATE_LISTEN), "LISTEN") != 0 ||
        kstrcmp(tcp_state_name((tcp_state_t)99), "DESCONHECIDO") != 0) return 2;
    if (tcp_connect(0U, TCP_SERVER_IP, TCP_SERVER_PORT, 512U,
                    tcp_callback, &handle) != ERR_INVALID ||
        tcp_connect(TCP_CLIENT_PORT, 0U, TCP_SERVER_PORT, 512U,
                    tcp_callback, &handle) != ERR_INVALID ||
        tcp_connect(TCP_CLIENT_PORT, TCP_SERVER_IP, 0U, 512U,
                    tcp_callback, &handle) != ERR_INVALID ||
        tcp_connect(TCP_CLIENT_PORT, TCP_SERVER_IP, TCP_SERVER_PORT, 0U,
                    tcp_callback, &handle) != ERR_INVALID ||
        tcp_connect(TCP_CLIENT_PORT, TCP_SERVER_IP, TCP_SERVER_PORT, 4097U,
                    tcp_callback, &handle) != ERR_INVALID ||
        tcp_connect(TCP_CLIENT_PORT, TCP_SERVER_IP, TCP_SERVER_PORT, 512U,
                    NULL, &handle) != ERR_NULL ||
        tcp_connect(TCP_CLIENT_PORT, TCP_SERVER_IP, TCP_SERVER_PORT, 512U,
                    tcp_callback, NULL) != ERR_NULL) return 3;
    callback_connected = 0U;
    callback_writable = 0U;
    if (!establish(&handle, TCP_CLIENT_PORT, 512U) ||
        tcp_send(handle, NULL, 1U, &accepted) != ERR_NULL ||
        tcp_send(handle, payload, sizeof(payload), NULL) != ERR_NULL ||
        tcp_send(0U, payload, sizeof(payload), &accepted) != ERR_INVALID ||
        tcp_send(handle, payload, 0U, &accepted) != OK || accepted != 0U ||
        tcp_set_receive_window(handle, 4097U) != ERR_INVALID ||
        tcp_set_receive_window(0U, 100U) != ERR_INVALID ||
        tcp_set_receive_window(handle, 4096U) != OK) return 4;
    client_sequence = read_u32(transmitted + TCP_OFFSET_SEQUENCE);
    if (tcp_send(handle, payload, sizeof(payload), &accepted) != OK ||
        accepted != sizeof(payload) || transmitted_length < TCP_HEADER_SIZE +
            sizeof(payload) || read_u32(transmitted + TCP_OFFSET_SEQUENCE) !=
            client_sequence) return 5;
    length = make_segment(segment, TCP_SERVER_PORT, TCP_CLIENT_PORT, 901U,
                          read_u32(transmitted + TCP_OFFSET_SEQUENCE) + 5U,
                          TCP_FLAG_ACK, 4096U, NULL, 0U, 0U);
    if (deliver(segment, length, TCP_SERVER_IP, TCP_LOCAL_IP) != OK ||
        !expect_info(0U, TCP_STATE_ESTABLISHED, handle)) return 6;
    length = make_segment(segment, TCP_SERVER_PORT, TCP_CLIENT_PORT, 901U,
                          read_u32(transmitted + TCP_OFFSET_SEQUENCE) + 5U,
                          TCP_FLAG_ACK | TCP_FLAG_PSH, 4096U, payload,
                          sizeof(payload), 0U);
    if (deliver(segment, length, TCP_SERVER_IP, TCP_LOCAL_IP) != OK ||
        callback_data == 0U || callback_data_length != sizeof(payload)) return 7;
    if (deliver(segment, length, TCP_SERVER_IP, TCP_LOCAL_IP) != OK ||
        tcp_get_status(&status) != OK || status.rx_duplicates == 0U) return 8;
    length = make_segment(segment, TCP_SERVER_PORT, TCP_CLIENT_PORT, 1000U,
                          read_u32(transmitted + TCP_OFFSET_SEQUENCE) + 5U,
                          TCP_FLAG_ACK, 4096U, NULL, 0U, 0U);
    if (deliver(segment, length, TCP_SERVER_IP, TCP_LOCAL_IP) != OK ||
        tcp_get_status(&status) != OK || status.rx_out_of_order == 0U) return 9;
    length = make_segment(segment, TCP_SERVER_PORT, TCP_CLIENT_PORT, 906U,
                          read_u32(transmitted + TCP_OFFSET_SEQUENCE) + 5U,
                          TCP_FLAG_ACK | TCP_FLAG_FIN, 4096U, NULL, 0U, 0U);
    if (deliver(segment, length, TCP_SERVER_IP, TCP_LOCAL_IP) != OK ||
        callback_eof == 0U || !expect_info(0U, TCP_STATE_CLOSE_WAIT, handle) ||
        tcp_close(handle) != OK || !expect_info(0U, TCP_STATE_LAST_ACK, handle)) {
        return 10;
    }
    length = make_segment(segment, TCP_SERVER_PORT, TCP_CLIENT_PORT, 907U,
                          read_u32(transmitted + TCP_OFFSET_SEQUENCE) + 1U,
                          TCP_FLAG_ACK, 4096U, NULL, 0U, 0U);
    if (deliver(segment, length, TCP_SERVER_IP, TCP_LOCAL_IP) != OK ||
        callback_closed == 0U || tcp_get_connection_info(0U, &info) != OK ||
        info.used || tcp_validate_state() != OK) return 11;
    if (tcp_set_receive_window(handle, 100U) != ERR_INVALID ||
        tcp_send(handle, payload, sizeof(payload), &accepted) != ERR_INVALID ||
        tcp_close(handle) != ERR_INVALID || tcp_abort(handle) != ERR_INVALID) return 12;
    if (tcp_get_connection_info(0U, &info) != OK || info.used ||
        tcp_get_connection_info(1U, &info) != OK || info.used ||
        tcp_get_status(&status) != OK || status.connection_count != 0U) return 13;
    if (tcp_connect(TCP_CLIENT_PORT, TCP_SERVER_IP, TCP_SERVER_PORT, 512U,
                    tcp_callback, &second_handle) != OK ||
        tcp_connect(TCP_CLIENT_PORT, TCP_SERVER_IP, TCP_SERVER_PORT, 512U,
                    tcp_callback, &handle) != ERR_INVALID ||
        tcp_abort(second_handle) != OK || callback_closed == 0U) return 14;
    fake_frequency = 0U;
    if (tcp_connect(TCP_CLIENT_PORT, TCP_SERVER_IP, TCP_SERVER_PORT, 512U,
                    tcp_callback, &handle) != ERR_INVALID ||
        tcp_maintain() != ERR_STATE) return 15;
    fake_frequency = 1000U;
    if (tcp_reset() != OK) return 16;
    callback_result = ERR_STATE;
    if (!establish(&handle, TCP_CLIENT_PORT, 512U)) return 17;
    length = make_segment(segment, TCP_SERVER_PORT, TCP_CLIENT_PORT, 901U,
                          read_u32(transmitted + TCP_OFFSET_SEQUENCE),
                          TCP_FLAG_ACK | TCP_FLAG_PSH, 4096U, payload,
                          sizeof(payload), 0U);
    {
        int delivered = deliver(segment, length, TCP_SERVER_IP, TCP_LOCAL_IP);
        int information = expect_info(0U, TCP_STATE_FAILED, handle);
        int aborted = tcp_abort(handle);
        if (delivered != ERR_OVERFLOW || callback_error == 0U ||
            callback_error_code != ERR_OVERFLOW || !information ||
            aborted != OK) {
            return 18;
        }
    }
    callback_result = OK;
    register_result = OK;
    if (tcp_connect(TCP_OTHER_PORT, TCP_SERVER_IP, TCP_SERVER_PORT, 512U,
                    tcp_callback, &handle) != OK) return 19;
    client_sequence = read_u32(transmitted + TCP_OFFSET_SEQUENCE);
    length = make_segment(segment, TCP_SERVER_PORT, TCP_OTHER_PORT, 900U,
                          client_sequence + 1U, TCP_FLAG_RST | TCP_FLAG_ACK,
                          4096U, NULL, 0U, 0U);
    if (deliver(segment, length, TCP_SERVER_IP, TCP_LOCAL_IP) != OK ||
        callback_error_code != ERR_UNAVAILABLE ||
        !expect_info(0U, TCP_STATE_FAILED, handle) || tcp_abort(handle) != OK) {
        return 20;
    }
    if (tcp_connect(TCP_CLIENT_PORT, TCP_SERVER_IP, TCP_SERVER_PORT, 512U,
                    tcp_callback, &handle) != OK) return 21;
    client_sequence = read_u32(transmitted + TCP_OFFSET_SEQUENCE);
    length = make_segment(segment, TCP_SERVER_PORT, TCP_CLIENT_PORT, 900U,
                          client_sequence + 1U, TCP_FLAG_SYN | TCP_FLAG_ACK,
                          4096U, NULL, 0U, 0U);
    segment[TCP_OFFSET_CHECKSUM] ^= 1U;
    if (deliver(segment, length, TCP_SERVER_IP, TCP_LOCAL_IP) != OK ||
        tcp_get_status(&status) != OK || status.rx_checksum_errors == 0U ||
        tcp_abort(handle) != OK) return 22;
    segment[0] = 0U;
    if (deliver(segment, TCP_HEADER_SIZE - 1U, TCP_SERVER_IP, TCP_LOCAL_IP) !=
            OK || tcp_get_status(&status) != OK || status.rx_invalid == 0U) {
        return 23;
    }
    fake_ipv4_status.configured = 0U;
    if (tcp_connect(TCP_CLIENT_PORT, TCP_SERVER_IP, TCP_SERVER_PORT, 512U,
                    tcp_callback, &handle) != ERR_STATE) return 24;
    fake_ipv4_status.configured = 1U;
    send_flag = 0U;
    if (tcp_connect(TCP_CLIENT_PORT, TCP_SERVER_IP, TCP_SERVER_PORT, 512U,
                    tcp_callback, &handle) != OK ||
        tcp_maintain() != OK || tcp_abort(handle) != OK) return 25;
    send_flag = 1U;
    send_result = ERR_TIMEOUT;
    if (tcp_connect(TCP_CLIENT_PORT, TCP_SERVER_IP, TCP_SERVER_PORT, 512U,
                    tcp_callback, &handle) != ERR_TIMEOUT) return 26;
    send_result = OK;
    if (tcp_reset() != OK) return 27;
    for (uint32_t index = 0U; index < TCP_CONNECTION_CAPACITY; index++) {
        if (tcp_connect((uint16_t)(TCP_CLIENT_PORT + index), TCP_SERVER_IP,
                        (uint16_t)(TCP_SERVER_PORT + index), 512U,
                        tcp_callback, &handles[index]) != OK) return 28;
    }
    if (tcp_connect(60000U, TCP_SERVER_IP, 60000U, 512U,
                    tcp_callback, &handle) != ERR_OVERFLOW ||
        tcp_get_status(&status) != OK || status.connection_count !=
            TCP_CONNECTION_CAPACITY || tcp_validate_state() != OK) return 29;
    if (tcp_reset() != OK || tcp_get_status(&status) != OK ||
        status.connection_count != 0U || tcp_validate_state() != OK) return 30;
    if (tcp_connect(TCP_CLIENT_PORT, TCP_SERVER_IP, TCP_SERVER_PORT, 512U,
                    tcp_callback, &handle) != OK) return 31;
    fake_ticks += 1000U;
    if (tcp_maintain() != OK) return 32;
    fake_ticks += 2000U;
    if (tcp_maintain() != OK) return 33;
    fake_ticks += 4000U;
    if (tcp_maintain() != OK) return 34;
    fake_ticks += 8000U;
    if (tcp_maintain() != OK || !expect_info(0U, TCP_STATE_FAILED, handle) ||
        tcp_get_status(&status) != OK || status.retransmissions < 3U ||
        status.timeouts == 0U || tcp_abort(handle) != OK) return 35;
    length = make_segment(segment, TCP_SERVER_PORT, 60001U, 1U, 0U,
                          TCP_FLAG_SYN, 4096U, NULL, 0U, TCP_TEST_MSS);
    if (deliver(segment, length, TCP_SERVER_IP, TCP_LOCAL_IP) != OK ||
        status.rx_no_connection == 0U || transmitted_destination !=
            TCP_SERVER_IP) {
        if (tcp_get_status(&status) != OK || status.rx_no_connection == 0U ||
            transmitted_destination != TCP_SERVER_IP) return 36;
    }
    if (tcp_get_status(&status) != OK || status.segments_tx == 0U ||
        status.syn_tx == 0U || status.resets_tx == 0U ||
        tcp_validate_state() != OK || tcp_reset() != OK) return 37;
    for (uint32_t index = 0U; index < sizeof(large_payload); index++) {
        large_payload[index] = (uint8_t)index;
    }
    (void)large_payload;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    log_init();
    result = check_tcp();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
