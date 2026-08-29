#include "core/sk_buff.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/spinlock.h"
#include "memory/slab.h"

#define SKB_TRACKING_CAPACITY NET_BUFFER_TRACKING_CAPACITY

typedef struct {
    sk_buff_t skb;
    net_buffer_t lifetime;
    uint8_t storage[SK_BUFF_STORAGE_SIZE];
} skb_entry_t;

static kmem_cache_t* skb_cache;
static sk_buff_t* skb_registry[SKB_TRACKING_CAPACITY];
static spinlock_t skb_lock;
static uint8_t skb_lock_initialized;
static uint8_t skb_testing;
static sk_buff_stats_t skb_stats;

static int skb_check_geometry(const skb_entry_t* entry);

static void skb_record_failure_locked(int result) {
    skb_stats.invalid_operations++;
    skb_stats.last_error = result;
}

static void skb_log_failure(const char* message) {
    if (!skb_testing) LOG_WARN("SKB", message);
}

static int32_t skb_find_locked(const sk_buff_t* skb) {
    for (uint32_t index = 0U; index < SKB_TRACKING_CAPACITY; index++) {
        if (skb_registry[index] == skb) return (int32_t)index;
    }
    return -1;
}

static int32_t skb_free_slot_locked(void) {
    for (uint32_t index = 0U; index < SKB_TRACKING_CAPACITY; index++) {
        if (!skb_registry[index]) return (int32_t)index;
    }
    return -1;
}

static uint32_t skb_registry_count_locked(void) {
    uint32_t count = 0U;

    for (uint32_t index = 0U; index < SKB_TRACKING_CAPACITY; index++) {
        if (skb_registry[index]) count++;
    }
    return count;
}

static skb_entry_t* skb_entry_from_pointer(const sk_buff_t* skb) {
    if (!skb || !skb_cache || !kmem_cache_owns(skb_cache, skb)) return NULL;
    return (skb_entry_t*)skb;
}

static int skb_validate_locked(const sk_buff_t* skb, skb_entry_t** out_entry) {
    skb_entry_t* entry;
    int result = OK;

    if (!skb) {
        result = ERR_NULL;
    } else {
        entry = skb_entry_from_pointer(skb);
        if (!entry || skb_find_locked(skb) < 0) {
            result = ERR_INVALID;
        } else if (entry->lifetime.state == NET_BUFFER_STATE_FREED ||
                   !entry->lifetime.refcount) {
            result = ERR_STATE;
        } else if (skb_check_geometry(entry) != OK) {
            result = ERR_STATE;
        } else if (out_entry) {
            *out_entry = entry;
        }
    }
    if (result != OK && !skb_testing) {
        LOG_WARN("SKB", "Descriptor de sk_buff invalido");
    }
    return result;
}

static int skb_update_layout(skb_entry_t* entry, uint32_t headroom,
                             uint32_t length) {
    int result = net_buffer_set_layout(&entry->lifetime, headroom, length);

    if (result != OK && !skb_testing) {
        LOG_WARN("SKB", "Geometria de sk_buff recusada");
    }
    return result;
}

static void skb_sync_refcount(skb_entry_t* entry) {
    entry->skb.refcount = entry->lifetime.refcount;
}

static int skb_check_geometry(const skb_entry_t* entry) {
    const sk_buff_t* skb;
    uint32_t capacity;
    uint32_t headroom;
    uint32_t length;

    if (!entry) {
        LOG_ERROR("SKB", "Entrada nula na validacao de geometria");
        return ERR_NULL;
    }
    skb = &entry->skb;
    if (!skb->head || !skb->data || !skb->tail || !skb->end ||
        skb->head > skb->data || skb->data > skb->tail ||
        skb->tail > skb->end) {
        LOG_ERROR("SKB", "Ponteiros de sk_buff inconsistentes");
        return ERR_STATE;
    }
    capacity = (uint32_t)(skb->end - skb->head);
    headroom = (uint32_t)(skb->data - skb->head);
    length = (uint32_t)(skb->tail - skb->data);
    if (skb->len != length ||
        entry->lifetime.capacity != capacity ||
        entry->lifetime.headroom != headroom ||
        entry->lifetime.length != length ||
        entry->lifetime.tailroom != capacity - headroom - length) {
        LOG_ERROR("SKB", "Metadados de sk_buff inconsistentes");
        return ERR_STATE;
    }
    return OK;
}

