#include "drivers/pci.h"
#include "core/errors.h"
#include "core/log.h"
#include "process/process.h"

static pci_device_t devices[PCI_MAX_DEVICES];
static uint8_t device_count = 0;
static int pci_initialized = 0;
static int pci_scan_result = ERR_STATE;

#define PCI_CONFIG_ENABLE 0x80000000U
#define PCI_VENDOR_ABSENT 0xFFFFU
#define PCI_HEADER_MULTIFUNCTION 0x80U
#define PCI_BUS_COUNT 256U
#define PCI_DEVICES_PER_BUS 32U
#define PCI_FUNCTIONS_PER_DEVICE 8U
#define PCI_COOPERATIVE_BUS_INTERVAL 8U
#define PCI_COMMAND_IO_SPACE 0x01U
#define PCI_COMMAND_MEMORY_SPACE 0x02U
#define PCI_COMMAND_BUS_MASTER 0x04U

static void outl(uint16_t port, uint32_t val) {
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static uint32_t inl(uint16_t port) {
    uint32_t result;
    asm volatile("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function,
                  uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (device << 11) |
                                  (function << 8) | (offset & 0xFC) |
                                  PCI_CONFIG_ENABLE);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write(uint8_t bus, uint8_t device, uint8_t function,
               uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (device << 11) |
                                  (function << 8) | (offset & 0xFC) |
                                  PCI_CONFIG_ENABLE);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static int pci_scan_function(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t reg0 = pci_read(bus, device, function, PCI_VENDOR_ID);
    uint16_t vendor = (uint16_t)reg0;
    uint32_t reg2;
    uint32_t reg15;
    pci_device_t* dev;

    if (vendor == PCI_VENDOR_ABSENT) return OK;
    if (device_count >= PCI_MAX_DEVICES) {
        LOG_ERROR("PCI", "Limite de dispositivos PCI atingido");
        return ERR_OVERFLOW;
    }

    reg2 = pci_read(bus, device, function, PCI_REVISION);
    reg15 = pci_read(bus, device, function, PCI_INTERRUPT_LINE);
    dev = &devices[device_count];
    dev->vendor_id = vendor;
    dev->device_id = (uint16_t)(reg0 >> 16);
    dev->class = (uint8_t)(reg2 >> 24);
    dev->subclass = (uint8_t)(reg2 >> 16);
    dev->prog_if = (uint8_t)(reg2 >> 8);
    dev->revision = (uint8_t)reg2;
    dev->irq = (uint8_t)reg15;
    dev->bus = bus;
    dev->device = device;
    dev->function = function;
    dev->bar0 = pci_read(bus, device, function, PCI_BAR0);
    dev->bar1 = pci_read(bus, device, function, PCI_BAR1);
    dev->bar2 = pci_read(bus, device, function, PCI_BAR2);
    dev->bar3 = pci_read(bus, device, function, PCI_BAR3);
    dev->bar4 = pci_read(bus, device, function, PCI_BAR4);
    dev->bar5 = pci_read(bus, device, function, PCI_BAR5);
    dev->present = 1;
    device_count++;
    return OK;
}

static int pci_scan_device(uint8_t bus, uint8_t device) {
    uint32_t reg0 = pci_read(bus, device, 0, PCI_VENDOR_ID);
    uint8_t header_type;
    int result;

    if ((reg0 & PCI_VENDOR_ABSENT) == PCI_VENDOR_ABSENT) return OK;
    header_type = (uint8_t)(pci_read(bus, device, 0, PCI_HEADER_TYPE) >> 16);
    result = pci_scan_function(bus, device, 0);
    if (result != OK) {
        LOG_ERROR("PCI", "Falha ao registrar funcao PCI");
        return result;
    }
    if (!(header_type & PCI_HEADER_MULTIFUNCTION)) return OK;

    for (uint8_t function = 1; function < PCI_FUNCTIONS_PER_DEVICE; function++) {
        result = pci_scan_function(bus, device, function);
        if (result != OK) {
            LOG_ERROR("PCI", "Falha ao registrar funcao PCI multipla");
            return result;
        }
    }
    return OK;
}

int pci_init(void) {
    int result = OK;

    LOG_INFO("PCI", "Inicializando varredura PCI");
    device_count = 0;
    pci_initialized = 0;

    for (uint16_t bus = 0; bus < PCI_BUS_COUNT; bus++) {
        for (uint8_t device = 0; device < PCI_DEVICES_PER_BUS; device++) {
            result = pci_scan_device((uint8_t)bus, device);
            if (result != OK) goto done;
        }
        if ((bus + 1U) % PCI_COOPERATIVE_BUS_INTERVAL == 0U) {
            process_yield();
        }
    }

done:
    pci_initialized = 1;
    pci_scan_result = result;
    if (result == ERR_OVERFLOW) {
        LOG_WARN("PCI", "Varredura PCI parcial; inventario limitado");
        return result;
    }
    if (result != OK) {
        LOG_ERROR("PCI", "Falha na varredura PCI");
        return result;
    }

    LOG_INFO("PCI", "Varredura PCI concluida com sucesso");
    return OK;
}

int pci_get_device_count(uint8_t* out_count) {
    if (!out_count) {
        LOG_ERROR("PCI", "Destino nulo ao consultar contagem");
        return ERR_NULL;
    }
    if (!pci_initialized) {
        LOG_ERROR("PCI", "Consulta antes da varredura PCI");
        return ERR_STATE;
    }

    *out_count = device_count;
    return pci_scan_result;
}

int pci_get_device_at(uint8_t index, pci_device_t* out_device) {
    if (!out_device) {
        LOG_ERROR("PCI", "Destino nulo ao consultar dispositivo");
        return ERR_NULL;
    }
    if (!pci_initialized) {
        LOG_ERROR("PCI", "Consulta antes da varredura PCI");
        return ERR_STATE;
    }
    if (index >= device_count) {
        LOG_ERROR("PCI", "Indice de dispositivo PCI invalido");
        return ERR_INVALID;
    }

    *out_device = devices[index];
    return OK;
}

pci_device_t* pci_get_device(uint8_t class, uint8_t subclass) {
    if (!pci_initialized) {
        LOG_ERROR("PCI", "Busca antes da varredura PCI");
        return 0;
    }
    for (uint8_t i = 0; i < device_count; i++) {
        if (devices[i].present && devices[i].class == class &&
            devices[i].subclass == subclass) {
            return &devices[i];
        }
    }
    return 0;
}

pci_device_t* pci_get_device_by_id(uint16_t vendor_id, uint16_t device_id) {
    if (!pci_initialized) {
        LOG_ERROR("PCI", "Busca antes da varredura PCI");
        return 0;
    }
    for (uint8_t i = 0; i < device_count; i++) {
        if (devices[i].present && devices[i].vendor_id == vendor_id &&
            devices[i].device_id == device_id) {
            return &devices[i];
        }
    }
    return 0;
}

int pci_enable_memory_and_bus_mastering(const pci_device_t* dev) {
    uint32_t command;

    if (!dev) {
        LOG_ERROR("PCI", "Dispositivo nulo ao habilitar memoria PCI");
        return ERR_NULL;
    }
    if (!pci_initialized) {
        LOG_ERROR("PCI", "PCI nao inicializado ao habilitar dispositivo");
        return ERR_STATE;
    }
    command = pci_read(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command |= PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER;
    pci_write(dev->bus, dev->device, dev->function, PCI_COMMAND, command);
    command = pci_read(dev->bus, dev->device, dev->function, PCI_COMMAND);
    if ((command & (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER)) !=
        (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER)) {
        LOG_ERROR("PCI", "Nao foi possivel habilitar memoria e DMA PCI");
        return ERR_UNAVAILABLE;
    }
    return OK;
}

int pci_enable_io_and_bus_mastering(const pci_device_t* dev) {
    uint32_t command;

    if (!dev) {
        LOG_ERROR("PCI", "Dispositivo nulo ao habilitar I/O PCI");
        return ERR_NULL;
    }
    if (!pci_initialized) {
        LOG_ERROR("PCI", "PCI nao inicializado ao habilitar I/O");
        return ERR_STATE;
    }
    command = pci_read(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command |= PCI_COMMAND_IO_SPACE | PCI_COMMAND_BUS_MASTER;
    pci_write(dev->bus, dev->device, dev->function, PCI_COMMAND, command);
    command = pci_read(dev->bus, dev->device, dev->function, PCI_COMMAND);
    if ((command & (PCI_COMMAND_IO_SPACE | PCI_COMMAND_BUS_MASTER)) !=
        (PCI_COMMAND_IO_SPACE | PCI_COMMAND_BUS_MASTER)) {
        LOG_ERROR("PCI", "Nao foi possivel habilitar I/O e DMA PCI");
        return ERR_UNAVAILABLE;
    }
    return OK;
}

void pci_enable_bus_mastering(pci_device_t* dev) {
    uint32_t command;

    if (!dev) {
        LOG_ERROR("PCI", "Dispositivo nulo ao habilitar bus mastering");
        return;
    }
    if (!pci_initialized) {
        LOG_ERROR("PCI", "PCI nao inicializado ao habilitar bus mastering");
        return;
    }
    command = pci_read(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command |= PCI_COMMAND_BUS_MASTER;
    pci_write(dev->bus, dev->device, dev->function, PCI_COMMAND, command);
}
