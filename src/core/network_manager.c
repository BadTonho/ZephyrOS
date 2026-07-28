#include "core/network_manager.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/string.h"
#include "drivers/e1000.h"
#include "drivers/pci.h"

#define NETWORK_PCI_CLASS 0x02U
#define NETWORK_VENDOR_INTEL 0x8086U
#define NETWORK_DEVICE_E1000 0x100EU
#define NETWORK_VENDOR_REALTEK 0x10ECU
#define NETWORK_DEVICE_RTL8139 0x8139U
#define NETWORK_HEX_BYTE_DIGITS 2U
#define NETWORK_DIAGNOSTIC_FRAME_SIZE 60U
#define NETWORK_ETHERNET_DESTINATION_OFFSET 0U
#define NETWORK_ETHERNET_SOURCE_OFFSET 6U
#define NETWORK_ETHERNET_TYPE_OFFSET 12U
#define NETWORK_ETHERNET_PAYLOAD_OFFSET 14U
#define NETWORK_DIAGNOSTIC_ETHERTYPE_HIGH 0x88U
#define NETWORK_DIAGNOSTIC_ETHERTYPE_LOW 0xB5U
#define NETWORK_ETHERNET_BROADCAST_OCTET 0xFFU

static network_interface_info_t
    network_interfaces[NETWORK_MANAGER_MAX_INTERFACES];
static network_manager_status_t network_status;
static int network_manager_initialized = 0;

static void network_append_char(char* text, uint32_t capacity,
                                uint32_t* offset, char value) {
    if (!text || !offset || !capacity || *offset + 1U >= capacity) return;
    text[*offset] = value;
    (*offset)++;
    text[*offset] = '\0';
}

static void network_append_text(char* text, uint32_t capacity,
                                uint32_t* offset, const char* value) {
    if (!value) return;
    while (*value) {
        network_append_char(text, capacity, offset, *value);
        value++;
    }
}

static void network_append_hex(char* text, uint32_t capacity,
                               uint32_t* offset, uint32_t value,
                               uint32_t digits) {
    static const char hex[] = "0123456789ABCDEF";

    while (digits > 0U) {
        uint32_t shift = (digits - 1U) * 4U;
        network_append_char(text, capacity, offset,
                            hex[(value >> shift) & 0x0FU]);
        digits--;
    }
}

static void network_set_text(char* destination, uint32_t capacity,
                             const char* source) {
    uint32_t offset = 0;

    if (!destination || !capacity) return;
    destination[0] = '\0';
    network_append_text(destination, capacity, &offset, source);
}

static network_adapter_model_t network_detect_model(
    const pci_device_t* pci) {
    if (pci->vendor_id == NETWORK_VENDOR_INTEL &&
        pci->device_id == NETWORK_DEVICE_E1000) {
        return NETWORK_ADAPTER_E1000;
    }
    if (pci->vendor_id == NETWORK_VENDOR_REALTEK &&
        pci->device_id == NETWORK_DEVICE_RTL8139) {
        return NETWORK_ADAPTER_RTL8139;
    }
    return NETWORK_ADAPTER_UNKNOWN;
}

static void network_copy_bars(network_interface_info_t* entry,
                              const pci_device_t* pci) {
    entry->bars[0] = pci->bar0;
    entry->bars[1] = pci->bar1;
    entry->bars[2] = pci->bar2;
    entry->bars[3] = pci->bar3;
    entry->bars[4] = pci->bar4;
    entry->bars[5] = pci->bar5;
}

static int network_is_e1000_entry(const network_interface_info_t* entry,
                                  const e1000_status_t* status) {
    return entry && status && status->detected &&
           entry->model == NETWORK_ADAPTER_E1000 &&
           entry->bus == status->bus && entry->device == status->device &&
           entry->function == status->function;
}

