#include "core/memory.h"
#include "core/video.h"
#include "core/panic.h"
#include "core/log.h"
#include "core/string.h"

static memory_info_t mem_info;

static uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

void memory_init(uint32_t mmap_addr) {
    LOG_INFO("MEM", "Iniciando mapa de memoria");

    if (!mmap_addr) {
        LOG_ERROR("MEM", "Endereco do mapa E820 nulo");
        panic_memory("Mapa E820 nao foi recebido", 0, 0, 0, 0);
        return;
    }

    uint32_t mmap_count = *(uint32_t*)(mmap_addr - 4);
    mem_info.mmap = (mmap_entry_t*)mmap_addr;
    mem_info.mmap_entries = mmap_count;

    if (mmap_count == 0) {
        LOG_ERROR("MEM", "Mapa E820 sem entradas");
        panic_memory("Mapa E820 vazio", mmap_count, 0, 0, 0);
        return;
    }

    mem_info.total_memory = 0;
    mem_info.free_memory = 0;

    for (uint32_t i = 0; i < mmap_count; i++) {
        mmap_entry_t* entry = &mem_info.mmap[i];
        uint64_t base = ((uint64_t)entry->base_high << 32) | entry->base_low;
        uint64_t length = ((uint64_t)entry->length_high << 32) | entry->length_low;
        uint64_t end = base + length;

        if (entry->type != 1 || base > MAX_PHYSICAL_ADDRESS) {
            continue;
        }

        if (end > MAX_PHYSICAL_ADDRESS) {
            end = MAX_PHYSICAL_ADDRESS;
        }

        if (base >= KERNEL_END && end > base) {
            mem_info.free_memory += (uint32_t)(end - base);
        }
        if ((uint32_t)end > mem_info.total_memory) {
            mem_info.total_memory = (uint32_t)end;
        }
    }

    mem_info.total_pages = mem_info.total_memory / PAGE_SIZE;
    mem_info.free_pages = mem_info.free_memory / PAGE_SIZE;

    if (mem_info.total_memory <= KERNEL_END || mem_info.free_memory == 0) {
        LOG_ERROR("MEM", "Mapa E820 sem memoria utilizavel");
        panic_memory("Nenhuma pagina livre foi encontrada", mmap_count,
                     mem_info.total_memory, mem_info.free_memory,
                     mem_info.free_pages);
        return;
    }

    mem_info.bitmap_size = align_up((mem_info.total_pages + 7) / 8, PAGE_SIZE);

    mem_info.bitmap = (uint8_t*)KERNEL_END;
    kmemset(mem_info.bitmap, 0xFF, mem_info.bitmap_size);

    for (uint32_t i = 0; i < mmap_count; i++) {
        mmap_entry_t* entry = &mem_info.mmap[i];
        if (entry->type != 1) continue;

        uint64_t base = ((uint64_t)entry->base_high << 32) | entry->base_low;
        uint64_t length = ((uint64_t)entry->length_high << 32) | entry->length_low;
        uint64_t end = base + length;

        if (base > MAX_PHYSICAL_ADDRESS) continue;
        if (end > MAX_PHYSICAL_ADDRESS) end = MAX_PHYSICAL_ADDRESS;
        if (end <= base) continue;
        length = end - base;

        if (base < KERNEL_END) {
            uint64_t new_base = align_up(KERNEL_END, PAGE_SIZE);
            if (new_base >= base + length) continue;
            length -= (new_base - base);
            base = new_base;
        }

        uint32_t start_page = (uint32_t)(base / PAGE_SIZE);
        uint32_t end_page = (uint32_t)((base + length) / PAGE_SIZE);

        for (uint32_t p = start_page; p < end_page; p++) {
            mem_info.bitmap[p / 8] &= ~(1 << (p % 8));
        }
    }

    uint32_t bitmap_end = KERNEL_END + mem_info.bitmap_size;
    uint32_t bitmap_pages = (bitmap_end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t p = 0; p < bitmap_pages; p++) {
        mem_info.bitmap[p / 8] |= (1 << (p % 8));
    }

    mem_info.used_memory = 0;
    for (uint32_t p = 0; p < mem_info.total_pages; p++) {
        if (mem_info.bitmap[p / 8] & (1 << (p % 8))) {
            mem_info.used_memory += PAGE_SIZE;
        }
    }
    mem_info.free_memory = mem_info.total_memory - mem_info.used_memory;
    mem_info.free_pages = mem_info.free_memory / PAGE_SIZE;
    LOG_INFO("MEM", "Mapa de memoria inicializado");
}

void* pmm_alloc_page(void) {
    for (uint32_t i = 0; i < mem_info.total_pages; i++) {
        if (!(mem_info.bitmap[i / 8] & (1 << (i % 8)))) {
            mem_info.bitmap[i / 8] |= (1 << (i % 8));
            mem_info.free_pages--;
            mem_info.used_memory += PAGE_SIZE;
            mem_info.free_memory -= PAGE_SIZE;
            return (void*)(i * PAGE_SIZE);
        }
    }
    return 0;
}

