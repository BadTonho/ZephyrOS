#include "core/tcp.h"
#include "core/errors.h"
#include "core/ipv4.h"
#include "core/log.h"
#include "core/string.h"
#include "core/timer.h"

#define TCP_OFFSET_SOURCE_PORT 0U
#define TCP_OFFSET_DESTINATION_PORT 2U
#define TCP_OFFSET_SEQUENCE 4U
#define TCP_OFFSET_ACKNOWLEDGMENT 8U
#define TCP_OFFSET_DATA 12U
#define TCP_OFFSET_FLAGS 13U
#define TCP_OFFSET_WINDOW 14U
#define TCP_OFFSET_CHECKSUM 16U
#define TCP_OFFSET_URGENT 18U

#define TCP_FLAG_FIN 0x01U
#define TCP_FLAG_SYN 0x02U
#define TCP_FLAG_RST 0x04U
#define TCP_FLAG_PSH 0x08U
#define TCP_FLAG_ACK 0x10U
#define TCP_FLAG_URG 0x20U

#define TCP_OPTION_END 0U
#define TCP_OPTION_NOP 1U
#define TCP_OPTION_MSS 2U
#define TCP_OPTION_MSS_LENGTH 4U
#define TCP_SYN_HEADER_SIZE 24U

#define TCP_HANDLE_SLOT_MASK 0xFFU
#define TCP_HANDLE_GENERATION_SHIFT 8U
#define TCP_HANDLE_GENERATION_MAX 0x00FFFFFFU
#define TCP_INITIAL_RTO_SECONDS 1U
#define TCP_MAX_RTO_SECONDS 60U
#define TCP_IDLE_TIMEOUT_SECONDS 30U
#define TCP_TIME_WAIT_SECONDS 30U
#define TCP_MAX_RETRANSMISSIONS 3U
#define TCP_MAX_SAFE_TICKS 0x7FFFFFFFU
#define TCP_ISN_SALT 0x91E10DA5U
#define TCP_ISN_INCREMENT 0x01010101U

typedef struct {
    uint8_t active;
    uint8_t pending_active;
    uint8_t pending_sent;
    uint8_t pending_retransmitted;
    uint8_t close_requested;
    uint8_t retransmissions;
    uint16_t local_port;
    uint16_t remote_port;
    uint16_t local_window;
    uint16_t remote_window;
    uint16_t remote_mss;
    uint16_t pending_length;
    uint16_t pending_flags;
    uint32_t generation;
    uint32_t remote_ip;
    uint32_t send_unacknowledged;
    uint32_t send_next;
    uint32_t receive_next;
    uint32_t pending_sequence;
    uint32_t pending_sent_tick;
    uint32_t last_activity_tick;
    uint32_t time_wait_tick;
    uint32_t rto_ticks;
    uint32_t srtt_ticks;
    uint32_t rttvar_ticks;
    tcp_state_t state;
    tcp_event_fn callback;
    int last_error;
    uint8_t pending_payload[TCP_LOCAL_MSS];
} tcp_connection_t;

typedef struct {
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t sequence;
    uint32_t acknowledgment;
    uint16_t window;
    uint16_t header_length;
    uint16_t payload_length;
    uint16_t flags;
    uint16_t mss;
    const uint8_t* payload;
} tcp_segment_view_t;

static tcp_status_t tcp_status;
static tcp_connection_t tcp_connections[TCP_CONNECTION_CAPACITY];
static uint32_t tcp_generations[TCP_CONNECTION_CAPACITY];
static uint8_t tcp_tx_buffer[TCP_SYN_HEADER_SIZE + TCP_LOCAL_MSS];
static uint32_t tcp_isn_counter;

static uint16_t tcp_read_u16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

static uint32_t tcp_read_u32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) | data[3];
}

static void tcp_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

static void tcp_write_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static uint32_t tcp_add_bytes(uint32_t sum, const uint8_t* data,
                              uint16_t length) {
    while (length >= 2U) {
        sum += tcp_read_u16(data);
        data += 2U;
        length -= 2U;
    }
    if (length) sum += (uint16_t)((uint16_t)data[0] << 8U);
    return sum;
}

static uint16_t tcp_checksum(uint32_t source_ip,
                             uint32_t destination_ip,
                             const uint8_t* segment,
                             uint16_t length) {
    uint32_t sum = 0;

    sum += (uint16_t)(source_ip >> 16U);
    sum += (uint16_t)source_ip;
    sum += (uint16_t)(destination_ip >> 16U);
    sum += (uint16_t)destination_ip;
    sum += IPV4_PROTOCOL_TCP;
    sum += length;
    sum = tcp_add_bytes(sum, segment, length);
    while (sum >> 16U) sum = (sum & 0xFFFFU) + (sum >> 16U);
    return (uint16_t)(~sum);
}

static uint32_t tcp_seconds_to_ticks(uint32_t seconds,
                                     uint32_t frequency) {
    if (!seconds || !frequency) return 0;
    if (seconds > TCP_MAX_SAFE_TICKS / frequency) {
        return TCP_MAX_SAFE_TICKS;
    }
    return seconds * frequency;
}

static uint8_t tcp_elapsed(uint32_t now, uint32_t start,
                           uint32_t interval) {
    return interval && (uint32_t)(now - start) >= interval;
}

static uint8_t tcp_sequence_less(uint32_t first, uint32_t second) {
    return (int32_t)(first - second) < 0;
}

static uint8_t tcp_sequence_between(uint32_t value, uint32_t first,
                                    uint32_t last) {
    return !tcp_sequence_less(value, first) &&
           !tcp_sequence_less(last, value);
}

static tcp_connection_handle_t tcp_make_handle(uint32_t index) {
    return (tcp_connections[index].generation <<
            TCP_HANDLE_GENERATION_SHIFT) | (index + 1U);
}

static int32_t tcp_find_handle(tcp_connection_handle_t handle) {
    uint32_t slot = handle & TCP_HANDLE_SLOT_MASK;
    uint32_t generation = handle >> TCP_HANDLE_GENERATION_SHIFT;

    if (!slot || slot > TCP_CONNECTION_CAPACITY || !generation) return -1;
    slot--;
    if (!tcp_connections[slot].active ||
        tcp_connections[slot].generation != generation) return -1;
    return (int32_t)slot;
}