static void network_apply_e1000_status(network_interface_info_t* entry,
                                       const e1000_status_t* status) {
    if (!network_is_e1000_entry(entry, status)) return;
    for (uint32_t index = 0; index < NETWORK_MAC_ADDRESS_SIZE; index++) {
        entry->mac_address[index] = status->mac_address[index];
    }
    entry->rx_packets = status->rx_packets;
    entry->tx_packets = status->tx_packets;
    entry->rx_errors = status->rx_errors;
    entry->tx_errors = status->tx_errors;
    entry->rx_dropped = status->rx_dropped;
    entry->driver_error = status->last_error;
    if (status->initialized) {
        entry->state = NETWORK_INTERFACE_ACTIVE;
        entry->link = status->link_up ? NETWORK_LINK_UP : NETWORK_LINK_DOWN;
    } else if (status->last_error != ERR_NOT_FOUND) {
        entry->state = NETWORK_INTERFACE_DRIVER_ERROR;
        entry->link = NETWORK_LINK_UNKNOWN;
    }
}

static void network_copy_interface(network_interface_info_t* entry,
                                   const pci_device_t* pci,
                                   const e1000_status_t* e1000_status) {
    kmemset(entry, 0, sizeof(*entry));
    entry->model = network_detect_model(pci);
    entry->state = entry->model == NETWORK_ADAPTER_UNKNOWN ?
                   NETWORK_INTERFACE_UNSUPPORTED :
                   NETWORK_INTERFACE_DRIVER_MISSING;
    entry->link = NETWORK_LINK_UNKNOWN;
    entry->vendor_id = pci->vendor_id;
    entry->device_id = pci->device_id;
    entry->class_code = pci->class;
    entry->subclass_code = pci->subclass;
    entry->prog_if = pci->prog_if;
    entry->revision = pci->revision;
    entry->bus = pci->bus;
    entry->device = pci->device;
    entry->function = pci->function;
    entry->irq = pci->irq;
    network_copy_bars(entry, pci);
    entry->driver_error = ERR_UNAVAILABLE;
    network_apply_e1000_status(entry, e1000_status);
}

static int network_build_snapshot(void) {
    uint8_t pci_count = 0;
    e1000_status_t e1000_status;
    int pci_result;
    int result = OK;

    kmemset(network_interfaces, 0, sizeof(network_interfaces));
    kmemset(&network_status, 0, sizeof(network_status));
    network_status.initialized = network_manager_initialized ? 1U : 0U;
    network_status.last_error = ERR_NOT_FOUND;
    if (e1000_get_status(&e1000_status) != OK) {
        LOG_ERROR("NET", "Falha ao consultar estado do E1000");
        return ERR_STATE;
    }
    pci_result = pci_get_device_count(&pci_count);
    if (pci_result != OK && pci_result != ERR_OVERFLOW) {
        LOG_ERROR("NET", "Falha ao consultar inventario PCI");
        return pci_result;
    }
    if (pci_result == ERR_OVERFLOW) {
        network_status.partial = 1;
        result = ERR_OVERFLOW;
    }

    for (uint8_t index = 0; index < pci_count; index++) {
        pci_device_t pci;
        int entry_result = pci_get_device_at(index, &pci);

        if (entry_result != OK) {
            LOG_ERROR("NET", "Falha ao consultar controlador PCI");
            return entry_result;
        }
        if (pci.class != NETWORK_PCI_CLASS) continue;
        if (network_status.interface_count >=
            NETWORK_MANAGER_MAX_INTERFACES) {
            network_status.partial = 1;
            network_status.last_error = ERR_OVERFLOW;
            LOG_WARN("NET", "Limite do inventario de rede atingido");
            result = ERR_OVERFLOW;
            break;
        }
        network_copy_interface(
            &network_interfaces[network_status.interface_count], &pci,
            &e1000_status);
        if (network_interfaces[network_status.interface_count].model !=
            NETWORK_ADAPTER_UNKNOWN) {
            network_status.recognized_count++;
        }
        if (network_interfaces[network_status.interface_count].state ==
            NETWORK_INTERFACE_ACTIVE) {
            network_status.active_count++;
        }
        network_status.interface_count++;
    }
    if (network_status.active_count) {
        network_status.packet_io_available = 1;
    }
    if (network_status.partial) {
        network_status.last_error = ERR_OVERFLOW;
    } else if (network_status.active_count) {
        network_status.last_error = OK;
    } else if (network_status.interface_count) {
        network_status.last_error = ERR_UNAVAILABLE;
        for (uint32_t index = 0; index < network_status.interface_count;
             index++) {
            if (network_interfaces[index].state ==
                NETWORK_INTERFACE_DRIVER_ERROR) {
                network_status.last_error =
                    network_interfaces[index].driver_error;
                break;
            }
        }
    }
    return result;
}

