#include "core/arp.h"
#include "core/errors.h"
#include "core/ethernet.h"
#include "core/log.h"
#include "core/string.h"
#include "core/timer.h"

#define ARP_ETHERTYPE 0x0806U
#define ARP_PROTOCOL_IPV4 0x0800U
#define ARP_HARDWARE_ETHERNET 0x0001U
#define ARP_OPERATION_REQUEST 0x0001U
#define ARP_OPERATION_REPLY 0x0002U
#define ARP_PACKET_SIZE 28U
#define ARP_HARDWARE_SIZE 6U
#define ARP_PROTOCOL_SIZE 4U
#define ARP_RETRY_INTERVAL_SECONDS 1U
#define ARP_MAX_ATTEMPTS 3U
#define ARP_CACHE_TTL_SECONDS 30U
#define ARP_BROADCAST_OCTET 0xFFU
#define ARP_MAX_TICK_INTERVAL 0xFFFFFFFFU
#define ARP_IPV4_BROADCAST 0xFFFFFFFFU
#define ARP_IPV4_FIRST_OCTET_SHIFT 24U
#define ARP_IPV4_LOOPBACK_PREFIX 127U
#define ARP_IPV4_MULTICAST_MIN 224U
#define ARP_IPV4_MULTICAST_MAX 239U
#define ARP_MAC_GROUP_BIT 0x01U

#define ARP_OFFSET_HARDWARE_TYPE 0U
#define ARP_OFFSET_PROTOCOL_TYPE 2U
#define ARP_OFFSET_HARDWARE_SIZE 4U
#define ARP_OFFSET_PROTOCOL_SIZE 5U
#define ARP_OFFSET_OPERATION 6U
#define ARP_OFFSET_SENDER_MAC 8U
#define ARP_OFFSET_SENDER_IP 14U
#define ARP_OFFSET_TARGET_MAC 18U
#define ARP_OFFSET_TARGET_IP 24U

typedef struct {
    uint8_t used;
    uint32_t ip_address;
    uint8_t mac_address[ARP_MAC_ADDRESS_SIZE];
    arp_entry_state_t state;
    uint32_t state_tick;
    uint32_t last_attempt_tick;
    uint8_t attempts;
} arp_cache_entry_t;

static arp_cache_entry_t arp_cache[ARP_CACHE_CAPACITY];
static arp_status_t arp_status;

static uint16_t arp_read_u16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

static uint32_t arp_read_u32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) |
           data[3];
}

static void arp_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

static void arp_write_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static uint8_t arp_mac_is_zero(const uint8_t* mac_address) {
    for (uint32_t index = 0; index < ARP_MAC_ADDRESS_SIZE; index++) {
        if (mac_address[index] != 0U) return 0;
    }
    return 1;
}

static uint8_t arp_mac_is_equal(const uint8_t* first,
                                const uint8_t* second) {
    for (uint32_t index = 0; index < ARP_MAC_ADDRESS_SIZE; index++) {
        if (first[index] != second[index]) return 0;
    }
    return 1;
}

static uint8_t arp_mac_is_valid(const uint8_t* mac_address) {
    return !arp_mac_is_zero(mac_address) &&
           (mac_address[0] & ARP_MAC_GROUP_BIT) == 0U;
}

static uint8_t arp_text_is_equal(const char* first, const char* second) {
    if (!first || !second) return 0;
    while (*first && *second && *first == *second) {
        first++;
        second++;
    }
    return *first == '\0' && *second == '\0';
}

static int arp_copy_text(char* destination, uint32_t capacity,
                         const char* source) {
    uint32_t length = 0;

    if (!destination || !source || !capacity) {
        LOG_ERROR("NET", "Texto ARP invalido");
        return ERR_NULL;
    }
    while (source[length]) {
        if (length + 1U >= capacity) {
            LOG_ERROR("NET", "ID de interface excede limite ARP");
            return ERR_OVERFLOW;
        }
        destination[length] = source[length];
        length++;
    }
    destination[length] = '\0';
    if (!length) {
        LOG_ERROR("NET", "ID vazio na configuracao ARP");
        return ERR_INVALID;
    }
    return OK;
}

