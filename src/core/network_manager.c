#include "core/network_manager.h"
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
#include "core/recovery.h"
#include "core/socket.h"
#include "core/string.h"
#include "core/tcp.h"
#include "core/timer.h"
#include "core/udp.h"
#include "core/usb_manager.h"
#include "drivers/e1000.h"
#include "drivers/pci.h"
#include "drivers/rtl8811cu.h"
#include "drivers/rtl8139.h"

#define NETWORK_PCI_CLASS 0x02U
#define NETWORK_VENDOR_INTEL 0x8086U
#define NETWORK_DEVICE_E1000 0x100EU
#define NETWORK_VENDOR_REALTEK 0x10ECU
#define NETWORK_DEVICE_RTL8139 0x8139U
#define NETWORK_HEX_BYTE_DIGITS 2U
#define NETWORK_DIAGNOSTIC_ETHERTYPE 0x88B5U
#define NETWORK_ETHERNET_BROADCAST_OCTET 0xFFU

static network_interface_info_t
    network_interfaces[NETWORK_MANAGER_MAX_INTERFACES];
static network_manager_status_t network_status;
static int network_manager_initialized = 0;
static uint32_t network_last_protocol_tick;
static uint8_t network_has_protocol_tick;
static network_ipv4_source_t network_ipv4_source;
static uint8_t network_manual_dns_override;
static int network_id_matches(const char* expected,
                              const char* requested);

typedef struct {
    uint8_t used;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t port;
    network_transport_t transport;
    int result;
} network_driver_record_t;

static network_driver_record_t
    network_driver_records[NETWORK_MANAGER_MAX_INTERFACES];

static void network_append_char(char* text, uint32_t capacity,
                                uint32_t* offset, char value) {
    if (!text || !offset || !capacity || *offset + 1U >= capacity) return;
    text[*offset] = value;
    (*offset)++;
    text[*offset] = '\0';
}

static void network_append_text(char* text, uint32_t capacity,
                                uint32_t* offset, const char* value) {
    if (!value) return;
    while (*value) {
        network_append_char(text, capacity, offset, *value);
        value++;
    }
}

static void network_append_hex(char* text, uint32_t capacity,
                               uint32_t* offset, uint32_t value,
                               uint32_t digits) {
    static const char hex[] = "0123456789ABCDEF";

    while (digits > 0U) {
        uint32_t shift = (digits - 1U) * 4U;
        network_append_char(text, capacity, offset,
                            hex[(value >> shift) & 0x0FU]);
        digits--;
    }
}

static void network_set_text(char* destination, uint32_t capacity,
                             const char* source) {
    uint32_t offset = 0;

    if (!destination || !capacity) return;
    destination[0] = '\0';
    network_append_text(destination, capacity, &offset, source);
}

static network_adapter_model_t network_detect_model(
    const pci_device_t* pci) {
    if (pci->vendor_id == NETWORK_VENDOR_INTEL &&
        pci->device_id == NETWORK_DEVICE_E1000) {
        return NETWORK_ADAPTER_E1000;
    }
    if (pci->vendor_id == NETWORK_VENDOR_REALTEK &&
        pci->device_id == NETWORK_DEVICE_RTL8139) {
        return NETWORK_ADAPTER_RTL8139;
    }
    return NETWORK_ADAPTER_UNKNOWN;
}

static void network_copy_bars(network_interface_info_t* entry,
                              const pci_device_t* pci) {
    entry->bars[0] = pci->bar0;
    entry->bars[1] = pci->bar1;
    entry->bars[2] = pci->bar2;
    entry->bars[3] = pci->bar3;
    entry->bars[4] = pci->bar4;
    entry->bars[5] = pci->bar5;
}

static network_driver_record_t* network_find_driver_record(
    network_transport_t transport, uint8_t bus, uint8_t device,
    uint8_t function, uint8_t port) {
    for (uint32_t index = 0;
         index < NETWORK_MANAGER_MAX_INTERFACES; index++) {
        network_driver_record_t* record =
            &network_driver_records[index];

        if (record->used && record->transport == transport &&
            record->bus == bus &&
            record->device == device &&
            record->function == function && record->port == port) {
            return record;
        }
    }
    return NULL;
}

static int network_record_driver_result(
    const network_interface_info_t* entry, int result) {
    network_driver_record_t* record;

    if (!entry) {
        LOG_ERROR("NET", "Interface nula ao registrar driver");
        return ERR_NULL;
    }
    record = network_find_driver_record(
        entry->transport, entry->bus, entry->device,
        entry->function, entry->usb_port);
    if (!record) {
        for (uint32_t index = 0;
             index < NETWORK_MANAGER_MAX_INTERFACES; index++) {
            if (!network_driver_records[index].used) {
                record = &network_driver_records[index];
                break;
            }
        }
    }
    if (!record) {
        LOG_ERROR("NET", "Registro de drivers de rede lotado");
        return ERR_OVERFLOW;
    }
    record->used = 1;
    record->bus = entry->bus;
    record->device = entry->device;
    record->function = entry->function;
    record->port = entry->usb_port;
    record->transport = entry->transport;
    record->result = result;
    return OK;
}

static void network_apply_ethernet_status(
    network_interface_info_t* entry) {
    network_driver_record_t* record;
    network_interface_text_t text;
    ethernet_interface_status_t status;

    if (!entry || entry->model == NETWORK_ADAPTER_UNKNOWN) return;
    record = network_find_driver_record(
        entry->transport, entry->bus, entry->device,
        entry->function, entry->usb_port);
    if (!record) return;
    entry->driver_error = record->result;
    if (record->result != OK) {
        entry->state = NETWORK_INTERFACE_DRIVER_ERROR;
        return;
    }
    if (network_manager_format_text(entry, &text) != OK ||
        ethernet_get_interface_status(text.id, &status) != OK) {
        entry->state = NETWORK_INTERFACE_DRIVER_ERROR;
        entry->driver_error = ERR_STATE;
        return;
    }
    entry->ethernet_attached = status.attached;
    entry->state = status.driver.initialized ?
                   NETWORK_INTERFACE_ACTIVE :
                   NETWORK_INTERFACE_DRIVER_ERROR;
    entry->link = status.driver.link_up ?
                  NETWORK_LINK_UP : NETWORK_LINK_DOWN;
    kmemcpy(entry->mac_address, status.mac_address,
            NETWORK_MAC_ADDRESS_SIZE);
    entry->rx_packets = status.driver.rx_packets;
    entry->tx_packets = status.driver.tx_packets;
    entry->rx_errors = status.driver.rx_errors;
    entry->tx_errors = status.driver.tx_errors;
    entry->rx_dropped = status.driver.rx_dropped;
    entry->rx_queue_depth = status.driver.rx_queue_depth;
    entry->rx_queue_high_water =
        status.driver.rx_queue_high_water;
    entry->rx_queue_dropped = status.driver.rx_queue_dropped;
    entry->rx_interrupts = status.driver.rx_interrupts;
    entry->driver_error = status.driver.last_error;
}

static void network_copy_interface(network_interface_info_t* entry,
                                   const pci_device_t* pci) {
    kmemset(entry, 0, sizeof(*entry));
    entry->transport = NETWORK_TRANSPORT_PCI;
    entry->model = network_detect_model(pci);
    entry->state = entry->model == NETWORK_ADAPTER_UNKNOWN ?
                   NETWORK_INTERFACE_UNSUPPORTED :
                   NETWORK_INTERFACE_DRIVER_MISSING;
    entry->link = NETWORK_LINK_UNKNOWN;
    entry->vendor_id = pci->vendor_id;
    entry->device_id = pci->device_id;
    entry->class_code = pci->class;
    entry->subclass_code = pci->subclass;
    entry->prog_if = pci->prog_if;
    entry->revision = pci->revision;
    entry->bus = pci->bus;
    entry->device = pci->device;
    entry->function = pci->function;
    entry->irq = pci->irq;
    network_copy_bars(entry, pci);
    entry->driver_error = ERR_UNAVAILABLE;
    network_apply_ethernet_status(entry);
}

static void network_copy_usb_interface(network_interface_info_t* entry,
                                       const usb_device_info_t* device) {
    kmemset(entry, 0, sizeof(*entry));
    entry->transport = NETWORK_TRANSPORT_USB;
    entry->model = NETWORK_ADAPTER_RTL8811CU;
    entry->state = NETWORK_INTERFACE_DRIVER_MISSING;
    entry->link = NETWORK_LINK_UNKNOWN;
    entry->vendor_id = device->vendor_id;
    entry->device_id = device->product_id;
    entry->revision = (uint8_t)(device->device_revision & 0xFFU);
    entry->bus = device->controller_bus;
    entry->device = device->controller_device;
    entry->function = device->controller_function;
    entry->usb_port = device->port_number;
    entry->usb_address = device->usb_address;
    entry->usb_revision = device->device_revision;
    entry->usb_endpoint_count = device->endpoint_count;
    network_set_text(entry->usb_device_id, sizeof(entry->usb_device_id),
                     device->id);
    entry->driver_error = ERR_UNAVAILABLE;
    network_apply_ethernet_status(entry);
}

