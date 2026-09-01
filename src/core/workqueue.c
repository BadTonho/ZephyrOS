#include "core/workqueue.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/wait.h"
#include "process/process.h"

#define WORKQUEUE_EFLAGS_INTERRUPT (1U << 9U)
#define WORKQUEUE_SELF_TEST_CASE_COUNT 12U
#define WORKQUEUE_TEST_ROLLOVER_DEADLINE 0x10U

typedef struct {
    work_struct_t* registry[WORKQUEUE_CAPACITY];
    uint32_t generations[WORKQUEUE_CAPACITY];
    work_struct_t* ready_first[WORK_PRIORITY_COUNT];
    work_struct_t* ready_last[WORK_PRIORITY_COUNT];
    work_struct_t* delayed_first;
    work_struct_t* delayed_last;
    wait_queue_head_t wait_queue;
    wait_queue_head_t probe_wait_queue;
    work_struct_t probe_work;
    uint32_t probe_generation;
    uint32_t probe_pid;
    uint8_t probe_interrupts_enabled;
    uint8_t power_quiescing;
    workqueue_stats_t stats;
} workqueue_service_t;

typedef struct {
    workqueue_service_t* service;
    uint32_t observed_generation;
} workqueue_probe_wait_t;

typedef struct {
    uint32_t calls;
    uint32_t* order;
    uint32_t* order_count;
    uint32_t value;
    work_struct_t* rerun_work;
    workqueue_service_t* service;
    uint8_t request_rerun;
    uint8_t request_cancel;
    uint8_t interrupts_enabled;
} workqueue_test_context_t;

static workqueue_service_t workqueue_service;
static workqueue_service_t workqueue_test_service;
static work_struct_t workqueue_test_works[WORKQUEUE_CAPACITY];
static work_struct_t workqueue_test_extra;
static workqueue_test_context_t workqueue_test_contexts[WORKQUEUE_CAPACITY];
static workqueue_test_context_t workqueue_test_extra_context;
static uint32_t workqueue_test_order[WORKQUEUE_CAPACITY];
static uint32_t workqueue_test_order_count;
#if defined(ZEPHYROS_HOST_TEST)
static uint8_t workqueue_host_interrupts_enabled = 1U;
#endif

static int workqueue_internal_result(int result) {
    return result;
}

static uint32_t workqueue_irq_save(void) {
#if defined(ZEPHYROS_HOST_TEST)
    uint32_t flags = workqueue_host_interrupts_enabled ?
                     WORKQUEUE_EFLAGS_INTERRUPT : 0U;

    workqueue_host_interrupts_enabled = 0U;
    return flags;
#else
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
#endif
}

static void workqueue_irq_restore(uint32_t flags) {
#if defined(ZEPHYROS_HOST_TEST)
    workqueue_host_interrupts_enabled =
        (flags & WORKQUEUE_EFLAGS_INTERRUPT) ? 1U : 0U;
#else
    if (flags & WORKQUEUE_EFLAGS_INTERRUPT) {
        asm volatile("sti" : : : "memory");
    }
#endif
}

static uint8_t workqueue_interrupts_enabled(void) {
#if defined(ZEPHYROS_HOST_TEST)
    return workqueue_host_interrupts_enabled;
#else
    uint32_t flags;

    asm volatile("pushf\n\tpop %0" : "=r"(flags));
    return (uint8_t)((flags & WORKQUEUE_EFLAGS_INTERRUPT) != 0U);
#endif
}

static int workqueue_deadline_reached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

static int workqueue_owner_valid(const char* owner) {
    uint32_t length = 0U;

    if (!owner || !owner[0]) return 0;
    while (length < WORKQUEUE_OWNER_SIZE && owner[length]) length++;
    return length < WORKQUEUE_OWNER_SIZE;
}

