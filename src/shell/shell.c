#include "apps/shell.h"
#include "apps/shell_input.h"
#include "apps/shell_dispatch.h"
#include "apps/shell_command_utils.h"
#include "apps/shell_job.h"
#include "apps/shell_runtime.h"
#include "apps/taskmanager.h"
#include "apps/guitest.h"
#include "core/app_loader.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/string.h"
#include "core/video.h"
#include "fs/fs.h"
#include "process/process.h"
#include "ui/taskbar.h"
#include "ui/desktop.h"
#include "ui/filemanager.h"
#include "ui/settings.h"
#include "ui/updater.h"
#include "ui/appstore.h"
#include "ui/wm.h"
#define SHELL_WHEEL_SCROLL_LINES 3

typedef enum {
    SHELL_PROMPT_STATE_HIDDEN = 0,
    SHELL_PROMPT_STATE_REQUESTED,
    SHELL_PROMPT_STATE_VISIBLE,
    SHELL_PROMPT_STATE_BLOCKED
} shell_prompt_state_t;

static void process_input(void);
static int shell_should_show_prompt(void);
static void shell_prompt_request(void);
static void shell_prompt_reconcile(void);
static void shell_prompt_hide(void);

static shell_prompt_state_t shell_prompt_state = SHELL_PROMPT_STATE_HIDDEN;
static uint32_t shell_prompt_epoch;
static uint32_t shell_prompt_rendered_epoch;
static uint32_t shell_prompt_warned_epoch;
static shell_lifecycle_status_t shell_lifecycle;
static uint32_t shell_lifecycle_generation_counter;

static uint8_t shell_lifecycle_scene_active(void) {
    uint8_t hosted_shell = (uint8_t)(
        shell_runtime_is_hosted_visible() &&
        wm_is_hosted_app_focused(WM_APP_SHELL));

    return (uint8_t)((desktop_is_active() && !hosted_shell) ||
                     fm_is_running() ||
                     taskmgr_is_open() || taskmgr_is_gui_open() ||
                     settings_is_open() || updater_is_open() ||
                     appstore_is_open() || (wm_is_active() && !hosted_shell) ||
                     guitest_is_active());
}

static shell_lifecycle_layer_t shell_lifecycle_blocking_layer(void) {
    if (shell_job_input_blocked()) {
        return SHELL_LIFECYCLE_LAYER_JOB;
    }
    if (shell_checks_input_blocked()) {
        return SHELL_LIFECYCLE_LAYER_INPUT;
    }
    if (app_loader_is_foreground_active()) {
        return SHELL_LIFECYCLE_LAYER_LOADER;
    }
    if (shell_runtime_is_hosted_visible() &&
        !wm_is_hosted_app_focused(WM_APP_SHELL)) {
        return SHELL_LIFECYCLE_LAYER_FOCUS;
    }
    if (shell_lifecycle_scene_active()) return SHELL_LIFECYCLE_LAYER_SCENE;
    if (!video_terminal_is_active()) return SHELL_LIFECYCLE_LAYER_VIDEO;
    return SHELL_LIFECYCLE_LAYER_NONE;
}

static void shell_finalize_closed_scene(uint8_t scene_was_active) {
    if (!scene_was_active || shell_lifecycle_scene_active()) return;
    shell_runtime_begin_operation(SHELL_LIFECYCLE_LAYER_SCENE);
    shell_runtime_finish_command();
}

static void shell_lifecycle_refresh(void) {
    shell_lifecycle.prompt_state =
        (shell_lifecycle_prompt_state_t)shell_prompt_state;
    shell_lifecycle.input_blocked =
        (uint8_t)(shell_checks_input_blocked() || shell_job_input_blocked());
    shell_lifecycle.terminal_active =
        (uint8_t)video_terminal_is_active();
    shell_lifecycle.hosted_visible =
        (uint8_t)shell_runtime_is_hosted_visible();
    shell_lifecycle.focus_shell = shell_lifecycle.hosted_visible ?
        (uint8_t)wm_is_hosted_app_focused(WM_APP_SHELL) :
        (uint8_t)!wm_is_active();
    shell_lifecycle.scene_active = shell_lifecycle_scene_active();
    shell_lifecycle.job_active = (uint8_t)shell_job_is_active();
    shell_lifecycle.loader_active =
        (uint8_t)app_loader_is_foreground_active();
}