static int network_get_protocol_status(
    ethernet_status_t* ethernet_status,
    arp_status_t* arp_layer_status,
    ipv4_status_t* ipv4_layer_status,
    icmp_status_t* icmp_layer_status,
    udp_status_t* udp_layer_status,
    dhcp_status_t* dhcp_layer_status,
    dns_status_t* dns_layer_status,
    tcp_status_t* tcp_layer_status,
    net_socket_status_t* socket_layer_status,
    http_status_t* http_layer_status) {
    if (!ethernet_status || !arp_layer_status ||
        !ipv4_layer_status || !icmp_layer_status ||
        !udp_layer_status || !dhcp_layer_status ||
        !dns_layer_status || !tcp_layer_status ||
        !socket_layer_status || !http_layer_status) {
        LOG_ERROR("NET", "Destino nulo no snapshot de protocolos");
        return ERR_NULL;
    }
    if (ethernet_get_status(ethernet_status) != OK ||
        arp_get_status(arp_layer_status) != OK ||
        ipv4_get_status(ipv4_layer_status) != OK ||
        icmp_get_status(icmp_layer_status) != OK ||
        udp_get_status(udp_layer_status) != OK ||
        dhcp_get_status(dhcp_layer_status) != OK ||
        dns_get_status(dns_layer_status) != OK ||
        tcp_get_status(tcp_layer_status) != OK ||
        net_socket_get_status(socket_layer_status) != OK ||
        http_get_status(http_layer_status) != OK) {
        LOG_ERROR("NET", "Falha ao consultar protocolos de rede");
        return ERR_STATE;
    }
    return OK;
}

static void network_apply_protocol_status(
    const ethernet_status_t* ethernet_status,
    const arp_status_t* arp_layer_status,
    const ipv4_status_t* ipv4_layer_status,
    const icmp_status_t* icmp_layer_status,
    const udp_status_t* udp_layer_status,
    const dhcp_status_t* dhcp_layer_status,
    const dns_status_t* dns_layer_status,
    const tcp_status_t* tcp_layer_status,
    const net_socket_status_t* socket_layer_status,
    const http_status_t* http_layer_status) {
    if (!network_status.active_count) return;
    network_status.packet_io_available = 1;
    network_status.ethernet_available =
        ethernet_status->initialized ? 1U : 0U;
    network_status.arp_available =
        ethernet_status->initialized &&
        arp_layer_status->initialized ? 1U : 0U;
    network_status.arp_configured =
        network_status.arp_available &&
        arp_layer_status->configured ? 1U : 0U;
    network_status.ipv4_available =
        network_status.arp_available &&
        ipv4_layer_status->initialized ? 1U : 0U;
    network_status.ipv4_configured =
        network_status.ipv4_available &&
        ipv4_layer_status->configured ? 1U : 0U;
    network_status.icmp_available =
        network_status.ipv4_available &&
        icmp_layer_status->initialized ? 1U : 0U;
    network_status.udp_available =
        network_status.ipv4_available &&
        udp_layer_status->initialized ? 1U : 0U;
    network_status.dhcp_available =
        network_status.udp_available &&
        dhcp_layer_status->initialized ? 1U : 0U;
    network_status.dhcp_bound =
        network_status.dhcp_available &&
        (dhcp_layer_status->state == DHCP_STATE_BOUND ||
         dhcp_layer_status->state == DHCP_STATE_RENEWING ||
         dhcp_layer_status->state == DHCP_STATE_REBINDING);
    network_status.dns_available =
        network_status.udp_available &&
        dns_layer_status->initialized ? 1U : 0U;
    network_status.dns_configured =
        network_status.dns_available &&
        dns_layer_status->configured ? 1U : 0U;
    network_status.tcp_available =
        network_status.ipv4_available &&
        tcp_layer_status->initialized ? 1U : 0U;
    network_status.sockets_available =
        network_status.tcp_available &&
        socket_layer_status->initialized ? 1U : 0U;
    network_status.http_available =
        network_status.sockets_available &&
        network_status.dns_available &&
        http_layer_status->initialized ? 1U : 0U;
    network_status.tcp_connection_count =
        tcp_layer_status->connection_count;
    network_status.socket_count = socket_layer_status->active_count;
    network_status.ipv4_source = ipv4_layer_status->configured ?
        network_ipv4_source : NETWORK_IPV4_SOURCE_NONE;
}

static int network_collect_interfaces(void) {
    uint8_t pci_count = 0;
    uint32_t usb_count = 0U;
    int pci_result;
    int usb_result;
    int result = OK;

    pci_result = pci_get_device_count(&pci_count);
    if (pci_result != OK && pci_result != ERR_OVERFLOW) {
        LOG_ERROR("NET", "Falha ao consultar inventario PCI");
        return pci_result;
    }
    if (pci_result == ERR_OVERFLOW) {
        network_status.partial = 1;
        result = ERR_OVERFLOW;
    }
    for (uint8_t index = 0; index < pci_count; index++) {
        pci_device_t pci;
        int entry_result = pci_get_device_at(index, &pci);

        if (entry_result != OK) {
            LOG_ERROR("NET", "Falha ao consultar controlador PCI");
            return entry_result;
        }
        if (pci.class != NETWORK_PCI_CLASS) continue;
        if (network_status.interface_count >=
            NETWORK_MANAGER_MAX_INTERFACES) {
            network_status.partial = 1;
            network_status.last_error = ERR_OVERFLOW;
            LOG_WARN("NET", "Limite do inventario de rede atingido");
            return ERR_OVERFLOW;
        }
        network_copy_interface(
            &network_interfaces[network_status.interface_count], &pci);
        if (network_interfaces[network_status.interface_count].model !=
            NETWORK_ADAPTER_UNKNOWN) {
            network_status.recognized_count++;
        }
        if (network_interfaces[network_status.interface_count].state ==
            NETWORK_INTERFACE_ACTIVE) {
            network_status.active_count++;
        }
        if (network_interfaces[network_status.interface_count].state ==
            NETWORK_INTERFACE_DRIVER_ERROR) {
            network_status.driver_error_count++;
        }
        network_status.interface_count++;
    }
    usb_result = usb_manager_get_device_count(&usb_count);
    if (usb_result != OK) {
        if (usb_result != ERR_NOT_FOUND && usb_result != ERR_STATE) {
            LOG_WARN("NET", "Inventario USB indisponivel para Wi-Fi");
            network_status.partial = 1U;
            network_status.last_error = usb_result;
            result = result == OK ? usb_result : result;
        }
        return result;
    }
    for (uint32_t index = 0U; index < usb_count; index++) {
        usb_device_info_t device;
        int entry_result = usb_manager_get_device(index, &device);

        if (entry_result != OK) {
            LOG_WARN("NET", "Falha ao consultar dispositivo USB de rede");
            continue;
        }
        if (device.vendor_id != RTL8811CU_VENDOR_ID ||
            device.product_id != RTL8811CU_PRODUCT_ID) continue;
        if (network_status.interface_count >=
            NETWORK_MANAGER_MAX_INTERFACES) {
            network_status.partial = 1U;
            network_status.last_error = ERR_OVERFLOW;
            LOG_WARN("NET", "Limite do inventario de rede atingido");
            return ERR_OVERFLOW;
        }
        network_copy_usb_interface(
            &network_interfaces[network_status.interface_count], &device);
        network_status.recognized_count++;
        if (network_interfaces[network_status.interface_count].state ==
            NETWORK_INTERFACE_ACTIVE) {
            network_status.active_count++;
        }
        if (network_interfaces[network_status.interface_count].state ==
            NETWORK_INTERFACE_DRIVER_ERROR) {
            network_status.driver_error_count++;
        }
        network_status.interface_count++;
    }
    return result;
}

static void network_apply_interface_roles(
    const ipv4_status_t* ipv4_status,
    const dhcp_status_t* dhcp_status) {
    uint8_t dhcp_pending =
        dhcp_status->state == DHCP_STATE_SELECTING ||
        dhcp_status->state == DHCP_STATE_REQUESTING ||
        dhcp_status->state == DHCP_STATE_APPLYING;

    if (ipv4_status->configured) {
        network_set_text(network_status.l3_interface_id,
                         sizeof(network_status.l3_interface_id),
                         ipv4_status->interface_id);
    }
    for (uint32_t index = 0;
         index < network_status.interface_count; index++) {
        network_interface_text_t text;
        network_interface_info_t* entry = &network_interfaces[index];

        if (network_manager_format_text(entry, &text) != OK) continue;
        entry->l3_active = ipv4_status->configured &&
            network_id_matches(text.id, ipv4_status->interface_id);
        entry->dhcp_pending = dhcp_pending &&
            network_id_matches(text.id, dhcp_status->interface_id);
    }
}

