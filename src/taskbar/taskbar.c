#include "ui/taskbar.h"
#include "core/video.h"
#include "core/keyboard.h"
#include "core/timer.h"
#include "ui/settings.h"
#include "drivers/vesa.h"
#include "drivers/font.h"
#include "drivers/mouse.h"
#include "ui/gui.h"

static tb_button_t buttons[TASKBAR_BUTTON_MAX];
static int button_count = 0;
static int menu_open = 0;
static int menu_selection = 0;
static int config_menu_open = 0;
static int config_selection = 0;
static uint32_t last_second = 0;

static tb_config_t config = {
    .position = TB_POS_BOTTOM,
    .icon_size = TB_SIZE_MEDIUM,
    .pinned = 1,
    .custom_x = 0,
    .custom_y = 0,
    .width = SCREEN_COLS,
    .height = 1
};

#define MENU_ITEM_COUNT 7
static const char* menu_items[MENU_ITEM_COUNT] = {
    "Desktop",
    "Shell",
    "Explorer",
    "Task Manager",
    "Configuracoes",
    "Reiniciar",
    "Desligar"
};

#define CONFIG_ITEM_COUNT 8
static const char* config_items[CONFIG_ITEM_COUNT] = {
    "Posicao: ",
    "Tamanho: ",
    "Fixado:  ",
    "Mover p/ Topo",
    "Mover p/ Baixo",
    "Mover p/ Esquerda",
    "Mover p/ Direita",
    "Posicao Custom..."
};

static const char* position_names[] = {"Baixo", "Cima", "Esquerda", "Direita", "Custom"};
static const char* size_names[] = {"Pequeno", "Medio", "Grande"};

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

static void update_dimensions(void) {
    switch (config.position) {
        case TB_POS_BOTTOM:
        case TB_POS_TOP:
            config.width = SCREEN_COLS;
            config.height = 1;
            break;
        case TB_POS_LEFT:
        case TB_POS_RIGHT:
            config.width = 1;
            config.height = SCREEN_ROWS;
            break;
        case TB_POS_CUSTOM:
            config.width = 40;
            config.height = 1;
            break;
    }
}

void taskbar_init(void) {
    button_count = 0;
    menu_open = 0;
    menu_selection = 0;
    config_menu_open = 0;
    config_selection = 0;
    last_second = 0;

    update_dimensions();
    taskbar_add_app(TB_APP_SHELL, "Shell");
}

static int get_row(void) {
    if (config.position == TB_POS_BOTTOM || config.position == TB_POS_CUSTOM) {
        if (config.position == TB_POS_CUSTOM) return config.custom_y;
        return SCREEN_ROWS - 1;
    }
    if (config.position == TB_POS_TOP) return 0;
    return SCREEN_ROWS - 1;
}

static int get_col(void) {
    if (config.position == TB_POS_LEFT) return 0;
    if (config.position == TB_POS_RIGHT) return SCREEN_COLS - 1;
    if (config.position == TB_POS_CUSTOM) return config.custom_x;
    return 0;
}

static int get_icon_char_count(void) {
    switch (config.icon_size) {
        case TB_SIZE_SMALL: return 6;
        case TB_SIZE_MEDIUM: return 10;
        case TB_SIZE_LARGE: return 14;
    }
    return 10;
}

#define TASKBAR_HEIGHT 24

static void taskbar_update_clock_gui(void);
static void taskbar_draw_menu_gui(void);

static void taskbar_draw_gui(void) {
    vesa_mode_t* mode = vesa_get_mode();
    if (!mode || !mode->initialized || !vesa_has_backbuffer()) return;

    mouse_invalidate_cursor();

    int tb_y = mode->height - TASKBAR_HEIGHT;
    if (config.position == TB_POS_TOP) tb_y = 0;
    
    // Fill taskbar background
    vesa_color_t bg; bg.raw = GUI_COLOR_BG;
    vesa_color_t light; light.raw = GUI_COLOR_BORDER_L;
    vesa_color_t dark; dark.raw = GUI_COLOR_BORDER_D;

    vesa_fill_rect(0, tb_y, mode->width, TASKBAR_HEIGHT, bg);
    
    // Draw 3D border for the taskbar itself
    vesa_draw_hline(0, tb_y, mode->width, light);
    
    int start_x = 2;
    int btn_height = TASKBAR_HEIGHT - 4;
    int btn_y = tb_y + 2;

    // Start Button
    gui_draw_button(start_x, btn_y, 60, btn_height, "Inicio", menu_open);
    
    start_x += 64;

    // App Buttons
    for (int i = 0; i < button_count; i++) {
        if (start_x > (int)mode->width - 100) break;
        tb_button_t* btn = &buttons[i];
        gui_draw_button(start_x, btn_y, 90, btn_height, btn->name, btn->active);
        start_x += 94;
    }

    taskbar_update_clock_gui();
}

