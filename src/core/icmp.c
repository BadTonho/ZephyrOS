#include "core/icmp.h"
#include "core/errors.h"
#include "core/ipv4.h"
#include "core/log.h"
#include "core/string.h"
#include "core/timer.h"

#define ICMP_TYPE_ECHO_REPLY 0U
#define ICMP_TYPE_ECHO_REQUEST 8U
#define ICMP_ECHO_CODE 0U
#define ICMP_OFFSET_TYPE 0U
#define ICMP_OFFSET_CODE 1U
#define ICMP_OFFSET_CHECKSUM 2U
#define ICMP_OFFSET_IDENTIFIER 4U
#define ICMP_OFFSET_SEQUENCE 6U
#define ICMP_OFFSET_DATA 8U
#define ICMP_CHECKSUM_VECTOR_EXPECTED 0x52FDU
#define ICMP_MILLISECONDS_PER_SECOND 1000U
#define ICMP_MAX_UINT32 0xFFFFFFFFU

typedef struct {
    uint8_t active;
    uint32_t target_ip;
    uint16_t length;
    uint8_t message[IPV4_MAX_PAYLOAD_SIZE];
} icmp_pending_reply_t;

static icmp_status_t icmp_status;
static icmp_pending_reply_t icmp_pending_reply;
static uint8_t icmp_ping_message[ICMP_ECHO_MESSAGE_SIZE];
static timer_owner_handle_t icmp_timer_owner;
static timer_handle_t icmp_ping_timer;
static uint32_t icmp_ping_timeout_ms;
static uint32_t icmp_ping_sent_tick;
static uint32_t icmp_ping_configuration_generation;
static uint16_t icmp_next_identifier = 1U;

static uint16_t icmp_read_u16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

static void icmp_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

static uint16_t icmp_checksum(const uint8_t* data, uint16_t length) {
    uint32_t sum = 0;

    while (length >= 2U) {
        sum += icmp_read_u16(data);
        data += 2U;
        length -= 2U;
    }
    if (length) sum += (uint16_t)((uint16_t)data[0] << 8U);
    while (sum >> 16U) sum = (sum & 0xFFFFU) + (sum >> 16U);
    return (uint16_t)(~sum);
}

static uint8_t icmp_data_is_equal(const uint8_t* first,
                                  const uint8_t* second,
                                  uint16_t length) {
    for (uint16_t index = 0; index < length; index++) {
        if (first[index] != second[index]) return 0;
    }
    return 1;
}

static void icmp_build_ping_message(void) {
    uint16_t checksum;

    kmemset(icmp_ping_message, 0, sizeof(icmp_ping_message));
    icmp_ping_message[ICMP_OFFSET_TYPE] = ICMP_TYPE_ECHO_REQUEST;
    icmp_ping_message[ICMP_OFFSET_CODE] = ICMP_ECHO_CODE;
    icmp_write_u16(icmp_ping_message + ICMP_OFFSET_IDENTIFIER,
                   icmp_status.identifier);
    icmp_write_u16(icmp_ping_message + ICMP_OFFSET_SEQUENCE,
                   icmp_status.current_sequence);
    for (uint32_t index = 0; index < ICMP_ECHO_DATA_SIZE; index++) {
        icmp_ping_message[ICMP_OFFSET_DATA + index] =
            (uint8_t)(index ^ icmp_status.identifier ^
                      icmp_status.current_sequence);
    }
    checksum = icmp_checksum(icmp_ping_message,
                             sizeof(icmp_ping_message));
    icmp_write_u16(icmp_ping_message + ICMP_OFFSET_CHECKSUM, checksum);
}

static void icmp_preserve_module_counters(icmp_status_t* saved) {
    saved->echo_requests_rx = icmp_status.echo_requests_rx;
    saved->echo_requests_tx = icmp_status.echo_requests_tx;
    saved->echo_replies_rx = icmp_status.echo_replies_rx;
    saved->echo_replies_tx = icmp_status.echo_replies_tx;
    saved->invalid_packets = icmp_status.invalid_packets;
    saved->ignored_packets = icmp_status.ignored_packets;
    saved->pending_reply_drops = icmp_status.pending_reply_drops;
}

