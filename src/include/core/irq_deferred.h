#ifndef IRQ_DEFERRED_H
#define IRQ_DEFERRED_H

#include "types.h"

#define IRQ_DEFERRED_CAPACITY 32U
#define IRQ_DEFERRED_OWNER_SIZE 16U

typedef void (*irq_deferred_callback_t)(void* context);

typedef struct {
    irq_deferred_callback_t callback;
    void* context;
    volatile uint8_t queued;
    volatile uint8_t cancelled;
} irq_deferred_work_t;

typedef struct {
    uint8_t initialized;
    uint32_t queued;
    uint32_t capacity;
    uint32_t dispatched;
    uint32_t cancelled;
    uint32_t rejected;
    uint32_t peak_queued;
    int last_error;
} irq_deferred_status_t;

int irq_deferred_init(void);
int irq_deferred_schedule(irq_deferred_work_t* work);
int irq_deferred_cancel(irq_deferred_work_t* work);
int irq_deferred_dispatch(uint32_t budget, uint32_t* out_processed);
int irq_deferred_get_status(irq_deferred_status_t* out_status);
int irq_deferred_validate_state(void);

#endif
