#include "apps/shell.h"
#include "core/video.h"
#include "core/keyboard.h"
#include "fs/fs.h"
#include "core/memory.h"
#include "core/timer.h"
#include "process/process.h"
#include "drivers/speaker.h"
#include "process/thread.h"
#include "apps/taskmanager.h"
#include "ui/taskbar.h"
#include "ui/desktop.h"
#include "ui/settings.h"
#include "ui/wm.h"
#include "memory/compress.h"
#include "apps/mediaplayer.h"
#include "apps/editor.h"
#include "ui/filemanager.h"
#include "memory/paging.h"
#include "core/string.h"
#include "core/errors.h"
#include "core/log.h"
#include "drivers/mouse.h"
#include "ui/gui.h"
#include "apps/guitest.h"
#include "core/recovery.h"

static char input_buffer[SHELL_BUFFER_SIZE];
static int input_pos = 0;

void shell_handle_app_request(uint32_t request) {
    if (taskmgr_is_gui_open() && request != IPC_APP_OPEN_TASKMANAGER_GUI) {
        taskmgr_close();
    }
    if (settings_is_open() && request != IPC_APP_OPEN_SETTINGS) {
        settings_close();
    }

    switch ((ipc_app_request_t)request) {
        case IPC_APP_OPEN_SHELL:
            desktop_set_active(0);
            video_clear();
            shell_print_prompt();
            taskbar_draw();
            break;
        case IPC_APP_OPEN_EXPLORER:
            if (recovery_is_enabled(RECOVERY_COMPONENT_FILEMANAGER)) {
                desktop_set_active(0);
                fm_run();
            } else {
                video_print("Erro: File Manager indisponivel.\n", 0x0C);
            }
            break;
        case IPC_APP_OPEN_TASKMANAGER:
            if (recovery_is_enabled(RECOVERY_COMPONENT_TASKMANAGER)) {
                desktop_set_active(0);
                taskmgr_run();
            } else {
                video_print("Erro: Task Manager indisponivel.\n", 0x0C);
            }
            break;
        case IPC_APP_OPEN_TASKMANAGER_GUI:
            if (!recovery_is_enabled(RECOVERY_COMPONENT_TASKMANAGER)) {
                video_print("Erro: Task Manager indisponivel.\n", 0x0C);
                break;
            }
            desktop_set_active(0);
            if (taskmgr_open_gui() != OK) {
                LOG_WARN("SHELL", "GUI do Task Manager indisponivel; usando TUI");
                taskmgr_run();
            }
            break;
        case IPC_APP_OPEN_DESKTOP:
            video_clear();
            desktop_set_active(1);
            desktop_draw();
            taskbar_draw();
            break;
        case IPC_APP_OPEN_SETTINGS:
            if (recovery_is_enabled(RECOVERY_COMPONENT_SETTINGS)) {
                desktop_set_active(0);
                settings_open();
            } else {
                video_print("Erro: Configuracoes indisponiveis.\n", 0x0C);
            }
            break;
        default:
            LOG_ERROR("SHELL", "Solicitacao de aplicativo invalida");
            break;
    }
}

static void shell_redraw_after_overlay_close(void) {
    /* O menu grafico nao atualiza o buffer de texto; limpe-o antes de redesenhar. */
    video_clear();

    if (desktop_is_active()) {
        desktop_draw();
        taskbar_draw();
        return;
    }

    if (wm_is_active()) {
        wm_draw_all();
        taskbar_draw();
        return;
    }

    if (guitest_is_active()) {
        taskbar_draw();
        guitest_draw();
        return;
    }

    shell_print_prompt();
    taskbar_draw();
}



static void str_upper(char* str) {
    while (*str) {
        if (*str >= 'a' && *str <= 'z') {
            *str -= 32;
        }
        str++;
    }
}

