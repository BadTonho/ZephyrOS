#include "core/dhcp.h"
#include "core/errors.h"
#include "core/ipv4.h"
#include "core/log.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/udp.h"

#define DHCP_BOOTREQUEST 1U
#define DHCP_BOOTREPLY 2U
#define DHCP_HTYPE_ETHERNET 1U
#define DHCP_BROADCAST_FLAG 0x8000U
#define DHCP_MAGIC_COOKIE 0x63825363U
#define DHCP_FIXED_SIZE 240U
#define DHCP_MIN_MESSAGE_SIZE 300U
#define DHCP_MAX_ATTEMPTS 4U
#define DHCP_FINAL_GRACE_SECONDS 1U
#define DHCP_MIN_LEASE_SECONDS 4U
#define DHCP_T2_NUMERATOR 7U
#define DHCP_T2_DENOMINATOR 8U
#define DHCP_MAX_SAFE_TICKS 0x7FFFFFFFU

#define DHCP_MESSAGE_DISCOVER 1U
#define DHCP_MESSAGE_OFFER 2U
#define DHCP_MESSAGE_REQUEST 3U
#define DHCP_MESSAGE_ACK 5U
#define DHCP_MESSAGE_NAK 6U
#define DHCP_MESSAGE_RELEASE 7U

#define DHCP_OPTION_PAD 0U
#define DHCP_OPTION_SUBNET_MASK 1U
#define DHCP_OPTION_ROUTER 3U
#define DHCP_OPTION_DNS 6U
#define DHCP_OPTION_REQUESTED_IP 50U
#define DHCP_OPTION_LEASE_TIME 51U
#define DHCP_OPTION_MESSAGE_TYPE 53U
#define DHCP_OPTION_SERVER_ID 54U
#define DHCP_OPTION_PARAMETER_LIST 55U
#define DHCP_OPTION_T1 58U
#define DHCP_OPTION_T2 59U
#define DHCP_OPTION_CLIENT_ID 61U
#define DHCP_OPTION_END 255U

#define DHCP_OFFSET_OP 0U
#define DHCP_OFFSET_HTYPE 1U
#define DHCP_OFFSET_HLEN 2U
#define DHCP_OFFSET_XID 4U
#define DHCP_OFFSET_FLAGS 10U
#define DHCP_OFFSET_CIADDR 12U
#define DHCP_OFFSET_YIADDR 16U
#define DHCP_OFFSET_CHADDR 28U
#define DHCP_OFFSET_COOKIE 236U
#define DHCP_OFFSET_OPTIONS 240U

typedef struct {
    uint8_t message_type;
    uint8_t has_message_type;
    uint8_t has_mask;
    uint8_t has_gateway;
    uint8_t has_dns;
    uint8_t has_lease;
    uint8_t has_server;
    uint8_t has_t1;
    uint8_t has_t2;
    uint32_t yiaddr;
    uint32_t subnet_mask;
    uint32_t gateway;
    uint32_t dns_server;
    uint32_t lease_seconds;
    uint32_t server_identifier;
    uint32_t t1_seconds;
    uint32_t t2_seconds;
} dhcp_parsed_t;

static dhcp_status_t dhcp_status;
static udp_endpoint_handle_t dhcp_endpoint;
static dhcp_lease_t dhcp_offer;
static dhcp_lease_t dhcp_pending_lease;
static dhcp_event_t dhcp_pending_event;
static dhcp_state_t dhcp_drop_target;
static uint32_t dhcp_last_attempt_tick;
static uint32_t dhcp_retry_ticks;
static uint32_t dhcp_bound_tick;
static uint8_t dhcp_apply_was_bound;
static uint8_t dhcp_tx_buffer[DHCP_MAX_MESSAGE_SIZE];

static uint32_t dhcp_read_u32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) | data[3];
}

static void dhcp_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

static void dhcp_write_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static uint8_t dhcp_mac_equal(const uint8_t* first,
                              const uint8_t* second) {
    for (uint32_t index = 0; index < DHCP_MAC_ADDRESS_SIZE; index++) {
        if (first[index] != second[index]) return 0;
    }
    return 1;
}

static uint8_t dhcp_mac_valid(const uint8_t* mac) {
    uint8_t nonzero = 0;

    if (!mac || (mac[0] & 0x01U)) return 0;
    for (uint32_t index = 0; index < DHCP_MAC_ADDRESS_SIZE; index++) {
        if (mac[index]) nonzero = 1;
    }
    return nonzero;
}

static uint8_t dhcp_text_is_equal(const char* first,
                                  const char* second) {
    uint32_t index = 0;

    if (!first || !second) return 0;
    while (first[index] && second[index] &&
           first[index] == second[index]) {
        index++;
    }
    return first[index] == second[index];
}

static int dhcp_copy_text(char* destination, uint32_t capacity,
                          const char* source) {
    uint32_t length = 0;

    if (!destination || !source || !capacity) {
        LOG_ERROR("NET", "Texto DHCP invalido");
        return ERR_NULL;
    }
    while (source[length]) {
        if (length + 1U >= capacity) {
            LOG_ERROR("NET", "ID de interface excede limite DHCP");
            return ERR_OVERFLOW;
        }
        destination[length] = source[length];
        length++;
    }
    destination[length] = '\0';
    if (!length) {
        LOG_ERROR("NET", "ID vazio na configuracao DHCP");
        return ERR_INVALID;
    }
    return OK;
}

static uint32_t dhcp_seconds_to_ticks(uint32_t seconds,
                                      uint32_t frequency) {
    if (!frequency || !seconds) return 0;
    if (seconds > DHCP_MAX_SAFE_TICKS / frequency) {
        return DHCP_MAX_SAFE_TICKS;
    }
    return seconds * frequency;
}

static uint8_t dhcp_elapsed(uint32_t now, uint32_t start,
                            uint32_t interval) {
    return interval && (uint32_t)(now - start) >= interval;
}