static void icmp_restore_module_counters(const icmp_status_t* saved) {
    icmp_status.echo_requests_rx = saved->echo_requests_rx;
    icmp_status.echo_requests_tx = saved->echo_requests_tx;
    icmp_status.echo_replies_rx = saved->echo_replies_rx;
    icmp_status.echo_replies_tx = saved->echo_replies_tx;
    icmp_status.invalid_packets = saved->invalid_packets;
    icmp_status.ignored_packets = saved->ignored_packets;
    icmp_status.pending_reply_drops = saved->pending_reply_drops;
}

static void icmp_finish_sequence(icmp_ping_event_t event,
                                 uint32_t rtt_ticks, uint8_t ttl) {
    icmp_status.last_event = event;
    icmp_status.last_rtt_ticks = rtt_ticks;
    icmp_status.last_reply_ttl = ttl;
    icmp_status.event_generation++;
    if (event == ICMP_PING_EVENT_REPLY) {
        icmp_status.received++;
        icmp_status.rtt_total_ticks += rtt_ticks;
        if (icmp_status.received == 1U ||
            rtt_ticks < icmp_status.rtt_min_ticks) {
            icmp_status.rtt_min_ticks = rtt_ticks;
        }
        if (rtt_ticks > icmp_status.rtt_max_ticks) {
            icmp_status.rtt_max_ticks = rtt_ticks;
        }
    } else {
        icmp_status.timeouts++;
        icmp_status.last_error = ERR_TIMEOUT;
    }
    if ((uint32_t)icmp_status.received + icmp_status.timeouts >=
        icmp_status.requested_count) {
        icmp_status.state = ICMP_PING_COMPLETE;
        return;
    }
    icmp_status.current_sequence++;
    icmp_status.state = ICMP_PING_RESOLVING;
}

static int icmp_ping_timeout_callback(timer_handle_t handle, void* context) {
    int result;

    if (handle != icmp_ping_timer || context != &icmp_status) {
        result = ERR_INVALID;
        LOG_ERROR_CODE("NET", ERR_INVALID,
                       "Callback recebeu timer ICMP invalido");
        return result;
    }
    if (icmp_status.state != ICMP_PING_WAITING_REPLY) {
        result = ERR_STATE;
        LOG_ERROR_CODE("NET", ERR_STATE,
                       "Timeout ICMP executado fora de espera");
        return result;
    }
    icmp_finish_sequence(ICMP_PING_EVENT_TIMEOUT, 0U, 0U);
    return OK;
}

static int icmp_cancel_ping_timer(void) {
    int result;

    if (!icmp_ping_timer) {
        result = ERR_STATE;
        LOG_ERROR_CODE("NET", ERR_STATE, "Timer ICMP nao foi criado");
        return result;
    }
    result = timer_cancel(icmp_ping_timer);
    if (result != OK) {
        LOG_ERROR_CODE("NET", result, "Falha ao cancelar timeout ICMP");
    }
    return result;
}

static int icmp_queue_reply(const ipv4_packet_view_t* packet) {
    uint16_t checksum;

    if (icmp_pending_reply.active) {
        icmp_status.pending_reply_drops++;
        LOG_WARN("NET", "Reply ICMP descartado por slot ocupado");
        return ERR_OVERFLOW;
    }
    if (packet->payload_length > IPV4_MAX_PAYLOAD_SIZE) {
        icmp_status.invalid_packets++;
        LOG_ERROR("NET", "Echo ICMP excede payload IPv4");
        return ERR_OVERFLOW;
    }
    icmp_pending_reply.active = 1;
    icmp_pending_reply.target_ip = packet->source_ip;
    icmp_pending_reply.length = packet->payload_length;
    kmemcpy(icmp_pending_reply.message, packet->payload,
            packet->payload_length);
    icmp_pending_reply.message[ICMP_OFFSET_TYPE] =
        ICMP_TYPE_ECHO_REPLY;
    icmp_write_u16(icmp_pending_reply.message + ICMP_OFFSET_CHECKSUM, 0U);
    checksum = icmp_checksum(icmp_pending_reply.message,
                             icmp_pending_reply.length);
    icmp_write_u16(icmp_pending_reply.message + ICMP_OFFSET_CHECKSUM,
                   checksum);
    return OK;
}

