#include "core/memory.h"
#include "core/video.h"
#include "core/panic.h"
#include "core/log.h"
#include "core/string.h"
#include "memory/paging.h"

static memory_info_t mem_info;
static uint8_t* pmm_owner_bitmap = 0;
static uint32_t pmm_owner_bitmap_size = 0;
static uint32_t pmm_owned_pages = 0;
static uint32_t pmm_allocation_failures = 0;
static uint32_t pmm_invalid_frees = 0;
static uint32_t heap_allocation_failures = 0;
static uint32_t heap_invalid_frees = 0;
static uint32_t heap_double_frees = 0;

/* O E820 pode retirar ate 128 KiB do topo dos 32 MiB para firmware/ACPI. */
#define FIRMWARE_TOP_RESERVE 0x00020000U
#define MIN_USABLE_MEMORY_END (MIN_PHYSICAL_MEMORY - FIRMWARE_TOP_RESERVE)

#if KERNEL_START != LOW_MEMORY_END
#error "Inicio do kernel deve coincidir com o fim da memoria baixa"
#endif

#if KERNEL_END != USER_SPACE_START
#error "Fim reservado do kernel deve coincidir com o inicio ZAPP"
#endif

#if HEAP_START != USER_SPACE_END
#error "Heap deve iniciar depois da janela virtual ZAPP"
#endif

#if PHYSICAL_IDENTITY_START != (HEAP_START + HEAP_SIZE)
#error "Inicio do mapa fisico deve coincidir com o fim do heap"
#endif

#if BOOT_TRANSITION_END > PMM_BITMAP_STORAGE_START || \
    PMM_BITMAP_STORAGE_END != KERNEL_STACK_START || \
    KERNEL_STACK_TOP >= LOW_MEMORY_END
#error "Mapa de memoria baixa invalido"
#endif

#if MIN_USABLE_MEMORY_END <= PHYSICAL_IDENTITY_START
#error "RAM minima nao deixa paginas livres para o PMM"
#endif

static uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static int memory_range_is_usable(uint32_t start, uint32_t end,
                                  uint32_t mmap_count) {
    for (uint32_t i = 0; i < mmap_count; i++) {
        mmap_entry_t* entry = &mem_info.mmap[i];
        uint64_t base = ((uint64_t)entry->base_high << 32) | entry->base_low;
        uint64_t range_end = base +
            (((uint64_t)entry->length_high << 32) | entry->length_low);

        if (range_end >= base && entry->type == 1 &&
            base <= start && range_end >= end) {
            return 1;
        }
    }
    return 0;
}

static void memory_find_total(uint32_t mmap_count) {
    mem_info.total_memory = 0;
    for (uint32_t i = 0; i < mmap_count; i++) {
        mmap_entry_t* entry = &mem_info.mmap[i];
        uint64_t base = ((uint64_t)entry->base_high << 32) | entry->base_low;
        uint64_t length = ((uint64_t)entry->length_high << 32) | entry->length_low;
        uint64_t end = base + length;

        if (entry->type != 1 || base > MAX_PHYSICAL_ADDRESS ||
            end <= base) continue;
        if (end > MAX_PHYSICAL_ADDRESS) end = MAX_PHYSICAL_ADDRESS;
        end &= ~((uint64_t)PAGE_SIZE - 1U);
        if (end > mem_info.total_memory) mem_info.total_memory = (uint32_t)end;
    }
}

static void memory_mark_usable(uint32_t mmap_count) {
    for (uint32_t i = 0; i < mmap_count; i++) {
        mmap_entry_t* entry = &mem_info.mmap[i];
        if (entry->type != 1) continue;

        uint64_t base = ((uint64_t)entry->base_high << 32) | entry->base_low;
        uint64_t length = ((uint64_t)entry->length_high << 32) | entry->length_low;
        uint64_t end = base + length;

        if (base > MAX_PHYSICAL_ADDRESS || end <= base) continue;
        if (end > MAX_PHYSICAL_ADDRESS) end = MAX_PHYSICAL_ADDRESS;
        base = (base + PAGE_SIZE - 1U) & ~((uint64_t)PAGE_SIZE - 1U);
        end &= ~((uint64_t)PAGE_SIZE - 1U);
        if (end <= base) continue;

        uint32_t start_page = (uint32_t)(base / PAGE_SIZE);
        uint32_t end_page = (uint32_t)(end / PAGE_SIZE);
        if (end_page > mem_info.total_pages) end_page = mem_info.total_pages;
        for (uint32_t p = start_page; p < end_page; p++) {
            mem_info.bitmap[p / 8] &= (uint8_t)~(1U << (p % 8));
        }
    }
}

