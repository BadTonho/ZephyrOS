#include "core/wait.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/timer.h"
#include "process/process.h"
#include "process/thread.h"

#define WAIT_EFLAGS_INTERRUPT_ENABLE (1U << 9U)
#define WAIT_QUEUE_ID_GENERATION_SHIFT 8U
#define WAIT_QUEUE_ID_GENERATION_MAX 0x00FFFFFFU
#define WAIT_VALIDATE_ENTRY_LIMIT 256U

typedef struct {
    wait_stats_t stats;
    wait_queue_head_t* registry[WAIT_QUEUE_REGISTRY_CAPACITY];
    uint32_t generations[WAIT_QUEUE_REGISTRY_CAPACITY];
} wait_service_t;

typedef struct {
    uint8_t blocked;
    uint8_t woken;
    wait_reason_t reason;
} wait_test_target_t;

typedef struct {
    uint8_t ready;
    uint32_t calls;
} wait_test_condition_t;

static wait_service_t wait_service;
static uint32_t wait_test_wake_order[4];
static uint32_t wait_test_wake_count;
static wait_queue_head_t
    wait_test_capacity_queues[WAIT_QUEUE_REGISTRY_CAPACITY];

static uint32_t wait_irq_save(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static void wait_irq_restore(uint32_t flags) {
    if (flags & WAIT_EFLAGS_INTERRUPT_ENABLE) {
        asm volatile("sti" : : : "memory");
    }
}

static uint8_t wait_interrupts_enabled(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0" : "=r"(flags));
    return (flags & WAIT_EFLAGS_INTERRUPT_ENABLE) ? 1U : 0U;
}

static uint32_t wait_name_length(const char* text, uint32_t capacity) {
    uint32_t length = 0U;

    if (!text) return 0U;
    while (length < capacity && text[length]) length++;
    return length;
}

static void wait_copy_text(char* destination, uint32_t capacity,
                           const char* source) {
    uint32_t index = 0U;

    if (!destination || !capacity) return;
    if (!source) source = "";
    while (index + 1U < capacity && source[index]) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int wait_queue_valid(const wait_queue_head_t* queue) {
    uint32_t slot;

    if (!queue || !queue->initialized || !queue->id) return 0;
    slot = queue->registry_slot;
    return slot < WAIT_QUEUE_REGISTRY_CAPACITY &&
           wait_service.registry[slot] == queue;
}

static int wait_reason_valid(wait_reason_t reason) {
    return reason == WAIT_REASON_EVENT ||
           reason == WAIT_REASON_TIMEOUT ||
           reason == WAIT_REASON_CANCELLED ||
           reason == WAIT_REASON_DEVICE_UNAVAILABLE ||
           reason == WAIT_REASON_SIGNAL;
}

static int wait_target_valid(wait_target_type_t target) {
    return target == WAIT_TARGET_PROCESS || target == WAIT_TARGET_THREAD ||
           target == WAIT_TARGET_ANY;
}

static void wait_note_reason_locked(wait_reason_t reason) {
    if (reason == WAIT_REASON_EVENT) wait_service.stats.event_wakes++;
    else if (reason == WAIT_REASON_TIMEOUT) {
        wait_service.stats.timeout_wakes++;
    } else if (reason == WAIT_REASON_CANCELLED) {
        wait_service.stats.cancellation_wakes++;
    } else if (reason == WAIT_REASON_DEVICE_UNAVAILABLE) {
        wait_service.stats.unavailable_wakes++;
    } else if (reason == WAIT_REASON_SIGNAL) {
        wait_service.stats.signal_wakes++;
    }
}

static void wait_queue_append_locked(wait_queue_head_t* queue,
                                     wait_queue_entry_t* entry) {
    entry->previous = queue->last;
    entry->next = 0;
    entry->queue = queue;
    entry->linked = 1U;
    if (queue->last) queue->last->next = entry;
    else queue->first = entry;
    queue->last = entry;
    queue->waiters++;
    if (queue->waiters > queue->peak_waiters) {
        queue->peak_waiters = queue->waiters;
    }
    wait_service.stats.active_waiters++;
    wait_service.stats.waits_started++;
    if (wait_service.stats.active_waiters >
        wait_service.stats.peak_waiters) {
        wait_service.stats.peak_waiters =
            wait_service.stats.active_waiters;
    }
}

static int wait_queue_unlink_locked(wait_queue_entry_t* entry,
                                    wait_reason_t reason) {
    wait_queue_head_t* queue;

    if (!entry || !entry->linked || !entry->queue) {
        wait_service.stats.orphan_errors++;
        return 0;
    }
    queue = entry->queue;
    if (entry->previous) entry->previous->next = entry->next;
    else queue->first = entry->next;
    if (entry->next) entry->next->previous = entry->previous;
    else queue->last = entry->previous;
    if (queue->waiters) queue->waiters--;
    else wait_service.stats.orphan_errors++;
    if (wait_service.stats.active_waiters) {
        wait_service.stats.active_waiters--;
    } else {
        wait_service.stats.orphan_errors++;
    }
    entry->previous = 0;
    entry->next = 0;
    entry->queue = 0;
    entry->linked = 0U;
    entry->reason = reason;
    wait_note_reason_locked(reason);
    return 1;
}

static uint32_t wait_queue_make_id(uint32_t slot, uint32_t generation) {
    return (generation << WAIT_QUEUE_ID_GENERATION_SHIFT) | (slot + 1U);
}

static int wait_queue_register_locked(wait_queue_head_t* queue) {
    for (uint32_t slot = 0U; slot < WAIT_QUEUE_REGISTRY_CAPACITY; slot++) {
        uint32_t generation;

        if (wait_service.registry[slot]) continue;
        generation = wait_service.generations[slot] + 1U;
        if (!generation || generation > WAIT_QUEUE_ID_GENERATION_MAX) {
            generation = 1U;
        }
        wait_service.generations[slot] = generation;
        wait_service.registry[slot] = queue;
        queue->registry_slot = (uint8_t)slot;
        queue->id = wait_queue_make_id(slot, generation);
        wait_service.stats.channels_active++;
        if (wait_service.stats.channels_active >
            wait_service.stats.registry_peak) {
            wait_service.stats.registry_peak =
                wait_service.stats.channels_active;
        }
        return 1;
    }
    wait_service.stats.registration_rejections++;
    return 0;
}

static void wait_queue_unregister_locked(wait_queue_head_t* queue) {
    uint32_t slot = queue->registry_slot;

    if (slot < WAIT_QUEUE_REGISTRY_CAPACITY &&
        wait_service.registry[slot] == queue) {
        wait_service.registry[slot] = 0;
        if (wait_service.stats.channels_active) {
            wait_service.stats.channels_active--;
        }
    } else {
        wait_service.stats.orphan_errors++;
    }
}

static int wait_queue_reset_locked(wait_queue_head_t* queue) {
    if (!wait_queue_valid(queue) || queue->waiters ||
        queue->first || queue->last) return 0;
    wait_queue_unregister_locked(queue);
    kmemset(queue, 0, sizeof(*queue));
    return 1;
}

static int wait_queue_deadline_reached(uint32_t deadline) {
    return (int32_t)(timer_get_ticks() - deadline) >= 0;
}

static int wait_condition_snapshot(wait_queue_head_t* queue,
                                   wait_condition_fn_t condition,
                                   void* context, uint8_t* out_ready,
                                   uint8_t* out_available,
                                   uint32_t* out_condition) {
    uint32_t flags = wait_irq_save();
    int result;

    if (!wait_queue_valid(queue)) {
        wait_service.stats.invalid_operations++;
        wait_irq_restore(flags);
        LOG_ERROR("WAIT", "Fila invalida ao avaliar condicao");
        return ERR_STATE;
    }
    result = condition(context, out_ready);
    if (result == OK) {
        *out_available = queue->available;
        *out_condition = queue->condition;
    }
    wait_irq_restore(flags);
    return result;
}

void wait_init(void) {
    LOG_INFO("WAIT", "Inicializando servico de espera");
    kmemset(&wait_service, 0, sizeof(wait_service));
    wait_service.stats.initialized = 1U;
    wait_service.stats.registry_capacity = WAIT_QUEUE_REGISTRY_CAPACITY;
    LOG_INFO("WAIT", "Servico de espera inicializado com sucesso");
}

int init_waitqueue_head(wait_queue_head_t* queue, const char* owner) {
    uint32_t flags;
    uint32_t length;
    int result;

    if (!wait_service.stats.initialized) {
        LOG_ERROR("WAIT", "Fila criada antes da inicializacao");
        return ERR_STATE;
    }
    if (!queue || !owner) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Parametros nulos ao criar fila");
        return ERR_NULL;
    }
    if (queue->initialized) {
        wait_service.stats.invalid_operations++;
        LOG_WARN("WAIT", "Fila ja inicializada");
        return ERR_STATE;
    }
    length = wait_name_length(owner, WAIT_CHANNEL_OWNER_LENGTH);
    if (!length || length >= WAIT_CHANNEL_OWNER_LENGTH) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Proprietario de fila invalido");
        return length ? ERR_OVERFLOW : ERR_INVALID;
    }

    flags = wait_irq_save();
    kmemset(queue, 0, sizeof(*queue));
    wait_copy_text(queue->owner, sizeof(queue->owner), owner);
    queue->available = 1U;
    queue->initialized = 1U;
    result = wait_queue_register_locked(queue) ? OK : ERR_OVERFLOW;
    if (result != OK) kmemset(queue, 0, sizeof(*queue));
    wait_irq_restore(flags);
    if (result != OK) LOG_ERROR("WAIT", "Registro de filas ficou cheio");
    return result;
}