void pmm_free_page(void* addr) {
    uint32_t page = (uint32_t)addr / PAGE_SIZE;
    mem_info.bitmap[page / 8] &= ~(1 << (page % 8));
    mem_info.free_pages++;
    mem_info.used_memory -= PAGE_SIZE;
    mem_info.free_memory += PAGE_SIZE;
}

void* pmm_alloc_pages(uint32_t count) {
    for (uint32_t i = 0; i < mem_info.total_pages - count; i++) {
        int found = 1;
        for (uint32_t j = 0; j < count; j++) {
            if (mem_info.bitmap[(i + j) / 8] & (1 << ((i + j) % 8))) {
                found = 0;
                break;
            }
        }
        if (found) {
            for (uint32_t j = 0; j < count; j++) {
                mem_info.bitmap[(i + j) / 8] |= (1 << ((i + j) % 8));
            }
            mem_info.free_pages -= count;
            mem_info.used_memory += count * PAGE_SIZE;
            mem_info.free_memory -= count * PAGE_SIZE;
            return (void*)(i * PAGE_SIZE);
        }
    }
    return 0;
}

void pmm_free_pages(void* addr, uint32_t count) {
    uint32_t page = (uint32_t)addr / PAGE_SIZE;
    for (uint32_t i = 0; i < count; i++) {
        mem_info.bitmap[(page + i) / 8] &= ~(1 << ((page + i) % 8));
    }
    mem_info.free_pages += count;
    mem_info.used_memory -= count * PAGE_SIZE;
    mem_info.free_memory += count * PAGE_SIZE;
}

#define HEAP_MAGIC         0x48454150
#define HEAP_MAGIC_ALIGNED 0x414C4947

typedef struct heap_block {
    uint32_t magic;
    uint32_t size;
    int free;
    struct heap_block* next;
    struct heap_block* prev;
} heap_block_t;

static heap_block_t* heap_base = 0;

static void* kmalloc_internal(uint32_t size) {
    if (!heap_base) {
        heap_base = (heap_block_t*)HEAP_START;
        heap_base->magic = HEAP_MAGIC;
        heap_base->size = HEAP_SIZE - sizeof(heap_block_t);
        heap_base->free = 1;
        heap_base->next = 0;
        heap_base->prev = 0;
    }

    heap_block_t* curr = heap_base;
    while (curr) {
        if (curr->free && curr->size >= size) {
            if (curr->size > size + sizeof(heap_block_t) + 16) {
                heap_block_t* new_block = (heap_block_t*)((uint8_t*)curr + sizeof(heap_block_t) + size);
                new_block->magic = HEAP_MAGIC;
                new_block->size = curr->size - size - sizeof(heap_block_t);
                new_block->free = 1;
                new_block->next = curr->next;
                new_block->prev = curr;
                if (new_block->next) {
                    new_block->next->prev = new_block;
                }
                curr->next = new_block;
                curr->size = size;
            }
            curr->free = 0;
            return (void*)((uint8_t*)curr + sizeof(heap_block_t));
        }
        curr = curr->next;
    }

    return 0;
}

void* kmalloc(uint32_t size) {
    return kmalloc_internal(size);
}

void* kmalloc_aligned(uint32_t size) {
    uint32_t total_size = size + PAGE_SIZE + sizeof(heap_block_t);
    void* ptr = kmalloc_internal(total_size);
    if (!ptr) return 0;

    uint32_t addr = (uint32_t)ptr;
    uint32_t aligned = align_up(addr, PAGE_SIZE);

    if (aligned != addr) {
        heap_block_t* dummy = (heap_block_t*)(aligned - sizeof(heap_block_t));
        dummy->magic = HEAP_MAGIC_ALIGNED;
        dummy->next = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    }

    return (void*)aligned;
}

void kfree(void* ptr) {
    if (!ptr) return;
    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    
    if (block->magic == HEAP_MAGIC_ALIGNED) {
        block = block->next;
    }

    if (block->magic != HEAP_MAGIC) {
        LOG_ERROR("MEM", "kfree em ponteiro invalido (magic errado)");
        return;
    }

    block->free = 1;

    if (block->next && block->next->free) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }

    if (block->prev && block->prev->free) {
        block->prev->size += sizeof(heap_block_t) + block->size;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
    }
}

uint32_t memory_get_total(void) { return mem_info.total_memory; }
uint32_t memory_get_free(void) { return mem_info.free_memory; }
uint32_t memory_get_used(void) { return mem_info.used_memory; }
uint32_t memory_get_total_pages(void) { return mem_info.total_pages; }
uint32_t memory_get_free_pages(void) { return mem_info.free_pages; }
uint32_t memory_get_mmap_entries(void) { return mem_info.mmap_entries; }
