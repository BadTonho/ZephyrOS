#include "fs/block_cache.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "core/wait.h"

#define BLOCK_CACHE_HASH_OFFSET 2166136261U
#define BLOCK_CACHE_HASH_PRIME 16777619U
#define BLOCK_CACHE_TEST_SECTOR_COUNT 128U
#define BLOCK_CACHE_TEST_MAX_TRANSFER 8U
#define BLOCK_CACHE_TEST_LBA 7U
#define BLOCK_CACHE_TEST_SEED_A 0x31U
#define BLOCK_CACHE_TEST_SEED_B 0xA7U

typedef struct {
    char device_id[BLOCK_DEVICE_ID_SIZE];
    uint32_t lba;
    uint16_t block_size;
    uint8_t data[BLOCK_CACHE_BLOCK_SIZE];
    block_cache_state_t state;
    uint32_t references;
    uint32_t pins;
    uint32_t generation;
    int status;
    uint32_t hash_next;
    uint32_t lru_previous;
    uint32_t lru_next;
    wait_queue_head_t waiters;
} block_cache_entry_t;

typedef struct {
    block_cache_entry_t* entry;
    uint32_t generation;
} block_cache_wait_context_t;

typedef enum {
    BLOCK_CACHE_LOOKUP_MISS = 0,
    BLOCK_CACHE_LOOKUP_HIT,
    BLOCK_CACHE_LOOKUP_WAIT,
    BLOCK_CACHE_LOOKUP_ERROR
} block_cache_lookup_result_t;

typedef struct {
    uint32_t reads;
    uint32_t writes;
    uint8_t seed;
    int forced_result;
} block_cache_test_context_t;

static block_cache_entry_t block_cache_entries[BLOCK_CACHE_CAPACITY];
static uint32_t block_cache_buckets[BLOCK_CACHE_HASH_BUCKETS];
static spinlock_t block_cache_lock;
static block_cache_stats_t block_cache_stats;
static uint32_t block_cache_lru_head;
static uint32_t block_cache_lru_tail;
static uint8_t block_cache_initialized;

