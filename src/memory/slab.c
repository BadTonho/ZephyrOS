#include "memory/slab.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "memory/paging.h"

#define KMEM_SLAB_STATE_EMPTY 0U
#define KMEM_SLAB_STATE_PARTIAL 1U
#define KMEM_SLAB_STATE_FULL 2U
#define KMEM_SLAB_LINK_NONE (-1)
#define KMEM_SLAB_BITMAP_WORDS \
    ((KMEM_SLAB_MAX_OBJECTS + 31U) / 32U)
#define KMEM_SLAB_TEST_OBJECTS 256U

typedef struct {
    kmem_cache_t* owner;
    uint8_t* memory;
    uint32_t pages;
    uint32_t object_count;
    uint32_t active_objects;
    uint16_t free_head;
    int16_t next;
    int16_t previous;
    uint8_t state;
    uint8_t used;
    uint32_t allocation_bitmap[KMEM_SLAB_BITMAP_WORDS];
    uint16_t free_next[KMEM_SLAB_MAX_OBJECTS];
} kmem_slab_t;

struct kmem_cache {
    char name[KMEM_CACHE_NAME_LENGTH];
    uint32_t object_size;
    uint32_t alignment;
    uint32_t object_stride;
    uint32_t objects_per_slab;
    uint32_t slab_pages;
    uint32_t active_objects;
    uint32_t capacity;
    uint32_t slab_count;
    uint32_t allocation_failures;
    uint32_t invalid_frees;
    uint32_t double_frees;
    int16_t full_head;
    int16_t partial_head;
    int16_t empty_head;
    uint8_t used;
};

static kmem_cache_t cache_table[KMEM_CACHE_MAX];
static kmem_slab_t slab_table[KMEM_SLAB_MAX];
static spinlock_t slab_lock;
static uint32_t cache_count;
static uint32_t slab_count;
static uint32_t global_allocation_failures;
static uint32_t global_invalid_frees;
static uint32_t global_double_frees;
static uint8_t slab_initialized;

