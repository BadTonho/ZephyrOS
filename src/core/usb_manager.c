#include "core/usb_manager.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/string.h"
#include "drivers/pci.h"
#include "drivers/uhci.h"

#define USB_HEX_DIGITS_BYTE 2U

static usb_controller_info_t usb_controllers[USB_MANAGER_MAX_CONTROLLERS];
static usb_port_info_t usb_ports[USB_MANAGER_MAX_PORTS];
static usb_device_info_t usb_devices[USB_MANAGER_MAX_DEVICES];
static usb_manager_status_t usb_status;
static uint32_t usb_port_count;
static uint32_t usb_device_count;
static uint32_t usb_runtime_failures;
static int usb_manager_initialized;

static void usb_append_char(char* text, uint32_t capacity,
                            uint32_t* offset, char value) {
    if (!text || !offset || capacity == 0U || *offset + 1U >= capacity) return;
    text[*offset] = value;
    (*offset)++;
    text[*offset] = '\0';
}

static void usb_append_text(char* text, uint32_t capacity,
                            uint32_t* offset, const char* value) {
    if (!value) return;
    while (*value) usb_append_char(text, capacity, offset, *value++);
}

static void usb_append_hex(char* text, uint32_t capacity, uint32_t* offset,
                           uint32_t value, uint32_t digits) {
    static const char hex[] = "0123456789ABCDEF";

    while (digits > 0U) {
        uint32_t shift = (digits - 1U) * 4U;
        usb_append_char(text, capacity, offset, hex[(value >> shift) & 0xFU]);
        digits--;
    }
}