static void block_cache_copy_text(char* destination, uint32_t capacity,
                                  const char* source) {
    uint32_t index = 0U;

    if (!destination || !capacity) return;
    if (!source) source = "";
    while (source[index] && index + 1U < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int block_cache_text_equal(const char* left, const char* right) {
    if (!left || !right) return 0;
    while (*left && *right) {
        if (*left != *right) return 0;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static int block_cache_text_terminated(const char* text, uint32_t capacity) {
    if (!text || !capacity) return 0;
    for (uint32_t index = 0U; index < capacity; index++) {
        if (!text[index]) return 1;
    }
    return 0;
}

static uint32_t block_cache_hash(const char* device_id, uint32_t lba,
                                 uint16_t block_size) {
    uint32_t hash = BLOCK_CACHE_HASH_OFFSET;

    for (uint32_t index = 0U; index < BLOCK_DEVICE_ID_SIZE && device_id[index];
         index++) {
        hash = (hash ^ (uint8_t)device_id[index]) * BLOCK_CACHE_HASH_PRIME;
    }
    hash = (hash ^ (uint8_t)lba) * BLOCK_CACHE_HASH_PRIME;
    hash = (hash ^ (uint8_t)(lba >> 8U)) * BLOCK_CACHE_HASH_PRIME;
    hash = (hash ^ (uint8_t)(lba >> 16U)) * BLOCK_CACHE_HASH_PRIME;
    hash = (hash ^ (uint8_t)(lba >> 24U)) * BLOCK_CACHE_HASH_PRIME;
    hash = (hash ^ (uint8_t)block_size) * BLOCK_CACHE_HASH_PRIME;
    hash = (hash ^ (uint8_t)(block_size >> 8U)) * BLOCK_CACHE_HASH_PRIME;
    return hash % BLOCK_CACHE_HASH_BUCKETS;
}

static void block_cache_next_generation(block_cache_entry_t* entry) {
    entry->generation++;
    if (!entry->generation) entry->generation = 1U;
}

static void block_cache_lru_remove_locked(uint32_t index) {
    block_cache_entry_t* entry = &block_cache_entries[index];

    if (block_cache_lru_head != index && block_cache_lru_tail != index &&
        entry->lru_previous == BLOCK_CACHE_INVALID_INDEX &&
        entry->lru_next == BLOCK_CACHE_INVALID_INDEX) {
        return;
    }

    if (entry->lru_previous != BLOCK_CACHE_INVALID_INDEX) {
        block_cache_entries[entry->lru_previous].lru_next = entry->lru_next;
    } else {
        block_cache_lru_head = entry->lru_next;
    }
    if (entry->lru_next != BLOCK_CACHE_INVALID_INDEX) {
        block_cache_entries[entry->lru_next].lru_previous =
            entry->lru_previous;
    } else {
        block_cache_lru_tail = entry->lru_previous;
    }
    entry->lru_previous = BLOCK_CACHE_INVALID_INDEX;
    entry->lru_next = BLOCK_CACHE_INVALID_INDEX;
}

static void block_cache_lru_push_front_locked(uint32_t index) {
    block_cache_entry_t* entry = &block_cache_entries[index];

    entry->lru_previous = BLOCK_CACHE_INVALID_INDEX;
    entry->lru_next = block_cache_lru_head;
    if (block_cache_lru_head != BLOCK_CACHE_INVALID_INDEX) {
        block_cache_entries[block_cache_lru_head].lru_previous = index;
    } else {
        block_cache_lru_tail = index;
    }
    block_cache_lru_head = index;
}

static void block_cache_hash_remove_locked(uint32_t index) {
    block_cache_entry_t* entry = &block_cache_entries[index];
    uint32_t bucket = block_cache_hash(entry->device_id, entry->lba,
                                       entry->block_size);
    uint32_t current = block_cache_buckets[bucket];
    uint32_t previous = BLOCK_CACHE_INVALID_INDEX;

    while (current != BLOCK_CACHE_INVALID_INDEX) {
        if (current == index) {
            if (previous == BLOCK_CACHE_INVALID_INDEX) {
                block_cache_buckets[bucket] = entry->hash_next;
            } else {
                block_cache_entries[previous].hash_next = entry->hash_next;
            }
            entry->hash_next = BLOCK_CACHE_INVALID_INDEX;
            return;
        }
        previous = current;
        current = block_cache_entries[current].hash_next;
    }
}

static void block_cache_hash_insert_locked(uint32_t index) {
    block_cache_entry_t* entry = &block_cache_entries[index];
    uint32_t bucket = block_cache_hash(entry->device_id, entry->lba,
                                       entry->block_size);

    entry->hash_next = block_cache_buckets[bucket];
    block_cache_buckets[bucket] = index;
}

static int block_cache_lru_state(block_cache_state_t state) {
    return state == BLOCK_CACHE_VALID || state == BLOCK_CACHE_ERROR;
}

static void block_cache_release_locked(uint32_t index) {
    block_cache_entry_t* entry = &block_cache_entries[index];

    if (entry->state == BLOCK_CACHE_FREE) return;
    block_cache_hash_remove_locked(index);
    if (block_cache_lru_state(entry->state)) {
        block_cache_lru_remove_locked(index);
    }
    entry->device_id[0] = '\0';
    entry->lba = 0U;
    entry->block_size = 0U;
    entry->state = BLOCK_CACHE_FREE;
    entry->references = 0U;
    entry->pins = 0U;
    entry->status = ERR_STATE;
    entry->hash_next = BLOCK_CACHE_INVALID_INDEX;
    entry->lru_previous = BLOCK_CACHE_INVALID_INDEX;
    entry->lru_next = BLOCK_CACHE_INVALID_INDEX;
    block_cache_next_generation(entry);
}

static uint32_t block_cache_find_locked(const char* device_id, uint32_t lba,
                                        uint16_t block_size) {
    uint32_t bucket = block_cache_hash(device_id, lba, block_size);
    uint32_t index = block_cache_buckets[bucket];

    while (index != BLOCK_CACHE_INVALID_INDEX) {
        block_cache_entry_t* entry = &block_cache_entries[index];

        if (block_cache_text_equal(entry->device_id, device_id) &&
            entry->lba == lba && entry->block_size == block_size) {
            return index;
        }
        index = entry->hash_next;
    }
    return BLOCK_CACHE_INVALID_INDEX;
}

static int block_cache_entry_eligible(const block_cache_entry_t* entry) {
    if (!entry || entry->references || entry->pins) return 0;
    return entry->state == BLOCK_CACHE_VALID ||
           entry->state == BLOCK_CACHE_ERROR;
}

static uint32_t block_cache_choose_victim_locked(void) {
    for (uint32_t index = 0U; index < BLOCK_CACHE_CAPACITY; index++) {
        if (block_cache_entries[index].state == BLOCK_CACHE_FREE) return index;
    }
    for (uint32_t index = block_cache_lru_tail;
         index != BLOCK_CACHE_INVALID_INDEX;
         index = block_cache_entries[index].lru_previous) {
        if (block_cache_entry_eligible(&block_cache_entries[index])) {
            return index;
        }
    }
    return BLOCK_CACHE_INVALID_INDEX;
}

static uint32_t block_cache_reserve_run_locked(const char* device_id,
                                               uint32_t lba,
                                               uint32_t max_count,
                                               uint32_t* indexes) {
    uint32_t reserved = 0U;

    while (reserved < max_count && reserved < BLOCK_CACHE_CAPACITY) {
        uint32_t current_lba = lba + reserved;
        uint32_t index = block_cache_find_locked(device_id, current_lba,
                                                 BLOCK_CACHE_BLOCK_SIZE);
        block_cache_entry_t* entry;

        if (index != BLOCK_CACHE_INVALID_INDEX) {
            entry = &block_cache_entries[index];
            if (entry->state == BLOCK_CACHE_ERROR &&
                block_cache_entry_eligible(entry)) {
                block_cache_release_locked(index);
            } else {
                break;
            }
        }
        index = block_cache_choose_victim_locked();
        if (index == BLOCK_CACHE_INVALID_INDEX) break;
        entry = &block_cache_entries[index];
        if (entry->state != BLOCK_CACHE_FREE) {
            block_cache_release_locked(index);
            block_cache_stats.evictions++;
        }
        block_cache_next_generation(entry);
        block_cache_copy_text(entry->device_id, BLOCK_DEVICE_ID_SIZE,
                              device_id);
        entry->lba = current_lba;
        entry->block_size = BLOCK_CACHE_BLOCK_SIZE;
        entry->state = BLOCK_CACHE_READING;
        entry->references = 0U;
        entry->pins = 0U;
        entry->status = ERR_STATE;
        entry->hash_next = BLOCK_CACHE_INVALID_INDEX;
        entry->lru_previous = BLOCK_CACHE_INVALID_INDEX;
        entry->lru_next = BLOCK_CACHE_INVALID_INDEX;
        block_cache_hash_insert_locked(index);
        indexes[reserved++] = index;
        block_cache_stats.misses++;
    }
    return reserved;
}

static int block_cache_wait_condition(void* context, uint8_t* out_ready) {
    block_cache_wait_context_t* wait = (block_cache_wait_context_t*)context;

    if (!wait || !wait->entry || !out_ready) {
        LOG_ERROR("BLKCACHE", "Contexto invalido na condicao de espera");
        return ERR_NULL;
    }
    spinlock_acquire(&block_cache_lock);
    *out_ready = wait->entry->generation != wait->generation ||
                 wait->entry->state != BLOCK_CACHE_READING;
    spinlock_release(&block_cache_lock);
    return OK;
}

static int block_cache_wait_for_entry(block_cache_entry_t* entry,
                                      uint32_t generation) {
    block_cache_wait_context_t context;
    wait_reason_t reason;
    int result;

    context.entry = entry;
    context.generation = generation;
    if (!entry) {
        LOG_ERROR("BLKCACHE", "Entrada nula na espera do cache");
        return ERR_NULL;
    }
    result = wait_event(&entry->waiters, block_cache_wait_condition,
                        &context, &reason);
    if (result != OK) {
        LOG_WARN("BLKCACHE", "Espera do cache retornou erro");
        return result;
    }
    if (reason != WAIT_REASON_EVENT) {
        LOG_WARN("BLKCACHE", "Espera do cache terminou sem evento");
        return ERR_STATE;
    }
    return OK;
}

static block_cache_lookup_result_t block_cache_lookup(
    const char* device_id, uint32_t lba, uint8_t* output, int* out_error,
    block_cache_entry_t** out_wait_entry, uint32_t* out_generation) {
    uint32_t index;
    block_cache_entry_t* entry;
    int error;

    spinlock_acquire(&block_cache_lock);
    index = block_cache_find_locked(device_id, lba, BLOCK_CACHE_BLOCK_SIZE);
    if (index == BLOCK_CACHE_INVALID_INDEX) {
        spinlock_release(&block_cache_lock);
        return BLOCK_CACHE_LOOKUP_MISS;
    }
    entry = &block_cache_entries[index];
    if (entry->state == BLOCK_CACHE_READING) {
        *out_wait_entry = entry;
        *out_generation = entry->generation;
        spinlock_release(&block_cache_lock);
        return BLOCK_CACHE_LOOKUP_WAIT;
    }
    if (entry->state == BLOCK_CACHE_ERROR) {
        error = entry->status == OK ? ERR_STATE : entry->status;
        block_cache_release_locked(index);
        block_cache_stats.last_error = error;
        spinlock_release(&block_cache_lock);
        *out_error = error;
        return BLOCK_CACHE_LOOKUP_ERROR;
    }
    if (entry->state == BLOCK_CACHE_DIRTY ||
        entry->state == BLOCK_CACHE_WRITEBACK) {
        block_cache_stats.last_error = ERR_STATE;
        spinlock_release(&block_cache_lock);
        *out_error = ERR_STATE;
        return BLOCK_CACHE_LOOKUP_ERROR;
    }
    if (entry->state != BLOCK_CACHE_VALID) {
        spinlock_release(&block_cache_lock);
        return BLOCK_CACHE_LOOKUP_MISS;
    }
    entry->references++;
    block_cache_lru_remove_locked(index);
    block_cache_lru_push_front_locked(index);
    block_cache_stats.hits++;
    block_cache_stats.reads_avoided++;
    kmemcpy(output, entry->data, BLOCK_CACHE_BLOCK_SIZE);
    if (entry->references) entry->references--;
    spinlock_release(&block_cache_lock);
    return BLOCK_CACHE_LOOKUP_HIT;
}

static void block_cache_complete_run(const uint32_t* indexes,
                                     uint32_t count, const uint8_t* buffer,
                                     int result) {
    block_cache_entry_t* entries[BLOCK_CACHE_CAPACITY];

    spinlock_acquire(&block_cache_lock);
    for (uint32_t index = 0U; index < count; index++) {
        block_cache_entry_t* entry = &block_cache_entries[indexes[index]];

        entries[index] = entry;
        if (result == OK) {
            kmemcpy(entry->data,
                    buffer + index * BLOCK_CACHE_BLOCK_SIZE,
                    BLOCK_CACHE_BLOCK_SIZE);
            entry->state = BLOCK_CACHE_VALID;
            entry->status = OK;
            block_cache_lru_push_front_locked(indexes[index]);
        } else {
            entry->status = result;
            block_cache_stats.errors++;
            block_cache_stats.last_error = result;
            block_cache_release_locked(indexes[index]);
        }
    }
    if (result == OK) block_cache_stats.physical_reads += count;
    spinlock_release(&block_cache_lock);

    for (uint32_t index = 0U; index < count; index++) {
        uint32_t woken = 0U;

        if (wake_up_all(&entries[index]->waiters, &woken) != OK) {
            LOG_WARN("BLKCACHE", "Falha ao acordar leitores do bloco");
        }
    }
}

static int block_cache_lba_in_range(uint32_t value, uint32_t start,
                                    uint32_t count) {
    return value >= start && value - start < count;
}

static int block_cache_range_valid(uint32_t lba, uint32_t count) {
    if (!count) return 0;
    return lba <= 0xFFFFFFFFU - (count - 1U);
}

static int block_cache_range_has_blocker_locked(const char* device_id,
                                                uint32_t lba,
                                                uint32_t count) {
    for (uint32_t index = 0U; index < BLOCK_CACHE_CAPACITY; index++) {
        block_cache_entry_t* entry = &block_cache_entries[index];

        if (entry->state == BLOCK_CACHE_FREE ||
            !block_cache_text_equal(entry->device_id, device_id) ||
            !block_cache_lba_in_range(entry->lba, lba, count)) {
            continue;
        }
        if (entry->state == BLOCK_CACHE_READING || entry->references ||
            entry->pins || entry->state == BLOCK_CACHE_DIRTY ||
            entry->state == BLOCK_CACHE_WRITEBACK || entry->waiters.waiters) {
            return 1;
        }
    }
    return 0;
}

static uint32_t block_cache_divide_u64(uint32_t high, uint32_t low,
                                       uint32_t divisor) {
    uint32_t remainder = 0U;
    uint32_t quotient = 0U;

    if (!divisor) return 0U;
    for (uint32_t bit = 32U; bit > 0U; bit--) {
        uint32_t carry = remainder >> 31U;
        uint32_t shifted = (remainder << 1U) |
                           ((high >> (bit - 1U)) & 1U);

        if (carry || shifted >= divisor) {
            remainder = shifted - divisor;
        }
    }
    for (uint32_t bit = 32U; bit > 0U; bit--) {
        uint32_t carry = remainder >> 31U;
        uint32_t shifted = (remainder << 1U) |
                           ((low >> (bit - 1U)) & 1U);

        if (carry || shifted >= divisor) {
            remainder = shifted - divisor;
            quotient |= 1U << (bit - 1U);
        }
    }
    return quotient;
}

static uint32_t block_cache_percent(uint32_t numerator,
                                    uint32_t denominator) {
    uint32_t high = 0U;
    uint32_t low = 0U;

    if (!denominator) return 0U;
    for (uint32_t index = 0U; index < 100U; index++) {
        uint32_t previous = low;

        low += numerator;
        if (low < previous) high++;
    }
    return block_cache_divide_u64(high, low, denominator);
}

static int block_cache_validate_device(const block_device_t* device,
                                       uint32_t lba, uint8_t count,
                                       uint32_t buffer_bytes) {
    uint32_t required;

    if (!device) {
        LOG_ERROR("BLKCACHE", "Dispositivo nulo na validacao do cache");
        return ERR_NULL;
    }
    if (!block_cache_text_terminated(device->id, BLOCK_DEVICE_ID_SIZE)) {
        LOG_ERROR("BLKCACHE", "ID invalido na validacao do cache");
        return ERR_INVALID;
    }
    if (device->sector_size != BLOCK_CACHE_BLOCK_SIZE ||
        !device->sector_count || !device->max_transfer_sectors ||
        device->max_transfer_sectors > BLOCK_MAX_TRANSFER_SECTORS) {
        LOG_ERROR("BLKCACHE", "Geometria invalida na validacao do cache");
        return ERR_STATE;
    }
    if (!device->online) {
        LOG_WARN("BLKCACHE", "Dispositivo offline na leitura do cache");
        return ERR_DISK;
    }
    if (!count) {
        LOG_ERROR("BLKCACHE", "Quantidade nula na leitura do cache");
        return ERR_INVALID;
    }
    if (count > device->max_transfer_sectors) {
        LOG_ERROR("BLKCACHE", "Quantidade excede limite do cache");
        return ERR_OVERFLOW;
    }
    if (count > 0xFFFFFFFFU / BLOCK_CACHE_BLOCK_SIZE) {
        LOG_ERROR("BLKCACHE", "Tamanho excede limite do cache");
        return ERR_OVERFLOW;
    }
    required = (uint32_t)count * BLOCK_CACHE_BLOCK_SIZE;
    if (buffer_bytes < required) {
        LOG_ERROR("BLKCACHE", "Buffer menor que a leitura do cache");
        return ERR_INVALID;
    }
    if (lba >= device->sector_count ||
        count > device->sector_count - lba) {
        LOG_ERROR("BLKCACHE", "LBA fora dos limites do cache");
        return ERR_DISK;
    }
    if (!device->ops.read && !device->ops.submit) {
        LOG_ERROR("BLKCACHE", "Backend ausente na leitura do cache");
        return ERR_STATE;
    }
    return OK;
}

int block_cache_init(void) {
    int result;

    if (block_cache_initialized) return OK;
    spinlock_init(&block_cache_lock);
    kmemset(block_cache_entries, 0, sizeof(block_cache_entries));
    kmemset(&block_cache_stats, 0, sizeof(block_cache_stats));
    block_cache_lru_head = BLOCK_CACHE_INVALID_INDEX;
    block_cache_lru_tail = BLOCK_CACHE_INVALID_INDEX;
    for (uint32_t index = 0U; index < BLOCK_CACHE_HASH_BUCKETS; index++) {
        block_cache_buckets[index] = BLOCK_CACHE_INVALID_INDEX;
    }
    for (uint32_t index = 0U; index < BLOCK_CACHE_CAPACITY; index++) {
        block_cache_entries[index].state = BLOCK_CACHE_FREE;
        block_cache_entries[index].status = ERR_STATE;
        block_cache_entries[index].hash_next = BLOCK_CACHE_INVALID_INDEX;
        block_cache_entries[index].lru_previous = BLOCK_CACHE_INVALID_INDEX;
        block_cache_entries[index].lru_next = BLOCK_CACHE_INVALID_INDEX;
        result = init_waitqueue_head(&block_cache_entries[index].waiters,
                                     "Block cache");
        if (result != OK) {
            LOG_ERROR("BLKCACHE", "Falha ao inicializar fila de leitores");
            return result;
        }
        block_cache_entries[index].generation = 1U;
    }
    block_cache_stats.capacity = BLOCK_CACHE_CAPACITY;
    block_cache_stats.block_size = BLOCK_CACHE_BLOCK_SIZE;
    block_cache_stats.memory_bytes =
        BLOCK_CACHE_CAPACITY * BLOCK_CACHE_BLOCK_SIZE;
    block_cache_stats.last_error = OK;
    block_cache_initialized = 1U;
    LOG_INFO("BLKCACHE", "Cache de blocos inicializado");
    return OK;
}

int block_cache_read(const block_device_t* device, uint32_t lba,
                     uint8_t count, uint8_t* buffer, uint32_t buffer_bytes,
                     block_cache_read_backend_t backend) {
    uint32_t offset = 0U;
    int result;

    if (!device || !buffer || !backend) {
        LOG_ERROR("BLKCACHE", "Argumento nulo na leitura do cache");
        return ERR_NULL;
    }
    if (!block_cache_initialized) {
        LOG_ERROR("BLKCACHE", "Leitura antes da inicializacao do cache");
        return ERR_STATE;
    }
    result = block_cache_validate_device(device, lba, count, buffer_bytes);
    if (result != OK) {
        LOG_ERROR("BLKCACHE", "Leitura recusada pela geometria do cache");
        return result;
    }
    while (offset < count) {
        block_cache_entry_t* wait_entry = 0;
        uint32_t wait_generation = 0U;
        int lookup_error = ERR_STATE;
        block_cache_lookup_result_t lookup;

        lookup = block_cache_lookup(
            device->id, lba + offset,
            buffer + offset * BLOCK_CACHE_BLOCK_SIZE, &lookup_error,
            &wait_entry, &wait_generation);
        if (lookup == BLOCK_CACHE_LOOKUP_HIT) {
            offset++;
            continue;
        }
        if (lookup == BLOCK_CACHE_LOOKUP_WAIT) {
            result = block_cache_wait_for_entry(wait_entry, wait_generation);
            if (result != OK) {
                LOG_ERROR("BLKCACHE", "Espera por leitura do cache falhou");
                return result;
            }
            continue;
        }
        if (lookup == BLOCK_CACHE_LOOKUP_ERROR) return lookup_error;
        {
            uint32_t indexes[BLOCK_CACHE_CAPACITY];
            uint32_t remaining = count - offset;
            uint32_t reserved;

            if (remaining > BLOCK_CACHE_CAPACITY) {
                remaining = BLOCK_CACHE_CAPACITY;
            }
            spinlock_acquire(&block_cache_lock);
            reserved = block_cache_reserve_run_locked(
                device->id, lba + offset, remaining, indexes);
            spinlock_release(&block_cache_lock);
            if (!reserved) {
                spinlock_acquire(&block_cache_lock);
                block_cache_stats.misses++;
                block_cache_stats.bypasses++;
                spinlock_release(&block_cache_lock);
                result = backend(
                    device->id, lba + offset, 1U,
                    buffer + offset * BLOCK_CACHE_BLOCK_SIZE);
                spinlock_acquire(&block_cache_lock);
                if (result == OK) block_cache_stats.physical_reads++;
                else {
                    block_cache_stats.errors++;
                    block_cache_stats.last_error = result;
                }
                spinlock_release(&block_cache_lock);
                if (result != OK) return result;
                offset++;
                continue;
            }
            result = backend(
                device->id, lba + offset, (uint8_t)reserved,
                buffer + offset * BLOCK_CACHE_BLOCK_SIZE);
            block_cache_complete_run(
                indexes, reserved,
                buffer + offset * BLOCK_CACHE_BLOCK_SIZE, result);
            if (result != OK) return result;
            offset += reserved;
        }
    }
    return OK;
}

int block_cache_get_stats(block_cache_stats_t* out_stats) {
    block_cache_stats_t snapshot;
    uint32_t requests;

    if (!out_stats) {
        LOG_ERROR("BLKCACHE", "Destino nulo nas metricas do cache");
        return ERR_NULL;
    }
    if (!block_cache_initialized) {
        LOG_ERROR("BLKCACHE", "Metricas antes da inicializacao do cache");
        return ERR_STATE;
    }
    spinlock_acquire(&block_cache_lock);
    snapshot = block_cache_stats;
    snapshot.entries = 0U;
    snapshot.valid_entries = 0U;
    snapshot.reading_entries = 0U;
    snapshot.dirty_entries = 0U;
    snapshot.writeback_entries = 0U;
    snapshot.pinned_entries = 0U;
    for (uint32_t index = 0U; index < BLOCK_CACHE_CAPACITY; index++) {
        block_cache_entry_t* entry = &block_cache_entries[index];

        if (entry->state != BLOCK_CACHE_FREE) snapshot.entries++;
        if (entry->state == BLOCK_CACHE_VALID) snapshot.valid_entries++;
        if (entry->state == BLOCK_CACHE_READING) snapshot.reading_entries++;
        if (entry->state == BLOCK_CACHE_DIRTY) snapshot.dirty_entries++;
        if (entry->state == BLOCK_CACHE_WRITEBACK) {
            snapshot.writeback_entries++;
        }
        if (entry->pins) snapshot.pinned_entries++;
    }
    spinlock_release(&block_cache_lock);
    requests = snapshot.hits + snapshot.misses;
    snapshot.hit_rate_percent = block_cache_percent(snapshot.hits, requests);
    *out_stats = snapshot;
    return OK;
}

static int block_cache_validate_identifier(const char* device_id) {
    if (!device_id) {
        LOG_ERROR("BLKCACHE", "ID nulo na invalidacao do cache");
        return ERR_NULL;
    }
    if (!block_cache_text_terminated(device_id, BLOCK_DEVICE_ID_SIZE)) {
        LOG_ERROR("BLKCACHE", "ID nao terminado na invalidacao do cache");
        return ERR_INVALID;
    }
    return OK;
}

int block_cache_clear(void) {
    uint32_t invalidated = 0U;

    if (!block_cache_initialized) {
        LOG_ERROR("BLKCACHE", "Limpeza antes da inicializacao do cache");
        return ERR_STATE;
    }
    spinlock_acquire(&block_cache_lock);
    for (uint32_t index = 0U; index < BLOCK_CACHE_CAPACITY; index++) {
        block_cache_entry_t* entry = &block_cache_entries[index];

        if (entry->state != BLOCK_CACHE_FREE &&
            (entry->state == BLOCK_CACHE_READING || entry->references ||
             entry->pins || entry->state == BLOCK_CACHE_DIRTY ||
             entry->state == BLOCK_CACHE_WRITEBACK || entry->waiters.waiters)) {
            spinlock_release(&block_cache_lock);
            LOG_WARN("BLKCACHE", "Limpeza recusada por entrada ocupada");
            return ERR_STATE;
        }
    }
    for (uint32_t index = 0U; index < BLOCK_CACHE_CAPACITY; index++) {
        if (block_cache_entries[index].state == BLOCK_CACHE_FREE) continue;
        block_cache_release_locked(index);
        invalidated++;
    }
    block_cache_stats.invalidations += invalidated;
    spinlock_release(&block_cache_lock);
    return OK;
}

static int block_cache_invalidate(const char* device_id, uint32_t lba,
                                  uint32_t count, uint8_t all_device) {
    uint32_t invalidated = 0U;
    int result;

    result = block_cache_validate_identifier(device_id);
    if (result != OK) return result;
    if (!all_device && !block_cache_range_valid(lba, count)) {
        LOG_ERROR("BLKCACHE", "Faixa invalida na invalidacao do cache");
        return count ? ERR_OVERFLOW : ERR_INVALID;
    }
    if (!block_cache_initialized) return ERR_STATE;
    spinlock_acquire(&block_cache_lock);
    if (all_device) {
        for (uint32_t index = 0U; index < BLOCK_CACHE_CAPACITY; index++) {
            block_cache_entry_t* entry = &block_cache_entries[index];

            if (entry->state != BLOCK_CACHE_FREE &&
                block_cache_text_equal(entry->device_id, device_id) &&
                !block_cache_entry_eligible(entry)) {
                spinlock_release(&block_cache_lock);
                LOG_WARN("BLKCACHE", "Invalidacao recusada por entrada ocupada");
                return ERR_STATE;
            }
        }
    } else if (block_cache_range_has_blocker_locked(device_id, lba, count)) {
        spinlock_release(&block_cache_lock);
        LOG_WARN("BLKCACHE", "Invalidacao recusada por entrada ocupada");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < BLOCK_CACHE_CAPACITY; index++) {
        block_cache_entry_t* entry = &block_cache_entries[index];
        int matches = entry->state != BLOCK_CACHE_FREE &&
                      block_cache_text_equal(entry->device_id, device_id);

        if (matches && !all_device) {
            matches = block_cache_lba_in_range(entry->lba, lba, count);
        }
        if (!matches) continue;
        block_cache_release_locked(index);
        invalidated++;
    }
    block_cache_stats.invalidations += invalidated;
    spinlock_release(&block_cache_lock);
    return OK;
}

int block_cache_invalidate_device(const char* device_id) {
    return block_cache_invalidate(device_id, 0U, 0U, 1U);
}

int block_cache_invalidate_range(const char* device_id, uint32_t lba,
                                 uint32_t sector_count) {
    return block_cache_invalidate(device_id, lba, sector_count, 0U);
}

int block_cache_validate_state(void) {
    uint8_t hash_seen[BLOCK_CACHE_CAPACITY];
    uint8_t lru_seen[BLOCK_CACHE_CAPACITY];
    uint32_t lru_count = 0U;
    uint32_t previous = BLOCK_CACHE_INVALID_INDEX;

    if (!block_cache_initialized) {
        LOG_ERROR("BLKCACHE", "Estado consultado antes da inicializacao");
        return ERR_STATE;
    }
    spinlock_acquire(&block_cache_lock);
    kmemset(hash_seen, 0, sizeof(hash_seen));
    kmemset(lru_seen, 0, sizeof(lru_seen));
    for (uint32_t bucket = 0U; bucket < BLOCK_CACHE_HASH_BUCKETS; bucket++) {
        uint32_t index = block_cache_buckets[bucket];
        uint32_t guard = 0U;

        while (index != BLOCK_CACHE_INVALID_INDEX) {
            block_cache_entry_t* entry;

            if (index >= BLOCK_CACHE_CAPACITY || guard++ >= BLOCK_CACHE_CAPACITY) {
                spinlock_release(&block_cache_lock);
                LOG_ERROR("BLKCACHE", "Bucket invalido no cache de blocos");
                return ERR_STATE;
            }
            entry = &block_cache_entries[index];
            if (hash_seen[index]) {
                spinlock_release(&block_cache_lock);
                LOG_ERROR("BLKCACHE", "Entrada duplicada no hash do cache");
                return ERR_STATE;
            }
            hash_seen[index] = 1U;
            if (entry->state == BLOCK_CACHE_FREE ||
                !block_cache_text_terminated(entry->device_id,
                                             BLOCK_DEVICE_ID_SIZE) ||
                entry->block_size != BLOCK_CACHE_BLOCK_SIZE ||
                block_cache_hash(entry->device_id, entry->lba,
                                 entry->block_size) != bucket) {
                spinlock_release(&block_cache_lock);
                LOG_ERROR("BLKCACHE", "Entrada invalida no hash do cache");
                return ERR_STATE;
            }
            index = entry->hash_next;
        }
    }
    for (uint32_t index = block_cache_lru_head;
         index != BLOCK_CACHE_INVALID_INDEX;
         index = block_cache_entries[index].lru_next) {
        block_cache_entry_t* entry;

        if (index >= BLOCK_CACHE_CAPACITY || lru_count++ >= BLOCK_CACHE_CAPACITY) {
            spinlock_release(&block_cache_lock);
            LOG_ERROR("BLKCACHE", "Lista LRU invalida no cache");
            return ERR_STATE;
        }
        entry = &block_cache_entries[index];
        if (lru_seen[index]) {
            spinlock_release(&block_cache_lock);
            LOG_ERROR("BLKCACHE", "Entrada duplicada na LRU do cache");
            return ERR_STATE;
        }
        lru_seen[index] = 1U;
        if (!block_cache_lru_state(entry->state) ||
            entry->lru_previous != previous) {
            spinlock_release(&block_cache_lock);
            LOG_ERROR("BLKCACHE", "Ligacao LRU invalida no cache");
            return ERR_STATE;
        }
        previous = index;
    }
    if (previous != block_cache_lru_tail) {
        spinlock_release(&block_cache_lock);
        LOG_ERROR("BLKCACHE", "Cauda LRU invalida no cache");
        return ERR_STATE;
    }
    if (block_cache_lru_head != BLOCK_CACHE_INVALID_INDEX &&
        block_cache_entries[block_cache_lru_head].lru_previous !=
            BLOCK_CACHE_INVALID_INDEX) {
        spinlock_release(&block_cache_lock);
        LOG_ERROR("BLKCACHE", "Cabeca LRU invalida no cache");
        return ERR_STATE;
    }
    if (block_cache_lru_tail != BLOCK_CACHE_INVALID_INDEX &&
        block_cache_entries[block_cache_lru_tail].lru_next !=
            BLOCK_CACHE_INVALID_INDEX) {
        spinlock_release(&block_cache_lock);
        LOG_ERROR("BLKCACHE", "Cauda LRU aponta para outra entrada");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < BLOCK_CACHE_CAPACITY; index++) {
        block_cache_entry_t* entry = &block_cache_entries[index];

        if (entry->state == BLOCK_CACHE_FREE) {
            if (entry->device_id[0] ||
                entry->hash_next != BLOCK_CACHE_INVALID_INDEX ||
                entry->lru_previous != BLOCK_CACHE_INVALID_INDEX ||
                entry->lru_next != BLOCK_CACHE_INVALID_INDEX ||
                entry->references || entry->pins) {
                spinlock_release(&block_cache_lock);
                LOG_ERROR("BLKCACHE", "Entrada livre contaminada no cache");
                return ERR_STATE;
            }
            continue;
        }
        if (!hash_seen[index] ||
            (block_cache_lru_state(entry->state) && !lru_seen[index]) ||
            (!block_cache_lru_state(entry->state) && lru_seen[index])) {
            spinlock_release(&block_cache_lock);
            LOG_ERROR("BLKCACHE", "Entrada fora das estruturas do cache");
            return ERR_STATE;
        }
        if (!block_cache_text_terminated(entry->device_id,
                                         BLOCK_DEVICE_ID_SIZE) ||
            !entry->generation || entry->block_size != BLOCK_CACHE_BLOCK_SIZE ||
            entry->state > BLOCK_CACHE_ERROR) {
            spinlock_release(&block_cache_lock);
            LOG_ERROR("BLKCACHE", "Metadado invalido no cache");
            return ERR_STATE;
        }
        if (!block_cache_lru_state(entry->state) &&
            (entry->lru_previous != BLOCK_CACHE_INVALID_INDEX ||
             entry->lru_next != BLOCK_CACHE_INVALID_INDEX)) {
            spinlock_release(&block_cache_lock);
            LOG_ERROR("BLKCACHE", "Entrada nao elegivel entrou na LRU");
            return ERR_STATE;
        }
    }
    spinlock_release(&block_cache_lock);
    return OK;
}

static int block_cache_test_submit(block_request_t* request) {
    block_cache_test_context_t* context;

    if (!request || !request->device_context) {
        LOG_ERROR("BLKCACHE", "Requisicao invalida no backend mock");
        return ERR_NULL;
    }
    context = (block_cache_test_context_t*)request->device_context;
    if (request->operation == BLOCK_OPERATION_READ) {
        context->reads++;
        if (context->forced_result != OK) {
            LOG_WARN("BLKCACHE", "Backend mock simulou erro de leitura");
            return context->forced_result;
        }
        for (uint32_t sector = 0U; sector < request->sector_count; sector++) {
            uint8_t* output = (uint8_t*)request->buffer +
                              sector * BLOCK_CACHE_BLOCK_SIZE;

            for (uint32_t byte = 0U; byte < BLOCK_CACHE_BLOCK_SIZE; byte++) {
                output[byte] = (uint8_t)(context->seed + request->lba +
                                         sector + byte);
            }
        }
    } else if (request->operation == BLOCK_OPERATION_WRITE) {
        context->writes++;
        if (context->forced_result != OK) {
            LOG_WARN("BLKCACHE", "Backend mock simulou erro de escrita");
            return context->forced_result;
        }
    }
    request->completed_sectors = request->sector_count;
    return OK;
}

static void block_cache_test_device(block_device_t* device,
                                    block_cache_test_context_t* context,
                                    const char* id, uint8_t seed) {
    kmemset(device, 0, sizeof(*device));
    block_cache_copy_text(device->id, BLOCK_DEVICE_ID_SIZE, id);
    block_cache_copy_text(device->model, BLOCK_DEVICE_MODEL_SIZE,
                          "cache deterministic");
    device->provider = BLOCK_PROVIDER_ATA;
    device->sector_count = BLOCK_CACHE_TEST_SECTOR_COUNT;
    device->sector_size = BLOCK_CACHE_BLOCK_SIZE;
    device->online = 1U;
    device->max_transfer_sectors = BLOCK_CACHE_TEST_MAX_TRANSFER;
    device->ops.context = context;
    device->ops.submit = block_cache_test_submit;
    context->seed = seed;
    context->forced_result = OK;
}

static int block_cache_test_inventory(char ids[][BLOCK_DEVICE_ID_SIZE],
                                      uint32_t* out_count) {
    uint32_t count;

    if (!ids || !out_count || block_get_count(&count) != OK ||
        count > BLOCK_MAX_DEVICES) {
        LOG_ERROR("BLKCACHE", "Falha ao obter inventario para o autoteste");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < count; index++) {
        block_device_t device;

        if (block_get_at(index, &device) != OK) {
            LOG_ERROR("BLKCACHE", "Falha ao copiar inventario para o autoteste");
            return ERR_STATE;
        }
        block_cache_copy_text(ids[index], BLOCK_DEVICE_ID_SIZE, device.id);
    }
    *out_count = count;
    return OK;
}

static int block_cache_test_inventory_unchanged(
    char ids[][BLOCK_DEVICE_ID_SIZE], uint32_t count) {
    uint32_t current_count;

    if (block_get_count(&current_count) != OK || current_count != count) {
        LOG_ERROR("BLKCACHE", "Quantidade do inventario mudou no autoteste");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < count; index++) {
        block_device_t device;

        if (block_get_at(index, &device) != OK ||
            !block_cache_text_equal(ids[index], device.id)) {
            LOG_ERROR("BLKCACHE", "ID do inventario mudou no autoteste");
            return ERR_STATE;
        }
    }
    return OK;
}

int block_cache_self_test(void) {
    static char initial_ids[BLOCK_MAX_DEVICES][BLOCK_DEVICE_ID_SIZE];
    static uint8_t first_buffer[BLOCK_CACHE_BLOCK_SIZE];
    static uint8_t second_buffer[BLOCK_CACHE_BLOCK_SIZE];
    static uint8_t write_buffer[BLOCK_CACHE_BLOCK_SIZE];
    block_cache_test_context_t context_a;
    block_cache_test_context_t context_b;
    block_device_t device_a;
    block_device_t device_b;
    block_cache_stats_t stats;
    uint32_t initial_count;
    uint32_t reads_before;
    int result;

    if (!block_cache_initialized) return ERR_STATE;
    if (block_cache_test_inventory(initial_ids, &initial_count) != OK) {
        LOG_ERROR("BLKCACHE", "Falha ao salvar inventario do autoteste");
        return ERR_STATE;
    }
    if (block_cache_clear() != OK) {
        LOG_ERROR("BLKCACHE", "Cache ocupado antes do autoteste");
        return ERR_STATE;
    }
    kmemset(&context_a, 0, sizeof(context_a));
    kmemset(&context_b, 0, sizeof(context_b));
    block_cache_test_device(&device_a, &context_a, "blk-cache-test-a",
                            BLOCK_CACHE_TEST_SEED_A);
    block_cache_test_device(&device_b, &context_b, "blk-cache-test-b",
                            BLOCK_CACHE_TEST_SEED_B);
    if (block_register(&device_a) != OK || block_register(&device_b) != OK) {
        LOG_ERROR("BLKCACHE", "Falha ao registrar mocks do cache");
        goto block_cache_self_test_fail;
    }
    result = block_read(device_a.id, BLOCK_CACHE_TEST_LBA, 1U, first_buffer);
    if (result != OK || context_a.reads != 1U) goto block_cache_self_test_fail;
    result = block_read(device_a.id, BLOCK_CACHE_TEST_LBA, 1U, second_buffer);
    if (result != OK || context_a.reads != 1U ||
        first_buffer[0] != second_buffer[0]) goto block_cache_self_test_fail;
    result = block_read(device_b.id, BLOCK_CACHE_TEST_LBA, 1U, second_buffer);
    if (result != OK || context_b.reads != 1U ||
        first_buffer[0] == second_buffer[0]) goto block_cache_self_test_fail;
    if (block_cache_invalidate_range(device_a.id, BLOCK_CACHE_TEST_LBA, 1U) !=
        OK) goto block_cache_self_test_fail;
    result = block_read(device_a.id, BLOCK_CACHE_TEST_LBA, 1U, first_buffer);
    if (result != OK || context_a.reads != 2U) goto block_cache_self_test_fail;
    if (block_cache_clear() != OK) goto block_cache_self_test_fail;
    context_a.reads = 0U;
    for (uint32_t index = 0U; index < BLOCK_CACHE_CAPACITY; index++) {
        if (block_read(device_a.id, index, 1U, first_buffer) != OK) {
            goto block_cache_self_test_fail;
        }
    }
    if (block_read(device_a.id, 0U, 1U, first_buffer) != OK) {
        goto block_cache_self_test_fail;
    }
    reads_before = context_a.reads;
    if (block_read(device_a.id, BLOCK_CACHE_CAPACITY, 1U, first_buffer) != OK ||
        block_read(device_a.id, 1U, 1U, first_buffer) != OK ||
        context_a.reads != reads_before + 2U) {
        goto block_cache_self_test_fail;
    }
    spinlock_acquire(&block_cache_lock);
    for (uint32_t index = 0U; index < BLOCK_CACHE_CAPACITY; index++) {
        if (block_cache_entries[index].state != BLOCK_CACHE_VALID) {
            spinlock_release(&block_cache_lock);
            goto block_cache_self_test_fail;
        }
    }
    for (uint32_t index = 0U; index < BLOCK_CACHE_CAPACITY; index++) {
        block_cache_entries[index].pins = 1U;
    }
    spinlock_release(&block_cache_lock);
    reads_before = context_a.reads;
    result = block_read(device_a.id, BLOCK_CACHE_CAPACITY + 1U, 1U,
                        first_buffer);
    spinlock_acquire(&block_cache_lock);
    for (uint32_t index = 0U; index < BLOCK_CACHE_CAPACITY; index++) {
        block_cache_entries[index].pins = 0U;
    }
    spinlock_release(&block_cache_lock);
    if (result != OK || context_a.reads != reads_before + 1U ||
        block_cache_get_stats(&stats) != OK || stats.bypasses == 0U) {
        goto block_cache_self_test_fail;
    }
    if (block_cache_clear() != OK) goto block_cache_self_test_fail;
    context_a.forced_result = ERR_TIMEOUT;
    result = block_read(device_a.id, 90U, 1U, first_buffer);
    context_a.forced_result = OK;
    if (result != ERR_TIMEOUT ||
        block_read(device_a.id, 90U, 1U, first_buffer) != OK) {
        goto block_cache_self_test_fail;
    }
    if (block_cache_clear() != OK) goto block_cache_self_test_fail;
    if (block_read(device_a.id, 12U, 1U, first_buffer) != OK) {
        goto block_cache_self_test_fail;
    }
    kmemset(write_buffer, 0x5A, sizeof(write_buffer));
    if (block_write(device_a.id, 12U, 1U, write_buffer) != OK ||
        block_read(device_a.id, 12U, 1U, first_buffer) != OK) {
        goto block_cache_self_test_fail;
    }
    if (context_a.reads < 2U || block_cache_get_stats(&stats) != OK ||
        stats.hits == 0U || stats.misses == 0U || stats.evictions == 0U ||
        stats.invalidations == 0U || block_cache_validate_state() != OK) {
        goto block_cache_self_test_fail;
    }
    if (block_unregister(device_a.id) != OK ||
        block_unregister(device_b.id) != OK ||
        block_cache_clear() != OK ||
        block_cache_test_inventory_unchanged(initial_ids, initial_count) != OK) {
        LOG_ERROR("BLKCACHE", "Autoteste alterou o inventario real");
        return ERR_STATE;
    }
    return OK;

block_cache_self_test_fail:
    (void)block_unregister(device_a.id);
    (void)block_unregister(device_b.id);
    (void)block_cache_clear();
    LOG_ERROR("BLKCACHE", "Autoteste do cache falhou");
    return ERR_STATE;
}