void taskbar_draw(void) {
    vesa_mode_t* mode = vesa_get_mode();
    if (mode && mode->initialized && vesa_has_backbuffer()) {
        int tb_y = mode->height - TASKBAR_HEIGHT;
        if (config.position == TB_POS_TOP) tb_y = 0;
        vesa_frame_begin_region(0, tb_y, mode->width, TASKBAR_HEIGHT);
        taskbar_draw_gui();
        taskbar_redraw_menu();
        vesa_frame_end();
        return;
    }

    vesa_frame_begin();

    int row = get_row();
    int col = get_col();
    int is_horizontal = (config.position == TB_POS_BOTTOM || config.position == TB_POS_TOP || config.position == TB_POS_CUSTOM);

    if (is_horizontal) {
        video_fill_rect(col, row, SCREEN_COLS, 1, ' ', 0x07);

        int x = col + 1;

        uint8_t start_color = menu_open ? 0x1F : 0x07;
        video_put_char_at('[', start_color, x, row);
        video_print_at(x + 1, row, "Inicio", start_color);
        video_put_char_at(']', start_color, x + 7, row);
        x += 9;

        int icon_chars = get_icon_char_count();

        for (int i = 0; i < button_count; i++) {
            if (x > SCREEN_COLS - 12) break;

            video_put_char_at(' ', 0x07, x, row);
            x++;

            tb_button_t* btn = &buttons[i];
            uint8_t color = btn->active ? 0x1F : 0x07;

            int name_len = 0;
            while (btn->name[name_len]) name_len++;

            video_put_char_at('[', color, x, row);
            for (int j = 0; j < name_len && j < icon_chars - 2; j++) {
                video_put_char_at(btn->name[j], color, x + 1 + j, row);
            }
            video_put_char_at(']', color, x + 1 + name_len, row);

            x += 2 + name_len;
        }

        taskbar_update_clock();
    } else {
        for (int y = 0; y < SCREEN_ROWS; y++) {
            video_put_char_at(0xBA, 0x07, col, y);
        }

        video_put_char_at(0xC9, 0x07, col, 0);
        video_put_char_at(0xC8, 0x07, col, SCREEN_ROWS - 1);

        video_put_char_at('I', 0x0F, col, 2);
        video_put_char_at('n', 0x0F, col, 3);
        video_put_char_at('i', 0x0F, col, 4);
        video_put_char_at('c', 0x0F, col, 5);
        video_put_char_at('i', 0x0F, col, 6);
        video_put_char_at('o', 0x0F, col, 7);

        for (int i = 0; i < button_count && i < 8; i++) {
            tb_button_t* btn = &buttons[i];
            uint8_t color = btn->active ? 0x1F : 0x07;

            int name_len = 0;
            while (btn->name[name_len]) name_len++;

            int start_y = 9 + i * 2;
            if (start_y + 1 > SCREEN_ROWS - 2) break;

            video_put_char_at('[', color, col, start_y);
            if (name_len > 0) video_put_char_at(btn->name[0], color, col, start_y + 1);
            video_put_char_at(']', color, col, start_y + 2 < SCREEN_ROWS - 1 ? start_y + 2 : SCREEN_ROWS - 2);
        }

        taskbar_update_clock();
    }

    taskbar_redraw_menu();
    vesa_frame_end();
}