static void print_num(uint32_t num) {
    char buf[16];
    int i = 0;
    if (num == 0) { buf[i++] = '0'; }
    else {
        char tmp[16];
        int j = 0;
        while (num > 0) { tmp[j++] = '0' + (num % 10); num /= 10; }
        while (j > 0) { buf[i++] = tmp[--j]; }
    }
    buf[i] = '\0';
    video_print(buf, 0x07);
}

static uint32_t parse_number(const char* str) {
    uint32_t num = 0;
    while (*str >= '0' && *str <= '9') {
        num = num * 10 + (*str - '0');
        str++;
    }
    return num;
}

static void cmd_help(void) {
    video_print("Comandos disponiveis:\n", 0x0B);
    video_print("  help     - Mostra esta mensagem\n", 0x07);
    video_print("  clear    - Limpa a tela\n", 0x07);
    video_print("  desktop  - Abre a area de trabalho\n", 0x07);
    video_print("  guimode  - Alterna Desktop classic/modern\n", 0x07);
    video_print("  settings - Abre o painel de configuracoes\n", 0x07);
    video_print("  wm       - Abre gerenciador de janelas\n", 0x07);
    video_print("  ls       - Lista arquivos\n", 0x07);
    video_print("  cat      - Exibe conteudo de arquivo\n", 0x07);
    video_print("  echo     - Exibe texto\n", 0x07);
    video_print("  mem      - Mostra informacoes de memoria\n", 0x07);
    video_print("  procs    - Mostra processos ativos\n", 0x07);
    video_print("  threads  - Mostra threads ativas\n", 0x07);
    video_print("  uptime   - Mostra tempo ligado\n", 0x07);
    video_print("  beep     - Toca um beep (freq duracao_ms)\n", 0x07);
    video_print("  melody   - Toca uma melodia\n", 0x07);
    video_print("  explorer - Abre o gerenciador de arquivos\n", 0x07);
    video_print("  taskmgr  - Abre o gerenciador de tarefas\n", 0x07);
    video_print("  taskcfg  - Configura a barra de tarefas\n", 0x07);
    video_print("  compress - Liga/desliga compressao de RAM\n", 0x07);
    video_print("  stats    - Mostra estatisticas de compressao\n", 0x07);
    video_print("  mouse    - Mostra status do mouse PS/2\n", 0x07);
    video_print("  health   - Mostra estado dos componentes\n", 0x07);
    video_print("  play     - Toca arquivo WAV\n", 0x07);
    video_print("  view     - Exibe imagem BMP\n", 0x07);
    video_print("  stop     - Para player de midia\n", 0x07);
    video_print("  edit     - Editor de texto\n", 0x07);
    video_print("             edit (novo) | edit arquivo.txt\n", 0x08);
    video_print("  reboot   - Reinicia o sistema\n", 0x07);
    video_print("  shutdown - Desliga o sistema\n", 0x07);
    video_print("  guitest  - Testa primitivas GUI 2D\n", 0x07);
}

static void cmd_health_print_component(recovery_component_id_t component) {
    const recovery_component_t* entry = recovery_get(component);

    if (!entry) return;

    video_print("  ", 0x07);
    video_print(entry->name, 0x0B);
    video_print(": ", 0x07);
    video_print(recovery_state_name(entry->state), 0x0F);
    video_print("  falhas=", 0x08);
    print_num(entry->failures);
    video_print("  erro=", 0x08);
    print_num((uint32_t)entry->last_error);
    video_print("\n", 0x07);
    video_print("    motivo: ", 0x08);
    video_print(entry->last_message, 0x07);
    video_print("\n", 0x07);
}

static const char* shell_process_state_name(process_state_t state) {
    switch (state) {
        case PROCESS_STATE_READY: return "READY";
        case PROCESS_STATE_RUNNING: return "RUNNING";
        case PROCESS_STATE_BLOCKED: return "BLOCKED";
        case PROCESS_STATE_ZOMBIE: return "ZOMBIE";
        default: return "UNUSED";
    }
}

