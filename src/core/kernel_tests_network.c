#include "kernel_tests.h"

#include "core/arp.h"
#include "core/crypto.h"
#include "core/dhcp.h"
#include "core/dns.h"
#include "core/ethernet.h"
#include "core/http.h"
#include "core/icmp.h"
#include "core/ipv4.h"
#include "core/log.h"
#include "core/net_buffer.h"
#include "core/net_socket.h"
#include "core/route.h"
#include "core/sk_buff.h"
#include "core/socket.h"
#include "core/tcp.h"
#include "core/tls.h"
#include "core/tls_client.h"
#include "core/udp.h"

#define KERNEL_TEST_NETWORK_TAG "TST4"

static int network_check_socket(void) {
    socket_self_test_result_t result;

    if (socket_self_test(&result) != OK || result.failed != 0U ||
        !result.lifecycle || !result.fd_mapping || !result.duplicate_bind ||
        !result.unix_bind_connect || !result.unix_accept ||
        !result.stream_io || !result.queue_full || !result.nonblocking ||
        !result.close_wakeup || !result.eof || !result.cancellation ||
        !result.invalid_inputs || !result.invariants ||
        socket_validate_state() != OK) {
        LOG_ERROR(KERNEL_TEST_NETWORK_TAG,
                  "fase=network-socket resultado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_NETWORK_TAG, ERR_STATE,
                       "fase=network-socket resultado invalido");
        return ERR_STATE;
    }
    return OK;
}

static int network_check_net_socket(void) {
    net_socket_self_test_result_t result;

    if (net_socket_self_test(&result) != OK || result.failed != 0U ||
        !result.lifecycle || !result.event_mapping ||
        !result.readable_wake_one || !result.terminal_wake_all ||
        !result.timeout || !result.cancellation || !result.invariants ||
        net_socket_validate_state() != OK) {
        LOG_ERROR(KERNEL_TEST_NETWORK_TAG,
                  "fase=network-net-socket resultado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_NETWORK_TAG, ERR_STATE,
                       "fase=network-net-socket resultado invalido");
        return ERR_STATE;
    }
    return OK;
}

static int network_check_route(void) {
    route_self_test_result_t result;

    if (route_self_test(&result) != OK || result.failed != 0U ||
        !result.lifecycle || !result.direct_route || !result.default_route ||
        !result.longest_prefix || !result.delete_route ||
        !result.duplicate_route || !result.overflow ||
        !result.invalid_input || !result.reset || !result.invariants ||
        route_validate_state() != OK) {
        LOG_ERROR(KERNEL_TEST_NETWORK_TAG,
                  "fase=network-route resultado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_NETWORK_TAG, ERR_STATE,
                       "fase=network-route resultado invalido");
        return ERR_STATE;
    }
    return OK;
}

static int network_check_tls(void) {
    tls_self_test_result_t result;

    if (tls_self_test(&result) != OK || result.failed ||
        !result.valid_identity || !result.time_unavailable ||
        !result.certificate_future || !result.certificate_expired ||
        !result.untrusted_chain || !result.san_mismatch ||
        !result.pin_absent || !result.pin_match || !result.pin_mismatch ||
        !result.trust_rotation || !result.trust_revocation ||
        !result.invariants || !result.passed || tls_validate_state() != OK ||
        tls_client_validate_state() != OK) {
        LOG_ERROR(KERNEL_TEST_NETWORK_TAG,
                  "fase=network-tls resultado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_NETWORK_TAG, ERR_STATE,
                       "fase=network-tls resultado invalido");
        return ERR_STATE;
    }
    return OK;
}

static int network_check_validators(void) {
    if (arp_validate_state() != OK || ethernet_validate_state() != OK ||
        ipv4_validate_state() != OK || icmp_validate_state() != OK ||
        udp_validate_state() != OK || dhcp_validate_state() != OK ||
        dns_validate_state() != OK || tcp_validate_state() != OK ||
        http_validate_state() != OK) {
        LOG_ERROR(KERNEL_TEST_NETWORK_TAG,
                  "fase=network-validators resultado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_NETWORK_TAG, ERR_STATE,
                       "fase=network-validators resultado invalido");
        return ERR_STATE;
    }
    return OK;
}

int kernel_tests_run_network(const kernel_tests_runtime_t* runtime) {
    net_buffer_stats_t net_buffer_before;
    net_buffer_stats_t net_buffer_after;
    sk_buff_stats_t skb_before;
    sk_buff_stats_t skb_after;
    socket_status_t socket_before;
    socket_status_t socket_after;
    net_socket_status_t net_socket_before;
    net_socket_status_t net_socket_after;
    route_status_t route_before;
    route_status_t route_after;
    int result;

    LOG_INFO(KERNEL_TEST_NETWORK_TAG, "fase=network-preconditions inicio");
    if (net_buffer_get_stats(&net_buffer_before) != OK ||
        skb_get_stats(&skb_before) != OK ||
        socket_get_status(&socket_before) != OK ||
        net_socket_get_status(&net_socket_before) != OK ||
        route_get_status(&route_before) != OK) {
        return kernel_tests_phase_result("fase=network-preconditions",
                                         ERR_STATE);
    }
    if (net_buffer_before.active_buffers != 0U ||
        skb_before.active_buffers != 0U || socket_before.active_count != 0U ||
        net_socket_before.active_count != 0U) {
        return kernel_tests_phase_result("fase=network-preconditions",
                                         ERR_STATE);
    }
    result = kernel_tests_progress(runtime);
    if (result != OK) return result;

    result = net_buffer_self_test();
    if (result == OK) result = net_buffer_validate_state();
    if (result != OK) {
        return kernel_tests_phase_result("fase=network-net-buffer", result);
    }
    result = skb_self_test();
    if (result == OK) result = skb_validate_state();
    if (result != OK) {
        return kernel_tests_phase_result("fase=network-skb", result);
    }
    result = network_check_socket();
    if (result != OK) {
        return kernel_tests_phase_result("fase=network-socket", result);
    }
    result = network_check_net_socket();
    if (result != OK) {
        return kernel_tests_phase_result("fase=network-net-socket", result);
    }
    result = network_check_route();
    if (result != OK) {
        return kernel_tests_phase_result("fase=network-route", result);
    }
    result = crypto_self_test();
    if (result != OK) {
        return kernel_tests_phase_result("fase=network-crypto", result);
    }
    result = network_check_tls();
    if (result != OK) {
        return kernel_tests_phase_result("fase=network-tls", result);
    }
    result = network_check_validators();
    if (result != OK) {
        return kernel_tests_phase_result("fase=network-validators", result);
    }
    result = kernel_tests_progress(runtime);
    if (result != OK) return result;

    if (net_buffer_get_stats(&net_buffer_after) != OK ||
        skb_get_stats(&skb_after) != OK ||
        socket_get_status(&socket_after) != OK ||
        net_socket_get_status(&net_socket_after) != OK ||
        route_get_status(&route_after) != OK ||
        net_buffer_after.active_buffers != net_buffer_before.active_buffers ||
        skb_after.active_buffers != skb_before.active_buffers ||
        socket_after.active_count != socket_before.active_count ||
        net_socket_after.active_count != net_socket_before.active_count ||
        route_after.entry_count != route_before.entry_count ||
        net_buffer_validate_state() != OK || skb_validate_state() != OK ||
        socket_validate_state() != OK || net_socket_validate_state() != OK ||
        route_validate_state() != OK) {
        return kernel_tests_phase_result("fase=network-postconditions",
                                         ERR_STATE);
    }
    return kernel_tests_phase_result("fase=network-postconditions", OK);
}