int wait_channel_init(wait_channel_t* channel, const char* owner) {
    return init_waitqueue_head(channel, owner);
}

int wait_channel_reset(wait_channel_t* channel) {
    uint32_t flags;
    int result;

    flags = wait_irq_save();
    if (!wait_queue_valid(channel)) {
        wait_service.stats.invalid_operations++;
        wait_irq_restore(flags);
        LOG_ERROR("WAIT", "Fila invalida ao destruir");
        return ERR_STATE;
    }
    result = wait_queue_reset_locked(channel);
    wait_irq_restore(flags);
    if (!result) {
        wait_service.stats.invalid_operations++;
        LOG_WARN("WAIT", "Fila com waiters nao pode ser destruida");
        return ERR_STATE;
    }
    return OK;
}

int wait_channel_signal(wait_channel_t* channel) {
    uint32_t flags = wait_irq_save();

    if (!wait_queue_valid(channel)) {
        wait_service.stats.invalid_operations++;
        wait_irq_restore(flags);
        LOG_ERROR("WAIT", "Fila invalida ao sinalizar");
        return ERR_STATE;
    }
    channel->condition++;
    if (!channel->condition) channel->condition = 1U;
    wait_irq_restore(flags);
    return OK;
}

int wait_channel_get_condition(const wait_channel_t* channel,
                               uint32_t* out_condition) {
    uint32_t flags;

    if (!out_condition) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Destino nulo para condicao da fila");
        return ERR_NULL;
    }
    flags = wait_irq_save();
    if (!wait_queue_valid(channel)) {
        wait_service.stats.invalid_operations++;
        wait_irq_restore(flags);
        LOG_ERROR("WAIT", "Fila invalida ao consultar condicao");
        return ERR_STATE;
    }
    *out_condition = channel->condition;
    wait_irq_restore(flags);
    return OK;
}