uint8_t arp_ipv4_is_valid(uint32_t ip_address) {
    uint8_t first_octet =
        (uint8_t)(ip_address >> ARP_IPV4_FIRST_OCTET_SHIFT);

    if (ip_address == 0U || ip_address == ARP_IPV4_BROADCAST) return 0;
    if (first_octet == ARP_IPV4_LOOPBACK_PREFIX) return 0;
    return first_octet < ARP_IPV4_MULTICAST_MIN ||
           first_octet > ARP_IPV4_MULTICAST_MAX;
}

static uint32_t arp_seconds_to_ticks(uint32_t seconds,
                                     uint32_t frequency) {
    uint64_t result = (uint64_t)seconds * frequency;

    if (result > ARP_MAX_TICK_INTERVAL) return ARP_MAX_TICK_INTERVAL;
    return (uint32_t)result;
}

static uint8_t arp_elapsed(uint32_t now, uint32_t since,
                           uint32_t interval) {
    return (uint32_t)(now - since) >= interval;
}

static int32_t arp_find_entry(uint32_t ip_address) {
    for (uint32_t index = 0; index < ARP_CACHE_CAPACITY; index++) {
        if (arp_cache[index].used &&
            arp_cache[index].ip_address == ip_address) {
            return (int32_t)index;
        }
    }
    return -1;
}

static int32_t arp_select_entry(uint32_t now, uint32_t frequency) {
    int32_t oldest_index = -1;
    uint32_t oldest_age = 0;
    uint32_t ttl = arp_seconds_to_ticks(ARP_CACHE_TTL_SECONDS, frequency);

    for (uint32_t index = 0; index < ARP_CACHE_CAPACITY; index++) {
        arp_cache_entry_t* entry = &arp_cache[index];
        uint32_t age;

        if (!entry->used) return (int32_t)index;
        if (entry->state == ARP_ENTRY_INCOMPLETE) continue;
        age = (uint32_t)(now - entry->state_tick);
        if (age >= ttl) {
            kmemset(entry, 0, sizeof(*entry));
            return (int32_t)index;
        }
        if (oldest_index < 0 || age > oldest_age) {
            oldest_index = (int32_t)index;
            oldest_age = age;
        }
    }
    return oldest_index;
}

static void arp_build_packet(uint8_t* packet, uint16_t operation,
                             const uint8_t* target_mac,
                             uint32_t target_ip) {
    kmemset(packet, 0, ARP_PACKET_SIZE);
    arp_write_u16(packet + ARP_OFFSET_HARDWARE_TYPE,
                  ARP_HARDWARE_ETHERNET);
    arp_write_u16(packet + ARP_OFFSET_PROTOCOL_TYPE, ARP_PROTOCOL_IPV4);
    packet[ARP_OFFSET_HARDWARE_SIZE] = ARP_HARDWARE_SIZE;
    packet[ARP_OFFSET_PROTOCOL_SIZE] = ARP_PROTOCOL_SIZE;
    arp_write_u16(packet + ARP_OFFSET_OPERATION, operation);
    kmemcpy(packet + ARP_OFFSET_SENDER_MAC, arp_status.local_mac,
            ARP_MAC_ADDRESS_SIZE);
    arp_write_u32(packet + ARP_OFFSET_SENDER_IP, arp_status.local_ip);
    if (target_mac) {
        kmemcpy(packet + ARP_OFFSET_TARGET_MAC, target_mac,
                ARP_MAC_ADDRESS_SIZE);
    }
    arp_write_u32(packet + ARP_OFFSET_TARGET_IP, target_ip);
}

