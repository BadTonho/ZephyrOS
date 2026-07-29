#include "core/udp.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"

#define UDP_OFFSET_SOURCE_PORT 0U
#define UDP_OFFSET_DESTINATION_PORT 2U
#define UDP_OFFSET_LENGTH 4U
#define UDP_OFFSET_CHECKSUM 6U
#define UDP_HANDLE_SLOT_MASK 0xFFU
#define UDP_HANDLE_GENERATION_SHIFT 8U
#define UDP_HANDLE_GENERATION_MAX 0x00FFFFFFU
#define UDP_CHECKSUM_VECTOR_EXPECTED 0x5E7DU

typedef struct {
    uint8_t active;
    uint8_t flags;
    uint16_t local_port;
    uint32_t generation;
    udp_receive_fn callback;
} udp_endpoint_t;

static udp_status_t udp_status;
static udp_endpoint_t udp_endpoints[UDP_ENDPOINT_CAPACITY];
static uint32_t udp_generations[UDP_ENDPOINT_CAPACITY];
static uint8_t udp_tx_buffer[IPV4_MAX_PAYLOAD_SIZE];

static uint16_t udp_read_u16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

static void udp_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

static uint32_t udp_add_bytes(uint32_t sum, const uint8_t* data,
                              uint16_t length) {
    while (length >= 2U) {
        sum += udp_read_u16(data);
        data += 2U;
        length -= 2U;
    }
    if (length) sum += (uint16_t)((uint16_t)data[0] << 8U);
    return sum;
}

static uint16_t udp_checksum(uint32_t source_ip, uint32_t destination_ip,
                             const uint8_t* segment,
                             uint16_t segment_length) {
    uint32_t sum = 0;

    sum += (uint16_t)(source_ip >> 16U);
    sum += (uint16_t)source_ip;
    sum += (uint16_t)(destination_ip >> 16U);
    sum += (uint16_t)destination_ip;
    sum += IPV4_PROTOCOL_UDP;
    sum += segment_length;
    sum = udp_add_bytes(sum, segment, segment_length);
    while (sum >> 16U) sum = (sum & 0xFFFFU) + (sum >> 16U);
    return (uint16_t)(~sum);
}

static int32_t udp_find_port(uint16_t port) {
    for (uint32_t index = 0; index < UDP_ENDPOINT_CAPACITY; index++) {
        if (udp_endpoints[index].active &&
            udp_endpoints[index].local_port == port) {
            return (int32_t)index;
        }
    }
    return -1;
}

static int32_t udp_find_handle(udp_endpoint_handle_t handle) {
    uint32_t slot = handle & UDP_HANDLE_SLOT_MASK;
    uint32_t generation = handle >> UDP_HANDLE_GENERATION_SHIFT;

    if (!slot || slot > UDP_ENDPOINT_CAPACITY || !generation) return -1;
    slot--;
    if (!udp_endpoints[slot].active ||
        udp_endpoints[slot].generation != generation) return -1;
    return (int32_t)slot;
}

static int udp_deliver(const ipv4_packet_view_t* packet,
                       uint16_t source_port, uint16_t destination_port,
                       uint16_t udp_length) {
    udp_datagram_view_t view;
    int32_t index = udp_find_port(destination_port);
    int result;

    if (index < 0 ||
        (packet->delivery == IPV4_DELIVERY_LIMITED_BROADCAST &&
         !(udp_endpoints[index].flags & UDP_BIND_ALLOW_BROADCAST))) {
        udp_status.rx_no_listener++;
        return OK;
    }
    view.interface_id = packet->interface_id;
    view.payload = packet->payload + UDP_HEADER_SIZE;
    view.payload_length = udp_length - UDP_HEADER_SIZE;
    view.source_ip = packet->source_ip;
    view.destination_ip = packet->destination_ip;
    view.source_port = source_port;
    view.destination_port = destination_port;
    view.delivery = packet->delivery;
    result = udp_endpoints[index].callback(&view);
    if (result != OK) {
        udp_status.rx_protocol_errors++;
        udp_status.last_error = result;
        LOG_WARN("NET", "Endpoint UDP recusou datagrama");
        return result;
    }
    udp_status.rx_delivered++;
    return OK;
}