static int32_t tcp_find_tuple(uint32_t remote_ip,
                              uint16_t local_port,
                              uint16_t remote_port) {
    for (uint32_t index = 0; index < TCP_CONNECTION_CAPACITY; index++) {
        tcp_connection_t* connection = &tcp_connections[index];

        if (connection->active &&
            connection->remote_ip == remote_ip &&
            connection->local_port == local_port &&
            connection->remote_port == remote_port) {
            return (int32_t)index;
        }
    }
    return -1;
}

static int tcp_emit(uint32_t index, tcp_event_t event,
                    const uint8_t* data, uint16_t length, int error) {
    tcp_connection_t* connection = &tcp_connections[index];
    int result;

    if (!connection->callback) {
        LOG_ERROR("NET", "Conexao TCP sem callback");
        return ERR_STATE;
    }
    result = connection->callback(
        tcp_make_handle(index), event, data, length, error);
    if (result != OK) {
        tcp_status.callback_errors++;
        tcp_status.last_error = result;
        LOG_WARN("NET", "Consumidor TCP recusou evento");
    }
    return result;
}

static void tcp_clear_connection(uint32_t index, uint8_t notify) {
    tcp_connection_handle_t handle;
    tcp_event_fn callback;

    if (!tcp_connections[index].active) return;
    handle = tcp_make_handle(index);
    callback = tcp_connections[index].callback;
    kmemset(&tcp_connections[index], 0, sizeof(tcp_connections[index]));
    if (tcp_status.connection_count) tcp_status.connection_count--;
    if (notify && callback &&
        callback(handle, TCP_EVENT_CLOSED, NULL, 0U, OK) != OK) {
        tcp_status.callback_errors++;
        LOG_WARN("NET", "Consumidor TCP recusou fechamento");
    }
}

static void tcp_fail(uint32_t index, int error) {
    tcp_connection_t* connection = &tcp_connections[index];

    if (!connection->active || connection->state == TCP_STATE_FAILED) {
        return;
    }
    connection->state = TCP_STATE_FAILED;
    connection->last_error = error;
    connection->pending_active = 0;
    tcp_status.last_error = error;
    tcp_emit(index, TCP_EVENT_ERROR, NULL, 0U, error);
}

static uint16_t tcp_build_segment(uint32_t source_ip,
                                  uint32_t destination_ip,
                                  uint16_t source_port,
                                  uint16_t destination_port,
                                  uint32_t sequence,
                                  uint32_t acknowledgment,
                                  uint16_t flags, uint16_t window,
                                  const uint8_t* payload,
                                  uint16_t payload_length) {
    uint16_t header_length =
        (flags & TCP_FLAG_SYN) ? TCP_SYN_HEADER_SIZE :
                                TCP_HEADER_MIN_SIZE;
    uint16_t total_length = header_length + payload_length;
    uint16_t checksum;

    kmemset(tcp_tx_buffer, 0, total_length);
    tcp_write_u16(tcp_tx_buffer + TCP_OFFSET_SOURCE_PORT, source_port);
    tcp_write_u16(tcp_tx_buffer + TCP_OFFSET_DESTINATION_PORT,
                  destination_port);
    tcp_write_u32(tcp_tx_buffer + TCP_OFFSET_SEQUENCE, sequence);
    tcp_write_u32(tcp_tx_buffer + TCP_OFFSET_ACKNOWLEDGMENT,
                  acknowledgment);
    tcp_tx_buffer[TCP_OFFSET_DATA] =
        (uint8_t)((header_length / 4U) << 4U);
    tcp_tx_buffer[TCP_OFFSET_FLAGS] = (uint8_t)flags;
    tcp_write_u16(tcp_tx_buffer + TCP_OFFSET_WINDOW, window);
    if (flags & TCP_FLAG_SYN) {
        tcp_tx_buffer[TCP_HEADER_MIN_SIZE] = TCP_OPTION_MSS;
        tcp_tx_buffer[TCP_HEADER_MIN_SIZE + 1U] = TCP_OPTION_MSS_LENGTH;
        tcp_write_u16(tcp_tx_buffer + TCP_HEADER_MIN_SIZE + 2U,
                      TCP_LOCAL_MSS);
    }
    if (payload_length) {
        kmemcpy(tcp_tx_buffer + header_length, payload, payload_length);
    }
    checksum = tcp_checksum(source_ip, destination_ip,
                            tcp_tx_buffer, total_length);
    tcp_write_u16(tcp_tx_buffer + TCP_OFFSET_CHECKSUM, checksum);
    return total_length;
}

static int tcp_transmit(tcp_connection_t* connection,
                        uint32_t sequence, uint16_t flags,
                        const uint8_t* payload, uint16_t length,
                        uint8_t* out_sent) {
    ipv4_status_t ipv4;
    uint16_t segment_length;
    int result;

    if (!connection || !out_sent || (length && !payload)) {
        LOG_ERROR("NET", "Argumento nulo na transmissao TCP");
        return ERR_NULL;
    }
    *out_sent = 0;
    if (ipv4_get_status(&ipv4) != OK || !ipv4.configured) {
        LOG_ERROR("NET", "IPv4 indisponivel para TCP");
        return ERR_STATE;
    }
    segment_length = tcp_build_segment(
        ipv4.local_ip, connection->remote_ip,
        connection->local_port, connection->remote_port,
        sequence, (flags & TCP_FLAG_ACK) ?
            connection->receive_next : 0U,
        flags, connection->local_window, payload, length);
    result = ipv4_send(connection->remote_ip, IPV4_PROTOCOL_TCP,
                       tcp_tx_buffer, segment_length, out_sent);
    if (result != OK || !*out_sent) return result;
    tcp_status.segments_tx++;
    tcp_status.bytes_tx += length;
    if (flags & TCP_FLAG_SYN) tcp_status.syn_tx++;
    if (flags & TCP_FLAG_FIN) tcp_status.fin_tx++;
    if (flags & TCP_FLAG_RST) tcp_status.resets_tx++;
    connection->last_activity_tick = timer_get_ticks();
    return OK;
}

static int tcp_send_ack(uint32_t index) {
    uint8_t sent = 0;
    int result = tcp_transmit(&tcp_connections[index],
                              tcp_connections[index].send_next,
                              TCP_FLAG_ACK, NULL, 0U, &sent);

    if (result != OK) {
        LOG_WARN("NET", "ACK TCP nao foi transmitido");
        return result;
    }
    return OK;
}