static int arp_send_request(arp_cache_entry_t* entry, uint32_t now) {
    uint8_t packet[ARP_PACKET_SIZE];
    uint8_t destination[ARP_MAC_ADDRESS_SIZE];
    int result;

    if (!entry) {
        LOG_ERROR("NET", "Entrada nula ao enviar request ARP");
        return ERR_NULL;
    }
    kmemset(destination, ARP_BROADCAST_OCTET, sizeof(destination));
    arp_build_packet(packet, ARP_OPERATION_REQUEST, NULL,
                     entry->ip_address);
    entry->attempts++;
    entry->last_attempt_tick = now;
    result = ethernet_send(destination, ARP_ETHERTYPE,
                           packet, sizeof(packet));
    if (result != OK) {
        arp_status.last_error = result;
        LOG_ERROR("NET", "Falha ao enviar request ARP");
        return result;
    }
    arp_status.tx_requests++;
    arp_status.last_error = OK;
    return OK;
}

static int arp_send_reply(const uint8_t* target_mac, uint32_t target_ip) {
    uint8_t packet[ARP_PACKET_SIZE];
    int result;

    arp_build_packet(packet, ARP_OPERATION_REPLY, target_mac, target_ip);
    result = ethernet_send(target_mac, ARP_ETHERTYPE,
                           packet, sizeof(packet));
    if (result != OK) {
        arp_status.last_error = result;
        LOG_ERROR("NET", "Falha ao enviar reply ARP");
        return result;
    }
    arp_status.tx_replies++;
    arp_status.last_error = OK;
    return OK;
}

static void arp_mark_resolved(arp_cache_entry_t* entry,
                              const uint8_t* mac_address,
                              uint32_t now) {
    entry->state = ARP_ENTRY_RESOLVED;
    entry->state_tick = now;
    kmemcpy(entry->mac_address, mac_address, ARP_MAC_ADDRESS_SIZE);
}

static int arp_learn_request(uint32_t ip_address,
                             const uint8_t* mac_address, uint32_t now) {
    uint32_t frequency = timer_get_frequency();
    int32_t index = arp_find_entry(ip_address);

    if (index < 0) index = arp_select_entry(now, frequency);
    if (index < 0) {
        LOG_WARN("NET", "Cache ARP sem entrada substituivel");
        return ERR_OVERFLOW;
    }
    kmemset(&arp_cache[index], 0, sizeof(arp_cache[index]));
    arp_cache[index].used = 1;
    arp_cache[index].ip_address = ip_address;
    arp_mark_resolved(&arp_cache[index], mac_address, now);
    return OK;
}

static uint8_t arp_header_is_valid(const ethernet_frame_view_t* frame,
                                   uint16_t* out_operation) {
    const uint8_t* packet;

    if (!frame || !out_operation || frame->payload_length < ARP_PACKET_SIZE) {
        return 0;
    }
    packet = frame->payload;
    *out_operation = arp_read_u16(packet + ARP_OFFSET_OPERATION);
    return frame->ethertype == ARP_ETHERTYPE &&
           arp_read_u16(packet + ARP_OFFSET_HARDWARE_TYPE) ==
               ARP_HARDWARE_ETHERNET &&
           arp_read_u16(packet + ARP_OFFSET_PROTOCOL_TYPE) ==
               ARP_PROTOCOL_IPV4 &&
           packet[ARP_OFFSET_HARDWARE_SIZE] == ARP_HARDWARE_SIZE &&
           packet[ARP_OFFSET_PROTOCOL_SIZE] == ARP_PROTOCOL_SIZE &&
           (*out_operation == ARP_OPERATION_REQUEST ||
            *out_operation == ARP_OPERATION_REPLY);
}

static int arp_handle_request(const ethernet_frame_view_t* frame,
                              uint32_t sender_ip, uint32_t target_ip) {
    const uint8_t* sender_mac =
        frame->payload + ARP_OFFSET_SENDER_MAC;
    const uint8_t* target_mac =
        frame->payload + ARP_OFFSET_TARGET_MAC;
    uint32_t now = timer_get_ticks();

    arp_status.rx_requests++;
    if (!arp_status.configured || target_ip != arp_status.local_ip) {
        arp_status.ignored_packets++;
        return OK;
    }
    if ((!arp_mac_is_zero(target_mac) &&
         !arp_mac_is_equal(target_mac, arp_status.local_mac)) ||
        sender_ip == arp_status.local_ip) {
        arp_status.invalid_packets++;
        LOG_DEBUG("NET", "Request ARP com enderecos inconsistentes");
        return OK;
    }
    if (arp_learn_request(sender_ip, sender_mac, now) != OK) {
        arp_status.last_error = ERR_OVERFLOW;
    }
    return arp_send_reply(sender_mac, sender_ip);
}