static void cmd_health_print_kernel(void) {
    process_t* current = process_get_current();
    ipc_stats_t ipc;

    ipc_get_stats(&ipc);
    video_print("\nEstado do kernel:\n", 0x0B);
    video_print("  Processo atual: PID ", 0x07);
    print_num(process_get_current_pid());
    video_print("  estado=", 0x08);
    video_print(current ? shell_process_state_name(current->state) : "N/D", 0x07);
    video_print("\n", 0x07);

    video_print("  Processos: total=", 0x07);
    print_num(process_get_count());
    video_print(" READY=", 0x08);
    print_num(process_get_state_count(PROCESS_STATE_READY));
    video_print(" RUNNING=", 0x08);
    print_num(process_get_state_count(PROCESS_STATE_RUNNING));
    video_print(" BLOCKED=", 0x08);
    print_num(process_get_state_count(PROCESS_STATE_BLOCKED));
    video_print(" ZOMBIE=", 0x08);
    print_num(process_get_state_count(PROCESS_STATE_ZOMBIE));
    video_print("\n", 0x07);

    video_print("  Threads: ", 0x07);
    print_num(thread_get_count());
    video_print("  ticks=", 0x08);
    print_num(timer_get_ticks());
    video_print("\n", 0x07);

    video_print("  IPC: foco=", 0x07);
    print_num(process_get_focus());
    video_print(" enviados=", 0x08);
    print_num(ipc.sent);
    video_print(" recebidos=", 0x08);
    print_num(ipc.received);
    video_print(" falhas=", 0x08);
    print_num(ipc.failed);
    video_print(" filas_cheias=", 0x08);
    print_num(ipc.queue_full);
    video_print("\n", 0x07);

    video_print("  Paging: ", 0x07);
    video_print(paging_is_ready() ? "READY" : "DISABLED", 0x0F);
    video_print("\n", 0x07);
    video_print("  Memoria KB: total=", 0x07);
    print_num(memory_get_total() / 1024);
    video_print(" usada=", 0x08);
    print_num(memory_get_used() / 1024);
    video_print(" livre=", 0x08);
    print_num(memory_get_free() / 1024);
    video_print(" paginas_livres=", 0x08);
    print_num(memory_get_free_pages());
    video_print("\n", 0x07);
    video_print("  Paginas: total=", 0x07);
    print_num(memory_get_total_pages());
    video_print("\n", 0x07);
}

static void cmd_health(void) {
    video_print("Estado dos componentes:\n", 0x0B);

    for (uint32_t i = 0; i < recovery_get_count(); i++) {
        cmd_health_print_component((recovery_component_id_t)i);
    }

    cmd_health_print_kernel();
}

static void cmd_guimode(const char* args) {
    char mode_name[16];
    int i = 0;
    int result;

    if (!args || !*args) {
        video_print("Modo Desktop: ", 0x0B);
        video_print(desktop_get_mode() == DESKTOP_MODE_MODERN ?
                    "modern\n" : "classic\n", 0x07);
        return;
    }

    while (args[i] && args[i] != ' ' && i < (int)sizeof(mode_name) - 1) {
        mode_name[i] = args[i];
        i++;
    }
    mode_name[i] = '\0';

    if (kstrcmp(mode_name, "classic") == 0) {
        result = desktop_set_mode(DESKTOP_MODE_CLASSIC);
        if (result == OK) {
            video_print("Desktop em modo classic.\n", 0x0A);
        }
        return;
    }

    if (kstrcmp(mode_name, "modern") == 0) {
        if (!recovery_is_available(RECOVERY_COMPONENT_VESA) ||
            !recovery_is_available(RECOVERY_COMPONENT_BACKBUFFER)) {
            video_print("Modo modern indisponivel; classic mantido.\n", 0x0C);
            return;
        }

        result = desktop_set_mode(DESKTOP_MODE_MODERN);
        if (result == OK) {
            video_print("Desktop em modo modern.\n", 0x0A);
        } else if (result == ERR_NOT_FOUND) {
            video_print("Modo modern requer VESA; classic mantido.\n", 0x0C);
        }
        return;
    }

    video_print("Uso: guimode classic|modern\n", 0x0C);
}