static int tcp_try_send_pending(uint32_t index) {
    tcp_connection_t* connection = &tcp_connections[index];
    uint8_t sent = 0;
    int result;

    if (!connection->pending_active || connection->pending_sent) return OK;
    result = tcp_transmit(
        connection, connection->pending_sequence,
        connection->pending_flags, connection->pending_payload,
        connection->pending_length, &sent);
    if (result != OK) {
        LOG_WARN("NET", "Segmento TCP pendente nao foi transmitido");
        return result;
    }
    if (sent) {
        connection->pending_sent = 1;
        connection->pending_sent_tick = timer_get_ticks();
    }
    return OK;
}

static uint32_t tcp_pending_end(const tcp_connection_t* connection) {
    uint32_t consumed = connection->pending_length;

    if (connection->pending_flags & TCP_FLAG_SYN) consumed++;
    if (connection->pending_flags & TCP_FLAG_FIN) consumed++;
    return connection->pending_sequence + consumed;
}

static int tcp_queue_tracked(uint32_t index, uint16_t flags,
                             const uint8_t* payload, uint16_t length) {
    tcp_connection_t* connection = &tcp_connections[index];
    uint32_t consumed = length;

    if (connection->pending_active) return ERR_STATE;
    if (length > TCP_LOCAL_MSS || (length && !payload)) {
        LOG_ERROR("NET", "Payload invalido ao enfileirar TCP");
        return ERR_INVALID;
    }
    connection->pending_active = 1;
    connection->pending_sent = 0;
    connection->pending_retransmitted = 0;
    connection->retransmissions = 0;
    connection->pending_sequence = connection->send_next;
    connection->pending_flags = flags;
    connection->pending_length = length;
    if (length) kmemcpy(connection->pending_payload, payload, length);
    if (flags & TCP_FLAG_SYN) consumed++;
    if (flags & TCP_FLAG_FIN) consumed++;
    connection->send_next += consumed;
    return tcp_try_send_pending(index);
}

static uint32_t tcp_absolute_difference(uint32_t first,
                                        uint32_t second) {
    return first > second ? first - second : second - first;
}

static void tcp_update_rto(tcp_connection_t* connection,
                           uint32_t sample, uint32_t frequency) {
    uint32_t variation;
    uint32_t granularity = 1U;
    uint32_t minimum = tcp_seconds_to_ticks(
        TCP_INITIAL_RTO_SECONDS, frequency);
    uint32_t maximum = tcp_seconds_to_ticks(
        TCP_MAX_RTO_SECONDS, frequency);

    if (!sample || !frequency) return;
    if (!connection->srtt_ticks) {
        connection->srtt_ticks = sample;
        connection->rttvar_ticks = sample / 2U;
    } else {
        variation = tcp_absolute_difference(
            connection->srtt_ticks, sample);
        connection->rttvar_ticks =
            (3U * connection->rttvar_ticks + variation) / 4U;
        connection->srtt_ticks =
            (7U * connection->srtt_ticks + sample) / 8U;
    }
    variation = 4U * connection->rttvar_ticks;
    if (variation < granularity) variation = granularity;
    connection->rto_ticks = connection->srtt_ticks + variation;
    if (connection->rto_ticks < minimum) {
        connection->rto_ticks = minimum;
    }
    if (connection->rto_ticks > maximum) {
        connection->rto_ticks = maximum;
    }
}

static void tcp_clear_pending(uint32_t index) {
    tcp_connection_t* connection = &tcp_connections[index];
    uint32_t frequency = timer_get_frequency();
    uint32_t now = timer_get_ticks();

    if (connection->pending_sent &&
        !connection->pending_retransmitted) {
        tcp_update_rto(connection,
                       (uint32_t)(now - connection->pending_sent_tick),
                       frequency);
    }
    connection->pending_active = 0;
    connection->pending_sent = 0;
    connection->pending_length = 0;
    connection->pending_flags = 0;
    connection->retransmissions = 0;
}

static int tcp_parse_options(const uint8_t* segment,
                             uint16_t header_length,
                             uint16_t* out_mss) {
    uint16_t offset = TCP_HEADER_MIN_SIZE;

    if (!segment || !out_mss) {
        LOG_ERROR("NET", "Argumento nulo ao interpretar opcoes TCP");
        return ERR_NULL;
    }
    *out_mss = TCP_LOCAL_MSS;
    while (offset < header_length) {
        uint8_t kind = segment[offset++];
        uint8_t length;

        if (kind == TCP_OPTION_END) return OK;
        if (kind == TCP_OPTION_NOP) continue;
        if (offset >= header_length) return ERR_INVALID;
        length = segment[offset++];
        if (length < 2U ||
            (uint32_t)offset + length - 2U > header_length) {
            return ERR_INVALID;
        }
        if (kind == TCP_OPTION_MSS) {
            if (length != TCP_OPTION_MSS_LENGTH) return ERR_INVALID;
            *out_mss = tcp_read_u16(segment + offset);
            if (!*out_mss) return ERR_INVALID;
        }
        offset = (uint16_t)(offset + length - 2U);
    }
    return OK;
}

static int tcp_parse_segment(const ipv4_packet_view_t* packet,
                             tcp_segment_view_t* view) {
    uint8_t data_words;

    if (!packet || !view || packet->protocol != IPV4_PROTOCOL_TCP ||
        packet->payload_length < TCP_HEADER_MIN_SIZE) {
        LOG_WARN("NET", "Segmento TCP curto ou invalido");
        return ERR_INVALID;
    }
    data_words = packet->payload[TCP_OFFSET_DATA] >> 4U;
    view->header_length = (uint16_t)data_words * 4U;
    if (view->header_length < TCP_HEADER_MIN_SIZE ||
        view->header_length > TCP_HEADER_MAX_SIZE ||
        view->header_length > packet->payload_length ||
        (packet->payload[TCP_OFFSET_DATA] & 0x0FU) != 0U ||
        tcp_read_u16(packet->payload + TCP_OFFSET_URGENT) != 0U) {
        LOG_WARN("NET", "Cabecalho TCP possui tamanho ou urgente invalido");
        return ERR_INVALID;
    }
    view->source_port =
        tcp_read_u16(packet->payload + TCP_OFFSET_SOURCE_PORT);
    view->destination_port =
        tcp_read_u16(packet->payload + TCP_OFFSET_DESTINATION_PORT);
    view->sequence = tcp_read_u32(packet->payload + TCP_OFFSET_SEQUENCE);
    view->acknowledgment =
        tcp_read_u32(packet->payload + TCP_OFFSET_ACKNOWLEDGMENT);
    view->flags = packet->payload[TCP_OFFSET_FLAGS];
    view->window = tcp_read_u16(packet->payload + TCP_OFFSET_WINDOW);
    view->payload = packet->payload + view->header_length;
    view->payload_length = packet->payload_length - view->header_length;
    if (!view->source_port || !view->destination_port ||
        (view->flags & TCP_FLAG_URG) ||
        ((view->flags & TCP_FLAG_SYN) && (view->flags & TCP_FLAG_FIN)) ||
        tcp_parse_options(packet->payload, view->header_length,
                          &view->mss) != OK) {
        LOG_WARN("NET", "Campos ou opcoes TCP invalidos");
        return ERR_INVALID;
    }
    return OK;
}