static int arp_handle_reply(const ethernet_frame_view_t* frame,
                            uint32_t sender_ip, uint32_t target_ip) {
    const uint8_t* sender_mac =
        frame->payload + ARP_OFFSET_SENDER_MAC;
    const uint8_t* target_mac =
        frame->payload + ARP_OFFSET_TARGET_MAC;
    int32_t index;

    arp_status.rx_replies++;
    if (!arp_status.configured || target_ip != arp_status.local_ip ||
        !arp_mac_is_equal(target_mac, arp_status.local_mac) ||
        frame->destination_type != ETHERNET_DESTINATION_LOCAL_UNICAST ||
        !arp_mac_is_equal(frame->destination, arp_status.local_mac)) {
        arp_status.ignored_packets++;
        return OK;
    }
    index = arp_find_entry(sender_ip);
    if (index < 0 ||
        arp_cache[index].state != ARP_ENTRY_INCOMPLETE) {
        arp_status.ignored_packets++;
        return OK;
    }
    arp_mark_resolved(&arp_cache[index], sender_mac, timer_get_ticks());
    arp_status.last_error = OK;
    return OK;
}

static int arp_handle_frame(const ethernet_frame_view_t* frame) {
    const uint8_t* packet;
    const uint8_t* sender_mac;
    uint16_t operation = 0;
    uint32_t sender_ip;
    uint32_t target_ip;

    if (!arp_header_is_valid(frame, &operation)) {
        arp_status.invalid_packets++;
        LOG_DEBUG("NET", "Pacote ARP com cabecalho invalido");
        return OK;
    }
    packet = frame->payload;
    sender_mac = packet + ARP_OFFSET_SENDER_MAC;
    sender_ip = arp_read_u32(packet + ARP_OFFSET_SENDER_IP);
    target_ip = arp_read_u32(packet + ARP_OFFSET_TARGET_IP);
    if (!arp_mac_is_valid(sender_mac) ||
        !arp_mac_is_equal(sender_mac, frame->source) ||
        !arp_ipv4_is_valid(sender_ip) ||
        !arp_ipv4_is_valid(target_ip)) {
        arp_status.invalid_packets++;
        LOG_DEBUG("NET", "Pacote ARP com enderecos invalidos");
        return OK;
    }
    if (operation == ARP_OPERATION_REQUEST) {
        return arp_handle_request(frame, sender_ip, target_ip);
    }
    return arp_handle_reply(frame, sender_ip, target_ip);
}

int arp_init(void) {
    int result;

    LOG_INFO("NET", "Inicializando protocolo ARP");
    if (arp_status.initialized) {
        LOG_WARN("NET", "Protocolo ARP ja estava inicializado");
        LOG_INFO("NET", "Protocolo ARP inicializado com sucesso");
        return OK;
    }
    kmemset(arp_cache, 0, sizeof(arp_cache));
    kmemset(&arp_status, 0, sizeof(arp_status));
    result = ethernet_register_handler(ARP_ETHERTYPE, arp_handle_frame);
    if (result != OK) {
        arp_status.last_error = result;
        LOG_ERROR("NET", "Falha ao registrar protocolo ARP");
        return result;
    }
    arp_status.initialized = 1;
    arp_status.last_error = OK;
    LOG_INFO("NET", "Protocolo ARP inicializado com sucesso");
    return OK;
}

