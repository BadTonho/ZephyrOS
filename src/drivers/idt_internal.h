#ifndef ZEPHYROS_IDT_INTERNAL_H
#define ZEPHYROS_IDT_INTERNAL_H

#include "drivers/idt.h"

int idt_test_probe_begin(void);
int idt_test_probe_end(void);
uint32_t idt_test_probe_get_count(uint8_t vector);

#endif
