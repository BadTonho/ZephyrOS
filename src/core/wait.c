#include "core/wait.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/timer.h"

#define WAIT_TEST_EFLAGS_INTERRUPT_ENABLE (1U << 9U)

typedef struct {
    wait_stats_t stats;
} wait_service_t;

typedef struct {
    uint32_t observed_condition;
    uint32_t deadline_tick;
    uint8_t deadline_active;
    wait_reason_t reason;
    uint8_t active;
} wait_test_waiter_t;

static wait_service_t wait_service;

static uint32_t wait_irq_save(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static void wait_irq_restore(uint32_t flags) {
    if (flags & WAIT_TEST_EFLAGS_INTERRUPT_ENABLE) {
        asm volatile("sti" : : : "memory");
    }
}

static uint32_t wait_name_length(const char* text) {
    uint32_t length = 0U;

    if (!text) return 0U;
    while (length < WAIT_CHANNEL_OWNER_LENGTH && text[length]) length++;
    return length;
}

static void wait_copy_owner(char* destination, const char* source) {
    uint32_t index = 0U;

    while (index + 1U < WAIT_CHANNEL_OWNER_LENGTH && source[index]) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int wait_channel_valid(const wait_channel_t* channel) {
    return channel && channel->initialized;
}

static void wait_test_count(wait_self_test_result_t* result, int passed) {
    if (passed) result->passed++;
    else result->failed++;
}

static int wait_deadline_reached_at(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

static wait_reason_t wait_test_evaluate(const wait_channel_t* channel,
                                        const wait_test_waiter_t* waiter,
                                        uint32_t now) {
    if (!channel || !channel->initialized || !waiter || !waiter->active) {
        return WAIT_REASON_NONE;
    }
    if (!channel->available) return WAIT_REASON_DEVICE_UNAVAILABLE;
    if (channel->condition != waiter->observed_condition) {
        return WAIT_REASON_EVENT;
    }
    if (waiter->deadline_active &&
        wait_deadline_reached_at(now, waiter->deadline_tick)) {
        return WAIT_REASON_TIMEOUT;
    }
    return WAIT_REASON_NONE;
}

static uint32_t wait_test_cancel(wait_test_waiter_t* waiters,
                                 uint32_t count, wait_wake_mode_t mode) {
    uint32_t cancelled = 0U;

    if (!waiters) return 0U;
    for (uint32_t index = 0U; index < count; index++) {
        if (!waiters[index].active) continue;
        waiters[index].active = 0U;
        waiters[index].reason = WAIT_REASON_CANCELLED;
        cancelled++;
        if (mode == WAIT_WAKE_ONE) break;
    }
    return cancelled;
}

static int wait_test_run_waiters(wait_channel_t* channel,
                                 wait_test_waiter_t* waiters) {
    if (!channel || !waiters) {
        LOG_ERROR("WAIT", "Estruturas nulas no autoteste de waiters");
        return 0;
    }

    waiters[0].observed_condition = channel->condition;
    waiters[0].deadline_tick = WAIT_TIMEOUT_INFINITE;
    waiters[0].deadline_active = 0U;
    waiters[0].reason = WAIT_REASON_NONE;
    waiters[0].active = 1U;
    waiters[1] = waiters[0];
    if (wait_channel_signal(channel) != OK) return 0;

    waiters[0].reason = wait_test_evaluate(channel, &waiters[0], 0U);
    if (waiters[0].reason != WAIT_REASON_EVENT) return 0;
    waiters[0].active = 0U;
    waiters[1].active = 1U;
    waiters[1].observed_condition = channel->condition;
    waiters[1].deadline_tick = 10U;
    waiters[1].deadline_active = 1U;
    waiters[1].reason = wait_test_evaluate(channel, &waiters[1], 10U);
    if (waiters[1].reason != WAIT_REASON_TIMEOUT) return 0;

    waiters[0].active = 1U;
    waiters[0].reason = WAIT_REASON_NONE;
    waiters[0].observed_condition = channel->condition;
    if (wait_test_cancel(waiters, 2U, WAIT_WAKE_ONE) != 1U ||
        waiters[0].reason != WAIT_REASON_CANCELLED || !waiters[1].active) {
        return 0;
    }
    return wait_test_cancel(waiters, 2U, WAIT_WAKE_ALL) == 1U &&
           waiters[1].reason == WAIT_REASON_CANCELLED;
}

static int wait_test_run_unavailable(wait_channel_t* channel,
                                     wait_test_waiter_t* waiter) {
    if (!channel || !waiter) {
        LOG_ERROR("WAIT", "Estruturas nulas no autoteste de disponibilidade");
        return 0;
    }
    waiter->observed_condition = channel->condition;
    waiter->deadline_active = 0U;
    waiter->active = 1U;
    channel->available = 0U;
    if (wait_test_evaluate(channel, waiter, 0U) !=
        WAIT_REASON_DEVICE_UNAVAILABLE) {
        channel->available = 1U;
        LOG_ERROR("WAIT", "Autoteste nao detectou recurso ausente");
        return 0;
    }
    channel->available = 1U;
    return 1;
}

void wait_init(void) {
    LOG_INFO("WAIT", "Inicializando servico de espera");
    kmemset(&wait_service, 0, sizeof(wait_service));
    wait_service.stats.initialized = 1U;
    LOG_INFO("WAIT", "Servico de espera inicializado com sucesso");
}

int wait_channel_init(wait_channel_t* channel, const char* owner) {
    uint32_t length;

    if (!wait_service.stats.initialized) {
        LOG_ERROR("WAIT", "Canal criado antes da inicializacao");
        return ERR_STATE;
    }
    if (!channel || !owner) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Parametros nulos ao criar canal");
        return ERR_NULL;
    }
    if (channel->initialized) {
        wait_service.stats.invalid_operations++;
        LOG_WARN("WAIT", "Canal ja inicializado");
        return ERR_STATE;
    }
    length = wait_name_length(owner);
    if (!length) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Proprietario de canal vazio");
        return ERR_INVALID;
    }
    if (length >= WAIT_CHANNEL_OWNER_LENGTH) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Proprietario de canal excede o limite");
        return ERR_OVERFLOW;
    }

    kmemset(channel, 0, sizeof(*channel));
    wait_copy_owner(channel->owner, owner);
    channel->available = 1U;
    channel->initialized = 1U;
    wait_service.stats.channels_active++;
    return OK;
}