int arp_configure(const char* interface_id, const uint8_t* local_mac,
                  uint32_t local_ip) {
    char validated_id[ARP_INTERFACE_ID_SIZE];
    int result;

    if (!arp_status.initialized) {
        LOG_ERROR("NET", "Configuracao ARP antes da inicializacao");
        return ERR_STATE;
    }
    if (!interface_id || !local_mac) {
        LOG_ERROR("NET", "Argumento nulo na configuracao ARP");
        return ERR_NULL;
    }
    if (!arp_ipv4_is_valid(local_ip) || !arp_mac_is_valid(local_mac)) {
        LOG_ERROR("NET", "Endereco invalido na configuracao ARP");
        return ERR_INVALID;
    }
    result = arp_copy_text(validated_id, sizeof(validated_id),
                           interface_id);
    if (result != OK) return result;
    if (arp_status.configured && arp_status.local_ip == local_ip &&
        arp_text_is_equal(arp_status.interface_id, validated_id) &&
        arp_mac_is_equal(arp_status.local_mac, local_mac)) {
        return OK;
    }
    kmemset(arp_cache, 0, sizeof(arp_cache));
    kmemset(&arp_status, 0, sizeof(arp_status));
    arp_status.initialized = 1;
    arp_status.configured = 1;
    arp_status.local_ip = local_ip;
    kmemcpy(arp_status.local_mac, local_mac, ARP_MAC_ADDRESS_SIZE);
    arp_copy_text(arp_status.interface_id,
                  sizeof(arp_status.interface_id), validated_id);
    arp_status.last_error = OK;
    LOG_INFO("NET", "Sessao ARP configurada em memoria");
    return OK;
}

int arp_unconfigure(void) {
    if (!arp_status.initialized) {
        LOG_ERROR("NET", "Remocao ARP antes da inicializacao");
        return ERR_STATE;
    }
    kmemset(arp_cache, 0, sizeof(arp_cache));
    kmemset(&arp_status, 0, sizeof(arp_status));
    arp_status.initialized = 1;
    arp_status.last_error = OK;
    LOG_INFO("NET", "Configuracao ARP removida da memoria");
    return OK;
}

int arp_resolve(uint32_t ip_address, uint8_t* out_mac,
                uint8_t* out_resolved) {
    uint32_t now;
    uint32_t frequency;
    uint32_t ttl_ticks;
    int32_t index;

    if (!out_mac || !out_resolved) {
        LOG_ERROR("NET", "Destino nulo na resolucao ARP");
        return ERR_NULL;
    }
    *out_resolved = 0;
    kmemset(out_mac, 0, ARP_MAC_ADDRESS_SIZE);
    if (!arp_status.initialized || !arp_status.configured) {
        LOG_ERROR("NET", "Resolucao ARP antes da configuracao");
        return ERR_STATE;
    }
    if (!arp_ipv4_is_valid(ip_address)) {
        LOG_ERROR("NET", "IPv4 invalido na resolucao ARP");
        return ERR_INVALID;
    }
    if (ip_address == arp_status.local_ip) {
        kmemcpy(out_mac, arp_status.local_mac, ARP_MAC_ADDRESS_SIZE);
        *out_resolved = 1;
        return OK;
    }
    now = timer_get_ticks();
    frequency = timer_get_frequency();
    if (!frequency) {
        LOG_ERROR("NET", "Timer indisponivel para resolucao ARP");
        return ERR_STATE;
    }
    index = arp_find_entry(ip_address);
    ttl_ticks = arp_seconds_to_ticks(ARP_CACHE_TTL_SECONDS, frequency);
    if (index >= 0 &&
        arp_cache[index].state != ARP_ENTRY_INCOMPLETE &&
        arp_elapsed(now, arp_cache[index].state_tick, ttl_ticks)) {
        kmemset(&arp_cache[index], 0, sizeof(arp_cache[index]));
        index = -1;
    }
    if (index >= 0 && arp_cache[index].state == ARP_ENTRY_RESOLVED) {
        kmemcpy(out_mac, arp_cache[index].mac_address,
                ARP_MAC_ADDRESS_SIZE);
        *out_resolved = 1;
        arp_status.cache_hits++;
        return OK;
    }
    if (index >= 0 && arp_cache[index].state == ARP_ENTRY_FAILED) {
        LOG_WARN("NET", "Resolucao ARP em estado de timeout");
        return ERR_TIMEOUT;
    }
    if (index >= 0) return OK;
    index = arp_select_entry(now, frequency);
    if (index < 0) {
        LOG_ERROR("NET", "Cache ARP ocupado por resolucoes pendentes");
        return ERR_OVERFLOW;
    }
    kmemset(&arp_cache[index], 0, sizeof(arp_cache[index]));
    arp_cache[index].used = 1;
    arp_cache[index].ip_address = ip_address;
    arp_cache[index].state = ARP_ENTRY_INCOMPLETE;
    arp_cache[index].state_tick = now;
    return arp_send_request(&arp_cache[index], now);
}