static void slab_copy_text(char* destination, uint32_t capacity,
                           const char* source) {
    uint32_t index = 0U;

    if (!destination || capacity == 0U) return;
    while (source && source[index] && index + 1U < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static uint32_t slab_align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static int slab_power_of_two(uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

static int slab_cache_registered(const kmem_cache_t* cache) {
    uint32_t index;

    if (!cache) return 0;
    for (index = 0U; index < KMEM_CACHE_MAX; index++) {
        if (&cache_table[index] == cache && cache_table[index].used) return 1;
    }
    return 0;
}

static int slab_find_cache_slot(void) {
    uint32_t index;

    for (index = 0U; index < KMEM_CACHE_MAX; index++) {
        if (!cache_table[index].used) return (int)index;
    }
    return -1;
}

static int slab_find_free_record(void) {
    uint32_t index;

    for (index = 0U; index < KMEM_SLAB_MAX; index++) {
        if (!slab_table[index].used) return (int)index;
    }
    return -1;
}

static void slab_list_remove(kmem_cache_t* cache, int slab_index) {
    kmem_slab_t* slab = &slab_table[slab_index];
    int16_t* head;

    if (slab->state == KMEM_SLAB_STATE_FULL) head = &cache->full_head;
    else if (slab->state == KMEM_SLAB_STATE_PARTIAL) {
        head = &cache->partial_head;
    } else {
        head = &cache->empty_head;
    }
    if (slab->previous != KMEM_SLAB_LINK_NONE) {
        slab_table[slab->previous].next = slab->next;
    } else {
        *head = slab->next;
    }
    if (slab->next != KMEM_SLAB_LINK_NONE) {
        slab_table[slab->next].previous = slab->previous;
    }
    slab->next = KMEM_SLAB_LINK_NONE;
    slab->previous = KMEM_SLAB_LINK_NONE;
}

static void slab_list_add(kmem_cache_t* cache, int slab_index, uint8_t state) {
    kmem_slab_t* slab = &slab_table[slab_index];
    int16_t* head;

    slab->state = state;
    if (state == KMEM_SLAB_STATE_FULL) head = &cache->full_head;
    else if (state == KMEM_SLAB_STATE_PARTIAL) {
        head = &cache->partial_head;
    } else {
        head = &cache->empty_head;
    }
    slab->previous = KMEM_SLAB_LINK_NONE;
    slab->next = *head;
    if (*head != KMEM_SLAB_LINK_NONE) {
        slab_table[*head].previous = (int16_t)slab_index;
    }
    *head = (int16_t)slab_index;
}

static void slab_set_bit(kmem_slab_t* slab, uint32_t index, int used) {
    uint32_t word = index / 32U;
    uint32_t bit = index % 32U;

    if (used) slab->allocation_bitmap[word] |= 1U << bit;
    else slab->allocation_bitmap[word] &= ~(1U << bit);
}

static int slab_bit_is_set(const kmem_slab_t* slab, uint32_t index) {
    return (slab->allocation_bitmap[index / 32U] &
            (1U << (index % 32U))) != 0U;
}

static int slab_prepare_record(kmem_cache_t* cache) {
    int record_index;
    void* memory;
    uint32_t index;
    kmem_slab_t* slab;

    record_index = slab_find_free_record();
    if (record_index < 0) {
        LOG_ERROR("MEM", "Limite global de slabs atingido");
        return ERR_UNAVAILABLE;
    }
    memory = pmm_alloc_pages(cache->slab_pages);
    if (!memory) {
        LOG_ERROR("MEM", "Falha ao reservar paginas para slab");
        return ERR_MEM;
    }
    slab = &slab_table[record_index];
    kmemset(slab, 0, sizeof(kmem_slab_t));
    slab->owner = cache;
    slab->memory = (uint8_t*)memory;
    slab->pages = cache->slab_pages;
    slab->object_count = cache->objects_per_slab;
    slab->free_head = 0U;
    slab->next = KMEM_SLAB_LINK_NONE;
    slab->previous = KMEM_SLAB_LINK_NONE;
    slab->used = 1U;
    for (index = 0U; index < slab->object_count; index++) {
        slab->free_next[index] = index + 1U < slab->object_count ?
                                 (uint16_t)(index + 1U) :
                                 (uint16_t)KMEM_SLAB_MAX_OBJECTS;
    }
    slab_list_add(cache, record_index, KMEM_SLAB_STATE_EMPTY);
    cache->slab_count++;
    cache->capacity += slab->object_count;
    slab_count++;
    return OK;
}

static int slab_object_location(const kmem_cache_t* cache, const void* object,
                                int* slab_index_out, uint32_t* object_index) {
    uint32_t index;
    uint32_t address;
    uint32_t start;
    uint32_t end;
    uint32_t offset;

    if (!cache || !object || !slab_index_out || !object_index) return 0;
    address = (uint32_t)object;
    for (index = 0U; index < KMEM_SLAB_MAX; index++) {
        kmem_slab_t* slab = &slab_table[index];
        if (!slab->used || slab->owner != cache) continue;
        start = (uint32_t)slab->memory;
        end = start + cache->object_stride * slab->object_count;
        if (address < start || address >= end) continue;
        offset = address - start;
        if (offset % cache->object_stride != 0U) return 0;
        *slab_index_out = (int)index;
        *object_index = offset / cache->object_stride;
        return *object_index < slab->object_count;
    }
    return 0;
}

static int slab_validate_slab(const kmem_slab_t* slab) {
    uint32_t index;
    uint32_t active = 0U;
    uint32_t free_count = 0U;
    uint16_t current;

    if (!slab || !slab->used || !slab->owner || !slab->memory ||
        slab->object_count < KMEM_SLAB_MIN_OBJECTS ||
        slab->object_count > KMEM_SLAB_MAX_OBJECTS || slab->pages == 0U) {
        return 0;
    }
    for (index = 0U; index < slab->object_count; index++) {
        if (slab_bit_is_set(slab, index)) active++;
    }
    current = slab->free_head;
    while (current != KMEM_SLAB_MAX_OBJECTS && free_count <= slab->object_count) {
        if (current >= slab->object_count || slab_bit_is_set(slab, current)) {
            return 0;
        }
        free_count++;
        current = slab->free_next[current];
    }
    if (current != KMEM_SLAB_MAX_OBJECTS || active + free_count !=
        slab->object_count || active != slab->active_objects) return 0;
    if ((active == 0U && slab->state != KMEM_SLAB_STATE_EMPTY) ||
        (active > 0U && active < slab->object_count &&
         slab->state != KMEM_SLAB_STATE_PARTIAL) ||
        (active == slab->object_count && slab->state != KMEM_SLAB_STATE_FULL)) {
        return 0;
    }
    return 1;
}

static int slab_validate_list(const kmem_cache_t* cache, int16_t head,
                              uint8_t state, uint32_t expected) {
    int16_t previous = KMEM_SLAB_LINK_NONE;
    uint32_t count = 0U;

    while (head != KMEM_SLAB_LINK_NONE && count <= KMEM_SLAB_MAX) {
        kmem_slab_t* slab;

        if (head < 0 || (uint32_t)head >= KMEM_SLAB_MAX) return 0;
        slab = &slab_table[head];
        if (!slab->used || slab->owner != cache || slab->state != state ||
            slab->previous != previous) return 0;
        previous = head;
        head = slab->next;
        count++;
    }
    return head == KMEM_SLAB_LINK_NONE && count == expected;
}

int kmem_cache_init(void) {
    if (slab_initialized) return OK;
    LOG_INFO("MEM", "Inicializando registrador SLAB");
    kmemset(cache_table, 0, sizeof(cache_table));
    kmemset(slab_table, 0, sizeof(slab_table));
    spinlock_init(&slab_lock);
    cache_count = 0U;
    slab_count = 0U;
    global_allocation_failures = 0U;
    global_invalid_frees = 0U;
    global_double_frees = 0U;
    slab_initialized = 1U;
    LOG_INFO("MEM", "Registrador SLAB inicializado");
    return OK;
}

kmem_cache_t* kmem_cache_create(const char* name, uint32_t object_size,
                                uint32_t alignment) {
    uint32_t stride;
    uint32_t required_bytes;
    uint32_t pages;
    uint32_t objects;
    int slot;
    kmem_cache_t* cache;

    if (!slab_initialized) {
        LOG_ERROR("MEM", "SLAB nao inicializado ao criar cache");
        return 0;
    }
    if (!name || !name[0] || object_size == 0U || !slab_power_of_two(alignment) ||
        alignment > PAGE_SIZE) {
        LOG_ERROR("MEM", "Parametros invalidos ao criar cache SLAB");
        return 0;
    }
    if (object_size > 0xFFFFFFFFU - alignment + 1U) {
        LOG_ERROR("MEM", "Tamanho de objeto excede o limite do cache");
        return 0;
    }
    stride = slab_align_up(object_size, alignment);
    if (stride == 0U || stride > 0xFFFFFFFFU / KMEM_SLAB_MIN_OBJECTS) {
        LOG_ERROR("MEM", "Stride invalido no cache SLAB");
        return 0;
    }
    required_bytes = stride * KMEM_SLAB_MIN_OBJECTS;
    if (required_bytes > 0xFFFFFFFFU - (PAGE_SIZE - 1U)) {
        LOG_ERROR("MEM", "Cache SLAB excede o endereco de 32 bits");
        return 0;
    }
    pages = (required_bytes + PAGE_SIZE - 1U) / PAGE_SIZE;
    if (pages == 0U || pages > 0xFFFFFFFFU / PAGE_SIZE) {
        LOG_ERROR("MEM", "Quantidade de paginas invalida no cache SLAB");
        return 0;
    }
    objects = (pages * PAGE_SIZE) / stride;
    if (objects > KMEM_SLAB_MAX_OBJECTS) objects = KMEM_SLAB_MAX_OBJECTS;
    if (objects < KMEM_SLAB_MIN_OBJECTS) {
        LOG_ERROR("MEM", "Cache SLAB nao comporta oito objetos");
        return 0;
    }
    slot = slab_find_cache_slot();
    if (slot < 0 || cache_count >= KMEM_CACHE_MAX) {
        LOG_ERROR("MEM", "Limite de caches SLAB atingido");
        return 0;
    }
    for (uint32_t index = 0U; index < KMEM_CACHE_MAX; index++) {
        if (cache_table[index].used &&
            kstrcmp(cache_table[index].name, name) == 0) {
            LOG_ERROR("MEM", "Nome de cache SLAB duplicado");
            return 0;
        }
    }
    cache = &cache_table[slot];
    kmemset(cache, 0, sizeof(kmem_cache_t));
    slab_copy_text(cache->name, KMEM_CACHE_NAME_LENGTH, name);
    cache->object_size = object_size;
    cache->alignment = alignment;
    cache->object_stride = stride;
    cache->objects_per_slab = objects;
    cache->slab_pages = pages;
    cache->full_head = KMEM_SLAB_LINK_NONE;
    cache->partial_head = KMEM_SLAB_LINK_NONE;
    cache->empty_head = KMEM_SLAB_LINK_NONE;
    cache->used = 1U;
    cache_count++;
    return cache;
}

void* kmem_cache_alloc(kmem_cache_t* cache) {
    int slab_index;
    kmem_slab_t* slab;
    uint32_t object_index;
    void* object;

    if (!slab_initialized || !slab_cache_registered(cache)) {
        LOG_ERROR("MEM", "Cache SLAB invalido na alocacao");
        global_allocation_failures++;
        return 0;
    }
    if (!paging_is_ready()) {
        LOG_ERROR("MEM", "Paging inativo para alocacao de slab");
        cache->allocation_failures++;
        global_allocation_failures++;
        return 0;
    }
    spinlock_acquire(&slab_lock);
    slab_index = cache->partial_head;
    if (slab_index == KMEM_SLAB_LINK_NONE) slab_index = cache->empty_head;
    if (slab_index == KMEM_SLAB_LINK_NONE) {
        int result = slab_prepare_record(cache);
        if (result != OK) {
            cache->allocation_failures++;
            global_allocation_failures++;
            spinlock_release(&slab_lock);
            LOG_ERROR("MEM", "Falha ao criar slab para cache");
            return 0;
        }
        slab_index = cache->empty_head;
    }
    slab = &slab_table[slab_index];
    object_index = slab->free_head;
    if (object_index >= slab->object_count) {
        spinlock_release(&slab_lock);
        LOG_ERROR("MEM", "Freelist SLAB inconsistente");
        cache->allocation_failures++;
        global_allocation_failures++;
        return 0;
    }
    slab->free_head = slab->free_next[object_index];
    slab_set_bit(slab, object_index, 1);
    slab->active_objects++;
    cache->active_objects++;
    if (slab->active_objects == 1U) {
        slab_list_remove(cache, slab_index);
        slab_list_add(cache, slab_index, KMEM_SLAB_STATE_PARTIAL);
    }
    if (slab->active_objects == slab->object_count) {
        slab_list_remove(cache, slab_index);
        slab_list_add(cache, slab_index, KMEM_SLAB_STATE_FULL);
    }
    object = slab->memory + object_index * cache->object_stride;
    spinlock_release(&slab_lock);
    kmemset(object, 0, cache->object_size);
    return object;
}

void kmem_cache_free(kmem_cache_t* cache, void* object) {
    int slab_index;
    uint32_t object_index;
    kmem_slab_t* slab;
    uint8_t previous_state;

    if (!slab_initialized || !slab_cache_registered(cache) || !object) {
        LOG_ERROR("MEM", "Objeto invalido na liberacao SLAB");
        global_invalid_frees++;
        return;
    }
    spinlock_acquire(&slab_lock);
    if (!slab_object_location(cache, object, &slab_index, &object_index)) {
        cache->invalid_frees++;
        global_invalid_frees++;
        spinlock_release(&slab_lock);
        LOG_ERROR("MEM", "Ponteiro externo ou desalinhado no SLAB");
        return;
    }
    slab = &slab_table[slab_index];
    if (!slab_bit_is_set(slab, object_index)) {
        cache->double_frees++;
        global_double_frees++;
        spinlock_release(&slab_lock);
        LOG_ERROR("MEM", "Double free detectado no SLAB");
        return;
    }
    previous_state = slab->state;
    if (previous_state == KMEM_SLAB_STATE_FULL ||
        (previous_state == KMEM_SLAB_STATE_PARTIAL &&
         slab->active_objects == 1U)) {
        slab_list_remove(cache, slab_index);
    }
    slab_set_bit(slab, object_index, 0);
    slab->free_next[object_index] = slab->free_head;
    slab->free_head = (uint16_t)object_index;
    slab->active_objects--;
    cache->active_objects--;
    if (slab->active_objects == 0U) {
        slab_list_add(cache, slab_index, KMEM_SLAB_STATE_EMPTY);
    } else if (previous_state == KMEM_SLAB_STATE_FULL) {
        slab_list_add(cache, slab_index, KMEM_SLAB_STATE_PARTIAL);
    }
    spinlock_release(&slab_lock);
}

int kmem_cache_destroy(kmem_cache_t* cache) {
    uint32_t index;

    if (!slab_initialized || !slab_cache_registered(cache)) {
        LOG_ERROR("MEM", "Cache SLAB invalido na destruicao");
        return ERR_INVALID;
    }
    spinlock_acquire(&slab_lock);
    if (cache->active_objects != 0U) {
        spinlock_release(&slab_lock);
        LOG_ERROR("MEM", "Destruicao de cache SLAB com objetos ativos");
        return ERR_STATE;
    }
    for (index = 0U; index < KMEM_SLAB_MAX; index++) {
        kmem_slab_t* slab = &slab_table[index];
        if (!slab->used || slab->owner != cache) continue;
        slab_list_remove(cache, (int)index);
        pmm_free_pages(slab->memory, slab->pages);
        kmemset(slab, 0, sizeof(kmem_slab_t));
        slab_count--;
    }
    kmemset(cache, 0, sizeof(kmem_cache_t));
    cache_count--;
    spinlock_release(&slab_lock);
    return OK;
}

static void slab_copy_info(const kmem_cache_t* cache, kmem_cache_info_t* info) {
    slab_copy_text(info->name, KMEM_CACHE_NAME_LENGTH, cache->name);
    info->object_size = cache->object_size;
    info->alignment = cache->alignment;
    info->object_stride = cache->object_stride;
    info->objects_per_slab = cache->objects_per_slab;
    info->active_objects = cache->active_objects;
    info->capacity = cache->capacity;
    info->slabs = cache->slab_count;
    info->pages = cache->slab_count * cache->slab_pages;
    info->allocation_failures = cache->allocation_failures;
    info->invalid_frees = cache->invalid_frees;
    info->double_frees = cache->double_frees;
    info->initialized = cache->used;
}

int kmem_cache_get_info(const kmem_cache_t* cache, kmem_cache_info_t* info) {
    if (!info) {
        LOG_ERROR("MEM", "Destino nulo nas informacoes do cache SLAB");
        return ERR_NULL;
    }
    if (!slab_cache_registered(cache)) {
        LOG_ERROR("MEM", "Cache SLAB invalido nas informacoes");
        return ERR_INVALID;
    }
    spinlock_acquire(&slab_lock);
    kmemset(info, 0, sizeof(kmem_cache_info_t));
    slab_copy_info(cache, info);
    spinlock_release(&slab_lock);
    return OK;
}

int kmem_cache_get_info_at(uint32_t index, kmem_cache_info_t* info) {
    if (index >= KMEM_CACHE_MAX) {
        LOG_ERROR("MEM", "Indice invalido nas informacoes SLAB");
        return ERR_INVALID;
    }
    if (!info) {
        LOG_ERROR("MEM", "Destino nulo nas informacoes SLAB");
        return ERR_NULL;
    }
    kmemset(info, 0, sizeof(kmem_cache_info_t));
    if (!cache_table[index].used) return OK;
    spinlock_acquire(&slab_lock);
    slab_copy_info(&cache_table[index], info);
    spinlock_release(&slab_lock);
    return OK;
}

uint32_t kmem_cache_get_count(void) {
    return cache_count;
}

int kmem_cache_owns(const kmem_cache_t* cache, const void* object) {
    int slab_index;
    uint32_t object_index;
    int result;

    if (!slab_cache_registered(cache) || !object) return 0;
    spinlock_acquire(&slab_lock);
    result = slab_object_location(cache, object, &slab_index, &object_index);
    spinlock_release(&slab_lock);
    return result;
}

int kmem_cache_validate(void) {
    uint32_t index;

    if (!slab_initialized) {
        LOG_ERROR("MEM", "SLAB nao inicializado na validacao");
        return ERR_UNAVAILABLE;
    }
    spinlock_acquire(&slab_lock);
    for (index = 0U; index < KMEM_SLAB_MAX; index++) {
        if (slab_table[index].used && !slab_validate_slab(&slab_table[index])) {
            spinlock_release(&slab_lock);
            LOG_ERROR("MEM", "Metadados de slab invalidos");
            return ERR_STATE;
        }
    }
    for (index = 0U; index < KMEM_CACHE_MAX; index++) {
        kmem_cache_t* cache = &cache_table[index];
        uint32_t active = 0U;
        uint32_t capacity = 0U;
        uint32_t slabs = 0U;
        uint32_t full = 0U;
        uint32_t partial = 0U;
        uint32_t empty = 0U;
        if (!cache->used) continue;
        for (uint32_t slab_index = 0U; slab_index < KMEM_SLAB_MAX;
             slab_index++) {
            if (slab_table[slab_index].used &&
                slab_table[slab_index].owner == cache) {
                active += slab_table[slab_index].active_objects;
                capacity += slab_table[slab_index].object_count;
                slabs++;
                if (slab_table[slab_index].state == KMEM_SLAB_STATE_FULL) full++;
                else if (slab_table[slab_index].state == KMEM_SLAB_STATE_PARTIAL) partial++;
                else if (slab_table[slab_index].state == KMEM_SLAB_STATE_EMPTY) empty++;
                else {
                    spinlock_release(&slab_lock);
                    LOG_ERROR("MEM", "Estado de slab invalido");
                    return ERR_STATE;
                }
            }
        }
        if (!slab_validate_list(cache, cache->full_head,
                                KMEM_SLAB_STATE_FULL, full) ||
            !slab_validate_list(cache, cache->partial_head,
                                KMEM_SLAB_STATE_PARTIAL, partial) ||
            !slab_validate_list(cache, cache->empty_head,
                                KMEM_SLAB_STATE_EMPTY, empty) ||
            active != cache->active_objects || capacity != cache->capacity ||
            slabs != cache->slab_count) {
            spinlock_release(&slab_lock);
            LOG_ERROR("MEM", "Contadores de cache SLAB inconsistentes");
            return ERR_STATE;
        }
    }
    spinlock_release(&slab_lock);
    return OK;
}

void kmem_cache_get_stats(kmem_slab_stats_t* stats) {
    uint32_t index;

    if (!stats) {
        LOG_ERROR("MEM", "Destino nulo nas estatisticas SLAB");
        return;
    }
    kmemset(stats, 0, sizeof(kmem_slab_stats_t));
    stats->initialized = slab_initialized;
    stats->valid = slab_initialized && kmem_cache_validate() == OK;
    stats->caches = cache_count;
    stats->slabs = slab_count;
    stats->allocation_failures = global_allocation_failures;
    stats->invalid_frees = global_invalid_frees;
    stats->double_frees = global_double_frees;
    for (index = 0U; index < KMEM_CACHE_MAX; index++) {
        if (!cache_table[index].used) continue;
        stats->active_objects += cache_table[index].active_objects;
        stats->capacity += cache_table[index].capacity;
        stats->pages += cache_table[index].slab_count *
                        cache_table[index].slab_pages;
    }
}

int kmem_cache_self_test(void) {
    kmem_cache_t* cache;
    kmem_cache_info_t info;
    memory_pmm_stats_t pmm_before;
    memory_pmm_stats_t pmm_after;
    void* objects[KMEM_SLAB_TEST_OBJECTS];
    uint32_t index;
    uint32_t allocated = 0U;
    uint8_t external = 0U;
    int result;

    if (!paging_is_ready()) {
        LOG_ERROR("MEM", "Autoteste SLAB solicitado antes do paging");
        return ERR_STATE;
    }
    memory_get_pmm_stats(&pmm_before);
    cache = kmem_cache_create("slabtest", sizeof(uint32_t), 16U);
    if (!cache) return ERR_UNAVAILABLE;
    for (index = 0U; index < KMEM_SLAB_TEST_OBJECTS; index++) {
        objects[index] = kmem_cache_alloc(cache);
        if (!objects[index]) break;
        allocated++;
        if (((uint32_t)objects[index] & 15U) != 0U) external = 1U;
    }
    result = allocated == KMEM_SLAB_TEST_OBJECTS && !external ? OK : ERR_STATE;
    if (kmem_cache_get_info(cache, &info) != OK ||
        info.active_objects != allocated || info.capacity < allocated ||
        info.active_objects != info.capacity || info.slabs < 2U) {
        result = ERR_STATE;
    }
    for (index = 0U; index < allocated; index += 2U) {
        kmem_cache_free(cache, objects[index]);
    }
    if (kmem_cache_get_info(cache, &info) != OK ||
        info.active_objects != allocated / 2U ||
        info.active_objects == info.capacity) {
        result = ERR_STATE;
    }
    for (index = 0U; index < allocated; index += 2U) {
        objects[index] = kmem_cache_alloc(cache);
        if (!objects[index]) result = ERR_STATE;
    }
    if (allocated > 0U) {
        uint32_t value = 0U;
        if (kmem_cache_destroy(cache) != ERR_STATE) result = ERR_STATE;
        kmem_cache_free(cache, (uint8_t*)objects[0] + 1U);
        kmem_cache_free(cache, &value);
        kmem_cache_free(cache, objects[0]);
        kmem_cache_free(cache, objects[0]);
        if (kmem_cache_get_info(cache, &info) != OK ||
            info.invalid_frees == 0U || info.double_frees == 0U) {
            result = ERR_STATE;
        }
    }
    for (index = 1U; index < allocated; index++) kmem_cache_free(cache,
                                                                   objects[index]);
    if (kmem_cache_get_info(cache, &info) != OK || info.active_objects != 0U) {
        result = ERR_STATE;
    }
    if (kmem_cache_destroy(cache) != OK) result = ERR_STATE;
    memory_get_pmm_stats(&pmm_after);
    if (!pmm_before.initialized || !pmm_after.initialized ||
        pmm_before.owned_pages != pmm_after.owned_pages) {
        result = ERR_STATE;
    }
    if (kmem_cache_validate() != OK) result = ERR_STATE;
    if (result != OK) LOG_ERROR("MEM", "Autoteste SLAB falhou");
    else LOG_INFO("MEM", "Autoteste SLAB concluido");
    return result;
}
