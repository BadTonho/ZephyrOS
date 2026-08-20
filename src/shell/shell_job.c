#include "apps/shell_job.h"
#include "apps/shell_command_utils.h"
#include "apps/shell.h"
#include "apps/shell_runtime.h"
#include "core/errors.h"
#include "core/keyboard.h"
#include "core/log.h"
#include "process/process.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/video.h"
#include "core/wait.h"

#define SHELL_JOB_SCANCODE_ESCAPE 0x01U
#define SHELL_JOB_SCANCODE_F12 0x58U
#define SHELL_JOB_STATUS_ARGUMENT "status"

static shell_job_context_t shell_job_context;
static const shell_job_definition_t* shell_job_definition;
static uint8_t shell_job_cancel_called;
static uint8_t shell_job_block_warning;
static uint32_t shell_job_ipc_queue_full_baseline;
static uint8_t shell_job_queue_warning;
static uint32_t shell_job_generation_counter;
static int shell_job_drain_error;
static uint8_t shell_job_cancel_failed;
static uint8_t shell_job_timeout_requested;

static void shell_job_mark_cancel_requested(void) {
    if (!shell_job_is_active() || shell_job_context.cancel_requested) return;
    shell_job_context.cancel_requested = 1U;
    shell_job_context.cancel_requests++;
    shell_job_context.state = SHELL_JOB_STATE_CANCEL_REQUESTED;
    LOG_INFO("SHELL", "Cancelamento de job solicitado");
}

static void shell_job_mark_timeout_requested(void) {
    if (!shell_job_is_active() || shell_job_context.cancel_requested) return;
    shell_job_context.cancel_requested = 1U;
    shell_job_context.state = SHELL_JOB_STATE_CANCEL_REQUESTED;
    shell_job_context.last_error = ERR_TIMEOUT;
    shell_job_timeout_requested = 1U;
    LOG_WARN("SHELL", "Timeout de job atingido; iniciando drenagem");
}