static int tcp_send_raw_reset(const ipv4_packet_view_t* packet,
                              const tcp_segment_view_t* segment) {
    tcp_connection_t temporary;
    uint32_t acknowledgment = segment->sequence +
        segment->payload_length +
        ((segment->flags & TCP_FLAG_SYN) ? 1U : 0U) +
        ((segment->flags & TCP_FLAG_FIN) ? 1U : 0U);
    uint8_t sent = 0;
    uint16_t flags;

    if (segment->flags & TCP_FLAG_RST) return OK;
    kmemset(&temporary, 0, sizeof(temporary));
    temporary.remote_ip = packet->source_ip;
    temporary.local_port = segment->destination_port;
    temporary.remote_port = segment->source_port;
    temporary.local_window = 0;
    if (segment->flags & TCP_FLAG_ACK) {
        temporary.send_next = segment->acknowledgment;
        flags = TCP_FLAG_RST;
    } else {
        temporary.receive_next = acknowledgment;
        flags = TCP_FLAG_RST | TCP_FLAG_ACK;
    }
    return tcp_transmit(&temporary, temporary.send_next,
                        flags, NULL, 0U, &sent);
}

static int tcp_process_syn_sent(uint32_t index,
                                const tcp_segment_view_t* segment) {
    tcp_connection_t* connection = &tcp_connections[index];

    if (segment->flags & TCP_FLAG_RST) {
        if ((segment->flags & TCP_FLAG_ACK) &&
            segment->acknowledgment == connection->send_next) {
            tcp_status.resets_rx++;
            tcp_fail(index, ERR_UNAVAILABLE);
        }
        return OK;
    }
    if ((segment->flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) !=
            (TCP_FLAG_SYN | TCP_FLAG_ACK) ||
        segment->acknowledgment != connection->send_next ||
        segment->payload_length) {
        tcp_status.rx_invalid++;
        return OK;
    }
    connection->send_unacknowledged = segment->acknowledgment;
    tcp_clear_pending(index);
    connection->receive_next = segment->sequence + 1U;
    connection->remote_window = segment->window;
    connection->remote_mss =
        segment->mss < TCP_LOCAL_MSS ? segment->mss : TCP_LOCAL_MSS;
    connection->state = TCP_STATE_ESTABLISHED;
    connection->last_activity_tick = timer_get_ticks();
    tcp_status.syn_ack_rx++;
    tcp_send_ack(index);
    tcp_emit(index, TCP_EVENT_CONNECTED, NULL, 0U, OK);
    tcp_emit(index, TCP_EVENT_WRITABLE, NULL, 0U, OK);
    return OK;
}

static int tcp_begin_close(uint32_t index) {
    tcp_connection_t* connection = &tcp_connections[index];
    int result;

    if (connection->pending_active) return OK;
    if (connection->state == TCP_STATE_ESTABLISHED) {
        connection->state = TCP_STATE_FIN_WAIT_1;
    } else if (connection->state == TCP_STATE_CLOSE_WAIT) {
        connection->state = TCP_STATE_LAST_ACK;
    } else {
        return OK;
    }
    result = tcp_queue_tracked(
        index, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0U);
    if (result != OK) {
        tcp_fail(index, result);
        LOG_ERROR("NET", "Falha ao iniciar fechamento TCP");
    }
    return result;
}

static int tcp_process_ack(uint32_t index,
                           const tcp_segment_view_t* segment,
                           uint8_t* out_closed) {
    tcp_connection_t* connection = &tcp_connections[index];
    uint16_t pending_flags = connection->pending_flags;
    uint8_t completed = 0;

    *out_closed = 0;
    if (!(segment->flags & TCP_FLAG_ACK)) return OK;
    if (!tcp_sequence_between(segment->acknowledgment,
                              connection->send_unacknowledged,
                              connection->send_next)) {
        tcp_status.rx_invalid++;
        tcp_send_ack(index);
        return OK;
    }
    connection->remote_window = segment->window;
    if (tcp_sequence_less(connection->send_unacknowledged,
                          segment->acknowledgment)) {
        connection->send_unacknowledged = segment->acknowledgment;
        if (connection->pending_active &&
            !tcp_sequence_less(segment->acknowledgment,
                               tcp_pending_end(connection))) {
            completed = 1;
            tcp_clear_pending(index);
        }
    }
    if (completed && (pending_flags & TCP_FLAG_FIN)) {
        if (connection->state == TCP_STATE_FIN_WAIT_1) {
            connection->state = TCP_STATE_FIN_WAIT_2;
        } else if (connection->state == TCP_STATE_CLOSING) {
            connection->state = TCP_STATE_TIME_WAIT;
            connection->time_wait_tick = timer_get_ticks();
        } else if (connection->state == TCP_STATE_LAST_ACK) {
            tcp_clear_connection(index, 1U);
            *out_closed = 1;
            return OK;
        }
    }
    if (completed) {
        tcp_emit(index, TCP_EVENT_WRITABLE, NULL, 0U, OK);
    }
    if (connection->close_requested && !connection->pending_active) {
        return tcp_begin_close(index);
    }
    return OK;
}

static int tcp_accept_payload(uint32_t index,
                              const tcp_segment_view_t* segment) {
    tcp_connection_t* connection = &tcp_connections[index];

    if (!segment->payload_length) return OK;
    if (segment->sequence != connection->receive_next) {
        if (tcp_sequence_less(segment->sequence,
                              connection->receive_next)) {
            tcp_status.rx_duplicates++;
        } else {
            tcp_status.rx_out_of_order++;
        }
        tcp_send_ack(index);
        return OK;
    }
    if (segment->payload_length > connection->local_window) {
        tcp_status.rx_out_of_order++;
        tcp_send_ack(index);
        return OK;
    }
    if (tcp_emit(index, TCP_EVENT_DATA, segment->payload,
                 segment->payload_length, OK) != OK) {
        tcp_fail(index, ERR_OVERFLOW);
        LOG_WARN("NET", "Dados TCP nao couberam no consumidor");
        return ERR_OVERFLOW;
    }
    connection->receive_next += segment->payload_length;
    connection->local_window -= segment->payload_length;
    tcp_status.bytes_rx += segment->payload_length;
    tcp_send_ack(index);
    return OK;
}