static int network_build_snapshot(void) {
    ethernet_status_t ethernet_status;
    arp_status_t arp_layer_status;
    ipv4_status_t ipv4_layer_status;
    icmp_status_t icmp_layer_status;
    udp_status_t udp_layer_status;
    dhcp_status_t dhcp_layer_status;
    dns_status_t dns_layer_status;
    tcp_status_t tcp_layer_status;
    net_socket_status_t socket_layer_status;
    http_status_t http_layer_status;
    int result = OK;

    kmemset(network_interfaces, 0, sizeof(network_interfaces));
    kmemset(&network_status, 0, sizeof(network_status));
    network_status.initialized = network_manager_initialized ? 1U : 0U;
    network_status.last_error = ERR_NOT_FOUND;
    if (network_get_protocol_status(
            &ethernet_status, &arp_layer_status,
            &ipv4_layer_status, &icmp_layer_status,
            &udp_layer_status, &dhcp_layer_status,
            &dns_layer_status, &tcp_layer_status,
            &socket_layer_status, &http_layer_status) != OK) {
        return ERR_STATE;
    }
    result = network_collect_interfaces();
    if (result != OK && result != ERR_OVERFLOW) return result;
    network_apply_interface_roles(&ipv4_layer_status,
                                  &dhcp_layer_status);
    network_apply_protocol_status(
        &ethernet_status, &arp_layer_status,
        &ipv4_layer_status, &icmp_layer_status,
        &udp_layer_status, &dhcp_layer_status,
        &dns_layer_status, &tcp_layer_status,
        &socket_layer_status, &http_layer_status);
    if (network_status.partial) {
        network_status.last_error = ERR_OVERFLOW;
    } else if (network_status.driver_error_count) {
        network_status.last_error = ERR_STATE;
        for (uint32_t index = 0;
             index < network_status.interface_count; index++) {
            if (network_interfaces[index].state ==
                NETWORK_INTERFACE_DRIVER_ERROR) {
                network_status.last_error =
                    network_interfaces[index].driver_error;
                break;
            }
        }
    } else if (network_status.active_count) {
        network_status.last_error =
            network_status.ethernet_available &&
            network_status.arp_available &&
            network_status.ipv4_available &&
            network_status.icmp_available &&
            network_status.udp_available &&
            network_status.dhcp_available &&
            network_status.dns_available &&
            network_status.tcp_available &&
            network_status.sockets_available &&
            network_status.http_available ? OK : ERR_STATE;
    } else if (network_status.interface_count) {
        network_status.last_error = ERR_UNAVAILABLE;
        for (uint32_t index = 0; index < network_status.interface_count;
             index++) {
            if (network_interfaces[index].state ==
                NETWORK_INTERFACE_DRIVER_ERROR) {
                network_status.last_error =
                    network_interfaces[index].driver_error;
                break;
            }
        }
    }
    return result;
}

static int network_find_pci_device(
    const network_interface_info_t* entry, pci_device_t* out_pci) {
    uint8_t count = 0;
    int result;

    if (!entry || !out_pci) {
        LOG_ERROR("NET", "Argumento nulo ao localizar NIC PCI");
        return ERR_NULL;
    }
    result = pci_get_device_count(&count);
    if (result != OK && result != ERR_OVERFLOW) return result;
    for (uint8_t index = 0; index < count; index++) {
        result = pci_get_device_at(index, out_pci);
        if (result != OK) return result;
        if (out_pci->bus == entry->bus &&
            out_pci->device == entry->device &&
            out_pci->function == entry->function) {
            return OK;
        }
    }
    LOG_ERROR("NET", "NIC desapareceu do inventario PCI");
    return ERR_NOT_FOUND;
}

static int network_probe_drivers(void) {
    LOG_INFO("NET", "Inicializando drivers das interfaces reconhecidas");
    for (uint32_t index = 0;
         index < network_status.interface_count; index++) {
        network_interface_info_t* entry = &network_interfaces[index];
        network_interface_text_t text;
        ethernet_interface_t interface;
        pci_device_t pci;
        usb_device_info_t usb_device;
        int result;

        if (entry->model == NETWORK_ADAPTER_UNKNOWN) continue;
        result = network_manager_format_text(entry, &text);
        if (result == OK && entry->transport == NETWORK_TRANSPORT_PCI) {
            result = network_find_pci_device(entry, &pci);
            if (result == OK) {
                kmemset(&interface, 0, sizeof(interface));
                result = entry->model == NETWORK_ADAPTER_E1000 ?
                    e1000_init(&pci, text.id, &interface) :
                    rtl8139_init(&pci, text.id, &interface);
            }
        } else if (result == OK && entry->transport == NETWORK_TRANSPORT_USB) {
            result = usb_manager_find_device(entry->usb_device_id,
                                              &usb_device);
            if (result == OK) {
                kmemset(&interface, 0, sizeof(interface));
                result = rtl8811cu_init(&usb_device, &interface);
            }
        }
        if (result == OK) {
            result = ethernet_attach_interface(&interface);
        }
        if (network_record_driver_result(entry, result) != OK) {
            LOG_ERROR("NET", "Falha ao registrar resultado do driver");
        }
        if (result != OK) {
            LOG_WARN("NET", "Falha isolada ao inicializar interface");
        }
    }
    LOG_INFO("NET", "Probe dos drivers de rede concluido");
    return OK;
}

static int network_start_ethernet(void) {
    ethernet_status_t layer_status;
    int result;

    if (!network_status.active_count) return OK;
    result = ethernet_get_status(&layer_status);
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao consultar inicializacao Ethernet");
        return result;
    }
    if (layer_status.initialized) {
        network_status.ethernet_available = 1;
        if (!network_status.partial && !network_status.driver_error_count) {
            network_status.last_error = OK;
        }
        return OK;
    }
    LOG_ERROR("NET", "Registro Ethernet incoerente com inventario");
    return ERR_STATE;
}

static int network_start_arp(void) {
    arp_status_t status;
    int result;

    if (!network_status.ethernet_available) return OK;
    result = arp_get_status(&status);
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao consultar inicializacao ARP");
        return result;
    }
    if (!status.initialized) {
        result = arp_init();
        if (result != OK) {
            LOG_ERROR("NET", "Falha ao iniciar protocolo ARP");
            return result;
        }
        result = arp_get_status(&status);
        if (result != OK) {
            LOG_ERROR("NET", "Falha ao confirmar inicializacao ARP");
            return result;
        }
    }
    network_status.arp_available = 1;
    network_status.arp_configured = status.configured;
    if (!network_status.partial) network_status.last_error = OK;
    return OK;
}

static int network_start_ipv4(void) {
    ipv4_status_t status;
    int result;

    if (!network_status.arp_available) return OK;
    result = ipv4_get_status(&status);
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao consultar inicializacao IPv4");
        return result;
    }
    if (!status.initialized) {
        result = ipv4_init();
        if (result != OK) {
            LOG_ERROR("NET", "Falha ao iniciar protocolo IPv4");
            return result;
        }
        result = ipv4_get_status(&status);
        if (result != OK || !status.initialized) {
            LOG_ERROR("NET", "Falha ao confirmar inicializacao IPv4");
            return result != OK ? result : ERR_STATE;
        }
    }
    network_status.ipv4_available = 1;
    network_status.ipv4_configured = status.configured;
    if (!network_status.partial) network_status.last_error = OK;
    return OK;
}

static int network_start_icmp(void) {
    icmp_status_t status;
    int result;

    if (!network_status.ipv4_available) return OK;
    result = icmp_get_status(&status);
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao consultar inicializacao ICMP");
        return result;
    }
    if (!status.initialized) {
        result = icmp_init();
        if (result != OK) {
            LOG_ERROR("NET", "Falha ao iniciar protocolo ICMP");
            return result;
        }
        result = icmp_get_status(&status);
        if (result != OK || !status.initialized) {
            LOG_ERROR("NET", "Falha ao confirmar inicializacao ICMP");
            return result != OK ? result : ERR_STATE;
        }
    }
    network_status.icmp_available = 1;
    if (!network_status.partial) network_status.last_error = OK;
    return OK;
}

static int network_start_udp(void) {
    udp_status_t status;
    int result;

    if (!network_status.ipv4_available) return OK;
    result = udp_get_status(&status);
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao consultar inicializacao UDP");
        return result;
    }
    if (!status.initialized) {
        result = udp_init();
        if (result != OK || udp_get_status(&status) != OK ||
            !status.initialized) {
            LOG_ERROR("NET", "Falha ao iniciar protocolo UDP");
            return result != OK ? result : ERR_STATE;
        }
    }
    network_status.udp_available = 1;
    return OK;
}

static int network_start_dhcp(void) {
    dhcp_status_t status;
    int result;

    if (!network_status.udp_available) return OK;
    result = dhcp_get_status(&status);
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao consultar inicializacao DHCP");
        return result;
    }
    if (!status.initialized) {
        result = dhcp_init();
        if (result != OK || dhcp_get_status(&status) != OK ||
            !status.initialized) {
            LOG_ERROR("NET", "Falha ao iniciar cliente DHCP");
            return result != OK ? result : ERR_STATE;
        }
    }
    network_status.dhcp_available = 1;
    return OK;
}

static int network_start_dns(void) {
    dns_status_t status;
    int result;

    if (!network_status.udp_available) return OK;
    result = dns_get_status(&status);
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao consultar inicializacao DNS");
        return result;
    }
    if (!status.initialized) {
        result = dns_init();
        if (result != OK || dns_get_status(&status) != OK ||
            !status.initialized) {
            LOG_ERROR("NET", "Falha ao iniciar cliente DNS");
            return result != OK ? result : ERR_STATE;
        }
    }
    network_status.dns_available = 1;
    network_status.dns_configured = status.configured;
    return OK;
}

