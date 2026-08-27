#include "core/ethernet.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"

#define ETHERNET_DESTINATION_OFFSET 0U
#define ETHERNET_SOURCE_OFFSET 6U
#define ETHERNET_TYPE_OFFSET 12U
#define ETHERNET_TYPE_MINIMUM 0x0600U
#define ETHERNET_BROADCAST_OCTET 0xFFU

typedef struct {
    uint8_t active;
    uint16_t ethertype;
    ethernet_protocol_handler_fn handler;
} ethernet_protocol_entry_t;

typedef struct {
    ethernet_interface_t interface;
    ethernet_interface_status_t status;
} ethernet_slot_t;

static ethernet_slot_t ethernet_slots[ETHERNET_INTERFACE_CAPACITY];
static ethernet_status_t ethernet_status;
static ethernet_protocol_entry_t
    ethernet_handlers[ETHERNET_PROTOCOL_HANDLER_CAPACITY];
static uint8_t ethernet_rx_buffer[ETHERNET_MAX_FRAME_SIZE];
static uint8_t ethernet_tx_buffer[ETHERNET_MAX_FRAME_SIZE];
static uint8_t ethernet_poll_cursor;

static uint8_t ethernet_text_equal(const char* first,
                                   const char* second) {
    if (!first || !second) return 0;
    while (*first && *second) {
        if (*first != *second) return 0;
        first++;
        second++;
    }
    return *first == '\0' && *second == '\0';
}

static int ethernet_copy_id(char* destination, const char* source) {
    uint32_t index = 0;

    if (!destination || !source) {
        LOG_ERROR("NET", "ID nulo na camada Ethernet");
        return ERR_NULL;
    }
    while (source[index] && index + 1U < ETHERNET_INTERFACE_ID_SIZE) {
        destination[index] = source[index];
        index++;
    }
    if (source[index]) {
        LOG_ERROR("NET", "ID Ethernet excede o limite");
        return ERR_OVERFLOW;
    }
    destination[index] = '\0';
    return index ? OK : ERR_INVALID;
}

static uint8_t ethernet_mac_is_zero(const uint8_t* address) {
    for (uint32_t index = 0; index < ETHERNET_MAC_ADDRESS_SIZE; index++) {
        if (address[index] != 0U) return 0;
    }
    return 1;
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
    return !ethernet_mac_is_zero(source) && !(source[0] & 0x01U);
}

static ethernet_slot_t* ethernet_find_slot(const char* interface_id) {
    if (!interface_id) return NULL;
    for (uint32_t index = 0; index < ETHERNET_INTERFACE_CAPACITY; index++) {
        if (ethernet_slots[index].status.attached &&
            ethernet_text_equal(
                ethernet_slots[index].interface.interface_id,
                interface_id)) {
            return &ethernet_slots[index];
        }
    }
    return NULL;
}

static ethernet_destination_t ethernet_classify_destination(
    const ethernet_slot_t* slot, const uint8_t* destination) {
    if (ethernet_mac_is_broadcast(destination)) {
        return ETHERNET_DESTINATION_BROADCAST;
    }
    if (ethernet_mac_is_equal(destination,
                              slot->interface.mac_address)) {
        return ETHERNET_DESTINATION_LOCAL_UNICAST;
    }
    return ETHERNET_DESTINATION_UNKNOWN;
}

static ethernet_protocol_handler_fn ethernet_find_handler(
    uint16_t ethertype) {
    for (uint32_t index = 0;
         index < ETHERNET_PROTOCOL_HANDLER_CAPACITY; index++) {
        if (ethernet_handlers[index].active &&
            ethernet_handlers[index].ethertype == ethertype) {
            return ethernet_handlers[index].handler;
        }
    }
    return NULL;
}

