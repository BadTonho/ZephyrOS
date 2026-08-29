#include "apps/shell.h"
#include "apps/shell_input.h"
#include "apps/shell_dispatch.h"
#include "apps/shell_job.h"
#include "core/video.h"
#include "core/keyboard.h"
#include "fs/fs.h"
#include "fs/storage.h"
#include "fs/file_index.h"
#include "core/memory.h"
#include "core/timer.h"
#include "core/wait.h"
#include "process/process.h"
#include "drivers/ata.h"
#include "drivers/speaker.h"
#include "process/thread.h"
#include "apps/taskmanager.h"
#include "ui/taskbar.h"
#include "ui/desktop.h"
#include "ui/settings.h"
#include "ui/updater.h"
#include "ui/appstore.h"
#include "ui/wm.h"
#include "memory/compress.h"
#include "apps/mediaplayer.h"
#include "apps/editor.h"
#include "ui/filemanager.h"
#include "ui/icons.h"
#include "memory/paging.h"
#include "core/string.h"
#include "core/errors.h"
#include "core/log.h"
#include "drivers/mouse.h"
#include "ui/gui.h"
#include "apps/guitest.h"
#include "core/recovery.h"
#include "core/device_manager.h"
#include "core/usb_manager.h"
#include "drivers/usb_msc.h"
#include "core/arp.h"
#include "core/dhcp.h"
#include "core/dns.h"
#include "core/http.h"
#include "core/ipv4.h"
#include "core/icmp.h"
#include "core/net_buffer.h"
#include "core/net_socket.h"
#include "core/tcp.h"
#include "core/udp.h"
#include "core/network_manager.h"
#include "core/power.h"
#include "core/app_api.h"
#include "core/app_catalog.h"
#include "core/app_builtin.h"
#include "core/app_loader.h"
#include "core/app_package.h"
#include "core/app_remote.h"
#include "core/update.h"
#include "core/update_remote.h"
#include "core/update_remote_config.h"
#include "core/syscall.h"
#include "drivers/idt.h"
#include "drivers/pci.h"
#include "drivers/vesa.h"
#include "drivers/font.h"
#include "drivers/acpi.h"
#include "ui/display.h"
#include "apps/shell_command_utils.h"
#include "apps/shell_runtime.h"

#define SHELL_Q2CHECK_FAULT_RUNS 2U
#define SHELL_HOSTED_DEFAULT_CONTENT_WIDTH 880
#define SHELL_HOSTED_DEFAULT_CONTENT_HEIGHT 560
#define SHELL_HOSTED_FRAME_WIDTH 4
#define SHELL_HOSTED_FRAME_HEIGHT 28
#define SHELL_IPV4_TEXT_SIZE 16U
#define SHELL_IPV4_OCTET_COUNT 4U
#define SHELL_IPV4_OCTET_MAX 255U
#define SHELL_IPV4_OCTET_BITS 8U
#define SHELL_IPV4_OCTET_DIGITS 3U
#define SHELL_NET_QEMU_REPLY_IPV4 0x0A000202U
#define SHELL_NET_QEMU_TIMEOUT_IPV4 0x0A0002FEU
#define SHELL_NET_QEMU_SUBNET_MASK 0xFFFFFF00U
#define SHELL_NET_CHECK_WAIT_SECONDS 5U
#define SHELL_NET_CHECK_EXPECTED_ATTEMPTS 3U
#define SHELL_PING_WAIT_EXTRA_SECONDS 5U
#define SHELL_DHCP_WAIT_SECONDS 20U
#define SHELL_DNS_WAIT_SECONDS 20U
#define SHELL_TCP_WAIT_SECONDS 45U
#define SHELL_HTTP_WAIT_SECONDS 50U
#define SHELL_HTTP_SUITE_ATTEMPTS 3U
#define SHELL_SOCKET_WAIT_SLICE_TICKS 1U
#define SHELL_HTTP_PREVIEW_SIZE 512U
#define SHELL_NETWORK_REQUIRED_IPV4_HANDLERS 3U
#define SHELL_DNS_NAME_SIZE DNS_NAME_BUFFER_SIZE
#define SHELL_MILLISECONDS_PER_SECOND 1000U
#define SHELL_MAX_TICK_INTERVAL 0xFFFFFFFFU
#define SHELL_NOINLINE __attribute__((noinline))
#define SHELL_UPDATE_SCANCODE_ESCAPE 0x01U
#define SHELL_UPDATE_SCANCODE_F12 0x58U
#define SHELL_INDEX_ACTION_SIZE 16U
#define SHELL_FS_DIRECTORY_ATTRIBUTE 0x10U
#define SHELL_LOG_TAIL_DEFAULT 10U
#define SHELL_LOG_TAIL_MAXIMUM 16U
#define SHELL_WHEEL_SCROLL_LINES 3
#define APP_CHECK_DEMO_PATH "DEMO.ZAP"
#define APP_CHECK_DEMO_MAX_CLEANUP 4U
#define APP_INPUT_TEST_PATH "INPUT.ZAP"
#define APP_INPUT_EVENT_OFFSET 128U
#define APP_INPUT_EVENT_DATA1_OFFSET (APP_INPUT_EVENT_OFFSET + 4U)
#define SHELL_APP_OUTPUTTEST_FAILURE_CODE 1U
#define SHELL_Q2CHECK_FIRST_FAULT_INDEX 0U
#define SHELL_Q2CHECK_SECOND_FAULT_INDEX 1U
#define SHELL_Q2CHECK_EXPECTED_FAULT_VECTOR 14U
#define SHELL_Q2CHECK_EXPECTED_FAULT_ERROR 4U
#define SHELL_MEMCHECK_BLOCK_A 96U
#define SHELL_MEMCHECK_BLOCK_B 160U
#define SHELL_MEMCHECK_BLOCK_C 224U
#define SHELL_REGCHECK_NON_ATA_DEVICE_COUNT 7U
#define SHELL_REGCHECK_PCI_NETWORK_CLASS 0x02U
#define SHELL_REGCHECK_ACPI_SDT_HEADER_SIZE 36U
#define SHELL_REGCHECK_ACPI_MAX_TABLE_SIZE (1024U * 1024U)
#define SHELL_REGCHECK_ACPI_MAX_ROOT_ENTRIES 256U

static uint8_t shell_network_job_inner_failed;

static int shell_network_job_block_tick(void) {
    if (shell_job_is_active()) {
        shell_job_pump_events();
        if (shell_job_cancel_requested()) return 1;
    }
    process_yield();
    return shell_job_is_active() && shell_job_cancel_requested();
}

typedef enum {
    SHELL_Q2CHECK_IDLE = 0,
    SHELL_Q2CHECK_FIRST_FAULT,
    SHELL_Q2CHECK_SECOND_FAULT
} shell_q2check_state_t;

typedef enum {
    SHELL_REGCHECK_IDLE = 0,
    SHELL_REGCHECK_WAIT_DEMO,
    SHELL_REGCHECK_WAIT_CANCEL
} shell_regcheck_state_t;

typedef struct {
    uint32_t initial_focus;
    uint32_t initial_user_count;
    uint32_t initial_zombie_count;
    uint32_t initial_fault_count;
    uint32_t expected_pid;
    int logger_result;
    int fault_result[SHELL_Q2CHECK_FAULT_RUNS];
    int summary_result;
    int cleanup_result;
    shell_q2check_state_t state;
} shell_q2check_t;

typedef struct {
    uint32_t initial_focus;
    uint32_t initial_process_count;
    uint32_t initial_user_count;
    uint32_t initial_zombie_count;
    uint32_t initial_user_directories;
    uint32_t initial_user_pages;
    uint32_t expected_pid;
    int health_result;
    int services_result;
    int scheduler_result;
    int memory_result;
    int package_result;
    int thread_result;
    int processes_result;
    int device_scan_result;
    int devices_result;
    int usb_result;
    int network_result;
    int acpi_result;
    int power_result;
    int index_result;
    int loader_result;
    int cancellation_result;
    int cleanup_result;
    uint8_t full_mode;
    uint8_t loader_started;
    uint8_t cancellation_started;
    shell_regcheck_state_t state;
} shell_regcheck_t;

typedef struct {
    uint8_t reply;
    uint8_t cache_hit;
    uint8_t timeout;
    uint8_t ipv4;
    uint8_t icmp;
    uint8_t polling;
    uint8_t invariants;
} shell_net_qemu_check_t;

typedef struct {
    uint8_t udp;
    uint8_t dhcp;
    uint8_t lease;
    uint8_t dns;
    uint8_t dns_cache;
    uint8_t icmp;
    uint8_t polling;
    uint8_t invariants;
} shell_net_qemu_dhcp_check_t;

typedef struct {
    uint8_t dhcp;
    uint8_t dns;
    uint8_t tcp;
    uint8_t checksum;
    uint8_t sockets;
    uint8_t http;
    uint8_t closing;
    uint8_t polling;
    uint8_t invariants;
} shell_net_qemu_tcp_check_t;

typedef struct {
    uint32_t ticks;
    keyboard_metrics_t keyboard;
    ipc_stats_t ipc;
    scheduler_stats_t scheduler;
    vesa_metrics_t vesa;
    memory_heap_stats_t heap;
    memory_pmm_stats_t pmm;
    paging_user_stats_t paging_user;
    paging_boot_stats_t paging_boot;
} shell_kmetrics_snapshot_t;

typedef struct {
    shell_kmetrics_snapshot_t snapshot;
    uint8_t valid;
} shell_kmetrics_baseline_t;

typedef struct {
    char operation[16];
    char first[FS_MAX_PATH];
    char second[UPDATE_REMOTE_URL_SIZE];
    char third[32];
    char extra[2];
    update_remote_status_t remote_status;
    update_remote_options_t remote_options;
    update_remote_result_t remote_result;
} shell_update_workspace_t;

typedef struct {
    char operation[16];
    char value[FS_MAX_PATH];
    char option[16];
    char extra[16];
    app_catalog_entry_t entry;
    app_package_action_result_t action;
    app_package_status_t status;
    app_launch_info_t launch;
    uint32_t pid;
    char remote_url[APP_REMOTE_URL_SIZE];
    char remote_token[APP_REMOTE_URL_SIZE];
    app_remote_entry_t remote_entry;
    app_remote_status_t remote_status;
    app_remote_options_t remote_options;
    app_remote_result_t remote_result;
} shell_store_workspace_t;

typedef struct {
    char query[FILE_INDEX_QUERY_SIZE];
    file_index_result_t results[FILE_INDEX_MAX_RESULTS];
    file_index_search_status_t status;
} shell_index_workspace_t;


static char shell_http_command_url[HTTP_URL_BUFFER_SIZE];
static char shell_http_preview[SHELL_HTTP_PREVIEW_SIZE + 1U];
static http_status_t shell_http_command_status;
static http_status_t shell_http_wait_status;

static int shell_regcheck_same_network(
    const network_interface_info_t* left,
    const network_interface_info_t* right) {
    if (left->transport != right->transport ||
        left->model != right->model || left->state != right->state ||
        left->link != right->link || left->vendor_id != right->vendor_id ||
        left->device_id != right->device_id ||
        left->class_code != right->class_code ||
        left->subclass_code != right->subclass_code ||
        left->prog_if != right->prog_if || left->revision != right->revision ||
        left->bus != right->bus || left->device != right->device ||
        left->function != right->function || left->irq != right->irq ||
        left->usb_port != right->usb_port ||
        left->usb_address != right->usb_address ||
        left->usb_revision != right->usb_revision ||
        left->usb_endpoint_count != right->usb_endpoint_count ||
        kstrcmp(left->usb_device_id, right->usb_device_id) != 0) {
        return 0;
    }
    for (uint32_t bar = 0; bar < NETWORK_PCI_BAR_COUNT; bar++) {
        if (left->bars[bar] != right->bars[bar]) return 0;
    }
    return 1;
}

static int shell_regcheck_validate_network_entry(
    const network_interface_info_t* info, uint32_t* recognized,
    uint32_t* active, uint32_t* driver_errors,
    uint32_t* l3_count, uint32_t* dhcp_count) {
    network_interface_info_t found;
    network_interface_text_t text;
    int result;

    if (!info || !recognized || !active || !driver_errors ||
        !l3_count || !dhcp_count) {
        LOG_ERROR("SHELL", "RegCheck Full recebeu entrada Network nula");
        return ERR_NULL;
    }
    if (info->transport > NETWORK_TRANSPORT_USB ||
        (info->transport == NETWORK_TRANSPORT_PCI &&
         info->class_code != SHELL_REGCHECK_PCI_NETWORK_CLASS) ||
        (info->transport == NETWORK_TRANSPORT_USB &&
         (info->model != NETWORK_ADAPTER_RTL8811CU ||
          !info->usb_device_id[0])) ||
        info->model > NETWORK_ADAPTER_RTL8811CU ||
        info->state > NETWORK_INTERFACE_DRIVER_ERROR ||
        info->link > NETWORK_LINK_UP ||
        (info->state != NETWORK_INTERFACE_ACTIVE &&
         info->link != NETWORK_LINK_UNKNOWN) ||
        (info->state == NETWORK_INTERFACE_DRIVER_ERROR &&
         info->driver_error == OK) ||
        (info->model == NETWORK_ADAPTER_UNKNOWN &&
         info->state != NETWORK_INTERFACE_UNSUPPORTED) ||
        (info->model != NETWORK_ADAPTER_UNKNOWN &&
         info->state == NETWORK_INTERFACE_UNSUPPORTED) ||
        (info->state == NETWORK_INTERFACE_ACTIVE &&
         !info->ethernet_attached) ||
        (info->ethernet_attached &&
         info->state != NETWORK_INTERFACE_ACTIVE) ||
        (info->l3_active && !info->ethernet_attached) ||
        (info->dhcp_pending && !info->ethernet_attached)) {
        LOG_ERROR("SHELL", "RegCheck Full detectou entrada Network invalida");
        return ERR_STATE;
    }
    result = network_manager_format_text(info, &text);
    if (result != OK || !text.id[0] || !text.name[0] || !text.driver[0]) {
        LOG_ERROR("SHELL", "RegCheck Full nao formatou entrada Network");
        return result == OK ? ERR_STATE : result;
    }
    result = network_manager_find(text.id, &found);
    if (result != OK || !shell_regcheck_same_network(info, &found)) {
        LOG_ERROR("SHELL", "RegCheck Full detectou ID Network instavel");
        return result == OK ? ERR_STATE : result;
    }
    if (info->model != NETWORK_ADAPTER_UNKNOWN) (*recognized)++;
    if (info->state == NETWORK_INTERFACE_ACTIVE) (*active)++;
    if (info->state == NETWORK_INTERFACE_DRIVER_ERROR) {
        (*driver_errors)++;
    }
    if (info->l3_active) (*l3_count)++;
    if (info->dhcp_pending) (*dhcp_count)++;
    return OK;
}

static int shell_regcheck_validate_network_recovery(
    const network_manager_status_t* status) {
    const recovery_component_t* component =
        recovery_get(RECOVERY_COMPONENT_NETWORK);
    recovery_state_t expected_state;
    int expected_error;

    if (!component || !status) {
        LOG_ERROR("SHELL", "RegCheck Full nao consultou recovery Network");
        return ERR_STATE;
    }
    if (status->partial) {
        expected_state = RECOVERY_STATE_DEGRADED;
        expected_error = ERR_OVERFLOW;
    } else if (status->driver_error_count) {
        expected_state = RECOVERY_STATE_DEGRADED;
        expected_error = status->last_error;
    } else if (status->active_count &&
               status->ethernet_available &&
               status->arp_available &&
               status->ipv4_available &&
               status->icmp_available &&
               status->udp_available &&
               status->dhcp_available &&
               status->dns_available &&
               status->tcp_available &&
               status->sockets_available &&
               status->http_available) {
        expected_state = RECOVERY_STATE_READY;
        expected_error = OK;
    } else if (status->active_count) {
        expected_state = RECOVERY_STATE_DEGRADED;
        expected_error = status->last_error;
    } else if (status->interface_count) {
        expected_state = RECOVERY_STATE_DEGRADED;
        expected_error = status->last_error;
    } else {
        expected_state = RECOVERY_STATE_DISABLED;
        expected_error = ERR_NOT_FOUND;
    }
    if (component->state != expected_state ||
        component->last_error != expected_error) {
        LOG_ERROR("SHELL", "RegCheck Full detectou recovery Network incoerente");
        return ERR_STATE;
    }
    return OK;
}

static int shell_regcheck_validate_arp(
    const network_manager_status_t* network_status) {
    arp_status_t status;
    ethernet_status_t ethernet_status;
    int result;

    if (!network_status) {
        LOG_ERROR("SHELL", "RegCheck Full recebeu estado ARP nulo");
        return ERR_NULL;
    }
    result = arp_get_status(&status);
    if (result != OK ||
        ethernet_get_status(&ethernet_status) != OK ||
        arp_validate_state() != OK ||
        (network_status->arp_available && !status.initialized) ||
        ethernet_status.handler_count >
            ETHERNET_PROTOCOL_HANDLER_CAPACITY ||
        (network_status->arp_available &&
         !ethernet_status.handler_count) ||
        (network_status->arp_configured !=
         (uint8_t)(network_status->arp_available && status.configured)) ||
        status.cache_entries > ARP_CACHE_CAPACITY ||
        status.cache_entries != status.incomplete_entries +
                                status.resolved_entries +
                                status.failed_entries) {
        LOG_ERROR("SHELL", "RegCheck Full detectou estado ARP invalido");
        return result == OK ? ERR_STATE : result;
    }
    return OK;
}

static int shell_regcheck_validate_ipv4_icmp(
    const network_manager_status_t* network_status) {
    ipv4_status_t ipv4_status;
    icmp_status_t icmp_status;
    int result;

    if (!network_status) {
        LOG_ERROR("SHELL", "RegCheck Full recebeu estado IPv4 nulo");
        return ERR_NULL;
    }
    result = ipv4_get_status(&ipv4_status);
    if (result != OK || icmp_get_status(&icmp_status) != OK ||
        ipv4_validate_state() != OK || icmp_validate_state() != OK ||
        (network_status->ipv4_available && !ipv4_status.initialized) ||
        (network_status->icmp_available && !icmp_status.initialized) ||
        (network_status->ipv4_available &&
         !network_status->arp_available) ||
        (network_status->icmp_available &&
         !network_status->ipv4_available) ||
        (network_status->ipv4_configured !=
         (uint8_t)(network_status->ipv4_available &&
                   ipv4_status.configured)) ||
        (network_status->ipv4_configured &&
         (!network_status->arp_configured ||
          ipv4_status.local_ip == 0U ||
          kstrcmp(network_status->l3_interface_id,
                  ipv4_status.interface_id) != 0)) ||
        ipv4_status.handler_count > IPV4_PROTOCOL_HANDLER_CAPACITY ||
        (network_status->icmp_available &&
         !ipv4_status.handler_count)) {
        LOG_ERROR("SHELL", "RegCheck Full detectou estado IPv4/ICMP invalido");
        return result == OK ? ERR_STATE : result;
    }
    return OK;
}

static int shell_regcheck_validate_udp_dhcp_dns(
    const network_manager_status_t* network_status) {
    udp_status_t udp;
    dhcp_status_t dhcp;
    dns_status_t dns;
    int result;

    if (!network_status) {
        LOG_ERROR("SHELL", "RegCheck recebeu estado UDP nulo");
        return ERR_NULL;
    }
    result = udp_get_status(&udp);
    if (result != OK || dhcp_get_status(&dhcp) != OK ||
        dns_get_status(&dns) != OK ||
        udp_validate_state() != OK ||
        dhcp_validate_state() != OK ||
        dns_validate_state() != OK ||
        (network_status->udp_available && !udp.initialized) ||
        (network_status->dhcp_available && !dhcp.initialized) ||
        (network_status->dns_available && !dns.initialized) ||
        (network_status->dhcp_bound !=
         (uint8_t)(dhcp.state == DHCP_STATE_BOUND ||
                   dhcp.state == DHCP_STATE_RENEWING ||
                   dhcp.state == DHCP_STATE_REBINDING)) ||
        (network_status->dns_configured !=
         (uint8_t)(network_status->dns_available &&
                   dns.configured)) ||
        udp.endpoint_count > UDP_ENDPOINT_CAPACITY ||
        dns.cache_entries > DNS_CACHE_CAPACITY) {
        LOG_ERROR("SHELL", "RegCheck detectou UDP/DHCP/DNS invalido");
        return result == OK ? ERR_STATE : result;
    }
    return OK;
}

static int shell_regcheck_validate_tcp_socket_http(
    const network_manager_status_t* network_status) {
    tcp_status_t tcp;
    net_socket_status_t sockets;
    http_status_t http;
    ipv4_status_t ipv4;
    int result;

    if (!network_status) {
        LOG_ERROR("SHELL", "RegCheck recebeu estado TCP nulo");
        return ERR_NULL;
    }
    result = tcp_get_status(&tcp);
    if (result != OK || net_socket_get_status(&sockets) != OK ||
        http_get_status(&http) != OK ||
        ipv4_get_status(&ipv4) != OK ||
        tcp_validate_state() != OK ||
        net_socket_validate_state() != OK ||
        http_validate_state() != OK ||
        (network_status->tcp_available && !tcp.initialized) ||
        (network_status->sockets_available && !sockets.initialized) ||
        (network_status->http_available && !http.initialized) ||
        (network_status->tcp_available &&
         !network_status->ipv4_available) ||
        (network_status->sockets_available &&
         !network_status->tcp_available) ||
        (network_status->http_available &&
         (!network_status->sockets_available ||
          !network_status->dns_available)) ||
        (network_status->tcp_available &&
         ipv4.handler_count <
             SHELL_NETWORK_REQUIRED_IPV4_HANDLERS) ||
        tcp.connection_count > TCP_CONNECTION_CAPACITY ||
        sockets.active_count > NET_SOCKET_CAPACITY ||
        network_status->tcp_connection_count !=
            tcp.connection_count ||
        network_status->socket_count != sockets.active_count) {
        LOG_ERROR("SHELL", "RegCheck detectou TCP/sockets/HTTP invalido");
        return result == OK ? ERR_STATE : result;
    }
    return OK;
}

