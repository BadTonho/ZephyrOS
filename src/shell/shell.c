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

static void process_input(void);
static int shell_should_show_prompt(void);
void shell_runtime_reset_input(void) {
    shell_input_reset();
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
    /* O prompt continua no historico enquanto outro app cobre o terminal. */
    if (video_terminal_is_active()) video_terminal_suspend();
}

void shell_runtime_suspend_terminal_for_scene(void) {
    if (!shell_runtime_is_hosted_visible()) {
        shell_runtime_suspend_terminal();
    }
}

void shell_runtime_resume_terminal(void) {
    shell_input_resume_terminal(wm_is_active());
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
                if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
                    desktop_set_active(0);
                }
                fm_run();
            } else {
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
                video_print("Erro: Task Manager indisponivel.\n", 0x0C);
            }
            break;
        case IPC_APP_OPEN_TASKMANAGER_GUI:
            if (!recovery_is_enabled(RECOVERY_COMPONENT_TASKMANAGER)) {
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
                    video_print("Erro: System Updater indisponivel.\n",
                                0x0C);
                }
            } else {
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
                    video_print("Erro: App Store indisponivel.\n", 0x0C);
                }
            } else {
                video_print("Erro: App Store indisponivel.\n", 0x0C);
            }
            break;
        default:
            LOG_ERROR("SHELL", "Solicitacao de aplicativo invalida");
            break;
    }
}

static void shell_redraw_after_overlay_close(void) {
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
}


void shell_runtime_finish_command(void) {
    shell_runtime_reset_input();
    if (shell_should_show_prompt()) shell_print_prompt();
    /* Uma conclusao de job e uma fronteira de renderizacao: relatorios
       extensos podem deixar uma atualizacao parcial pendente no backbuffer. */
    video_flush_updates();
}



void shell_report_user_test_result(void) {
    shell_checks_report_user_test_result();
}



void shell_report_app_loader_result(void) {
    app_loader_result_t result;

    /* Mantem o resultado no loader enquanto uma UI nativa cobre o terminal. */
    if (!video_terminal_is_active()) return;
    if (app_loader_take_finished_result(&result) != OK) return;

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
    shell_input_init();
    shell_job_reset();
    shell_hosted_reset();
    shell_diagnostics_reset();
}

void shell_print_prompt(void) {
    shell_input_print_prompt(wm_is_active());
}

static int shell_should_show_prompt(void) {
    if (shell_runtime_is_hosted_visible()) {
        return !shell_checks_input_blocked() && !shell_job_input_blocked() &&
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
}

static void process_input(void) {
    const char* input = shell_input_get_buffer();

    if (!input[0]) {
        shell_print_prompt();
        return;
    }

    shell_process_command(input);

    shell_runtime_reset_input();
    if (shell_should_show_prompt()) {
        shell_print_prompt();
    }
}

void shell_runtime_handle_terminal_key(uint8_t scancode) {
    shell_input_event_t event = shell_input_handle_key(
        scancode, wm_is_active(), shell_checks_input_blocked());
    if (event == SHELL_INPUT_EVENT_COMMAND_READY) process_input();
}

void shell_handle_key(uint8_t scancode) {
    if (shell_job_is_active()) {
        if (shell_checks_handle_job_key(scancode)) return;
        shell_job_handle_key(scancode);
        return;
    }

    int config_result = taskbar_handle_config_key(scancode);
    if (config_result) {
        shell_input_cancel_extended();
        if (config_result == 9) {
            shell_redraw_after_overlay_close();
        }
        return;
    }

    int tb_result = taskbar_handle_key(scancode);
    if (tb_result) {
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
        guitest_handle_key(scancode);
        return;
    }

    if (wm_is_active()) {
        if (wm_handle_key(scancode) == WM_RESULT_EXIT) {
            wm_set_active(0);
            shell_runtime_reset_input();
            video_terminal_begin();
            shell_print_prompt();
            taskbar_draw();
        }
        return;
    }

    if (taskmgr_is_gui_open()) {
        taskmgr_gui_handle_key(scancode);
        return;
    }

    if (settings_is_open()) {
        settings_handle_key(scancode);
        return;
    }

    if (updater_is_open()) {
        updater_handle_key(scancode);
        return;
    }

    if (appstore_is_open()) {
        appstore_handle_key(scancode);
        return;
    }

    if (desktop_is_active()) {
        int result = desktop_handle_key(scancode);
        if (result == -1) {
            desktop_set_active(0);
            shell_runtime_reset_input();
            video_terminal_begin();
            shell_print_prompt();
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
        taskmgr_handle_key(scancode);
        return;
    }

    shell_runtime_handle_terminal_key(scancode);
}

int shell_process_command(const char* input) {
    if (!input) {
        LOG_ERROR("SHELL", "Comando nulo recebido");
        return ERR_NULL;
    }

    shell_runtime_resume_terminal();
    return shell_dispatch_execute(input);
}