static int icmp_try_pending_reply(void) {
    uint8_t sent = 0;
    int result;

    if (!icmp_pending_reply.active) return OK;
    result = ipv4_send(icmp_pending_reply.target_ip,
                       IPV4_PROTOCOL_ICMP,
                       icmp_pending_reply.message,
                       icmp_pending_reply.length, &sent);
    if (result != OK) {
        icmp_status.last_error = result;
        icmp_pending_reply.active = 0;
        LOG_WARN("NET", "Falha ao enviar Echo Reply pendente");
        return result;
    }
    if (sent) {
        icmp_pending_reply.active = 0;
        icmp_status.echo_replies_tx++;
        icmp_status.last_error = OK;
    }
    return OK;
}

static uint8_t icmp_reply_matches_ping(
    const ipv4_packet_view_t* packet) {
    if (icmp_status.state != ICMP_PING_WAITING_REPLY ||
        packet->source_ip != icmp_status.target_ip ||
        packet->payload_length != ICMP_ECHO_MESSAGE_SIZE ||
        icmp_read_u16(packet->payload + ICMP_OFFSET_IDENTIFIER) !=
            icmp_status.identifier ||
        icmp_read_u16(packet->payload + ICMP_OFFSET_SEQUENCE) !=
            icmp_status.current_sequence) return 0;
    return icmp_data_is_equal(packet->payload + ICMP_OFFSET_DATA,
                              icmp_ping_message + ICMP_OFFSET_DATA,
                              ICMP_ECHO_DATA_SIZE);
}

static int icmp_handle_echo_reply(const ipv4_packet_view_t* packet) {
    uint32_t rtt_ticks;
    int result;

    icmp_status.echo_replies_rx++;
    if (!icmp_reply_matches_ping(packet)) {
        icmp_status.ignored_packets++;
        return OK;
    }
    result = icmp_cancel_ping_timer();
    if (result != OK) {
        icmp_status.last_error = result;
        return result;
    }
    rtt_ticks = (uint32_t)(timer_get_ticks() - icmp_ping_sent_tick);
    if (!rtt_ticks) rtt_ticks = 1U;
    icmp_finish_sequence(ICMP_PING_EVENT_REPLY, rtt_ticks,
                         packet->ttl);
    icmp_status.last_error = OK;
    return OK;
}

static int icmp_handle_packet(const ipv4_packet_view_t* packet) {
    uint8_t type;
    uint8_t code;
    int result;

    if (!packet || packet->payload_length < ICMP_ECHO_HEADER_SIZE) {
        icmp_status.invalid_packets++;
        return OK;
    }
    if (icmp_checksum(packet->payload, packet->payload_length) != 0U) {
        icmp_status.invalid_packets++;
        return OK;
    }
    type = packet->payload[ICMP_OFFSET_TYPE];
    code = packet->payload[ICMP_OFFSET_CODE];
    if (code != ICMP_ECHO_CODE) {
        icmp_status.invalid_packets++;
        return OK;
    }
    if (type == ICMP_TYPE_ECHO_REPLY) {
        return icmp_handle_echo_reply(packet);
    }
    if (type != ICMP_TYPE_ECHO_REQUEST) {
        icmp_status.ignored_packets++;
        return OK;
    }
    icmp_status.echo_requests_rx++;
    result = icmp_queue_reply(packet);
    if (result == ERR_OVERFLOW) return OK;
    if (result != OK) return result;
    result = icmp_try_pending_reply();
    return result == ERR_TIMEOUT ? OK : result;
}

