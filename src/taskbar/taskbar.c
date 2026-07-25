#include "ui/taskbar.h"
#include "core/video.h"
#include "core/keyboard.h"
#include "core/log.h"
#include "core/timer.h"
#include "ui/desktop.h"
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
static int pending_window_id = -1;

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

#define TASKBAR_HEIGHT 24
#define TASKBAR_SIDE_WIDTH 96
#define TASKBAR_CUSTOM_WIDTH 320
#define TASKBAR_MENU_WIDTH 160
#define TASKBAR_BUTTON_HEIGHT 20

static int taskbar_uses_gui(void) {
    vesa_mode_t* mode = vesa_get_mode();

    return desktop_get_mode() == DESKTOP_MODE_MODERN && mode &&
           mode->initialized && vesa_has_backbuffer();
}

static int taskbar_config_item_count(void) {
    return CONFIG_ITEM_COUNT;
}

static int taskbar_is_horizontal_gui(void) {
    return config.position == TB_POS_BOTTOM ||
           config.position == TB_POS_TOP ||
           config.position == TB_POS_CUSTOM;
}

static int taskbar_point_in_rect(int px, int py, const tb_rect_t* rect) {
    return px >= rect->x && px < rect->x + rect->width &&
           py >= rect->y && py < rect->y + rect->height;
}

static void taskbar_clamp_rect(tb_rect_t* rect, vesa_mode_t* mode) {
    if (rect->width > (int)mode->width) rect->width = mode->width;
    if (rect->height > (int)mode->height) rect->height = mode->height;
    if (rect->x < 0) rect->x = 0;
    if (rect->y < 0) rect->y = 0;
    if (rect->x + rect->width > (int)mode->width) {
        rect->x = mode->width - rect->width;
    }
    if (rect->y + rect->height > (int)mode->height) {
        rect->y = mode->height - rect->height;
    }
}

int taskbar_get_bounds(tb_rect_t* bounds) {
    vesa_mode_t* mode = vesa_get_mode();

    if (!bounds) {
        LOG_ERROR("TASKBAR", "Destino de limites nulo");
        return 0;
    }
    if (!taskbar_uses_gui()) return 0;

    bounds->x = 0;
    bounds->y = 0;
    bounds->width = mode->width;
    bounds->height = TASKBAR_HEIGHT;

    switch (config.position) {
        case TB_POS_BOTTOM:
            bounds->y = mode->height - TASKBAR_HEIGHT;
            break;
        case TB_POS_TOP:
            break;
        case TB_POS_LEFT:
            bounds->width = TASKBAR_SIDE_WIDTH;
            bounds->height = mode->height;
            break;
        case TB_POS_RIGHT:
            bounds->x = mode->width - TASKBAR_SIDE_WIDTH;
            bounds->width = TASKBAR_SIDE_WIDTH;
            bounds->height = mode->height;
            break;
        case TB_POS_CUSTOM:
            bounds->x = config.custom_x * FONT_WIDTH;
            bounds->y = config.custom_y * FONT_HEIGHT;
            bounds->width = TASKBAR_CUSTOM_WIDTH;
            taskbar_clamp_rect(bounds, mode);
            break;
    }
    return 1;
}

