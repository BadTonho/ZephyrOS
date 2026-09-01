#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/arp.h"
#include "core/dhcp.h"
#include "core/dns.h"
#include "core/ethernet.h"
#include "core/errors.h"
#include "core/http.h"
#include "core/icmp.h"
#include "core/ipv4.h"
#include "core/log.h"
#include "core/net_socket.h"
#include "core/network_manager.h"
#include "core/recovery.h"
#include "core/route.h"
#include "core/socket.h"
#include "core/tcp.h"
#include "core/timer.h"
#include "core/udp.h"
#include "core/usb_manager.h"
#include "drivers/e1000.h"
#include "drivers/pci.h"
#include "drivers/rtl8139.h"
#include "drivers/rtl8811cu.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define FAKE_PCI_CAPACITY 2U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static pci_device_t fake_pci[FAKE_PCI_CAPACITY];
static uint8_t fake_pci_count;
static uint32_t fake_ticks = 100U;
static ethernet_status_t fake_ethernet_status;
static arp_status_t fake_arp_status;
static ipv4_status_t fake_ipv4_status;
static icmp_status_t fake_icmp_status;
static udp_status_t fake_udp_status;
static dhcp_status_t fake_dhcp_status;
static dns_status_t fake_dns_status;
static tcp_status_t fake_tcp_status;
static net_socket_status_t fake_net_socket_status;
static http_status_t fake_http_status;

static void __attribute__((no_instrument_function))
coverage_record(void* function) {
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
    printf("ZCOV_BEGIN|case=host:core:network-manager|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:network-manager|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:network-manager|value=0x%08X\n",
           (uint32_t)result);
}

uint32_t timer_get_ticks(void) {
    return fake_ticks++;
}

int serial_is_ready(void) {
    return 1;
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

int pci_get_device_count(uint8_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = fake_pci_count;
    return OK;
}

int pci_get_device_at(uint8_t index, pci_device_t* out_device) {
    if (!out_device) return ERR_NULL;
    if (index >= fake_pci_count) return ERR_NOT_FOUND;
    *out_device = fake_pci[index];
    return OK;
}

int usb_manager_get_device_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    return ERR_NOT_FOUND;
}

int usb_manager_get_device(uint32_t index, usb_device_info_t* out_info) {
    (void)index;
    if (!out_info) return ERR_NULL;
    return ERR_NOT_FOUND;
}

int usb_manager_find_device(const char* id, usb_device_info_t* out_info) {
    (void)id;
    if (!out_info) return ERR_NULL;
    return ERR_NOT_FOUND;
}

int ethernet_init(void) {
    return OK;
}

int ethernet_attach_interface(const ethernet_interface_t* interface) {
    (void)interface;
    return ERR_UNAVAILABLE;
}

int ethernet_register_handler(uint16_t ethertype,
                              ethernet_protocol_handler_fn handler) {
    (void)ethertype;
    (void)handler;
    return ERR_UNAVAILABLE;
}

int ethernet_poll(uint32_t budget, uint32_t* out_processed) {
    (void)budget;
    if (!out_processed) return ERR_NULL;
    *out_processed = 0U;
    return OK;
}

int ethernet_send(const char* interface_id, const uint8_t* destination,
                  uint16_t ethertype, const uint8_t* payload,
                  uint16_t payload_length) {
    (void)interface_id;
    (void)destination;
    (void)ethertype;
    (void)payload;
    (void)payload_length;
    return ERR_UNAVAILABLE;
}

int ethernet_get_status(ethernet_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_ethernet_status;
    return OK;
}

int ethernet_get_interface_status(const char* interface_id,
                                  ethernet_interface_status_t* out_status) {
    (void)interface_id;
    if (!out_status) return ERR_NULL;
    return ERR_NOT_FOUND;
}

int ethernet_validate_state(void) {
    return OK;
}

int ethernet_quiesce(void) {
    return OK;
}

int ethernet_set_quiescing(uint8_t active) {
    (void)active;
    return OK;
}

int arp_init(void) {
    fake_arp_status.initialized = 1U;
    return OK;
}

int arp_configure(const char* interface_id, const uint8_t* local_mac,
                  uint32_t local_ip) {
    (void)interface_id;
    (void)local_mac;
    (void)local_ip;
    return ERR_UNAVAILABLE;
}

int arp_unconfigure(void) {
    return OK;
}

