#include "core/irq_deferred.h"
#include "core/errors.h"
#include "core/log.h"

typedef struct {
    irq_deferred_work_t* entries[IRQ_DEFERRED_CAPACITY];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    irq_deferred_status_t status;
    irq_deferred_irq_status_t irq[IRQ_DEFERRED_IRQ_COUNT];
} irq_deferred_service_t;

typedef struct {
    irq_deferred_service_t* service;
    irq_deferred_work_t* work;
    uint32_t calls;
    uint8_t request_rerun;
    uint8_t interrupts_enabled;
} irq_deferred_test_callback_t;

static irq_deferred_service_t service;

static uint32_t irq_deferred_irq_save(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static void irq_deferred_irq_restore(uint32_t flags) {
    if (flags & (1U << 9U)) asm volatile("sti" : : : "memory");
}

static uint8_t irq_deferred_interrupts_enabled(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0" : "=r"(flags));
    return (uint8_t)((flags & (1U << 9U)) != 0U);
}

static int irq_deferred_owner_valid(const char* owner) {
    uint32_t length = 0U;

    if (!owner || !owner[0]) return 0;
    while (length < IRQ_DEFERRED_OWNER_SIZE && owner[length]) length++;
    return length < IRQ_DEFERRED_OWNER_SIZE;
}

static void irq_deferred_copy_owner(char* destination, const char* source) {
    uint32_t index = 0U;

    while (index + 1U < IRQ_DEFERRED_OWNER_SIZE && source[index]) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void irq_deferred_service_init(irq_deferred_service_t* target) {
    for (uint32_t index = 0U; index < IRQ_DEFERRED_CAPACITY; index++) {
        target->entries[index] = 0;
    }
    target->head = 0U;
    target->tail = 0U;
    target->count = 0U;
    target->status.initialized = 1U;
    target->status.queued = 0U;
    target->status.running = 0U;
    target->status.capacity = IRQ_DEFERRED_USABLE_CAPACITY;
    target->status.scheduled = 0U;
    target->status.dispatched = 0U;
    target->status.coalesced = 0U;
    target->status.reruns = 0U;
    target->status.cancelled = 0U;
    target->status.rejected = 0U;
    target->status.peak_queued = 0U;
    target->status.context_errors = 0U;
    target->status.last_error = OK;
    for (uint32_t irq = 0U; irq < IRQ_DEFERRED_IRQ_COUNT; irq++) {
        target->irq[irq].scheduled = 0U;
        target->irq[irq].dispatched = 0U;
        target->irq[irq].coalesced = 0U;
        target->irq[irq].rejected = 0U;
        target->irq[irq].cancelled = 0U;
    }
}

static int irq_deferred_schedule_on(irq_deferred_service_t* target,
                                    irq_deferred_work_t* work) {
    irq_deferred_irq_status_t* irq_status = 0;
    uint32_t flags;

    if (!target || !work || !work->callback) return ERR_NULL;
    if (!target->status.initialized || !work->initialized) return ERR_STATE;
    if (work->irq_line < IRQ_DEFERRED_IRQ_COUNT) {
        irq_status = &target->irq[work->irq_line];
    }
    flags = irq_deferred_irq_save();
    if (work->queued) {
        work->coalesced++;
        target->status.coalesced++;
        if (irq_status) irq_status->coalesced++;
        target->status.last_error = OK;
        irq_deferred_irq_restore(flags);
        return OK;
    }
    if (work->running) {
        work->rerun_requested = 1U;
        work->coalesced++;
        target->status.coalesced++;
        if (irq_status) irq_status->coalesced++;
        target->status.last_error = OK;
        irq_deferred_irq_restore(flags);
        return OK;
    }
    if (target->count >= IRQ_DEFERRED_USABLE_CAPACITY) {
        work->rejected++;
        target->status.rejected++;
        target->status.last_error = ERR_OVERFLOW;
        if (irq_status) irq_status->rejected++;
        irq_deferred_irq_restore(flags);
        return ERR_OVERFLOW;
    }
    work->cancelled = 0U;
    work->queued = 1U;
    work->scheduled++;
    target->entries[target->tail] = work;
    target->tail = (target->tail + 1U) % IRQ_DEFERRED_CAPACITY;
    target->count++;
    target->status.queued = target->count;
    target->status.scheduled++;
    target->status.last_error = OK;
    if (irq_status) irq_status->scheduled++;
    if (target->count > target->status.peak_queued) {
        target->status.peak_queued = target->count;
    }
    irq_deferred_irq_restore(flags);
    return OK;
}

static int irq_deferred_cancel_on(irq_deferred_service_t* target,
                                  irq_deferred_work_t* work) {
    uint32_t flags;

    if (!target || !work) return ERR_NULL;
    if (!target->status.initialized || !work->initialized) return ERR_STATE;
    flags = irq_deferred_irq_save();
    if (!work->queued && !work->running) {
        irq_deferred_irq_restore(flags);
        return ERR_NOT_FOUND;
    }
    if (work->queued) {
        uint32_t original_count = target->count;
        uint32_t retained = 0U;

        for (uint32_t index = 0U; index < original_count; index++) {
            uint32_t read_index =
                (target->head + index) % IRQ_DEFERRED_CAPACITY;
            irq_deferred_work_t* current = target->entries[read_index];

            if (current == work) continue;
            target->entries[(target->head + retained) %
                            IRQ_DEFERRED_CAPACITY] = current;
            retained++;
        }
        for (uint32_t index = retained; index < original_count; index++) {
            target->entries[(target->head + index) %
                            IRQ_DEFERRED_CAPACITY] = 0;
        }
        target->count = retained;
        target->tail = (target->head + retained) % IRQ_DEFERRED_CAPACITY;
        target->status.queued = retained;
        work->queued = 0U;
        work->cancelled = 0U;
    } else {
        work->cancelled = 1U;
    }
    work->rerun_requested = 0U;
    target->status.cancelled++;
    target->status.last_error = OK;
    if (work->irq_line < IRQ_DEFERRED_IRQ_COUNT) {
        target->irq[work->irq_line].cancelled++;
    }
    irq_deferred_irq_restore(flags);
    return OK;
}

static int irq_deferred_dispatch_on(irq_deferred_service_t* target,
                                    uint32_t budget,
                                    uint32_t* out_processed) {
    uint32_t processed = 0U;

    if (!target || !out_processed) return ERR_NULL;
    if (!target->status.initialized) return ERR_STATE;
    while (processed < budget) {
        irq_deferred_work_t* work;
        uint8_t cancelled;
        uint32_t flags = irq_deferred_irq_save();

        if (!target->count) {
            irq_deferred_irq_restore(flags);
            break;
        }
        work = target->entries[target->head];
        target->entries[target->head] = 0;
        target->head = (target->head + 1U) % IRQ_DEFERRED_CAPACITY;
        target->count--;
        target->status.queued = target->count;
        cancelled = work ? work->cancelled : 1U;
        if (work) {
            work->queued = 0U;
            work->running = cancelled ? 0U : 1U;
            work->cancelled = 0U;
            if (!cancelled) target->status.running++;
        }
        irq_deferred_irq_restore(flags);
        if (!work || cancelled) continue;
        work->callback(work->context);
        flags = irq_deferred_irq_save();
        work->running = 0U;
        work->dispatched++;
        if (target->status.running) target->status.running--;
        target->status.dispatched++;
        if (work->irq_line < IRQ_DEFERRED_IRQ_COUNT) {
            target->irq[work->irq_line].dispatched++;
        }
        if (work->rerun_requested && !work->cancelled) {
            work->rerun_requested = 0U;
            if (target->count < IRQ_DEFERRED_USABLE_CAPACITY) {
                work->queued = 1U;
                work->scheduled++;
                target->entries[target->tail] = work;
                target->tail = (target->tail + 1U) % IRQ_DEFERRED_CAPACITY;
                target->count++;
                target->status.queued = target->count;
                target->status.scheduled++;
                target->status.reruns++;
                if (work->irq_line < IRQ_DEFERRED_IRQ_COUNT) {
                    target->irq[work->irq_line].scheduled++;
                }
            } else {
                work->rejected++;
                target->status.rejected++;
                target->status.last_error = ERR_OVERFLOW;
                if (work->irq_line < IRQ_DEFERRED_IRQ_COUNT) {
                    target->irq[work->irq_line].rejected++;
                }
            }
        } else {
            work->rerun_requested = 0U;
            work->cancelled = 0U;
        }
        irq_deferred_irq_restore(flags);
        processed++;
    }
    *out_processed = processed;
    return OK;
}

static int irq_deferred_validate_service(
    const irq_deferred_service_t* target) {
    uint32_t index;

    if (!target || !target->status.initialized ||
        target->count > IRQ_DEFERRED_USABLE_CAPACITY ||
        target->head >= IRQ_DEFERRED_CAPACITY ||
        target->tail >= IRQ_DEFERRED_CAPACITY ||
        target->tail !=
            (target->head + target->count) % IRQ_DEFERRED_CAPACITY ||
        target->status.queued != target->count ||
        target->status.capacity != IRQ_DEFERRED_USABLE_CAPACITY) {
        return ERR_STATE;
    }
    index = target->head;
    for (uint32_t count = 0U; count < target->count; count++) {
        irq_deferred_work_t* work = target->entries[index];

        if (!work || !work->initialized || !work->queued || work->running ||
            !work->callback || !irq_deferred_owner_valid(work->owner) ||
            (work->irq_line >= IRQ_DEFERRED_IRQ_COUNT &&
             work->irq_line != IRQ_DEFERRED_IRQ_NONE)) {
            return ERR_STATE;
        }
        index = (index + 1U) % IRQ_DEFERRED_CAPACITY;
    }
    return OK;
}

static void irq_deferred_test_callback(void* context) {
    irq_deferred_test_callback_t* state =
        (irq_deferred_test_callback_t*)context;

    if (!state) return;
    state->calls++;
    state->interrupts_enabled = irq_deferred_interrupts_enabled();
    if (state->request_rerun && state->calls == 1U) {
        (void)irq_deferred_schedule_on(state->service, state->work);
    }
}

int irq_deferred_init(void) {
    LOG_INFO("IDT", "Inicializando fila de Bottom-Half");
    irq_deferred_service_init(&service);
    LOG_INFO("IDT", "Fila de Bottom-Half inicializada");
    return OK;
}

int irq_deferred_work_init(irq_deferred_work_t* work, const char* owner,
                           uint8_t irq_line,
                           irq_deferred_callback_t callback, void* context) {
    if (!work || !callback) {
        LOG_ERROR("IDT", "Trabalho Bottom-Half invalido");
        return ERR_NULL;
    }
    if (!irq_deferred_owner_valid(owner) ||
        (irq_line >= IRQ_DEFERRED_IRQ_COUNT &&
         irq_line != IRQ_DEFERRED_IRQ_NONE)) {
        LOG_ERROR("IDT", "Identidade Bottom-Half invalida");
        return ERR_INVALID;
    }
    if (work->queued || work->running) {
        LOG_ERROR("IDT", "Trabalho Bottom-Half ativo na inicializacao");
        return ERR_STATE;
    }
    work->callback = callback;
    work->context = context;
    irq_deferred_copy_owner(work->owner, owner);
    work->irq_line = irq_line;
    work->initialized = 1U;
    work->queued = 0U;
    work->running = 0U;
    work->cancelled = 0U;
    work->rerun_requested = 0U;
    work->scheduled = 0U;
    work->dispatched = 0U;
    work->coalesced = 0U;
    work->rejected = 0U;
    return OK;
}

int irq_deferred_schedule(irq_deferred_work_t* work) {
    int result = irq_deferred_schedule_on(&service, work);

    if (result == ERR_NULL || result == ERR_STATE) {
        LOG_ERROR_CODE("IDT", result, "Falha ao agendar Bottom-Half");
    }
    return result;
}

int irq_deferred_cancel(irq_deferred_work_t* work) {
    int result = irq_deferred_cancel_on(&service, work);

    if (result == ERR_NULL || result == ERR_STATE) {
        LOG_ERROR_CODE("IDT", result, "Falha ao cancelar Bottom-Half");
    }
    return result;
}

int irq_deferred_dispatch(uint32_t budget, uint32_t* out_processed) {
    int result;

    if (!out_processed) {
        LOG_ERROR("IDT", "Destino nulo no despacho Bottom-Half");
        return ERR_NULL;
    }
    if (!irq_deferred_interrupts_enabled()) {
        service.status.context_errors++;
        service.status.last_error = ERR_STATE;
        LOG_ERROR("IDT", "Despacho Bottom-Half com interrupcoes desabilitadas");
        return ERR_STATE;
    }
    result = irq_deferred_dispatch_on(&service, budget, out_processed);
    if (result != OK) {
        LOG_ERROR_CODE("IDT", result, "Despacho Bottom-Half falhou");
    }
    return result;
}

int irq_deferred_get_status(irq_deferred_status_t* out_status) {
    uint32_t flags;

    if (!out_status) {
        LOG_ERROR("IDT", "Destino nulo ao consultar Bottom-Half");
        return ERR_NULL;
    }
    if (!service.status.initialized) {
        LOG_ERROR("IDT", "Consulta Bottom-Half antes da inicializacao");
        return ERR_STATE;
    }
    flags = irq_deferred_irq_save();
    *out_status = service.status;
    irq_deferred_irq_restore(flags);
    return OK;
}

int irq_deferred_get_irq_status(uint8_t irq_line,
                                irq_deferred_irq_status_t* out_status) {
    uint32_t flags;

    if (!out_status) {
        LOG_ERROR("IDT", "Destino nulo ao consultar Bottom-Half por IRQ");
        return ERR_NULL;
    }
    if (!service.status.initialized) {
        LOG_ERROR("IDT", "Consulta por IRQ antes da inicializacao Bottom-Half");
        return ERR_STATE;
    }
    if (irq_line >= IRQ_DEFERRED_IRQ_COUNT) {
        LOG_ERROR("IDT", "Linha invalida na consulta Bottom-Half");
        return ERR_INVALID;
    }
    flags = irq_deferred_irq_save();
    *out_status = service.irq[irq_line];
    irq_deferred_irq_restore(flags);
    return OK;
}

int irq_deferred_validate_state(void) {
    int result = irq_deferred_validate_service(&service);

    if (result != OK) {
        LOG_ERROR("IDT", "Estado da fila Bottom-Half inconsistente");
    }
    return result;
}

int irq_deferred_self_test(irq_deferred_self_test_result_t* out_result) {
    irq_deferred_service_t test_service;
    irq_deferred_work_t works[IRQ_DEFERRED_CAPACITY];
    irq_deferred_test_callback_t callbacks[IRQ_DEFERRED_CAPACITY];
    uint32_t processed = 0U;

    if (!out_result) {
        LOG_ERROR("IDT", "Destino nulo no autoteste Bottom-Half");
        return ERR_NULL;
    }
    for (uint32_t byte = 0U; byte < sizeof(*out_result); byte++) {
        ((uint8_t*)out_result)[byte] = 0U;
    }
    irq_deferred_service_init(&test_service);
    for (uint32_t index = 0U; index < IRQ_DEFERRED_CAPACITY; index++) {
        callbacks[index].service = &test_service;
        callbacks[index].work = &works[index];
        callbacks[index].calls = 0U;
        callbacks[index].request_rerun = 0U;
        callbacks[index].interrupts_enabled = 0U;
        works[index].queued = 0U;
        works[index].running = 0U;
        if (irq_deferred_work_init(&works[index], "IRQCheck", 1U,
                                   irq_deferred_test_callback,
                                   &callbacks[index]) != OK) {
            return ERR_STATE;
        }
    }
    out_result->lifecycle =
        irq_deferred_schedule_on(&test_service, &works[0]) == OK &&
        irq_deferred_dispatch_on(&test_service, 1U, &processed) == OK &&
        processed == 1U && callbacks[0].calls == 1U;
    processed = 0U;
    out_result->coalescing =
        irq_deferred_schedule_on(&test_service, &works[1]) == OK &&
        irq_deferred_schedule_on(&test_service, &works[1]) == OK &&
        test_service.status.coalesced == 1U &&
        irq_deferred_dispatch_on(&test_service, 1U, &processed) == OK &&
        callbacks[1].calls == 1U;
    callbacks[2].request_rerun = 1U;
    processed = 0U;
    out_result->rerun =
        irq_deferred_schedule_on(&test_service, &works[2]) == OK &&
        irq_deferred_dispatch_on(&test_service, 2U, &processed) == OK &&
        callbacks[2].calls == 2U && test_service.status.reruns == 1U;
    processed = 0U;
    out_result->cancellation =
        irq_deferred_schedule_on(&test_service, &works[3]) == OK &&
        irq_deferred_cancel_on(&test_service, &works[3]) == OK &&
        irq_deferred_dispatch_on(&test_service, 1U, &processed) == OK &&
        callbacks[3].calls == 0U;
    irq_deferred_service_init(&test_service);
    out_result->capacity = 1U;
    for (uint32_t index = 0U; index < IRQ_DEFERRED_USABLE_CAPACITY; index++) {
        if (irq_deferred_schedule_on(&test_service, &works[index]) != OK) {
            out_result->capacity = 0U;
        }
    }
    if (irq_deferred_schedule_on(&test_service,
                                 &works[IRQ_DEFERRED_USABLE_CAPACITY]) !=
        ERR_OVERFLOW) {
        out_result->capacity = 0U;
    }
    out_result->attribution = test_service.irq[1].scheduled ==
                              IRQ_DEFERRED_USABLE_CAPACITY &&
                              test_service.irq[1].rejected == 1U;
    out_result->interrupt_context = callbacks[0].interrupts_enabled;
    out_result->invariants =
        irq_deferred_validate_service(&test_service) == OK;
    for (uint32_t index = 0U; index < 8U; index++) {
        uint8_t passed = ((uint8_t*)out_result)[index];

        if (passed) out_result->passed++;
        else out_result->failed++;
    }
    if (out_result->failed) {
        LOG_ERROR("IDT", "Autoteste Bottom-Half detectou falha");
        return ERR_STATE;
    }
    return OK;
}
