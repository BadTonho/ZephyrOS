#include "core/wifi_manager.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "drivers/rtl8811cu.h"
#include "drivers/pci.h"

#define WIFI_VENDOR_INTEL 0x8086U
#define WIFI_DEVICE_E1000 0x100EU
#define WIFI_VENDOR_REALTEK 0x10ECU
#define WIFI_DEVICE_RTL8139 0x8139U
#define WIFI_HEX_DIGITS_BYTE 2U
#define WIFI_PCI_DEVICE_MAX 31U
#define WIFI_PCI_FUNCTION_MAX 7U

static const char WIFI_CANDIDATE_NAME[] = "PCI network candidate";
static const char WIFI_USB_CANDIDATE_NAME[] = "Realtek RTL8811CU USB";

static wifi_interface_info_t wifi_interfaces[WIFI_MANAGER_MAX_INTERFACES];
static wifi_manager_status_t wifi_status;
static int wifi_manager_initialized;

static void wifi_append_char(char* text, uint32_t capacity,
                             uint32_t* offset, char value) {
    if (!text || !offset || capacity == 0U || *offset + 1U >= capacity) {
        return;
    }
    text[*offset] = value;
    (*offset)++;
    text[*offset] = '\0';
}

static void wifi_append_text(char* text, uint32_t capacity,
                             uint32_t* offset, const char* value) {
    if (!value) return;
    while (*value) {
        wifi_append_char(text, capacity, offset, *value);
        value++;
    }
}

static void wifi_append_hex(char* text, uint32_t capacity,
                            uint32_t* offset, uint32_t value,
                            uint32_t digits) {
    static const char hex[] = "0123456789ABCDEF";

    while (digits > 0U) {
        uint32_t shift = (digits - 1U) * 4U;
        wifi_append_char(text, capacity, offset,
                         hex[(value >> shift) & 0x0FU]);
        digits--;
    }
}