static uint8_t dhcp_lease_valid(const dhcp_lease_t* lease) {
    uint32_t network;
    uint32_t broadcast;

    if (!lease || !ipv4_address_is_unicast(lease->address) ||
        !ipv4_mask_is_valid(lease->subnet_mask) ||
        !ipv4_address_is_unicast(lease->server_identifier) ||
        lease->lease_seconds < DHCP_MIN_LEASE_SECONDS) return 0;
    network = lease->address & lease->subnet_mask;
    broadcast = network | ~lease->subnet_mask;
    if (lease->address == network || lease->address == broadcast) return 0;
    if (lease->gateway &&
        (!ipv4_address_is_unicast(lease->gateway) ||
         (lease->gateway & lease->subnet_mask) != network ||
         lease->gateway == lease->address ||
         lease->gateway == network ||
         lease->gateway == broadcast)) return 0;
    if (lease->dns_server &&
        (!ipv4_address_is_unicast(lease->dns_server) ||
         lease->dns_server == lease->address)) return 0;
    return lease->t1_seconds > 0U &&
           lease->t1_seconds < lease->t2_seconds &&
           lease->t2_seconds < lease->lease_seconds;
}

static void dhcp_set_default_timers(dhcp_lease_t* lease) {
    if (!lease) return;
    if (!lease->t1_seconds || !lease->t2_seconds ||
        lease->t1_seconds >= lease->t2_seconds ||
        lease->t2_seconds >= lease->lease_seconds) {
        lease->t1_seconds = lease->lease_seconds / 2U;
        lease->t2_seconds =
            (lease->lease_seconds / DHCP_T2_DENOMINATOR) *
                DHCP_T2_NUMERATOR;
        lease->t2_seconds +=
            (lease->lease_seconds % DHCP_T2_DENOMINATOR) *
                DHCP_T2_NUMERATOR / DHCP_T2_DENOMINATOR;
        if (lease->t2_seconds <= lease->t1_seconds) {
            lease->t2_seconds = lease->t1_seconds + 1U;
        }
    }
}

static int dhcp_append_option(uint16_t* offset, uint8_t code,
                              const uint8_t* data, uint8_t length) {
    if (!offset || (length && !data)) {
        LOG_ERROR("NET", "Argumento nulo ao montar opcao DHCP");
        return ERR_NULL;
    }
    if ((uint32_t)*offset + 2U + length > DHCP_MAX_MESSAGE_SIZE) {
        LOG_ERROR("NET", "Opcao DHCP excede buffer");
        return ERR_OVERFLOW;
    }
    dhcp_tx_buffer[(*offset)++] = code;
    dhcp_tx_buffer[(*offset)++] = length;
    if (length) {
        kmemcpy(dhcp_tx_buffer + *offset, data, length);
        *offset = (uint16_t)(*offset + length);
    }
    return OK;
}

static int dhcp_append_u32_option(uint16_t* offset, uint8_t code,
                                  uint32_t value) {
    uint8_t data[4];

    dhcp_write_u32(data, value);
    return dhcp_append_option(offset, code, data, sizeof(data));
}

static void dhcp_build_base(uint16_t flags, uint32_t ciaddr) {
    kmemset(dhcp_tx_buffer, 0, sizeof(dhcp_tx_buffer));
    dhcp_tx_buffer[DHCP_OFFSET_OP] = DHCP_BOOTREQUEST;
    dhcp_tx_buffer[DHCP_OFFSET_HTYPE] = DHCP_HTYPE_ETHERNET;
    dhcp_tx_buffer[DHCP_OFFSET_HLEN] = DHCP_MAC_ADDRESS_SIZE;
    dhcp_write_u32(dhcp_tx_buffer + DHCP_OFFSET_XID,
                   dhcp_status.transaction_id);
    dhcp_write_u16(dhcp_tx_buffer + DHCP_OFFSET_FLAGS, flags);
    dhcp_write_u32(dhcp_tx_buffer + DHCP_OFFSET_CIADDR, ciaddr);
    kmemcpy(dhcp_tx_buffer + DHCP_OFFSET_CHADDR,
            dhcp_status.local_mac, DHCP_MAC_ADDRESS_SIZE);
    dhcp_write_u32(dhcp_tx_buffer + DHCP_OFFSET_COOKIE,
                   DHCP_MAGIC_COOKIE);
}

static int dhcp_append_identity(uint16_t* offset,
                                uint8_t message_type,
                                uint8_t include_parameters) {
    uint8_t client_id[DHCP_MAC_ADDRESS_SIZE + 1U];
    uint8_t requested[] = {
        DHCP_OPTION_SUBNET_MASK, DHCP_OPTION_ROUTER, DHCP_OPTION_DNS,
        DHCP_OPTION_LEASE_TIME, DHCP_OPTION_T1, DHCP_OPTION_T2
    };
    int result;

    client_id[0] = DHCP_HTYPE_ETHERNET;
    kmemcpy(client_id + 1U, dhcp_status.local_mac,
            DHCP_MAC_ADDRESS_SIZE);
    result = dhcp_append_option(offset, DHCP_OPTION_MESSAGE_TYPE,
                                &message_type, 1U);
    if (result == OK) {
        result = dhcp_append_option(offset, DHCP_OPTION_CLIENT_ID,
                                    client_id, sizeof(client_id));
    }
    if (result == OK && include_parameters) {
        result = dhcp_append_option(offset, DHCP_OPTION_PARAMETER_LIST,
                                    requested, sizeof(requested));
    }
    return result;
}

static int dhcp_finish_message(uint16_t* offset,
                               uint16_t* out_length) {
    if (!offset || !out_length) {
        LOG_ERROR("NET", "Destino nulo ao concluir mensagem DHCP");
        return ERR_NULL;
    }
    if (*offset >= DHCP_MAX_MESSAGE_SIZE) {
        LOG_ERROR("NET", "Mensagem DHCP excede buffer");
        return ERR_OVERFLOW;
    }
    dhcp_tx_buffer[(*offset)++] = DHCP_OPTION_END;
    if (*offset < DHCP_MIN_MESSAGE_SIZE) *offset = DHCP_MIN_MESSAGE_SIZE;
    *out_length = *offset;
    return OK;
}

