#include <stdint.h>
#include <stdio.h>

#include "apps/shell.h"
#include "apps/shell_job.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/timer.h"
#include "core/video.h"
#include "process/process.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_OUTPUT_CAPACITY 4096U
#define HOST_IPC_CAPACITY 4U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint32_t fake_ticks;
static ipc_stats_t fake_ipc_stats;
static ipc_msg_t fake_messages[HOST_IPC_CAPACITY];
static uint32_t fake_message_count;
static uint32_t fake_message_offset;
static uint32_t keyboard_process_calls;
static uint32_t app_request_calls;
static uint32_t report_user_calls;
static uint32_t report_loader_calls;
static uint32_t hosted_progress_calls;
static uint32_t finish_command_calls;
static uint32_t log_calls;
static char video_output[HOST_OUTPUT_CAPACITY];
static uint32_t video_output_length;
static int step_mode;
static uint32_t step_calls;
static uint32_t cancel_calls;
static uint32_t drain_calls;
static int cancel_result;
static int drain_mode;
static int finish_state;
static int finish_result;

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
    printf("ZCOV_BEGIN|case=host:shell:job|value=0x%08X\n", coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:job|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:job|value=0x%08X\n",
           (uint32_t)result);
}

static uint32_t host_strlen(const char* text) {
    uint32_t length = 0U;

    if (!text) return 0U;
    while (text[length]) length++;
    return length;
}

static int host_contains(const char* text, const char* fragment) {
    uint32_t text_length;
    uint32_t fragment_length;

    if (!text || !fragment) return 0;
    text_length = host_strlen(text);
    fragment_length = host_strlen(fragment);
    if (fragment_length > text_length) return 0;
    for (uint32_t offset = 0U; offset + fragment_length <= text_length; offset++) {
        uint32_t index = 0U;
        while (index < fragment_length &&
               text[offset + index] == fragment[index]) index++;
        if (index == fragment_length) return 1;
    }
    return 0;
}

void kmemset(void* destination, uint8_t value, uint32_t size) {
    uint8_t* bytes = (uint8_t*)destination;

    if (!bytes) return;
    for (uint32_t index = 0U; index < size; index++) bytes[index] = value;
}

void kmemcpy(void* destination, const void* source, uint32_t size) {
    uint8_t* output = (uint8_t*)destination;
    const uint8_t* input = (const uint8_t*)source;

    if (!output || !input) return;
    for (uint32_t index = 0U; index < size; index++) output[index] = input[index];
}

int kstrcmp(const char* left, const char* right) {
    uint32_t index = 0U;

    if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    while (left[index] && left[index] == right[index]) index++;
    return (unsigned char)left[index] - (unsigned char)right[index];
}

uint32_t kstrlen(const char* text) {
    return host_strlen(text);
}

uint32_t timer_get_ticks(void) {
    return fake_ticks;
}

int wait_deadline_remaining_active(uint32_t deadline_tick, uint8_t active,
                                   uint32_t* out_remaining) {
    if (!out_remaining) return ERR_NULL;
    if (!active) {
        *out_remaining = WAIT_TIMEOUT_INFINITE;
        return OK;
    }
    *out_remaining = deadline_tick > fake_ticks ? deadline_tick - fake_ticks : 0U;
    return OK;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
    log_calls++;
}

void video_print(const char* text, uint8_t color) {
    (void)color;
    if (!text) return;
    while (*text && video_output_length + 1U < HOST_OUTPUT_CAPACITY) {
        video_output[video_output_length++] = *text++;
    }
    video_output[video_output_length] = '\0';
}