static void network_sync_recovery(int result) {
    const recovery_component_t* current =
        recovery_get(RECOVERY_COMPONENT_NETWORK);
    recovery_state_t state;
    int error;
    const char* message;

    if (result != OK && result != ERR_OVERFLOW) {
        state = RECOVERY_STATE_DISABLED;
        error = result;
        message = "Inventario de rede indisponivel";
    } else if (network_status.partial) {
        state = RECOVERY_STATE_DEGRADED;
        error = ERR_OVERFLOW;
        message = "Inventario de rede parcial";
    } else if (network_status.active_count) {
        state = RECOVERY_STATE_READY;
        error = OK;
        message = "Componente operacional";
    } else if (network_status.interface_count) {
        state = RECOVERY_STATE_DEGRADED;
        error = network_status.last_error;
        message = error == ERR_UNAVAILABLE ?
                  "Controlador detectado; driver nao implementado" :
                  "Controlador detectado; driver com falha";
    } else {
        state = RECOVERY_STATE_DISABLED;
        error = ERR_NOT_FOUND;
        message = "Nenhum controlador de rede detectado";
    }

    if (!current) {
        LOG_ERROR("NET", "Componente Network ausente do recovery");
        return;
    }
    if (current->state == state && current->last_error == error) return;
    if (state == RECOVERY_STATE_READY) {
        if (recovery_mark_ready(RECOVERY_COMPONENT_NETWORK) != OK) {
            LOG_ERROR("NET", "Falha ao marcar Network pronto");
        }
    } else if (state == RECOVERY_STATE_DEGRADED) {
        if (recovery_mark_degraded(RECOVERY_COMPONENT_NETWORK,
                                   error, message) != OK) {
            LOG_ERROR("NET", "Falha ao marcar Network degradado");
        }
    } else if (recovery_mark_disabled(RECOVERY_COMPONENT_NETWORK,
                                      error, message) != OK) {
        LOG_ERROR("NET", "Falha ao desabilitar Network");
    }
}

static char network_ascii_upper(char value) {
    if (value >= 'a' && value <= 'z') return (char)(value - ('a' - 'A'));
    return value;
}

static int network_id_matches(const char* expected, const char* requested) {
    if (!expected || !requested) return 0;
    while (*expected && *requested) {
        if (*expected == ':' && *requested == '-') {
            expected++;
            requested++;
            continue;
        }
        if (network_ascii_upper(*expected) !=
            network_ascii_upper(*requested)) {
            return 0;
        }
        expected++;
        requested++;
    }
    return *expected == '\0' && *requested == '\0';
}

int network_manager_init(void) {
    int result;

    LOG_INFO("NET", "Inicializando inventario de rede");
    network_manager_initialized = 1;
    result = network_build_snapshot();
    if (result != OK && result != ERR_OVERFLOW) {
        network_manager_initialized = 0;
        network_status.initialized = 0;
        network_sync_recovery(result);
        LOG_ERROR("NET", "Falha ao inicializar inventario de rede");
        return result;
    }
    network_sync_recovery(result);
    if (result == ERR_OVERFLOW) {
        LOG_WARN("NET", "Inventario de rede inicializado parcialmente");
    }
    LOG_INFO("NET", "Inventario de rede inicializado com sucesso");
    return result;
}