static int network_start_tcp(void) {
    tcp_status_t status;
    int result;

    if (!network_status.ipv4_available) return OK;
    result = tcp_get_status(&status);
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao consultar inicializacao TCP");
        return result;
    }
    if (!status.initialized) {
        result = tcp_init();
        if (result != OK || tcp_get_status(&status) != OK ||
            !status.initialized) {
            LOG_ERROR("NET", "Falha ao iniciar protocolo TCP");
            return result != OK ? result : ERR_STATE;
        }
    }
    network_status.tcp_available = 1;
    network_status.tcp_connection_count = status.connection_count;
    return OK;
}

static int network_start_sockets(void) {
    net_socket_status_t status;
    int result;

    result = socket_init();
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao iniciar camada generica de sockets");
        return result;
    }
    if (!network_status.tcp_available) return OK;
    result = net_socket_get_status(&status);
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao consultar sockets nativos");
        return result;
    }
    if (!status.initialized) {
        result = net_socket_init();
        if (result != OK || net_socket_get_status(&status) != OK ||
            !status.initialized) {
            LOG_ERROR("NET", "Falha ao iniciar sockets nativos");
            return result != OK ? result : ERR_STATE;
        }
    }
    network_status.sockets_available = 1;
    network_status.socket_count = status.active_count;
    return OK;
}

static int network_start_http(void) {
    http_status_t status;
    int result;

    if (!network_status.sockets_available ||
        !network_status.dns_available) return OK;
    result = http_get_status(&status);
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao consultar cliente HTTP");
        return result;
    }
    if (!status.initialized) {
        result = http_init();
        if (result != OK || http_get_status(&status) != OK ||
            !status.initialized) {
            LOG_ERROR("NET", "Falha ao iniciar cliente HTTP");
            return result != OK ? result : ERR_STATE;
        }
    }
    network_status.http_available = 1;
    return OK;
}

static void network_sync_recovery(int result) {
    const recovery_component_t* current =
        recovery_get(RECOVERY_COMPONENT_NETWORK);
    recovery_state_t state;
    int error;
    const char* message;

    if (result != OK && result != ERR_OVERFLOW) {
        state = RECOVERY_STATE_DISABLED;
        error = result;
        message = "Inventario de rede indisponivel";
    } else if (network_status.partial) {
        state = RECOVERY_STATE_DEGRADED;
        error = ERR_OVERFLOW;
        message = "Inventario de rede parcial";
    } else if (network_status.driver_error_count) {
        state = RECOVERY_STATE_DEGRADED;
        error = network_status.last_error;
        message = "Uma ou mais interfaces falharam";
    } else if (network_status.active_count &&
               !network_status.ethernet_available) {
        state = RECOVERY_STATE_DEGRADED;
        error = network_status.last_error;
        message = "Camada Ethernet indisponivel";
    } else if (network_status.active_count &&
               !network_status.arp_available) {
        state = RECOVERY_STATE_DEGRADED;
        error = network_status.last_error;
        message = "Protocolo ARP indisponivel";
    } else if (network_status.active_count &&
               !network_status.ipv4_available) {
        state = RECOVERY_STATE_DEGRADED;
        error = network_status.last_error;
        message = "Protocolo IPv4 indisponivel";
    } else if (network_status.active_count &&
               !network_status.icmp_available) {
        state = RECOVERY_STATE_DEGRADED;
        error = network_status.last_error;
        message = "Protocolo ICMP indisponivel";
    } else if (network_status.active_count &&
               !network_status.udp_available) {
        state = RECOVERY_STATE_DEGRADED;
        error = network_status.last_error;
        message = "Protocolo UDP indisponivel";
    } else if (network_status.active_count &&
               !network_status.dhcp_available) {
        state = RECOVERY_STATE_DEGRADED;
        error = network_status.last_error;
        message = "Cliente DHCP indisponivel";
    } else if (network_status.active_count &&
               !network_status.dns_available) {
        state = RECOVERY_STATE_DEGRADED;
        error = network_status.last_error;
        message = "Cliente DNS indisponivel";
    } else if (network_status.active_count &&
               !network_status.tcp_available) {
        state = RECOVERY_STATE_DEGRADED;
        error = network_status.last_error;
        message = "Protocolo TCP indisponivel";
    } else if (network_status.active_count &&
               !network_status.sockets_available) {
        state = RECOVERY_STATE_DEGRADED;
        error = network_status.last_error;
        message = "Sockets nativos indisponiveis";
    } else if (network_status.active_count &&
               !network_status.http_available) {
        state = RECOVERY_STATE_DEGRADED;
        error = network_status.last_error;
        message = "Cliente HTTP indisponivel";
    } else if (network_status.active_count) {
        state = RECOVERY_STATE_READY;
        error = OK;
        message = "Componente operacional";
    } else if (network_status.interface_count) {
        state = RECOVERY_STATE_DEGRADED;
        error = network_status.last_error;
        message = error == ERR_UNAVAILABLE ?
                  "Controlador detectado; driver nao implementado" :
                  "Controlador detectado; driver com falha";
    } else {
        state = RECOVERY_STATE_DISABLED;
        error = ERR_NOT_FOUND;
        message = "Nenhum controlador de rede detectado";
    }

    if (!current) {
        LOG_ERROR("NET", "Componente Network ausente do recovery");
        return;
    }
    if (current->state == state && current->last_error == error) return;
    if (state == RECOVERY_STATE_READY) {
        if (recovery_mark_ready(RECOVERY_COMPONENT_NETWORK) != OK) {
            LOG_ERROR("NET", "Falha ao marcar Network pronto");
        }
    } else if (state == RECOVERY_STATE_DEGRADED) {
        if (recovery_mark_degraded(RECOVERY_COMPONENT_NETWORK,
                                   error, message) != OK) {
            LOG_ERROR("NET", "Falha ao marcar Network degradado");
        }
    } else if (recovery_mark_disabled(RECOVERY_COMPONENT_NETWORK,
                                      error, message) != OK) {
        LOG_ERROR("NET", "Falha ao desabilitar Network");
    }
}

static char network_ascii_upper(char value) {
    if (value >= 'a' && value <= 'z') return (char)(value - ('a' - 'A'));
    return value;
}

static int network_id_matches(const char* expected, const char* requested) {
    if (!expected || !requested) return 0;
    while (*expected && *requested) {
        if (*expected == ':' && *requested == '-') {
            expected++;
            requested++;
            continue;
        }
        if (network_ascii_upper(*expected) !=
            network_ascii_upper(*requested)) {
            return 0;
        }
        expected++;
        requested++;
    }
    return *expected == '\0' && *requested == '\0';
}

static void network_start_protocols(void) {
    int result = network_start_ethernet();

    if (result != OK) {
        network_status.ethernet_available = 0;
        network_status.last_error = result;
        LOG_ERROR("NET", "Inventario ativo sem camada Ethernet");
    }
    result = network_start_arp();
    if (result != OK) {
        network_status.arp_available = 0;
        network_status.arp_configured = 0;
        network_status.last_error = result;
        LOG_ERROR("NET", "Inventario ativo sem protocolo ARP");
    }
    result = network_start_ipv4();
    if (result != OK) {
        network_status.ipv4_available = 0;
        network_status.ipv4_configured = 0;
        network_status.last_error = result;
        LOG_ERROR("NET", "Inventario ativo sem protocolo IPv4");
    }
    result = network_start_icmp();
    if (result != OK) {
        network_status.icmp_available = 0;
        network_status.last_error = result;
        LOG_ERROR("NET", "Inventario ativo sem protocolo ICMP");
    }
    result = network_start_udp();
    if (result != OK) {
        network_status.udp_available = 0;
        network_status.last_error = result;
        LOG_ERROR("NET", "Inventario ativo sem protocolo UDP");
    }
    result = network_start_dhcp();
    if (result != OK) {
        network_status.dhcp_available = 0;
        network_status.last_error = result;
        LOG_ERROR("NET", "Inventario ativo sem cliente DHCP");
    }
    result = network_start_dns();
    if (result != OK) {
        network_status.dns_available = 0;
        network_status.dns_configured = 0;
        network_status.last_error = result;
        LOG_ERROR("NET", "Inventario ativo sem cliente DNS");
    }
    result = network_start_tcp();
    if (result != OK) {
        network_status.tcp_available = 0;
        network_status.tcp_connection_count = 0;
        network_status.last_error = result;
        LOG_ERROR("NET", "Inventario ativo sem protocolo TCP");
    }
    result = network_start_sockets();
    if (result != OK) {
        network_status.sockets_available = 0;
        network_status.socket_count = 0;
        network_status.last_error = result;
        LOG_ERROR("NET", "Inventario ativo sem sockets nativos");
    }
    result = network_start_http();
    if (result != OK) {
        network_status.http_available = 0;
        network_status.last_error = result;
        LOG_ERROR("NET", "Inventario ativo sem cliente HTTP");
    }
}

