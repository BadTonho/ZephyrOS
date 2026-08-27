#ifndef IRQ_DEFERRED_H
#define IRQ_DEFERRED_H

#include "types.h"

#define IRQ_DEFERRED_CAPACITY 32U
#define IRQ_DEFERRED_USABLE_CAPACITY (IRQ_DEFERRED_CAPACITY - 1U)
#define IRQ_DEFERRED_OWNER_SIZE 16U
#define IRQ_DEFERRED_IRQ_COUNT 16U
#define IRQ_DEFERRED_IRQ_NONE 0xFFU

typedef void (*irq_deferred_callback_t)(void* context);
typedef void (*irq_deferred_notifier_t)(void* context);

typedef struct {
    irq_deferred_callback_t callback;
    void* context;
    char owner[IRQ_DEFERRED_OWNER_SIZE];
    uint8_t irq_line;
    volatile uint8_t initialized;
    volatile uint8_t queued;
    volatile uint8_t running;
    volatile uint8_t cancelled;
    volatile uint8_t rerun_requested;
    uint32_t scheduled;
    uint32_t dispatched;
    uint32_t coalesced;
    uint32_t rejected;
} irq_deferred_work_t;

typedef struct {
    uint32_t scheduled;
    uint32_t dispatched;
    uint32_t coalesced;
    uint32_t rejected;
    uint32_t cancelled;
} irq_deferred_irq_status_t;

typedef struct {
    uint8_t initialized;
    uint32_t queued;
    uint32_t running;
    uint32_t capacity;
    uint32_t scheduled;
    uint32_t dispatched;
    uint32_t coalesced;
    uint32_t reruns;
    uint32_t cancelled;
    uint32_t rejected;
    uint32_t peak_queued;
    uint32_t context_errors;
    int last_error;
} irq_deferred_status_t;

typedef struct {
    uint8_t lifecycle;
    uint8_t coalescing;
    uint8_t rerun;
    uint8_t cancellation;
    uint8_t capacity;
    uint8_t attribution;
    uint8_t interrupt_context;
    uint8_t invariants;
    uint32_t passed;
    uint32_t failed;
} irq_deferred_self_test_result_t;

int irq_deferred_init(void);
int irq_deferred_work_init(irq_deferred_work_t* work, const char* owner,
                           uint8_t irq_line,
                           irq_deferred_callback_t callback, void* context);
int irq_deferred_schedule(irq_deferred_work_t* work);
int irq_deferred_cancel(irq_deferred_work_t* work);
int irq_deferred_set_notifier(irq_deferred_notifier_t notifier,
                              void* context);
int irq_deferred_dispatch(uint32_t budget, uint32_t* out_processed);
int irq_deferred_get_status(irq_deferred_status_t* out_status);
int irq_deferred_get_irq_status(uint8_t irq_line,
                                irq_deferred_irq_status_t* out_status);
int irq_deferred_validate_state(void);
int irq_deferred_self_test(irq_deferred_self_test_result_t* out_result);

#endif
