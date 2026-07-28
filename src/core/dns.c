#include "core/dns.h"
#include "core/errors.h"
#include "core/ipv4.h"
#include "core/log.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/udp.h"

#define DNS_HEADER_SIZE 12U
#define DNS_TYPE_A 1U
#define DNS_TYPE_CNAME 5U
#define DNS_CLASS_IN 1U
#define DNS_FLAG_QUERY_RECURSION 0x0100U
#define DNS_FLAG_RESPONSE 0x8000U
#define DNS_FLAG_TRUNCATED 0x0200U
#define DNS_FLAG_OPCODE_MASK 0x7800U
#define DNS_FLAG_RCODE_MASK 0x000FU
#define DNS_MAX_POINTER_DEPTH 16U
#define DNS_MAX_CNAME_DEPTH 4U
#define DNS_MAX_RECORDS 32U
#define DNS_MAX_ATTEMPTS 3U
#define DNS_RETRY_SECONDS 1U
#define DNS_EPHEMERAL_PORT_MIN 49152U
#define DNS_EPHEMERAL_PORT_RANGE 16384U
#define DNS_MAX_SAFE_TICKS 0x7FFFFFFFU

#define DNS_OFFSET_ID 0U
#define DNS_OFFSET_FLAGS 2U
#define DNS_OFFSET_QDCOUNT 4U
#define DNS_OFFSET_ANCOUNT 6U
#define DNS_OFFSET_NSCOUNT 8U
#define DNS_OFFSET_ARCOUNT 10U

typedef struct {
    uint8_t used;
    char name[DNS_NAME_BUFFER_SIZE];
    uint32_t address;
    uint32_t ttl_seconds;
    uint32_t stored_tick;
} dns_cache_entry_t;

typedef struct {
    uint8_t has_a;
    uint8_t has_cname;
    uint32_t address;
    uint32_t ttl_seconds;
    char cname[DNS_NAME_BUFFER_SIZE];
} dns_record_match_t;

static dns_status_t dns_status;
static dns_cache_entry_t dns_cache[DNS_CACHE_CAPACITY];
static udp_endpoint_handle_t dns_endpoint;
static uint8_t dns_tx_buffer[DNS_MAX_PACKET_SIZE];
static char dns_active_name[DNS_NAME_BUFFER_SIZE];
static uint32_t dns_last_tx_tick;
static uint32_t dns_chain_ttl;
static uint8_t dns_chain_has_ttl;
static uint16_t dns_id_sequence;

static uint16_t dns_read_u16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

static uint32_t dns_read_u32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) | data[3];
}

static void dns_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

static char dns_ascii_lower(char value) {
    if (value >= 'A' && value <= 'Z') {
        return (char)(value + ('a' - 'A'));
    }
    return value;
}

static uint8_t dns_name_equal(const char* first, const char* second) {
    if (!first || !second) return 0;
    while (*first && *second) {
        if (dns_ascii_lower(*first) != dns_ascii_lower(*second)) return 0;
        first++;
        second++;
    }
    return *first == '\0' && *second == '\0';
}

static int dns_copy_name(char* destination, const char* source) {
    uint32_t length = 0;

    if (!destination || !source) {
        LOG_ERROR("NET", "Nome DNS nulo");
        return ERR_NULL;
    }
    while (source[length]) {
        if (length >= DNS_NAME_MAX_LENGTH) {
            LOG_ERROR("NET", "Nome DNS excede limite");
            return ERR_OVERFLOW;
        }
        destination[length] = dns_ascii_lower(source[length]);
        length++;
    }
    destination[length] = '\0';
    return OK;
}