static void cmd_clear(void) {
    video_clear();
    taskbar_draw();
}

static void cmd_ls(void) {
    if (!recovery_is_available(RECOVERY_COMPONENT_FILESYSTEM)) {
        video_print("Erro: filesystem indisponivel.\n", 0x0C);
        return;
    }

    video_print("Arquivos no disco:\n", 0x0B);
    int count = fs_list_dir();
    if (count == 0) {
        video_print("  (vazio)\n", 0x08);
    }
}

static void cmd_cat(const char* filename) {
    if (!recovery_is_available(RECOVERY_COMPONENT_FILESYSTEM)) {
        video_print("Erro: filesystem indisponivel.\n", 0x0C);
        return;
    }

    if (!filename || !*filename) {
        video_print("Uso: cat <arquivo>\n", 0x0C);
        return;
    }

    char name[12];
    int i = 0;
    while (filename[i] && i < 11) {
        name[i] = filename[i];
        i++;
    }
    name[i] = '\0';
    str_upper(name);

    uint8_t* buffer = (uint8_t*)kmalloc(4096);
    if (!buffer) {
        video_print("Erro: sem memoria!\n", 0x0C);
        return;
    }

    int bytes = fs_read_file(name, buffer, 4095);
    if (bytes < 0) {
        video_print("Erro: arquivo nao encontrado: ", 0x0C);
        video_print(filename, 0x0C);
        video_print("\n", 0x0C);
        kfree(buffer);
        buffer = 0;
        return;
    }

    if (bytes == 0) {
        video_print("(arquivo vazio)\n", 0x08);
        kfree(buffer);
        buffer = 0;
        return;
    }

    buffer[bytes] = '\0';
    video_print((char*)buffer, 0x07);
    video_print("\n", 0x07);
    kfree(buffer);
    buffer = 0;
}

static void cmd_echo(const char* text) {
    if (text && *text) {
        video_print(text, 0x07);
    }
    video_print("\n", 0x07);
}

static void cmd_mem(void) {
    video_print("Memoria:\n", 0x0B);
    video_print("  Total: ", 0x07);
    print_num(memory_get_total() / 1024);
    video_print(" KB\n", 0x07);

    video_print("  Livre: ", 0x07);
    print_num(memory_get_free() / 1024);
    video_print(" KB\n", 0x07);

    video_print("  Usada: ", 0x07);
    print_num(memory_get_used() / 1024);
    video_print(" KB\n", 0x07);
}

static void cmd_procs(void) {
    video_print("Processos ativos:\n", 0x0B);

    extern process_t processes[];
    extern uint32_t process_count;

    const char* state_names[] = {"UNUSED", "READY", "RUNNING", "BLOCKED", "ZOMBIE"};

    for (int i = 0; i < 64; i++) {
        if (processes[i].state != 0) {
            video_print("  PID ", 0x07);
            print_num(processes[i].pid);
            video_print("  ", 0x07);
            video_print(processes[i].name, 0x0B);
            video_print("  ", 0x07);
            video_print(state_names[processes[i].state], 0x08);
            video_print("\n", 0x07);
        }
    }

    video_print("Total: ", 0x07);
    print_num(process_count);
    video_print(" processos\n", 0x07);
}