static void ethernet_record_frame(ethernet_slot_t* slot,
                                  const uint8_t* frame, uint16_t length,
                                  ethernet_destination_t destination_type) {
    ethernet_interface_status_t* status = &slot->status;

    status->rx_frames++;
    ethernet_status.rx_frames++;
    if (destination_type == ETHERNET_DESTINATION_BROADCAST) {
        status->rx_broadcast++;
    } else {
        status->rx_unicast++;
    }
    status->last_frame_length = length;
    status->last_ethertype =
        (uint16_t)(((uint16_t)frame[ETHERNET_TYPE_OFFSET] << 8U) |
                   frame[ETHERNET_TYPE_OFFSET + 1U]);
    kmemcpy(status->last_destination,
            frame + ETHERNET_DESTINATION_OFFSET,
            ETHERNET_MAC_ADDRESS_SIZE);
    kmemcpy(status->last_source, frame + ETHERNET_SOURCE_OFFSET,
            ETHERNET_MAC_ADDRESS_SIZE);
    status->last_destination_type = destination_type;
}

static void ethernet_dispatch_frame(
    ethernet_slot_t* slot, const uint8_t* frame, uint16_t length,
    uint16_t ethertype, ethernet_destination_t destination_type) {
    ethernet_protocol_handler_fn handler =
        ethernet_find_handler(ethertype);
    ethernet_frame_view_t view;
    int result;

    if (!handler) {
        slot->status.rx_unhandled++;
        return;
    }
    view.interface_id = slot->interface.interface_id;
    view.destination = frame + ETHERNET_DESTINATION_OFFSET;
    view.source = frame + ETHERNET_SOURCE_OFFSET;
    view.payload = frame + ETHERNET_HEADER_SIZE;
    view.payload_length = length - ETHERNET_HEADER_SIZE;
    view.ethertype = ethertype;
    view.destination_type = destination_type;
    result = handler(&view);
    if (result == OK) {
        slot->status.rx_delivered++;
        ethernet_status.rx_delivered++;
        return;
    }
    slot->status.rx_protocol_errors++;
    slot->status.last_error = result;
    LOG_WARN("NET", "Protocolo Ethernet recusou frame recebido");
}

static void ethernet_process_frame(ethernet_slot_t* slot,
                                   const uint8_t* frame,
                                   uint16_t length) {
    ethernet_destination_t destination_type;
    uint16_t ethertype;

    if (length < ETHERNET_HEADER_SIZE || length > ETHERNET_MAX_FRAME_SIZE) {
        slot->status.rx_invalid++;
        LOG_DEBUG("NET", "Frame Ethernet com tamanho invalido descartado");
        return;
    }
    destination_type = ethernet_classify_destination(
        slot, frame + ETHERNET_DESTINATION_OFFSET);
    if (destination_type == ETHERNET_DESTINATION_UNKNOWN) {
        slot->status.rx_filtered++;
        return;
    }
    if (!ethernet_source_is_valid(frame + ETHERNET_SOURCE_OFFSET)) {
        slot->status.rx_invalid++;
        LOG_DEBUG("NET", "Frame Ethernet com origem invalida descartado");
        return;
    }
    ethertype = (uint16_t)(
        ((uint16_t)frame[ETHERNET_TYPE_OFFSET] << 8U) |
        frame[ETHERNET_TYPE_OFFSET + 1U]);
    if (ethertype < ETHERNET_TYPE_MINIMUM) {
        slot->status.rx_invalid++;
        LOG_DEBUG("NET", "Frame sem cabecalho Ethernet II descartado");
        return;
    }
    ethernet_record_frame(slot, frame, length, destination_type);
    ethernet_dispatch_frame(slot, frame, length, ethertype,
                            destination_type);
}