int skb_init(void) {
    if (!skb_lock_initialized) {
        spinlock_init(&skb_lock);
        skb_lock_initialized = 1U;
    }
    if (skb_stats.initialized) return OK;
    if (net_buffer_init() != OK) {
        LOG_ERROR("SKB", "Falha ao inicializar lifetime de buffers");
        return ERR_STATE;
    }
    skb_cache = kmem_cache_create("sk_buff", sizeof(skb_entry_t), 16U);
    if (!skb_cache) {
        LOG_ERROR("SKB", "Falha ao criar cache de sk_buff");
        return ERR_MEM;
    }
    spinlock_acquire(&skb_lock);
    kmemset(skb_registry, 0, sizeof(skb_registry));
    kmemset(&skb_stats, 0, sizeof(skb_stats));
    skb_stats.initialized = 1U;
    skb_stats.last_error = OK;
    spinlock_release(&skb_lock);
    LOG_INFO("SKB", "Runtime de sk_buff inicializado");
    return OK;
}

sk_buff_t* alloc_skb(uint32_t size) {
    skb_entry_t* entry;
    int32_t slot;
    int result;

    if (!size || size > SK_BUFF_STORAGE_SIZE) {
        LOG_WARN("SKB", "Tamanho invalido para alocacao de sk_buff");
        return NULL;
    }
    if (!skb_stats.initialized || !skb_cache) {
        LOG_ERROR("SKB", "Alocacao de sk_buff antes da inicializacao");
        return NULL;
    }
    entry = (skb_entry_t*)kmem_cache_alloc(skb_cache);
    if (!entry) {
        LOG_ERROR("SKB", "Falha ao alocar objeto de sk_buff");
        return NULL;
    }
    result = net_buffer_begin(&entry->lifetime, size, 0U, 1U,
                              NET_BUFFER_OWNER_ETHERNET);
    if (result != OK) {
        kmem_cache_free(skb_cache, entry);
        return NULL;
    }
    entry->skb.head = entry->storage;
    entry->skb.data = entry->storage;
    entry->skb.tail = entry->storage;
    entry->skb.end = entry->storage + size;
    entry->skb.len = 0U;
    entry->skb.dev = NULL;
    skb_sync_refcount(entry);
    spinlock_acquire(&skb_lock);
    slot = skb_free_slot_locked();
    if (slot < 0) {
        skb_record_failure_locked(ERR_OVERFLOW);
        spinlock_release(&skb_lock);
        net_buffer_complete(&entry->lifetime, ERR_OVERFLOW,
                            NET_BUFFER_OWNER_NONE);
        net_buffer_release(&entry->lifetime);
        kmem_cache_free(skb_cache, entry);
        LOG_ERROR("SKB", "Rastreamento de sk_buff cheio");
        return NULL;
    }
    skb_registry[slot] = &entry->skb;
    skb_stats.active_buffers++;
    skb_stats.allocations++;
    if (skb_stats.active_buffers > skb_stats.peak_buffers) {
        skb_stats.peak_buffers = skb_stats.active_buffers;
    }
    spinlock_release(&skb_lock);
    return &entry->skb;
}

void* skb_put(sk_buff_t* skb, uint32_t len) {
    skb_entry_t* entry = NULL;
    uint8_t* old_tail;
    uint32_t new_length;
    int result;

    if (!skb_lock_initialized) {
        LOG_ERROR("SKB", "skb_put antes da inicializacao");
        return NULL;
    }
    spinlock_acquire(&skb_lock);
    result = skb_validate_locked(skb, &entry);
    if (result == OK && len > (uint32_t)(skb->end - skb->tail)) {
        result = ERR_OVERFLOW;
    }
    if (result == OK) {
        old_tail = skb->tail;
        new_length = skb->len + len;
        result = skb_update_layout(entry,
                                   (uint32_t)(skb->data - skb->head),
                                   new_length);
        if (result == OK) {
            skb->tail += len;
            skb->len = new_length;
            spinlock_release(&skb_lock);
            return old_tail;
        }
    }
    if (result != OK) skb_record_failure_locked(result);
    spinlock_release(&skb_lock);
    skb_log_failure("skb_put recusou o tamanho solicitado");
    return NULL;
}