void shell_runtime_reset_lifecycle_status(void) {
    kmemset(&shell_lifecycle, 0, sizeof(shell_lifecycle));
    shell_lifecycle.prompt_state = SHELL_LIFECYCLE_PROMPT_HIDDEN;
    shell_lifecycle.last_layer = SHELL_LIFECYCLE_LAYER_NONE;
    shell_lifecycle.last_error = OK;
    shell_lifecycle_refresh();
}

void shell_runtime_begin_operation(shell_lifecycle_layer_t layer) {
    if (shell_lifecycle.operation_active) {
        shell_lifecycle.last_layer = layer;
        shell_lifecycle_refresh();
        return;
    }
    shell_lifecycle_generation_counter++;
    if (!shell_lifecycle_generation_counter) {
        shell_lifecycle_generation_counter = 1U;
    }
    shell_lifecycle.generation = shell_lifecycle_generation_counter;
    shell_lifecycle.operation_active = 1U;
    shell_lifecycle.last_layer = layer;
    shell_lifecycle.last_error = OK;
    shell_lifecycle_refresh();
}

void shell_runtime_note_lifecycle_layer(shell_lifecycle_layer_t layer) {
    shell_lifecycle.last_layer = layer;
    shell_lifecycle_refresh();
}

void shell_runtime_note_lifecycle_error(int error_code) {
    shell_lifecycle.last_error = (uint32_t)error_code;
    shell_lifecycle_refresh();
}

void shell_runtime_note_lifecycle_input_blocked(void) {
    shell_lifecycle.input_blocked_events++;
    shell_lifecycle_refresh();
}

int shell_runtime_get_lifecycle_status(shell_lifecycle_status_t* status_out) {
    if (!status_out) {
        LOG_ERROR("SHELL", "Destino nulo para status de liveness");
        return ERR_NULL;
    }
    shell_lifecycle_refresh();
    *status_out = shell_lifecycle;
    return OK;
}

static void shell_prompt_hide(void) {
    shell_prompt_state = SHELL_PROMPT_STATE_HIDDEN;
    shell_prompt_epoch++;
    if (!shell_prompt_epoch) shell_prompt_epoch = 1U;
    shell_lifecycle.prompt_state = SHELL_LIFECYCLE_PROMPT_HIDDEN;
}

static void shell_prompt_request(void) {
    shell_lifecycle.prompt_requests++;
    if (shell_prompt_state != SHELL_PROMPT_STATE_VISIBLE) {
        shell_prompt_state = SHELL_PROMPT_STATE_REQUESTED;
        shell_lifecycle.prompt_state = SHELL_LIFECYCLE_PROMPT_REQUESTED;
    }
}

static void shell_prompt_reconcile(void) {
    shell_lifecycle.prompt_reconciliations++;
    if (!shell_should_show_prompt()) {
        shell_prompt_state = SHELL_PROMPT_STATE_BLOCKED;
        shell_lifecycle.prompt_state = SHELL_LIFECYCLE_PROMPT_BLOCKED;
        shell_lifecycle.prompt_blocked++;
        shell_lifecycle.last_layer = shell_lifecycle_blocking_layer();
        shell_lifecycle_refresh();
        return;
    }
    if (shell_prompt_state == SHELL_PROMPT_STATE_VISIBLE &&
        shell_prompt_rendered_epoch == shell_prompt_epoch) return;
    if (shell_prompt_state == SHELL_PROMPT_STATE_VISIBLE) {
        shell_prompt_state = SHELL_PROMPT_STATE_REQUESTED;
    }

    shell_input_print_prompt(wm_is_active());
    if (!video_terminal_is_active()) {
        shell_prompt_state = SHELL_PROMPT_STATE_REQUESTED;
        shell_lifecycle.prompt_state = SHELL_LIFECYCLE_PROMPT_REQUESTED;
        shell_lifecycle.prompt_missing++;
        shell_lifecycle.last_layer = SHELL_LIFECYCLE_LAYER_VIDEO;
        if (shell_prompt_warned_epoch != shell_prompt_epoch) {
            shell_prompt_warned_epoch = shell_prompt_epoch;
            LOG_WARN("SHELL", "Prompt pendente; terminal ainda indisponivel");
        }
        return;
    }
    shell_prompt_rendered_epoch = shell_prompt_epoch;
    shell_prompt_state = SHELL_PROMPT_STATE_VISIBLE;
    shell_lifecycle.prompt_state = SHELL_LIFECYCLE_PROMPT_VISIBLE;
    shell_lifecycle.prompt_rendered++;
    shell_lifecycle_refresh();
}

