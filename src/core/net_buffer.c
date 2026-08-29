#include "core/net_buffer.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/spinlock.h"

#define NET_BUFFER_COUNTER_MAX 0xFFFFFFFFU

static net_buffer_stats_t net_buffer_stats;
static net_buffer_t* net_buffer_registry[NET_BUFFER_TRACKING_CAPACITY];
static spinlock_t net_buffer_lock;
static uint8_t net_buffer_lock_initialized;
static uint8_t net_buffer_testing;

static uint8_t net_buffer_owner_valid(net_buffer_owner_t owner) {
    return owner >= NET_BUFFER_OWNER_DRIVER &&
           owner <= NET_BUFFER_OWNER_SOCKET;
}

static uint8_t net_buffer_state_owner_valid(net_buffer_state_t state,
                                             net_buffer_owner_t owner) {
    if (state == NET_BUFFER_STATE_ALLOCATED) {
        return net_buffer_owner_valid(owner);
    }
    if (state == NET_BUFFER_STATE_RX) {
        return owner == NET_BUFFER_OWNER_DRIVER ||
               owner == NET_BUFFER_OWNER_ETHERNET;
    }
    if (state == NET_BUFFER_STATE_QUEUED) {
        return owner == NET_BUFFER_OWNER_PROTOCOL ||
               owner == NET_BUFFER_OWNER_SOCKET;
    }
    if (state == NET_BUFFER_STATE_IN_FLIGHT) {
        return net_buffer_owner_valid(owner);
    }
    if (state == NET_BUFFER_STATE_DROPPED) {
        return owner == NET_BUFFER_OWNER_NONE;
    }
    if (state == NET_BUFFER_STATE_DELIVERED) {
        return owner == NET_BUFFER_OWNER_NONE || net_buffer_owner_valid(owner);
    }
    return 0;
}

static uint8_t net_buffer_alignment_valid(uint32_t alignment) {
    if (!alignment || alignment > NET_BUFFER_MAX_ALIGNMENT) return 0;
    return (alignment & (alignment - 1U)) == 0U;
}

static int32_t net_buffer_find_locked(const net_buffer_t* buffer) {
    for (uint32_t index = 0; index < NET_BUFFER_TRACKING_CAPACITY; index++) {
        if (net_buffer_registry[index] == buffer) return (int32_t)index;
    }
    return -1;
}

static int32_t net_buffer_free_slot_locked(void) {
    for (uint32_t index = 0; index < NET_BUFFER_TRACKING_CAPACITY; index++) {
        if (!net_buffer_registry[index]) return (int32_t)index;
    }
    return -1;
}

static int net_buffer_fail_locked(int result, uint8_t duplicate) {
    net_buffer_stats.invalid_transitions++;
    if (duplicate) net_buffer_stats.duplicate_completions++;
    net_buffer_stats.last_error = result;
    return result;
}

static void net_buffer_log_failure(const char* message, int result) {
    if (!net_buffer_testing) LOG_ERROR("NETBUF", message);
    (void)result;
}

static uint8_t net_buffer_transition_allowed(net_buffer_state_t current,
                                              net_buffer_state_t next) {
    if (current == NET_BUFFER_STATE_ALLOCATED) {
        return next == NET_BUFFER_STATE_RX ||
               next == NET_BUFFER_STATE_QUEUED ||
               next == NET_BUFFER_STATE_IN_FLIGHT;
    }
    if (current == NET_BUFFER_STATE_RX) {
        return next == NET_BUFFER_STATE_QUEUED ||
               next == NET_BUFFER_STATE_IN_FLIGHT;
    }
    if (current == NET_BUFFER_STATE_QUEUED) {
        return next == NET_BUFFER_STATE_IN_FLIGHT;
    }
    return 0;
}

static int net_buffer_check_active_locked(const net_buffer_t* buffer) {
    int result = OK;

    if (!buffer) {
        result = ERR_NULL;
    } else if (!net_buffer_stats.initialized) {
        result = ERR_STATE;
    } else if (net_buffer_find_locked(buffer) < 0) {
        result = ERR_INVALID;
    } else if (!buffer->refcount || buffer->state == NET_BUFFER_STATE_FREED) {
        result = ERR_STATE;
    }
    if (result != OK && !net_buffer_testing) {
        LOG_WARN("NETBUF", "Buffer nao esta ativo");
    }
    return result;
}