static void usb_set_text(char* destination, uint32_t capacity,
                         const char* source) {
    uint32_t offset = 0;

    if (!destination || !capacity) return;
    destination[0] = '\0';
    usb_append_text(destination, capacity, &offset, source);
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

static usb_controller_model_t usb_classify(uint8_t prog_if) {
    if (prog_if == USB_CONTROLLER_PROG_IF_UHCI) {
        return USB_CONTROLLER_MODEL_UHCI;
    }
    if (prog_if == USB_CONTROLLER_PROG_IF_EHCI) {
        return USB_CONTROLLER_MODEL_EHCI;
    }
    return USB_CONTROLLER_MODEL_OTHER;
}

static void usb_format_controller_id(const usb_controller_info_t* info,
                                     char* output, uint32_t capacity) {
    uint32_t offset = 0;

    if (!info || !output || !capacity) return;
    output[0] = '\0';
    usb_append_text(output, capacity, &offset, "usb-pci-");
    usb_append_hex(output, capacity, &offset, info->bus, USB_HEX_DIGITS_BYTE);
    usb_append_char(output, capacity, &offset, ':');
    usb_append_hex(output, capacity, &offset, info->device,
                   USB_HEX_DIGITS_BYTE);
    usb_append_char(output, capacity, &offset, '.');
    usb_append_hex(output, capacity, &offset, info->function, 1U);
}

static int usb_copy_pci(const pci_device_t* pci,
                        usb_controller_info_t* controller) {
    if (!pci || !controller) {
        LOG_ERROR("USB", "Argumento nulo ao copiar controlador PCI");
        return ERR_NULL;
    }
    kmemset(controller, 0, sizeof(*controller));
    controller->model = usb_classify(pci->prog_if);
    controller->state = controller->model == USB_CONTROLLER_MODEL_OTHER ||
                        controller->model == USB_CONTROLLER_MODEL_EHCI ?
                        USB_CONTROLLER_DEGRADED : USB_CONTROLLER_DISABLED;
    controller->reason = controller->model == USB_CONTROLLER_MODEL_OTHER ||
                         controller->model == USB_CONTROLLER_MODEL_EHCI ?
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

static void usb_sync_recovery(int result) {
    const recovery_component_t* current =
        recovery_get(RECOVERY_COMPONENT_USB);
    recovery_state_t state;
    int error;
    const char* message;

    if (!current) {
        LOG_ERROR("USB", "Componente USB ausente do recovery");
        return;
    }
    if (result != OK && result != ERR_OVERFLOW) {
        state = RECOVERY_STATE_DISABLED;
        error = result;
        message = "Inventario USB indisponivel";
    } else if (usb_status.partial) {
        state = RECOVERY_STATE_DEGRADED;
        error = ERR_OVERFLOW;
        message = "Inventario USB parcial";
    } else if (!usb_status.controller_count) {
        state = RECOVERY_STATE_DISABLED;
        error = ERR_NOT_FOUND;
        message = "Nenhum controlador USB detectado";
    } else if (usb_runtime_failures || usb_status.other_count ||
               usb_status.ehci_count == usb_status.controller_count) {
        state = RECOVERY_STATE_DEGRADED;
        error = usb_status.last_error == OK ? ERR_UNAVAILABLE :
                usb_status.last_error;
        message = "Driver UHCI ou porta USB degradado";
    } else {
        state = RECOVERY_STATE_READY;
        error = OK;
        message = "USB UHCI operacional";
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
    kmemset(usb_controllers, 0, sizeof(usb_controllers));
    usb_port_count = 0U;
    usb_device_count = 0U;
    usb_runtime_failures = 0U;
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
            usb_status.partial = 1U;
            continue;
        }
        result = usb_copy_pci(&pci,
                              &usb_controllers[usb_status.controller_count]);
        if (result != OK) return result;
        if (usb_controllers[usb_status.controller_count].model ==
            USB_CONTROLLER_MODEL_UHCI) usb_status.uhci_count++;
        else if (usb_controllers[usb_status.controller_count].model ==
                 USB_CONTROLLER_MODEL_EHCI) usb_status.ehci_count++;
        else usb_status.other_count++;
        usb_status.controller_count++;
    }
    if (pci_result == ERR_OVERFLOW) usb_status.partial = 1U;
    if (usb_status.partial) usb_status.last_error = ERR_OVERFLOW;
    else if (!usb_status.controller_count) usb_status.last_error = ERR_NOT_FOUND;
    else usb_status.last_error = OK;
    return usb_status.partial ? ERR_OVERFLOW : OK;
}

static void usb_apply_runtime_controller(uint32_t index) {
    usb_controller_info_t* info = &usb_controllers[index];
    usb_uhci_status_t runtime;
    uint32_t ports = 0U;
    uint32_t devices = 0U;

    if (info->model != USB_CONTROLLER_MODEL_UHCI) return;
    /* Uma falha de inicializacao ja foi registrada no inventario. Nao
       consultar um runtime inexistente a cada ciclo do kernel, pois isso
       transforma uma falha isolada em uma inundacao de logs. */
    if (!info->uhci_initialized && info->uhci_last_error != OK) {
        usb_runtime_failures++;
        return;
    }
    if (uhci_get_status(info->bus, info->device, info->function, &runtime) != OK) {
        info->state = USB_CONTROLLER_DISABLED;
        info->reason = USB_CONTROLLER_REASON_DRIVER_FAILURE;
        info->uhci_last_error = ERR_UNAVAILABLE;
        usb_runtime_failures++;
        return;
    }
    info->uhci_initialized = runtime.initialized;
    info->uhci_irq_registered = runtime.irq_registered;
    info->uhci_dma_ready = runtime.dma_ready;
    info->uhci_transfer_ready = runtime.control_transfer_ready;
    info->uhci_port_count = runtime.port_count;
    info->uhci_device_count = runtime.device_count;
    info->uhci_port_errors = runtime.port_errors;
    info->uhci_last_error = runtime.last_error;
    if (!runtime.initialized || !runtime.running || !runtime.dma_ready ||
        !runtime.irq_registered) {
        info->state = USB_CONTROLLER_DISABLED;
        info->reason = USB_CONTROLLER_REASON_DRIVER_FAILURE;
        usb_runtime_failures++;
        return;
    }
    info->state = runtime.port_errors ? USB_CONTROLLER_DEGRADED :
                  USB_CONTROLLER_READY;
    info->reason = runtime.port_errors ? USB_CONTROLLER_REASON_PORT_FAILURE :
                  USB_CONTROLLER_REASON_DRIVER_READY;
    usb_status.uhci_ready_count++;
    usb_status.dma_initialized = 1U;
    usb_status.irq_initialized = 1U;
    usb_status.transfer_available = 1U;
    usb_status.dma_td_capacity += runtime.td_capacity;
    usb_status.dma_td_in_use += runtime.td_in_use;
    usb_status.port_count += runtime.port_count;
    usb_status.degraded_port_count += runtime.port_errors;
    if (runtime.port_errors) usb_runtime_failures++;
    if (uhci_get_port_count(info->bus, info->device, info->function,
                            &ports) != OK) return;
    for (uint32_t port = 0; port < ports && usb_port_count <
         USB_MANAGER_MAX_PORTS; port++) {
        if (uhci_get_port(info->bus, info->device, info->function, port,
                          &usb_ports[usb_port_count]) == OK) {
            usb_port_count++;
        }
    }
    if (uhci_get_device_count(info->bus, info->device, info->function,
                             &devices) != OK) return;
    for (uint32_t device = 0; device < devices && usb_device_count <
         USB_MANAGER_MAX_DEVICES; device++) {
        if (uhci_get_device(info->bus, info->device, info->function, device,
                            &usb_devices[usb_device_count]) == OK) {
            usb_device_count++;
        }
    }
}

static void usb_collect_runtime(void) {
    usb_status.dma_initialized = 0U;
    usb_status.irq_initialized = 0U;
    usb_status.transfer_available = 0U;
    usb_status.uhci_ready_count = 0U;
    usb_status.port_count = 0U;
    usb_status.configured_device_count = 0U;
    usb_status.degraded_port_count = 0U;
    usb_status.dma_td_capacity = 0U;
    usb_status.dma_td_in_use = 0U;
    usb_status.class_driver_active = 0U;
    usb_status.hub_support_active = 0U;
    usb_status.hotplug_active = 0U;
    usb_port_count = 0U;
    usb_device_count = 0U;
    usb_runtime_failures = 0U;
    for (uint32_t index = 0; index < usb_status.controller_count; index++) {
        usb_apply_runtime_controller(index);
    }
    usb_status.configured_device_count = usb_device_count;
    if (usb_status.partial) usb_status.last_error = ERR_OVERFLOW;
    else if (!usb_status.controller_count) usb_status.last_error = ERR_NOT_FOUND;
    else if (usb_status.other_count || usb_status.ehci_count ==
             usb_status.controller_count || usb_runtime_failures) {
        usb_status.last_error = ERR_UNAVAILABLE;
    } else usb_status.last_error = OK;
}

static int usb_start_controllers(void) {
    uint8_t pci_count = 0;
    int pci_result = pci_get_device_count(&pci_count);

    if (pci_result != OK && pci_result != ERR_OVERFLOW) return pci_result;
    for (uint32_t index = 0; index < usb_status.controller_count; index++) {
        usb_controller_info_t* info = &usb_controllers[index];
        pci_device_t pci;
        char id[USB_CONTROLLER_ID_SIZE];
        int found = 0;

        if (info->model != USB_CONTROLLER_MODEL_UHCI) continue;
        for (uint8_t pci_index = 0; pci_index < pci_count; pci_index++) {
            if (pci_get_device_at(pci_index, &pci) == OK &&
                pci.bus == info->bus && pci.device == info->device &&
                pci.function == info->function) {
                found = 1;
                break;
            }
        }
        if (!found) {
            info->state = USB_CONTROLLER_DEGRADED;
            info->reason = USB_CONTROLLER_REASON_DRIVER_FAILURE;
            info->uhci_last_error = ERR_NOT_FOUND;
            continue;
        }
        usb_format_controller_id(info, id, sizeof(id));
        {
            int init_result = uhci_init(&pci, id);

            if (init_result == OK) continue;
            info->state = USB_CONTROLLER_DEGRADED;
            info->reason = USB_CONTROLLER_REASON_DRIVER_FAILURE;
            info->uhci_last_error = init_result;
            LOG_ERROR("USB", "Falha ao inicializar driver UHCI");
        }
    }
    return OK;
}

int usb_manager_init(void) {
    int result;

    LOG_INFO("USB", "Inicializando inventario e runtime USB");
    usb_manager_initialized = 1;
    result = usb_build_snapshot();
    if (result != OK && result != ERR_OVERFLOW) {
        usb_manager_initialized = 0;
        usb_status.initialized = 0U;
        usb_sync_recovery(result);
        LOG_ERROR("USB", "Falha ao inicializar inventario USB");
        return result;
    }
    usb_start_controllers();
    usb_collect_runtime();
    usb_status.initialized = 1U;
    usb_sync_recovery(result);
    if (result == ERR_OVERFLOW) LOG_WARN("USB", "Inventario USB parcial");
    LOG_INFO("USB", "Inventario e runtime USB inicializados");
    return result;
}

int usb_manager_refresh(void) {
    int result;

    if (!usb_manager_initialized) {
        LOG_ERROR("USB", "Atualizacao antes da inicializacao USB");
        return ERR_STATE;
    }
    result = usb_build_snapshot();
    if (result != OK && result != ERR_OVERFLOW) {
        usb_status.initialized = 1U;
        usb_sync_recovery(result);
        LOG_ERROR("USB", "Falha ao atualizar inventario USB");
        return result;
    }
    usb_start_controllers();
    usb_collect_runtime();
    usb_status.initialized = 1U;
    usb_sync_recovery(result);
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
        LOG_ERROR("USB", "Contagem antes da inicializacao USB");
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
        LOG_ERROR("USB", "Consulta de controlador antes da inicializacao USB");
        return ERR_STATE;
    }
    if (index >= usb_status.controller_count) {
        LOG_ERROR("USB", "Indice de controlador USB invalido");
        return ERR_INVALID;
    }
    *out_info = usb_controllers[index];
    return OK;
}