static int dhcp_build_discover(uint16_t* out_length) {
    uint16_t offset = DHCP_OFFSET_OPTIONS;
    int result;

    dhcp_build_base(DHCP_BROADCAST_FLAG, 0U);
    result = dhcp_append_identity(&offset, DHCP_MESSAGE_DISCOVER, 1U);
    if (result != OK) return result;
    return dhcp_finish_message(&offset, out_length);
}

static int dhcp_build_request(uint8_t selecting,
                              uint8_t rebinding,
                              uint16_t* out_length) {
    uint16_t offset = DHCP_OFFSET_OPTIONS;
    uint16_t flags = selecting || rebinding ? DHCP_BROADCAST_FLAG : 0U;
    uint32_t ciaddr = selecting ? 0U : dhcp_status.lease.address;
    int result;

    dhcp_build_base(flags, ciaddr);
    result = dhcp_append_identity(&offset, DHCP_MESSAGE_REQUEST, 1U);
    if (result == OK && selecting) {
        result = dhcp_append_u32_option(
            &offset, DHCP_OPTION_REQUESTED_IP, dhcp_offer.address);
    }
    if (result == OK && selecting) {
        result = dhcp_append_u32_option(
            &offset, DHCP_OPTION_SERVER_ID,
            dhcp_offer.server_identifier);
    }
    if (result != OK) return result;
    return dhcp_finish_message(&offset, out_length);
}

static int dhcp_build_release(uint16_t* out_length) {
    uint16_t offset = DHCP_OFFSET_OPTIONS;
    int result;

    dhcp_build_base(0U, dhcp_status.lease.address);
    result = dhcp_append_identity(&offset, DHCP_MESSAGE_RELEASE, 0U);
    if (result == OK) {
        result = dhcp_append_u32_option(
            &offset, DHCP_OPTION_SERVER_ID,
            dhcp_status.lease.server_identifier);
    }
    if (result != OK) return result;
    return dhcp_finish_message(&offset, out_length);
}

static void dhcp_record_attempt(uint32_t now,
                                uint32_t retry_ticks) {
    dhcp_status.attempts++;
    dhcp_last_attempt_tick = now;
    dhcp_retry_ticks = retry_ticks;
}

static uint32_t dhcp_acquisition_retry_ticks(uint32_t frequency) {
    uint32_t next_attempt = (uint32_t)dhcp_status.attempts + 1U;
    uint32_t seconds = next_attempt >= DHCP_MAX_ATTEMPTS ?
                       DHCP_FINAL_GRACE_SECONDS :
                       (1U << (next_attempt - 1U));

    return dhcp_seconds_to_ticks(seconds, frequency);
}

static int dhcp_send_discover(void) {
    uint16_t length;
    uint32_t frequency = timer_get_frequency();
    uint8_t sent = 0;
    int result;

    result = dhcp_build_discover(&length);
    if (result == OK) {
        result = udp_send_limited_broadcast(
            dhcp_endpoint, dhcp_status.interface_id, 0U,
            DHCP_SERVER_PORT,
            dhcp_tx_buffer, length, &sent);
    }
    if (result != OK || !sent || !frequency) {
        LOG_ERROR("NET", "Falha ao transmitir DHCPDISCOVER");
        return result != OK ? result : ERR_STATE;
    }
    dhcp_status.discovers_tx++;
    dhcp_record_attempt(timer_get_ticks(),
                        dhcp_acquisition_retry_ticks(frequency));
    return OK;
}

static int dhcp_send_selecting_request(void) {
    uint16_t length;
    uint32_t frequency = timer_get_frequency();
    uint8_t sent = 0;
    int result;

    result = dhcp_build_request(1U, 0U, &length);
    if (result == OK) {
        result = udp_send_limited_broadcast(
            dhcp_endpoint, dhcp_status.interface_id, 0U,
            DHCP_SERVER_PORT,
            dhcp_tx_buffer, length, &sent);
    }
    if (result != OK || !sent || !frequency) {
        LOG_ERROR("NET", "Falha ao transmitir DHCPREQUEST inicial");
        return result != OK ? result : ERR_STATE;
    }
    dhcp_status.requests_tx++;
    dhcp_record_attempt(timer_get_ticks(),
                        dhcp_acquisition_retry_ticks(frequency));
    return OK;
}

static uint32_t dhcp_adaptive_retry(uint32_t deadline_seconds) {
    uint32_t frequency = timer_get_frequency();
    uint32_t elapsed_seconds =
        frequency ? (uint32_t)(timer_get_ticks() - dhcp_bound_tick) /
                        frequency : 0U;
    uint32_t remaining = deadline_seconds > elapsed_seconds ?
                         deadline_seconds - elapsed_seconds : 1U;
    uint32_t delay = remaining / 2U;

    if (!delay) delay = 1U;
    return dhcp_seconds_to_ticks(delay, frequency);
}

static int dhcp_send_bound_request(uint8_t rebinding) {
    uint16_t length;
    uint8_t sent = 0;
    int result;

    result = dhcp_build_request(0U, rebinding, &length);
    if (result != OK) return result;
    if (rebinding) {
        result = udp_send_limited_broadcast(
            dhcp_endpoint, dhcp_status.interface_id,
            dhcp_status.lease.address,
            DHCP_SERVER_PORT, dhcp_tx_buffer, length, &sent);
    } else {
        result = udp_send(
            dhcp_endpoint, dhcp_status.lease.server_identifier,
            DHCP_SERVER_PORT, dhcp_tx_buffer, length, &sent);
    }
    if (result != OK) return result;
    if (!sent) return OK;
    dhcp_status.requests_tx++;
    dhcp_status.attempts++;
    dhcp_last_attempt_tick = timer_get_ticks();
    dhcp_retry_ticks = dhcp_adaptive_retry(
        rebinding ? dhcp_status.lease.lease_seconds :
                    dhcp_status.lease.t2_seconds);
    return OK;
}

