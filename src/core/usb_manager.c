#include "core/usb_manager.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/string.h"
#include "drivers/pci.h"

#define USB_HEX_DIGITS_BYTE 2U

static usb_controller_info_t
    usb_controllers[USB_MANAGER_MAX_CONTROLLERS];
static usb_manager_status_t usb_status;
static int usb_manager_initialized = 0;

static void usb_append_char(char* text, uint32_t capacity,
                            uint32_t* offset, char value) {
    if (!text || !offset || capacity == 0 || *offset + 1U >= capacity) return;
    text[*offset] = value;
    (*offset)++;
    text[*offset] = '\0';
}

static void usb_append_text(char* text, uint32_t capacity,
                            uint32_t* offset, const char* value) {
    if (!value) return;
    while (*value) {
        usb_append_char(text, capacity, offset, *value);
        value++;
    }
}

static void usb_append_hex(char* text, uint32_t capacity, uint32_t* offset,
                           uint32_t value, uint32_t digits) {
    static const char hex[] = "0123456789ABCDEF";

    while (digits > 0) {
        uint32_t shift = (digits - 1U) * 4U;
        usb_append_char(text, capacity, offset,
                        hex[(value >> shift) & 0x0FU]);
        digits--;
    }
}

static void usb_set_text(char* destination, uint32_t capacity,
                         const char* source) {
    uint32_t offset = 0;

    if (!destination || capacity == 0) return;
    destination[0] = '\0';
    usb_append_text(destination, capacity, &offset, source);
}

static usb_controller_model_t usb_classify(uint8_t prog_if) {
    if (prog_if == USB_CONTROLLER_PROG_IF_UHCI) {
        return USB_CONTROLLER_MODEL_UHCI;
    }
    if (prog_if == USB_CONTROLLER_PROG_IF_EHCI) {
        return USB_CONTROLLER_MODEL_EHCI;
    }
    return USB_CONTROLLER_MODEL_OTHER;
}

static int usb_copy_pci(const pci_device_t* pci,
                        usb_controller_info_t* controller) {
    if (!pci || !controller) {
        LOG_ERROR("USB", "Argumento nulo ao copiar controlador PCI");
        return ERR_NULL;
    }
    controller->model = usb_classify(pci->prog_if);
    controller->state = controller->model == USB_CONTROLLER_MODEL_OTHER ?
                        USB_CONTROLLER_DEGRADED : USB_CONTROLLER_READY;
    controller->reason = controller->model == USB_CONTROLLER_MODEL_OTHER ?
                         USB_CONTROLLER_REASON_OUT_OF_SCOPE :
                         USB_CONTROLLER_REASON_DRIVER_NOT_INITIALIZED;
    controller->vendor_id = pci->vendor_id;
    controller->device_id = pci->device_id;
    controller->class_code = pci->class;
    controller->subclass_code = pci->subclass;
    controller->prog_if = pci->prog_if;
    controller->revision = pci->revision;
    controller->bus = pci->bus;
    controller->device = pci->device;
    controller->function = pci->function;
    controller->irq = pci->irq;
    controller->bars[0] = pci->bar0;
    controller->bars[1] = pci->bar1;
    controller->bars[2] = pci->bar2;
    controller->bars[3] = pci->bar3;
    controller->bars[4] = pci->bar4;
    controller->bars[5] = pci->bar5;
    return OK;
}

static char usb_ascii_upper(char value) {
    if (value >= 'a' && value <= 'z') return (char)(value - ('a' - 'A'));
    return value;
}

static int usb_id_matches(const char* expected, const char* requested) {
    if (!expected || !requested) return 0;
    while (*expected && *requested) {
        if (*expected == ':' && *requested == '-') {
            expected++;
            requested++;
            continue;
        }
        if (usb_ascii_upper(*expected) != usb_ascii_upper(*requested)) return 0;
        expected++;
        requested++;
    }
    return *expected == '\0' && *requested == '\0';
}