int usb_manager_find(const char* id, usb_controller_info_t* out_info) {
    char expected[USB_CONTROLLER_ID_SIZE];

    if (!id || !out_info) {
        LOG_ERROR("USB", "Argumento nulo ao buscar controlador USB");
        return ERR_NULL;
    }
    if (!usb_manager_initialized) {
        LOG_ERROR("USB", "Busca de controlador antes da inicializacao USB");
        return ERR_STATE;
    }
    for (uint32_t index = 0; index < usb_status.controller_count; index++) {
        usb_format_controller_id(&usb_controllers[index], expected,
                                 sizeof(expected));
        if (usb_id_matches(expected, id)) {
            *out_info = usb_controllers[index];
            return OK;
        }
    }
    LOG_WARN("USB", "Controlador USB solicitado nao encontrado");
    return ERR_NOT_FOUND;
}

int usb_manager_poll(uint32_t budget, uint32_t* out_processed) {
    int result;

    if (!out_processed) {
        LOG_ERROR("USB", "Destino nulo no polling USB");
        return ERR_NULL;
    }
    if (!usb_manager_initialized) {
        LOG_ERROR("USB", "Polling antes da inicializacao USB");
        return ERR_STATE;
    }
    result = uhci_poll(budget, out_processed);
    if (result != OK) {
        LOG_ERROR("USB", "Polling UHCI falhou");
        return result;
    }
    usb_collect_runtime();
    usb_sync_recovery(OK);
    return OK;
}

