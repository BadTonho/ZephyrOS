#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "drivers/idt.h"
#include "core/timer.h"
#include "process/process.h"
#include "process/thread.h"
#include "drivers/serial.h"
#include "core/video.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

static void __attribute__((no_instrument_function)) coverage_record(
    void* function) {
    uintptr_t address = (uintptr_t)function;

    if (!coverage_active || !address) return;
    for (uint32_t index = 0U; index < coverage_count; index++) {
        if (coverage_addresses[index] == address) return;
    }
    if (coverage_count < HOST_COVERAGE_CAPACITY) {
        coverage_addresses[coverage_count++] = address;
    }
}

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(
    void* function, void* caller) {
    (void)caller;
    coverage_record(function);
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(
    void* function, void* caller) {
    (void)function;
    (void)caller;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:core:timer|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:timer|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:timer|value=0x%08X\n",
           (uint32_t)result);
}

static uint32_t notifier_calls;

int idt_register_handler(uint8_t vector, isr_handler_t handler) {
    return vector == 32U && handler ? OK : ERR_INVALID;
}

int idt_unmask_irq(uint8_t line) {
    return line == 0U ? OK : ERR_INVALID;
}

void scheduler_tick(void) {
}

void thread_scheduler_tick(void) {
}

process_t* process_get_current(void) {
    return 0;
}

void scheduler_preempt_user(void) {
}

uint8_t serial_is_ready(void) {
    return 1U;
}

uint32_t serial_write_text(const char* text, uint32_t length) {
    (void)text;
    return length;
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void video_newline(void) {
}

static void pending_notifier(void* context) {
    uint32_t* calls = (uint32_t*)context;

    if (calls) (*calls)++;
}

typedef struct {
    uint32_t calls;
    timer_handle_t last_handle;
    int result;
} timer_callback_state_t;

static int timer_callback(timer_handle_t handle, void* context) {
    timer_callback_state_t* state = (timer_callback_state_t*)context;

    if (!state) return ERR_NULL;
    state->calls++;
    state->last_handle = handle;
    return state->result;
}

static int check_timer(void) {
    timer_owner_handle_t owner;
    timer_handle_t once;
    timer_handle_t periodic;
    timer_handle_t failing;
    timer_handle_t temporary;
    timer_info_t info;
    timer_info_t infos[4];
    timer_stats_t stats;
    timer_callback_state_t once_state = {0U, 0U, OK};
    timer_callback_state_t periodic_state = {0U, 0U, OK};
    timer_callback_state_t failing_state = {0U, 0U, ERR_STATE};
    uint32_t dispatched = 0U;
    uint32_t count = 0U;

    if (timer_get_stats(&stats) != ERR_STATE ||
        timer_set_pending_notifier(NULL, NULL) != ERR_STATE ||
        timer_init(0U) != ERR_INVALID || timer_init(50U) != OK ||
        timer_init(50U) != OK || timer_validate_state() != OK) return 1;
    if (timer_get_frequency() != 50U || timer_get_ticks() != 0U) return 2;
    if (timer_owner_create(NULL, &owner) != ERR_NULL ||
        timer_owner_create("", &owner) != ERR_INVALID ||
        timer_owner_create("host", &owner) != OK) return 3;
    if (timer_set_pending_notifier(pending_notifier, &notifier_calls) != OK ||
        timer_create(owner, "", timer_callback, &once_state, &once) !=
            ERR_INVALID ||
        timer_create(0U, "once", timer_callback, &once_state, &once) !=
            ERR_INVALID ||
        timer_create(owner, "once", NULL, &once_state, &once) != ERR_NULL ||
        timer_create(owner, "once", timer_callback, &once_state, NULL) !=
            ERR_NULL ||
        timer_create(owner, "once", timer_callback, &once_state, &once) !=
            OK) return 4;
    if (timer_get_info(once, NULL) != ERR_NULL ||
        timer_get_info(0U, &info) != ERR_INVALID ||
        timer_get_info(once, &info) != OK || info.state != TIMER_STATE_IDLE ||
        info.mode != TIMER_MODE_ONE_SHOT) return 5;
    if (timer_copy_active(NULL, 1U, &count) != ERR_NULL ||
        timer_copy_active(infos, 0U, &count) != ERR_INVALID ||
        timer_copy_active(infos, 4U, &count) != OK || count != 1U) return 6;
    if (timer_start_once(once, 20U) != OK ||
        timer_start_once(once, 20U) != ERR_STATE) return 7;
    timer_handler(NULL);
    if (notifier_calls != 1U || timer_get_stats(&stats) != OK ||
        stats.pending != 1U || timer_dispatch_pending(0U, &dispatched) !=
            ERR_INVALID || timer_dispatch_pending(1U, NULL) != ERR_NULL ||
        timer_dispatch_pending(1U, &dispatched) != OK || dispatched != 1U ||
        once_state.calls != 1U || timer_get_info(once, &info) != OK ||
        info.state != TIMER_STATE_IDLE || info.executions != 1U) return 8;
    if (timer_start_once(once, 20U) != OK || timer_cancel(once) != OK ||
        timer_cancel(once) != OK || timer_destroy(once) != OK ||
        timer_destroy(once) != ERR_INVALID) return 9;
    if (timer_create(owner, "periodic", timer_callback, &periodic_state,
                     &periodic) != OK ||
        timer_start_periodic(periodic, 1000U) != OK) return 10;
    for (uint32_t tick = 0U; tick < 50U; tick++) timer_handler(NULL);
    if (timer_dispatch_pending(1U, &dispatched) != OK || dispatched != 1U ||
        periodic_state.calls != 1U || timer_get_info(periodic, &info) != OK ||
        info.state != TIMER_STATE_ARMED || info.period_ticks != 50U) return 11;
    if (timer_cancel(periodic) != OK || timer_destroy(periodic) != OK ||
        timer_create(owner, "failing", timer_callback, &failing_state,
                     &failing) != OK || timer_start_once(failing, 20U) != OK) {
        return 12;
    }
    timer_handler(NULL);
    if (timer_dispatch_pending(1U, &dispatched) != ERR_STATE ||
        dispatched != 1U || failing_state.calls != 1U ||
        timer_get_info(failing, &info) != OK || info.last_error != ERR_STATE) {
        return 13;
    }
    if (timer_create(owner, "temporary", timer_callback, &once_state,
                     &temporary) != OK || timer_start_once(temporary, 20U) !=
            OK || timer_owner_destroy(owner) != OK ||
        timer_get_info(failing, &info) != ERR_INVALID ||
        timer_get_info(temporary, &info) != ERR_INVALID ||
        timer_get_stats(&stats) != OK || stats.occupancy != 0U ||
        stats.owner_occupancy != 0U || timer_validate_state() != OK) return 14;
    if (!timer_state_name(TIMER_STATE_PENDING) ||
        !timer_mode_name(TIMER_MODE_PERIODIC) ||
        timer_state_name((timer_state_t)99) == NULL ||
        timer_mode_name((timer_mode_t)99) == NULL) return 15;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    log_init();
    result = check_timer();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