static int net_buffer_complete_locked(net_buffer_t* buffer, int result,
                                      net_buffer_owner_t delivered_owner) {
    int validation = net_buffer_check_active_locked(buffer);

    if (validation != OK) return net_buffer_fail_locked(validation, 0);
    if (buffer->state == NET_BUFFER_STATE_DELIVERED ||
        buffer->state == NET_BUFFER_STATE_DROPPED) {
        return net_buffer_fail_locked(ERR_STATE, 1);
    }
    if (result == OK) {
        if (buffer->state != NET_BUFFER_STATE_RX &&
            buffer->state != NET_BUFFER_STATE_IN_FLIGHT) {
            return net_buffer_fail_locked(ERR_STATE, 0);
        }
        if (delivered_owner != NET_BUFFER_OWNER_NONE &&
            !net_buffer_owner_valid(delivered_owner)) {
            return net_buffer_fail_locked(ERR_INVALID, 0);
        }
        buffer->state = NET_BUFFER_STATE_DELIVERED;
        buffer->owner = delivered_owner;
        net_buffer_stats.delivered++;
    } else {
        if (buffer->state != NET_BUFFER_STATE_ALLOCATED &&
            buffer->state != NET_BUFFER_STATE_RX &&
            buffer->state != NET_BUFFER_STATE_QUEUED &&
            buffer->state != NET_BUFFER_STATE_IN_FLIGHT) {
            return net_buffer_fail_locked(ERR_STATE, 0);
        }
        if (delivered_owner != NET_BUFFER_OWNER_NONE) {
            return net_buffer_fail_locked(ERR_INVALID, 0);
        }
        buffer->state = NET_BUFFER_STATE_DROPPED;
        buffer->owner = NET_BUFFER_OWNER_NONE;
        buffer->completion_error = result;
        net_buffer_stats.dropped++;
    }
    return OK;
}

int net_buffer_init(void) {
    if (!net_buffer_lock_initialized) {
        spinlock_init(&net_buffer_lock);
        net_buffer_lock_initialized = 1U;
    }
    spinlock_acquire(&net_buffer_lock);
    if (net_buffer_stats.initialized) {
        spinlock_release(&net_buffer_lock);
        return OK;
    }
    kmemset(&net_buffer_stats, 0, sizeof(net_buffer_stats));
    kmemset(net_buffer_registry, 0, sizeof(net_buffer_registry));
    net_buffer_stats.initialized = 1;
    net_buffer_stats.last_error = OK;
    spinlock_release(&net_buffer_lock);
    LOG_INFO("NETBUF", "Contrato de buffers de rede inicializado");
    return OK;
}

int net_buffer_begin(net_buffer_t* buffer, uint32_t capacity,
                     uint32_t headroom, uint32_t alignment,
                     net_buffer_owner_t owner) {
    int32_t slot;

    if (!buffer) {
        LOG_ERROR("NETBUF", "Descriptor nulo ao alocar buffer");
        return ERR_NULL;
    }
    if (!capacity || headroom > capacity ||
        !net_buffer_alignment_valid(alignment) ||
        !net_buffer_owner_valid(owner)) {
        LOG_ERROR("NETBUF", "Geometria ou owner invalido no buffer");
        return ERR_INVALID;
    }
    spinlock_acquire(&net_buffer_lock);
    if (!net_buffer_stats.initialized) {
        spinlock_release(&net_buffer_lock);
        LOG_ERROR("NETBUF", "Alocacao antes da inicializacao");
        return ERR_STATE;
    }
    if (net_buffer_find_locked(buffer) >= 0 ||
        buffer->state != NET_BUFFER_STATE_INVALID) {
        spinlock_release(&net_buffer_lock);
        LOG_ERROR("NETBUF", "Descriptor de buffer ja esta em uso");
        return ERR_STATE;
    }
    slot = net_buffer_free_slot_locked();
    if (slot < 0) {
        net_buffer_fail_locked(ERR_OVERFLOW, 0);
        spinlock_release(&net_buffer_lock);
        LOG_ERROR("NETBUF", "Rastreamento estatico de buffers cheio");
        return ERR_OVERFLOW;
    }
    kmemset(buffer, 0, sizeof(*buffer));
    buffer->state = NET_BUFFER_STATE_ALLOCATED;
    buffer->owner = owner;
    buffer->refcount = 1U;
    buffer->capacity = capacity;
    buffer->headroom = headroom;
    buffer->tailroom = capacity - headroom;
    buffer->alignment = alignment;
    net_buffer_registry[slot] = buffer;
    net_buffer_stats.active_buffers++;
    net_buffer_stats.allocations++;
    if (net_buffer_stats.active_buffers > net_buffer_stats.peak_buffers) {
        net_buffer_stats.peak_buffers = net_buffer_stats.active_buffers;
    }
    spinlock_release(&net_buffer_lock);
    return OK;
}