static int arp_maintain_entry(arp_cache_entry_t* entry, uint32_t now,
                              uint32_t retry_ticks, uint32_t ttl_ticks) {
    if (!entry->used) return OK;
    if (entry->state != ARP_ENTRY_INCOMPLETE) {
        if (arp_elapsed(now, entry->state_tick, ttl_ticks)) {
            kmemset(entry, 0, sizeof(*entry));
        }
        return OK;
    }
    if (!arp_elapsed(now, entry->last_attempt_tick, retry_ticks)) return OK;
    if (entry->attempts < ARP_MAX_ATTEMPTS) {
        return arp_send_request(entry, now);
    }
    entry->state = ARP_ENTRY_FAILED;
    entry->state_tick = now;
    arp_status.timeouts++;
    arp_status.last_error = ERR_TIMEOUT;
    return OK;
}

int arp_maintain(void) {
    uint32_t frequency;
    uint32_t retry_ticks;
    uint32_t ttl_ticks;
    uint32_t now;
    int first_error = OK;

    if (!arp_status.initialized) {
        LOG_ERROR("NET", "Manutencao ARP antes da inicializacao");
        return ERR_STATE;
    }
    if (!arp_status.configured) return OK;
    frequency = timer_get_frequency();
    if (!frequency) {
        LOG_ERROR("NET", "Timer indisponivel na manutencao ARP");
        return ERR_STATE;
    }
    arp_status.maintenance_cycles++;
    now = timer_get_ticks();
    retry_ticks = arp_seconds_to_ticks(ARP_RETRY_INTERVAL_SECONDS,
                                       frequency);
    ttl_ticks = arp_seconds_to_ticks(ARP_CACHE_TTL_SECONDS, frequency);
    for (uint32_t index = 0; index < ARP_CACHE_CAPACITY; index++) {
        int result = arp_maintain_entry(&arp_cache[index], now,
                                        retry_ticks, ttl_ticks);
        if (result != OK && first_error == OK) first_error = result;
    }
    if (first_error != OK) {
        arp_status.maintenance_errors++;
        LOG_ERROR("NET", "Falha durante manutencao ARP");
    }
    return first_error;
}

int arp_clear(void) {
    if (!arp_status.initialized || !arp_status.configured) {
        LOG_ERROR("NET", "Limpeza ARP antes da configuracao");
        return ERR_STATE;
    }
    kmemset(arp_cache, 0, sizeof(arp_cache));
    arp_status.last_error = OK;
    LOG_INFO("NET", "Cache ARP limpo");
    return OK;
}

static void arp_count_entries(arp_status_t* status) {
    for (uint32_t index = 0; index < ARP_CACHE_CAPACITY; index++) {
        if (!arp_cache[index].used) continue;
        status->cache_entries++;
        if (arp_cache[index].state == ARP_ENTRY_INCOMPLETE) {
            status->incomplete_entries++;
        } else if (arp_cache[index].state == ARP_ENTRY_RESOLVED) {
            status->resolved_entries++;
        } else if (arp_cache[index].state == ARP_ENTRY_FAILED) {
            status->failed_entries++;
        }
    }
}

