#include "fs/sysfs.h"
#include "fs/vfs_internal.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/poll.h"
#include "core/power.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "drivers/pci.h"
#include "core/network_manager.h"
#include "fs/block.h"

#define SYSFS_PCI_ATTRIBUTE_COUNT 17U
#define SYSFS_NET_ATTRIBUTE_COUNT 42U
#define SYSFS_BLOCK_ATTRIBUTE_COUNT 13U
#define SYSFS_POWER_STATE_COUNT 6U
#define SYSFS_PATH_PREFIX_SIZE 5U
#define SYSFS_UINT32_MAX 0xFFFFFFFFU
#define SYSFS_TEST_BUFFER_SIZE 512U

typedef struct {
    sysfs_node_kind_t kind;
    char identifier[SYSFS_IDENTIFIER_SIZE];
    char attribute[SYSFS_MAX_ATTRIBUTES];
} sysfs_node_ref_t;

static const char* const sysfs_pci_attributes[SYSFS_PCI_ATTRIBUTE_COUNT] = {
    "bus", "device", "function", "vendor_id", "device_id", "class",
    "subclass", "prog_if", "revision", "irq", "bar0", "bar1", "bar2",
    "bar3", "bar4", "bar5", "present"
};

static const char* const sysfs_net_attributes[SYSFS_NET_ATTRIBUTE_COUNT] = {
    "id", "name", "driver", "transport", "model", "state", "link",
    "vendor_id", "device_id", "class", "subclass", "prog_if", "revision",
    "bus", "device", "function", "irq", "bar0", "bar1", "bar2", "bar3",
    "bar4", "bar5", "usb_device_id", "usb_port", "usb_address",
    "usb_revision", "usb_endpoint_count", "mac_address",
    "ethernet_attached", "l3_active", "dhcp_pending", "rx_packets",
    "tx_packets", "rx_errors", "tx_errors", "rx_dropped", "rx_queue_depth",
    "rx_queue_high_water", "rx_queue_dropped", "rx_interrupts",
    "driver_error"
};

static const char* const sysfs_block_attributes[SYSFS_BLOCK_ATTRIBUTE_COUNT] = {
    "id", "model", "provider", "sector_count", "capacity_bytes", "sector_size",
    "read_only", "online", "read_ops", "write_ops", "max_transfer_sectors",
    "capabilities", "last_error"
};

static const char* const sysfs_power_states[SYSFS_POWER_STATE_COUNT] = {
    "S0", "S1", "S2", "S3", "S4", "S5"
};

static const file_operations_t sysfs_operations;
static spinlock_t sysfs_lock;
static uint8_t sysfs_ready;
static uint32_t sysfs_inventory_generation;
static uint32_t sysfs_active_snapshots;

static void sysfs_test_count(sysfs_test_result_t* result, uint8_t passed) {
    result->total++;
    if (passed) result->passed++;
}

static int sysfs_path_equal(const char* left, const char* right) {
    return left && right && kstrcmp(left, right) == 0;
}

static int sysfs_path_starts(const char* path, const char* prefix) {
    uint32_t index = 0U;

    if (!path || !prefix) return 0;
    while (prefix[index]) {
        if (path[index] != prefix[index]) return 0;
        index++;
    }
    return 1;
}

