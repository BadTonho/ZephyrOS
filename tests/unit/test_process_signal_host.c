#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "drivers/serial.h"
#include "memory/paging.h"
#include "process/process.h"
#include "process/signal.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U
#define SIGNAL_FIXTURE_USER_PID 10U
#define SIGNAL_FIXTURE_CHILD_PID 11U
#define SIGNAL_FIXTURE_CODE_SIZE 128U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static process_t signal_user;
static process_t signal_child;
static process_t* signal_current;

process_t* processes[MAX_PROCESSES];
uint32_t process_count;

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
    printf("ZCOV_BEGIN|case=host:process:signals|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:process:signals|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:process:signals|value=0x%08X\n",
           (uint32_t)result);
}

uint32_t timer_get_ticks(void) {
    return 100U;
}

uint32_t timer_get_frequency(void) {
    return 1000U;
}

uint8_t serial_is_ready(void) {
    return 1U;
}

uint32_t serial_write_text(const char* text, uint32_t length) {
    (void)text;
    return length;
}

uint32_t serial_flush(uint32_t budget) {
    (void)budget;
    return 0U;
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void video_newline(void) {
}

process_t* process_get_by_pid(uint32_t pid) {
    for (uint32_t index = 0U; index < MAX_PROCESSES; index++) {
        if (processes[index] && processes[index]->pid == pid) {
            return processes[index];
        }
    }
    return 0;
}

process_t* process_get_current(void) {
    return signal_current;
}

int process_is_user(const process_t* process) {
    return process && process->context.user_mode != 0U;
}

int process_terminate_user_signal(uint32_t pid, uint32_t signal_number,
                                  int faulted) {
    process_t* process = process_get_by_pid(pid);

    if (!process) return ERR_NOT_FOUND;
    process->state = PROCESS_STATE_ZOMBIE;
    process->termination_signal = signal_number;
    process->faulted = faulted ? 1U : 0U;
    process->pending_signals = 0U;
    process->blocked_signals = 0U;
    process->active_signal = 0U;
    process->signal_context_valid = 0U;
    process->cancel_pending = 0U;
    process->cancel_exit_code = 0U;
    return OK;
}

int process_prepare_user_termination(registers_t* regs) {
    (void)regs;
    return OK;
}

int process_apply_pending_cancel(registers_t* regs) {
    (void)regs;
    return OK;
}

int paging_copy_to_user(void* destination, const void* source,
                        uint32_t size) {
    if (!destination || !source || !size) return ERR_INVALID;
    return OK;
}

int wait_queue_remove(wait_queue_entry_t* entry, wait_reason_t reason) {
    (void)reason;
    if (!entry) return ERR_NULL;
    entry->linked = 0U;
    return OK;
}

static void fixture_reset(void) {
    kmemset(processes, 0, sizeof(processes));
    kmemset(&signal_user, 0, sizeof(signal_user));
    kmemset(&signal_child, 0, sizeof(signal_child));
    signal_user.pid = SIGNAL_FIXTURE_USER_PID;
    signal_user.state = PROCESS_STATE_READY;
    signal_user.context.user_mode = 1U;
    signal_user.user_code_size = SIGNAL_FIXTURE_CODE_SIZE;
    signal_child.pid = SIGNAL_FIXTURE_CHILD_PID;
    signal_child.state = PROCESS_STATE_READY;
    signal_child.context.user_mode = 1U;
    signal_child.user_code_size = SIGNAL_FIXTURE_CODE_SIZE;
    processes[0] = &signal_user;
    processes[1] = &signal_child;
    process_count = 2U;
    signal_current = &signal_user;
}

static int check_initialization(void) {
    process_signal_stats_t stats;

    if (process_signal_get_stats(NULL) != ERR_NULL) return 1;
    if (process_signal_validate_state() != ERR_STATE) return 2;
    if (process_signal_get_stats(&stats) != ERR_STATE || stats.initialized) {
        return 3;
    }
    if (process_signal_init() != OK || process_signal_init() != OK) return 4;
    if (process_signal_get_stats(&stats) != OK || !stats.initialized ||
        stats.last_error != OK) return 5;
    process_signal_process_created(SIGNAL_FIXTURE_USER_PID, 0U);
    process_signal_process_created(SIGNAL_FIXTURE_CHILD_PID,
                                   SIGNAL_FIXTURE_USER_PID);
    if (signal_user.parent_pid != 0U ||
        signal_child.parent_pid != SIGNAL_FIXTURE_USER_PID) return 6;
    if (process_signal_validate_state() != OK) return 7;
    if (!process_signal_name(APP_SIGNAL_INT) ||
        !process_signal_name(APP_SIGNAL_KILL) ||
        !process_signal_name(APP_SIGNAL_SEGV) ||
        !process_signal_name(APP_SIGNAL_TERM) ||
        !process_signal_name(APP_SIGNAL_CHLD) ||
        kstrcmp(process_signal_name(1U), "INVALID") != 0) return 8;
    return 0;
}

static int check_actions_and_masks(void) {
    app_signal_action_t action;
    app_signal_action_t old_action;
    uint32_t old_mask = 0U;

    kmemset(&action, 0, sizeof(action));
    if (process_signal_action(0U, &action, NULL) != ERR_INVALID) return 10;
    action.disposition = APP_SIGNAL_DISPOSITION_IGNORE;
    action.handler = 1U;
    if (process_signal_action(APP_SIGNAL_KILL, &action, NULL) !=
        ERR_UNAVAILABLE) return 11;
    action.disposition = 3U;
    action.handler = 0U;
    if (process_signal_action(APP_SIGNAL_INT, &action, NULL) != ERR_INVALID) {
        return 12;
    }
    action.disposition = APP_SIGNAL_DISPOSITION_HANDLER;
    action.handler = USER_CODE_BASE + SIGNAL_FIXTURE_CODE_SIZE;
    if (process_signal_action(APP_SIGNAL_INT, &action, NULL) != ERR_INVALID) {
        return 13;
    }
    action.handler = USER_CODE_BASE + 4U;
    action.mask = APP_SIGNAL_BIT(APP_SIGNAL_TERM);
    if (process_signal_action(APP_SIGNAL_INT, &action, &old_action) != OK ||
        old_action.disposition != APP_SIGNAL_DISPOSITION_DEFAULT) return 14;
    if (process_signal_mask(3U, 0U, NULL) != ERR_INVALID ||
        process_signal_mask(APP_SIGNAL_MASK_SET, 1U, NULL) != ERR_INVALID) {
        return 15;
    }
    if (process_signal_mask(APP_SIGNAL_MASK_SET,
                            APP_SIGNAL_BIT(APP_SIGNAL_KILL) |
                            APP_SIGNAL_BIT(APP_SIGNAL_TERM), &old_mask) != OK ||
        old_mask != 0U ||
        signal_user.blocked_signals != APP_SIGNAL_BIT(APP_SIGNAL_TERM)) {
        return 16;
    }
    if (process_signal_mask(APP_SIGNAL_MASK_UNBLOCK,
                            APP_SIGNAL_BIT(APP_SIGNAL_TERM), &old_mask) != OK ||
        old_mask != APP_SIGNAL_BIT(APP_SIGNAL_TERM) ||
        signal_user.blocked_signals != 0U) return 17;
    return 0;
}

static int check_delivery_and_send(void) {
    app_signal_action_t action;
    registers_t regs;
    process_signal_info_t info[2];
    uint32_t count = 0U;
    uint32_t old_pending;

    if (process_signal_send(SIGNAL_FIXTURE_USER_PID, 0U) != ERR_INVALID ||
        process_signal_send(999U, APP_SIGNAL_INT) != ERR_NOT_FOUND) return 20;
    if (process_signal_send(SIGNAL_FIXTURE_USER_PID, APP_SIGNAL_INT) != OK ||
        process_signal_send(SIGNAL_FIXTURE_USER_PID, APP_SIGNAL_INT) != OK ||
        signal_user.pending_signals != APP_SIGNAL_BIT(APP_SIGNAL_INT)) {
        return 21;
    }
    if (process_signal_send(SIGNAL_FIXTURE_USER_PID, APP_SIGNAL_CHLD) != OK) {
        return 22;
    }
    if (process_signal_raise(APP_SIGNAL_TERM) != OK ||
        !(signal_user.pending_signals & APP_SIGNAL_BIT(APP_SIGNAL_TERM))) {
        return 23;
    }
    if (process_signal_copy_info(NULL, 1U, &count) != ERR_NULL ||
        process_signal_copy_info(info, 1U, NULL) != ERR_NULL ||
        process_signal_copy_info(info, 2U, &count) != OK || count != 2U) {
        return 24;
    }
    if (info[0].pid != SIGNAL_FIXTURE_USER_PID ||
        info[0].pending != signal_user.pending_signals) return 25;
    old_pending = signal_user.pending_signals;
    signal_user.pending_signals = APP_SIGNAL_BIT(APP_SIGNAL_INT);
    signal_user.blocked_signals = 0U;
    signal_user.state = PROCESS_STATE_RUNNING;
    kmemset(&regs, 0, sizeof(regs));
    regs.cs = 3U;
    regs.eflags = 1U << 9U;
    regs.useresp = USER_STACK_TOP;
    if (process_signal_prepare_user_return(&regs) != OK ||
        !signal_user.signal_context_valid ||
        signal_user.active_signal != APP_SIGNAL_INT ||
        regs.eip != USER_CODE_BASE + 4U ||
        regs.eax != APP_SIGNAL_INT ||
        regs.useresp != USER_STACK_TOP - 8U) return 26;
    if (process_signal_return(&regs) != OK || signal_user.signal_context_valid ||
        signal_user.active_signal != 0U) return 27;
    action.disposition = APP_SIGNAL_DISPOSITION_IGNORE;
    action.handler = 0U;
    action.mask = 0U;
    if (process_signal_action(APP_SIGNAL_INT, &action, NULL) != OK) return 28;
    signal_user.pending_signals = APP_SIGNAL_BIT(APP_SIGNAL_INT);
    if (process_signal_prepare_user_return(&regs) != OK ||
        signal_user.pending_signals != 0U) return 29;
    action.disposition = APP_SIGNAL_DISPOSITION_DEFAULT;
    if (process_signal_action(APP_SIGNAL_INT, &action, NULL) != OK) return 30;
    signal_user.pending_signals = old_pending;
    signal_user.state = PROCESS_STATE_READY;
    if (process_signal_prepare_user_return(NULL) != ERR_NULL ||
        process_signal_return(NULL) != ERR_NULL ||
        process_signal_record_user_fault(NULL) != ERR_STATE) return 31;
    signal_user.pending_signals = APP_SIGNAL_BIT(APP_SIGNAL_TERM);
    signal_user.state = PROCESS_STATE_RUNNING;
    regs.cs = 3U;
    regs.eflags = 1U << 9U;
    regs.useresp = USER_STACK_TOP;
    if (process_signal_prepare_user_return(&regs) != OK ||
        signal_user.state != PROCESS_STATE_ZOMBIE ||
        signal_user.termination_signal != APP_SIGNAL_TERM ||
        process_signal_validate_state() != OK) return 32;
    process_signal_process_created(SIGNAL_FIXTURE_USER_PID, 0U);
    signal_user.state = PROCESS_STATE_READY;
    return 0;
}

static int check_lifecycle_and_self_test(void) {
    process_signal_self_test_t self_test;
    process_signal_stats_t stats;

    signal_child.state = PROCESS_STATE_ZOMBIE;
    process_signal_process_exited(SIGNAL_FIXTURE_CHILD_PID);
    if (!signal_child.signal_exit_notified ||
        signal_user.last_child_pid != SIGNAL_FIXTURE_CHILD_PID) return 40;
    process_signal_process_exited(SIGNAL_FIXTURE_CHILD_PID);
    process_signal_process_destroyed(SIGNAL_FIXTURE_USER_PID);
    if (signal_child.parent_pid != 0U) return 41;
    if (process_signal_get_stats(&stats) != OK || stats.sent == 0U ||
        stats.coalesced == 0U || stats.ignored == 0U ||
        stats.child_notifications == 0U) return 42;
    if (process_signal_self_test(NULL) != ERR_NULL ||
        process_signal_self_test(&self_test) != OK || self_test.lifecycle == 0U ||
        self_test.actions == 0U || self_test.blocking == 0U ||
        self_test.coalescing == 0U || self_test.fatal_rules == 0U ||
        self_test.child_notification == 0U || self_test.frame_rules == 0U ||
        self_test.invariants == 0U) return 43;
    if (process_signal_validate_state() != OK) return 44;
    return 0;
}

int main(void) {
    int result = 0;

    fixture_reset();
    coverage_active = 1U;
    log_init();
    if (!result) result = check_initialization();
    if (!result) result = check_actions_and_masks();
    if (!result) result = check_delivery_and_send();
    if (!result) result = check_lifecycle_and_self_test();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