static int udp_handle_ipv4(const ipv4_packet_view_t* packet) {
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t udp_length;
    uint16_t received_checksum;

    if (!packet || packet->protocol != IPV4_PROTOCOL_UDP) {
        udp_status.rx_invalid++;
        return OK;
    }
    if (packet->payload_length < UDP_HEADER_SIZE) {
        udp_status.rx_length_errors++;
        return OK;
    }
    source_port = udp_read_u16(packet->payload + UDP_OFFSET_SOURCE_PORT);
    destination_port =
        udp_read_u16(packet->payload + UDP_OFFSET_DESTINATION_PORT);
    udp_length = udp_read_u16(packet->payload + UDP_OFFSET_LENGTH);
    if (!source_port || !destination_port) {
        udp_status.rx_invalid++;
        return OK;
    }
    if (udp_length < UDP_HEADER_SIZE ||
        udp_length != packet->payload_length) {
        udp_status.rx_length_errors++;
        return OK;
    }
    received_checksum =
        udp_read_u16(packet->payload + UDP_OFFSET_CHECKSUM);
    if (received_checksum &&
        udp_checksum(packet->source_ip, packet->destination_ip,
                     packet->payload, udp_length) != 0U) {
        udp_status.rx_checksum_errors++;
        return OK;
    }
    udp_status.rx_datagrams++;
    udp_status.rx_bytes += udp_length - UDP_HEADER_SIZE;
    if (packet->delivery == IPV4_DELIVERY_LIMITED_BROADCAST) {
        udp_status.rx_broadcast++;
    }
    udp_status.last_error = OK;
    return udp_deliver(packet, source_port, destination_port, udp_length);
}

int udp_init(void) {
    int result;

    LOG_INFO("NET", "Inicializando protocolo UDP");
    if (udp_status.initialized) {
        LOG_WARN("NET", "Protocolo UDP ja estava inicializado");
        LOG_INFO("NET", "Protocolo UDP inicializado com sucesso");
        return OK;
    }
    kmemset(&udp_status, 0, sizeof(udp_status));
    kmemset(udp_endpoints, 0, sizeof(udp_endpoints));
    kmemset(udp_generations, 0, sizeof(udp_generations));
    result = ipv4_register_handler(IPV4_PROTOCOL_UDP, udp_handle_ipv4);
    if (result != OK) {
        udp_status.last_error = result;
        LOG_ERROR("NET", "Falha ao registrar protocolo UDP");
        return result;
    }
    udp_status.initialized = 1;
    udp_status.last_error = OK;
    LOG_INFO("NET", "Protocolo UDP inicializado com sucesso");
    return OK;
}

int udp_bind(uint16_t local_port, uint8_t flags,
             udp_receive_fn callback,
             udp_endpoint_handle_t* out_handle) {
    int32_t free_index = -1;
    uint32_t generation;

    if (!callback || !out_handle) {
        LOG_ERROR("NET", "Argumento nulo ao vincular endpoint UDP");
        return ERR_NULL;
    }
    *out_handle = 0;
    if (!udp_status.initialized) {
        LOG_ERROR("NET", "Bind UDP antes da inicializacao");
        return ERR_STATE;
    }
    if (!local_port || (flags & ~UDP_BIND_ALLOW_BROADCAST)) {
        LOG_ERROR("NET", "Parametros invalidos no bind UDP");
        return ERR_INVALID;
    }
    if (udp_find_port(local_port) >= 0) {
        LOG_ERROR("NET", "Porta UDP ja vinculada");
        return ERR_STATE;
    }
    for (uint32_t index = 0; index < UDP_ENDPOINT_CAPACITY; index++) {
        if (!udp_endpoints[index].active) {
            free_index = (int32_t)index;
            break;
        }
    }
    if (free_index < 0) {
        LOG_ERROR("NET", "Tabela de endpoints UDP cheia");
        return ERR_OVERFLOW;
    }
    generation = udp_generations[free_index] + 1U;
    if (!generation || generation > UDP_HANDLE_GENERATION_MAX) {
        generation = 1U;
    }
    udp_generations[free_index] = generation;
    udp_endpoints[free_index].active = 1;
    udp_endpoints[free_index].flags = flags;
    udp_endpoints[free_index].local_port = local_port;
    udp_endpoints[free_index].generation = generation;
    udp_endpoints[free_index].callback = callback;
    udp_status.endpoint_count++;
    *out_handle =
        (generation << UDP_HANDLE_GENERATION_SHIFT) |
        ((uint32_t)free_index + 1U);
    return OK;
}