void taskbar_update_clock(void) {
    uint32_t ticks = timer_get_ticks();
    uint32_t current_second = ticks / 50;

    if (current_second == last_second) return;
    last_second = current_second;

    uint32_t total_secs = current_second;
    uint32_t hours = total_secs / 3600;
    uint32_t minutes = (total_secs % 3600) / 60;
    uint32_t seconds = total_secs % 60;

    char time_str[6];
    char num_buf[4];

    int_to_str(hours, num_buf);
    time_str[0] = (hours < 10) ? '0' : num_buf[0];
    time_str[1] = (hours < 10) ? num_buf[0] : num_buf[1];
    time_str[2] = ':';

    int_to_str(minutes, num_buf);
    time_str[3] = (minutes < 10) ? '0' : num_buf[0];
    time_str[4] = (minutes < 10) ? num_buf[0] : num_buf[1];
    time_str[5] = '\0';

    vesa_mode_t* mode = vesa_get_mode();
    if (mode && mode->initialized && vesa_has_backbuffer()) {
        int tb_y = mode->height - TASKBAR_HEIGHT;
        int clock_w = 40;
        int clock_x = mode->width - clock_w - 10;
        int clock_y;
        if (config.position == TB_POS_TOP) tb_y = 0;
        clock_y = tb_y + 4;
        vesa_frame_begin_region((uint32_t)clock_x, (uint32_t)clock_y,
                                (uint32_t)clock_w, 16);
        mouse_invalidate_cursor();

        vesa_color_t bg; bg.raw = GUI_COLOR_BG;
        
        vesa_fill_rect(clock_x, clock_y, clock_w, 16, bg); // limpa o fundo antigo
        gui_draw_text(clock_x, clock_y, time_str, GUI_COLOR_TEXT);
        vesa_frame_end();
        return;
    }

    int row = get_row();
    int col = get_col();
    int is_horizontal = (config.position == TB_POS_BOTTOM || config.position == TB_POS_TOP || config.position == TB_POS_CUSTOM);

    if (is_horizontal) {
        video_print_at(SCREEN_COLS - 10, row, time_str, 0x07);
    } else {
        int clock_y_tui = SCREEN_ROWS - 5;
        video_put_char_at(time_str[0], 0x07, col, clock_y_tui);
        video_put_char_at(time_str[1], 0x07, col, clock_y_tui + 1);
        video_put_char_at(time_str[2], 0x07, col, clock_y_tui + 2);
        video_put_char_at(time_str[3], 0x07, col, clock_y_tui + 3);
        video_put_char_at(time_str[4], 0x07, col, clock_y_tui + 4);
    }
}

static void taskbar_update_clock_gui(void) {
    // Força o relógio a se redesenhar no GUI ignorando `last_second` 
    // fazendo um backup e invalidando-o
    uint32_t bk = last_second;
    last_second = 0xFFFFFFFF;
    taskbar_update_clock();
    last_second = bk;
}

void taskbar_add_app(tb_app_type_t type, const char* name) {
    for (int i = 0; i < button_count; i++) {
        if (buttons[i].type == type) {
            buttons[i].active = 1;
            taskbar_draw();
            return;
        }
    }

    if (button_count >= TASKBAR_BUTTON_MAX) return;

    buttons[button_count].name = name;
    buttons[button_count].type = type;
    buttons[button_count].active = 1;
    button_count++;

    taskbar_draw();
}

void taskbar_remove_app(tb_app_type_t type) {
    for (int i = 0; i < button_count; i++) {
        if (buttons[i].type == type) {
            buttons[i].active = 0;
            break;
        }
    }

    taskbar_draw();
}

int taskbar_is_menu_open(void) {
    return menu_open || config_menu_open;
}

static void taskbar_draw_menu(void) {
    if (!menu_open) {
        taskbar_draw();
        return;
    }

    vesa_mode_t* mode = vesa_get_mode();
    if (mode && mode->initialized && vesa_has_backbuffer()) {
        taskbar_draw_menu_gui();
        return;
    }

    int menu_x = 1;
    int menu_y = 18;

    video_fill_rect(menu_x, menu_y, 16, MENU_ITEM_COUNT + 2, ' ', 0x17);
    video_draw_box(menu_x, menu_y, 16, MENU_ITEM_COUNT + 2, 0x01);

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        uint8_t color = (menu_selection == i) ? 0x1F : 0x17;

        if (menu_selection == i) {
            video_fill_rect(menu_x + 1, menu_y + 1 + i, 14, 1, ' ', 0x1F);
        }

        video_print_at(menu_x + 2, menu_y + 1 + i, menu_items[i], color);
    }
}

