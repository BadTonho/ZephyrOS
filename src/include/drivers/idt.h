#ifndef IDT_H
#define IDT_H

#include "types.h"
#include "core/errors.h"

#define IDT_IRQ_LINE_COUNT 16U
#define IDT_SHARED_IRQ_HANDLER_CAPACITY 4U

typedef struct {
    uint16_t base_low;
    uint16_t selector;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

typedef struct {
    uint32_t ds, es, fs, gs;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} __attribute__((packed)) registers_t;

typedef void (*isr_handler_t)(registers_t*);

typedef struct {
    uint8_t irq_line;
    uint8_t registered_handlers;
    uint32_t occurrences;
} idt_irq_status_t;

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags);
int idt_register_handler(uint8_t n, isr_handler_t handler);
int idt_register_shared_irq_handler(uint8_t irq_line,
                                    isr_handler_t handler);
int idt_unmask_irq(uint8_t irq_line);
int idt_get_shared_irq_handler_count(uint8_t irq_line,
                                     uint8_t* out_count);
int idt_get_irq_status(uint8_t irq_line, idt_irq_status_t* out_status);
int idt_validate_irq_state(void);
int idt_enable_user_syscall(void);
int idt_is_user_syscall_enabled(void);

#endif