int wait_channel_reset(wait_channel_t* channel) {
    if (!wait_channel_valid(channel)) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Canal invalido ao destruir");
        return ERR_STATE;
    }
    if (channel->waiters) {
        wait_service.stats.invalid_operations++;
        LOG_WARN("WAIT", "Canal com waiters nao pode ser destruido");
        return ERR_STATE;
    }

    if (wait_service.stats.channels_active > 0U) {
        wait_service.stats.channels_active--;
    }
    kmemset(channel, 0, sizeof(*channel));
    return OK;
}

int wait_channel_signal(wait_channel_t* channel) {
    if (!wait_channel_valid(channel)) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Canal invalido ao sinalizar");
        return ERR_STATE;
    }
    channel->condition++;
    if (!channel->condition) channel->condition = 1U;
    return OK;
}

int wait_channel_get_condition(const wait_channel_t* channel,
                              uint32_t* out_condition) {
    if (!out_condition) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Destino nulo para condicao do canal");
        return ERR_NULL;
    }
    if (!wait_channel_valid(channel)) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Canal invalido ao consultar condicao");
        return ERR_STATE;
    }
    *out_condition = channel->condition;
    return OK;
}

int wait_channel_set_available(wait_channel_t* channel, uint8_t available) {
    if (!wait_channel_valid(channel)) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Canal invalido ao alterar disponibilidade");
        return ERR_STATE;
    }
    channel->available = available ? 1U : 0U;
    return OK;
}

int wait_channel_is_available(const wait_channel_t* channel,
                             uint8_t* out_available) {
    if (!out_available) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Destino nulo para disponibilidade do canal");
        return ERR_NULL;
    }
    if (!wait_channel_valid(channel)) {
        wait_service.stats.invalid_operations++;
        LOG_ERROR("WAIT", "Canal invalido ao consultar disponibilidade");
        return ERR_STATE;
    }
    *out_available = channel->available;
    return OK;
}

void wait_note_waiter(wait_channel_t* channel) {
    if (!wait_channel_valid(channel)) {
        LOG_ERROR("WAIT", "Canal invalido ao registrar waiter");
        return;
    }
    channel->waiters++;
    wait_service.stats.active_waiters++;
    wait_service.stats.waits_started++;
    if (wait_service.stats.active_waiters > wait_service.stats.peak_waiters) {
        wait_service.stats.peak_waiters = wait_service.stats.active_waiters;
    }
}