static uint16_t ethernet_build_frame(
    const ethernet_slot_t* slot, const uint8_t* destination,
    uint16_t ethertype, const uint8_t* payload, uint16_t payload_length) {
    uint16_t frame_length = ETHERNET_HEADER_SIZE + payload_length;

    if (frame_length < ETHERNET_MIN_FRAME_SIZE) {
        frame_length = ETHERNET_MIN_FRAME_SIZE;
    }
    kmemset(ethernet_tx_buffer, 0, frame_length);
    kmemcpy(ethernet_tx_buffer + ETHERNET_DESTINATION_OFFSET,
            destination, ETHERNET_MAC_ADDRESS_SIZE);
    kmemcpy(ethernet_tx_buffer + ETHERNET_SOURCE_OFFSET,
            slot->interface.mac_address, ETHERNET_MAC_ADDRESS_SIZE);
    ethernet_tx_buffer[ETHERNET_TYPE_OFFSET] = (uint8_t)(ethertype >> 8U);
    ethernet_tx_buffer[ETHERNET_TYPE_OFFSET + 1U] = (uint8_t)ethertype;
    if (payload_length) {
        kmemcpy(ethernet_tx_buffer + ETHERNET_HEADER_SIZE,
                payload, payload_length);
    }
    return frame_length;
}

int ethernet_init(void) {
    LOG_INFO("NET", "Inicializando camada Ethernet");
    if (ethernet_status.initialized) {
        LOG_WARN("NET", "Camada Ethernet ja estava inicializada");
        LOG_INFO("NET", "Camada Ethernet inicializada com sucesso");
        return OK;
    }
    kmemset(ethernet_slots, 0, sizeof(ethernet_slots));
    kmemset(&ethernet_status, 0, sizeof(ethernet_status));
    kmemset(ethernet_handlers, 0, sizeof(ethernet_handlers));
    ethernet_poll_cursor = 0;
    ethernet_status.initialized = 1;
    ethernet_status.last_error = OK;
    LOG_INFO("NET", "Camada Ethernet inicializada com sucesso");
    return OK;
}

int ethernet_attach_interface(const ethernet_interface_t* interface) {
    ethernet_slot_t* slot = NULL;
    int result;

    if (!ethernet_status.initialized) {
        LOG_ERROR("NET", "Anexo de interface antes da camada Ethernet");
        return ERR_STATE;
    }
    if (!interface || !interface->initialized ||
        !interface->driver_context || !interface->get_driver_status ||
        !interface->service_pending || !interface->rx_pending ||
        !interface->receive_frame ||
        !interface->send_frame) {
        LOG_ERROR("NET", "Interface invalida para camada Ethernet");
        return interface ? ERR_INVALID : ERR_NULL;
    }
    if (ethernet_mac_is_zero(interface->mac_address) ||
        (interface->mac_address[0] & 0x01U)) {
        LOG_ERROR("NET", "MAC local invalido para camada Ethernet");
        return ERR_INVALID;
    }
    if (ethernet_find_slot(interface->interface_id)) return OK;
    for (uint32_t index = 0; index < ETHERNET_INTERFACE_CAPACITY; index++) {
        if (!ethernet_slots[index].status.attached) {
            slot = &ethernet_slots[index];
            break;
        }
    }
    if (!slot) {
        LOG_ERROR("NET", "Limite de interfaces Ethernet atingido");
        return ERR_OVERFLOW;
    }
    kmemset(slot, 0, sizeof(*slot));
    slot->interface = *interface;
    result = ethernet_copy_id(slot->interface.interface_id,
                              interface->interface_id);
    if (result != OK) return result;
    slot->status.attached = 1;
    ethernet_copy_id(slot->status.interface_id,
                     slot->interface.interface_id);
    kmemcpy(slot->status.mac_address, slot->interface.mac_address,
            ETHERNET_MAC_ADDRESS_SIZE);
    slot->status.last_error = OK;
    ethernet_status.interface_count++;
    return OK;
}