int icmp_init(void) {
    int result;

    LOG_INFO("NET", "Inicializando protocolo ICMP");
    if (icmp_status.initialized) {
        LOG_WARN("NET", "Protocolo ICMP ja estava inicializado");
        LOG_INFO("NET", "Protocolo ICMP inicializado com sucesso");
        return OK;
    }
    kmemset(&icmp_status, 0, sizeof(icmp_status));
    kmemset(&icmp_pending_reply, 0, sizeof(icmp_pending_reply));
    result = timer_owner_create("ICMP", &icmp_timer_owner);
    if (result != OK) {
        LOG_ERROR_CODE("NET", result,
                       "Falha ao criar proprietario de timer ICMP");
        return result;
    }
    result = timer_create(icmp_timer_owner, "PING_TIMEOUT",
                          icmp_ping_timeout_callback, &icmp_status,
                          &icmp_ping_timer);
    if (result != OK) {
        timer_owner_destroy(icmp_timer_owner);
        icmp_timer_owner = 0U;
        LOG_ERROR_CODE("NET", result, "Falha ao criar timeout ICMP");
        return result;
    }
    result = ipv4_register_handler(IPV4_PROTOCOL_ICMP,
                                   icmp_handle_packet);
    if (result != OK) {
        timer_owner_destroy(icmp_timer_owner);
        icmp_timer_owner = 0U;
        icmp_ping_timer = 0U;
        icmp_status.last_error = result;
        LOG_ERROR("NET", "Falha ao registrar protocolo ICMP");
        return result;
    }
    icmp_status.initialized = 1;
    icmp_status.state = ICMP_PING_IDLE;
    icmp_status.last_error = OK;
    LOG_INFO("NET", "Protocolo ICMP inicializado com sucesso");
    return OK;
}

int icmp_ping_start(uint32_t target_ip, uint8_t count,
                    uint32_t timeout_seconds) {
    ipv4_status_t ipv4;
    icmp_status_t saved;
    int result;

    if (!icmp_status.initialized) {
        LOG_ERROR("NET", "Ping solicitado antes da inicializacao ICMP");
        return ERR_STATE;
    }
    if (!ipv4_address_is_unicast(target_ip) ||
        !count || count > ICMP_PING_MAX_COUNT || !timeout_seconds) {
        LOG_ERROR("NET", "Parametros invalidos para ping ICMP");
        return ERR_INVALID;
    }
    if (icmp_status.state == ICMP_PING_RESOLVING ||
        icmp_status.state == ICMP_PING_WAITING_REPLY) {
        LOG_WARN("NET", "Ja existe ping ICMP em andamento");
        return ERR_STATE;
    }
    if (ipv4_get_status(&ipv4) != OK || !ipv4.configured ||
        target_ip == ipv4.local_ip) {
        LOG_ERROR("NET", "Ping ICMP sem configuracao IPv4 valida");
        return ERR_STATE;
    }
    if (timeout_seconds > ICMP_MAX_UINT32 /
                          ICMP_MILLISECONDS_PER_SECOND) {
        LOG_ERROR_CODE("NET", ERR_INVALID,
                       "Timeout do ping ICMP excede limite");
        return ERR_INVALID;
    }
    result = icmp_cancel_ping_timer();
    if (result != OK) {
        icmp_status.last_error = result;
        return result;
    }
    kmemset(&saved, 0, sizeof(saved));
    icmp_preserve_module_counters(&saved);
    kmemset(&icmp_status, 0, sizeof(icmp_status));
    icmp_status.initialized = 1;
    icmp_restore_module_counters(&saved);
    icmp_status.state = ICMP_PING_RESOLVING;
    icmp_status.target_ip = target_ip;
    icmp_status.requested_count = count;
    icmp_status.identifier = icmp_next_identifier++;
    if (!icmp_next_identifier) icmp_next_identifier = 1U;
    icmp_status.current_sequence = 1U;
    icmp_status.last_error = OK;
    icmp_ping_timeout_ms = timeout_seconds * ICMP_MILLISECONDS_PER_SECOND;
    icmp_ping_configuration_generation =
        ipv4.configuration_generation;
    return OK;
}

