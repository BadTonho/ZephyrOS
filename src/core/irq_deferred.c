#include "core/irq_deferred.h"
#include "core/errors.h"
#include "core/log.h"

typedef struct {
    irq_deferred_work_t* entries[IRQ_DEFERRED_CAPACITY];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    irq_deferred_status_t status;
} irq_deferred_service_t;

static irq_deferred_service_t service;

static uint32_t irq_deferred_irq_save(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static void irq_deferred_irq_restore(uint32_t flags) {
    if (flags & (1U << 9U)) asm volatile("sti" : : : "memory");
}

int irq_deferred_init(void) {
    LOG_INFO("THRD", "Inicializando fila de conclusoes diferidas");
    service.head = 0U;
    service.tail = 0U;
    service.count = 0U;
    service.status.initialized = 1U;
    service.status.queued = 0U;
    service.status.capacity = IRQ_DEFERRED_CAPACITY - 1U;
    service.status.dispatched = 0U;
    service.status.cancelled = 0U;
    service.status.rejected = 0U;
    service.status.peak_queued = 0U;
    service.status.last_error = OK;
    LOG_INFO("THRD", "Fila de conclusoes diferidas inicializada");
    return OK;
}

int irq_deferred_schedule(irq_deferred_work_t* work) {
    uint32_t flags;

    if (!work || !work->callback) {
        LOG_ERROR("THRD", "Trabalho diferido invalido");
        return ERR_NULL;
    }
    if (!service.status.initialized) {
        LOG_ERROR("THRD", "Fila diferida nao inicializada");
        return ERR_STATE;
    }
    flags = irq_deferred_irq_save();
    if (work->queued) {
        irq_deferred_irq_restore(flags);
        service.status.last_error = ERR_STATE;
        return ERR_STATE;
    }
    if (service.count + 1U >= IRQ_DEFERRED_CAPACITY) {
        service.status.rejected++;
        service.status.last_error = ERR_OVERFLOW;
        irq_deferred_irq_restore(flags);
        return ERR_OVERFLOW;
    }
    work->cancelled = 0U;
    work->queued = 1U;
    service.entries[service.tail] = work;
    service.tail = (service.tail + 1U) % IRQ_DEFERRED_CAPACITY;
    service.count++;
    service.status.queued = service.count;
    if (service.count > service.status.peak_queued) {
        service.status.peak_queued = service.count;
    }
    irq_deferred_irq_restore(flags);
    return OK;
}

int irq_deferred_cancel(irq_deferred_work_t* work) {
    uint32_t flags;

    if (!work) {
        LOG_ERROR("THRD", "Trabalho diferido nulo no cancelamento");
        return ERR_NULL;
    }
    if (!service.status.initialized) {
        LOG_ERROR("THRD", "Cancelamento antes da inicializacao diferida");
        return ERR_STATE;
    }
    flags = irq_deferred_irq_save();
    if (!work->queued) {
        irq_deferred_irq_restore(flags);
        return ERR_NOT_FOUND;
    }
    work->cancelled = 1U;
    service.status.cancelled++;
    service.status.last_error = OK;
    irq_deferred_irq_restore(flags);
    return OK;
}

int irq_deferred_dispatch(uint32_t budget, uint32_t* out_processed) {
    uint32_t processed = 0U;

    if (!out_processed) {
        LOG_ERROR("THRD", "Destino nulo no despacho diferido");
        return ERR_NULL;
    }
    if (!service.status.initialized) {
        LOG_ERROR("THRD", "Despacho diferido antes da inicializacao");
        return ERR_STATE;
    }
    while (processed < budget) {
        irq_deferred_work_t* work;
        uint8_t cancelled;
        uint32_t flags = irq_deferred_irq_save();

        if (!service.count) {
            irq_deferred_irq_restore(flags);
            break;
        }
        work = service.entries[service.head];
        service.entries[service.head] = 0;
        service.head = (service.head + 1U) % IRQ_DEFERRED_CAPACITY;
        service.count--;
        service.status.queued = service.count;
        cancelled = work ? work->cancelled : 1U;
        if (work) {
            work->queued = 0U;
            work->cancelled = 0U;
        }
        irq_deferred_irq_restore(flags);
        if (!work || cancelled) continue;
        work->callback(work->context);
        processed++;
        service.status.dispatched++;
    }
    *out_processed = processed;
    return OK;
}

int irq_deferred_get_status(irq_deferred_status_t* out_status) {
    uint32_t flags;

    if (!out_status) {
        LOG_ERROR("THRD", "Destino nulo ao consultar fila diferida");
        return ERR_NULL;
    }
    if (!service.status.initialized) {
        LOG_ERROR("THRD", "Consulta da fila diferida antes da inicializacao");
        return ERR_STATE;
    }
    flags = irq_deferred_irq_save();
    *out_status = service.status;
    irq_deferred_irq_restore(flags);
    return OK;
}

int irq_deferred_validate_state(void) {
    if (!service.status.initialized || service.count >= IRQ_DEFERRED_CAPACITY ||
        service.head >= IRQ_DEFERRED_CAPACITY ||
        service.tail >= IRQ_DEFERRED_CAPACITY ||
        service.status.queued != service.count ||
        service.status.capacity != IRQ_DEFERRED_CAPACITY - 1U) {
        LOG_ERROR("THRD", "Estado da fila diferida inconsistente");
        return ERR_STATE;
    }
    return OK;
}