static void wifi_append_decimal(char* text, uint32_t capacity,
                                uint32_t* offset, uint32_t value) {
    char digits[10];
    uint32_t count = 0U;

    if (value == 0U) {
        wifi_append_char(text, capacity, offset, '0');
        return;
    }
    while (value > 0U && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (count > 0U) {
        wifi_append_char(text, capacity, offset, digits[--count]);
    }
}

static void wifi_format_id(const pci_device_t* pci, char* output,
                           uint32_t capacity) {
    uint32_t offset = 0U;

    if (!output || capacity == 0U) return;
    output[0] = '\0';
    wifi_append_char(output, capacity, &offset, 'w');
    wifi_append_char(output, capacity, &offset, 'i');
    wifi_append_char(output, capacity, &offset, 'f');
    wifi_append_char(output, capacity, &offset, 'i');
    wifi_append_char(output, capacity, &offset, '-');
    wifi_append_hex(output, capacity, &offset, pci->bus,
                    WIFI_HEX_DIGITS_BYTE);
    wifi_append_char(output, capacity, &offset, ':');
    wifi_append_hex(output, capacity, &offset, pci->device,
                    WIFI_HEX_DIGITS_BYTE);
    wifi_append_char(output, capacity, &offset, '.');
    wifi_append_decimal(output, capacity, &offset, pci->function);
}

static void wifi_format_usb_id(const usb_device_info_t* device, char* output,
                               uint32_t capacity) {
    uint32_t offset = 0U;

    if (!device || !output || capacity == 0U) return;
    output[0] = '\0';
    wifi_append_text(output, capacity, &offset, "wifi-usb-");
    wifi_append_hex(output, capacity, &offset, device->controller_bus,
                    WIFI_HEX_DIGITS_BYTE);
    wifi_append_char(output, capacity, &offset, ':');
    wifi_append_hex(output, capacity, &offset, device->controller_device,
                    WIFI_HEX_DIGITS_BYTE);
    wifi_append_char(output, capacity, &offset, '.');
    wifi_append_hex(output, capacity, &offset, device->controller_function,
                    1U);
    wifi_append_text(output, capacity, &offset, "-p");
    wifi_append_decimal(output, capacity, &offset, device->port_number);
}

static void wifi_copy_text(char* destination, uint32_t capacity,
                           const char* source) {
    uint32_t offset = 0U;

    if (!destination || capacity == 0U) return;
    destination[0] = '\0';
    wifi_append_text(destination, capacity, &offset, source);
}

static int wifi_is_supported_ethernet(const pci_device_t* pci) {
    if (!pci) return 0;
    return (pci->vendor_id == WIFI_VENDOR_INTEL &&
            pci->device_id == WIFI_DEVICE_E1000) ||
           (pci->vendor_id == WIFI_VENDOR_REALTEK &&
            pci->device_id == WIFI_DEVICE_RTL8139);
}

static int wifi_info_is_supported_ethernet(
    const wifi_interface_info_t* info) {
    if (!info) return 0;
    if (info->transport != WIFI_TRANSPORT_PCI) return 0;
    return (info->vendor_id == WIFI_VENDOR_INTEL &&
            info->device_id == WIFI_DEVICE_E1000) ||
           (info->vendor_id == WIFI_VENDOR_REALTEK &&
            info->device_id == WIFI_DEVICE_RTL8139);
}

static int wifi_validate_pci_metadata(const pci_device_t* pci) {
    if (!pci) {
        LOG_ERROR("WIFI", "Controlador PCI nulo no inventario");
        return ERR_NULL;
    }
    if (!pci->present) {
        LOG_ERROR("WIFI", "Controlador PCI ausente no inventario");
        return ERR_NOT_FOUND;
    }
    if (pci->device > WIFI_PCI_DEVICE_MAX ||
        pci->function > WIFI_PCI_FUNCTION_MAX) {
        LOG_ERROR("WIFI", "Localizacao PCI invalida no inventario");
        return ERR_INVALID;
    }
    return OK;
}

static int wifi_id_matches(const char* expected, const char* requested) {
    if (!expected || !requested) return 0;
    while (*expected && *requested) {
        char left = *expected;
        char right = *requested;

        if (left >= 'a' && left <= 'z') left = (char)(left - 'a' + 'A');
        if (right >= 'a' && right <= 'z') right = (char)(right - 'a' + 'A');
        if (left != right) return 0;
        expected++;
        requested++;
    }
    return *expected == '\0' && *requested == '\0';
}

static int wifi_copy_pci(const pci_device_t* pci,
                         wifi_interface_info_t* out_info) {
    if (!pci || !out_info) {
        LOG_ERROR("WIFI", "Argumento nulo ao copiar controlador PCI");
        return ERR_NULL;
    }
    kmemset(out_info, 0, sizeof(*out_info));
    out_info->transport = WIFI_TRANSPORT_PCI;
    wifi_format_id(pci, out_info->id, sizeof(out_info->id));
    out_info->vendor_id = pci->vendor_id;
    out_info->device_id = pci->device_id;
    out_info->class_code = pci->class;
    out_info->subclass_code = pci->subclass;
    out_info->prog_if = pci->prog_if;
    out_info->revision = pci->revision;
    out_info->bus = pci->bus;
    out_info->device = pci->device;
    out_info->function = pci->function;
    out_info->irq = pci->irq;
    out_info->bars[0] = pci->bar0;
    out_info->bars[1] = pci->bar1;
    out_info->bars[2] = pci->bar2;
    out_info->bars[3] = pci->bar3;
    out_info->bars[4] = pci->bar4;
    out_info->bars[5] = pci->bar5;
    kmemcpy(out_info->name, WIFI_CANDIDATE_NAME,
            sizeof(WIFI_CANDIDATE_NAME));
    out_info->state = WIFI_INTERFACE_UNSUPPORTED;
    out_info->driver_error = ERR_UNAVAILABLE;
    return OK;
}

static int wifi_copy_usb(const usb_device_info_t* device,
                         wifi_interface_info_t* out_info) {
    rtl8811cu_probe_info_t probe;
    rtl8811cu_status_t driver_status;
    int result;

    if (!device || !out_info) {
        LOG_ERROR("WIFI", "Argumento nulo ao copiar dispositivo USB");
        return ERR_NULL;
    }
    kmemset(out_info, 0, sizeof(*out_info));
    out_info->transport = WIFI_TRANSPORT_USB;
    wifi_format_usb_id(device, out_info->id, sizeof(out_info->id));
    wifi_copy_text(out_info->name, sizeof(out_info->name),
                   WIFI_USB_CANDIDATE_NAME);
    out_info->vendor_id = device->vendor_id;
    out_info->device_id = device->product_id;
    out_info->class_code = device->interface_class;
    out_info->subclass_code = device->interface_subclass;
    out_info->prog_if = device->interface_protocol;
    out_info->revision = (uint8_t)(device->device_revision & 0xFFU);
    out_info->bus = device->controller_bus;
    out_info->device = device->controller_device;
    out_info->function = device->controller_function;
    out_info->irq = WIFI_PCI_IRQ_UNKNOWN;
    out_info->usb_port = device->port_number;
    out_info->usb_address = device->usb_address;
    out_info->usb_revision = device->device_revision;
    out_info->usb_endpoint_count = device->endpoint_count;
    wifi_copy_text(out_info->usb_device_id, sizeof(out_info->usb_device_id),
                   device->id);
    result = rtl8811cu_probe(device, &probe);
    if (result != OK) {
        out_info->state = probe.revision_supported ?
                          WIFI_INTERFACE_ERROR : WIFI_INTERFACE_UNSUPPORTED;
        out_info->driver_error = result;
        return OK;
    }
    result = rtl8811cu_get_status(&driver_status);
    if (result == OK && driver_status.state == RTL8811CU_STATE_READY) {
        out_info->state = WIFI_INTERFACE_READY;
        out_info->driver_error = OK;
    } else if (result == OK && driver_status.state == RTL8811CU_STATE_ERROR) {
        out_info->state = WIFI_INTERFACE_ERROR;
        out_info->driver_error = driver_status.last_error;
    } else {
        out_info->state = WIFI_INTERFACE_UNSUPPORTED;
        out_info->driver_error = ERR_UNAVAILABLE;
    }
    return OK;
}

static int wifi_collect_interfaces(void) {
    uint8_t pci_count = 0U;
    int pci_result = pci_get_device_count(&pci_count);

    if (pci_result != OK && pci_result != ERR_OVERFLOW) {
        LOG_ERROR("WIFI", "Falha ao consultar inventario PCI");
        return pci_result;
    }
    for (uint8_t index = 0U; index < pci_count; index++) {
        pci_device_t pci;
        int result = pci_get_device_at(index, &pci);

        if (result != OK) {
            LOG_ERROR("WIFI", "Falha ao consultar entrada PCI");
            return result;
        }
        result = wifi_validate_pci_metadata(&pci);
        if (result != OK) return result;
        if (pci.class != WIFI_PCI_CLASS_NETWORK ||
            wifi_is_supported_ethernet(&pci)) {
            continue;
        }
        if (wifi_status.interface_count >= WIFI_MANAGER_MAX_INTERFACES) {
            wifi_status.partial = 1U;
            wifi_status.last_error = ERR_OVERFLOW;
            LOG_WARN("WIFI", "Limite de candidatos Wi-Fi atingido");
            return ERR_OVERFLOW;
        }
        result = wifi_copy_pci(
            &pci, &wifi_interfaces[wifi_status.interface_count]);
        if (result != OK) return result;
        wifi_status.interface_count++;
        wifi_status.candidate_count++;
        wifi_status.unsupported_count++;
    }
    if (pci_result == ERR_OVERFLOW) {
        wifi_status.partial = 1U;
        wifi_status.last_error = ERR_OVERFLOW;
        return ERR_OVERFLOW;
    }
    return OK;
}

static int wifi_collect_usb_interfaces(void) {
    uint32_t device_count = 0U;
    int result = usb_manager_get_device_count(&device_count);

    if (result == ERR_STATE) {
        LOG_WARN("WIFI", "Inventario USB ainda nao inicializado");
        return OK;
    }
    if (result != OK) {
        LOG_ERROR("WIFI", "Falha ao consultar inventario USB de Wi-Fi");
        return result;
    }
    for (uint32_t index = 0U; index < device_count; index++) {
        usb_device_info_t device;
        wifi_interface_info_t* interface_info;

        result = usb_manager_get_device(index, &device);
        if (result != OK) {
            LOG_ERROR("WIFI", "Falha ao consultar dispositivo USB de Wi-Fi");
            return result;
        }
        if (device.vendor_id != RTL8811CU_VENDOR_ID ||
            device.product_id != RTL8811CU_PRODUCT_ID) {
            continue;
        }
        if (wifi_status.interface_count >= WIFI_MANAGER_MAX_INTERFACES) {
            wifi_status.partial = 1U;
            wifi_status.last_error = ERR_OVERFLOW;
            LOG_WARN("WIFI", "Limite de interfaces Wi-Fi atingido");
            return ERR_OVERFLOW;
        }
        interface_info = &wifi_interfaces[wifi_status.interface_count];
        result = wifi_copy_usb(&device, interface_info);
        if (result != OK) return result;
        wifi_status.interface_count++;
        wifi_status.candidate_count++;
        if (interface_info->state == WIFI_INTERFACE_READY) {
            wifi_status.ready_count++;
        } else if (interface_info->state == WIFI_INTERFACE_UNSUPPORTED) {
            wifi_status.unsupported_count++;
        } else if (interface_info->state == WIFI_INTERFACE_ERROR) {
            wifi_status.error_count++;
            wifi_status.last_error = interface_info->driver_error;
        }
    }
    return OK;
}

int wifi_manager_init(void) {
    int result;

    LOG_INFO("WIFI", "Inicializando inventario PCI e USB de Wi-Fi");
    wifi_manager_initialized = 1;
    result = wifi_manager_refresh();
    if (result != OK && result != ERR_OVERFLOW) {
        wifi_manager_initialized = 0;
        LOG_ERROR("WIFI", "Falha ao inicializar inventario PCI e USB de Wi-Fi");
        return result;
    }
    if (result == ERR_OVERFLOW) {
        LOG_WARN("WIFI", "Inventario PCI e USB de Wi-Fi parcial");
    }
    LOG_INFO("WIFI", "Inventario PCI e USB de Wi-Fi inicializado");
    return result;
}

int wifi_manager_refresh(void) {
    int result;

    if (!wifi_manager_initialized) {
        LOG_ERROR("WIFI", "Atualizacao antes da inicializacao");
        return ERR_STATE;
    }
    kmemset(wifi_interfaces, 0, sizeof(wifi_interfaces));
    kmemset(&wifi_status, 0, sizeof(wifi_status));
    wifi_status.initialized = 1U;
    result = wifi_collect_interfaces();
    if (result != OK && result != ERR_OVERFLOW) {
        wifi_status.last_error = result;
        LOG_ERROR("WIFI", "Falha ao atualizar inventario PCI de Wi-Fi");
        return result;
    }
    if (result == ERR_OVERFLOW) {
        wifi_status.partial = 1U;
    }
    result = wifi_collect_usb_interfaces();
    if (result != OK && result != ERR_OVERFLOW) {
        wifi_status.last_error = result;
        LOG_ERROR("WIFI", "Falha ao atualizar inventario USB de Wi-Fi");
        return result;
    }
    if (result == ERR_OVERFLOW) {
        wifi_status.partial = 1U;
    }
    if (wifi_status.partial) {
        wifi_status.last_error = ERR_OVERFLOW;
        LOG_WARN("WIFI", "Inventario PCI e USB de Wi-Fi atualizado parcialmente");
    } else if (!wifi_status.candidate_count) {
        wifi_status.last_error = ERR_NOT_FOUND;
        LOG_WARN("WIFI", "Nenhum candidato PCI ou USB de Wi-Fi encontrado");
    } else if (wifi_status.error_count) {
        LOG_WARN("WIFI", "Inventario Wi-Fi atualizado com falha isolada");
    } else if (wifi_status.ready_count) {
        wifi_status.last_error = OK;
        LOG_INFO("WIFI", "Inventario Wi-Fi atualizado com backend pronto");
    } else {
        wifi_status.last_error = ERR_UNAVAILABLE;
        LOG_INFO("WIFI", "Inventario PCI e USB de Wi-Fi atualizado");
    }
    return wifi_status.partial ? ERR_OVERFLOW : OK;
}

int wifi_manager_get_status(wifi_manager_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("WIFI", "Destino nulo ao consultar estado");
        return ERR_NULL;
    }
    if (!wifi_manager_initialized) {
        LOG_ERROR("WIFI", "Consulta de estado antes da inicializacao");
        return ERR_STATE;
    }
    *out_status = wifi_status;
    return OK;
}