void* skb_push(sk_buff_t* skb, uint32_t len) {
    skb_entry_t* entry = NULL;
    uint8_t* new_data;
    uint32_t new_length;
    int result;

    if (!skb_lock_initialized) {
        LOG_ERROR("SKB", "skb_push antes da inicializacao");
        return NULL;
    }
    spinlock_acquire(&skb_lock);
    result = skb_validate_locked(skb, &entry);
    if (result == OK && len > (uint32_t)(skb->data - skb->head)) {
        result = ERR_OVERFLOW;
    }
    if (result == OK) {
        new_data = skb->data - len;
        new_length = skb->len + len;
        result = skb_update_layout(
            entry, (uint32_t)(new_data - skb->head), new_length);
        if (result == OK) {
            skb->data = new_data;
            skb->len = new_length;
            spinlock_release(&skb_lock);
            return new_data;
        }
    }
    if (result != OK) skb_record_failure_locked(result);
    spinlock_release(&skb_lock);
    skb_log_failure("skb_push recusou o cabecalho solicitado");
    return NULL;
}

void* skb_pull(sk_buff_t* skb, uint32_t len) {
    skb_entry_t* entry = NULL;
    uint8_t* new_data;
    uint32_t new_length;
    int result;

    if (!skb_lock_initialized) {
        LOG_ERROR("SKB", "skb_pull antes da inicializacao");
        return NULL;
    }
    spinlock_acquire(&skb_lock);
    result = skb_validate_locked(skb, &entry);
    if (result == OK && len > skb->len) result = ERR_OVERFLOW;
    if (result == OK) {
        new_data = skb->data + len;
        new_length = skb->len - len;
        result = skb_update_layout(
            entry, (uint32_t)(new_data - skb->head), new_length);
        if (result == OK) {
            skb->data = new_data;
            skb->len = new_length;
            spinlock_release(&skb_lock);
            return new_data;
        }
    }
    if (result != OK) skb_record_failure_locked(result);
    spinlock_release(&skb_lock);
    skb_log_failure("skb_pull recusou o tamanho solicitado");
    return NULL;
}

int skb_transition(sk_buff_t* skb, net_buffer_state_t next_state,
                   net_buffer_owner_t next_owner) {
    skb_entry_t* entry = NULL;
    int result;

    if (!skb_lock_initialized) {
        LOG_ERROR("SKB", "Transicao antes da inicializacao");
        return ERR_STATE;
    }
    spinlock_acquire(&skb_lock);
    result = skb_validate_locked(skb, &entry);
    if (result == OK) {
        result = net_buffer_transition(&entry->lifetime, next_state,
                                       next_owner);
        skb_sync_refcount(entry);
    }
    if (result != OK) skb_record_failure_locked(result);
    spinlock_release(&skb_lock);
    if (result != OK) skb_log_failure("Transicao de sk_buff recusada");
    return result;
}

int skb_complete(sk_buff_t* skb, int result,
                 net_buffer_owner_t delivered_owner) {
    skb_entry_t* entry = NULL;
    int completion_result;

    if (!skb_lock_initialized) {
        LOG_ERROR("SKB", "Conclusao antes da inicializacao");
        return ERR_STATE;
    }
    spinlock_acquire(&skb_lock);
    completion_result = skb_validate_locked(skb, &entry);
    if (completion_result == OK) {
        completion_result = net_buffer_complete(
            &entry->lifetime, result, delivered_owner);
        skb_sync_refcount(entry);
        if (completion_result == OK) {
            if (result == OK) skb_stats.completions++;
            else skb_stats.drops++;
        }
    }
    if (completion_result != OK) skb_record_failure_locked(completion_result);
    spinlock_release(&skb_lock);
    if (completion_result != OK) {
        skb_log_failure("Conclusao de sk_buff recusada");
    }
    return completion_result;
}

int skb_retain(sk_buff_t* skb) {
    skb_entry_t* entry = NULL;
    int result;

    if (!skb_lock_initialized) {
        LOG_ERROR("SKB", "Retencao antes da inicializacao");
        return ERR_STATE;
    }
    spinlock_acquire(&skb_lock);
    result = skb_validate_locked(skb, &entry);
    if (result == OK) {
        result = net_buffer_retain(&entry->lifetime);
        skb_sync_refcount(entry);
    }
    if (result != OK) skb_record_failure_locked(result);
    spinlock_release(&skb_lock);
    if (result != OK) skb_log_failure("Retencao de sk_buff recusada");
    return result;
}