static int dhcp_parse_address_list(uint32_t* address,
                                   uint8_t* has_address,
                                   const uint8_t* value,
                                   uint8_t length) {
    if (!address || !has_address || !value) {
        LOG_ERROR("NET", "Lista de enderecos DHCP nula");
        return ERR_NULL;
    }
    if (!length || (length % 4U)) {
        LOG_WARN("NET", "Lista de enderecos DHCP malformada");
        return ERR_INVALID;
    }
    if (*has_address) return OK;
    for (uint32_t offset = 0; offset < length; offset += 4U) {
        uint32_t candidate = dhcp_read_u32(value + offset);

        if (!ipv4_address_is_unicast(candidate)) continue;
        *address = candidate;
        *has_address = 1;
        return OK;
    }
    return OK;
}

static int dhcp_invalid_option_length(void) {
    LOG_WARN("NET", "Tamanho de opcao DHCP invalido");
    return ERR_INVALID;
}

static int dhcp_parse_option_value(dhcp_parsed_t* parsed,
                                   uint8_t code,
                                   const uint8_t* value,
                                   uint8_t length) {
    if (code == DHCP_OPTION_MESSAGE_TYPE) {
        if (length != 1U) return dhcp_invalid_option_length();
        parsed->message_type = value[0];
        parsed->has_message_type = 1;
    } else if (code == DHCP_OPTION_SUBNET_MASK) {
        if (length != 4U) return dhcp_invalid_option_length();
        parsed->subnet_mask = dhcp_read_u32(value);
        parsed->has_mask = 1;
    } else if (code == DHCP_OPTION_ROUTER) {
        return dhcp_parse_address_list(
            &parsed->gateway, &parsed->has_gateway, value, length);
    } else if (code == DHCP_OPTION_DNS) {
        return dhcp_parse_address_list(
            &parsed->dns_server, &parsed->has_dns, value, length);
    } else if (code == DHCP_OPTION_LEASE_TIME) {
        if (length != 4U) return dhcp_invalid_option_length();
        parsed->lease_seconds = dhcp_read_u32(value);
        parsed->has_lease = 1;
    } else if (code == DHCP_OPTION_SERVER_ID) {
        if (length != 4U) return dhcp_invalid_option_length();
        parsed->server_identifier = dhcp_read_u32(value);
        parsed->has_server = 1;
    } else if (code == DHCP_OPTION_T1) {
        if (length != 4U) return dhcp_invalid_option_length();
        parsed->t1_seconds = dhcp_read_u32(value);
        parsed->has_t1 = 1;
    } else if (code == DHCP_OPTION_T2) {
        if (length != 4U) return dhcp_invalid_option_length();
        parsed->t2_seconds = dhcp_read_u32(value);
        parsed->has_t2 = 1;
    }
    return OK;
}

static int dhcp_parse_options_internal(const uint8_t* data,
                                       uint16_t length,
                                       dhcp_parsed_t* parsed,
                                       uint8_t report_errors) {
    uint16_t offset = DHCP_OFFSET_OPTIONS;

    if (!data || !parsed || length < DHCP_FIXED_SIZE) {
        if (report_errors) {
            LOG_WARN("NET", "Bloco de opcoes DHCP invalido");
        }
        return ERR_INVALID;
    }
    while (offset < length) {
        uint8_t code = data[offset++];
        uint8_t option_length;

        if (code == DHCP_OPTION_END) return OK;
        if (code == DHCP_OPTION_PAD) continue;
        if (offset >= length) {
            if (report_errors) {
                LOG_WARN("NET", "Opcao DHCP sem tamanho");
            }
            return ERR_INVALID;
        }
        option_length = data[offset++];
        if ((uint32_t)offset + option_length > length) {
            if (report_errors) {
                LOG_WARN("NET", "Opcao DHCP truncada");
            }
            return ERR_INVALID;
        }
        if (dhcp_parse_option_value(
                parsed, code, data + offset, option_length) != OK) {
            if (report_errors) {
                LOG_WARN("NET", "Valor de opcao DHCP invalido");
            }
            return ERR_INVALID;
        }
        offset = (uint16_t)(offset + option_length);
    }
    if (report_errors) {
        LOG_WARN("NET", "Mensagem DHCP sem marcador final");
    }
    return ERR_INVALID;
}

static int dhcp_parse_options(const uint8_t* data, uint16_t length,
                              dhcp_parsed_t* parsed) {
    return dhcp_parse_options_internal(data, length, parsed, 1U);
}

static int dhcp_parse_reply(const udp_datagram_view_t* datagram,
                            dhcp_parsed_t* parsed) {
    const uint8_t* data;

    if (!datagram || !parsed ||
        datagram->source_port != DHCP_SERVER_PORT ||
        datagram->destination_port != DHCP_CLIENT_PORT ||
        datagram->payload_length < DHCP_FIXED_SIZE ||
        datagram->payload_length > DHCP_MAX_MESSAGE_SIZE) {
        LOG_WARN("NET", "Datagrama DHCP com envelope invalido");
        return ERR_INVALID;
    }
    data = datagram->payload;
    if (data[DHCP_OFFSET_OP] != DHCP_BOOTREPLY ||
        data[DHCP_OFFSET_HTYPE] != DHCP_HTYPE_ETHERNET ||
        data[DHCP_OFFSET_HLEN] != DHCP_MAC_ADDRESS_SIZE ||
        dhcp_read_u32(data + DHCP_OFFSET_XID) !=
            dhcp_status.transaction_id ||
        !dhcp_mac_equal(data + DHCP_OFFSET_CHADDR,
                        dhcp_status.local_mac) ||
        dhcp_read_u32(data + DHCP_OFFSET_COOKIE) !=
            DHCP_MAGIC_COOKIE) {
        LOG_WARN("NET", "Cabecalho BOOTP/DHCP invalido");
        return ERR_INVALID;
    }
    kmemset(parsed, 0, sizeof(*parsed));
    parsed->yiaddr = dhcp_read_u32(data + DHCP_OFFSET_YIADDR);
    if (dhcp_parse_options(data, datagram->payload_length,
                           parsed) != OK ||
        !parsed->has_message_type || !parsed->has_server ||
        parsed->server_identifier != datagram->source_ip) {
        LOG_WARN("NET", "Opcoes ou servidor DHCP invalidos");
        return ERR_INVALID;
    }
    return OK;
}

