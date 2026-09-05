#include <stdint.h>
#include <stdio.h>

#include "apps/shell_runtime.h"
#include "core/arp.h"
#include "core/dhcp.h"
#include "core/dns.h"
#include "core/errors.h"
#include "core/ethernet.h"
#include "core/http.h"
#include "core/icmp.h"
#include "core/ipv4.h"
#include "core/log.h"
#include "core/net_buffer.h"
#include "core/net_socket.h"
#include "core/network_manager.h"
#include "core/recovery.h"
#include "core/route.h"
#include "core/sk_buff.h"
#include "core/socket.h"
#include "core/string.h"
#include "core/tcp.h"
#include "core/timer.h"
#include "core/udp.h"
#include "drivers/idt.h"

int shell_network_validate_for_checks(void);

#define HOST_COVERAGE_CAPACITY 1024U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_REQUIRED_IPV4_HANDLERS 3U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

static network_manager_status_t fixture_network_status;
static network_interface_info_t fixture_network_interface;
static ethernet_status_t fixture_ethernet_status;
static arp_status_t fixture_arp_status;
static ipv4_status_t fixture_ipv4_status;
static icmp_status_t fixture_icmp_status;
static udp_status_t fixture_udp_status;
static dhcp_status_t fixture_dhcp_status;
static dns_status_t fixture_dns_status;
static tcp_status_t fixture_tcp_status;
static net_socket_status_t fixture_net_socket_status;
static http_status_t fixture_http_status;
static recovery_component_t fixture_recovery_network;
static int fixture_network_status_result;
static uint32_t fixture_timer_frequency;

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
    printf("ZCOV_BEGIN|case=host:shell:network-checks|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:network-checks|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:network-checks|value=0x%08X\n",
           (uint32_t)result);
}

static void fixture_reset(void) {
    kmemset(&fixture_network_status, 0, sizeof(fixture_network_status));
    fixture_network_status.initialized = 1U;
    fixture_network_status.packet_io_available = 1U;
    fixture_network_status.ethernet_available = 1U;
    fixture_network_status.arp_available = 1U;
    fixture_network_status.ipv4_available = 1U;
    fixture_network_status.icmp_available = 1U;
    fixture_network_status.udp_available = 1U;
    fixture_network_status.dhcp_available = 1U;
    fixture_network_status.dns_available = 1U;
    fixture_network_status.tcp_available = 1U;
    fixture_network_status.sockets_available = 1U;
    fixture_network_status.http_available = 1U;
    fixture_network_status.interface_count = 1U;
    fixture_network_status.recognized_count = 1U;
    fixture_network_status.active_count = 1U;
    fixture_network_status.ipv4_source = NETWORK_IPV4_SOURCE_STATIC;
    fixture_network_status.arp_configured = 1U;
    fixture_network_status.ipv4_configured = 1U;
    kmemcpy(fixture_network_status.l3_interface_id, "eth0", 5U);
    fixture_network_status.last_error = OK;

    kmemset(&fixture_network_interface, 0, sizeof(fixture_network_interface));
    fixture_network_interface.transport = NETWORK_TRANSPORT_PCI;
    fixture_network_interface.model = NETWORK_ADAPTER_E1000;
    fixture_network_interface.state = NETWORK_INTERFACE_ACTIVE;
    fixture_network_interface.link = NETWORK_LINK_UP;
    fixture_network_interface.class_code = 0x02U;
    fixture_network_interface.bus = 0U;
    fixture_network_interface.device = 3U;
    fixture_network_interface.function = 0U;
    fixture_network_interface.irq = 11U;
    fixture_network_interface.ethernet_attached = 1U;
    fixture_network_interface.l3_active = 1U;
    kmemcpy(fixture_network_interface.mac_address,
            (const uint8_t[]){0x52U, 0x54U, 0x00U, 0x12U, 0x34U, 0x56U}, 6U);

    kmemset(&fixture_ethernet_status, 0, sizeof(fixture_ethernet_status));
    fixture_ethernet_status.initialized = 1U;
    fixture_ethernet_status.interface_count = 1U;
    fixture_ethernet_status.handler_count = 1U;

    kmemset(&fixture_arp_status, 0, sizeof(fixture_arp_status));
    fixture_arp_status.initialized = 1U;
    fixture_arp_status.configured = 1U;
    kmemset(&fixture_ipv4_status, 0, sizeof(fixture_ipv4_status));
    fixture_ipv4_status.initialized = 1U;
    fixture_ipv4_status.configured = 1U;
    fixture_ipv4_status.local_ip = 0xC0A8010AU;
    fixture_ipv4_status.subnet_mask = 0xFFFFFF00U;
    fixture_ipv4_status.gateway = 0xC0A80101U;
    kmemcpy(fixture_ipv4_status.interface_id, "eth0", 5U);
    fixture_ipv4_status.handler_count = HOST_REQUIRED_IPV4_HANDLERS;
    kmemset(&fixture_icmp_status, 0, sizeof(fixture_icmp_status));
    fixture_icmp_status.initialized = 1U;
    kmemset(&fixture_udp_status, 0, sizeof(fixture_udp_status));
    fixture_udp_status.initialized = 1U;
    kmemset(&fixture_dhcp_status, 0, sizeof(fixture_dhcp_status));
    fixture_dhcp_status.initialized = 1U;
    fixture_dhcp_status.state = DHCP_STATE_IDLE;
    kmemset(&fixture_dns_status, 0, sizeof(fixture_dns_status));
    fixture_dns_status.initialized = 1U;
    kmemset(&fixture_tcp_status, 0, sizeof(fixture_tcp_status));
    fixture_tcp_status.initialized = 1U;
    kmemset(&fixture_net_socket_status, 0,
            sizeof(fixture_net_socket_status));
    fixture_net_socket_status.initialized = 1U;
    kmemset(&fixture_http_status, 0, sizeof(fixture_http_status));
    fixture_http_status.initialized = 1U;
    kmemset(&fixture_recovery_network, 0,
            sizeof(fixture_recovery_network));
    fixture_recovery_network.name = "NETWORK";
    fixture_recovery_network.state = RECOVERY_STATE_READY;
    fixture_recovery_network.last_error = OK;
    fixture_recovery_network.last_message = "ready";
    fixture_network_status_result = OK;
    fixture_timer_frequency = 1000U;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

int network_manager_get_status(network_manager_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_network_status;
    return fixture_network_status_result;
}

int network_manager_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = fixture_network_status.interface_count;
    return OK;
}