static int net_buffer_set_layout_locked(net_buffer_t* buffer,
                                        uint32_t headroom,
                                        uint32_t length) {
    int result = net_buffer_check_active_locked(buffer);

    if (result == OK && (buffer->state == NET_BUFFER_STATE_DELIVERED ||
                         buffer->state == NET_BUFFER_STATE_DROPPED)) {
        result = ERR_STATE;
    }
    if (result == OK && headroom > buffer->capacity) result = ERR_OVERFLOW;
    if (result == OK && length > buffer->capacity - headroom) {
        result = ERR_OVERFLOW;
    }
    if (result == OK) {
        buffer->headroom = headroom;
        buffer->length = length;
        buffer->tailroom = buffer->capacity - headroom - length;
    } else {
        net_buffer_fail_locked(result, 0);
        if (!net_buffer_testing) {
            LOG_WARN("NETBUF", "Geometria de buffer recusada");
        }
    }
    return result;
}

int net_buffer_set_layout(net_buffer_t* buffer, uint32_t headroom,
                          uint32_t length) {
    int result;

    spinlock_acquire(&net_buffer_lock);
    result = net_buffer_set_layout_locked(buffer, headroom, length);
    spinlock_release(&net_buffer_lock);
    if (result != OK && !net_buffer_testing) {
        LOG_WARN("NETBUF", "Geometria invalida no buffer");
    }
    return result;
}

int net_buffer_set_length(net_buffer_t* buffer, uint32_t length) {
    uint32_t headroom = 0U;
    int result;

    spinlock_acquire(&net_buffer_lock);
    result = net_buffer_check_active_locked(buffer);
    if (result == OK) {
        headroom = buffer->headroom;
        result = net_buffer_set_layout_locked(buffer, headroom, length);
    } else {
        net_buffer_fail_locked(result, 0);
    }
    spinlock_release(&net_buffer_lock);
    if (result != OK) net_buffer_log_failure("Tamanho invalido no buffer", result);
    return result;
}

int net_buffer_transition(net_buffer_t* buffer,
                          net_buffer_state_t next_state,
                          net_buffer_owner_t next_owner) {
    int result;

    if (!net_buffer_owner_valid(next_owner) ||
        next_state == NET_BUFFER_STATE_INVALID ||
        next_state == NET_BUFFER_STATE_FREED ||
        next_state == NET_BUFFER_STATE_DELIVERED ||
        next_state == NET_BUFFER_STATE_DROPPED) {
        spinlock_acquire(&net_buffer_lock);
        net_buffer_fail_locked(ERR_INVALID, 0);
        spinlock_release(&net_buffer_lock);
        if (!net_buffer_testing) {
            LOG_WARN("NETBUF", "Transicao ou owner invalido");
        }
        return ERR_INVALID;
    }
    spinlock_acquire(&net_buffer_lock);
    result = net_buffer_check_active_locked(buffer);
    if (result == OK && !net_buffer_transition_allowed(buffer->state,
                                                       next_state)) {
        result = ERR_STATE;
    }
    if (result == OK && !net_buffer_state_owner_valid(next_state,
                                                      next_owner)) {
        result = ERR_INVALID;
    }
    if (result == OK) {
        buffer->state = next_state;
        buffer->owner = next_owner;
    } else {
        net_buffer_fail_locked(result, 0);
    }
    spinlock_release(&net_buffer_lock);
    if (result != OK) net_buffer_log_failure("Transicao de buffer recusada", result);
    return result;
}

int net_buffer_complete(net_buffer_t* buffer, int result,
                        net_buffer_owner_t delivered_owner) {
    int completion_result;

    spinlock_acquire(&net_buffer_lock);
    completion_result = net_buffer_complete_locked(buffer, result,
                                                    delivered_owner);
    spinlock_release(&net_buffer_lock);
    if (completion_result != OK) {
        net_buffer_log_failure("Conclusao de buffer recusada", completion_result);
    }
    return completion_result;
}