void taskbar_redraw_menu(void) {
    if (menu_open) taskbar_draw_menu();
}

static void taskbar_draw_menu_gui(void) {
    if (!menu_open) return;
    
    vesa_mode_t* mode = vesa_get_mode();
    if (!mode || !mode->initialized || !vesa_has_backbuffer()) return;

    int tb_y = mode->height - TASKBAR_HEIGHT;
    if (config.position == TB_POS_TOP) tb_y = 0;

    int menu_w = 160;
    int menu_h = MENU_ITEM_COUNT * FONT_HEIGHT + 10;
    
    int menu_x = 2;
    int menu_y = tb_y - menu_h;
    if (config.position == TB_POS_TOP) menu_y = TASKBAR_HEIGHT;

    vesa_frame_begin_region((uint32_t)menu_x, (uint32_t)menu_y,
                            (uint32_t)menu_w, (uint32_t)menu_h);
    mouse_invalidate_cursor();
    
    vesa_color_t bg; bg.raw = GUI_COLOR_BG;
    vesa_color_t light; light.raw = GUI_COLOR_BORDER_L;
    vesa_color_t dark; dark.raw = GUI_COLOR_BORDER_D;
    
    vesa_fill_rect(menu_x, menu_y, menu_w, menu_h, bg);
    vesa_draw_hline(menu_x, menu_y, menu_w, light);
    vesa_draw_vline(menu_x, menu_y, menu_h, light);
    vesa_draw_hline(menu_x, menu_y + menu_h - 1, menu_w, dark);
    vesa_draw_vline(menu_x + menu_w - 1, menu_y, menu_h, dark);
    
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        int item_y = menu_y + 5 + i * FONT_HEIGHT;
        
        if (menu_selection == i) {
            vesa_color_t sel_bg; sel_bg.raw = GUI_COLOR_TITLE_BG;
            vesa_fill_rect(menu_x + 3, item_y, menu_w - 6, FONT_HEIGHT, sel_bg);
            gui_draw_text(menu_x + 10, item_y, menu_items[i], GUI_COLOR_TEXT_W);
        } else {
            gui_draw_text(menu_x + 10, item_y, menu_items[i], GUI_COLOR_TEXT);
        }
    }

    vesa_frame_end();
}

static void taskbar_close_menu(void) {
    menu_open = 0;
    menu_selection = 0;
    taskbar_draw();
}

void taskbar_draw_config_menu(void) {
    vesa_frame_begin();
    mouse_invalidate_cursor();

    if (!config_menu_open) {
        config_menu_open = 1;
        config_selection = 0;
    }

    int menu_x = 20;
    int menu_y = 5;
    int menu_w = 40;
    int menu_h = CONFIG_ITEM_COUNT + 2;

    video_fill_rect(menu_x, menu_y, menu_w, menu_h, ' ', 0x17);
    video_draw_box(menu_x, menu_y, menu_w, menu_h, 0x01);

    video_print_at(menu_x + 2, menu_y + 1, "Configuracoes da Taskbar", 0x1F);
    video_draw_hline(menu_x + 1, menu_y + 2, menu_w - 2, 0xC4, 0x01);

    for (int i = 0; i < CONFIG_ITEM_COUNT; i++) {
        uint8_t color = (config_selection == i) ? 0x1F : 0x17;

        if (config_selection == i) {
            video_fill_rect(menu_x + 1, menu_y + 3 + i, menu_w - 2, 1, ' ', 0x1F);
        }

        video_print_at(menu_x + 2, menu_y + 3 + i, config_items[i], color);

        if (i == 0) {
            video_print_at(menu_x + 12, menu_y + 3 + i, position_names[config.position], color);
        } else if (i == 1) {
            video_print_at(menu_x + 12, menu_y + 3 + i, size_names[config.icon_size], color);
        } else if (i == 2) {
            video_print_at(menu_x + 12, menu_y + 3 + i, config.pinned ? "Sim" : "Nao", color);
        }
    }

    video_draw_hline(menu_x + 1, menu_y + menu_h - 2, menu_w - 2, 0xC4, 0x01);
    video_print_at(menu_x + 2, menu_y + menu_h - 1, "Esc: Fechar | Enter: Alterar", 0x08);

    vesa_frame_end();
}

