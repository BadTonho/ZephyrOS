#include "ui/settings.h"
#include "core/video.h"
#include "core/keyboard.h"
#include "ui/taskbar.h"
#include "ui/desktop.h"
#include "drivers/speaker.h"
#include "core/memory.h"
#include "core/timer.h"
#include "process/process.h"
#include "process/thread.h"
#include "ui/wm.h"
#include "ui/icons.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/errors.h"
#include "drivers/vesa.h"
#include "drivers/font.h"
#include "ui/gui.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

#define SETTINGS_MODERN_MIN_WIDTH 600
#define SETTINGS_MODERN_MIN_HEIGHT 400
#define SETTINGS_MODERN_DEFAULT_WIDTH 720
#define SETTINGS_MODERN_DEFAULT_HEIGHT 520
#define SETTINGS_MODERN_TASKBAR_HEIGHT 24
#define SETTINGS_MODERN_MARGIN 12
#define SETTINGS_MODERN_SIDE_WIDTH 184
#define SETTINGS_MODERN_TITLE_HEIGHT 22
#define SETTINGS_MODERN_ROW_HEIGHT 32
#define SETTINGS_MODERN_TAB_HEIGHT 24
#define SETTINGS_MODERN_DIALOG_WIDTH 480
#define SETTINGS_MODERN_DIALOG_HEIGHT 300

typedef enum {
    SETTINGS_DIALOG_NONE = 0,
    SETTINGS_DIALOG_SYSTEM_INFO,
    SETTINGS_DIALOG_MEMORY,
    SETTINGS_DIALOG_PROCESSES,
    SETTINGS_DIALOG_VERSION,
    SETTINGS_DIALOG_CREDITS
} settings_dialog_t;

static int settings_active = 0;
static int selected_category = 0;
static int selected_option = 0;
static int editing_option = 0;
static int icon_editor_active = 0;
static int icon_editor_selected = 0;
static int icon_editor_field = 0;
static int icon_editor_count = 0;
static icon_entry_t* icon_editor_entries = 0;
static const char* icon_editor_title = 0;
static const char** icon_editor_names = 0;
static settings_mode_t settings_mode = SETTINGS_MODE_CLASSIC;
static settings_dialog_t settings_dialog = SETTINGS_DIALOG_NONE;
static int settings_gui_x = 0;
static int settings_gui_y = 0;
static int settings_gui_width = SETTINGS_MODERN_DEFAULT_WIDTH;
static int settings_gui_height = SETTINGS_MODERN_DEFAULT_HEIGHT;

static settings_page_t categories[SETTINGS_CAT_COUNT];

static const char* display_theme_values[] = {"Classico", "Escuro", "Azul"};
static const char* display_resolution_values[] = {"80x25", "80x50", "Auto"};
static const char* taskbar_position_values[] = {"Baixo", "Cima", "Esquerda", "Direita", "Custom"};
static const char* taskbar_size_values[] = {"Pequeno", "Medio", "Grande"};
static const char* windows_button_side_values[] = {"Direita", "Esquerda"};
static const char* windows_button_order_values[] = {
    "x _ <>", "x <> _", "_ <> x", "<> _ x", "_ x <>", "<> x _"
};
static const char* windows_border_values[] = {"Simples", "Dupla"};
static const char* sound_volume_values[] = {"Mudo", "Baixo", "Medio", "Alto", "Maximo"};

static void settings_draw_modern_main(void);
static void settings_draw_modern_icon_editor(void);
static void settings_draw_modern_dialog(void);
static int settings_modern_layout(void);
static void settings_modern_draw(void);
static void settings_apply_category(void);
static void settings_execute_selected_action(void);
static void settings_clear_overlay(void);

