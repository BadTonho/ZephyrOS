#include "drivers/tss.h"
#include "drivers/idt.h"

static tss_entry_t tss;

extern void tss_flush(void);

static void memset(void* dst, uint8_t val, uint32_t size) {
    uint8_t* d = (uint8_t*)dst;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = val;
    }
}

void tss_init(void) {
    uint32_t base = (uint32_t)&tss;
    uint32_t limit = base + sizeof(tss_entry_t);

    idt_set_gate(40, 0, 0, 0);

    memset(&tss, 0, sizeof(tss_entry_t));

    tss.ss0 = 0x10;
    tss.esp0 = 0x90000;
    tss.cs = 0x08;
    tss.ds = 0x10;
    tss.es = 0x10;
    tss.fs = 0x10;
    tss.gs = 0x10;
    tss.ss = 0x10;
    tss.iomap_base = sizeof(tss_entry_t);

    uint32_t tss_base_low = base & 0xFFFF;
    uint32_t tss_base_mid = (base >> 16) & 0xFF;
    uint32_t tss_base_high = (base >> 24) & 0xFF;

    uint8_t* gdt_tss = (uint8_t*)0x800 + 40;
    gdt_tss[0] = (uint8_t)(limit & 0xFF);
    gdt_tss[1] = (uint8_t)((limit >> 8) & 0xFF);
    gdt_tss[2] = (uint8_t)(tss_base_low & 0xFF);
    gdt_tss[3] = (uint8_t)((tss_base_low >> 8) & 0xFF);
    gdt_tss[4] = (uint8_t)(tss_base_mid & 0xFF);
    gdt_tss[5] = 0x89;
    gdt_tss[6] = 0x60;
    gdt_tss[7] = (uint8_t)(tss_base_high & 0xFF);

    tss_flush();
}

void tss_set_kernel_stack(uint32_t stack) {
    tss.esp0 = stack;
}
