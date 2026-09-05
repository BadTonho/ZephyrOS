#include <stdint.h>
#include <stdio.h>

#include "apps/shell_job.h"
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
#include "core/poll.h"
#include "core/recovery.h"
#include "core/route.h"
#include "core/sk_buff.h"
#include "core/socket.h"
#include "core/string.h"
#include "core/tcp.h"
#include "core/timer.h"
#include "core/udp.h"
#include "core/wait.h"
#include "drivers/idt.h"
#include "fs/vfs.h"
#include "process/process.h"
#include "process/thread.h"

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
static uint32_t fixture_timer_ticks;
static net_buffer_stats_t fixture_net_buffer_stats;
static sk_buff_stats_t fixture_skb_stats;
static socket_status_t fixture_socket_status;

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
    kmemset(&fixture_net_buffer_stats, 0, sizeof(fixture_net_buffer_stats));
    fixture_net_buffer_stats.initialized = 1U;
    fixture_net_buffer_stats.active_buffers = 2U;
    fixture_net_buffer_stats.peak_buffers = 3U;
    kmemset(&fixture_skb_stats, 0, sizeof(fixture_skb_stats));
    fixture_skb_stats.initialized = 1U;
    fixture_skb_stats.active_buffers = 1U;
    fixture_skb_stats.peak_buffers = 2U;
    kmemset(&fixture_socket_status, 0, sizeof(fixture_socket_status));
    fixture_socket_status.initialized = 1U;
    fixture_socket_status.active_count = 1U;
    fixture_network_status_result = OK;
    fixture_timer_frequency = 1000U;
    fixture_timer_ticks = 0U;
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

const char* network_manager_model_name(network_adapter_model_t model) {
    return model == NETWORK_ADAPTER_E1000 ? "E1000" : "DESCONHECIDO";
}

const char* network_manager_interface_state_name(
    network_interface_state_t state) {
    return state == NETWORK_INTERFACE_ACTIVE ? "ATIVA" : "INDISPONIVEL";
}

const char* network_manager_link_state_name(network_link_state_t state) {
    return state == NETWORK_LINK_UP ? "UP" :
           state == NETWORK_LINK_DOWN ? "DOWN" : "UNKNOWN";
}