int taskbar_get_work_area(tb_rect_t* area) {
    vesa_mode_t* mode = vesa_get_mode();
    tb_rect_t bounds;

    if (!area) {
        LOG_ERROR("TASKBAR", "Destino de area de trabalho nulo");
        return 0;
    }
    if (!taskbar_uses_gui() || !taskbar_get_bounds(&bounds)) return 0;

    area->x = 0;
    area->y = 0;
    area->width = mode->width;
    area->height = mode->height;

    switch (config.position) {
        case TB_POS_BOTTOM:
            area->height -= bounds.height;
            break;
        case TB_POS_TOP:
            area->y += bounds.height;
            area->height -= bounds.height;
            break;
        case TB_POS_LEFT:
            area->x += bounds.width;
            area->width -= bounds.width;
            break;
        case TB_POS_RIGHT:
            area->width -= bounds.width;
            break;
        case TB_POS_CUSTOM:
            break;
    }
    return 1;
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
    pending_window_id = -1;

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

static void taskbar_draw_menu_gui(void);

static void taskbar_format_time(uint32_t current_second, char* time_str) {
    uint32_t hours = current_second / 3600;
    uint32_t minutes = (current_second % 3600) / 60;
    char num_buf[4];

    int_to_str(hours, num_buf);
    time_str[0] = (hours < 10) ? '0' : num_buf[0];
    time_str[1] = (hours < 10) ? num_buf[0] : num_buf[1];
    time_str[2] = ':';
    int_to_str(minutes, num_buf);
    time_str[3] = (minutes < 10) ? '0' : num_buf[0];
    time_str[4] = (minutes < 10) ? num_buf[0] : num_buf[1];
    time_str[5] = '\0';
}

static void taskbar_draw_clock_gui_text(const char* time_str) {
    tb_rect_t bounds;
    int clock_x;
    int clock_y;
    vesa_color_t bg;

    if (!taskbar_get_bounds(&bounds)) return;
    if (taskbar_is_horizontal_gui()) {
        clock_x = bounds.x + bounds.width - 48;
        clock_y = bounds.y + 4;
    } else {
        clock_x = bounds.x + (bounds.width - 40) / 2;
        clock_y = bounds.y + bounds.height - 20;
    }
    bg.raw = GUI_COLOR_BG;
    vesa_fill_rect(clock_x, clock_y, 40, 16, bg);
    gui_draw_text(clock_x, clock_y, time_str, GUI_COLOR_TEXT);
}

static int taskbar_get_button_rect(const tb_rect_t* bounds, int index,
                                   tb_rect_t* rect) {
    int vertical = !taskbar_is_horizontal_gui();
    int clock_limit;

    if (!bounds || !rect || index < -1 || index >= button_count) return 0;

    if (!vertical) {
        rect->x = bounds->x + (index < 0 ? 2 : 66 + index * 94);
        rect->y = bounds->y + 2;
        rect->width = index < 0 ? 60 : 90;
        rect->height = TASKBAR_BUTTON_HEIGHT;
        clock_limit = bounds->x + bounds->width - 50;
        return rect->x + rect->width <= clock_limit;
    }

    rect->x = bounds->x + 2;
    rect->y = bounds->y + (index < 0 ? 2 : 26 + index * 22);
    rect->width = bounds->width - 4;
    rect->height = TASKBAR_BUTTON_HEIGHT;
    clock_limit = bounds->y + bounds->height - 22;
    return rect->y + rect->height <= clock_limit;
}

static void taskbar_draw_gui(void) {
    tb_rect_t bounds;
    tb_rect_t button;
    char time_str[6];
    if (!taskbar_uses_gui()) return;
    if (!taskbar_get_bounds(&bounds)) return;

    mouse_invalidate_cursor();

    vesa_color_t bg; bg.raw = GUI_COLOR_BG;
    vesa_color_t light; light.raw = GUI_COLOR_BORDER_L;

    vesa_fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, bg);
    vesa_draw_hline(bounds.x, bounds.y, bounds.width, light);
    vesa_draw_vline(bounds.x, bounds.y, bounds.height, light);

    if (taskbar_get_button_rect(&bounds, -1, &button)) {
        gui_draw_button(button.x, button.y, button.width, button.height,
                        "Inicio", menu_open);
    }
    for (int i = 0; i < button_count; i++) {
        tb_button_t* btn = &buttons[i];
        if (!taskbar_get_button_rect(&bounds, i, &button)) break;
        gui_draw_button(button.x, button.y, button.width, button.height,
                        btn->name, btn->active);
    }

    taskbar_format_time(timer_get_ticks() / 50, time_str);
    taskbar_draw_clock_gui_text(time_str);
}