static void init_categories(void) {
    for (int i = 0; i < SETTINGS_CAT_COUNT; i++) {
        categories[i].option_count = 0;
    }

    categories[SETTINGS_CAT_DISPLAY].name = "Tela";
    categories[SETTINGS_CAT_DISPLAY].option_count = 3;
    categories[SETTINGS_CAT_DISPLAY].options[0] = (settings_option_t){
        "Tema", SETTINGS_OPT_LIST, 0, 2,
        display_theme_values, 3
    };
    categories[SETTINGS_CAT_DISPLAY].options[1] = (settings_option_t){
        "Resolucao", SETTINGS_OPT_LIST, 0, 2,
        display_resolution_values, 3
    };
    categories[SETTINGS_CAT_DISPLAY].options[2] = (settings_option_t){
        "Mostrar grade", SETTINGS_OPT_TOGGLE, 0, 1, NULL, 0
    };

    categories[SETTINGS_CAT_TASKBAR].name = "Barra de Tarefas";
    categories[SETTINGS_CAT_TASKBAR].option_count = 5;
    categories[SETTINGS_CAT_TASKBAR].options[0] = (settings_option_t){
        "Posicao", SETTINGS_OPT_LIST, 0, 4,
        taskbar_position_values, 5
    };
    categories[SETTINGS_CAT_TASKBAR].options[1] = (settings_option_t){
        "Tamanho icone", SETTINGS_OPT_LIST, 1, 2,
        taskbar_size_values, 3
    };
    categories[SETTINGS_CAT_TASKBAR].options[2] = (settings_option_t){
        "Fixada", SETTINGS_OPT_TOGGLE, 1, 1, NULL, 0
    };
    categories[SETTINGS_CAT_TASKBAR].options[3] = (settings_option_t){
        "Mostrar relogio", SETTINGS_OPT_TOGGLE, 1, 1, NULL, 0
    };
    categories[SETTINGS_CAT_TASKBAR].options[4] = (settings_option_t){
        "Auto-ocultar", SETTINGS_OPT_TOGGLE, 0, 1, NULL, 0
    };

    categories[SETTINGS_CAT_WINDOWS].name = "Janelas";
    categories[SETTINGS_CAT_WINDOWS].option_count = 4;
    categories[SETTINGS_CAT_WINDOWS].options[0] = (settings_option_t){
        "Botoes lado", SETTINGS_OPT_LIST, 0, 1,
        windows_button_side_values, 2
    };
    categories[SETTINGS_CAT_WINDOWS].options[1] = (settings_option_t){
        "Ordem botoes", SETTINGS_OPT_LIST, 0, 5,
        windows_button_order_values, 6
    };
    categories[SETTINGS_CAT_WINDOWS].options[2] = (settings_option_t){
        "Mostrar titulo", SETTINGS_OPT_TOGGLE, 1, 1, NULL, 0
    };
    categories[SETTINGS_CAT_WINDOWS].options[3] = (settings_option_t){
        "Borda", SETTINGS_OPT_LIST, 0, 1,
        windows_border_values, 2
    };

    categories[SETTINGS_CAT_ICONS].name = "Icones";
    categories[SETTINGS_CAT_ICONS].option_count = 4;
    categories[SETTINGS_CAT_ICONS].options[0] = (settings_option_t){
        "Desktop", SETTINGS_OPT_ACTION, 0, 0, NULL, 0
    };
    categories[SETTINGS_CAT_ICONS].options[1] = (settings_option_t){
        "Janela (WM)", SETTINGS_OPT_ACTION, 0, 0, NULL, 0
    };
    categories[SETTINGS_CAT_ICONS].options[2] = (settings_option_t){
        "Arquivos", SETTINGS_OPT_ACTION, 0, 0, NULL, 0
    };
    categories[SETTINGS_CAT_ICONS].options[3] = (settings_option_t){
        "Restaurar padrao", SETTINGS_OPT_ACTION, 0, 0, NULL, 0
    };

    categories[SETTINGS_CAT_SYSTEM].name = "Sistema";
    categories[SETTINGS_CAT_SYSTEM].option_count = 4;
    categories[SETTINGS_CAT_SYSTEM].options[0] = (settings_option_t){
        "Nome do PC", SETTINGS_OPT_ACTION, 0, 0, NULL, 0
    };
    categories[SETTINGS_CAT_SYSTEM].options[1] = (settings_option_t){
        "Info memoria", SETTINGS_OPT_ACTION, 0, 0, NULL, 0
    };
    categories[SETTINGS_CAT_SYSTEM].options[2] = (settings_option_t){
        "Processos", SETTINGS_OPT_ACTION, 0, 0, NULL, 0
    };
    categories[SETTINGS_CAT_SYSTEM].options[3] = (settings_option_t){
        "Reiniciar", SETTINGS_OPT_ACTION, 0, 0, NULL, 0
    };

    categories[SETTINGS_CAT_SOUND].name = "Som";
    categories[SETTINGS_CAT_SOUND].option_count = 3;
    categories[SETTINGS_CAT_SOUND].options[0] = (settings_option_t){
        "Volume", SETTINGS_OPT_LIST, 2, 4,
        sound_volume_values, 5
    };
    categories[SETTINGS_CAT_SOUND].options[1] = (settings_option_t){
        "Beep ao iniciar", SETTINGS_OPT_TOGGLE, 1, 1, NULL, 0
    };
    categories[SETTINGS_CAT_SOUND].options[2] = (settings_option_t){
        "Som teclado", SETTINGS_OPT_TOGGLE, 0, 1, NULL, 0
    };

    categories[SETTINGS_CAT_ABOUT].name = "Sobre";
    categories[SETTINGS_CAT_ABOUT].option_count = 2;
    categories[SETTINGS_CAT_ABOUT].options[0] = (settings_option_t){
        "Versao", SETTINGS_OPT_ACTION, 0, 0, NULL, 0
    };
    categories[SETTINGS_CAT_ABOUT].options[1] = (settings_option_t){
        "Creditos", SETTINGS_OPT_ACTION, 0, 0, NULL, 0
    };
}

static int settings_modern_layout(void) {
    vesa_mode_t* mode = vesa_get_mode();
    int work_top = 0;
    int work_bottom;
    tb_config_t* taskbar_config;

    if (!mode || !mode->initialized || !vesa_has_backbuffer()) return 0;
    if (mode->width < SETTINGS_MODERN_MIN_WIDTH ||
        mode->height < SETTINGS_MODERN_MIN_HEIGHT) return 0;

    taskbar_config = taskbar_get_config();
    work_bottom = (int)mode->height - SETTINGS_MODERN_TASKBAR_HEIGHT;
    if (taskbar_config && taskbar_config->position == TB_POS_TOP) {
        work_top = SETTINGS_MODERN_TASKBAR_HEIGHT;
        work_bottom = (int)mode->height;
    }

    settings_gui_width = mode->width < SETTINGS_MODERN_DEFAULT_WIDTH ?
                         (int)mode->width - 24 : SETTINGS_MODERN_DEFAULT_WIDTH;
    settings_gui_height = mode->height < SETTINGS_MODERN_DEFAULT_HEIGHT ?
                          work_bottom - work_top - 24 : SETTINGS_MODERN_DEFAULT_HEIGHT;
    if (settings_gui_width < SETTINGS_MODERN_MIN_WIDTH ||
        settings_gui_height < SETTINGS_MODERN_MIN_HEIGHT) return 0;

    settings_gui_x = ((int)mode->width - settings_gui_width) / 2;
    settings_gui_y = work_top + ((work_bottom - work_top) - settings_gui_height) / 2;
    return settings_gui_y >= work_top &&
           settings_gui_y + settings_gui_height <= work_bottom;
}

static void settings_select_mode(void) {
    settings_mode = SETTINGS_MODE_CLASSIC;
    if (desktop_get_mode() != DESKTOP_MODE_MODERN) return;

    if (settings_modern_layout()) {
        settings_mode = SETTINGS_MODE_MODERN;
        return;
    }

    LOG_WARN("SETTINGS", "Visual moderno indisponivel; usando TUI");
}

void settings_init(void) {
    settings_active = 0;
    selected_category = 0;
    selected_option = 0;
    editing_option = 0;
    settings_mode = SETTINGS_MODE_CLASSIC;
    settings_dialog = SETTINGS_DIALOG_NONE;
    init_categories();
}