static int icmp_validate_active_configuration(void) {
    ipv4_status_t ipv4;
    int cancel_result;

    if (ipv4_get_status(&ipv4) != OK || !ipv4.configured ||
        ipv4.configuration_generation !=
            icmp_ping_configuration_generation) {
        cancel_result = icmp_cancel_ping_timer();
        icmp_status.state = ICMP_PING_FAILED;
        icmp_status.last_error = cancel_result == OK ? ERR_STATE :
                                                       cancel_result;
        LOG_ERROR("NET", "Configuracao IPv4 mudou durante ping");
        return icmp_status.last_error;
    }
    return OK;
}

static int icmp_send_current_ping(void) {
    uint8_t sent = 0;
    int result;

    icmp_build_ping_message();
    result = ipv4_send(icmp_status.target_ip, IPV4_PROTOCOL_ICMP,
                       icmp_ping_message, sizeof(icmp_ping_message),
                       &sent);
    if (result != OK) {
        int cancel_result = icmp_cancel_ping_timer();

        icmp_status.state = ICMP_PING_FAILED;
        icmp_status.last_error = cancel_result == OK ? result : cancel_result;
        LOG_WARN("NET", "Falha ao preparar ou enviar Echo Request");
        return icmp_status.last_error;
    }
    if (!sent) return OK;
    icmp_status.sent++;
    icmp_status.echo_requests_tx++;
    icmp_ping_sent_tick = timer_get_ticks();
    icmp_status.state = ICMP_PING_WAITING_REPLY;
    result = timer_start_once(icmp_ping_timer, icmp_ping_timeout_ms);
    if (result != OK) {
        icmp_status.state = ICMP_PING_FAILED;
        icmp_status.last_error = result;
        LOG_ERROR_CODE("NET", result, "Falha ao armar timeout ICMP");
        return result;
    }
    return OK;
}

int icmp_maintain(void) {
    int result;

    if (!icmp_status.initialized) {
        LOG_ERROR("NET", "Manutencao ICMP antes da inicializacao");
        return ERR_STATE;
    }
    if (icmp_pending_reply.active) {
        return icmp_try_pending_reply();
    }
    if (icmp_status.state != ICMP_PING_RESOLVING &&
        icmp_status.state != ICMP_PING_WAITING_REPLY) return OK;
    result = icmp_validate_active_configuration();
    if (result != OK) return result;
    if (icmp_status.state == ICMP_PING_RESOLVING) {
        return icmp_send_current_ping();
    }
    return OK;
}

int icmp_reset(void) {
    int result;

    if (!icmp_status.initialized) {
        LOG_ERROR("NET", "Reset ICMP antes da inicializacao");
        return ERR_STATE;
    }
    result = icmp_cancel_ping_timer();
    if (result != OK) return result;
    kmemset(&icmp_status, 0, sizeof(icmp_status));
    kmemset(&icmp_pending_reply, 0, sizeof(icmp_pending_reply));
    kmemset(icmp_ping_message, 0, sizeof(icmp_ping_message));
    icmp_ping_timeout_ms = 0U;
    icmp_ping_sent_tick = 0;
    icmp_ping_configuration_generation = 0;
    icmp_status.initialized = 1;
    icmp_status.state = ICMP_PING_IDLE;
    icmp_status.last_error = OK;
    LOG_INFO("NET", "Estado ICMP reiniciado");
    return OK;
}

int icmp_get_status(icmp_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("NET", "Destino nulo ao consultar ICMP");
        return ERR_NULL;
    }
    *out_status = icmp_status;
    out_status->reply_pending = icmp_pending_reply.active;
    return OK;
}