static void cmd_threads(void) {
    video_print("Threads ativas:\n", 0x0B);

    const char* state_names[] = {"UNUSED", "RUNNING", "BLOCKED", "FINISHED"};
    uint32_t count = thread_get_count();

    for (uint32_t i = 0; i < 32; i++) {
        thread_t* t = thread_get_by_id(i + 1);
        if (t) {
            video_print("  TID ", 0x07);
            print_num(t->id);
            video_print("  ", 0x07);
            video_print(t->name, 0x0B);
            video_print("  ", 0x07);
            video_print(state_names[t->state], 0x08);
            video_print("\n", 0x07);
        }
    }

    video_print("Total: ", 0x07);
    print_num(count);
    video_print(" threads\n", 0x07);
}

static void cmd_uptime(void) {
    uint32_t ticks = timer_get_ticks();
    uint32_t seconds = ticks / 50;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;

    video_print("Uptime: ", 0x0B);
    print_num(hours);
    video_print("h ", 0x07);
    print_num(minutes % 60);
    video_print("m ", 0x07);
    print_num(seconds % 60);
    video_print("s\n", 0x07);
}

static void cmd_beep(const char* args) {
    if (!args || !*args) {
        speaker_beep(800, 200);
        video_print("Beep!\n", 0x0A);
        return;
    }

    uint32_t freq = parse_number(args);
    while (*args && *args != ' ') args++;
    while (*args == ' ') args++;

    uint32_t dur = parse_number(args);
    if (dur == 0) dur = 200;
    if (freq == 0) freq = 800;

    speaker_beep(freq, dur);
    video_print("Beep! (", 0x0A);
    print_num(freq);
    video_print(" Hz, ", 0x0A);
    print_num(dur);
    video_print(" ms)\n", 0x0A);
}

static void cmd_melody(void) {
    video_print("Tocando melodia...\n", 0x0A);

    uint32_t freqs[] = {523, 587, 659, 698, 784, 880, 988, 1047};
    uint32_t durs[] =  {200, 200, 200, 200, 200, 200, 200, 400};

    speaker_play_melody(freqs, durs, 8);
    video_print("Melodia concluida!\n", 0x0A);
}

static void cmd_reboot(void) {
    video_print("Reiniciando...\n", 0x0E);
    asm volatile("cli");
    asm volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
    for (;;) asm volatile("hlt");
}

static void cmd_shutdown(void) {
    video_print("Desligando...\n", 0x0E);
    asm volatile("cli");
    for (;;) asm volatile("hlt");
}

void shell_init(void) {
    input_pos = 0;
    input_buffer[0] = '\0';
    
    shell_print_prompt();
}

void shell_print_prompt(void) {
    video_print(SHELL_PROMPT, 0x0A);
}

static void process_input(void) {
    video_print("\n", 0x07);

    if (input_pos == 0) {
        shell_print_prompt();
        return;
    }

    input_buffer[input_pos] = '\0';
    shell_process_command(input_buffer);

    input_pos = 0;
    input_buffer[0] = '\0';
    shell_print_prompt();
}