void settings_open(void) {
    if (!recovery_is_enabled(RECOVERY_COMPONENT_SETTINGS)) {
        LOG_WARN("SETTINGS", "Configuracoes indisponiveis; abertura ignorada");
        settings_active = 0;
        return;
    }

    settings_active = 1;
    icon_editor_active = 0;
    icon_editor_entries = 0;
    icon_editor_names = 0;
    icon_editor_title = 0;
    selected_category = 0;
    selected_option = 0;
    editing_option = 0;
    settings_dialog = SETTINGS_DIALOG_NONE;
    settings_select_mode();
    settings_draw();
}

void settings_close(void) {
    settings_active = 0;
    settings_clear_overlay();
    desktop_set_active(1);
    desktop_draw();
    taskbar_draw();
}

static void settings_draw_classic(void) {
    video_fill_rect(0, 0, SCREEN_COLS, SCREEN_ROWS, ' ', 0x07);

    video_fill_rect(0, 0, SCREEN_COLS, 1, ' ', 0x1F);
    video_print_at((SCREEN_COLS - 28) / 2, 0, " Configuracoes do ZephyrOS ", 0x1F);

    video_fill_rect(0, 1, 25, SCREEN_ROWS - 1, ' ', 0x07);
    video_draw_box(0, 1, 25, SCREEN_ROWS - 3, 0x08);

    for (int i = 0; i < SETTINGS_CAT_COUNT; i++) {
        uint8_t color = (selected_category == i) ? 0x1F : 0x07;
        if (selected_category == i) {
            video_fill_rect(1, 2 + i, 23, 1, ' ', 0x1F);
        }
        video_print_at(2, 2 + i, categories[i].name, color);
    }

    video_draw_box(26, 1, SCREEN_COLS - 28, SCREEN_ROWS - 3, 0x08);

    video_print_at(28, 2, categories[selected_category].name, 0x0F);
    video_draw_hline(27, 3, SCREEN_COLS - 30, 0xC4, 0x08);

    settings_page_t* page = &categories[selected_category];

    for (int i = 0; i < page->option_count; i++) {
        uint8_t color = 0x07;
        if (selected_option == i) {
            color = editing_option ? 0x1E : 0x1F;
            video_fill_rect(27, 4 + i, SCREEN_COLS - 30, 1, ' ', editing_option ? 0x1E : 0x1F);
        }

        video_print_at(28, 4 + i, page->options[i].name, color);

        if (page->options[i].type == SETTINGS_OPT_TOGGLE) {
            video_print_at(55, 4 + i, page->options[i].value ? "[x]" : "[ ]", color);
        } else if (page->options[i].type == SETTINGS_OPT_LIST) {
            int val = page->options[i].value;
            if (page->options[i].list_values &&
                val >= 0 && val < page->options[i].list_count) {
                video_print_at(55, 4 + i, "< ", color);
                video_print_at(57, 4 + i, page->options[i].list_values[val], color);
                video_print_at(57 + 10, 4 + i, " >", color);
            }
        } else if (page->options[i].type == SETTINGS_OPT_ACTION) {
            video_print_at(55, 4 + i, "[Executar]", color);
        }
    }

    video_draw_hline(27, SCREEN_ROWS - 3, SCREEN_COLS - 30, 0xC4, 0x08);
    video_print_at(28, SCREEN_ROWS - 2, "Tab:Categorias  Enter:Alterar  Esc:Fechar", 0x08);
}

void settings_draw(void) {
    vesa_mode_t* mode = vesa_get_mode();

    if (settings_mode == SETTINGS_MODE_MODERN &&
        (!mode || !mode->initialized || !vesa_has_backbuffer())) {
        LOG_WARN("SETTINGS", "Backbuffer indisponivel; retornando para TUI");
        settings_mode = SETTINGS_MODE_CLASSIC;
    }
    if (settings_mode == SETTINGS_MODE_MODERN) {
        settings_modern_draw();
        return;
    }
    settings_draw_classic();
}

static void apply_taskbar_settings(void) {
    settings_page_t* tb = &categories[SETTINGS_CAT_TASKBAR];

    taskbar_set_position((tb_position_t)tb->options[0].value);
    taskbar_set_icon_size((tb_icon_size_t)tb->options[1].value);
    taskbar_set_pinned(tb->options[2].value);
}

static void apply_wm_settings(void) {
    settings_page_t* wm = &categories[SETTINGS_CAT_WINDOWS];
    wm_config_t* cfg = wm_get_config();

    wm_set_btn_position((wm_btn_position_t)wm->options[0].value);
    wm_set_btn_order((wm_btn_order_t)wm->options[1].value);
    wm_set_show_title(wm->options[2].value);
    wm_set_border_style(wm->options[3].value);
}

static void settings_apply_category(void) {
    if (selected_category == SETTINGS_CAT_TASKBAR) {
        apply_taskbar_settings();
    }
    if (selected_category == SETTINGS_CAT_WINDOWS) {
        apply_wm_settings();
    }
}

static void settings_clear_overlay(void) {
    icon_editor_active = 0;
    icon_editor_entries = 0;
    icon_editor_names = 0;
    icon_editor_title = 0;
    icon_editor_count = 0;
    settings_dialog = SETTINGS_DIALOG_NONE;
}

static void int_to_str(uint32_t num, char* buf) {
    int i = 0;
    if (num == 0) { buf[i++] = '0'; }
    else {
        char tmp[16];
        int j = 0;
        while (num > 0) { tmp[j++] = '0' + (num % 10); num /= 10; }
        while (j > 0) { buf[i++] = tmp[--j]; }
    }
    buf[i] = '\0';
}

static const char* desktop_icon_names[] = {"Shell", "Explorer", "TaskMgr"};
static const char* window_icon_names[] = {"Fechar", "Minimizar", "Maximizar"};
static const char* file_icon_names[] = {"Pasta", "Arquivo"};