int network_manager_refresh(void) {
    int result;

    if (!network_manager_initialized) {
        LOG_ERROR("NET", "Atualizacao antes da inicializacao de rede");
        return ERR_STATE;
    }
    result = network_build_snapshot();
    network_sync_recovery(result);
    if (result != OK && result != ERR_OVERFLOW) {
        LOG_ERROR("NET", "Falha ao atualizar inventario de rede");
        return result;
    }
    if (result == ERR_OVERFLOW) {
        LOG_WARN("NET", "Inventario de rede atualizado parcialmente");
    } else {
        LOG_INFO("NET", "Inventario de rede atualizado com sucesso");
    }
    return result;
}

int network_manager_get_status(network_manager_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("NET", "Destino nulo ao consultar estado de rede");
        return ERR_NULL;
    }
    if (!network_manager_initialized) {
        LOG_ERROR("NET", "Consulta antes da inicializacao de rede");
        return ERR_STATE;
    }
    *out_status = network_status;
    return OK;
}

int network_manager_get_count(uint32_t* out_count) {
    if (!out_count) {
        LOG_ERROR("NET", "Destino nulo ao consultar interfaces");
        return ERR_NULL;
    }
    if (!network_manager_initialized) {
        LOG_ERROR("NET", "Contagem antes da inicializacao de rede");
        return ERR_STATE;
    }
    *out_count = network_status.interface_count;
    return OK;
}

int network_manager_get_interface(uint32_t index,
                                  network_interface_info_t* out_info) {
    e1000_status_t e1000_status;

    if (!out_info) {
        LOG_ERROR("NET", "Destino nulo ao consultar interface");
        return ERR_NULL;
    }
    if (!network_manager_initialized) {
        LOG_ERROR("NET", "Interface consultada antes da inicializacao");
        return ERR_STATE;
    }
    if (index >= network_status.interface_count) {
        LOG_ERROR("NET", "Indice de interface de rede invalido");
        return ERR_INVALID;
    }
    *out_info = network_interfaces[index];
    if (e1000_get_status(&e1000_status) != OK) {
        LOG_ERROR("NET", "Falha ao atualizar estado do E1000");
        return ERR_STATE;
    }
    network_apply_e1000_status(out_info, &e1000_status);
    return OK;
}

int network_manager_format_text(const network_interface_info_t* info,
                                network_interface_text_t* out_text) {
    uint32_t offset = 0;

    if (!info || !out_text) {
        LOG_ERROR("NET", "Argumento nulo ao formatar interface");
        return ERR_NULL;
    }
    kmemset(out_text, 0, sizeof(*out_text));
    network_append_text(out_text->id, NETWORK_INTERFACE_ID_SIZE,
                        &offset, "net-pci-");
    network_append_hex(out_text->id, NETWORK_INTERFACE_ID_SIZE,
                       &offset, info->bus, NETWORK_HEX_BYTE_DIGITS);
    network_append_char(out_text->id, NETWORK_INTERFACE_ID_SIZE,
                        &offset, ':');
    network_append_hex(out_text->id, NETWORK_INTERFACE_ID_SIZE,
                       &offset, info->device, NETWORK_HEX_BYTE_DIGITS);
    network_append_char(out_text->id, NETWORK_INTERFACE_ID_SIZE,
                        &offset, '.');
    network_append_hex(out_text->id, NETWORK_INTERFACE_ID_SIZE,
                       &offset, info->function, 1U);

    if (info->model == NETWORK_ADAPTER_E1000) {
        network_set_text(out_text->name, NETWORK_INTERFACE_NAME_SIZE,
                         "Intel 82540EM (E1000)");
        network_set_text(out_text->driver, NETWORK_DRIVER_NAME_SIZE,
                         info->state == NETWORK_INTERFACE_ACTIVE ?
                         "e1000 (ativo)" :
                         info->state == NETWORK_INTERFACE_DRIVER_ERROR ?
                         "e1000 (erro)" : "e1000 (pendente)");
    } else if (info->model == NETWORK_ADAPTER_RTL8139) {
        network_set_text(out_text->name, NETWORK_INTERFACE_NAME_SIZE,
                         "Realtek RTL8139");
        network_set_text(out_text->driver, NETWORK_DRIVER_NAME_SIZE,
                         "rtl8139 (planejado)");
    } else {
        network_set_text(out_text->name, NETWORK_INTERFACE_NAME_SIZE,
                         "Controlador de rede PCI");
        network_set_text(out_text->driver, NETWORK_DRIVER_NAME_SIZE,
                         "nao suportado");
    }
    return OK;
}

