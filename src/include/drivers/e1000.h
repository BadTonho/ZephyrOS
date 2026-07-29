#ifndef E1000_H
#define E1000_H

#include "types.h"
#include "core/ethernet.h"
#include "drivers/idt.h"
#include "drivers/pci.h"

#define E1000_DEVICE_CAPACITY 4U

int e1000_init(const pci_device_t* pci, const char* interface_id,
               ethernet_interface_t* out_interface);
void e1000_handler(registers_t* regs);

#endif