static void icon_editor_draw_classic(void) {
    video_clear();
    video_print_at(2, 1, icon_editor_title, 0x0F);
    video_draw_hline(2, 2, 76, 0xC4, 0x08);

    for (int i = 0; i < icon_editor_count; i++) {
        uint8_t color = (icon_editor_selected == i) ? 0x1F : 0x07;
        video_print_at(4, 4 + i * 2, icon_editor_names[i], color);
        video_print_at(6, 5 + i * 2, "Caractere:", 0x08);
        video_put_char_at(icon_editor_entries[i].ch, icon_editor_entries[i].color, 17, 5 + i * 2);
        video_print_at(20, 5 + i * 2, "Cor:", 0x08);
        char cbuf[4];
        int_to_str(icon_editor_entries[i].color, cbuf);
        video_print_at(25, 5 + i * 2, cbuf, 0x07);
        video_print_at(30, 5 + i * 2, "Cor sel:", 0x08);
        int_to_str(icon_editor_entries[i].color_selected, cbuf);
        video_print_at(39, 5 + i * 2, cbuf, 0x07);

        if (icon_editor_selected == i) {
            int x = icon_editor_field == 0 ? 15 : (icon_editor_field == 1 ? 23 : 37);
            video_put_char_at('>', 0x0E, x, 5 + i * 2);
        }
    }

    video_draw_hline(2, 22, 76, 0xC4, 0x08);
    video_print_at(3, 23, "Up/Down:Item  Left/Right:Campo  +/-:Valor  Enter:Ok  Esc:Voltar", 0x08);
}

static void icon_editor_draw(void) {
    if (settings_mode == SETTINGS_MODE_MODERN) {
        settings_modern_draw();
        return;
    }
    icon_editor_draw_classic();
}

static void icon_editor_open(const char* title, icon_entry_t* entries,
                             int count, const char** names) {
    if (!entries || !names || count <= 0) return;
    icon_editor_active = 1;
    icon_editor_selected = 0;
    icon_editor_field = 0;
    icon_editor_count = count;
    icon_editor_entries = entries;
    icon_editor_title = title;
    icon_editor_names = names;
    icon_editor_draw();
}

static int icon_editor_handle_key(uint8_t scancode) {
    if (scancode & 0x80) return 1;
    if (scancode == 0x01 || scancode == 0x1C) {
        icon_editor_active = 0;
        settings_draw();
        return 1;
    }

    if (scancode == 0x48) {
        icon_editor_selected = icon_editor_selected > 0 ? icon_editor_selected - 1 : icon_editor_count - 1;
    } else if (scancode == 0x50) {
        icon_editor_selected = icon_editor_selected + 1 < icon_editor_count ? icon_editor_selected + 1 : 0;
    } else if (scancode == 0x4B && icon_editor_field > 0) {
        icon_editor_field--;
    } else if (scancode == 0x4D && icon_editor_field < 2) {
        icon_editor_field++;
    } else if (scancode == 0x0D || scancode == 0x2B) {
        if (icon_editor_field == 0) {
            icon_editor_entries[icon_editor_selected].ch++;
            if (icon_editor_entries[icon_editor_selected].ch > 0x7E) {
                icon_editor_entries[icon_editor_selected].ch = 0x20;
            }
        } else if (icon_editor_field == 1) {
            icon_editor_entries[icon_editor_selected].color++;
        } else {
            icon_editor_entries[icon_editor_selected].color_selected++;
        }
    } else if (scancode == 0x0A) {
        if (icon_editor_field == 0) {
            icon_editor_entries[icon_editor_selected].ch--;
            if (icon_editor_entries[icon_editor_selected].ch < 0x20) {
                icon_editor_entries[icon_editor_selected].ch = 0x7E;
            }
        } else if (icon_editor_field == 1) {
            icon_editor_entries[icon_editor_selected].color--;
        } else {
            icon_editor_entries[icon_editor_selected].color_selected--;
        }
    }

    icon_editor_draw();
    return 1;
}

static void execute_icons_action(int option) {
    icon_registry_t* reg = icons_get_registry();

    switch (option) {
        case 0:
            icon_editor_open("Icones - Desktop", reg->desktop,
                ICON_DESKTOP_COUNT, desktop_icon_names);
            break;
        case 1:
            icon_editor_open("Icones - Janela", reg->wm,
                ICON_WM_COUNT, window_icon_names);
            break;
        case 2:
            icon_editor_open("Icones - Arquivos", reg->fm,
                ICON_FM_COUNT, file_icon_names);
            break;
        case 3:
            icons_reset_defaults();
            break;
    }
}

static void execute_system_action(int option) {
    if (settings_mode == SETTINGS_MODE_MODERN) {
        if (option == 0) settings_dialog = SETTINGS_DIALOG_SYSTEM_INFO;
        if (option == 1) settings_dialog = SETTINGS_DIALOG_MEMORY;
        if (option == 2) settings_dialog = SETTINGS_DIALOG_PROCESSES;
        if (option == 3) {
            vesa_color_t background;
            background.raw = GUI_COLOR_BG;
            vesa_clear(background);
            gui_draw_text(0, 0, "Reiniciando...", GUI_COLOR_TEXT);
            asm volatile("cli");
            asm volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
            for (;;) asm volatile("hlt");
        }
        settings_draw();
        return;
    }

    switch (option) {
        case 0:
            video_clear();
            video_print_at(10, 10, "ZephyrOS v0.1", 0x0F);
            video_print_at(10, 12, "Computador: ZephyrOS-PC", 0x07);
            video_print_at(10, 14, "Pressione Esc para voltar", 0x08);
            break;
        case 1: {
            video_clear();
            video_print_at(10, 8, "Informacoes de Memoria:", 0x0F);
            video_print_at(10, 10, "Total: ", 0x07);
            uint32_t total = memory_get_total() / 1024;
            char buf[16];
            int_to_str(total, buf);
            video_print(buf, 0x0B);
            video_print(" KB", 0x07);
            video_print_at(10, 12, "Livre: ", 0x07);
            uint32_t free = memory_get_free() / 1024;
            int_to_str(free, buf);
            video_print(buf, 0x0B);
            video_print(" KB", 0x07);
            video_print_at(10, 14, "Uso: ", 0x07);
            uint32_t used = memory_get_used() / 1024;
            int_to_str(used, buf);
            video_print(buf, 0x0B);
            video_print(" KB", 0x07);
            video_print_at(10, 16, "Pressione Esc para voltar", 0x08);
            break;
        }
        case 2: {
            video_clear();
            video_print_at(10, 8, "Processos Ativos:", 0x0F);
            extern process_t processes[];
            extern uint32_t process_count;
            int y = 10;
            for (int i = 0; i < 64 && y < 20; i++) {
                if (processes[i].state != 0) {
                    video_print_at(12, y, processes[i].name, 0x0B);
                    y++;
                }
            }
            video_print_at(10, 22, "Pressione Esc para voltar", 0x08);
            break;
        }
        case 3:
            video_clear();
            video_print_at(25, 11, "Reiniciando...", 0x0E);
            asm volatile("cli");
            asm volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
            for (;;) asm volatile("hlt");
            break;
    }
}