int network_manager_get_interface(uint32_t index,
                                  network_interface_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (index >= fixture_network_status.interface_count) return ERR_NOT_FOUND;
    *out_info = fixture_network_interface;
    return OK;
}

int network_manager_find(const char* id, network_interface_info_t* out_info) {
    if (!id || !out_info) return ERR_NULL;
    if (kstrcmp(id, "eth0") != 0) return ERR_NOT_FOUND;
    *out_info = fixture_network_interface;
    return OK;
}

int network_manager_format_text(const network_interface_info_t* info,
                                network_interface_text_t* out_text) {
    if (!info || !out_text) return ERR_NULL;
    kmemset(out_text, 0, sizeof(*out_text));
    kmemcpy(out_text->id, "eth0", 5U);
    kmemcpy(out_text->name, "Ethernet 0", 11U);
    kmemcpy(out_text->driver, "e1000", 6U);
    return OK;
}

int ethernet_get_status(ethernet_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_ethernet_status;
    return OK;
}

int ethernet_validate_state(void) {
    return OK;
}

int skb_self_test(void) {
    return OK;
}

int net_buffer_self_test(void) {
    return OK;
}

int socket_self_test(socket_self_test_result_t* out_result) {
    if (!out_result) return ERR_NULL;
    kmemset(out_result, 0, sizeof(*out_result));
    out_result->invariants = 1U;
    out_result->passed = 1U;
    return OK;
}

int arp_get_status(arp_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_arp_status;
    return OK;
}

int arp_validate_state(void) {
    return OK;
}

int ipv4_get_status(ipv4_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_ipv4_status;
    return OK;
}

int ipv4_validate_state(void) {
    return OK;
}

int icmp_get_status(icmp_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_icmp_status;
    return OK;
}

int icmp_validate_state(void) {
    return OK;
}

int udp_get_status(udp_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_udp_status;
    return OK;
}

int udp_validate_state(void) {
    return OK;
}

int dhcp_get_status(dhcp_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_dhcp_status;
    return OK;
}

int dhcp_validate_state(void) {
    return OK;
}

int dns_get_status(dns_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_dns_status;
    return OK;
}

int dns_validate_state(void) {
    return OK;
}

int tcp_get_status(tcp_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_tcp_status;
    return OK;
}

int tcp_validate_state(void) {
    return OK;
}

int net_socket_get_status(net_socket_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_net_socket_status;
    return OK;
}

int net_socket_validate_state(void) {
    return OK;
}

int http_get_status(http_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_http_status;
    return OK;
}

int http_validate_state(void) {
    return OK;
}

int route_validate_state(void) {
    return OK;
}

int route_self_test(route_self_test_result_t* out_result) {
    if (!out_result) return ERR_NULL;
    kmemset(out_result, 0, sizeof(*out_result));
    out_result->invariants = 1U;
    out_result->passed = 1U;
    return OK;
}

const recovery_component_t* recovery_get(recovery_component_id_t component) {
    return component == RECOVERY_COMPONENT_NETWORK ?
           &fixture_recovery_network : 0;
}

int idt_get_shared_irq_handler_count(uint8_t irq_line,
                                     uint8_t* out_count) {
    if (!out_count || irq_line != fixture_network_interface.irq) {
        return ERR_INVALID;
    }
    *out_count = 1U;
    return OK;
}

uint32_t timer_get_frequency(void) {
    return fixture_timer_frequency;
}

uint32_t timer_get_ticks(void) {
    return 0U;
}

uint8_t ipv4_address_is_unicast(uint32_t ip_address) {
    uint8_t first_octet = (uint8_t)(ip_address >> 24U);

    if (!ip_address || ip_address == 0xFFFFFFFFU || first_octet == 127U) {
        return 0U;
    }
    return first_octet < 224U || first_octet > 239U;
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

static int expect_result(int expected, int actual, const char* label) {
    if (expected == actual) return 0;
    fprintf(stderr, "network-checks-host: %s esperado=%d obtido=%d\n",
            label, expected, actual);
    return 1;
}

int main(void) {
    int result = 0;

    fixture_reset();
    coverage_active = 1U;
    result = expect_result(OK, shell_network_validate_for_checks(),
                           "fixture saudavel");
    if (!result) {
        fixture_network_interface.class_code = 0x01U;
        result = expect_result(ERR_STATE, shell_network_validate_for_checks(),
                               "interface invalida");
    }
    if (!result) {
        fixture_reset();
        fixture_network_status_result = ERR_UNAVAILABLE;
        result = expect_result(ERR_UNAVAILABLE,
                               shell_network_validate_for_checks(),
                               "estado indisponivel");
    }
    if (!result) {
        fixture_reset();
        result = expect_result(OK, shell_network_checks_host_test_contracts(),
                               "contratos internos");
    }
    coverage_active = 0U;
    coverage_emit(result);
    if (result) printf("network-checks-host: FAIL code=%d\n", result);
    else printf("network-checks-host: PASS\n");
    return result;
}