static void sysfs_copy_text(char* destination, uint32_t capacity,
                            const char* source) {
    uint32_t index = 0U;

    if (!destination || !capacity) return;
    if (!source) source = "";
    while (source[index] && index + 1U < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int sysfs_append_char(char* buffer, uint32_t capacity,
                             uint32_t* length, char value) {
    if (!buffer || !length) {
        LOG_ERROR("SYSFS", "Destino nulo ao serializar atributo");
        return ERR_NULL;
    }
    if (*length >= capacity) {
        LOG_WARN("SYSFS", "Snapshot sysfs excedeu a capacidade");
        return ERR_OVERFLOW;
    }
    buffer[*length] = value;
    (*length)++;
    return OK;
}

static int sysfs_append_text(char* buffer, uint32_t capacity,
                             uint32_t* length, const char* text) {
    int result;

    if (!text) {
        LOG_ERROR("SYSFS", "Texto nulo ao serializar atributo");
        return ERR_NULL;
    }
    while (*text) {
        result = sysfs_append_char(buffer, capacity, length, *text);
        if (result != OK) return result;
        text++;
    }
    return OK;
}

static int sysfs_append_decimal(char* buffer, uint32_t capacity,
                                uint32_t* length, uint32_t value) {
    char digits[10];
    uint32_t count = 0U;
    int result;

    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value && count < sizeof(digits));
    while (count) {
        count--;
        result = sysfs_append_char(buffer, capacity, length, digits[count]);
        if (result != OK) return result;
    }
    return OK;
}

static int sysfs_append_signed(char* buffer, uint32_t capacity,
                               uint32_t* length, int32_t value) {
    uint32_t magnitude;
    int result;

    if (value < 0) {
        result = sysfs_append_char(buffer, capacity, length, '-');
        if (result != OK) return result;
        magnitude = (uint32_t)(-(value + 1)) + 1U;
    } else {
        magnitude = (uint32_t)value;
    }
    return sysfs_append_decimal(buffer, capacity, length, magnitude);
}

static int sysfs_append_hex_width(char* buffer, uint32_t capacity,
                                  uint32_t* length, uint32_t value,
                                  uint32_t digits) {
    static const char hex[] = "0123456789abcdef";
    int result = sysfs_append_text(buffer, capacity, length, "0x");

    for (int shift = (int)((digits - 1U) * 4U);
         result == OK && shift >= 0; shift -= 4) {
        result = sysfs_append_char(buffer, capacity, length,
                                   hex[(value >> (uint32_t)shift) & 0x0FU]);
    }
    return result;
}

static int sysfs_append_hex_digits(char* buffer, uint32_t capacity,
                                   uint32_t* length, uint32_t value,
                                   uint32_t digits) {
    static const char hex[] = "0123456789abcdef";
    int result = OK;

    for (int shift = (int)((digits - 1U) * 4U);
         result == OK && shift >= 0; shift -= 4) {
        result = sysfs_append_char(buffer, capacity, length,
                                   hex[(value >> (uint32_t)shift) & 0x0FU]);
    }
    return result;
}

static int sysfs_append_line_decimal(char* buffer, uint32_t capacity,
                                     uint32_t* length, const char* key,
                                     uint32_t value) {
    int result = sysfs_append_text(buffer, capacity, length, key);

    if (result == OK) result = sysfs_append_char(buffer, capacity, length, ' ');
    if (result == OK) result = sysfs_append_decimal(buffer, capacity, length,
                                                     value);
    if (result == OK) result = sysfs_append_char(buffer, capacity, length, '\n');
    return result;
}

static int sysfs_append_line_signed(char* buffer, uint32_t capacity,
                                    uint32_t* length, const char* key,
                                    int32_t value) {
    int result = sysfs_append_text(buffer, capacity, length, key);

    if (result == OK) result = sysfs_append_char(buffer, capacity, length, ' ');
    if (result == OK) result = sysfs_append_signed(buffer, capacity, length,
                                                    value);
    if (result == OK) result = sysfs_append_char(buffer, capacity, length, '\n');
    return result;
}

static int sysfs_append_line_hex(char* buffer, uint32_t capacity,
                                 uint32_t* length, const char* key,
                                 uint32_t value, uint32_t digits) {
    int result = sysfs_append_text(buffer, capacity, length, key);

    if (result == OK) result = sysfs_append_char(buffer, capacity, length, ' ');
    if (result == OK) result = sysfs_append_hex_width(buffer, capacity, length,
                                                       value, digits);
    if (result == OK) result = sysfs_append_char(buffer, capacity, length, '\n');
    return result;
}

static int sysfs_append_line_text(char* buffer, uint32_t capacity,
                                  uint32_t* length, const char* key,
                                  const char* value) {
    int result = sysfs_append_text(buffer, capacity, length, key);

    if (result == OK) result = sysfs_append_char(buffer, capacity, length, ' ');
    if (result == OK && value) {
        for (uint32_t index = 0U; value[index] && result == OK; index++) {
            uint8_t character = (uint8_t)value[index];

            if (character < 0x20U || character > 0x7EU) character = '?';
            result = sysfs_append_char(buffer, capacity, length,
                                       (char)character);
        }
    }
    if (result == OK) result = sysfs_append_char(buffer, capacity, length, '\n');
    return result;
}

static int sysfs_append_line_bool(char* buffer, uint32_t capacity,
                                  uint32_t* length, const char* key,
                                  uint8_t value) {
    return sysfs_append_line_text(buffer, capacity, length, key,
                                  value ? "true" : "false");
}

static int sysfs_append_mac(char* buffer, uint32_t capacity, uint32_t* length,
                            const char* key, const uint8_t* mac) {
    int result = sysfs_append_text(buffer, capacity, length, key);

    if (result == OK) result = sysfs_append_char(buffer, capacity, length, ' ');
    if (result == OK) result = sysfs_append_text(buffer, capacity, length, "0x");
    for (uint32_t index = 0U; result == OK && index < NETWORK_MAC_ADDRESS_SIZE;
         index++) {
        result = sysfs_append_hex_digits(buffer, capacity, length, mac[index], 2U);
    }
    if (result == OK) result = sysfs_append_char(buffer, capacity, length, '\n');
    return result;
}

static int sysfs_format_pci_id(char* output, uint32_t capacity,
                               const pci_device_t* device) {
    uint32_t length = 0U;
    static const char hex[] = "0123456789abcdef";

    if (!output || !device || capacity < SYSFS_PCI_ID_SIZE) {
        LOG_WARN("SYSFS", "Capacidade insuficiente para identificador PCI");
        return ERR_OVERFLOW;
    }
    output[length++] = hex[(device->bus >> 4U) & 0x0FU];
    output[length++] = hex[device->bus & 0x0FU];
    output[length++] = ':';
    output[length++] = hex[(device->device >> 4U) & 0x0FU];
    output[length++] = hex[device->device & 0x0FU];
    output[length++] = '.';
    output[length++] = hex[device->function & 0x0FU];
    output[length] = '\0';
    return OK;
}

static int sysfs_attribute_index(const char* name, const char* const* names,
                                 uint32_t count) {
    if (!name || !names) return -1;
    for (uint32_t index = 0U; index < count; index++) {
        if (sysfs_path_equal(name, names[index])) return (int)index;
    }
    return -1;
}

static int sysfs_pci_copy(const char* identifier, pci_device_t* output) {
    uint8_t count = 0U;
    int result;

    if (!identifier || !output) {
        LOG_ERROR("SYSFS", "Argumento nulo na copia de dispositivo PCI");
        return ERR_NULL;
    }
    result = pci_get_device_count(&count);
    if (result != OK && result != ERR_OVERFLOW) return ERR_NOT_FOUND;
    for (uint8_t index = 0U; index < count; index++) {
        pci_device_t device;

        if (pci_get_device_at(index, &device) != OK || !device.present) continue;
        {
            char current[SYSFS_PCI_ID_SIZE];

            if (sysfs_format_pci_id(current, sizeof(current), &device) == OK &&
                sysfs_path_equal(current, identifier)) {
                *output = device;
                return OK;
            }
        }
    }
    LOG_WARN("SYSFS", "Dispositivo PCI ausente no inventario");
    return ERR_NOT_FOUND;
}

static int sysfs_network_copy(const char* identifier,
                              network_interface_info_t* output,
                              network_interface_text_t* output_text) {
    uint32_t count = 0U;
    int result;

    if (!identifier || !output) {
        LOG_ERROR("SYSFS", "Argumento nulo na copia de interface de rede");
        return ERR_NULL;
    }
    result = network_manager_get_count(&count);
    if (result != OK) return ERR_NOT_FOUND;
    for (uint32_t index = 0U; index < count; index++) {
        network_interface_info_t info;
        network_interface_text_t text;

        if (network_manager_get_interface(index, &info) != OK) continue;
        if (network_manager_format_text(&info, &text) != OK) continue;
        if (sysfs_path_equal(text.id, identifier)) {
            *output = info;
            if (output_text) *output_text = text;
            return OK;
        }
    }
    LOG_WARN("SYSFS", "Interface de rede ausente no inventario");
    return ERR_NOT_FOUND;
}

static int sysfs_block_copy(const char* identifier, block_device_t* output) {
    uint32_t count = 0U;
    int result;

    if (!identifier || !output) {
        LOG_ERROR("SYSFS", "Argumento nulo na copia de bloco");
        return ERR_NULL;
    }
    result = block_get_count(&count);
    if (result != OK) return ERR_NOT_FOUND;
    for (uint32_t index = 0U; index < count; index++) {
        block_device_t device;

        if (block_get_at(index, &device) != OK) continue;
        if (sysfs_path_equal(device.id, identifier)) {
            *output = device;
            return OK;
        }
    }
    LOG_WARN("SYSFS", "Dispositivo de bloco ausente no inventario");
    return ERR_NOT_FOUND;
}

static int sysfs_device_exists(sysfs_node_kind_t kind, const char* identifier) {
    pci_device_t pci;
    network_interface_info_t network;
    block_device_t block;

    if (kind == SYSFS_NODE_PCI_DEVICE) return sysfs_pci_copy(identifier, &pci);
    if (kind == SYSFS_NODE_NET_DEVICE) {
        return sysfs_network_copy(identifier, &network, 0);
    }
    if (kind == SYSFS_NODE_BLOCK_DEVICE) {
        return sysfs_block_copy(identifier, &block);
    }
    LOG_ERROR("SYSFS", "Tipo de dispositivo sysfs invalido");
    return ERR_INVALID;
}

static int sysfs_parse_dynamic(const char* canonical_path, const char* prefix,
                               sysfs_node_kind_t node_kind,
                               sysfs_node_kind_t attribute_kind,
                               const char* const* attributes, uint32_t count,
                               sysfs_node_ref_t* node) {
    const char* suffix;
    const char* separator = 0;
    uint32_t length;

    if (!sysfs_path_starts(canonical_path, prefix)) return ERR_NOT_FOUND;
    suffix = canonical_path + kstrlen(prefix);
    if (!*suffix) return ERR_INVALID;
    for (uint32_t index = 0U; suffix[index]; index++) {
        if (suffix[index] == '/') {
            separator = suffix + index;
            break;
        }
    }
    length = separator ? (uint32_t)(separator - suffix) : kstrlen(suffix);
    if (!length || length >= sizeof(node->identifier)) return ERR_INVALID;
    for (uint32_t index = 0U; index < length; index++) {
        if (suffix[index] == '\\') return ERR_INVALID;
        node->identifier[index] = suffix[index];
    }
    node->identifier[length] = '\0';
    if (sysfs_device_exists(node_kind, node->identifier) != OK) {
        LOG_WARN("SYSFS", "Atributo sysfs ausente no dispositivo");
        return ERR_NOT_FOUND;
    }
    if (!separator) {
        node->kind = node_kind;
        return OK;
    }
    if (!separator[1] || separator[1] == '/') return ERR_INVALID;
    if (kstrlen(separator + 1U) >= sizeof(node->attribute) ||
        sysfs_attribute_index(separator + 1U, attributes, count) < 0) {
        return ERR_NOT_FOUND;
    }
    node->kind = attribute_kind;
    sysfs_copy_text(node->attribute, sizeof(node->attribute), separator + 1U);
    return OK;
}

static int sysfs_parse_path(const char* canonical_path, sysfs_node_ref_t* node) {
    static const char* const pci_prefix = "/sys/bus/pci/devices/";
    static const char* const net_prefix = "/sys/class/net/";
    static const char* const block_prefix = "/sys/class/block/";
    int result;

    if (!canonical_path || !node) {
        LOG_ERROR("SYSFS", "Argumento nulo ao analisar caminho sysfs");
        return ERR_NULL;
    }
    if (!sysfs_path_starts(canonical_path, "/sys") ||
        (canonical_path[4] && canonical_path[4] != '/')) {
        LOG_WARN("SYSFS", "Caminho fora do namespace sysfs");
        return ERR_INVALID;
    }
    kmemset(node, 0, sizeof(*node));
    if (sysfs_path_equal(canonical_path, "/sys")) {
        node->kind = SYSFS_NODE_ROOT;
        return OK;
    }
    if (sysfs_path_equal(canonical_path, "/sys/bus")) {
        node->kind = SYSFS_NODE_BUS;
        return OK;
    }
    if (sysfs_path_equal(canonical_path, "/sys/bus/pci")) {
        node->kind = SYSFS_NODE_BUS_PCI;
        return OK;
    }
    if (sysfs_path_equal(canonical_path, "/sys/bus/pci/devices")) {
        node->kind = SYSFS_NODE_PCI_DEVICES;
        return OK;
    }
    if (sysfs_path_equal(canonical_path, "/sys/class")) {
        node->kind = SYSFS_NODE_CLASS;
        return OK;
    }
    if (sysfs_path_equal(canonical_path, "/sys/class/net")) {
        node->kind = SYSFS_NODE_CLASS_NET;
        return OK;
    }
    if (sysfs_path_equal(canonical_path, "/sys/class/block")) {
        node->kind = SYSFS_NODE_CLASS_BLOCK;
        return OK;
    }
    if (sysfs_path_equal(canonical_path, "/sys/power")) {
        node->kind = SYSFS_NODE_POWER;
        return OK;
    }
    if (sysfs_path_equal(canonical_path, "/sys/power/state")) {
        node->kind = SYSFS_NODE_POWER_STATE;
        return OK;
    }
    result = sysfs_parse_dynamic(canonical_path, pci_prefix,
                                  SYSFS_NODE_PCI_DEVICE,
                                  SYSFS_NODE_PCI_ATTRIBUTE,
                                  sysfs_pci_attributes,
                                  SYSFS_PCI_ATTRIBUTE_COUNT, node);
    if (result != ERR_NOT_FOUND) return result;
    result = sysfs_parse_dynamic(canonical_path, net_prefix,
                                  SYSFS_NODE_NET_DEVICE,
                                  SYSFS_NODE_NET_ATTRIBUTE,
                                  sysfs_net_attributes,
                                  SYSFS_NET_ATTRIBUTE_COUNT, node);
    if (result != ERR_NOT_FOUND) return result;
    return sysfs_parse_dynamic(canonical_path, block_prefix,
                               SYSFS_NODE_BLOCK_DEVICE,
                               SYSFS_NODE_BLOCK_ATTRIBUTE,
                               sysfs_block_attributes,
                               SYSFS_BLOCK_ATTRIBUTE_COUNT, node);
}

static int sysfs_render_pci(char* buffer, uint32_t capacity,
                            uint32_t* length, const sysfs_node_ref_t* node) {
    pci_device_t device;
    int attribute;
    int result;

    result = sysfs_pci_copy(node->identifier, &device);
    if (result != OK) return result;
    attribute = sysfs_attribute_index(node->attribute, sysfs_pci_attributes,
                                      SYSFS_PCI_ATTRIBUTE_COUNT);
    if (attribute < 0 || (uint32_t)attribute >= SYSFS_PCI_ATTRIBUTE_COUNT) {
        LOG_ERROR("SYSFS", "Atributo PCI sysfs invalido");
        return ERR_INVALID;
    }
    if (attribute == 0) return sysfs_append_line_hex(buffer, capacity, length,
                                                      "bus", device.bus, 2U);
    if (attribute == 1) return sysfs_append_line_hex(buffer, capacity, length,
                                                      "device", device.device, 2U);
    if (attribute == 2) return sysfs_append_line_hex(buffer, capacity, length,
                                                      "function", device.function, 1U);
    if (attribute == 3) return sysfs_append_line_hex(buffer, capacity, length,
                                                      "vendor_id", device.vendor_id, 4U);
    if (attribute == 4) return sysfs_append_line_hex(buffer, capacity, length,
                                                      "device_id", device.device_id, 4U);
    if (attribute == 5) return sysfs_append_line_hex(buffer, capacity, length,
                                                      "class", device.class, 2U);
    if (attribute == 6) return sysfs_append_line_hex(buffer, capacity, length,
                                                      "subclass", device.subclass, 2U);
    if (attribute == 7) return sysfs_append_line_hex(buffer, capacity, length,
                                                      "prog_if", device.prog_if, 2U);
    if (attribute == 8) return sysfs_append_line_hex(buffer, capacity, length,
                                                      "revision", device.revision, 2U);
    if (attribute == 9) return sysfs_append_line_decimal(buffer, capacity,
                                                          length, "irq", device.irq);
    if (attribute >= 10 && attribute <= 15) {
        static const char* const bars[] = {
            "bar0", "bar1", "bar2", "bar3", "bar4", "bar5"
        };
        const uint32_t values[] = {device.bar0, device.bar1, device.bar2,
                                    device.bar3, device.bar4, device.bar5};

        return sysfs_append_line_hex(buffer, capacity, length,
                                     bars[attribute - 10],
                                     values[attribute - 10], 8U);
    }
    return sysfs_append_line_bool(buffer, capacity, length, "present",
                                  device.present);
}

static const char* sysfs_network_model(network_adapter_model_t model) {
    if (model == NETWORK_ADAPTER_E1000) return "e1000";
    if (model == NETWORK_ADAPTER_RTL8139) return "rtl8139";
    if (model == NETWORK_ADAPTER_RTL8811CU) return "rtl8811cu";
    return "unknown";
}

static const char* sysfs_network_state(network_interface_state_t state) {
    if (state == NETWORK_INTERFACE_DRIVER_MISSING) return "missing";
    if (state == NETWORK_INTERFACE_UNSUPPORTED) return "unsupported";
    if (state == NETWORK_INTERFACE_ACTIVE) return "active";
    if (state == NETWORK_INTERFACE_DRIVER_ERROR) return "error";
    return "unknown";
}

static const char* sysfs_network_link(network_link_state_t state) {
    if (state == NETWORK_LINK_DOWN) return "down";
    if (state == NETWORK_LINK_UP) return "up";
    return "unknown";
}

static int sysfs_render_network(char* buffer, uint32_t capacity,
                                uint32_t* length,
                                const sysfs_node_ref_t* node) {
    network_interface_info_t info;
    network_interface_text_t text;
    int attribute;
    int result;

    result = sysfs_network_copy(node->identifier, &info, &text);
    if (result != OK) return result;
    attribute = sysfs_attribute_index(node->attribute, sysfs_net_attributes,
                                      SYSFS_NET_ATTRIBUTE_COUNT);
    if (attribute < 0 || (uint32_t)attribute >= SYSFS_NET_ATTRIBUTE_COUNT) {
        LOG_ERROR("SYSFS", "Atributo de rede sysfs invalido");
        return ERR_INVALID;
    }
    if (attribute == 0) return sysfs_append_line_text(buffer, capacity, length,
                                                       "id", text.id);
    if (attribute == 1) return sysfs_append_line_text(buffer, capacity, length,
                                                       "name", text.name);
    if (attribute == 2) return sysfs_append_line_text(buffer, capacity, length,
                                                       "driver", text.driver);
    if (attribute == 3) return sysfs_append_line_text(buffer, capacity, length,
                                                       "transport", info.transport == NETWORK_TRANSPORT_USB ? "usb" : "pci");
    if (attribute == 4) return sysfs_append_line_text(buffer, capacity, length,
                                                       "model", sysfs_network_model(info.model));
    if (attribute == 5) return sysfs_append_line_text(buffer, capacity, length,
                                                       "state", sysfs_network_state(info.state));
    if (attribute == 6) return sysfs_append_line_text(buffer, capacity, length,
                                                       "link", sysfs_network_link(info.link));
    if (attribute == 7) return sysfs_append_line_hex(buffer, capacity, length,
                                                      "vendor_id", info.vendor_id, 4U);
    if (attribute == 8) return sysfs_append_line_hex(buffer, capacity, length,
                                                      "device_id", info.device_id, 4U);
    if (attribute == 9) return sysfs_append_line_hex(buffer, capacity, length,
                                                      "class", info.class_code, 2U);
    if (attribute == 10) return sysfs_append_line_hex(buffer, capacity, length,
                                                       "subclass", info.subclass_code, 2U);
    if (attribute == 11) return sysfs_append_line_hex(buffer, capacity, length,
                                                       "prog_if", info.prog_if, 2U);
    if (attribute == 12) return sysfs_append_line_hex(buffer, capacity, length,
                                                       "revision", info.revision, 2U);
    if (attribute == 13) return sysfs_append_line_hex(buffer, capacity, length,
                                                       "bus", info.bus, 2U);
    if (attribute == 14) return sysfs_append_line_hex(buffer, capacity, length,
                                                       "device", info.device, 2U);
    if (attribute == 15) return sysfs_append_line_hex(buffer, capacity, length,
                                                       "function", info.function, 1U);
    if (attribute == 16) return sysfs_append_line_decimal(buffer, capacity,
                                                           length, "irq", info.irq);
    if (attribute >= 17 && attribute <= 22) {
        static const char* const bars[] = {
            "bar0", "bar1", "bar2", "bar3", "bar4", "bar5"
        };

        return sysfs_append_line_hex(buffer, capacity, length,
                                     bars[attribute - 17],
                                     info.bars[attribute - 17], 8U);
    }
    if (attribute == 23) return sysfs_append_line_text(
        buffer, capacity, length, "usb_device_id",
        info.usb_device_id[0] ? info.usb_device_id : "none");
    if (attribute == 24) return sysfs_append_line_decimal(buffer, capacity,
                                                           length, "usb_port", info.usb_port);
    if (attribute == 25) return sysfs_append_line_decimal(buffer, capacity,
                                                           length, "usb_address", info.usb_address);
    if (attribute == 26) return sysfs_append_line_hex(buffer, capacity, length,
                                                       "usb_revision", info.usb_revision, 4U);
    if (attribute == 27) return sysfs_append_line_decimal(buffer, capacity,
                                                           length, "usb_endpoint_count", info.usb_endpoint_count);
    if (attribute == 28) return sysfs_append_mac(buffer, capacity, length,
                                                  "mac_address", info.mac_address);
    if (attribute == 29) return sysfs_append_line_bool(buffer, capacity, length,
                                                        "ethernet_attached", info.ethernet_attached);
    if (attribute == 30) return sysfs_append_line_bool(buffer, capacity, length,
                                                        "l3_active", info.l3_active);
    if (attribute == 31) return sysfs_append_line_bool(buffer, capacity, length,
                                                        "dhcp_pending", info.dhcp_pending);
    if (attribute == 32) return sysfs_append_line_decimal(buffer, capacity,
                                                           length, "rx_packets", info.rx_packets);
    if (attribute == 33) return sysfs_append_line_decimal(buffer, capacity,
                                                           length, "tx_packets", info.tx_packets);
    if (attribute == 34) return sysfs_append_line_decimal(buffer, capacity,
                                                           length, "rx_errors", info.rx_errors);
    if (attribute == 35) return sysfs_append_line_decimal(buffer, capacity,
                                                           length, "tx_errors", info.tx_errors);
    if (attribute == 36) return sysfs_append_line_decimal(buffer, capacity,
                                                           length, "rx_dropped", info.rx_dropped);
    if (attribute == 37) return sysfs_append_line_decimal(buffer, capacity,
                                                           length, "rx_queue_depth", info.rx_queue_depth);
    if (attribute == 38) return sysfs_append_line_decimal(buffer, capacity,
                                                           length, "rx_queue_high_water", info.rx_queue_high_water);
    if (attribute == 39) return sysfs_append_line_decimal(buffer, capacity,
                                                           length, "rx_queue_dropped", info.rx_queue_dropped);
    if (attribute == 40) return sysfs_append_line_decimal(buffer, capacity,
                                                           length, "rx_interrupts", info.rx_interrupts);
    return sysfs_append_line_signed(buffer, capacity, length, "driver_error",
                                    info.driver_error);
}

static const char* sysfs_block_provider(block_provider_t provider) {
    if (provider == BLOCK_PROVIDER_ATA) return "ata";
    if (provider == BLOCK_PROVIDER_USB_MSC) return "usb_msc";
    return "unknown";
}

static int sysfs_render_block(char* buffer, uint32_t capacity,
                              uint32_t* length,
                              const sysfs_node_ref_t* node) {
    block_device_t device;
    uint32_t capacity_bytes;
    int attribute;
    int result;

    result = sysfs_block_copy(node->identifier, &device);
    if (result != OK) return result;
    if (device.sector_size &&
        device.sector_count > SYSFS_UINT32_MAX / device.sector_size) {
        LOG_ERROR("SYSFS", "Capacidade de bloco excedeu o formato textual");
        return ERR_OVERFLOW;
    }
    capacity_bytes = device.sector_count * device.sector_size;
    attribute = sysfs_attribute_index(node->attribute, sysfs_block_attributes,
                                      SYSFS_BLOCK_ATTRIBUTE_COUNT);
    if (attribute < 0 || (uint32_t)attribute >= SYSFS_BLOCK_ATTRIBUTE_COUNT) {
        LOG_ERROR("SYSFS", "Atributo de bloco sysfs invalido");
        return ERR_INVALID;
    }
    if (attribute == 0) return sysfs_append_line_text(buffer, capacity, length,
                                                       "id", device.id);
    if (attribute == 1) return sysfs_append_line_text(buffer, capacity, length,
                                                       "model", device.model);
    if (attribute == 2) return sysfs_append_line_text(buffer, capacity, length,
                                                       "provider", sysfs_block_provider(device.provider));
    if (attribute == 3) return sysfs_append_line_decimal(buffer, capacity,
                                                          length, "sector_count", device.sector_count);
    if (attribute == 4) return sysfs_append_line_decimal(buffer, capacity,
                                                          length, "capacity_bytes", capacity_bytes);
    if (attribute == 5) return sysfs_append_line_decimal(buffer, capacity,
                                                          length, "sector_size", device.sector_size);
    if (attribute == 6) return sysfs_append_line_bool(buffer, capacity, length,
                                                      "read_only", device.read_only);
    if (attribute == 7) return sysfs_append_line_bool(buffer, capacity, length,
                                                      "online", device.online);
    if (attribute == 8) return sysfs_append_line_decimal(buffer, capacity,
                                                          length, "read_ops", device.read_ops);
    if (attribute == 9) return sysfs_append_line_decimal(buffer, capacity,
                                                          length, "write_ops", device.write_ops);
    if (attribute == 10) return sysfs_append_line_decimal(buffer, capacity,
                                                          length, "max_transfer_sectors", device.max_transfer_sectors);
    if (attribute == 11) return sysfs_append_line_hex(buffer, capacity, length,
                                                      "capabilities", device.capabilities, 8U);
    return sysfs_append_line_signed(buffer, capacity, length, "last_error",
                                    device.last_error);
}

static const char* sysfs_power_capability(power_capability_t capability) {
    if (capability == POWER_CAPABILITY_AVAILABLE) return "available";
    if (capability == POWER_CAPABILITY_SIMULATED) return "simulated";
    return "unavailable";
}

static void sysfs_power_default(power_status_t* status) {
    kmemset(status, 0, sizeof(*status));
    for (uint32_t index = 0U; index < POWER_STATE_COUNT; index++) {
        status->states[index] = POWER_CAPABILITY_UNAVAILABLE;
    }
    status->cpu_idle = POWER_CAPABILITY_UNAVAILABLE;
    status->hardware_poweroff = POWER_CAPABILITY_UNAVAILABLE;
    status->reboot = POWER_CAPABILITY_UNAVAILABLE;
}

static int sysfs_render_power(char* buffer, uint32_t capacity,
                              uint32_t* length) {
    power_status_t status;
    int result = power_get_status(&status);

    if (result != OK) {
        sysfs_power_default(&status);
        LOG_WARN("SYSFS", "Estado de energia indisponivel; publicando fallback");
        result = OK;
    }
    for (uint32_t index = 0U; index < POWER_STATE_COUNT && result == OK;
         index++) {
        result = sysfs_append_text(buffer, capacity, length, "state ");
        if (result == OK) result = sysfs_append_text(buffer, capacity, length,
                                                      sysfs_power_states[index]);
        if (result == OK) result = sysfs_append_char(buffer, capacity, length, ' ');
        if (result == OK) result = sysfs_append_text(
            buffer, capacity, length,
            sysfs_power_capability(status.states[index]));
        if (result == OK) result = sysfs_append_char(buffer, capacity, length, '\n');
    }
    if (result == OK) result = sysfs_append_line_text(
        buffer, capacity, length, "cpu_idle",
        sysfs_power_capability(status.cpu_idle));
    if (result == OK) result = sysfs_append_line_text(
        buffer, capacity, length, "hardware_poweroff",
        sysfs_power_capability(status.hardware_poweroff));
    if (result == OK) result = sysfs_append_line_text(
        buffer, capacity, length, "reboot",
        sysfs_power_capability(status.reboot));
    return result;
}

static int sysfs_render_snapshot(const sysfs_node_ref_t* node,
                                 sysfs_file_context_t* context) {
    uint32_t length = 0U;
    int result;

    if (!node || !context || !context->snapshot) {
        LOG_ERROR("SYSFS", "Contexto nulo ao gerar snapshot sysfs");
        return ERR_NULL;
    }
    if (node->kind == SYSFS_NODE_PCI_ATTRIBUTE) {
        result = sysfs_render_pci((char*)context->snapshot,
                                  PROCFS_MAX_SNAPSHOT_SIZE, &length, node);
    } else if (node->kind == SYSFS_NODE_NET_ATTRIBUTE) {
        result = sysfs_render_network((char*)context->snapshot,
                                      PROCFS_MAX_SNAPSHOT_SIZE, &length, node);
    } else if (node->kind == SYSFS_NODE_BLOCK_ATTRIBUTE) {
        result = sysfs_render_block((char*)context->snapshot,
                                    PROCFS_MAX_SNAPSHOT_SIZE, &length, node);
    } else if (node->kind == SYSFS_NODE_POWER_STATE) {
        result = sysfs_render_power((char*)context->snapshot,
                                    PROCFS_MAX_SNAPSHOT_SIZE, &length);
    } else {
        LOG_ERROR("SYSFS", "Tipo de snapshot sysfs invalido");
        return ERR_INVALID;
    }
    context->snapshot_size = length;
    return result;
}

int sysfs_init(void) {
    if (sysfs_ready) return OK;
    LOG_INFO("SYSFS", "Inicializando sysfs somente leitura");
    spinlock_init(&sysfs_lock);
    spinlock_acquire(&sysfs_lock);
    sysfs_inventory_generation = 1U;
    sysfs_active_snapshots = 0U;
    sysfs_ready = 1U;
    spinlock_release(&sysfs_lock);
    LOG_INFO("SYSFS", "Sysfs inicializado com sucesso");
    return OK;
}

int sysfs_is_ready(void) {
    return sysfs_ready;
}

int sysfs_lookup(const char* canonical_path, vfs_lookup_result_t* result) {
    sysfs_node_ref_t node;
    int status;

    if (!canonical_path || !result) {
        LOG_ERROR("SYSFS", "Destino nulo no lookup sysfs");
        return ERR_NULL;
    }
    if (!sysfs_ready) {
        LOG_ERROR("SYSFS", "Lookup sysfs antes da inicializacao");
        return ERR_STATE;
    }
    status = sysfs_parse_path(canonical_path, &node);
    if (status != OK) return status;
    result->size = 0U;
    result->attributes = 0U;
    result->read_only = 1U;
    if (kstrlen(canonical_path) > SYSFS_PATH_PREFIX_SIZE) {
        sysfs_copy_text(result->relative_path, VFS_MAX_PATH,
                        canonical_path + SYSFS_PATH_PREFIX_SIZE);
    } else {
        result->relative_path[0] = '\0';
    }
    result->type = node.kind == SYSFS_NODE_PCI_ATTRIBUTE ||
                   node.kind == SYSFS_NODE_NET_ATTRIBUTE ||
                   node.kind == SYSFS_NODE_BLOCK_ATTRIBUTE ||
                   node.kind == SYSFS_NODE_POWER_STATE ?
                   VFS_NODE_REGULAR : VFS_NODE_DIRECTORY;
    return OK;
}

int sysfs_open_file(const vfs_lookup_result_t* lookup, uint32_t mode,
                    vnode_t* vnode, file_t* file,
                    sysfs_file_context_t* context) {
    sysfs_node_ref_t node;
    int result;

    if (!lookup || !vnode || !file || !context) {
        LOG_ERROR("SYSFS", "Argumento nulo na abertura sysfs");
        return ERR_NULL;
    }
    if (lookup->mount_kind != VFS_MOUNT_SYSFS) {
        LOG_ERROR("SYSFS", "Montagem invalida na abertura sysfs");
        return ERR_INVALID;
    }
    if (mode != VFS_MODE_READ) {
        LOG_WARN("SYSFS", "Abertura sysfs gravavel recusada");
        return ERR_UNAVAILABLE;
    }
    result = sysfs_parse_path(lookup->canonical_path, &node);
    if (result != OK) return result;
    if (node.kind != SYSFS_NODE_PCI_ATTRIBUTE &&
        node.kind != SYSFS_NODE_NET_ATTRIBUTE &&
        node.kind != SYSFS_NODE_BLOCK_ATTRIBUTE &&
        node.kind != SYSFS_NODE_POWER_STATE) {
        LOG_ERROR("SYSFS", "Abertura solicitada para diretorio sysfs");
        return ERR_INVALID;
    }
    kmemset(context, 0, sizeof(*context));
    context->snapshot = (uint8_t*)kmalloc(PROCFS_MAX_SNAPSHOT_SIZE);
    if (!context->snapshot) {
        LOG_ERROR("SYSFS", "Falha ao alocar snapshot sysfs");
        return ERR_MEM;
    }
    context->node_kind = node.kind;
    sysfs_copy_text(context->identifier, sizeof(context->identifier),
                    node.identifier);
    sysfs_copy_text(context->attribute, sizeof(context->attribute),
                    node.attribute);
    spinlock_acquire(&sysfs_lock);
    context->generation = sysfs_inventory_generation;
    spinlock_release(&sysfs_lock);
    result = sysfs_render_snapshot(&node, context);
    if (result != OK) {
        kfree(context->snapshot);
        context->snapshot = 0;
        context->snapshot_size = 0U;
        LOG_ERROR("SYSFS", "Falha ao gerar snapshot sysfs");
        return result;
    }
    context->mount_slot = lookup->mount_slot;
    context->mount_generation = lookup->mount_generation;
    context->mount_acquired = 1U;
    spinlock_acquire(&sysfs_lock);
    sysfs_active_snapshots++;
    spinlock_release(&sysfs_lock);
    vnode->type = VFS_NODE_REGULAR;
    vnode->operations = &sysfs_operations;
    vnode->private_data = context;
    vnode->size = context->snapshot_size;
    file->mode = mode;
    file->offset = 0U;
    return OK;
}

static int sysfs_collect_pci_ids(char ids[][SYSFS_PCI_ID_SIZE],
                                 uint32_t capacity, uint32_t* out_count) {
    uint8_t count = 0U;
    uint32_t used = 0U;
    int result;

    if (!ids || !out_count) {
        LOG_ERROR("SYSFS", "Destino nulo ao enumerar PCI sysfs");
        return ERR_NULL;
    }
    *out_count = 0U;
    result = pci_get_device_count(&count);
    if (result != OK && result != ERR_OVERFLOW) return OK;
    for (uint8_t index = 0U; index < count; index++) {
        pci_device_t device;

        if (pci_get_device_at(index, &device) != OK || !device.present) continue;
        if (used >= capacity) {
            LOG_ERROR("SYSFS", "Inventario PCI excedeu capacidade sysfs");
            return ERR_OVERFLOW;
        }
        if (sysfs_format_pci_id(ids[used], SYSFS_PCI_ID_SIZE, &device) != OK) {
            LOG_ERROR("SYSFS", "Identificador PCI excedeu capacidade sysfs");
            return ERR_OVERFLOW;
        }
        used++;
    }
    for (uint32_t left = 0U; left < used; left++) {
        for (uint32_t right = left + 1U; right < used; right++) {
            pci_device_t left_device;
            pci_device_t right_device;

            sysfs_pci_copy(ids[left], &left_device);
            sysfs_pci_copy(ids[right], &right_device);
            if (left_device.bus > right_device.bus ||
                (left_device.bus == right_device.bus &&
                 (left_device.device > right_device.device ||
                  (left_device.device == right_device.device &&
                   left_device.function > right_device.function)))) {
                char swap[SYSFS_PCI_ID_SIZE];

                sysfs_copy_text(swap, sizeof(swap), ids[left]);
                sysfs_copy_text(ids[left], SYSFS_PCI_ID_SIZE, ids[right]);
                sysfs_copy_text(ids[right], SYSFS_PCI_ID_SIZE, swap);
            }
        }
    }
    *out_count = used;
    return OK;
}

static int sysfs_collect_network_ids(char ids[][NETWORK_INTERFACE_ID_SIZE],
                                     uint32_t capacity, uint32_t* out_count) {
    uint32_t count = 0U;
    uint32_t used = 0U;
    int result;

    if (!ids || !out_count) {
        LOG_ERROR("SYSFS", "Destino nulo ao enumerar rede sysfs");
        return ERR_NULL;
    }
    *out_count = 0U;
    result = network_manager_get_count(&count);
    if (result != OK) return OK;
    for (uint32_t index = 0U; index < count; index++) {
        network_interface_info_t info;
        network_interface_text_t text;

        if (network_manager_get_interface(index, &info) != OK ||
            network_manager_format_text(&info, &text) != OK) continue;
        if (used >= capacity) {
            LOG_ERROR("SYSFS", "Inventario de rede excedeu capacidade sysfs");
            return ERR_OVERFLOW;
        }
        sysfs_copy_text(ids[used], NETWORK_INTERFACE_ID_SIZE, text.id);
        used++;
    }
    for (uint32_t left = 0U; left < used; left++) {
        for (uint32_t right = left + 1U; right < used; right++) {
            if (kstrcmp(ids[left], ids[right]) > 0) {
                char swap[NETWORK_INTERFACE_ID_SIZE];

                sysfs_copy_text(swap, sizeof(swap), ids[left]);
                sysfs_copy_text(ids[left], NETWORK_INTERFACE_ID_SIZE, ids[right]);
                sysfs_copy_text(ids[right], NETWORK_INTERFACE_ID_SIZE, swap);
            }
        }
    }
    *out_count = used;
    return OK;
}

static int sysfs_collect_block_ids(char ids[][BLOCK_DEVICE_ID_SIZE],
                                   uint32_t capacity, uint32_t* out_count) {
    uint32_t count = 0U;
    uint32_t used = 0U;
    int result;

    if (!ids || !out_count) {
        LOG_ERROR("SYSFS", "Destino nulo ao enumerar blocos sysfs");
        return ERR_NULL;
    }
    *out_count = 0U;
    result = block_get_count(&count);
    if (result != OK) return OK;
    for (uint32_t index = 0U; index < count; index++) {
        block_device_t device;

        if (block_get_at(index, &device) != OK) continue;
        if (used >= capacity) {
            LOG_ERROR("SYSFS", "Inventario de blocos excedeu capacidade sysfs");
            return ERR_OVERFLOW;
        }
        sysfs_copy_text(ids[used], BLOCK_DEVICE_ID_SIZE, device.id);
        used++;
    }
    for (uint32_t left = 0U; left < used; left++) {
        for (uint32_t right = left + 1U; right < used; right++) {
            if (kstrcmp(ids[left], ids[right]) > 0) {
                char swap[BLOCK_DEVICE_ID_SIZE];

                sysfs_copy_text(swap, sizeof(swap), ids[left]);
                sysfs_copy_text(ids[left], BLOCK_DEVICE_ID_SIZE, ids[right]);
                sysfs_copy_text(ids[right], BLOCK_DEVICE_ID_SIZE, swap);
            }
        }
    }
    *out_count = used;
    return OK;
}

static int sysfs_list_name(vfs_dir_entry_t* entries, uint32_t capacity,
                           uint32_t* count, const char* name,
                           vfs_node_type_t type) {
    if (*count >= capacity) {
        LOG_ERROR("SYSFS", "Listagem sysfs sem capacidade");
        return ERR_OVERFLOW;
    }
    kmemset(&entries[*count], 0, sizeof(entries[*count]));
    sysfs_copy_text(entries[*count].name, sizeof(entries[*count].name), name);
    entries[*count].type = type;
    (*count)++;
    return OK;
}

static int sysfs_list_attributes(vfs_dir_entry_t* entries, uint32_t capacity,
                                 uint32_t* count, const char* const* names,
                                 uint32_t name_count) {
    for (uint32_t index = 0U; index < name_count; index++) {
        if (sysfs_list_name(entries, capacity, count, names[index],
                            VFS_NODE_REGULAR) != OK) {
            LOG_ERROR("SYSFS", "Atributos sysfs excederam a listagem");
            return ERR_OVERFLOW;
        }
    }
    return OK;
}

int sysfs_list_path(const char* canonical_path, vfs_dir_entry_t* entries,
                    uint32_t capacity, uint32_t* out_count) {
    sysfs_node_ref_t node;
    uint32_t count = 0U;
    int result;

    if (!canonical_path || !entries || !out_count) {
        LOG_ERROR("SYSFS", "Destino nulo na listagem sysfs");
        return ERR_NULL;
    }
    *out_count = 0U;
    if (!capacity) {
        LOG_ERROR("SYSFS", "Capacidade nula na listagem sysfs");
        return ERR_OVERFLOW;
    }
    result = sysfs_parse_path(canonical_path, &node);
    if (result != OK) return result;
    if (node.kind == SYSFS_NODE_ROOT) {
        result = sysfs_list_name(entries, capacity, &count, "bus",
                                 VFS_NODE_DIRECTORY);
        if (result == OK) result = sysfs_list_name(entries, capacity, &count,
                                                    "class", VFS_NODE_DIRECTORY);
        if (result == OK) result = sysfs_list_name(entries, capacity, &count,
                                                    "power", VFS_NODE_DIRECTORY);
    } else if (node.kind == SYSFS_NODE_BUS) {
        result = sysfs_list_name(entries, capacity, &count, "pci",
                                 VFS_NODE_DIRECTORY);
    } else if (node.kind == SYSFS_NODE_BUS_PCI) {
        result = sysfs_list_name(entries, capacity, &count, "devices",
                                 VFS_NODE_DIRECTORY);
    } else if (node.kind == SYSFS_NODE_PCI_DEVICES) {
        char ids[PCI_MAX_DEVICES][SYSFS_PCI_ID_SIZE];
        uint32_t total = 0U;

        result = sysfs_collect_pci_ids(ids, PCI_MAX_DEVICES, &total);
        for (uint32_t index = 0U; result == OK && index < total; index++) {
            result = sysfs_list_name(entries, capacity, &count, ids[index],
                                     VFS_NODE_DIRECTORY);
        }
    } else if (node.kind == SYSFS_NODE_CLASS) {
        result = sysfs_list_name(entries, capacity, &count, "net",
                                 VFS_NODE_DIRECTORY);
        if (result == OK) result = sysfs_list_name(entries, capacity, &count,
                                                    "block", VFS_NODE_DIRECTORY);
    } else if (node.kind == SYSFS_NODE_CLASS_NET) {
        char ids[NETWORK_MANAGER_MAX_INTERFACES][NETWORK_INTERFACE_ID_SIZE];
        uint32_t total = 0U;

        result = sysfs_collect_network_ids(ids, NETWORK_MANAGER_MAX_INTERFACES,
                                           &total);
        for (uint32_t index = 0U; result == OK && index < total; index++) {
            result = sysfs_list_name(entries, capacity, &count, ids[index],
                                     VFS_NODE_DIRECTORY);
        }
    } else if (node.kind == SYSFS_NODE_CLASS_BLOCK) {
        char ids[BLOCK_MAX_DEVICES][BLOCK_DEVICE_ID_SIZE];
        uint32_t total = 0U;

        result = sysfs_collect_block_ids(ids, BLOCK_MAX_DEVICES, &total);
        for (uint32_t index = 0U; result == OK && index < total; index++) {
            result = sysfs_list_name(entries, capacity, &count, ids[index],
                                     VFS_NODE_DIRECTORY);
        }
    } else if (node.kind == SYSFS_NODE_POWER) {
        result = sysfs_list_name(entries, capacity, &count, "state",
                                 VFS_NODE_REGULAR);
    } else if (node.kind == SYSFS_NODE_PCI_DEVICE) {
        result = sysfs_list_attributes(entries, capacity, &count,
                                       sysfs_pci_attributes,
                                       SYSFS_PCI_ATTRIBUTE_COUNT);
    } else if (node.kind == SYSFS_NODE_NET_DEVICE) {
        result = sysfs_list_attributes(entries, capacity, &count,
                                       sysfs_net_attributes,
                                       SYSFS_NET_ATTRIBUTE_COUNT);
    } else if (node.kind == SYSFS_NODE_BLOCK_DEVICE) {
        result = sysfs_list_attributes(entries, capacity, &count,
                                       sysfs_block_attributes,
                                       SYSFS_BLOCK_ATTRIBUTE_COUNT);
    } else {
        LOG_ERROR("SYSFS", "Listagem solicitada para arquivo sysfs");
        return ERR_INVALID;
    }
    if (result != OK) {
        LOG_WARN("SYSFS", "Listagem sysfs excedeu capacidade");
        return result;
    }
    *out_count = count;
    return OK;
}

int sysfs_list(vfs_dir_entry_t* entries, uint32_t capacity,
               uint32_t* out_count) {
    return sysfs_list_path("/sys", entries, capacity, out_count);
}

static int sysfs_read(file_t* file, void* buffer, uint32_t size,
                      uint32_t* bytes_read) {
    sysfs_file_context_t* context;
    uint32_t available;

    if (!file || !file->vnode || !bytes_read) {
        LOG_ERROR("SYSFS", "Argumento nulo na leitura sysfs");
        return ERR_NULL;
    }
    *bytes_read = 0U;
    if (!(file->mode & VFS_MODE_READ)) {
        LOG_WARN("SYSFS", "Leitura sysfs sem permissao");
        return ERR_UNAVAILABLE;
    }
    if (size && !buffer) {
        LOG_ERROR("SYSFS", "Buffer nulo na leitura sysfs");
        return ERR_NULL;
    }
    context = (sysfs_file_context_t*)file->vnode->private_data;
    if (!context || !context->snapshot || file->offset > context->snapshot_size) {
        LOG_ERROR("SYSFS", "Snapshot sysfs invalido na leitura");
        return ERR_STATE;
    }
    available = context->snapshot_size - file->offset;
    if (size > available) size = available;
    if (size) kmemcpy(buffer, context->snapshot + file->offset, size);
    file->offset += size;
    *bytes_read = size;
    return OK;
}

static int sysfs_write(file_t* file, const void* buffer, uint32_t size,
                       uint32_t* bytes_written) {
    (void)file;
    (void)buffer;
    (void)size;
    if (!bytes_written) {
        LOG_ERROR("SYSFS", "Contador nulo na escrita sysfs");
        return ERR_NULL;
    }
    *bytes_written = 0U;
    LOG_WARN("SYSFS", "Escrita sysfs recusada");
    return ERR_UNAVAILABLE;
}

static int sysfs_open(vnode_t* vnode, file_t* file) {
    if (!vnode || !file) {
        LOG_ERROR("SYSFS", "Argumento nulo na abertura do vnode sysfs");
        return ERR_NULL;
    }
    return OK;
}

static int sysfs_close(file_t* file) {
    sysfs_file_context_t* context;

    if (!file || !file->vnode) {
        LOG_ERROR("SYSFS", "Arquivo nulo no fechamento sysfs");
        return ERR_NULL;
    }
    context = (sysfs_file_context_t*)file->vnode->private_data;
    if (!context) {
        LOG_ERROR("SYSFS", "Contexto ausente no fechamento sysfs");
        return ERR_STATE;
    }
    if (context->snapshot) {
        kfree(context->snapshot);
        context->snapshot = 0;
        context->snapshot_size = 0U;
        spinlock_acquire(&sysfs_lock);
        if (sysfs_active_snapshots) sysfs_active_snapshots--;
        spinlock_release(&sysfs_lock);
    }
    if (context->mount_acquired) {
        vfs_mount_release(context->mount_slot, context->mount_generation);
        context->mount_acquired = 0U;
    }
    return OK;
}

static int sysfs_lseek(file_t* file, int32_t offset, uint32_t whence,
                       uint32_t* position) {
    sysfs_file_context_t* context;
    uint32_t base;
    uint32_t target;

    if (!file || !file->vnode || !position) {
        LOG_ERROR("SYSFS", "Argumento nulo no seek sysfs");
        return ERR_NULL;
    }
    *position = 0U;
    context = (sysfs_file_context_t*)file->vnode->private_data;
    if (!context || !context->snapshot) {
        LOG_ERROR("SYSFS", "Snapshot ausente no seek sysfs");
        return ERR_STATE;
    }
    if (whence == VFS_SEEK_SET) base = 0U;
    else if (whence == VFS_SEEK_CUR) base = file->offset;
    else if (whence == VFS_SEEK_END) base = context->snapshot_size;
    else {
        LOG_WARN("SYSFS", "Origem de seek sysfs invalida");
        return ERR_INVALID;
    }
    if (offset < 0) {
        uint32_t magnitude = (uint32_t)(-(offset + 1)) + 1U;

        if (magnitude > base) {
            LOG_WARN("SYSFS", "Seek sysfs antes do inicio");
            return ERR_INVALID;
        }
        target = base - magnitude;
    } else {
        if (base > SYSFS_UINT32_MAX - (uint32_t)offset) {
            LOG_WARN("SYSFS", "Seek sysfs excedeu o contador");
            return ERR_OVERFLOW;
        }
        target = base + (uint32_t)offset;
    }
    if (target > context->snapshot_size) {
        LOG_WARN("SYSFS", "Seek sysfs depois do fim");
        return ERR_INVALID;
    }
    file->offset = target;
    *position = target;
    return OK;
}

static int sysfs_ioctl(file_t* file, uint32_t request, void* argument) {
    (void)file;
    (void)request;
    (void)argument;
    LOG_WARN("SYSFS", "Ioctl sysfs nao suportado");
    return ERR_UNAVAILABLE;
}

static int sysfs_sync(file_t* file) {
    (void)file;
    LOG_WARN("SYSFS", "Sync sysfs nao suportado");
    return ERR_UNAVAILABLE;
}

static int sysfs_poll(file_t* file, uint32_t events, uint32_t* revents) {
    (void)events;
    if (!file || !revents) {
        LOG_ERROR("SYSFS", "Argumento nulo no poll sysfs");
        return ERR_NULL;
    }
    *revents = POLLIN;
    return OK;
}

static const file_operations_t sysfs_operations = {
    sysfs_open, sysfs_read, sysfs_write, sysfs_close, sysfs_lseek,
    sysfs_ioctl, sysfs_sync, sysfs_poll
};

int sysfs_validate_state(void) {
    uint32_t active;
    uint32_t generation;

    spinlock_acquire(&sysfs_lock);
    active = sysfs_active_snapshots;
    generation = sysfs_inventory_generation;
    spinlock_release(&sysfs_lock);
    if (!sysfs_ready || !generation || active > VFS_MAX_OPEN_FILES) {
        LOG_ERROR("SYSFS", "Invariantes sysfs invalidas");
        return ERR_STATE;
    }
    return OK;
}

static int sysfs_buffer_valid(const uint8_t* buffer, uint32_t size) {
    if (!buffer) return 0;
    for (uint32_t index = 0U; index < size; index++) {
        if (!buffer[index] || buffer[index] == '\r' || buffer[index] == 0x1BU ||
            buffer[index] > 0x7EU) return 0;
    }
    return 1;
}

static int sysfs_buffer_contains(const uint8_t* buffer, uint32_t size,
                                 const char* text) {
    uint32_t text_length;

    if (!buffer || !text) return 0;
    text_length = kstrlen(text);
    if (!text_length || text_length > size) return 0;
    for (uint32_t index = 0U; index + text_length <= size; index++) {
        uint32_t offset = 0U;

        while (offset < text_length &&
               buffer[index + offset] == (uint8_t)text[offset]) offset++;
        if (offset == text_length) return 1;
    }
    return 0;
}

int sysfs_self_test(sysfs_test_result_t* result) {
    static const char* const root_names[] = {"bus", "class", "power"};
    static vfs_dir_entry_t entries[VFS_MAX_DIR_ENTRIES];
    vfs_lookup_result_t lookup;
    static uint8_t first[SYSFS_TEST_BUFFER_SIZE];
    static uint8_t second[SYSFS_TEST_BUFFER_SIZE];
    uint32_t count = 0U;
    uint32_t bytes = 0U;
    uint32_t second_bytes = 0U;
    uint32_t position = 0U;
    uint32_t before_active;
    int32_t fd = VFS_FD_INVALID;
    int open_result;

    if (!result) {
        LOG_ERROR("SYSFS", "Resultado nulo no autoteste sysfs");
        return ERR_NULL;
    }
    kmemset(result, 0, sizeof(*result));
    if (!sysfs_ready) {
        LOG_ERROR("SYSFS", "Autoteste sysfs antes da inicializacao");
        return ERR_UNAVAILABLE;
    }
    result->registry = sysfs_validate_state() == OK;
    sysfs_test_count(result, result->registry);
    result->lookup = vfs_lookup("/sys", &lookup) == OK &&
                     lookup.type == VFS_NODE_DIRECTORY &&
                     vfs_lookup("/sys/power/state", &lookup) == OK &&
                     lookup.type == VFS_NODE_REGULAR &&
                     vfs_lookup("/sys/no-such-node", &lookup) == ERR_NOT_FOUND &&
                     vfs_lookup("/sys/class/net/missing-device", &lookup) ==
                     ERR_NOT_FOUND;
    sysfs_test_count(result, result->lookup);
    result->listing = vfs_list_dir("/sys", entries, VFS_MAX_DIR_ENTRIES,
                                   &count) == OK && count == 3U;
    if (result->listing) {
        for (uint32_t index = 0U; index < 3U; index++) {
            if (kstrcmp(entries[index].name, root_names[index]) != 0) {
                result->listing = 0U;
                break;
            }
        }
        result->listing = result->listing &&
            vfs_list_dir("/sys/bus", entries, VFS_MAX_DIR_ENTRIES, &count) == OK &&
            count == 1U && kstrcmp(entries[0].name, "pci") == 0;
        result->listing = result->listing &&
            vfs_list_dir("/sys/class", entries, VFS_MAX_DIR_ENTRIES, &count) == OK &&
            count == 2U && kstrcmp(entries[0].name, "net") == 0 &&
            kstrcmp(entries[1].name, "block") == 0;
        result->listing = result->listing &&
            vfs_list_dir("/sys", entries, 2U, &count) == ERR_OVERFLOW;
    }
    sysfs_test_count(result, result->listing);
    result->power = vfs_list_dir("/sys/power", entries, VFS_MAX_DIR_ENTRIES,
                                 &count) == OK && count == 1U &&
                    kstrcmp(entries[0].name, "state") == 0;
    sysfs_test_count(result, result->power);
    spinlock_acquire(&sysfs_lock);
    before_active = sysfs_active_snapshots;
    spinlock_release(&sysfs_lock);
    open_result = vfs_open("/sys/power/state", VFS_MODE_READ, &fd);
    if (open_result == OK) {
        open_result = vfs_read(fd, first, sizeof(first), &bytes);
        if (open_result == OK) {
            open_result = vfs_lseek(fd, 0, VFS_SEEK_SET, &position);
        }
        if (open_result == OK) {
            open_result = vfs_read(fd, second, sizeof(second), &second_bytes);
        }
        if (open_result == OK) {
            open_result = vfs_lseek(fd, -1, VFS_SEEK_CUR, &position);
            if (open_result == OK &&
                (second_bytes == 0U || position != second_bytes - 1U)) {
                open_result = ERR_STATE;
            }
        }
        if (open_result == OK) {
            open_result = vfs_lseek(fd, 0, VFS_SEEK_SET, &position);
        }
        if (open_result == OK && (bytes != second_bytes ||
                                  !sysfs_buffer_valid(first, bytes) ||
                                  !sysfs_buffer_valid(second, second_bytes) ||
                                  !sysfs_buffer_contains(second, second_bytes,
                                                         "state S0") ||
                                  !sysfs_buffer_contains(second, second_bytes,
                                                         "reboot "))) {
            open_result = ERR_STATE;
        }
        if (open_result == OK) {
            open_result = vfs_lseek(fd, 0, VFS_SEEK_END, &position);
        }
        if (open_result == OK) {
            open_result = vfs_read(fd, first, sizeof(first), &bytes);
            if (open_result == OK && bytes != 0U) open_result = ERR_STATE;
        }
        if (vfs_close(fd) != OK) open_result = ERR_STATE;
        fd = VFS_FD_INVALID;
    }
    result->read = open_result == OK;
    sysfs_test_count(result, result->read);
    result->permissions = vfs_open("/sys/power/state", VFS_MODE_WRITE, &fd) ==
                          ERR_UNAVAILABLE;
    if (fd != VFS_FD_INVALID) (void)vfs_close(fd);
    sysfs_test_count(result, result->permissions);
    result->seek_eof = vfs_open("/sys/power/state", VFS_MODE_READ, &fd) == OK;
    if (result->seek_eof) {
        result->seek_eof = vfs_lseek(fd, -1, VFS_SEEK_SET, &position) ==
                           ERR_INVALID &&
                           vfs_lseek(fd, 1, VFS_SEEK_END, &position) ==
                           ERR_INVALID &&
                           vfs_close(fd) == OK;
        fd = VFS_FD_INVALID;
    }
    sysfs_test_count(result, result->seek_eof);
    result->inventories = vfs_list_dir("/sys/bus/pci/devices", entries,
                                       VFS_MAX_DIR_ENTRIES, &count) == OK &&
                          vfs_list_dir("/sys/class/net", entries,
                                       VFS_MAX_DIR_ENTRIES, &count) == OK &&
                          vfs_list_dir("/sys/class/block", entries,
                                       VFS_MAX_DIR_ENTRIES, &count) == OK;
    sysfs_test_count(result, result->inventories);
    result->format = sysfs_buffer_valid(first, second_bytes);
    sysfs_test_count(result, result->format);
    spinlock_acquire(&sysfs_lock);
    result->cleanup = sysfs_active_snapshots == before_active;
    spinlock_release(&sysfs_lock);
    sysfs_test_count(result, result->cleanup);
    return result->passed == result->total ? OK : ERR_STATE;
}