int network_manager_init(void) {
    int result;
    int snapshot_result;

    LOG_INFO("NET", "Inicializando inventario de rede");
    network_ipv4_source = NETWORK_IPV4_SOURCE_NONE;
    network_manual_dns_override = 0;
    kmemset(network_driver_records, 0,
            sizeof(network_driver_records));
    network_manager_initialized = 1;
    result = ethernet_init();
    if (result != OK) {
        network_manager_initialized = 0;
        LOG_ERROR("NET", "Falha ao inicializar registro Ethernet");
        return result;
    }
    result = network_build_snapshot();
    if (result != OK && result != ERR_OVERFLOW) {
        network_manager_initialized = 0;
        network_status.initialized = 0;
        network_sync_recovery(result);
        LOG_ERROR("NET", "Falha ao inicializar inventario de rede");
        return result;
    }
    if (network_probe_drivers() != OK) {
        network_manager_initialized = 0;
        LOG_ERROR("NET", "Falha ao executar probe das interfaces");
        return ERR_STATE;
    }
    snapshot_result = network_build_snapshot();
    if (snapshot_result != OK && snapshot_result != ERR_OVERFLOW) {
        network_manager_initialized = 0;
        LOG_ERROR("NET", "Falha ao reconstruir inventario apos probe");
        return snapshot_result;
    }
    if (snapshot_result == ERR_OVERFLOW) result = ERR_OVERFLOW;
    network_start_protocols();
    snapshot_result = network_build_snapshot();
    if (snapshot_result != OK && snapshot_result != ERR_OVERFLOW) {
        network_status.last_error = snapshot_result;
        LOG_ERROR("NET", "Falha ao consolidar estado da rede");
        result = snapshot_result;
    } else if (snapshot_result == ERR_OVERFLOW) {
        result = ERR_OVERFLOW;
    }
    network_sync_recovery(result);
    if (result == ERR_OVERFLOW) {
        LOG_WARN("NET", "Inventario de rede inicializado parcialmente");
    }
    LOG_INFO("NET", "Inventario de rede inicializado com sucesso");
    return result;
}

int network_manager_refresh(void) {
    int result;

    if (!network_manager_initialized) {
        LOG_ERROR("NET", "Atualizacao antes da inicializacao de rede");
        return ERR_STATE;
    }
    result = network_build_snapshot();
    network_sync_recovery(result);
    if (result != OK && result != ERR_OVERFLOW) {
        LOG_ERROR("NET", "Falha ao atualizar inventario de rede");
        return result;
    }
    if (result == ERR_OVERFLOW) {
        LOG_WARN("NET", "Inventario de rede atualizado parcialmente");
    } else {
        LOG_INFO("NET", "Inventario de rede atualizado com sucesso");
    }
    return result;
}

static uint8_t network_dns_reachable(uint32_t local_ip,
                                     uint32_t subnet_mask,
                                     uint32_t gateway,
                                     uint32_t server_ip) {
    uint32_t network = local_ip & subnet_mask;
    uint32_t broadcast = network | ~subnet_mask;

    if (!ipv4_address_is_unicast(server_ip) ||
        server_ip == local_ip || server_ip == network ||
        server_ip == broadcast) return 0;
    return (server_ip & subnet_mask) == network || gateway;
}

static uint8_t network_ipv4_parameters_valid(uint32_t local_ip,
                                             uint32_t subnet_mask,
                                             uint32_t gateway) {
    uint32_t network;
    uint32_t broadcast;

    if (!ipv4_address_is_unicast(local_ip) ||
        !ipv4_mask_is_valid(subnet_mask)) return 0;
    network = local_ip & subnet_mask;
    broadcast = network | ~subnet_mask;
    if (local_ip == network || local_ip == broadcast) return 0;
    if (!gateway) return 1;
    return ipv4_address_is_unicast(gateway) &&
           (gateway & subnet_mask) == network &&
           gateway != local_ip && gateway != network &&
           gateway != broadcast;
}

static int network_restore_configuration(
    const ipv4_status_t* ipv4_before,
    const arp_status_t* arp_before,
    const dns_status_t* dns_before) {
    int result;

    if (ipv4_before->configured) {
        result = ipv4_configure(
            ipv4_before->interface_id, ipv4_before->local_mac,
            ipv4_before->local_ip, ipv4_before->subnet_mask,
            ipv4_before->gateway);
    } else {
        result = ipv4_unconfigure();
        if (result == OK && arp_before->configured) {
            result = arp_configure(
                arp_before->interface_id, arp_before->local_mac,
                arp_before->local_ip);
        } else if (result == OK) {
            result = arp_unconfigure();
        }
    }
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao restaurar IPv4 anterior");
        return result;
    }
    if (dns_before->configured) {
        result = dns_configure(dns_before->server_ip);
    } else {
        result = dns_unconfigure();
    }
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao restaurar DNS anterior");
        return result;
    }
    return icmp_reset();
}

static int network_reset_remote_clients(void) {
    int result;

    if (network_status.http_available) {
        result = http_reset();
        if (result != OK) {
            LOG_ERROR("NET", "Falha ao cancelar cliente HTTP");
            return result;
        }
    }
    if (network_status.sockets_available) {
        result = net_socket_reset();
        if (result != OK) {
            LOG_ERROR("NET", "Falha ao cancelar sockets nativos");
            return result;
        }
    }
    network_status.tcp_connection_count = 0;
    network_status.socket_count = 0;
    return OK;
}

static uint8_t network_ipv4_changed(
    const ipv4_status_t* before, const char* interface_id,
    const dhcp_lease_t* lease) {
    return !before->configured ||
           !network_id_matches(before->interface_id, interface_id) ||
           before->local_ip != lease->address ||
           before->subnet_mask != lease->subnet_mask ||
           before->gateway != lease->gateway;
}

static int network_apply_dhcp_lease(const dhcp_lease_t* lease) {
    network_interface_info_t info;
    network_interface_text_t text;
    ipv4_status_t ipv4_before;
    arp_status_t arp_before;
    dns_status_t dns_before;
    dhcp_status_t dhcp;
    network_ipv4_source_t old_source = network_ipv4_source;
    uint8_t old_manual = network_manual_dns_override;
    uint8_t changed;
    uint8_t lease_configuration_changed;
    int result;

    if (!lease || dhcp_get_status(&dhcp) != OK ||
        network_manager_find(dhcp.interface_id, &info) != OK ||
        network_manager_format_text(&info, &text) != OK ||
        arp_get_status(&arp_before) != OK ||
        ipv4_get_status(&ipv4_before) != OK ||
        dns_get_status(&dns_before) != OK) {
        LOG_ERROR("NET", "Falha ao preparar aplicacao do lease DHCP");
        return ERR_STATE;
    }
    if (lease->dns_server &&
        !network_dns_reachable(lease->address, lease->subnet_mask,
                               lease->gateway,
                               lease->dns_server)) {
        LOG_ERROR("NET", "Lease DHCP forneceu DNS sem rota");
        return ERR_INVALID;
    }
    changed = network_ipv4_changed(&ipv4_before, text.id, lease);
    lease_configuration_changed =
        changed || old_source != NETWORK_IPV4_SOURCE_DHCP ||
        dhcp.lease.dns_server != lease->dns_server;
    if (lease_configuration_changed) {
        result = network_reset_remote_clients();
        if (result != OK) return result;
    }
    result = ipv4_configure(text.id, info.mac_address, lease->address,
                            lease->subnet_mask, lease->gateway);
    if (result == OK &&
        (changed || old_source != NETWORK_IPV4_SOURCE_DHCP)) {
        result = icmp_reset();
    }
    if (result == OK &&
        (!old_manual || lease_configuration_changed)) {
        result = lease->dns_server ?
            dns_configure(lease->dns_server) : dns_unconfigure();
    }
    if (result != OK) {
        network_restore_configuration(
            &ipv4_before, &arp_before, &dns_before);
        network_ipv4_source = old_source;
        network_manual_dns_override = old_manual;
        LOG_ERROR("NET", "Aplicacao atomica do lease DHCP falhou");
        return result;
    }
    network_ipv4_source = NETWORK_IPV4_SOURCE_DHCP;
    if (lease_configuration_changed) {
        network_manual_dns_override = 0;
    }
    network_status.arp_configured = 1;
    network_status.ipv4_configured = 1;
    network_status.ipv4_source = network_ipv4_source;
    network_set_text(network_status.l3_interface_id,
                     sizeof(network_status.l3_interface_id), text.id);
    network_status.dns_configured =
        (lease->dns_server ||
         (!lease_configuration_changed && old_manual)) ? 1U : 0U;
    return OK;
}

static int network_drop_dhcp_configuration(void) {
    int result;

    if (network_ipv4_source != NETWORK_IPV4_SOURCE_DHCP) return OK;
    result = network_reset_remote_clients();
    if (result == OK) result = icmp_reset();
    if (result == OK) result = dns_unconfigure();
    if (result == OK) result = ipv4_unconfigure();
    if (result == OK) result = arp_unconfigure();
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao remover configuracao DHCP");
        return result;
    }
    network_ipv4_source = NETWORK_IPV4_SOURCE_NONE;
    network_manual_dns_override = 0;
    network_status.arp_configured = 0;
    network_status.ipv4_configured = 0;
    network_status.dns_configured = 0;
    network_status.ipv4_source = NETWORK_IPV4_SOURCE_NONE;
    network_status.l3_interface_id[0] = '\0';
    return OK;
}