int skb_release(sk_buff_t* skb) {
    skb_entry_t* entry = NULL;
    int32_t slot;
    int result;
    uint8_t released = 0U;

    if (!skb_lock_initialized) {
        LOG_ERROR("SKB", "Liberacao antes da inicializacao");
        return ERR_STATE;
    }
    spinlock_acquire(&skb_lock);
    result = skb_validate_locked(skb, &entry);
    if (result == OK) {
        result = net_buffer_release(&entry->lifetime);
        skb_sync_refcount(entry);
        if (result == OK && entry->lifetime.state == NET_BUFFER_STATE_FREED) {
            slot = skb_find_locked(skb);
            if (slot < 0) {
                result = ERR_INVALID;
            } else {
                skb_registry[slot] = NULL;
                if (skb_stats.active_buffers) skb_stats.active_buffers--;
                skb_stats.frees++;
                released = 1U;
            }
        }
    }
    if (result != OK) skb_record_failure_locked(result);
    spinlock_release(&skb_lock);
    if (released) {
        kmem_cache_free(skb_cache, (void*)skb);
    }
    if (result != OK) skb_log_failure("Liberacao de sk_buff recusada");
    return result;
}

void free_skb(sk_buff_t* skb) {
    (void)skb_release(skb);
}

int skb_get_stats(sk_buff_stats_t* out_stats) {
    if (!out_stats) {
        LOG_ERROR("SKB", "Destino nulo ao consultar estatisticas");
        return ERR_NULL;
    }
    if (!skb_lock_initialized) {
        LOG_ERROR("SKB", "Consulta de estatisticas antes da inicializacao");
        return ERR_STATE;
    }
    spinlock_acquire(&skb_lock);
    if (!skb_stats.initialized) {
        spinlock_release(&skb_lock);
        LOG_ERROR("SKB", "Consulta de estatisticas antes da inicializacao");
        return ERR_STATE;
    }
    *out_stats = skb_stats;
    spinlock_release(&skb_lock);
    return OK;
}

int skb_validate_state(void) {
    kmem_cache_info_t cache_info;
    net_buffer_stats_t buffer_stats;
    int result = OK;

    if (!skb_lock_initialized) {
        LOG_ERROR("SKB", "Validacao antes da inicializacao");
        return ERR_STATE;
    }
    if (net_buffer_validate_state() != OK) result = ERR_STATE;
    spinlock_acquire(&skb_lock);
    if (!skb_stats.initialized || !skb_cache) {
        result = ERR_STATE;
    } else {
        for (uint32_t index = 0U; index < SKB_TRACKING_CAPACITY; index++) {
            sk_buff_t* skb = skb_registry[index];
            skb_entry_t* entry;

            if (!skb) continue;
            entry = (skb_entry_t*)skb;
            if (skb_check_geometry(entry) != OK ||
                skb->refcount != entry->lifetime.refcount ||
                entry->lifetime.state == NET_BUFFER_STATE_FREED) {
                result = ERR_STATE;
                break;
            }
        }
        if (net_buffer_get_stats(&buffer_stats) != OK ||
            kmem_cache_get_info(skb_cache, &cache_info) != OK ||
            buffer_stats.active_buffers != skb_stats.active_buffers ||
            cache_info.active_objects != skb_stats.active_buffers ||
            skb_stats.active_buffers > SKB_TRACKING_CAPACITY ||
            skb_stats.frees > skb_stats.allocations ||
            skb_stats.peak_buffers < skb_stats.active_buffers) {
            result = ERR_STATE;
        }
    }
    spinlock_release(&skb_lock);
    if (result != OK) LOG_ERROR("SKB", "Estado global de sk_buff invalido");
    return result;
}

static int skb_test_pointer_ops(void) {
    sk_buff_t* skb = alloc_skb(128U);
    uint8_t completed = 0U;
    uint32_t references;
    int result = OK;

    if (!skb) {
        LOG_ERROR("SKB", "Falha ao alocar fixture de ponteiros");
        return ERR_MEM;
    }
    if (skb->head != skb->data || skb->data != skb->tail ||
        skb->len != 0U || !skb_put(skb, 32U) || skb->len != 32U ||
        !skb_pull(skb, 8U) || skb->len != 24U ||
        !skb_push(skb, 4U) || skb->len != 28U ||
        skb_put(skb, 128U) != NULL || skb_pull(skb, 64U) != NULL ||
        skb_push(skb, 64U) != NULL || skb_validate_state() != OK) {
        result = ERR_STATE;
    }
    if (result == OK && skb_transition(
            skb, NET_BUFFER_STATE_RX, NET_BUFFER_OWNER_ETHERNET) != OK) {
        result = ERR_STATE;
    }
    if (skb_complete(skb, result == OK ? OK : ERR_CANCELLED,
                     result == OK ? NET_BUFFER_OWNER_PROTOCOL :
                     NET_BUFFER_OWNER_NONE) != OK) {
        result = ERR_STATE;
    } else {
        completed = 1U;
    }
    if (!completed) return ERR_STATE;
    references = skb->refcount;
    while (references > 0U) {
        if (skb_release(skb) != OK) result = ERR_STATE;
        references--;
    }
    if (result != OK) LOG_ERROR("SKB", "Fixture de ponteiros falhou");
    return result;
}