void taskbar_draw(void) {
    if (taskbar_uses_gui()) {
        tb_rect_t bounds;
        if (!taskbar_get_bounds(&bounds)) return;
        vesa_frame_begin_region(bounds.x, bounds.y, bounds.width, bounds.height);
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

    char time_str[6];
    taskbar_format_time(current_second, time_str);

    if (taskbar_uses_gui()) {
        tb_rect_t bounds;
        int clock_x;
        int clock_y;

        if (!taskbar_get_bounds(&bounds)) return;
        if (taskbar_is_horizontal_gui()) {
            clock_x = bounds.x + bounds.width - 48;
            clock_y = bounds.y + 4;
        } else {
            clock_x = bounds.x + (bounds.width - 40) / 2;
            clock_y = bounds.y + bounds.height - 20;
        }
        vesa_frame_begin_region((uint32_t)clock_x, (uint32_t)clock_y,
                                40, 16);
        mouse_invalidate_cursor();
        taskbar_draw_clock_gui_text(time_str);
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

void taskbar_add_app(tb_app_type_t type, const char* name) {
    if (!name) {
        LOG_ERROR("TASKBAR", "Nome de aplicativo nulo");
        return;
    }

    for (int i = 0; i < button_count; i++) {
        if (buttons[i].type == type) {
            buttons[i].active = 1;
            return;
        }
    }

    if (button_count >= TASKBAR_BUTTON_MAX) {
        LOG_WARN("TASKBAR", "Limite de botoes atingido");
        return;
    }

    buttons[button_count].name = name;
    buttons[button_count].type = type;
    buttons[button_count].active = 1;
    buttons[button_count].window_id = -1;
    button_count++;
}

void taskbar_remove_app(tb_app_type_t type) {
    for (int i = 0; i < button_count; i++) {
        if (buttons[i].type == type) {
            buttons[i].active = 0;
            break;
        }
    }

}

void taskbar_add_window(int window_id, const char* name) {
    if (!name || window_id < 0) {
        LOG_ERROR("TASKBAR", "Janela invalida para taskbar");
        return;
    }

    for (int i = 0; i < button_count; i++) {
        if (buttons[i].type == TB_APP_WINDOW &&
            buttons[i].window_id == window_id) {
            buttons[i].active = 1;
            return;
        }
    }

    if (button_count >= TASKBAR_BUTTON_MAX) {
        LOG_WARN("TASKBAR", "Limite de botoes atingido para janela");
        return;
    }

    buttons[button_count].name = name;
    buttons[button_count].type = TB_APP_WINDOW;
    buttons[button_count].active = 1;
    buttons[button_count].window_id = window_id;
    button_count++;
}

void taskbar_remove_window(int window_id) {
    for (int i = 0; i < button_count; i++) {
        if (buttons[i].type == TB_APP_WINDOW &&
            buttons[i].window_id == window_id) {
            for (int j = i; j < button_count - 1; j++) {
                buttons[j] = buttons[j + 1];
            }
            button_count--;
            return;
        }
    }
}

void taskbar_set_window_active(int window_id, int active) {
    for (int i = 0; i < button_count; i++) {
        if (buttons[i].type == TB_APP_WINDOW &&
            buttons[i].window_id == window_id) {
            buttons[i].active = active ? 1 : 0;
            return;
        }
    }
}

int taskbar_take_window_request(void) {
    int window_id = pending_window_id;
    pending_window_id = -1;
    return window_id;
}

int taskbar_is_menu_open(void) {
    return menu_open || config_menu_open;
}

static void taskbar_draw_menu(void) {
    if (!menu_open) {
        taskbar_draw();
        return;
    }

    if (taskbar_uses_gui()) {
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

static int taskbar_get_menu_bounds(tb_rect_t* menu) {
    vesa_mode_t* mode = vesa_get_mode();
    tb_rect_t bounds;

    if (!menu || !taskbar_get_bounds(&bounds)) return 0;

    menu->width = TASKBAR_MENU_WIDTH;
    menu->height = MENU_ITEM_COUNT * FONT_HEIGHT + 10;
    menu->x = bounds.x + 2;
    menu->y = bounds.y - menu->height;

    switch (config.position) {
        case TB_POS_TOP:
            menu->y = bounds.y + bounds.height;
            break;
        case TB_POS_LEFT:
            menu->x = bounds.x + bounds.width;
            menu->y = bounds.y + 2;
            break;
        case TB_POS_RIGHT:
            menu->x = bounds.x - menu->width;
            menu->y = bounds.y + 2;
            break;
        case TB_POS_CUSTOM:
            if (menu->y < 0) menu->y = bounds.y + bounds.height;
            break;
        case TB_POS_BOTTOM:
            break;
    }

    taskbar_clamp_rect(menu, mode);
    return 1;
}

static void taskbar_draw_menu_gui(void) {
    tb_rect_t menu;
    if (!menu_open) return;
    if (!taskbar_uses_gui()) return;
    if (!taskbar_get_menu_bounds(&menu)) return;

    vesa_frame_begin_region((uint32_t)menu.x, (uint32_t)menu.y,
                            (uint32_t)menu.width, (uint32_t)menu.height);
    mouse_invalidate_cursor();
    
    vesa_color_t bg; bg.raw = GUI_COLOR_BG;
    vesa_color_t light; light.raw = GUI_COLOR_BORDER_L;
    vesa_color_t dark; dark.raw = GUI_COLOR_BORDER_D;
    
    vesa_fill_rect(menu.x, menu.y, menu.width, menu.height, bg);
    vesa_draw_hline(menu.x, menu.y, menu.width, light);
    vesa_draw_vline(menu.x, menu.y, menu.height, light);
    vesa_draw_hline(menu.x, menu.y + menu.height - 1, menu.width, dark);
    vesa_draw_vline(menu.x + menu.width - 1, menu.y, menu.height, dark);
    
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        int item_y = menu.y + 5 + i * FONT_HEIGHT;
        
        if (menu_selection == i) {
            vesa_color_t sel_bg; sel_bg.raw = GUI_COLOR_TITLE_BG;
            vesa_fill_rect(menu.x + 3, item_y, menu.width - 6, FONT_HEIGHT, sel_bg);
            gui_draw_text(menu.x + 10, item_y, menu_items[i], GUI_COLOR_TEXT_W);
        } else {
            gui_draw_text(menu.x + 10, item_y, menu_items[i], GUI_COLOR_TEXT);
        }
    }

    vesa_frame_end();
}

static void taskbar_reset_menu(void) {
    menu_open = 0;
    menu_selection = 0;
}

static void taskbar_close_menu(void) {
    taskbar_reset_menu();
    taskbar_draw();
}

void taskbar_draw_config_menu(void) {
    int item_count;
    int menu_h;

    vesa_frame_begin();
    mouse_invalidate_cursor();

    if (!config_menu_open) {
        config_menu_open = 1;
        config_selection = 0;
    }

    int menu_x = 20;
    int menu_y = 5;
    int menu_w = 40;
    item_count = taskbar_config_item_count();
    menu_h = item_count + 2;

    video_fill_rect(menu_x, menu_y, menu_w, menu_h, ' ', 0x17);
    video_draw_box(menu_x, menu_y, menu_w, menu_h, 0x01);

    video_print_at(menu_x + 2, menu_y + 1, "Configuracoes da Taskbar", 0x1F);
    video_draw_hline(menu_x + 1, menu_y + 2, menu_w - 2, 0xC4, 0x01);

    for (int i = 0; i < item_count; i++) {
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
    int item_count;

    if (!config_menu_open) return 0;

    if (scancode & 0x80) return 0;

    if (scancode == 0x01) {
        taskbar_close_config_menu();
        return 9;
    }

    item_count = taskbar_config_item_count();

    if (scancode == 0x48) {
        if (config_selection > 0) config_selection--;
        else config_selection = item_count - 1;
        taskbar_draw_config_menu();
        return 1;
    }

    if (scancode == 0x50) {
        if (config_selection < item_count - 1) config_selection++;
        else config_selection = 0;
        taskbar_draw_config_menu();
        return 1;
    }

    if (scancode == 0x1C) {
        switch (config_selection) {
            case 0:
                taskbar_set_position((tb_position_t)
                                     ((config.position + 1) % 5));
                break;
            case 1:
                taskbar_set_icon_size((tb_icon_size_t)
                                      ((config.icon_size + 1) % 3));
                break;
            case 2:
                taskbar_set_pinned(!config.pinned);
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
                taskbar_set_custom_position(20, 12);
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
            taskbar_reset_menu();

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
    if (pos < TB_POS_BOTTOM || pos > TB_POS_CUSTOM) {
        LOG_ERROR("TASKBAR", "Posicao invalida");
        return;
    }
    config.position = pos;
    update_dimensions();
}

void taskbar_set_icon_size(tb_icon_size_t size) {
    if (size < TB_SIZE_SMALL || size > TB_SIZE_LARGE) {
        LOG_ERROR("TASKBAR", "Tamanho de icone invalido");
        return;
    }
    config.icon_size = size;
}

void taskbar_set_pinned(int pinned) {
    config.pinned = pinned;
}

void taskbar_set_custom_position(int x, int y) {
    config.position = TB_POS_CUSTOM;
    config.custom_x = x;
    config.custom_y = y;
    update_dimensions();
}

tb_config_t* taskbar_get_config(void) {
    return &config;
}

static int taskbar_handle_click_gui(int px, int py) {
    tb_rect_t bounds;
    tb_rect_t menu;
    tb_rect_t button;

    if (!taskbar_get_bounds(&bounds)) return 0;

    // Se menu aberto, verifica clique no menu
    if (menu_open) {
        if (!taskbar_get_menu_bounds(&menu)) {
            taskbar_close_menu();
            return 1;
        }
        if (taskbar_point_in_rect(px, py, &menu)) {
            int rel_y = py - (menu.y + 5);
            if (rel_y >= 0 && rel_y < MENU_ITEM_COUNT * FONT_HEIGHT) {
                int selected = rel_y / FONT_HEIGHT;
                taskbar_reset_menu();
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

        if (taskbar_get_button_rect(&bounds, -1, &button) &&
            taskbar_point_in_rect(px, py, &button)) {
            taskbar_close_menu();
            return 9;
        }

        /* O clique que fecha o menu nunca deve alcancar a cena abaixo. */
        taskbar_close_menu();
        return 1;
    }

    if (!taskbar_point_in_rect(px, py, &bounds)) return 0;

    if (taskbar_get_button_rect(&bounds, -1, &button) &&
        taskbar_point_in_rect(px, py, &button)) {
        menu_open = 1;
        menu_selection = 0;
        taskbar_draw_menu();
        return 1;
    }

    for (int i = 0; i < button_count; i++) {
        if (!taskbar_get_button_rect(&bounds, i, &button) ||
            !taskbar_point_in_rect(px, py, &button)) {
            continue;
        }

        switch (buttons[i].type) {
            case TB_APP_SHELL: return 2;
            case TB_APP_EXPLORER: return 3;
            case TB_APP_TASKMGR: return 4;
            case TB_APP_DESKTOP: return 7;
            case TB_APP_WINDOW:
                pending_window_id = buttons[i].window_id;
                return TB_ACTION_WINDOW;
            default: return 1;
        }
    }

    return 1;
}

int taskbar_handle_click(int px, int py) {
    if (taskbar_uses_gui()) {
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

    if (menu_open) {
        int menu_x = 1;
        int menu_y = 18;

        if (col >= menu_x && col < menu_x + 16 &&
            row >= menu_y + 1 && row < menu_y + 1 + MENU_ITEM_COUNT) {
            int selected = row - menu_y - 1;
            taskbar_reset_menu();
            switch (selected) {
                case 0: return 7;  /* Desktop */
                case 1: return 2;  /* Shell */
                case 2: return 3;  /* Explorer */
                case 3: return 4;  /* TaskMgr */
                case 4: return 8;  /* Configuracoes */
                case 5: return 5;  /* Reiniciar */
                case 6: return 6;  /* Desligar */
            }
            return 1;
        }

        if (row != tb_row || col < 1 || col > 8) {
            taskbar_close_menu();
            return 1;
        }
    }

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

    return 0;
}
