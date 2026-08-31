#include "core/timer.h"
#include "drivers/idt.h"
#include "core/log.h"
#include "core/errors.h"
#include "core/string.h"
#include "process/process.h"
#include "process/thread.h"

#define TIMER_PIT_INPUT_FREQUENCY 1193180U
#define TIMER_PIT_COMMAND_PORT 0x43U
#define TIMER_PIT_CHANNEL_ZERO_PORT 0x40U
#define TIMER_PIT_MODE_COMMAND 0x36U
#define TIMER_PIC_COMMAND_PORT 0x20U
#define TIMER_PIC_EOI 0x20U
#define TIMER_IRQ_VECTOR 32U
#define TIMER_IRQ_LINE 0U
#define TIMER_EFLAGS_INTERRUPT_ENABLE (1U << 9U)
#define TIMER_HANDLE_SLOT_MASK 0xFFU
#define TIMER_HANDLE_GENERATION_SHIFT 8U
#define TIMER_HANDLE_GENERATION_MAX 0x00FFFFFFU
#define TIMER_MILLISECONDS_PER_SECOND 1000U
#define TIMER_PIT_MAX_DIVISOR 0xFFFFU
#define TIMER_PRIVATE_TEST_CAPACITY 4U
#define TIMER_PRIVATE_OWNER_CAPACITY 2U

typedef struct {
    uint8_t active;
    uint32_t generation;
    char name[TIMER_NAME_CAPACITY];
} timer_owner_entry_t;

typedef struct {
    uint8_t active;
    uint32_t generation;
    timer_owner_handle_t owner;
    char name[TIMER_NAME_CAPACITY];
    timer_callback_fn callback;
    void* context;
    timer_mode_t mode;
    timer_state_t state;
    uint32_t deadline_tick;
    uint32_t period_ticks;
    uint32_t executions;
    uint32_t delayed_callbacks;
    uint32_t missed_periods;
    uint32_t last_lateness_ticks;
    int last_error;
} timer_entry_t;

typedef struct {
    timer_owner_entry_t* owners;
    timer_entry_t* timers;
    uint32_t* owner_generations;
    uint32_t* timer_generations;
    uint32_t owner_capacity;
    uint32_t timer_capacity;
    volatile uint32_t current_tick;
    uint32_t frequency;
    uint8_t initialized;
    timer_stats_t stats;
} timer_service_t;

typedef struct {
    uint32_t index;
    uint32_t generation;
    timer_handle_t handle;
    timer_callback_fn callback;
    void* context;
} timer_dispatch_event_t;

typedef struct {
    timer_owner_entry_t owners[TIMER_PRIVATE_OWNER_CAPACITY];
    timer_entry_t timers[TIMER_PRIVATE_TEST_CAPACITY];
    uint32_t owner_generations[TIMER_PRIVATE_OWNER_CAPACITY];
    uint32_t timer_generations[TIMER_PRIVATE_TEST_CAPACITY];
    timer_service_t service;
    timer_owner_handle_t owner;
} timer_test_fixture_t;

typedef struct {
    uint32_t calls;
    timer_handle_t last_handle;
    int result;
} timer_test_callback_state_t;

static timer_owner_entry_t timer_owners[TIMER_OWNER_CAPACITY];
static timer_entry_t timer_entries[TIMER_CAPACITY];
static uint32_t timer_owner_generations[TIMER_OWNER_CAPACITY];
static uint32_t timer_generations[TIMER_CAPACITY];
static timer_service_t timer_service;
static timer_pending_notifier_t timer_pending_notifier;
static void* timer_pending_notifier_context;

