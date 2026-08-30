#include "core/route.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"

static route_entry_t route_entries[ROUTE_TABLE_CAPACITY];
static route_entry_t route_base_direct;
static route_entry_t route_base_default;
static route_status_t route_status;
static uint8_t route_base_valid;

static int route_copy_text(char* destination, uint32_t capacity,
                           const char* source) {
    uint32_t length = 0U;

    if (!destination || !source || !capacity) {
        LOG_ERROR("NET", "Texto invalido na tabela de rotas");
        return ERR_NULL;
    }
    while (source[length]) {
        if (length + 1U >= capacity) {
            LOG_ERROR("NET", "ID de interface excede a tabela de rotas");
            return ERR_OVERFLOW;
        }
        destination[length] = source[length];
        length++;
    }
    destination[length] = '\0';
    if (!length) {
        LOG_ERROR("NET", "ID vazio na tabela de rotas");
        return ERR_INVALID;
    }
    return OK;
}

static int route_mask_prefix(uint32_t subnet_mask,
                             uint8_t* out_prefix_length) {
    uint8_t prefix_length = 0U;
    uint8_t saw_zero = 0U;

    if (!out_prefix_length) {
        LOG_ERROR("NET", "Prefixo nulo na mascara de rota");
        return ERR_NULL;
    }
    for (uint32_t bit = 0U; bit < 32U; bit++) {
        if (subnet_mask & (0x80000000U >> bit)) {
            if (saw_zero) return ERR_INVALID;
            prefix_length++;
        } else {
            saw_zero = 1U;
        }
    }
    *out_prefix_length = prefix_length;
    return OK;
}

static int route_prepare_entry(route_entry_t* entry, uint32_t network,
                               uint32_t subnet_mask, uint32_t gateway,
                               const char* interface_id) {
    int result;

    if (!entry || !interface_id) return ERR_NULL;
    kmemset(entry, 0, sizeof(*entry));
    result = route_mask_prefix(subnet_mask, &entry->prefix_length);
    if (result != OK) {
        LOG_WARN("NET", "Mascara invalida na tabela de rotas");
        return result;
    }
    if (network != (network & subnet_mask)) {
        LOG_WARN("NET", "Rede nao canonica na tabela de rotas");
        return ERR_INVALID;
    }
    if (gateway && !ipv4_address_is_unicast(gateway)) {
        LOG_WARN("NET", "Gateway invalido na tabela de rotas");
        return ERR_INVALID;
    }
    result = route_copy_text(entry->interface_id,
                             sizeof(entry->interface_id), interface_id);
    if (result != OK) return result;
    entry->network = network;
    entry->subnet_mask = subnet_mask;
    entry->gateway = gateway;
    entry->used = 1U;
    return OK;
}

static int32_t route_find(uint32_t network, uint32_t subnet_mask) {
    for (uint32_t index = 0U; index < ROUTE_TABLE_CAPACITY; index++) {
        if (route_entries[index].used &&
            route_entries[index].network == network &&
            route_entries[index].subnet_mask == subnet_mask) {
            return (int32_t)index;
        }
    }
    return -1;
}

static int32_t route_find_free(void) {
    for (uint32_t index = 0U; index < ROUTE_TABLE_CAPACITY; index++) {
        if (!route_entries[index].used) return (int32_t)index;
    }
    return -1;
}

static int route_insert(const route_entry_t* entry) {
    int32_t index;

    if (!entry) {
        LOG_ERROR("NET", "Entrada nula ao inserir rota");
        return ERR_NULL;
    }
    index = route_find_free();
    if (index < 0) {
        LOG_WARN("NET", "Tabela de rotas sem espaco");
        return ERR_OVERFLOW;
    }
    route_entries[index] = *entry;
    route_status.entry_count++;
    return OK;
}

static void route_clear_entries(void) {
    kmemset(route_entries, 0, sizeof(route_entries));
    route_status.entry_count = 0U;
}

int route_init(void) {
    if (route_status.initialized) return OK;
    kmemset(&route_status, 0, sizeof(route_status));
    kmemset(route_entries, 0, sizeof(route_entries));
    kmemset(&route_base_direct, 0, sizeof(route_base_direct));
    kmemset(&route_base_default, 0, sizeof(route_base_default));
    route_status.initialized = 1U;
    route_status.last_error = OK;
    LOG_INFO("NET", "Tabela de rotas inicializada");
    return OK;
}