int wait_queue_entry_init(wait_queue_entry_t* entry, void* target,
                          const char* target_name,
                          wait_target_type_t target_type,
                          uint32_t target_id,
                          wait_queue_transition_fn_t block,
                          wait_queue_transition_fn_t wake,
                          wait_queue_yield_fn_t yield) {
    if (!entry || !target || !target_name || !block || !wake || !yield) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Metadados nulos na entrada de espera");
        return ERR_NULL;
    }
    if ((target_type != WAIT_TARGET_PROCESS &&
         target_type != WAIT_TARGET_THREAD) || entry->linked) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Entrada de espera em estado invalido");
        return ERR_STATE;
    }
    kmemset(entry, 0, sizeof(*entry));
    entry->target = target;
    entry->target_name = target_name;
    entry->target_type = target_type;
    entry->target_id = target_id;
    entry->block = block;
    entry->wake = wake;
    entry->yield = yield;
    entry->deadline_tick = WAIT_TIMEOUT_INFINITE;
    return OK;
}

int wait_queue_block(wait_queue_head_t* queue, wait_queue_entry_t* entry,
                     uint32_t observed_condition, uint32_t timeout_ticks,
                     wait_reason_t* out_reason) {
    uint32_t flags;

    if (!out_reason) {
        LOG_ERROR("WAIT", "Destino nulo para resultado da espera");
        return ERR_NULL;
    }
    *out_reason = WAIT_REASON_NONE;
    if (!queue || !entry || !entry->target || !entry->block ||
        !entry->wake || !entry->yield || entry->linked) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Fila ou entrada invalida para bloqueio");
        return ERR_INVALID;
    }
    if (timeout_ticks != WAIT_TIMEOUT_INFINITE &&
        timeout_ticks > WAIT_MAX_TIMEOUT_TICKS) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Timeout de espera excede o limite");
        return ERR_INVALID;
    }
    if (!wait_interrupts_enabled()) {
        wait_service.stats.context_errors++;
        LOG_ERROR("WAIT", "Bloqueio solicitado com interrupcoes desabilitadas");
        return ERR_STATE;
    }

    flags = wait_irq_save();
    if (!wait_queue_valid(queue)) {
        wait_service.stats.invalid_operations++;
        wait_irq_restore(flags);
        LOG_ERROR("WAIT", "Fila nao registrada no bloqueio");
        return ERR_STATE;
    }
    if (!queue->available) {
        entry->reason = WAIT_REASON_DEVICE_UNAVAILABLE;
        *out_reason = entry->reason;
        wait_irq_restore(flags);
        return OK;
    }
    if (queue->condition != observed_condition) {
        entry->reason = WAIT_REASON_EVENT;
        *out_reason = entry->reason;
        wait_irq_restore(flags);
        return OK;
    }
    if (timeout_ticks == WAIT_TIMEOUT_IMMEDIATE) {
        entry->reason = WAIT_REASON_TIMEOUT;
        *out_reason = entry->reason;
        wait_irq_restore(flags);
        return OK;
    }

    entry->observed_condition = observed_condition;
    entry->deadline_active = timeout_ticks == WAIT_TIMEOUT_INFINITE ? 0U : 1U;
    entry->deadline_tick = entry->deadline_active ?
                           timer_get_ticks() + timeout_ticks :
                           WAIT_TIMEOUT_INFINITE;
    entry->reason = WAIT_REASON_NONE;
    wait_queue_append_locked(queue, entry);
    entry->block(entry->target, entry);
    wait_irq_restore(flags);
    entry->yield(entry->target);
    *out_reason = entry->reason;
    return OK;
}