static void taskbar_close_config_menu(void) {
    config_menu_open = 0;
    config_selection = 0;
    taskbar_draw();
}

int taskbar_handle_config_key(uint8_t scancode) {
    if (!config_menu_open) return 0;

    if (scancode & 0x80) return 0;

    if (scancode == 0x01) {
        taskbar_close_config_menu();
        return 9;
    }

    if (scancode == 0x48) {
        if (config_selection > 0) config_selection--;
        else config_selection = CONFIG_ITEM_COUNT - 1;
        taskbar_draw_config_menu();
        return 1;
    }

    if (scancode == 0x50) {
        if (config_selection < CONFIG_ITEM_COUNT - 1) config_selection++;
        else config_selection = 0;
        taskbar_draw_config_menu();
        return 1;
    }

    if (scancode == 0x1C) {
        switch (config_selection) {
            case 0:
                config.position = (config.position + 1) % 5;
                update_dimensions();
                break;
            case 1:
                config.icon_size = (config.icon_size + 1) % 3;
                break;
            case 2:
                config.pinned = !config.pinned;
                break;
            case 3:
                taskbar_set_position(TB_POS_TOP);
                break;
            case 4:
                taskbar_set_position(TB_POS_BOTTOM);
                break;
            case 5:
                taskbar_set_position(TB_POS_LEFT);
                break;
            case 6:
                taskbar_set_position(TB_POS_RIGHT);
                break;
            case 7:
                config.position = TB_POS_CUSTOM;
                config.custom_x = 20;
                config.custom_y = 12;
                update_dimensions();
                break;
        }
        taskbar_draw_config_menu();
        return 1;
    }

    return 1;
}

int taskbar_handle_key(uint8_t scancode) {
    if (scancode & 0x80) return 0;

    if (config_menu_open) {
        return taskbar_handle_config_key(scancode);
    }

    if (menu_open) {
        if (scancode == 0x01 || scancode == 0x5B || scancode == 0x5C) {
            taskbar_close_menu();
            return 9;
        }

        if (scancode == 0x48) {
            if (menu_selection > 0) menu_selection--;
            else menu_selection = MENU_ITEM_COUNT - 1;
            taskbar_draw_menu();
            return 1;
        }

        if (scancode == 0x50) {
            if (menu_selection < MENU_ITEM_COUNT - 1) menu_selection++;
            else menu_selection = 0;
            taskbar_draw_menu();
            return 1;
        }

        if (scancode == 0x1C) {
            int selected = menu_selection;
            taskbar_close_menu();

            switch (selected) {
                case 0: return 7;
                case 1: return 2;
                case 2: return 3;
                case 3: return 4;
                case 4: return 8;
                case 5: return 5;
                case 6: return 6;
            }
            return 1;
        }

        return 1;
    }

    if (scancode == 0x3B) {
        config_menu_open = 1;
        config_selection = 0;
        taskbar_draw_config_menu();
        return 1;
    }

    if (scancode == 0x5B || scancode == 0x5C) {
        menu_open = 1;
        menu_selection = 0;
        taskbar_draw_menu();
        return 1;
    }

    return 0;
}

void taskbar_set_position(tb_position_t pos) {
    config.position = pos;
    update_dimensions();
    taskbar_draw();
}

void taskbar_set_icon_size(tb_icon_size_t size) {
    config.icon_size = size;
    taskbar_draw();
}

void taskbar_set_pinned(int pinned) {
    config.pinned = pinned;
    taskbar_draw();
}

void taskbar_set_custom_position(int x, int y) {
    config.position = TB_POS_CUSTOM;
    config.custom_x = x;
    config.custom_y = y;
    update_dimensions();
    taskbar_draw();
}

tb_config_t* taskbar_get_config(void) {
    return &config;
}

