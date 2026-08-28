#ifndef PROCESS_SIGNAL_H
#define PROCESS_SIGNAL_H

#include "types.h"
#include "core/app_api.h"
#include "drivers/idt.h"

#define PROCESS_SIGNAL_ACTION_COUNT 18U

typedef struct {
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t pending;
    uint32_t blocked;
    uint32_t active_signal;
    uint32_t last_delivered;
    uint32_t termination_signal;
    uint32_t last_child_pid;
    uint32_t delivered;
    uint32_t caught;
    uint32_t ignored;
} process_signal_info_t;

typedef struct {
    uint32_t sent;
    uint32_t coalesced;
    uint32_t delivered;
    uint32_t caught;
    uint32_t ignored;
    uint32_t terminated;
    uint32_t blocked;
    uint32_t woken;
    uint32_t rejected;
    uint32_t child_notifications;
    uint32_t user_faults;
    uint32_t frame_failures;
    uint32_t invariant_failures;
    int last_error;
    uint8_t initialized;
    uint32_t internal_failures;
} process_signal_stats_t;

typedef struct {
    uint8_t lifecycle;
    uint8_t actions;
    uint8_t blocking;
    uint8_t coalescing;
    uint8_t fatal_rules;
    uint8_t child_notification;
    uint8_t frame_rules;
    uint8_t invariants;
} process_signal_self_test_t;

int process_signal_init(void);
int process_signal_send(uint32_t pid, uint32_t signal_number);
int process_signal_raise(uint32_t signal_number);
int process_signal_action(uint32_t signal_number,
                          const app_signal_action_t* action,
                          app_signal_action_t* old_action);
int process_signal_mask(uint32_t operation, uint32_t mask,
                        uint32_t* old_mask);
int process_signal_prepare_user_return(registers_t* regs);
int process_signal_return(registers_t* regs);
int process_signal_record_user_fault(registers_t* regs);
void process_signal_process_created(uint32_t pid, uint32_t parent_pid);
void process_signal_process_exited(uint32_t pid);
void process_signal_process_destroyed(uint32_t pid);
int process_signal_copy_info(process_signal_info_t* output,
                             uint32_t max_entries, uint32_t* out_count);
int process_signal_get_stats(process_signal_stats_t* out_stats);
int process_signal_validate_state(void);
int process_signal_self_test(process_signal_self_test_t* out_test);
const char* process_signal_name(uint32_t signal_number);

#endif