static void network_refresh_dynamic_status(void) {
    dhcp_status_t dhcp;
    dns_status_t dns;
    ipv4_status_t ipv4;
    tcp_status_t tcp;
    net_socket_status_t sockets;

    if (dhcp_get_status(&dhcp) == OK) {
        network_status.dhcp_bound =
            dhcp.state == DHCP_STATE_BOUND ||
            dhcp.state == DHCP_STATE_RENEWING ||
            dhcp.state == DHCP_STATE_REBINDING;
    }
    if (dns_get_status(&dns) == OK) {
        network_status.dns_configured = dns.configured;
    }
    if (ipv4_get_status(&ipv4) == OK) {
        network_status.ipv4_configured = ipv4.configured;
        network_set_text(network_status.l3_interface_id,
                         sizeof(network_status.l3_interface_id),
                         ipv4.configured ? ipv4.interface_id : "");
    }
    if (tcp_get_status(&tcp) == OK) {
        network_status.tcp_connection_count = tcp.connection_count;
    }
    if (net_socket_get_status(&sockets) == OK) {
        network_status.socket_count = sockets.active_count;
    }
    network_status.ipv4_source = network_status.ipv4_configured ?
        network_ipv4_source : NETWORK_IPV4_SOURCE_NONE;
}

static int network_process_dhcp_event(void) {
    dhcp_event_t event;
    dhcp_lease_t lease;
    int result;
    int complete_result;

    result = dhcp_take_event(&event, &lease);
    if (result != OK || event == DHCP_EVENT_NONE) return result;
    if (event == DHCP_EVENT_APPLY_LEASE) {
        result = network_apply_dhcp_lease(&lease);
    } else {
        result = network_drop_dhcp_configuration();
    }
    complete_result = dhcp_complete_event(event, result);
    if (complete_result != OK) {
        LOG_ERROR("NET", "Falha ao concluir evento DHCP");
        return complete_result;
    }
    network_refresh_dynamic_status();
    return result;
}

int network_manager_poll(uint32_t* out_processed) {
    uint32_t current_tick;
    int maintenance_error = OK;
    int result;

    if (!out_processed) {
        LOG_ERROR("NET", "Destino nulo ao processar recepcao");
        return ERR_NULL;
    }
    *out_processed = 0;
    if (!network_manager_initialized) {
        LOG_ERROR("NET", "Recepcao antes da inicializacao de rede");
        return ERR_STATE;
    }
    if (!network_status.ethernet_available) return OK;
    result = ethernet_poll(ETHERNET_RX_POLL_BUDGET, out_processed);
    if (result != OK) {
        network_status.last_error = result;
        LOG_ERROR("NET", "Falha ao processar recepcao Ethernet");
        return result;
    }
    current_tick = timer_get_ticks();
    if (!network_has_protocol_tick ||
        current_tick != network_last_protocol_tick) {
        network_has_protocol_tick = 1;
        network_last_protocol_tick = current_tick;
        if (network_status.arp_configured) {
            result = arp_maintain();
            if (result != OK) {
                network_status.last_error = result;
                maintenance_error = result;
                LOG_WARN("NET", "Manutencao ARP falhou sem parar polling");
            }
        }
        if (network_status.icmp_available &&
            network_status.ipv4_configured) {
            result = icmp_maintain();
            if (result != OK) {
                network_status.last_error = result;
                if (maintenance_error == OK) maintenance_error = result;
                LOG_WARN("NET", "Manutencao ICMP falhou sem parar polling");
            }
        }
        if (network_status.dhcp_available) {
            result = dhcp_maintain();
            if (result != OK) {
                network_status.last_error = result;
                if (maintenance_error == OK) maintenance_error = result;
                LOG_WARN("NET", "Manutencao DHCP falhou sem parar polling");
            }
            result = network_process_dhcp_event();
            if (result != OK) {
                network_status.last_error = result;
                if (maintenance_error == OK) maintenance_error = result;
                LOG_WARN("NET", "Evento DHCP falhou sem parar polling");
            }
        }
        if (network_status.dns_available) {
            result = dns_maintain();
            if (result != OK) {
                network_status.last_error = result;
                if (maintenance_error == OK) maintenance_error = result;
                LOG_WARN("NET", "Manutencao DNS falhou sem parar polling");
            }
        }
        if (network_status.tcp_available) {
            result = tcp_maintain();
            if (result != OK) {
                network_status.last_error = result;
                if (maintenance_error == OK) maintenance_error = result;
                LOG_WARN("NET", "Manutencao TCP falhou sem parar polling");
            }
        }
        if (network_status.sockets_available) {
            result = net_socket_maintain();
            if (result != OK) {
                network_status.last_error = result;
                if (maintenance_error == OK) maintenance_error = result;
                LOG_WARN("NET", "Manutencao de sockets falhou");
            }
        }
        if (network_status.http_available) {
            result = http_maintain();
            if (result != OK) {
                network_status.last_error = result;
                if (maintenance_error == OK) maintenance_error = result;
                LOG_WARN("NET", "Manutencao HTTP falhou sem parar polling");
            }
        }
        network_refresh_dynamic_status();
    }
    if (maintenance_error == OK && !network_status.partial &&
        !network_status.driver_error_count) {
        network_status.last_error = OK;
    }
    return OK;
}

int network_manager_get_status(network_manager_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("NET", "Destino nulo ao consultar estado de rede");
        return ERR_NULL;
    }
    if (!network_manager_initialized) {
        LOG_ERROR("NET", "Consulta antes da inicializacao de rede");
        return ERR_STATE;
    }
    *out_status = network_status;
    return OK;
}

int network_manager_get_count(uint32_t* out_count) {
    if (!out_count) {
        LOG_ERROR("NET", "Destino nulo ao consultar interfaces");
        return ERR_NULL;
    }
    if (!network_manager_initialized) {
        LOG_ERROR("NET", "Contagem antes da inicializacao de rede");
        return ERR_STATE;
    }
    *out_count = network_status.interface_count;
    return OK;
}

int network_manager_get_interface(uint32_t index,
                                  network_interface_info_t* out_info) {
    network_interface_text_t text;
    ipv4_status_t ipv4;
    dhcp_status_t dhcp;

    if (!out_info) {
        LOG_ERROR("NET", "Destino nulo ao consultar interface");
        return ERR_NULL;
    }
    if (!network_manager_initialized) {
        LOG_ERROR("NET", "Interface consultada antes da inicializacao");
        return ERR_STATE;
    }
    if (index >= network_status.interface_count) {
        LOG_ERROR("NET", "Indice de interface de rede invalido");
        return ERR_INVALID;
    }
    *out_info = network_interfaces[index];
    network_apply_ethernet_status(out_info);
    if (network_manager_format_text(out_info, &text) != OK ||
        ipv4_get_status(&ipv4) != OK ||
        dhcp_get_status(&dhcp) != OK) {
        LOG_ERROR("NET", "Falha ao atualizar papel da interface");
        return ERR_STATE;
    }
    out_info->l3_active = ipv4.configured &&
        network_id_matches(text.id, ipv4.interface_id);
    out_info->dhcp_pending =
        (dhcp.state == DHCP_STATE_SELECTING ||
         dhcp.state == DHCP_STATE_REQUESTING ||
         dhcp.state == DHCP_STATE_APPLYING) &&
        network_id_matches(text.id, dhcp.interface_id);
    return OK;
}

