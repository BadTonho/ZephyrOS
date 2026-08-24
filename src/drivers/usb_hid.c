#include "drivers/usb_hid.h"
#include "core/errors.h"
#include "core/input.h"
#include "core/log.h"
#include "core/string.h"
#include "drivers/uhci.h"

#define USB_HID_CLASS 0x03U
#define USB_HID_SUBCLASS_BOOT 0x01U
#define USB_HID_PROTOCOL_KEYBOARD 0x01U
#define USB_HID_PROTOCOL_MOUSE 0x02U
#define USB_HID_REQUEST_CLASS_INTERFACE 0x21U
#define USB_HID_REQUEST_SET_IDLE 0x0AU
#define USB_HID_REQUEST_SET_PROTOCOL 0x0BU
#define USB_HID_ROLLOVER_MIN 0x01U
#define USB_HID_ROLLOVER_MAX 0x03U
#define USB_HID_BUTTON_MASK 0x07U

typedef struct {
    usb_hid_info_t info;
    usb_device_info_t device;
    uint8_t previous_modifiers;
    uint8_t previous_keys[6];
    uint8_t seen_refresh;
} usb_hid_record_t;

static usb_hid_record_t usb_hid_records[USB_HID_MAX_DEVICES];
static uint32_t usb_hid_count;
static uint8_t usb_hid_initialized;