int net_buffer_retain(net_buffer_t* buffer) {
    int result;

    spinlock_acquire(&net_buffer_lock);
    result = net_buffer_check_active_locked(buffer);
    if (result == OK) {
        if (buffer->refcount == NET_BUFFER_COUNTER_MAX) {
            result = ERR_OVERFLOW;
        } else {
            buffer->refcount++;
            net_buffer_stats.ref_acquires++;
        }
    }
    if (result != OK) net_buffer_fail_locked(result, 0);
    spinlock_release(&net_buffer_lock);
    if (result != OK) net_buffer_log_failure("Retencao de buffer recusada", result);
    return result;
}

int net_buffer_release(net_buffer_t* buffer) {
    int32_t slot;
    int result;

    spinlock_acquire(&net_buffer_lock);
    result = net_buffer_check_active_locked(buffer);
    if (result == OK && buffer->state != NET_BUFFER_STATE_DELIVERED &&
        buffer->state != NET_BUFFER_STATE_DROPPED) {
        result = ERR_STATE;
    }
    if (result == OK) {
        if (buffer->refcount > 1U) {
            buffer->refcount--;
        } else {
            slot = net_buffer_find_locked(buffer);
            if (slot < 0) {
                result = ERR_INVALID;
            } else {
                buffer->refcount = 0;
                buffer->state = NET_BUFFER_STATE_FREED;
                buffer->owner = NET_BUFFER_OWNER_NONE;
                net_buffer_registry[slot] = NULL;
                if (net_buffer_stats.active_buffers) {
                    net_buffer_stats.active_buffers--;
                }
                net_buffer_stats.frees++;
            }
        }
        net_buffer_stats.ref_releases++;
    }
    if (result != OK) net_buffer_fail_locked(result, 0);
    spinlock_release(&net_buffer_lock);
    if (result != OK) net_buffer_log_failure("Liberacao de buffer recusada", result);
    return result;
}

int net_buffer_note_copy(uint32_t bytes) {
    spinlock_acquire(&net_buffer_lock);
    if (!net_buffer_stats.initialized) {
        spinlock_release(&net_buffer_lock);
        LOG_ERROR("NETBUF", "Copia registrada antes da inicializacao");
        return ERR_STATE;
    }
    if (bytes > NET_BUFFER_COUNTER_MAX - net_buffer_stats.copied_bytes) {
        net_buffer_fail_locked(ERR_OVERFLOW, 0);
        spinlock_release(&net_buffer_lock);
        LOG_ERROR("NETBUF", "Contador de bytes copiados excedeu limite");
        return ERR_OVERFLOW;
    }
    net_buffer_stats.copies++;
    net_buffer_stats.copied_bytes += bytes;
    spinlock_release(&net_buffer_lock);
    return OK;
}

int net_buffer_note_clone(void) {
    spinlock_acquire(&net_buffer_lock);
    if (!net_buffer_stats.initialized) {
        spinlock_release(&net_buffer_lock);
        LOG_ERROR("NETBUF", "Clone registrado antes da inicializacao");
        return ERR_STATE;
    }
    net_buffer_stats.clones++;
    spinlock_release(&net_buffer_lock);
    return OK;
}

int net_buffer_note_fragment(void) {
    spinlock_acquire(&net_buffer_lock);
    if (!net_buffer_stats.initialized) {
        spinlock_release(&net_buffer_lock);
        LOG_ERROR("NETBUF", "Fragmentacao registrada antes da inicializacao");
        return ERR_STATE;
    }
    net_buffer_stats.fragments++;
    spinlock_release(&net_buffer_lock);
    return OK;
}

int net_buffer_get_stats(net_buffer_stats_t* out_stats) {
    if (!out_stats) {
        LOG_ERROR("NETBUF", "Destino nulo ao consultar buffers");
        return ERR_NULL;
    }
    spinlock_acquire(&net_buffer_lock);
    if (!net_buffer_stats.initialized) {
        spinlock_release(&net_buffer_lock);
        LOG_ERROR("NETBUF", "Consulta antes da inicializacao");
        return ERR_STATE;
    }
    *out_stats = net_buffer_stats;
    spinlock_release(&net_buffer_lock);
    return OK;
}

