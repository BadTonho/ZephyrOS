#include "core/input.h"
#include "core/errors.h"
#include "core/log.h"

#define INPUT_POINTER_DELTA_LIMIT 32767

typedef struct {
    input_key_event_t key_queue[INPUT_KEY_QUEUE_CAPACITY];
    input_pointer_event_t pointer_queue[INPUT_POINTER_QUEUE_CAPACITY];
    uint32_t key_head;
    uint32_t key_tail;
    uint32_t pointer_head;
    uint32_t pointer_tail;
    input_key_sink_t key_sink;
    input_pointer_sink_t pointer_sink;
    uint8_t dispatch_turn;
    input_metrics_t metrics;
} input_service_t;

static input_service_t input_service;

static uint32_t input_irq_save(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static void input_irq_restore(uint32_t flags) {
    if (flags & (1U << 9U)) asm volatile("sti" : : : "memory");
}

static uint32_t input_next(uint32_t value, uint32_t capacity) {
    return (value + 1U) % capacity;
}

static uint32_t input_key_count(void) {
    if (input_service.key_head >= input_service.key_tail) {
        return input_service.key_head - input_service.key_tail;
    }
    return INPUT_KEY_QUEUE_CAPACITY - input_service.key_tail +
           input_service.key_head;
}

static uint32_t input_pointer_count(void) {
    if (input_service.pointer_head >= input_service.pointer_tail) {
        return input_service.pointer_head - input_service.pointer_tail;
    }
    return INPUT_POINTER_QUEUE_CAPACITY - input_service.pointer_tail +
           input_service.pointer_head;
}

static int32_t input_pointer_accumulate_delta(int32_t current,
                                              int32_t delta) {
    int64_t total = (int64_t)current + (int64_t)delta;

    if (total > INPUT_POINTER_DELTA_LIMIT) return INPUT_POINTER_DELTA_LIMIT;
    if (total < -INPUT_POINTER_DELTA_LIMIT) return -INPUT_POINTER_DELTA_LIMIT;
    return (int32_t)total;
}

int input_init(void) {
    LOG_INFO("KBD", "Inicializando nucleo de entrada");
    input_service.key_head = 0U;
    input_service.key_tail = 0U;
    input_service.pointer_head = 0U;
    input_service.pointer_tail = 0U;
    input_service.key_sink = 0;
    input_service.pointer_sink = 0;
    input_service.dispatch_turn = 0U;
    input_service.metrics.initialized = 1U;
    input_service.metrics.key_queued = 0U;
    input_service.metrics.pointer_queued = 0U;
    input_service.metrics.key_capacity = INPUT_KEY_QUEUE_CAPACITY - 1U;
    input_service.metrics.pointer_capacity = INPUT_POINTER_QUEUE_CAPACITY - 1U;
    input_service.metrics.key_published = 0U;
    input_service.metrics.pointer_published = 0U;
    input_service.metrics.key_processed = 0U;
    input_service.metrics.pointer_processed = 0U;
    input_service.metrics.key_dropped = 0U;
    input_service.metrics.pointer_dropped = 0U;
    input_service.metrics.key_peak_queued = 0U;
    input_service.metrics.pointer_peak_queued = 0U;
    input_service.metrics.last_error = OK;
    LOG_INFO("KBD", "Nucleo de entrada inicializado com sucesso");
    return OK;
}

int input_register_key_sink(input_key_sink_t sink) {
    if (!sink) {
        LOG_ERROR("KBD", "Destino nulo ao registrar entrada de teclado");
        return ERR_NULL;
    }
    if (!input_service.metrics.initialized) {
        LOG_ERROR("KBD", "Registro de teclado antes da inicializacao de entrada");
        return ERR_STATE;
    }
    if (input_service.key_sink && input_service.key_sink != sink) {
        LOG_ERROR("KBD", "Entrada de teclado ja possui consumidor");
        return ERR_STATE;
    }
    input_service.key_sink = sink;
    return OK;
}

int input_register_pointer_sink(input_pointer_sink_t sink) {
    if (!sink) {
        LOG_ERROR("MOUSE", "Destino nulo ao registrar entrada de ponteiro");
        return ERR_NULL;
    }
    if (!input_service.metrics.initialized) {
        LOG_ERROR("MOUSE", "Registro de ponteiro antes da inicializacao");
        return ERR_STATE;
    }
    if (input_service.pointer_sink && input_service.pointer_sink != sink) {
        LOG_ERROR("MOUSE", "Entrada de ponteiro ja possui consumidor");
        return ERR_STATE;
    }
    input_service.pointer_sink = sink;
    return OK;
}

int input_publish_key(const input_key_event_t* event) {
    uint32_t flags;
    uint32_t next;

    if (!event) return ERR_NULL;
    if (!input_service.metrics.initialized) return ERR_STATE;
    flags = input_irq_save();
    next = input_next(input_service.key_head, INPUT_KEY_QUEUE_CAPACITY);
    if (next == input_service.key_tail) {
        input_service.metrics.key_dropped++;
        input_service.metrics.last_error = ERR_OVERFLOW;
        input_irq_restore(flags);
        return ERR_OVERFLOW;
    }
    input_service.key_queue[input_service.key_head] = *event;
    input_service.key_head = next;
    input_service.metrics.key_published++;
    input_service.metrics.key_queued = input_key_count();
    if (input_service.metrics.key_queued > input_service.metrics.key_peak_queued) {
        input_service.metrics.key_peak_queued = input_service.metrics.key_queued;
    }
    input_irq_restore(flags);
    return OK;
}

int input_publish_pointer(const input_pointer_event_t* event) {
    uint32_t flags;
    uint32_t next;

    if (!event) return ERR_NULL;
    if (!input_service.metrics.initialized) return ERR_STATE;
    flags = input_irq_save();
    if (event->wheel == 0 &&
        input_service.pointer_head != input_service.pointer_tail) {
        uint32_t last = input_service.pointer_head == 0U ?
                        INPUT_POINTER_QUEUE_CAPACITY - 1U :
                        input_service.pointer_head - 1U;
        input_pointer_event_t* queued = &input_service.pointer_queue[last];

        if (queued->wheel == 0 && queued->buttons == event->buttons &&
            queued->source == event->source) {
            queued->dx = input_pointer_accumulate_delta(queued->dx,
                                                        event->dx);
            queued->dy = input_pointer_accumulate_delta(queued->dy,
                                                        event->dy);
            input_service.metrics.pointer_published++;
            input_irq_restore(flags);
            return OK;
        }
    }
    next = input_next(input_service.pointer_head,
                      INPUT_POINTER_QUEUE_CAPACITY);
    if (next == input_service.pointer_tail) {
        input_service.metrics.pointer_dropped++;
        input_service.metrics.last_error = ERR_OVERFLOW;
        input_irq_restore(flags);
        return ERR_OVERFLOW;
    }
    input_service.pointer_queue[input_service.pointer_head] = *event;
    input_service.pointer_head = next;
    input_service.metrics.pointer_published++;
    input_service.metrics.pointer_queued = input_pointer_count();
    if (input_service.metrics.pointer_queued >
        input_service.metrics.pointer_peak_queued) {
        input_service.metrics.pointer_peak_queued =
            input_service.metrics.pointer_queued;
    }
    input_irq_restore(flags);
    return OK;
}

static int input_dispatch_key(void) {
    input_key_event_t event;
    uint32_t flags;
    int result;

    if (!input_service.key_sink) return ERR_STATE;
    flags = input_irq_save();
    if (input_service.key_tail == input_service.key_head) {
        input_irq_restore(flags);
        return ERR_NOT_FOUND;
    }
    event = input_service.key_queue[input_service.key_tail];
    input_irq_restore(flags);
    result = input_service.key_sink(&event);
    if (result != OK) return result;
    flags = input_irq_save();
    input_service.key_tail = input_next(input_service.key_tail,
                                        INPUT_KEY_QUEUE_CAPACITY);
    input_service.metrics.key_queued = input_key_count();
    input_service.metrics.key_processed++;
    input_irq_restore(flags);
    return OK;
}

static int input_dispatch_pointer(void) {
    input_pointer_event_t event;
    uint32_t flags;
    int result;

    if (!input_service.pointer_sink) return ERR_STATE;
    flags = input_irq_save();
    if (input_service.pointer_tail == input_service.pointer_head) {
        input_irq_restore(flags);
        return ERR_NOT_FOUND;
    }
    event = input_service.pointer_queue[input_service.pointer_tail];
    input_irq_restore(flags);
    result = input_service.pointer_sink(&event);
    if (result != OK) return result;
    flags = input_irq_save();
    input_service.pointer_tail = input_next(input_service.pointer_tail,
                                            INPUT_POINTER_QUEUE_CAPACITY);
    input_service.metrics.pointer_queued = input_pointer_count();
    input_service.metrics.pointer_processed++;
    input_irq_restore(flags);
    return OK;
}

int input_dispatch(uint32_t budget, uint32_t* out_processed) {
    uint32_t processed = 0U;

    if (!out_processed) {
        LOG_ERROR("KBD", "Destino nulo no despacho de entrada");
        return ERR_NULL;
    }
    if (!input_service.metrics.initialized) {
        LOG_ERROR("KBD", "Despacho de entrada antes da inicializacao");
        return ERR_STATE;
    }
    while (processed < budget) {
        int result;
        uint8_t prefer_key = input_service.dispatch_turn == 0U;

        if (prefer_key) result = input_dispatch_key();
        else result = input_dispatch_pointer();
        if (result == ERR_NOT_FOUND || result == ERR_STATE) {
            if (prefer_key) result = input_dispatch_pointer();
            else result = input_dispatch_key();
        }
        if (result == ERR_NOT_FOUND) break;
        if (result != OK) {
            input_service.metrics.last_error = result;
            if (result == ERR_OVERFLOW) {
                LOG_WARN("KBD", "Consumidor de entrada temporariamente cheio");
            } else {
                LOG_ERROR("KBD", "Falha ao encaminhar evento de entrada");
            }
            break;
        }
        input_service.dispatch_turn ^= 1U;
        processed++;
    }
    *out_processed = processed;
    return OK;
}

int input_get_metrics(input_metrics_t* out_metrics) {
    uint32_t flags;

    if (!out_metrics) {
        LOG_ERROR("KBD", "Destino nulo nas metricas de entrada");
        return ERR_NULL;
    }
    if (!input_service.metrics.initialized) {
        LOG_ERROR("KBD", "Metricas de entrada antes da inicializacao");
        return ERR_STATE;
    }
    flags = input_irq_save();
    input_service.metrics.key_queued = input_key_count();
    input_service.metrics.pointer_queued = input_pointer_count();
    *out_metrics = input_service.metrics;
    input_irq_restore(flags);
    return OK;
}

int input_validate_state(void) {
    input_metrics_t metrics;

    if (input_get_metrics(&metrics) != OK ||
        metrics.key_capacity != INPUT_KEY_QUEUE_CAPACITY - 1U ||
        metrics.pointer_capacity != INPUT_POINTER_QUEUE_CAPACITY - 1U ||
        metrics.key_queued > metrics.key_capacity ||
        metrics.pointer_queued > metrics.pointer_capacity) {
        LOG_ERROR("KBD", "Estado do nucleo de entrada inconsistente");
        return ERR_STATE;
    }
    return OK;
}
