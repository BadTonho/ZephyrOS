#ifndef SHELL_JOB_H
#define SHELL_JOB_H

#include "types.h"

#define SHELL_JOB_COMMAND_SIZE 32U
#define SHELL_JOB_ARGUMENT_SIZE 256U
#define SHELL_JOB_PHASE_SIZE 32U

typedef enum {
    SHELL_JOB_STATE_IDLE = 0,
    SHELL_JOB_STATE_RUNNING,
    SHELL_JOB_STATE_CANCEL_REQUESTED,
    SHELL_JOB_STATE_SUCCEEDED,
    SHELL_JOB_STATE_FAILED,
    SHELL_JOB_STATE_CANCELLED
} shell_job_state_t;

typedef enum {
    SHELL_JOB_KIND_NONE = 0,
    SHELL_JOB_KIND_NETWORK,
    SHELL_JOB_KIND_PACKAGES,
    SHELL_JOB_KIND_CHECK,
    SHELL_JOB_KIND_INDEX
} shell_job_kind_t;

typedef enum {
    SHELL_JOB_STEP_PENDING = 0,
    SHELL_JOB_STEP_COMPLETE,
    SHELL_JOB_STEP_FAILED,
    SHELL_JOB_STEP_CANCELLED
} shell_job_step_result_t;

typedef struct shell_job_context shell_job_context_t;

typedef shell_job_step_result_t (*shell_job_step_fn)(
    shell_job_context_t* context);
typedef int (*shell_job_cancel_fn)(shell_job_context_t* context);
typedef void (*shell_job_finish_fn)(shell_job_context_t* context,
                                    shell_job_state_t state,
                                    int result);

typedef struct {
    const char* command;
    shell_job_kind_t kind;
    shell_job_step_fn step;
    shell_job_cancel_fn cancel;
    shell_job_finish_fn finish;
} shell_job_definition_t;

struct shell_job_context {
    char command[SHELL_JOB_COMMAND_SIZE];
    char arguments[SHELL_JOB_ARGUMENT_SIZE];
    char phase[SHELL_JOB_PHASE_SIZE];
    shell_job_kind_t kind;
    shell_job_state_t state;
    uint32_t progress;
    uint32_t total;
    uint32_t started_tick;
    uint32_t completed_ticks;
    uint32_t blocked_events;
    uint32_t cancel_requests;
    int last_error;
    uint8_t cancel_requested;
};

typedef struct {
    shell_job_state_t state;
    shell_job_kind_t kind;
    char command[SHELL_JOB_COMMAND_SIZE];
    char phase[SHELL_JOB_PHASE_SIZE];
    uint32_t progress;
    uint32_t total;
    uint32_t started_tick;
    uint32_t completed_ticks;
    uint32_t blocked_events;
    uint32_t cancel_requests;
    int last_error;
    uint8_t active;
} shell_job_status_t;

void shell_job_reset(void);
int shell_job_start(const shell_job_definition_t* definition,
                   const char* arguments);
void shell_job_poll(void);
void shell_job_pump_events(void);
void shell_job_handle_key(uint8_t scancode);
void shell_job_request_cancel(void);
int shell_job_is_active(void);
int shell_job_input_blocked(void);
int shell_job_get_status(shell_job_status_t* status_out);
int shell_job_cancel_requested(void);
void shell_job_set_phase(shell_job_context_t* context, const char* phase);
void shell_job_set_progress(shell_job_context_t* context,
                            uint32_t progress, uint32_t total);
const char* shell_job_state_name(shell_job_state_t state);
const char* shell_job_kind_name(shell_job_kind_t kind);

void shell_dispatch_cmd_job(const char* arguments);

int shell_network_start_job(const char* command, const char* arguments);
int shell_storage_start_job(const char* arguments);
int shell_packages_start_job(const char* command, const char* arguments);
int shell_checks_start_job(const char* command);

#endif