static void usb_sync_recovery(int result) {
    const recovery_component_t* current;
    recovery_state_t state;
    int error;
    const char* message;

    if (result != OK && result != ERR_OVERFLOW) {
        state = RECOVERY_STATE_DISABLED;
        error = result;
        message = "Inventario USB indisponivel";
    } else if (usb_status.partial) {
        state = RECOVERY_STATE_DEGRADED;
        error = ERR_OVERFLOW;
        message = "Inventario USB parcial";
    } else if (usb_status.controller_count == 0U) {
        state = RECOVERY_STATE_DISABLED;
        error = ERR_NOT_FOUND;
        message = "Nenhum controlador USB detectado";
    } else if (usb_status.other_count > 0U) {
        state = RECOVERY_STATE_DEGRADED;
        error = ERR_UNAVAILABLE;
        message = "Controlador USB fora do escopo detectado";
    } else {
        state = RECOVERY_STATE_READY;
        error = OK;
        message = "Inventario USB operacional";
    }
    current = recovery_get(RECOVERY_COMPONENT_USB);
    if (!current) {
        LOG_ERROR("USB", "Componente USB ausente do recovery");
        return;
    }
    if (current->state == state && current->last_error == error) return;
    if (state == RECOVERY_STATE_READY) {
        if (recovery_mark_ready(RECOVERY_COMPONENT_USB) != OK) {
            LOG_ERROR("USB", "Falha ao marcar USB pronto");
        }
    } else if (state == RECOVERY_STATE_DEGRADED) {
        if (recovery_mark_degraded(RECOVERY_COMPONENT_USB, error, message) != OK) {
            LOG_ERROR("USB", "Falha ao marcar USB degradado");
        }
    } else if (recovery_mark_disabled(RECOVERY_COMPONENT_USB, error, message) != OK) {
        LOG_ERROR("USB", "Falha ao desabilitar USB");
    }
}

static int usb_build_snapshot(void) {
    uint8_t pci_count = 0;
    int pci_result;

    kmemset(&usb_status, 0, sizeof(usb_status));
    pci_result = pci_get_device_count(&pci_count);
    if (pci_result != OK && pci_result != ERR_OVERFLOW) {
        usb_status.last_error = pci_result;
        LOG_ERROR("USB", "Falha ao consultar snapshot PCI");
        return pci_result;
    }
    for (uint8_t index = 0; index < pci_count; index++) {
        pci_device_t pci;
        int result = pci_get_device_at(index, &pci);

        if (result != OK) {
            LOG_ERROR("USB", "Falha ao consultar dispositivo PCI");
            return result;
        }
        if (pci.class != USB_CONTROLLER_PCI_CLASS ||
            pci.subclass != USB_CONTROLLER_PCI_SUBCLASS) continue;
        if (usb_status.controller_count >= USB_MANAGER_MAX_CONTROLLERS) {
            usb_status.partial = 1;
            continue;
        }
        result = usb_copy_pci(&pci,
                              &usb_controllers[usb_status.controller_count]);
        if (result != OK) {
            LOG_ERROR("USB", "Falha ao registrar controlador USB");
            return result;
        }
        if (usb_controllers[usb_status.controller_count].model ==
            USB_CONTROLLER_MODEL_UHCI) {
            usb_status.uhci_count++;
        } else if (usb_controllers[usb_status.controller_count].model ==
                   USB_CONTROLLER_MODEL_EHCI) {
            usb_status.ehci_count++;
        } else {
            usb_status.other_count++;
        }
        usb_status.controller_count++;
    }
    if (pci_result == ERR_OVERFLOW) usb_status.partial = 1;
    if (usb_status.partial) usb_status.last_error = ERR_OVERFLOW;
    else if (usb_status.other_count > 0U) usb_status.last_error = ERR_UNAVAILABLE;
    else if (usb_status.controller_count == 0U) usb_status.last_error = ERR_NOT_FOUND;
    else usb_status.last_error = OK;
    return usb_status.partial ? ERR_OVERFLOW : OK;
}