int arp_get_status(arp_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("NET", "Destino nulo ao consultar ARP");
        return ERR_NULL;
    }
    *out_status = arp_status;
    out_status->cache_entries = 0;
    out_status->incomplete_entries = 0;
    out_status->resolved_entries = 0;
    out_status->failed_entries = 0;
    arp_count_entries(out_status);
    return OK;
}

int arp_get_cache_entry(uint32_t index,
                        arp_cache_entry_info_t* out_entry) {
    uint32_t frequency;
    uint32_t now;

    if (!out_entry) {
        LOG_ERROR("NET", "Destino nulo ao consultar entrada ARP");
        return ERR_NULL;
    }
    if (index >= ARP_CACHE_CAPACITY) {
        LOG_ERROR("NET", "Indice de cache ARP invalido");
        return ERR_INVALID;
    }
    kmemset(out_entry, 0, sizeof(*out_entry));
    if (!arp_cache[index].used) return OK;
    out_entry->used = 1;
    out_entry->ip_address = arp_cache[index].ip_address;
    out_entry->state = arp_cache[index].state;
    out_entry->attempts = arp_cache[index].attempts;
    kmemcpy(out_entry->mac_address, arp_cache[index].mac_address,
            ARP_MAC_ADDRESS_SIZE);
    frequency = timer_get_frequency();
    now = timer_get_ticks();
    if (frequency) {
        out_entry->age_seconds =
            (uint32_t)(now - arp_cache[index].state_tick) / frequency;
    }
    return OK;
}

static int arp_validate_entry(uint32_t index) {
    const arp_cache_entry_t* entry = &arp_cache[index];

    if (!entry->used) return OK;
    if (!arp_ipv4_is_valid(entry->ip_address) ||
        entry->ip_address == arp_status.local_ip ||
        entry->state > ARP_ENTRY_FAILED ||
        (entry->state == ARP_ENTRY_INCOMPLETE &&
         (!entry->attempts || entry->attempts > ARP_MAX_ATTEMPTS)) ||
        (entry->state == ARP_ENTRY_RESOLVED &&
         !arp_mac_is_valid(entry->mac_address)) ||
        (entry->state == ARP_ENTRY_FAILED &&
         entry->attempts != ARP_MAX_ATTEMPTS)) {
        LOG_ERROR("NET", "Entrada individual do cache ARP invalida");
        return ERR_STATE;
    }
    for (uint32_t other = index + 1U;
         other < ARP_CACHE_CAPACITY; other++) {
        if (arp_cache[other].used &&
            arp_cache[other].ip_address == entry->ip_address) {
            LOG_ERROR("NET", "Cache ARP contem IPv4 duplicado");
            return ERR_STATE;
        }
    }
    return OK;
}

int arp_validate_state(void) {
    if (!arp_status.initialized) {
        if (arp_status.configured) {
            LOG_ERROR("NET", "ARP configurado sem inicializacao");
            return ERR_STATE;
        }
        return OK;
    }
    if (arp_status.configured &&
        (!arp_status.interface_id[0] ||
         !arp_ipv4_is_valid(arp_status.local_ip) ||
         !arp_mac_is_valid(arp_status.local_mac))) {
        LOG_ERROR("NET", "Configuracao ARP inconsistente");
        return ERR_STATE;
    }
    for (uint32_t index = 0; index < ARP_CACHE_CAPACITY; index++) {
        if ((!arp_status.configured && arp_cache[index].used) ||
            arp_validate_entry(index) != OK) {
            LOG_ERROR("NET", "Cache ARP inconsistente");
            return ERR_STATE;
        }
    }
    return OK;
}

const char* arp_entry_state_name(arp_entry_state_t state) {
    if (state == ARP_ENTRY_INCOMPLETE) return "INCOMPLETE";
    if (state == ARP_ENTRY_RESOLVED) return "RESOLVED";
    if (state == ARP_ENTRY_FAILED) return "FAILED";
    return "INVALID";
}
