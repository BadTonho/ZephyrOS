#include "core/ipv4.h"
#include "core/arp.h"
#include "core/errors.h"
#include "core/ethernet.h"
#include "core/log.h"
#include "core/string.h"

#define IPV4_ETHERTYPE 0x0800U
#define IPV4_VERSION 4U
#define IPV4_IHL_WORDS 5U
#define IPV4_VERSION_SHIFT 4U
#define IPV4_DEFAULT_TTL 64U
#define IPV4_FLAG_RESERVED 0x8000U
#define IPV4_FLAG_DONT_FRAGMENT 0x4000U
#define IPV4_FRAGMENT_MASK 0x3FFFU
#define IPV4_FIRST_OCTET_SHIFT 24U
#define IPV4_LOOPBACK_PREFIX 127U
#define IPV4_MULTICAST_MIN 224U
#define IPV4_MULTICAST_MAX 239U
#define IPV4_BROADCAST 0xFFFFFFFFU
#define IPV4_BROADCAST_MAC_OCTET 0xFFU
#define IPV4_MAC_GROUP_BIT 0x01U
#define IPV4_CHECKSUM_VECTOR_EXPECTED 0x22D8U

#define IPV4_OFFSET_VERSION_IHL 0U
#define IPV4_OFFSET_TOS 1U
#define IPV4_OFFSET_TOTAL_LENGTH 2U
#define IPV4_OFFSET_IDENTIFICATION 4U
#define IPV4_OFFSET_FLAGS_FRAGMENT 6U
#define IPV4_OFFSET_TTL 8U
#define IPV4_OFFSET_PROTOCOL 9U
#define IPV4_OFFSET_CHECKSUM 10U
#define IPV4_OFFSET_SOURCE 12U
#define IPV4_OFFSET_DESTINATION 16U

typedef struct {
    uint8_t active;
    uint8_t protocol;
    ipv4_protocol_handler_fn handler;
} ipv4_protocol_entry_t;

static ipv4_status_t ipv4_status;
static ipv4_protocol_entry_t
    ipv4_handlers[IPV4_PROTOCOL_HANDLER_CAPACITY];
static uint8_t ipv4_tx_buffer[IPV4_MTU];

static uint16_t ipv4_read_u16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

static uint32_t ipv4_read_u32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) |
           data[3];
}

static void ipv4_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

static void ipv4_write_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static uint16_t ipv4_checksum(const uint8_t* data, uint16_t length) {
    uint32_t sum = 0;

    while (length >= 2U) {
        sum += ipv4_read_u16(data);
        data += 2U;
        length -= 2U;
    }
    if (length) sum += (uint16_t)((uint16_t)data[0] << 8U);
    while (sum >> 16U) sum = (sum & 0xFFFFU) + (sum >> 16U);
    return (uint16_t)(~sum);
}

static uint8_t ipv4_mac_is_valid(const uint8_t* mac_address) {
    uint8_t nonzero = 0;

    if (!mac_address) return 0;
    for (uint32_t index = 0; index < IPV4_MAC_ADDRESS_SIZE; index++) {
        if (mac_address[index]) nonzero = 1;
    }
    return nonzero && !(mac_address[0] & IPV4_MAC_GROUP_BIT);
}

static uint8_t ipv4_mac_is_equal(const uint8_t* first,
                                 const uint8_t* second) {
    for (uint32_t index = 0; index < IPV4_MAC_ADDRESS_SIZE; index++) {
        if (first[index] != second[index]) return 0;
    }
    return 1;
}

static uint8_t ipv4_text_is_equal(const char* first, const char* second) {
    if (!first || !second) return 0;
    while (*first && *second && *first == *second) {
        first++;
        second++;
    }
    return *first == '\0' && *second == '\0';
}

static int ipv4_copy_text(char* destination, uint32_t capacity,
                          const char* source) {
    uint32_t length = 0;

    if (!destination || !source || !capacity) {
        LOG_ERROR("NET", "Texto IPv4 invalido");
        return ERR_NULL;
    }
    while (source[length]) {
        if (length + 1U >= capacity) {
            LOG_ERROR("NET", "ID de interface excede limite IPv4");
            return ERR_OVERFLOW;
        }
        destination[length] = source[length];
        length++;
    }
    destination[length] = '\0';
    if (!length) {
        LOG_ERROR("NET", "ID vazio na configuracao IPv4");
        return ERR_INVALID;
    }
    return OK;
}