int usb_manager_init(void) {
    int result;

    LOG_INFO("USB", "Inicializando inventario USB");
    usb_manager_initialized = 1;
    result = usb_build_snapshot();
    if (result != OK && result != ERR_OVERFLOW) {
        usb_manager_initialized = 0;
        usb_status.initialized = 0;
        usb_sync_recovery(result);
        LOG_ERROR("USB", "Falha ao inicializar inventario USB");
        return result;
    }
    usb_status.initialized = 1;
    usb_sync_recovery(result);
    if (result == ERR_OVERFLOW) LOG_WARN("USB", "Inventario USB parcial");
    LOG_INFO("USB", "Inventario USB inicializado com sucesso");
    return result;
}

int usb_manager_refresh(void) {
    int result;

    if (!usb_manager_initialized) {
        LOG_ERROR("USB", "Atualizacao antes da inicializacao USB");
        return ERR_STATE;
    }
    result = usb_build_snapshot();
    usb_status.initialized = 1;
    usb_sync_recovery(result);
    if (result != OK && result != ERR_OVERFLOW) {
        LOG_ERROR("USB", "Falha ao atualizar inventario USB");
        return result;
    }
    if (result == ERR_OVERFLOW) LOG_WARN("USB", "Inventario USB atualizado parcialmente");
    else LOG_INFO("USB", "Inventario USB atualizado com sucesso");
    return result;
}

int usb_manager_get_status(usb_manager_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("USB", "Destino nulo ao consultar status USB");
        return ERR_NULL;
    }
    if (!usb_manager_initialized) {
        LOG_ERROR("USB", "Consulta de status antes da inicializacao USB");
        return ERR_STATE;
    }
    *out_status = usb_status;
    return OK;
}

int usb_manager_get_count(uint32_t* out_count) {
    if (!out_count) {
        LOG_ERROR("USB", "Destino nulo ao consultar contagem USB");
        return ERR_NULL;
    }
    if (!usb_manager_initialized) {
        LOG_ERROR("USB", "Consulta de contagem antes da inicializacao USB");
        return ERR_STATE;
    }
    *out_count = usb_status.controller_count;
    return OK;
}

int usb_manager_get_info(uint32_t index, usb_controller_info_t* out_info) {
    if (!out_info) {
        LOG_ERROR("USB", "Destino nulo ao consultar controlador USB");
        return ERR_NULL;
    }
    if (!usb_manager_initialized) {
        LOG_ERROR("USB", "Consulta antes da inicializacao USB");
        return ERR_STATE;
    }
    if (index >= usb_status.controller_count) {
        LOG_ERROR("USB", "Indice de controlador USB invalido");
        return ERR_INVALID;
    }
    *out_info = usb_controllers[index];
    return OK;
}

static void usb_format_id(const usb_controller_info_t* info,
                          usb_controller_text_t* out_text) {
    uint32_t offset = 0;

    usb_append_text(out_text->id, USB_CONTROLLER_ID_SIZE, &offset, "usb-pci-");
    usb_append_hex(out_text->id, USB_CONTROLLER_ID_SIZE, &offset, info->bus,
                   USB_HEX_DIGITS_BYTE);
    usb_append_char(out_text->id, USB_CONTROLLER_ID_SIZE, &offset, ':');
    usb_append_hex(out_text->id, USB_CONTROLLER_ID_SIZE, &offset, info->device,
                   USB_HEX_DIGITS_BYTE);
    usb_append_char(out_text->id, USB_CONTROLLER_ID_SIZE, &offset, '.');
    usb_append_hex(out_text->id, USB_CONTROLLER_ID_SIZE, &offset,
                   info->function, 1U);
}