void shell_runtime_reset_input(void) {
    shell_input_reset();
    shell_prompt_hide();
}

int shell_handle_mouse(mouse_event_t* event) {
    if (!event) {
        LOG_ERROR("SHELL", "Evento de mouse nulo");
        return 0;
    }
    if (!video_terminal_is_active() ||
        event->event != MOUSE_EVENT_WHEEL || event->wheel == 0) return 0;
    return video_terminal_scroll(event->wheel * SHELL_WHEEL_SCROLL_LINES);
}


void shell_runtime_suspend_terminal(void) {
    if (video_terminal_is_active()) {
        shell_runtime_reset_input();
        video_terminal_suspend();
        return;
    }
    shell_prompt_hide();
}

void shell_runtime_suspend_terminal_for_scene(void) {
    if (!shell_runtime_is_hosted_visible()) {
        shell_runtime_suspend_terminal();
    }
}

void shell_runtime_resume_terminal(void) {
    shell_input_resume_terminal(wm_is_active());
    if (video_terminal_is_active() &&
        shell_prompt_state == SHELL_PROMPT_STATE_REQUESTED) {
        shell_prompt_reconcile();
    }
}


int shell_runtime_prepare_filemanager(void) {
    int result;

    if (fs_get_type() != FS_TYPE_NONE) {
        if (!recovery_is_enabled(RECOVERY_COMPONENT_FILEMANAGER)) {
            LOG_WARN("SHELL", "Recuperando File Manager apos filesystem disponivel");
            recovery_mark_ready(RECOVERY_COMPONENT_FILESYSTEM);
            recovery_mark_ready(RECOVERY_COMPONENT_FILEMANAGER);
        }
        return OK;
    }

    LOG_WARN("SHELL", "Filesystem indisponivel; tentando remontar para o Explorer");
    result = fs_init();
    if (result == OK && fs_get_type() != FS_TYPE_NONE) {
        recovery_mark_ready(RECOVERY_COMPONENT_FILESYSTEM);
        recovery_mark_ready(RECOVERY_COMPONENT_FILEMANAGER);
        LOG_INFO("SHELL", "Filesystem remontado para o Explorer");
        return OK;
    }

    recovery_mark_disabled(RECOVERY_COMPONENT_FILESYSTEM, result,
                           "Sistema de arquivos indisponivel");
    recovery_mark_disabled(RECOVERY_COMPONENT_FILEMANAGER, result,
                           "File Manager requer filesystem");
    LOG_ERROR("SHELL", "Nao foi possivel preparar filesystem para o Explorer");
    return result == OK ? ERR_UNAVAILABLE : result;
}

