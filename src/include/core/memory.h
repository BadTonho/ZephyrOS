#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

#define PAGE_SIZE 4096
#define KERNEL_START 0x1000
#define KERNEL_END 0x20000
#define HEAP_START 0x20000
#define HEAP_SIZE 0x100000

#define MMAP_ENTRY_SIZE 24

typedef struct {
    uint32_t base_low;
    uint32_t base_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t type;
    uint32_t acpi;
} __attribute__((packed)) mmap_entry_t;

typedef struct {
    uint32_t total_memory;
    uint32_t free_memory;
    uint32_t used_memory;
    uint32_t total_pages;
    uint32_t free_pages;
    uint32_t bitmap_size;
    uint8_t* bitmap;
    uint32_t mmap_entries;
    mmap_entry_t* mmap;
} memory_info_t;

void memory_init(uint32_t mmap_addr);
void* pmm_alloc_page(void);
void pmm_free_page(void* addr);
void* pmm_alloc_pages(uint32_t count);
void pmm_free_pages(void* addr, uint32_t count);

void* kmalloc(uint32_t size);
void* kmalloc_aligned(uint32_t size);
void kfree(void* ptr);

uint32_t memory_get_total(void);
uint32_t memory_get_free(void);
uint32_t memory_get_used(void);

#endif