static void memory_reserve_range(uint32_t start, uint32_t end) {
    uint32_t start_page = start / PAGE_SIZE;
    uint32_t end_page = align_up(end, PAGE_SIZE) / PAGE_SIZE;
    if (end_page > mem_info.total_pages) end_page = mem_info.total_pages;
    for (uint32_t p = start_page; p < end_page; p++) {
        mem_info.bitmap[p / 8] |= (1U << (p % 8));
    }
}

static void memory_recount(void) {
    mem_info.free_pages = 0;
    for (uint32_t p = 0; p < mem_info.total_pages; p++) {
        if (!(mem_info.bitmap[p / 8] & (1U << (p % 8)))) {
            mem_info.free_pages++;
        }
    }
    mem_info.free_memory = mem_info.free_pages * PAGE_SIZE;
    mem_info.used_memory = mem_info.total_memory - mem_info.free_memory;
}

void memory_init(uint32_t mmap_addr) {
    uint32_t mmap_count;
    uint32_t bitmap_capacity;

    LOG_INFO("MEM", "Iniciando mapa de memoria");
    if (!mmap_addr) {
        LOG_ERROR("MEM", "Endereco do mapa E820 nulo");
        panic_memory("Mapa E820 nao foi recebido", 0, 0, 0, 0);
        return;
    }

    mmap_count = *(uint32_t*)(mmap_addr - 4);
    kmemset(&mem_info, 0, sizeof(mem_info));
    mem_info.mmap = (mmap_entry_t*)mmap_addr;
    mem_info.mmap_entries = mmap_count;
    if (mmap_count == 0) {
        LOG_ERROR("MEM", "Mapa E820 sem entradas");
        panic_memory("Mapa E820 vazio", 0, 0, 0, 0);
        return;
    }

    memory_find_total(mmap_count);
    mem_info.total_pages = mem_info.total_memory / PAGE_SIZE;
    if (mem_info.total_memory < MIN_USABLE_MEMORY_END) {
        LOG_ERROR("MEM", "RAM fisica abaixo do minimo suportado");
        panic_memory("ZephyrOS requer pelo menos 32 MiB", mmap_count,
                     mem_info.total_memory, 0, 0);
        return;
    }
    if (!memory_range_is_usable(PMM_BITMAP_STORAGE_START, KERNEL_STACK_TOP,
                                mmap_count) ||
        !memory_range_is_usable(KERNEL_START, KERNEL_END, mmap_count) ||
        !memory_range_is_usable(HEAP_START, HEAP_START + HEAP_SIZE,
                                mmap_count)) {
        LOG_ERROR("MEM", "Mapa E820 nao cobre o layout reservado");
        panic_memory("Layout fisico indisponivel", mmap_count,
                     mem_info.total_memory, 0, 0);
        return;
    }

    mem_info.bitmap_size = align_up((mem_info.total_pages + 7U) / 8U,
                                    PAGE_SIZE);
    bitmap_capacity = (PMM_BITMAP_STORAGE_END -
                       PMM_BITMAP_STORAGE_START) / 2U;
    if (mem_info.bitmap_size > bitmap_capacity) {
        LOG_ERROR("MEM", "Espaco insuficiente para os bitmaps do PMM");
        panic_memory("Bitmaps do PMM excedem a memoria baixa", mmap_count,
                     mem_info.total_memory, 0, 0);
        return;
    }

    mem_info.bitmap = (uint8_t*)PMM_BITMAP_STORAGE_START;
    pmm_owner_bitmap = mem_info.bitmap + mem_info.bitmap_size;
    pmm_owner_bitmap_size = mem_info.bitmap_size;
    kmemset(mem_info.bitmap, 0xFF, mem_info.bitmap_size);
    kmemset(pmm_owner_bitmap, 0, pmm_owner_bitmap_size);
    memory_mark_usable(mmap_count);
    memory_reserve_range(0, PHYSICAL_IDENTITY_START);
    memory_recount();
    if (mem_info.free_pages == 0) {
        LOG_ERROR("MEM", "Mapa E820 sem paginas livres mapeaveis");
        panic_memory("Nenhuma pagina livre foi encontrada", mmap_count,
                     mem_info.total_memory, 0, 0);
        return;
    }

    pmm_owned_pages = 0;
    pmm_allocation_failures = 0;
    pmm_invalid_frees = 0;
    heap_allocation_failures = 0;
    heap_invalid_frees = 0;
    heap_double_frees = 0;
    LOG_INFO("MEM", "Mapa de memoria inicializado");
}