int udp_unbind(udp_endpoint_handle_t handle) {
    int32_t index;

    if (!udp_status.initialized) {
        LOG_ERROR("NET", "Unbind UDP antes da inicializacao");
        return ERR_STATE;
    }
    index = udp_find_handle(handle);
    if (index < 0) {
        LOG_ERROR("NET", "Endpoint UDP invalido no unbind");
        return ERR_INVALID;
    }
    kmemset(&udp_endpoints[index], 0, sizeof(udp_endpoints[index]));
    udp_status.endpoint_count--;
    return OK;
}

static int udp_build_segment(uint16_t source_port,
                             uint16_t destination_port,
                             uint32_t source_ip,
                             uint32_t destination_ip,
                             const uint8_t* payload,
                             uint16_t payload_length,
                             uint16_t* out_length) {
    uint16_t segment_length = UDP_HEADER_SIZE + payload_length;
    uint16_t checksum;

    if (!out_length || (payload_length && !payload)) {
        LOG_ERROR("NET", "Argumento nulo ao montar UDP");
        return ERR_NULL;
    }
    if (!source_port || !destination_port ||
        payload_length > UDP_MAX_PAYLOAD_SIZE) {
        LOG_ERROR("NET", "Parametros invalidos ao montar UDP");
        return ERR_INVALID;
    }
    kmemset(udp_tx_buffer, 0, segment_length);
    udp_write_u16(udp_tx_buffer + UDP_OFFSET_SOURCE_PORT, source_port);
    udp_write_u16(udp_tx_buffer + UDP_OFFSET_DESTINATION_PORT,
                  destination_port);
    udp_write_u16(udp_tx_buffer + UDP_OFFSET_LENGTH, segment_length);
    if (payload_length) {
        kmemcpy(udp_tx_buffer + UDP_HEADER_SIZE, payload, payload_length);
    }
    checksum = udp_checksum(source_ip, destination_ip, udp_tx_buffer,
                            segment_length);
    if (!checksum) checksum = 0xFFFFU;
    udp_write_u16(udp_tx_buffer + UDP_OFFSET_CHECKSUM, checksum);
    *out_length = segment_length;
    return OK;
}

int udp_send(udp_endpoint_handle_t handle, uint32_t destination_ip,
             uint16_t destination_port, const uint8_t* payload,
             uint16_t payload_length, uint8_t* out_sent) {
    ipv4_status_t ipv4;
    uint16_t segment_length;
    int32_t index;
    int result;

    if (!out_sent || (payload_length && !payload)) {
        LOG_ERROR("NET", "Argumento nulo na transmissao UDP");
        return ERR_NULL;
    }
    *out_sent = 0;
    if (!udp_status.initialized) {
        LOG_ERROR("NET", "Transmissao UDP antes da inicializacao");
        return ERR_STATE;
    }
    index = udp_find_handle(handle);
    if (index < 0) {
        LOG_ERROR("NET", "Endpoint UDP invalido na transmissao");
        return ERR_INVALID;
    }
    result = ipv4_get_status(&ipv4);
    if (result != OK || !ipv4.configured) {
        LOG_ERROR("NET", "IPv4 indisponivel na transmissao UDP");
        return result != OK ? result : ERR_STATE;
    }
    result = udp_build_segment(udp_endpoints[index].local_port,
                               destination_port, ipv4.local_ip,
                               destination_ip, payload, payload_length,
                               &segment_length);
    if (result != OK) return result;
    result = ipv4_send(destination_ip, IPV4_PROTOCOL_UDP, udp_tx_buffer,
                       segment_length, out_sent);
    if (result != OK) {
        udp_status.tx_errors++;
        udp_status.last_error = result;
        return result;
    }
    if (!*out_sent) return OK;
    udp_status.tx_datagrams++;
    udp_status.tx_bytes += payload_length;
    udp_status.last_error = OK;
    return OK;
}