int ethernet_register_handler(uint16_t ethertype,
                              ethernet_protocol_handler_fn handler) {
    int32_t free_index = -1;

    if (!ethernet_status.initialized) {
        LOG_ERROR("NET", "Registro de protocolo antes da camada Ethernet");
        return ERR_STATE;
    }
    if (!handler || ethertype < ETHERNET_TYPE_MINIMUM) {
        LOG_ERROR("NET", "Handler Ethernet invalido");
        return handler ? ERR_INVALID : ERR_NULL;
    }
    for (uint32_t index = 0;
         index < ETHERNET_PROTOCOL_HANDLER_CAPACITY; index++) {
        if (!ethernet_handlers[index].active && free_index < 0) {
            free_index = (int32_t)index;
        } else if (ethernet_handlers[index].active &&
                   ethernet_handlers[index].ethertype == ethertype) {
            if (ethernet_handlers[index].handler == handler) return OK;
            LOG_ERROR("NET", "EtherType ja possui outro handler");
            return ERR_STATE;
        }
    }
    if (free_index < 0) {
        LOG_ERROR("NET", "Tabela de protocolos Ethernet cheia");
        return ERR_OVERFLOW;
    }
    ethernet_handlers[free_index].active = 1;
    ethernet_handlers[free_index].ethertype = ethertype;
    ethernet_handlers[free_index].handler = handler;
    ethernet_status.handler_count++;
    return OK;
}

static int ethernet_poll_slot(ethernet_slot_t* slot,
                              uint8_t* out_received) {
    uint8_t pending = 0;
    uint16_t length = 0;
    int result;

    *out_received = 0;
    result = slot->interface.service_pending(slot->interface.driver_context);
    if (result != OK) goto failed;
    result = slot->interface.rx_pending(
        slot->interface.driver_context, &pending);
    if (result != OK) goto failed;
    if (!pending) return OK;
    slot->status.polls++;
    result = slot->interface.receive_frame(
        slot->interface.driver_context, ethernet_rx_buffer,
        sizeof(ethernet_rx_buffer), &length, out_received);
    if (result != OK) goto failed;
    if (*out_received) ethernet_process_frame(slot, ethernet_rx_buffer,
                                              length);
    slot->status.last_error = OK;
    return OK;

failed:
    slot->status.poll_errors++;
    slot->status.last_error = result;
    ethernet_status.poll_errors++;
    LOG_WARN("NET", "Falha isolada no polling de interface Ethernet");
    return result;
}

int ethernet_poll(uint32_t budget, uint32_t* out_processed) {
    uint32_t processed = 0;
    uint32_t idle_slots = 0;

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
    if (!ethernet_status.interface_count) return OK;
    ethernet_status.polls++;
    while (processed < budget &&
           idle_slots < ETHERNET_INTERFACE_CAPACITY) {
        ethernet_slot_t* slot = &ethernet_slots[ethernet_poll_cursor];
        uint8_t received = 0;

        ethernet_poll_cursor =
            (ethernet_poll_cursor + 1U) % ETHERNET_INTERFACE_CAPACITY;
        if (!slot->status.attached) {
            idle_slots++;
            continue;
        }
        if (ethernet_poll_slot(slot, &received) != OK || !received) {
            idle_slots++;
            continue;
        }
        processed++;
        idle_slots = 0;
    }
    ethernet_status.last_error = OK;
    *out_processed = processed;
    return OK;
}