int net_buffer_restore_stats(const net_buffer_stats_t* saved_stats) {
    int result = OK;

    if (!saved_stats) {
        LOG_ERROR("NETBUF", "Snapshot nulo ao restaurar estatisticas");
        return ERR_NULL;
    }
    spinlock_acquire(&net_buffer_lock);
    for (uint32_t index = 0U; index < NET_BUFFER_TRACKING_CAPACITY; index++) {
        if (net_buffer_registry[index]) {
            result = ERR_STATE;
            break;
        }
    }
    if (result == OK && (!saved_stats->initialized ||
                         saved_stats->active_buffers ||
                         saved_stats->frees > saved_stats->allocations ||
                         saved_stats->peak_buffers < saved_stats->active_buffers)) {
        result = ERR_STATE;
    }
    if (result == OK) net_buffer_stats = *saved_stats;
    spinlock_release(&net_buffer_lock);
    if (result != OK) LOG_ERROR("NETBUF", "Snapshot de estatisticas invalido");
    return result;
}

static int net_buffer_validate_entry_locked(const net_buffer_t* buffer) {
    int result = OK;

    if (!buffer || !buffer->refcount ||
        !net_buffer_state_owner_valid(buffer->state, buffer->owner)) {
        result = ERR_STATE;
    } else if (buffer->state < NET_BUFFER_STATE_ALLOCATED ||
               buffer->state > NET_BUFFER_STATE_DROPPED ||
               !buffer->capacity || buffer->headroom > buffer->capacity ||
               buffer->length > buffer->capacity - buffer->headroom ||
               buffer->tailroom !=
                   buffer->capacity - buffer->headroom - buffer->length ||
               !net_buffer_alignment_valid(buffer->alignment)) {
        result = ERR_STATE;
    }
    if (result != OK) LOG_ERROR("NETBUF", "Entrada de buffer invalida");
    return result;
}

int net_buffer_validate_state(void) {
    uint32_t active = 0;
    int result = OK;

    spinlock_acquire(&net_buffer_lock);
    if (!net_buffer_stats.initialized) {
        result = net_buffer_stats.active_buffers ? ERR_STATE : OK;
    } else {
        for (uint32_t index = 0; index < NET_BUFFER_TRACKING_CAPACITY; index++) {
            net_buffer_t* buffer = net_buffer_registry[index];

            if (!buffer) continue;
            active++;
            if (net_buffer_validate_entry_locked(buffer) != OK) {
                result = ERR_STATE;
                break;
            }
            for (uint32_t other = index + 1U;
                 other < NET_BUFFER_TRACKING_CAPACITY; other++) {
                if (buffer == net_buffer_registry[other]) result = ERR_STATE;
            }
        }
        if (active != net_buffer_stats.active_buffers ||
            active > NET_BUFFER_TRACKING_CAPACITY ||
            net_buffer_stats.frees > net_buffer_stats.allocations ||
            net_buffer_stats.peak_buffers < active) {
            result = ERR_STATE;
        }
    }
    spinlock_release(&net_buffer_lock);
    if (result != OK) LOG_ERROR("NETBUF", "Estado global de buffers invalido");
    return result;
}

static int net_buffer_test_delivered(void) {
    net_buffer_t fixture;
    int result;

    kmemset(&fixture, 0, sizeof(fixture));
    result = net_buffer_begin(&fixture, 1518U, 32U, 1U,
                              NET_BUFFER_OWNER_ETHERNET);
    if (result == OK && net_buffer_release(&fixture) != ERR_STATE) {
        result = ERR_STATE;
    }
    if (result == OK && net_buffer_transition(
            &fixture, NET_BUFFER_STATE_DELIVERED,
            NET_BUFFER_OWNER_NONE) != ERR_INVALID) {
        result = ERR_STATE;
    }
    if (result == OK) result = net_buffer_set_length(&fixture, 128U);
    if (result == OK) result = net_buffer_transition(
        &fixture, NET_BUFFER_STATE_RX, NET_BUFFER_OWNER_ETHERNET);
    if (result == OK) result = net_buffer_transition(
        &fixture, NET_BUFFER_STATE_QUEUED, NET_BUFFER_OWNER_PROTOCOL);
    if (result == OK) result = net_buffer_retain(&fixture);
    if (result == OK) result = net_buffer_transition(
        &fixture, NET_BUFFER_STATE_IN_FLIGHT, NET_BUFFER_OWNER_SOCKET);
    if (result == OK) result = net_buffer_complete(
        &fixture, OK, NET_BUFFER_OWNER_SOCKET);
    if (result == OK) result = net_buffer_release(&fixture);
    if (result == OK) result = net_buffer_release(&fixture);
    if (result == OK && net_buffer_release(&fixture) != ERR_INVALID) {
        result = ERR_STATE;
    }
    if (result == OK && (fixture.state != NET_BUFFER_STATE_FREED ||
                         fixture.refcount ||
                         net_buffer_validate_state() != OK)) {
        result = ERR_STATE;
    }
    if (result != OK) LOG_ERROR("NETBUF", "Fixture de entrega falhou");
    return result;
}

