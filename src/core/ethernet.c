#include "core/ethernet.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"

#define ETHERNET_DESTINATION_OFFSET 0U
#define ETHERNET_SOURCE_OFFSET 6U
#define ETHERNET_TYPE_OFFSET 12U
#define ETHERNET_TYPE_MINIMUM 0x0600U
#define ETHERNET_BROADCAST_OCTET 0xFFU

static ethernet_interface_t ethernet_interface;
static ethernet_status_t ethernet_status;
static uint8_t ethernet_rx_buffer[ETHERNET_MAX_FRAME_SIZE];
static uint8_t ethernet_tx_buffer[ETHERNET_MAX_FRAME_SIZE];

static uint8_t ethernet_mac_is_zero(const uint8_t* address) {
    uint8_t result = 1;

    for (uint32_t index = 0; index < ETHERNET_MAC_ADDRESS_SIZE; index++) {
        if (address[index] != 0U) result = 0;
    }
    return result;
}

static uint8_t ethernet_mac_is_broadcast(const uint8_t* address) {
    for (uint32_t index = 0; index < ETHERNET_MAC_ADDRESS_SIZE; index++) {
        if (address[index] != ETHERNET_BROADCAST_OCTET) return 0;
    }
    return 1;
}

static uint8_t ethernet_mac_is_equal(const uint8_t* first,
                                     const uint8_t* second) {
    for (uint32_t index = 0; index < ETHERNET_MAC_ADDRESS_SIZE; index++) {
        if (first[index] != second[index]) return 0;
    }
    return 1;
}

static uint8_t ethernet_source_is_valid(const uint8_t* source) {
    if (ethernet_mac_is_zero(source)) return 0;
    return (source[0] & 0x01U) == 0U;
}

static ethernet_destination_t ethernet_classify_destination(
    const uint8_t* destination) {
    if (ethernet_mac_is_broadcast(destination)) {
        return ETHERNET_DESTINATION_BROADCAST;
    }
    if (ethernet_mac_is_equal(destination,
                              ethernet_interface.mac_address)) {
        return ETHERNET_DESTINATION_LOCAL_UNICAST;
    }
    return ETHERNET_DESTINATION_UNKNOWN;
}

static void ethernet_copy_mac(uint8_t* destination,
                              const uint8_t* source) {
    kmemcpy(destination, source, ETHERNET_MAC_ADDRESS_SIZE);
}

static void ethernet_record_frame(const uint8_t* frame, uint16_t length,
                                  ethernet_destination_t destination_type) {
    ethernet_status.rx_frames++;
    ethernet_status.rx_unhandled++;
    if (destination_type == ETHERNET_DESTINATION_BROADCAST) {
        ethernet_status.rx_broadcast++;
    } else {
        ethernet_status.rx_unicast++;
    }
    ethernet_status.last_frame_length = length;
    ethernet_status.last_ethertype =
        (uint16_t)(((uint16_t)frame[ETHERNET_TYPE_OFFSET] << 8U) |
                   frame[ETHERNET_TYPE_OFFSET + 1U]);
    ethernet_copy_mac(ethernet_status.last_destination,
                      frame + ETHERNET_DESTINATION_OFFSET);
    ethernet_copy_mac(ethernet_status.last_source,
                      frame + ETHERNET_SOURCE_OFFSET);
    ethernet_status.last_destination_type = destination_type;
}

static void ethernet_process_frame(const uint8_t* frame, uint16_t length) {
    ethernet_destination_t destination_type;
    uint16_t ethertype;

    if (length < ETHERNET_HEADER_SIZE || length > ETHERNET_MAX_FRAME_SIZE) {
        ethernet_status.rx_invalid++;
        LOG_DEBUG("NET", "Frame Ethernet com tamanho invalido descartado");
        return;
    }
    destination_type = ethernet_classify_destination(
        frame + ETHERNET_DESTINATION_OFFSET);
    if (destination_type == ETHERNET_DESTINATION_UNKNOWN) {
        ethernet_status.rx_filtered++;
        LOG_DEBUG("NET", "Frame Ethernet fora do filtro local descartado");
        return;
    }
    if (!ethernet_source_is_valid(frame + ETHERNET_SOURCE_OFFSET)) {
        ethernet_status.rx_invalid++;
        LOG_DEBUG("NET", "Frame Ethernet com origem invalida descartado");
        return;
    }
    ethertype = (uint16_t)(
        ((uint16_t)frame[ETHERNET_TYPE_OFFSET] << 8U) |
        frame[ETHERNET_TYPE_OFFSET + 1U]);
    if (ethertype < ETHERNET_TYPE_MINIMUM) {
        ethernet_status.rx_invalid++;
        LOG_DEBUG("NET", "Frame sem cabecalho Ethernet II descartado");
        return;
    }
    ethernet_record_frame(frame, length, destination_type);
}