int arp_resolve(uint32_t ip_address, uint8_t* out_mac,
                uint8_t* out_resolved) {
    (void)ip_address;
    if (!out_mac || !out_resolved) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int arp_maintain(void) {
    return OK;
}

int arp_clear(void) {
    return OK;
}

int arp_get_status(arp_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_arp_status;
    return OK;
}

int arp_get_cache_entry(uint32_t index, arp_cache_entry_info_t* out_entry) {
    (void)index;
    if (!out_entry) return ERR_NULL;
    return ERR_NOT_FOUND;
}

int arp_validate_state(void) {
    return OK;
}

uint8_t arp_ipv4_is_valid(uint32_t ip_address) {
    return ip_address != 0U && ip_address != 0xFFFFFFFFU;
}

int ipv4_init(void) {
    fake_ipv4_status.initialized = 1U;
    return OK;
}

int ipv4_configure(const char* interface_id, const uint8_t* local_mac,
                   uint32_t local_ip, uint32_t subnet_mask,
                   uint32_t gateway) {
    (void)interface_id;
    (void)local_mac;
    (void)local_ip;
    (void)subnet_mask;
    (void)gateway;
    return ERR_UNAVAILABLE;
}

int ipv4_unconfigure(void) {
    return OK;
}

int ipv4_register_handler(uint8_t protocol,
                          ipv4_protocol_handler_fn handler) {
    (void)protocol;
    (void)handler;
    return ERR_UNAVAILABLE;
}

int ipv4_send(uint32_t destination_ip, uint8_t protocol,
              const uint8_t* payload, uint16_t payload_length,
              uint8_t* out_sent) {
    (void)destination_ip;
    (void)protocol;
    (void)payload;
    (void)payload_length;
    if (!out_sent) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int ipv4_send_limited_broadcast(const char* interface_id,
                                uint32_t source_ip, uint8_t protocol,
                                const uint8_t* payload,
                                uint16_t payload_length, uint8_t* out_sent) {
    (void)interface_id;
    (void)source_ip;
    (void)protocol;
    (void)payload;
    (void)payload_length;
    if (!out_sent) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int ipv4_get_status(ipv4_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_ipv4_status;
    return OK;
}

int ipv4_validate_state(void) {
    return OK;
}

uint8_t ipv4_address_is_unicast(uint32_t ip_address) {
    return ip_address != 0U && ip_address != 0xFFFFFFFFU;
}

uint8_t ipv4_mask_is_valid(uint32_t subnet_mask) {
    return subnet_mask == 0xFFFFFF00U;
}

int icmp_init(void) {
    fake_icmp_status.initialized = 1U;
    return OK;
}

int icmp_send_echo(uint32_t destination_ip, uint16_t sequence,
                   uint32_t timeout_ticks, uint8_t* out_replied) {
    (void)destination_ip;
    (void)sequence;
    (void)timeout_ticks;
    if (!out_replied) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int icmp_get_status(icmp_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_icmp_status;
    return OK;
}

int icmp_maintain(void) {
    return OK;
}

int icmp_reset(void) {
    return OK;
}

int icmp_validate_state(void) {
    return OK;
}

int udp_init(void) {
    fake_udp_status.initialized = 1U;
    return OK;
}

int udp_bind(uint16_t local_port, uint8_t flags, udp_receive_fn callback,
             udp_endpoint_handle_t* out_handle) {
    (void)local_port;
    (void)flags;
    (void)callback;
    if (!out_handle) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int udp_unbind(udp_endpoint_handle_t handle) {
    (void)handle;
    return OK;
}

int udp_send(udp_endpoint_handle_t handle, uint32_t destination_ip,
             uint16_t destination_port, const uint8_t* payload,
             uint16_t payload_length, uint8_t* out_sent) {
    (void)handle;
    (void)destination_ip;
    (void)destination_port;
    (void)payload;
    (void)payload_length;
    if (!out_sent) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int udp_send_limited_broadcast(udp_endpoint_handle_t handle,
                               const char* interface_id,
                               uint32_t source_ip,
                               uint16_t destination_port,
                               const uint8_t* payload,
                               uint16_t payload_length, uint8_t* out_sent) {
    (void)handle;
    (void)interface_id;
    (void)source_ip;
    (void)destination_port;
    (void)payload;
    (void)payload_length;
    if (!out_sent) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int udp_get_status(udp_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_udp_status;
    return OK;
}

int udp_validate_state(void) {
    return OK;
}

int dhcp_init(void) {
    fake_dhcp_status.initialized = 1U;
    return OK;
}

int dhcp_acquire(const char* interface_id, const uint8_t* local_mac) {
    (void)interface_id;
    (void)local_mac;
    return ERR_UNAVAILABLE;
}

int dhcp_renew(void) {
    return ERR_UNAVAILABLE;
}

int dhcp_release(uint8_t* out_sent) {
    if (!out_sent) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int dhcp_reset(void) {
    return OK;
}

int dhcp_maintain(void) {
    return OK;
}

int dhcp_take_event(dhcp_event_t* out_event, dhcp_lease_t* out_lease) {
    if (!out_event || !out_lease) return ERR_NULL;
    *out_event = DHCP_EVENT_NONE;
    memset(out_lease, 0, sizeof(*out_lease));
    return OK;
}

int dhcp_complete_event(dhcp_event_t event, int result) {
    (void)event;
    (void)result;
    return OK;
}

int dhcp_get_status(dhcp_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_dhcp_status;
    return OK;
}

int dhcp_validate_state(void) {
    return OK;
}

int dns_init(void) {
    fake_dns_status.initialized = 1U;
    return OK;
}

int dns_configure(uint32_t server_ip) {
    (void)server_ip;
    return ERR_UNAVAILABLE;
}

int dns_unconfigure(void) {
    return OK;
}

int dns_resolve(const char* name, uint32_t* out_ip, uint8_t* out_resolved) {
    (void)name;
    if (!out_ip || !out_resolved) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int dns_maintain(void) {
    return OK;
}

int dns_reset(void) {
    return OK;
}

int dns_clear(void) {
    return OK;
}

int dns_get_status(dns_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_dns_status;
    return OK;
}

int dns_get_cache_entry(uint32_t index, dns_cache_entry_info_t* out_entry) {
    (void)index;
    if (!out_entry) return ERR_NULL;
    return ERR_NOT_FOUND;
}

int dns_validate_state(void) {
    return OK;
}

int tcp_init(void) {
    fake_tcp_status.initialized = 1U;
    return OK;
}

int tcp_connect(uint16_t local_port, uint32_t remote_ip,
                uint16_t remote_port, uint16_t receive_window,
                tcp_event_fn callback, tcp_connection_handle_t* out_handle) {
    (void)local_port;
    (void)remote_ip;
    (void)remote_port;
    (void)receive_window;
    (void)callback;
    if (!out_handle) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int tcp_send(tcp_connection_handle_t handle, const uint8_t* data,
             uint16_t length, uint16_t* out_accepted) {
    (void)handle;
    (void)data;
    (void)length;
    if (!out_accepted) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int tcp_set_receive_window(tcp_connection_handle_t handle,
                           uint16_t receive_window) {
    (void)handle;
    (void)receive_window;
    return ERR_UNAVAILABLE;
}

int tcp_close(tcp_connection_handle_t handle) {
    (void)handle;
    return OK;
}

int tcp_abort(tcp_connection_handle_t handle) {
    (void)handle;
    return OK;
}

int tcp_maintain(void) {
    return OK;
}

int tcp_reset(void) {
    return OK;
}

int tcp_get_status(tcp_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_tcp_status;
    return OK;
}

int tcp_get_connection_info(uint32_t index, tcp_connection_info_t* out_info) {
    (void)index;
    if (!out_info) return ERR_NULL;
    return ERR_NOT_FOUND;
}

int tcp_validate_state(void) {
    return OK;
}

int socket_init(void) {
    return OK;
}

int net_socket_init(void) {
    fake_net_socket_status.initialized = 1U;
    return OK;
}

int net_socket_open(net_socket_type_t type, net_socket_handle_t* out_handle) {
    (void)type;
    if (!out_handle) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int net_socket_connect(net_socket_handle_t handle, uint32_t remote_ip,
                       uint16_t remote_port) {
    (void)handle;
    (void)remote_ip;
    (void)remote_port;
    return ERR_UNAVAILABLE;
}

int net_socket_send(net_socket_handle_t handle, const uint8_t* data,
                    uint16_t length, uint16_t* out_written) {
    (void)handle;
    (void)data;
    (void)length;
    if (!out_written) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int net_socket_receive(net_socket_handle_t handle, uint8_t* buffer,
                       uint16_t capacity, uint16_t* out_read,
                       uint8_t* out_eof) {
    (void)handle;
    (void)buffer;
    (void)capacity;
    if (!out_read || !out_eof) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int net_socket_wait(net_socket_handle_t handle, net_socket_event_mask_t events,
                    uint32_t timeout_ticks, net_socket_event_mask_t* out_events,
                    wait_reason_t* out_reason) {
    (void)handle;
    (void)events;
    (void)timeout_ticks;
    if (!out_events || !out_reason) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int net_socket_close(net_socket_handle_t handle) {
    (void)handle;
    return OK;
}

int net_socket_abort(net_socket_handle_t handle) {
    (void)handle;
    return OK;
}

int net_socket_maintain(void) {
    return OK;
}

int net_socket_reset(void) {
    return OK;
}

int net_socket_get_status(net_socket_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_net_socket_status;
    return OK;
}

int net_socket_get_info(uint32_t index, net_socket_info_t* out_info) {
    (void)index;
    if (!out_info) return ERR_NULL;
    return ERR_NOT_FOUND;
}

int net_socket_get_handle_info(net_socket_handle_t handle,
                               net_socket_info_t* out_info) {
    (void)handle;
    if (!out_info) return ERR_NULL;
    return ERR_NOT_FOUND;
}

int net_socket_validate_state(void) {
    return OK;
}

int http_init(void) {
    fake_http_status.initialized = 1U;
    return OK;
}

int http_get_start(const char* url) {
    (void)url;
    return ERR_UNAVAILABLE;
}

int http_get_stream_start(const char* url, uint32_t body_limit,
                          http_body_sink_t sink, void* context) {
    (void)url;
    (void)body_limit;
    (void)sink;
    (void)context;
    return ERR_UNAVAILABLE;
}

int http_get_start_ex(const char* url, const http_request_options_t* options) {
    (void)url;
    (void)options;
    return ERR_UNAVAILABLE;
}

int http_get_stream_start_ex(const char* url, uint32_t body_limit,
                             http_body_sink_t sink, void* context,
                             const http_request_options_t* options) {
    (void)url;
    (void)body_limit;
    (void)sink;
    (void)context;
    (void)options;
    return ERR_UNAVAILABLE;
}

int http_maintain(void) {
    return OK;
}

int http_reset(void) {
    return OK;
}

int http_get_status(http_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_http_status;
    return OK;
}

int http_get_body(const uint8_t** out_body, uint32_t* out_length) {
    if (!out_body || !out_length) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int http_validate_state(void) {
    return OK;
}

int e1000_init(const pci_device_t* pci, const char* interface_id,
               ethernet_interface_t* out_interface) {
    (void)pci;
    (void)interface_id;
    if (!out_interface) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int rtl8139_init(const pci_device_t* pci, const char* interface_id,
                 ethernet_interface_t* out_interface) {
    (void)pci;
    (void)interface_id;
    if (!out_interface) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int rtl8811cu_init(const usb_device_info_t* device,
                   ethernet_interface_t* out_interface) {
    (void)device;
    if (!out_interface) return ERR_NULL;
    return ERR_UNAVAILABLE;
}

int route_validate_state(void) {
    return OK;
}

static void reset_fixtures(void) {
    memset(fake_pci, 0, sizeof(fake_pci));
    memset(&fake_ethernet_status, 0, sizeof(fake_ethernet_status));
    memset(&fake_arp_status, 0, sizeof(fake_arp_status));
    memset(&fake_ipv4_status, 0, sizeof(fake_ipv4_status));
    memset(&fake_icmp_status, 0, sizeof(fake_icmp_status));
    memset(&fake_udp_status, 0, sizeof(fake_udp_status));
    memset(&fake_dhcp_status, 0, sizeof(fake_dhcp_status));
    memset(&fake_dns_status, 0, sizeof(fake_dns_status));
    memset(&fake_tcp_status, 0, sizeof(fake_tcp_status));
    memset(&fake_net_socket_status, 0, sizeof(fake_net_socket_status));
    memset(&fake_http_status, 0, sizeof(fake_http_status));
    fake_pci_count = 2U;
    fake_pci[0].vendor_id = 0x1234U;
    fake_pci[0].device_id = 0x5678U;
    fake_pci[0].class = 0x03U;
    fake_pci[0].bus = 0U;
    fake_pci[0].device = 1U;
    fake_pci[0].function = 0U;
    fake_pci[1].vendor_id = 0x8086U;
    fake_pci[1].device_id = 0x100EU;
    fake_pci[1].class = 0x02U;
    fake_pci[1].bus = 1U;
    fake_pci[1].device = 2U;
    fake_pci[1].function = 0U;
    fake_ticks = 100U;
}

static int test_before_initialization(void) {
    network_manager_status_t status;
    network_interface_info_t info;
    uint32_t count;

    if (network_manager_get_status(NULL) != ERR_NULL ||
        network_manager_get_status(&status) != ERR_STATE ||
        network_manager_get_count(NULL) != ERR_NULL ||
        network_manager_get_count(&count) != ERR_STATE ||
        network_manager_get_interface(0U, NULL) != ERR_NULL ||
        network_manager_get_interface(0U, &info) != ERR_STATE) return 1;
    if (network_manager_refresh() != ERR_STATE ||
        network_manager_set_quiescing(1U) != ERR_STATE ||
        network_manager_quiesce() != ERR_UNAVAILABLE) return 2;
    if (network_manager_poll(NULL) != ERR_NULL ||
        network_manager_poll(&count) != ERR_STATE) return 3;
    if (network_manager_find(NULL, &info) != ERR_NULL ||
        network_manager_find("missing", NULL) != ERR_NULL ||
        network_manager_find("missing", &info) != ERR_STATE) return 4;
    if (network_manager_format_text(NULL, NULL) != ERR_NULL ||
        network_manager_send_diagnostic(NULL) != ERR_NULL ||
        network_manager_configure_arp(NULL, 1U) != ERR_NULL ||
        network_manager_configure_ipv4(NULL, 1U, 0xFFFFFF00U, 0U) != ERR_NULL ||
        network_manager_acquire_dhcp(NULL) != ERR_NULL ||
        network_manager_release_dhcp(NULL) != ERR_NULL ||
        network_manager_get_ethernet_diagnostic(NULL, NULL) != ERR_NULL) return 5;
    if (network_manager_renew_dhcp() != ERR_STATE ||
        network_manager_configure_dns(0x08080808U) != ERR_STATE) return 6;
    return 0;
}

static int test_inventory_and_offline_contract(void) {
    network_manager_status_t status;
    network_interface_info_t info;
    network_interface_text_t text;
    network_ethernet_diagnostic_t diagnostic;
    uint32_t count = 0U;
    uint32_t processed = 99U;
    uint8_t sent = 0U;

    reset_fixtures();
    if (recovery_init() != OK || network_manager_init() != OK) return 20;
    if (network_manager_get_status(&status) != OK) return 21;
    if (!status.initialized || status.interface_count != 1U ||
        status.recognized_count != 1U || status.active_count != 0U ||
        status.driver_error_count != 1U || status.last_error != ERR_UNAVAILABLE ||
        status.ethernet_available || status.ipv4_available ||
        status.sockets_available) return 21;
    if (network_manager_get_count(&count) != OK || count != 1U ||
        network_manager_get_interface(count, &info) != ERR_INVALID ||
        network_manager_find("missing", &info) != ERR_NOT_FOUND) return 22;
    if (network_manager_get_interface(0U, &info) != OK ||
        info.model != NETWORK_ADAPTER_E1000 ||
        info.state != NETWORK_INTERFACE_DRIVER_ERROR ||
        info.driver_error != ERR_UNAVAILABLE) return 23;
    if (network_manager_format_text(&info, &text) != OK ||
        strcmp(text.id, "net-pci-01:02.0") != 0 ||
        strcmp(text.driver, "e1000 (erro)") != 0) return 24;
    if (network_manager_send_diagnostic(text.id) != ERR_UNAVAILABLE ||
        network_manager_configure_arp(text.id, 0xC0A80102U) != ERR_UNAVAILABLE ||
        network_manager_configure_ipv4(text.id, 0xC0A80102U,
                                       0xFFFFFF00U, 0U) != ERR_UNAVAILABLE ||
        network_manager_acquire_dhcp(text.id) != ERR_UNAVAILABLE ||
        network_manager_get_ethernet_diagnostic(text.id, &diagnostic) !=
            ERR_UNAVAILABLE) return 25;
    if (network_manager_release_dhcp(&sent) != ERR_UNAVAILABLE ||
        network_manager_renew_dhcp() != ERR_STATE ||
        network_manager_configure_dns(0x08080808U) != ERR_STATE) return 26;
    if (network_manager_set_quiescing(1U) != OK ||
        network_manager_poll(&processed) != ERR_UNAVAILABLE ||
        network_manager_set_quiescing(0U) != OK ||
        network_manager_poll(&processed) != OK || processed != 0U ||
        network_manager_quiesce() != OK) return 27;
    if (network_manager_refresh() != OK ||
        network_manager_get_status(&status) != OK ||
        status.interface_count != 1U || status.driver_error_count != 1U) return 28;
    return 0;
}

static int test_format_and_names(void) {
    network_interface_info_t info;
    network_interface_text_t text;

    memset(&info, 0, sizeof(info));
    info.transport = NETWORK_TRANSPORT_USB;
    info.model = NETWORK_ADAPTER_RTL8811CU;
    info.state = NETWORK_INTERFACE_DRIVER_MISSING;
    info.bus = 3U;
    info.device = 4U;
    info.function = 1U;
    info.usb_port = 2U;
    if (network_manager_format_text(&info, &text) != OK ||
        strcmp(text.id, "net-usb-03:04.1-p2") != 0 ||
        strcmp(text.name, "Realtek RTL8811CU (USB)") != 0 ||
        strcmp(text.driver, "rtl8811cu (pendente)") != 0) return 40;
    info.model = NETWORK_ADAPTER_RTL8139;
    info.state = NETWORK_INTERFACE_ACTIVE;
    if (network_manager_format_text(&info, &text) != OK ||
        strcmp(text.name, "Realtek RTL8139") != 0 ||
        strcmp(text.driver, "rtl8139 (ativo)") != 0) return 41;
    info.model = NETWORK_ADAPTER_UNKNOWN;
    info.transport = NETWORK_TRANSPORT_PCI;
    if (network_manager_format_text(&info, &text) != OK ||
        strcmp(text.name, "Controlador de rede PCI") != 0 ||
        strcmp(text.driver, "nao suportado") != 0) return 42;
    if (strcmp(network_manager_model_name(NETWORK_ADAPTER_E1000), "E1000") != 0 ||
        strcmp(network_manager_model_name(NETWORK_ADAPTER_RTL8139), "RTL8139") != 0 ||
        strcmp(network_manager_model_name(NETWORK_ADAPTER_RTL8811CU),
               "RTL8811CU") != 0 ||
        strcmp(network_manager_model_name((network_adapter_model_t)99),
               "DESCONHECIDO") != 0) return 43;
    if (strcmp(network_manager_interface_state_name(
                   NETWORK_INTERFACE_DRIVER_MISSING), "DRIVER AUSENTE") != 0 ||
        strcmp(network_manager_interface_state_name(
                   NETWORK_INTERFACE_UNSUPPORTED), "NAO SUPORTADA") != 0 ||
        strcmp(network_manager_interface_state_name(
                   NETWORK_INTERFACE_ACTIVE), "ATIVA") != 0 ||
        strcmp(network_manager_interface_state_name(
                   NETWORK_INTERFACE_DRIVER_ERROR), "ERRO NO DRIVER") != 0 ||
        strcmp(network_manager_interface_state_name(
                   (network_interface_state_t)99), "DESCONHECIDO") != 0) return 44;
    if (strcmp(network_manager_link_state_name(NETWORK_LINK_UNKNOWN),
               "DESCONHECIDO") != 0 ||
        strcmp(network_manager_link_state_name(NETWORK_LINK_DOWN), "DOWN") != 0 ||
        strcmp(network_manager_link_state_name(NETWORK_LINK_UP), "UP") != 0 ||
        strcmp(network_manager_ipv4_source_name(NETWORK_IPV4_SOURCE_NONE),
               "NONE") != 0 ||
        strcmp(network_manager_ipv4_source_name(NETWORK_IPV4_SOURCE_STATIC),
               "STATIC") != 0 ||
        strcmp(network_manager_ipv4_source_name(NETWORK_IPV4_SOURCE_DHCP),
               "DHCP") != 0 ||
        strcmp(network_manager_ipv4_source_name((network_ipv4_source_t)99),
               "DESCONHECIDO") != 0) return 45;
    return 0;
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    log_init();
    if (!result) result = test_before_initialization();
    if (!result) result = test_inventory_and_offline_contract();
    if (!result) result = test_format_and_names();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        printf("NETWORK_MANAGER_HOST_FAIL:%d\n", result);
        return result;
    }
    printf("NETWORK_MANAGER_HOST_PASS\n");
    return 0;
}