int wait_queue_remove(wait_queue_entry_t* entry, wait_reason_t reason) {
    uint32_t flags;
    int result;

    if (!entry || !wait_reason_valid(reason)) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Remocao recebeu parametros invalidos");
        return ERR_INVALID;
    }
    flags = wait_irq_save();
    result = wait_queue_unlink_locked(entry, reason);
    if (result) entry->wake(entry->target, entry);
    wait_irq_restore(flags);
    if (!result) LOG_WARN("WAIT", "Entrada ausente ao remover waiter");
    return result ? OK : ERR_STATE;
}

int wait_queue_wake_target(wait_queue_head_t* queue,
                           wait_target_type_t target,
                           wait_wake_mode_t mode, wait_reason_t reason,
                           uint32_t* out_woken) {
    wait_queue_entry_t* entry;
    uint32_t flags;
    uint32_t woken = 0U;

    if (!out_woken) {
        LOG_ERROR("WAIT", "Destino nulo para contagem de wake");
        return ERR_NULL;
    }
    *out_woken = 0U;
    if (!queue || !wait_target_valid(target) ||
        (mode != WAIT_WAKE_ONE && mode != WAIT_WAKE_ALL) ||
        !wait_reason_valid(reason)) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Parametros invalidos ao acordar fila");
        return ERR_INVALID;
    }
    flags = wait_irq_save();
    if (!wait_queue_valid(queue)) {
        wait_service.stats.invalid_operations++;
        wait_irq_restore(flags);
        LOG_ERROR("WAIT", "Fila nao registrada no wake");
        return ERR_STATE;
    }
    if (reason == WAIT_REASON_EVENT ||
        reason == WAIT_REASON_DEVICE_UNAVAILABLE) {
        queue->condition++;
        if (!queue->condition) queue->condition = 1U;
    }
    if (reason == WAIT_REASON_DEVICE_UNAVAILABLE) queue->available = 0U;
    if (mode == WAIT_WAKE_ONE) wait_service.stats.wake_one_calls++;
    else wait_service.stats.wake_all_calls++;

    entry = queue->first;
    while (entry) {
        wait_queue_entry_t* next = entry->next;

        if (target == WAIT_TARGET_ANY || entry->target_type == target) {
            if (wait_queue_unlink_locked(entry, reason)) {
                entry->wake(entry->target, entry);
                woken++;
            }
            if (mode == WAIT_WAKE_ONE) break;
        }
        entry = next;
    }
    wait_irq_restore(flags);
    *out_woken = woken;
    return OK;
}

int wake_up(wait_queue_head_t* queue, uint32_t* out_woken) {
    return wait_queue_wake_target(queue, WAIT_TARGET_ANY, WAIT_WAKE_ONE,
                                  WAIT_REASON_EVENT, out_woken);
}

int wake_up_all(wait_queue_head_t* queue, uint32_t* out_woken) {
    return wait_queue_wake_target(queue, WAIT_TARGET_ANY, WAIT_WAKE_ALL,
                                  WAIT_REASON_EVENT, out_woken);
}

int wait_channel_set_available(wait_channel_t* channel, uint8_t available) {
    uint32_t flags;
    uint32_t woken = 0U;

    if (!available) {
        return wait_queue_wake_target(
            channel, WAIT_TARGET_ANY, WAIT_WAKE_ALL,
            WAIT_REASON_DEVICE_UNAVAILABLE, &woken);
    }
    flags = wait_irq_save();
    if (!wait_queue_valid(channel)) {
        wait_service.stats.invalid_operations++;
        wait_irq_restore(flags);
        LOG_ERROR("WAIT", "Fila invalida ao alterar disponibilidade");
        return ERR_STATE;
    }
    channel->available = 1U;
    wait_irq_restore(flags);
    return OK;
}

int wait_channel_is_available(const wait_channel_t* channel,
                              uint8_t* out_available) {
    uint32_t flags;

    if (!out_available) {
        LOG_ERROR("WAIT", "Destino nulo para disponibilidade da fila");
        return ERR_NULL;
    }
    flags = wait_irq_save();
    if (!wait_queue_valid(channel)) {
        wait_service.stats.invalid_operations++;
        wait_irq_restore(flags);
        LOG_ERROR("WAIT", "Fila invalida ao consultar disponibilidade");
        return ERR_STATE;
    }
    *out_available = channel->available;
    wait_irq_restore(flags);
    return OK;
}