uint8_t ipv4_address_is_unicast(uint32_t ip_address) {
    uint8_t first_octet =
        (uint8_t)(ip_address >> IPV4_FIRST_OCTET_SHIFT);

    if (!ip_address || ip_address == IPV4_BROADCAST) return 0;
    if (first_octet == IPV4_LOOPBACK_PREFIX) return 0;
    return first_octet < IPV4_MULTICAST_MIN ||
           first_octet > IPV4_MULTICAST_MAX;
}

uint8_t ipv4_mask_is_valid(uint32_t subnet_mask) {
    uint8_t saw_zero = 0;
    uint8_t prefix = 0;

    for (uint32_t bit = 0; bit < 32U; bit++) {
        uint32_t value = subnet_mask & (0x80000000U >> bit);

        if (value) {
            if (saw_zero) return 0;
            prefix++;
        } else {
            saw_zero = 1;
        }
    }
    return prefix >= 1U && prefix <= 30U;
}

static uint8_t ipv4_config_is_valid(uint32_t local_ip,
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

static ipv4_protocol_handler_fn ipv4_find_handler(uint8_t protocol) {
    for (uint32_t index = 0;
         index < IPV4_PROTOCOL_HANDLER_CAPACITY; index++) {
        if (ipv4_handlers[index].active &&
            ipv4_handlers[index].protocol == protocol) {
            return ipv4_handlers[index].handler;
        }
    }
    return NULL;
}

static void ipv4_dispatch(const char* interface_id,
                          const uint8_t* packet, uint16_t total_length,
                          ipv4_delivery_t delivery) {
    ipv4_packet_view_t view;
    ipv4_protocol_handler_fn handler;
    int result;

    view.interface_id = interface_id;
    view.payload = packet + IPV4_HEADER_SIZE;
    view.payload_length = total_length - IPV4_HEADER_SIZE;
    view.source_ip = ipv4_read_u32(packet + IPV4_OFFSET_SOURCE);
    view.destination_ip =
        ipv4_read_u32(packet + IPV4_OFFSET_DESTINATION);
    view.identification =
        ipv4_read_u16(packet + IPV4_OFFSET_IDENTIFICATION);
    view.protocol = packet[IPV4_OFFSET_PROTOCOL];
    view.ttl = packet[IPV4_OFFSET_TTL];
    view.delivery = delivery;
    handler = ipv4_find_handler(view.protocol);
    if (!handler) {
        ipv4_status.rx_unhandled++;
        return;
    }
    result = handler(&view);
    if (result == OK) {
        ipv4_status.rx_delivered++;
        return;
    }
    ipv4_status.rx_protocol_errors++;
    ipv4_status.last_error = result;
    LOG_WARN("NET", "Protocolo IPv4 recusou datagrama recebido");
}

static uint8_t ipv4_header_is_supported(const uint8_t* packet,
                                        uint16_t length,
                                        uint16_t* out_total_length) {
    uint8_t version = packet[IPV4_OFFSET_VERSION_IHL] >>
                      IPV4_VERSION_SHIFT;
    uint8_t ihl = packet[IPV4_OFFSET_VERSION_IHL] & 0x0FU;
    uint16_t flags_fragment;

    if (version != IPV4_VERSION || ihl < IPV4_IHL_WORDS) return 0;
    if (ihl != IPV4_IHL_WORDS) {
        ipv4_status.rx_options++;
        return 0;
    }
    *out_total_length =
        ipv4_read_u16(packet + IPV4_OFFSET_TOTAL_LENGTH);
    if (*out_total_length < IPV4_HEADER_SIZE ||
        *out_total_length > length ||
        packet[IPV4_OFFSET_TTL] == 0U) return 0;
    flags_fragment =
        ipv4_read_u16(packet + IPV4_OFFSET_FLAGS_FRAGMENT);
    if (flags_fragment & IPV4_FLAG_RESERVED) return 0;
    if (flags_fragment & IPV4_FRAGMENT_MASK) {
        ipv4_status.rx_fragments++;
        return 0;
    }
    return 1;
}

static int ipv4_handle_frame(const ethernet_frame_view_t* frame) {
    const uint8_t* packet;
    uint32_t source_ip;
    uint32_t destination_ip;
    uint16_t total_length = 0;
    ipv4_delivery_t delivery;

    if (!frame || !frame->interface_id ||
        frame->ethertype != IPV4_ETHERTYPE ||
        frame->payload_length < IPV4_HEADER_SIZE) {
        ipv4_status.rx_invalid++;
        return OK;
    }
    packet = frame->payload;
    if (!ipv4_header_is_supported(packet, frame->payload_length,
                                  &total_length)) {
        ipv4_status.rx_invalid++;
        return OK;
    }
    if (ipv4_checksum(packet, IPV4_HEADER_SIZE) != 0U) {
        ipv4_status.rx_checksum_errors++;
        return OK;
    }
    source_ip = ipv4_read_u32(packet + IPV4_OFFSET_SOURCE);
    destination_ip = ipv4_read_u32(packet + IPV4_OFFSET_DESTINATION);
    if (frame->destination_type == ETHERNET_DESTINATION_BROADCAST &&
        destination_ip == IPV4_LIMITED_BROADCAST &&
        packet[IPV4_OFFSET_PROTOCOL] == IPV4_PROTOCOL_UDP) {
        delivery = IPV4_DELIVERY_LIMITED_BROADCAST;
    } else if (ipv4_status.configured &&
               ipv4_text_is_equal(frame->interface_id,
                                  ipv4_status.interface_id) &&
               frame->destination_type ==
                   ETHERNET_DESTINATION_LOCAL_UNICAST &&
               destination_ip == ipv4_status.local_ip) {
        delivery = IPV4_DELIVERY_LOCAL_UNICAST;
    } else {
        ipv4_status.rx_ignored++;
        return OK;
    }
    if (!ipv4_address_is_unicast(source_ip) ||
        (ipv4_status.configured &&
         source_ip == ipv4_status.local_ip)) {
        ipv4_status.rx_invalid++;
        return OK;
    }
    ipv4_status.rx_packets++;
    ipv4_status.rx_bytes += total_length;
    if (delivery == IPV4_DELIVERY_LIMITED_BROADCAST) {
        ipv4_status.rx_limited_broadcast++;
    }
    ipv4_status.last_error = OK;
    ipv4_dispatch(frame->interface_id, packet, total_length, delivery);
    return OK;
}

static void ipv4_build_packet(uint32_t source_ip,
                              uint32_t destination_ip, uint8_t protocol,
                              const uint8_t* payload,
                              uint16_t payload_length) {
    uint16_t total_length = IPV4_HEADER_SIZE + payload_length;
    uint16_t identification = ipv4_status.next_identification;
    uint16_t checksum;

    kmemset(ipv4_tx_buffer, 0, total_length);
    ipv4_tx_buffer[IPV4_OFFSET_VERSION_IHL] =
        (uint8_t)((IPV4_VERSION << IPV4_VERSION_SHIFT) | IPV4_IHL_WORDS);
    ipv4_tx_buffer[IPV4_OFFSET_TOS] = 0U;
    ipv4_write_u16(ipv4_tx_buffer + IPV4_OFFSET_TOTAL_LENGTH,
                   total_length);
    ipv4_write_u16(ipv4_tx_buffer + IPV4_OFFSET_IDENTIFICATION,
                   identification);
    ipv4_write_u16(ipv4_tx_buffer + IPV4_OFFSET_FLAGS_FRAGMENT,
                   IPV4_FLAG_DONT_FRAGMENT);
    ipv4_tx_buffer[IPV4_OFFSET_TTL] = IPV4_DEFAULT_TTL;
    ipv4_tx_buffer[IPV4_OFFSET_PROTOCOL] = protocol;
    ipv4_write_u32(ipv4_tx_buffer + IPV4_OFFSET_SOURCE,
                   source_ip);
    ipv4_write_u32(ipv4_tx_buffer + IPV4_OFFSET_DESTINATION,
                   destination_ip);
    if (payload_length) {
        kmemcpy(ipv4_tx_buffer + IPV4_HEADER_SIZE,
                payload, payload_length);
    }
    checksum = ipv4_checksum(ipv4_tx_buffer, IPV4_HEADER_SIZE);
    ipv4_write_u16(ipv4_tx_buffer + IPV4_OFFSET_CHECKSUM, checksum);
    ipv4_status.next_identification++;
    if (!ipv4_status.next_identification) {
        ipv4_status.next_identification = 1U;
    }
}

static int ipv4_select_next_hop(uint32_t destination_ip,
                                uint32_t* out_next_hop,
                                uint8_t* out_via_gateway) {
    uint32_t network = ipv4_status.local_ip & ipv4_status.subnet_mask;
    uint32_t broadcast = network | ~ipv4_status.subnet_mask;

    if (!out_next_hop || !out_via_gateway) {
        LOG_ERROR("NET", "Destino nulo ao selecionar rota IPv4");
        return ERR_NULL;
    }
    if (!ipv4_address_is_unicast(destination_ip) ||
        destination_ip == ipv4_status.local_ip ||
        destination_ip == network || destination_ip == broadcast) {
        LOG_ERROR("NET", "Destino IPv4 invalido para transmissao");
        return ERR_INVALID;
    }
    *out_via_gateway = 0;
    if ((destination_ip & ipv4_status.subnet_mask) == network) {
        *out_next_hop = destination_ip;
        return OK;
    }
    if (!ipv4_status.gateway) {
        LOG_WARN("NET", "Destino IPv4 externo sem gateway");
        return ERR_UNAVAILABLE;
    }
    *out_next_hop = ipv4_status.gateway;
    *out_via_gateway = 1;
    return OK;
}

int ipv4_init(void) {
    int result;

    LOG_INFO("NET", "Inicializando protocolo IPv4");
    if (ipv4_status.initialized) {
        LOG_WARN("NET", "Protocolo IPv4 ja estava inicializado");
        LOG_INFO("NET", "Protocolo IPv4 inicializado com sucesso");
        return OK;
    }
    kmemset(&ipv4_status, 0, sizeof(ipv4_status));
    kmemset(ipv4_handlers, 0, sizeof(ipv4_handlers));
    result = ethernet_register_handler(IPV4_ETHERTYPE, ipv4_handle_frame);
    if (result != OK) {
        ipv4_status.last_error = result;
        LOG_ERROR("NET", "Falha ao registrar protocolo IPv4");
        return result;
    }
    ipv4_status.initialized = 1;
    ipv4_status.next_identification = 1U;
    ipv4_status.last_error = OK;
    LOG_INFO("NET", "Protocolo IPv4 inicializado com sucesso");
    return OK;
}

int ipv4_configure(const char* interface_id, const uint8_t* local_mac,
                   uint32_t local_ip, uint32_t subnet_mask,
                   uint32_t gateway) {
    char validated_id[IPV4_INTERFACE_ID_SIZE];
    uint32_t next_generation;
    uint8_t handler_count;
    int result;

    if (!ipv4_status.initialized) {
        LOG_ERROR("NET", "Configuracao IPv4 antes da inicializacao");
        return ERR_STATE;
    }
    if (!interface_id || !local_mac) {
        LOG_ERROR("NET", "Argumento nulo na configuracao IPv4");
        return ERR_NULL;
    }
    if (!ipv4_mac_is_valid(local_mac) ||
        !ipv4_config_is_valid(local_ip, subnet_mask, gateway)) {
        LOG_ERROR("NET", "Endereco invalido na configuracao IPv4");
        return ERR_INVALID;
    }
    kmemset(validated_id, 0, sizeof(validated_id));
    result = ipv4_copy_text(validated_id, sizeof(validated_id),
                            interface_id);
    if (result != OK) return result;
    if (ipv4_status.configured && ipv4_status.local_ip == local_ip &&
        ipv4_status.subnet_mask == subnet_mask &&
        ipv4_status.gateway == gateway &&
        ipv4_text_is_equal(ipv4_status.interface_id, validated_id) &&
        ipv4_mac_is_equal(ipv4_status.local_mac, local_mac)) return OK;
    result = arp_configure(validated_id, local_mac, local_ip);
    if (result != OK) {
        LOG_ERROR("NET", "ARP recusou configuracao IPv4");
        return result;
    }
    result = arp_clear();
    if (result != OK) {
        LOG_ERROR("NET", "Falha ao limpar ARP na configuracao IPv4");
        return result;
    }
    next_generation = ipv4_status.configuration_generation + 1U;
    handler_count = ipv4_status.handler_count;
    kmemset(&ipv4_status, 0, sizeof(ipv4_status));
    ipv4_status.initialized = 1;
    ipv4_status.configured = 1;
    ipv4_status.handler_count = handler_count;
    ipv4_status.local_ip = local_ip;
    ipv4_status.subnet_mask = subnet_mask;
    ipv4_status.gateway = gateway;
    ipv4_status.configuration_generation = next_generation;
    ipv4_status.next_identification = 1U;
    kmemcpy(ipv4_status.local_mac, local_mac, IPV4_MAC_ADDRESS_SIZE);
    kmemcpy(ipv4_status.interface_id, validated_id, sizeof(validated_id));
    ipv4_status.last_error = OK;
    LOG_INFO("NET", "Sessao IPv4 configurada em memoria");
    return OK;
}

int ipv4_unconfigure(void) {
    uint32_t next_generation;
    uint8_t handler_count;

    if (!ipv4_status.initialized) {
        LOG_ERROR("NET", "Remocao IPv4 antes da inicializacao");
        return ERR_STATE;
    }
    next_generation = ipv4_status.configuration_generation + 1U;
    handler_count = ipv4_status.handler_count;
    kmemset(&ipv4_status, 0, sizeof(ipv4_status));
    ipv4_status.initialized = 1;
    ipv4_status.handler_count = handler_count;
    ipv4_status.configuration_generation = next_generation;
    ipv4_status.next_identification = 1U;
    ipv4_status.last_error = OK;
    LOG_INFO("NET", "Configuracao IPv4 removida da memoria");
    return OK;
}

int ipv4_register_handler(uint8_t protocol,
                          ipv4_protocol_handler_fn handler) {
    int32_t free_index = -1;

    if (!ipv4_status.initialized) {
        LOG_ERROR("NET", "Registro de protocolo antes do IPv4");
        return ERR_STATE;
    }
    if (!handler) {
        LOG_ERROR("NET", "Handler IPv4 invalido");
        return ERR_NULL;
    }
    for (uint32_t index = 0;
         index < IPV4_PROTOCOL_HANDLER_CAPACITY; index++) {
        if (!ipv4_handlers[index].active && free_index < 0) {
            free_index = (int32_t)index;
        } else if (ipv4_handlers[index].active &&
                   ipv4_handlers[index].protocol == protocol) {
            if (ipv4_handlers[index].handler == handler) return OK;
            LOG_ERROR("NET", "Protocolo IPv4 ja possui outro handler");
            return ERR_STATE;
        }
    }
    if (free_index < 0) {
        LOG_ERROR("NET", "Tabela de protocolos IPv4 cheia");
        return ERR_OVERFLOW;
    }
    ipv4_handlers[free_index].active = 1;
    ipv4_handlers[free_index].protocol = protocol;
    ipv4_handlers[free_index].handler = handler;
    ipv4_status.handler_count++;
    return OK;
}

int ipv4_send(uint32_t destination_ip, uint8_t protocol,
              const uint8_t* payload, uint16_t payload_length,
              uint8_t* out_sent) {
    uint8_t next_hop_mac[IPV4_MAC_ADDRESS_SIZE];
    uint32_t next_hop;
    uint8_t via_gateway;
    uint8_t resolved = 0;
    int result;

    if (!out_sent || (payload_length && !payload)) {
        LOG_ERROR("NET", "Argumento nulo na transmissao IPv4");
        return ERR_NULL;
    }
    *out_sent = 0;
    if (!ipv4_status.initialized || !ipv4_status.configured) {
        LOG_ERROR("NET", "Transmissao IPv4 antes da configuracao");
        return ERR_STATE;
    }
    if (payload_length > IPV4_MAX_PAYLOAD_SIZE) {
        LOG_ERROR("NET", "Payload IPv4 excede MTU");
        return ERR_INVALID;
    }
    result = ipv4_select_next_hop(destination_ip, &next_hop,
                                  &via_gateway);
    if (result != OK) return result;
    result = arp_resolve(next_hop, next_hop_mac, &resolved);
    if (result != OK) {
        ipv4_status.last_error = result;
        LOG_WARN("NET", "Resolucao do proximo salto IPv4 falhou");
        return result;
    }
    if (!resolved) return OK;
    ipv4_build_packet(ipv4_status.local_ip, destination_ip, protocol,
                      payload, payload_length);
    result = ethernet_send(ipv4_status.interface_id, next_hop_mac,
                           IPV4_ETHERTYPE, ipv4_tx_buffer,
                           IPV4_HEADER_SIZE + payload_length);
    if (result != OK) {
        ipv4_status.last_error = result;
        LOG_ERROR("NET", "Falha ao transmitir datagrama IPv4");
        return result;
    }
    ipv4_status.tx_packets++;
    ipv4_status.tx_bytes += IPV4_HEADER_SIZE + payload_length;
    if (via_gateway) ipv4_status.tx_via_gateway++;
    else ipv4_status.tx_direct++;
    ipv4_status.last_error = OK;
    *out_sent = 1;
    return OK;
}

int ipv4_send_limited_broadcast(const char* interface_id,
                                uint32_t source_ip, uint8_t protocol,
                                const uint8_t* payload,
                                uint16_t payload_length,
                                uint8_t* out_sent) {
    uint8_t destination_mac[IPV4_MAC_ADDRESS_SIZE];
    int result;

    if (!interface_id || !out_sent || (payload_length && !payload)) {
        LOG_ERROR("NET", "Argumento nulo no broadcast IPv4");
        return ERR_NULL;
    }
    *out_sent = 0;
    if (!ipv4_status.initialized) {
        LOG_ERROR("NET", "Broadcast IPv4 antes da inicializacao");
        return ERR_STATE;
    }
    if (!interface_id[0] || protocol != IPV4_PROTOCOL_UDP ||
        payload_length > IPV4_MAX_PAYLOAD_SIZE ||
        (source_ip &&
         (!ipv4_status.configured ||
          source_ip != ipv4_status.local_ip ||
          !ipv4_text_is_equal(interface_id,
                              ipv4_status.interface_id)))) {
        LOG_ERROR("NET", "Parametros invalidos no broadcast IPv4");
        return ERR_INVALID;
    }
    kmemset(destination_mac, IPV4_BROADCAST_MAC_OCTET,
            sizeof(destination_mac));
    ipv4_build_packet(source_ip, IPV4_LIMITED_BROADCAST, protocol,
                      payload, payload_length);
    result = ethernet_send(interface_id, destination_mac, IPV4_ETHERTYPE,
                           ipv4_tx_buffer,
                           IPV4_HEADER_SIZE + payload_length);
    if (result != OK) {
        ipv4_status.last_error = result;
        LOG_ERROR("NET", "Falha ao transmitir broadcast IPv4");
        return result;
    }
    ipv4_status.tx_packets++;
    ipv4_status.tx_bytes += IPV4_HEADER_SIZE + payload_length;
    ipv4_status.tx_limited_broadcast++;
    ipv4_status.last_error = OK;
    *out_sent = 1;
    return OK;
}

int ipv4_get_status(ipv4_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("NET", "Destino nulo ao consultar IPv4");
        return ERR_NULL;
    }
    *out_status = ipv4_status;
    return OK;
}