static int dhcp_offer_from_reply(const dhcp_parsed_t* parsed) {
    if (!parsed || parsed->message_type != DHCP_MESSAGE_OFFER ||
        !ipv4_address_is_unicast(parsed->yiaddr) ||
        !ipv4_address_is_unicast(parsed->server_identifier)) {
        LOG_WARN("NET", "DHCPOFFER invalido");
        return ERR_INVALID;
    }
    kmemset(&dhcp_offer, 0, sizeof(dhcp_offer));
    dhcp_offer.address = parsed->yiaddr;
    dhcp_offer.server_identifier = parsed->server_identifier;
    dhcp_status.offers_rx++;
    dhcp_status.state = DHCP_STATE_REQUESTING;
    dhcp_status.attempts = 0;
    return dhcp_send_selecting_request();
}

static int dhcp_lease_from_reply(const dhcp_parsed_t* parsed,
                                 dhcp_lease_t* lease) {
    if (!parsed || !lease || parsed->message_type != DHCP_MESSAGE_ACK ||
        !parsed->has_mask || !parsed->has_lease) {
        LOG_WARN("NET", "DHCPACK sem configuracao obrigatoria");
        return ERR_INVALID;
    }
    kmemset(lease, 0, sizeof(*lease));
    lease->address = parsed->yiaddr;
    if (!lease->address &&
        (dhcp_status.state == DHCP_STATE_RENEWING ||
         dhcp_status.state == DHCP_STATE_REBINDING)) {
        lease->address = dhcp_status.lease.address;
    }
    lease->subnet_mask = parsed->subnet_mask;
    lease->gateway = parsed->has_gateway ? parsed->gateway : 0U;
    lease->dns_server = parsed->has_dns ? parsed->dns_server : 0U;
    lease->server_identifier = parsed->server_identifier;
    lease->lease_seconds = parsed->lease_seconds;
    lease->t1_seconds = parsed->has_t1 ? parsed->t1_seconds : 0U;
    lease->t2_seconds = parsed->has_t2 ? parsed->t2_seconds : 0U;
    dhcp_set_default_timers(lease);
    return dhcp_lease_valid(lease) ? OK : ERR_INVALID;
}

static int dhcp_queue_apply(const dhcp_parsed_t* parsed) {
    dhcp_lease_t lease;
    int result = dhcp_lease_from_reply(parsed, &lease);

    if (result != OK) return result;
    dhcp_apply_was_bound = dhcp_status.lease.address ? 1U : 0U;
    dhcp_pending_lease = lease;
    dhcp_pending_event = DHCP_EVENT_APPLY_LEASE;
    dhcp_status.pending_event = dhcp_pending_event;
    dhcp_status.state = DHCP_STATE_APPLYING;
    dhcp_status.acks_rx++;
    return OK;
}

static void dhcp_queue_drop(dhcp_state_t target) {
    dhcp_pending_lease = dhcp_status.lease;
    dhcp_pending_event = DHCP_EVENT_DROP_LEASE;
    dhcp_status.pending_event = dhcp_pending_event;
    dhcp_drop_target = target;
    dhcp_status.state = DHCP_STATE_APPLYING;
}

static int dhcp_handle_datagram(const udp_datagram_view_t* datagram) {
    dhcp_parsed_t parsed;
    int result;

    if (!datagram || !datagram->interface_id) {
        dhcp_status.invalid_packets++;
        LOG_DEBUG("NET", "Datagrama DHCP sem interface de origem");
        return OK;
    }
    if (!dhcp_text_is_equal(datagram->interface_id,
                            dhcp_status.interface_id)) {
        dhcp_status.ignored_packets++;
        return OK;
    }
    if (dhcp_status.state != DHCP_STATE_SELECTING &&
        dhcp_status.state != DHCP_STATE_REQUESTING &&
        dhcp_status.state != DHCP_STATE_RENEWING &&
        dhcp_status.state != DHCP_STATE_REBINDING) {
        dhcp_status.ignored_packets++;
        return OK;
    }
    result = dhcp_parse_reply(datagram, &parsed);
    if (result != OK) {
        dhcp_status.invalid_packets++;
        return OK;
    }
    if (dhcp_status.state == DHCP_STATE_REQUESTING &&
        (parsed.server_identifier != dhcp_offer.server_identifier ||
         (parsed.message_type == DHCP_MESSAGE_ACK &&
          parsed.yiaddr != dhcp_offer.address))) {
        dhcp_status.ignored_packets++;
        return OK;
    }
    if (dhcp_status.state == DHCP_STATE_RENEWING &&
        parsed.server_identifier !=
            dhcp_status.lease.server_identifier) {
        dhcp_status.ignored_packets++;
        return OK;
    }
    if (parsed.message_type == DHCP_MESSAGE_NAK) {
        dhcp_status.naks_rx++;
        if (dhcp_status.lease.address) {
            dhcp_queue_drop(DHCP_STATE_FAILED);
        } else {
            dhcp_status.state = DHCP_STATE_FAILED;
            dhcp_status.last_error = ERR_STATE;
        }
        return OK;
    }
    if (dhcp_status.state == DHCP_STATE_SELECTING) {
        result = dhcp_offer_from_reply(&parsed);
    } else if (parsed.message_type == DHCP_MESSAGE_ACK) {
        result = dhcp_queue_apply(&parsed);
    } else {
        dhcp_status.ignored_packets++;
        return OK;
    }
    if (result != OK) {
        dhcp_status.invalid_packets++;
        dhcp_status.last_error = result;
    }
    return OK;
}