static int shell_regcheck_validate_network_summary(
    const network_manager_status_t* status,
    const ethernet_status_t* ethernet, uint32_t count) {
    if (!status || !ethernet) {
        LOG_ERROR("SHELL", "RegCheck recebeu resumo Network nulo");
        return ERR_NULL;
    }
    if (!status->initialized || count != status->interface_count ||
        count > NETWORK_MANAGER_MAX_INTERFACES ||
        status->recognized_count > count ||
        status->active_count > count ||
        status->driver_error_count > status->recognized_count ||
        status->active_count > ethernet->interface_count ||
        (status->arp_configured && !status->arp_available) ||
        (status->ipv4_configured && !status->ipv4_available) ||
        (status->dhcp_bound && !status->dhcp_available) ||
        (status->dns_configured &&
         (!status->dns_available || !status->ipv4_configured)) ||
        (status->arp_available &&
         (!status->active_count || !status->ethernet_available)) ||
        (status->ipv4_available &&
         (!status->active_count || !status->arp_available)) ||
        (status->icmp_available &&
         (!status->active_count || !status->ipv4_available)) ||
        (status->udp_available &&
         (!status->active_count || !status->ipv4_available)) ||
        (status->dhcp_available && !status->udp_available) ||
        (status->dns_available && !status->udp_available) ||
        (status->tcp_available && !status->ipv4_available) ||
        (status->sockets_available && !status->tcp_available) ||
        (status->http_available &&
         (!status->sockets_available || !status->dns_available)) ||
        status->tcp_connection_count > TCP_CONNECTION_CAPACITY ||
        status->socket_count > NET_SOCKET_CAPACITY ||
        (status->ipv4_configured &&
         (status->ipv4_source == NETWORK_IPV4_SOURCE_NONE ||
          !status->l3_interface_id[0])) ||
        (!status->ipv4_configured &&
         (status->ipv4_source != NETWORK_IPV4_SOURCE_NONE ||
          status->l3_interface_id[0])) ||
        (status->ethernet_available && !status->active_count) ||
        (status->packet_io_available && !status->active_count) ||
        (!status->packet_io_available && status->active_count) ||
        (status->active_count && status->ethernet_available &&
         status->arp_available && status->ipv4_available &&
         status->icmp_available && status->udp_available &&
         status->dhcp_available && status->dns_available &&
         status->tcp_available && status->sockets_available &&
         status->http_available && !status->partial &&
         !status->driver_error_count && status->last_error != OK) ||
        (!status->active_count && !status->interface_count &&
         status->last_error != ERR_NOT_FOUND)) {
        LOG_ERROR("SHELL", "RegCheck Full detectou estado Network invalido");
        return ERR_STATE;
    }
    return OK;
}

int shell_network_validate_for_checks(void) {
    network_manager_status_t status;
    ethernet_status_t ethernet;
    dhcp_status_t dhcp;
    uint32_t count = 0;
    uint32_t recognized = 0;
    uint32_t active = 0;
    uint32_t driver_errors = 0;
    uint32_t l3_count = 0;
    uint32_t dhcp_count = 0;
    int result = network_manager_get_status(&status);

    if (result != OK ||
        ethernet_get_status(&ethernet) != OK ||
        ethernet_validate_state() != OK ||
        net_buffer_self_test() != OK ||
        dhcp_get_status(&dhcp) != OK ||
        network_manager_get_count(&count) != OK) {
        LOG_ERROR("SHELL", "RegCheck nao consultou estado Network");
        return result == OK ? ERR_STATE : result;
    }
    result = shell_regcheck_validate_network_summary(
        &status, &ethernet, count);
    if (result != OK) return result;
    for (uint32_t index = 0; index < count; index++) {
        network_interface_info_t info;
        network_interface_text_t text;
        uint8_t shared_count = 0;

        result = network_manager_get_interface(index, &info);
        if (result != OK) {
            LOG_ERROR("SHELL", "RegCheck Full nao consultou entrada Network");
            return result;
        }
        result = shell_regcheck_validate_network_entry(
            &info, &recognized, &active, &driver_errors,
            &l3_count, &dhcp_count);
        if (result != OK) return result;
        if (network_manager_format_text(&info, &text) != OK) {
            LOG_ERROR("SHELL", "RegCheck nao formatou ID de rede");
            return ERR_STATE;
        }
        for (uint32_t previous = 0; previous < index; previous++) {
            network_interface_info_t other;
            network_interface_text_t other_text;

            if (network_manager_get_interface(previous, &other) != OK ||
                network_manager_format_text(&other, &other_text) != OK ||
                kstrcmp(text.id, other_text.id) == 0) {
                LOG_ERROR("SHELL", "RegCheck detectou ID de rede duplicado");
                return ERR_STATE;
            }
        }
        if (info.state == NETWORK_INTERFACE_ACTIVE &&
            (idt_get_shared_irq_handler_count(
                 info.irq, &shared_count) != OK ||
             !shared_count ||
             shared_count > IDT_SHARED_IRQ_HANDLER_CAPACITY)) {
            LOG_ERROR("SHELL", "RegCheck detectou IRQ compartilhada invalida");
            return ERR_STATE;
        }
    }
    if (recognized != status.recognized_count ||
        active != status.active_count ||
        driver_errors != status.driver_error_count ||
        l3_count > 1U || dhcp_count > 1U ||
        l3_count != (uint32_t)status.ipv4_configured ||
        dhcp_count != (uint32_t)(
            dhcp.state == DHCP_STATE_SELECTING ||
            dhcp.state == DHCP_STATE_REQUESTING ||
            dhcp.state == DHCP_STATE_APPLYING)) {
        LOG_ERROR("SHELL", "RegCheck Full detectou contadores Network invalidos");
        return ERR_STATE;
    }
    result = shell_regcheck_validate_arp(&status);
    if (result != OK) return result;
    result = shell_regcheck_validate_ipv4_icmp(&status);
    if (result != OK) return result;
    result = shell_regcheck_validate_udp_dhcp_dns(&status);
    if (result != OK) return result;
    result = shell_regcheck_validate_tcp_socket_http(&status);
    if (result != OK) return result;
    return shell_regcheck_validate_network_recovery(&status);
}

static uint8_t cmd_network_state_color(network_interface_state_t state) {
    if (state == NETWORK_INTERFACE_ACTIVE) return 0x0A;
    if (state == NETWORK_INTERFACE_DRIVER_MISSING) return 0x0E;
    return 0x0C;
}

static network_link_state_t cmd_net_get_link_state(uint32_t count) {
    network_link_state_t result = NETWORK_LINK_UNKNOWN;

    for (uint32_t index = 0; index < count; index++) {
        network_interface_info_t info;

        if (network_manager_get_interface(index, &info) != OK) {
            LOG_ERROR("SHELL", "Falha ao consultar link de rede");
            return NETWORK_LINK_UNKNOWN;
        }
        if (info.state != NETWORK_INTERFACE_ACTIVE) continue;
        if (info.link == NETWORK_LINK_UP) return NETWORK_LINK_UP;
        if (info.link == NETWORK_LINK_DOWN) result = NETWORK_LINK_DOWN;
    }
    return result;
}

static void cmd_net_status(void) {
    network_manager_status_t status;
    net_buffer_stats_t buffers;
    const recovery_component_t* health;
    network_link_state_t link;
    int buffer_result;

    if (network_manager_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado de rede indisponivel");
        video_print("Erro: diagnostico de rede indisponivel.\n", 0x0C);
        return;
    }
    health = recovery_get(RECOVERY_COMPONENT_NETWORK);
    if (!health) {
        LOG_ERROR("SHELL", "Componente Network ausente do health");
        video_print("Erro: health de rede indisponivel.\n", 0x0C);
        return;
    }
    buffer_result = net_buffer_get_stats(&buffers);

    video_print("Rede:\n  Servico: ", 0x0B);
    video_print(recovery_state_name(health->state),
                health->state == RECOVERY_STATE_READY ? 0x0A :
                health->state == RECOVERY_STATE_DEGRADED ? 0x0E : 0x0C);
    video_print("\n  Inventario: ", 0x07);
    video_print(status.partial ? "PARCIAL" : "COMPLETO",
                status.partial ? 0x0E : 0x0A);
    video_print("\n  Controladores detectados: ", 0x07);
    shell_command_print_num(status.interface_count);
    video_print("\n  Modelos reconhecidos: ", 0x07);
    shell_command_print_num(status.recognized_count);
    video_print("\n  Drivers ativos: ", 0x07);
    shell_command_print_num(status.active_count);
    video_print("  Erros de driver: ", 0x07);
    shell_command_print_num(status.driver_error_count);
    video_print("\n  Link: ", 0x07);
    link = cmd_net_get_link_state(status.interface_count);
    video_print(network_manager_link_state_name(link),
                link == NETWORK_LINK_UP ? 0x0A :
                link == NETWORK_LINK_DOWN ? 0x0E : 0x08);
    video_print("\n  RX/TX: ", 0x07);
    video_print(status.packet_io_available ?
                "DISPONIVEL" : "NAO IMPLEMENTADO",
                status.packet_io_available ? 0x0A : 0x0E);
    video_print("\n  Ethernet L2: ", 0x07);
    video_print(status.ethernet_available ?
                "DISPONIVEL" : "INDISPONIVEL",
                status.ethernet_available ? 0x0A : 0x0E);
    video_print("\n  Buffers NET0: ", 0x07);
    if (buffer_result != OK) {
        video_print("INDISPONIVEL", 0x0E);
    } else {
        video_print("ativos=", 0x08);
        shell_command_print_num(buffers.active_buffers);
        video_print(" pico=", 0x08);
        shell_command_print_num(buffers.peak_buffers);
        video_print(" copias=", 0x08);
        shell_command_print_num(buffers.copies);
        video_print(" descartes=", 0x08);
        shell_command_print_num(buffers.dropped);
    }
    video_print("\n  ARP: ", 0x07);
    if (!status.arp_available) {
        video_print("INDISPONIVEL", 0x0E);
    } else if (status.arp_configured) {
        video_print("CONFIGURADO", 0x0A);
    } else {
        video_print("DISPONIVEL (NAO CONFIGURADO)", 0x0E);
    }
    video_print("\n  IPv4: ", 0x07);
    if (!status.ipv4_available) {
        video_print("INDISPONIVEL", 0x0E);
    } else if (status.ipv4_configured) {
        video_print("CONFIGURADO", 0x0A);
    } else {
        video_print("DISPONIVEL (NAO CONFIGURADO)", 0x0E);
    }
    video_print("  Fonte: ", 0x07);
    video_print(network_manager_ipv4_source_name(status.ipv4_source),
                status.ipv4_source == NETWORK_IPV4_SOURCE_DHCP ? 0x0B :
                status.ipv4_source == NETWORK_IPV4_SOURCE_STATIC ?
                0x0A : 0x08);
    video_print("  Interface L3: ", 0x07);
    video_print(status.l3_interface_id[0] ?
                status.l3_interface_id : "N/D",
                status.l3_interface_id[0] ? 0x0B : 0x08);
    video_print("\n  ICMP Echo: ", 0x07);
    video_print(status.icmp_available ? "DISPONIVEL" : "INDISPONIVEL",
                status.icmp_available ? 0x0A : 0x0E);
    video_print("\n  UDP: ", 0x07);
    video_print(status.udp_available ? "DISPONIVEL" : "INDISPONIVEL",
                status.udp_available ? 0x0A : 0x0E);
    video_print("\n  DHCP: ", 0x07);
    if (!status.dhcp_available) {
        video_print("INDISPONIVEL", 0x0E);
    } else {
        video_print(status.dhcp_bound ? "BOUND" : "DISPONIVEL",
                    status.dhcp_bound ? 0x0A : 0x0E);
    }
    video_print("\n  DNS: ", 0x07);
    if (!status.dns_available) {
        video_print("INDISPONIVEL", 0x0E);
    } else {
        video_print(status.dns_configured ?
                    "CONFIGURADO" : "DISPONIVEL (NAO CONFIGURADO)",
                    status.dns_configured ? 0x0A : 0x0E);
    }
    video_print("\n  TCP: ", 0x07);
    video_print(status.tcp_available ? "DISPONIVEL" : "INDISPONIVEL",
                status.tcp_available ? 0x0A : 0x0E);
    video_print("  Conexoes: ", 0x07);
    shell_command_print_num(status.tcp_connection_count);
    video_print("\n  Sockets nativos: ", 0x07);
    video_print(status.sockets_available ?
                "DISPONIVEL" : "INDISPONIVEL",
                status.sockets_available ? 0x0A : 0x0E);
    video_print("  Ativos: ", 0x07);
    shell_command_print_num(status.socket_count);
    video_print("\n  HTTP: ", 0x07);
    video_print(status.http_available ? "DISPONIVEL" : "INDISPONIVEL",
                status.http_available ? 0x0A : 0x0E);
    video_print("\n  Ultimo erro: ", 0x07);
    shell_command_print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
}

static void cmd_net_print_mac(const uint8_t* mac_address) {
    if (!mac_address) {
        video_print("N/D", 0x08);
        return;
    }
    for (uint32_t index = 0; index < NETWORK_MAC_ADDRESS_SIZE; index++) {
        if (index) video_print(":", 0x08);
        shell_command_print_hex(mac_address[index], 2U);
    }
}

static int cmd_net_print_interface(const network_interface_info_t* info) {
    network_interface_text_t text;
    int result;

    if (!info) {
        LOG_ERROR("SHELL", "Interface nula ao listar rede");
        return ERR_NULL;
    }
    result = network_manager_format_text(info, &text);
    if (result != OK) {
        LOG_ERROR("SHELL", "Falha ao formatar interface de rede");
        return result;
    }
    video_print("  ", 0x07);
    video_print(text.id, 0x0B);
    video_print("  ", 0x07);
    video_print(network_manager_interface_state_name(info->state),
                cmd_network_state_color(info->state));
    video_print("  ", 0x07);
    video_print(network_manager_model_name(info->model), 0x07);
    video_print(info->transport == NETWORK_TRANSPORT_USB ?
                "  USB " : "  PCI ", 0x08);
    shell_command_print_hex(info->bus, 2U);
    video_print(":", 0x08);
    shell_command_print_hex(info->device, 2U);
    video_print(".", 0x08);
    shell_command_print_num(info->function);
    if (info->transport == NETWORK_TRANSPORT_USB) {
        video_print("-p", 0x08);
        shell_command_print_num(info->usb_port);
    }
    video_print("  ", 0x07);
    video_print(text.driver, 0x08);
    if (info->l3_active) video_print("  [L3]", 0x0A);
    if (info->dhcp_pending) video_print("  [DHCP]", 0x0E);
    video_print("\n", 0x07);
    return OK;
}

static void cmd_net_devices(void) {
    uint32_t count = 0;

    if (network_manager_get_count(&count) != OK) {
        LOG_ERROR("SHELL", "Inventario de rede indisponivel");
        video_print("Erro: inventario de rede indisponivel.\n", 0x0C);
        return;
    }
    if (!count) {
        video_print("Nenhum controlador de rede detectado.\n", 0x0E);
        return;
    }
    video_print("Controladores de rede:\n", 0x0B);
    for (uint32_t index = 0; index < count; index++) {
        network_interface_info_t info;

        if (network_manager_get_interface(index, &info) != OK ||
            cmd_net_print_interface(&info) != OK) {
            LOG_ERROR("SHELL", "Falha ao listar controlador de rede");
            video_print("Erro: entrada de rede indisponivel.\n", 0x0C);
            return;
        }
    }
}

static void cmd_net_info(const char* args) {
    char id[NETWORK_INTERFACE_ID_SIZE];
    network_interface_info_t info;
    network_interface_text_t text;
    int result = shell_command_read_single_arg(args, id, sizeof(id));

    if (result != OK) {
        LOG_WARN("SHELL", "Uso invalido de net info");
        video_print("Uso: net info <id>\n", 0x0C);
        return;
    }
    result = network_manager_find(id, &info);
    if (result == ERR_NOT_FOUND) {
        video_print("Erro: interface de rede nao encontrada.\n", 0x0C);
        return;
    }
    if (result != OK ||
        network_manager_format_text(&info, &text) != OK) {
        LOG_ERROR("SHELL", "Falha ao consultar interface de rede");
        video_print("Erro: interface de rede indisponivel.\n", 0x0C);
        return;
    }

    video_print("Interface de rede:\n  ID: ", 0x0B);
    video_print(text.id, 0x0B);
    video_print("\n  Nome: ", 0x07);
    video_print(text.name, 0x07);
    video_print("\n  Estado: ", 0x07);
    video_print(network_manager_interface_state_name(info.state),
                cmd_network_state_color(info.state));
    video_print("\n  Driver: ", 0x07);
    video_print(text.driver, cmd_network_state_color(info.state));
    video_print("\n  Link: ", 0x07);
    video_print(network_manager_link_state_name(info.link),
                info.link == NETWORK_LINK_UP ? 0x0A :
                info.link == NETWORK_LINK_DOWN ? 0x0E : 0x08);
    video_print("\n  Vinculo Ethernet: ", 0x07);
    video_print(info.ethernet_attached ? "SIM" : "NAO",
                info.ethernet_attached ? 0x0A : 0x0E);
    video_print("  Papel L3: ", 0x07);
    video_print(info.l3_active ? "ATIVO" : "NAO",
                info.l3_active ? 0x0A : 0x08);
    video_print("  DHCP pendente: ", 0x07);
    video_print(info.dhcp_pending ? "SIM" : "NAO",
                info.dhcp_pending ? 0x0E : 0x08);
    video_print("\n  MAC: ", 0x07);
    if (info.state == NETWORK_INTERFACE_ACTIVE) {
        cmd_net_print_mac(info.mac_address);
    } else {
        video_print("N/D", 0x08);
    }
    video_print("\n  RX pacotes: ", 0x07);
    shell_command_print_num(info.rx_packets);
    video_print("  TX pacotes: ", 0x07);
    shell_command_print_num(info.tx_packets);
    video_print("\n  RX erros: ", 0x07);
    shell_command_print_num(info.rx_errors);
    video_print("  TX erros: ", 0x07);
    shell_command_print_num(info.tx_errors);
    video_print("  RX descartados: ", 0x07);
    shell_command_print_num(info.rx_dropped);
    video_print("\n  Fila RX atual/pico: ", 0x07);
    shell_command_print_num(info.rx_queue_depth);
    video_print("/", 0x07);
    shell_command_print_num(info.rx_queue_high_water);
    video_print("  Fila cheia: ", 0x07);
    shell_command_print_num(info.rx_queue_dropped);
    video_print("  IRQs RX: ", 0x07);
    shell_command_print_num(info.rx_interrupts);
    video_print("\n  Erro do driver: ", 0x07);
    shell_command_print_num((uint32_t)info.driver_error);
    video_print("\n  Vendor: 0x", 0x07);
    shell_command_print_hex(info.vendor_id, 4U);
    video_print("  Device: 0x", 0x07);
    shell_command_print_hex(info.device_id, 4U);
    video_print("\n  Classe: 0x", 0x07);
    shell_command_print_hex(info.class_code, 2U);
    video_print("  Subclasse: 0x", 0x07);
    shell_command_print_hex(info.subclass_code, 2U);
    video_print("  Prog-if: 0x", 0x07);
    shell_command_print_hex(info.prog_if, 2U);
    video_print("  Revisao: 0x", 0x07);
    shell_command_print_hex(info.revision, 2U);
    video_print(info.transport == NETWORK_TRANSPORT_USB ?
                "\n  USB: " : "\n  PCI: ", 0x07);
    shell_command_print_hex(info.bus, 2U);
    video_print(":", 0x07);
    shell_command_print_hex(info.device, 2U);
    video_print(".", 0x07);
    shell_command_print_num(info.function);
    if (info.transport == NETWORK_TRANSPORT_USB) {
        video_print("-p", 0x07);
        shell_command_print_num(info.usb_port);
        video_print("  USB ID: ", 0x07);
        video_print(info.usb_device_id, 0x08);
        video_print("  Revisao USB: 0x", 0x07);
        shell_command_print_hex(info.usb_revision, 4U);
        video_print("  Endpoints: ", 0x07);
        shell_command_print_num(info.usb_endpoint_count);
    }
    video_print("  IRQ: ", 0x07);
    if (info.irq == NETWORK_IRQ_UNKNOWN) {
        video_print("N/D", 0x08);
    } else {
        shell_command_print_num(info.irq);
    }
    for (uint32_t bar = 0; bar < NETWORK_PCI_BAR_COUNT; bar++) {
        video_print("\n  BAR", 0x07);
        shell_command_print_num(bar);
        video_print(": 0x", 0x07);
        shell_command_print_hex(info.bars[bar], 8U);
    }
    video_print("\n", 0x07);
}

static void cmd_net_test(const char* args) {
    char id[NETWORK_INTERFACE_ID_SIZE];
    int result = shell_command_read_single_arg(args, id, sizeof(id));

    if (result != OK) {
        LOG_WARN("SHELL", "Uso invalido de net test");
        video_print("Uso: net test <id>\n", 0x0C);
        return;
    }
    result = network_manager_send_diagnostic(id);
    if (result == OK) {
        video_print("Teste TX Ethernet concluido.\n", 0x0A);
        return;
    }
    LOG_WARN("SHELL", "Teste TX Ethernet nao concluiu");
    video_print("Erro: teste TX Ethernet falhou (codigo ", 0x0C);
    shell_command_print_num((uint32_t)result);
    video_print(").\n", 0x0C);
}

static const char* cmd_net_destination_name(
    ethernet_destination_t destination) {
    if (destination == ETHERNET_DESTINATION_LOCAL_UNICAST) {
        return "UNICAST LOCAL";
    }
    if (destination == ETHERNET_DESTINATION_BROADCAST) return "BROADCAST";
    return "DESCONHECIDO";
}

static void cmd_net_print_last_ethernet(
    const ethernet_interface_status_t* status) {
    if (!status || !status->rx_frames) {
        video_print("  Ultimo frame: N/D\n", 0x08);
        return;
    }
    video_print("  Ultimo frame: ", 0x07);
    video_print(cmd_net_destination_name(status->last_destination_type),
                0x0B);
    video_print("  tamanho=", 0x07);
    shell_command_print_num(status->last_frame_length);
    video_print("  EtherType=0x", 0x07);
    shell_command_print_hex(status->last_ethertype, 4U);
    video_print("\n    Origem: ", 0x07);
    cmd_net_print_mac(status->last_source);
    video_print("  Destino: ", 0x07);
    cmd_net_print_mac(status->last_destination);
    video_print("\n", 0x07);
}

