#ifndef RTL8139_H
#define RTL8139_H

#include "types.h"
#include "core/ethernet.h"
#include "drivers/idt.h"
#include "drivers/pci.h"

#define RTL8139_DEVICE_CAPACITY 4U

int rtl8139_init(const pci_device_t* pci, const char* interface_id,
                 ethernet_interface_t* out_interface);
void rtl8139_handler(registers_t* regs);

#endif