int wifi_manager_get_count(uint32_t* out_count) {
    if (!out_count) {
        LOG_ERROR("WIFI", "Destino nulo ao consultar contagem");
        return ERR_NULL;
    }
    if (!wifi_manager_initialized) {
        LOG_ERROR("WIFI", "Consulta de contagem antes da inicializacao");
        return ERR_STATE;
    }
    *out_count = wifi_status.interface_count;
    return OK;
}

int wifi_manager_get_interface(wifi_interface_info_t* out_info,
                               uint32_t index) {
    if (!out_info) {
        LOG_ERROR("WIFI", "Destino nulo ao consultar interface");
        return ERR_NULL;
    }
    if (!wifi_manager_initialized) {
        LOG_ERROR("WIFI", "Consulta de interface antes da inicializacao");
        return ERR_STATE;
    }
    if (index >= wifi_status.interface_count) {
        LOG_ERROR("WIFI", "Indice de interface Wi-Fi invalido");
        return ERR_INVALID;
    }
    *out_info = wifi_interfaces[index];
    return OK;
}

int wifi_manager_find(const char* id, wifi_interface_info_t* out_info) {
    if (!id || !out_info) {
        LOG_ERROR("WIFI", "Argumento nulo ao buscar interface");
        return ERR_NULL;
    }
    if (!wifi_manager_initialized) {
        LOG_ERROR("WIFI", "Busca de interface antes da inicializacao");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < wifi_status.interface_count; index++) {
        if (wifi_id_matches(wifi_interfaces[index].id, id)) {
            *out_info = wifi_interfaces[index];
            return OK;
        }
    }
    LOG_WARN("WIFI", "Interface Wi-Fi solicitada nao encontrada");
    return ERR_NOT_FOUND;
}

