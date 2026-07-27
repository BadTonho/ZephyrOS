#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

#define PAGE_SIZE 4096U
#define LOW_MEMORY_END 0x00100000U
#define KERNEL_START 0x00100000U
#define KERNEL_END 0x00800000U
#define HEAP_START 0x01000000U
/* O framebuffer 1024x768x32 exige cerca de 3 MiB para o backbuffer. */
#define HEAP_SIZE 0x00400000U
#define PHYSICAL_IDENTITY_START (HEAP_START + HEAP_SIZE)
#define MIN_PHYSICAL_MEMORY 0x02000000U

#define BOOT_TRANSITION_START 0x00004000U
#define BOOT_TRANSITION_END 0x00010000U
#define PMM_BITMAP_STORAGE_START 0x00088000U
#define PMM_BITMAP_STORAGE_END 0x00098000U
#define KERNEL_STACK_START PMM_BITMAP_STORAGE_END
#define KERNEL_STACK_TOP 0x0009F000U
#define MAX_PHYSICAL_ADDRESS 0xFFFFFFFFULL

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

typedef struct {
    uint32_t total_bytes;
    uint32_t used_bytes;
    uint32_t free_bytes;
    uint32_t allocated_blocks;
    uint32_t free_blocks;
    uint32_t largest_free_block;
    uint32_t fragmentation_percent;
    uint32_t allocation_failures;
    uint32_t invalid_frees;
    uint32_t double_frees;
    uint8_t initialized;
    uint8_t valid;
} memory_heap_stats_t;

typedef struct {
    uint32_t owned_pages;
    uint32_t allocation_failures;
    uint32_t invalid_frees;
    uint8_t initialized;
} memory_pmm_stats_t;

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
uint32_t memory_get_total_pages(void);
uint32_t memory_get_free_pages(void);
uint32_t memory_get_mmap_entries(void);
void memory_get_heap_stats(memory_heap_stats_t* stats);
void memory_get_pmm_stats(memory_pmm_stats_t* stats);

#endif