int ethernet_send(const char* interface_id, const uint8_t* destination,
                  uint16_t ethertype, const uint8_t* payload,
                  uint16_t payload_length) {
    ethernet_slot_t* slot;
    uint16_t frame_length;
    int result;

    if (!ethernet_status.initialized) {
        LOG_ERROR("NET", "Transmissao Ethernet antes da inicializacao");
        return ERR_STATE;
    }
    if (!interface_id || !destination || (payload_length && !payload)) {
        LOG_ERROR("NET", "Argumento nulo para transmissao Ethernet");
        return ERR_NULL;
    }
    if (ethernet_mac_is_zero(destination) ||
        ethertype < ETHERNET_TYPE_MINIMUM ||
        payload_length > ETHERNET_MAX_PAYLOAD_SIZE) {
        LOG_ERROR("NET", "Cabecalho Ethernet invalido para transmissao");
        return ERR_INVALID;
    }
    slot = ethernet_find_slot(interface_id);
    if (!slot) {
        LOG_ERROR("NET", "Interface Ethernet de transmissao ausente");
        return ERR_NOT_FOUND;
    }
    frame_length = ethernet_build_frame(slot, destination, ethertype,
                                        payload, payload_length);
    result = slot->interface.send_frame(
        slot->interface.driver_context, ethernet_tx_buffer, frame_length);
    if (result != OK) {
        slot->status.last_error = result;
        LOG_ERROR("NET", "Falha ao transmitir pela interface Ethernet");
        return result;
    }
    slot->status.tx_frames++;
    slot->status.last_error = OK;
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

int ethernet_get_interface_status(
    const char* interface_id, ethernet_interface_status_t* out_status) {
    ethernet_slot_t* slot;
    int result;

    if (!interface_id || !out_status) {
        LOG_ERROR("NET", "Argumento nulo ao consultar interface Ethernet");
        return ERR_NULL;
    }
    slot = ethernet_find_slot(interface_id);
    if (!slot) {
        LOG_WARN("NET", "Interface Ethernet consultada nao encontrada");
        return ERR_NOT_FOUND;
    }
    result = slot->interface.get_driver_status(
        slot->interface.driver_context, &slot->status.driver);
    if (result != OK) {
        slot->status.last_error = result;
        LOG_ERROR("NET", "Falha ao consultar estado do driver Ethernet");
        return result;
    }
    *out_status = slot->status;
    return OK;
}

int ethernet_validate_state(void) {
    uint8_t attached = 0;
    uint8_t handlers = 0;
    uint32_t rx_frames = 0;
    uint32_t rx_delivered = 0;
    uint32_t tx_frames = 0;

    if (!ethernet_status.initialized) {
        if (ethernet_status.interface_count) {
            LOG_ERROR("NET", "Interfaces Ethernet sem camada inicializada");
            return ERR_STATE;
        }
        return OK;
    }
    for (uint32_t index = 0; index < ETHERNET_INTERFACE_CAPACITY; index++) {
        ethernet_slot_t* slot = &ethernet_slots[index];

        if (!slot->status.attached) continue;
        attached++;
        if (!slot->interface.interface_id[0] ||
            !slot->interface.driver_context ||
            !slot->interface.get_driver_status ||
            !slot->interface.rx_pending ||
            !slot->interface.receive_frame ||
            !slot->interface.send_frame) {
            LOG_ERROR("NET", "Registro Ethernet contem interface invalida");
            return ERR_STATE;
        }
        for (uint32_t other = index + 1U;
             other < ETHERNET_INTERFACE_CAPACITY; other++) {
            if (ethernet_slots[other].status.attached &&
                ethernet_text_equal(slot->interface.interface_id,
                    ethernet_slots[other].interface.interface_id)) {
                LOG_ERROR("NET", "Registro Ethernet contem ID duplicado");
                return ERR_STATE;
            }
        }
        rx_frames += slot->status.rx_frames;
        rx_delivered += slot->status.rx_delivered;
        tx_frames += slot->status.tx_frames;
    }
    for (uint32_t index = 0;
         index < ETHERNET_PROTOCOL_HANDLER_CAPACITY; index++) {
        if (ethernet_handlers[index].active) handlers++;
    }
    if (attached != ethernet_status.interface_count ||
        handlers != ethernet_status.handler_count ||
        rx_frames != ethernet_status.rx_frames ||
        rx_delivered != ethernet_status.rx_delivered ||
        tx_frames != ethernet_status.tx_frames ||
        ethernet_poll_cursor >= ETHERNET_INTERFACE_CAPACITY) {
        LOG_ERROR("NET", "Contadores do registro Ethernet incoerentes");
        return ERR_STATE;
    }
    return OK;
}