int network_manager_format_text(const network_interface_info_t* info,
                                network_interface_text_t* out_text) {
    uint32_t offset = 0;

    if (!info || !out_text) {
        LOG_ERROR("NET", "Argumento nulo ao formatar interface");
        return ERR_NULL;
    }
    kmemset(out_text, 0, sizeof(*out_text));
    network_append_text(out_text->id, NETWORK_INTERFACE_ID_SIZE,
                        &offset, info->transport == NETWORK_TRANSPORT_USB ?
                        "net-usb-" : "net-pci-");
    network_append_hex(out_text->id, NETWORK_INTERFACE_ID_SIZE,
                       &offset, info->bus, NETWORK_HEX_BYTE_DIGITS);
    network_append_char(out_text->id, NETWORK_INTERFACE_ID_SIZE,
                        &offset, ':');
    network_append_hex(out_text->id, NETWORK_INTERFACE_ID_SIZE,
                       &offset, info->device, NETWORK_HEX_BYTE_DIGITS);
    network_append_char(out_text->id, NETWORK_INTERFACE_ID_SIZE,
                        &offset, '.');
    network_append_hex(out_text->id, NETWORK_INTERFACE_ID_SIZE,
                       &offset, info->function, 1U);
    if (info->transport == NETWORK_TRANSPORT_USB) {
        network_append_text(out_text->id, NETWORK_INTERFACE_ID_SIZE,
                            &offset, "-p");
        network_append_hex(out_text->id, NETWORK_INTERFACE_ID_SIZE,
                           &offset, info->usb_port, 1U);
    }

    if (info->model == NETWORK_ADAPTER_E1000) {
        network_set_text(out_text->name, NETWORK_INTERFACE_NAME_SIZE,
                         "Intel 82540EM (E1000)");
        network_set_text(out_text->driver, NETWORK_DRIVER_NAME_SIZE,
                         info->state == NETWORK_INTERFACE_ACTIVE ?
                         "e1000 (ativo)" :
                         info->state == NETWORK_INTERFACE_DRIVER_ERROR ?
                         "e1000 (erro)" : "e1000 (pendente)");
    } else if (info->model == NETWORK_ADAPTER_RTL8139) {
        network_set_text(out_text->name, NETWORK_INTERFACE_NAME_SIZE,
                         "Realtek RTL8139");
        network_set_text(out_text->driver, NETWORK_DRIVER_NAME_SIZE,
                         info->state == NETWORK_INTERFACE_ACTIVE ?
                         "rtl8139 (ativo)" :
                         info->state == NETWORK_INTERFACE_DRIVER_ERROR ?
                         "rtl8139 (erro)" : "rtl8139 (pendente)");
    } else if (info->model == NETWORK_ADAPTER_RTL8811CU) {
        network_set_text(out_text->name, NETWORK_INTERFACE_NAME_SIZE,
                         "Realtek RTL8811CU (USB)");
        network_set_text(out_text->driver, NETWORK_DRIVER_NAME_SIZE,
                         info->state == NETWORK_INTERFACE_ACTIVE ?
                         "rtl8811cu (ativo)" :
                         info->state == NETWORK_INTERFACE_DRIVER_ERROR ?
                         "rtl8811cu (erro)" : "rtl8811cu (pendente)");
    } else {
        network_set_text(out_text->name, NETWORK_INTERFACE_NAME_SIZE,
                         info->transport == NETWORK_TRANSPORT_USB ?
                         "Controlador de rede USB" :
                         "Controlador de rede PCI");
        network_set_text(out_text->driver, NETWORK_DRIVER_NAME_SIZE,
                         "nao suportado");
    }
    return OK;
}

int network_manager_find(const char* id, network_interface_info_t* out_info) {
    network_interface_text_t text;

    if (!id || !out_info) {
        LOG_ERROR("NET", "Argumento nulo ao buscar interface");
        return ERR_NULL;
    }
    if (!network_manager_initialized) {
        LOG_ERROR("NET", "Busca antes da inicializacao de rede");
        return ERR_STATE;
    }
    for (uint32_t index = 0; index < network_status.interface_count; index++) {
        int result = network_manager_format_text(&network_interfaces[index],
                                                 &text);

        if (result != OK) {
            LOG_ERROR("NET", "Falha ao formatar interface durante busca");
            return result;
        }
        if (network_id_matches(text.id, id)) {
            return network_manager_get_interface(index, out_info);
        }
    }
    LOG_WARN("NET", "Interface de rede solicitada nao encontrada");
    return ERR_NOT_FOUND;
}

int network_manager_send_diagnostic(const char* id) {
    network_interface_info_t info;
    network_interface_text_t text;
    uint8_t destination[NETWORK_MAC_ADDRESS_SIZE];
    static const char payload[] = "ZEPHYROS-S2.3-ETHERNET";
    int result;

    if (!id) {
        LOG_ERROR("NET", "ID nulo para teste de rede");
        return ERR_NULL;
    }
    result = network_manager_find(id, &info);
    if (result != OK) {
        LOG_ERROR("NET", "Interface invalida para teste de rede");
        return result;
    }
    if (info.state != NETWORK_INTERFACE_ACTIVE ||
        !network_status.ethernet_available) {
        LOG_WARN("NET", "Teste solicitado sem interface ativa");
        return ERR_UNAVAILABLE;
    }
    result = network_manager_format_text(&info, &text);
    if (result != OK) return result;
    for (uint32_t index = 0; index < NETWORK_MAC_ADDRESS_SIZE; index++) {
        destination[index] = NETWORK_ETHERNET_BROADCAST_OCTET;
    }
    result = ethernet_send(text.id, destination,
                           NETWORK_DIAGNOSTIC_ETHERTYPE,
                           (const uint8_t*)payload, sizeof(payload) - 1U);
    if (result != OK) {
        LOG_ERROR("NET", "Falha no teste de transmissao Ethernet");
        return result;
    }
    return OK;
}

static int network_cancel_dynamic_clients(uint8_t clear_dns) {
    int result;

    if (network_status.dhcp_available) {
        result = dhcp_reset();
        if (result != OK) {
            LOG_ERROR("NET", "Falha ao cancelar cliente DHCP");
            return result;
        }
    }
    if (network_status.dns_available) {
        result = clear_dns ? dns_unconfigure() : dns_reset();
        if (result != OK) {
            LOG_ERROR("NET", "Falha ao cancelar cliente DNS");
            return result;
        }
    }
    return OK;
}

int network_manager_configure_arp(const char* id, uint32_t local_ip) {
    network_interface_info_t info;
    network_interface_text_t text;
    arp_status_t status;
    ipv4_status_t ipv4_status;
    dhcp_status_t dhcp_status;
    int result;

    if (!id) {
        LOG_ERROR("NET", "ID nulo para configuracao ARP");
        return ERR_NULL;
    }
    result = network_manager_find(id, &info);
    if (result != OK) {
        LOG_ERROR("NET", "Interface invalida para configuracao ARP");
        return result;
    }
    if (info.state != NETWORK_INTERFACE_ACTIVE ||
        !network_status.ethernet_available ||
        !network_status.arp_available) {
        LOG_WARN("NET", "Configuracao ARP sem interface ativa");
        return ERR_UNAVAILABLE;
    }
    if (!arp_ipv4_is_valid(local_ip)) {
        LOG_ERROR("NET", "IPv4 invalido para configuracao ARP");
        return ERR_INVALID;
    }
    result = network_manager_format_text(&info, &text);
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao formatar interface para ARP");
        return result;
    }
    result = ipv4_get_status(&ipv4_status);
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao consultar coerencia IPv4");
        return result;
    }
    if (!ipv4_status.configured && network_status.dhcp_available &&
        dhcp_get_status(&dhcp_status) == OK &&
        (dhcp_status.state == DHCP_STATE_SELECTING ||
         dhcp_status.state == DHCP_STATE_REQUESTING)) {
        result = network_cancel_dynamic_clients(1U);
        if (result != OK) return result;
    }
    if (ipv4_status.configured &&
        (ipv4_status.local_ip != local_ip ||
         !network_id_matches(ipv4_status.interface_id, text.id))) {
        result = network_reset_remote_clients();
        if (result != OK) return result;
        if (network_status.icmp_available) {
            result = icmp_reset();
            if (result != OK) {
                LOG_ERROR("NET", "Falha ao cancelar ICMP para configurar ARP");
                return result;
            }
        }
        result = network_cancel_dynamic_clients(1U);
        if (result != OK) return result;
        result = ipv4_unconfigure();
        if (result != OK) {
            LOG_ERROR("NET", "Falha ao invalidar IPv4 para configurar ARP");
            return result;
        }
        network_status.ipv4_configured = 0;
        network_status.dns_configured = 0;
        network_ipv4_source = NETWORK_IPV4_SOURCE_NONE;
        network_manual_dns_override = 0;
        network_status.l3_interface_id[0] = '\0';
    }
    result = arp_configure(text.id, info.mac_address, local_ip);
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao configurar sessao ARP");
        return result;
    }
    result = arp_get_status(&status);
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao confirmar configuracao ARP");
        return result;
    }
    network_status.arp_configured = status.configured;
    return OK;
}