int usb_manager_get_uhci_status(uint32_t index, usb_uhci_status_t* out_status) {
    usb_controller_info_t info;

    if (!out_status) {
        LOG_ERROR("USB", "Destino nulo ao consultar runtime UHCI");
        return ERR_NULL;
    }
    if (usb_manager_get_info(index, &info) != OK) {
        LOG_ERROR("USB", "Controlador ausente ao consultar runtime UHCI");
        return ERR_INVALID;
    }
    if (info.model != USB_CONTROLLER_MODEL_UHCI) {
        LOG_ERROR("USB", "Runtime UHCI solicitado para controlador nao-UHCI");
        return ERR_UNAVAILABLE;
    }
    return uhci_get_status(info.bus, info.device, info.function, out_status);
}

int usb_manager_get_port_count(uint32_t* out_count) {
    if (!out_count) {
        LOG_ERROR("USB", "Destino nulo na contagem de portas USB");
        return ERR_NULL;
    }
    if (!usb_manager_initialized) {
        LOG_ERROR("USB", "Contagem de portas antes da inicializacao USB");
        return ERR_STATE;
    }
    *out_count = usb_port_count;
    return OK;
}

int usb_manager_get_port(uint32_t index, usb_port_info_t* out_info) {
    if (!out_info) {
        LOG_ERROR("USB", "Destino nulo ao consultar porta USB");
        return ERR_NULL;
    }
    if (!usb_manager_initialized) {
        LOG_ERROR("USB", "Consulta de porta antes da inicializacao USB");
        return ERR_STATE;
    }
    if (index >= usb_port_count) {
        LOG_ERROR("USB", "Indice de porta USB invalido");
        return ERR_INVALID;
    }
    *out_info = usb_ports[index];
    return OK;
}

