#include "core/device_manager.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/string.h"
#include "core/timer.h"
#include "drivers/ac97.h"
#include "drivers/ata.h"
#include "drivers/pci.h"

#define DEVICE_HEX_DIGITS_BYTE 2U
#define DEVICE_HEX_DIGITS_WORD 4U
#define DEVICE_KEYBOARD_IRQ 1U
#define DEVICE_MOUSE_IRQ 12U
#define DEVICE_PIT_IRQ 0U
#define DEVICE_PCI_CLASS_STORAGE 0x01U
#define DEVICE_PCI_CLASS_NETWORK 0x02U
#define DEVICE_PCI_CLASS_VIDEO 0x03U
#define DEVICE_PCI_CLASS_AUDIO 0x04U
#define DEVICE_PCI_SUBCLASS_AC97 0x01U
#define DEVICE_PCI_CLASS_SYSTEM 0x06U
#define DEVICE_PCI_CLASS_SERIAL 0x0CU

static device_info_t device_entries[DEVICE_MANAGER_MAX_DEVICES];
static uint32_t device_count = 0;
static int device_manager_initialized = 0;

static void device_append_char(char* text, uint32_t capacity,
                               uint32_t* offset, char value) {
    if (!text || !offset || capacity == 0 || *offset + 1 >= capacity) return;
    text[*offset] = value;
    (*offset)++;
    text[*offset] = '\0';
}

static void device_append_text(char* text, uint32_t capacity,
                               uint32_t* offset, const char* value) {
    if (!value) return;
    while (*value) {
        device_append_char(text, capacity, offset, *value);
        value++;
    }
}