static void shell_job_copy_text(char* destination, uint32_t capacity,
                                const char* source) {
    uint32_t index = 0U;

    if (!destination || !capacity) return;
    if (!source) source = "";
    while (source[index] && index + 1U < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void shell_job_complete(shell_job_state_t state, int result) {
    shell_job_context.last_error = result;
    shell_job_context.completed_ticks = timer_get_ticks();

    if (shell_job_definition && shell_job_definition->finish) {
        shell_job_definition->finish(&shell_job_context, state, result);
    }

    if (state == SHELL_JOB_STATE_SUCCEEDED) {
        LOG_INFO("SHELL", "Job cooperativo concluido");
    } else if (state == SHELL_JOB_STATE_CANCELLED) {
        LOG_WARN("SHELL", "Job cooperativo cancelado");
    } else if (result == ERR_TIMEOUT) {
        LOG_WARN("SHELL", "Job cooperativo excedeu o prazo");
    } else {
        LOG_ERROR("SHELL", "Job cooperativo terminou com erro");
    }

    shell_job_context.state = state;
    shell_job_definition = NULL;
    shell_job_cancel_called = 0U;
    shell_job_block_warning = 0U;
    shell_job_ipc_queue_full_baseline = 0U;
    shell_job_queue_warning = 0U;
    shell_runtime_finish_command();
}

static void shell_job_begin_drain(int result) {
    shell_job_drain_error = result;
    shell_job_context.state = SHELL_JOB_STATE_DRAINING;
    shell_job_set_phase(&shell_job_context, "drenando");
}

static void shell_job_poll_drain(void) {
    shell_job_step_result_t drain_result;

    if (!shell_job_definition) return;
    if (!shell_job_definition->drain) {
        shell_job_complete(shell_job_cancel_failed ||
                               shell_job_timeout_requested ?
                           SHELL_JOB_STATE_FAILED : SHELL_JOB_STATE_CANCELLED,
                           shell_job_drain_error == OK ? ERR_TIMEOUT :
                                                          shell_job_drain_error);
        return;
    }

    drain_result = shell_job_definition->drain(&shell_job_context);
    if (drain_result == SHELL_JOB_STEP_PENDING) return;
    if (drain_result == SHELL_JOB_STEP_FAILED) {
        shell_job_complete(SHELL_JOB_STATE_FAILED,
                           shell_job_context.last_error == OK ? ERR_STATE :
                                                                 shell_job_context.last_error);
        return;
    }
    shell_job_complete(shell_job_cancel_failed ||
                           shell_job_timeout_requested ?
                       SHELL_JOB_STATE_FAILED : SHELL_JOB_STATE_CANCELLED,
                       shell_job_drain_error == OK ? ERR_TIMEOUT :
                                                      shell_job_drain_error);
}

void shell_job_reset(void) {
    kmemset(&shell_job_context, 0, sizeof(shell_job_context));
    shell_job_context.state = SHELL_JOB_STATE_IDLE;
    shell_job_definition = NULL;
    shell_job_cancel_called = 0U;
    shell_job_block_warning = 0U;
    shell_job_ipc_queue_full_baseline = 0U;
    shell_job_queue_warning = 0U;
    shell_job_generation_counter = 0U;
    shell_job_drain_error = OK;
    shell_job_cancel_failed = 0U;
    shell_job_timeout_requested = 0U;
}

int shell_job_start(const shell_job_definition_t* definition,
                   const char* arguments) {
    if (!definition || !definition->command || !definition->step) {
        LOG_ERROR("SHELL", "Definicao de job invalida");
        return ERR_NULL;
    }
    if (shell_job_is_active()) {
        LOG_WARN("SHELL", "Tentativa de iniciar job enquanto outro esta ativo");
        return ERR_STATE;
    }

    kmemset(&shell_job_context, 0, sizeof(shell_job_context));
    shell_job_copy_text(shell_job_context.command,
                        sizeof(shell_job_context.command),
                        definition->command);
    shell_job_copy_text(shell_job_context.arguments,
                        sizeof(shell_job_context.arguments), arguments);
    shell_job_copy_text(shell_job_context.phase,
                        sizeof(shell_job_context.phase), "inicializando");
    shell_job_context.kind = definition->kind;
    shell_job_context.state = SHELL_JOB_STATE_RUNNING;
    shell_job_context.started_tick = timer_get_ticks();
    shell_job_context.last_error = OK;
    shell_job_context.deadline_tick = WAIT_TIMEOUT_INFINITE;
    shell_job_generation_counter++;
    if (!shell_job_generation_counter) shell_job_generation_counter = 1U;
    shell_job_context.generation = shell_job_generation_counter;
    shell_job_definition = definition;
    shell_job_cancel_called = 0U;
    shell_job_drain_error = OK;
    shell_job_cancel_failed = 0U;
    shell_job_timeout_requested = 0U;
    shell_job_block_warning = 0U;
    {
        ipc_stats_t ipc_stats;
        ipc_get_stats(&ipc_stats);
        shell_job_ipc_queue_full_baseline = ipc_stats.queue_full;
    }
    shell_job_queue_warning = 0U;

    LOG_INFO("SHELL", "Job cooperativo iniciado");
    if (arguments && (kstrcmp(arguments, "regcheck") == 0 ||
                      kstrcmp(arguments, "regcheck full") == 0)) {
        video_print("Operacao iniciada; entrada bloqueada. F11/Esc cancela.\n",
                    0x0E);
    } else {
        video_print("Operacao iniciada; entrada bloqueada. F12/Esc cancela.\n",
                    0x0E);
    }
    return OK;
}

void shell_job_poll(void) {
    shell_job_step_result_t step_result;
    int result;

    if (!shell_job_is_active()) return;

    shell_job_context.wakeups++;
    if (shell_job_context.deadline_active) {
        uint32_t remaining = 0U;

        if (wait_deadline_remaining_active(shell_job_context.deadline_tick,
                                           shell_job_context.deadline_active,
                                           &remaining) != OK) {
            shell_job_context.last_error = ERR_STATE;
            shell_job_complete(SHELL_JOB_STATE_FAILED, ERR_STATE);
            return;
        }
        if (!remaining) shell_job_mark_timeout_requested();
    }
    if (shell_job_context.state == SHELL_JOB_STATE_DRAINING) {
        shell_job_poll_drain();
        return;
    }

    if (shell_job_context.cancel_requested &&
        shell_job_definition->cancel && !shell_job_cancel_called) {
        shell_job_cancel_called = 1U;
        result = shell_job_definition->cancel(&shell_job_context);
        if (result == OK) {
            shell_job_begin_drain(ERR_TIMEOUT);
        } else {
            shell_job_cancel_failed = 1U;
            shell_job_begin_drain(result);
        }
        return;
    }

    step_result = shell_job_definition->step(&shell_job_context);
    if (step_result == SHELL_JOB_STEP_PENDING) return;
    if (step_result == SHELL_JOB_STEP_COMPLETE) {
        shell_job_complete(SHELL_JOB_STATE_SUCCEEDED,
                           shell_job_context.last_error);
    } else if (step_result == SHELL_JOB_STEP_CANCELLED) {
        shell_job_begin_drain(shell_job_context.last_error == OK ? ERR_TIMEOUT :
                                                                    shell_job_context.last_error);
    } else {
        shell_job_complete(SHELL_JOB_STATE_FAILED,
                           shell_job_context.last_error == OK ? ERR_STATE :
                                                                 shell_job_context.last_error);
    }
}

void shell_job_pump_events(void) {
    ipc_msg_t message;
    ipc_stats_t ipc_stats;

    if (!shell_job_is_active()) return;
    keyboard_process_events();
    while (ipc_receive(&message)) {
        if (message.type == IPC_MSG_KEYBOARD) {
            shell_job_handle_key((uint8_t)message.data1);
        } else if (message.type == IPC_MSG_APP_REQUEST) {
            shell_handle_app_request(message.data1);
        }
    }
    ipc_get_stats(&ipc_stats);
    if (!shell_job_queue_warning &&
        ipc_stats.queue_full > shell_job_ipc_queue_full_baseline) {
        shell_job_queue_warning = 1U;
        LOG_WARN("SHELL", "Fila IPC atingiu o limite durante job");
    }
    shell_report_user_test_result();
    shell_report_app_loader_result();
    shell_hosted_present_progress();
}

void shell_job_handle_key(uint8_t scancode) {
    if (!shell_job_is_active()) return;
    if (scancode == SHELL_JOB_SCANCODE_ESCAPE ||
        scancode == SHELL_JOB_SCANCODE_F12) {
        shell_job_mark_cancel_requested();
        return;
    }

    shell_job_context.blocked_events++;
    if (!shell_job_block_warning) {
        shell_job_block_warning = 1U;
        LOG_WARN("SHELL", "Entrada ignorada durante job cooperativo");
    }
}

void shell_job_request_cancel(void) {
    shell_job_mark_cancel_requested();
}

int shell_job_is_active(void) {
    return shell_job_definition != NULL &&
           shell_job_context.state != SHELL_JOB_STATE_IDLE &&
           shell_job_context.state != SHELL_JOB_STATE_SUCCEEDED &&
           shell_job_context.state != SHELL_JOB_STATE_FAILED &&
           shell_job_context.state != SHELL_JOB_STATE_CANCELLED;
}

int shell_job_input_blocked(void) {
    return shell_job_is_active();
}

int shell_job_get_status(shell_job_status_t* status_out) {
    if (!status_out) {
        LOG_ERROR("SHELL", "Destino nulo para status do job");
        return ERR_NULL;
    }
    status_out->state = shell_job_context.state;
    status_out->kind = shell_job_context.kind;
    shell_job_copy_text(status_out->command, sizeof(status_out->command),
                        shell_job_context.command);
    shell_job_copy_text(status_out->phase, sizeof(status_out->phase),
                        shell_job_context.phase);
    status_out->progress = shell_job_context.progress;
    status_out->total = shell_job_context.total;
    status_out->started_tick = shell_job_context.started_tick;
    status_out->completed_ticks = shell_job_context.completed_ticks;
    status_out->blocked_events = shell_job_context.blocked_events;
    status_out->cancel_requests = shell_job_context.cancel_requests;
    status_out->last_error = shell_job_context.last_error;
    status_out->active = shell_job_is_active();
    status_out->generation = shell_job_context.generation;
    status_out->deadline_tick = shell_job_context.deadline_tick;
    status_out->wakeups = shell_job_context.wakeups;
    status_out->stale_events = shell_job_context.stale_events;
    status_out->cancel_requested = shell_job_context.cancel_requested;
    status_out->draining = shell_job_context.state == SHELL_JOB_STATE_DRAINING;
    status_out->deadline_active = shell_job_context.deadline_active;
    return OK;
}

int shell_job_cancel_requested(void) {
    return shell_job_context.cancel_requested;
}

uint32_t shell_job_get_generation(void) {
    return shell_job_context.generation;
}

int shell_job_generation_matches(uint32_t generation) {
    if (!generation || !shell_job_is_active()) return 0;
    return shell_job_context.generation == generation;
}

void shell_job_note_stale_event(uint32_t generation) {
    if (!generation) return;
    if (shell_job_is_active() && shell_job_context.generation == generation) {
        return;
    }
    shell_job_context.stale_events++;
    LOG_WARN("SHELL", "Evento de geracao antiga descartado");
}

int shell_job_get_wait_timeout(uint32_t* timeout_out) {
    if (!timeout_out) {
        LOG_ERROR("SHELL", "Destino nulo para timeout do job");
        return ERR_NULL;
    }
    if (!shell_job_is_active() || !shell_job_context.deadline_active) {
        *timeout_out = WAIT_TIMEOUT_INFINITE;
        return OK;
    }
    return wait_deadline_remaining_active(shell_job_context.deadline_tick,
                                          shell_job_context.deadline_active,
                                          timeout_out);
}

void shell_job_set_phase(shell_job_context_t* context, const char* phase) {
    if (!context) return;
    shell_job_copy_text(context->phase, sizeof(context->phase), phase);
}

void shell_job_set_progress(shell_job_context_t* context,
                            uint32_t progress, uint32_t total) {
    if (!context) return;
    context->progress = progress;
    context->total = total;
}

void shell_job_set_timeout(shell_job_context_t* context, uint32_t ticks) {
    if (!context) return;
    if (!ticks) {
        shell_job_clear_timeout(context);
        return;
    }
    context->deadline_tick = timer_get_ticks() + ticks;
    context->deadline_active = 1U;
}

void shell_job_set_deadline(shell_job_context_t* context,
                            uint32_t deadline_tick) {
    if (!context) return;
    if (deadline_tick == WAIT_TIMEOUT_INFINITE) {
        shell_job_clear_timeout(context);
        return;
    }
    context->deadline_tick = deadline_tick;
    context->deadline_active = 1U;
}

void shell_job_clear_timeout(shell_job_context_t* context) {
    if (!context) return;
    context->deadline_tick = WAIT_TIMEOUT_INFINITE;
    context->deadline_active = 0U;
}

const char* shell_job_state_name(shell_job_state_t state) {
    switch (state) {
        case SHELL_JOB_STATE_RUNNING: return "RUNNING";
        case SHELL_JOB_STATE_CANCEL_REQUESTED: return "CANCEL_REQUESTED";
        case SHELL_JOB_STATE_SUCCEEDED: return "SUCCEEDED";
        case SHELL_JOB_STATE_FAILED: return "FAILED";
        case SHELL_JOB_STATE_CANCELLED: return "CANCELLED";
        case SHELL_JOB_STATE_DRAINING: return "DRAINING";
        default: return "IDLE";
    }
}

const char* shell_job_kind_name(shell_job_kind_t kind) {
    switch (kind) {
        case SHELL_JOB_KIND_NETWORK: return "NETWORK";
        case SHELL_JOB_KIND_PACKAGES: return "PACKAGES";
        case SHELL_JOB_KIND_CHECK: return "CHECK";
        case SHELL_JOB_KIND_INDEX: return "INDEX";
        default: return "NONE";
    }
}

void shell_dispatch_cmd_job(const char* arguments) {
    shell_job_status_t status;

    if (!arguments || kstrcmp(arguments, SHELL_JOB_STATUS_ARGUMENT) != 0) {
        LOG_WARN("SHELL", "Uso invalido do comando job");
        video_print("Uso: job status\n", 0x0C);
        return;
    }
    if (shell_job_get_status(&status) != OK) return;
    video_print("Job: ", 0x0B);
    video_print(shell_job_state_name(status.state), 0x0A);
    video_print(" geracao=", 0x07);
    shell_command_print_num(status.generation);
    video_print(" tipo=", 0x07);
    video_print(shell_job_kind_name(status.kind), 0x0B);
    video_print(" comando=", 0x07);
    video_print(status.command[0] ? status.command : "nenhum", 0x0B);
    video_print(" fase=", 0x07);
    video_print(status.phase[0] ? status.phase : "-", 0x07);
    video_print(" progresso=", 0x07);
    shell_command_print_num(status.progress);
    video_print("/", 0x07);
    shell_command_print_num(status.total);
    video_print(" erro=", 0x07);
    shell_command_print_num((uint32_t)status.last_error);
    video_print(" bloqueadas=", 0x07);
    shell_command_print_num(status.blocked_events);
    video_print(" cancelamentos=", 0x07);
    shell_command_print_num(status.cancel_requests);
    video_print(" deadline=", 0x07);
    shell_command_print_num(status.deadline_tick);
    video_print(" deadline_ativo=", 0x07);
    shell_command_print_num(status.deadline_active);
    video_print(" acordadas=", 0x07);
    shell_command_print_num(status.wakeups);
    video_print(" tardias=", 0x07);
    shell_command_print_num(status.stale_events);
    video_print("\n", 0x07);
}