static uint8_t dns_label_character(char value) {
    value = dns_ascii_lower(value);
    return (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9') || value == '-';
}

static int dns_normalize_name(const char* source, char* destination) {
    uint32_t source_length;
    uint32_t output = 0;
    uint32_t label_length = 0;

    if (!source || !destination) {
        LOG_ERROR("NET", "Nome DNS nulo na normalizacao");
        return ERR_NULL;
    }
    source_length = kstrlen(source);
    if (source_length && source[source_length - 1U] == '.') source_length--;
    if (!source_length || source_length > DNS_NAME_MAX_LENGTH) {
        LOG_ERROR("NET", "Tamanho de nome DNS invalido");
        return ERR_INVALID;
    }
    for (uint32_t index = 0; index < source_length; index++) {
        char value = dns_ascii_lower(source[index]);

        if (value == '.') {
            if (!label_length || label_length > 63U ||
                destination[output - 1U] == '-') {
                LOG_ERROR("NET", "Label DNS invalido");
                return ERR_INVALID;
            }
            label_length = 0;
        } else {
            if (!dns_label_character(value) ||
                (!label_length && value == '-')) {
                LOG_ERROR("NET", "Caractere invalido no nome DNS");
                return ERR_INVALID;
            }
            label_length++;
            if (label_length > 63U) {
                LOG_ERROR("NET", "Label DNS excede limite");
                return ERR_INVALID;
            }
        }
        destination[output++] = value;
    }
    if (!label_length || destination[output - 1U] == '-') {
        LOG_ERROR("NET", "Label DNS invalido");
        return ERR_INVALID;
    }
    destination[output] = '\0';
    return OK;
}

static uint32_t dns_seconds_to_ticks(uint32_t seconds,
                                     uint32_t frequency) {
    if (!seconds || !frequency) return 0;
    if (seconds > DNS_MAX_SAFE_TICKS / frequency) {
        return DNS_MAX_SAFE_TICKS;
    }
    return seconds * frequency;
}

static uint8_t dns_cache_expired(const dns_cache_entry_t* entry,
                                 uint32_t now,
                                 uint32_t frequency) {
    uint32_t ttl_ticks;

    if (!entry->used || !entry->ttl_seconds) return 1;
    ttl_ticks = dns_seconds_to_ticks(entry->ttl_seconds, frequency);
    return ttl_ticks &&
           (uint32_t)(now - entry->stored_tick) >= ttl_ticks;
}

static int32_t dns_find_cache(const char* name, uint32_t now,
                              uint32_t frequency) {
    for (uint32_t index = 0; index < DNS_CACHE_CAPACITY; index++) {
        if (!dns_cache[index].used ||
            !dns_name_equal(dns_cache[index].name, name)) continue;
        if (dns_cache_expired(&dns_cache[index], now, frequency)) {
            kmemset(&dns_cache[index], 0, sizeof(dns_cache[index]));
            return -1;
        }
        return (int32_t)index;
    }
    return -1;
}

static int32_t dns_select_cache(uint32_t now, uint32_t frequency) {
    int32_t oldest = -1;
    uint32_t oldest_age = 0;

    for (uint32_t index = 0; index < DNS_CACHE_CAPACITY; index++) {
        uint32_t age;

        if (!dns_cache[index].used ||
            dns_cache_expired(&dns_cache[index], now, frequency)) {
            return (int32_t)index;
        }
        age = (uint32_t)(now - dns_cache[index].stored_tick);
        if (oldest < 0 || age > oldest_age) {
            oldest = (int32_t)index;
            oldest_age = age;
        }
    }
    return oldest;
}

static void dns_cache_store(const char* name, uint32_t address,
                            uint32_t ttl_seconds) {
    uint32_t frequency = timer_get_frequency();
    uint32_t now = timer_get_ticks();
    int32_t index;

    if (!ttl_seconds || !frequency) return;
    index = dns_find_cache(name, now, frequency);
    if (index < 0) index = dns_select_cache(now, frequency);
    if (index < 0) return;
    kmemset(&dns_cache[index], 0, sizeof(dns_cache[index]));
    dns_copy_name(dns_cache[index].name, name);
    dns_cache[index].used = 1;
    dns_cache[index].address = address;
    dns_cache[index].ttl_seconds = ttl_seconds;
    dns_cache[index].stored_tick = now;
}

static int dns_read_name(const uint8_t* message, uint16_t length,
                         uint16_t* offset, char* out_name) {
    uint16_t cursor;
    uint16_t resume = 0;
    uint32_t output = 0;
    uint8_t jumped = 0;
    uint8_t depth = 0;

    if (!message || !offset || !out_name || *offset >= length) {
        LOG_WARN("NET", "Nome comprimido DNS invalido");
        return ERR_INVALID;
    }
    cursor = *offset;
    while (cursor < length) {
        uint8_t label = message[cursor++];

        if ((label & 0xC0U) == 0xC0U) {
            uint16_t pointer;

            if (cursor >= length || depth++ >= DNS_MAX_POINTER_DEPTH) {
                return ERR_INVALID;
            }
            pointer = (uint16_t)(((uint16_t)(label & 0x3FU) << 8U) |
                                 message[cursor++]);
            if (pointer >= length) return ERR_INVALID;
            if (!jumped) resume = cursor;
            jumped = 1;
            cursor = pointer;
            continue;
        }
        if (label & 0xC0U || label > 63U) return ERR_INVALID;
        if (!label) {
            if (!jumped) resume = cursor;
            if (!output) return ERR_INVALID;
            out_name[output] = '\0';
            *offset = resume;
            return OK;
        }
        if ((uint32_t)cursor + label > length ||
            output + label + (output ? 1U : 0U) >
                DNS_NAME_MAX_LENGTH) return ERR_INVALID;
        if (output) out_name[output++] = '.';
        for (uint32_t index = 0; index < label; index++) {
            char value = (char)message[cursor++];

            if (!dns_label_character(value)) return ERR_INVALID;
            out_name[output++] = dns_ascii_lower(value);
        }
    }
    return ERR_INVALID;
}

static int dns_encode_name(const char* name, uint16_t* offset) {
    const char* cursor = name;

    if (!name || !offset) {
        LOG_ERROR("NET", "Destino nulo ao codificar nome DNS");
        return ERR_NULL;
    }
    while (*cursor) {
        const char* label = cursor;
        uint8_t length = 0;

        while (*cursor && *cursor != '.') {
            length++;
            cursor++;
        }
        if (!length ||
            (uint32_t)*offset + 1U + length >= DNS_MAX_PACKET_SIZE) {
            LOG_ERROR("NET", "Nome DNS excede pacote de consulta");
            return ERR_OVERFLOW;
        }
        dns_tx_buffer[(*offset)++] = length;
        kmemcpy(dns_tx_buffer + *offset, label, length);
        *offset = (uint16_t)(*offset + length);
        if (*cursor == '.') cursor++;
    }
    if (*offset >= DNS_MAX_PACKET_SIZE) {
        LOG_ERROR("NET", "Pacote DNS sem espaco para terminador");
        return ERR_OVERFLOW;
    }
    dns_tx_buffer[(*offset)++] = 0U;
    return OK;
}

static int dns_build_query(uint16_t* out_length) {
    uint16_t offset = DNS_HEADER_SIZE;
    int result;

    if (!out_length) {
        LOG_ERROR("NET", "Destino nulo ao montar consulta DNS");
        return ERR_NULL;
    }
    kmemset(dns_tx_buffer, 0, sizeof(dns_tx_buffer));
    dns_write_u16(dns_tx_buffer + DNS_OFFSET_ID,
                  dns_status.transaction_id);
    dns_write_u16(dns_tx_buffer + DNS_OFFSET_FLAGS,
                  DNS_FLAG_QUERY_RECURSION);
    dns_write_u16(dns_tx_buffer + DNS_OFFSET_QDCOUNT, 1U);
    result = dns_encode_name(dns_active_name, &offset);
    if (result != OK || (uint32_t)offset + 4U > DNS_MAX_PACKET_SIZE) {
        return result != OK ? result : ERR_OVERFLOW;
    }
    dns_write_u16(dns_tx_buffer + offset, DNS_TYPE_A);
    dns_write_u16(dns_tx_buffer + offset + 2U, DNS_CLASS_IN);
    *out_length = offset + 4U;
    return OK;
}

static uint16_t dns_next_transaction_id(void) {
    dns_id_sequence++;
    if (!dns_id_sequence) dns_id_sequence = 1U;
    dns_id_sequence ^= (uint16_t)timer_get_ticks();
    dns_id_sequence ^= (uint16_t)dns_status.server_ip;
    if (!dns_id_sequence) dns_id_sequence = 1U;
    return dns_id_sequence;
}

static void dns_fail(int error) {
    dns_status.state = DNS_STATE_FAILED;
    dns_status.last_error = error;
    dns_status.event_generation++;
    if (error == ERR_TIMEOUT) dns_status.timeouts++;
}

static int dns_send_query(void) {
    uint16_t length;
    uint8_t sent = 0;
    int result = dns_build_query(&length);

    if (result == OK) {
        result = udp_send(dns_endpoint, dns_status.server_ip,
                          DNS_SERVER_PORT, dns_tx_buffer, length, &sent);
    }
    if (result != OK) {
        dns_fail(result);
        return result;
    }
    if (!sent) {
        dns_status.state = DNS_STATE_RESOLVING_ARP;
        return OK;
    }
    dns_status.state = DNS_STATE_WAITING_REPLY;
    dns_status.attempts++;
    dns_status.queries_tx++;
    dns_last_tx_tick = timer_get_ticks();
    return OK;
}

static int dns_parse_question(const uint8_t* message, uint16_t length,
                              uint16_t* offset) {
    char question[DNS_NAME_BUFFER_SIZE];

    if (dns_read_name(message, length, offset, question) != OK ||
        (uint32_t)*offset + 4U > length ||
        !dns_name_equal(question, dns_active_name) ||
        dns_read_u16(message + *offset) != DNS_TYPE_A ||
        dns_read_u16(message + *offset + 2U) != DNS_CLASS_IN) {
        LOG_WARN("NET", "Pergunta DNS recebida nao corresponde");
        return ERR_INVALID;
    }
    *offset = (uint16_t)(*offset + 4U);
    return OK;
}

static int dns_scan_records(const uint8_t* message, uint16_t length,
                            uint16_t records_offset,
                            uint16_t record_count,
                            const char* target,
                            dns_record_match_t* match) {
    uint16_t offset = records_offset;

    kmemset(match, 0, sizeof(*match));
    for (uint32_t index = 0; index < record_count; index++) {
        char owner[DNS_NAME_BUFFER_SIZE];
        uint16_t type;
        uint16_t class_code;
        uint32_t ttl;
        uint16_t data_length;
        uint16_t data_offset;

        if (dns_read_name(message, length, &offset, owner) != OK ||
            (uint32_t)offset + 10U > length) {
            LOG_WARN("NET", "Registro DNS truncado");
            return ERR_INVALID;
        }
        type = dns_read_u16(message + offset);
        class_code = dns_read_u16(message + offset + 2U);
        ttl = dns_read_u32(message + offset + 4U);
        data_length = dns_read_u16(message + offset + 8U);
        data_offset = offset + 10U;
        if ((uint32_t)data_offset + data_length > length) {
            LOG_WARN("NET", "RDATA DNS excede mensagem");
            return ERR_INVALID;
        }
        if (class_code == DNS_CLASS_IN &&
            dns_name_equal(owner, target) &&
            type == DNS_TYPE_A && data_length == 4U) {
            match->has_a = 1;
            match->address = dns_read_u32(message + data_offset);
            match->ttl_seconds = ttl;
        } else if (class_code == DNS_CLASS_IN &&
                   dns_name_equal(owner, target) &&
                   type == DNS_TYPE_CNAME) {
            uint16_t cname_offset = data_offset;

            if (dns_read_name(message, length, &cname_offset,
                              match->cname) != OK ||
                cname_offset != data_offset + data_length) {
                return ERR_INVALID;
            }
            match->has_cname = 1;
            match->ttl_seconds = ttl;
        }
        offset = (uint16_t)(data_offset + data_length);
    }
    return OK;
}

static uint32_t dns_ttl_min(uint32_t current, uint32_t candidate,
                            uint8_t has_current) {
    if (!has_current || candidate < current) return candidate;
    return current;
}

static int dns_find_answer(const uint8_t* message, uint16_t length,
                           uint16_t records_offset,
                           uint16_t record_count,
                           uint32_t* out_ip, uint32_t* out_ttl,
                           char* out_follow, uint8_t* out_depth) {
    char target[DNS_NAME_BUFFER_SIZE];
    uint32_t ttl = 0;
    uint8_t has_ttl = 0;

    dns_copy_name(target, dns_active_name);
    *out_depth = 0;
    out_follow[0] = '\0';
    for (uint32_t depth = 0; depth <= DNS_MAX_CNAME_DEPTH; depth++) {
        dns_record_match_t match;
        int result = dns_scan_records(
            message, length, records_offset, record_count,
            target, &match);

        if (result != OK) return result;
        if (match.has_a) {
            if (!ipv4_address_is_unicast(match.address)) {
                LOG_WARN("NET", "Registro A retornou IPv4 invalido");
                return ERR_INVALID;
            }
            *out_ip = match.address;
            *out_ttl = dns_ttl_min(
                ttl, match.ttl_seconds, has_ttl);
            if (*out_depth) dns_copy_name(out_follow, target);
            return OK;
        }
        if (!match.has_cname) break;
        ttl = dns_ttl_min(ttl, match.ttl_seconds, has_ttl);
        has_ttl = 1;
        dns_copy_name(target, match.cname);
        (*out_depth)++;
    }
    if (*out_depth) {
        dns_copy_name(out_follow, target);
        *out_ttl = ttl;
        LOG_DEBUG("NET", "CNAME DNS exige consulta complementar");
        return ERR_UNAVAILABLE;
    }
    LOG_DEBUG("NET", "Resposta DNS sem registro A");
    return ERR_NOT_FOUND;
}

static int dns_parse_response(const udp_datagram_view_t* datagram,
                              uint32_t* out_ip, uint32_t* out_ttl,
                              char* out_follow, uint8_t* out_depth) {
    const uint8_t* message = datagram->payload;
    uint16_t length = datagram->payload_length;
    uint16_t flags;
    uint32_t record_total;
    uint16_t offset = DNS_HEADER_SIZE;

    if (length < DNS_HEADER_SIZE || length > DNS_MAX_PACKET_SIZE ||
        dns_read_u16(message + DNS_OFFSET_ID) !=
            dns_status.transaction_id) {
        LOG_WARN("NET", "Cabecalho ou ID DNS invalido");
        return ERR_INVALID;
    }
    flags = dns_read_u16(message + DNS_OFFSET_FLAGS);
    if (!(flags & DNS_FLAG_RESPONSE) ||
        (flags & DNS_FLAG_OPCODE_MASK) ||
        (flags & DNS_FLAG_TRUNCATED) ||
        dns_read_u16(message + DNS_OFFSET_QDCOUNT) != 1U) {
        LOG_WARN("NET", "Flags de resposta DNS invalidas");
        return ERR_INVALID;
    }
    if (dns_parse_question(message, length, &offset) != OK) {
        return ERR_INVALID;
    }
    if (flags & DNS_FLAG_RCODE_MASK) return ERR_NOT_FOUND;
    record_total =
        (uint32_t)dns_read_u16(message + DNS_OFFSET_ANCOUNT) +
        dns_read_u16(message + DNS_OFFSET_NSCOUNT) +
        dns_read_u16(message + DNS_OFFSET_ARCOUNT);
    if (!record_total || record_total > DNS_MAX_RECORDS) {
        return ERR_INVALID;
    }
    return dns_find_answer(message, length, offset,
                           (uint16_t)record_total,
                           out_ip, out_ttl, out_follow, out_depth);
}

static int dns_follow_cname(const char* name, uint8_t depth) {
    if (!name || !depth ||
        (uint32_t)dns_status.cname_depth + depth >
            DNS_MAX_CNAME_DEPTH) {
        LOG_WARN("NET", "Cadeia CNAME excede limite");
        return ERR_NOT_FOUND;
    }
    dns_status.cname_depth =
        (uint8_t)(dns_status.cname_depth + depth);
    dns_copy_name(dns_active_name, name);
    dns_copy_name(dns_status.canonical_name, name);
    dns_status.transaction_id = dns_next_transaction_id();
    dns_status.attempts = 0;
    dns_status.state = DNS_STATE_RESOLVING_ARP;
    return dns_send_query();
}

static int dns_handle_datagram(const udp_datagram_view_t* datagram) {
    char follow[DNS_NAME_BUFFER_SIZE];
    uint32_t address = 0;
    uint32_t ttl = 0;
    uint8_t depth = 0;
    int result;

    if (!datagram || datagram->source_port != DNS_SERVER_PORT ||
        datagram->destination_port != dns_status.local_port ||
        datagram->source_ip != dns_status.server_ip) {
        dns_status.ignored_packets++;
        return OK;
    }
    if (dns_status.state != DNS_STATE_WAITING_REPLY) {
        dns_status.ignored_packets++;
        return OK;
    }
    if (datagram->payload_length < 2U ||
        dns_read_u16(datagram->payload) !=
            dns_status.transaction_id) {
        dns_status.ignored_packets++;
        return OK;
    }
    result = dns_parse_response(datagram, &address, &ttl,
                                follow, &depth);
    if (result == ERR_UNAVAILABLE) {
        dns_status.replies_rx++;
        dns_chain_ttl = dns_ttl_min(
            dns_chain_ttl, ttl, dns_chain_has_ttl);
        dns_chain_has_ttl = 1;
        result = dns_follow_cname(follow, depth);
        if (result != OK &&
            dns_status.state != DNS_STATE_FAILED) dns_fail(result);
        return OK;
    }
    if (result != OK) {
        if (result == ERR_INVALID) dns_status.invalid_packets++;
        else dns_status.replies_rx++;
        dns_fail(result);
        return OK;
    }
    dns_status.replies_rx++;
    dns_status.result_ip = address;
    dns_status.cname_depth =
        (uint8_t)(dns_status.cname_depth + depth);
    if (depth && follow[0]) {
        dns_copy_name(dns_status.canonical_name, follow);
    }
    ttl = dns_ttl_min(dns_chain_ttl, ttl, dns_chain_has_ttl);
    dns_cache_store(dns_status.query_name, address, ttl);
    dns_status.state = DNS_STATE_COMPLETE;
    dns_status.last_error = OK;
    dns_status.event_generation++;
    return OK;
}

static void dns_reset_query(void) {
    dns_status.state = DNS_STATE_IDLE;
    dns_status.query_name[0] = '\0';
    dns_status.canonical_name[0] = '\0';
    dns_status.result_ip = 0;
    dns_status.transaction_id = 0;
    dns_status.attempts = 0;
    dns_status.cname_depth = 0;
    dns_active_name[0] = '\0';
    dns_last_tx_tick = 0;
    dns_chain_ttl = 0;
    dns_chain_has_ttl = 0;
}

int dns_init(void) {
    uint16_t local_port;
    int result;

    LOG_INFO("NET", "Inicializando cliente DNS");
    if (dns_status.initialized) {
        LOG_WARN("NET", "Cliente DNS ja estava inicializado");
        LOG_INFO("NET", "Cliente DNS inicializado com sucesso");
        return OK;
    }
    kmemset(&dns_status, 0, sizeof(dns_status));
    kmemset(dns_cache, 0, sizeof(dns_cache));
    local_port = (uint16_t)(DNS_EPHEMERAL_PORT_MIN +
        (timer_get_ticks() % DNS_EPHEMERAL_PORT_RANGE));
    result = udp_bind(local_port, 0U, dns_handle_datagram,
                      &dns_endpoint);
    if (result != OK) {
        dns_status.last_error = result;
        LOG_ERROR("NET", "Falha ao vincular endpoint DNS");
        return result;
    }
    dns_status.initialized = 1;
    dns_status.local_port = local_port;
    dns_status.state = DNS_STATE_IDLE;
    dns_status.last_error = OK;
    LOG_INFO("NET", "Cliente DNS inicializado com sucesso");
    return OK;
}

int dns_configure(uint32_t server_ip) {
    uint16_t local_port;

    if (!dns_status.initialized) {
        LOG_ERROR("NET", "Configuracao DNS antes da inicializacao");
        return ERR_STATE;
    }
    if (!ipv4_address_is_unicast(server_ip)) {
        LOG_ERROR("NET", "Servidor DNS invalido");
        return ERR_INVALID;
    }
    if (dns_status.configured &&
        dns_status.server_ip == server_ip) return OK;
    local_port = dns_status.local_port;
    kmemset(&dns_status, 0, sizeof(dns_status));
    kmemset(dns_cache, 0, sizeof(dns_cache));
    dns_status.initialized = 1;
    dns_status.configured = 1;
    dns_status.local_port = local_port;
    dns_status.server_ip = server_ip;
    dns_status.state = DNS_STATE_IDLE;
    dns_status.last_error = OK;
    dns_active_name[0] = '\0';
    LOG_INFO("NET", "Servidor DNS configurado em memoria");
    return OK;
}

int dns_unconfigure(void) {
    uint16_t local_port;

    if (!dns_status.initialized) {
        LOG_ERROR("NET", "Remocao DNS antes da inicializacao");
        return ERR_STATE;
    }
    local_port = dns_status.local_port;
    kmemset(&dns_status, 0, sizeof(dns_status));
    kmemset(dns_cache, 0, sizeof(dns_cache));
    dns_status.initialized = 1;
    dns_status.local_port = local_port;
    dns_status.state = DNS_STATE_IDLE;
    dns_status.last_error = OK;
    dns_active_name[0] = '\0';
    LOG_INFO("NET", "Configuracao DNS removida da memoria");
    return OK;
}

int dns_resolve(const char* name, uint32_t* out_ip,
                uint8_t* out_resolved) {
    char normalized[DNS_NAME_BUFFER_SIZE];
    uint32_t frequency;
    int32_t cache_index;
    int result;

    if (!name || !out_ip || !out_resolved) {
        LOG_ERROR("NET", "Argumento nulo na resolucao DNS");
        return ERR_NULL;
    }
    *out_ip = 0;
    *out_resolved = 0;
    if (!dns_status.initialized || !dns_status.configured) {
        LOG_ERROR("NET", "Resolucao DNS antes da configuracao");
        return ERR_STATE;
    }
    result = dns_normalize_name(name, normalized);
    if (result != OK) return result;
    frequency = timer_get_frequency();
    if (!frequency) {
        LOG_ERROR("NET", "Timer indisponivel na resolucao DNS");
        return ERR_STATE;
    }
    cache_index = dns_find_cache(normalized, timer_get_ticks(), frequency);
    if (cache_index >= 0) {
        *out_ip = dns_cache[cache_index].address;
        *out_resolved = 1;
        dns_status.cache_hits++;
        dns_reset_query();
        dns_copy_name(dns_status.query_name, normalized);
        dns_copy_name(dns_status.canonical_name, normalized);
        dns_status.result_ip = *out_ip;
        dns_status.state = DNS_STATE_COMPLETE;
        dns_status.last_error = OK;
        return OK;
    }
    if ((dns_status.state == DNS_STATE_RESOLVING_ARP ||
         dns_status.state == DNS_STATE_WAITING_REPLY) &&
        dns_name_equal(normalized, dns_status.query_name)) return OK;
    if (dns_status.state == DNS_STATE_RESOLVING_ARP ||
        dns_status.state == DNS_STATE_WAITING_REPLY) {
        LOG_ERROR("NET", "Outra consulta DNS esta ativa");
        return ERR_STATE;
    }
    dns_reset_query();
    dns_status.cache_misses++;
    dns_copy_name(dns_status.query_name, normalized);
    dns_copy_name(dns_status.canonical_name, normalized);
    dns_copy_name(dns_active_name, normalized);
    dns_status.transaction_id = dns_next_transaction_id();
    dns_status.state = DNS_STATE_RESOLVING_ARP;
    dns_status.last_error = OK;
    return dns_send_query();
}

int dns_maintain(void) {
    uint32_t frequency;
    uint32_t retry_ticks;
    int result;

    if (!dns_status.initialized) {
        LOG_ERROR("NET", "Manutencao DNS antes da inicializacao");
        return ERR_STATE;
    }
    dns_status.maintenance_cycles++;
    if (dns_status.state == DNS_STATE_RESOLVING_ARP) {
        result = dns_send_query();
        if (result != OK) {
            LOG_WARN("NET", "Envio DNS falhou durante resolucao ARP");
        }
        return result;
    }
    if (dns_status.state != DNS_STATE_WAITING_REPLY) return OK;
    frequency = timer_get_frequency();
    retry_ticks = dns_seconds_to_ticks(DNS_RETRY_SECONDS, frequency);
    if (!frequency || !retry_ticks) {
        LOG_ERROR("NET", "Timer indisponivel na manutencao DNS");
        return ERR_STATE;
    }
    if ((uint32_t)(timer_get_ticks() - dns_last_tx_tick) <
        retry_ticks) return OK;
    if (dns_status.attempts >= DNS_MAX_ATTEMPTS) {
        dns_fail(ERR_TIMEOUT);
        return OK;
    }
    result = dns_send_query();
    if (result != OK) {
        LOG_WARN("NET", "Retentativa DNS falhou");
    }
    return result;
}

int dns_reset(void) {
    if (!dns_status.initialized) {
        LOG_ERROR("NET", "Reset DNS antes da inicializacao");
        return ERR_STATE;
    }
    dns_reset_query();
    dns_status.last_error = OK;
    LOG_INFO("NET", "Consulta DNS reiniciada");
    return OK;
}

int dns_clear(void) {
    if (!dns_status.initialized) {
        LOG_ERROR("NET", "Limpeza DNS antes da inicializacao");
        return ERR_STATE;
    }
    kmemset(dns_cache, 0, sizeof(dns_cache));
    LOG_INFO("NET", "Cache DNS limpo");
    return OK;
}

int dns_get_status(dns_status_t* out_status) {
    uint32_t frequency;
    uint32_t now;

    if (!out_status) {
        LOG_ERROR("NET", "Destino nulo ao consultar DNS");
        return ERR_NULL;
    }
    *out_status = dns_status;
    out_status->cache_entries = 0;
    frequency = timer_get_frequency();
    now = timer_get_ticks();
    for (uint32_t index = 0; index < DNS_CACHE_CAPACITY; index++) {
        if (dns_cache[index].used &&
            !dns_cache_expired(&dns_cache[index], now, frequency)) {
            out_status->cache_entries++;
        }
    }
    return OK;
}

int dns_get_cache_entry(uint32_t index,
                        dns_cache_entry_info_t* out_entry) {
    uint32_t frequency;
    uint32_t now;
    uint32_t age_seconds;

    if (!out_entry) {
        LOG_ERROR("NET", "Destino nulo ao consultar cache DNS");
        return ERR_NULL;
    }
    if (index >= DNS_CACHE_CAPACITY) {
        LOG_ERROR("NET", "Indice de cache DNS invalido");
        return ERR_INVALID;
    }
    kmemset(out_entry, 0, sizeof(*out_entry));
    frequency = timer_get_frequency();
    now = timer_get_ticks();
    if (!dns_cache[index].used ||
        dns_cache_expired(&dns_cache[index], now, frequency)) return OK;
    age_seconds = frequency ?
        (uint32_t)(now - dns_cache[index].stored_tick) / frequency : 0U;
    out_entry->used = 1;
    dns_copy_name(out_entry->name, dns_cache[index].name);
    out_entry->address = dns_cache[index].address;
    out_entry->age_seconds = age_seconds;
    out_entry->ttl_remaining_seconds =
        dns_cache[index].ttl_seconds > age_seconds ?
        dns_cache[index].ttl_seconds - age_seconds : 0U;
    return OK;
}

static int dns_validate_name_vectors(void) {
    uint8_t message[] = {
        1U, 'a', 3U, 'c', 'o', 'm', 0U,
        0xC0U, 0x00U, 0xC0U, 0x09U
    };
    char name[DNS_NAME_BUFFER_SIZE];
    uint16_t offset = 7U;

    if (dns_read_name(message, sizeof(message), &offset, name) != OK ||
        !dns_name_equal(name, "a.com")) {
        LOG_ERROR("NET", "Vetor de compressao DNS falhou");
        return ERR_STATE;
    }
    offset = 9U;
    if (dns_read_name(message, sizeof(message), &offset, name) !=
        ERR_INVALID) {
        LOG_ERROR("NET", "Ciclo de ponteiro DNS foi aceito");
        return ERR_STATE;
    }
    return OK;
}

static int dns_validate_record_vectors(void) {
    uint8_t records[40U];
    dns_record_match_t match;
    uint16_t offset = 0;

    kmemset(records, 0, sizeof(records));
    records[offset++] = 1U;
    records[offset++] = 'a';
    records[offset++] = 0U;
    dns_write_u16(records + offset, DNS_TYPE_CNAME);
    dns_write_u16(records + offset + 2U, DNS_CLASS_IN);
    records[offset + 7U] = 60U;
    dns_write_u16(records + offset + 8U, 3U);
    offset += 10U;
    records[offset++] = 1U;
    records[offset++] = 'b';
    records[offset++] = 0U;
    records[offset++] = 1U;
    records[offset++] = 'b';
    records[offset++] = 0U;
    dns_write_u16(records + offset, DNS_TYPE_A);
    dns_write_u16(records + offset + 2U, DNS_CLASS_IN);
    records[offset + 7U] = 30U;
    dns_write_u16(records + offset + 8U, 4U);
    offset += 10U;
    records[offset++] = 10U;
    records[offset++] = 0U;
    records[offset++] = 2U;
    records[offset++] = 3U;
    if (dns_scan_records(records, offset, 0U, 2U, "a", &match) != OK ||
        !match.has_cname || !dns_name_equal(match.cname, "b") ||
        dns_scan_records(records, offset, 0U, 2U, "b", &match) != OK ||
        !match.has_a || match.address != 0x0A000203U) {
        LOG_ERROR("NET", "Vetor A/CNAME DNS falhou");
        return ERR_STATE;
    }
    return OK;
}

int dns_validate_state(void) {
    if (dns_validate_name_vectors() != OK ||
        dns_validate_record_vectors() != OK) return ERR_STATE;
    if (!dns_status.initialized) {
        if (dns_endpoint || dns_status.configured) {
            LOG_ERROR("NET", "DNS ativo sem inicializacao");
            return ERR_STATE;
        }
        return OK;
    }
    if (!dns_endpoint || !dns_status.local_port ||
        dns_status.state > DNS_STATE_FAILED ||
        (dns_status.configured &&
         !ipv4_address_is_unicast(dns_status.server_ip))) {
        LOG_ERROR("NET", "Estado basico DNS inconsistente");
        return ERR_STATE;
    }
    if ((dns_status.state == DNS_STATE_RESOLVING_ARP ||
         dns_status.state == DNS_STATE_WAITING_REPLY) &&
        (!dns_status.configured || !dns_status.query_name[0] ||
         !dns_active_name[0] ||
         dns_status.attempts > DNS_MAX_ATTEMPTS)) {
        LOG_ERROR("NET", "Consulta DNS inconsistente");
        return ERR_STATE;
    }
    for (uint32_t index = 0; index < DNS_CACHE_CAPACITY; index++) {
        if (dns_cache[index].used &&
            (!dns_cache[index].name[0] ||
             !ipv4_address_is_unicast(dns_cache[index].address) ||
             !dns_cache[index].ttl_seconds)) {
            LOG_ERROR("NET", "Entrada de cache DNS inconsistente");
            return ERR_STATE;
        }
    }
    return OK;
}

const char* dns_state_name(dns_state_t state) {
    if (state == DNS_STATE_IDLE) return "IDLE";
    if (state == DNS_STATE_RESOLVING_ARP) return "RESOLVING_ARP";
    if (state == DNS_STATE_WAITING_REPLY) return "WAITING_REPLY";
    if (state == DNS_STATE_COMPLETE) return "COMPLETE";
    if (state == DNS_STATE_FAILED) return "FAILED";
    return "DESCONHECIDO";
}
