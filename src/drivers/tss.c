#include "drivers/tss.h"
#include "core/log.h"
#include "core/string.h"

static tss_entry_t tss;
static uint64_t gdt[6];

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

extern void tss_flush(void);

static void tss_load_gdt(uint32_t base, uint32_t limit) {
    gdt_ptr_t pointer;
    uint64_t tss_descriptor = 0;

    gdt[0] = 0;
    gdt[1] = 0x00CF9A000000FFFFULL;
    gdt[2] = 0x00CF92000000FFFFULL;
    gdt[3] = 0;
    gdt[4] = 0;

    tss_descriptor |= (uint64_t)(limit & 0xFFFF);
    tss_descriptor |= (uint64_t)(base & 0xFFFFFF) << 16;
    tss_descriptor |= (uint64_t)0x89 << 40;
    tss_descriptor |= (uint64_t)((limit >> 16) & 0x0F) << 48;
    tss_descriptor |= (uint64_t)((base >> 24) & 0xFF) << 56;
    gdt[5] = tss_descriptor;

    pointer.limit = sizeof(gdt) - 1;
    pointer.base = (uint32_t)&gdt;
    asm volatile("lgdt %0" : : "m"(pointer) : "memory");
    asm volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        : : : "ax", "memory");
}

void tss_init(void) {
    LOG_INFO("THRD", "Inicializando TSS");

    uint32_t base = (uint32_t)&tss;
    uint32_t limit = sizeof(tss_entry_t) - 1;

    kmemset(&tss, 0, sizeof(tss_entry_t));

    tss.ss0 = 0x10;
    tss.esp0 = 0x90000;
    tss.cs = 0x08;
    tss.ds = 0x10;
    tss.es = 0x10;
    tss.fs = 0x10;
    tss.gs = 0x10;
    tss.ss = 0x10;
    tss.iomap_base = sizeof(tss_entry_t);

    tss_load_gdt(base, limit);
    tss_flush();
    LOG_INFO("THRD", "TSS inicializada");
}

void tss_set_kernel_stack(uint32_t stack) {
    tss.esp0 = stack;
}
