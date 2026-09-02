#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/net_buffer.h"
#include "core/net_socket.h"
#include "core/string.h"
#include "core/tcp.h"
#include "core/timer.h"
#include "core/video.h"
#include "drivers/serial.h"
#include "process/process.h"
#include "process/thread.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define SOCKET_TEST_REMOTE_IP 0x0A00020FU
#define SOCKET_TEST_REMOTE_PORT 8080U
#define SOCKET_TEST_TCP_HANDLE 0xA501U
#define SOCKET_TEST_TIMEOUT 10U
#define SOCKET_TEST_SEND_SIZE 600U
#define SOCKET_TEST_DATA_SIZE 5U
#define SOCKET_TEST_QUEUE_SIZE 3U
#define SOCKET_TEST_INVALID_TIMEOUT 0xFFFFFFFEU

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
    printf("ZCOV_BEGIN|case=host:network:socket|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:network:socket|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:network:socket|value=0x%08X\n",
           (uint32_t)result);
}

static tcp_status_t fake_tcp_status;
static tcp_event_fn fake_tcp_callback;
static tcp_connection_handle_t fake_tcp_handle;
static int fake_tcp_connect_result;
static int fake_tcp_send_result;
static int fake_tcp_close_result;
static int fake_tcp_abort_result;
static int fake_tcp_reset_result;
static uint16_t fake_tcp_accept;
static uint16_t fake_tcp_window;
static uint32_t fake_tcp_send_calls;
static uint32_t fake_tcp_close_calls;
static uint32_t fake_tcp_abort_calls;
static uint32_t fake_tcp_reset_calls;
static uint32_t fake_poll_notifications;
static uint32_t fake_ticks;
static uint32_t fake_frequency;
static wait_reason_t fake_wait_reason;

static void reset_fakes(void) {
    kmemset(&fake_tcp_status, 0, sizeof(fake_tcp_status));
    fake_tcp_status.initialized = 1U;
    fake_tcp_callback = NULL;
    fake_tcp_handle = SOCKET_TEST_TCP_HANDLE;
    fake_tcp_connect_result = OK;
    fake_tcp_send_result = OK;
    fake_tcp_close_result = OK;
    fake_tcp_abort_result = OK;
    fake_tcp_reset_result = OK;
    fake_tcp_accept = UINT16_MAX;
    fake_tcp_window = 0U;
    fake_tcp_send_calls = 0U;
    fake_tcp_close_calls = 0U;
    fake_tcp_abort_calls = 0U;
    fake_tcp_reset_calls = 0U;
    fake_poll_notifications = 0U;
    fake_ticks = 100U;
    fake_frequency = 1000U;
    fake_wait_reason = WAIT_REASON_TIMEOUT;
}

static int check(int condition, const char* name) {
    if (condition) return OK;
    printf("socket-host failure: %s\n", name);
    return ERR_STATE;
}

static int check_result(int actual, int expected, const char* name) {
    return check(actual == expected, name);
}

static int check_info(net_socket_handle_t handle, net_socket_state_t state,
                      uint16_t tx, uint16_t rx, const char* name) {
    net_socket_info_t info;

    if (net_socket_get_handle_info(handle, &info) != OK) return ERR_STATE;
    return check(info.state == state && info.tx_queued == tx &&
                 info.rx_queued == rx, name);
}