static void dhcp_clear_session(dhcp_state_t state) {
    dhcp_status.state = state;
    kmemset(dhcp_status.interface_id, 0,
            sizeof(dhcp_status.interface_id));
    kmemset(dhcp_status.local_mac, 0, sizeof(dhcp_status.local_mac));
    dhcp_status.transaction_id = 0;
    kmemset(&dhcp_status.lease, 0, sizeof(dhcp_status.lease));
    dhcp_status.lease_remaining_seconds = 0;
    dhcp_status.t1_remaining_seconds = 0;
    dhcp_status.t2_remaining_seconds = 0;
    dhcp_status.attempts = 0;
    dhcp_status.pending_event = DHCP_EVENT_NONE;
    dhcp_pending_event = DHCP_EVENT_NONE;
    kmemset(&dhcp_offer, 0, sizeof(dhcp_offer));
    kmemset(&dhcp_pending_lease, 0, sizeof(dhcp_pending_lease));
    dhcp_last_attempt_tick = 0;
    dhcp_retry_ticks = 0;
    dhcp_bound_tick = 0;
    dhcp_apply_was_bound = 0;
}

int dhcp_init(void) {
    int result;

    LOG_INFO("NET", "Inicializando cliente DHCP");
    if (dhcp_status.initialized) {
        LOG_WARN("NET", "Cliente DHCP ja estava inicializado");
        LOG_INFO("NET", "Cliente DHCP inicializado com sucesso");
        return OK;
    }
    kmemset(&dhcp_status, 0, sizeof(dhcp_status));
    result = udp_bind(DHCP_CLIENT_PORT, UDP_BIND_ALLOW_BROADCAST,
                      dhcp_handle_datagram, &dhcp_endpoint);
    if (result != OK) {
        dhcp_status.last_error = result;
        LOG_ERROR("NET", "Falha ao vincular porta do cliente DHCP");
        return result;
    }
    dhcp_status.initialized = 1;
    dhcp_status.state = DHCP_STATE_IDLE;
    dhcp_status.last_error = OK;
    LOG_INFO("NET", "Cliente DHCP inicializado com sucesso");
    return OK;
}

int dhcp_acquire(const char* interface_id, const uint8_t* local_mac) {
    char validated_id[DHCP_INTERFACE_ID_SIZE];
    uint32_t transaction_id;
    int result;

    if (!dhcp_status.initialized) {
        LOG_ERROR("NET", "Aquisicao DHCP antes da inicializacao");
        return ERR_STATE;
    }
    if (!interface_id || !local_mac) {
        LOG_ERROR("NET", "Argumento nulo na aquisicao DHCP");
        return ERR_NULL;
    }
    if (!dhcp_mac_valid(local_mac) ||
        (dhcp_status.state != DHCP_STATE_IDLE &&
         dhcp_status.state != DHCP_STATE_FAILED &&
         dhcp_status.state != DHCP_STATE_EXPIRED)) {
        LOG_ERROR("NET", "Estado ou MAC invalido na aquisicao DHCP");
        return ERR_STATE;
    }
    result = dhcp_copy_text(validated_id, sizeof(validated_id),
                            interface_id);
    if (result != OK) return result;
    dhcp_clear_session(DHCP_STATE_SELECTING);
    kmemcpy(dhcp_status.interface_id, validated_id,
            sizeof(validated_id));
    kmemcpy(dhcp_status.local_mac, local_mac,
            DHCP_MAC_ADDRESS_SIZE);
    transaction_id = timer_get_ticks() ^ 0x5A17D4C3U;
    transaction_id ^= (uint32_t)local_mac[2] << 24U;
    transaction_id ^= (uint32_t)local_mac[3] << 16U;
    transaction_id ^= (uint32_t)local_mac[4] << 8U;
    transaction_id ^= local_mac[5];
    if (!transaction_id) transaction_id = 1U;
    dhcp_status.transaction_id = transaction_id;
    dhcp_status.last_error = OK;
    result = dhcp_send_discover();
    if (result != OK) {
        dhcp_status.state = DHCP_STATE_FAILED;
        dhcp_status.last_error = result;
    }
    return result;
}

int dhcp_renew(void) {
    int result;

    if (!dhcp_status.initialized) {
        LOG_ERROR("NET", "Renovacao DHCP antes da inicializacao");
        return ERR_STATE;
    }
    if (dhcp_status.state != DHCP_STATE_BOUND ||
        !dhcp_status.lease.address) {
        LOG_ERROR("NET", "Renovacao DHCP sem lease ativo");
        return ERR_STATE;
    }
    dhcp_status.state = DHCP_STATE_RENEWING;
    dhcp_status.attempts = 0;
    dhcp_retry_ticks = 0;
    result = dhcp_send_bound_request(0U);
    if (result != OK) {
        dhcp_status.last_error = result;
        LOG_WARN("NET", "Primeira tentativa de renovacao DHCP falhou");
    }
    return OK;
}

int dhcp_release(uint8_t* out_sent) {
    uint16_t length;
    int send_result = OK;

    if (!out_sent) {
        LOG_ERROR("NET", "Destino nulo na liberacao DHCP");
        return ERR_NULL;
    }
    *out_sent = 0;
    if (!dhcp_status.initialized) {
        LOG_ERROR("NET", "Liberacao DHCP antes da inicializacao");
        return ERR_STATE;
    }
    if (dhcp_status.state == DHCP_STATE_SELECTING ||
        dhcp_status.state == DHCP_STATE_REQUESTING ||
        dhcp_status.state == DHCP_STATE_FAILED) {
        dhcp_clear_session(DHCP_STATE_IDLE);
        return OK;
    }
    if (dhcp_status.state != DHCP_STATE_BOUND &&
        dhcp_status.state != DHCP_STATE_RENEWING &&
        dhcp_status.state != DHCP_STATE_REBINDING) {
        LOG_ERROR("NET", "Liberacao DHCP sem lease ativo");
        return ERR_STATE;
    }
    if (dhcp_build_release(&length) == OK) {
        send_result = udp_send(
            dhcp_endpoint, dhcp_status.lease.server_identifier,
            DHCP_SERVER_PORT, dhcp_tx_buffer, length, out_sent);
    }
    if (send_result == OK && *out_sent) {
        dhcp_status.releases_tx++;
    } else {
        dhcp_status.last_error =
            send_result == OK ? ERR_UNAVAILABLE : send_result;
        LOG_WARN("NET", "DHCPRELEASE nao foi transmitido");
    }
    dhcp_queue_drop(DHCP_STATE_IDLE);
    return OK;
}