int usb_manager_get_device_count(uint32_t* out_count) {
    if (!out_count) {
        LOG_ERROR("USB", "Destino nulo na contagem de dispositivos USB");
        return ERR_NULL;
    }
    if (!usb_manager_initialized) {
        LOG_ERROR("USB", "Contagem de dispositivos antes da inicializacao USB");
        return ERR_STATE;
    }
    *out_count = usb_device_count;
    return OK;
}

int usb_manager_get_device(uint32_t index, usb_device_info_t* out_info) {
    if (!out_info) {
        LOG_ERROR("USB", "Destino nulo ao consultar dispositivo USB");
        return ERR_NULL;
    }
    if (!usb_manager_initialized) {
        LOG_ERROR("USB", "Consulta de dispositivo antes da inicializacao USB");
        return ERR_STATE;
    }
    if (index >= usb_device_count) {
        LOG_ERROR("USB", "Indice de dispositivo USB invalido");
        return ERR_INVALID;
    }
    *out_info = usb_devices[index];
    return OK;
}

int usb_manager_find_device(const char* id, usb_device_info_t* out_info) {
    if (!id || !out_info) {
        LOG_ERROR("USB", "Argumento nulo ao buscar dispositivo USB");
        return ERR_NULL;
    }
    if (!usb_manager_initialized) {
        LOG_ERROR("USB", "Busca de dispositivo antes da inicializacao USB");
        return ERR_STATE;
    }
    for (uint32_t index = 0; index < usb_device_count; index++) {
        if (usb_id_matches(usb_devices[index].id, id)) {
            *out_info = usb_devices[index];
            return OK;
        }
    }
    LOG_WARN("USB", "Dispositivo USB solicitado nao encontrado");
    return ERR_NOT_FOUND;
}

int usb_manager_format_device_text(const usb_device_info_t* info,
                                   usb_device_text_t* out_text) {
    if (!info || !out_text) {
        LOG_ERROR("USB", "Argumento nulo ao formatar dispositivo USB");
        return ERR_NULL;
    }
    kmemset(out_text, 0, sizeof(*out_text));
    usb_set_text(out_text->id, USB_DEVICE_ID_SIZE, info->id);
    usb_set_text(out_text->controller_id, USB_CONTROLLER_ID_SIZE,
                 info->controller_id);
    usb_set_text(out_text->detail, USB_CONTROLLER_DETAIL_SIZE,
                 info->class_driver_active ? "Driver de classe ativo" :
                 "Configurado; sem driver de classe USB");
    return OK;
}