static int ipv4_validate_checksum_vector(void) {
    uint8_t header[IPV4_HEADER_SIZE];
    uint16_t checksum;

    kmemset(header, 0, sizeof(header));
    header[IPV4_OFFSET_VERSION_IHL] =
        (uint8_t)((IPV4_VERSION << IPV4_VERSION_SHIFT) | IPV4_IHL_WORDS);
    ipv4_write_u16(header + IPV4_OFFSET_TOTAL_LENGTH, IPV4_HEADER_SIZE);
    ipv4_write_u16(header + IPV4_OFFSET_IDENTIFICATION, 1U);
    ipv4_write_u16(header + IPV4_OFFSET_FLAGS_FRAGMENT,
                   IPV4_FLAG_DONT_FRAGMENT);
    header[IPV4_OFFSET_TTL] = IPV4_DEFAULT_TTL;
    header[IPV4_OFFSET_PROTOCOL] = IPV4_PROTOCOL_ICMP;
    ipv4_write_u32(header + IPV4_OFFSET_SOURCE, 0x0A00020FU);
    ipv4_write_u32(header + IPV4_OFFSET_DESTINATION, 0x0A000202U);
    checksum = ipv4_checksum(header, sizeof(header));
    if (checksum != IPV4_CHECKSUM_VECTOR_EXPECTED) {
        LOG_ERROR("NET", "Resultado do vetor checksum IPv4 incorreto");
        return ERR_STATE;
    }
    ipv4_write_u16(header + IPV4_OFFSET_CHECKSUM, checksum);
    if (ipv4_checksum(header, sizeof(header)) != 0U) {
        LOG_ERROR("NET", "Vetor de checksum IPv4 falhou");
        return ERR_STATE;
    }
    return OK;
}