void wait_note_wake(wait_channel_t* channel, wait_reason_t reason) {
    if (!wait_channel_valid(channel)) {
        LOG_ERROR("WAIT", "Canal invalido ao registrar wake");
        return;
    }
    if (channel->waiters > 0U) channel->waiters--;
    if (wait_service.stats.active_waiters > 0U) {
        wait_service.stats.active_waiters--;
    }
    if (reason == WAIT_REASON_EVENT) wait_service.stats.event_wakes++;
    else if (reason == WAIT_REASON_TIMEOUT) wait_service.stats.timeout_wakes++;
    else if (reason == WAIT_REASON_CANCELLED) {
        wait_service.stats.cancellation_wakes++;
    } else if (reason == WAIT_REASON_DEVICE_UNAVAILABLE) {
        wait_service.stats.unavailable_wakes++;
    }
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
    if ((int32_t)(now - deadline_tick) >= 0) {
        *out_remaining = 0U;
    } else {
        *out_remaining = deadline_tick - now;
    }
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

int wait_self_test(wait_self_test_result_t* out_result) {
    wait_channel_t channel;
    wait_channel_t invalid_channel;
    wait_test_waiter_t waiters[2];
    uint32_t condition = 0U;
    uint32_t flags;
    wait_stats_t before;
    int test_result;
    int result;

    if (!out_result) {
        LOG_ERROR("WAIT", "Destino nulo para autoteste");
        return ERR_NULL;
    }
    kmemset(out_result, 0, sizeof(*out_result));
    flags = wait_irq_save();
    if (wait_get_stats(&before) != OK) {
        wait_irq_restore(flags);
        LOG_ERROR("WAIT", "Autoteste sem estatisticas do servico");
        return ERR_STATE;
    }
    kmemset(&channel, 0, sizeof(channel));
    kmemset(&invalid_channel, 0, sizeof(invalid_channel));

    out_result->channel_lifecycle =
        wait_channel_init(&channel, "WaitCheck") == OK &&
        wait_channel_init(&channel, "WaitCheckAgain") == ERR_STATE;
    wait_test_count(out_result, out_result->channel_lifecycle);

    result = wait_channel_get_condition(&channel, &condition);
    out_result->condition_signal = result == OK && condition == 0U;
    if (wait_channel_signal(&channel) != OK) out_result->condition_signal = 0U;
    if (wait_channel_get_condition(&channel, &condition) != OK ||
        condition != 1U) out_result->condition_signal = 0U;
    wait_test_count(out_result, out_result->condition_signal);

    wait_channel_set_available(&channel, 0U);
    out_result->availability = channel.available == 0U;
    wait_channel_set_available(&channel, 1U);
    out_result->availability = out_result->availability && channel.available;
    wait_test_count(out_result, out_result->availability);

    out_result->accounting = wait_test_run_waiters(&channel, waiters);
    wait_test_count(out_result, out_result->accounting);

    out_result->reasons = wait_reason_name(WAIT_REASON_EVENT) != 0 &&
                          wait_reason_name(WAIT_REASON_TIMEOUT) != 0 &&
                          wait_reason_name(WAIT_REASON_CANCELLED) != 0 &&
                          wait_reason_name(WAIT_REASON_DEVICE_UNAVAILABLE) != 0;
    out_result->reasons = out_result->reasons &&
                          wait_test_run_unavailable(&channel, &waiters[0]);
    wait_test_count(out_result, out_result->reasons);

    out_result->limits = wait_channel_init(&invalid_channel, "") ==
                         ERR_INVALID &&
                         wait_channel_init(&invalid_channel,
                                           "123456789012345678901234") ==
                             ERR_OVERFLOW &&
                         WAIT_TIMEOUT_IMMEDIATE == 0U &&
                         WAIT_TIMEOUT_INFINITE > WAIT_MAX_TIMEOUT_TICKS;
    wait_test_count(out_result, out_result->limits);

    waiters[0].active = 1U;
    waiters[0].reason = WAIT_REASON_NONE;
    channel.waiters = 1U;
    out_result->reset = wait_channel_reset(&channel) == ERR_STATE;
    channel.waiters = 0U;
    out_result->reset = out_result->reset &&
                        wait_channel_reset(&channel) == OK;
    wait_test_count(out_result, out_result->reset);

    out_result->invariants = before.initialized &&
                             wait_wake_mode_name(WAIT_WAKE_ONE) != 0 &&
                             wait_wake_mode_name(WAIT_WAKE_ALL) != 0 &&
                             wait_deadline_reached_at(0x00000005U,
                                                      0x00000004U) &&
                             wait_deadline_reached_at(0x00000004U,
                                                      0xFFFFFFFEU) &&
                             !wait_deadline_reached_at(0xFFFFFFFEU,
                                                       0x00000004U) &&
                             wait_test_evaluate(&channel, &waiters[0], 0U) ==
                                 WAIT_REASON_NONE;
    wait_test_count(out_result, out_result->invariants);
    test_result = out_result->failed ? ERR_STATE : OK;
    wait_service.stats = before;
    wait_irq_restore(flags);
    if (out_result->failed) {
        LOG_ERROR("WAIT", "Autoteste de esperas encontrou falhas");
    }
    return test_result;
}

const char* wait_reason_name(wait_reason_t reason) {
    switch (reason) {
        case WAIT_REASON_NONE: return "WAITING";
        case WAIT_REASON_EVENT: return "EVENT";
        case WAIT_REASON_TIMEOUT: return "TIMEOUT";
        case WAIT_REASON_CANCELLED: return "CANCELLED";
        case WAIT_REASON_DEVICE_UNAVAILABLE: return "DEVICE_UNAVAILABLE";
        default: return "INVALID";
    }
}

const char* wait_wake_mode_name(wait_wake_mode_t mode) {
    if (mode == WAIT_WAKE_ONE) return "ONE";
    if (mode == WAIT_WAKE_ALL) return "ALL";
    return "INVALID";
}