void shell_command_print_num(uint32_t value) {
    char digits[12];
    uint32_t length = 0U;

    if (!value) {
        video_print("0", 0x07);
        return;
    }
    while (value && length < sizeof(digits)) {
        digits[length++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    while (length) {
        char digit[2] = {digits[--length], '\0'};
        video_print(digit, 0x07);
    }
}

void keyboard_process_events(void) {
    keyboard_process_calls++;
}

int ipc_receive(ipc_msg_t* message) {
    if (!message || fake_message_offset >= fake_message_count) return 0;
    *message = fake_messages[fake_message_offset++];
    fake_ipc_stats.received++;
    return 1;
}

void ipc_get_stats(ipc_stats_t* stats) {
    if (stats) *stats = fake_ipc_stats;
}

void shell_handle_app_request(uint32_t request) {
    (void)request;
    app_request_calls++;
}

void shell_report_user_test_result(void) {
    report_user_calls++;
}

void shell_report_app_loader_result(void) {
    report_loader_calls++;
}

void shell_hosted_present_progress(void) {
    hosted_progress_calls++;
}

void shell_runtime_finish_command(void) {
    finish_command_calls++;
}

static shell_job_step_result_t fake_step(shell_job_context_t* context) {
    step_calls++;
    shell_job_set_phase(context, step_calls == 1U ? "executando" : "finalizando");
    shell_job_set_progress(context, step_calls, 2U);
    if (step_mode == 0) {
        if (step_calls == 1U) {
            shell_job_set_next_wake(context, fake_ticks + 3U);
            return SHELL_JOB_STEP_PENDING;
        }
        context->last_error = OK;
        return SHELL_JOB_STEP_COMPLETE;
    }
    if (step_mode == 1) {
        context->last_error = ERR_DISK;
        return SHELL_JOB_STEP_FAILED;
    }
    if (step_mode == 2) {
        context->last_error = ERR_CANCELLED;
        return SHELL_JOB_STEP_CANCELLED;
    }
    shell_job_set_timeout(context, 2U);
    return SHELL_JOB_STEP_PENDING;
}

static int fake_cancel(shell_job_context_t* context) {
    (void)context;
    cancel_calls++;
    return cancel_result;
}

static shell_job_step_result_t fake_drain(shell_job_context_t* context) {
    (void)context;
    drain_calls++;
    if (drain_mode == 0 && drain_calls == 1U) return SHELL_JOB_STEP_PENDING;
    if (drain_mode == 2) {
        context->last_error = ERR_STATE;
        return SHELL_JOB_STEP_FAILED;
    }
    return SHELL_JOB_STEP_COMPLETE;
}

static void fake_finish(shell_job_context_t* context, shell_job_state_t state,
                        int result) {
    (void)context;
    finish_state = state;
    finish_result = result;
}

static const shell_job_definition_t full_definition = {
    "fixture", SHELL_JOB_KIND_CHECK, fake_step, fake_cancel, fake_finish,
    fake_drain
};

static const shell_job_definition_t no_drain_definition = {
    "fixture", SHELL_JOB_KIND_CHECK, fake_step, fake_cancel, fake_finish, NULL
};

static void fixture_reset(void) {
    fake_ticks = 10U;
    fake_ipc_stats.sent = 0U;
    fake_ipc_stats.received = 0U;
    fake_ipc_stats.failed = 0U;
    fake_ipc_stats.queue_full = 0U;
    fake_message_count = 0U;
    fake_message_offset = 0U;
    keyboard_process_calls = 0U;
    app_request_calls = 0U;
    report_user_calls = 0U;
    report_loader_calls = 0U;
    hosted_progress_calls = 0U;
    finish_command_calls = 0U;
    log_calls = 0U;
    video_output_length = 0U;
    video_output[0] = '\0';
    step_mode = 0;
    step_calls = 0U;
    cancel_calls = 0U;
    drain_calls = 0U;
    cancel_result = OK;
    drain_mode = 0;
    finish_state = -1;
    finish_result = -1;
    shell_job_reset();
}

static int check_names_and_idle(void) {
    shell_job_status_t status;
    uint32_t timeout;

    if (shell_job_get_status(NULL) != ERR_NULL ||
        shell_job_get_wait_timeout(NULL) != ERR_NULL) return 1;
    if (shell_job_get_status(&status) != OK || status.state != SHELL_JOB_STATE_IDLE ||
        status.active || shell_job_is_active() || shell_job_input_blocked() ||
        shell_job_cancel_requested() || shell_job_get_generation() != 0U) return 2;
    if (shell_job_get_wait_timeout(&timeout) != OK ||
        timeout != WAIT_TIMEOUT_INFINITE) return 3;
    if (shell_job_state_name(SHELL_JOB_STATE_IDLE)[0] != 'I' ||
        shell_job_state_name(SHELL_JOB_STATE_RUNNING)[0] != 'R' ||
        shell_job_state_name(SHELL_JOB_STATE_CANCEL_REQUESTED)[0] != 'C' ||
        shell_job_state_name(SHELL_JOB_STATE_SUCCEEDED)[0] != 'S' ||
        shell_job_state_name(SHELL_JOB_STATE_FAILED)[0] != 'F' ||
        shell_job_state_name(SHELL_JOB_STATE_CANCELLED)[0] != 'C' ||
        shell_job_state_name(SHELL_JOB_STATE_DRAINING)[0] != 'D' ||
        shell_job_state_name((shell_job_state_t)99)[0] != 'I') return 4;
    if (shell_job_kind_name(SHELL_JOB_KIND_NONE)[0] != 'N' ||
        shell_job_kind_name(SHELL_JOB_KIND_NETWORK)[0] != 'N' ||
        shell_job_kind_name(SHELL_JOB_KIND_PACKAGES)[0] != 'P' ||
        shell_job_kind_name(SHELL_JOB_KIND_CHECK)[0] != 'C' ||
        shell_job_kind_name(SHELL_JOB_KIND_INDEX)[0] != 'I' ||
        shell_job_kind_name((shell_job_kind_t)99)[0] != 'N') return 5;
    shell_job_note_stale_event(7U);
    if (shell_job_get_status(&status) != OK || status.stale_events != 1U) return 6;
    shell_job_set_phase(NULL, "ignored");
    shell_job_set_progress(NULL, 1U, 2U);
    shell_job_set_timeout(NULL, 1U);
    shell_job_set_deadline(NULL, 1U);
    shell_job_set_next_wake(NULL, 1U);
    shell_job_clear_timeout(NULL);
    shell_job_clear_next_wake(NULL);
    return 0;
}

static int check_success_and_deadlines(void) {
    shell_job_status_t status;
    shell_job_context_t context;
    uint32_t timeout;
    uint32_t generation;

    if (shell_job_start(NULL, "") != ERR_NULL ||
        shell_job_start(&(shell_job_definition_t){"bad", SHELL_JOB_KIND_CHECK,
                                                   NULL, NULL, NULL, NULL}, "") != ERR_NULL) {
        return 1;
    }
    if (shell_job_start(&full_definition, "argumento longo") != OK) return 2;
    generation = shell_job_get_generation();
    if (!generation || !shell_job_is_active() || !shell_job_input_blocked() ||
        !shell_job_generation_matches(generation) ||
        shell_job_generation_matches(0U)) return 3;
    if (shell_job_get_status(&status) != OK || status.state != SHELL_JOB_STATE_RUNNING ||
        kstrcmp(status.command, "fixture") != 0 ||
        kstrcmp(status.phase, "inicializando") != 0 || status.started_tick != 10U) return 4;
    if (shell_job_start(&full_definition, "") != ERR_STATE) return 5;
    kmemset(&context, 0, sizeof(context));
    shell_job_set_deadline(&context, WAIT_TIMEOUT_INFINITE);
    shell_job_set_deadline(&context, 20U);
    shell_job_set_timeout(&context, 0U);
    shell_job_set_next_wake(&context, 12U);
    shell_job_clear_next_wake(&context);
    shell_job_poll();
    if (step_calls != 1U || shell_job_get_wait_timeout(&timeout) != OK ||
        timeout != 3U) return 6;
    fake_ticks = 11U;
    shell_job_poll();
    if (step_calls != 1U) return 7;
    fake_ticks = 13U;
    shell_job_poll();
    if (finish_state != SHELL_JOB_STATE_SUCCEEDED || finish_result != OK ||
        shell_job_is_active() || finish_command_calls != 1U) return 8;
    if (shell_job_generation_matches(generation)) return 9;
    shell_job_note_stale_event(generation);
    if (shell_job_get_status(&status) != OK) return 10;
    if (status.stale_events != 1U ||
        status.wakeups != 3U || status.progress != 2U || status.total != 2U) return 10;
    return 0;
}

static int check_failure_and_cancel(void) {
    shell_job_status_t status;

    fixture_reset();
    step_mode = 1;
    if (shell_job_start(&full_definition, "") != OK) return 1;
    shell_job_poll();
    if (finish_state != SHELL_JOB_STATE_FAILED || finish_result != ERR_DISK ||
        shell_job_is_active()) return 2;

    fixture_reset();
    if (shell_job_start(&full_definition, "") != OK) return 3;
    shell_job_handle_key(0x20U);
    shell_job_handle_key(0x01U);
    shell_job_handle_key(0x58U);
    if (!shell_job_cancel_requested() || shell_job_get_status(&status) != OK ||
        status.cancel_requests != 1U || status.blocked_events != 1U) return 4;
    shell_job_poll();
    if (cancel_calls != 1U || finish_state != -1 ||
        shell_job_get_status(&status) != OK || !status.draining) return 5;
    shell_job_poll();
    if (drain_calls != 1U || finish_state != -1) return 6;
    shell_job_poll();
    if (finish_state != SHELL_JOB_STATE_CANCELLED || finish_result != ERR_TIMEOUT ||
        shell_job_is_active()) return 7;

    fixture_reset();
    cancel_result = ERR_STATE;
    if (shell_job_start(&full_definition, "") != OK) return 8;
    shell_job_request_cancel();
    shell_job_poll();
    shell_job_poll();
    shell_job_poll();
    if (finish_state != SHELL_JOB_STATE_FAILED || finish_result != ERR_STATE) return 9;
    return 0;
}

static int check_timeout_cancelled_and_drain_errors(void) {
    fixture_reset();
    step_mode = 3;
    if (shell_job_start(&full_definition, "") != OK) return 1;
    shell_job_poll();
    fake_ticks = 12U;
    shell_job_poll();
    shell_job_poll();
    shell_job_poll();
    if (finish_state != SHELL_JOB_STATE_FAILED || finish_result != ERR_TIMEOUT ||
        shell_job_is_active()) return 2;

    fixture_reset();
    step_mode = 2;
    if (shell_job_start(&full_definition, "") != OK) return 3;
    shell_job_poll();
    shell_job_poll();
    shell_job_poll();
    if (finish_state != SHELL_JOB_STATE_CANCELLED || finish_result != ERR_CANCELLED) return 4;

    fixture_reset();
    if (shell_job_start(&no_drain_definition, "") != OK) return 5;
    shell_job_request_cancel();
    shell_job_poll();
    shell_job_poll();
    if (finish_state != SHELL_JOB_STATE_CANCELLED || finish_result != ERR_TIMEOUT) return 6;

    fixture_reset();
    drain_mode = 2;
    if (shell_job_start(&full_definition, "") != OK) return 7;
    shell_job_request_cancel();
    shell_job_poll();
    shell_job_poll();
    if (finish_state != SHELL_JOB_STATE_FAILED || finish_result != ERR_STATE) return 8;
    return 0;
}

static int check_event_pump_and_command(void) {
    shell_job_status_t status;

    fixture_reset();
    if (shell_job_start(&full_definition, "") != OK) return 1;
    fake_messages[0].type = IPC_MSG_KEYBOARD;
    fake_messages[0].data1 = 0x20U;
    fake_messages[1].type = IPC_MSG_APP_REQUEST;
    fake_messages[1].data1 = IPC_APP_OPEN_SHELL;
    fake_message_count = 2U;
    shell_job_pump_events();
    if (keyboard_process_calls != 1U || app_request_calls != 1U ||
        report_user_calls != 1U || report_loader_calls != 1U ||
        hosted_progress_calls != 1U ||
        shell_job_get_status(&status) != OK || status.blocked_events != 1U) return 2;
    shell_job_request_cancel();
    shell_job_poll();
    shell_job_poll();
    shell_job_poll();
    if (finish_state != SHELL_JOB_STATE_CANCELLED) return 3;

    fixture_reset();
    shell_dispatch_cmd_job(NULL);
    if (kstrcmp(video_output, "Uso: job status\n") != 0) return 4;
    video_output_length = 0U;
    video_output[0] = '\0';
    shell_dispatch_cmd_job("invalid");
    if (kstrcmp(video_output, "Uso: job status\n") != 0) return 5;
    video_output_length = 0U;
    video_output[0] = '\0';
    if (shell_job_start(&full_definition, "") != OK) return 6;
    shell_dispatch_cmd_job("status");
    if (!kstrlen(video_output) ||
        !host_contains(video_output, "Job: RUNNING") ||
        !host_contains(video_output, "tipo=CHECK")) return 7;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    fixture_reset();
    result = check_names_and_idle();
    if (result == 0) result = check_success_and_deadlines();
    if (result == 0) result = check_failure_and_cancel();
    if (result == 0) result = check_timeout_cancelled_and_drain_errors();
    if (result == 0) result = check_event_pump_and_command();
    coverage_active = 0U;
    if (result == 0 && !log_calls) result = 99;
    coverage_emit(result);
    return result;
}