static int ipv4_validate_handlers(void) {
    uint32_t count = 0;

    for (uint32_t index = 0;
         index < IPV4_PROTOCOL_HANDLER_CAPACITY; index++) {
        if (!ipv4_handlers[index].active) continue;
        if (!ipv4_handlers[index].handler) {
            LOG_ERROR("NET", "Tabela de handlers IPv4 inconsistente");
            return ERR_STATE;
        }
        count++;
        for (uint32_t other = index + 1U;
             other < IPV4_PROTOCOL_HANDLER_CAPACITY; other++) {
            if (ipv4_handlers[other].active &&
                ipv4_handlers[other].protocol ==
                    ipv4_handlers[index].protocol) {
                LOG_ERROR("NET", "Handler IPv4 duplicado");
                return ERR_STATE;
            }
        }
    }
    if (count != ipv4_status.handler_count) {
        LOG_ERROR("NET", "Contagem de handlers IPv4 inconsistente");
        return ERR_STATE;
    }
    return OK;
}

int ipv4_validate_state(void) {
    arp_status_t arp_status;

    if (ipv4_validate_checksum_vector() != OK ||
        ipv4_validate_handlers() != OK) return ERR_STATE;
    if (!ipv4_status.initialized) {
        if (ipv4_status.configured || ipv4_status.handler_count) {
            LOG_ERROR("NET", "IPv4 configurado sem inicializacao");
            return ERR_STATE;
        }
        return OK;
    }
    if (!ipv4_status.next_identification ||
        ipv4_status.handler_count > IPV4_PROTOCOL_HANDLER_CAPACITY) {
        LOG_ERROR("NET", "Estado basico IPv4 inconsistente");
        return ERR_STATE;
    }
    if (!ipv4_status.configured) return OK;
    if (!ipv4_status.interface_id[0] ||
        !ipv4_mac_is_valid(ipv4_status.local_mac) ||
        !ipv4_config_is_valid(ipv4_status.local_ip,
                              ipv4_status.subnet_mask,
                              ipv4_status.gateway) ||
        arp_get_status(&arp_status) != OK || !arp_status.configured ||
        arp_status.local_ip != ipv4_status.local_ip ||
        !ipv4_text_is_equal(arp_status.interface_id,
                            ipv4_status.interface_id) ||
        !ipv4_mac_is_equal(arp_status.local_mac,
                           ipv4_status.local_mac)) {
        LOG_ERROR("NET", "Configuracao IPv4 inconsistente");
        return ERR_STATE;
    }
    return OK;
}

const char* ipv4_protocol_name(uint8_t protocol) {
    if (protocol == IPV4_PROTOCOL_ICMP) return "ICMP";
    if (protocol == IPV4_PROTOCOL_TCP) return "TCP";
    if (protocol == IPV4_PROTOCOL_UDP) return "UDP";
    return "DESCONHECIDO";
}