static int tcp_accept_fin(uint32_t index,
                          const tcp_segment_view_t* segment) {
    tcp_connection_t* connection = &tcp_connections[index];
    uint32_t fin_sequence = segment->sequence + segment->payload_length;

    if (!(segment->flags & TCP_FLAG_FIN)) return OK;
    if (fin_sequence != connection->receive_next) {
        tcp_send_ack(index);
        return OK;
    }
    connection->receive_next++;
    tcp_status.fin_rx++;
    tcp_send_ack(index);
    tcp_emit(index, TCP_EVENT_EOF, NULL, 0U, OK);
    if (connection->state == TCP_STATE_ESTABLISHED) {
        connection->state = TCP_STATE_CLOSE_WAIT;
    } else if (connection->state == TCP_STATE_FIN_WAIT_1) {
        connection->state = connection->pending_active ?
                            TCP_STATE_CLOSING : TCP_STATE_TIME_WAIT;
        if (connection->state == TCP_STATE_TIME_WAIT) {
            connection->time_wait_tick = timer_get_ticks();
        }
    } else if (connection->state == TCP_STATE_FIN_WAIT_2) {
        connection->state = TCP_STATE_TIME_WAIT;
        connection->time_wait_tick = timer_get_ticks();
    }
    return OK;
}

static int tcp_process_connection(uint32_t index,
                                  const tcp_segment_view_t* segment) {
    tcp_connection_t* connection = &tcp_connections[index];
    uint8_t closed = 0;
    int result;

    connection->last_activity_tick = timer_get_ticks();
    if (connection->state == TCP_STATE_SYN_SENT) {
        return tcp_process_syn_sent(index, segment);
    }
    if (connection->state == TCP_STATE_TIME_WAIT) {
        if ((segment->flags & TCP_FLAG_FIN) &&
            segment->sequence + segment->payload_length + 1U ==
                connection->receive_next) {
            tcp_send_ack(index);
            connection->time_wait_tick = timer_get_ticks();
        }
        return OK;
    }
    if (segment->sequence != connection->receive_next) {
        if (tcp_sequence_less(segment->sequence,
                              connection->receive_next)) {
            tcp_status.rx_duplicates++;
        } else {
            tcp_status.rx_out_of_order++;
        }
        tcp_send_ack(index);
        return OK;
    }
    if (segment->flags & TCP_FLAG_RST) {
        tcp_status.resets_rx++;
        tcp_fail(index, ERR_UNAVAILABLE);
        return OK;
    }
    if (segment->flags & TCP_FLAG_SYN) {
        tcp_status.rx_invalid++;
        tcp_fail(index, ERR_STATE);
        return OK;
    }
    if (!(segment->flags & TCP_FLAG_ACK)) {
        tcp_status.rx_invalid++;
        tcp_send_ack(index);
        return OK;
    }
    result = tcp_process_ack(index, segment, &closed);
    if (result != OK || closed) return result;
    result = tcp_accept_payload(index, segment);
    if (result != OK) return result;
    return tcp_accept_fin(index, segment);
}

static int tcp_handle_ipv4(const ipv4_packet_view_t* packet) {
    tcp_segment_view_t segment;
    int32_t index;

    if (tcp_parse_segment(packet, &segment) != OK) {
        tcp_status.rx_invalid++;
        return OK;
    }
    if (tcp_checksum(packet->source_ip, packet->destination_ip,
                     packet->payload, packet->payload_length) != 0U) {
        tcp_status.rx_checksum_errors++;
        return OK;
    }
    tcp_status.segments_rx++;
    index = tcp_find_tuple(packet->source_ip, segment.destination_port,
                           segment.source_port);
    if (index < 0) {
        tcp_status.rx_no_connection++;
        if (tcp_send_raw_reset(packet, &segment) != OK) {
            LOG_WARN("NET", "RST TCP para porta fechada nao foi enviado");
        }
        return OK;
    }
    tcp_status.last_error = OK;
    return tcp_process_connection((uint32_t)index, &segment);
}

int tcp_init(void) {
    int result;

    LOG_INFO("NET", "Inicializando protocolo TCP");
    if (tcp_status.initialized) {
        LOG_WARN("NET", "Protocolo TCP ja estava inicializado");
        LOG_INFO("NET", "Protocolo TCP inicializado com sucesso");
        return OK;
    }
    kmemset(&tcp_status, 0, sizeof(tcp_status));
    kmemset(tcp_connections, 0, sizeof(tcp_connections));
    kmemset(tcp_generations, 0, sizeof(tcp_generations));
    tcp_isn_counter = 0;
    result = ipv4_register_handler(IPV4_PROTOCOL_TCP, tcp_handle_ipv4);
    if (result != OK) {
        tcp_status.last_error = result;
        LOG_ERROR("NET", "Falha ao registrar TCP no IPv4");
        return result;
    }
    tcp_status.initialized = 1;
    tcp_status.last_error = OK;
    LOG_INFO("NET", "Protocolo TCP inicializado com sucesso");
    return OK;
}

static uint32_t tcp_initial_sequence(uint32_t remote_ip,
                                     uint16_t local_port,
                                     uint16_t remote_port) {
    uint32_t sequence;

    tcp_isn_counter += TCP_ISN_INCREMENT;
    sequence = timer_get_ticks() ^ remote_ip ^
        ((uint32_t)local_port << 16U) ^ remote_port ^
        TCP_ISN_SALT ^ tcp_isn_counter;

    if (!sequence) sequence = 1U;
    return sequence;
}