void shell_handle_key(uint8_t scancode) {
    int config_result = taskbar_handle_config_key(scancode);
    if (config_result) {
        if (config_result == 9) {
            shell_redraw_after_overlay_close();
        }
        return;
    }

    int tb_result = taskbar_handle_key(scancode);
    if (tb_result) {
        if (tb_result == 2) {
            shell_handle_app_request(IPC_APP_OPEN_SHELL);
        } else if (tb_result == 3) {
            shell_handle_app_request(IPC_APP_OPEN_EXPLORER);
        } else if (tb_result == 4) {
            shell_handle_app_request(IPC_APP_OPEN_TASKMANAGER_GUI);
        } else if (tb_result == 5) {
            cmd_reboot();
        } else if (tb_result == 6) {
            cmd_shutdown();
        } else if (tb_result == 7) {
            shell_handle_app_request(IPC_APP_OPEN_DESKTOP);
        } else if (tb_result == 8) {
            shell_handle_app_request(IPC_APP_OPEN_SETTINGS);
        } else if (tb_result == 9) {
            shell_redraw_after_overlay_close();
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

    if (wm_is_active()) {
        wm_handle_key(scancode);
        return;
    }

    if (desktop_is_active()) {
        int result = desktop_handle_key(scancode);
        if (result == -1) {
            desktop_set_active(0);
            video_clear();
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

    static const char scancode_table[128] = {
        0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
        '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
        0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
        0,  '\\','z','x','c','v','b','n','m',',','.','/',0,
        '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };

    if (scancode & 0x80) return;

    char c = scancode_table[scancode];

    if (scancode == 0x0E) {
        if (input_pos > 0) {
            input_pos--;
            input_buffer[input_pos] = '\0';
            video_backspace();
        }
        return;
    }

    if (scancode == 0x1C) {
        process_input();
        return;
    }

    if (c && input_pos < SHELL_BUFFER_SIZE - 1) {
        input_buffer[input_pos++] = c;
        input_buffer[input_pos] = '\0';
        video_put_char(c, 0x07);
    }

    taskbar_update_clock();
}

int shell_process_command(const char* input) {
    while (*input == ' ') input++;

    if (!*input) return 0;

    char cmd[32];
    int i = 0;
    while (*input && *input != ' ' && i < 31) {
        cmd[i++] = *input++;
    }
    cmd[i] = '\0';

    while (*input == ' ') input++;

    if (kstrcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (kstrcmp(cmd, "clear") == 0) {
        cmd_clear();
    } else if (kstrcmp(cmd, "ls") == 0) {
        cmd_ls();
    } else if (kstrcmp(cmd, "cat") == 0) {
        cmd_cat(input);
    } else if (kstrcmp(cmd, "echo") == 0) {
        cmd_echo(input);
    } else if (kstrcmp(cmd, "mem") == 0) {
        cmd_mem();
    } else if (kstrcmp(cmd, "procs") == 0) {
        cmd_procs();
    } else if (kstrcmp(cmd, "threads") == 0) {
        cmd_threads();
    } else if (kstrcmp(cmd, "uptime") == 0) {
        cmd_uptime();
    } else if (kstrcmp(cmd, "health") == 0) {
        cmd_health();
    } else if (kstrcmp(cmd, "beep") == 0) {
        cmd_beep(input);
    } else if (kstrcmp(cmd, "melody") == 0) {
        cmd_melody();
    } else if (kstrcmp(cmd, "desktop") == 0) {
        if (!desktop_is_active()) {
            desktop_set_active(1);
            desktop_draw();
        }
    } else if (kstrcmp(cmd, "guimode") == 0) {
        cmd_guimode(input);
    } else if (kstrcmp(cmd, "explorer") == 0) {
        if (!recovery_is_enabled(RECOVERY_COMPONENT_FILEMANAGER)) {
            video_print("Erro: Explorer indisponivel.\n", 0x0C);
        } else {
            fm_run();
        }
    } else if (kstrcmp(cmd, "reboot") == 0) {
        cmd_reboot();
    } else if (kstrcmp(cmd, "shutdown") == 0) {
        cmd_shutdown();
    } else if (kstrcmp(cmd, "guitest") == 0) {
        if (recovery_is_enabled(RECOVERY_COMPONENT_GUITEST)) {
            guitest_open();
        } else {
            video_print("Erro: GUI Test indisponivel.\n", 0x0C);
        }
    } else if (kstrcmp(cmd, "taskmgr") == 0) {
        if (recovery_is_enabled(RECOVERY_COMPONENT_TASKMANAGER)) {
            taskmgr_run();
        } else {
            video_print("Erro: Task Manager indisponivel.\n", 0x0C);
        }
    } else if (kstrcmp(cmd, "taskcfg") == 0) {
        taskbar_draw_config_menu();
    } else if (kstrcmp(cmd, "settings") == 0) {
        if (recovery_is_enabled(RECOVERY_COMPONENT_SETTINGS)) {
            settings_open();
        } else {
            video_print("Erro: Configuracoes indisponiveis.\n", 0x0C);
        }
    } else if (kstrcmp(cmd, "wm") == 0) {
        if (recovery_is_enabled(RECOVERY_COMPONENT_WM)) {
            wm_set_active(1);
        } else {
            video_print("Erro: Window Manager indisponivel.\n", 0x0C);
        }
    } else if (kstrcmp(cmd, "play") == 0) {
        if (!recovery_is_available(RECOVERY_COMPONENT_FILESYSTEM) ||
            !recovery_is_enabled(RECOVERY_COMPONENT_MEDIAPLAYER) ||
            !recovery_is_available(RECOVERY_COMPONENT_AC97)) {
            video_print("Erro: audio ou filesystem indisponivel.\n", 0x0C);
        } else if (!*input) {
            video_print("Uso: play <arquivo.wav>\n", 0x0C);
        } else {
            char name[13];
            int n = 0;
            while (input[n] && n < 12) { name[n] = input[n]; n++; }
            name[n] = '\0';
            str_upper(name);
            video_print("Tocando: ", 0x0A);
            video_print(name, 0x0A);
            video_print("\n", 0x0A);
            int play_result = mp_play_audio(name);
            if (play_result != OK) {
                video_print("Erro: nao foi possivel reproduzir o audio.\n", 0x0C);
            }
        }
    } else if (kstrcmp(cmd, "view") == 0) {
        if (!recovery_is_available(RECOVERY_COMPONENT_FILESYSTEM) ||
            !recovery_is_enabled(RECOVERY_COMPONENT_MEDIAPLAYER) ||
            !recovery_is_enabled(RECOVERY_COMPONENT_VESA)) {
            video_print("Erro: imagem ou filesystem indisponivel.\n", 0x0C);
        } else if (!*input) {
            video_print("Uso: view <arquivo.bmp>\n", 0x0C);
        } else {
            char name[13];
            int n = 0;
            while (input[n] && n < 12) { name[n] = input[n]; n++; }
            name[n] = '\0';
            str_upper(name);
            int view_result = mp_play_image(name);
            if (view_result != OK) {
                video_print("Erro: nao foi possivel exibir a imagem.\n", 0x0C);
            }
        }
    } else if (kstrcmp(cmd, "stop") == 0) {
        mp_stop();
        video_print("Player parado.\n", 0x0A);
    } else if (kstrcmp(cmd, "compress") == 0) {
        if (compress_is_enabled()) {
            compress_disable();
            video_print("Compressao de RAM DESATIVADA\n", 0x0C);
        } else {
            compress_enable();
            video_print("Compressao de RAM ATIVADA\n", 0x0A);
        }
    } else if (kstrcmp(cmd, "stats") == 0) {
        compress_print_stats();
    } else if (kstrcmp(cmd, "edit") == 0) {
        if (!recovery_is_enabled(RECOVERY_COMPONENT_EDITOR)) {
            video_print("Erro: Editor indisponivel.\n", 0x0C);
        } else if (*input) {
            char name[13];
            int n = 0;
            while (input[n] && n < 12) { name[n] = input[n]; n++; }
            name[n] = '\0';
            str_upper(name);
            editor_run_file(name);
        } else {
            editor_run();
        }
    } else if (kstrcmp(cmd, "mouse") == 0) {
        video_print("Mouse PS/2:\n", 0x0B);
        video_print("  X: ", 0x07);
        print_num(mouse_get_x());
        video_print("\n  Y: ", 0x07);
        print_num(mouse_get_y());
        video_print("\n  Botoes: ", 0x07);
        print_num(mouse_get_buttons());
        video_print("\n", 0x07);
    } else {
        video_print("Comando nao encontrado: ", 0x0C);
        video_print(cmd, 0x0C);
        video_print("\n", 0x0C);
        video_print("Digite 'help' para ver os comandos.\n", 0x08);
    }

    return 0;
}