static void execute_about_action(int option) {
    if (settings_mode == SETTINGS_MODE_MODERN) {
        settings_dialog = option == 0 ? SETTINGS_DIALOG_VERSION : SETTINGS_DIALOG_CREDITS;
        settings_draw();
        return;
    }

    video_clear();
    if (option == 0) {
        video_print_at(10, 8, "ZephyrOS v0.1", 0x0F);
        video_print_at(10, 10, "Sistema Operacional Educacional", 0x07);
        video_print_at(10, 12, "Desenvolvido em C e Assembly x86", 0x07);
        video_print_at(10, 14, "VGA Text Mode 80x25", 0x07);
    } else {
        video_print_at(10, 8, "Creditos:", 0x0F);
        video_print_at(10, 10, "Kernel e Drivers", 0x0B);
        video_print_at(12, 11, "Desenvolvido para fins educacionais", 0x07);
        video_print_at(10, 13, "Sistema de Arquivos", 0x0B);
        video_print_at(12, 14, "FAT12 implementation", 0x07);
        video_print_at(10, 16, "Interface", 0x0B);
        video_print_at(12, 17, "TUI estilo Windows 95", 0x07);
    }
    video_print_at(10, 22, "Pressione Esc para voltar", 0x08);
}

static vesa_color_t settings_gui_color(uint32_t raw) {
    vesa_color_t color;
    color.raw = raw;
    return color;
}

static void settings_gui_draw_num(int x, int y, uint32_t value, uint32_t color) {
    char buffer[16];
    int_to_str(value, buffer);
    gui_draw_text((uint32_t)x, (uint32_t)y, buffer, color);
}

static int settings_gui_hit(int x, int y, int left, int top, int width, int height) {
    return x >= left && x < left + width && y >= top && y < top + height;
}

static void settings_gui_draw_option_value(settings_option_t* option,
                                           int x, int y, int width, int selected) {
    const char* value = "";
    char toggle[4];

    if (option->type == SETTINGS_OPT_TOGGLE) {
        toggle[0] = '[';
        toggle[1] = option->value ? 'x' : ' ';
        toggle[2] = ']';
        toggle[3] = '\0';
        gui_draw_button((uint32_t)x, (uint32_t)y, (uint32_t)width, 24,
                        toggle, selected);
        return;
    }
    if (option->type == SETTINGS_OPT_LIST && option->list_values &&
        option->value >= 0 && option->value < option->list_count) {
        value = option->list_values[option->value];
    }
    if (option->type == SETTINGS_OPT_ACTION) value = "Executar";
    gui_draw_button((uint32_t)x, (uint32_t)y, (uint32_t)width, 24,
                    value, selected);
}

static void settings_gui_draw_main_header(int x, int y, int width) {
    gui_draw_text((uint32_t)(x + 12), (uint32_t)(y + 8),
                  categories[selected_category].name, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + width - 220), (uint32_t)(y + 8),
                  "Configuracoes do sistema", GUI_COLOR_TEXT);
    vesa_draw_hline((uint32_t)(x + 8), (uint32_t)(y + 30),
                    (uint32_t)(width - 16), settings_gui_color(GUI_COLOR_BORDER_D));
}

static void settings_draw_modern_main(void) {
    int side_x = settings_gui_x + SETTINGS_MODERN_MARGIN;
    int side_y = settings_gui_y + 32;
    int side_height = settings_gui_height - 76;
    int content_x = side_x + SETTINGS_MODERN_SIDE_WIDTH + 8;
    int content_y = side_y;
    int content_width = settings_gui_width - SETTINGS_MODERN_SIDE_WIDTH - 32;
    int content_height = side_height;
    settings_page_t* page = &categories[selected_category];

    gui_draw_panel((uint32_t)side_x, (uint32_t)side_y,
                   SETTINGS_MODERN_SIDE_WIDTH, (uint32_t)side_height,
                   GUI_COLOR_BG, 0);
    gui_draw_text((uint32_t)(side_x + 12), (uint32_t)(side_y + 10),
                  "Categorias", GUI_COLOR_TEXT);

    for (int i = 0; i < SETTINGS_CAT_COUNT; i++) {
        int row_y = side_y + 38 + i * SETTINGS_MODERN_ROW_HEIGHT;
        uint32_t background = i == selected_category ? GUI_COLOR_TITLE_BG : GUI_COLOR_BG;
        uint32_t text_color = i == selected_category ? GUI_COLOR_TEXT_W : GUI_COLOR_TEXT;
        gui_draw_panel((uint32_t)(side_x + 6), (uint32_t)row_y,
                       SETTINGS_MODERN_SIDE_WIDTH - 12, 28, background,
                       i == selected_category);
        gui_draw_text((uint32_t)(side_x + 16), (uint32_t)(row_y + 6),
                      categories[i].name, text_color);
    }

    gui_draw_panel((uint32_t)content_x, (uint32_t)content_y,
                   (uint32_t)content_width, (uint32_t)content_height,
                   GUI_COLOR_BG, 0);
    settings_gui_draw_main_header(content_x, content_y, content_width);

    for (int i = 0; i < page->option_count; i++) {
        int row_y = content_y + 46 + i * SETTINGS_MODERN_ROW_HEIGHT;
        int value_x = content_x + content_width - 190;
        uint32_t text_color = i == selected_option ? GUI_COLOR_TEXT_W : GUI_COLOR_TEXT;

        if (i == selected_option) {
            vesa_color_t selection = settings_gui_color(GUI_COLOR_TITLE_BG);
            vesa_fill_rect((uint32_t)(content_x + 6), (uint32_t)row_y,
                           (uint32_t)(content_width - 12), 28, selection);
        }
        gui_draw_text((uint32_t)(content_x + 18), (uint32_t)(row_y + 6),
                      page->options[i].name, text_color);
        settings_gui_draw_option_value(&page->options[i], value_x, row_y + 2,
                                       166, i == selected_option && editing_option);
    }

    if (page->option_count == 0) {
        gui_draw_text((uint32_t)(content_x + 18), (uint32_t)(content_y + 52),
                      "Nenhuma opcao disponivel", 0x00800000);
    }
    gui_draw_text((uint32_t)(settings_gui_x + 18),
                  (uint32_t)(settings_gui_y + settings_gui_height - 34),
                  "Tab categorias | Setas navegam | Enter edita | Esc fecha",
                  GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(settings_gui_x + settings_gui_width - 150),
                  (uint32_t)(settings_gui_y + settings_gui_height - 34),
                  settings_mode == SETTINGS_MODE_MODERN ? "GUI" : "TUI",
                  GUI_COLOR_TEXT);
}