int usb_manager_format_text(const usb_controller_info_t* info,
                            usb_controller_text_t* out_text) {
    if (!info || !out_text) {
        LOG_ERROR("USB", "Argumento nulo ao formatar controlador USB");
        return ERR_NULL;
    }
    kmemset(out_text, 0, sizeof(*out_text));
    usb_format_id(info, out_text);
    if (info->model == USB_CONTROLLER_MODEL_UHCI) {
        usb_set_text(out_text->name, USB_CONTROLLER_NAME_SIZE,
                     "UHCI USB Controller");
        usb_set_text(out_text->detail, USB_CONTROLLER_DETAIL_SIZE,
                     "Inventariado; driver USB nao inicializado");
    } else if (info->model == USB_CONTROLLER_MODEL_EHCI) {
        usb_set_text(out_text->name, USB_CONTROLLER_NAME_SIZE,
                     "EHCI USB Controller");
        usb_set_text(out_text->detail, USB_CONTROLLER_DETAIL_SIZE,
                     "Inventariado; driver USB nao inicializado");
    } else {
        usb_set_text(out_text->name, USB_CONTROLLER_NAME_SIZE,
                     "USB Controller desconhecido");
        usb_set_text(out_text->detail, USB_CONTROLLER_DETAIL_SIZE,
                     "Controlador fora do escopo da EP4.1");
    }
    usb_set_text(out_text->location, USB_CONTROLLER_LOCATION_SIZE, "PCI ");
    {
        uint32_t offset = 4U;
        usb_append_hex(out_text->location, USB_CONTROLLER_LOCATION_SIZE, &offset,
                       info->bus, USB_HEX_DIGITS_BYTE);
        usb_append_char(out_text->location, USB_CONTROLLER_LOCATION_SIZE,
                        &offset, ':');
        usb_append_hex(out_text->location, USB_CONTROLLER_LOCATION_SIZE, &offset,
                       info->device, USB_HEX_DIGITS_BYTE);
        usb_append_char(out_text->location, USB_CONTROLLER_LOCATION_SIZE,
                        &offset, '.');
        usb_append_hex(out_text->location, USB_CONTROLLER_LOCATION_SIZE, &offset,
                       info->function, 1U);
    }
    return OK;
}

int usb_manager_find(const char* id, usb_controller_info_t* out_info) {
    usb_controller_text_t text;

    if (!id || !out_info) {
        LOG_ERROR("USB", "Argumento nulo ao buscar controlador USB");
        return ERR_NULL;
    }
    if (!usb_manager_initialized) {
        LOG_ERROR("USB", "Busca antes da inicializacao USB");
        return ERR_STATE;
    }
    for (uint32_t index = 0; index < usb_status.controller_count; index++) {
        if (usb_manager_format_text(&usb_controllers[index], &text) != OK) {
            LOG_ERROR("USB", "Falha ao formatar controlador durante busca");
            return ERR_STATE;
        }
        if (usb_id_matches(text.id, id)) {
            *out_info = usb_controllers[index];
            return OK;
        }
    }
    LOG_WARN("USB", "Controlador USB solicitado nao encontrado");
    return ERR_NOT_FOUND;
}

const char* usb_manager_model_name(usb_controller_model_t model) {
    if (model == USB_CONTROLLER_MODEL_UHCI) return "UHCI";
    if (model == USB_CONTROLLER_MODEL_EHCI) return "EHCI";
    return "OUTRO";
}

const char* usb_manager_state_name(usb_controller_state_t state) {
    if (state == USB_CONTROLLER_READY) return "READY";
    if (state == USB_CONTROLLER_DEGRADED) return "DEGRADED";
    if (state == USB_CONTROLLER_DISABLED) return "DISABLED";
    return "UNKNOWN";
}

const char* usb_manager_reason_name(usb_controller_reason_t reason) {
    if (reason == USB_CONTROLLER_REASON_DRIVER_NOT_INITIALIZED) {
        return "DRIVER_NOT_INITIALIZED";
    }
    if (reason == USB_CONTROLLER_REASON_OUT_OF_SCOPE) return "OUT_OF_SCOPE";
    return "UNKNOWN";
}