void shell_handle_app_request(uint32_t request) {
    int hosted_workspace = desktop_get_mode() == DESKTOP_MODE_CLASSIC &&
                           wm_is_active();
    uint8_t scene_was_active = shell_lifecycle_scene_active();
    uint8_t operation_owner = (uint8_t)!shell_lifecycle.operation_active;

    shell_runtime_begin_operation(
        request == IPC_APP_OPEN_SHELL ? SHELL_LIFECYCLE_LAYER_FOCUS :
                                        SHELL_LIFECYCLE_LAYER_SCENE);

    if (!hosted_workspace) {
        if (taskmgr_is_gui_open() && request != IPC_APP_OPEN_TASKMANAGER_GUI) {
            taskmgr_close();
        }
        if (settings_is_open() && request != IPC_APP_OPEN_SETTINGS) {
            settings_close();
        }
        if (updater_is_open() && request != IPC_APP_OPEN_UPDATER) {
            updater_close();
        }
        if (appstore_is_open() && request != IPC_APP_OPEN_APP_STORE) {
            appstore_close();
        }
    }

    switch ((ipc_app_request_t)request) {
        case IPC_APP_OPEN_SHELL:
            if (desktop_get_mode() == DESKTOP_MODE_CLASSIC) {
                if (shell_hosted_open() == OK) break;
                shell_runtime_reset_input();
                video_terminal_begin();
                shell_print_prompt();
                taskbar_draw();
                break;
            }
            if (wm_is_active()) wm_set_active(0);
            desktop_set_active(0);
            if (!video_terminal_is_active()) {
                shell_runtime_reset_input();
                video_terminal_begin();
                shell_print_prompt();
            }
            taskbar_draw();
            break;
        case IPC_APP_OPEN_EXPLORER:
            if (shell_runtime_prepare_filemanager() == OK) {
                shell_runtime_suspend_terminal_for_scene();
                fm_run();
            } else {
                shell_runtime_note_lifecycle_error(ERR_UNAVAILABLE);
                video_print("Erro: File Manager indisponivel.\n", 0x0C);
            }
            break;
        case IPC_APP_OPEN_TASKMANAGER:
            if (recovery_is_enabled(RECOVERY_COMPONENT_TASKMANAGER)) {
                shell_runtime_suspend_terminal_for_scene();
                if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
                    desktop_set_active(0);
                }
                if (desktop_get_mode() == DESKTOP_MODE_CLASSIC &&
                    taskmgr_open_gui() != OK) {
                    desktop_set_active(0);
                    wm_set_active(0);
                    shell_runtime_suspend_terminal();
                    LOG_WARN("SHELL", "GUI do Task Manager indisponivel; usando TUI");
                    taskmgr_run();
                } else if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
                    taskmgr_run();
                }
            } else {
                shell_runtime_note_lifecycle_error(ERR_UNAVAILABLE);
                video_print("Erro: Task Manager indisponivel.\n", 0x0C);
            }
            break;
        case IPC_APP_OPEN_TASKMANAGER_GUI:
            if (!recovery_is_enabled(RECOVERY_COMPONENT_TASKMANAGER)) {
                shell_runtime_note_lifecycle_error(ERR_UNAVAILABLE);
                video_print("Erro: Task Manager indisponivel.\n", 0x0C);
                break;
            }
            shell_runtime_suspend_terminal_for_scene();
            if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
                desktop_set_active(0);
            }
            if (taskmgr_open_gui() != OK) {
                desktop_set_active(0);
                wm_set_active(0);
                shell_runtime_suspend_terminal();
                LOG_WARN("SHELL", "GUI do Task Manager indisponivel; usando TUI");
                taskmgr_run();
            }
            break;
        case IPC_APP_OPEN_DESKTOP:
            if (wm_is_active()) wm_set_active(0);
            shell_runtime_suspend_terminal();
            video_clear();
            desktop_set_active(1);
            desktop_draw();
            break;
        case IPC_APP_OPEN_SETTINGS:
            if (recovery_is_enabled(RECOVERY_COMPONENT_SETTINGS)) {
                shell_runtime_suspend_terminal_for_scene();
                if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
                    desktop_set_active(0);
                }
                settings_open();
            } else {
                shell_runtime_note_lifecycle_error(ERR_UNAVAILABLE);
                video_print("Erro: Configuracoes indisponiveis.\n", 0x0C);
            }
            break;
        case IPC_APP_OPEN_UPDATER:
            if (recovery_is_enabled(
                    RECOVERY_COMPONENT_SYSTEM_UPDATER)) {
                shell_runtime_suspend_terminal_for_scene();
                if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
                    desktop_set_active(0);
                }
                if (updater_open() != OK) {
                    shell_runtime_note_lifecycle_error(ERR_UNAVAILABLE);
                    video_print("Erro: System Updater indisponivel.\n",
                                0x0C);
                }
            } else {
                shell_runtime_note_lifecycle_error(ERR_UNAVAILABLE);
                video_print("Erro: System Updater indisponivel.\n",
                            0x0C);
            }
            break;
        case IPC_APP_OPEN_APP_STORE:
            if (recovery_is_enabled(RECOVERY_COMPONENT_APP_STORE)) {
                shell_runtime_suspend_terminal_for_scene();
                if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
                    desktop_set_active(0);
                }
                if (appstore_open() != OK) {
                    shell_runtime_note_lifecycle_error(ERR_UNAVAILABLE);
                    video_print("Erro: App Store indisponivel.\n", 0x0C);
                }
            } else {
                shell_runtime_note_lifecycle_error(ERR_UNAVAILABLE);
                video_print("Erro: App Store indisponivel.\n", 0x0C);
            }
            break;
        default:
            shell_runtime_note_lifecycle_error(ERR_INVALID);
            LOG_ERROR("SHELL", "Solicitacao de aplicativo invalida");
            break;
    }
    if (operation_owner ||
        (request == IPC_APP_OPEN_SHELL && scene_was_active)) {
        shell_runtime_finish_command();
    }
}