static void cmd_net_ethernet(const char* args) {
    char id[NETWORK_INTERFACE_ID_SIZE];
    network_ethernet_diagnostic_t diagnostic;
    int result = shell_command_read_single_arg(args, id, sizeof(id));

    if (result != OK) {
        LOG_WARN("SHELL", "Uso invalido de net ethernet");
        video_print("Uso: net ethernet <id>\n", 0x0C);
        return;
    }
    result = network_manager_get_ethernet_diagnostic(id, &diagnostic);
    if (result != OK) {
        LOG_WARN("SHELL", "Diagnostico Ethernet nao concluiu");
        video_print("Erro: diagnostico Ethernet falhou (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Camada Ethernet:\n  Estado: ATIVA", 0x0B);
    video_print("\n  Processados nesta consulta: ", 0x07);
    shell_command_print_num(diagnostic.processed_now);
    video_print("\n  Fila RX atual/pico: ", 0x07);
    shell_command_print_num(diagnostic.driver_queue_depth);
    video_print("/", 0x07);
    shell_command_print_num(diagnostic.driver_queue_high_water);
    video_print("  Descartes por fila cheia: ", 0x07);
    shell_command_print_num(diagnostic.driver_queue_dropped);
    video_print("  IRQs RX: ", 0x07);
    shell_command_print_num(diagnostic.driver_rx_interrupts);
    video_print("\n  Frames L2 aceitos: ", 0x07);
    shell_command_print_num(diagnostic.interface.rx_frames);
    video_print("  Unicast: ", 0x07);
    shell_command_print_num(diagnostic.interface.rx_unicast);
    video_print("  Broadcast: ", 0x07);
    shell_command_print_num(diagnostic.interface.rx_broadcast);
    video_print("\n  Invalidos: ", 0x07);
    shell_command_print_num(diagnostic.interface.rx_invalid);
    video_print("  Filtrados: ", 0x07);
    shell_command_print_num(diagnostic.interface.rx_filtered);
    video_print("  Sem protocolo: ", 0x07);
    shell_command_print_num(diagnostic.interface.rx_unhandled);
    video_print("\n  Entregues a protocolo: ", 0x07);
    shell_command_print_num(diagnostic.interface.rx_delivered);
    video_print("  Erros de protocolo: ", 0x07);
    shell_command_print_num(diagnostic.interface.rx_protocol_errors);
    video_print("  Handlers: ", 0x07);
    shell_command_print_num(diagnostic.layer.handler_count);
    video_print("\n  Polls: ", 0x07);
    shell_command_print_num(diagnostic.interface.polls);
    video_print("  TX montados pela camada: ", 0x07);
    shell_command_print_num(diagnostic.interface.tx_frames);
    video_print("\n", 0x07);
    cmd_net_print_last_ethernet(&diagnostic.interface);
}

static int cmd_net_invalid_ipv4(void) {
    LOG_WARN("SHELL", "IPv4 decimal invalido");
    return ERR_INVALID;
}

static int cmd_net_parse_ipv4(const char* text, uint32_t* out_ip) {
    uint32_t ip_address = 0;

    if (!text || !out_ip) {
        LOG_ERROR("SHELL", "Destino nulo ao interpretar IPv4");
        return ERR_NULL;
    }
    for (uint32_t octet = 0;
         octet < SHELL_IPV4_OCTET_COUNT; octet++) {
        uint32_t value = 0;
        uint32_t digits = 0;

        while (*text >= '0' && *text <= '9') {
            if (digits >= SHELL_IPV4_OCTET_DIGITS) {
                return cmd_net_invalid_ipv4();
            }
            value = value * 10U + (uint32_t)(*text - '0');
            if (value > SHELL_IPV4_OCTET_MAX) {
                return cmd_net_invalid_ipv4();
            }
            digits++;
            text++;
        }
        if (!digits) return cmd_net_invalid_ipv4();
        ip_address = (ip_address << SHELL_IPV4_OCTET_BITS) | value;
        if (octet + 1U < SHELL_IPV4_OCTET_COUNT) {
            if (*text != '.') return cmd_net_invalid_ipv4();
            text++;
        }
    }
    if (*text) return cmd_net_invalid_ipv4();
    *out_ip = ip_address;
    return OK;
}

static void cmd_net_print_ipv4(uint32_t ip_address) {
    shell_command_print_num((ip_address >> 24U) & SHELL_IPV4_OCTET_MAX);
    video_print(".", 0x07);
    shell_command_print_num((ip_address >> 16U) & SHELL_IPV4_OCTET_MAX);
    video_print(".", 0x07);
    shell_command_print_num((ip_address >> 8U) & SHELL_IPV4_OCTET_MAX);
    video_print(".", 0x07);
    shell_command_print_num(ip_address & SHELL_IPV4_OCTET_MAX);
}

static void cmd_net_arp_config(const char* args) {
    char id[NETWORK_INTERFACE_ID_SIZE];
    char ip_text[SHELL_IPV4_TEXT_SIZE];
    uint32_t ip_address = 0;
    int result = shell_command_read_two_args(args, id, sizeof(id),
                                     ip_text, sizeof(ip_text));

    if (result != OK ||
        cmd_net_parse_ipv4(ip_text, &ip_address) != OK ||
        !arp_ipv4_is_valid(ip_address)) {
        LOG_WARN("SHELL", "Uso invalido de net arp config");
        video_print("Uso: net arp config <id> <ip-local>\n", 0x0C);
        return;
    }
    result = network_manager_configure_arp(id, ip_address);
    if (result != OK) {
        LOG_WARN("SHELL", "Configuracao ARP nao concluiu");
        video_print("Erro: configuracao ARP falhou (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("ARP configurado em RAM para ", 0x0A);
    cmd_net_print_ipv4(ip_address);
    video_print(".\n", 0x0A);
}

static void cmd_net_arp_status(void) {
    arp_status_t status;

    if (arp_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado ARP indisponivel");
        video_print("Erro: estado ARP indisponivel.\n", 0x0C);
        return;
    }
    video_print("ARP:\n  Estado: ", 0x0B);
    video_print(status.initialized ? "DISPONIVEL" : "INDISPONIVEL",
                status.initialized ? 0x0A : 0x0E);
    video_print("\n  Configuracao: ", 0x07);
    video_print(status.configured ? "ATIVA" : "NAO CONFIGURADO",
                status.configured ? 0x0A : 0x0E);
    video_print("\n  Interface: ", 0x07);
    video_print(status.configured ? status.interface_id : "N/D",
                status.configured ? 0x0B : 0x08);
    video_print("\n  IPv4 local: ", 0x07);
    if (status.configured) {
        cmd_net_print_ipv4(status.local_ip);
    } else {
        video_print("N/D", 0x08);
    }
    video_print("\n  Cache total/pendente/resolvido/falho: ", 0x07);
    shell_command_print_num(status.cache_entries);
    video_print("/", 0x07);
    shell_command_print_num(status.incomplete_entries);
    video_print("/", 0x07);
    shell_command_print_num(status.resolved_entries);
    video_print("/", 0x07);
    shell_command_print_num(status.failed_entries);
    video_print("\n  Cache hits: ", 0x07);
    shell_command_print_num(status.cache_hits);
    video_print("  Manutencao ciclos/erros: ", 0x07);
    shell_command_print_num(status.maintenance_cycles);
    video_print("/", 0x07);
    shell_command_print_num(status.maintenance_errors);
    video_print("\n  Requests RX/TX: ", 0x07);
    shell_command_print_num(status.rx_requests);
    video_print("/", 0x07);
    shell_command_print_num(status.tx_requests);
    video_print("  Replies RX/TX: ", 0x07);
    shell_command_print_num(status.rx_replies);
    video_print("/", 0x07);
    shell_command_print_num(status.tx_replies);
    video_print("\n  Invalidos: ", 0x07);
    shell_command_print_num(status.invalid_packets);
    video_print("  Ignorados: ", 0x07);
    shell_command_print_num(status.ignored_packets);
    video_print("  Timeouts: ", 0x07);
    shell_command_print_num(status.timeouts);
    video_print("\n  Ultimo erro: ", 0x07);
    shell_command_print_num((uint32_t)status.last_error);
    video_print("\n  Cobertura: reply=", 0x07);
    video_print(status.tx_requests && status.rx_replies ?
                "OK" : "NAO TESTADO",
                status.tx_requests && status.rx_replies ? 0x0A : 0x0E);
    video_print("  cache-hit=", 0x07);
    video_print(status.cache_hits ? "OK" : "NAO TESTADO",
                status.cache_hits ? 0x0A : 0x0E);
    video_print("  timeout=", 0x07);
    video_print(status.timeouts ? "OK" : "NAO TESTADO",
                status.timeouts ? 0x0A : 0x0E);
    video_print("\n", 0x07);
}

static void cmd_net_arp_resolve(const char* args) {
    char ip_text[SHELL_IPV4_TEXT_SIZE];
    uint8_t mac_address[ARP_MAC_ADDRESS_SIZE];
    uint8_t resolved = 0;
    uint32_t ip_address = 0;
    int result = shell_command_read_single_arg(args, ip_text, sizeof(ip_text));

    if (result != OK ||
        cmd_net_parse_ipv4(ip_text, &ip_address) != OK ||
        !arp_ipv4_is_valid(ip_address)) {
        LOG_WARN("SHELL", "Uso invalido de net arp resolve");
        video_print("Uso: net arp resolve <ip>\n", 0x0C);
        return;
    }
    result = arp_resolve(ip_address, mac_address, &resolved);
    if (result == ERR_TIMEOUT) {
        video_print("Resolucao ARP falhou por timeout.\n", 0x0C);
        return;
    }
    if (result != OK) {
        LOG_WARN("SHELL", "Resolucao ARP nao iniciou");
        video_print("Erro: resolucao ARP falhou (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    if (!resolved) {
        video_print("Resolucao ARP pendente.\n", 0x0E);
        return;
    }
    video_print("MAC resolvido: ", 0x0A);
    cmd_net_print_mac(mac_address);
    video_print("\n", 0x07);
}

static void cmd_net_arp_table(void) {
    arp_status_t status;
    uint32_t shown = 0;

    if (arp_get_status(&status) != OK || !status.initialized) {
        LOG_WARN("SHELL", "Tabela ARP indisponivel");
        video_print("Erro: ARP indisponivel.\n", 0x0C);
        return;
    }
    video_print("Tabela ARP:\n", 0x0B);
    for (uint32_t index = 0; index < ARP_CACHE_CAPACITY; index++) {
        arp_cache_entry_info_t entry;
        int result = arp_get_cache_entry(index, &entry);

        if (result != OK) {
            LOG_ERROR("SHELL", "Falha ao consultar entrada ARP");
            video_print("Erro: entrada ARP indisponivel.\n", 0x0C);
            return;
        }
        if (!entry.used) continue;
        video_print("  ", 0x07);
        cmd_net_print_ipv4(entry.ip_address);
        video_print("  ", 0x07);
        if (entry.state == ARP_ENTRY_RESOLVED) {
            cmd_net_print_mac(entry.mac_address);
        } else {
            video_print("N/D", 0x08);
        }
        video_print("  ", 0x07);
        video_print(arp_entry_state_name(entry.state),
                    entry.state == ARP_ENTRY_RESOLVED ? 0x0A :
                    entry.state == ARP_ENTRY_FAILED ? 0x0C : 0x0E);
        video_print("  idade=", 0x07);
        shell_command_print_num(entry.age_seconds);
        video_print("s tentativas=", 0x07);
        shell_command_print_num(entry.attempts);
        video_print("\n", 0x07);
        shown++;
    }
    if (!shown) video_print("  Vazia.\n", 0x08);
}

static void cmd_net_arp_clear(void) {
    int result = arp_clear();

    if (result != OK) {
        LOG_WARN("SHELL", "Limpeza do cache ARP nao concluiu");
        video_print("Erro: cache ARP nao foi limpo (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Cache ARP limpo; configuracao preservada.\n", 0x0A);
}

static void cmd_net_arp(const char* args) {
    const char* config_args;
    const char* resolve_args;

    config_args = shell_command_match_subcommand(args, "config");
    if (config_args) {
        cmd_net_arp_config(config_args);
        return;
    }
    resolve_args = shell_command_match_subcommand(args, "resolve");
    if (resolve_args) {
        cmd_net_arp_resolve(resolve_args);
        return;
    }
    if (shell_command_args_equal(args, "status")) {
        cmd_net_arp_status();
        return;
    }
    if (shell_command_args_equal(args, "table")) {
        cmd_net_arp_table();
        return;
    }
    if (shell_command_args_equal(args, "clear")) {
        cmd_net_arp_clear();
        return;
    }
    LOG_WARN("SHELL", "Uso invalido de net arp");
    video_print("Uso: net arp config <id> <ip> | status | resolve <ip> | "
                "table | clear\n", 0x0C);
}

static uint32_t cmd_net_ticks_to_milliseconds(uint32_t ticks) {
    uint32_t frequency = timer_get_frequency();

    if (!frequency) return 0;
    if (ticks >
        SHELL_MAX_TICK_INTERVAL / SHELL_MILLISECONDS_PER_SECOND) {
        return SHELL_MAX_TICK_INTERVAL;
    }
    return ticks * SHELL_MILLISECONDS_PER_SECOND / frequency;
}

static void cmd_net_ipv4_print_config(const ipv4_status_t* status) {
    video_print("IPv4:\n  Estado: ", 0x0B);
    video_print(status->initialized ? "DISPONIVEL" : "INDISPONIVEL",
                status->initialized ? 0x0A : 0x0E);
    video_print("\n  Configuracao: ", 0x07);
    video_print(status->configured ? "ATIVA" : "NAO CONFIGURADO",
                status->configured ? 0x0A : 0x0E);
    video_print("\n  Interface: ", 0x07);
    video_print(status->configured ? status->interface_id : "N/D",
                status->configured ? 0x0B : 0x08);
    video_print("\n  IPv4 local: ", 0x07);
    if (status->configured) cmd_net_print_ipv4(status->local_ip);
    else video_print("N/D", 0x08);
    video_print("  Mascara: ", 0x07);
    if (status->configured) cmd_net_print_ipv4(status->subnet_mask);
    else video_print("N/D", 0x08);
    video_print("  Gateway: ", 0x07);
    if (status->configured && status->gateway) {
        cmd_net_print_ipv4(status->gateway);
    } else {
        video_print(status->configured ? "SEM ROTA" : "N/D", 0x08);
    }
    video_print("\n  MTU: ", 0x07);
    shell_command_print_num(IPV4_MTU);
}

static void cmd_net_ipv4_print_counters(const ipv4_status_t* status) {
    video_print("\n  Pacotes RX/TX: ", 0x07);
    shell_command_print_num(status->rx_packets);
    video_print("/", 0x07);
    shell_command_print_num(status->tx_packets);
    video_print("  Bytes RX/TX: ", 0x07);
    shell_command_print_num(status->rx_bytes);
    video_print("/", 0x07);
    shell_command_print_num(status->tx_bytes);
    video_print("\n  TX rota direta/gateway: ", 0x07);
    shell_command_print_num(status->tx_direct);
    video_print("/", 0x07);
    shell_command_print_num(status->tx_via_gateway);
    video_print("  Broadcast RX/TX: ", 0x07);
    shell_command_print_num(status->rx_limited_broadcast);
    video_print("/", 0x07);
    shell_command_print_num(status->tx_limited_broadcast);
    video_print("  Entregues: ", 0x07);
    shell_command_print_num(status->rx_delivered);
    video_print("\n  Invalidos/checksum/opcoes/fragmentos: ", 0x07);
    shell_command_print_num(status->rx_invalid);
    video_print("/", 0x07);
    shell_command_print_num(status->rx_checksum_errors);
    video_print("/", 0x07);
    shell_command_print_num(status->rx_options);
    video_print("/", 0x07);
    shell_command_print_num(status->rx_fragments);
    video_print("\n  Ignorados/sem handler/erros protocolo: ", 0x07);
    shell_command_print_num(status->rx_ignored);
    video_print("/", 0x07);
    shell_command_print_num(status->rx_unhandled);
    video_print("/", 0x07);
    shell_command_print_num(status->rx_protocol_errors);
    video_print("  Handlers: ", 0x07);
    shell_command_print_num(status->handler_count);
    video_print("\n  Ultimo erro: ", 0x07);
    shell_command_print_num((uint32_t)status->last_error);
    video_print("\n", 0x07);
}

static void cmd_net_icmp_print_status(const icmp_status_t* status) {
    uint32_t average_ticks = status->received ?
        status->rtt_total_ticks / status->received : 0U;
    uint32_t loss_percent = status->requested_count ?
        (uint32_t)status->timeouts * 100U /
            status->requested_count : 0U;

    video_print("ICMP Echo:\n  Estado: ", 0x0B);
    video_print(status->initialized ? "DISPONIVEL" : "INDISPONIVEL",
                status->initialized ? 0x0A : 0x0E);
    video_print("\n  Echo request RX/TX: ", 0x07);
    shell_command_print_num(status->echo_requests_rx);
    video_print("/", 0x07);
    shell_command_print_num(status->echo_requests_tx);
    video_print("  Echo reply RX/TX: ", 0x07);
    shell_command_print_num(status->echo_replies_rx);
    video_print("/", 0x07);
    shell_command_print_num(status->echo_replies_tx);
    video_print("\n  Sessao: ", 0x07);
    video_print(icmp_ping_state_name(status->state),
                status->state == ICMP_PING_FAILED ? 0x0C :
                status->state == ICMP_PING_COMPLETE ? 0x0A : 0x0E);
    video_print("  Alvo: ", 0x07);
    if (status->requested_count) cmd_net_print_ipv4(status->target_ip);
    else video_print("N/D", 0x08);
    video_print("  id/seq: ", 0x07);
    shell_command_print_num(status->identifier);
    video_print("/", 0x07);
    shell_command_print_num(status->current_sequence);
    video_print("\n  Tentativas pedidas: ", 0x07);
    shell_command_print_num(status->requested_count);
    video_print("  enviados/recebidos/timeouts: ", 0x07);
    shell_command_print_num(status->sent);
    video_print("/", 0x07);
    shell_command_print_num(status->received);
    video_print("/", 0x07);
    shell_command_print_num(status->timeouts);
    video_print("  Perdas confirmadas: ", 0x07);
    shell_command_print_num(status->timeouts);
    video_print(" (", 0x07);
    shell_command_print_num(loss_percent);
    video_print("%)", 0x07);
    video_print("\n  RTT min/medio/max: ", 0x07);
    shell_command_print_num(cmd_net_ticks_to_milliseconds(status->rtt_min_ticks));
    video_print("/", 0x07);
    shell_command_print_num(cmd_net_ticks_to_milliseconds(average_ticks));
    video_print("/", 0x07);
    shell_command_print_num(cmd_net_ticks_to_milliseconds(status->rtt_max_ticks));
    video_print(" ms  Reply pendente: ", 0x07);
    video_print(status->reply_pending ? "SIM" : "NAO",
                status->reply_pending ? 0x0E : 0x0A);
    video_print("\n  Invalidos/ignorados/slot ocupado: ", 0x07);
    shell_command_print_num(status->invalid_packets);
    video_print("/", 0x07);
    shell_command_print_num(status->ignored_packets);
    video_print("/", 0x07);
    shell_command_print_num(status->pending_reply_drops);
    video_print("  Ultimo erro: ", 0x07);
    shell_command_print_num((uint32_t)status->last_error);
    video_print("\n", 0x07);
}

static void cmd_net_ipv4_status(void) {
    ipv4_status_t ipv4_status;
    icmp_status_t icmp_status;

    if (ipv4_get_status(&ipv4_status) != OK ||
        icmp_get_status(&icmp_status) != OK) {
        LOG_ERROR("SHELL", "Estado IPv4 ou ICMP indisponivel");
        video_print("Erro: estado IPv4/ICMP indisponivel.\n", 0x0C);
        return;
    }
    cmd_net_ipv4_print_config(&ipv4_status);
    cmd_net_ipv4_print_counters(&ipv4_status);
    cmd_net_icmp_print_status(&icmp_status);
}

static void cmd_net_ipv4_config(const char* args) {
    char id[NETWORK_INTERFACE_ID_SIZE];
    char ip_text[SHELL_IPV4_TEXT_SIZE];
    char mask_text[SHELL_IPV4_TEXT_SIZE];
    char gateway_text[SHELL_IPV4_TEXT_SIZE];
    uint32_t local_ip = 0;
    uint32_t subnet_mask = 0;
    uint32_t gateway = 0;
    int result;

    result = shell_command_read_four_args(
        args, id, sizeof(id), ip_text, sizeof(ip_text),
        mask_text, sizeof(mask_text), gateway_text, sizeof(gateway_text));
    if (result != OK ||
        cmd_net_parse_ipv4(ip_text, &local_ip) != OK ||
        cmd_net_parse_ipv4(mask_text, &subnet_mask) != OK ||
        cmd_net_parse_ipv4(gateway_text, &gateway) != OK) {
        LOG_WARN("SHELL", "Uso invalido de net ipv4 config");
        video_print("Uso: net ipv4 config <id> <ip> <mascara> <gateway>\n",
                    0x0C);
        return;
    }
    result = network_manager_configure_ipv4(
        id, local_ip, subnet_mask, gateway);
    if (result != OK) {
        LOG_WARN("SHELL", "Configuracao IPv4 nao concluiu");
        video_print("Erro: configuracao IPv4 falhou (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("IPv4 configurado em RAM para ", 0x0A);
    cmd_net_print_ipv4(local_ip);
    video_print(".\n", 0x0A);
}

static void cmd_net_ipv4(const char* args) {
    const char* config_args = shell_command_match_subcommand(args, "config");

    if (config_args) {
        cmd_net_ipv4_config(config_args);
        return;
    }
    if (shell_command_args_equal(args, "status")) {
        cmd_net_ipv4_status();
        return;
    }
    LOG_WARN("SHELL", "Uso invalido de net ipv4");
    video_print("Uso: net ipv4 config <id> <ip> <mascara> <gateway> | "
                "status\n", 0x0C);
}

static void cmd_net_udp_status(void) {
    udp_status_t status;

    if (udp_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado UDP indisponivel");
        video_print("Erro: estado UDP indisponivel.\n", 0x0C);
        return;
    }
    video_print("UDP:\n  Estado: ", 0x0B);
    video_print(status.initialized ? "DISPONIVEL" : "INDISPONIVEL",
                status.initialized ? 0x0A : 0x0E);
    video_print("\n  Endpoints: ", 0x07);
    shell_command_print_num(status.endpoint_count);
    video_print("/", 0x07);
    shell_command_print_num(UDP_ENDPOINT_CAPACITY);
    video_print("  Datagramas RX/TX: ", 0x07);
    shell_command_print_num(status.rx_datagrams);
    video_print("/", 0x07);
    shell_command_print_num(status.tx_datagrams);
    video_print("\n  Bytes RX/TX: ", 0x07);
    shell_command_print_num(status.rx_bytes);
    video_print("/", 0x07);
    shell_command_print_num(status.tx_bytes);
    video_print("  Broadcast RX/TX: ", 0x07);
    shell_command_print_num(status.rx_broadcast);
    video_print("/", 0x07);
    shell_command_print_num(status.tx_broadcast);
    video_print("\n  Invalidos/tamanho/checksum/sem listener: ", 0x07);
    shell_command_print_num(status.rx_invalid);
    video_print("/", 0x07);
    shell_command_print_num(status.rx_length_errors);
    video_print("/", 0x07);
    shell_command_print_num(status.rx_checksum_errors);
    video_print("/", 0x07);
    shell_command_print_num(status.rx_no_listener);
    video_print("  Entregues/erros RX/TX: ", 0x07);
    shell_command_print_num(status.rx_delivered);
    video_print("/", 0x07);
    shell_command_print_num(status.rx_protocol_errors);
    video_print("/", 0x07);
    shell_command_print_num(status.tx_errors);
    video_print("\n  Ultimo erro: ", 0x07);
    shell_command_print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
}

static void cmd_net_udp(const char* args) {
    if (shell_command_args_equal(args, "status")) {
        cmd_net_udp_status();
        return;
    }
    LOG_WARN("SHELL", "Uso invalido de net udp");
    video_print("Uso: net udp status\n", 0x0C);
}

static void cmd_net_dhcp_print_lease(const dhcp_status_t* status) {
    video_print("\n  Interface: ", 0x07);
    video_print(status->interface_id[0] ? status->interface_id : "N/D",
                status->interface_id[0] ? 0x0B : 0x08);
    video_print("  IPv4: ", 0x07);
    if (status->lease.address) cmd_net_print_ipv4(status->lease.address);
    else video_print("N/D", 0x08);
    video_print("\n  Mascara: ", 0x07);
    if (status->lease.subnet_mask) {
        cmd_net_print_ipv4(status->lease.subnet_mask);
    } else {
        video_print("N/D", 0x08);
    }
    video_print("  Gateway: ", 0x07);
    if (status->lease.gateway) cmd_net_print_ipv4(status->lease.gateway);
    else video_print("N/D", 0x08);
    video_print("  DNS: ", 0x07);
    if (status->lease.dns_server) {
        cmd_net_print_ipv4(status->lease.dns_server);
    } else {
        video_print("N/D", 0x08);
    }
}

static void cmd_net_dhcp_status(void) {
    dhcp_status_t status;

    if (dhcp_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado DHCP indisponivel");
        video_print("Erro: estado DHCP indisponivel.\n", 0x0C);
        return;
    }
    video_print("DHCP:\n  Estado: ", 0x0B);
    video_print(status.initialized ? "DISPONIVEL" : "INDISPONIVEL",
                status.initialized ? 0x0A : 0x0E);
    video_print("  Sessao: ", 0x07);
    video_print(dhcp_state_name(status.state),
                status.state == DHCP_STATE_BOUND ? 0x0A :
                status.state == DHCP_STATE_FAILED ||
                status.state == DHCP_STATE_EXPIRED ? 0x0C : 0x0E);
    cmd_net_dhcp_print_lease(&status);
    video_print("\n  Lease/T1/T2 restantes: ", 0x07);
    shell_command_print_num(status.lease_remaining_seconds);
    video_print("/", 0x07);
    shell_command_print_num(status.t1_remaining_seconds);
    video_print("/", 0x07);
    shell_command_print_num(status.t2_remaining_seconds);
    video_print(" s  Tentativas: ", 0x07);
    shell_command_print_num(status.attempts);
    video_print("\n  Discover/Offer/Request/ACK/NAK: ", 0x07);
    shell_command_print_num(status.discovers_tx);
    video_print("/", 0x07);
    shell_command_print_num(status.offers_rx);
    video_print("/", 0x07);
    shell_command_print_num(status.requests_tx);
    video_print("/", 0x07);
    shell_command_print_num(status.acks_rx);
    video_print("/", 0x07);
    shell_command_print_num(status.naks_rx);
    video_print("  Releases: ", 0x07);
    shell_command_print_num(status.releases_tx);
    video_print("\n  Invalidos/ignorados/timeouts: ", 0x07);
    shell_command_print_num(status.invalid_packets);
    video_print("/", 0x07);
    shell_command_print_num(status.ignored_packets);
    video_print("/", 0x07);
    shell_command_print_num(status.timeouts);
    video_print("  Ultimo erro: ", 0x07);
    shell_command_print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
}

static int cmd_net_dhcp_wait(dhcp_status_t* out_status) {
    uint32_t frequency = timer_get_frequency();
    uint32_t start_tick = timer_get_ticks();
    uint32_t wait_ticks;
    dhcp_status_t status;

    if (!out_status) {
        LOG_ERROR("SHELL", "Destino nulo na espera DHCP");
        return ERR_NULL;
    }
    if (!frequency ||
        frequency > SHELL_MAX_TICK_INTERVAL / SHELL_DHCP_WAIT_SECONDS) {
        LOG_ERROR("SHELL", "Timer invalido na espera DHCP");
        return ERR_STATE;
    }
    wait_ticks = frequency * SHELL_DHCP_WAIT_SECONDS;
    do {
        if (dhcp_get_status(&status) != OK) return ERR_STATE;
        if (status.state == DHCP_STATE_BOUND) {
            *out_status = status;
            return OK;
        }
        if (status.state == DHCP_STATE_FAILED ||
            status.state == DHCP_STATE_EXPIRED) {
            *out_status = status;
            return status.last_error == OK ? ERR_STATE :
                                             status.last_error;
        }
        if (shell_network_job_block_tick()) {
            shell_network_job_inner_failed = 1U;
            return ERR_TIMEOUT;
        }
    } while ((uint32_t)(timer_get_ticks() - start_tick) <= wait_ticks);
    *out_status = status;
    LOG_WARN("SHELL", "Guarda de tempo DHCP expirou");
    shell_network_job_inner_failed = 1U;
    return ERR_TIMEOUT;
}

static void cmd_net_dhcp_acquire(const char* args) {
    char id[NETWORK_INTERFACE_ID_SIZE];
    dhcp_status_t status;
    int result = shell_command_read_single_arg(args, id, sizeof(id));

    if (result != OK) {
        LOG_WARN("SHELL", "Uso invalido de net dhcp acquire");
        video_print("Uso: net dhcp acquire <id>\n", 0x0C);
        return;
    }
    result = network_manager_acquire_dhcp(id);
    if (result == OK) result = cmd_net_dhcp_wait(&status);
    if (result != OK) {
        LOG_WARN("SHELL", "Aquisicao DHCP nao concluiu");
        video_print("Erro: DHCP falhou (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Lease DHCP aplicado: ", 0x0A);
    cmd_net_print_ipv4(status.lease.address);
    video_print(".\n", 0x0A);
}

static void cmd_net_dhcp_renew(void) {
    dhcp_status_t status;
    int result = network_manager_renew_dhcp();

    if (result == OK) result = cmd_net_dhcp_wait(&status);
    if (result != OK) {
        LOG_WARN("SHELL", "Renovacao DHCP nao concluiu");
        video_print("Erro: renovacao DHCP falhou (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Lease DHCP renovado.\n", 0x0A);
}

static void cmd_net_dhcp_release(void) {
    uint8_t sent = 0;
    int result = network_manager_release_dhcp(&sent);

    if (result != OK) {
        LOG_WARN("SHELL", "Liberacao DHCP nao concluiu");
        video_print("Erro: liberacao DHCP falhou (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print(sent ? "DHCPRELEASE enviado; lease removido.\n" :
                       "Lease DHCP removido localmente.\n",
                sent ? 0x0A : 0x0E);
}

static void cmd_net_dhcp(const char* args) {
    const char* acquire_args = shell_command_match_subcommand(args, "acquire");

    if (acquire_args) {
        cmd_net_dhcp_acquire(acquire_args);
        return;
    }
    if (shell_command_args_equal(args, "status")) {
        cmd_net_dhcp_status();
        return;
    }
    if (shell_command_args_equal(args, "renew")) {
        cmd_net_dhcp_renew();
        return;
    }
    if (shell_command_args_equal(args, "release")) {
        cmd_net_dhcp_release();
        return;
    }
    LOG_WARN("SHELL", "Uso invalido de net dhcp");
    video_print("Uso: net dhcp acquire <id> | status | renew | release\n",
                0x0C);
}

static void cmd_net_dns_status(void) {
    dns_status_t status;

    if (dns_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado DNS indisponivel");
        video_print("Erro: estado DNS indisponivel.\n", 0x0C);
        return;
    }
    video_print("DNS:\n  Estado: ", 0x0B);
    video_print(status.initialized ? "DISPONIVEL" : "INDISPONIVEL",
                status.initialized ? 0x0A : 0x0E);
    video_print("  Configuracao: ", 0x07);
    video_print(status.configured ? "ATIVA" : "NAO CONFIGURADO",
                status.configured ? 0x0A : 0x0E);
    video_print("\n  Servidor: ", 0x07);
    if (status.configured) cmd_net_print_ipv4(status.server_ip);
    else video_print("N/D", 0x08);
    video_print("  Porta local: ", 0x07);
    shell_command_print_num(status.local_port);
    video_print("  Cache: ", 0x07);
    shell_command_print_num(status.cache_entries);
    video_print("/", 0x07);
    shell_command_print_num(DNS_CACHE_CAPACITY);
    video_print("\n  Consulta: ", 0x07);
    video_print(dns_state_name(status.state),
                status.state == DNS_STATE_COMPLETE ? 0x0A :
                status.state == DNS_STATE_FAILED ? 0x0C : 0x0E);
    video_print("  Nome: ", 0x07);
    video_print(status.query_name[0] ? status.query_name : "N/D",
                status.query_name[0] ? 0x0B : 0x08);
    video_print("\n  Queries/replies/cache hit/miss: ", 0x07);
    shell_command_print_num(status.queries_tx);
    video_print("/", 0x07);
    shell_command_print_num(status.replies_rx);
    video_print("/", 0x07);
    shell_command_print_num(status.cache_hits);
    video_print("/", 0x07);
    shell_command_print_num(status.cache_misses);
    video_print("\n  Invalidos/ignorados/timeouts: ", 0x07);
    shell_command_print_num(status.invalid_packets);
    video_print("/", 0x07);
    shell_command_print_num(status.ignored_packets);
    video_print("/", 0x07);
    shell_command_print_num(status.timeouts);
    video_print("  Ultimo erro: ", 0x07);
    shell_command_print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
}

static void cmd_net_dns_table(void) {
    uint8_t found = 0;

    video_print("Cache DNS:\n", 0x0B);
    for (uint32_t index = 0; index < DNS_CACHE_CAPACITY; index++) {
        dns_cache_entry_info_t entry;

        if (dns_get_cache_entry(index, &entry) != OK) {
            video_print("  Erro ao consultar entrada.\n", 0x0C);
            return;
        }
        if (!entry.used) continue;
        found = 1;
        video_print("  ", 0x07);
        video_print(entry.name, 0x0B);
        video_print("  ", 0x07);
        cmd_net_print_ipv4(entry.address);
        video_print("  ttl=", 0x07);
        shell_command_print_num(entry.ttl_remaining_seconds);
        video_print("s idade=", 0x07);
        shell_command_print_num(entry.age_seconds);
        video_print("s\n", 0x07);
    }
    if (!found) video_print("  Vazio.\n", 0x08);
}

static void cmd_net_dns_config(const char* args) {
    char server_text[SHELL_IPV4_TEXT_SIZE];
    uint32_t server_ip = 0;
    int result = shell_command_read_single_arg(args, server_text,
                                       sizeof(server_text));

    if (result != OK ||
        cmd_net_parse_ipv4(server_text, &server_ip) != OK) {
        LOG_WARN("SHELL", "Uso invalido de net dns config");
        video_print("Uso: net dns config <servidor>\n", 0x0C);
        return;
    }
    result = network_manager_configure_dns(server_ip);
    if (result != OK) {
        video_print("Erro: configuracao DNS falhou (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Servidor DNS configurado: ", 0x0A);
    cmd_net_print_ipv4(server_ip);
    video_print(".\n", 0x0A);
}

static void cmd_net_dns(const char* args) {
    const char* config_args = shell_command_match_subcommand(args, "config");

    if (config_args) {
        cmd_net_dns_config(config_args);
        return;
    }
    if (shell_command_args_equal(args, "status")) {
        cmd_net_dns_status();
        return;
    }
    if (shell_command_args_equal(args, "table")) {
        cmd_net_dns_table();
        return;
    }
    if (shell_command_args_equal(args, "clear")) {
        if (dns_clear() == OK) video_print("Cache DNS limpo.\n", 0x0A);
        else video_print("Erro: cache DNS nao foi limpo.\n", 0x0C);
        return;
    }
    LOG_WARN("SHELL", "Uso invalido de net dns");
    video_print("Uso: net dns config <servidor> | status | table | clear\n",
                0x0C);
}

static int cmd_dns_wait(const char* name, uint32_t* out_ip) {
    dns_status_t status;
    uint32_t frequency = timer_get_frequency();
    uint32_t start_tick = timer_get_ticks();
    uint32_t wait_ticks;
    uint8_t resolved = 0;
    int result;

    if (!name || !out_ip) {
        LOG_ERROR("SHELL", "Destino nulo na espera DNS");
        return ERR_NULL;
    }
    if (!frequency ||
        frequency > SHELL_MAX_TICK_INTERVAL / SHELL_DNS_WAIT_SECONDS) {
        LOG_ERROR("SHELL", "Timer invalido na espera DNS");
        return ERR_STATE;
    }
    wait_ticks = frequency * SHELL_DNS_WAIT_SECONDS;
    result = dns_resolve(name, out_ip, &resolved);
    if (result != OK || resolved) return result;
    do {
        if (dns_get_status(&status) != OK) return ERR_STATE;
        if (status.state == DNS_STATE_COMPLETE) {
            *out_ip = status.result_ip;
            return OK;
        }
        if (status.state == DNS_STATE_FAILED) {
            return status.last_error == OK ? ERR_STATE :
                                             status.last_error;
        }
        if (shell_network_job_block_tick()) {
            shell_network_job_inner_failed = 1U;
            return ERR_TIMEOUT;
        }
    } while ((uint32_t)(timer_get_ticks() - start_tick) <= wait_ticks);
    LOG_WARN("SHELL", "Guarda de tempo DNS expirou");
    shell_network_job_inner_failed = 1U;
    return ERR_TIMEOUT;
}

static void cmd_nslookup(const char* args) {
    char name[SHELL_DNS_NAME_SIZE];
    dns_status_t status;
    uint32_t address = 0;
    int result = shell_command_read_single_arg(args, name, sizeof(name));

    if (result != OK) {
        LOG_WARN("SHELL", "Uso invalido de nslookup");
        video_print("Uso: nslookup <dominio>\n", 0x0C);
        return;
    }
    result = cmd_dns_wait(name, &address);
    if (result != OK || dns_get_status(&status) != OK) {
        video_print("Erro: consulta DNS falhou (codigo ", 0x0C);
        shell_command_print_num((uint32_t)(result != OK ? result : ERR_STATE));
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Servidor: ", 0x07);
    cmd_net_print_ipv4(status.server_ip);
    video_print("\nNome: ", 0x07);
    video_print(status.canonical_name[0] ?
                status.canonical_name : name, 0x0B);
    video_print("\nEndereco: ", 0x07);
    cmd_net_print_ipv4(address);
    video_print("\n", 0x07);
}

static int cmd_ping_parse_count(const char* text, uint8_t* out_count) {
    uint32_t value = 0;

    if (!text || !out_count) {
        LOG_ERROR("SHELL", "Destino nulo ao interpretar quantidade ping");
        return ERR_NULL;
    }
    if (!*text) {
        LOG_WARN("SHELL", "Quantidade ausente no ping");
        return ERR_INVALID;
    }
    while (*text) {
        if (*text < '0' || *text > '9') {
            LOG_WARN("SHELL", "Quantidade nao numerica no ping");
            return ERR_INVALID;
        }
        value = value * 10U + (uint32_t)(*text - '0');
        if (value > ICMP_PING_MAX_COUNT) {
            LOG_WARN("SHELL", "Quantidade excede limite do ping");
            return ERR_INVALID;
        }
        text++;
    }
    if (!value) {
        LOG_WARN("SHELL", "Quantidade zero no ping");
        return ERR_INVALID;
    }
    *out_count = (uint8_t)value;
    return OK;
}

static int cmd_ping_parse_args(const char* args, char* out_target,
                               uint32_t target_size,
                               uint8_t* out_count) {
    const char* cursor = args;
    char count_text[4];
    int result;

    if (!args || !out_target || !target_size || !out_count) {
        LOG_ERROR("SHELL", "Argumentos nulos ao interpretar ping");
        return ERR_NULL;
    }
    result = shell_command_read_token(&cursor, out_target, target_size);
    if (result != OK) return result;
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    *out_count = ICMP_PING_DEFAULT_COUNT;
    if (*cursor) {
        result = shell_command_read_token(&cursor, count_text, sizeof(count_text));
        if (result != OK ||
            cmd_ping_parse_count(count_text, out_count) != OK) {
            return ERR_INVALID;
        }
    }
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (*cursor) {
        LOG_WARN("SHELL", "Sintaxe invalida no ping");
        return ERR_INVALID;
    }
    return OK;
}

static uint8_t cmd_ping_target_is_numeric(const char* target) {
    uint8_t saw_dot = 0;

    if (!target || !*target) return 0;
    while (*target) {
        if (*target == '.') saw_dot = 1;
        else if (*target < '0' || *target > '9') return 0;
        target++;
    }
    return saw_dot;
}

static int cmd_ping_resolve_target(const char* target,
                                   uint32_t* out_ip) {
    if (!target || !out_ip) {
        LOG_ERROR("SHELL", "Destino nulo ao resolver ping");
        return ERR_NULL;
    }
    if (cmd_ping_target_is_numeric(target)) {
        if (cmd_net_parse_ipv4(target, out_ip) != OK ||
            !ipv4_address_is_unicast(*out_ip)) return ERR_INVALID;
        return OK;
    }
    return cmd_dns_wait(target, out_ip);
}

static void cmd_ping_print_event(const icmp_status_t* status) {
    uint32_t sequence =
        (uint32_t)status->received + status->timeouts;

    if (status->last_event == ICMP_PING_EVENT_REPLY) {
        video_print("Resposta de ", 0x0A);
        cmd_net_print_ipv4(status->target_ip);
        video_print(": bytes=", 0x07);
        shell_command_print_num(ICMP_ECHO_DATA_SIZE);
        video_print(" seq=", 0x07);
        shell_command_print_num(sequence);
        video_print(" ttl=", 0x07);
        shell_command_print_num(status->last_reply_ttl);
        video_print(" tempo=", 0x07);
        shell_command_print_num(cmd_net_ticks_to_milliseconds(
            status->last_rtt_ticks));
        video_print("ms\n", 0x07);
    } else if (status->last_event == ICMP_PING_EVENT_TIMEOUT) {
        video_print("Timeout para ", 0x0E);
        cmd_net_print_ipv4(status->target_ip);
        video_print(" seq=", 0x07);
        shell_command_print_num(sequence);
        video_print("\n", 0x07);
    }
}

static int cmd_ping_wait(uint8_t print_events,
                         icmp_status_t* out_status) {
    icmp_status_t status;
    uint32_t frequency = timer_get_frequency();
    uint32_t start_tick;
    uint32_t wait_ticks;
    uint32_t event_generation;
    uint32_t wait_seconds;

    if (!out_status) {
        LOG_ERROR("SHELL", "Destino nulo na espera de ping");
        return ERR_NULL;
    }
    if (icmp_get_status(&status) != OK || !frequency) {
        LOG_ERROR("SHELL", "Estado ou timer indisponivel para ping");
        return ERR_STATE;
    }
    wait_seconds =
        (uint32_t)status.requested_count + SHELL_PING_WAIT_EXTRA_SECONDS;
    if (frequency > SHELL_MAX_TICK_INTERVAL / wait_seconds) {
        LOG_ERROR("SHELL", "Intervalo do ping excede timer");
        return ERR_STATE;
    }
    wait_ticks = frequency * wait_seconds;
    start_tick = timer_get_ticks();
    event_generation = status.event_generation;
    do {
        if (icmp_get_status(&status) != OK) {
            LOG_ERROR("SHELL", "Estado ICMP perdido durante ping");
            return ERR_STATE;
        }
        if (status.event_generation != event_generation) {
            event_generation = status.event_generation;
            if (print_events) cmd_ping_print_event(&status);
        }
        if (status.state == ICMP_PING_COMPLETE) {
            *out_status = status;
            return OK;
        }
        if (status.state == ICMP_PING_FAILED) {
            *out_status = status;
            return status.last_error == OK ? ERR_STATE :
                                             status.last_error;
        }
        if (shell_network_job_block_tick()) {
            shell_network_job_inner_failed = 1U;
            return ERR_TIMEOUT;
        }
    } while ((uint32_t)(timer_get_ticks() - start_tick) <= wait_ticks);
    *out_status = status;
    LOG_WARN("SHELL", "Guarda de tempo do ping expirou");
    shell_network_job_inner_failed = 1U;
    return ERR_TIMEOUT;
}

static void cmd_ping_print_summary(const icmp_status_t* status) {
    uint32_t lost = status->requested_count - status->received;
    uint32_t loss_percent = status->requested_count ?
        lost * 100U / status->requested_count : 0U;
    uint32_t average_ticks = status->received ?
        status->rtt_total_ticks / status->received : 0U;

    video_print("\nResumo de ", 0x0B);
    cmd_net_print_ipv4(status->target_ip);
    video_print(": enviados=", 0x07);
    shell_command_print_num(status->sent);
    video_print(" recebidos=", 0x07);
    shell_command_print_num(status->received);
    video_print(" perdidos=", 0x07);
    shell_command_print_num(lost);
    video_print(" (", 0x07);
    shell_command_print_num(loss_percent);
    video_print("%)\nRTT min/medio/max = ", 0x07);
    shell_command_print_num(cmd_net_ticks_to_milliseconds(status->rtt_min_ticks));
    video_print("/", 0x07);
    shell_command_print_num(cmd_net_ticks_to_milliseconds(average_ticks));
    video_print("/", 0x07);
    shell_command_print_num(cmd_net_ticks_to_milliseconds(status->rtt_max_ticks));
    video_print(" ms\n", 0x07);
}

static int cmd_ping_execute(uint32_t target_ip, uint8_t count,
                            uint8_t print_events,
                            icmp_status_t* out_status) {
    int result = icmp_ping_start(target_ip, count,
                                 ICMP_PING_TIMEOUT_SECONDS);

    if (result != OK) return result;
    return cmd_ping_wait(print_events, out_status);
}

static void cmd_ping(const char* args) {
    icmp_status_t status;
    char target_text[SHELL_DNS_NAME_SIZE];
    uint32_t target_ip = 0;
    uint8_t count = ICMP_PING_DEFAULT_COUNT;
    int result = cmd_ping_parse_args(
        args, target_text, sizeof(target_text), &count);

    kmemset(&status, 0, sizeof(status));
    if (result != OK) {
        LOG_WARN("SHELL", "Uso invalido de ping");
        video_print("Uso: ping <ip-ou-dominio> [quantidade 1-10]\n",
                    0x0C);
        return;
    }
    result = cmd_ping_resolve_target(target_text, &target_ip);
    if (result != OK) {
        LOG_WARN("SHELL", "Destino do ping nao foi resolvido");
        video_print("Erro: destino do ping nao resolvido (codigo ",
                    0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("PING ", 0x0B);
    video_print(target_text, 0x0B);
    video_print(" [", 0x07);
    cmd_net_print_ipv4(target_ip);
    video_print("] com 32 bytes de dados:\n", 0x07);
    result = cmd_ping_execute(target_ip, count, 1U, &status);
    if (result != OK) {
        LOG_WARN("SHELL", "Ping nao concluiu normalmente");
        video_print("Erro: ping falhou (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        if (result == ERR_TIMEOUT &&
            (status.state == ICMP_PING_RESOLVING ||
             status.state == ICMP_PING_WAITING_REPLY)) {
            icmp_reset();
        }
        return;
    }
    cmd_ping_print_summary(&status);
}

static int cmd_net_parse_port(const char* text, uint16_t* out_port) {
    uint32_t value = 0;

    if (!text || !out_port) {
        LOG_ERROR("SHELL", "Destino nulo ao interpretar porta TCP");
        return ERR_NULL;
    }
    if (!*text) return ERR_INVALID;
    while (*text) {
        if (*text < '0' || *text > '9') return ERR_INVALID;
        value = value * 10U + (uint32_t)(*text - '0');
        if (value > 65535U) return ERR_INVALID;
        text++;
    }
    if (!value) return ERR_INVALID;
    *out_port = (uint16_t)value;
    return OK;
}

static void cmd_net_tcp_status(void) {
    tcp_status_t status;

    if (tcp_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado TCP indisponivel");
        video_print("Erro: estado TCP indisponivel.\n", 0x0C);
        return;
    }
    video_print("TCP:\n  Estado: ", 0x0B);
    video_print(status.initialized ? "DISPONIVEL" : "INDISPONIVEL",
                status.initialized ? 0x0A : 0x0E);
    video_print("\n  Conexoes: ", 0x07);
    shell_command_print_num(status.connection_count);
    video_print("/", 0x07);
    shell_command_print_num(TCP_CONNECTION_CAPACITY);
    video_print("  Segmentos RX/TX: ", 0x07);
    shell_command_print_num(status.segments_rx);
    video_print("/", 0x07);
    shell_command_print_num(status.segments_tx);
    video_print("\n  Bytes RX/TX: ", 0x07);
    shell_command_print_num(status.bytes_rx);
    video_print("/", 0x07);
    shell_command_print_num(status.bytes_tx);
    video_print("  SYN TX/SYN-ACK RX: ", 0x07);
    shell_command_print_num(status.syn_tx);
    video_print("/", 0x07);
    shell_command_print_num(status.syn_ack_rx);
    video_print("\n  FIN RX/TX: ", 0x07);
    shell_command_print_num(status.fin_rx);
    video_print("/", 0x07);
    shell_command_print_num(status.fin_tx);
    video_print("  RST RX/TX: ", 0x07);
    shell_command_print_num(status.resets_rx);
    video_print("/", 0x07);
    shell_command_print_num(status.resets_tx);
    video_print("\n  Retransmissoes/timeouts: ", 0x07);
    shell_command_print_num(status.retransmissions);
    video_print("/", 0x07);
    shell_command_print_num(status.timeouts);
    video_print("  Invalidos/checksum: ", 0x07);
    shell_command_print_num(status.rx_invalid);
    video_print("/", 0x07);
    shell_command_print_num(status.rx_checksum_errors);
    video_print("\n  Sem conexao/duplicados/fora de ordem: ", 0x07);
    shell_command_print_num(status.rx_no_connection);
    video_print("/", 0x07);
    shell_command_print_num(status.rx_duplicates);
    video_print("/", 0x07);
    shell_command_print_num(status.rx_out_of_order);
    video_print("\n  Ultimo erro: ", 0x07);
    shell_command_print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
}

static int cmd_net_wait_socket_connected(
    net_socket_handle_t handle, net_socket_info_t* out_info) {
    uint32_t frequency = timer_get_frequency();
    uint32_t start_tick = timer_get_ticks();
    uint32_t wait_ticks;
    net_socket_event_mask_t events = 0U;
    wait_reason_t reason = WAIT_REASON_NONE;
    net_socket_info_t info;
    int result;

    if (!out_info) {
        LOG_ERROR("SHELL", "Destino nulo na espera TCP");
        return ERR_NULL;
    }
    if (!frequency ||
        frequency > SHELL_MAX_TICK_INTERVAL / SHELL_TCP_WAIT_SECONDS) {
        LOG_ERROR("SHELL", "Timer invalido na espera TCP");
        return ERR_STATE;
    }
    wait_ticks = frequency * SHELL_TCP_WAIT_SECONDS;
    do {
        uint32_t elapsed = timer_get_ticks() - start_tick;
        uint32_t remaining;
        uint32_t slice;

        if (elapsed >= wait_ticks) {
            reason = WAIT_REASON_TIMEOUT;
            break;
        }
        remaining = wait_ticks - elapsed;
        slice = remaining < SHELL_SOCKET_WAIT_SLICE_TICKS ?
                remaining : SHELL_SOCKET_WAIT_SLICE_TICKS;

        result = net_socket_wait(handle, NET_SOCKET_EVENT_CONNECTED,
                                 slice, &events, &reason);
        if (result != OK) return result;
        if (reason != WAIT_REASON_TIMEOUT) break;
        if (shell_job_is_active()) {
            shell_job_pump_events();
            if (shell_job_cancel_requested()) return ERR_TIMEOUT;
        }
    } while ((uint32_t)(timer_get_ticks() - start_tick) < wait_ticks);
    if (reason == WAIT_REASON_TIMEOUT) {
        LOG_WARN("SHELL", "Guarda de tempo TCP expirou");
        shell_network_job_inner_failed = 1U;
        return ERR_TIMEOUT;
    }
    result = net_socket_get_handle_info(handle, &info);
    if (result != OK) return result;
    *out_info = info;
    if ((events & NET_SOCKET_EVENT_CONNECTED) != 0U &&
        info.state == NET_SOCKET_STATE_CONNECTED) return OK;
    if ((events & NET_SOCKET_EVENT_ERROR) != 0U ||
        info.state == NET_SOCKET_STATE_ERROR) {
        return info.last_error == OK ? ERR_STATE : info.last_error;
    }
    return ERR_STATE;
}

static void cmd_net_tcp_connect(const char* args) {
    char target[SHELL_DNS_NAME_SIZE];
    char port_text[6];
    net_socket_handle_t handle = 0;
    net_socket_info_t info;
    uint32_t address = 0;
    uint16_t port = 0;
    int result;

    result = shell_command_read_two_args(args, target, sizeof(target),
                                 port_text, sizeof(port_text));
    if (result != OK ||
        cmd_net_parse_port(port_text, &port) != OK) {
        LOG_WARN("SHELL", "Uso invalido de net tcp connect");
        video_print("Uso: net tcp connect <ip-ou-dominio> <porta>\n",
                    0x0C);
        return;
    }
    result = cmd_ping_resolve_target(target, &address);
    if (result == OK) {
        result = net_socket_open(NET_SOCKET_TYPE_STREAM, &handle);
    }
    if (result == OK) result = net_socket_connect(handle, address, port);
    if (result == OK) {
        video_print("Conectando a ", 0x07);
        cmd_net_print_ipv4(address);
        video_print(":", 0x07);
        shell_command_print_num(port);
        video_print("...\n", 0x07);
        result = cmd_net_wait_socket_connected(handle, &info);
    }
    if (result != OK) {
        if (handle) net_socket_abort(handle);
        video_print("Erro: conexao TCP falhou (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Conexao TCP estabelecida. Porta local: ", 0x0A);
    shell_command_print_num(info.local_port);
    video_print(".\n", 0x0A);
    net_socket_abort(handle);
}

static void cmd_net_tcp(const char* args) {
    const char* connect_args = shell_command_match_subcommand(args, "connect");

    if (shell_command_args_equal(args, "status")) {
        cmd_net_tcp_status();
        return;
    }
    if (connect_args) {
        cmd_net_tcp_connect(connect_args);
        return;
    }
    LOG_WARN("SHELL", "Uso invalido de net tcp");
    video_print("Uso: net tcp status | "
                "net tcp connect <ip-ou-dominio> <porta>\n", 0x0C);
}

static void cmd_net_socket_status(void) {
    net_socket_status_t status;

    if (net_socket_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado de sockets indisponivel");
        video_print("Erro: estado de sockets indisponivel.\n", 0x0C);
        return;
    }
    video_print("Sockets nativos:\n  Estado: ", 0x0B);
    video_print(status.initialized ? "DISPONIVEL" : "INDISPONIVEL",
                status.initialized ? 0x0A : 0x0E);
    video_print("\n  Ativos: ", 0x07);
    shell_command_print_num(status.active_count);
    video_print("/", 0x07);
    shell_command_print_num(NET_SOCKET_CAPACITY);
    video_print("  Opens/connects/closes/aborts: ", 0x07);
    shell_command_print_num(status.opens);
    video_print("/", 0x07);
    shell_command_print_num(status.connects);
    video_print("/", 0x07);
    shell_command_print_num(status.closes);
    video_print("/", 0x07);
    shell_command_print_num(status.aborts);
    video_print("\n  Bytes fila TX/TCP TX/TCP RX/leitura: ", 0x07);
    shell_command_print_num(status.bytes_queued_tx);
    video_print("/", 0x07);
    shell_command_print_num(status.bytes_sent_tcp);
    video_print("/", 0x07);
    shell_command_print_num(status.bytes_received_tcp);
    video_print("/", 0x07);
    shell_command_print_num(status.bytes_read);
    video_print("  Overflows/stale: ", 0x07);
    shell_command_print_num(status.rx_overflows);
    video_print("/", 0x07);
    shell_command_print_num(status.stale_handles);
    video_print("\n  Waits/eventos/timeouts/cancelamentos/falhas: ", 0x07);
    shell_command_print_num(status.wait_calls);
    video_print("/", 0x07);
    shell_command_print_num(status.wait_events);
    video_print("/", 0x07);
    shell_command_print_num(status.wait_timeouts);
    video_print("/", 0x07);
    shell_command_print_num(status.wait_cancellations);
    video_print("/", 0x07);
    shell_command_print_num(status.wait_failures);
    video_print("\n  Ultimo erro: ", 0x07);
    shell_command_print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
}

static void cmd_net_socket_table(void) {
    uint8_t any = 0;

    video_print("Tabela de sockets:\n", 0x0B);
    for (uint32_t index = 0; index < NET_SOCKET_CAPACITY; index++) {
        net_socket_info_t info;

        if (net_socket_get_info(index, &info) != OK || !info.used) {
            continue;
        }
        any = 1;
        video_print("  #", 0x07);
        shell_command_print_num(info.handle);
        video_print(" ", 0x07);
        video_print(net_socket_state_name(info.state),
                    info.state == NET_SOCKET_STATE_CONNECTED ? 0x0A :
                    info.state == NET_SOCKET_STATE_ERROR ? 0x0C : 0x0E);
        video_print(" ", 0x07);
        cmd_net_print_ipv4(info.remote_ip);
        video_print(":", 0x07);
        shell_command_print_num(info.remote_port);
        video_print(" local=", 0x07);
        shell_command_print_num(info.local_port);
        video_print(" tx/rx=", 0x07);
        shell_command_print_num(info.tx_queued);
        video_print("/", 0x07);
        shell_command_print_num(info.rx_queued);
        video_print(" waiters=", 0x07);
        shell_command_print_num(info.waiters);
        video_print("\n", 0x07);
    }
    if (!any) video_print("  Vazia.\n", 0x08);
}

static void cmd_net_socket_check_case(const char* name, uint8_t passed) {
    video_print("  ", 0x07);
    video_print(name, 0x07);
    video_print(": ", 0x07);
    video_print(passed ? "OK\n" : "ERRO\n", passed ? 0x0A : 0x0C);
}

static void cmd_net_socket_check(void) {
    net_socket_self_test_result_t test;
    int result = net_socket_self_test(&test);

    video_print("Autoteste de sockets e filas privadas:\n", 0x0B);
    cmd_net_socket_check_case("ciclo", test.lifecycle);
    cmd_net_socket_check_case("mapeamento de eventos", test.event_mapping);
    cmd_net_socket_check_case("READABLE acorda um", test.readable_wake_one);
    cmd_net_socket_check_case("terminais acordam todos",
                              test.terminal_wake_all);
    cmd_net_socket_check_case("timeout", test.timeout);
    cmd_net_socket_check_case("cancelamento", test.cancellation);
    cmd_net_socket_check_case("invariantes", test.invariants);
    video_print("Resultado: ", 0x0B);
    video_print(result == OK ? "OK\n" : "ERRO\n",
                result == OK ? 0x0A : 0x0C);
}

static void cmd_net_socket(const char* args) {
    if (shell_command_args_equal(args, "status")) {
        cmd_net_socket_status();
        return;
    }
    if (shell_command_args_equal(args, "table")) {
        cmd_net_socket_table();
        return;
    }
    if (shell_command_args_equal(args, "check")) {
        cmd_net_socket_check();
        return;
    }
    LOG_WARN("SHELL", "Uso invalido de net socket");
    video_print("Uso: net socket status | net socket table | "
                "net socket check\n", 0x0C);
}

static int cmd_http_wait(http_status_t* out_status) {
    uint32_t frequency = timer_get_frequency();
    uint32_t start_tick = timer_get_ticks();
    uint32_t wait_ticks;
    http_status_t* status = &shell_http_wait_status;

    if (!out_status) {
        LOG_ERROR("SHELL", "Destino nulo na espera HTTP");
        return ERR_NULL;
    }
    if (!frequency ||
        frequency > SHELL_MAX_TICK_INTERVAL / SHELL_HTTP_WAIT_SECONDS) {
        LOG_ERROR("SHELL", "Timer invalido na espera HTTP");
        return ERR_STATE;
    }
    wait_ticks = frequency * SHELL_HTTP_WAIT_SECONDS;
    do {
        if (http_get_status(status) != OK) return ERR_STATE;
        if (status->state == HTTP_STATE_COMPLETE) {
            *out_status = *status;
            return OK;
        }
        if (status->state == HTTP_STATE_FAILED) {
            *out_status = *status;
            return status->last_error == OK ? ERR_STATE :
                                              status->last_error;
        }
        if (shell_network_job_block_tick()) {
            shell_network_job_inner_failed = 1U;
            return ERR_TIMEOUT;
        }
    } while ((uint32_t)(timer_get_ticks() - start_tick) <= wait_ticks);
    *out_status = *status;
    LOG_WARN("SHELL", "Guarda de tempo HTTP expirou");
    shell_network_job_inner_failed = 1U;
    return ERR_TIMEOUT;
}

static void cmd_http_status(void) {
    http_status_t status;

    if (http_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado HTTP indisponivel");
        video_print("Erro: estado HTTP indisponivel.\n", 0x0C);
        return;
    }
    video_print("HTTP:\n  Estado: ", 0x0B);
    video_print(http_state_name(status.state),
                status.state == HTTP_STATE_COMPLETE ? 0x0A :
                status.state == HTTP_STATE_FAILED ? 0x0C : 0x0E);
    video_print("\n  URL: ", 0x07);
    video_print(status.url[0] ? status.url : "N/D", 0x07);
    video_print("\n  Destino: ", 0x07);
    if (status.resolved_ip) cmd_net_print_ipv4(status.resolved_ip);
    else video_print("N/D", 0x08);
    video_print(":", 0x07);
    shell_command_print_num(status.port);
    video_print("  Status: ", 0x07);
    shell_command_print_num(status.status_code);
    video_print("\n  Headers/corpo: ", 0x07);
    shell_command_print_num(status.headers_length);
    video_print("/", 0x07);
    shell_command_print_num(status.body_length);
    video_print("  Content-Length: ", 0x07);
    if (status.has_content_length) shell_command_print_num(status.content_length);
    else video_print("N/D (ate EOF)", 0x08);
    video_print("\n  Requests/respostas: ", 0x07);
    shell_command_print_num(status.requests_tx);
    video_print("/", 0x07);
    shell_command_print_num(status.responses_rx);
    video_print("  Bytes RX: ", 0x07);
    shell_command_print_num(status.bytes_rx);
    video_print("  Parse/overflow/timeout: ", 0x07);
    shell_command_print_num(status.parse_errors);
    video_print("/", 0x07);
    shell_command_print_num(status.overflows);
    video_print("/", 0x07);
    shell_command_print_num(status.timeouts);
    video_print("\n  Ultimo erro: ", 0x07);
    shell_command_print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
}

static void cmd_http_print_preview(void) {
    const uint8_t* body;
    uint32_t length;

    if (http_get_body(&body, &length) != OK) return;
    if (length > SHELL_HTTP_PREVIEW_SIZE) {
        length = SHELL_HTTP_PREVIEW_SIZE;
    }
    for (uint32_t index = 0; index < length; index++) {
        uint8_t value = body[index];

        shell_http_preview[index] =
            (value >= 0x20U && value <= 0x7EU) ||
            value == '\n' || value == '\r' || value == '\t' ?
            (char)value : '.';
    }
    shell_http_preview[length] = '\0';
    video_print("Previa do corpo", 0x0B);
    if (length == SHELL_HTTP_PREVIEW_SIZE) {
        video_print(" (primeiros 512 bytes)", 0x0B);
    }
    video_print(":\n", 0x0B);
    video_print(length ? shell_http_preview : "(vazio)", 0x07);
    video_print("\n", 0x07);
}

static int cmd_http_execute(const char* url, uint8_t print_result,
                            http_status_t* out_status) {
    int result;

    if (!url || !out_status) {
        LOG_ERROR("SHELL", "Argumento nulo no GET HTTP");
        return ERR_NULL;
    }
    result = http_get_start(url);
    if (result == OK) result = cmd_http_wait(out_status);
    if (result == OK && print_result) {
        video_print("HTTP ", 0x0A);
        shell_command_print_num(out_status->status_code);
        video_print("  headers=", 0x07);
        shell_command_print_num(out_status->headers_length);
        video_print(" corpo=", 0x07);
        shell_command_print_num(out_status->body_length);
        video_print(" bytes\n", 0x07);
        cmd_http_print_preview();
    }
    return result;
}

static void cmd_http(const char* args) {
    const char* get_args = shell_command_match_subcommand(args, "get");
    int result;

    if (shell_command_args_equal(args, "status")) {
        cmd_http_status();
        return;
    }
    if (!get_args ||
        shell_command_read_single_arg(get_args, shell_http_command_url,
                              sizeof(shell_http_command_url)) != OK) {
        LOG_WARN("SHELL", "Uso invalido de http");
        video_print("Uso: http get <url> | http status\n", 0x0C);
        return;
    }
    video_print("Executando HTTP GET...\n", 0x07);
    result = cmd_http_execute(shell_http_command_url, 1U,
                              &shell_http_command_status);
    if (result != OK) {
        if (result == ERR_UNAVAILABLE) {
            video_print(
                "Erro: resposta usa Transfer-Encoding nao suportado.\n",
                0x0C);
        } else {
            video_print("Erro: HTTP GET falhou (codigo ", 0x0C);
            shell_command_print_num((uint32_t)result);
            video_print(").\n", 0x0C);
        }
    }
}

static int cmd_net_find_arp_entry(uint32_t ip_address,
                                  arp_cache_entry_info_t* out_entry,
                                  uint8_t* out_found) {
    if (!out_entry || !out_found) {
        LOG_ERROR("SHELL", "Destino nulo ao buscar entrada ARP");
        return ERR_NULL;
    }
    *out_found = 0;
    kmemset(out_entry, 0, sizeof(*out_entry));
    for (uint32_t index = 0; index < ARP_CACHE_CAPACITY; index++) {
        int result = arp_get_cache_entry(index, out_entry);

        if (result != OK) {
            LOG_ERROR("SHELL", "Falha ao buscar entrada ARP da suite");
            return result;
        }
        if (out_entry->used && out_entry->ip_address == ip_address) {
            *out_found = 1;
            return OK;
        }
    }
    return OK;
}

static int cmd_net_wait_arp_state(uint32_t ip_address,
                                  arp_entry_state_t expected_state) {
    arp_cache_entry_info_t entry;
    uint32_t frequency = timer_get_frequency();
    uint32_t start_tick;
    uint32_t wait_ticks;
    uint8_t found;
    int result;

    if (!frequency ||
        frequency > SHELL_MAX_TICK_INTERVAL / SHELL_NET_CHECK_WAIT_SECONDS) {
        LOG_ERROR("SHELL", "Timer invalido na espera da suite ARP");
        return ERR_STATE;
    }
    wait_ticks = frequency * SHELL_NET_CHECK_WAIT_SECONDS;
    start_tick = timer_get_ticks();
    do {
        result = cmd_net_find_arp_entry(ip_address, &entry, &found);
        if (result != OK) return result;
        if (found && entry.state == expected_state) return OK;
        if (shell_network_job_block_tick()) {
            shell_network_job_inner_failed = 1U;
            return ERR_TIMEOUT;
        }
    } while ((uint32_t)(timer_get_ticks() - start_tick) <= wait_ticks);
    LOG_WARN("SHELL", "Estado ARP esperado nao apareceu na suite");
    shell_network_job_inner_failed = 1U;
    return ERR_TIMEOUT;
}

static int cmd_net_check_qemu_reply(const arp_status_t* baseline,
                                    shell_net_qemu_check_t* check) {
    arp_status_t after_reply;
    arp_status_t after_hit;
    uint8_t mac_address[ARP_MAC_ADDRESS_SIZE];
    uint8_t resolved = 0;
    int result;

    if (!baseline || !check) {
        LOG_ERROR("SHELL", "Estado nulo no teste ARP de reply");
        return ERR_NULL;
    }
    result = arp_resolve(SHELL_NET_QEMU_REPLY_IPV4, mac_address, &resolved);
    if (result != OK || resolved) {
        LOG_WARN("SHELL", "Request inicial da suite ARP falhou");
        return result != OK ? result : ERR_STATE;
    }
    result = cmd_net_wait_arp_state(SHELL_NET_QEMU_REPLY_IPV4,
                                    ARP_ENTRY_RESOLVED);
    if (result != OK) return result;
    result = arp_get_status(&after_reply);
    if (result != OK) {
        LOG_ERROR("SHELL", "Estado ARP indisponivel depois do reply");
        return result;
    }
    check->reply = after_reply.rx_replies > baseline->rx_replies &&
                   after_reply.tx_requests > baseline->tx_requests;
    result = arp_resolve(SHELL_NET_QEMU_REPLY_IPV4, mac_address, &resolved);
    if (result != OK) {
        LOG_WARN("SHELL", "Consulta de cache ARP falhou na suite");
        return result;
    }
    result = arp_get_status(&after_hit);
    if (result != OK) {
        LOG_ERROR("SHELL", "Estado ARP indisponivel depois do cache hit");
        return result;
    }
    check->cache_hit = resolved &&
        after_hit.cache_hits == after_reply.cache_hits + 1U &&
        after_hit.tx_requests == after_reply.tx_requests;
    return OK;
}

static int cmd_net_check_qemu_timeout(const arp_status_t* baseline,
                                      shell_net_qemu_check_t* check) {
    arp_cache_entry_info_t entry;
    arp_status_t after_timeout;
    uint8_t mac_address[ARP_MAC_ADDRESS_SIZE];
    uint8_t resolved = 0;
    uint8_t found = 0;
    int result;

    if (!baseline || !check) {
        LOG_ERROR("SHELL", "Estado nulo no teste ARP de timeout");
        return ERR_NULL;
    }
    result = arp_resolve(SHELL_NET_QEMU_TIMEOUT_IPV4,
                         mac_address, &resolved);
    if (result != OK || resolved) {
        LOG_WARN("SHELL", "Request de timeout da suite ARP falhou");
        return result != OK ? result : ERR_STATE;
    }
    result = cmd_net_wait_arp_state(SHELL_NET_QEMU_TIMEOUT_IPV4,
                                    ARP_ENTRY_FAILED);
    if (result != OK) return result;
    result = cmd_net_find_arp_entry(SHELL_NET_QEMU_TIMEOUT_IPV4,
                                    &entry, &found);
    if (result != OK) return result;
    result = arp_get_status(&after_timeout);
    if (result != OK) {
        LOG_ERROR("SHELL", "Estado ARP indisponivel depois do timeout");
        return result;
    }
    check->timeout = found && entry.state == ARP_ENTRY_FAILED &&
        entry.attempts == SHELL_NET_CHECK_EXPECTED_ATTEMPTS &&
        after_timeout.timeouts > baseline->timeouts;
    return OK;
}

static void cmd_net_check_print_case(const char* label, uint8_t passed) {
    video_print("  ", 0x07);
    video_print(label, 0x07);
    video_print(": ", 0x07);
    video_print(passed ? "OK\n" : "ERRO\n", passed ? 0x0A : 0x0C);
}

static int cmd_net_check_qemu_icmp(
    const ipv4_status_t* ipv4_baseline,
    const icmp_status_t* icmp_baseline,
    shell_net_qemu_check_t* check) {
    ipv4_status_t ipv4_after;
    icmp_status_t icmp_after;
    int result;

    if (!ipv4_baseline || !icmp_baseline || !check) {
        LOG_ERROR("SHELL", "Estado nulo no teste ICMP QEMU");
        return ERR_NULL;
    }
    result = cmd_ping_execute(SHELL_NET_QEMU_REPLY_IPV4, 1U, 0U,
                              &icmp_after);
    if (result != OK) {
        LOG_WARN("SHELL", "Ping ICMP da suite QEMU falhou");
        return result;
    }
    if (ipv4_get_status(&ipv4_after) != OK) {
        LOG_ERROR("SHELL", "Estado IPv4 final da suite indisponivel");
        return ERR_STATE;
    }
    check->ipv4 =
        ipv4_after.tx_packets > ipv4_baseline->tx_packets &&
        ipv4_after.rx_packets > ipv4_baseline->rx_packets &&
        ipv4_after.rx_checksum_errors ==
            ipv4_baseline->rx_checksum_errors &&
        ipv4_after.rx_delivered > ipv4_baseline->rx_delivered;
    check->icmp =
        icmp_after.state == ICMP_PING_COMPLETE &&
        icmp_after.sent == 1U && icmp_after.received == 1U &&
        icmp_after.timeouts == 0U &&
        icmp_after.echo_requests_tx >
            icmp_baseline->echo_requests_tx &&
        icmp_after.echo_replies_rx >
            icmp_baseline->echo_replies_rx &&
        icmp_after.rtt_total_ticks > 0U;
    return check->ipv4 && check->icmp ? OK : ERR_STATE;
}

static int cmd_net_check_qemu_dhcp_acquire(
    const char* id, const udp_status_t* udp_before,
    const dhcp_status_t* dhcp_before,
    shell_net_qemu_dhcp_check_t* check) {
    udp_status_t udp_after;
    dhcp_status_t dhcp_after;
    int result = network_manager_acquire_dhcp(id);

    if (result == OK) result = cmd_net_dhcp_wait(&dhcp_after);
    if (result != OK) return result;
    if (udp_get_status(&udp_after) != OK) {
        LOG_ERROR("SHELL", "Estado UDP final da suite indisponivel");
        return ERR_STATE;
    }
    check->udp =
        udp_after.tx_datagrams > udp_before->tx_datagrams &&
        udp_after.rx_datagrams > udp_before->rx_datagrams &&
        udp_after.rx_checksum_errors ==
            udp_before->rx_checksum_errors;
    check->dhcp =
        dhcp_after.state == DHCP_STATE_BOUND &&
        dhcp_after.discovers_tx > dhcp_before->discovers_tx &&
        dhcp_after.offers_rx > dhcp_before->offers_rx &&
        dhcp_after.requests_tx > dhcp_before->requests_tx &&
        dhcp_after.acks_rx > dhcp_before->acks_rx;
    check->lease =
        dhcp_after.lease.address != 0U &&
        dhcp_after.lease.subnet_mask == SHELL_NET_QEMU_SUBNET_MASK &&
        (dhcp_after.lease.address & SHELL_NET_QEMU_SUBNET_MASK) ==
            0x0A000200U &&
        dhcp_after.lease.gateway == SHELL_NET_QEMU_REPLY_IPV4 &&
        dhcp_after.lease.dns_server == 0x0A000203U;
    return check->udp && check->dhcp && check->lease ? OK : ERR_STATE;
}

static int cmd_net_check_qemu_dns(
    const char* domain, const dns_status_t* dns_before,
    shell_net_qemu_dhcp_check_t* check) {
    static dns_status_t dns_after;
    static udp_status_t udp_before_hit;
    static udp_status_t udp_after_hit;
    uint32_t address = 0;
    uint32_t cached_address = 0;
    uint8_t resolved = 0;
    int result = cmd_dns_wait(domain, &address);

    if (result != OK) return result;
    if (dns_get_status(&dns_after) != OK ||
        udp_get_status(&udp_before_hit) != OK) {
        LOG_ERROR("SHELL", "Estado DNS/UDP final da suite indisponivel");
        return ERR_STATE;
    }
    check->dns =
        ipv4_address_is_unicast(address) &&
        dns_after.queries_tx > dns_before->queries_tx &&
        dns_after.replies_rx > dns_before->replies_rx &&
        dns_after.invalid_packets == dns_before->invalid_packets;
    result = dns_resolve(domain, &cached_address, &resolved);
    if (result != OK || udp_get_status(&udp_after_hit) != OK) {
        return result != OK ? result : ERR_STATE;
    }
    check->dns_cache =
        resolved && cached_address == address &&
        udp_after_hit.tx_datagrams == udp_before_hit.tx_datagrams;
    return check->dns && check->dns_cache ? OK : ERR_STATE;
}

static int cmd_net_check_qemu_dhcp_ping(
    shell_net_qemu_dhcp_check_t* check) {
    ipv4_status_t ipv4_before;
    ipv4_status_t ipv4_after;
    icmp_status_t icmp_before;
    icmp_status_t icmp_after;
    int result;

    if (ipv4_get_status(&ipv4_before) != OK ||
        icmp_get_status(&icmp_before) != OK) {
        LOG_ERROR("SHELL", "Linha-base ICMP S2.6 indisponivel");
        return ERR_STATE;
    }
    result = cmd_ping_execute(SHELL_NET_QEMU_REPLY_IPV4, 1U, 0U,
                              &icmp_after);
    if (result != OK) return result;
    if (ipv4_get_status(&ipv4_after) != OK) {
        LOG_ERROR("SHELL", "Estado IPv4 final da suite indisponivel");
        return ERR_STATE;
    }
    check->icmp =
        ipv4_after.tx_packets > ipv4_before.tx_packets &&
        ipv4_after.rx_packets > ipv4_before.rx_packets &&
        ipv4_after.rx_checksum_errors ==
            ipv4_before.rx_checksum_errors &&
        icmp_after.state == ICMP_PING_COMPLETE &&
        icmp_after.received == 1U &&
        icmp_after.echo_replies_rx > icmp_before.echo_replies_rx;
    return check->icmp ? OK : ERR_STATE;
}

static void cmd_net_check_qemu_dhcp_print(
    const shell_net_qemu_dhcp_check_t* check,
    uint8_t passed) {
    cmd_net_check_print_case("UDP RX/TX e checksum", check->udp);
    cmd_net_check_print_case("DHCP Discover/Offer/Request/ACK",
                             check->dhcp);
    cmd_net_check_print_case("Lease, gateway e DNS QEMU", check->lease);
    cmd_net_check_print_case("Consulta DNS A", check->dns);
    cmd_net_check_print_case("Cache DNS sem novo TX", check->dns_cache);
    cmd_net_check_print_case("IPv4/ICMP para gateway", check->icmp);
    cmd_net_check_print_case("Polling e manutencao", check->polling);
    cmd_net_check_print_case("Invariantes de rede", check->invariants);
    video_print("Resultado da suite: ", 0x07);
    video_print(passed ? "OK\n" : "ERRO\n",
                passed ? 0x0A : 0x0C);
}

static SHELL_NOINLINE void cmd_net_check_qemu_dhcp(const char* args) {
    /* O compilador nao pode incorporar estes snapshots ao dispatcher:
       a pilha do processo Shell possui somente 4 KiB. */
    static char id[NETWORK_INTERFACE_ID_SIZE];
    static char domain[SHELL_DNS_NAME_SIZE];
    static udp_status_t udp_before;
    static dhcp_status_t dhcp_before;
    static dhcp_status_t dhcp_after;
    static dns_status_t dns_before;
    static dns_status_t dns_after;
    static shell_net_qemu_dhcp_check_t check;
    int acquire_result;
    int dns_result;
    int ping_result;
    uint8_t passed;
    uint8_t release_sent = 0;

    if (shell_command_read_two_args(args, id, sizeof(id),
                            domain, sizeof(domain)) != OK) {
        LOG_WARN("SHELL", "Uso invalido da suite DHCP QEMU");
        video_print("Uso: net check qemu dhcp <id> <dominio>\n", 0x0C);
        return;
    }
    if (dhcp_get_status(&dhcp_before) != OK) {
        video_print("Erro: estado DHCP indisponivel.\n", 0x0C);
        return;
    }
    if (dhcp_before.state == DHCP_STATE_BOUND ||
        dhcp_before.state == DHCP_STATE_RENEWING ||
        dhcp_before.state == DHCP_STATE_REBINDING) {
        if (network_manager_release_dhcp(&release_sent) != OK) {
            video_print("Erro: lease anterior nao foi removido.\n", 0x0C);
            return;
        }
    } else if (dhcp_before.state == DHCP_STATE_SELECTING ||
               dhcp_before.state == DHCP_STATE_REQUESTING ||
               dhcp_before.state == DHCP_STATE_APPLYING) {
        if (dhcp_reset() != OK) {
            video_print("Erro: sessao DHCP anterior nao foi reiniciada.\n",
                        0x0C);
            return;
        }
    }
    if (udp_get_status(&udp_before) != OK ||
        dhcp_get_status(&dhcp_before) != OK ||
        dns_get_status(&dns_before) != OK) {
        video_print("Erro: linha-base S2.6 indisponivel.\n", 0x0C);
        return;
    }
    kmemset(&check, 0, sizeof(check));
    video_print("=== Suite de rede - DHCP/DNS QEMU ===\n", 0x0B);
    video_print("Aguardando DHCP, DNS e ICMP...\n", 0x07);
    acquire_result = cmd_net_check_qemu_dhcp_acquire(
        id, &udp_before, &dhcp_before, &check);
    if (acquire_result == OK && dns_clear() == OK &&
        dns_get_status(&dns_before) == OK) {
        dns_result = cmd_net_check_qemu_dns(
            domain, &dns_before, &check);
    } else {
        dns_result = ERR_STATE;
    }
    ping_result = acquire_result == OK ?
        cmd_net_check_qemu_dhcp_ping(&check) : ERR_STATE;
    if (dhcp_get_status(&dhcp_after) == OK &&
        dns_get_status(&dns_after) == OK) {
        check.polling =
            dhcp_after.maintenance_cycles >
                dhcp_before.maintenance_cycles &&
            dns_after.maintenance_cycles >
                dns_before.maintenance_cycles;
    }
    check.invariants = shell_network_validate_for_checks() == OK;
    passed = acquire_result == OK && dns_result == OK &&
        ping_result == OK && check.udp && check.dhcp && check.lease &&
        check.dns && check.dns_cache && check.icmp &&
        check.polling && check.invariants;
    cmd_net_check_qemu_dhcp_print(&check, passed);
    cmd_net_udp_status();
    cmd_net_dhcp_status();
    cmd_net_dns_status();
    cmd_net_dns_table();
    cmd_net_ipv4_status();
}

static uint8_t shell_network_id_equal(const char* first,
                                      const char* second) {
    if (!first || !second) return 0;
    while (*first && *second) {
        char left = *first == ':' ? '-' : *first;
        char right = *second == ':' ? '-' : *second;

        if (left >= 'a' && left <= 'z') left -= (char)('a' - 'A');
        if (right >= 'a' && right <= 'z') right -= (char)('a' - 'A');
        if (left != right) return 0;
        first++;
        second++;
    }
    return *first == '\0' && *second == '\0';
}

static int cmd_net_qemu_tcp_prepare_dhcp(
    const char* id, shell_net_qemu_tcp_check_t* check) {
    dhcp_status_t status;
    uint8_t release_sent = 0;
    int result;

    if (!id || !check) {
        LOG_ERROR("SHELL", "Argumento nulo na preparacao TCP/DHCP");
        return ERR_NULL;
    }
    if (dhcp_get_status(&status) != OK) return ERR_STATE;
    if ((status.state == DHCP_STATE_BOUND ||
         status.state == DHCP_STATE_RENEWING ||
         status.state == DHCP_STATE_REBINDING) &&
        (!shell_network_id_equal(status.interface_id, id) ||
         status.lease.subnet_mask != SHELL_NET_QEMU_SUBNET_MASK ||
         status.lease.gateway != SHELL_NET_QEMU_REPLY_IPV4 ||
         !status.lease.dns_server)) {
        result = network_manager_release_dhcp(&release_sent);
        if (result != OK) return result;
        if (dhcp_get_status(&status) != OK) return ERR_STATE;
    }
    if (status.state != DHCP_STATE_BOUND &&
        status.state != DHCP_STATE_RENEWING &&
        status.state != DHCP_STATE_REBINDING) {
        if (status.state == DHCP_STATE_SELECTING ||
            status.state == DHCP_STATE_REQUESTING ||
            status.state == DHCP_STATE_APPLYING) {
            result = dhcp_reset();
            if (result != OK) return result;
        }
        result = network_manager_acquire_dhcp(id);
        if (result == OK) result = cmd_net_dhcp_wait(&status);
        if (result != OK) return result;
    }
    if (dhcp_get_status(&status) != OK) return ERR_STATE;
    check->dhcp =
        (status.state == DHCP_STATE_BOUND ||
         status.state == DHCP_STATE_RENEWING ||
         status.state == DHCP_STATE_REBINDING) &&
        status.lease.address != 0U &&
        status.lease.subnet_mask == SHELL_NET_QEMU_SUBNET_MASK &&
        status.lease.gateway == SHELL_NET_QEMU_REPLY_IPV4 &&
        status.lease.dns_server != 0U;
    return check->dhcp ? OK : ERR_STATE;
}

static int cmd_net_build_http_url(const char* domain,
                                  char* url,
                                  uint32_t capacity) {
    static const char prefix[] = "http://";
    uint32_t prefix_length = sizeof(prefix) - 1U;
    uint32_t domain_length;

    if (!domain || !url || !capacity) {
        LOG_ERROR("SHELL", "Destino nulo ao montar URL da suite TCP");
        return ERR_NULL;
    }
    domain_length = kstrlen(domain);
    if (!domain_length ||
        prefix_length + domain_length + 2U > capacity) {
        LOG_ERROR("SHELL", "Dominio excede URL da suite TCP");
        return ERR_OVERFLOW;
    }
    kmemcpy(url, prefix, prefix_length);
    kmemcpy(url + prefix_length, domain, domain_length);
    url[prefix_length + domain_length] = '/';
    url[prefix_length + domain_length + 1U] = '\0';
    return OK;
}

static void cmd_net_qemu_tcp_wait_close(uint32_t fin_baseline) {
    uint32_t frequency = timer_get_frequency();
    uint32_t start_tick = timer_get_ticks();
    uint32_t wait_ticks;

    if (!frequency ||
        frequency > SHELL_MAX_TICK_INTERVAL /
                    SHELL_NET_CHECK_WAIT_SECONDS) return;
    wait_ticks = frequency * SHELL_NET_CHECK_WAIT_SECONDS;
    do {
        tcp_status_t status;

        if (tcp_get_status(&status) == OK &&
            status.fin_tx > fin_baseline) return;
        if (shell_network_job_block_tick()) {
            shell_network_job_inner_failed = 1U;
            return;
        }
    } while ((uint32_t)(timer_get_ticks() - start_tick) <= wait_ticks);
    shell_network_job_inner_failed = 1U;
}

static int cmd_net_qemu_tcp_run_http(
    const char* url, http_status_t* out_status) {
    int result = ERR_TIMEOUT;

    if (!url || !out_status) {
        LOG_ERROR("SHELL", "Argumento nulo no HTTP da suite TCP");
        return ERR_NULL;
    }
    for (uint32_t attempt = 0;
         attempt < SHELL_HTTP_SUITE_ATTEMPTS; attempt++) {
        if (attempt) {
            video_print("[WRN] Timeout HTTP; repetindo conexao ", 0x0E);
            shell_command_print_num(attempt + 1U);
            video_print("/", 0x0E);
            shell_command_print_num(SHELL_HTTP_SUITE_ATTEMPTS);
            video_print("...\n", 0x0E);
            result = http_reset();
            if (result != OK) return result;
        }
        result = cmd_http_execute(url, 0U, out_status);
        if (result != ERR_TIMEOUT) return result;
    }
    return result;
}

static void cmd_net_check_qemu_tcp_print(
    const shell_net_qemu_tcp_check_t* check, uint8_t passed) {
    cmd_net_check_print_case("Lease DHCP QEMU", check->dhcp);
    cmd_net_check_print_case("Resolucao DNS A", check->dns);
    cmd_net_check_print_case("Handshake e dados TCP", check->tcp);
    cmd_net_check_print_case("Checksum TCP", check->checksum);
    cmd_net_check_print_case("Socket RX/TX", check->sockets);
    cmd_net_check_print_case("Resposta HTTP suportada", check->http);
    cmd_net_check_print_case("Fechamento TCP", check->closing);
    cmd_net_check_print_case("Polling e manutencao", check->polling);
    cmd_net_check_print_case("Invariantes de rede", check->invariants);
    video_print("Resultado da suite: ", 0x07);
    video_print(passed ? "OK\n" : "ERRO\n",
                passed ? 0x0A : 0x0C);
}

static SHELL_NOINLINE void cmd_net_check_qemu_tcp(const char* args) {
    static char id[NETWORK_INTERFACE_ID_SIZE];
    static char domain[SHELL_DNS_NAME_SIZE];
    static char url[HTTP_URL_BUFFER_SIZE];
    static shell_net_qemu_tcp_check_t check;
    static tcp_status_t tcp_before;
    static tcp_status_t tcp_after;
    static net_socket_status_t sockets_before;
    static net_socket_status_t sockets_after;
    static http_status_t http_before;
    static http_status_t http_after;
    uint32_t address = 0;
    int result;
    uint8_t passed;
    uint8_t metrics_valid = 0;

    if (shell_command_read_two_args(args, id, sizeof(id),
                            domain, sizeof(domain)) != OK) {
        LOG_WARN("SHELL", "Uso invalido da suite TCP QEMU");
        video_print("Uso: net check qemu tcp <id> <dominio>\n", 0x0C);
        return;
    }
    kmemset(&check, 0, sizeof(check));
    kmemset(&tcp_before, 0, sizeof(tcp_before));
    kmemset(&tcp_after, 0, sizeof(tcp_after));
    kmemset(&sockets_before, 0, sizeof(sockets_before));
    kmemset(&sockets_after, 0, sizeof(sockets_after));
    kmemset(&http_before, 0, sizeof(http_before));
    kmemset(&http_after, 0, sizeof(http_after));
    video_print("=== Suite de rede - TCP/HTTP QEMU ===\n", 0x0B);
    video_print("Aguardando DHCP, DNS, TCP e HTTP...\n", 0x07);
    result = cmd_net_qemu_tcp_prepare_dhcp(id, &check);
    if (result == OK) {
        result = cmd_dns_wait(domain, &address);
        check.dns = result == OK && ipv4_address_is_unicast(address);
    }
    if (result == OK) result = cmd_net_build_http_url(
        domain, url, sizeof(url));
    if (result == OK) result = http_reset();
    if (result == OK &&
        (tcp_get_status(&tcp_before) != OK ||
         net_socket_get_status(&sockets_before) != OK ||
         http_get_status(&http_before) != OK)) {
        result = ERR_STATE;
    } else if (result == OK) {
        metrics_valid = 1;
    }
    if (result == OK) {
        result = cmd_net_qemu_tcp_run_http(url, &http_after);
        if (result == OK) {
            cmd_net_qemu_tcp_wait_close(tcp_before.fin_tx);
        }
    }
    if (tcp_get_status(&tcp_after) != OK ||
        net_socket_get_status(&sockets_after) != OK ||
        http_get_status(&http_after) != OK) {
        result = ERR_STATE;
        metrics_valid = 0;
        kmemset(&tcp_after, 0, sizeof(tcp_after));
        kmemset(&sockets_after, 0, sizeof(sockets_after));
        kmemset(&http_after, 0, sizeof(http_after));
    }
    check.tcp = metrics_valid &&
        tcp_after.syn_tx > tcp_before.syn_tx &&
        tcp_after.syn_ack_rx > tcp_before.syn_ack_rx &&
        tcp_after.segments_rx > tcp_before.segments_rx &&
        tcp_after.segments_tx > tcp_before.segments_tx;
    check.checksum = metrics_valid &&
        tcp_after.rx_checksum_errors == tcp_before.rx_checksum_errors;
    check.sockets = metrics_valid &&
        sockets_after.connects > sockets_before.connects &&
        sockets_after.bytes_sent_tcp > sockets_before.bytes_sent_tcp &&
        sockets_after.bytes_received_tcp >
            sockets_before.bytes_received_tcp;
    check.http = result == OK &&
        http_after.state == HTTP_STATE_COMPLETE &&
        http_after.resolved_ip == address &&
        http_after.status_code >= HTTP_STATUS_MINIMUM &&
        http_after.status_code <= HTTP_STATUS_MAXIMUM &&
        http_after.headers_length != 0U &&
        http_after.requests_tx > http_before.requests_tx &&
        http_after.responses_rx > http_before.responses_rx;
    check.closing = metrics_valid &&
        tcp_after.fin_tx > tcp_before.fin_tx;
    check.polling = metrics_valid &&
        tcp_after.maintenance_cycles > tcp_before.maintenance_cycles &&
        sockets_after.maintenance_cycles >
            sockets_before.maintenance_cycles &&
        http_after.maintenance_cycles >
            http_before.maintenance_cycles;
    check.invariants = shell_network_validate_for_checks() == OK;
    passed = check.dhcp && check.dns && check.tcp &&
        check.checksum && check.sockets && check.http &&
        check.closing && check.polling && check.invariants;
    cmd_net_check_qemu_tcp_print(&check, passed);
    cmd_net_tcp_status();
    cmd_net_socket_status();
    cmd_net_socket_table();
    cmd_http_status();
}

static SHELL_NOINLINE void cmd_net_check_qemu_static(const char* args) {
    static char id[NETWORK_INTERFACE_ID_SIZE];
    static char ip_text[SHELL_IPV4_TEXT_SIZE];
    static arp_status_t baseline;
    static arp_status_t final_status;
    static ipv4_status_t ipv4_baseline;
    static icmp_status_t icmp_baseline;
    static shell_net_qemu_check_t check;
    uint32_t local_ip = 0;
    uint8_t passed;
    int reply_result;
    int timeout_result;
    int icmp_result;
    int result;
    result = shell_command_read_two_args(args, id, sizeof(id),
                                 ip_text, sizeof(ip_text));
    if (result != OK || cmd_net_parse_ipv4(ip_text, &local_ip) != OK ||
        !arp_ipv4_is_valid(local_ip) ||
        local_ip == SHELL_NET_QEMU_REPLY_IPV4 ||
        local_ip == SHELL_NET_QEMU_TIMEOUT_IPV4) {
        LOG_WARN("SHELL", "Uso invalido de net check qemu");
        video_print("Uso: net check qemu <id> <ip-local> | "
                    "net check qemu dhcp <id> <dominio> | "
                    "net check qemu tcp <id> <dominio> | "
                    "net check qemu multi <id-a> <id-b>\n", 0x0C);
        return;
    }
    result = network_manager_configure_ipv4(
        id, local_ip, SHELL_NET_QEMU_SUBNET_MASK,
        SHELL_NET_QEMU_REPLY_IPV4);
    if (result != OK || arp_clear() != OK ||
        arp_get_status(&baseline) != OK ||
        ipv4_get_status(&ipv4_baseline) != OK ||
        icmp_get_status(&icmp_baseline) != OK) {
        LOG_ERROR("SHELL", "Preparacao da suite de rede QEMU falhou");
        video_print("Erro: nao foi possivel preparar a suite QEMU.\n", 0x0C);
        return;
    }
    kmemset(&check, 0, sizeof(check));
    video_print("=== Suite de rede - perfil QEMU ===\n", 0x0B);
    video_print("Aguardando ARP, IPv4, ICMP e timeout...\n", 0x07);
    reply_result = cmd_net_check_qemu_reply(&baseline, &check);
    timeout_result = cmd_net_check_qemu_timeout(&baseline, &check);
    icmp_result = cmd_net_check_qemu_icmp(
        &ipv4_baseline, &icmp_baseline, &check);
    result = arp_get_status(&final_status);
    if (result != OK) {
        LOG_ERROR("SHELL", "Estado final da suite ARP indisponivel");
        video_print("Erro: resultado final ARP indisponivel.\n", 0x0C);
        return;
    }
    check.polling =
        final_status.maintenance_cycles > baseline.maintenance_cycles &&
        final_status.maintenance_errors == baseline.maintenance_errors;
    check.invariants = shell_network_validate_for_checks() == OK;
    cmd_net_check_print_case("Request/reply 10.0.2.2", check.reply);
    cmd_net_check_print_case("Cache hit sem novo TX", check.cache_hit);
    cmd_net_check_print_case("Timeout 10.0.2.254", check.timeout);
    cmd_net_check_print_case("IPv4 RX/TX e checksum", check.ipv4);
    cmd_net_check_print_case("ICMP Echo e RTT", check.icmp);
    cmd_net_check_print_case("Polling e manutencao", check.polling);
    cmd_net_check_print_case("Invariantes de rede", check.invariants);
    passed = reply_result == OK && timeout_result == OK &&
        icmp_result == OK &&
        check.reply && check.cache_hit && check.timeout && check.polling &&
        check.ipv4 && check.icmp && check.invariants;
    video_print("Resultado da suite: ", 0x07);
    video_print(passed ? "OK\n" : "ERRO\n", passed ? 0x0A : 0x0C);
    cmd_net_arp_status();
    cmd_net_arp_table();
    cmd_net_ipv4_status();
}

static SHELL_NOINLINE void cmd_net_check_qemu_multi(
    const char* args) {
    static char first_id[NETWORK_INTERFACE_ID_SIZE];
    static char second_id[NETWORK_INTERFACE_ID_SIZE];
    static network_interface_info_t first_info;
    static network_interface_info_t second_info;
    static network_interface_text_t first_text;
    static network_interface_text_t second_text;
    static ethernet_interface_status_t first_before;
    static ethernet_interface_status_t first_middle;
    static ethernet_interface_status_t first_after;
    static ethernet_interface_status_t second_before;
    static ethernet_interface_status_t second_middle;
    static ethernet_interface_status_t second_after;
    uint8_t first_isolated;
    uint8_t second_isolated;
    uint8_t invariants;
    int result;

    result = shell_command_read_two_args(
        args, first_id, sizeof(first_id),
        second_id, sizeof(second_id));
    if (result != OK ||
        network_manager_find(first_id, &first_info) != OK ||
        network_manager_find(second_id, &second_info) != OK ||
        network_manager_format_text(&first_info, &first_text) != OK ||
        network_manager_format_text(&second_info, &second_text) != OK ||
        first_info.state != NETWORK_INTERFACE_ACTIVE ||
        second_info.state != NETWORK_INTERFACE_ACTIVE ||
        kstrcmp(first_text.id, second_text.id) == 0) {
        LOG_WARN("SHELL", "Uso invalido da suite Multi-NIC QEMU");
        video_print(
            "Uso: net check qemu multi <id-a> <id-b>\n", 0x0C);
        return;
    }
    if (ethernet_get_interface_status(
            first_text.id, &first_before) != OK ||
        ethernet_get_interface_status(
            second_text.id, &second_before) != OK ||
        network_manager_send_diagnostic(first_text.id) != OK ||
        ethernet_get_interface_status(
            first_text.id, &first_middle) != OK ||
        ethernet_get_interface_status(
            second_text.id, &second_middle) != OK) {
        LOG_ERROR("SHELL", "Primeiro TX da suite Multi-NIC falhou");
        video_print("Erro: primeiro TX Multi-NIC falhou.\n", 0x0C);
        return;
    }
    first_isolated =
        first_middle.tx_frames == first_before.tx_frames + 1U &&
        first_middle.driver.tx_packets ==
            first_before.driver.tx_packets + 1U &&
        second_middle.tx_frames == second_before.tx_frames &&
        second_middle.driver.tx_packets ==
            second_before.driver.tx_packets;
    result = network_manager_send_diagnostic(second_text.id);
    if (result != OK ||
        ethernet_get_interface_status(
            first_text.id, &first_after) != OK ||
        ethernet_get_interface_status(
            second_text.id, &second_after) != OK) {
        LOG_ERROR("SHELL", "Segundo TX da suite Multi-NIC falhou");
        video_print("Erro: segundo TX Multi-NIC falhou.\n", 0x0C);
        return;
    }
    second_isolated =
        first_after.tx_frames == first_middle.tx_frames &&
        first_after.driver.tx_packets ==
            first_middle.driver.tx_packets &&
        second_after.tx_frames == second_middle.tx_frames + 1U &&
        second_after.driver.tx_packets ==
            second_middle.driver.tx_packets + 1U;
    invariants = shell_network_validate_for_checks() == OK;
    video_print("=== Suite de rede - Multi-NIC QEMU ===\n", 0x0B);
    cmd_net_check_print_case("TX isolado na primeira NIC",
                             first_isolated);
    cmd_net_check_print_case("TX isolado na segunda NIC",
                             second_isolated);
    cmd_net_check_print_case("Invariantes Multi-NIC", invariants);
    video_print("Resultado da suite: ", 0x07);
    video_print(first_isolated && second_isolated && invariants ?
                "OK\n" : "ERRO\n",
                first_isolated && second_isolated && invariants ?
                0x0A : 0x0C);
}

static SHELL_NOINLINE void cmd_net_check_qemu(const char* args) {
    const char* tcp_args = shell_command_match_subcommand(args, "tcp");
    const char* dhcp_args;
    const char* multi_args;

    multi_args = shell_command_match_subcommand(args, "multi");
    if (multi_args) {
        cmd_net_check_qemu_multi(multi_args);
        return;
    }
    if (tcp_args) {
        cmd_net_check_qemu_tcp(tcp_args);
        return;
    }
    dhcp_args = shell_command_match_subcommand(args, "dhcp");
    if (dhcp_args) {
        cmd_net_check_qemu_dhcp(dhcp_args);
        return;
    }
    cmd_net_check_qemu_static(args);
}

static void cmd_net_check(const char* args) {
    char id[NETWORK_INTERFACE_ID_SIZE];
    network_interface_info_t info;
    network_manager_status_t network_status;
    arp_status_t arp_status;
    const char* qemu_args;
    uint8_t has_interface = 0;
    int result;

    if (!args) {
        LOG_ERROR("SHELL", "Argumentos nulos em net check");
        video_print("Erro: diagnostico agrupado invalido.\n", 0x0C);
        return;
    }
    qemu_args = shell_command_match_subcommand(args, "qemu");
    if (qemu_args) {
        cmd_net_check_qemu(qemu_args);
        return;
    }
    if (*args) {
        result = shell_command_read_single_arg(args, id, sizeof(id));
        if (result != OK) {
            LOG_WARN("SHELL", "Uso invalido de net check");
            video_print("Uso: net check [id] | "
                        "net check qemu <id> <ip-local> | "
                        "net check qemu dhcp <id> <dominio> | "
                        "net check qemu tcp <id> <dominio> | "
                        "net check qemu multi <id-a> <id-b>\n", 0x0C);
            return;
        }
        result = network_manager_find(id, &info);
        if (result != OK) {
            LOG_WARN("SHELL", "Interface invalida em net check");
            video_print("Erro: interface de rede nao encontrada.\n", 0x0C);
            return;
        }
        has_interface = 1;
    }
    if (network_manager_get_status(&network_status) != OK) {
        LOG_ERROR("SHELL", "Estado Network indisponivel em net check");
        video_print("Erro: estado de rede indisponivel.\n", 0x0C);
        return;
    }

    video_begin_update();
    video_print("=== Diagnostico de rede agrupado ===\n", 0x0B);
    video_print("\n[1] Estado geral\n", 0x0B);
    cmd_net_status();
    video_end_update();

    video_begin_update();
    video_print("\n[2] Controladores\n", 0x0B);
    cmd_net_devices();
    video_end_update();

    if (has_interface) {
        video_begin_update();
        video_print("\n[3] Interface\n", 0x0B);
        cmd_net_info(id);
        video_end_update();

        video_begin_update();
        video_print("\n[4] Ethernet\n", 0x0B);
        if (info.state == NETWORK_INTERFACE_ACTIVE &&
            network_status.ethernet_available) {
            cmd_net_ethernet(id);
        } else {
            video_print("Diagnostico Ethernet nao aplicavel.\n", 0x0E);
        }
        video_end_update();
    }

    video_begin_update();
    video_print(has_interface ? "\n[5] ARP\n" : "\n[3] ARP\n", 0x0B);
    cmd_net_arp_status();
    if (arp_get_status(&arp_status) == OK && arp_status.initialized) {
        cmd_net_arp_table();
    }
    video_end_update();

    video_begin_update();
    video_print(has_interface ? "\n[6] IPv4 e ICMP\n" :
                                "\n[4] IPv4 e ICMP\n", 0x0B);
    cmd_net_ipv4_status();
    video_end_update();

    video_begin_update();
    video_print(has_interface ? "\n[7] UDP\n" : "\n[5] UDP\n", 0x0B);
    cmd_net_udp_status();
    video_end_update();

    video_begin_update();
    video_print(has_interface ? "\n[8] DHCP\n" : "\n[6] DHCP\n", 0x0B);
    cmd_net_dhcp_status();
    video_end_update();

    video_begin_update();
    video_print(has_interface ? "\n[9] DNS\n" : "\n[7] DNS\n", 0x0B);
    cmd_net_dns_status();
    cmd_net_dns_table();
    video_end_update();

    video_begin_update();
    video_print(has_interface ? "\n[10] TCP\n" : "\n[8] TCP\n", 0x0B);
    cmd_net_tcp_status();
    video_end_update();

    video_begin_update();
    video_print(has_interface ? "\n[11] Sockets\n" :
                                "\n[9] Sockets\n", 0x0B);
    cmd_net_socket_status();
    cmd_net_socket_table();
    video_end_update();

    video_begin_update();
    video_print(has_interface ? "\n[12] HTTP\n" :
                                "\n[10] HTTP\n", 0x0B);
    cmd_http_status();
    video_end_update();

    video_begin_update();
    video_print(has_interface ? "\n[13] Invariantes: " :
                                "\n[11] Invariantes: ", 0x0B);
    result = shell_network_validate_for_checks();
    video_print(result == OK ? "OK\n" : "ERRO\n",
                result == OK ? 0x0A : 0x0C);
    video_end_update();
}

static void cmd_net(const char* args) {
    const char* arp_args;
    const char* check_args;
    const char* dhcp_args;
    const char* dns_args;
    const char* ethernet_args;
    const char* info_args;
    const char* ipv4_args;
    const char* socket_args;
    const char* tcp_args;
    const char* test_args;
    const char* udp_args;

    if (shell_command_args_equal(args, "status")) {
        cmd_net_status();
        return;
    }
    if (shell_command_args_equal(args, "devices")) {
        cmd_net_devices();
        return;
    }
    arp_args = shell_command_match_subcommand(args, "arp");
    if (arp_args) {
        cmd_net_arp(arp_args);
        return;
    }
    ipv4_args = shell_command_match_subcommand(args, "ipv4");
    if (ipv4_args) {
        cmd_net_ipv4(ipv4_args);
        return;
    }
    udp_args = shell_command_match_subcommand(args, "udp");
    if (udp_args) {
        cmd_net_udp(udp_args);
        return;
    }
    dhcp_args = shell_command_match_subcommand(args, "dhcp");
    if (dhcp_args) {
        cmd_net_dhcp(dhcp_args);
        return;
    }
    dns_args = shell_command_match_subcommand(args, "dns");
    if (dns_args) {
        cmd_net_dns(dns_args);
        return;
    }
    tcp_args = shell_command_match_subcommand(args, "tcp");
    if (tcp_args) {
        cmd_net_tcp(tcp_args);
        return;
    }
    socket_args = shell_command_match_subcommand(args, "socket");
    if (socket_args) {
        cmd_net_socket(socket_args);
        return;
    }
    check_args = shell_command_match_subcommand(args, "check");
    if (check_args) {
        cmd_net_check(check_args);
        return;
    }
    info_args = shell_command_match_subcommand(args, "info");
    if (info_args) {
        cmd_net_info(info_args);
        return;
    }
    ethernet_args = shell_command_match_subcommand(args, "ethernet");
    if (ethernet_args) {
        cmd_net_ethernet(ethernet_args);
        return;
    }
    test_args = shell_command_match_subcommand(args, "test");
    if (test_args) {
        cmd_net_test(test_args);
        return;
    }
    LOG_WARN("SHELL", "Uso invalido de net");
    video_print("Uso: net status | net devices | net info <id> | "
                "net ethernet <id> | net test <id> | net arp ... | "
                "net ipv4 ... | net udp ... | net dhcp ... | "
                "net dns ... | net tcp ... | net socket ... | "
                "net check ...\n", 0x0C);
}

typedef enum {
    SHELL_NETWORK_JOB_NONE = 0,
    SHELL_NETWORK_JOB_DNS,
    SHELL_NETWORK_JOB_PING,
    SHELL_NETWORK_JOB_HTTP,
    SHELL_NETWORK_JOB_DHCP,
    SHELL_NETWORK_JOB_CHECK
} shell_network_job_operation_t;

typedef enum {
    SHELL_NETWORK_JOB_PHASE_RESOLVE = 0,
    SHELL_NETWORK_JOB_PHASE_TRANSFER,
    SHELL_NETWORK_JOB_PHASE_COMPLETE
} shell_network_job_phase_t;

typedef enum {
    SHELL_NETWORK_CHECK_MODE_COMMAND = 0,
    SHELL_NETWORK_CHECK_MODE_REPORT,
    SHELL_NETWORK_CHECK_MODE_QEMU
} shell_network_check_mode_t;

typedef enum {
    SHELL_NETWORK_CHECK_STAGE_GENERAL = 0,
    SHELL_NETWORK_CHECK_STAGE_CONTROLLERS,
    SHELL_NETWORK_CHECK_STAGE_INTERFACE,
    SHELL_NETWORK_CHECK_STAGE_ETHERNET,
    SHELL_NETWORK_CHECK_STAGE_ARP,
    SHELL_NETWORK_CHECK_STAGE_IPV4_ICMP,
    SHELL_NETWORK_CHECK_STAGE_UDP,
    SHELL_NETWORK_CHECK_STAGE_DHCP,
    SHELL_NETWORK_CHECK_STAGE_DNS,
    SHELL_NETWORK_CHECK_STAGE_TCP,
    SHELL_NETWORK_CHECK_STAGE_SOCKETS,
    SHELL_NETWORK_CHECK_STAGE_HTTP,
    SHELL_NETWORK_CHECK_STAGE_INVARIANTS,
    SHELL_NETWORK_CHECK_STAGE_DONE
} shell_network_check_stage_t;

typedef struct {
    shell_network_job_operation_t operation;
    shell_network_job_phase_t phase;
    shell_network_check_mode_t check_mode;
    shell_network_check_stage_t check_stage;
    char target[SHELL_DNS_NAME_SIZE];
    char url[HTTP_URL_BUFFER_SIZE];
    char interface_id[NETWORK_INTERFACE_ID_SIZE];
    uint32_t target_ip;
    uint8_t count;
    uint8_t dhcp_renew;
    net_socket_handle_t socket;
    uint16_t port;
    uint32_t event_generation;
    uint32_t job_generation;
    uint32_t check_progress;
    uint32_t check_total;
    uint8_t check_has_interface;
} shell_network_job_t;

static shell_network_job_t shell_network_job;

static int shell_network_job_timeout(shell_job_context_t* context,
                                     uint32_t seconds) {
    uint32_t frequency = timer_get_frequency();
    uint32_t wait_ticks;

    if (!frequency ||
        frequency > SHELL_MAX_TICK_INTERVAL / seconds) {
        context->last_error = ERR_STATE;
        LOG_ERROR("SHELL", "Timer invalido em job de rede");
        return 1;
    }
    wait_ticks = frequency * seconds;
    shell_job_set_deadline(context, context->started_tick + wait_ticks);
    if ((uint32_t)(timer_get_ticks() - context->started_tick) > wait_ticks) {
        context->last_error = ERR_TIMEOUT;
        LOG_WARN("SHELL", "Timeout em job cooperativo de rede");
        return 1;
    }
    return 0;
}

static void shell_network_job_print_error(const char* operation, int result) {
    video_print("Erro: ", 0x0C);
    video_print(operation, 0x0C);
    video_print(" falhou (codigo ", 0x0C);
    shell_command_print_num((uint32_t)result);
    video_print(").\n", 0x0C);
}

static void shell_network_job_print_ping_header(void) {
    video_print("PING ", 0x0B);
    video_print(shell_network_job.target, 0x0B);
    video_print(" [", 0x07);
    cmd_net_print_ipv4(shell_network_job.target_ip);
    video_print("] com 32 bytes de dados:\n", 0x07);
}

static int shell_network_job_check_stage_is_optional(
    shell_network_check_stage_t stage) {
    return stage == SHELL_NETWORK_CHECK_STAGE_INTERFACE ||
           stage == SHELL_NETWORK_CHECK_STAGE_ETHERNET;
}

static const char* shell_network_job_check_stage_name(
    shell_network_check_stage_t stage) {
    switch (stage) {
        case SHELL_NETWORK_CHECK_STAGE_GENERAL: return "estado";
        case SHELL_NETWORK_CHECK_STAGE_CONTROLLERS: return "controladores";
        case SHELL_NETWORK_CHECK_STAGE_INTERFACE: return "interface";
        case SHELL_NETWORK_CHECK_STAGE_ETHERNET: return "ethernet";
        case SHELL_NETWORK_CHECK_STAGE_ARP: return "arp";
        case SHELL_NETWORK_CHECK_STAGE_IPV4_ICMP: return "ipv4-icmp";
        case SHELL_NETWORK_CHECK_STAGE_UDP: return "udp";
        case SHELL_NETWORK_CHECK_STAGE_DHCP: return "dhcp";
        case SHELL_NETWORK_CHECK_STAGE_DNS: return "dns";
        case SHELL_NETWORK_CHECK_STAGE_TCP: return "tcp";
        case SHELL_NETWORK_CHECK_STAGE_SOCKETS: return "sockets";
        case SHELL_NETWORK_CHECK_STAGE_HTTP: return "http";
        case SHELL_NETWORK_CHECK_STAGE_INVARIANTS: return "invariantes";
        default: return "concluindo";
    }
}

static int shell_network_job_check_emit_stage(
    shell_job_context_t* context) {
    arp_status_t arp_status;
    network_interface_info_t info;
    network_manager_status_t network_status;
    int result = OK;

    if (!context) return ERR_NULL;
    shell_job_set_phase(context, shell_network_job_check_stage_name(
                                      shell_network_job.check_stage));
    video_begin_update();
    switch (shell_network_job.check_stage) {
        case SHELL_NETWORK_CHECK_STAGE_GENERAL:
            video_print("=== Diagnostico de rede agrupado ===\n", 0x0B);
            video_print("\n[1] Estado geral\n", 0x0B);
            cmd_net_status();
            break;
        case SHELL_NETWORK_CHECK_STAGE_CONTROLLERS:
            video_print("\n[2] Controladores\n", 0x0B);
            cmd_net_devices();
            break;
        case SHELL_NETWORK_CHECK_STAGE_INTERFACE:
            video_print("\n[3] Interface\n", 0x0B);
            cmd_net_info(shell_network_job.interface_id);
            break;
        case SHELL_NETWORK_CHECK_STAGE_ETHERNET:
            video_print("\n[4] Ethernet\n", 0x0B);
            if (network_manager_find(shell_network_job.interface_id, &info) != OK ||
                network_manager_get_status(&network_status) != OK) {
                LOG_ERROR("SHELL", "Estado Ethernet indisponivel em net check");
                video_print("Erro: estado Ethernet indisponivel.\n", 0x0C);
                result = ERR_STATE;
            } else if (info.state == NETWORK_INTERFACE_ACTIVE &&
                       network_status.ethernet_available) {
                cmd_net_ethernet(shell_network_job.interface_id);
            } else {
                video_print("Diagnostico Ethernet nao aplicavel.\n", 0x0E);
            }
            break;
        case SHELL_NETWORK_CHECK_STAGE_ARP:
            video_print(shell_network_job.check_has_interface ? "\n[5] ARP\n" :
                        "\n[3] ARP\n", 0x0B);
            cmd_net_arp_status();
            if (arp_get_status(&arp_status) == OK && arp_status.initialized) {
                cmd_net_arp_table();
            }
            break;
        case SHELL_NETWORK_CHECK_STAGE_IPV4_ICMP:
            video_print(shell_network_job.check_has_interface ?
                        "\n[6] IPv4 e ICMP\n" : "\n[4] IPv4 e ICMP\n", 0x0B);
            cmd_net_ipv4_status();
            break;
        case SHELL_NETWORK_CHECK_STAGE_UDP:
            video_print(shell_network_job.check_has_interface ? "\n[7] UDP\n" :
                        "\n[5] UDP\n", 0x0B);
            cmd_net_udp_status();
            break;
        case SHELL_NETWORK_CHECK_STAGE_DHCP:
            video_print(shell_network_job.check_has_interface ? "\n[8] DHCP\n" :
                        "\n[6] DHCP\n", 0x0B);
            cmd_net_dhcp_status();
            break;
        case SHELL_NETWORK_CHECK_STAGE_DNS:
            video_print(shell_network_job.check_has_interface ? "\n[9] DNS\n" :
                        "\n[7] DNS\n", 0x0B);
            cmd_net_dns_status();
            cmd_net_dns_table();
            break;
        case SHELL_NETWORK_CHECK_STAGE_TCP:
            video_print(shell_network_job.check_has_interface ? "\n[10] TCP\n" :
                        "\n[8] TCP\n", 0x0B);
            cmd_net_tcp_status();
            break;
        case SHELL_NETWORK_CHECK_STAGE_SOCKETS:
            video_print(shell_network_job.check_has_interface ?
                        "\n[11] Sockets\n" : "\n[9] Sockets\n", 0x0B);
            cmd_net_socket_status();
            cmd_net_socket_table();
            break;
        case SHELL_NETWORK_CHECK_STAGE_HTTP:
            video_print(shell_network_job.check_has_interface ? "\n[12] HTTP\n" :
                        "\n[10] HTTP\n", 0x0B);
            cmd_http_status();
            break;
        case SHELL_NETWORK_CHECK_STAGE_INVARIANTS:
            video_print(shell_network_job.check_has_interface ?
                        "\n[13] Invariantes: " : "\n[11] Invariantes: ",
                        0x0B);
            result = shell_network_validate_for_checks();
            video_print(result == OK ? "OK\n" : "ERRO\n",
                        result == OK ? 0x0A : 0x0C);
            break;
        default:
            result = ERR_STATE;
            break;
    }
    video_end_update();
    return result;
}

static shell_job_step_result_t shell_network_job_check_report_step(
    shell_job_context_t* context) {
    int result;

    if (!context) return SHELL_JOB_STEP_FAILED;
    if (context->cancel_requested) {
        context->last_error = ERR_TIMEOUT;
        return SHELL_JOB_STEP_CANCELLED;
    }
    while (shell_network_job.check_stage < SHELL_NETWORK_CHECK_STAGE_DONE &&
           !shell_network_job.check_has_interface &&
           shell_network_job_check_stage_is_optional(
               shell_network_job.check_stage)) {
        shell_network_job.check_stage++;
    }
    if (shell_network_job.check_stage >= SHELL_NETWORK_CHECK_STAGE_DONE) {
        context->last_error = OK;
        return SHELL_JOB_STEP_COMPLETE;
    }

    result = shell_network_job_check_emit_stage(context);
    shell_network_job.check_stage++;
    shell_network_job.check_progress++;
    shell_job_set_progress(context, shell_network_job.check_progress,
                           shell_network_job.check_total);
    if (result != OK) {
        context->last_error = result;
        return SHELL_JOB_STEP_FAILED;
    }
    if (shell_network_job.check_stage >= SHELL_NETWORK_CHECK_STAGE_DONE) {
        context->last_error = OK;
        return SHELL_JOB_STEP_COMPLETE;
    }
    /* Cada secao ja foi apresentada; a proxima rodada e agendada sem
       transformar o prazo do job em timeout nem bloquear um tick fixo. */
    shell_job_set_next_wake(context, timer_get_ticks());
    return SHELL_JOB_STEP_PENDING;
}

static shell_job_step_result_t shell_network_job_step(
    shell_job_context_t* context) {
    /* O processo nativo possui uma pilha de 4 KiB. Esses snapshots sao
       temporarios e nao podem competir com a pilha profunda dos diagnosticos. */
    static dns_status_t dns_status;
    static icmp_status_t icmp_status;
    static http_status_t http_status;
    static dhcp_status_t dhcp_status;
    int result;

    if (!context) return SHELL_JOB_STEP_FAILED;
    if (!shell_job_generation_matches(shell_network_job.job_generation)) {
        context->stale_events++;
        context->last_error = ERR_STATE;
        LOG_WARN("SHELL", "Resultado de rede pertence a geracao antiga");
        return SHELL_JOB_STEP_FAILED;
    }
    if (shell_network_job.operation == SHELL_NETWORK_JOB_CHECK) {
        if (shell_network_job.check_mode == SHELL_NETWORK_CHECK_MODE_REPORT) {
            return shell_network_job_check_report_step(context);
        }
        if (context->cancel_requested) {
            context->last_error = ERR_TIMEOUT;
            return SHELL_JOB_STEP_CANCELLED;
        }
        shell_job_set_phase(context, "diagnostico");
        cmd_net(context->arguments);
        if (context->cancel_requested) {
            context->last_error = ERR_TIMEOUT;
            return SHELL_JOB_STEP_CANCELLED;
        }
        if (shell_network_job_inner_failed) {
            context->last_error = ERR_TIMEOUT;
            return SHELL_JOB_STEP_FAILED;
        }
        context->last_error = OK;
        return SHELL_JOB_STEP_COMPLETE;
    }
    if (shell_network_job.operation == SHELL_NETWORK_JOB_DNS) {
        if (dns_get_status(&dns_status) != OK) {
            context->last_error = ERR_STATE;
            return SHELL_JOB_STEP_FAILED;
        }
        shell_job_set_phase(context, dns_state_name(dns_status.state));
        shell_job_set_progress(context, dns_status.replies_rx,
                               dns_status.queries_tx);
        if (dns_status.state == DNS_STATE_COMPLETE) {
            shell_network_job.target_ip = dns_status.result_ip;
            context->last_error = OK;
            return SHELL_JOB_STEP_COMPLETE;
        }
        if (dns_status.state == DNS_STATE_FAILED) {
            context->last_error = dns_status.last_error == OK ? ERR_STATE :
                                                               dns_status.last_error;
            return SHELL_JOB_STEP_FAILED;
        }
        if (shell_network_job_timeout(context, SHELL_DNS_WAIT_SECONDS)) {
            dns_reset();
            return SHELL_JOB_STEP_FAILED;
        }
        return SHELL_JOB_STEP_PENDING;
    }

    if (shell_network_job.operation == SHELL_NETWORK_JOB_PING) {
        if (shell_network_job.phase == SHELL_NETWORK_JOB_PHASE_RESOLVE) {
            if (dns_get_status(&dns_status) != OK) {
                context->last_error = ERR_STATE;
                return SHELL_JOB_STEP_FAILED;
            }
            shell_job_set_phase(context, dns_state_name(dns_status.state));
            if (dns_status.state == DNS_STATE_COMPLETE) {
                shell_network_job.target_ip = dns_status.result_ip;
                result = icmp_ping_start(shell_network_job.target_ip,
                                          shell_network_job.count,
                                          ICMP_PING_TIMEOUT_SECONDS);
                if (result != OK) {
                    context->last_error = result;
                    return SHELL_JOB_STEP_FAILED;
                }
                shell_network_job.phase = SHELL_NETWORK_JOB_PHASE_TRANSFER;
                shell_network_job.event_generation = 0U;
                shell_network_job_print_ping_header();
                return SHELL_JOB_STEP_PENDING;
            }
            if (dns_status.state == DNS_STATE_FAILED) {
                context->last_error = dns_status.last_error == OK ? ERR_STATE :
                                                                   dns_status.last_error;
                return SHELL_JOB_STEP_FAILED;
            }
            if (shell_network_job_timeout(context, SHELL_DNS_WAIT_SECONDS)) {
                dns_reset();
                return SHELL_JOB_STEP_FAILED;
            }
            return SHELL_JOB_STEP_PENDING;
        }

        if (icmp_get_status(&icmp_status) != OK) {
            context->last_error = ERR_STATE;
            return SHELL_JOB_STEP_FAILED;
        }
        shell_job_set_phase(context, icmp_ping_state_name(icmp_status.state));
        shell_job_set_progress(context, icmp_status.received +
                               icmp_status.timeouts,
                               icmp_status.requested_count);
        if (icmp_status.event_generation != shell_network_job.event_generation) {
            shell_network_job.event_generation = icmp_status.event_generation;
            cmd_ping_print_event(&icmp_status);
        }
        if (icmp_status.state == ICMP_PING_COMPLETE) {
            context->last_error = OK;
            return SHELL_JOB_STEP_COMPLETE;
        }
        if (icmp_status.state == ICMP_PING_FAILED) {
            context->last_error = icmp_status.last_error == OK ? ERR_STATE :
                                                                  icmp_status.last_error;
            return SHELL_JOB_STEP_FAILED;
        }
        if (shell_network_job_timeout(context,
                                      shell_network_job.count +
                                      SHELL_PING_WAIT_EXTRA_SECONDS)) {
            icmp_reset();
            return SHELL_JOB_STEP_FAILED;
        }
        return SHELL_JOB_STEP_PENDING;
    }

    if (shell_network_job.operation == SHELL_NETWORK_JOB_HTTP) {
        if (http_get_status(&http_status) != OK) {
            context->last_error = ERR_STATE;
            return SHELL_JOB_STEP_FAILED;
        }
        shell_job_set_phase(context, http_state_name(http_status.state));
        shell_job_set_progress(context, http_status.body_length,
                               http_status.content_length);
        if (http_status.state == HTTP_STATE_COMPLETE) {
            context->last_error = OK;
            return SHELL_JOB_STEP_COMPLETE;
        }
        if (http_status.state == HTTP_STATE_FAILED) {
            context->last_error = http_status.last_error == OK ? ERR_STATE :
                                                                  http_status.last_error;
            return SHELL_JOB_STEP_FAILED;
        }
        if (shell_network_job_timeout(context, SHELL_HTTP_WAIT_SECONDS)) {
            http_reset();
            return SHELL_JOB_STEP_FAILED;
        }
        return SHELL_JOB_STEP_PENDING;
    }

    if (shell_network_job.operation == SHELL_NETWORK_JOB_DHCP) {
        if (dhcp_get_status(&dhcp_status) != OK) {
            context->last_error = ERR_STATE;
            return SHELL_JOB_STEP_FAILED;
        }
        shell_job_set_phase(context, dhcp_state_name(dhcp_status.state));
        shell_job_set_progress(context, dhcp_status.acks_rx,
                               dhcp_status.discovers_tx);
        if (dhcp_status.state == DHCP_STATE_BOUND) {
            context->last_error = OK;
            return SHELL_JOB_STEP_COMPLETE;
        }
        if (dhcp_status.state == DHCP_STATE_FAILED ||
            dhcp_status.state == DHCP_STATE_EXPIRED) {
            context->last_error = dhcp_status.last_error == OK ? ERR_STATE :
                                                                  dhcp_status.last_error;
            return SHELL_JOB_STEP_FAILED;
        }
        if (shell_network_job_timeout(context, SHELL_DHCP_WAIT_SECONDS)) {
            dhcp_reset();
            return SHELL_JOB_STEP_FAILED;
        }
        return SHELL_JOB_STEP_PENDING;
    }

    context->last_error = ERR_STATE;
    return SHELL_JOB_STEP_FAILED;
}

static int shell_network_job_cancel(shell_job_context_t* context) {
    int result = OK;

    (void)context;
    if (shell_network_job.operation == SHELL_NETWORK_JOB_DNS) {
        result = dns_reset();
    } else if (shell_network_job.operation == SHELL_NETWORK_JOB_PING) {
        result = icmp_reset();
        dns_reset();
    } else if (shell_network_job.operation == SHELL_NETWORK_JOB_HTTP) {
        result = http_reset();
    } else if (shell_network_job.operation == SHELL_NETWORK_JOB_DHCP) {
        result = dhcp_reset();
    }
    return result;
}

static shell_job_step_result_t shell_network_job_drain(
    shell_job_context_t* context) {
    (void)context;
    return SHELL_JOB_STEP_COMPLETE;
}

static void shell_network_job_finish(shell_job_context_t* context,
                                     shell_job_state_t state, int result) {
    dns_status_t dns_status;
    icmp_status_t icmp_status;
    http_status_t http_status;
    dhcp_status_t dhcp_status;

    if (state == SHELL_JOB_STATE_CANCELLED) {
        video_print("Operacao de rede cancelada.\n", 0x0E);
        return;
    }
    if (state != SHELL_JOB_STATE_SUCCEEDED) {
        shell_network_job_print_error("operacao de rede", result);
        return;
    }
    if (shell_network_job.operation == SHELL_NETWORK_JOB_DNS) {
        if (dns_get_status(&dns_status) != OK) return;
        video_print("Servidor: ", 0x07);
        cmd_net_print_ipv4(dns_status.server_ip);
        video_print("\nNome: ", 0x07);
        video_print(dns_status.canonical_name[0] ?
                    dns_status.canonical_name : shell_network_job.target, 0x0B);
        video_print("\nEndereco: ", 0x07);
        cmd_net_print_ipv4(shell_network_job.target_ip);
        video_print("\n", 0x07);
    } else if (shell_network_job.operation == SHELL_NETWORK_JOB_PING) {
        if (icmp_get_status(&icmp_status) == OK) {
            cmd_ping_print_summary(&icmp_status);
        }
    } else if (shell_network_job.operation == SHELL_NETWORK_JOB_HTTP) {
        if (http_get_status(&http_status) != OK) return;
        video_print("HTTP ", 0x0A);
        shell_command_print_num(http_status.status_code);
        video_print(" headers=", 0x07);
        shell_command_print_num(http_status.headers_length);
        video_print(" corpo=", 0x07);
        shell_command_print_num(http_status.body_length);
        video_print(" bytes\n", 0x07);
        cmd_http_print_preview();
    } else if (shell_network_job.operation == SHELL_NETWORK_JOB_DHCP) {
        if (dhcp_get_status(&dhcp_status) != OK) return;
        video_print(shell_network_job.dhcp_renew ?
                    "Lease DHCP renovado.\n" : "Lease DHCP aplicado: ",
                    0x0A);
        if (!shell_network_job.dhcp_renew) {
            cmd_net_print_ipv4(dhcp_status.lease.address);
            video_print(".\n", 0x0A);
        }
    } else if (shell_network_job.operation == SHELL_NETWORK_JOB_CHECK) {
        /* O diagnostico ja imprime cada caso durante o passo cooperativo. */
    }
    (void)context;
}

static const shell_job_definition_t shell_network_job_definition = {
    "network", SHELL_JOB_KIND_NETWORK, shell_network_job_step,
    shell_network_job_cancel, shell_network_job_finish,
    shell_network_job_drain
};

static int shell_network_job_prepare_check(const char* arguments) {
    char id[NETWORK_INTERFACE_ID_SIZE];
    network_interface_info_t info;
    network_manager_status_t status;
    const char* qemu_args;
    int result;

    if (!arguments) {
        LOG_ERROR("SHELL", "Argumentos nulos em job net check");
        video_print("Erro: diagnostico agrupado invalido.\n", 0x0C);
        return ERR_NULL;
    }
    qemu_args = shell_command_match_subcommand(arguments, "qemu");
    if (qemu_args) {
        shell_network_job.check_mode = SHELL_NETWORK_CHECK_MODE_QEMU;
        return OK;
    }
    if (*arguments) {
        result = shell_command_read_single_arg(arguments, id, sizeof(id));
        if (result != OK) {
            LOG_WARN("SHELL", "Uso invalido de net check");
            video_print("Uso: net check [id] | "
                        "net check qemu <id> <ip-local> | "
                        "net check qemu dhcp <id> <dominio> | "
                        "net check qemu tcp <id> <dominio> | "
                        "net check qemu multi <id-a> <id-b>\n", 0x0C);
            return result;
        }
        result = network_manager_find(id, &info);
        if (result != OK) {
            LOG_WARN("SHELL", "Interface invalida em job net check");
            video_print("Erro: interface de rede nao encontrada.\n", 0x0C);
            return result;
        }
        kmemcpy(shell_network_job.interface_id, id, kstrlen(id) + 1U);
        shell_network_job.check_has_interface = 1U;
    }
    if (network_manager_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado Network indisponivel em job net check");
        video_print("Erro: estado de rede indisponivel.\n", 0x0C);
        return ERR_STATE;
    }
    shell_network_job.check_mode = SHELL_NETWORK_CHECK_MODE_REPORT;
    shell_network_job.check_stage = SHELL_NETWORK_CHECK_STAGE_GENERAL;
    shell_network_job.check_total = shell_network_job.check_has_interface ?
                                    13U : 11U;
    return OK;
}

static int shell_network_job_start(const char* arguments) {
    int result = shell_job_start(&shell_network_job_definition, arguments);

    if (result == OK) {
        shell_network_job_inner_failed = 0U;
        shell_network_job.job_generation = shell_job_get_generation();
    }
    return result;
}

static int shell_network_start_dns_job(const char* arguments) {
    uint8_t resolved = 0U;
    int result;

    result = shell_command_read_single_arg(arguments, shell_network_job.target,
                                           sizeof(shell_network_job.target));
    if (result != OK) {
        video_print("Uso: nslookup <dominio>\n", 0x0C);
        return 1;
    }
    kmemset(&shell_network_job, 0, sizeof(shell_network_job));
    shell_network_job.operation = SHELL_NETWORK_JOB_DNS;
    shell_command_read_single_arg(arguments, shell_network_job.target,
                                  sizeof(shell_network_job.target));
    result = dns_resolve(shell_network_job.target,
                          &shell_network_job.target_ip, &resolved);
    if (result != OK) {
        shell_network_job_print_error("consulta DNS", result);
        return 1;
    }
    shell_network_job.phase = SHELL_NETWORK_JOB_PHASE_RESOLVE;
    if (shell_network_job_start(arguments) != OK) {
        dns_reset();
    }
    return 1;
}

static int shell_network_start_ping_job(const char* arguments) {
    char target[SHELL_DNS_NAME_SIZE];
    uint8_t count = ICMP_PING_DEFAULT_COUNT;
    int result;

    result = cmd_ping_parse_args(arguments, target, sizeof(target), &count);
    if (result != OK) {
        video_print("Uso: ping <ip-ou-dominio> [quantidade 1-10]\n", 0x0C);
        return 1;
    }
    kmemset(&shell_network_job, 0, sizeof(shell_network_job));
    shell_network_job.operation = SHELL_NETWORK_JOB_PING;
    shell_network_job.count = count;
    kmemcpy(shell_network_job.target, target, kstrlen(target) + 1U);
    if (cmd_ping_target_is_numeric(target)) {
        result = cmd_net_parse_ipv4(target, &shell_network_job.target_ip);
        if (result != OK || !ipv4_address_is_unicast(shell_network_job.target_ip)) {
            shell_network_job_print_error("destino do ping", ERR_INVALID);
            return 1;
        }
        shell_network_job.phase = SHELL_NETWORK_JOB_PHASE_TRANSFER;
        result = icmp_ping_start(shell_network_job.target_ip, count,
                                 ICMP_PING_TIMEOUT_SECONDS);
        if (result == OK) shell_network_job_print_ping_header();
    } else {
        uint8_t resolved = 0U;
        result = dns_resolve(target, &shell_network_job.target_ip, &resolved);
        shell_network_job.phase = SHELL_NETWORK_JOB_PHASE_RESOLVE;
    }
    if (result != OK) {
        shell_network_job_print_error("ping", result);
        return 1;
    }
    if (shell_network_job_start(arguments) != OK) {
        icmp_reset();
        dns_reset();
    }
    return 1;
}

static int shell_network_start_http_job(const char* arguments) {
    const char* get_args = shell_command_match_subcommand(arguments, "get");
    int result;

    if (!get_args ||
        shell_command_read_single_arg(get_args, shell_network_job.url,
                                      sizeof(shell_network_job.url)) != OK) {
        video_print("Uso: http get <url> | http status\n", 0x0C);
        return 1;
    }
    kmemset(&shell_network_job, 0, sizeof(shell_network_job));
    shell_network_job.operation = SHELL_NETWORK_JOB_HTTP;
    shell_command_read_single_arg(get_args, shell_network_job.url,
                                  sizeof(shell_network_job.url));
    result = http_get_start(shell_network_job.url);
    if (result != OK) {
        shell_network_job_print_error("HTTP GET", result);
        return 1;
    }
    shell_network_job.phase = SHELL_NETWORK_JOB_PHASE_TRANSFER;
    video_print("Executando HTTP GET...\n", 0x07);
    if (shell_network_job_start(arguments) != OK) {
        http_reset();
    }
    return 1;
}

static int shell_network_start_dhcp_job(const char* arguments) {
    const char* acquire_args = shell_command_match_subcommand(arguments,
                                                               "acquire");
    int result;

    kmemset(&shell_network_job, 0, sizeof(shell_network_job));
    if (acquire_args) {
        result = shell_command_read_single_arg(acquire_args,
                                               shell_network_job.interface_id,
                                               sizeof(shell_network_job.interface_id));
        if (result != OK) {
            video_print("Uso: net dhcp acquire <id>\n", 0x0C);
            return 1;
        }
        result = network_manager_acquire_dhcp(shell_network_job.interface_id);
        shell_network_job.dhcp_renew = 0U;
    } else if (shell_command_args_equal(arguments, "renew")) {
        result = network_manager_renew_dhcp();
        shell_network_job.dhcp_renew = 1U;
    } else {
        return 0;
    }
    if (result != OK) {
        shell_network_job_print_error("DHCP", result);
        return 1;
    }
    shell_network_job.operation = SHELL_NETWORK_JOB_DHCP;
    shell_network_job.phase = SHELL_NETWORK_JOB_PHASE_TRANSFER;
    if (shell_network_job_start(arguments) != OK) {
        dhcp_reset();
    }
    return 1;
}

int shell_network_start_job(const char* command, const char* arguments) {
    if (!command) return 0;
    if (kstrcmp(command, "ping") == 0) {
        return shell_network_start_ping_job(arguments);
    }
    if (kstrcmp(command, "nslookup") == 0) {
        return shell_network_start_dns_job(arguments);
    }
    if (kstrcmp(command, "http") == 0) {
        if (shell_command_args_equal(arguments, "status")) return 0;
        return shell_network_start_http_job(arguments);
    }
    if (kstrcmp(command, "net") == 0) {
        const char* arp_args = shell_command_match_subcommand(arguments, "arp");
        const char* dhcp_args = shell_command_match_subcommand(arguments, "dhcp");
        const char* check_args = shell_command_match_subcommand(arguments, "check");
        const char* tcp_args = shell_command_match_subcommand(arguments, "tcp");

        if (dhcp_args) return shell_network_start_dhcp_job(dhcp_args);
        if (check_args) {
            kmemset(&shell_network_job, 0, sizeof(shell_network_job));
            shell_network_job.operation = SHELL_NETWORK_JOB_CHECK;
            shell_network_job.phase = SHELL_NETWORK_JOB_PHASE_TRANSFER;
            if (shell_network_job_prepare_check(check_args) != OK) return 1;
            if (shell_network_job_start(arguments) != OK) {
                video_print("Job de rede recusado.\n", 0x0C);
            }
            return 1;
        }
        if ((tcp_args && shell_command_match_subcommand(tcp_args, "connect")) ||
            (arp_args && shell_command_match_subcommand(arp_args, "resolve"))) {
            kmemset(&shell_network_job, 0, sizeof(shell_network_job));
            shell_network_job.operation = SHELL_NETWORK_JOB_CHECK;
            shell_network_job.phase = SHELL_NETWORK_JOB_PHASE_TRANSFER;
            if (shell_network_job_start(arguments) != OK) {
                video_print("Job de rede recusado.\n", 0x0C);
            }
            return 1;
        }
    }
    return 0;
}

#define SHELL_NETWORK_WRAP_ARGS(adapter, handler) \
    void adapter(const char* arguments) { handler(arguments); }

void shell_dispatch_cmd_net(const char* arguments) {
    if (shell_network_start_job("net", arguments)) return;
    cmd_net(arguments);
}

void shell_dispatch_cmd_ping(const char* arguments) {
    if (shell_network_start_job("ping", arguments)) return;
    cmd_ping(arguments);
}

void shell_dispatch_cmd_nslookup(const char* arguments) {
    if (shell_network_start_job("nslookup", arguments)) return;
    cmd_nslookup(arguments);
}

void shell_dispatch_cmd_http(const char* arguments) {
    if (shell_network_start_job("http", arguments)) return;
    cmd_http(arguments);
}

#undef SHELL_NETWORK_WRAP_ARGS