int network_manager_find(const char* id, network_interface_info_t* out_info) {
    network_interface_text_t text;

    if (!id || !out_info) {
        LOG_ERROR("NET", "Argumento nulo ao buscar interface");
        return ERR_NULL;
    }
    if (!network_manager_initialized) {
        LOG_ERROR("NET", "Busca antes da inicializacao de rede");
        return ERR_STATE;
    }
    for (uint32_t index = 0; index < network_status.interface_count; index++) {
        int result = network_manager_format_text(&network_interfaces[index],
                                                 &text);

        if (result != OK) {
            LOG_ERROR("NET", "Falha ao formatar interface durante busca");
            return result;
        }
        if (network_id_matches(text.id, id)) {
            return network_manager_get_interface(index, out_info);
        }
    }
    LOG_WARN("NET", "Interface de rede solicitada nao encontrada");
    return ERR_NOT_FOUND;
}

int network_manager_send_diagnostic(const char* id) {
    network_interface_info_t info;
    uint8_t frame[NETWORK_DIAGNOSTIC_FRAME_SIZE];
    static const char payload[] = "ZEPHYROS-S2.2-E1000";
    int result;

    if (!id) {
        LOG_ERROR("NET", "ID nulo para teste de rede");
        return ERR_NULL;
    }
    result = network_manager_find(id, &info);
    if (result != OK) {
        LOG_ERROR("NET", "Interface invalida para teste de rede");
        return result;
    }
    if (info.model != NETWORK_ADAPTER_E1000 ||
        info.state != NETWORK_INTERFACE_ACTIVE) {
        LOG_WARN("NET", "Teste solicitado sem E1000 ativo");
        return ERR_UNAVAILABLE;
    }
    kmemset(frame, 0, sizeof(frame));
    for (uint32_t index = 0; index < NETWORK_MAC_ADDRESS_SIZE; index++) {
        frame[NETWORK_ETHERNET_DESTINATION_OFFSET + index] =
            NETWORK_ETHERNET_BROADCAST_OCTET;
        frame[NETWORK_ETHERNET_SOURCE_OFFSET + index] =
            info.mac_address[index];
    }
    frame[NETWORK_ETHERNET_TYPE_OFFSET] = NETWORK_DIAGNOSTIC_ETHERTYPE_HIGH;
    frame[NETWORK_ETHERNET_TYPE_OFFSET + 1U] =
        NETWORK_DIAGNOSTIC_ETHERTYPE_LOW;
    for (uint32_t index = 0; index < sizeof(payload) - 1U; index++) {
        frame[NETWORK_ETHERNET_PAYLOAD_OFFSET + index] =
            (uint8_t)payload[index];
    }
    result = e1000_send_frame(frame, sizeof(frame));
    if (result != OK) {
        LOG_ERROR("NET", "Falha no teste de transmissao E1000");
        return result;
    }
    return OK;
}

const char* network_manager_model_name(network_adapter_model_t model) {
    if (model == NETWORK_ADAPTER_E1000) return "E1000";
    if (model == NETWORK_ADAPTER_RTL8139) return "RTL8139";
    return "DESCONHECIDO";
}

const char* network_manager_interface_state_name(
    network_interface_state_t state) {
    if (state == NETWORK_INTERFACE_DRIVER_MISSING) return "DRIVER AUSENTE";
    if (state == NETWORK_INTERFACE_UNSUPPORTED) return "NAO SUPORTADA";
    if (state == NETWORK_INTERFACE_ACTIVE) return "ATIVA";
    if (state == NETWORK_INTERFACE_DRIVER_ERROR) return "ERRO NO DRIVER";
    return "DESCONHECIDO";
}

const char* network_manager_link_state_name(network_link_state_t state) {
    if (state == NETWORK_LINK_DOWN) return "DOWN";
    if (state == NETWORK_LINK_UP) return "UP";
    return "DESCONHECIDO";
}