static void shell_redraw_after_overlay_close(void) {
    shell_runtime_note_lifecycle_layer(SHELL_LIFECYCLE_LAYER_SCENE);
    if (appstore_is_open()) {
        appstore_draw();
        return;
    }
    if (updater_is_open()) {
        updater_draw();
        return;
    }
    if (desktop_is_active()) {
        desktop_draw();
        return;
    }

    if (wm_is_active()) {
        wm_draw_all();
        return;
    }

    if (guitest_is_active()) {
        guitest_draw();
        return;
    }

    /* Menus desenham por coordenadas e nao pertencem ao historico textual. */
    video_terminal_begin();
    taskbar_draw();
    shell_runtime_begin_operation(SHELL_LIFECYCLE_LAYER_SCENE);
    shell_runtime_finish_command();
}

void shell_runtime_finish_command(void) {
    shell_lifecycle.finalization_requests++;
    if (shell_lifecycle.generation && !shell_lifecycle.operation_active) {
        shell_lifecycle.duplicate_finalizations++;
        return;
    }
    shell_runtime_reset_input();
    shell_prompt_request();
    shell_prompt_reconcile();
    shell_lifecycle_refresh();
    if (!shell_lifecycle.operation_active) return;
    if (shell_lifecycle_blocking_layer() != SHELL_LIFECYCLE_LAYER_NONE) {
        if (!shell_lifecycle.job_active && !shell_lifecycle.scene_active &&
            !shell_lifecycle.loader_active &&
            !shell_lifecycle.terminal_active) {
            shell_lifecycle.prompt_missing++;
        }
        return;
    }
    shell_lifecycle.operation_active = 0U;
    shell_lifecycle.finalizations++;
}



void shell_report_user_test_result(void) {
    shell_checks_report_user_test_result();
}



void shell_report_app_loader_result(void) {
    app_loader_result_t result;

    /* Mantem o resultado no loader enquanto uma UI nativa cobre o terminal. */
    if (!video_terminal_is_active()) return;
    if (app_loader_take_finished_result(&result) != OK) return;
    shell_runtime_begin_operation(SHELL_LIFECYCLE_LAYER_LOADER);
    shell_runtime_note_lifecycle_layer(SHELL_LIFECYCLE_LAYER_LOADER);

    if (shell_checks_handle_loader_result(&result)) return;
    if (shell_core_handle_loader_result(&result)) return;

    video_print("\n[", 0x08);
    if (result.start_failed) {
        video_print("ERRO", 0x0C);
        video_print("] Aplicativo ZAPP PID ", 0x07);
        shell_command_print_num(result.pid);
        video_print(" nao iniciou (codigo ", 0x07);
        shell_command_print_num(result.exit_code);
        video_print(").\n", 0x07);
    } else if (result.termination_signal) {
        video_print(result.termination_signal == APP_SIGNAL_SEGV ?
                    "WARN" : "INFO",
                    result.termination_signal == APP_SIGNAL_SEGV ?
                    0x0E : 0x0A);
        video_print("] Aplicativo ZAPP PID ", 0x07);
        shell_command_print_num(result.pid);
        video_print(" encerrado por ", 0x07);
        video_print(process_signal_name(result.termination_signal), 0x0E);
        video_print("; foco devolvido ao Shell.\n", 0x07);
    } else if (result.faulted) {
        video_print("WARN", 0x0E);
        video_print("] Aplicativo ZAPP PID ", 0x07);
        shell_command_print_num(result.pid);
        video_print(" encerrou apos falha isolada.\n", 0x07);
    } else if (result.cancelled) {
        video_print("INFO", 0x0A);
        video_print("] Aplicativo ZAPP PID ", 0x07);
        shell_command_print_num(result.pid);
        video_print(" cancelado; foco devolvido ao Shell.\n", 0x07);
    } else if (result.exit_code != APP_EXIT_SUCCESS) {
        video_print("ERRO", 0x0C);
        video_print("] Aplicativo ZAPP PID ", 0x07);
        shell_command_print_num(result.pid);
        video_print(" encerrou com codigo ", 0x07);
        shell_command_print_num(result.exit_code);
        video_print(".\n", 0x07);
    } else {
        video_print("INFO", 0x0A);
        video_print("] Aplicativo ZAPP PID ", 0x07);
        shell_command_print_num(result.pid);
        video_print(" encerrou com codigo ", 0x07);
        shell_command_print_num(result.exit_code);
        video_print(".\n", 0x07);
    }

    shell_runtime_finish_command();
}


