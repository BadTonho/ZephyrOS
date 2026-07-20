#include "drivers/pci.h"
#include "core/video.h"
#include "core/log.h"

static pci_device_t devices[32];
static uint8_t device_count = 0;

static void outl(uint16_t port, uint32_t val) {
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static uint32_t inl(uint16_t port) {
    uint32_t result;
    asm volatile("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static uint16_t inw(uint16_t port) {
    uint16_t result;
    asm volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static void outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static uint8_t inb(uint16_t port) {
    uint8_t result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static void pci_scan_device(uint8_t bus, uint8_t device) {
    for (uint8_t function = 0; function < 8; function++) {
        uint32_t reg0 = pci_read(bus, device, function, 0);
        uint16_t vendor = reg0 & 0xFFFF;
        if (vendor == 0xFFFF) continue;

        uint32_t reg2 = pci_read(bus, device, function, 8);
        uint32_t reg3 = pci_read(bus, device, function, 12);

        pci_device_t* dev = &devices[device_count];
        dev->vendor_id = vendor;
        dev->device_id = (reg0 >> 16) & 0xFFFF;
        dev->class = (reg2 >> 24) & 0xFF;
        dev->subclass = (reg2 >> 16) & 0xFF;
        dev->prog_if = (reg2 >> 8) & 0xFF;
        dev->revision = reg2 & 0xFF;
        dev->bus = bus;
        dev->device = device;
        dev->function = function;

        uint32_t reg4 = pci_read(bus, device, function, 0x10);
        uint32_t reg5 = pci_read(bus, device, function, 0x14);
        uint32_t reg6 = pci_read(bus, device, function, 0x18);
        uint32_t reg7 = pci_read(bus, device, function, 0x1C);
        uint32_t reg8 = pci_read(bus, device, function, 0x20);
        uint32_t reg9 = pci_read(bus, device, function, 0x24);

        dev->bar0 = reg4;
        dev->bar1 = reg5;
        dev->bar2 = reg6;
        dev->bar3 = reg7;
        dev->bar4 = reg8;
        dev->bar5 = reg9;

        uint32_t reg15 = pci_read(bus, device, function, 0x3C);
        dev->irq = reg15 & 0xFF;

        dev->present = 1;
        device_count++;
    }
}

void pci_init(void) {
    LOG_INFO("PCI", "Inicializando varredura PCI");
    device_count = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            uint32_t reg0 = pci_read(bus, device, 0, 0);
            uint16_t vendor = reg0 & 0xFFFF;
            if (vendor == 0xFFFF) continue;

            uint32_t reg2 = pci_read(bus, device, 0, 8);
            uint8_t header_type = (pci_read(bus, device, 0, 0x0E) >> 16) & 0xFF;

            pci_scan_device(bus, device);

            if (header_type & 0x80) {
                for (uint8_t function = 1; function < 8; function++) {
                    uint32_t reg0_f = pci_read(bus, device, function, 0);
                    if ((reg0_f & 0xFFFF) != 0xFFFF) {
                        pci_scan_device(bus, device);
                    }
                }
            }
        }
    }
    LOG_INFO("PCI", "Varredura PCI concluida");
}

pci_device_t* pci_get_device(uint8_t class, uint8_t subclass) {
    for (uint8_t i = 0; i < device_count; i++) {
        if (devices[i].present &&
            devices[i].class == class &&
            devices[i].subclass == subclass) {
            return &devices[i];
        }
    }
    return 0;
}

pci_device_t* pci_get_device_by_id(uint16_t vendor_id, uint16_t device_id) {
    for (uint8_t i = 0; i < device_count; i++) {
        if (devices[i].present &&
            devices[i].vendor_id == vendor_id &&
            devices[i].device_id == device_id) {
            return &devices[i];
        }
    }
    return 0;
}

void pci_enable_bus_mastering(pci_device_t* dev) {
    if (!dev) return;
    uint32_t command = pci_read(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command |= 0x04;
    pci_write(dev->bus, dev->device, dev->function, PCI_COMMAND, command);
}