static int net_buffer_test_dropped(void) {
    net_buffer_t fixture;
    int result;

    kmemset(&fixture, 0, sizeof(fixture));
    result = net_buffer_begin(&fixture, 512U, 0U, 1U,
                              NET_BUFFER_OWNER_ETHERNET);
    if (result == OK) result = net_buffer_complete(
        &fixture, ERR_TIMEOUT, NET_BUFFER_OWNER_NONE);
    if (result == OK && net_buffer_complete(
        &fixture, ERR_TIMEOUT, NET_BUFFER_OWNER_NONE) != ERR_STATE) {
        result = ERR_STATE;
    }
    if (result == OK) result = net_buffer_release(&fixture);
    if (result != OK) return result;

    kmemset(&fixture, 0, sizeof(fixture));
    result = net_buffer_begin(&fixture, 512U, 0U, 1U,
                              NET_BUFFER_OWNER_ETHERNET);
    if (result == OK) result = net_buffer_transition(
        &fixture, NET_BUFFER_STATE_QUEUED, NET_BUFFER_OWNER_PROTOCOL);
    if (result == OK) result = net_buffer_complete(
        &fixture, ERR_CANCELLED, NET_BUFFER_OWNER_NONE);
    if (result == OK && net_buffer_complete(
        &fixture, ERR_CANCELLED, NET_BUFFER_OWNER_NONE) != ERR_STATE) {
        result = ERR_STATE;
    }
    if (result == OK) result = net_buffer_release(&fixture);
    return result;
}

int net_buffer_self_test(void) {
    net_buffer_stats_t saved;
    net_buffer_t invalid_fixture;
    int result = OK;

    if (net_buffer_get_stats(&saved) != OK) return ERR_STATE;
    spinlock_acquire(&net_buffer_lock);
    if (net_buffer_stats.active_buffers || net_buffer_testing) {
        spinlock_release(&net_buffer_lock);
        LOG_ERROR("NETBUF", "Autoteste exige ausencia de buffers ativos");
        return ERR_STATE;
    }
    net_buffer_testing = 1U;
    spinlock_release(&net_buffer_lock);
    kmemset(&invalid_fixture, 0, sizeof(invalid_fixture));
    if (net_buffer_begin(&invalid_fixture, 64U, 16U, 1U,
                         NET_BUFFER_OWNER_ETHERNET) != OK ||
        net_buffer_set_length(&invalid_fixture, 49U) != ERR_OVERFLOW) {
        result = ERR_STATE;
    }
    if (invalid_fixture.state != NET_BUFFER_STATE_INVALID) {
        net_buffer_complete(&invalid_fixture, ERR_STATE,
                            NET_BUFFER_OWNER_NONE);
        net_buffer_release(&invalid_fixture);
    }
    if (result == OK) result = net_buffer_test_delivered();
    if (result == OK) result = net_buffer_test_dropped();
    if (result == OK && net_buffer_note_copy(128U) != OK) result = ERR_STATE;
    if (result == OK && net_buffer_note_clone() != OK) result = ERR_STATE;
    if (result == OK && net_buffer_note_fragment() != OK) result = ERR_STATE;
    if (net_buffer_validate_state() != OK) result = ERR_STATE;
    spinlock_acquire(&net_buffer_lock);
    if (net_buffer_stats.active_buffers ||
        net_buffer_find_locked(&invalid_fixture) >= 0) {
        result = ERR_STATE;
    }
    kmemset(net_buffer_registry, 0, sizeof(net_buffer_registry));
    net_buffer_stats.active_buffers = 0U;
    net_buffer_testing = 0U;
    net_buffer_stats = saved;
    spinlock_release(&net_buffer_lock);
    if (result != OK) LOG_ERROR("NETBUF", "Autoteste de buffers falhou");
    return result;
}