static int wait_current_task(wait_queue_head_t* queue,
                             uint32_t observed_condition,
                             uint32_t timeout_ticks,
                             wait_reason_t* out_reason) {
    if (thread_get_current()) {
        return thread_wait(queue, observed_condition,
                           timeout_ticks, out_reason);
    }
    return process_wait(queue, observed_condition,
                        timeout_ticks, out_reason);
}

int wait_event_timeout(wait_queue_head_t* queue,
                       wait_condition_fn_t condition, void* context,
                       uint32_t timeout_ticks, wait_reason_t* out_reason) {
    uint32_t deadline = WAIT_TIMEOUT_INFINITE;
    uint32_t remaining = timeout_ticks;

    if (!queue || !condition || !out_reason) {
        LOG_ERROR("WAIT", "Parametros nulos em wait_event_timeout");
        return ERR_NULL;
    }
    if (timeout_ticks != WAIT_TIMEOUT_INFINITE &&
        timeout_ticks > WAIT_MAX_TIMEOUT_TICKS) {
        LOG_ERROR("WAIT", "Timeout invalido em wait_event_timeout");
        return ERR_INVALID;
    }
    *out_reason = WAIT_REASON_NONE;
    if (timeout_ticks != WAIT_TIMEOUT_INFINITE) {
        deadline = timer_get_ticks() + timeout_ticks;
    }

    while (1) {
        uint32_t observed_condition;
        uint8_t ready = 0U;
        uint8_t available = 0U;
        int result = wait_condition_snapshot(
            queue, condition, context, &ready, &available,
            &observed_condition);

        if (result != OK) {
            LOG_ERROR("WAIT", "Condicao de espera retornou erro");
            return result;
        }
        if (ready) {
            *out_reason = WAIT_REASON_EVENT;
            return OK;
        }
        if (!available) {
            *out_reason = WAIT_REASON_DEVICE_UNAVAILABLE;
            return OK;
        }
        if (timeout_ticks != WAIT_TIMEOUT_INFINITE) {
            if (wait_queue_deadline_reached(deadline)) remaining = 0U;
            else remaining = deadline - timer_get_ticks();
        }
        result = wait_current_task(queue, observed_condition,
                                   remaining, out_reason);
        if (result != OK || *out_reason != WAIT_REASON_EVENT) return result;
    }
}

int wait_event(wait_queue_head_t* queue, wait_condition_fn_t condition,
               void* context, wait_reason_t* out_reason) {
    return wait_event_timeout(queue, condition, context,
                              WAIT_TIMEOUT_INFINITE, out_reason);
}

void wait_note_waiter(wait_channel_t* channel) {
    (void)channel;
}

void wait_note_wake(wait_channel_t* channel, wait_reason_t reason) {
    (void)channel;
    (void)reason;
}

int wait_deadline_remaining_active(uint32_t deadline_tick, uint8_t active,
                                   uint32_t* out_remaining) {
    uint32_t now;

    if (!out_remaining) {
        LOG_ERROR("WAIT", "Destino nulo para prazo restante");
        return ERR_NULL;
    }
    if (!active) {
        *out_remaining = 0U;
        return OK;
    }
    now = timer_get_ticks();
    *out_remaining = (int32_t)(now - deadline_tick) >= 0 ?
                     0U : deadline_tick - now;
    return OK;
}

int wait_deadline_remaining(uint32_t deadline_tick, uint32_t* out_remaining) {
    return wait_deadline_remaining_active(
        deadline_tick, deadline_tick != WAIT_TIMEOUT_INFINITE, out_remaining);
}

int wait_get_stats(wait_stats_t* out_stats) {
    uint32_t flags;

    if (!out_stats) {
        LOG_ERROR("WAIT", "Destino nulo para estatisticas");
        return ERR_NULL;
    }
    flags = wait_irq_save();
    if (!wait_service.stats.initialized) {
        wait_irq_restore(flags);
        LOG_ERROR("WAIT", "Estatisticas consultadas antes da inicializacao");
        return ERR_STATE;
    }
    *out_stats = wait_service.stats;
    wait_irq_restore(flags);
    return OK;
}

int wait_queue_copy_info(wait_queue_info_t* output,
                         uint32_t max_entries, uint32_t* out_count) {
    uint32_t flags;
    uint32_t count = 0U;

    if (!out_count || (max_entries && !output)) {
        LOG_ERROR("WAIT", "Destino invalido para listar filas");
        return ERR_NULL;
    }
    flags = wait_irq_save();
    for (uint32_t slot = 0U;
         slot < WAIT_QUEUE_REGISTRY_CAPACITY && count < max_entries; slot++) {
        wait_queue_head_t* queue = wait_service.registry[slot];
        wait_queue_info_t* info;

        if (!queue) continue;
        info = &output[count++];
        kmemset(info, 0, sizeof(*info));
        info->id = queue->id;
        wait_copy_text(info->owner, sizeof(info->owner), queue->owner);
        info->condition = queue->condition;
        info->waiters = queue->waiters;
        info->peak_waiters = queue->peak_waiters;
        info->available = queue->available;
    }
    *out_count = count;
    wait_irq_restore(flags);
    return OK;
}

