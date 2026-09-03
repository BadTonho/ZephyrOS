#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "drivers/pci.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_FAKE_DEVICE_CAPACITY (PCI_MAX_DEVICES + 1U)

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint8_t irq;
    uint32_t bars[6];
    uint32_t command;
} fake_pci_device_t;

static fake_pci_device_t fake_devices[HOST_FAKE_DEVICE_CAPACITY];
static uint32_t fake_device_count;
static uint32_t selected_address;
static uint32_t config_writes;
static uint32_t yield_calls;
static uint8_t reject_command_writes;

static void __attribute__((no_instrument_function)) coverage_record(
    void* function) {
    uintptr_t address = (uintptr_t)function;

    if (!coverage_active || !address) return;
    for (uint32_t index = 0U; index < coverage_count; index++) {
        if (coverage_addresses[index] == address) return;
    }
    if (coverage_count < HOST_COVERAGE_CAPACITY) {
        coverage_addresses[coverage_count++] = address;
    }
}

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(
    void* function, void* caller) {
    (void)caller;
    coverage_record(function);
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(
    void* function, void* caller) {
    (void)function;
    (void)caller;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:drivers:pci|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:pci|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:pci|value=0x%08X\n",
           (uint32_t)result);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void process_yield(void) {
    yield_calls++;
}

static fake_pci_device_t* fake_find(uint8_t bus, uint8_t device,
                                    uint8_t function) {
    for (uint32_t index = 0U; index < fake_device_count; index++) {
        fake_pci_device_t* current = &fake_devices[index];

        if (current->bus == bus && current->device == device &&
            current->function == function) return current;
    }
    return 0;
}

static fake_pci_device_t* fake_selected(uint8_t* out_offset) {
    uint8_t bus = (uint8_t)(selected_address >> 16U);
    uint8_t device = (uint8_t)(selected_address >> 11U) & 0x1FU;
    uint8_t function = (uint8_t)(selected_address >> 8U) & 0x07U;

    *out_offset = (uint8_t)(selected_address & 0xFCU);
    return fake_find(bus, device, function);
}

void pci_host_outl(uint16_t port, uint32_t value) {
    if (port == PCI_CONFIG_ADDRESS) {
        selected_address = value;
        return;
    }
    if (port == PCI_CONFIG_DATA) {
        uint8_t offset;
        fake_pci_device_t* device = fake_selected(&offset);

        config_writes++;
        if (device && offset == PCI_COMMAND && !reject_command_writes) {
            device->command = value;
        }
    }
}

uint32_t pci_host_inl(uint16_t port) {
    uint8_t offset;
    fake_pci_device_t* device;

    if (port != PCI_CONFIG_DATA) return 0U;
    device = fake_selected(&offset);
    if (!device) return UINT32_MAX;
    if (offset == PCI_VENDOR_ID) {
        return (uint32_t)device->vendor_id |
               ((uint32_t)device->device_id << 16U);
    }
    if (offset == PCI_REVISION) {
        return ((uint32_t)device->class_code << 24U) |
               ((uint32_t)device->subclass << 16U) |
               ((uint32_t)device->prog_if << 8U) | device->revision;
    }
    if (offset == (PCI_HEADER_TYPE & 0xFCU)) {
        return (uint32_t)device->header_type << 16U;
    }
    if (offset == PCI_INTERRUPT_LINE) return device->irq;
    if (offset == PCI_COMMAND) return device->command;
    if (offset >= PCI_BAR0 && offset <= PCI_BAR5 &&
        ((offset - PCI_BAR0) % 4U) == 0U) {
        return device->bars[(offset - PCI_BAR0) / 4U];
    }
    return 0U;
}

static void reset_hardware(void) {
    fake_device_count = 0U;
    selected_address = 0U;
    config_writes = 0U;
    yield_calls = 0U;
    reject_command_writes = 0U;
}

static fake_pci_device_t* add_device(uint8_t bus, uint8_t device,
                                     uint8_t function, uint16_t vendor_id,
                                     uint16_t device_id, uint8_t class_code,
                                     uint8_t subclass, uint8_t header_type) {
    fake_pci_device_t* current = &fake_devices[fake_device_count++];

    *current = (fake_pci_device_t){0};
    current->bus = bus;
    current->device = device;
    current->function = function;
    current->vendor_id = vendor_id;
    current->device_id = device_id;
    current->class_code = class_code;
    current->subclass = subclass;
    current->prog_if = 1U;
    current->revision = 3U;
    current->header_type = header_type;
    current->irq = 11U;
    for (uint32_t index = 0U; index < 6U; index++) {
        current->bars[index] = 0x1000U + index * 0x100U;
    }
    return current;
}

static int check_uninitialized(void) {
    uint8_t count;
    pci_device_t device;

    if (pci_get_device_count(0) != ERR_NULL) return 1;
    if (pci_get_device_count(&count) != ERR_STATE) return 2;
    if (pci_get_device_at(0U, 0) != ERR_NULL) return 3;
    if (pci_get_device_at(0U, &device) != ERR_STATE) return 4;
    if (pci_get_device(2U, 0U) != 0) return 5;
    if (pci_get_device_by_id(0x1234U, 0x5678U) != 0) return 6;
    if (pci_enable_memory_and_bus_mastering(0) != ERR_NULL) return 7;
    if (pci_enable_io_and_bus_mastering(0) != ERR_NULL) return 8;
    if (pci_enable_memory_and_bus_mastering(&device) != ERR_STATE) return 9;
    if (pci_enable_io_and_bus_mastering(&device) != ERR_STATE) return 10;
    pci_enable_bus_mastering(0);
    pci_enable_bus_mastering(&device);
    return 0;
}

static int check_inventory(void) {
    uint8_t count = 0U;
    pci_device_t device;
    pci_device_t* found;
    fake_pci_device_t* primary;

    reset_hardware();
    primary = add_device(0U, 0U, 0U, 0x1234U, 0x5678U, 2U, 0U, 0x80U);
    add_device(0U, 0U, 1U, 0x1111U, 0x2222U, 3U, 4U, 0U);
    add_device(0U, 1U, 0U, 0x9ABCU, 0xDEF0U, 1U, 6U, 0U);
    if (pci_init() != OK) return 1;
    if (yield_calls != 32U) return 2;
    if (pci_get_device_count(&count) != OK || count != 3U) return 3;
    if (pci_get_device_at(0U, &device) != OK ||
        device.vendor_id != primary->vendor_id ||
        device.device_id != primary->device_id || device.class != 2U ||
        device.subclass != 0U || device.function != 0U ||
        device.bar5 != primary->bars[5]) return 4;
    if (pci_get_device_at(3U, &device) != ERR_INVALID) return 5;
    found = pci_get_device(3U, 4U);
    if (!found || found->vendor_id != 0x1111U) return 6;
    found = pci_get_device_by_id(0x9ABCU, 0xDEF0U);
    if (!found || found->class != 1U || found->subclass != 6U) return 7;
    if (pci_get_device(9U, 9U) != 0 ||
        pci_get_device_by_id(0xAAAAU, 0xBBBBU) != 0) return 8;
    if (pci_read(0U, 0U, 0U, PCI_VENDOR_ID) != 0x56781234U) return 9;
    pci_write(0U, 0U, 0U, PCI_COMMAND, 0x55U);
    if (config_writes == 0U || pci_read(0U, 0U, 0U, PCI_COMMAND) != 0x55U) {
        return 10;
    }
    if (pci_enable_memory_and_bus_mastering(&device) != OK) return 11;
    if (pci_enable_io_and_bus_mastering(&device) != OK) return 12;
    pci_enable_bus_mastering(&device);
    if ((primary->command & 7U) != 7U) return 13;
    primary->command = 0U;
    reject_command_writes = 1U;
    if (pci_enable_memory_and_bus_mastering(&device) != ERR_UNAVAILABLE) {
        return 14;
    }
    return 0;
}

static int check_overflow_and_reset(void) {
    uint8_t count = 0U;
    pci_device_t device;

    reset_hardware();
    for (uint32_t index = 0U; index < HOST_FAKE_DEVICE_CAPACITY; index++) {
        add_device((uint8_t)(index / 32U), (uint8_t)(index % 32U), 0U,
                   (uint16_t)(0x1000U + index), (uint16_t)(0x2000U + index),
                   1U, 1U, 0U);
    }
    if (pci_init() != ERR_OVERFLOW) return 1;
    if (pci_get_device_count(&count) != ERR_OVERFLOW ||
        count != PCI_MAX_DEVICES) return 2;
    if (pci_get_device_at(PCI_MAX_DEVICES - 1U, &device) != OK) return 3;
    if (pci_get_device_at(PCI_MAX_DEVICES, &device) != ERR_INVALID) return 4;
    reset_hardware();
    if (pci_init() != OK || pci_get_device_count(&count) != OK || count != 0U) {
        return 5;
    }
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_uninitialized();
    if (result == 0) result = check_inventory();
    if (result == 0) result = check_overflow_and_reset();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