static void settings_gui_draw_icon_editor(void) {
    int x = settings_gui_x + SETTINGS_MODERN_MARGIN + 24;
    int y = settings_gui_y + 42;
    int width = settings_gui_width - 72;
    int row_width = width - 16;

    gui_draw_panel((uint32_t)(settings_gui_x + SETTINGS_MODERN_MARGIN),
                   (uint32_t)(settings_gui_y + 32), (uint32_t)(settings_gui_width - 24),
                   (uint32_t)(settings_gui_height - 76), GUI_COLOR_BG, 0);
    gui_draw_text((uint32_t)x, (uint32_t)y, icon_editor_title, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)x, (uint32_t)(y + 28),
                  "Item", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 128), (uint32_t)(y + 28),
                  "Caractere", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 250), (uint32_t)(y + 28),
                  "Cor", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 340), (uint32_t)(y + 28),
                  "Cor sel.", GUI_COLOR_TEXT);

    for (int i = 0; i < icon_editor_count; i++) {
        int row_y = y + 52 + i * 34;
        uint32_t row_color = i == icon_editor_selected ? GUI_COLOR_TEXT_W : GUI_COLOR_TEXT;
        char character[2];
        char color_value[16];
        char selected_color_value[16];

        character[0] = icon_editor_entries[i].ch;
        character[1] = '\0';
        int_to_str((uint32_t)(uint8_t)icon_editor_entries[i].color, color_value);
        int_to_str((uint32_t)(uint8_t)icon_editor_entries[i].color_selected,
                   selected_color_value);
        if (i == icon_editor_selected) {
            vesa_fill_rect((uint32_t)(x - 8), (uint32_t)row_y,
                           (uint32_t)row_width, 28,
                           settings_gui_color(GUI_COLOR_TITLE_BG));
        }
        gui_draw_text((uint32_t)x, (uint32_t)(row_y + 6), icon_editor_names[i], row_color);
        gui_draw_button((uint32_t)(x + 112), (uint32_t)(row_y + 2), 88, 24,
                        character,
                        icon_editor_field == 0 && i == icon_editor_selected);
        gui_draw_button((uint32_t)(x + 224), (uint32_t)(row_y + 2), 72, 24,
                        color_value,
                        icon_editor_field == 1 && i == icon_editor_selected);
        gui_draw_button((uint32_t)(x + 314), (uint32_t)(row_y + 2), 72, 24,
                        selected_color_value,
                        icon_editor_field == 2 && i == icon_editor_selected);
    }

    gui_draw_button((uint32_t)(settings_gui_x + settings_gui_width - 136),
                    (uint32_t)(settings_gui_y + settings_gui_height - 54),
                    104, 28, "Voltar", 0);
    gui_draw_text((uint32_t)(settings_gui_x + 24),
                  (uint32_t)(settings_gui_y + settings_gui_height - 42),
                  "Setas: item/campo | +/-: alterar | Enter: concluir",
                  GUI_COLOR_TEXT);
}

static void settings_gui_draw_process_list(int x, int y, int width) {
    int row = 0;
    extern process_t processes[];

    for (int i = 0; i < 64 && row < 8; i++) {
        if (processes[i].state == PROCESS_STATE_UNUSED) continue;
        gui_draw_text((uint32_t)x, (uint32_t)(y + row * 24),
                      processes[i].name, GUI_COLOR_TEXT);
        settings_gui_draw_num(x + width - 80, y + row * 24,
                              processes[i].pid, GUI_COLOR_TEXT);
        row++;
    }
    if (row == 0) {
        gui_draw_text((uint32_t)x, (uint32_t)y,
                      "Nenhum processo ativo", GUI_COLOR_TEXT);
    }
}

