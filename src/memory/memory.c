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

    /* O heap e o backbuffer vivem nessa faixa e nao podem ser reutilizados pelo PMM. */
    uint32_t heap_start_page = HEAP_START / PAGE_SIZE;
    uint32_t heap_end_page = (HEAP_START + HEAP_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    if (heap_start_page < mem_info.total_pages) {
        if (heap_end_page > mem_info.total_pages) {
            heap_end_page = mem_info.total_pages;
        }
        for (uint32_t p = heap_start_page; p < heap_end_page; p++) {
            mem_info.bitmap[p / 8] |= (1 << (p % 8));
        }
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
#define HEAP_MAGIC_FREED   0x46524545
#define HEAP_MIN_SPLIT     16
#define HEAP_END           (HEAP_START + HEAP_SIZE)

typedef struct heap_block {
    uint32_t magic;
    uint32_t size;
    int free;
    struct heap_block* next;
    struct heap_block* prev;
    void* user_ptr;
} heap_block_t;

typedef struct heap_aligned_header {
    uint32_t magic;
    heap_block_t* block;
} heap_aligned_header_t;

static heap_block_t* heap_base = 0;

static int heap_range_contains(uint32_t address, uint32_t size) {
    if (address < HEAP_START || address > HEAP_END) return 0;
    return size <= HEAP_END - address;
}

static void heap_initialize(void) {
    if (heap_base) return;

    heap_base = (heap_block_t*)HEAP_START;
    heap_base->magic = HEAP_MAGIC;
    heap_base->size = HEAP_SIZE - sizeof(heap_block_t);
    heap_base->free = 1;
    heap_base->next = 0;
    heap_base->prev = 0;
    heap_base->user_ptr = 0;
}

static int heap_block_matches(heap_block_t* block, void* user_ptr) {
    uint32_t block_address;
    uint32_t payload_address;
    uint32_t user_address;

    if (!block || !user_ptr) return 0;

    block_address = (uint32_t)block;
    if (!heap_range_contains(block_address, sizeof(heap_block_t))) return 0;
    if (block->magic != HEAP_MAGIC || block->user_ptr != user_ptr) return 0;

    payload_address = block_address + sizeof(heap_block_t);
    if (!heap_range_contains(payload_address, block->size)) return 0;

    user_address = (uint32_t)user_ptr;
    return user_address >= payload_address &&
           user_address < payload_address + block->size;
}

static void* kmalloc_internal(uint32_t size) {
    if (size == 0 || size > HEAP_SIZE - sizeof(heap_block_t)) {
        LOG_ERROR("MEM", "Tamanho de alocacao invalido");
        return 0;
    }

    heap_initialize();

    heap_block_t* curr = heap_base;
    while (curr) {
        if (curr->magic != HEAP_MAGIC) {
            LOG_ERROR("MEM", "Lista do heap corrompida durante alocacao");
            return 0;
        }
        if (curr->free && curr->size >= size) {
            if (curr->size - size >= sizeof(heap_block_t) + HEAP_MIN_SPLIT) {
                heap_block_t* new_block = (heap_block_t*)((uint8_t*)curr + sizeof(heap_block_t) + size);
                new_block->magic = HEAP_MAGIC;
                new_block->size = curr->size - size - sizeof(heap_block_t);
                new_block->free = 1;
                new_block->next = curr->next;
                new_block->prev = curr;
                new_block->user_ptr = 0;
                if (new_block->next) {
                    new_block->next->prev = new_block;
                }
                curr->next = new_block;
                curr->size = size;
            }
            curr->free = 0;
            curr->user_ptr = (uint8_t*)curr + sizeof(heap_block_t);
            return (void*)((uint8_t*)curr + sizeof(heap_block_t));
        }
        curr = curr->next;
    }

    LOG_ERROR("MEM", "Heap sem bloco livre suficiente");
    return 0;
}

void* kmalloc(uint32_t size) {
    return kmalloc_internal(size);
}

void* kmalloc_aligned(uint32_t size) {
    uint32_t max_size = HEAP_SIZE - sizeof(heap_block_t) -
                        PAGE_SIZE - sizeof(heap_aligned_header_t);
    uint32_t total_size;
    uint32_t raw_address;
    uint32_t aligned_address;
    void* raw_ptr;
    heap_block_t* block;
    heap_aligned_header_t* header;

    if (size == 0 || size > max_size) {
        LOG_ERROR("MEM", "Tamanho de alocacao alinhada invalido");
        return 0;
    }

    total_size = size + PAGE_SIZE + sizeof(heap_aligned_header_t);
    raw_ptr = kmalloc_internal(total_size);
    if (!raw_ptr) {
        LOG_ERROR("MEM", "Falha ao alocar bloco alinhado");
        return 0;
    }

    raw_address = (uint32_t)raw_ptr;
    aligned_address = align_up(raw_address + sizeof(heap_aligned_header_t), PAGE_SIZE);
    if (!aligned_address || !heap_range_contains(aligned_address, size)) {
        kfree(raw_ptr);
        raw_ptr = 0;
        LOG_ERROR("MEM", "Endereco alinhado fora dos limites do heap");
        return 0;
    }

    block = (heap_block_t*)(raw_address - sizeof(heap_block_t));
    header = (heap_aligned_header_t*)(aligned_address - sizeof(heap_aligned_header_t));
    header->magic = HEAP_MAGIC_ALIGNED;
    header->block = block;
    block->user_ptr = (void*)aligned_address;

    return (void*)aligned_address;
}

static heap_block_t* heap_block_from_pointer(void* ptr) {
    uint32_t address;
    heap_aligned_header_t* aligned_header;
    heap_block_t* block;

    if (!ptr) return 0;

    address = (uint32_t)ptr;
    if (!heap_range_contains(address, 1)) return 0;

    if ((address & (PAGE_SIZE - 1)) == 0 &&
        address >= HEAP_START + sizeof(heap_aligned_header_t)) {
        aligned_header = (heap_aligned_header_t*)(address - sizeof(heap_aligned_header_t));
        if (aligned_header->magic == HEAP_MAGIC_ALIGNED &&
            heap_block_matches(aligned_header->block, ptr)) {
            return aligned_header->block;
        }
    }

    if (address < HEAP_START + sizeof(heap_block_t)) return 0;
    block = (heap_block_t*)(address - sizeof(heap_block_t));
    if (heap_block_matches(block, ptr)) return block;
    return 0;
}

static void heap_merge_next(heap_block_t* block) {
    heap_block_t* next;

    if (!block) return;
    next = block->next;
    if (!next || !next->free) return;
    if (next->magic != HEAP_MAGIC) {
        LOG_ERROR("MEM", "Lista do heap corrompida ao unir blocos");
        return;
    }

    block->size += sizeof(heap_block_t) + next->size;
    block->next = next->next;
    if (block->next) block->next->prev = block;

    next->magic = HEAP_MAGIC_FREED;
    next->size = 0;
    next->free = 1;
    next->next = 0;
    next->prev = 0;
    next->user_ptr = 0;
}

void kfree(void* ptr) {
    heap_block_t* block;
    heap_block_t* prev;

    if (!ptr) return;

    block = heap_block_from_pointer(ptr);
    if (!block) {
        LOG_ERROR("MEM", "kfree em ponteiro invalido");
        return;
    }
    if (block->free) {
        LOG_ERROR("MEM", "Tentativa de liberar bloco ja liberado");
        return;
    }

    block->free = 1;
    block->user_ptr = 0;

    /* Os vizinhos fisicos ficam registrados no proprio cabecalho. */
    heap_merge_next(block);
    prev = block->prev;
    if (prev && prev->magic == HEAP_MAGIC && prev->free) {
        heap_merge_next(prev);
    }
}

uint32_t memory_get_total(void) { return mem_info.total_memory; }
uint32_t memory_get_free(void) { return mem_info.free_memory; }
uint32_t memory_get_used(void) { return mem_info.used_memory; }
uint32_t memory_get_total_pages(void) { return mem_info.total_pages; }
uint32_t memory_get_free_pages(void) { return mem_info.free_pages; }
uint32_t memory_get_mmap_entries(void) { return mem_info.mmap_entries; }