int tcp_connect(uint16_t local_port, uint32_t remote_ip,
                uint16_t remote_port, uint16_t receive_window,
                tcp_event_fn callback,
                tcp_connection_handle_t* out_handle) {
    int32_t free_index = -1;
    uint32_t generation;
    uint32_t frequency;
    tcp_connection_t* connection;
    int result;

    if (!callback || !out_handle) {
        LOG_ERROR("NET", "Argumento nulo na conexao TCP");
        return ERR_NULL;
    }
    *out_handle = 0;
    if (!tcp_status.initialized) {
        LOG_ERROR("NET", "Conexao TCP antes da inicializacao");
        return ERR_STATE;
    }
    frequency = timer_get_frequency();
    if (!local_port || !remote_port || !receive_window ||
        receive_window > TCP_DEFAULT_RECEIVE_WINDOW || !frequency ||
        !ipv4_address_is_unicast(remote_ip) ||
        tcp_find_tuple(remote_ip, local_port, remote_port) >= 0) {
        LOG_ERROR("NET", "Parametros invalidos na conexao TCP");
        return ERR_INVALID;
    }
    for (uint32_t index = 0; index < TCP_CONNECTION_CAPACITY; index++) {
        if (!tcp_connections[index].active) {
            free_index = (int32_t)index;
            break;
        }
    }
    if (free_index < 0) {
        LOG_ERROR("NET", "Tabela de conexoes TCP cheia");
        return ERR_OVERFLOW;
    }
    generation = tcp_generations[free_index] + 1U;
    if (!generation || generation > TCP_HANDLE_GENERATION_MAX) {
        generation = 1U;
    }
    tcp_generations[free_index] = generation;
    connection = &tcp_connections[free_index];
    kmemset(connection, 0, sizeof(*connection));
    connection->active = 1;
    connection->generation = generation;
    connection->state = TCP_STATE_SYN_SENT;
    connection->local_port = local_port;
    connection->remote_port = remote_port;
    connection->remote_ip = remote_ip;
    connection->local_window = receive_window;
    connection->remote_mss = TCP_LOCAL_MSS;
    connection->send_unacknowledged =
        tcp_initial_sequence(remote_ip, local_port, remote_port);
    connection->send_next = connection->send_unacknowledged;
    connection->rto_ticks = tcp_seconds_to_ticks(
        TCP_INITIAL_RTO_SECONDS, frequency);
    connection->last_activity_tick = timer_get_ticks();
    connection->callback = callback;
    tcp_status.connection_count++;
    *out_handle = tcp_make_handle((uint32_t)free_index);
    result = tcp_queue_tracked((uint32_t)free_index, TCP_FLAG_SYN,
                               NULL, 0U);
    if (result != OK) {
        tcp_status.last_error = result;
        tcp_clear_connection((uint32_t)free_index, 0U);
        *out_handle = 0;
        LOG_ERROR("NET", "Falha ao iniciar handshake TCP");
        return result;
    }
    return OK;
}

int tcp_send(tcp_connection_handle_t handle, const uint8_t* data,
             uint16_t length, uint16_t* out_accepted) {
    int32_t index;
    tcp_connection_t* connection;
    uint16_t accepted;
    int result;

    if (!data || !out_accepted) {
        LOG_ERROR("NET", "Argumento nulo no envio TCP");
        return ERR_NULL;
    }
    *out_accepted = 0;
    if (!tcp_status.initialized) {
        LOG_ERROR("NET", "Envio TCP antes da inicializacao");
        return ERR_STATE;
    }
    index = tcp_find_handle(handle);
    if (index < 0) {
        LOG_ERROR("NET", "Handle TCP invalido no envio");
        return ERR_INVALID;
    }
    connection = &tcp_connections[index];
    if ((connection->state != TCP_STATE_ESTABLISHED &&
         connection->state != TCP_STATE_CLOSE_WAIT) ||
        connection->pending_active || !length ||
        !connection->remote_window) return OK;
    accepted = length;
    if (accepted > TCP_LOCAL_MSS) accepted = TCP_LOCAL_MSS;
    if (accepted > connection->remote_mss) {
        accepted = connection->remote_mss;
    }
    if (accepted > connection->remote_window) {
        accepted = connection->remote_window;
    }
    result = tcp_queue_tracked((uint32_t)index,
                               TCP_FLAG_ACK | TCP_FLAG_PSH,
                               data, accepted);
    if (result != OK) {
        tcp_fail((uint32_t)index, result);
        LOG_ERROR("NET", "Falha ao enfileirar dados TCP");
        return result;
    }
    connection->remote_window -= accepted;
    *out_accepted = accepted;
    return OK;
}

int tcp_set_receive_window(tcp_connection_handle_t handle,
                           uint16_t receive_window) {
    int32_t index;
    uint16_t previous;

    if (!tcp_status.initialized) {
        LOG_ERROR("NET", "Janela TCP antes da inicializacao");
        return ERR_STATE;
    }
    if (receive_window > TCP_DEFAULT_RECEIVE_WINDOW) {
        LOG_ERROR("NET", "Janela TCP excede limite local");
        return ERR_INVALID;
    }
    index = tcp_find_handle(handle);
    if (index < 0) {
        LOG_ERROR("NET", "Handle TCP invalido ao ajustar janela");
        return ERR_INVALID;
    }
    previous = tcp_connections[index].local_window;
    tcp_connections[index].local_window = receive_window;
    if (tcp_connections[index].state == TCP_STATE_ESTABLISHED &&
        receive_window > previous &&
        (!previous || receive_window - previous >= TCP_LOCAL_MSS)) {
        return tcp_send_ack((uint32_t)index);
    }
    return OK;
}

int tcp_close(tcp_connection_handle_t handle) {
    int32_t index;
    tcp_connection_t* connection;

    if (!tcp_status.initialized) {
        LOG_ERROR("NET", "Fechamento TCP antes da inicializacao");
        return ERR_STATE;
    }
    index = tcp_find_handle(handle);
    if (index < 0) {
        LOG_ERROR("NET", "Handle TCP invalido no fechamento");
        return ERR_INVALID;
    }
    connection = &tcp_connections[index];
    if (connection->state == TCP_STATE_SYN_SENT ||
        connection->state == TCP_STATE_FAILED) {
        tcp_clear_connection((uint32_t)index, 1U);
        return OK;
    }
    connection->close_requested = 1;
    return tcp_begin_close((uint32_t)index);
}

int tcp_abort(tcp_connection_handle_t handle) {
    int32_t index;
    tcp_connection_t* connection;
    uint8_t sent = 0;

    if (!tcp_status.initialized) {
        LOG_ERROR("NET", "Aborto TCP antes da inicializacao");
        return ERR_STATE;
    }
    index = tcp_find_handle(handle);
    if (index < 0) {
        LOG_ERROR("NET", "Handle TCP invalido no aborto");
        return ERR_INVALID;
    }
    connection = &tcp_connections[index];
    if (connection->state != TCP_STATE_SYN_SENT &&
        connection->state != TCP_STATE_FAILED &&
        connection->state != TCP_STATE_TIME_WAIT) {
        tcp_transmit(connection, connection->send_next,
                     TCP_FLAG_RST | TCP_FLAG_ACK, NULL, 0U, &sent);
    }
    tcp_clear_connection((uint32_t)index, 0U);
    return OK;
}