int route_reset(void) {
    int result;

    if (!route_status.initialized) {
        LOG_ERROR("NET", "Reset de rotas antes da inicializacao");
        return ERR_STATE;
    }
    route_clear_entries();
    if (!route_base_valid) {
        route_status.last_error = ERR_STATE;
        return ERR_STATE;
    }
    result = route_insert(&route_base_direct);
    if (result == OK && route_base_default.used) {
        result = route_insert(&route_base_default);
    }
    if (result != OK) {
        route_status.last_error = result;
        LOG_ERROR("NET", "Falha ao restaurar rotas base");
        return result;
    }
    route_status.last_error = OK;
    return OK;
}

int route_clear(void) {
    if (!route_status.initialized) {
        LOG_ERROR("NET", "Limpeza de rotas antes da inicializacao");
        return ERR_STATE;
    }
    route_clear_entries();
    kmemset(&route_base_direct, 0, sizeof(route_base_direct));
    kmemset(&route_base_default, 0, sizeof(route_base_default));
    route_base_valid = 0U;
    route_status.last_error = OK;
    return OK;
}

int route_set_base(uint32_t local_ip, uint32_t subnet_mask,
                   uint32_t gateway, const char* interface_id) {
    uint32_t network;
    uint32_t broadcast;
    int result;

    if (!route_status.initialized) {
        LOG_ERROR("NET", "Configuracao de rota antes da inicializacao");
        return ERR_STATE;
    }
    if (!interface_id) return ERR_NULL;
    if (!ipv4_address_is_unicast(local_ip) ||
        !ipv4_mask_is_valid(subnet_mask)) {
        LOG_ERROR("NET", "Configuracao base de rota invalida");
        return ERR_INVALID;
    }
    network = local_ip & subnet_mask;
    broadcast = network | ~subnet_mask;
    if (local_ip == network || local_ip == broadcast ||
        (gateway && (!ipv4_address_is_unicast(gateway) ||
                     (gateway & subnet_mask) != network ||
                     gateway == local_ip || gateway == network ||
                     gateway == broadcast))) {
        LOG_ERROR("NET", "Gateway invalido na configuracao base de rota");
        return ERR_INVALID;
    }
    kmemset(&route_base_direct, 0, sizeof(route_base_direct));
    result = route_prepare_entry(&route_base_direct, network, subnet_mask,
                                 0U, interface_id);
    if (result != OK) return result;
    kmemset(&route_base_default, 0, sizeof(route_base_default));
    if (gateway) {
        result = route_prepare_entry(&route_base_default, 0U, 0U,
                                     gateway, interface_id);
        if (result != OK) return result;
    }
    route_base_valid = 1U;
    return route_reset();
}

int route_add(uint32_t network, uint32_t subnet_mask, uint32_t gateway,
              const char* interface_id) {
    route_entry_t entry;
    int result;

    if (!route_status.initialized) {
        LOG_ERROR("NET", "Adicao de rota antes da inicializacao");
        return ERR_STATE;
    }
    result = route_prepare_entry(&entry, network, subnet_mask, gateway,
                                 interface_id);
    if (result != OK) {
        route_status.last_error = result;
        return result;
    }
    if (route_find(network, subnet_mask) >= 0) {
        route_status.last_error = ERR_STATE;
        LOG_WARN("NET", "Rota duplicada rejeitada");
        return ERR_STATE;
    }
    result = route_insert(&entry);
    if (result != OK) {
        route_status.last_error = result;
        LOG_WARN("NET", "Tabela de rotas cheia");
        return result;
    }
    route_status.adds++;
    route_status.last_error = OK;
    return OK;
}

int route_delete(uint32_t network, uint32_t subnet_mask) {
    int32_t index;
    uint8_t prefix_length;

    if (!route_status.initialized) {
        LOG_ERROR("NET", "Remocao de rota antes da inicializacao");
        return ERR_STATE;
    }
    if (route_mask_prefix(subnet_mask, &prefix_length) != OK ||
        network != (network & subnet_mask)) {
        LOG_WARN("NET", "Rota invalida na remocao");
        return ERR_INVALID;
    }
    index = route_find(network, subnet_mask);
    if (index < 0) {
        route_status.last_error = ERR_NOT_FOUND;
        LOG_WARN("NET", "Rota nao encontrada na remocao");
        return ERR_NOT_FOUND;
    }
    kmemset(&route_entries[index], 0, sizeof(route_entries[index]));
    if (route_status.entry_count) route_status.entry_count--;
    route_status.deletes++;
    route_status.last_error = OK;
    return OK;
}