static int taskbar_handle_click_gui(int px, int py) {
    vesa_mode_t* mode = vesa_get_mode();
    int tb_y = mode->height - TASKBAR_HEIGHT;
    if (config.position == TB_POS_TOP) tb_y = 0;

    // Se menu aberto, verifica clique no menu
    if (menu_open) {
        int menu_w = 160;
        int menu_h = MENU_ITEM_COUNT * FONT_HEIGHT + 10;
        int menu_x = 2;
        int menu_y = tb_y - menu_h;
        if (config.position == TB_POS_TOP) menu_y = TASKBAR_HEIGHT;
        
        if (px >= menu_x && px < menu_x + menu_w &&
            py >= menu_y && py < menu_y + menu_h) {
            
            int rel_y = py - (menu_y + 5);
            if (rel_y >= 0 && rel_y < MENU_ITEM_COUNT * FONT_HEIGHT) {
                int selected = rel_y / FONT_HEIGHT;
                taskbar_close_menu();
                switch (selected) {
                    case 0: return 7;  /* Desktop */
                    case 1: return 2;  /* Shell */
                    case 2: return 3;  /* Explorer */
                    case 3: return 4;  /* TaskMgr */
                    case 4: return 8;  /* Configuracoes */
                    case 5: return 5;  /* Reiniciar */
                    case 6: return 6;  /* Desligar */
                }
            }
            return 1;
        }
        // Clicou fora do menu, mas o menu estava aberto, vamos fechar
        // E prosseguir para ver se clicou na taskbar
        if (py < tb_y || py >= tb_y + TASKBAR_HEIGHT) {
            taskbar_close_menu();
            return 0; // Deixa outro componente processar
        }
    }

    // Clique na taskbar
    if (py >= tb_y && py < tb_y + TASKBAR_HEIGHT) {
        int start_x = 2;
        if (px >= start_x && px < start_x + 60) {
            if (menu_open) {
                taskbar_close_menu();
                return 9;
            }
            menu_open = 1;
            menu_selection = 0;
            taskbar_draw_menu();
            return 1;
        }

        start_x += 64;
        for (int i = 0; i < button_count; i++) {
            if (px >= start_x && px < start_x + 90) {
                switch (buttons[i].type) {
                    case TB_APP_SHELL: return 2;
                    case TB_APP_EXPLORER: return 3;
                    case TB_APP_TASKMGR: return 4;
                    case TB_APP_DESKTOP: return 7;
                    default: return 1;
                }
            }
            start_x += 94;
        }
        return 1; // Clicou na taskbar, interceptado
    }
    return 0;
}

int taskbar_handle_click(int px, int py) {
    vesa_mode_t* mode = vesa_get_mode();
    if (mode && mode->initialized && vesa_has_backbuffer()) {
        return taskbar_handle_click_gui(px, py);
    }

    /* Converte pixel para coordenadas de texto (fonte 8x16) */
    int col = px / 8;
    int row = py / 16;
    int tb_row = get_row();

    int is_horizontal = (config.position == TB_POS_BOTTOM ||
                         config.position == TB_POS_TOP ||
                         config.position == TB_POS_CUSTOM);

    if (!is_horizontal) return 0;
    if (row != tb_row) return 0;

    /* Botao Inicio esta nas colunas 1..8 */
    if (col >= 1 && col <= 8) {
        if (menu_open) {
            menu_open = 0;
            menu_selection = 0;
            taskbar_draw();
            return 9;
        }
        menu_open = 1;
        menu_selection = 0;
        taskbar_draw_menu();
        return 1;
    }

    /* Se o menu esta aberto, verifica clique em item do menu */
    if (menu_open) {
        int menu_x = 1;
        int menu_y = 18;
        if (col >= menu_x && col < menu_x + 16 &&
            row >= menu_y + 1 && row < menu_y + 1 + MENU_ITEM_COUNT) {
            int selected = row - menu_y - 1;
            menu_open = 0;
            menu_selection = 0;
            taskbar_draw();
            switch (selected) {
                case 0: return 7;  /* Desktop */
                case 1: return 2;  /* Shell */
                case 2: return 3;  /* Explorer */
                case 3: return 4;  /* TaskMgr */
                case 4: return 8;  /* Configuracoes */
                case 5: return 5;  /* Reiniciar */
                case 6: return 6;  /* Desligar */
            }
        }
    }

    return 0;
}