static void wait_fill_waiter_info(wait_info_t* info,
                                  const wait_queue_head_t* queue,
                                  const wait_queue_entry_t* entry,
                                  uint32_t position) {
    kmemset(info, 0, sizeof(*info));
    info->id = entry->target_id;
    info->queue_id = queue->id;
    info->queue_position = position;
    info->target = entry->target_type;
    wait_copy_text(info->name, sizeof(info->name), entry->target_name);
    wait_copy_text(info->channel_owner, sizeof(info->channel_owner),
                   queue->owner);
    info->reason = entry->reason;
    info->deadline_tick = entry->deadline_tick;
    info->deadline_active = entry->deadline_active;
    if (info->deadline_active) {
        wait_deadline_remaining_active(entry->deadline_tick, 1U,
                                       &info->remaining_ticks);
    }
    info->active = entry->linked;
}

int wait_queue_copy_waiters(wait_info_t* output,
                            uint32_t max_entries, uint32_t* out_count) {
    uint32_t flags;
    uint32_t count = 0U;

    if (!out_count || (max_entries && !output)) {
        LOG_ERROR("WAIT", "Destino invalido para listar waiters");
        return ERR_NULL;
    }
    flags = wait_irq_save();
    for (uint32_t slot = 0U;
         slot < WAIT_QUEUE_REGISTRY_CAPACITY && count < max_entries; slot++) {
        wait_queue_head_t* queue = wait_service.registry[slot];
        wait_queue_entry_t* entry;
        uint32_t position = 0U;

        if (!queue) continue;
        entry = queue->first;
        while (entry && count < max_entries) {
            wait_fill_waiter_info(&output[count++], queue, entry, position++);
            entry = entry->next;
        }
    }
    *out_count = count;
    wait_irq_restore(flags);
    return OK;
}

static int wait_validate_queue_locked(const wait_queue_head_t* queue,
                                      uint32_t* out_waiters) {
    const wait_queue_entry_t* entry = queue->first;
    const wait_queue_entry_t* previous = 0;
    uint32_t count = 0U;

    while (entry && count < WAIT_VALIDATE_ENTRY_LIMIT) {
        if (!entry->linked || entry->queue != queue ||
            entry->previous != previous || !entry->target ||
            !entry->block || !entry->wake || !entry->yield) return 0;
        previous = entry;
        entry = entry->next;
        count++;
    }
    if (entry || previous != queue->last || count != queue->waiters ||
        (!count && (queue->first || queue->last)) ||
        (count && (!queue->first || !queue->last))) return 0;
    *out_waiters += count;
    return 1;
}

int wait_validate_state(void) {
    uint32_t flags;
    uint32_t queues = 0U;
    uint32_t waiters = 0U;
    int result = OK;

    flags = wait_irq_save();
    if (!wait_service.stats.initialized ||
        wait_service.stats.registry_capacity !=
            WAIT_QUEUE_REGISTRY_CAPACITY) result = ERR_STATE;
    for (uint32_t slot = 0U;
         result == OK && slot < WAIT_QUEUE_REGISTRY_CAPACITY; slot++) {
        wait_queue_head_t* queue = wait_service.registry[slot];

        if (!queue) continue;
        queues++;
        if (!queue->initialized || !queue->id ||
            queue->registry_slot != slot ||
            wait_name_length(queue->owner, WAIT_CHANNEL_OWNER_LENGTH) == 0U ||
            !wait_validate_queue_locked(queue, &waiters)) {
            result = ERR_STATE;
        }
    }
    if (queues != wait_service.stats.channels_active ||
        waiters != wait_service.stats.active_waiters ||
        queues > WAIT_QUEUE_REGISTRY_CAPACITY) result = ERR_STATE;
    wait_irq_restore(flags);
    if (result != OK) LOG_ERROR("WAIT", "Invariantes das filas invalidas");
    return result;
}

static void wait_test_block(void* target, wait_queue_entry_t* entry) {
    wait_test_target_t* test = (wait_test_target_t*)target;

    (void)entry;
    test->blocked = 1U;
}

static void wait_test_wake(void* target, wait_queue_entry_t* entry) {
    wait_test_target_t* test = (wait_test_target_t*)target;

    test->blocked = 0U;
    test->woken = 1U;
    test->reason = entry->reason;
    if (wait_test_wake_count < 4U) {
        wait_test_wake_order[wait_test_wake_count++] = entry->target_id;
    }
}

static void wait_test_yield(void* target) {
    (void)target;
}

static void wait_test_count(wait_self_test_result_t* result, int passed) {
    if (passed) result->passed++;
    else result->failed++;
}

static int wait_test_condition(void* context, uint8_t* out_ready) {
    wait_test_condition_t* condition = (wait_test_condition_t*)context;

    if (!context || !out_ready) {
        LOG_ERROR("WAIT", "Contexto nulo na condicao de autoteste");
        return ERR_NULL;
    }
    condition->calls++;
    *out_ready = condition->ready;
    return OK;
}