int dhcp_reset(void) {
    if (!dhcp_status.initialized) {
        LOG_ERROR("NET", "Reset DHCP antes da inicializacao");
        return ERR_STATE;
    }
    dhcp_clear_session(DHCP_STATE_IDLE);
    dhcp_status.last_error = OK;
    LOG_INFO("NET", "Sessao DHCP reiniciada");
    return OK;
}

static int dhcp_maintain_acquisition(uint32_t now) {
    int result;

    if (!dhcp_elapsed(now, dhcp_last_attempt_tick,
                      dhcp_retry_ticks)) return OK;
    if (dhcp_status.attempts >= DHCP_MAX_ATTEMPTS) {
        uint32_t final_ticks = dhcp_seconds_to_ticks(
            DHCP_FINAL_GRACE_SECONDS, timer_get_frequency());

        if (!dhcp_elapsed(now, dhcp_last_attempt_tick,
                          final_ticks)) return OK;
        dhcp_status.state = DHCP_STATE_FAILED;
        dhcp_status.timeouts++;
        dhcp_status.last_error = ERR_TIMEOUT;
        return OK;
    }
    if (dhcp_status.state == DHCP_STATE_SELECTING) {
        result = dhcp_send_discover();
    } else {
        result = dhcp_send_selecting_request();
    }
    if (result != OK) {
        dhcp_status.state = DHCP_STATE_FAILED;
        dhcp_status.last_error = result;
        LOG_ERROR("NET", "Retentativa DHCP falhou");
    }
    return result;
}

static int dhcp_maintain_bound(uint32_t now, uint32_t frequency) {
    uint32_t elapsed = (uint32_t)(now - dhcp_bound_tick);
    uint32_t lease_ticks = dhcp_seconds_to_ticks(
        dhcp_status.lease.lease_seconds, frequency);
    uint32_t t1_ticks = dhcp_seconds_to_ticks(
        dhcp_status.lease.t1_seconds, frequency);

    if (dhcp_elapsed(now, dhcp_bound_tick, lease_ticks)) {
        dhcp_status.timeouts++;
        dhcp_status.last_error = ERR_TIMEOUT;
        dhcp_queue_drop(DHCP_STATE_EXPIRED);
        return OK;
    }
    if (dhcp_status.state == DHCP_STATE_BOUND &&
        elapsed >= t1_ticks) {
        dhcp_status.state = DHCP_STATE_RENEWING;
        dhcp_status.attempts = 0;
        dhcp_retry_ticks = 0;
    }
    return OK;
}

static int dhcp_maintain_renewal(uint32_t now, uint32_t frequency) {
    uint32_t t2_ticks = dhcp_seconds_to_ticks(
        dhcp_status.lease.t2_seconds, frequency);

    if (dhcp_status.state == DHCP_STATE_RENEWING &&
        dhcp_elapsed(now, dhcp_bound_tick, t2_ticks)) {
        dhcp_status.state = DHCP_STATE_REBINDING;
        dhcp_status.attempts = 0;
        dhcp_retry_ticks = 0;
    }
    if (!dhcp_retry_ticks ||
        dhcp_elapsed(now, dhcp_last_attempt_tick,
                     dhcp_retry_ticks)) {
        int result = dhcp_send_bound_request(
            dhcp_status.state == DHCP_STATE_REBINDING);

        if (result != OK && result != ERR_TIMEOUT) {
            dhcp_status.last_error = result;
            LOG_WARN("NET", "Retentativa de lease DHCP falhou");
        }
    }
    return OK;
}

int dhcp_maintain(void) {
    uint32_t frequency;
    uint32_t now;

    if (!dhcp_status.initialized) {
        LOG_ERROR("NET", "Manutencao DHCP antes da inicializacao");
        return ERR_STATE;
    }
    frequency = timer_get_frequency();
    if (!frequency) {
        LOG_ERROR("NET", "Timer indisponivel na manutencao DHCP");
        return ERR_STATE;
    }
    dhcp_status.maintenance_cycles++;
    now = timer_get_ticks();
    if (dhcp_status.state == DHCP_STATE_SELECTING ||
        dhcp_status.state == DHCP_STATE_REQUESTING) {
        return dhcp_maintain_acquisition(now);
    }
    if (dhcp_status.state == DHCP_STATE_BOUND ||
        dhcp_status.state == DHCP_STATE_RENEWING ||
        dhcp_status.state == DHCP_STATE_REBINDING) {
        dhcp_maintain_bound(now, frequency);
    }
    if (dhcp_status.state == DHCP_STATE_RENEWING ||
        dhcp_status.state == DHCP_STATE_REBINDING) {
        return dhcp_maintain_renewal(now, frequency);
    }
    return OK;
}

int dhcp_take_event(dhcp_event_t* out_event,
                    dhcp_lease_t* out_lease) {
    if (!out_event || !out_lease) {
        LOG_ERROR("NET", "Destino nulo ao consultar evento DHCP");
        return ERR_NULL;
    }
    if (!dhcp_status.initialized) {
        LOG_ERROR("NET", "Consulta de evento DHCP antes da inicializacao");
        return ERR_STATE;
    }
    *out_event = dhcp_pending_event;
    *out_lease = dhcp_pending_lease;
    return OK;
}