static int tcp_maintain_pending(uint32_t index, uint32_t now,
                                uint32_t maximum_rto) {
    tcp_connection_t* connection = &tcp_connections[index];
    int result;

    if (!connection->pending_active) return OK;
    if (!connection->pending_sent) {
        result = tcp_try_send_pending(index);
        if (result != OK) {
            tcp_fail(index, result);
            LOG_WARN("NET", "Envio TCP falhou antes de iniciar RTO");
        }
        return OK;
    }
    if (!tcp_elapsed(now, connection->pending_sent_tick,
                     connection->rto_ticks)) return OK;
    if (connection->retransmissions >= TCP_MAX_RETRANSMISSIONS) {
        tcp_status.timeouts++;
        tcp_fail(index, ERR_TIMEOUT);
        return OK;
    }
    connection->retransmissions++;
    connection->pending_retransmitted = 1;
    connection->pending_sent = 0;
    if (connection->rto_ticks > maximum_rto / 2U) {
        connection->rto_ticks = maximum_rto;
    } else {
        connection->rto_ticks *= 2U;
    }
    tcp_status.retransmissions++;
    result = tcp_try_send_pending(index);
    if (result != OK && result != ERR_TIMEOUT) {
        connection->last_error = result;
        LOG_WARN("NET", "Retransmissao TCP nao foi enviada");
    }
    return OK;
}

static int tcp_maintain_connection(uint32_t index, uint32_t now,
                                   uint32_t idle_ticks,
                                   uint32_t time_wait_ticks,
                                   uint32_t maximum_rto) {
    tcp_connection_t* connection = &tcp_connections[index];

    if (connection->state == TCP_STATE_TIME_WAIT) {
        if (tcp_elapsed(now, connection->time_wait_tick,
                        time_wait_ticks)) {
            tcp_clear_connection(index, 1U);
        }
        return OK;
    }
    if (connection->state == TCP_STATE_FAILED) return OK;
    if (tcp_maintain_pending(index, now, maximum_rto) != OK) {
        LOG_WARN("NET", "Estado pendente TCP inconsistente");
        return ERR_STATE;
    }
    if (connection->state == TCP_STATE_FAILED ||
        (connection->pending_active &&
         !connection->pending_sent)) return OK;
    if (tcp_elapsed(now, connection->last_activity_tick, idle_ticks)) {
        tcp_status.timeouts++;
        tcp_fail(index, ERR_TIMEOUT);
        return OK;
    }
    if (connection->active && connection->close_requested &&
        !connection->pending_active) {
        return tcp_begin_close(index);
    }
    return OK;
}

int tcp_maintain(void) {
    uint32_t frequency;
    uint32_t now;
    uint32_t idle_ticks;
    uint32_t time_wait_ticks;
    uint32_t maximum_rto;

    if (!tcp_status.initialized) {
        LOG_ERROR("NET", "Manutencao TCP antes da inicializacao");
        return ERR_STATE;
    }
    frequency = timer_get_frequency();
    if (!frequency) {
        LOG_ERROR("NET", "Timer indisponivel na manutencao TCP");
        return ERR_STATE;
    }
    tcp_status.maintenance_cycles++;
    now = timer_get_ticks();
    idle_ticks = tcp_seconds_to_ticks(TCP_IDLE_TIMEOUT_SECONDS,
                                      frequency);
    time_wait_ticks = tcp_seconds_to_ticks(TCP_TIME_WAIT_SECONDS,
                                           frequency);
    maximum_rto = tcp_seconds_to_ticks(TCP_MAX_RTO_SECONDS, frequency);
    for (uint32_t index = 0; index < TCP_CONNECTION_CAPACITY; index++) {
        if (!tcp_connections[index].active) continue;
        if (tcp_maintain_connection(index, now, idle_ticks,
                                    time_wait_ticks,
                                    maximum_rto) != OK) {
            LOG_WARN("NET", "Manutencao de conexao TCP falhou");
        }
    }
    return OK;
}

int tcp_reset(void) {
    if (!tcp_status.initialized) {
        LOG_ERROR("NET", "Reset TCP antes da inicializacao");
        return ERR_STATE;
    }
    for (uint32_t index = 0; index < TCP_CONNECTION_CAPACITY; index++) {
        tcp_clear_connection(index, 1U);
    }
    tcp_status.connection_count = 0;
    tcp_status.last_error = OK;
    LOG_INFO("NET", "Conexoes TCP reiniciadas");
    return OK;
}

int tcp_get_status(tcp_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("NET", "Destino nulo ao consultar TCP");
        return ERR_NULL;
    }
    *out_status = tcp_status;
    return OK;
}

int tcp_get_connection_info(uint32_t index,
                            tcp_connection_info_t* out_info) {
    tcp_connection_t* connection;
    uint32_t frequency;

    if (!out_info) {
        LOG_ERROR("NET", "Destino nulo ao consultar conexao TCP");
        return ERR_NULL;
    }
    if (index >= TCP_CONNECTION_CAPACITY) {
        LOG_ERROR("NET", "Indice de conexao TCP invalido");
        return ERR_INVALID;
    }
    kmemset(out_info, 0, sizeof(*out_info));
    connection = &tcp_connections[index];
    if (!connection->active) return OK;
    frequency = timer_get_frequency();
    out_info->used = 1;
    out_info->handle = tcp_make_handle(index);
    out_info->state = connection->state;
    out_info->remote_ip = connection->remote_ip;
    out_info->local_port = connection->local_port;
    out_info->remote_port = connection->remote_port;
    out_info->send_unacknowledged = connection->send_unacknowledged;
    out_info->send_next = connection->send_next;
    out_info->receive_next = connection->receive_next;
    out_info->local_window = connection->local_window;
    out_info->remote_window = connection->remote_window;
    out_info->remote_mss = connection->remote_mss;
    out_info->pending_length = connection->pending_length;
    out_info->retransmissions = connection->retransmissions;
    out_info->rto_milliseconds = frequency ?
        connection->rto_ticks * 1000U / frequency : 0U;
    out_info->idle_seconds = frequency ?
        (uint32_t)(timer_get_ticks() -
                   connection->last_activity_tick) / frequency : 0U;
    out_info->last_error = connection->last_error;
    return OK;
}