static void settings_gui_draw_dialog_content(int x, int y, int width) {
    uint32_t total;
    uint32_t free_memory;
    uint32_t used_memory;

    switch (settings_dialog) {
        case SETTINGS_DIALOG_SYSTEM_INFO:
            gui_draw_text((uint32_t)x, (uint32_t)y, "ZephyrOS v0.1", GUI_COLOR_TEXT);
            gui_draw_text((uint32_t)x, (uint32_t)(y + 34),
                          "Computador: ZephyrOS-PC", GUI_COLOR_TEXT);
            gui_draw_text((uint32_t)x, (uint32_t)(y + 68),
                          "Sistema operacional educacional", GUI_COLOR_TEXT);
            break;
        case SETTINGS_DIALOG_MEMORY:
            total = memory_get_total() / 1024;
            free_memory = memory_get_free() / 1024;
            used_memory = memory_get_used() / 1024;
            gui_draw_text((uint32_t)x, (uint32_t)y, "Memoria do sistema", GUI_COLOR_TEXT);
            gui_draw_text((uint32_t)x, (uint32_t)(y + 34), "Total:", GUI_COLOR_TEXT);
            settings_gui_draw_num(x + 120, y + 34, total, GUI_COLOR_TEXT);
            gui_draw_text((uint32_t)(x + 176), (uint32_t)(y + 34), "KB", GUI_COLOR_TEXT);
            gui_draw_text((uint32_t)x, (uint32_t)(y + 68), "Livre:", GUI_COLOR_TEXT);
            settings_gui_draw_num(x + 120, y + 68, free_memory, GUI_COLOR_TEXT);
            gui_draw_text((uint32_t)(x + 176), (uint32_t)(y + 68), "KB", GUI_COLOR_TEXT);
            gui_draw_text((uint32_t)x, (uint32_t)(y + 102), "Usada:", GUI_COLOR_TEXT);
            settings_gui_draw_num(x + 120, y + 102, used_memory, GUI_COLOR_TEXT);
            gui_draw_text((uint32_t)(x + 176), (uint32_t)(y + 102), "KB", GUI_COLOR_TEXT);
            break;
        case SETTINGS_DIALOG_PROCESSES:
            gui_draw_text((uint32_t)x, (uint32_t)y, "Processos ativos", GUI_COLOR_TEXT);
            settings_gui_draw_process_list(x, y + 34, width);
            break;
        case SETTINGS_DIALOG_VERSION:
            gui_draw_text((uint32_t)x, (uint32_t)y, "ZephyrOS v0.1", GUI_COLOR_TEXT);
            gui_draw_text((uint32_t)x, (uint32_t)(y + 34),
                          "VESA framebuffer + VGA fallback", GUI_COLOR_TEXT);
            gui_draw_text((uint32_t)x, (uint32_t)(y + 68),
                          "Kernel x86 freestanding", GUI_COLOR_TEXT);
            break;
        case SETTINGS_DIALOG_CREDITS:
            gui_draw_text((uint32_t)x, (uint32_t)y, "Creditos", GUI_COLOR_TEXT);
            gui_draw_text((uint32_t)x, (uint32_t)(y + 34),
                          "Kernel e drivers", GUI_COLOR_TEXT);
            gui_draw_text((uint32_t)x, (uint32_t)(y + 68),
                          "Sistema de arquivos FAT", GUI_COLOR_TEXT);
            gui_draw_text((uint32_t)x, (uint32_t)(y + 102),
                          "Interface TUI e GUI 2D", GUI_COLOR_TEXT);
            break;
        default:
            break;
    }
}

static void settings_draw_modern_dialog(void) {
    const char* title = "Informacoes";
    int width = SETTINGS_MODERN_DIALOG_WIDTH;
    int height = SETTINGS_MODERN_DIALOG_HEIGHT;
    int x = settings_gui_x + (settings_gui_width - width) / 2;
    int y = settings_gui_y + (settings_gui_height - height) / 2;

    if (settings_dialog == SETTINGS_DIALOG_PROCESSES) title = "Processos";
    if (settings_dialog == SETTINGS_DIALOG_VERSION) title = "Versao";
    if (settings_dialog == SETTINGS_DIALOG_CREDITS) title = "Creditos";
    gui_draw_window_frame((uint32_t)x, (uint32_t)y, (uint32_t)width,
                          (uint32_t)height, title, 1);
    settings_gui_draw_dialog_content(x + 20, y + 42, width - 40);
    gui_draw_button((uint32_t)(x + width - 124), (uint32_t)(y + height - 46),
                    96, 28, "Voltar", 0);
}

static void settings_modern_draw(void) {
    vesa_mode_t* mode = vesa_get_mode();

    if (!settings_active || settings_mode != SETTINGS_MODE_MODERN ||
        !mode || !mode->initialized || !vesa_has_backbuffer()) return;

    mouse_invalidate_cursor();
    vesa_frame_begin();
    vesa_clear(settings_gui_color(GUI_COLOR_BG));
    gui_draw_window_frame((uint32_t)settings_gui_x, (uint32_t)settings_gui_y,
                          (uint32_t)settings_gui_width, (uint32_t)settings_gui_height,
                          "Configuracoes do ZephyrOS", 1);

    if (icon_editor_active) {
        settings_gui_draw_icon_editor();
    } else if (settings_dialog != SETTINGS_DIALOG_NONE) {
        settings_draw_modern_dialog();
    } else {
        settings_draw_modern_main();
    }
    taskbar_draw();
    vesa_frame_end();
}

static void settings_execute_selected_action(void) {
    settings_page_t* page = &categories[selected_category];
    settings_option_t* option = &page->options[selected_option];

    if (option->type != SETTINGS_OPT_ACTION) return;
    if (selected_category == SETTINGS_CAT_SYSTEM) {
        execute_system_action(selected_option);
    } else if (selected_category == SETTINGS_CAT_ABOUT) {
        execute_about_action(selected_option);
    } else if (selected_category == SETTINGS_CAT_ICONS) {
        execute_icons_action(selected_option);
    }
}

int settings_handle_key(uint8_t scancode) {
    if (!settings_active) return 0;

    if (settings_dialog != SETTINGS_DIALOG_NONE) {
        if (scancode == 0x01 || scancode == 0x1C) {
            settings_dialog = SETTINGS_DIALOG_NONE;
            settings_draw();
        }
        return 1;
    }

    if (icon_editor_active) return icon_editor_handle_key(scancode);

    if (scancode & 0x80) return 0;

    settings_page_t* page = &categories[selected_category];

    if (editing_option) {
        if (scancode == 0x01) {
            editing_option = 0;
            settings_draw();
            return 1;
        }

        if (scancode == 0x1C) {
            editing_option = 0;

            settings_execute_selected_action();

            if (icon_editor_active) return 1;
            if (settings_dialog != SETTINGS_DIALOG_NONE) return 1;

            settings_apply_category();

            settings_draw();
            return 1;
        }

        if (page->options[selected_option].type == SETTINGS_OPT_TOGGLE) {
            if (scancode == 0x1C || scancode == 0x39) {
                page->options[selected_option].value = !page->options[selected_option].value;
                editing_option = 0;
                settings_apply_category();
                settings_draw();
                return 1;
            }
        }

        if (page->options[selected_option].type == SETTINGS_OPT_LIST) {
            if (scancode == 0x4B) {
                if (page->options[selected_option].value > 0) {
                    page->options[selected_option].value--;
                } else {
                    page->options[selected_option].value = page->options[selected_option].list_count - 1;
                }
                settings_apply_category();
                settings_draw();
                return 1;
            }
            if (scancode == 0x4D) {
                if (page->options[selected_option].value < page->options[selected_option].list_count - 1) {
                    page->options[selected_option].value++;
                } else {
                    page->options[selected_option].value = 0;
                }
                settings_apply_category();
                settings_draw();
                return 1;
            }
        }

        return 1;
    }

    if (scancode == 0x01) {
        settings_close();
        return 1;
    }

    if (scancode == 0x0F) {
        selected_option = 0;
        if (selected_category < SETTINGS_CAT_COUNT - 1) {
            selected_category++;
        } else {
            selected_category = 0;
        }
        settings_draw();
        return 1;
    }

    if (scancode == 0x48) {
        if (selected_option > 0) {
            selected_option--;
        } else {
            selected_option = page->option_count - 1;
        }
        settings_draw();
        return 1;
    }

    if (scancode == 0x50) {
        if (selected_option < page->option_count - 1) {
            selected_option++;
        } else {
            selected_option = 0;
        }
        settings_draw();
        return 1;
    }

    if (scancode == 0x1C) {
        editing_option = 1;
        settings_draw();
        return 1;
    }

    return 1;
}