int dhcp_complete_event(dhcp_event_t event, int result) {
    if (!dhcp_status.initialized) {
        LOG_ERROR("NET", "Conclusao DHCP antes da inicializacao");
        return ERR_STATE;
    }
    if (event == DHCP_EVENT_NONE || event != dhcp_pending_event) {
        LOG_ERROR("NET", "Evento DHCP invalido para conclusao");
        return ERR_INVALID;
    }
    if (event == DHCP_EVENT_APPLY_LEASE && result == OK) {
        dhcp_status.lease = dhcp_pending_lease;
        dhcp_status.state = DHCP_STATE_BOUND;
        dhcp_bound_tick = timer_get_ticks();
        dhcp_status.attempts = 0;
        dhcp_status.last_error = OK;
    } else if (event == DHCP_EVENT_APPLY_LEASE) {
        dhcp_status.state = dhcp_apply_was_bound ?
                            DHCP_STATE_BOUND : DHCP_STATE_FAILED;
        dhcp_status.last_error = result;
    } else {
        dhcp_state_t target = dhcp_drop_target;
        int final_error = target == DHCP_STATE_EXPIRED ?
                          ERR_TIMEOUT : result;

        dhcp_clear_session(target);
        dhcp_status.last_error = final_error;
    }
    dhcp_pending_event = DHCP_EVENT_NONE;
    dhcp_status.pending_event = DHCP_EVENT_NONE;
    kmemset(&dhcp_pending_lease, 0, sizeof(dhcp_pending_lease));
    return OK;
}

static uint32_t dhcp_remaining(uint32_t total_seconds,
                               uint32_t elapsed_seconds) {
    return total_seconds > elapsed_seconds ?
           total_seconds - elapsed_seconds : 0U;
}

int dhcp_get_status(dhcp_status_t* out_status) {
    uint32_t frequency;
    uint32_t elapsed_seconds = 0;

    if (!out_status) {
        LOG_ERROR("NET", "Destino nulo ao consultar DHCP");
        return ERR_NULL;
    }
    *out_status = dhcp_status;
    frequency = timer_get_frequency();
    if (frequency && dhcp_bound_tick && dhcp_status.lease.address) {
        elapsed_seconds =
            (uint32_t)(timer_get_ticks() - dhcp_bound_tick) / frequency;
        out_status->lease_remaining_seconds = dhcp_remaining(
            dhcp_status.lease.lease_seconds, elapsed_seconds);
        out_status->t1_remaining_seconds = dhcp_remaining(
            dhcp_status.lease.t1_seconds, elapsed_seconds);
        out_status->t2_remaining_seconds = dhcp_remaining(
            dhcp_status.lease.t2_seconds, elapsed_seconds);
    }
    return OK;
}

static int dhcp_validate_option_vectors(void) {
    uint8_t valid[DHCP_FIXED_SIZE + 4U];
    dhcp_parsed_t parsed;

    kmemset(valid, 0, sizeof(valid));
    valid[DHCP_OFFSET_OPTIONS] = DHCP_OPTION_MESSAGE_TYPE;
    valid[DHCP_OFFSET_OPTIONS + 1U] = 1U;
    valid[DHCP_OFFSET_OPTIONS + 2U] = DHCP_MESSAGE_ACK;
    valid[DHCP_OFFSET_OPTIONS + 3U] = DHCP_OPTION_END;
    kmemset(&parsed, 0, sizeof(parsed));
    if (dhcp_parse_options_internal(
            valid, sizeof(valid), &parsed, 0U) != OK ||
        !parsed.has_message_type ||
        parsed.message_type != DHCP_MESSAGE_ACK) {
        LOG_ERROR("NET", "Vetor de opcoes DHCP valido falhou");
        return ERR_STATE;
    }
    valid[DHCP_OFFSET_OPTIONS + 1U] = 8U;
    if (dhcp_parse_options_internal(
            valid, sizeof(valid), &parsed, 0U) != ERR_INVALID) {
        LOG_ERROR("NET", "Vetor DHCP truncado foi aceito");
        return ERR_STATE;
    }
    return OK;
}

int dhcp_validate_state(void) {
    udp_status_t udp;

    if (dhcp_validate_option_vectors() != OK) return ERR_STATE;
    if (udp_get_status(&udp) != OK) return ERR_STATE;
    if (!dhcp_status.initialized) {
        if (dhcp_endpoint || dhcp_status.state != DHCP_STATE_IDLE) {
            LOG_ERROR("NET", "DHCP ativo sem inicializacao");
            return ERR_STATE;
        }
        return OK;
    }
    if (!dhcp_endpoint || !udp.initialized ||
        dhcp_status.state > DHCP_STATE_EXPIRED ||
        dhcp_status.pending_event != dhcp_pending_event) {
        LOG_ERROR("NET", "Estado basico DHCP inconsistente");
        return ERR_STATE;
    }
    if ((dhcp_status.state == DHCP_STATE_BOUND ||
         dhcp_status.state == DHCP_STATE_RENEWING ||
         dhcp_status.state == DHCP_STATE_REBINDING) &&
        (!dhcp_lease_valid(&dhcp_status.lease) ||
         !dhcp_status.interface_id[0] ||
         !dhcp_mac_valid(dhcp_status.local_mac))) {
        LOG_ERROR("NET", "Lease DHCP inconsistente");
        return ERR_STATE;
    }
    if (dhcp_pending_event != DHCP_EVENT_NONE &&
        dhcp_status.state != DHCP_STATE_APPLYING) {
        LOG_ERROR("NET", "Evento DHCP fora do estado APPLYING");
        return ERR_STATE;
    }
    return OK;
}

const char* dhcp_state_name(dhcp_state_t state) {
    if (state == DHCP_STATE_IDLE) return "IDLE";
    if (state == DHCP_STATE_SELECTING) return "SELECTING";
    if (state == DHCP_STATE_REQUESTING) return "REQUESTING";
    if (state == DHCP_STATE_APPLYING) return "APPLYING";
    if (state == DHCP_STATE_BOUND) return "BOUND";
    if (state == DHCP_STATE_RENEWING) return "RENEWING";
    if (state == DHCP_STATE_REBINDING) return "REBINDING";
    if (state == DHCP_STATE_FAILED) return "FAILED";
    if (state == DHCP_STATE_EXPIRED) return "EXPIRED";
    return "DESCONHECIDO";
}