static int wait_test_prepare_entry(wait_queue_entry_t* entry,
                                   wait_test_target_t* target,
                                   wait_target_type_t type, uint32_t id) {
    kmemset(target, 0, sizeof(*target));
    kmemset(entry, 0, sizeof(*entry));
    return wait_queue_entry_init(entry, target, "fixture", type, id,
                                 wait_test_block, wait_test_wake,
                                 wait_test_yield);
}

static int wait_test_registry_capacity(void) {
    uint32_t flags = wait_irq_save();
    uint32_t initial = wait_service.stats.channels_active;
    uint32_t original_peak = wait_service.stats.registry_peak;
    uint32_t created = 0U;
    uint32_t expected;
    int valid = 1;

    if (initial > WAIT_QUEUE_REGISTRY_CAPACITY) {
        wait_irq_restore(flags);
        return 0;
    }
    expected = WAIT_QUEUE_REGISTRY_CAPACITY - initial;
    kmemset(wait_test_capacity_queues, 0,
            sizeof(wait_test_capacity_queues));
    while (created < expected) {
        wait_queue_head_t* queue = &wait_test_capacity_queues[created];

        wait_copy_text(queue->owner, sizeof(queue->owner), "wait-capacity");
        queue->available = 1U;
        queue->initialized = 1U;
        if (!wait_queue_register_locked(queue)) {
            valid = 0;
            break;
        }
        created++;
    }
    if (created != expected ||
        wait_service.stats.channels_active != WAIT_QUEUE_REGISTRY_CAPACITY) {
        valid = 0;
    }
    for (uint32_t slot = 0U; slot < WAIT_QUEUE_REGISTRY_CAPACITY; slot++) {
        if (!wait_service.registry[slot]) valid = 0;
    }
    while (created) {
        wait_queue_head_t* queue = &wait_test_capacity_queues[--created];

        wait_queue_unregister_locked(queue);
        kmemset(queue, 0, sizeof(*queue));
    }
    wait_service.stats.registry_peak = original_peak;
    wait_irq_restore(flags);
    return valid;
}

static void wait_test_cleanup(wait_queue_head_t* queue,
                              wait_queue_entry_t* entries,
                              uint32_t count) {
    uint32_t ignored;

    if (queue->initialized && queue->waiters) {
        wait_queue_wake_target(queue, WAIT_TARGET_ANY, WAIT_WAKE_ALL,
                               WAIT_REASON_CANCELLED, &ignored);
    }
    for (uint32_t index = 0U; index < count; index++) {
        entries[index].linked = 0U;
    }
    if (queue->initialized) wait_channel_reset(queue);
}