static void workqueue_copy_owner(char* destination, const char* source) {
    uint32_t index = 0U;

    while (index + 1U < WORKQUEUE_OWNER_SIZE && source[index]) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static uint32_t workqueue_pending_count(const workqueue_service_t* service) {
    return service->stats.ready_high + service->stats.ready_normal +
           service->stats.delayed + service->stats.running;
}

static void workqueue_update_peak(workqueue_service_t* service) {
    uint32_t pending = workqueue_pending_count(service);

    if (pending > service->stats.peak_pending) {
        service->stats.peak_pending = pending;
    }
}

static void workqueue_service_clear(workqueue_service_t* service) {
    kmemset(service, 0, sizeof(*service));
    service->stats.capacity = WORKQUEUE_CAPACITY;
    service->stats.execution_context = WORK_CONTEXT_NONE;
    service->stats.last_error = OK;
}

static void workqueue_list_remove(workqueue_service_t* service,
                                  work_struct_t* work) {
    work_struct_t** first;
    work_struct_t** last;

    if (work->state == WORK_STATE_DELAYED) {
        first = &service->delayed_first;
        last = &service->delayed_last;
        if (service->stats.delayed) service->stats.delayed--;
    } else {
        uint32_t priority = (uint32_t)work->priority;

        first = &service->ready_first[priority];
        last = &service->ready_last[priority];
        if (priority == WORK_PRIORITY_HIGH && service->stats.ready_high) {
            service->stats.ready_high--;
        } else if (priority == WORK_PRIORITY_NORMAL &&
                   service->stats.ready_normal) {
            service->stats.ready_normal--;
        }
    }
    if (work->previous) work->previous->next = work->next;
    else *first = work->next;
    if (work->next) work->next->previous = work->previous;
    else *last = work->previous;
    work->previous = 0;
    work->next = 0;
}

static void workqueue_ready_append(workqueue_service_t* service,
                                   work_struct_t* work) {
    uint32_t priority = (uint32_t)work->priority;

    work->previous = service->ready_last[priority];
    work->next = 0;
    if (service->ready_last[priority]) {
        service->ready_last[priority]->next = work;
    } else {
        service->ready_first[priority] = work;
    }
    service->ready_last[priority] = work;
    work->state = WORK_STATE_READY;
    if (priority == WORK_PRIORITY_HIGH) service->stats.ready_high++;
    else service->stats.ready_normal++;
    workqueue_update_peak(service);
}

static void workqueue_delayed_insert(workqueue_service_t* service,
                                     work_struct_t* work,
                                     uint32_t now) {
    work_struct_t* current = service->delayed_first;
    uint32_t remaining = work->deadline_tick - now;

    while (current) {
        uint32_t current_remaining = current->deadline_tick - now;

        if (current_remaining > remaining) break;
        current = current->next;
    }
    if (!current) {
        work->previous = service->delayed_last;
        work->next = 0;
        if (service->delayed_last) service->delayed_last->next = work;
        else service->delayed_first = work;
        service->delayed_last = work;
    } else {
        work->previous = current->previous;
        work->next = current;
        if (current->previous) current->previous->next = work;
        else service->delayed_first = work;
        current->previous = work;
    }
    work->state = WORK_STATE_DELAYED;
    service->stats.delayed++;
    workqueue_update_peak(service);
}

static void workqueue_promote_due(workqueue_service_t* service,
                                  uint32_t now) {
    while (service->delayed_first &&
           workqueue_deadline_reached(
               now, service->delayed_first->deadline_tick)) {
        work_struct_t* work = service->delayed_first;

        workqueue_list_remove(service, work);
        workqueue_ready_append(service, work);
    }
}

static int workqueue_register_on(workqueue_service_t* service,
                                 work_struct_t* work, const char* owner,
                                 work_priority_t priority,
                                 work_func_t callback, void* context) {
    uint32_t flags;
    uint32_t slot;

    if (!service || !work || !callback) {
        return workqueue_internal_result(ERR_NULL);
    }
    if (!workqueue_owner_valid(owner) || priority >= WORK_PRIORITY_COUNT) {
        return workqueue_internal_result(ERR_INVALID);
    }
    flags = workqueue_irq_save();
    if (!service->stats.initialized) {
        workqueue_irq_restore(flags);
        return workqueue_internal_result(ERR_STATE);
    }
    if (service == &workqueue_service && service->power_quiescing) {
        workqueue_irq_restore(flags);
        return workqueue_internal_result(ERR_UNAVAILABLE);
    }
    for (slot = 0U; slot < WORKQUEUE_CAPACITY; slot++) {
        if (service->registry[slot] == work) {
            workqueue_irq_restore(flags);
            return workqueue_internal_result(ERR_STATE);
        }
    }
    for (slot = 0U; slot < WORKQUEUE_CAPACITY; slot++) {
        if (!service->registry[slot]) break;
    }
    if (slot == WORKQUEUE_CAPACITY) {
        service->stats.rejected++;
        service->stats.last_error = ERR_OVERFLOW;
        workqueue_irq_restore(flags);
        return workqueue_internal_result(ERR_OVERFLOW);
    }
    kmemset(work, 0, sizeof(*work));
    service->generations[slot]++;
    if (!service->generations[slot] ||
        service->generations[slot] > WORKQUEUE_ID_GENERATION_MAX) {
        service->generations[slot] = 1U;
    }
    work->id = (service->generations[slot] << WORKQUEUE_ID_SLOT_BITS) |
               (slot + 1U);
    work->registry_slot = (uint8_t)slot;
    work->callback = callback;
    work->context = context;
    work->priority = priority;
    work->state = WORK_STATE_IDLE;
    work->initialized = 1U;
    work->last_error = OK;
    workqueue_copy_owner(work->owner, owner);
    service->registry[slot] = work;
    service->stats.registered++;
    workqueue_irq_restore(flags);
    return OK;
}

static void workqueue_notify(workqueue_service_t* service) {
    uint32_t flags;
    uint32_t woken = 0U;

    if (service != &workqueue_service || !service->wait_queue.initialized) {
        return;
    }
    flags = workqueue_irq_save();
    service->stats.wakeups++;
    workqueue_irq_restore(flags);
    if (wait_channel_signal(&service->wait_queue) != OK ||
        wake_up(&service->wait_queue, &woken) != OK) {
        flags = workqueue_irq_save();
        service->stats.wake_errors++;
        service->stats.last_error = ERR_STATE;
        workqueue_irq_restore(flags);
    }
}

static int workqueue_schedule_on(workqueue_service_t* service,
                                 work_struct_t* work,
                                 uint32_t delay_ticks) {
    uint32_t flags;
    uint32_t now;
    uint32_t deadline;

    if (!service || !work) return workqueue_internal_result(ERR_NULL);
    if (!service->stats.initialized || !work->initialized ||
        work->registry_slot >= WORKQUEUE_CAPACITY ||
        service->registry[work->registry_slot] != work) {
        return workqueue_internal_result(ERR_STATE);
    }
    if (service == &workqueue_service && service->power_quiescing) {
        return workqueue_internal_result(ERR_UNAVAILABLE);
    }
    if (delay_ticks > WORKQUEUE_MAX_DELAY_TICKS) {
        return workqueue_internal_result(ERR_INVALID);
    }
    flags = workqueue_irq_save();
    now = timer_get_ticks();
    workqueue_promote_due(service, now);
    deadline = now + delay_ticks;
    if (work->state == WORK_STATE_READY) {
        work->coalesced++;
        service->stats.coalesced++;
        workqueue_irq_restore(flags);
        workqueue_notify(service);
        return OK;
    }
    if (work->state == WORK_STATE_DELAYED) {
        work->coalesced++;
        service->stats.coalesced++;
        if (!delay_ticks) {
            workqueue_list_remove(service, work);
            workqueue_ready_append(service, work);
        } else if ((int32_t)(deadline - work->deadline_tick) < 0) {
            workqueue_list_remove(service, work);
            work->deadline_tick = deadline;
            workqueue_delayed_insert(service, work, now);
        }
        workqueue_irq_restore(flags);
        workqueue_notify(service);
        return OK;
    }
    if (work->state == WORK_STATE_RUNNING) {
        work->coalesced++;
        service->stats.coalesced++;
        if (!work->rerun_requested || !delay_ticks) {
            work->rerun_requested = 1U;
            work->rerun_delayed = delay_ticks ? 1U : 0U;
            work->rerun_deadline_tick = deadline;
        } else if (work->rerun_delayed &&
                   (int32_t)(deadline - work->rerun_deadline_tick) < 0) {
            work->rerun_deadline_tick = deadline;
        }
        workqueue_irq_restore(flags);
        workqueue_notify(service);
        return OK;
    }
    work->cancel_requested = 0U;
    work->scheduled++;
    service->stats.scheduled++;
    if (delay_ticks) {
        work->deadline_tick = deadline;
        workqueue_delayed_insert(service, work, now);
    } else {
        workqueue_ready_append(service, work);
    }
    service->stats.last_error = OK;
    workqueue_irq_restore(flags);
    workqueue_notify(service);
    return OK;
}

static int workqueue_cancel_on(workqueue_service_t* service,
                               work_struct_t* work) {
    uint32_t flags;

    if (!service || !work) return workqueue_internal_result(ERR_NULL);
    flags = workqueue_irq_save();
    if (!service->stats.initialized || !work->initialized ||
        work->registry_slot >= WORKQUEUE_CAPACITY ||
        service->registry[work->registry_slot] != work) {
        workqueue_irq_restore(flags);
        return workqueue_internal_result(ERR_STATE);
    }
    if (work->state == WORK_STATE_READY ||
        work->state == WORK_STATE_DELAYED) {
        workqueue_list_remove(service, work);
        work->state = WORK_STATE_IDLE;
    } else if (work->state == WORK_STATE_RUNNING) {
        work->cancel_requested = 1U;
    }
    work->rerun_requested = 0U;
    work->rerun_delayed = 0U;
    work->cancellations++;
    service->stats.cancelled++;
    workqueue_irq_restore(flags);
    return OK;
}

static work_struct_t* workqueue_take_ready(workqueue_service_t* service,
                                           work_priority_t priority) {
    work_struct_t* work = service->ready_first[priority];

    if (!work) return 0;
    workqueue_list_remove(service, work);
    work->state = WORK_STATE_RUNNING;
    service->stats.running++;
    workqueue_update_peak(service);
    return work;
}

static void workqueue_finish(workqueue_service_t* service,
                             work_struct_t* work, int callback_result,
                             uint32_t elapsed) {
    uint32_t now = timer_get_ticks();

    work->executed++;
    work->callback_ticks += elapsed;
    if (elapsed > work->max_callback_ticks) {
        work->max_callback_ticks = elapsed;
    }
    work->last_error = callback_result;
    service->stats.executed++;
    service->stats.total_callback_ticks += elapsed;
    if (elapsed > service->stats.max_callback_ticks) {
        service->stats.max_callback_ticks = elapsed;
    }
    if (callback_result != OK) {
        service->stats.callback_errors++;
        service->stats.last_error = callback_result;
    }
    if (service->stats.running) service->stats.running--;
    work->state = WORK_STATE_IDLE;
    if (work->cancel_requested) {
        work->cancel_requested = 0U;
        work->rerun_requested = 0U;
        work->rerun_delayed = 0U;
        return;
    }
    if (work->rerun_requested) {
        uint8_t delayed = work->rerun_delayed;
        uint32_t deadline = work->rerun_deadline_tick;

        work->rerun_requested = 0U;
        work->rerun_delayed = 0U;
        work->reruns++;
        work->scheduled++;
        service->stats.reruns++;
        service->stats.scheduled++;
        if (delayed && !workqueue_deadline_reached(now, deadline)) {
            work->deadline_tick = deadline;
            workqueue_delayed_insert(service, work, now);
        } else {
            workqueue_ready_append(service, work);
        }
    }
}

static int workqueue_execute_one(workqueue_service_t* service,
                                 work_priority_t priority) {
    work_struct_t* work;
    uint32_t flags;
    uint32_t started;
    uint32_t elapsed;
    int result;

    flags = workqueue_irq_save();
    workqueue_promote_due(service, timer_get_ticks());
    work = workqueue_take_ready(service, priority);
    workqueue_irq_restore(flags);
    if (!work) return ERR_NOT_FOUND;
    if (!workqueue_interrupts_enabled()) {
        service->stats.context_errors++;
        service->stats.last_error = ERR_STATE;
        flags = workqueue_irq_save();
        workqueue_finish(service, work, ERR_STATE, 0U);
        workqueue_irq_restore(flags);
        return ERR_STATE;
    }
    started = timer_get_ticks();
    result = work->callback(work->context);
    elapsed = timer_get_ticks() - started;
    flags = workqueue_irq_save();
    workqueue_finish(service, work, result, elapsed);
    workqueue_irq_restore(flags);
    if (result != OK) {
        LOG_ERROR_CODE("KERNEL", result, "Callback de trabalho falhou");
    }
    return OK;
}

static int workqueue_dispatch_on(workqueue_service_t* service,
                                 uint32_t high_budget,
                                 uint32_t normal_budget,
                                 uint32_t* out_executed) {
    uint32_t executed = 0U;

    if (!service || !out_executed) {
        return workqueue_internal_result(ERR_NULL);
    }
    if (!service->stats.initialized) {
        return workqueue_internal_result(ERR_STATE);
    }
    while (high_budget) {
        int result = workqueue_execute_one(service, WORK_PRIORITY_HIGH);

        if (result == ERR_NOT_FOUND) break;
        if (result != OK) return result;
        high_budget--;
        executed++;
    }
    while (normal_budget) {
        int result = workqueue_execute_one(service, WORK_PRIORITY_NORMAL);

        if (result == ERR_NOT_FOUND) break;
        if (result != OK) return result;
        normal_budget--;
        executed++;
    }
    *out_executed = executed;
    return OK;
}

static int workqueue_validate_on(const workqueue_service_t* service) {
    uint32_t registered = 0U;
    uint32_t ready[WORK_PRIORITY_COUNT] = {0U, 0U};
    uint32_t delayed = 0U;
    uint32_t running = 0U;

    if (!service || !service->stats.initialized ||
        service->stats.capacity != WORKQUEUE_CAPACITY ||
        service->stats.registered > WORKQUEUE_CAPACITY ||
        service->stats.running > 1U) {
        return workqueue_internal_result(ERR_STATE);
    }
    for (uint32_t priority = 0U;
         priority < WORK_PRIORITY_COUNT; priority++) {
        work_struct_t* previous = 0;
        work_struct_t* current = service->ready_first[priority];
        uint32_t count = 0U;

        while (current) {
            if (current->previous != previous ||
                current->state != WORK_STATE_READY ||
                current->priority != (work_priority_t)priority ||
                !current->initialized ||
                current->registry_slot >= WORKQUEUE_CAPACITY ||
                service->registry[current->registry_slot] != current ||
                ++count > WORKQUEUE_CAPACITY) {
                return workqueue_internal_result(ERR_STATE);
            }
            previous = current;
            current = current->next;
        }
        if (previous != service->ready_last[priority]) {
            return workqueue_internal_result(ERR_STATE);
        }
        if (priority == WORK_PRIORITY_HIGH &&
            count != service->stats.ready_high) {
            return workqueue_internal_result(ERR_STATE);
        }
        if (priority == WORK_PRIORITY_NORMAL &&
            count != service->stats.ready_normal) {
            return workqueue_internal_result(ERR_STATE);
        }
    }
    {
        work_struct_t* previous = 0;
        work_struct_t* current = service->delayed_first;
        uint32_t count = 0U;
        uint32_t last_deadline = 0U;

        while (current) {
            if (current->previous != previous ||
                current->state != WORK_STATE_DELAYED ||
                !current->initialized ||
                current->registry_slot >= WORKQUEUE_CAPACITY ||
                service->registry[current->registry_slot] != current ||
                (count && (int32_t)(current->deadline_tick -
                                    last_deadline) < 0) ||
                ++count > WORKQUEUE_CAPACITY) {
                return workqueue_internal_result(ERR_STATE);
            }
            last_deadline = current->deadline_tick;
            previous = current;
            current = current->next;
        }
        if (previous != service->delayed_last ||
            count != service->stats.delayed) {
            return workqueue_internal_result(ERR_STATE);
        }
    }
    for (uint32_t slot = 0U; slot < WORKQUEUE_CAPACITY; slot++) {
        work_struct_t* work = service->registry[slot];

        if (!work) continue;
        registered++;
        if (!work->initialized || work->registry_slot != slot ||
            !work->callback || !workqueue_owner_valid(work->owner) ||
            work->priority >= WORK_PRIORITY_COUNT ||
            work->state > WORK_STATE_RUNNING ||
            (work->id & WORKQUEUE_ID_SLOT_MASK) != slot + 1U ||
            !(work->id >> WORKQUEUE_ID_SLOT_BITS) ||
            service->generations[slot] !=
                (work->id >> WORKQUEUE_ID_SLOT_BITS)) {
            return workqueue_internal_result(ERR_STATE);
        }
        if (work->state == WORK_STATE_READY) ready[work->priority]++;
        else if (work->state == WORK_STATE_DELAYED) delayed++;
        else if (work->state == WORK_STATE_RUNNING) running++;
    }
    if (registered != service->stats.registered ||
        ready[WORK_PRIORITY_HIGH] != service->stats.ready_high ||
        ready[WORK_PRIORITY_NORMAL] != service->stats.ready_normal ||
        delayed != service->stats.delayed ||
        running != service->stats.running) {
        return workqueue_internal_result(ERR_STATE);
    }
    return OK;
}

static int workqueue_wait_condition(void* context, uint8_t* out_ready) {
    workqueue_service_t* service = (workqueue_service_t*)context;

    if (!service || !out_ready) {
        return workqueue_internal_result(ERR_NULL);
    }
    *out_ready = service->ready_first[WORK_PRIORITY_HIGH] ||
                 service->ready_first[WORK_PRIORITY_NORMAL] ||
                 (service->delayed_first &&
                  workqueue_deadline_reached(
                      timer_get_ticks(),
                      service->delayed_first->deadline_tick));
    return OK;
}

static uint32_t workqueue_next_timeout(workqueue_service_t* service) {
    uint32_t flags = workqueue_irq_save();
    uint32_t timeout = WAIT_TIMEOUT_INFINITE;

    if (service->ready_first[WORK_PRIORITY_HIGH] ||
        service->ready_first[WORK_PRIORITY_NORMAL]) {
        timeout = 0U;
    } else if (service->delayed_first) {
        uint32_t now = timer_get_ticks();

        timeout = workqueue_deadline_reached(
                      now, service->delayed_first->deadline_tick) ?
                  0U : service->delayed_first->deadline_tick - now;
    }
    workqueue_irq_restore(flags);
    return timeout;
}

static int workqueue_test_callback(void* context) {
    workqueue_test_context_t* test = (workqueue_test_context_t*)context;

    if (!test) return workqueue_internal_result(ERR_NULL);
    test->calls++;
    test->interrupts_enabled = workqueue_interrupts_enabled();
    if (test->order && test->order_count &&
        *test->order_count < WORKQUEUE_CAPACITY) {
        test->order[(*test->order_count)++] = test->value;
    }
    if (test->request_rerun && test->calls == 1U) {
        (void)workqueue_schedule_on(test->service,
                                   test->rerun_work, 0U);
    }
    if (test->request_cancel && test->calls == 1U) {
        (void)workqueue_cancel_on(test->service, test->rerun_work);
    }
    return OK;
}

static int workqueue_probe_condition(void* context, uint8_t* out_ready) {
    workqueue_probe_wait_t* probe = (workqueue_probe_wait_t*)context;

    if (!probe || !probe->service || !out_ready) {
        return workqueue_internal_result(ERR_NULL);
    }
    *out_ready = probe->service->probe_generation !=
                 probe->observed_generation;
    return OK;
}

static int workqueue_probe_callback(void* context) {
    workqueue_service_t* service = (workqueue_service_t*)context;
    uint32_t woken = 0U;
    int result;

    if (!service) return workqueue_internal_result(ERR_NULL);
    service->probe_pid = process_get_current_pid();
    service->probe_interrupts_enabled = workqueue_interrupts_enabled();
    service->probe_generation++;
    if (!service->probe_generation) service->probe_generation = 1U;
    result = wait_channel_signal(&service->probe_wait_queue);
    if (result != OK) return result;
    return wake_up_all(&service->probe_wait_queue, &woken);
}

int workqueue_init(void) {
    int result;

    LOG_INFO("KERNEL", "Inicializando fila de trabalhos do kernel");
    if (workqueue_service.stats.initialized) {
        LOG_WARN("KERNEL", "Fila de trabalhos ja estava inicializada");
        LOG_INFO("KERNEL", "Fila de trabalhos do kernel inicializada");
        return OK;
    }
    workqueue_service_clear(&workqueue_service);
    result = init_waitqueue_head(&workqueue_service.wait_queue, "KWORKER");
    if (result != OK) {
        LOG_ERROR_CODE("KERNEL", result,
                       "Falha ao inicializar espera da kworker");
        return result;
    }
    workqueue_service.stats.initialized = 1U;
    result = init_waitqueue_head(&workqueue_service.probe_wait_queue,
                                 "WORKQ PROBE");
    if (result != OK) {
        workqueue_service.stats.initialized = 0U;
        (void)wait_channel_reset(&workqueue_service.wait_queue);
        LOG_ERROR_CODE("KERNEL", result,
                       "Falha ao inicializar espera de prova da kworker");
        return result;
    }
    result = workqueue_register_on(&workqueue_service,
                                   &workqueue_service.probe_work,
                                   "Workq probe", WORK_PRIORITY_HIGH,
                                   workqueue_probe_callback,
                                   &workqueue_service);
    if (result != OK) {
        workqueue_service.stats.initialized = 0U;
        (void)wait_channel_reset(&workqueue_service.probe_wait_queue);
        (void)wait_channel_reset(&workqueue_service.wait_queue);
        LOG_ERROR_CODE("KERNEL", result,
                       "Falha ao registrar prova da kworker");
        return result;
    }
    LOG_INFO("KERNEL", "Fila de trabalhos do kernel inicializada");
    return OK;
}

int workqueue_power_set_quiescing(uint8_t active) {
    uint32_t flags;

    if (!workqueue_service.stats.initialized) {
        LOG_ERROR("KERNEL", "Gate da workqueue antes da inicializacao");
        return ERR_STATE;
    }
    flags = workqueue_irq_save();
    workqueue_service.power_quiescing = active ? 1U : 0U;
    workqueue_irq_restore(flags);
    return OK;
}

int workqueue_power_quiesce_until(uint32_t deadline_tick) {
    uint32_t flags;

    if (!workqueue_service.stats.initialized) {
        LOG_ERROR("KERNEL", "Quiescencia da workqueue antes da inicializacao");
        return ERR_STATE;
    }
    if ((int32_t)(timer_get_ticks() - deadline_tick) >= 0) {
        LOG_WARN("KERNEL", "Quiescencia da workqueue fora do prazo");
        return ERR_TIMEOUT;
    }
    flags = workqueue_irq_save();
    workqueue_service.power_quiescing = 1U;
    if (workqueue_service.stats.running) {
        workqueue_irq_restore(flags);
        LOG_ERROR("KERNEL", "Workqueue possui callback em execucao");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < WORKQUEUE_CAPACITY; index++) {
        work_struct_t* work = workqueue_service.registry[index];

        if (!work) continue;
        if (work->state == WORK_STATE_READY ||
            work->state == WORK_STATE_DELAYED) {
            workqueue_list_remove(&workqueue_service, work);
            work->state = WORK_STATE_IDLE;
        }
        work->rerun_requested = 0U;
        work->rerun_delayed = 0U;
        work->cancel_requested = 0U;
    }
    workqueue_irq_restore(flags);
    return OK;
}

int work_init(work_struct_t* work, const char* owner,
              work_priority_t priority,
              work_func_t callback, void* context) {
    int result = workqueue_register_on(&workqueue_service, work, owner,
                                       priority, callback, context);

    if (result != OK) {
        LOG_ERROR_CODE("KERNEL", result, "Falha ao registrar trabalho");
    }
    return result;
}

int work_destroy(work_struct_t* work) {
    uint32_t flags;

    if (!work) {
        LOG_ERROR("KERNEL", "Trabalho nulo na destruicao");
        return ERR_NULL;
    }
    flags = workqueue_irq_save();
    if (!workqueue_service.stats.initialized || !work->initialized ||
        work->registry_slot >= WORKQUEUE_CAPACITY ||
        workqueue_service.registry[work->registry_slot] != work ||
        work->state != WORK_STATE_IDLE) {
        workqueue_irq_restore(flags);
        LOG_ERROR("KERNEL", "Trabalho ativo ou invalido na destruicao");
        return ERR_STATE;
    }
    workqueue_service.registry[work->registry_slot] = 0;
    if (workqueue_service.stats.registered) {
        workqueue_service.stats.registered--;
    }
    kmemset(work, 0, sizeof(*work));
    workqueue_irq_restore(flags);
    return OK;
}

int schedule_work(work_struct_t* work) {
    int result = workqueue_schedule_on(&workqueue_service, work, 0U);

    if (result != OK) {
        uint32_t flags = workqueue_irq_save();

        workqueue_service.stats.rejected++;
        workqueue_service.stats.last_error = result;
        workqueue_irq_restore(flags);
        LOG_ERROR_CODE("KERNEL", result, "Falha ao agendar trabalho");
    }
    return result;
}

int schedule_delayed_work(work_struct_t* work, uint32_t delay_ticks) {
    int result = workqueue_schedule_on(&workqueue_service, work, delay_ticks);

    if (result != OK) {
        uint32_t flags = workqueue_irq_save();

        workqueue_service.stats.rejected++;
        workqueue_service.stats.last_error = result;
        workqueue_irq_restore(flags);
        LOG_ERROR_CODE("KERNEL", result,
                       "Falha ao agendar trabalho atrasado");
    }
    return result;
}

int cancel_work(work_struct_t* work) {
    int result;

    if (!work) {
        LOG_ERROR("KERNEL", "Trabalho nulo no cancelamento");
        return ERR_NULL;
    }
    result = workqueue_cancel_on(&workqueue_service, work);
    if (result != OK) {
        LOG_ERROR("KERNEL", "Trabalho invalido no cancelamento");
        return result;
    }
    return OK;
}

int workqueue_dispatch(uint32_t high_budget, uint32_t normal_budget,
                       uint32_t* out_executed) {
    uint32_t flags;
    uint32_t current_pid;
    uint8_t invalid_context = 0U;
    int result;

    if (!out_executed) {
        LOG_ERROR("KERNEL", "Destino nulo no despacho de trabalhos");
        return ERR_NULL;
    }
    if (!workqueue_service.stats.initialized) {
        LOG_ERROR("KERNEL", "Despacho antes da inicializacao da workqueue");
        return ERR_STATE;
    }
    if (workqueue_service.power_quiescing) {
        *out_executed = 0U;
        return OK;
    }
    current_pid = process_get_current_pid();
    flags = workqueue_irq_save();
    if (workqueue_service.stats.worker_pid &&
        current_pid == workqueue_service.stats.worker_pid) {
        workqueue_service.stats.execution_context = WORK_CONTEXT_KWORKER;
        workqueue_service.stats.worker_active = 1U;
        workqueue_service.stats.fallback_active = 0U;
    } else if (!workqueue_service.stats.fallback_active) {
        workqueue_service.stats.context_errors++;
        workqueue_service.stats.last_error = ERR_STATE;
        invalid_context = 1U;
    } else {
        workqueue_service.stats.execution_context =
            WORK_CONTEXT_SYSTEM_FALLBACK;
    }
    workqueue_irq_restore(flags);
    if (invalid_context) {
        LOG_ERROR("KERNEL", "Despacho fora da kworker ou fallback");
        return ERR_STATE;
    }
    result = workqueue_dispatch_on(&workqueue_service, high_budget,
                                   normal_budget, out_executed);
    if (result != OK) {
        LOG_ERROR_CODE("KERNEL", result, "Despacho de trabalhos falhou");
    }
    return result;
}

int workqueue_bind_worker(uint32_t pid) {
    uint32_t flags;

    if (!workqueue_service.stats.initialized) {
        LOG_ERROR("KERNEL", "Vinculo da kworker antes da inicializacao");
        return ERR_STATE;
    }
    if (!pid) {
        LOG_ERROR("KERNEL", "PID invalido ao vincular kworker");
        return ERR_INVALID;
    }
    if (!process_get_by_pid(pid)) {
        LOG_ERROR("KERNEL", "Processo da kworker nao encontrado");
        return ERR_NOT_FOUND;
    }
    flags = workqueue_irq_save();
    workqueue_service.stats.worker_pid = pid;
    workqueue_service.stats.worker_bound = 1U;
    workqueue_service.stats.fallback_active = 0U;
    workqueue_irq_restore(flags);
    return OK;
}

int workqueue_set_fallback(uint8_t active) {
    uint32_t flags;

    if (!workqueue_service.stats.initialized) {
        LOG_ERROR("KERNEL", "Fallback antes da inicializacao");
        return ERR_STATE;
    }
    flags = workqueue_irq_save();
    workqueue_service.stats.fallback_active = active ? 1U : 0U;
    workqueue_irq_restore(flags);
    return OK;
}

int workqueue_needs_fallback(uint8_t* out_required) {
    process_t* worker;
    uint32_t flags;

    if (!out_required) {
        LOG_ERROR("KERNEL", "Destino nulo na consulta de fallback");
        return ERR_NULL;
    }
    if (!workqueue_service.stats.initialized) {
        LOG_ERROR("KERNEL", "Consulta de fallback antes da inicializacao");
        return ERR_STATE;
    }
    if (!workqueue_service.stats.worker_bound) {
        *out_required = 1U;
        return OK;
    }
    worker = process_get_by_pid(workqueue_service.stats.worker_pid);
    *out_required = !worker || worker->state == PROCESS_STATE_UNUSED ||
                    worker->state == PROCESS_STATE_ZOMBIE;
    if (*out_required) {
        flags = workqueue_irq_save();
        workqueue_service.stats.worker_active = 0U;
        workqueue_irq_restore(flags);
    }
    return OK;
}

void workqueue_worker_main(void) {
    uint32_t flags = workqueue_irq_save();

    workqueue_service.stats.worker_active = 1U;
    workqueue_service.stats.execution_context = WORK_CONTEXT_KWORKER;
    workqueue_irq_restore(flags);
    while (1) {
        uint32_t executed = 0U;
        uint32_t timeout;
        wait_reason_t reason = WAIT_REASON_NONE;

        if (workqueue_dispatch(WORKQUEUE_HIGH_BUDGET,
                               WORKQUEUE_NORMAL_BUDGET,
                               &executed) != OK) {
            process_yield();
            continue;
        }
        if (executed) {
            process_yield();
            continue;
        }
        timeout = workqueue_next_timeout(&workqueue_service);
        flags = workqueue_irq_save();
        workqueue_service.stats.sleeps++;
        workqueue_irq_restore(flags);
        if (wait_event_timeout(&workqueue_service.wait_queue,
                               workqueue_wait_condition,
                               &workqueue_service, timeout,
                               &reason) != OK) {
            flags = workqueue_irq_save();
            workqueue_service.stats.last_error = ERR_STATE;
            workqueue_irq_restore(flags);
            LOG_ERROR("KERNEL", "Espera da kworker falhou");
            process_yield();
        }
    }
}

int workqueue_copy_info(work_info_t* output,
                        uint32_t max_entries, uint32_t* out_count) {
    uint32_t flags;
    uint32_t count = 0U;
    uint32_t now;

    if (!out_count || (max_entries && !output)) {
        LOG_ERROR("KERNEL", "Destino nulo no snapshot de trabalhos");
        return ERR_NULL;
    }
    if (!workqueue_service.stats.initialized) {
        LOG_ERROR("KERNEL", "Snapshot antes da inicializacao da workqueue");
        return ERR_STATE;
    }
    flags = workqueue_irq_save();
    now = timer_get_ticks();
    for (uint32_t slot = 0U;
         slot < WORKQUEUE_CAPACITY && count < max_entries; slot++) {
        work_struct_t* work = workqueue_service.registry[slot];
        work_info_t* info;

        if (!work) continue;
        info = &output[count++];
        kmemset(info, 0, sizeof(*info));
        info->id = work->id;
        info->generation = work->id >> WORKQUEUE_ID_SLOT_BITS;
        workqueue_copy_owner(info->owner, work->owner);
        info->priority = work->priority;
        info->state = work->state;
        info->deadline_tick = work->deadline_tick;
        if (work->state == WORK_STATE_DELAYED &&
            !workqueue_deadline_reached(now, work->deadline_tick)) {
            info->remaining_ticks = work->deadline_tick - now;
        }
        info->scheduled = work->scheduled;
        info->executed = work->executed;
        info->coalesced = work->coalesced;
        info->reruns = work->reruns;
        info->cancellations = work->cancellations;
        info->callback_ticks = work->callback_ticks;
        info->max_callback_ticks = work->max_callback_ticks;
        info->last_error = work->last_error;
    }
    workqueue_irq_restore(flags);
    *out_count = count;
    return OK;
}

int workqueue_get_stats(workqueue_stats_t* out_stats) {
    uint32_t flags;

    if (!out_stats) {
        LOG_ERROR("KERNEL", "Destino nulo no status da workqueue");
        return ERR_NULL;
    }
    if (!workqueue_service.stats.initialized) {
        LOG_ERROR("KERNEL", "Status antes da inicializacao da workqueue");
        return ERR_STATE;
    }
    flags = workqueue_irq_save();
    *out_stats = workqueue_service.stats;
    workqueue_irq_restore(flags);
    return OK;
}

int workqueue_validate_state(void) {
    uint32_t flags = workqueue_irq_save();
    int result = workqueue_validate_on(&workqueue_service);

    if (result != OK) {
        workqueue_service.stats.invariant_errors++;
        workqueue_service.stats.last_error = result;
    }
    workqueue_irq_restore(flags);
    if (result != OK) {
        LOG_ERROR("KERNEL", "Estado da workqueue inconsistente");
    }
    return result;
}

int workqueue_probe_worker(uint32_t timeout_ticks) {
    workqueue_probe_wait_t probe;
    wait_reason_t reason = WAIT_REASON_NONE;
    uint32_t flags;
    int result;

    if (!workqueue_service.stats.initialized ||
        !workqueue_service.stats.worker_bound ||
        timeout_ticks == WAIT_TIMEOUT_IMMEDIATE ||
        timeout_ticks > WAIT_MAX_TIMEOUT_TICKS) {
        LOG_ERROR("KERNEL", "Parametros invalidos na prova da kworker");
        return ERR_INVALID;
    }
    if (process_get_current_pid() == workqueue_service.stats.worker_pid) {
        LOG_ERROR("KERNEL", "Kworker nao pode aguardar a propria prova");
        return ERR_STATE;
    }
    flags = workqueue_irq_save();
    probe.service = &workqueue_service;
    probe.observed_generation = workqueue_service.probe_generation;
    workqueue_irq_restore(flags);
    result = schedule_work(&workqueue_service.probe_work);
    if (result != OK) return result;
    result = wait_event_timeout(&workqueue_service.probe_wait_queue,
                                workqueue_probe_condition, &probe,
                                timeout_ticks, &reason);
    if (result != OK) {
        (void)cancel_work(&workqueue_service.probe_work);
        LOG_ERROR_CODE("KERNEL", result, "Espera da prova da kworker falhou");
        return result;
    }
    if (reason != WAIT_REASON_EVENT) {
        (void)cancel_work(&workqueue_service.probe_work);
    }
    if (reason != WAIT_REASON_EVENT ||
        workqueue_service.probe_pid != workqueue_service.stats.worker_pid ||
        !workqueue_service.probe_interrupts_enabled) {
        LOG_ERROR("KERNEL", "Prova de contexto da kworker falhou");
        return ERR_STATE;
    }
    return OK;
}

static int workqueue_test_prepare(workqueue_self_test_result_t* out_result) {
    workqueue_service_clear(&workqueue_test_service);
    workqueue_test_service.stats.initialized = 1U;
    workqueue_test_order_count = 0U;
    for (uint32_t index = 0U; index < WORKQUEUE_CAPACITY; index++) {
        workqueue_test_contexts[index].calls = 0U;
        workqueue_test_contexts[index].order = workqueue_test_order;
        workqueue_test_contexts[index].order_count =
            &workqueue_test_order_count;
        workqueue_test_contexts[index].value = index;
        workqueue_test_contexts[index].rerun_work =
            &workqueue_test_works[index];
        workqueue_test_contexts[index].service = &workqueue_test_service;
        workqueue_test_contexts[index].request_rerun = 0U;
        workqueue_test_contexts[index].request_cancel = 0U;
        workqueue_test_contexts[index].interrupts_enabled = 0U;
        kmemset(&workqueue_test_works[index], 0,
                sizeof(workqueue_test_works[index]));
        if (workqueue_register_on(&workqueue_test_service,
                                  &workqueue_test_works[index], "WorkCheck",
                                  index == 1U ? WORK_PRIORITY_HIGH :
                                                WORK_PRIORITY_NORMAL,
                                  workqueue_test_callback,
                                  &workqueue_test_contexts[index]) != OK) {
            return workqueue_internal_result(ERR_STATE);
        }
    }
    kmemset(&workqueue_test_extra, 0, sizeof(workqueue_test_extra));
    kmemset(&workqueue_test_extra_context, 0,
            sizeof(workqueue_test_extra_context));
    out_result->capacity =
        workqueue_register_on(&workqueue_test_service,
                              &workqueue_test_extra, "Overflow",
                              WORK_PRIORITY_NORMAL,
                              workqueue_test_callback,
                              &workqueue_test_extra_context) == ERR_OVERFLOW &&
        workqueue_test_service.stats.registered == WORKQUEUE_CAPACITY;
    return OK;
}

int workqueue_self_test(workqueue_self_test_result_t* out_result) {
    uint32_t executed = 0U;
    uint32_t first_deadline;
    uint8_t promoted;

    if (!out_result) {
        LOG_ERROR("KERNEL", "Destino nulo no autoteste da workqueue");
        return ERR_NULL;
    }
    kmemset(out_result, 0, sizeof(*out_result));
    if (workqueue_test_prepare(out_result) != OK) {
        LOG_ERROR("KERNEL", "Fixture da workqueue nao foi preparada");
        return ERR_STATE;
    }
    out_result->lifecycle =
        workqueue_schedule_on(&workqueue_test_service,
                              &workqueue_test_works[0], 0U) == OK &&
        workqueue_dispatch_on(&workqueue_test_service, 0U, 1U,
                              &executed) == OK && executed == 1U &&
        workqueue_test_contexts[0].calls == 1U;
    workqueue_test_order_count = 0U;
    (void)workqueue_schedule_on(&workqueue_test_service,
                                &workqueue_test_works[2], 0U);
    (void)workqueue_schedule_on(&workqueue_test_service,
                                &workqueue_test_works[3], 0U);
    executed = 0U;
    (void)workqueue_dispatch_on(&workqueue_test_service, 0U, 2U, &executed);
    out_result->fifo = executed == 2U && workqueue_test_order_count == 2U &&
                       workqueue_test_order[0] == 2U &&
                       workqueue_test_order[1] == 3U;
    (void)workqueue_schedule_on(&workqueue_test_service,
                                &workqueue_test_works[4], 0U);
    (void)workqueue_schedule_on(&workqueue_test_service,
                                &workqueue_test_works[1], 0U);
    workqueue_test_order_count = 0U;
    executed = 0U;
    (void)workqueue_dispatch_on(&workqueue_test_service, 1U, 1U, &executed);
    out_result->priority = executed == 2U &&
                           workqueue_test_order_count == 2U &&
                           workqueue_test_order[0] == 1U &&
                           workqueue_test_order[1] == 4U;
    (void)workqueue_schedule_on(&workqueue_test_service,
                                &workqueue_test_works[5], 5U);
    out_result->delayed = workqueue_test_works[5].state ==
                          WORK_STATE_DELAYED;
    promoted = workqueue_schedule_on(&workqueue_test_service,
                                     &workqueue_test_works[5], 0U) == OK &&
               workqueue_test_works[5].state == WORK_STATE_READY;
    executed = 0U;
    (void)workqueue_dispatch_on(&workqueue_test_service, 0U, 1U, &executed);
    out_result->promotion = promoted && executed == 1U &&
                            workqueue_test_contexts[5].calls == 1U;
    (void)workqueue_schedule_on(&workqueue_test_service,
                                &workqueue_test_works[11], 10U);
    first_deadline = workqueue_test_works[11].deadline_tick;
    (void)workqueue_schedule_on(&workqueue_test_service,
                                &workqueue_test_works[11], 5U);
    out_result->delayed = out_result->delayed &&
        workqueue_test_works[11].state == WORK_STATE_DELAYED &&
        (int32_t)(workqueue_test_works[11].deadline_tick -
                  first_deadline) < 0;
    (void)workqueue_cancel_on(&workqueue_test_service,
                              &workqueue_test_works[11]);
    workqueue_test_works[6].deadline_tick =
        WORKQUEUE_TEST_ROLLOVER_DEADLINE;
    out_result->rollover =
        !workqueue_deadline_reached(
            WORKQUEUE_TEST_ROLLOVER_DEADLINE - 1U,
            WORKQUEUE_TEST_ROLLOVER_DEADLINE) &&
        workqueue_deadline_reached(WORKQUEUE_TEST_ROLLOVER_DEADLINE,
                                   WORKQUEUE_TEST_ROLLOVER_DEADLINE);
    (void)workqueue_schedule_on(&workqueue_test_service,
                                &workqueue_test_works[7], 0U);
    (void)workqueue_schedule_on(&workqueue_test_service,
                                &workqueue_test_works[7], 0U);
    out_result->coalescing = workqueue_test_works[7].coalesced == 1U;
    executed = 0U;
    (void)workqueue_dispatch_on(&workqueue_test_service, 0U, 1U, &executed);
    workqueue_test_contexts[8].request_rerun = 1U;
    (void)workqueue_schedule_on(&workqueue_test_service,
                                &workqueue_test_works[8], 0U);
    executed = 0U;
    (void)workqueue_dispatch_on(&workqueue_test_service, 0U, 2U, &executed);
    out_result->rerun = executed == 2U &&
                        workqueue_test_contexts[8].calls == 2U;
    (void)workqueue_schedule_on(&workqueue_test_service,
                                &workqueue_test_works[9], 0U);
    out_result->cancellation =
        workqueue_cancel_on(&workqueue_test_service,
                            &workqueue_test_works[9]) == OK &&
        workqueue_test_works[9].state == WORK_STATE_IDLE &&
        workqueue_test_works[9].cancellations == 1U;
    workqueue_test_contexts[10].request_rerun = 1U;
    workqueue_test_contexts[10].request_cancel = 1U;
    (void)workqueue_schedule_on(&workqueue_test_service,
                                &workqueue_test_works[10], 0U);
    executed = 0U;
    (void)workqueue_dispatch_on(&workqueue_test_service, 0U, 2U, &executed);
    out_result->cancellation = out_result->cancellation && executed == 1U &&
        workqueue_test_works[10].state == WORK_STATE_IDLE &&
        workqueue_test_works[10].cancellations == 1U &&
        workqueue_test_contexts[10].calls == 1U;
    out_result->interrupt_context =
        workqueue_test_contexts[0].interrupts_enabled;
    out_result->invariants =
        workqueue_validate_on(&workqueue_test_service) == OK;
    for (uint32_t index = 0U;
         index < WORKQUEUE_SELF_TEST_CASE_COUNT; index++) {
        uint8_t passed = ((uint8_t*)out_result)[index];

        if (passed) out_result->passed++;
        else out_result->failed++;
    }
    if (out_result->failed) {
        LOG_ERROR("KERNEL", "Autoteste da workqueue detectou falha");
        return ERR_STATE;
    }
    return OK;
}

const char* workqueue_priority_name(work_priority_t priority) {
    if (priority == WORK_PRIORITY_HIGH) return "HIGH";
    if (priority == WORK_PRIORITY_NORMAL) return "NORMAL";
    return "INVALID";
}

const char* workqueue_state_name(work_state_t state) {
    if (state == WORK_STATE_IDLE) return "IDLE";
    if (state == WORK_STATE_READY) return "READY";
    if (state == WORK_STATE_DELAYED) return "DELAYED";
    if (state == WORK_STATE_RUNNING) return "RUNNING";
    return "INVALID";
}

const char* workqueue_context_name(work_context_t context) {
    if (context == WORK_CONTEXT_KWORKER) return "KWORKER";
    if (context == WORK_CONTEXT_SYSTEM_FALLBACK) return "SYSTEM_FALLBACK";
    return "NONE";
}