static void device_append_decimal(char* text, uint32_t capacity,
                                  uint32_t* offset, uint32_t value) {
    char digits[11];
    uint32_t count = 0;

    if (value == 0) {
        device_append_char(text, capacity, offset, '0');
        return;
    }
    while (value > 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (count > 0) device_append_char(text, capacity, offset, digits[--count]);
}

static void device_append_hex(char* text, uint32_t capacity, uint32_t* offset,
                              uint32_t value, uint32_t digits) {
    static const char hex[] = "0123456789ABCDEF";

    while (digits > 0) {
        uint32_t shift = (digits - 1U) * 4U;
        device_append_char(text, capacity, offset,
                           hex[(value >> shift) & 0x0FU]);
        digits--;
    }
}

static void device_set_text(char* destination, uint32_t capacity,
                            const char* source) {
    uint32_t offset = 0;

    if (!destination || capacity == 0) return;
    destination[0] = '\0';
    device_append_text(destination, capacity, &offset, source);
}

static device_info_t* device_add(device_kind_t kind, device_status_t status) {
    device_info_t* entry;

    if (device_count >= DEVICE_MANAGER_MAX_DEVICES) {
        LOG_ERROR("DEV", "Limite do inventario de dispositivos atingido");
        return 0;
    }
    entry = &device_entries[device_count++];
    kmemset(entry, 0, sizeof(*entry));
    entry->kind = kind;
    entry->status = status;
    entry->irq = DEVICE_IRQ_UNKNOWN;
    return entry;
}

static device_status_t device_status_from_recovery(
    recovery_component_id_t component) {
    const recovery_component_t* entry = recovery_get(component);

    if (!entry) return DEVICE_STATUS_UNKNOWN;
    if (entry->state == RECOVERY_STATE_READY) return DEVICE_STATUS_READY;
    if (entry->state == RECOVERY_STATE_DEGRADED) return DEVICE_STATUS_DEGRADED;
    if (entry->state == RECOVERY_STATE_DISABLED) return DEVICE_STATUS_DISABLED;
    return DEVICE_STATUS_UNKNOWN;
}

static const char* device_pci_class_name(uint8_t class_code,
                                         uint8_t subclass_code) {
    if (class_code == DEVICE_PCI_CLASS_STORAGE) return "Controlador de disco";
    if (class_code == DEVICE_PCI_CLASS_NETWORK) return "Controlador de rede";
    if (class_code == DEVICE_PCI_CLASS_VIDEO) return "Adaptador de video";
    if (class_code == DEVICE_PCI_CLASS_AUDIO &&
        subclass_code == DEVICE_PCI_SUBCLASS_AC97) return "Audio AC97";
    if (class_code == DEVICE_PCI_CLASS_AUDIO) return "Dispositivo de audio";
    if (class_code == DEVICE_PCI_CLASS_SYSTEM) return "Controlador de sistema";
    if (class_code == DEVICE_PCI_CLASS_SERIAL) return "Barramento serial";
    return "Dispositivo PCI";
}

static char device_ascii_upper(char value) {
    if (value >= 'a' && value <= 'z') return (char)(value - ('a' - 'A'));
    return value;
}

static int device_id_matches(const char* expected, const char* requested) {
    if (!expected || !requested) return 0;
    while (*expected && *requested) {
        if (*expected == ':' && *requested == '-') {
            expected++;
            requested++;
            continue;
        }
        if (device_ascii_upper(*expected) != device_ascii_upper(*requested)) {
            return 0;
        }
        expected++;
        requested++;
    }
    return *expected == '\0' && *requested == '\0';
}

static int device_add_pci_entry(const pci_device_t* pci) {
    device_info_t* entry;

    if (!pci) {
        LOG_ERROR("DEV", "Dispositivo PCI nulo no inventario");
        return ERR_NULL;
    }
    entry = device_add(DEVICE_KIND_PCI, DEVICE_STATUS_READY);
    if (!entry) {
        LOG_ERROR("DEV", "Falha ao adicionar dispositivo PCI ao inventario");
        return ERR_OVERFLOW;
    }
    entry->vendor_id = pci->vendor_id;
    entry->device_id = pci->device_id;
    entry->class_code = pci->class;
    entry->subclass_code = pci->subclass;
    entry->bus = pci->bus;
    entry->device = pci->device;
    entry->function = pci->function;
    entry->irq = pci->irq;
    return OK;
}

static int device_add_pci_entries(void) {
    uint8_t pci_count = 0;
    int result = pci_get_device_count(&pci_count);

    if (result != OK && result != ERR_OVERFLOW) {
        LOG_ERROR("DEV", "Falha ao consultar inventario PCI");
        return result;
    }
    for (uint8_t index = 0; index < pci_count; index++) {
        pci_device_t pci;
        int entry_result = pci_get_device_at(index, &pci);

        if (entry_result != OK) {
            LOG_ERROR("DEV", "Falha ao consultar entrada PCI");
            return entry_result;
        }
        entry_result = device_add_pci_entry(&pci);
        if (entry_result != OK) {
            LOG_ERROR("DEV", "Falha ao adicionar entrada PCI ao inventario");
            return entry_result;
        }
    }
    return result;
}

static int device_add_ata(void) {
    uint8_t added = 0;

    if (!ata_get_device()) {
        device_info_t* entry = device_add(DEVICE_KIND_ATA_PRIMARY,
                                          DEVICE_STATUS_DISABLED);
        if (!entry) {
            LOG_ERROR("DEV", "Falha ao registrar ATA indisponivel");
            return ERR_OVERFLOW;
        }
        entry->device = 0;
        return OK;
    }

    for (uint8_t slot = 0; slot < ATA_MAX_DEVICES; slot++) {
        ata_device_t ata;
        device_info_t* entry;

        if (ata_get_device_at(slot, &ata) != OK) continue;
        entry = device_add(DEVICE_KIND_ATA_PRIMARY, DEVICE_STATUS_READY);
        if (!entry) {
            LOG_ERROR("DEV", "Falha ao adicionar ATA ao inventario");
            return ERR_OVERFLOW;
        }
        entry->device = slot;
        entry->bus = ata.channel;
        entry->function = ata.slave;
        entry->irq = ata.channel ? 15U : 14U;
        entry->capacity_sectors = ata.sectors;
        added++;
    }

    if (!added) LOG_WARN("DEV", "ATA inicializado sem slot inventariavel");
    return OK;
}

static int device_add_ac97(void) {
    ac97_device_t* ac97 = ac97_get_device();
    device_info_t* entry = device_add(DEVICE_KIND_AC97,
                                      ac97 && ac97->initialized ?
                                      DEVICE_STATUS_READY : DEVICE_STATUS_DISABLED);

    if (!entry) {
        LOG_ERROR("DEV", "Falha ao adicionar AC97 ao inventario");
        return ERR_OVERFLOW;
    }
    if (ac97 && ac97->initialized) entry->irq = ac97->irq;
    return OK;
}

static int device_add_builtin_entries(void) {
    device_info_t* entry;
    int result;

    result = device_add_ata();
    if (result != OK) {
        LOG_ERROR("DEV", "Falha ao adicionar ATA ao inventario");
        return result;
    }
    result = device_add_ac97();
    if (result != OK) {
        LOG_ERROR("DEV", "Falha ao adicionar AC97 ao inventario");
        return result;
    }
    entry = device_add(DEVICE_KIND_PS2_KEYBOARD, DEVICE_STATUS_READY);
    if (!entry) {
        LOG_ERROR("DEV", "Falha ao adicionar teclado ao inventario");
        return ERR_OVERFLOW;
    }
    entry->irq = DEVICE_KEYBOARD_IRQ;
    entry = device_add(DEVICE_KIND_PS2_MOUSE, DEVICE_STATUS_READY);
    if (!entry) {
        LOG_ERROR("DEV", "Falha ao adicionar mouse ao inventario");
        return ERR_OVERFLOW;
    }
    entry->irq = DEVICE_MOUSE_IRQ;
    entry = device_add(DEVICE_KIND_PIT, DEVICE_STATUS_READY);
    if (!entry) {
        LOG_ERROR("DEV", "Falha ao adicionar PIT ao inventario");
        return ERR_OVERFLOW;
    }
    entry->irq = DEVICE_PIT_IRQ;
    entry->capacity_sectors = timer_get_frequency();
    if (!device_add(DEVICE_KIND_VGA_TEXT, DEVICE_STATUS_READY)) {
        LOG_ERROR("DEV", "Falha ao adicionar VGA ao inventario");
        return ERR_OVERFLOW;
    }
    if (!device_add(DEVICE_KIND_VESA,
                    device_status_from_recovery(RECOVERY_COMPONENT_VESA))) {
        LOG_ERROR("DEV", "Falha ao adicionar VESA ao inventario");
        return ERR_OVERFLOW;
    }
    if (!device_add(DEVICE_KIND_PC_SPEAKER, DEVICE_STATUS_READY)) {
        LOG_ERROR("DEV", "Falha ao adicionar PC Speaker ao inventario");
        return ERR_OVERFLOW;
    }
    return OK;
}

static int device_manager_build_snapshot(void) {
    int pci_result;
    int result;

    device_count = 0;
    result = device_add_builtin_entries();
    if (result != OK) {
        LOG_ERROR("DEV", "Falha ao montar dispositivos internos");
        return result;
    }
    pci_result = device_add_pci_entries();
    if (pci_result != OK && pci_result != ERR_OVERFLOW) {
        LOG_ERROR("DEV", "Falha ao montar dispositivos PCI");
        return pci_result;
    }
    return pci_result;
}

static void device_format_pci_text(const device_info_t* info,
                                   device_text_t* out_text) {
    uint32_t offset = 0;

    device_append_text(out_text->id, DEVICE_ID_SIZE, &offset, "pci-");
    device_append_hex(out_text->id, DEVICE_ID_SIZE, &offset, info->bus,
                      DEVICE_HEX_DIGITS_BYTE);
    device_append_char(out_text->id, DEVICE_ID_SIZE, &offset, ':');
    device_append_hex(out_text->id, DEVICE_ID_SIZE, &offset, info->device,
                      DEVICE_HEX_DIGITS_BYTE);
    device_append_char(out_text->id, DEVICE_ID_SIZE, &offset, '.');
    device_append_decimal(out_text->id, DEVICE_ID_SIZE, &offset, info->function);

    device_set_text(out_text->name, DEVICE_NAME_SIZE,
                    device_pci_class_name(info->class_code, info->subclass_code));
    device_set_text(out_text->type, DEVICE_TYPE_SIZE, "PCI");
    offset = 0;
    device_append_text(out_text->location, DEVICE_LOCATION_SIZE, &offset, "PCI ");
    device_append_hex(out_text->location, DEVICE_LOCATION_SIZE, &offset, info->bus,
                      DEVICE_HEX_DIGITS_BYTE);
    device_append_char(out_text->location, DEVICE_LOCATION_SIZE, &offset, ':');
    device_append_hex(out_text->location, DEVICE_LOCATION_SIZE, &offset, info->device,
                      DEVICE_HEX_DIGITS_BYTE);
    device_append_char(out_text->location, DEVICE_LOCATION_SIZE, &offset, '.');
    device_append_decimal(out_text->location, DEVICE_LOCATION_SIZE, &offset,
                          info->function);
    offset = 0;
    device_append_text(out_text->detail, DEVICE_DETAIL_SIZE, &offset, "Vendor 0x");
    device_append_hex(out_text->detail, DEVICE_DETAIL_SIZE, &offset, info->vendor_id,
                      DEVICE_HEX_DIGITS_WORD);
    device_append_text(out_text->detail, DEVICE_DETAIL_SIZE, &offset, " Device 0x");
    device_append_hex(out_text->detail, DEVICE_DETAIL_SIZE, &offset, info->device_id,
                      DEVICE_HEX_DIGITS_WORD);
}

static void device_format_ata_text(const device_info_t* info,
                                   device_text_t* out_text) {
    ata_device_t ata;
    char id[] = "ata0";
    int found = info->status == DEVICE_STATUS_READY &&
                ata_get_device_at(info->device, &ata) == OK;

    if (info->device < ATA_MAX_DEVICES) id[3] = (char)('0' + info->device);
    device_set_text(out_text->id, DEVICE_ID_SIZE, id);
    device_set_text(out_text->name, DEVICE_NAME_SIZE,
                    found ? ata.model : "ATA indisponivel");
    device_set_text(out_text->type, DEVICE_TYPE_SIZE, "Armazenamento");
    device_set_text(out_text->location, DEVICE_LOCATION_SIZE,
                    info->bus ? "ATA secundario" : "ATA primario");
    if (info->status == DEVICE_STATUS_READY && found) {
        if (ata.channel) {
            device_set_text(out_text->detail, DEVICE_DETAIL_SIZE,
                            ata.slave ? "Canal secundario slave" :
                                        "Canal secundario master");
        } else {
            device_set_text(out_text->detail, DEVICE_DETAIL_SIZE,
                            ata.slave ? "Canal primario slave" :
                                        "Canal primario master");
        }
    } else {
        device_set_text(out_text->detail, DEVICE_DETAIL_SIZE,
                        "Nenhum disco ATA detectado");
    }
}

static int device_format_builtin_text(const device_info_t* info,
                                      device_text_t* out_text) {
    if (info->kind == DEVICE_KIND_ATA_PRIMARY) {
        device_format_ata_text(info, out_text);
        return OK;
    }
    if (info->kind == DEVICE_KIND_AC97) {
        device_set_text(out_text->id, DEVICE_ID_SIZE, "ac97");
        device_set_text(out_text->name, DEVICE_NAME_SIZE, "Controlador AC97");
        device_set_text(out_text->type, DEVICE_TYPE_SIZE, "Audio");
        device_set_text(out_text->location, DEVICE_LOCATION_SIZE, "PCI audio");
        device_set_text(out_text->detail, DEVICE_DETAIL_SIZE,
                        info->status == DEVICE_STATUS_READY ?
                        "Driver de audio pronto" : "Audio AC97 indisponivel");
        return OK;
    }
    if (info->kind == DEVICE_KIND_PS2_KEYBOARD) {
        device_set_text(out_text->id, DEVICE_ID_SIZE, "ps2-keyboard");
        device_set_text(out_text->name, DEVICE_NAME_SIZE, "Teclado PS/2");
        device_set_text(out_text->type, DEVICE_TYPE_SIZE, "Entrada");
        device_set_text(out_text->location, DEVICE_LOCATION_SIZE, "PS/2 IRQ1");
    } else if (info->kind == DEVICE_KIND_PS2_MOUSE) {
        device_set_text(out_text->id, DEVICE_ID_SIZE, "ps2-mouse");
        device_set_text(out_text->name, DEVICE_NAME_SIZE, "Mouse PS/2");
        device_set_text(out_text->type, DEVICE_TYPE_SIZE, "Entrada");
        device_set_text(out_text->location, DEVICE_LOCATION_SIZE, "PS/2 IRQ12");
    } else if (info->kind == DEVICE_KIND_PIT) {
        device_set_text(out_text->id, DEVICE_ID_SIZE, "pit");
        device_set_text(out_text->name, DEVICE_NAME_SIZE, "Timer PIT");
        device_set_text(out_text->type, DEVICE_TYPE_SIZE, "Sistema");
        device_set_text(out_text->location, DEVICE_LOCATION_SIZE, "PIT IRQ0");
    } else if (info->kind == DEVICE_KIND_VGA_TEXT) {
        device_set_text(out_text->id, DEVICE_ID_SIZE, "vga-text");
        device_set_text(out_text->name, DEVICE_NAME_SIZE, "Console VGA");
        device_set_text(out_text->type, DEVICE_TYPE_SIZE, "Video");
        device_set_text(out_text->location, DEVICE_LOCATION_SIZE, "Memoria 0xB8000");
    } else if (info->kind == DEVICE_KIND_VESA) {
        device_set_text(out_text->id, DEVICE_ID_SIZE, "vesa");
        device_set_text(out_text->name, DEVICE_NAME_SIZE, "Framebuffer VESA");
        device_set_text(out_text->type, DEVICE_TYPE_SIZE, "Video");
        device_set_text(out_text->location, DEVICE_LOCATION_SIZE, "VESA BIOS");
    } else if (info->kind == DEVICE_KIND_PC_SPEAKER) {
        device_set_text(out_text->id, DEVICE_ID_SIZE, "pc-speaker");
        device_set_text(out_text->name, DEVICE_NAME_SIZE, "PC Speaker");
        device_set_text(out_text->type, DEVICE_TYPE_SIZE, "Audio");
        device_set_text(out_text->location, DEVICE_LOCATION_SIZE, "Porta 0x61");
    } else {
        LOG_ERROR("DEV", "Tipo de dispositivo interno invalido");
        return ERR_INVALID;
    }
    device_set_text(out_text->detail, DEVICE_DETAIL_SIZE,
                    info->kind == DEVICE_KIND_VESA ?
                    "Estado monitorado pelo recovery" :
                    info->kind == DEVICE_KIND_PIT ?
                    "Timer do sistema inicializado" :
                    info->kind == DEVICE_KIND_VGA_TEXT ?
                    "Console de texto inicializado" :
                    info->kind == DEVICE_KIND_PC_SPEAKER ?
                    "Driver de speaker inicializado" :
                    "Driver inicializado; presenca nao confirmada");
    return OK;
}

int device_manager_init(void) {
    int result;

    LOG_INFO("DEV", "Inicializando inventario de dispositivos");
    device_manager_initialized = 1;
    result = device_manager_build_snapshot();
    if (result != OK && result != ERR_OVERFLOW) {
        device_manager_initialized = 0;
        LOG_ERROR("DEV", "Falha ao inicializar inventario de dispositivos");
        return result;
    }
    if (result == ERR_OVERFLOW) {
        LOG_WARN("DEV", "Inventario de dispositivos parcial");
    }
    LOG_INFO("DEV", "Inventario de dispositivos inicializado com sucesso");
    return result;
}

int device_manager_refresh(void) {
    int result;

    if (!device_manager_initialized) {
        LOG_ERROR("DEV", "Atualizacao antes da inicializacao do inventario");
        return ERR_STATE;
    }
    result = device_manager_build_snapshot();
    if (result != OK && result != ERR_OVERFLOW) {
        LOG_ERROR("DEV", "Falha ao atualizar inventario de dispositivos");
        return result;
    }
    if (result == ERR_OVERFLOW) {
        LOG_WARN("DEV", "Inventario atualizado parcialmente");
    } else {
        LOG_INFO("DEV", "Inventario de dispositivos atualizado com sucesso");
    }
    return result;
}

int device_manager_get_count(uint32_t* out_count) {
    if (!out_count) {
        LOG_ERROR("DEV", "Destino nulo ao consultar contagem");
        return ERR_NULL;
    }
    if (!device_manager_initialized) {
        LOG_ERROR("DEV", "Consulta antes da inicializacao do inventario");
        return ERR_STATE;
    }
    *out_count = device_count;
    return OK;
}

int device_manager_get_info(uint32_t index, device_info_t* out_info) {
    if (!out_info) {
        LOG_ERROR("DEV", "Destino nulo ao consultar dispositivo");
        return ERR_NULL;
    }
    if (!device_manager_initialized) {
        LOG_ERROR("DEV", "Consulta antes da inicializacao do inventario");
        return ERR_STATE;
    }
    if (index >= device_count) {
        LOG_ERROR("DEV", "Indice de dispositivo invalido");
        return ERR_INVALID;
    }
    *out_info = device_entries[index];
    return OK;
}

int device_manager_format_text(const device_info_t* info, device_text_t* out_text) {
    if (!info || !out_text) {
        LOG_ERROR("DEV", "Argumento nulo ao formatar dispositivo");
        return ERR_NULL;
    }
    kmemset(out_text, 0, sizeof(*out_text));
    if (info->kind == DEVICE_KIND_PCI) {
        device_format_pci_text(info, out_text);
        return OK;
    }
    return device_format_builtin_text(info, out_text);
}

int device_manager_find(const char* id, device_info_t* out_info) {
    device_text_t text;

    if (!id || !out_info) {
        LOG_ERROR("DEV", "Argumento nulo ao buscar dispositivo");
        return ERR_NULL;
    }
    if (!device_manager_initialized) {
        LOG_ERROR("DEV", "Busca antes da inicializacao do inventario");
        return ERR_STATE;
    }
    if (device_id_matches("ata-primary", id)) {
        for (uint32_t index = 0; index < device_count; index++) {
            if (device_entries[index].kind == DEVICE_KIND_ATA_PRIMARY &&
                device_entries[index].status == DEVICE_STATUS_READY) {
                *out_info = device_entries[index];
                return OK;
            }
        }
    }
    for (uint32_t index = 0; index < device_count; index++) {
        int result = device_manager_format_text(&device_entries[index], &text);

        if (result != OK) {
            LOG_ERROR("DEV", "Falha ao formatar dispositivo durante busca");
            return result;
        }
        if (device_id_matches(text.id, id)) {
            *out_info = device_entries[index];
            return OK;
        }
    }
    LOG_WARN("DEV", "Dispositivo solicitado nao encontrado");
    return ERR_NOT_FOUND;
}

const char* device_manager_status_name(device_status_t status) {
    if (status == DEVICE_STATUS_READY) return "READY";
    if (status == DEVICE_STATUS_DEGRADED) return "DEGRADED";
    if (status == DEVICE_STATUS_DISABLED) return "DISABLED";
    return "UNKNOWN";
}