static uint16_t ethernet_build_frame(const uint8_t* destination,
                                     uint16_t ethertype,
                                     const uint8_t* payload,
                                     uint16_t payload_length) {
    uint16_t frame_length = ETHERNET_HEADER_SIZE + payload_length;

    if (frame_length < ETHERNET_MIN_FRAME_SIZE) {
        frame_length = ETHERNET_MIN_FRAME_SIZE;
    }
    kmemset(ethernet_tx_buffer, 0, frame_length);
    ethernet_copy_mac(ethernet_tx_buffer + ETHERNET_DESTINATION_OFFSET,
                      destination);
    ethernet_copy_mac(ethernet_tx_buffer + ETHERNET_SOURCE_OFFSET,
                      ethernet_interface.mac_address);
    ethernet_tx_buffer[ETHERNET_TYPE_OFFSET] = (uint8_t)(ethertype >> 8U);
    ethernet_tx_buffer[ETHERNET_TYPE_OFFSET + 1U] = (uint8_t)ethertype;
    if (payload_length) {
        kmemcpy(ethernet_tx_buffer + ETHERNET_HEADER_SIZE,
                payload, payload_length);
    }
    return frame_length;
}

int ethernet_init(const ethernet_interface_t* interface) {
    LOG_INFO("NET", "Inicializando camada Ethernet");
    if (ethernet_status.initialized) {
        LOG_WARN("NET", "Camada Ethernet ja estava inicializada");
        return OK;
    }
    if (!interface || !interface->initialized ||
        !interface->rx_pending ||
        !interface->receive_frame || !interface->send_frame) {
        LOG_ERROR("NET", "Interface invalida para camada Ethernet");
        return ERR_INVALID;
    }
    if (ethernet_mac_is_zero(interface->mac_address) ||
        (interface->mac_address[0] & 0x01U)) {
        LOG_ERROR("NET", "MAC local invalido para camada Ethernet");
        return ERR_INVALID;
    }
    kmemset(&ethernet_interface, 0, sizeof(ethernet_interface));
    kmemset(&ethernet_status, 0, sizeof(ethernet_status));
    ethernet_interface = *interface;
    ethernet_copy_mac(ethernet_status.mac_address,
                      interface->mac_address);
    ethernet_status.initialized = 1;
    ethernet_status.last_error = OK;
    LOG_INFO("NET", "Camada Ethernet inicializada com sucesso");
    return OK;
}

int ethernet_poll(uint32_t budget, uint32_t* out_processed) {
    uint32_t processed = 0;
    uint8_t pending = 0;
    int result;

    if (!out_processed) {
        LOG_ERROR("NET", "Destino nulo ao processar fila Ethernet");
        return ERR_NULL;
    }
    *out_processed = 0;
    if (!ethernet_status.initialized) {
        LOG_ERROR("NET", "Polling Ethernet antes da inicializacao");
        return ERR_STATE;
    }
    if (!budget || budget > ETHERNET_RX_POLL_BUDGET) {
        LOG_ERROR("NET", "Orcamento de polling Ethernet invalido");
        return ERR_INVALID;
    }
    result = ethernet_interface.rx_pending(&pending);
    if (result != OK) {
        ethernet_status.last_error = result;
        LOG_ERROR("NET", "Falha ao consultar recepcao pendente");
        return result;
    }
    if (!pending) {
        ethernet_status.last_error = OK;
        return OK;
    }
    ethernet_status.polls++;
    while (processed < budget) {
        uint16_t length = 0;
        uint8_t received = 0;
        result = ethernet_interface.receive_frame(
            ethernet_rx_buffer, sizeof(ethernet_rx_buffer), &length,
            &received);

        if (result != OK) {
            ethernet_status.last_error = result;
            LOG_ERROR("NET", "Falha ao obter frame da interface");
            return result;
        }
        if (!received) break;
        ethernet_process_frame(ethernet_rx_buffer, length);
        processed++;
    }
    ethernet_status.last_error = OK;
    *out_processed = processed;
    return OK;
}

int ethernet_send(const uint8_t* destination, uint16_t ethertype,
                  const uint8_t* payload, uint16_t payload_length) {
    uint16_t frame_length;
    int result;

    if (!ethernet_status.initialized) {
        LOG_ERROR("NET", "Transmissao Ethernet antes da inicializacao");
        return ERR_STATE;
    }
    if (!destination || (payload_length && !payload)) {
        LOG_ERROR("NET", "Argumento nulo para transmissao Ethernet");
        return ERR_NULL;
    }
    if (ethernet_mac_is_zero(destination) ||
        ethertype < ETHERNET_TYPE_MINIMUM ||
        payload_length > ETHERNET_MAX_PAYLOAD_SIZE) {
        LOG_ERROR("NET", "Cabecalho Ethernet invalido para transmissao");
        return ERR_INVALID;
    }
    frame_length = ethernet_build_frame(destination, ethertype,
                                        payload, payload_length);
    result = ethernet_interface.send_frame(ethernet_tx_buffer, frame_length);
    if (result != OK) {
        ethernet_status.last_error = result;
        LOG_ERROR("NET", "Falha ao transmitir pela interface Ethernet");
        return result;
    }
    ethernet_status.tx_frames++;
    ethernet_status.last_error = OK;
    return OK;
}

int ethernet_get_status(ethernet_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("NET", "Destino nulo ao consultar camada Ethernet");
        return ERR_NULL;
    }
    *out_status = ethernet_status;
    return OK;
}