static void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint32_t timer_suspend_interrupts(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static void timer_restore_interrupts(uint32_t flags) {
    if (flags & TIMER_EFLAGS_INTERRUPT_ENABLE) {
        asm volatile("sti" : : : "memory");
    }
}

static int timer_name_is_valid(const char* name) {
    uint32_t length = 0U;

    if (!name || !name[0]) return 0;
    while (length < TIMER_NAME_CAPACITY && name[length]) length++;
    return length < TIMER_NAME_CAPACITY;
}

static void timer_copy_name(char* destination, const char* source) {
    uint32_t index = 0U;

    while (index + 1U < TIMER_NAME_CAPACITY && source[index]) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static uint32_t timer_next_generation(uint32_t generation) {
    generation++;
    if (!generation || generation > TIMER_HANDLE_GENERATION_MAX) {
        generation = 1U;
    }
    return generation;
}

static uint32_t timer_make_handle(uint32_t index, uint32_t generation) {
    return (generation << TIMER_HANDLE_GENERATION_SHIFT) | (index + 1U);
}

static int32_t timer_decode_handle(uint32_t handle, uint32_t capacity,
                                   uint32_t* out_generation) {
    uint32_t slot = handle & TIMER_HANDLE_SLOT_MASK;
    uint32_t generation = handle >> TIMER_HANDLE_GENERATION_SHIFT;

    if (!slot || slot > capacity || !generation) return -1;
    if (out_generation) *out_generation = generation;
    return (int32_t)(slot - 1U);
}

static int32_t timer_find_owner(const timer_service_t* service,
                                timer_owner_handle_t handle) {
    uint32_t generation;
    int32_t index = timer_decode_handle(handle, service->owner_capacity,
                                        &generation);

    if (index < 0 || !service->owners[index].active ||
        service->owners[index].generation != generation) return -1;
    return index;
}

static int32_t timer_find_entry(const timer_service_t* service,
                                timer_handle_t handle) {
    uint32_t generation;
    int32_t index = timer_decode_handle(handle, service->timer_capacity,
                                        &generation);

    if (index < 0 || !service->timers[index].active ||
        service->timers[index].generation != generation) return -1;
    return index;
}

static int timer_service_error(timer_service_t* service, int result) {
    if (service) service->stats.invalid_operations++;
    return result;
}

static int timer_result_code(int result) {
    return result;
}

static void timer_note_invalid_operation(void) {
    uint32_t flags = timer_suspend_interrupts();

    timer_service.stats.invalid_operations++;
    timer_restore_interrupts(flags);
}

static void timer_service_initialize(timer_service_t* service,
                                     timer_owner_entry_t* owners,
                                     uint32_t* owner_generations,
                                     uint32_t owner_capacity,
                                     timer_entry_t* timers,
                                     uint32_t* generations,
                                     uint32_t capacity,
                                     uint32_t frequency) {
    kmemset(owners, 0, sizeof(timer_owner_entry_t) * owner_capacity);
    kmemset(owner_generations, 0, sizeof(uint32_t) * owner_capacity);
    kmemset(timers, 0, sizeof(timer_entry_t) * capacity);
    kmemset(generations, 0, sizeof(uint32_t) * capacity);
    kmemset(service, 0, sizeof(*service));
    service->owners = owners;
    service->owner_generations = owner_generations;
    service->owner_capacity = owner_capacity;
    service->timers = timers;
    service->timer_generations = generations;
    service->timer_capacity = capacity;
    service->frequency = frequency;
    service->initialized = 1U;
    service->stats.initialized = 1U;
    service->stats.capacity = capacity;
    service->stats.owner_capacity = owner_capacity;
    service->stats.frequency = frequency;
}

static int timer_milliseconds_to_ticks(const timer_service_t* service,
                                       uint32_t milliseconds,
                                       uint32_t* out_ticks) {
    uint32_t whole_seconds;
    uint32_t remaining_ms;
    uint32_t whole_ticks;
    uint32_t partial_ticks;

    if (!service || !out_ticks || !milliseconds || !service->frequency) {
        return timer_result_code(ERR_INVALID);
    }
    whole_seconds = milliseconds / TIMER_MILLISECONDS_PER_SECOND;
    remaining_ms = milliseconds % TIMER_MILLISECONDS_PER_SECOND;
    if (whole_seconds > TIMER_MAX_INTERVAL_TICKS / service->frequency) {
        return timer_result_code(ERR_INVALID);
    }
    whole_ticks = whole_seconds * service->frequency;
    partial_ticks = (remaining_ms * service->frequency +
                     TIMER_MILLISECONDS_PER_SECOND - 1U) /
                    TIMER_MILLISECONDS_PER_SECOND;
    if (whole_ticks > TIMER_MAX_INTERVAL_TICKS - partial_ticks) {
        return timer_result_code(ERR_INVALID);
    }
    *out_ticks = whole_ticks + partial_ticks;
    if (!*out_ticks) *out_ticks = 1U;
    return OK;
}

static int timer_service_owner_create(timer_service_t* service,
                                      const char* name,
                                      timer_owner_handle_t* out_owner) {
    int32_t free_index = -1;
    uint32_t generation;

    if (!service || !service->initialized) {
        return timer_service_error(service, ERR_STATE);
    }
    if (!out_owner || !name) return timer_service_error(service, ERR_NULL);
    *out_owner = 0U;
    if (!timer_name_is_valid(name)) {
        return timer_service_error(service, ERR_INVALID);
    }
    for (uint32_t index = 0U; index < service->owner_capacity; index++) {
        if (!service->owners[index].active) {
            free_index = (int32_t)index;
            break;
        }
    }
    if (free_index < 0) return timer_result_code(ERR_OVERFLOW);
    generation = timer_next_generation(
        service->owner_generations[free_index]);
    service->owner_generations[free_index] = generation;
    service->owners[free_index].active = 1U;
    service->owners[free_index].generation = generation;
    timer_copy_name(service->owners[free_index].name, name);
    service->stats.owner_occupancy++;
    service->stats.owners_created++;
    *out_owner = timer_make_handle((uint32_t)free_index, generation);
    return OK;
}

static int timer_service_owner_destroy(timer_service_t* service,
                                       timer_owner_handle_t owner) {
    int32_t owner_index;

    if (!service || !service->initialized) {
        return timer_service_error(service, ERR_STATE);
    }
    owner_index = timer_find_owner(service, owner);
    if (owner_index < 0) return timer_service_error(service, ERR_INVALID);
    for (uint32_t index = 0U; index < service->timer_capacity; index++) {
        timer_entry_t* timer = &service->timers[index];

        if (!timer->active || timer->owner != owner) continue;
        if (timer->state != TIMER_STATE_IDLE) {
            service->stats.cancellations++;
        }
        kmemset(timer, 0, sizeof(*timer));
        service->stats.occupancy--;
        service->stats.timers_destroyed++;
    }
    kmemset(&service->owners[owner_index], 0,
            sizeof(service->owners[owner_index]));
    service->stats.owner_occupancy--;
    service->stats.owners_destroyed++;
    return OK;
}

static int timer_service_create(timer_service_t* service,
                                timer_owner_handle_t owner,
                                const char* name,
                                timer_callback_fn callback, void* context,
                                timer_handle_t* out_timer) {
    int32_t free_index = -1;
    uint32_t generation;

    if (!service || !service->initialized) {
        return timer_service_error(service, ERR_STATE);
    }
    if (!name || !callback || !out_timer) {
        return timer_service_error(service, ERR_NULL);
    }
    *out_timer = 0U;
    if (!timer_name_is_valid(name) || timer_find_owner(service, owner) < 0) {
        return timer_service_error(service, ERR_INVALID);
    }
    for (uint32_t index = 0U; index < service->timer_capacity; index++) {
        if (!service->timers[index].active) {
            free_index = (int32_t)index;
            break;
        }
    }
    if (free_index < 0) return timer_result_code(ERR_OVERFLOW);
    generation = timer_next_generation(service->timer_generations[free_index]);
    service->timer_generations[free_index] = generation;
    kmemset(&service->timers[free_index], 0,
            sizeof(service->timers[free_index]));
    service->timers[free_index].active = 1U;
    service->timers[free_index].generation = generation;
    service->timers[free_index].owner = owner;
    service->timers[free_index].callback = callback;
    service->timers[free_index].context = context;
    service->timers[free_index].state = TIMER_STATE_IDLE;
    service->timers[free_index].mode = TIMER_MODE_ONE_SHOT;
    timer_copy_name(service->timers[free_index].name, name);
    service->stats.occupancy++;
    service->stats.timers_created++;
    if (service->stats.occupancy > service->stats.high_watermark) {
        service->stats.high_watermark = service->stats.occupancy;
    }
    *out_timer = timer_make_handle((uint32_t)free_index, generation);
    return OK;
}

static int timer_service_start(timer_service_t* service,
                               timer_handle_t handle,
                               uint32_t milliseconds,
                               timer_mode_t mode) {
    uint32_t interval_ticks;
    int32_t index;
    int result;

    if (!service || !service->initialized) {
        return timer_service_error(service, ERR_STATE);
    }
    if (mode != TIMER_MODE_ONE_SHOT && mode != TIMER_MODE_PERIODIC) {
        return timer_service_error(service, ERR_INVALID);
    }
    index = timer_find_entry(service, handle);
    if (index < 0) return timer_service_error(service, ERR_INVALID);
    if (service->timers[index].state != TIMER_STATE_IDLE) {
        return timer_service_error(service, ERR_STATE);
    }
    result = timer_milliseconds_to_ticks(service, milliseconds,
                                         &interval_ticks);
    if (result != OK) return timer_service_error(service, result);
    service->timers[index].mode = mode;
    service->timers[index].state = TIMER_STATE_ARMED;
    service->timers[index].deadline_tick =
        service->current_tick + interval_ticks;
    service->timers[index].period_ticks =
        mode == TIMER_MODE_PERIODIC ? interval_ticks : 0U;
    service->timers[index].last_error = OK;
    service->stats.timers_started++;
    return OK;
}

static int timer_service_cancel(timer_service_t* service,
                                timer_handle_t handle) {
    int32_t index;

    if (!service || !service->initialized) {
        return timer_service_error(service, ERR_STATE);
    }
    index = timer_find_entry(service, handle);
    if (index < 0) return timer_service_error(service, ERR_INVALID);
    if (service->timers[index].state == TIMER_STATE_IDLE) return OK;
    service->timers[index].state = TIMER_STATE_IDLE;
    service->timers[index].deadline_tick = 0U;
    service->stats.cancellations++;
    return OK;
}

static int timer_service_destroy(timer_service_t* service,
                                 timer_handle_t handle) {
    int32_t index;

    if (!service || !service->initialized) {
        return timer_service_error(service, ERR_STATE);
    }
    index = timer_find_entry(service, handle);
    if (index < 0) return timer_service_error(service, ERR_INVALID);
    if (service->timers[index].state != TIMER_STATE_IDLE) {
        service->stats.cancellations++;
    }
    kmemset(&service->timers[index], 0, sizeof(service->timers[index]));
    service->stats.occupancy--;
    service->stats.timers_destroyed++;
    return OK;
}

static void timer_fill_info(const timer_service_t* service, uint32_t index,
                            timer_info_t* output) {
    const timer_entry_t* timer = &service->timers[index];
    int32_t owner_index = timer_find_owner(service, timer->owner);

    kmemset(output, 0, sizeof(*output));
    output->handle = timer_make_handle(index, timer->generation);
    output->owner = timer->owner;
    if (owner_index >= 0) {
        timer_copy_name(output->owner_name,
                        service->owners[owner_index].name);
    }
    timer_copy_name(output->name, timer->name);
    output->mode = timer->mode;
    output->state = timer->state;
    output->deadline_tick = timer->deadline_tick;
    output->period_ticks = timer->period_ticks;
    output->executions = timer->executions;
    output->delayed_callbacks = timer->delayed_callbacks;
    output->missed_periods = timer->missed_periods;
    output->last_lateness_ticks = timer->last_lateness_ticks;
    output->last_error = timer->last_error;
}

static int timer_deadline_reached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

static uint32_t timer_service_mark_expired(timer_service_t* service) {
    uint32_t now;
    uint32_t expired = 0U;

    if (!service || !service->initialized) return 0U;
    now = service->current_tick;

    for (uint32_t index = 0U; index < service->timer_capacity; index++) {
        timer_entry_t* timer = &service->timers[index];

        if (!timer->active || timer->state != TIMER_STATE_ARMED ||
            !timer_deadline_reached(now, timer->deadline_tick)) continue;
        timer->state = TIMER_STATE_PENDING;
        service->stats.expirations++;
        expired++;
    }
    return expired;
}

static int timer_service_prepare_dispatch(timer_service_t* service,
                                          timer_dispatch_event_t* event) {
    uint32_t now = service->current_tick;

    for (uint32_t index = 0U; index < service->timer_capacity; index++) {
        timer_entry_t* timer = &service->timers[index];
        uint32_t lateness;

        if (!timer->active || timer->state != TIMER_STATE_PENDING) continue;
        lateness = now - timer->deadline_tick;
        timer->last_lateness_ticks = lateness;
        if (lateness) {
            timer->delayed_callbacks++;
            service->stats.delayed_callbacks++;
        }
        if (timer->mode == TIMER_MODE_PERIODIC) {
            uint32_t periods = lateness / timer->period_ticks + 1U;
            uint32_t missed = periods - 1U;

            timer->deadline_tick += periods * timer->period_ticks;
            timer->state = TIMER_STATE_ARMED;
            timer->missed_periods += missed;
            service->stats.missed_periods += missed;
        } else {
            timer->deadline_tick = 0U;
            timer->state = TIMER_STATE_IDLE;
        }
        timer->executions++;
        event->index = index;
        event->generation = timer->generation;
        event->handle = timer_make_handle(index, timer->generation);
        event->callback = timer->callback;
        event->context = timer->context;
        return 1;
    }
    return 0;
}

static void timer_service_complete_dispatch(timer_service_t* service,
                                            const timer_dispatch_event_t* event,
                                            int callback_result) {
    timer_entry_t* timer = &service->timers[event->index];

    service->stats.callbacks++;
    if (callback_result != OK) service->stats.callback_errors++;
    if (timer->active && timer->generation == event->generation) {
        timer->last_error = callback_result;
    }
}

static int timer_service_dispatch_for_test(timer_service_t* service,
                                           uint32_t budget,
                                           uint32_t* out_dispatched) {
    uint32_t dispatched = 0U;
    int first_error = OK;

    while (dispatched < budget) {
        timer_dispatch_event_t event;
        int callback_result;

        if (!timer_service_prepare_dispatch(service, &event)) break;
        callback_result = event.callback(event.handle, event.context);
        timer_service_complete_dispatch(service, &event, callback_result);
        if (first_error == OK && callback_result != OK) {
            first_error = callback_result;
        }
        dispatched++;
    }
    if (out_dispatched) *out_dispatched = dispatched;
    return first_error;
}

static int timer_service_validate(const timer_service_t* service) {
    uint32_t owner_count = 0U;
    uint32_t timer_count = 0U;

    if (!service || !service->initialized || !service->frequency ||
        !service->owners || !service->timers ||
        !service->owner_generations || !service->timer_generations ||
        !service->owner_capacity || !service->timer_capacity) {
        return timer_result_code(ERR_STATE);
    }
    if (!service->stats.initialized ||
        service->stats.frequency != service->frequency ||
        service->stats.capacity != service->timer_capacity ||
        service->stats.owner_capacity != service->owner_capacity) {
        return timer_result_code(ERR_STATE);
    }
    for (uint32_t index = 0U; index < service->owner_capacity; index++) {
        const timer_owner_entry_t* owner = &service->owners[index];

        if (!owner->active) continue;
        owner_count++;
        if (!owner->generation || !timer_name_is_valid(owner->name) ||
            service->owner_generations[index] != owner->generation) {
            return timer_result_code(ERR_STATE);
        }
    }
    for (uint32_t index = 0U; index < service->timer_capacity; index++) {
        const timer_entry_t* timer = &service->timers[index];

        if (!timer->active) continue;
        timer_count++;
        if (!timer->generation || !timer->callback ||
            !timer_name_is_valid(timer->name) ||
            service->timer_generations[index] != timer->generation ||
            timer_find_owner(service, timer->owner) < 0 ||
            timer->state > TIMER_STATE_PENDING ||
            timer->mode > TIMER_MODE_PERIODIC ||
            (timer->state == TIMER_STATE_IDLE && timer->deadline_tick) ||
            (timer->mode == TIMER_MODE_ONE_SHOT && timer->period_ticks) ||
            (timer->mode == TIMER_MODE_PERIODIC &&
             timer->state != TIMER_STATE_IDLE && !timer->period_ticks)) {
            return timer_result_code(ERR_STATE);
        }
    }
    if (owner_count != service->stats.owner_occupancy ||
        timer_count != service->stats.occupancy ||
        owner_count > service->owner_capacity ||
        timer_count > service->timer_capacity ||
        service->stats.high_watermark < timer_count ||
        service->stats.high_watermark > service->timer_capacity) {
        return timer_result_code(ERR_STATE);
    }
    return OK;
}

static int timer_report_error(int result, const char* message) {
    if (result != OK) LOG_ERROR_CODE("TIMER", result, message);
    return result;
}

int timer_init(uint32_t frequency) {
    uint32_t divisor;
    uint32_t flags;
    int result;

    LOG_INFO("TIMER", "Inicializando timer");
    if (timer_service.initialized) {
        LOG_WARN("TIMER", "Timer ja estava inicializado");
        LOG_INFO("TIMER", "Timer inicializado com sucesso");
        return OK;
    }
    if (!frequency || frequency > TIMER_PIT_INPUT_FREQUENCY) {
        result = ERR_INVALID;
        LOG_ERROR_CODE("TIMER", ERR_INVALID, "Frequencia PIT invalida");
        return result;
    }
    divisor = TIMER_PIT_INPUT_FREQUENCY / frequency;
    if (!divisor || divisor > TIMER_PIT_MAX_DIVISOR) {
        result = ERR_INVALID;
        LOG_ERROR_CODE("TIMER", ERR_INVALID, "Divisor PIT invalido");
        return result;
    }
    flags = timer_suspend_interrupts();
    result = idt_register_handler(TIMER_IRQ_VECTOR, timer_handler);
    if (result != OK) {
        timer_restore_interrupts(flags);
        LOG_ERROR_CODE("TIMER", result, "Falha ao registrar IRQ do timer");
        return result;
    }
    result = idt_unmask_irq(TIMER_IRQ_LINE);
    if (result != OK) {
        timer_restore_interrupts(flags);
        LOG_ERROR_CODE("TIMER", result, "Falha ao habilitar IRQ do timer");
        return result;
    }
    timer_service_initialize(&timer_service, timer_owners,
                             timer_owner_generations, TIMER_OWNER_CAPACITY,
                             timer_entries, timer_generations, TIMER_CAPACITY,
                             frequency);
    timer_pending_notifier = 0;
    timer_pending_notifier_context = 0;
    outb(TIMER_PIT_COMMAND_PORT, TIMER_PIT_MODE_COMMAND);
    outb(TIMER_PIT_CHANNEL_ZERO_PORT, (uint8_t)(divisor & 0xFFU));
    outb(TIMER_PIT_CHANNEL_ZERO_PORT,
         (uint8_t)((divisor >> 8U) & 0xFFU));
    timer_restore_interrupts(flags);
    LOG_INFO("TIMER", "Timer inicializado com sucesso");
    return OK;
}

void timer_handler(registers_t* regs) {
    int interrupted_user = regs && ((regs->cs & 0x03U) == 0x03U);
    uint32_t expired;

    timer_service.current_tick++;
    expired = timer_service_mark_expired(&timer_service);
    if (expired && timer_pending_notifier) {
        timer_pending_notifier(timer_pending_notifier_context);
    }
    scheduler_tick();
    thread_scheduler_tick();

    outb(TIMER_PIC_COMMAND_PORT, TIMER_PIC_EOI);

    if (process_get_current() && interrupted_user) {
        scheduler_preempt_user();
    }
}

uint32_t timer_get_ticks(void) {
    return timer_service.current_tick;
}

uint32_t timer_get_frequency(void) {
    return timer_service.frequency;
}

int timer_set_pending_notifier(timer_pending_notifier_t notifier,
                               void* context) {
    uint32_t flags;

    if (!timer_service.initialized) {
        LOG_ERROR("TIMER", "Notificador antes da inicializacao do timer");
        return ERR_STATE;
    }
    flags = timer_suspend_interrupts();
    timer_pending_notifier = notifier;
    timer_pending_notifier_context = context;
    timer_restore_interrupts(flags);
    return OK;
}

int timer_owner_create(const char* name, timer_owner_handle_t* out_owner) {
    uint32_t flags = timer_suspend_interrupts();
    int result = timer_service_owner_create(&timer_service, name, out_owner);

    timer_restore_interrupts(flags);
    return timer_report_error(result, "Falha ao criar proprietario de timer");
}

int timer_owner_destroy(timer_owner_handle_t owner) {
    uint32_t flags = timer_suspend_interrupts();
    int result = timer_service_owner_destroy(&timer_service, owner);

    timer_restore_interrupts(flags);
    return timer_report_error(result, "Falha ao destruir proprietario de timer");
}

int timer_create(timer_owner_handle_t owner, const char* name,
                 timer_callback_fn callback, void* context,
                 timer_handle_t* out_timer) {
    uint32_t flags = timer_suspend_interrupts();
    int result = timer_service_create(&timer_service, owner, name, callback,
                                      context, out_timer);

    timer_restore_interrupts(flags);
    return timer_report_error(result, "Falha ao criar timer");
}

int timer_start_once(timer_handle_t timer, uint32_t milliseconds) {
    uint32_t flags = timer_suspend_interrupts();
    int result = timer_service_start(&timer_service, timer, milliseconds,
                                     TIMER_MODE_ONE_SHOT);

    timer_restore_interrupts(flags);
    return timer_report_error(result, "Falha ao iniciar timer one-shot");
}

int timer_start_periodic(timer_handle_t timer, uint32_t milliseconds) {
    uint32_t flags = timer_suspend_interrupts();
    int result = timer_service_start(&timer_service, timer, milliseconds,
                                     TIMER_MODE_PERIODIC);

    timer_restore_interrupts(flags);
    return timer_report_error(result, "Falha ao iniciar timer periodico");
}

int timer_cancel(timer_handle_t timer) {
    uint32_t flags = timer_suspend_interrupts();
    int result = timer_service_cancel(&timer_service, timer);

    timer_restore_interrupts(flags);
    return timer_report_error(result, "Falha ao cancelar timer");
}

int timer_get_info(timer_handle_t timer, timer_info_t* out_info) {
    uint32_t flags;
    int32_t index;
    int result = OK;

    if (!out_info) {
        timer_note_invalid_operation();
        return timer_report_error(ERR_NULL, "Destino nulo ao consultar timer");
    }
    flags = timer_suspend_interrupts();
    if (!timer_service.initialized) {
        result = timer_service_error(&timer_service, ERR_STATE);
    } else {
        index = timer_find_entry(&timer_service, timer);
        if (index < 0) result = timer_service_error(&timer_service, ERR_INVALID);
        else timer_fill_info(&timer_service, (uint32_t)index, out_info);
    }
    timer_restore_interrupts(flags);
    return timer_report_error(result, "Falha ao consultar timer");
}

int timer_destroy(timer_handle_t timer) {
    uint32_t flags = timer_suspend_interrupts();
    int result = timer_service_destroy(&timer_service, timer);

    timer_restore_interrupts(flags);
    return timer_report_error(result, "Falha ao destruir timer");
}

int timer_get_stats(timer_stats_t* out_stats) {
    uint32_t flags;

    if (!out_stats) {
        timer_note_invalid_operation();
        return timer_report_error(ERR_NULL,
                                  "Destino nulo para estatisticas de timer");
    }
    flags = timer_suspend_interrupts();
    if (!timer_service.initialized) {
        timer_service_error(&timer_service, ERR_STATE);
        timer_restore_interrupts(flags);
        return timer_report_error(ERR_STATE, "Servico de timer indisponivel");
    }
    *out_stats = timer_service.stats;
    out_stats->current_tick = timer_service.current_tick;
    out_stats->frequency = timer_service.frequency;
    out_stats->initialized = timer_service.initialized;
    out_stats->armed = 0U;
    out_stats->pending = 0U;
    for (uint32_t index = 0U; index < timer_service.timer_capacity; index++) {
        if (!timer_service.timers[index].active) continue;
        if (timer_service.timers[index].state == TIMER_STATE_ARMED) {
            out_stats->armed++;
        } else if (timer_service.timers[index].state == TIMER_STATE_PENDING) {
            out_stats->pending++;
        }
    }
    timer_restore_interrupts(flags);
    return OK;
}

int timer_copy_active(timer_info_t* output, uint32_t max_timers,
                      uint32_t* out_count) {
    uint32_t flags;
    uint32_t count = 0U;
    int result = OK;

    if (!output || !out_count) {
        timer_note_invalid_operation();
        return timer_report_error(ERR_NULL,
                                  "Destino nulo ao copiar timers ativos");
    }
    *out_count = 0U;
    if (!max_timers) {
        timer_note_invalid_operation();
        return timer_report_error(ERR_INVALID,
                                  "Limite invalido ao copiar timers ativos");
    }
    flags = timer_suspend_interrupts();
    if (!timer_service.initialized) {
        result = timer_service_error(&timer_service, ERR_STATE);
    } else {
        for (uint32_t index = 0U; index < timer_service.timer_capacity &&
                                count < max_timers; index++) {
            if (!timer_service.timers[index].active) continue;
            timer_fill_info(&timer_service, index, &output[count]);
            count++;
        }
        *out_count = count;
    }
    timer_restore_interrupts(flags);
    return timer_report_error(result, "Falha ao copiar timers ativos");
}

int timer_dispatch_pending(uint32_t budget, uint32_t* out_dispatched) {
    uint32_t dispatched = 0U;
    int first_error = OK;

    if (!out_dispatched) {
        timer_note_invalid_operation();
        return timer_report_error(ERR_NULL,
                                  "Destino nulo no despacho de timers");
    }
    *out_dispatched = 0U;
    if (!budget) {
        timer_note_invalid_operation();
        return timer_report_error(ERR_INVALID,
                                  "Orcamento invalido no despacho de timers");
    }
    while (dispatched < budget) {
        timer_dispatch_event_t event;
        uint32_t flags = timer_suspend_interrupts();
        int has_event;
        int callback_result;

        if (!timer_service.initialized) {
            timer_service_error(&timer_service, ERR_STATE);
            timer_restore_interrupts(flags);
            return timer_report_error(ERR_STATE,
                                      "Despacho antes da inicializacao");
        }
        has_event = timer_service_prepare_dispatch(&timer_service, &event);
        timer_restore_interrupts(flags);
        if (!has_event) break;
        callback_result = event.callback(event.handle, event.context);
        flags = timer_suspend_interrupts();
        timer_service_complete_dispatch(&timer_service, &event,
                                        callback_result);
        timer_restore_interrupts(flags);
        if (callback_result != OK) {
            LOG_ERROR_CODE("TIMER", callback_result,
                           "Callback de timer retornou erro");
            if (first_error == OK) first_error = callback_result;
        }
        dispatched++;
    }
    *out_dispatched = dispatched;
    return first_error;
}

int timer_validate_state(void) {
    uint32_t flags = timer_suspend_interrupts();
    int result = timer_service_validate(&timer_service);

    timer_restore_interrupts(flags);
    return timer_report_error(result, "Estado interno do timer inconsistente");
}

const char* timer_state_name(timer_state_t state) {
    if (state == TIMER_STATE_IDLE) return "IDLE";
    if (state == TIMER_STATE_ARMED) return "ARMED";
    if (state == TIMER_STATE_PENDING) return "PENDING";
    return "INVALID";
}

const char* timer_mode_name(timer_mode_t mode) {
    if (mode == TIMER_MODE_ONE_SHOT) return "ONE-SHOT";
    if (mode == TIMER_MODE_PERIODIC) return "PERIODIC";
    return "INVALID";
}

static int timer_test_callback(timer_handle_t handle, void* context) {
    timer_test_callback_state_t* state =
        (timer_test_callback_state_t*)context;

    if (!state) return timer_result_code(ERR_NULL);
    state->calls++;
    state->last_handle = handle;
    return state->result;
}

static int timer_test_fixture_init(timer_test_fixture_t* fixture) {
    kmemset(fixture, 0, sizeof(*fixture));
    timer_service_initialize(&fixture->service, fixture->owners,
                             fixture->owner_generations,
                             TIMER_PRIVATE_OWNER_CAPACITY,
                             fixture->timers, fixture->timer_generations,
                             TIMER_PRIVATE_TEST_CAPACITY, 50U);
    return timer_service_owner_create(&fixture->service, "TEST",
                                      &fixture->owner);
}

static int timer_test_create(timer_test_fixture_t* fixture,
                             timer_test_callback_state_t* callback_state,
                             const char* name, timer_handle_t* out_timer) {
    return timer_service_create(&fixture->service, fixture->owner, name,
                                timer_test_callback, callback_state,
                                out_timer);
}

static uint8_t timer_test_conversion_and_limits(void) {
    timer_test_fixture_t fixture;
    uint32_t value;

    if (timer_test_fixture_init(&fixture) != OK) return 0U;
    if (timer_milliseconds_to_ticks(&fixture.service, 1U, &value) != OK ||
        value != 1U) return 0U;
    if (timer_milliseconds_to_ticks(&fixture.service, 20U, &value) != OK ||
        value != 1U) return 0U;
    if (timer_milliseconds_to_ticks(&fixture.service, 21U, &value) != OK ||
        value != 2U) return 0U;
    if (timer_milliseconds_to_ticks(&fixture.service, 0U, &value) !=
        ERR_INVALID) return 0U;
    fixture.service.frequency = TIMER_PIT_INPUT_FREQUENCY;
    return timer_milliseconds_to_ticks(&fixture.service, 0xFFFFFFFFU,
                                       &value) == ERR_INVALID;
}

static uint8_t timer_test_one_shot(void) {
    timer_test_fixture_t fixture;
    timer_test_callback_state_t callback = {0U, 0U, OK};
    timer_handle_t handle;
    uint32_t dispatched;

    if (timer_test_fixture_init(&fixture) != OK ||
        timer_test_create(&fixture, &callback, "ONCE", &handle) != OK ||
        timer_service_start(&fixture.service, handle, 20U,
                            TIMER_MODE_ONE_SHOT) != OK) return 0U;
    fixture.service.current_tick = 1U;
    timer_service_mark_expired(&fixture.service);
    if (timer_service_dispatch_for_test(&fixture.service, 1U,
                                        &dispatched) != OK) return 0U;
    timer_service_mark_expired(&fixture.service);
    timer_service_dispatch_for_test(&fixture.service, 1U, &dispatched);
    return callback.calls == 1U && callback.last_handle == handle &&
           fixture.timers[0].state == TIMER_STATE_IDLE &&
           fixture.service.stats.expirations == 1U;
}

static uint8_t timer_test_periodic_no_drift(void) {
    timer_test_fixture_t fixture;
    timer_test_callback_state_t callback = {0U, 0U, OK};
    timer_handle_t handle;
    uint32_t dispatched;

    if (timer_test_fixture_init(&fixture) != OK ||
        timer_test_create(&fixture, &callback, "PERIOD", &handle) != OK ||
        timer_service_start(&fixture.service, handle, 100U,
                            TIMER_MODE_PERIODIC) != OK) return 0U;
    fixture.service.current_tick = 5U;
    timer_service_mark_expired(&fixture.service);
    timer_service_dispatch_for_test(&fixture.service, 1U, &dispatched);
    if (fixture.timers[0].deadline_tick != 10U) return 0U;
    fixture.service.current_tick = 10U;
    timer_service_mark_expired(&fixture.service);
    timer_service_dispatch_for_test(&fixture.service, 1U, &dispatched);
    return callback.calls == 2U && fixture.timers[0].deadline_tick == 15U &&
           fixture.timers[0].missed_periods == 0U;
}

static uint8_t timer_test_periodic_coalescing(void) {
    timer_test_fixture_t fixture;
    timer_test_callback_state_t callback = {0U, 0U, OK};
    timer_handle_t handle;
    uint32_t dispatched;

    if (timer_test_fixture_init(&fixture) != OK ||
        timer_test_create(&fixture, &callback, "LATE", &handle) != OK ||
        timer_service_start(&fixture.service, handle, 100U,
                            TIMER_MODE_PERIODIC) != OK) return 0U;
    fixture.service.current_tick = 18U;
    timer_service_mark_expired(&fixture.service);
    timer_service_dispatch_for_test(&fixture.service, 4U, &dispatched);
    return dispatched == 1U && callback.calls == 1U &&
           fixture.timers[0].deadline_tick == 20U &&
           fixture.timers[0].last_lateness_ticks == 13U &&
           fixture.timers[0].missed_periods == 2U &&
           fixture.service.stats.missed_periods == 2U;
}

static uint8_t timer_test_cancel_armed(void) {
    timer_test_fixture_t fixture;
    timer_test_callback_state_t callback = {0U, 0U, OK};
    timer_handle_t handle;
    uint32_t dispatched;

    if (timer_test_fixture_init(&fixture) != OK ||
        timer_test_create(&fixture, &callback, "CANCELA", &handle) != OK ||
        timer_service_start(&fixture.service, handle, 20U,
                            TIMER_MODE_ONE_SHOT) != OK ||
        timer_service_cancel(&fixture.service, handle) != OK ||
        timer_service_cancel(&fixture.service, handle) != OK) return 0U;
    fixture.service.current_tick = 2U;
    timer_service_mark_expired(&fixture.service);
    timer_service_dispatch_for_test(&fixture.service, 1U, &dispatched);
    return callback.calls == 0U && fixture.service.stats.cancellations == 1U;
}

static uint8_t timer_test_cancel_pending(void) {
    timer_test_fixture_t fixture;
    timer_test_callback_state_t callback = {0U, 0U, OK};
    timer_handle_t handle;
    uint32_t dispatched;

    if (timer_test_fixture_init(&fixture) != OK ||
        timer_test_create(&fixture, &callback, "PENDING", &handle) != OK ||
        timer_service_start(&fixture.service, handle, 20U,
                            TIMER_MODE_ONE_SHOT) != OK) return 0U;
    fixture.service.current_tick = 1U;
    timer_service_mark_expired(&fixture.service);
    if (fixture.timers[0].state != TIMER_STATE_PENDING ||
        timer_service_cancel(&fixture.service, handle) != OK) return 0U;
    timer_service_dispatch_for_test(&fixture.service, 1U, &dispatched);
    return dispatched == 0U && callback.calls == 0U;
}

static uint8_t timer_test_owner_destruction(void) {
    timer_test_fixture_t fixture;
    timer_test_callback_state_t callback = {0U, 0U, OK};
    timer_handle_t first;
    timer_handle_t second;
    uint32_t dispatched;

    if (timer_test_fixture_init(&fixture) != OK ||
        timer_test_create(&fixture, &callback, "FIRST", &first) != OK ||
        timer_test_create(&fixture, &callback, "SECOND", &second) != OK ||
        timer_service_start(&fixture.service, first, 20U,
                            TIMER_MODE_ONE_SHOT) != OK ||
        timer_service_start(&fixture.service, second, 20U,
                            TIMER_MODE_ONE_SHOT) != OK) return 0U;
    fixture.service.current_tick = 1U;
    timer_service_mark_expired(&fixture.service);
    if (timer_service_owner_destroy(&fixture.service, fixture.owner) != OK) {
        return 0U;
    }
    timer_service_dispatch_for_test(&fixture.service, 4U, &dispatched);
    return !dispatched && !callback.calls &&
           fixture.service.stats.occupancy == 0U &&
           timer_find_entry(&fixture.service, first) < 0 &&
           timer_find_entry(&fixture.service, second) < 0;
}

static uint8_t timer_test_stale_handles(void) {
    timer_test_fixture_t fixture;
    timer_test_callback_state_t callback = {0U, 0U, OK};
    timer_handle_t stale;
    timer_handle_t current;
    timer_handle_t wrapped;

    if (timer_test_fixture_init(&fixture) != OK ||
        timer_test_create(&fixture, &callback, "STALE", &stale) != OK ||
        timer_service_destroy(&fixture.service, stale) != OK ||
        timer_test_create(&fixture, &callback, "CURRENT", &current) != OK ||
        stale == current ||
        timer_service_cancel(&fixture.service, stale) != ERR_INVALID ||
        timer_service_destroy(&fixture.service, current) != OK) return 0U;
    fixture.timer_generations[0] = TIMER_HANDLE_GENERATION_MAX;
    if (timer_test_create(&fixture, &callback, "WRAP", &wrapped) != OK) {
        return 0U;
    }
    return (wrapped >> TIMER_HANDLE_GENERATION_SHIFT) == 1U;
}

static uint8_t timer_test_tick_wrap(void) {
    timer_test_fixture_t fixture;
    timer_test_callback_state_t callback = {0U, 0U, OK};
    timer_handle_t handle;

    if (timer_test_fixture_init(&fixture) != OK ||
        timer_test_create(&fixture, &callback, "WRAPTICK", &handle) != OK) {
        return 0U;
    }
    fixture.service.current_tick = 0xFFFFFFF0U;
    if (timer_service_start(&fixture.service, handle, 640U,
                            TIMER_MODE_ONE_SHOT) != OK ||
        fixture.timers[0].deadline_tick != 0x10U) return 0U;
    fixture.service.current_tick = 0x0FU;
    timer_service_mark_expired(&fixture.service);
    if (fixture.timers[0].state != TIMER_STATE_ARMED) return 0U;
    fixture.service.current_tick = 0x10U;
    timer_service_mark_expired(&fixture.service);
    return fixture.timers[0].state == TIMER_STATE_PENDING;
}

static uint8_t timer_test_capacity(void) {
    timer_test_fixture_t fixture;
    timer_test_callback_state_t callback = {0U, 0U, OK};
    timer_handle_t handles[TIMER_PRIVATE_TEST_CAPACITY];
    timer_handle_t extra;

    if (timer_test_fixture_init(&fixture) != OK) return 0U;
    for (uint32_t index = 0U; index < TIMER_PRIVATE_TEST_CAPACITY; index++) {
        if (timer_test_create(&fixture, &callback, "CAP", &handles[index]) !=
            OK) return 0U;
    }
    return timer_test_create(&fixture, &callback, "EXTRA", &extra) ==
               ERR_OVERFLOW &&
           fixture.service.stats.occupancy == TIMER_PRIVATE_TEST_CAPACITY &&
           fixture.service.stats.high_watermark == TIMER_PRIVATE_TEST_CAPACITY;
}

static uint8_t timer_test_callback_errors(void) {
    timer_test_fixture_t fixture;
    timer_test_callback_state_t callback = {0U, 0U, ERR_STATE};
    timer_handle_t handle;
    uint32_t dispatched;

    if (timer_test_fixture_init(&fixture) != OK ||
        timer_test_create(&fixture, &callback, "FAIL", &handle) != OK ||
        timer_service_start(&fixture.service, handle, 20U,
                            TIMER_MODE_ONE_SHOT) != OK) return 0U;
    fixture.service.current_tick = 1U;
    timer_service_mark_expired(&fixture.service);
    return timer_service_dispatch_for_test(&fixture.service, 1U,
                                           &dispatched) == ERR_STATE &&
           dispatched == 1U && callback.calls == 1U &&
           fixture.service.stats.callback_errors == 1U &&
           fixture.timers[0].last_error == ERR_STATE;
}

static uint8_t timer_test_invariants(void) {
    timer_test_fixture_t fixture;
    timer_test_callback_state_t callback = {0U, 0U, OK};
    timer_handle_t handle;

    if (timer_test_fixture_init(&fixture) != OK ||
        timer_test_create(&fixture, &callback, "VALID", &handle) != OK ||
        timer_service_start(&fixture.service, handle, 40U,
                            TIMER_MODE_PERIODIC) != OK ||
        timer_service_validate(&fixture.service) != OK) return 0U;
    fixture.timers[0].owner = 0U;
    return timer_service_validate(&fixture.service) == ERR_STATE;
}

static void timer_test_count(timer_self_test_result_t* result,
                             uint8_t passed) {
    if (passed) result->passed++;
    else result->failed++;
}

int timer_self_test(timer_self_test_result_t* out_result) {
    if (!out_result) {
        timer_note_invalid_operation();
        return timer_report_error(ERR_NULL,
                                  "Destino nulo no autoteste de timer");
    }
    kmemset(out_result, 0, sizeof(*out_result));
    out_result->conversion_and_limits = timer_test_conversion_and_limits();
    out_result->one_shot = timer_test_one_shot();
    out_result->periodic_no_drift = timer_test_periodic_no_drift();
    out_result->periodic_coalescing = timer_test_periodic_coalescing();
    out_result->cancel_armed = timer_test_cancel_armed();
    out_result->cancel_pending = timer_test_cancel_pending();
    out_result->owner_destruction = timer_test_owner_destruction();
    out_result->stale_handles = timer_test_stale_handles();
    out_result->tick_wrap = timer_test_tick_wrap();
    out_result->capacity = timer_test_capacity();
    out_result->callback_errors = timer_test_callback_errors();
    out_result->invariants = timer_test_invariants();
    timer_test_count(out_result, out_result->conversion_and_limits);
    timer_test_count(out_result, out_result->one_shot);
    timer_test_count(out_result, out_result->periodic_no_drift);
    timer_test_count(out_result, out_result->periodic_coalescing);
    timer_test_count(out_result, out_result->cancel_armed);
    timer_test_count(out_result, out_result->cancel_pending);
    timer_test_count(out_result, out_result->owner_destruction);
    timer_test_count(out_result, out_result->stale_handles);
    timer_test_count(out_result, out_result->tick_wrap);
    timer_test_count(out_result, out_result->capacity);
    timer_test_count(out_result, out_result->callback_errors);
    timer_test_count(out_result, out_result->invariants);
    if (out_result->failed) {
        return timer_report_error(ERR_STATE, "Autoteste de timer falhou");
    }
    return OK;
}