const char* network_manager_ipv4_source_name(network_ipv4_source_t source) {
    return source == NETWORK_IPV4_SOURCE_STATIC ? "STATIC" :
           source == NETWORK_IPV4_SOURCE_DHCP ? "DHCP" : "NONE";
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

int network_manager_send_diagnostic(const char* id) {
    (void)id;
    return ERR_UNAVAILABLE;
}

int network_manager_configure_arp(const char* id, uint32_t local_ip) {
    (void)id;
    (void)local_ip;
    return ERR_UNAVAILABLE;
}

int network_manager_acquire_dhcp(const char* id) {
    (void)id;
    return ERR_UNAVAILABLE;
}

int network_manager_renew_dhcp(void) {
    return ERR_UNAVAILABLE;
}

int network_manager_release_dhcp(uint8_t* out_sent) {
    if (out_sent) *out_sent = 0U;
    return ERR_UNAVAILABLE;
}

int network_manager_configure_dns(uint32_t server_ip) {
    (void)server_ip;
    return ERR_UNAVAILABLE;
}

int network_manager_get_ethernet_diagnostic(
    const char* id, network_ethernet_diagnostic_t* out_diagnostic) {
    (void)id;
    (void)out_diagnostic;
    return ERR_UNAVAILABLE;
}

int network_manager_configure_ipv4(const char* id, uint32_t local_ip,
                                   uint32_t subnet_mask, uint32_t gateway) {
    (void)id;
    (void)local_ip;
    (void)subnet_mask;
    (void)gateway;
    return ERR_UNAVAILABLE;
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

int ethernet_get_interface_status(const char* id,
                                 ethernet_interface_status_t* out_status) {
    (void)id;
    if (!out_status) return ERR_NULL;
    kmemset(out_status, 0, sizeof(*out_status));
    out_status->attached = 1U;
    out_status->rx_frames = 1U;
    out_status->last_frame_length = 64U;
    out_status->last_ethertype = 0x0800U;
    out_status->last_destination_type = ETHERNET_DESTINATION_LOCAL_UNICAST;
    kmemcpy(out_status->last_source,
            (const uint8_t[]){0x52U, 0x54U, 0x00U, 0x12U, 0x34U, 0x56U},
            ETHERNET_MAC_ADDRESS_SIZE);
    kmemcpy(out_status->last_destination,
            (const uint8_t[]){0x52U, 0x54U, 0x00U, 0x65U, 0x43U, 0x21U},
            ETHERNET_MAC_ADDRESS_SIZE);
    return OK;
}

int ethernet_validate_state(void) {
    return OK;
}

int skb_self_test(void) {
    return OK;
}

int skb_get_stats(sk_buff_stats_t* out_stats) {
    if (!out_stats) return ERR_NULL;
    *out_stats = fixture_skb_stats;
    return OK;
}

int net_buffer_self_test(void) {
    return OK;
}

int net_buffer_get_stats(net_buffer_stats_t* out_stats) {
    if (!out_stats) return ERR_NULL;
    *out_stats = fixture_net_buffer_stats;
    return OK;
}

int socket_self_test(socket_self_test_result_t* out_result) {
    if (!out_result) return ERR_NULL;
    kmemset(out_result, 0, sizeof(*out_result));
    out_result->invariants = 1U;
    out_result->passed = 1U;
    return OK;
}

int socket_get_info(uint32_t index, socket_info_t* out_info) {
    (void)index;
    if (!out_info) return ERR_NULL;
    kmemset(out_info, 0, sizeof(*out_info));
    return ERR_NOT_FOUND;
}

const char* socket_family_name(socket_family_t family) {
    return family == SOCKET_FAMILY_UNIX ? "AF_UNIX" : "AF_INET";
}

const char* socket_state_name(socket_state_t state) {
    return state == SOCKET_STATE_CONNECTED ? "CONNECTED" : "CLOSED";
}

int socket_create(socket_family_t family, socket_type_t type,
                  uint32_t flags, int32_t* fd_out) {
    (void)family;
    (void)type;
    (void)flags;
    if (fd_out) *fd_out = VFS_FD_INVALID;
    return ERR_UNAVAILABLE;
}

int socket_bind(int32_t fd, const socket_address_t* address) {
    (void)fd;
    (void)address;
    return ERR_UNAVAILABLE;
}

int socket_connect(int32_t fd, const socket_address_t* address) {
    (void)fd;
    (void)address;
    return ERR_UNAVAILABLE;
}

int socket_send(int32_t fd, const uint8_t* data, uint32_t size,
                uint32_t* sent_out) {
    (void)fd;
    (void)data;
    (void)size;
    if (sent_out) *sent_out = 0U;
    return ERR_UNAVAILABLE;
}

int socket_listen(int32_t fd, uint16_t backlog) {
    (void)fd;
    (void)backlog;
    return ERR_UNAVAILABLE;
}

int socket_accept(int32_t fd, int32_t* fd_out) {
    (void)fd;
    if (fd_out) *fd_out = VFS_FD_INVALID;
    return ERR_UNAVAILABLE;
}

int socket_close(int32_t fd) {
    (void)fd;
    return OK;
}

int socket_get_status(socket_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_socket_status;
    return OK;
}

int arp_get_status(arp_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_arp_status;
    return OK;
}

int arp_get_cache_entry(uint32_t index, arp_cache_entry_info_t* out_entry) {
    (void)index;
    if (!out_entry) return ERR_NULL;
    kmemset(out_entry, 0, sizeof(*out_entry));
    return ERR_NOT_FOUND;
}

int arp_clear(void) {
    return ERR_UNAVAILABLE;
}

int arp_resolve(uint32_t ip_address, uint8_t* out_mac,
                uint8_t* out_resolved) {
    (void)ip_address;
    (void)out_mac;
    if (out_resolved) *out_resolved = 0U;
    return ERR_UNAVAILABLE;
}

uint8_t arp_ipv4_is_valid(uint32_t ip_address) {
    return ip_address != 0U;
}

const char* arp_entry_state_name(arp_entry_state_t state) {
    return state == ARP_ENTRY_RESOLVED ? "RESOLVIDO" :
           state == ARP_ENTRY_FAILED ? "FALHOU" : "PENDENTE";
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

int icmp_ping_start(uint32_t target_ip, uint8_t count,
                    uint32_t timeout_seconds) {
    (void)target_ip;
    (void)count;
    (void)timeout_seconds;
    return ERR_UNAVAILABLE;
}

int icmp_reset(void) {
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

const char* dhcp_state_name(dhcp_state_t state) {
    return state == DHCP_STATE_BOUND ? "BOUND" : "IDLE";
}

int dhcp_reset(void) {
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

int dns_get_cache_entry(uint32_t index, dns_cache_entry_info_t* out_entry) {
    (void)index;
    if (!out_entry) return ERR_NULL;
    kmemset(out_entry, 0, sizeof(*out_entry));
    return ERR_NOT_FOUND;
}

int dns_resolve(const char* name, uint32_t* out_ip,
                uint8_t* out_resolved) {
    (void)name;
    if (!out_ip || !out_resolved) return ERR_NULL;
    *out_ip = 0U;
    *out_resolved = 0U;
    return ERR_UNAVAILABLE;
}

int dns_reset(void) {
    return OK;
}

int dns_clear(void) {
    return ERR_UNAVAILABLE;
}

const char* dns_state_name(dns_state_t state) {
    return state == DNS_STATE_COMPLETE ? "COMPLETE" : "IDLE";
}

int tcp_get_status(tcp_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_tcp_status;
    return OK;
}

int tcp_validate_state(void) {
    return OK;
}

int tcp_get_connection_info(uint32_t index, tcp_connection_info_t* out_info) {
    (void)index;
    if (!out_info) return ERR_NULL;
    kmemset(out_info, 0, sizeof(*out_info));
    return ERR_NOT_FOUND;
}

const char* tcp_state_name(tcp_state_t state) {
    return state == TCP_STATE_ESTABLISHED ? "ESTABLISHED" : "CLOSED";
}

int net_socket_get_status(net_socket_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_net_socket_status;
    return OK;
}

int net_socket_validate_state(void) {
    return OK;
}

int net_socket_self_test(net_socket_self_test_result_t* out_result) {
    if (!out_result) return ERR_NULL;
    kmemset(out_result, 0, sizeof(*out_result));
    return ERR_UNAVAILABLE;
}

int net_socket_get_info(uint32_t index, net_socket_info_t* out_info) {
    (void)index;
    if (!out_info) return ERR_NULL;
    kmemset(out_info, 0, sizeof(*out_info));
    return ERR_NOT_FOUND;
}

int net_socket_wait(net_socket_handle_t handle,
                    net_socket_event_mask_t events,
                    uint32_t timeout_ticks,
                    net_socket_event_mask_t* out_events,
                    wait_reason_t* out_reason) {
    (void)handle;
    (void)events;
    (void)timeout_ticks;
    if (out_events) *out_events = 0U;
    if (out_reason) *out_reason = WAIT_REASON_TIMEOUT;
    return OK;
}

int net_socket_get_handle_info(net_socket_handle_t handle,
                               net_socket_info_t* out_info) {
    (void)handle;
    if (!out_info) return ERR_NULL;
    kmemset(out_info, 0, sizeof(*out_info));
    return ERR_UNAVAILABLE;
}

int net_socket_open(net_socket_type_t type, net_socket_handle_t* out_handle) {
    (void)type;
    if (out_handle) *out_handle = 0U;
    return ERR_UNAVAILABLE;
}

int net_socket_connect(net_socket_handle_t handle, uint32_t remote_ip,
                       uint16_t remote_port) {
    (void)handle;
    (void)remote_ip;
    (void)remote_port;
    return ERR_UNAVAILABLE;
}

int net_socket_abort(net_socket_handle_t handle) {
    (void)handle;
    return OK;
}

const char* net_socket_state_name(net_socket_state_t state) {
    return state == NET_SOCKET_STATE_CONNECTED ? "CONNECTED" : "CLOSED";
}

int http_get_status(http_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_http_status;
    return OK;
}

int http_validate_state(void) {
    return OK;
}

int http_get_start(const char* url) {
    (void)url;
    return ERR_UNAVAILABLE;
}

int http_reset(void) {
    return OK;
}

int http_get_body(const uint8_t** out_body, uint32_t* out_length) {
    if (!out_body || !out_length) return ERR_NULL;
    *out_body = 0;
    *out_length = 0U;
    return ERR_UNAVAILABLE;
}

const char* http_state_name(http_state_t state) {
    return state == HTTP_STATE_COMPLETE ? "COMPLETE" :
           state == HTTP_STATE_FAILED ? "FAILED" : "WAITING";
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

int route_reset(void) {
    return OK;
}

int route_add(uint32_t network, uint32_t subnet_mask, uint32_t gateway,
              const char* interface_id) {
    (void)network;
    (void)subnet_mask;
    (void)gateway;
    (void)interface_id;
    return OK;
}

int route_delete(uint32_t network, uint32_t subnet_mask) {
    (void)network;
    (void)subnet_mask;
    return OK;
}

int route_set_default(uint32_t gateway, const char* interface_id) {
    (void)gateway;
    (void)interface_id;
    return OK;
}

int route_get_status(route_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    kmemset(out_status, 0, sizeof(*out_status));
    return ERR_UNAVAILABLE;
}

int route_get_entry(uint32_t index, route_entry_t* out_entry) {
    (void)index;
    if (!out_entry) return ERR_NULL;
    kmemset(out_entry, 0, sizeof(*out_entry));
    return ERR_NOT_FOUND;
}

const recovery_component_t* recovery_get(recovery_component_id_t component) {
    return component == RECOVERY_COMPONENT_NETWORK ?
           &fixture_recovery_network : 0;
}

const char* recovery_state_name(recovery_state_t state) {
    return state == RECOVERY_STATE_READY ? "READY" :
           state == RECOVERY_STATE_DEGRADED ? "DEGRADED" : "UNAVAILABLE";
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
    fixture_timer_ticks += 1000U;
    return fixture_timer_ticks;
}

uint8_t ipv4_address_is_unicast(uint32_t ip_address) {
    uint8_t first_octet = (uint8_t)(ip_address >> 24U);

    if (!ip_address || ip_address == 0xFFFFFFFFU || first_octet == 127U) {
        return 0U;
    }
    return first_octet < 224U || first_octet > 239U;
}

const char* icmp_ping_state_name(icmp_ping_state_t state) {
    return state == ICMP_PING_COMPLETE ? "COMPLETE" :
           state == ICMP_PING_FAILED ? "FAILED" : "WAITING";
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void video_begin_update(void) {}
void video_end_update(void) {}

int wait_queue_copy_waiters(wait_info_t* output, uint32_t max_entries,
                            uint32_t* out_count) {
    (void)output;
    (void)max_entries;
    if (!out_count) return ERR_NULL;
    *out_count = 0U;
    return OK;
}

int wait_queue_remove(wait_queue_entry_t* entry, wait_reason_t reason) {
    (void)entry;
    (void)reason;
    return OK;
}

void thread_yield(void) {}

void thread_destroy(thread_t* thread) {
    (void)thread;
}

void thread_block(uint32_t ticks) {
    (void)ticks;
}

thread_t* thread_create(const char* name, void (*entry)(void)) {
    (void)name;
    (void)entry;
    return 0;
}

thread_t* thread_get_current(void) {
    return 0;
}

process_t* process_get_current(void) {
    return 0;
}

uint32_t process_get_current_pid(void) {
    return 1U;
}

void process_yield(void) {}

int ipc_current_has_pending(void) {
    return 0;
}

int ipc_send(uint32_t pid, ipc_msg_t* message) {
    (void)pid;
    (void)message;
    return 0;
}

int ipc_receive(ipc_msg_t* message) {
    (void)message;
    return ERR_UNAVAILABLE;
}

int vfs_close(int32_t fd) {
    (void)fd;
    return ERR_INVALID;
}

int vfs_pipe(int32_t fds[2]) {
    if (fds) {
        fds[0] = VFS_FD_INVALID;
        fds[1] = VFS_FD_INVALID;
    }
    return ERR_UNAVAILABLE;
}

int vfs_read(int32_t fd, void* buffer, uint32_t size,
             uint32_t* bytes_read) {
    (void)fd;
    (void)buffer;
    (void)size;
    if (bytes_read) *bytes_read = 0U;
    return ERR_UNAVAILABLE;
}

int vfs_write(int32_t fd, const void* buffer, uint32_t size,
              uint32_t* bytes_written) {
    (void)fd;
    (void)buffer;
    (void)size;
    if (bytes_written) *bytes_written = 0U;
    return ERR_UNAVAILABLE;
}

int vfs_poll(pollfd_t* fds, uint32_t count, uint32_t timeout_ticks,
             uint32_t* out_ready) {
    (void)fds;
    (void)count;
    (void)timeout_ticks;
    if (out_ready) *out_ready = 0U;
    return ERR_UNAVAILABLE;
}

int vfs_select(uint32_t nfds, fd_set_t* readfds, fd_set_t* writefds,
               fd_set_t* exceptfds, uint32_t timeout_ticks,
               uint32_t* out_ready) {
    (void)nfds;
    (void)readfds;
    (void)writefds;
    (void)exceptfds;
    (void)timeout_ticks;
    if (out_ready) *out_ready = 0U;
    return ERR_UNAVAILABLE;
}

int shell_job_start(const shell_job_definition_t* definition,
                    const char* arguments) {
    (void)definition;
    (void)arguments;
    return ERR_STATE;
}

void shell_job_pump_events(void) {}

int shell_job_is_active(void) {
    return 0;
}

int shell_job_cancel_requested(void) {
    return 0;
}

uint32_t shell_job_get_generation(void) {
    return 1U;
}

int shell_job_generation_matches(uint32_t generation) {
    return generation == 1U;
}

void shell_job_set_phase(shell_job_context_t* context, const char* phase) {
    (void)context;
    (void)phase;
}

void shell_job_set_progress(shell_job_context_t* context,
                            uint32_t progress, uint32_t total) {
    (void)context;
    (void)progress;
    (void)total;
}

void shell_job_set_deadline(shell_job_context_t* context,
                            uint32_t deadline_tick) {
    if (context) context->deadline_tick = deadline_tick;
}

void shell_job_set_next_wake(shell_job_context_t* context,
                             uint32_t next_wake_tick) {
    if (context) context->next_wake_tick = next_wake_tick;
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
    if (!result) {
        fixture_reset();
        result = expect_result(OK, shell_network_host_test_contracts(),
                               "comandos e jobs");
    }
    coverage_active = 0U;
    coverage_emit(result);
    if (result) printf("network-checks-host: FAIL code=%d\n", result);
    else printf("network-checks-host: PASS\n");
    return result;
}