static int pmm_is_ready(void) {
    return mem_info.bitmap != 0 && pmm_owner_bitmap != 0 &&
           pmm_owner_bitmap_size == mem_info.bitmap_size;
}

static int pmm_page_is_free(uint32_t page) {
    return !(mem_info.bitmap[page / 8] & (1U << (page % 8)));
}

static int pmm_page_is_owned(uint32_t page) {
    return pmm_owner_bitmap[page / 8] & (1U << (page % 8));
}

static void pmm_set_page_owned(uint32_t page, int owned) {
    uint8_t bit = (uint8_t)(1U << (page % 8));

    if (owned) {
        pmm_owner_bitmap[page / 8] |= bit;
    } else {
        pmm_owner_bitmap[page / 8] &= (uint8_t)~bit;
    }
}

static void pmm_claim_pages(uint32_t first_page, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t page = first_page + i;
        mem_info.bitmap[page / 8] |= (1U << (page % 8));
        pmm_set_page_owned(page, 1);
    }
    pmm_owned_pages += count;
    mem_info.free_pages -= count;
    mem_info.used_memory += count * PAGE_SIZE;
    mem_info.free_memory -= count * PAGE_SIZE;
}

static int pmm_validate_release(void* addr, uint32_t count) {
    uint32_t address;
    uint32_t first_page;

    if (!pmm_is_ready()) {
        LOG_ERROR("MEM", "PMM indisponivel para liberar paginas");
        pmm_invalid_frees++;
        return 0;
    }
    if (!addr || count == 0) {
        LOG_ERROR("MEM", "Liberacao de paginas nula ou vazia");
        pmm_invalid_frees++;
        return 0;
    }

    address = (uint32_t)addr;
    if ((address & (PAGE_SIZE - 1U)) != 0) {
        LOG_ERROR("MEM", "Liberacao de pagina desalinhada");
        pmm_invalid_frees++;
        return 0;
    }
    first_page = address / PAGE_SIZE;
    if (first_page >= mem_info.total_pages ||
        count > mem_info.total_pages - first_page) {
        LOG_ERROR("MEM", "Intervalo de paginas fora da RAM");
        pmm_invalid_frees++;
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!pmm_page_is_owned(first_page + i)) {
            LOG_ERROR("MEM", "Liberacao de pagina nao pertencente ao PMM");
            pmm_invalid_frees++;
            return 0;
        }
    }
    return 1;
}

static void pmm_release_pages(uint32_t first_page, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t page = first_page + i;
        mem_info.bitmap[page / 8] &= (uint8_t)~(1U << (page % 8));
        pmm_set_page_owned(page, 0);
    }
    pmm_owned_pages -= count;
    mem_info.free_pages += count;
    mem_info.used_memory -= count * PAGE_SIZE;
    mem_info.free_memory += count * PAGE_SIZE;
}

void* pmm_alloc_page(void) {
    if (!pmm_is_ready()) {
        LOG_ERROR("MEM", "PMM indisponivel para alocar pagina");
        pmm_allocation_failures++;
        return 0;
    }
    for (uint32_t i = 0; i < mem_info.total_pages; i++) {
        if (pmm_page_is_free(i)) {
            pmm_claim_pages(i, 1);
            return (void*)(i * PAGE_SIZE);
        }
    }
    LOG_ERROR("MEM", "PMM sem pagina livre");
    pmm_allocation_failures++;
    return 0;
}

void pmm_free_page(void* addr) {
    uint32_t page;

    if (!pmm_validate_release(addr, 1)) return;
    page = (uint32_t)addr / PAGE_SIZE;
    pmm_release_pages(page, 1);
}