int wait_self_test(wait_self_test_result_t* out_result) {
    wait_queue_head_t queue;
    wait_queue_entry_t entries[3];
    wait_test_target_t targets[3];
    wait_reason_t reason = WAIT_REASON_NONE;
    uint32_t condition = 0U;
    uint32_t woken = 0U;
    uint8_t ready = 0U;
    wait_test_condition_t test_condition;
    int result = OK;

    if (!out_result) {
        LOG_ERROR("WAIT", "Destino nulo para autoteste");
        return ERR_NULL;
    }
    kmemset(out_result, 0, sizeof(*out_result));
    kmemset(&queue, 0, sizeof(queue));
    kmemset(entries, 0, sizeof(entries));
    kmemset(&test_condition, 0, sizeof(test_condition));
    wait_test_wake_count = 0U;

    out_result->channel_lifecycle =
        init_waitqueue_head(&queue, "wait-fixture") == OK;
    wait_test_count(out_result, out_result->channel_lifecycle);
    if (!out_result->channel_lifecycle) return ERR_STATE;

    out_result->condition_signal =
        wait_channel_get_condition(&queue, &condition) == OK &&
        wait_channel_signal(&queue) == OK &&
        queue.condition != condition;
    out_result->availability =
        wait_channel_set_available(&queue, 0U) == OK && !queue.available &&
        wait_channel_set_available(&queue, 1U) == OK && queue.available;

    for (uint32_t index = 0U; index < 3U; index++) {
        wait_test_prepare_entry(&entries[index], &targets[index],
            index == 1U ? WAIT_TARGET_THREAD : WAIT_TARGET_PROCESS,
            index + 1U);
    }
    condition = queue.condition;
    for (uint32_t index = 0U; index < 3U; index++) {
        if (wait_queue_block(&queue, &entries[index], condition,
                             WAIT_TIMEOUT_INFINITE, &reason) != OK) {
            result = ERR_STATE;
        }
    }
    out_result->fifo = result == OK && queue.waiters == 3U &&
        wake_up(&queue, &woken) == OK && woken == 1U &&
        wait_test_wake_order[0] == 1U;
    wait_test_count(out_result, out_result->fifo);
    out_result->wake_all = wake_up_all(&queue, &woken) == OK && woken == 2U &&
        wait_test_wake_order[1] == 2U && wait_test_wake_order[2] == 3U;
    wait_test_count(out_result, out_result->wake_all);
    out_result->process_thread = targets[0].woken && targets[1].woken &&
        targets[2].woken;
    wait_test_count(out_result, out_result->process_thread);
    wait_test_condition(&test_condition, &ready);
    test_condition.ready = 1U;
    wait_test_condition(&test_condition, &ready);
    out_result->condition_recheck = test_condition.calls == 2U && ready;
    wait_test_count(out_result, out_result->condition_recheck);

    wait_test_prepare_entry(&entries[0], &targets[0],
                            WAIT_TARGET_PROCESS, 4U);
    condition = queue.condition;
    wait_channel_signal(&queue);
    reason = WAIT_REASON_NONE;
    out_result->lost_wakeup =
        wait_queue_block(&queue, &entries[0], condition,
                         WAIT_TIMEOUT_INFINITE, &reason) == OK &&
        reason == WAIT_REASON_EVENT && !entries[0].linked;
    wait_test_count(out_result, out_result->lost_wakeup);

    condition = queue.condition;
    wait_queue_block(&queue, &entries[0], condition,
                     WAIT_TIMEOUT_INFINITE, &reason);
    {
        uint32_t flags = wait_irq_save();

        out_result->reset = !wait_queue_reset_locked(&queue);
        wait_irq_restore(flags);
    }
    wait_queue_remove(&entries[0], WAIT_REASON_TIMEOUT);
    out_result->reset = out_result->reset && !queue.waiters;
    wait_test_count(out_result, out_result->reset);
    out_result->reasons = targets[0].reason == WAIT_REASON_TIMEOUT;

    wait_test_prepare_entry(&entries[0], &targets[0],
                            WAIT_TARGET_PROCESS, 5U);
    condition = queue.condition;
    wait_queue_block(&queue, &entries[0], condition,
                     WAIT_TIMEOUT_INFINITE, &reason);
    wait_channel_set_available(&queue, 0U);
    out_result->availability = out_result->availability &&
        targets[0].reason == WAIT_REASON_DEVICE_UNAVAILABLE &&
        !entries[0].linked && !queue.waiters;
    wait_channel_set_available(&queue, 1U);
    wait_test_count(out_result, out_result->availability);

    wait_test_prepare_entry(&entries[0], &targets[0],
                            WAIT_TARGET_PROCESS, 6U);
    condition = queue.condition;
    wait_queue_block(&queue, &entries[0], condition,
                     WAIT_TIMEOUT_INFINITE, &reason);
    wait_queue_remove(&entries[0], WAIT_REASON_CANCELLED);
    out_result->reasons = out_result->reasons &&
        targets[0].reason == WAIT_REASON_CANCELLED;
    wait_test_count(out_result, out_result->reasons);

    out_result->accounting = queue.peak_waiters >= 3U;
    wait_test_count(out_result, out_result->accounting);
    out_result->limits = wait_test_registry_capacity() &&
        WAIT_QUEUE_REGISTRY_CAPACITY == 128U &&
        WAIT_MAX_TIMEOUT_TICKS < WAIT_TIMEOUT_INFINITE;
    wait_test_count(out_result, out_result->limits);
    {
        uint32_t flags = wait_irq_save();

        out_result->interrupt_context = !wait_interrupts_enabled();
        wait_irq_restore(flags);
    }
    out_result->interrupt_context = out_result->interrupt_context &&
        wait_interrupts_enabled();
    wait_test_count(out_result, out_result->interrupt_context);
    reason = WAIT_REASON_NONE;
    out_result->condition_signal = out_result->condition_signal &&
        wait_event_timeout(&queue, wait_test_condition, &test_condition,
                           WAIT_TIMEOUT_IMMEDIATE, &reason) == OK &&
        reason == WAIT_REASON_EVENT;
    wait_test_count(out_result, out_result->condition_signal);

    out_result->invariants = wait_validate_state() == OK;
    wait_test_count(out_result, out_result->invariants);
    wait_test_cleanup(&queue, entries, 3U);
    if (out_result->failed || result != OK) return ERR_STATE;
    return OK;
}

const char* wait_reason_name(wait_reason_t reason) {
    if (reason == WAIT_REASON_NONE) return "aguardando";
    if (reason == WAIT_REASON_EVENT) return "evento";
    if (reason == WAIT_REASON_TIMEOUT) return "timeout";
    if (reason == WAIT_REASON_CANCELLED) return "cancelado";
    if (reason == WAIT_REASON_DEVICE_UNAVAILABLE) return "indisponivel";
    if (reason == WAIT_REASON_SIGNAL) return "sinal";
    return "desconhecido";
}

const char* wait_wake_mode_name(wait_wake_mode_t mode) {
    if (mode == WAIT_WAKE_ONE) return "um";
    if (mode == WAIT_WAKE_ALL) return "todos";
    return "desconhecido";
}