static int icmp_validate_checksum_vector(void) {
    uint8_t message[ICMP_ECHO_HEADER_SIZE + 1U];
    uint16_t checksum;

    kmemset(message, 0, sizeof(message));
    message[ICMP_OFFSET_TYPE] = ICMP_TYPE_ECHO_REQUEST;
    icmp_write_u16(message + ICMP_OFFSET_IDENTIFIER, 1U);
    icmp_write_u16(message + ICMP_OFFSET_SEQUENCE, 1U);
    message[ICMP_OFFSET_DATA] = 0xA5U;
    checksum = icmp_checksum(message, sizeof(message));
    if (checksum != ICMP_CHECKSUM_VECTOR_EXPECTED) {
        LOG_ERROR("NET", "Resultado do vetor checksum ICMP incorreto");
        return ERR_STATE;
    }
    icmp_write_u16(message + ICMP_OFFSET_CHECKSUM, checksum);
    if (icmp_checksum(message, sizeof(message)) != 0U) {
        LOG_ERROR("NET", "Vetor de checksum ICMP falhou");
        return ERR_STATE;
    }
    return OK;
}

int icmp_validate_state(void) {
    ipv4_status_t ipv4;
    timer_info_t timer_info;
    uint32_t settled =
        (uint32_t)icmp_status.received + icmp_status.timeouts;

    if (icmp_validate_checksum_vector() != OK) return ERR_STATE;
    if (!icmp_status.initialized) {
        if (icmp_status.state != ICMP_PING_IDLE ||
            icmp_pending_reply.active || icmp_timer_owner ||
            icmp_ping_timer) {
            LOG_ERROR("NET", "ICMP ativo sem inicializacao");
            return ERR_STATE;
        }
        return OK;
    }
    if (timer_get_info(icmp_ping_timer, &timer_info) != OK ||
        timer_info.owner != icmp_timer_owner ||
        timer_info.mode != TIMER_MODE_ONE_SHOT ||
        (icmp_status.state == ICMP_PING_WAITING_REPLY &&
         timer_info.state != TIMER_STATE_ARMED &&
         timer_info.state != TIMER_STATE_PENDING) ||
        (icmp_status.state != ICMP_PING_WAITING_REPLY &&
         timer_info.state != TIMER_STATE_IDLE) ||
        ipv4_get_status(&ipv4) != OK || !ipv4.initialized ||
        icmp_status.state > ICMP_PING_FAILED ||
        icmp_status.requested_count > ICMP_PING_MAX_COUNT ||
        icmp_status.sent > icmp_status.requested_count ||
        settled > icmp_status.sent ||
        (icmp_status.received &&
         icmp_status.rtt_min_ticks > icmp_status.rtt_max_ticks)) {
        LOG_ERROR("NET", "Estado ICMP inconsistente");
        return ERR_STATE;
    }
    if ((icmp_status.state == ICMP_PING_RESOLVING ||
         icmp_status.state == ICMP_PING_WAITING_REPLY) &&
        (!icmp_status.identifier || !icmp_status.requested_count ||
         !ipv4.configured ||
         ipv4.configuration_generation !=
             icmp_ping_configuration_generation)) {
        LOG_ERROR("NET", "Sessao ICMP ativa inconsistente");
        return ERR_STATE;
    }
    if (icmp_status.state == ICMP_PING_COMPLETE &&
        settled != icmp_status.requested_count) {
        LOG_ERROR("NET", "Ping ICMP completo sem todas as tentativas");
        return ERR_STATE;
    }
    if (icmp_pending_reply.active &&
        (!ipv4.configured ||
         !ipv4_address_is_unicast(icmp_pending_reply.target_ip) ||
         icmp_pending_reply.length < ICMP_ECHO_HEADER_SIZE ||
         icmp_pending_reply.length > IPV4_MAX_PAYLOAD_SIZE)) {
        LOG_ERROR("NET", "Reply ICMP pendente inconsistente");
        return ERR_STATE;
    }
    return OK;
}

const char* icmp_ping_state_name(icmp_ping_state_t state) {
    if (state == ICMP_PING_IDLE) return "IDLE";
    if (state == ICMP_PING_RESOLVING) return "RESOLVING";
    if (state == ICMP_PING_WAITING_REPLY) return "WAITING_REPLY";
    if (state == ICMP_PING_COMPLETE) return "COMPLETE";
    if (state == ICMP_PING_FAILED) return "FAILED";
    return "INVALID";
}