static int trigger_tcp(tcp_event_t event, const uint8_t* data,
                       uint16_t length, int error) {
    if (!fake_tcp_callback) return ERR_STATE;
    return fake_tcp_callback(fake_tcp_handle, event, data, length, error);
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

uint8_t ipv4_address_is_unicast(uint32_t ip_address) {
    uint8_t first_octet = (uint8_t)(ip_address >> 24U);

    return (uint8_t)(ip_address && ip_address != 0xFFFFFFFFU &&
                     first_octet < 224U && first_octet != 127U);
}

int vfs_poll_notify(void) {
    fake_poll_notifications++;
    return OK;
}

thread_t* thread_get_current(void) {
    return NULL;
}

int thread_wait(wait_channel_t* channel, uint32_t observed_condition,
                uint32_t timeout_ticks, wait_reason_t* out_reason) {
    (void)channel;
    (void)observed_condition;
    (void)timeout_ticks;
    if (!out_reason) return ERR_NULL;
    *out_reason = fake_wait_reason;
    return OK;
}

int process_wait(wait_channel_t* channel, uint32_t observed_condition,
                 uint32_t timeout_ticks, wait_reason_t* out_reason) {
    (void)channel;
    (void)observed_condition;
    (void)timeout_ticks;
    if (!out_reason) return ERR_NULL;
    *out_reason = fake_wait_reason;
    return OK;
}

int tcp_init(void) {
    fake_tcp_status.initialized = 1U;
    return OK;
}

int tcp_get_status(tcp_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_tcp_status;
    return OK;
}

int tcp_connect(uint16_t local_port, uint32_t remote_ip,
                uint16_t remote_port, uint16_t receive_window,
                tcp_event_fn callback,
                tcp_connection_handle_t* out_handle) {
    if (!local_port || !remote_ip || !remote_port || !callback ||
        !out_handle) return ERR_NULL;
    if (fake_tcp_connect_result != OK) return fake_tcp_connect_result;
    fake_tcp_callback = callback;
    fake_tcp_window = receive_window;
    fake_tcp_status.connection_count = 1U;
    *out_handle = fake_tcp_handle;
    return OK;
}

int tcp_send(tcp_connection_handle_t handle, const uint8_t* data,
             uint16_t length, uint16_t* out_accepted) {
    uint16_t accepted;

    if (!data || !out_accepted || handle != fake_tcp_handle) return ERR_NULL;
    fake_tcp_send_calls++;
    *out_accepted = 0U;
    if (fake_tcp_send_result != OK) return fake_tcp_send_result;
    accepted = fake_tcp_accept < length ? fake_tcp_accept : length;
    *out_accepted = accepted;
    return OK;
}

int tcp_set_receive_window(tcp_connection_handle_t handle,
                           uint16_t receive_window) {
    if (handle != fake_tcp_handle) return ERR_INVALID;
    fake_tcp_window = receive_window;
    return OK;
}

int tcp_close(tcp_connection_handle_t handle) {
    if (handle != fake_tcp_handle) return ERR_INVALID;
    fake_tcp_close_calls++;
    return fake_tcp_close_result;
}

int tcp_abort(tcp_connection_handle_t handle) {
    if (handle != fake_tcp_handle) return ERR_INVALID;
    fake_tcp_abort_calls++;
    return fake_tcp_abort_result;
}

int tcp_maintain(void) {
    return OK;
}

int tcp_reset(void) {
    fake_tcp_reset_calls++;
    if (fake_tcp_reset_result == OK) fake_tcp_status.connection_count = 0U;
    return fake_tcp_reset_result;
}

int tcp_get_connection_info(uint32_t index,
                            tcp_connection_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (index || !fake_tcp_status.connection_count) return ERR_NOT_FOUND;
    kmemset(out_info, 0, sizeof(*out_info));
    out_info->used = 1U;
    out_info->handle = fake_tcp_handle;
    out_info->state = TCP_STATE_SYN_SENT;
    return OK;
}

int tcp_validate_state(void) {
    return OK;
}

const char* tcp_state_name(tcp_state_t state) {
    if (state == TCP_STATE_CLOSED) return "CLOSED";
    if (state == TCP_STATE_ESTABLISHED) return "ESTABLISHED";
    return "OTHER";
}

static int test_invalid_before_init(void) {
    net_socket_handle_t handle = 0U;
    uint16_t count = 0U;
    uint8_t eof = 0U;
    uint8_t byte = 0U;
    net_socket_event_mask_t events = 0U;
    wait_reason_t reason = WAIT_REASON_NONE;

    if (check_result(net_socket_open(NET_SOCKET_TYPE_STREAM, &handle),
                     ERR_STATE, "open before init") != OK) return ERR_STATE;
    if (check_result(net_socket_send(1U, &byte, 1U, &count),
                     ERR_STATE, "send before init") != OK) return ERR_STATE;
    if (check_result(net_socket_receive(1U, &byte, 1U, &count, &eof),
                     ERR_STATE, "receive before init") != OK) return ERR_STATE;
    if (check_result(net_socket_wait(1U, NET_SOCKET_EVENT_READABLE,
                                     0U, &events, &reason),
                     ERR_STATE, "wait before init") != OK) return ERR_STATE;
    if (check_result(net_socket_close(1U), ERR_STATE,
                     "close before init") != OK) return ERR_STATE;
    if (check_result(net_socket_abort(1U), ERR_STATE,
                     "abort before init") != OK) return ERR_STATE;
    return check_result(net_socket_maintain(), ERR_STATE,
                        "maintain before init");
}

static int test_open_and_connect(net_socket_handle_t* connected,
                                 net_socket_handle_t* failed) {
    net_socket_handle_t handle = 0U;
    net_socket_info_t info;

    if (check_result(net_socket_open(NET_SOCKET_TYPE_STREAM, NULL),
                     ERR_NULL, "open null output") != OK) return ERR_STATE;
    if (check_result(net_socket_open((net_socket_type_t)99, &handle),
                     ERR_UNAVAILABLE, "open unsupported type") != OK) {
        return ERR_STATE;
    }
    if (check_result(net_socket_open(NET_SOCKET_TYPE_STREAM, &handle),
                     OK, "open") != OK) return ERR_STATE;
    if (check_info(handle, NET_SOCKET_STATE_OPEN, 0U, 0U, "open info") != OK) {
        return ERR_STATE;
    }
    if (check_result(net_socket_connect(handle, SOCKET_TEST_REMOTE_IP, 0U),
                     ERR_INVALID, "connect without port") != OK) return ERR_STATE;
    if (check_result(net_socket_connect(handle, 0xE0000001U,
                                        SOCKET_TEST_REMOTE_PORT),
                     ERR_INVALID, "connect multicast") != OK) return ERR_STATE;

    fake_tcp_connect_result = ERR_UNAVAILABLE;
    if (check_result(net_socket_connect(handle, SOCKET_TEST_REMOTE_IP,
                                        SOCKET_TEST_REMOTE_PORT),
                     ERR_UNAVAILABLE, "connect transport error") != OK) {
        return ERR_STATE;
    }
    if (check_info(handle, NET_SOCKET_STATE_ERROR, 0U, 0U,
                   "connect error state") != OK) return ERR_STATE;
    if (check_result(net_socket_close(handle), OK,
                     "close failed connect") != OK) return ERR_STATE;
    *failed = handle;

    fake_tcp_connect_result = OK;
    if (check_result(net_socket_open(NET_SOCKET_TYPE_STREAM, &handle),
                     OK, "open connected") != OK) return ERR_STATE;
    if (check_result(net_socket_connect(handle, SOCKET_TEST_REMOTE_IP,
                                        SOCKET_TEST_REMOTE_PORT),
                     OK, "connect") != OK) return ERR_STATE;
    if (check_info(handle, NET_SOCKET_STATE_CONNECTING, 0U, 0U,
                   "connecting info") != OK) return ERR_STATE;
    if (check_result(trigger_tcp(TCP_EVENT_CONNECTED, NULL, 0U, OK),
                     OK, "connected event") != OK) return ERR_STATE;
    if (check_info(handle, NET_SOCKET_STATE_CONNECTED, 0U, 0U,
                   "connected info") != OK) return ERR_STATE;
    if (net_socket_get_handle_info(handle, &info) != OK ||
        info.local_port < TCP_EPHEMERAL_PORT_MIN ||
        info.remote_port != SOCKET_TEST_REMOTE_PORT ||
        info.remote_ip != SOCKET_TEST_REMOTE_IP) return ERR_STATE;
    *connected = handle;
    return OK;
}

static int test_io_and_events(net_socket_handle_t handle) {
    uint8_t send_data[SOCKET_TEST_SEND_SIZE];
    uint8_t receive_data[SOCKET_TEST_DATA_SIZE];
    uint8_t overflow_data[NET_SOCKET_RX_CAPACITY + 1U];
    uint16_t written = 0U;
    uint16_t read = 0U;
    uint8_t eof = 0U;
    net_socket_event_mask_t events = 0U;
    wait_reason_t reason = WAIT_REASON_NONE;
    net_socket_status_t status;

    kmemset(send_data, 0x5AU, sizeof(send_data));
    kmemset(overflow_data, 0xA5U, sizeof(overflow_data));
    if (check_result(net_socket_send(handle, NULL, 1U, &written),
                     ERR_NULL, "send null data") != OK) return ERR_STATE;
    if (check_result(net_socket_send(handle, send_data, 0U, &written),
                     OK, "send zero") != OK || written != 0U) return ERR_STATE;
    if (check_result(net_socket_wait(handle, NET_SOCKET_EVENT_CONNECTED,
                                     SOCKET_TEST_TIMEOUT, &events, &reason),
                     OK, "wait connected") != OK ||
        reason != WAIT_REASON_EVENT ||
        !(events & NET_SOCKET_EVENT_CONNECTED)) return ERR_STATE;
    if (check_result(net_socket_send(handle, send_data, sizeof(send_data),
                                     &written), OK, "send queued") != OK ||
        written != sizeof(send_data)) return ERR_STATE;
    if (check_info(handle, NET_SOCKET_STATE_CONNECTED, written, 0U,
                   "send queue info") != OK) return ERR_STATE;
    fake_tcp_accept = 300U;
    if (check_result(net_socket_maintain(), OK, "partial drain") != OK) {
        return ERR_STATE;
    }
    if (check_info(handle, NET_SOCKET_STATE_CONNECTED,
                   SOCKET_TEST_SEND_SIZE - 300U, 0U,
                   "partial drain info") != OK) return ERR_STATE;
    fake_tcp_accept = UINT16_MAX;
    if (check_result(net_socket_maintain(), OK, "complete drain") != OK ||
        check_info(handle, NET_SOCKET_STATE_CONNECTED, 0U, 0U,
                   "complete drain info") != OK) return ERR_STATE;
    if (check_result(trigger_tcp(TCP_EVENT_WRITABLE, NULL, 0U, OK),
                     OK, "writable event") != OK) return ERR_STATE;

    if (check_result(trigger_tcp(TCP_EVENT_DATA, (const uint8_t*)"hello",
                                 SOCKET_TEST_DATA_SIZE, OK),
                     OK, "data event") != OK) return ERR_STATE;
    if (check_result(net_socket_wait(handle, NET_SOCKET_EVENT_READABLE,
                                     SOCKET_TEST_TIMEOUT, &events, &reason),
                     OK, "wait readable") != OK ||
        reason != WAIT_REASON_EVENT || !(events & NET_SOCKET_EVENT_READABLE)) {
        return ERR_STATE;
    }
    if (check_result(net_socket_receive(handle, receive_data, 2U, &read, &eof),
                     OK, "partial receive") != OK || read != 2U || eof) {
        return ERR_STATE;
    }
    if (receive_data[0] != 'h' || receive_data[1] != 'e') return ERR_STATE;
    if (check_result(net_socket_receive(handle, receive_data,
                                        sizeof(receive_data), &read, &eof),
                     OK, "complete receive") != OK || read != 3U || eof) {
        return ERR_STATE;
    }
    if (receive_data[0] != 'l' || receive_data[1] != 'l' ||
        receive_data[2] != 'o') return ERR_STATE;
    if (check_result(trigger_tcp(TCP_EVENT_DATA, overflow_data,
                                 (uint16_t)sizeof(overflow_data), OK),
                     ERR_OVERFLOW, "receive overflow") != OK) return ERR_STATE;
    if (check_result(net_socket_get_status(&status), OK, "status after rx") != OK ||
        status.rx_overflows == 0U) return ERR_STATE;
    if (check_result(trigger_tcp(TCP_EVENT_EOF, NULL, 0U, OK),
                     OK, "eof event") != OK) return ERR_STATE;
    if (check_result(net_socket_wait(handle, NET_SOCKET_EVENT_EOF,
                                     SOCKET_TEST_TIMEOUT, &events, &reason),
                     OK, "wait eof") != OK || reason != WAIT_REASON_EVENT ||
        !(events & NET_SOCKET_EVENT_EOF)) return ERR_STATE;
    if (check_result(net_socket_receive(handle, receive_data, 0U, &read, &eof),
                     OK, "eof receive") != OK || !eof) return ERR_STATE;
    return OK;
}

static int test_close_abort_reset(net_socket_handle_t connected,
                                  net_socket_handle_t failed) {
    net_socket_handle_t handle = 0U;
    net_socket_handle_t replacement = 0U;
    uint8_t data[SOCKET_TEST_QUEUE_SIZE] = {1U, 2U, 3U};
    uint16_t written = 0U;

    if (check_result(net_socket_close(failed), ERR_INVALID,
                     "stale failed handle") != OK) return ERR_STATE;
    if (check_result(net_socket_close(connected), OK,
                     "close eof") != OK || fake_tcp_close_calls != 1U) {
        return ERR_STATE;
    }
    if (check_result(trigger_tcp(TCP_EVENT_CLOSED, NULL, 0U, OK),
                     OK, "closed event") != OK) return ERR_STATE;
    if (check_result(net_socket_open(NET_SOCKET_TYPE_STREAM, &replacement),
                     OK, "reuse slot") != OK || replacement == connected) {
        return ERR_STATE;
    }
    if (check_result(net_socket_close(connected), ERR_INVALID,
                     "stale generation") != OK) return ERR_STATE;
    if (check_result(net_socket_connect(replacement, SOCKET_TEST_REMOTE_IP,
                                        SOCKET_TEST_REMOTE_PORT), OK,
                     "reuse connect") != OK) return ERR_STATE;
    if (check_result(trigger_tcp(TCP_EVENT_CONNECTED, NULL, 0U, OK),
                     OK, "reuse connected") != OK) return ERR_STATE;
    if (check_result(net_socket_send(replacement, data, sizeof(data),
                                     &written), OK, "close queue") != OK ||
        written != sizeof(data)) return ERR_STATE;
    fake_tcp_accept = 0U;
    if (check_result(net_socket_close(replacement), OK,
                     "close with pending tx") != OK ||
        check_info(replacement, NET_SOCKET_STATE_CLOSING,
                   SOCKET_TEST_QUEUE_SIZE, 0U, "closing state") != OK) {
        return ERR_STATE;
    }
    if (check_result(net_socket_maintain(), OK, "closing no progress") != OK ||
        fake_tcp_close_calls != 1U) return ERR_STATE;
    fake_tcp_accept = UINT16_MAX;
    if (check_result(net_socket_maintain(), OK, "closing drain") != OK ||
        fake_tcp_close_calls != 2U) return ERR_STATE;
    if (check_result(trigger_tcp(TCP_EVENT_CLOSED, NULL, 0U, OK),
                     OK, "closed replacement") != OK) return ERR_STATE;

    if (check_result(net_socket_open(NET_SOCKET_TYPE_STREAM, &handle),
                     OK, "open abort") != OK) return ERR_STATE;
    if (check_result(net_socket_connect(handle, SOCKET_TEST_REMOTE_IP,
                                        SOCKET_TEST_REMOTE_PORT), OK,
                     "abort connect") != OK) return ERR_STATE;
    if (check_result(net_socket_abort(handle), OK, "abort") != OK ||
        fake_tcp_abort_calls == 0U) return ERR_STATE;
    if (check_result(net_socket_get_handle_info(handle, NULL), ERR_NULL,
                     "info null") != OK) return ERR_STATE;
    return OK;
}

static int test_wait_capacity_and_names(void) {
    net_socket_handle_t handles[NET_SOCKET_CAPACITY];
    net_socket_handle_t extra = 0U;
    net_socket_event_mask_t events = 0U;
    wait_reason_t reason = WAIT_REASON_NONE;
    net_socket_status_t status;
    net_socket_info_t info;
    int result = OK;

    for (uint32_t index = 0U; index < NET_SOCKET_CAPACITY; index++) {
        if (net_socket_open(NET_SOCKET_TYPE_STREAM, &handles[index]) != OK) {
            result = ERR_STATE;
            break;
        }
    }
    if (result == OK && check_result(net_socket_open(NET_SOCKET_TYPE_STREAM,
                                                     &extra), ERR_OVERFLOW,
                                     "socket capacity") != OK) result = ERR_STATE;
    if (result == OK && net_socket_get_info(NET_SOCKET_CAPACITY, &info) !=
        ERR_INVALID) result = ERR_STATE;
    if (result == OK && net_socket_wait(handles[0], NET_SOCKET_EVENT_READABLE,
                                        SOCKET_TEST_INVALID_TIMEOUT, &events,
                                        &reason) != ERR_INVALID) result = ERR_STATE;
    if (result == OK) {
        fake_wait_reason = WAIT_REASON_TIMEOUT;
        if (net_socket_wait(handles[0], NET_SOCKET_EVENT_READABLE, 0U,
                            &events, &reason) != OK ||
            reason != WAIT_REASON_TIMEOUT) result = ERR_STATE;
    }
    if (result == OK) {
        fake_wait_reason = WAIT_REASON_CANCELLED;
        if (net_socket_wait(handles[0], NET_SOCKET_EVENT_READABLE,
                            SOCKET_TEST_TIMEOUT, &events, &reason) != OK ||
            reason != WAIT_REASON_CANCELLED) result = ERR_STATE;
    }
    if (result == OK && net_socket_get_status(&status) != OK) result = ERR_STATE;
    if (result == OK && (status.wait_timeouts == 0U ||
                         status.wait_cancellations == 0U)) result = ERR_STATE;
    if (result == OK && net_socket_state_name(NET_SOCKET_STATE_CONNECTED)[0] !=
        'C') result = ERR_STATE;
    if (result == OK && net_socket_state_name((net_socket_state_t)99)[0] !=
        'D') result = ERR_STATE;
    for (uint32_t index = 0U; index < NET_SOCKET_CAPACITY; index++) {
        if (net_socket_close(handles[index]) != OK) result = ERR_STATE;
    }
    return result;
}

int main(void) {
    net_socket_handle_t connected = 0U;
    net_socket_handle_t failed = 0U;
    net_socket_status_t status;
    net_socket_self_test_result_t self_test;
    int result = OK;

    reset_fakes();
    coverage_active = 1U;
    result = test_invalid_before_init();
    if (result == OK) {
        wait_init();
        result = check_result(net_socket_init(), OK, "socket init");
    }
    if (result == OK) result = test_open_and_connect(&connected, &failed);
    if (result == OK) result = test_io_and_events(connected);
    if (result == OK) result = test_close_abort_reset(connected, failed);
    if (result == OK) result = test_wait_capacity_and_names();
    if (result == OK) result = check_result(net_socket_validate_state(), OK,
                                             "socket invariants");
    if (result == OK) result = check_result(net_socket_self_test(&self_test),
                                             OK, "socket self test");
    if (result == OK) result = check(self_test.failed == 0U &&
                                     self_test.passed >= 7U,
                                     "socket self test result");
    if (result == OK) result = check_result(net_socket_reset(), OK,
                                             "socket reset");
    if (result == OK) result = check_result(net_socket_get_status(&status),
                                             OK, "final socket status");
    if (result == OK) result = check(status.active_count == 0U &&
                                     fake_tcp_reset_calls == 1U &&
                                     fake_poll_notifications > 0U,
                                     "final socket cleanup");
    if (result == OK) result = check_result(net_buffer_validate_state(), OK,
                                             "buffer invariants");
    coverage_active = 0U;
    coverage_emit(result);
    if (result == OK) {
        printf("net-socket-host: PASS\n");
        return 0;
    }
    printf("net-socket-host: FAIL (%d)\n", result);
    return 1;
}
