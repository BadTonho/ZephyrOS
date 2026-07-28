#include "core/network_manager.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/string.h"
#include "drivers/pci.h"

#define NETWORK_PCI_CLASS 0x02U
#define NETWORK_VENDOR_INTEL 0x8086U
#define NETWORK_DEVICE_E1000 0x100EU
#define NETWORK_VENDOR_REALTEK 0x10ECU
#define NETWORK_DEVICE_RTL8139 0x8139U
#define NETWORK_HEX_BYTE_DIGITS 2U

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

static void network_copy_interface(network_interface_info_t* entry,
                                   const pci_device_t* pci) {
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
}

static int network_build_snapshot(void) {
    uint8_t pci_count = 0;
    int pci_result;
    int result = OK;

    kmemset(network_interfaces, 0, sizeof(network_interfaces));
    kmemset(&network_status, 0, sizeof(network_status));
    network_status.initialized = network_manager_initialized ? 1U : 0U;
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
            LOG_WARN("NET", "Limite do inventario de rede atingido");
            return ERR_OVERFLOW;
        }
        network_copy_interface(
            &network_interfaces[network_status.interface_count], &pci);
        if (network_interfaces[network_status.interface_count].model !=
            NETWORK_ADAPTER_UNKNOWN) {
            network_status.recognized_count++;
        }
        network_status.interface_count++;
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
        error = ERR_UNAVAILABLE;
        message = "Controlador detectado; driver nao implementado";
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
                         "e1000 (planejado)");
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
            *out_info = network_interfaces[index];
            return OK;
        }
    }
    LOG_WARN("NET", "Interface de rede solicitada nao encontrada");
    return ERR_NOT_FOUND;
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
    return "DESCONHECIDO";
}

const char* network_manager_link_state_name(network_link_state_t state) {
    if (state == NETWORK_LINK_DOWN) return "DOWN";
    if (state == NETWORK_LINK_UP) return "UP";
    return "DESCONHECIDO";
}