int usb_manager_validate_state(void) {
    uint32_t calculated_ports = 0U;
    uint32_t calculated_devices = 0U;

    if (!usb_manager_initialized) {
        LOG_ERROR("USB", "Validacao antes da inicializacao USB");
        return ERR_STATE;
    }
    if (usb_status.controller_count > USB_MANAGER_MAX_CONTROLLERS ||
        usb_port_count > USB_MANAGER_MAX_PORTS ||
        usb_device_count > USB_MANAGER_MAX_DEVICES ||
        usb_status.class_driver_active || usb_status.hub_support_active ||
        usb_status.hotplug_active) {
        LOG_ERROR("USB", "Limites ou capacidades USB invalidos");
        return ERR_STATE;
    }
    for (uint32_t index = 0; index < usb_status.controller_count; index++) {
        usb_controller_info_t* info = &usb_controllers[index];

        if (info->model == USB_CONTROLLER_MODEL_UHCI) {
            usb_uhci_status_t status;

            if (uhci_get_status(info->bus, info->device, info->function,
                                &status) == OK) {
                if (uhci_validate_state(info->bus, info->device,
                                        info->function) != OK ||
                    status.class_driver_active || status.hub_support_active ||
                    status.hotplug_active || status.td_in_use > status.td_capacity ||
                    status.buffer_in_use > status.buffer_capacity) {
                    LOG_ERROR("USB", "Runtime UHCI invalido");
                    return ERR_STATE;
                }
                calculated_ports += status.port_count;
                calculated_devices += status.device_count;
            }
        } else if (info->uhci_initialized || info->uhci_irq_registered ||
                   info->uhci_dma_ready || info->uhci_transfer_ready) {
            LOG_ERROR("USB", "EHCI recebeu estado de runtime UHCI");
            return ERR_STATE;
        }
    }
    if (calculated_ports != usb_port_count ||
        calculated_devices != usb_device_count ||
        usb_status.configured_device_count != usb_device_count ||
        usb_status.dma_td_in_use > usb_status.dma_td_capacity) {
        LOG_ERROR("USB", "Agregacao USB inconsistente");
        return ERR_STATE;
    }
    for (uint32_t index = 0; index < usb_port_count; index++) {
        usb_port_info_t* port = &usb_ports[index];
        uint8_t matched_device = 0U;

        if (!port->controller_id[0] || port->port_number == 0U ||
            port->port_number > USB_UHCI_PORT_COUNT ||
            port->state > USB_PORT_DEGRADED) {
            LOG_ERROR("USB", "Entrada de porta USB invalida");
            return ERR_STATE;
        }
        for (uint32_t device = 0; device < usb_device_count; device++) {
            if (usb_devices[device].port_number == port->port_number &&
                usb_devices[device].controller_bus == port->controller_bus &&
                usb_devices[device].controller_device == port->controller_device &&
                usb_devices[device].controller_function == port->controller_function &&
                usb_id_matches(port->device_id, usb_devices[device].id)) {
                matched_device = 1U;
                break;
            }
        }
        if (port->state == USB_PORT_CONFIGURED &&
            (!port->device_id[0] || !matched_device || !port->usb_address)) {
            LOG_ERROR("USB", "Associacao porta/dispositivo USB invalida");
            return ERR_STATE;
        }
    }
    for (uint32_t index = 0; index < usb_device_count; index++) {
        usb_device_info_t* device = &usb_devices[index];

        if (!device->id[0] || !device->controller_id[0] ||
            device->usb_address == 0U || device->usb_address > 127U ||
            device->state != USB_DEVICE_CONFIGURED ||
            device->class_driver_active || device->hub_present ||
            !device->device_descriptor_valid ||
            !device->configuration_descriptor_valid ||
            !device->max_packet_size0 || !device->configuration_length) {
            LOG_ERROR("USB", "Entrada de dispositivo USB invalida");
            return ERR_STATE;
        }
    }
    return OK;
}