void shell_init(void) {
    shell_prompt_state = SHELL_PROMPT_STATE_HIDDEN;
    shell_prompt_epoch = 0U;
    shell_prompt_rendered_epoch = 0U;
    shell_prompt_warned_epoch = 0U;
    shell_runtime_reset_lifecycle_status();
    shell_input_init();
    shell_job_reset();
    shell_hosted_reset();
    shell_diagnostics_reset();
}

void shell_print_prompt(void) {
    shell_prompt_request();
    shell_prompt_reconcile();
}

static int shell_should_show_prompt(void) {
    if (shell_runtime_is_hosted_visible()) {
        return wm_is_hosted_app_focused(WM_APP_SHELL) &&
               !shell_checks_input_blocked() && !shell_job_input_blocked() &&
               !app_loader_is_foreground_active();
    }

    /* Apps que retornam ao Desktop ja redesenham a cena antes de voltar. */
    if (desktop_is_active()) return 0;
    if (fm_is_running()) return 0;
    if (taskmgr_is_open() || taskmgr_is_gui_open()) return 0;
    if (settings_is_open() || wm_is_active() || guitest_is_active()) return 0;
    if (shell_checks_input_blocked()) return 0;
    if (shell_job_input_blocked()) return 0;
    if (app_loader_is_foreground_active()) return 0;
    return 1;
}

void shell_update_hosted_terminal(void) {
    shell_hosted_present_progress();
    shell_prompt_reconcile();
}

static void process_input(void) {
    const char* input = shell_input_get_buffer();

    shell_runtime_begin_operation(SHELL_LIFECYCLE_LAYER_DISPATCHER);

    if (!input[0]) {
        shell_runtime_finish_command();
        return;
    }

    (void)shell_process_command(input);
    shell_runtime_finish_command();
}

void shell_runtime_handle_terminal_key(uint8_t scancode) {
    shell_runtime_note_lifecycle_layer(SHELL_LIFECYCLE_LAYER_INPUT);
    shell_input_event_t event = shell_input_handle_key(
        scancode, wm_is_active(), shell_checks_input_blocked());
    if (event == SHELL_INPUT_EVENT_NONE && shell_checks_input_blocked()) {
        shell_runtime_note_lifecycle_input_blocked();
    }
    if (event == SHELL_INPUT_EVENT_COMMAND_READY) {
        shell_prompt_hide();
        process_input();
    }
    if (event == SHELL_INPUT_EVENT_CANCELLED) {
        shell_runtime_begin_operation(SHELL_LIFECYCLE_LAYER_INPUT);
        shell_runtime_finish_command();
    }
}