int udp_send_limited_broadcast(udp_endpoint_handle_t handle,
                               const char* interface_id,
                               uint32_t source_ip,
                               uint16_t destination_port,
                               const uint8_t* payload,
                               uint16_t payload_length,
                               uint8_t* out_sent) {
    uint16_t segment_length;
    int32_t index;
    int result;

    if (!interface_id || !out_sent ||
        (payload_length && !payload)) {
        LOG_ERROR("NET", "Argumento nulo no broadcast UDP");
        return ERR_NULL;
    }
    *out_sent = 0;
    if (!udp_status.initialized) {
        LOG_ERROR("NET", "Broadcast UDP antes da inicializacao");
        return ERR_STATE;
    }
    index = udp_find_handle(handle);
    if (index < 0 ||
        !(udp_endpoints[index].flags & UDP_BIND_ALLOW_BROADCAST)) {
        LOG_ERROR("NET", "Endpoint UDP invalido para broadcast");
        return ERR_INVALID;
    }
    result = udp_build_segment(udp_endpoints[index].local_port,
                               destination_port, source_ip,
                               IPV4_LIMITED_BROADCAST, payload,
                               payload_length, &segment_length);
    if (result != OK) return result;
    result = ipv4_send_limited_broadcast(
        interface_id, source_ip, IPV4_PROTOCOL_UDP, udp_tx_buffer,
        segment_length, out_sent);
    if (result != OK) {
        udp_status.tx_errors++;
        udp_status.last_error = result;
        return result;
    }
    if (!*out_sent) return OK;
    udp_status.tx_datagrams++;
    udp_status.tx_bytes += payload_length;
    udp_status.tx_broadcast++;
    udp_status.last_error = OK;
    return OK;
}

int udp_get_status(udp_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("NET", "Destino nulo ao consultar UDP");
        return ERR_NULL;
    }
    *out_status = udp_status;
    return OK;
}

static int udp_validate_checksum_vector(void) {
    uint8_t segment[UDP_HEADER_SIZE + 3U];
    uint16_t checksum;

    kmemset(segment, 0, sizeof(segment));
    udp_write_u16(segment + UDP_OFFSET_SOURCE_PORT, 1234U);
    udp_write_u16(segment + UDP_OFFSET_DESTINATION_PORT, 53U);
    udp_write_u16(segment + UDP_OFFSET_LENGTH, sizeof(segment));
    segment[UDP_HEADER_SIZE] = 0x41U;
    segment[UDP_HEADER_SIZE + 1U] = 0x42U;
    segment[UDP_HEADER_SIZE + 2U] = 0x43U;
    checksum = udp_checksum(0x0A00020FU, 0x0A000203U,
                            segment, sizeof(segment));
    if (checksum != UDP_CHECKSUM_VECTOR_EXPECTED) {
        LOG_ERROR("NET", "Resultado do vetor checksum UDP incorreto");
        return ERR_STATE;
    }
    if (!checksum) checksum = 0xFFFFU;
    udp_write_u16(segment + UDP_OFFSET_CHECKSUM, checksum);
    if (udp_checksum(0x0A00020FU, 0x0A000203U,
                     segment, sizeof(segment)) != 0U) {
        LOG_ERROR("NET", "Vetor de checksum UDP falhou");
        return ERR_STATE;
    }
    return OK;
}

int udp_validate_state(void) {
    uint32_t active = 0;

    if (udp_validate_checksum_vector() != OK) return ERR_STATE;
    for (uint32_t index = 0; index < UDP_ENDPOINT_CAPACITY; index++) {
        if (!udp_endpoints[index].active) continue;
        if (!udp_endpoints[index].callback ||
            !udp_endpoints[index].local_port ||
            !udp_endpoints[index].generation) {
            LOG_ERROR("NET", "Endpoint UDP inconsistente");
            return ERR_STATE;
        }
        active++;
        for (uint32_t other = index + 1U;
             other < UDP_ENDPOINT_CAPACITY; other++) {
            if (udp_endpoints[other].active &&
                udp_endpoints[other].local_port ==
                    udp_endpoints[index].local_port) {
                LOG_ERROR("NET", "Porta UDP duplicada");
                return ERR_STATE;
            }
        }
    }
    if (active != udp_status.endpoint_count ||
        active > UDP_ENDPOINT_CAPACITY ||
        (!udp_status.initialized && active)) {
        LOG_ERROR("NET", "Estado UDP inconsistente");
        return ERR_STATE;
    }
    return OK;
}