int network_manager_configure_ipv4(const char* id, uint32_t local_ip,
                                   uint32_t subnet_mask,
                                   uint32_t gateway) {
    network_interface_info_t info;
    network_interface_text_t text;
    ipv4_status_t before;
    ipv4_status_t after;
    uint8_t same_static;
    int result;

    if (!id) {
        LOG_ERROR("NET", "ID nulo para configuracao IPv4");
        return ERR_NULL;
    }
    result = network_manager_find(id, &info);
    if (result != OK) {
        LOG_ERROR("NET", "Interface invalida para configuracao IPv4");
        return result;
    }
    if (info.state != NETWORK_INTERFACE_ACTIVE ||
        !network_status.arp_available ||
        !network_status.ipv4_available ||
        !network_status.icmp_available) {
        LOG_WARN("NET", "Configuracao IPv4 sem pilha ativa");
        return ERR_UNAVAILABLE;
    }
    result = network_manager_format_text(&info, &text);
    if (result != OK || ipv4_get_status(&before) != OK) {
        LOG_ERROR("NET", "Falha ao preparar configuracao IPv4");
        return result != OK ? result : ERR_STATE;
    }
    same_static = before.configured &&
        before.local_ip == local_ip &&
        before.subnet_mask == subnet_mask &&
        before.gateway == gateway &&
        network_id_matches(before.interface_id, text.id) &&
        network_ipv4_source == NETWORK_IPV4_SOURCE_STATIC;
    if (same_static) return OK;
    if (!network_ipv4_parameters_valid(local_ip, subnet_mask,
                                       gateway)) {
        LOG_ERROR("NET", "Parametros IPv4 invalidos");
        return ERR_INVALID;
    }
    result = network_reset_remote_clients();
    if (result != OK) return result;
    result = ipv4_configure(text.id, info.mac_address, local_ip,
                            subnet_mask, gateway);
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao configurar sessao IPv4");
        return result;
    }
    result = ipv4_get_status(&after);
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao confirmar configuracao IPv4");
        return result;
    }
    if (after.configuration_generation !=
            before.configuration_generation ||
        network_ipv4_source != NETWORK_IPV4_SOURCE_STATIC) {
        result = icmp_reset();
        if (result != OK) {
            LOG_ERROR("NET", "Falha ao reiniciar ICMP apos configuracao");
            return result;
        }
        result = network_cancel_dynamic_clients(1U);
        if (result != OK) return result;
    }
    network_ipv4_source = NETWORK_IPV4_SOURCE_STATIC;
    network_manual_dns_override = 0;
    network_status.arp_configured = 1;
    network_status.ipv4_configured = after.configured;
    network_status.dns_configured = 0;
    network_status.dhcp_bound = 0;
    network_status.ipv4_source = network_ipv4_source;
    network_set_text(network_status.l3_interface_id,
                     sizeof(network_status.l3_interface_id), text.id);
    return OK;
}

int network_manager_acquire_dhcp(const char* id) {
    network_interface_info_t info;
    network_interface_text_t text;
    dhcp_status_t dhcp;
    int result;

    if (!id) {
        LOG_ERROR("NET", "ID nulo para aquisicao DHCP");
        return ERR_NULL;
    }
    result = network_manager_find(id, &info);
    if (result != OK) {
        LOG_ERROR("NET", "Interface invalida para aquisicao DHCP");
        return result;
    }
    if (info.state != NETWORK_INTERFACE_ACTIVE ||
        !network_status.arp_available ||
        !network_status.ipv4_available ||
        !network_status.icmp_available ||
        !network_status.udp_available ||
        !network_status.dhcp_available ||
        !network_status.dns_available) {
        LOG_WARN("NET", "Aquisicao DHCP sem pilha ativa");
        return ERR_UNAVAILABLE;
    }
    result = network_manager_format_text(&info, &text);
    if (result != OK || dhcp_get_status(&dhcp) != OK) {
        LOG_ERROR("NET", "Falha ao preparar aquisicao DHCP");
        return result != OK ? result : ERR_STATE;
    }
    if (dhcp.state != DHCP_STATE_IDLE &&
        dhcp.state != DHCP_STATE_FAILED &&
        dhcp.state != DHCP_STATE_EXPIRED) {
        result = dhcp_reset();
        if (result != OK) {
            LOG_ERROR("NET", "Falha ao reiniciar aquisicao DHCP");
            return result;
        }
    }
    result = dhcp_acquire(text.id, info.mac_address);
    if (result != OK) {
        LOG_ERROR("NET", "Cliente DHCP recusou aquisicao");
        return result;
    }
    network_status.dhcp_bound = 0;
    return OK;
}

int network_manager_renew_dhcp(void) {
    int result;

    if (!network_status.dhcp_available ||
        network_ipv4_source != NETWORK_IPV4_SOURCE_DHCP) {
        LOG_ERROR("NET", "Renovacao solicitada sem DHCP ativo");
        return ERR_STATE;
    }
    result = dhcp_renew();
    if (result != OK) {
        LOG_ERROR("NET", "Cliente DHCP recusou renovacao");
        return result;
    }
    return OK;
}

int network_manager_release_dhcp(uint8_t* out_sent) {
    int result;

    if (!out_sent) {
        LOG_ERROR("NET", "Destino nulo na liberacao DHCP");
        return ERR_NULL;
    }
    if (!network_status.dhcp_available) {
        LOG_ERROR("NET", "Liberacao sem cliente DHCP");
        return ERR_UNAVAILABLE;
    }
    result = dhcp_release(out_sent);
    if (result != OK) {
        LOG_ERROR("NET", "Cliente DHCP recusou liberacao");
        return result;
    }
    result = network_process_dhcp_event();
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao aplicar liberacao DHCP");
        return result;
    }
    network_refresh_dynamic_status();
    return OK;
}

int network_manager_configure_dns(uint32_t server_ip) {
    ipv4_status_t ipv4;
    dns_status_t dns;
    http_status_t http;
    int result;

    if (!network_status.dns_available ||
        ipv4_get_status(&ipv4) != OK || !ipv4.configured ||
        dns_get_status(&dns) != OK) {
        LOG_ERROR("NET", "Configuracao DNS sem IPv4 ativo");
        return ERR_STATE;
    }
    if (!network_dns_reachable(ipv4.local_ip, ipv4.subnet_mask,
                               ipv4.gateway, server_ip)) {
        LOG_ERROR("NET", "Servidor DNS invalido ou sem rota");
        return ERR_INVALID;
    }
    if ((!dns.configured || dns.server_ip != server_ip) &&
        network_status.http_available &&
        http_get_status(&http) == OK &&
        http.state != HTTP_STATE_IDLE &&
        http.state != HTTP_STATE_COMPLETE &&
        http.state != HTTP_STATE_FAILED) {
        result = http_reset();
        if (result != OK) {
            LOG_ERROR("NET", "Falha ao cancelar HTTP para trocar DNS");
            return result;
        }
    }
    result = dns_configure(server_ip);
    if (result != OK) {
        LOG_ERROR("NET", "Cliente DNS recusou configuracao");
        return result;
    }
    network_manual_dns_override = 1;
    network_status.dns_configured = 1;
    return OK;
}

int network_manager_get_ethernet_diagnostic(
    const char* id, network_ethernet_diagnostic_t* out_diagnostic) {
    network_interface_info_t info;
    network_interface_text_t text;
    int result;

    if (!id || !out_diagnostic) {
        LOG_ERROR("NET", "Argumento nulo no diagnostico Ethernet");
        return ERR_NULL;
    }
    kmemset(out_diagnostic, 0, sizeof(*out_diagnostic));
    result = network_manager_find(id, &info);
    if (result != OK) {
        LOG_ERROR("NET", "Interface invalida no diagnostico Ethernet");
        return result;
    }
    if (info.state != NETWORK_INTERFACE_ACTIVE ||
        !network_status.ethernet_available) {
        LOG_WARN("NET", "Diagnostico solicitado sem Ethernet ativa");
        return ERR_UNAVAILABLE;
    }
    result = network_manager_poll(&out_diagnostic->processed_now);
    if (result != OK) {
        LOG_ERROR("NET", "Falha no polling do diagnostico Ethernet");
        return result;
    }
    result = network_manager_format_text(&info, &text);
    if (result != OK) return result;
    if (ethernet_get_status(&out_diagnostic->layer) != OK ||
        ethernet_get_interface_status(
            text.id, &out_diagnostic->interface) != OK) {
        LOG_ERROR("NET", "Falha ao obter contadores Ethernet");
        return ERR_STATE;
    }
    out_diagnostic->driver_queue_depth =
        out_diagnostic->interface.driver.rx_queue_depth;
    out_diagnostic->driver_queue_high_water =
        out_diagnostic->interface.driver.rx_queue_high_water;
    out_diagnostic->driver_queue_dropped =
        out_diagnostic->interface.driver.rx_queue_dropped;
    out_diagnostic->driver_rx_interrupts =
        out_diagnostic->interface.driver.rx_interrupts;
    return OK;
}

const char* network_manager_model_name(network_adapter_model_t model) {
    if (model == NETWORK_ADAPTER_E1000) return "E1000";
    if (model == NETWORK_ADAPTER_RTL8139) return "RTL8139";
    if (model == NETWORK_ADAPTER_RTL8811CU) return "RTL8811CU";
    return "DESCONHECIDO";
}

const char* network_manager_interface_state_name(
    network_interface_state_t state) {
    if (state == NETWORK_INTERFACE_DRIVER_MISSING) return "DRIVER AUSENTE";
    if (state == NETWORK_INTERFACE_UNSUPPORTED) return "NAO SUPORTADA";
    if (state == NETWORK_INTERFACE_ACTIVE) return "ATIVA";
    if (state == NETWORK_INTERFACE_DRIVER_ERROR) return "ERRO NO DRIVER";
    return "DESCONHECIDO";
}

const char* network_manager_link_state_name(network_link_state_t state) {
    if (state == NETWORK_LINK_DOWN) return "DOWN";
    if (state == NETWORK_LINK_UP) return "UP";
    return "DESCONHECIDO";
}

const char* network_manager_ipv4_source_name(
    network_ipv4_source_t source) {
    if (source == NETWORK_IPV4_SOURCE_NONE) return "NONE";
    if (source == NETWORK_IPV4_SOURCE_STATIC) return "STATIC";
    if (source == NETWORK_IPV4_SOURCE_DHCP) return "DHCP";
    return "DESCONHECIDO";
}