static int skb_test_lifetime(void) {
    sk_buff_t* skb = alloc_skb(256U);
    uint8_t completed = 0U;
    uint32_t references;
    int result = OK;

    if (!skb) {
        LOG_ERROR("SKB", "Falha ao alocar fixture de lifetime");
        return ERR_MEM;
    }
    if (skb_release(skb) != ERR_STATE ||
        skb_transition(skb, NET_BUFFER_STATE_RX,
                       NET_BUFFER_OWNER_DRIVER) != OK ||
        skb_transition(skb, NET_BUFFER_STATE_QUEUED,
                       NET_BUFFER_OWNER_PROTOCOL) != OK ||
        skb_retain(skb) != OK ||
        skb_transition(skb, NET_BUFFER_STATE_IN_FLIGHT,
                       NET_BUFFER_OWNER_SOCKET) != OK ||
        skb_complete(skb, OK, NET_BUFFER_OWNER_SOCKET) != OK) {
        result = ERR_STATE;
    } else {
        completed = 1U;
    }
    if (!completed && skb_complete(skb, ERR_CANCELLED,
                                   NET_BUFFER_OWNER_NONE) == OK) {
        completed = 1U;
    }
    if (!completed) return ERR_STATE;
    references = skb->refcount;
    while (references > 0U) {
        if (skb_release(skb) != OK) result = ERR_STATE;
        references--;
    }
    if (result != OK) LOG_ERROR("SKB", "Fixture de lifetime falhou");
    return result;
}

static int skb_test_drop(void) {
    sk_buff_t* skb = alloc_skb(256U);
    uint8_t completed = 0U;
    uint32_t references;
    int result = OK;

    if (!skb) {
        LOG_ERROR("SKB", "Falha ao alocar fixture de descarte");
        return ERR_MEM;
    }
    if (skb_transition(skb, NET_BUFFER_STATE_IN_FLIGHT,
                       NET_BUFFER_OWNER_DRIVER) != OK ||
        skb_complete(skb, ERR_TIMEOUT, NET_BUFFER_OWNER_NONE) != OK) {
        result = ERR_STATE;
    } else {
        completed = 1U;
        if (skb_complete(skb, ERR_TIMEOUT,
                         NET_BUFFER_OWNER_NONE) != ERR_STATE) {
            result = ERR_STATE;
        }
    }
    if (!completed && skb_complete(skb, ERR_CANCELLED,
                                   NET_BUFFER_OWNER_NONE) == OK) {
        completed = 1U;
    }
    if (!completed) return ERR_STATE;
    references = skb->refcount;
    while (references > 0U) {
        if (skb_release(skb) != OK) result = ERR_STATE;
        references--;
    }
    if (result == OK && skb_release(skb) != ERR_INVALID) result = ERR_STATE;
    if (result != OK) LOG_ERROR("SKB", "Fixture de descarte falhou");
    return result;
}

int skb_self_test(void) {
    sk_buff_stats_t saved_stats;
    net_buffer_stats_t saved_buffer_stats;
    int result = OK;

    if (skb_get_stats(&saved_stats) != OK ||
        net_buffer_get_stats(&saved_buffer_stats) != OK) {
        LOG_ERROR("SKB", "Falha ao capturar metricas do autoteste");
        return ERR_STATE;
    }
    spinlock_acquire(&skb_lock);
    if (skb_stats.active_buffers || skb_testing) {
        spinlock_release(&skb_lock);
        LOG_ERROR("SKB", "Autoteste exige ausencia de sk_buff ativos");
        return ERR_STATE;
    }
    skb_testing = 1U;
    spinlock_release(&skb_lock);
    if (skb_test_pointer_ops() != OK) result = ERR_STATE;
    if (result == OK && skb_test_lifetime() != OK) result = ERR_STATE;
    if (result == OK && skb_test_drop() != OK) result = ERR_STATE;
    if (skb_validate_state() != OK) result = ERR_STATE;
    spinlock_acquire(&skb_lock);
    if (skb_stats.active_buffers || skb_registry_count_locked()) result = ERR_STATE;
    skb_stats = saved_stats;
    skb_testing = 0U;
    spinlock_release(&skb_lock);
    if (net_buffer_restore_stats(&saved_buffer_stats) != OK) result = ERR_STATE;
    if (result != OK) LOG_ERROR("SKB", "Autoteste de sk_buff falhou");
    return result;
}