int settings_is_open(void) {
    return settings_active;
}

settings_mode_t settings_get_mode(void) {
    return settings_mode;
}

static void settings_gui_dialog_bounds(int* x, int* y, int* width, int* height) {
    *width = SETTINGS_MODERN_DIALOG_WIDTH;
    *height = SETTINGS_MODERN_DIALOG_HEIGHT;
    *x = settings_gui_x + (settings_gui_width - *width) / 2;
    *y = settings_gui_y + (settings_gui_height - *height) / 2;
}

static int settings_gui_handle_icon_mouse(int px, int py) {
    int x = settings_gui_x + SETTINGS_MODERN_MARGIN + 24;
    int y = settings_gui_y + 42;

    if (settings_gui_hit(px, py, settings_gui_x + settings_gui_width - 136,
                         settings_gui_y + settings_gui_height - 54, 104, 28)) {
        icon_editor_active = 0;
        icon_editor_entries = 0;
        icon_editor_names = 0;
        settings_draw();
        return 1;
    }
    for (int i = 0; i < icon_editor_count; i++) {
        int row_y = y + 52 + i * 34;
        if (settings_gui_hit(px, py, x - 8, row_y,
                             settings_gui_width - 72, 28)) {
            icon_editor_selected = i;
            if (px >= x + 112 && px < x + 200) icon_editor_field = 0;
            else if (px >= x + 224 && px < x + 296) icon_editor_field = 1;
            else if (px >= x + 314 && px < x + 386) icon_editor_field = 2;
            settings_draw();
            return 1;
        }
    }
    return 1;
}

static int settings_gui_handle_dialog_mouse(int px, int py) {
    int x;
    int y;
    int width;
    int height;

    settings_gui_dialog_bounds(&x, &y, &width, &height);
    if (settings_gui_hit(px, py, x + width - 20, y + 3, 16, 16) ||
        settings_gui_hit(px, py, x + width - 124, y + height - 46, 96, 28)) {
        settings_dialog = SETTINGS_DIALOG_NONE;
        settings_draw();
    }
    return 1;
}

int settings_handle_mouse(mouse_event_t* event) {
    int value_x;
    int row;
    settings_page_t* page;

    if (!event) {
        LOG_ERROR("SETTINGS", "Evento de mouse nulo");
        return 0;
    }
    if (!settings_active) return 0;
    if (settings_mode != SETTINGS_MODE_MODERN) return 1;
    if (event->event != MOUSE_EVENT_PRESS ||
        !(event->changed & MOUSE_BTN_LEFT)) return 1;

    if (settings_gui_hit(event->x, event->y, settings_gui_x + settings_gui_width - 20,
                         settings_gui_y + 3, 16, 16)) {
        settings_close();
        return 1;
    }
    if (icon_editor_active) return settings_gui_handle_icon_mouse(event->x, event->y);
    if (settings_dialog != SETTINGS_DIALOG_NONE) {
        return settings_gui_handle_dialog_mouse(event->x, event->y);
    }
    if (!settings_gui_hit(event->x, event->y, settings_gui_x, settings_gui_y,
                          settings_gui_width, settings_gui_height)) return 1;

    if (settings_gui_hit(event->x, event->y,
                         settings_gui_x + SETTINGS_MODERN_MARGIN,
                         settings_gui_y + 32, SETTINGS_MODERN_SIDE_WIDTH,
                         settings_gui_height - 76)) {
        row = (event->y - (settings_gui_y + 70)) / SETTINGS_MODERN_ROW_HEIGHT;
        if (row >= 0 && row < SETTINGS_CAT_COUNT) {
            selected_category = row;
            selected_option = 0;
            editing_option = 0;
            settings_draw();
        }
        return 1;
    }

    page = &categories[selected_category];
    row = (event->y - (settings_gui_y + 32 + 46)) / SETTINGS_MODERN_ROW_HEIGHT;
    if (row < 0 || row >= page->option_count) return 1;
    selected_option = row;
    value_x = settings_gui_x + SETTINGS_MODERN_MARGIN + SETTINGS_MODERN_SIDE_WIDTH +
              8 + (settings_gui_width - SETTINGS_MODERN_SIDE_WIDTH - 32) - 190;
    if (event->x < value_x) {
        editing_option = 0;
        settings_draw();
        return 1;
    }

    if (page->options[row].type == SETTINGS_OPT_TOGGLE) {
        page->options[row].value = !page->options[row].value;
        settings_apply_category();
        editing_option = 0;
    } else if (page->options[row].type == SETTINGS_OPT_LIST) {
        page->options[row].value++;
        if (page->options[row].value >= page->options[row].list_count) {
            page->options[row].value = 0;
        }
        settings_apply_category();
        editing_option = 0;
    } else {
        editing_option = 0;
        settings_execute_selected_action();
        if (icon_editor_active || settings_dialog != SETTINGS_DIALOG_NONE) return 1;
    }
    settings_draw();
    return 1;
}