int wifi_manager_validate_state(void) {
    uint32_t unsupported_count = 0U;
    uint32_t ready_count = 0U;
    uint32_t error_count = 0U;

    if (!wifi_manager_initialized || !wifi_status.initialized ||
        wifi_status.interface_count > WIFI_MANAGER_MAX_INTERFACES) {
        LOG_ERROR("WIFI", "Estado global do inventario invalido");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < wifi_status.interface_count; index++) {
        wifi_interface_info_t* info = &wifi_interfaces[index];
        rtl8811cu_status_t driver_status;

        if (!info->id[0] || info->transport > WIFI_TRANSPORT_USB) {
            LOG_ERROR("WIFI", "Entrada de inventario Wi-Fi invalida");
            return ERR_STATE;
        }
        if (info->transport == WIFI_TRANSPORT_PCI &&
            (info->class_code != WIFI_PCI_CLASS_NETWORK ||
             wifi_info_is_supported_ethernet(info))) {
            LOG_ERROR("WIFI", "Entrada PCI invalida no inventario Wi-Fi");
            return ERR_STATE;
        }
        if (info->transport == WIFI_TRANSPORT_USB &&
            (info->vendor_id != RTL8811CU_VENDOR_ID ||
             info->device_id != RTL8811CU_PRODUCT_ID ||
             !info->usb_device_id[0])) {
            LOG_ERROR("WIFI", "Entrada USB invalida no inventario Wi-Fi");
            return ERR_STATE;
        }
        if (info->state == WIFI_INTERFACE_UNSUPPORTED) {
            unsupported_count++;
            if (info->driver_error != ERR_UNAVAILABLE) {
                LOG_ERROR("WIFI", "Erro de driver incoerente em candidato");
                return ERR_STATE;
            }
        } else if (info->state == WIFI_INTERFACE_READY) {
            ready_count++;
        } else if (info->state == WIFI_INTERFACE_ERROR) {
            error_count++;
            if (info->driver_error == OK) {
                LOG_ERROR("WIFI", "Interface em erro sem codigo");
                return ERR_STATE;
            }
        } else if (info->state != WIFI_INTERFACE_INVENTORIED) {
            LOG_ERROR("WIFI", "Estado de interface Wi-Fi invalido");
            return ERR_STATE;
        }
        if (info->transport == WIFI_TRANSPORT_USB &&
            (info->state == WIFI_INTERFACE_READY ||
             info->state == WIFI_INTERFACE_ERROR) &&
            (rtl8811cu_get_status(&driver_status) != OK ||
             rtl8811cu_validate_state() != OK)) {
            LOG_ERROR("WIFI", "Estado do backend RTL8811CU invalido");
            return ERR_STATE;
        }
    }
    if (wifi_status.candidate_count != wifi_status.interface_count ||
        wifi_status.unsupported_count != unsupported_count ||
        wifi_status.ready_count != ready_count ||
        wifi_status.error_count != error_count) {
        LOG_ERROR("WIFI", "Contadores do inventario Wi-Fi incoerentes");
        return ERR_STATE;
    }
    if (wifi_status.partial && wifi_status.last_error != ERR_OVERFLOW) {
        LOG_ERROR("WIFI", "Inventario parcial sem erro de overflow");
        return ERR_STATE;
    }
    if (!wifi_status.partial && !wifi_status.interface_count &&
        wifi_status.last_error != ERR_NOT_FOUND) {
        LOG_ERROR("WIFI", "Ausencia de candidatos com estado incoerente");
        return ERR_STATE;
    }
    if (!wifi_status.partial && wifi_status.interface_count &&
        !wifi_status.ready_count && !wifi_status.error_count &&
        wifi_status.last_error != ERR_UNAVAILABLE) {
        LOG_ERROR("WIFI", "Candidato sem estado de indisponibilidade");
        return ERR_STATE;
    }
    if (wifi_status.ready_count && wifi_status.last_error != OK &&
        !wifi_status.error_count) {
        LOG_ERROR("WIFI", "Backend pronto com codigo de erro Wi-Fi");
        return ERR_STATE;
    }
    return OK;
}