int usb_manager_format_text(const usb_controller_info_t* info,
                            usb_controller_text_t* out_text) {
    uint32_t offset = 0U;

    if (!info || !out_text) {
        LOG_ERROR("USB", "Argumento nulo ao formatar controlador USB");
        return ERR_NULL;
    }
    kmemset(out_text, 0, sizeof(*out_text));
    usb_format_controller_id(info, out_text->id, USB_CONTROLLER_ID_SIZE);
    usb_set_text(out_text->location, USB_CONTROLLER_LOCATION_SIZE, "PCI ");
    offset = 4U;
    usb_append_hex(out_text->location, USB_CONTROLLER_LOCATION_SIZE, &offset,
                   info->bus, USB_HEX_DIGITS_BYTE);
    usb_append_char(out_text->location, USB_CONTROLLER_LOCATION_SIZE, &offset, ':');
    usb_append_hex(out_text->location, USB_CONTROLLER_LOCATION_SIZE, &offset,
                   info->device, USB_HEX_DIGITS_BYTE);
    usb_append_char(out_text->location, USB_CONTROLLER_LOCATION_SIZE, &offset, '.');
    usb_append_hex(out_text->location, USB_CONTROLLER_LOCATION_SIZE, &offset,
                   info->function, 1U);
    if (info->model == USB_CONTROLLER_MODEL_UHCI) {
        usb_set_text(out_text->name, USB_CONTROLLER_NAME_SIZE, "UHCI USB Controller");
        usb_set_text(out_text->detail, USB_CONTROLLER_DETAIL_SIZE,
                     info->uhci_initialized ?
                     "Driver UHCI ativo; controle configurado" :
                     "Driver UHCI nao inicializado");
    } else if (info->model == USB_CONTROLLER_MODEL_EHCI) {
        usb_set_text(out_text->name, USB_CONTROLLER_NAME_SIZE, "EHCI USB Controller");
        usb_set_text(out_text->detail, USB_CONTROLLER_DETAIL_SIZE,
                     "Inventariado; driver EHCI fora do escopo");
    } else {
        usb_set_text(out_text->name, USB_CONTROLLER_NAME_SIZE,
                     "USB Controller desconhecido");
        usb_set_text(out_text->detail, USB_CONTROLLER_DETAIL_SIZE,
                     "Controlador fora do escopo");
    }
    return OK;
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
    if (reason == USB_CONTROLLER_REASON_DRIVER_NOT_INITIALIZED) return "DRIVER_NOT_INITIALIZED";
    if (reason == USB_CONTROLLER_REASON_DRIVER_READY) return "DRIVER_READY";
    if (reason == USB_CONTROLLER_REASON_DRIVER_FAILURE) return "DRIVER_FAILURE";
    if (reason == USB_CONTROLLER_REASON_PORT_FAILURE) return "PORT_FAILURE";
    if (reason == USB_CONTROLLER_REASON_OUT_OF_SCOPE) return "OUT_OF_SCOPE";
    return "UNKNOWN";
}

const char* usb_manager_uhci_state_name(usb_uhci_state_t state) {
    if (state == USB_UHCI_STATE_READY) return "READY";
    if (state == USB_UHCI_STATE_DEGRADED) return "DEGRADED";
    return "DISABLED";
}

const char* usb_manager_port_state_name(usb_port_state_t state) {
    if (state == USB_PORT_EMPTY) return "EMPTY";
    if (state == USB_PORT_RESETTING) return "RESETTING";
    if (state == USB_PORT_ENUMERATING) return "ENUMERATING";
    if (state == USB_PORT_CONFIGURED) return "CONFIGURED";
    if (state == USB_PORT_DEGRADED) return "DEGRADED";
    return "UNKNOWN";
}

const char* usb_manager_port_reason_name(usb_port_reason_t reason) {
    if (reason == USB_PORT_REASON_NONE) return "NONE";
    if (reason == USB_PORT_REASON_NO_DEVICE) return "NO_DEVICE";
    if (reason == USB_PORT_REASON_RESET_TIMEOUT) return "RESET_TIMEOUT";
    if (reason == USB_PORT_REASON_CONTROL_TIMEOUT) return "CONTROL_TIMEOUT";
    if (reason == USB_PORT_REASON_INVALID_DESCRIPTOR) return "INVALID_DESCRIPTOR";
    if (reason == USB_PORT_REASON_UNSUPPORTED_SPEED) return "UNSUPPORTED_SPEED";
    if (reason == USB_PORT_REASON_UNSUPPORTED_LAYOUT) return "UNSUPPORTED_LAYOUT";
    if (reason == USB_PORT_REASON_DRIVER_FAILURE) return "DRIVER_FAILURE";
    return "UNKNOWN";
}

const char* usb_manager_speed_name(usb_device_speed_t speed) {
    return speed == USB_DEVICE_SPEED_LOW ? "LOW" : "FULL";
}