static int tcp_validate_checksum_vector(void) {
    uint8_t segment[TCP_HEADER_MIN_SIZE + 3U];
    uint16_t checksum;

    kmemset(segment, 0, sizeof(segment));
    tcp_write_u16(segment + TCP_OFFSET_SOURCE_PORT, 49152U);
    tcp_write_u16(segment + TCP_OFFSET_DESTINATION_PORT, 80U);
    tcp_write_u32(segment + TCP_OFFSET_SEQUENCE, 1U);
    segment[TCP_OFFSET_DATA] = 5U << 4U;
    segment[TCP_OFFSET_FLAGS] = TCP_FLAG_ACK | TCP_FLAG_PSH;
    tcp_write_u16(segment + TCP_OFFSET_WINDOW, 4096U);
    segment[TCP_HEADER_MIN_SIZE] = 'A';
    segment[TCP_HEADER_MIN_SIZE + 1U] = 'B';
    segment[TCP_HEADER_MIN_SIZE + 2U] = 'C';
    checksum = tcp_checksum(0x0A00020FU, 0x5DB8D822U,
                            segment, sizeof(segment));
    if (!checksum) checksum = 0xFFFFU;
    tcp_write_u16(segment + TCP_OFFSET_CHECKSUM, checksum);
    if (tcp_checksum(0x0A00020FU, 0x5DB8D822U,
                     segment, sizeof(segment)) != 0U) {
        LOG_ERROR("NET", "Vetor de checksum TCP falhou");
        return ERR_STATE;
    }
    return OK;
}

static int tcp_validate_option_vector(void) {
    uint8_t segment[TCP_SYN_HEADER_SIZE];
    uint16_t mss = 0;

    kmemset(segment, 0, sizeof(segment));
    segment[TCP_HEADER_MIN_SIZE] = TCP_OPTION_MSS;
    segment[TCP_HEADER_MIN_SIZE + 1U] = TCP_OPTION_MSS_LENGTH;
    tcp_write_u16(segment + TCP_HEADER_MIN_SIZE + 2U, 1460U);
    if (tcp_parse_options(segment, sizeof(segment), &mss) != OK ||
        mss != 1460U) {
        LOG_ERROR("NET", "Vetor de opcao MSS TCP falhou");
        return ERR_STATE;
    }
    segment[TCP_HEADER_MIN_SIZE + 1U] = 5U;
    if (tcp_parse_options(segment, sizeof(segment), &mss) !=
        ERR_INVALID) {
        LOG_ERROR("NET", "Opcao TCP truncada foi aceita");
        return ERR_STATE;
    }
    return OK;
}

static int tcp_validate_rto_vector(void) {
    tcp_connection_t connection;

    kmemset(&connection, 0, sizeof(connection));
    tcp_update_rto(&connection, 50U, 50U);
    if (connection.srtt_ticks != 50U ||
        connection.rttvar_ticks != 25U ||
        connection.rto_ticks != 150U) {
        LOG_ERROR("NET", "Primeiro vetor RTO TCP falhou");
        return ERR_STATE;
    }
    tcp_update_rto(&connection, 50U, 50U);
    if (connection.srtt_ticks != 50U ||
        connection.rttvar_ticks != 18U ||
        connection.rto_ticks != 122U) {
        LOG_ERROR("NET", "Segundo vetor RTO TCP falhou");
        return ERR_STATE;
    }
    return OK;
}

int tcp_validate_state(void) {
    uint32_t active = 0;

    if (tcp_validate_checksum_vector() != OK ||
        tcp_validate_option_vector() != OK ||
        tcp_validate_rto_vector() != OK ||
        !tcp_sequence_less(0xFFFFFFF0U, 0x00000010U) ||
        tcp_sequence_less(0x00000010U, 0xFFFFFFF0U)) {
        LOG_ERROR("NET", "Vetores puros TCP falharam");
        return ERR_STATE;
    }
    for (uint32_t index = 0; index < TCP_CONNECTION_CAPACITY; index++) {
        tcp_connection_t* connection = &tcp_connections[index];

        if (!connection->active) continue;
        active++;
        if (!connection->generation || !connection->callback ||
            !connection->local_port || !connection->remote_port ||
            !ipv4_address_is_unicast(connection->remote_ip) ||
            connection->state == TCP_STATE_CLOSED ||
            connection->state > TCP_STATE_LISTEN ||
            connection->pending_length > TCP_LOCAL_MSS ||
            connection->local_window >
                TCP_DEFAULT_RECEIVE_WINDOW ||
            !connection->remote_mss || !connection->rto_ticks ||
            (!connection->pending_active &&
             connection->pending_sent)) {
            LOG_ERROR("NET", "Conexao TCP inconsistente");
            return ERR_STATE;
        }
        for (uint32_t other = index + 1U;
             other < TCP_CONNECTION_CAPACITY; other++) {
            if (tcp_connections[other].active &&
                tcp_connections[other].remote_ip ==
                    connection->remote_ip &&
                tcp_connections[other].local_port ==
                    connection->local_port &&
                tcp_connections[other].remote_port ==
                    connection->remote_port) {
                LOG_ERROR("NET", "Tupla TCP duplicada");
                return ERR_STATE;
            }
        }
    }
    if (active != tcp_status.connection_count ||
        active > TCP_CONNECTION_CAPACITY ||
        (!tcp_status.initialized && active)) {
        LOG_ERROR("NET", "Estado global TCP inconsistente");
        return ERR_STATE;
    }
    return OK;
}

const char* tcp_state_name(tcp_state_t state) {
    if (state == TCP_STATE_CLOSED) return "CLOSED";
    if (state == TCP_STATE_SYN_SENT) return "SYN_SENT";
    if (state == TCP_STATE_ESTABLISHED) return "ESTABLISHED";
    if (state == TCP_STATE_FIN_WAIT_1) return "FIN_WAIT_1";
    if (state == TCP_STATE_FIN_WAIT_2) return "FIN_WAIT_2";
    if (state == TCP_STATE_CLOSING) return "CLOSING";
    if (state == TCP_STATE_CLOSE_WAIT) return "CLOSE_WAIT";
    if (state == TCP_STATE_LAST_ACK) return "LAST_ACK";
    if (state == TCP_STATE_TIME_WAIT) return "TIME_WAIT";
    if (state == TCP_STATE_FAILED) return "FAILED";
    if (state == TCP_STATE_LISTEN) return "LISTEN";
    return "DESCONHECIDO";
}