void* pmm_alloc_pages(uint32_t count) {
    if (!pmm_is_ready()) {
        LOG_ERROR("MEM", "PMM indisponivel para alocar paginas");
        pmm_allocation_failures++;
        return 0;
    }
    if (count == 0 || count > mem_info.total_pages) {
        LOG_ERROR("MEM", "Quantidade de paginas invalida");
        pmm_allocation_failures++;
        return 0;
    }

    for (uint32_t i = 0; i <= mem_info.total_pages - count; i++) {
        int found = 1;
        for (uint32_t j = 0; j < count; j++) {
            if (!pmm_page_is_free(i + j)) {
                found = 0;
                break;
            }
        }
        if (found) {
            pmm_claim_pages(i, count);
            return (void*)(i * PAGE_SIZE);
        }
    }

    LOG_ERROR("MEM", "PMM sem intervalo contiguo livre");
    pmm_allocation_failures++;
    return 0;
}

void pmm_free_pages(void* addr, uint32_t count) {
    uint32_t page;

    if (!pmm_validate_release(addr, count)) return;
    page = (uint32_t)addr / PAGE_SIZE;
    pmm_release_pages(page, count);
}

#define HEAP_MAGIC         0x48454150
#define HEAP_MAGIC_ALIGNED 0x414C4947
#define HEAP_MAGIC_FREED   0x46524545
#define HEAP_ALIGNMENT     8U
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

static int heap_range_contains(uint32_t address, uint32_t size);

static int heap_block_is_consistent(heap_block_t* block,
                                    heap_block_t* expected_prev) {
    uint32_t block_address;
    uint32_t payload_address;
    uint32_t next_address;

    if (!block || !heap_range_contains((uint32_t)block,
                                      sizeof(heap_block_t))) {
        return 0;
    }
    if (block->magic != HEAP_MAGIC ||
        (block->free != 0 && block->free != 1) ||
        block->prev != expected_prev) {
        return 0;
    }

    block_address = (uint32_t)block;
    payload_address = block_address + sizeof(heap_block_t);
    if (!heap_range_contains(payload_address, block->size)) return 0;
    if (block->free && block->user_ptr) return 0;
    if (!block->free && (!block->user_ptr ||
        (uint32_t)block->user_ptr < payload_address ||
        (uint32_t)block->user_ptr >= payload_address + block->size)) {
        return 0;
    }

    next_address = payload_address + block->size;
    if (!block->next) return next_address == HEAP_END;
    return next_address < HEAP_END &&
           (uint32_t)block->next == next_address &&
           heap_range_contains((uint32_t)block->next, sizeof(heap_block_t));
}

static int heap_measure(memory_heap_stats_t* stats) {
    heap_block_t* block;
    heap_block_t* prev = 0;
    uint32_t max_blocks;

    if (!stats) return 0;

    stats->initialized = heap_base != 0;
    stats->valid = heap_base ? 1U : 0U;
    stats->allocation_failures = heap_allocation_failures;
    stats->invalid_frees = heap_invalid_frees;
    stats->double_frees = heap_double_frees;
    if (!heap_base) return 1;

    max_blocks = HEAP_SIZE / sizeof(heap_block_t) + 1U;
    for (block = heap_base; block && max_blocks > 0;
         prev = block, block = block->next, max_blocks--) {
        if (!heap_block_is_consistent(block, prev)) {
            LOG_ERROR("MEM", "Lista do heap corrompida durante inspecao");
            stats->valid = 0;
            return 0;
        }
        stats->total_bytes += block->size;
        if (block->free) {
            stats->free_blocks++;
            stats->free_bytes += block->size;
            if (block->size > stats->largest_free_block) {
                stats->largest_free_block = block->size;
            }
        } else {
            stats->allocated_blocks++;
            stats->used_bytes += block->size;
        }
    }
    if (block) {
        LOG_ERROR("MEM", "Lista do heap excedeu o limite de inspecao");
        stats->valid = 0;
        return 0;
    }
    if (stats->free_bytes != 0) {
        stats->fragmentation_percent =
            ((stats->free_bytes - stats->largest_free_block) * 100U) /
            stats->free_bytes;
    }
    return 1;
}