int wifi_manager_scan(wifi_scan_result_t* results, uint32_t capacity,
                      uint32_t* out_count) {
    rtl8811cu_scan_result_t driver_results[WIFI_SCAN_RESULT_CAPACITY];
    int result = ERR_UNAVAILABLE;

    if (!results || !out_count) {
        LOG_ERROR("WIFI", "Destino nulo no scan Wi-Fi");
        return ERR_NULL;
    }
    if (!capacity || capacity > WIFI_SCAN_RESULT_CAPACITY) {
        LOG_ERROR("WIFI", "Capacidade invalida no scan Wi-Fi");
        return ERR_INVALID;
    }
    if (!wifi_manager_initialized) {
        LOG_ERROR("WIFI", "Scan Wi-Fi antes da inicializacao");
        return ERR_STATE;
    }
    *out_count = 0U;
    kmemset(results, 0, sizeof(*results) * capacity);
    for (uint32_t index = 0U; index < wifi_status.interface_count; index++) {
        wifi_interface_info_t* info = &wifi_interfaces[index];
        uint32_t driver_count = 0U;

        if (info->transport != WIFI_TRANSPORT_USB ||
            info->state != WIFI_INTERFACE_READY) continue;
        result = rtl8811cu_scan(driver_results, capacity, &driver_count);
        if (result != OK) return result;
        for (uint32_t item = 0U; item < driver_count; item++) {
            results[item].ssid_length = driver_results[item].ssid_length;
            kmemcpy(results[item].ssid, driver_results[item].ssid,
                    sizeof(results[item].ssid));
            kmemcpy(results[item].bssid, driver_results[item].bssid,
                    WIFI_BSSID_LENGTH);
            results[item].channel = driver_results[item].channel;
            results[item].open_security = driver_results[item].open_security;
        }
        *out_count = driver_count;
        return OK;
    }
    LOG_WARN("WIFI", "Nenhum backend USB Wi-Fi pronto para scan");
    return result;
}

int wifi_manager_connect_open(const char* ssid) {
    if (!ssid) {
        LOG_ERROR("WIFI", "SSID nulo na conexao aberta");
        return ERR_NULL;
    }
    if (!wifi_manager_initialized) {
        LOG_ERROR("WIFI", "Conexao Wi-Fi antes da inicializacao");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < wifi_status.interface_count; index++) {
        wifi_interface_info_t* info = &wifi_interfaces[index];

        if (info->transport == WIFI_TRANSPORT_USB &&
            info->state == WIFI_INTERFACE_READY) {
            return rtl8811cu_connect_open(ssid);
        }
    }
    LOG_WARN("WIFI", "Nenhum backend USB Wi-Fi pronto para associacao");
    return ERR_UNAVAILABLE;
}

const char* wifi_manager_state_name(wifi_interface_state_t state) {
    if (state == WIFI_INTERFACE_INVENTORIED) return "INVENTORIED";
    if (state == WIFI_INTERFACE_UNSUPPORTED) return "UNSUPPORTED";
    if (state == WIFI_INTERFACE_READY) return "READY";
    if (state == WIFI_INTERFACE_ERROR) return "ERROR";
    return "UNKNOWN";
}