static int hid_text_equal(const char* left, const char* right) {
    if (!left || !right) return 0;
    while (*left && *right) {
        if (*left != *right) return 0;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static int hid_record_index(const char* id) {
    if (!id) return -1;
    for (uint32_t index = 0U; index < usb_hid_count; index++) {
        if (hid_text_equal(usb_hid_records[index].info.id, id)) {
            return (int)index;
        }
    }
    return -1;
}

static usb_hid_kind_t hid_kind_from_device(const usb_device_info_t* device) {
    return device->interface_protocol == USB_HID_PROTOCOL_MOUSE ?
           USB_HID_KIND_MOUSE : USB_HID_KIND_KEYBOARD;
}

static int hid_is_candidate(const usb_device_info_t* device) {
    if (!device || device->state != USB_DEVICE_CONFIGURED ||
        device->controller_model != USB_CONTROLLER_MODEL_UHCI ||
        device->interface_class != USB_HID_CLASS ||
        device->interface_subclass != USB_HID_SUBCLASS_BOOT ||
        (device->interface_protocol != USB_HID_PROTOCOL_KEYBOARD &&
         device->interface_protocol != USB_HID_PROTOCOL_MOUSE) ||
        device->interrupt_in_count != 1U ||
        !(device->interrupt_in_endpoint & 0x80U) ||
        !(device->interrupt_in_endpoint & 0x0FU) ||
        !device->interrupt_in_max_packet ||
        device->interrupt_in_max_packet > USB_UHCI_INTERRUPT_BUFFER_SIZE ||
        !device->interrupt_interval) return 0;
    if (device->interface_protocol == USB_HID_PROTOCOL_KEYBOARD) {
        return device->interrupt_in_max_packet >=
               USB_HID_KEYBOARD_REPORT_SIZE;
    }
    return device->interrupt_in_max_packet >= USB_HID_MOUSE_REPORT_MIN_SIZE;
}

static void hid_update_device(usb_hid_record_t* record,
                              const usb_device_info_t* device) {
    if (!record || !device) return;
    record->device = *device;
    kmemcpy(record->info.id, device->id, USB_DEVICE_ID_SIZE);
    kmemcpy(record->info.controller_id, device->controller_id,
            USB_PORT_CONTROLLER_ID_SIZE);
    record->info.kind = hid_kind_from_device(device);
    record->info.interface_number = device->interface_number;
    record->info.interrupt_endpoint = device->interrupt_in_endpoint;
    record->info.max_packet = device->interrupt_in_max_packet;
    record->info.interval = device->interrupt_interval;
}

static int hid_send_boot_requests(usb_hid_record_t* record) {
    uint16_t actual = 0U;
    int result;

    result = uhci_control_request(
        &record->device, USB_HID_REQUEST_CLASS_INTERFACE,
        USB_HID_REQUEST_SET_PROTOCOL, 0U, record->info.interface_number,
        0U, 0, &actual);
    if (result != OK) {
        LOG_ERROR("HID", "SET_PROTOCOL Boot falhou");
        return result;
    }
    result = uhci_control_request(
        &record->device, USB_HID_REQUEST_CLASS_INTERFACE,
        USB_HID_REQUEST_SET_IDLE, 0U, record->info.interface_number,
        0U, 0, &actual);
    if (result != OK) {
        LOG_ERROR("HID", "SET_IDLE Boot falhou");
        return result;
    }
    return OK;
}

static int hid_key_in_report(const uint8_t* report, uint8_t usage) {
    if (!report || !usage) return 0;
    for (uint32_t index = 2U; index < USB_HID_KEYBOARD_REPORT_SIZE;
         index++) {
        if (report[index] == usage) return 1;
    }
    return 0;
}

static int hid_key_in_previous(const usb_hid_record_t* record,
                               uint8_t usage) {
    if (!record || !usage) return 0;
    for (uint32_t index = 0U; index < 6U; index++) {
        if (record->previous_keys[index] == usage) return 1;
    }
    return 0;
}

static int hid_publish_key(usb_hid_record_t* record, uint16_t usage,
                           uint8_t pressed, uint8_t modifiers) {
    input_key_event_t event;
    int result;

    event.usage = usage;
    event.pressed = pressed;
    event.modifiers = modifiers;
    event.source = INPUT_SOURCE_USB_HID;
    result = input_publish_key(&event);
    if (result == ERR_OVERFLOW) {
        record->info.dropped_count++;
        return OK;
    }
    return result;
}

static int hid_process_keyboard(usb_hid_record_t* record,
                                const uint8_t* report, uint16_t length) {
    uint8_t modifiers;

    if (!record || !report || length != USB_HID_KEYBOARD_REPORT_SIZE) {
        if (record) {
            record->info.malformed_count++;
            record->info.last_error = ERR_INVALID;
        }
        return ERR_INVALID;
    }
    for (uint32_t index = 2U; index < USB_HID_KEYBOARD_REPORT_SIZE;
         index++) {
        if (report[index] >= USB_HID_ROLLOVER_MIN &&
            report[index] <= USB_HID_ROLLOVER_MAX) {
            record->info.malformed_count++;
            record->info.last_error = ERR_INVALID;
            return ERR_INVALID;
        }
        if (report[index]) {
            for (uint32_t other = index + 1U;
                 other < USB_HID_KEYBOARD_REPORT_SIZE; other++) {
                if (report[index] == report[other]) {
                    record->info.malformed_count++;
                    record->info.last_error = ERR_INVALID;
                    return ERR_INVALID;
                }
            }
        }
    }
    modifiers = report[0];
    for (uint8_t bit = 0U; bit < 8U; bit++) {
        uint8_t old_value = (record->previous_modifiers >> bit) & 1U;
        uint8_t new_value = (modifiers >> bit) & 1U;

        if (old_value == new_value) continue;
        if (hid_publish_key(record, INPUT_USAGE_LEFT_CTRL + bit,
                            new_value, modifiers) != OK) {
            record->info.error_count++;
        }
    }
    for (uint32_t index = 0U; index < 6U; index++) {
        uint8_t old_usage = record->previous_keys[index];

        if (old_usage && !hid_key_in_report(report, old_usage) &&
            hid_publish_key(record, old_usage, 0U, modifiers) != OK) {
            record->info.error_count++;
        }
    }
    for (uint32_t index = 2U; index < USB_HID_KEYBOARD_REPORT_SIZE;
         index++) {
        uint8_t usage = report[index];

        if (usage && !hid_key_in_previous(record, usage) &&
            hid_publish_key(record, usage, 1U, modifiers) != OK) {
            record->info.error_count++;
        }
    }
    record->previous_modifiers = modifiers;
    kmemcpy(record->previous_keys, report + 2U, sizeof(record->previous_keys));
    return OK;
}

static int hid_process_mouse(usb_hid_record_t* record,
                             const uint8_t* report, uint16_t length) {
    input_pointer_event_t event;
    int result;

    if (!record || !report || length < USB_HID_MOUSE_REPORT_MIN_SIZE ||
        length > USB_HID_MOUSE_REPORT_MAX_SIZE) {
        if (record) {
            record->info.malformed_count++;
            record->info.last_error = ERR_INVALID;
        }
        return ERR_INVALID;
    }
    event.dx = (int8_t)report[1];
    /* HID usa Y positivo para baixo; o input comum segue o contrato PS/2,
       no qual Y positivo representa movimento para cima. */
    event.dy = -(int8_t)report[2];
    event.wheel = length >= USB_HID_MOUSE_REPORT_MAX_SIZE ?
                  (int8_t)report[3] : 0;
    event.buttons = report[0] & USB_HID_BUTTON_MASK;
    event.source = INPUT_SOURCE_USB_HID;
    result = input_publish_pointer(&event);
    if (result == ERR_OVERFLOW) {
        record->info.dropped_count++;
        return OK;
    }
    return result;
}

static void hid_deactivate(usb_hid_record_t* record, int reason);

static void hid_interrupt_callback(void* context, int result,
                                   const uint8_t* data, uint16_t length) {
    usb_hid_record_t* record = (usb_hid_record_t*)context;
    int report_result = OK;

    if (!record || !record->info.active) return;
    if (result != OK) {
        if (result == ERR_TIMEOUT) {
            /* Timeout de polling e' transitivo; o UHCI rearma a requisicao. */
            record->info.timeout_count++;
            record->info.last_error = result;
            return;
        }
        record->info.error_count++;
        record->info.last_error = result;
        record->info.state = USB_HID_STATE_DEGRADED;
        if (result == ERR_NOT_FOUND || result == ERR_UNAVAILABLE) {
            hid_deactivate(record, result);
        }
        return;
    }
    record->info.report_count++;
    if (record->info.kind == USB_HID_KIND_KEYBOARD) {
        report_result = hid_process_keyboard(record, data, length);
    } else {
        report_result = hid_process_mouse(record, data, length);
    }
    if (report_result == ERR_INVALID) {
        record->info.state = USB_HID_STATE_DEGRADED;
        return;
    }
    if (report_result != OK) {
        record->info.error_count++;
        record->info.last_error = report_result;
        record->info.state = USB_HID_STATE_DEGRADED;
        return;
    }
    record->info.state = USB_HID_STATE_READY;
    record->info.last_error = OK;
}

static void hid_deactivate(usb_hid_record_t* record, int reason) {
    int result;

    if (!record) return;
    if (record->info.active) {
        result = uhci_interrupt_cancel(&record->device,
                                       record->info.interrupt_endpoint);
        if (result == OK) record->info.cancel_count++;
    }
    record->info.active = 0U;
    record->info.state = USB_HID_STATE_DISABLED;
    record->info.last_error = reason;
    record->previous_modifiers = 0U;
    kmemset(record->previous_keys, 0, sizeof(record->previous_keys));
}

static int hid_activate(usb_hid_record_t* record) {
    int result;

    if (!record) return ERR_NULL;
    record->info.active = 0U;
    record->info.state = USB_HID_STATE_DEGRADED;
    result = hid_send_boot_requests(record);
    if (result != OK) {
        record->info.last_error = result;
        record->info.error_count++;
        return result;
    }
    result = uhci_interrupt_submit(
        &record->device, record->info.interrupt_endpoint,
        record->info.max_packet, record->info.interval,
        hid_interrupt_callback, record);
    if (result != OK) {
        record->info.last_error = result;
        record->info.error_count++;
        LOG_ERROR("HID", "Falha ao submeter relatorio Interrupt IN");
        return result;
    }
    record->info.active = 1U;
    record->info.state = USB_HID_STATE_READY;
    record->info.last_error = OK;
    return OK;
}

static int hid_prepare_record(usb_hid_record_t* record,
                              const usb_device_info_t* device) {
    if (!record || !device) {
        LOG_ERROR("HID", "Registro ou dispositivo nulo no preparo HID");
        return ERR_NULL;
    }
    hid_update_device(record, device);
    record->seen_refresh = 1U;
    return OK;
}

int usb_hid_init(void) {
    int result;

    LOG_INFO("HID", "Inicializando driver USB HID Boot");
    kmemset(usb_hid_records, 0, sizeof(usb_hid_records));
    usb_hid_count = 0U;
    usb_hid_initialized = 1U;
    result = usb_hid_refresh();
    if (result != OK) {
        LOG_WARN("HID", "Driver USB HID inicializado em modo degradado");
        return result;
    }
    LOG_INFO("HID", "Driver USB HID Boot inicializado com sucesso");
    return OK;
}

int usb_hid_refresh(void) {
    uint32_t device_count = 0U;
    int first_error = OK;

    if (!usb_hid_initialized) {
        LOG_ERROR("HID", "Atualizacao HID antes da inicializacao");
        return ERR_STATE;
    }
    if (usb_manager_get_device_count(&device_count) != OK) {
        LOG_ERROR("HID", "Falha ao consultar dispositivos para HID");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < usb_hid_count; index++) {
        usb_hid_records[index].seen_refresh = 0U;
    }
    for (uint32_t index = 0U; index < device_count; index++) {
        usb_device_info_t device;
        usb_hid_record_t* record;
        int record_index;
        int result;

        if (usb_manager_get_device(index, &device) != OK ||
            !hid_is_candidate(&device)) continue;
        record_index = hid_record_index(device.id);
        if (record_index >= 0) {
            record = &usb_hid_records[record_index];
            if (record->info.active &&
                (record->info.kind != hid_kind_from_device(&device) ||
                 record->info.interrupt_endpoint !=
                 device.interrupt_in_endpoint ||
                 record->info.max_packet != device.interrupt_in_max_packet ||
                 record->info.interval != device.interrupt_interval)) {
                hid_deactivate(record, ERR_STATE);
            }
        } else {
            if (usb_hid_count >= USB_HID_MAX_DEVICES) {
                if (first_error == OK) first_error = ERR_OVERFLOW;
                continue;
            }
            record = &usb_hid_records[usb_hid_count++];
            kmemset(record, 0, sizeof(*record));
        }
        result = hid_prepare_record(record, &device);
        if (result != OK) {
            if (first_error == OK) first_error = result;
            continue;
        }
        if (record->info.active) continue;
        result = hid_activate(record);
        if (result != OK && first_error == OK) first_error = result;
    }
    for (uint32_t index = 0U; index < usb_hid_count; index++) {
        usb_hid_record_t* record = &usb_hid_records[index];

        if (record->seen_refresh) continue;
        if (record->info.active) hid_deactivate(record, ERR_NOT_FOUND);
        else if (record->info.id[0]) {
            record->info.state = USB_HID_STATE_DISABLED;
            record->info.last_error = ERR_NOT_FOUND;
        }
    }
    return first_error;
}

int usb_hid_get_count(uint32_t* out_count) {
    if (!out_count) {
        LOG_ERROR("HID", "Destino nulo na contagem HID");
        return ERR_NULL;
    }
    if (!usb_hid_initialized) {
        LOG_ERROR("HID", "Contagem HID antes da inicializacao");
        return ERR_STATE;
    }
    *out_count = usb_hid_count;
    return OK;
}

int usb_hid_get_at(uint32_t index, usb_hid_info_t* out_info) {
    if (!out_info) {
        LOG_ERROR("HID", "Destino nulo na consulta HID");
        return ERR_NULL;
    }
    if (!usb_hid_initialized) {
        LOG_ERROR("HID", "Consulta HID antes da inicializacao");
        return ERR_STATE;
    }
    if (index >= usb_hid_count) return ERR_INVALID;
    *out_info = usb_hid_records[index].info;
    return OK;
}

int usb_hid_find(const char* id, usb_hid_info_t* out_info) {
    int index;

    if (!id || !out_info) {
        LOG_ERROR("HID", "Argumento nulo na busca HID");
        return ERR_NULL;
    }
    if (!usb_hid_initialized) return ERR_STATE;
    index = hid_record_index(id);
    if (index < 0) return ERR_NOT_FOUND;
    *out_info = usb_hid_records[index].info;
    return OK;
}

int usb_hid_is_active(const char* id) {
    int index = hid_record_index(id);

    return usb_hid_initialized && index >= 0 &&
           usb_hid_records[index].info.active;
}

int usb_hid_validate_state(void) {
    if (!usb_hid_initialized || usb_hid_count > USB_HID_MAX_DEVICES) {
        LOG_ERROR("HID", "Estado HID invalido");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < usb_hid_count; index++) {
        usb_hid_record_t* record = &usb_hid_records[index];

        if (!record->info.id[0] || !record->info.controller_id[0] ||
            record->info.kind > USB_HID_KIND_MOUSE ||
            record->info.state > USB_HID_STATE_DEGRADED ||
            record->info.max_packet > USB_UHCI_INTERRUPT_BUFFER_SIZE ||
            (record->info.active && (!record->info.interrupt_endpoint ||
                                     !record->info.interval ||
                                     !record->device.id[0]))) {
            LOG_ERROR("HID", "Registro HID inconsistente");
            return ERR_STATE;
        }
        if (record->info.active &&
            !usb_hid_is_active(record->info.id)) {
            LOG_ERROR("HID", "Estado ativo HID nao publicado");
            return ERR_STATE;
        }
    }
    return OK;
}

const char* usb_hid_kind_name(usb_hid_kind_t kind) {
    if (kind == USB_HID_KIND_KEYBOARD) return "KEYBOARD";
    if (kind == USB_HID_KIND_MOUSE) return "MOUSE";
    return "UNKNOWN";
}

const char* usb_hid_state_name(usb_hid_state_t state) {
    if (state == USB_HID_STATE_READY) return "READY";
    if (state == USB_HID_STATE_DEGRADED) return "DEGRADED";
    return "DISABLED";
}