int route_set_default(uint32_t gateway, const char* interface_id) {
    route_entry_t entry;
    int32_t index;
    int result;

    if (!gateway) {
        LOG_WARN("NET", "Gateway padrao ausente");
        return ERR_INVALID;
    }
    result = route_prepare_entry(&entry, 0U, 0U, gateway, interface_id);
    if (result != OK) return result;
    if (!route_status.initialized) {
        LOG_ERROR("NET", "Gateway padrao antes da inicializacao");
        return ERR_STATE;
    }
    index = route_find(0U, 0U);
    if (index >= 0) {
        route_entries[index] = entry;
        route_status.replacements++;
        route_status.last_error = OK;
        return OK;
    }
    result = route_insert(&entry);
    if (result != OK) {
        route_status.last_error = result;
        return result;
    }
    route_status.adds++;
    route_status.last_error = OK;
    return OK;
}

int route_lookup(uint32_t destination_ip, route_match_t* out_match) {
    int32_t best_index = -1;
    uint8_t best_prefix = 0U;

    if (!out_match) {
        LOG_ERROR("NET", "Destino nulo no lookup de rota");
        return ERR_NULL;
    }
    kmemset(out_match, 0, sizeof(*out_match));
    if (!route_status.initialized) return ERR_STATE;
    route_status.lookups++;
    for (uint32_t index = 0U; index < ROUTE_TABLE_CAPACITY; index++) {
        route_entry_t* entry = &route_entries[index];

        if (!entry->used ||
            (destination_ip & entry->subnet_mask) != entry->network) {
            continue;
        }
        if (best_index < 0 || entry->prefix_length > best_prefix) {
            best_index = (int32_t)index;
            best_prefix = entry->prefix_length;
        }
    }
    if (best_index < 0) {
        route_status.misses++;
        route_status.last_error = ERR_NOT_FOUND;
        return ERR_NOT_FOUND;
    }
    out_match->matched = 1U;
    out_match->entry = route_entries[best_index];
    out_match->via_gateway = out_match->entry.gateway != 0U;
    out_match->next_hop = out_match->via_gateway ?
                          out_match->entry.gateway : destination_ip;
    route_status.matches++;
    route_status.last_error = OK;
    return OK;
}

int route_get_status(route_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("NET", "Destino nulo ao consultar rotas");
        return ERR_NULL;
    }
    if (!route_status.initialized) return ERR_STATE;
    *out_status = route_status;
    return OK;
}

int route_get_entry(uint32_t index, route_entry_t* out_entry) {
    if (!out_entry) {
        LOG_ERROR("NET", "Destino nulo ao consultar entrada de rota");
        return ERR_NULL;
    }
    if (index >= ROUTE_TABLE_CAPACITY) return ERR_INVALID;
    if (!route_status.initialized) return ERR_STATE;
    *out_entry = route_entries[index];
    return OK;
}

int route_validate_state(void) {
    uint32_t count = 0U;

    if (!route_status.initialized) return ERR_STATE;
    for (uint32_t index = 0U; index < ROUTE_TABLE_CAPACITY; index++) {
        route_entry_t* entry = &route_entries[index];
        uint8_t prefix_length;

        if (!entry->used) continue;
        count++;
        if (route_mask_prefix(entry->subnet_mask, &prefix_length) != OK ||
            prefix_length != entry->prefix_length ||
            entry->network != (entry->network & entry->subnet_mask) ||
            !entry->interface_id[0] ||
            (entry->gateway && !ipv4_address_is_unicast(entry->gateway))) {
            LOG_ERROR("NET", "Entrada de rota inconsistente");
            return ERR_STATE;
        }
        for (uint32_t other = index + 1U;
             other < ROUTE_TABLE_CAPACITY; other++) {
            if (route_entries[other].used &&
                route_entries[other].network == entry->network &&
                route_entries[other].subnet_mask == entry->subnet_mask) {
                LOG_ERROR("NET", "Rotas duplicadas na tabela");
                return ERR_STATE;
            }
        }
    }
    if (count != route_status.entry_count ||
        count > ROUTE_TABLE_CAPACITY) {
        LOG_ERROR("NET", "Contagem de rotas inconsistente");
        return ERR_STATE;
    }
    return OK;
}