void shell_handle_key(uint8_t scancode) {
    if (shell_job_is_active()) {
        if (shell_checks_handle_job_key(scancode)) return;
        shell_input_event_t job_input_event = shell_input_handle_key(
            scancode, wm_is_active(), 1U);
        if (job_input_event == SHELL_INPUT_EVENT_CANCELLED) {
            shell_runtime_note_lifecycle_layer(SHELL_LIFECYCLE_LAYER_INPUT);
            shell_job_request_cancel();
            return;
        }
        if (job_input_event == SHELL_INPUT_EVENT_NONE) {
            shell_runtime_note_lifecycle_input_blocked();
        }
        shell_runtime_note_lifecycle_layer(SHELL_LIFECYCLE_LAYER_JOB);
        shell_job_handle_key(scancode);
        return;
    }

    int config_result = taskbar_handle_config_key(scancode);
    if (config_result) {
        shell_runtime_note_lifecycle_layer(SHELL_LIFECYCLE_LAYER_FOCUS);
        shell_input_cancel_extended();
        if (config_result == 9) {
            shell_redraw_after_overlay_close();
        }
        return;
    }

    int tb_result = taskbar_handle_key(scancode);
    if (tb_result) {
        shell_runtime_note_lifecycle_layer(SHELL_LIFECYCLE_LAYER_FOCUS);
        shell_input_cancel_extended();
        if (tb_result == 2) {
            shell_handle_app_request(IPC_APP_OPEN_SHELL);
        } else if (tb_result == 3) {
            shell_handle_app_request(IPC_APP_OPEN_EXPLORER);
        } else if (tb_result == 4) {
            shell_handle_app_request(IPC_APP_OPEN_TASKMANAGER_GUI);
        } else if (tb_result == 5) {
            shell_core_reboot();
        } else if (tb_result == 6) {
            shell_core_shutdown("");
        } else if (tb_result == 7) {
            shell_handle_app_request(IPC_APP_OPEN_DESKTOP);
        } else if (tb_result == 8) {
            shell_handle_app_request(IPC_APP_OPEN_SETTINGS);
        } else if (tb_result == TB_ACTION_UPDATER) {
            shell_handle_app_request(IPC_APP_OPEN_UPDATER);
        } else if (tb_result == TB_ACTION_APPSTORE) {
            shell_handle_app_request(IPC_APP_OPEN_APP_STORE);
        } else if (tb_result == 9) {
            shell_redraw_after_overlay_close();
        }
        return;
    }

    if (guitest_is_active()) {
        uint8_t scene_was_active = shell_lifecycle_scene_active();
        guitest_handle_key(scancode);
        shell_finalize_closed_scene(scene_was_active);
        return;
    }

    if (wm_is_active()) {
        shell_runtime_note_lifecycle_layer(SHELL_LIFECYCLE_LAYER_FOCUS);
        if (wm_handle_key(scancode) == WM_RESULT_EXIT) {
            wm_set_active(0);
            shell_runtime_begin_operation(SHELL_LIFECYCLE_LAYER_SCENE);
            shell_runtime_reset_input();
            video_terminal_begin();
            shell_runtime_finish_command();
            taskbar_draw();
        }
        return;
    }

    if (taskmgr_is_gui_open()) {
        uint8_t scene_was_active = shell_lifecycle_scene_active();
        shell_runtime_note_lifecycle_layer(SHELL_LIFECYCLE_LAYER_SCENE);
        taskmgr_gui_handle_key(scancode);
        shell_finalize_closed_scene(scene_was_active);
        return;
    }

    if (settings_is_open()) {
        uint8_t scene_was_active = shell_lifecycle_scene_active();
        shell_runtime_note_lifecycle_layer(SHELL_LIFECYCLE_LAYER_SCENE);
        settings_handle_key(scancode);
        shell_finalize_closed_scene(scene_was_active);
        return;
    }

    if (updater_is_open()) {
        uint8_t scene_was_active = shell_lifecycle_scene_active();
        shell_runtime_note_lifecycle_layer(SHELL_LIFECYCLE_LAYER_SCENE);
        updater_handle_key(scancode);
        shell_finalize_closed_scene(scene_was_active);
        return;
    }

    if (appstore_is_open()) {
        uint8_t scene_was_active = shell_lifecycle_scene_active();
        shell_runtime_note_lifecycle_layer(SHELL_LIFECYCLE_LAYER_SCENE);
        appstore_handle_key(scancode);
        shell_finalize_closed_scene(scene_was_active);
        return;
    }

    if (desktop_is_active()) {
        shell_runtime_note_lifecycle_layer(SHELL_LIFECYCLE_LAYER_SCENE);
        int result = desktop_handle_key(scancode);
        if (result == -1) {
            desktop_set_active(0);
            shell_runtime_begin_operation(SHELL_LIFECYCLE_LAYER_SCENE);
            shell_runtime_reset_input();
            video_terminal_begin();
            shell_runtime_finish_command();
            taskbar_draw();
            return;
        }
        if (result == 2) {
            shell_handle_app_request(IPC_APP_OPEN_EXPLORER);
            return;
        }
        if (result == 3) {
            shell_handle_app_request(IPC_APP_OPEN_TASKMANAGER_GUI);
            return;
        }
        return;
    }

    if (taskmgr_is_open()) {
        uint8_t scene_was_active = shell_lifecycle_scene_active();
        shell_runtime_note_lifecycle_layer(SHELL_LIFECYCLE_LAYER_SCENE);
        taskmgr_handle_key(scancode);
        shell_finalize_closed_scene(scene_was_active);
        return;
    }

    shell_runtime_handle_terminal_key(scancode);
}

int shell_process_command(const char* input) {
    int result;

    if (!input) {
        LOG_ERROR("SHELL", "Comando nulo recebido");
        return ERR_NULL;
    }

    shell_runtime_resume_terminal();
    result = shell_dispatch_execute(input);
    if (result != OK) {
        shell_runtime_note_lifecycle_error(result);
        LOG_ERROR("SHELL", "Dispatcher retornou erro");
    }
    return result;
}
