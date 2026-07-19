#include "ui/settings.h"
#include "core/video.h"
#include "core/keyboard.h"
#include "ui/taskbar.h"
#include "drivers/speaker.h"
#include "core/memory.h"
#include "core/timer.h"
#include "process/process.h"
#include "process/thread.h"
#include "ui/wm.h"
#include "ui/icons.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

static uint8_t keyboard_get_scancode(void) {
    uint8_t scancode = 0;
    while (!scancode) {
        __asm__ volatile("inb $0x60, %0" : "=a"(scancode));
    }
    return scancode;
}

static int settings_active = 0;
static int selected_category = 0;
static int selected_option = 0;
static int editing_option = 0;

static settings_page_t categories[SETTINGS_CAT_COUNT];

static void init_categories(void) {
    for (int i = 0; i < SETTINGS_CAT_COUNT; i++) {
        categories[i].option_count = 0;
    }

    categories[SETTINGS_CAT_DISPLAY].name = "Tela";
    categories[SETTINGS_CAT_DISPLAY].option_count = 3;
    categories[SETTINGS_CAT_DISPLAY].options[0] = (settings_option_t){
        "Tema", SETTINGS_OPT_LIST, 0, 2,
        (const char*[]){"Classico", "Escuro", "Azul"}, 3
    };
    categories[SETTINGS_CAT_DISPLAY].options[1] = (settings_option_t){
        "Resolucao", SETTINGS_OPT_LIST, 0, 2,
        (const char*[]){"80x25", "80x50", "Auto"}, 3
    };
    categories[SETTINGS_CAT_DISPLAY].options[2] = (settings_option_t){
        "Mostrar grade", SETTINGS_OPT_TOGGLE, 0, 1, NULL, 0
    };

    categories[SETTINGS_CAT_TASKBAR].name = "Barra de Tarefas";
    categories[SETTINGS_CAT_TASKBAR].option_count = 5;
    categories[SETTINGS_CAT_TASKBAR].options[0] = (settings_option_t){
        "Posicao", SETTINGS_OPT_LIST, 0, 4,
        (const char*[]){"Baixo", "Cima", "Esquerda", "Direita", "Custom"}, 5
    };
    categories[SETTINGS_CAT_TASKBAR].options[1] = (settings_option_t){
        "Tamanho icone", SETTINGS_OPT_LIST, 1, 2,
        (const char*[]){"Pequeno", "Medio", "Grande"}, 3
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
        (const char*[]){"Direita", "Esquerda"}, 2
    };
    categories[SETTINGS_CAT_WINDOWS].options[1] = (settings_option_t){
        "Ordem botoes", SETTINGS_OPT_LIST, 0, 5,
        (const char*[]){"x _ <>", "x <> _", "_ <> x", "<> _ x", "_ x <>", "<> x _"}, 6
    };
    categories[SETTINGS_CAT_WINDOWS].options[2] = (settings_option_t){
        "Mostrar titulo", SETTINGS_OPT_TOGGLE, 1, 1, NULL, 0
    };
    categories[SETTINGS_CAT_WINDOWS].options[3] = (settings_option_t){
        "Borda", SETTINGS_OPT_LIST, 0, 1,
        (const char*[]){"Simples", "Dupla"}, 2
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
        (const char*[]){"Mudo", "Baixo", "Medio", "Alto", "Maximo"}, 5
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

void settings_init(void) {
    settings_active = 0;
    selected_category = 0;
    selected_option = 0;
    editing_option = 0;
    init_categories();
}

void settings_open(void) {
    settings_active = 1;
    selected_category = 0;
    selected_option = 0;
    editing_option = 0;
    settings_draw();
}

void settings_close(void) {
    settings_active = 0;
    video_clear();
    taskbar_draw();
}

void settings_draw(void) {
    video_fill_rect(0, 0, 80, 25, ' ', 0x07);

    video_fill_rect(0, 0, 80, 1, ' ', 0x1F);
    video_print_at(30, 0, " Configuracoes do ZephyrOS ", 0x1F);

    video_fill_rect(0, 1, 25, 24, ' ', 0x07);
    video_draw_box(0, 1, 25, 23, 0x08);

    for (int i = 0; i < SETTINGS_CAT_COUNT; i++) {
        uint8_t color = (selected_category == i) ? 0x1F : 0x07;
        if (selected_category == i) {
            video_fill_rect(1, 2 + i, 23, 1, ' ', 0x1F);
        }
        video_print_at(2, 2 + i, categories[i].name, color);
    }

    video_draw_box(26, 1, 53, 23, 0x08);

    video_print_at(28, 2, categories[selected_category].name, 0x0F);
    video_draw_hline(27, 3, 51, 0xC4, 0x08);

    settings_page_t* page = &categories[selected_category];

    for (int i = 0; i < page->option_count; i++) {
        uint8_t color = 0x07;
        if (selected_option == i) {
            color = editing_option ? 0x1E : 0x1F;
            video_fill_rect(27, 4 + i, 51, 1, ' ', editing_option ? 0x1E : 0x1F);
        }

        video_print_at(28, 4 + i, page->options[i].name, color);

        if (page->options[i].type == SETTINGS_OPT_TOGGLE) {
            video_print_at(55, 4 + i, page->options[i].value ? "[x]" : "[ ]", color);
        } else if (page->options[i].type == SETTINGS_OPT_LIST) {
            int val = page->options[i].value;
            if (val >= 0 && val < page->options[i].list_count) {
                video_print_at(55, 4 + i, "< ", color);
                video_print_at(57, 4 + i, page->options[i].list_values[val], color);
                video_print_at(57 + 10, 4 + i, " >", color);
            }
        } else if (page->options[i].type == SETTINGS_OPT_ACTION) {
            video_print_at(55, 4 + i, "[Executar]", color);
        }
    }

    video_draw_hline(27, 22, 51, 0xC4, 0x08);
    video_print_at(28, 23, "Tab:Categorias  Enter:Alterar  Esc:Fechar", 0x08);
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

static void icon_editor(const char* title, icon_entry_t* entries, int count, const char** names) {
    int sel = 0;
    int field = 0;

    while (1) {
        video_clear();
        video_print_at(2, 1, title, 0x0F);
        video_draw_hline(2, 2, 76, 0xC4, 0x08);

        for (int i = 0; i < count; i++) {
            uint8_t color = (sel == i) ? 0x1F : 0x07;
            video_print_at(4, 4 + i * 2, names[i], color);

            video_print_at(6, 5 + i * 2, "Caractere:", 0x08);
            video_put_char_at(entries[i].ch, entries[i].color, 17, 5 + i * 2);

            video_print_at(20, 5 + i * 2, "Cor:", 0x08);
            char cbuf[4];
            int_to_str(entries[i].color, cbuf);
            video_print_at(25, 5 + i * 2, cbuf, 0x07);

            video_print_at(30, 5 + i * 2, "Cor sel:", 0x08);
            int_to_str(entries[i].color_selected, cbuf);
            video_print_at(39, 5 + i * 2, cbuf, 0x07);

            if (sel == i) {
                if (field == 0) video_put_char_at('>', 0x0E, 15, 5 + i * 2);
                else if (field == 1) video_put_char_at('>', 0x0E, 23, 5 + i * 2);
                else video_put_char_at('>', 0x0E, 37, 5 + i * 2);
            }
        }

        video_draw_hline(2, 22, 76, 0xC4, 0x08);
        video_print_at(3, 23, "Up/Down:Item  Left/Right:Campo  +/-:Valor  Enter:Ok  Esc:Voltar", 0x08);

        uint8_t scancode = 0;
        while (!scancode) { scancode = keyboard_get_scancode(); }
        if (scancode & 0x80) continue;

        if (scancode == 0x01) return;

        if (scancode == 0x48) {
            if (sel > 0) sel--;
            else sel = count - 1;
        }
        if (scancode == 0x50) {
            if (sel < count - 1) sel++;
            else sel = 0;
        }

        if (scancode == 0x4B) {
            if (field > 0) field--;
        }
        if (scancode == 0x4D) {
            if (field < 2) field++;
        }

        if (scancode == 0x0D || scancode == 0x2B) {
            if (field == 0) {
                entries[sel].ch++;
                if (entries[sel].ch > 0x7E) entries[sel].ch = 0x20;
            } else if (field == 1) {
                entries[sel].color++;
            } else {
                entries[sel].color_selected++;
            }
        }
        if (scancode == 0x0A) {
            if (field == 0) {
                entries[sel].ch--;
                if (entries[sel].ch < 0x20) entries[sel].ch = 0x7E;
            } else if (field == 1) {
                entries[sel].color--;
            } else {
                entries[sel].color_selected--;
            }
        }
    }
}

static void execute_icons_action(int option) {
    icon_registry_t* reg = icons_get_registry();

    switch (option) {
        case 0:
            icon_editor("Icones - Desktop",
                reg->desktop, ICON_DESKTOP_COUNT,
                (const char*[]){"Shell", "Explorer", "TaskMgr"});
            break;
        case 1:
            icon_editor("Icones - Janela",
                reg->wm, ICON_WM_COUNT,
                (const char*[]){"Fechar", "Minimizar", "Maximizar"});
            break;
        case 2:
            icon_editor("Icones - Arquivos",
                reg->fm, ICON_FM_COUNT,
                (const char*[]){"Pasta", "Arquivo"});
            break;
        case 3:
            icons_reset_defaults();
            break;
    }
}

static void execute_system_action(int option) {
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
            while (1) {}
            break;
    }
}

static void execute_about_action(int option) {
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

int settings_handle_key(uint8_t scancode) {
    if (!settings_active) return 0;

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

            if (page->options[selected_option].type == SETTINGS_OPT_ACTION) {
                if (selected_category == SETTINGS_CAT_SYSTEM) {
                    execute_system_action(selected_option);
                } else if (selected_category == SETTINGS_CAT_ABOUT) {
                    execute_about_action(selected_option);
                } else if (selected_category == SETTINGS_CAT_ICONS) {
                    execute_icons_action(selected_option);
                }
            }

            if (selected_category == SETTINGS_CAT_TASKBAR) {
                apply_taskbar_settings();
            }

            if (selected_category == SETTINGS_CAT_WINDOWS) {
                apply_wm_settings();
            }

            settings_draw();
            return 1;
        }

        if (page->options[selected_option].type == SETTINGS_OPT_TOGGLE) {
            if (scancode == 0x1C || scancode == 0x39) {
                page->options[selected_option].value = !page->options[selected_option].value;
                editing_option = 0;
                if (selected_category == SETTINGS_CAT_TASKBAR) {
                    apply_taskbar_settings();
                }
                if (selected_category == SETTINGS_CAT_WINDOWS) {
                    apply_wm_settings();
                }
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
                if (selected_category == SETTINGS_CAT_TASKBAR) {
                    apply_taskbar_settings();
                }
                if (selected_category == SETTINGS_CAT_WINDOWS) {
                    apply_wm_settings();
                }
                settings_draw();
                return 1;
            }
            if (scancode == 0x4D) {
                if (page->options[selected_option].value < page->options[selected_option].list_count - 1) {
                    page->options[selected_option].value++;
                } else {
                    page->options[selected_option].value = 0;
                }
                if (selected_category == SETTINGS_CAT_TASKBAR) {
                    apply_taskbar_settings();
                }
                if (selected_category == SETTINGS_CAT_WINDOWS) {
                    apply_wm_settings();
                }
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