static uint32_t heap_align_size(uint32_t size) {
    uint32_t padding = HEAP_ALIGNMENT - 1U;

    if (size > 0xFFFFFFFFU - padding) return 0;
    return (size + padding) & ~padding;
}

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
    uint32_t aligned_size;
    heap_block_t* previous = 0;
    uint32_t max_blocks;

    if (size == 0 || size > HEAP_SIZE - sizeof(heap_block_t)) {
        LOG_ERROR("MEM", "Tamanho de alocacao invalido");
        heap_allocation_failures++;
        return 0;
    }

    aligned_size = heap_align_size(size);
    if (aligned_size == 0 || aligned_size > HEAP_SIZE - sizeof(heap_block_t)) {
        LOG_ERROR("MEM", "Alinhamento de alocacao invalido");
        heap_allocation_failures++;
        return 0;
    }

    heap_initialize();

    heap_block_t* curr = heap_base;
    max_blocks = HEAP_SIZE / sizeof(heap_block_t) + 1U;
    while (curr && max_blocks > 0) {
        if (!heap_block_is_consistent(curr, previous)) {
            LOG_ERROR("MEM", "Lista do heap corrompida durante alocacao");
            heap_allocation_failures++;
            return 0;
        }
        if (curr->free && curr->size >= aligned_size) {
            if (curr->size - aligned_size >= sizeof(heap_block_t) + HEAP_MIN_SPLIT) {
                heap_block_t* new_block = (heap_block_t*)((uint8_t*)curr +
                    sizeof(heap_block_t) + aligned_size);
                new_block->magic = HEAP_MAGIC;
                new_block->size = curr->size - aligned_size - sizeof(heap_block_t);
                new_block->free = 1;
                new_block->next = curr->next;
                new_block->prev = curr;
                new_block->user_ptr = 0;
                if (new_block->next) {
                    new_block->next->prev = new_block;
                }
                curr->next = new_block;
                curr->size = aligned_size;
            }
            curr->free = 0;
            curr->user_ptr = (uint8_t*)curr + sizeof(heap_block_t);
            return (void*)((uint8_t*)curr + sizeof(heap_block_t));
        }
        previous = curr;
        curr = curr->next;
        max_blocks--;
    }

    if (max_blocks == 0 && curr) {
        LOG_ERROR("MEM", "Lista do heap excedeu o limite durante alocacao");
    } else {
        LOG_ERROR("MEM", "Heap sem bloco livre suficiente");
    }
    heap_allocation_failures++;
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
        heap_allocation_failures++;
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
        heap_allocation_failures++;
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

static int heap_pointer_was_freed(void* ptr) {
    uint32_t address;
    heap_aligned_header_t* aligned_header;
    heap_block_t* block;

    if (!ptr) return 0;
    address = (uint32_t)ptr;
    if (!heap_range_contains(address, 1)) return 0;

    if ((address & (PAGE_SIZE - 1U)) == 0 &&
        address >= HEAP_START + sizeof(heap_aligned_header_t)) {
        aligned_header = (heap_aligned_header_t*)(address -
                                                  sizeof(heap_aligned_header_t));
        if (aligned_header->magic == HEAP_MAGIC_ALIGNED &&
            heap_range_contains((uint32_t)aligned_header->block,
                                sizeof(heap_block_t)) &&
            aligned_header->block->magic == HEAP_MAGIC_FREED) {
            return 1;
        }
    }
    if (address < HEAP_START + sizeof(heap_block_t)) return 0;
    block = (heap_block_t*)(address - sizeof(heap_block_t));
    return heap_range_contains((uint32_t)block, sizeof(heap_block_t)) &&
           block->magic == HEAP_MAGIC_FREED;
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
        if (heap_pointer_was_freed(ptr)) {
            LOG_ERROR("MEM", "Tentativa de liberar bloco ja liberado");
            heap_double_frees++;
            return;
        }
        LOG_ERROR("MEM", "kfree em ponteiro invalido");
        heap_invalid_frees++;
        return;
    }
    if (block->free) {
        LOG_ERROR("MEM", "Tentativa de liberar bloco ja liberado");
        heap_double_frees++;
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

void memory_get_heap_stats(memory_heap_stats_t* stats) {
    if (!stats) {
        LOG_ERROR("MEM", "Destino nulo ao consultar estatisticas do heap");
        return;
    }

    kmemset(stats, 0, sizeof(memory_heap_stats_t));
    heap_measure(stats);
}

void memory_get_pmm_stats(memory_pmm_stats_t* stats) {
    if (!stats) {
        LOG_ERROR("MEM", "Destino nulo ao consultar estatisticas do PMM");
        return;
    }

    kmemset(stats, 0, sizeof(memory_pmm_stats_t));
    stats->initialized = pmm_is_ready() ? 1U : 0U;
    stats->owned_pages = pmm_owned_pages;
    stats->allocation_failures = pmm_allocation_failures;
    stats->invalid_frees = pmm_invalid_frees;
}