static void route_test_count(route_self_test_result_t* result,
                             uint8_t passed) {
    if (passed) result->passed++;
    else result->failed++;
}

int route_self_test(route_self_test_result_t* out_result) {
    route_entry_t saved_entries[ROUTE_TABLE_CAPACITY];
    route_entry_t saved_direct;
    route_entry_t saved_default;
    route_status_t saved_status;
    uint8_t saved_base_valid;
    uint8_t invalid_mask;
    uint8_t invalid_gateway;
    uint8_t invalid_interface;
    route_match_t match;
    int result;

    if (!out_result) {
        LOG_ERROR("NET", "Destino nulo no autoteste de rotas");
        return ERR_NULL;
    }
    kmemset(out_result, 0, sizeof(*out_result));
    if (!route_status.initialized) return ERR_STATE;
    kmemcpy(saved_entries, route_entries, sizeof(saved_entries));
    saved_direct = route_base_direct;
    saved_default = route_base_default;
    saved_status = route_status;
    saved_base_valid = route_base_valid;
    route_clear_entries();
    route_base_valid = 0U;
    out_result->lifecycle = route_validate_state() == OK;
    route_test_count(out_result, out_result->lifecycle);
    result = route_add(0x0A000000U, 0xFF000000U, 0x0A000001U,
                       "net-pci-00:03.0");
    out_result->default_route = result == OK;
    result = route_add(0x0A010000U, 0xFFFF0000U, 0U,
                       "net-pci-00:03.0");
    out_result->direct_route = result == OK;
    route_test_count(out_result, out_result->direct_route);
    result = route_lookup(0x0A010203U, &match);
    out_result->longest_prefix = result == OK && match.matched &&
        !match.via_gateway && match.entry.prefix_length == 16U;
    route_test_count(out_result, out_result->longest_prefix);
    result = route_lookup(0xC0000201U, &match);
    out_result->default_route = out_result->default_route &&
        result == OK && match.via_gateway && match.next_hop == 0x0A000001U;
    route_test_count(out_result, out_result->default_route);
    out_result->duplicate_route = route_add(
        0x0A000000U, 0xFF000000U, 0x0A000001U,
        "net-pci-00:03.0") == ERR_STATE;
    route_test_count(out_result, out_result->duplicate_route);
    invalid_mask = route_add(
        0x0A000000U, 0xFF00FF00U, 0U,
        "net-pci-00:03.0") == ERR_INVALID;
    invalid_gateway = route_add(
        0x0A020000U, 0xFFFF0000U, 0xE0000001U,
        "net-pci-00:03.0") == ERR_INVALID;
    invalid_interface = route_add(
        0x0A030000U, 0xFFFF0000U, 0U, "") == ERR_INVALID;
    out_result->invalid_input = invalid_mask && invalid_gateway &&
        invalid_interface;
    route_test_count(out_result, out_result->invalid_input);
    out_result->delete_route = route_delete(
        0x0A010000U, 0xFFFF0000U) == OK &&
        route_lookup(0x0A010203U, &match) == OK && match.via_gateway;
    route_test_count(out_result, out_result->delete_route);
    route_clear_entries();
    for (uint32_t index = 0U; index < ROUTE_TABLE_CAPACITY; index++) {
        result = route_add(index << 24U, 0xFF000000U, 0U,
                           "net-pci-00:03.0");
        if (result != OK) break;
    }
    out_result->overflow = result == OK &&
        route_add(0xF0000000U, 0xFF000000U, 0U,
                  "net-pci-00:03.0") == ERR_OVERFLOW;
    route_test_count(out_result, out_result->overflow);
    out_result->reset = route_set_base(
        0x0A00020FU, 0xFFFFFF00U, 0x0A000202U,
        "net-pci-00:03.0") == OK && route_reset() == OK &&
        route_status.entry_count == 2U;
    route_test_count(out_result, out_result->reset);
    out_result->invariants = route_validate_state() == OK;
    route_test_count(out_result, out_result->invariants);
    kmemcpy(route_entries, saved_entries, sizeof(route_entries));
    route_base_direct = saved_direct;
    route_base_default = saved_default;
    route_status = saved_status;
    route_base_valid = saved_base_valid;
    if (out_result->failed) {
        LOG_ERROR("NET", "Autoteste de rotas falhou");
        return ERR_STATE;
    }
    return OK;
}
