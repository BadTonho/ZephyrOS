#include "taskbar.h"
#include "video.h"
#include "keyboard.h"
#include "timer.h"

static tb_button_t buttons[TASKBAR_BUTTON_MAX];
static int button_count = 0;
static int menu_open = 0;
static int menu_selection = 0;
static uint32_t last_second = 0;

#define MENU_ITEM_COUNT 5
static const char* menu_items[MENU_ITEM_COUNT] = {
    "Shell",
    "Explorer",
    "Task Manager",
    "Reiniciar",
    "Desligar"
};

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

void taskbar_init(void) {
    button_count = 0;
    menu_open = 0;
    menu_selection = 0;
    last_second = 0;

    taskbar_add_app(TB_APP_SHELL, "Shell");
}

void taskbar_draw(void) {
    video_fill_rect(0, TASKBAR_ROW, 80, 1, ' ', TASKBAR_COLOR_BG);

    int x = 1;

    uint8_t start_color = menu_open ? TASKBAR_COLOR_BTN_ACTIVE : TASKBAR_COLOR_BTN;
    video_put_char_at('[', start_color, x, TASKBAR_ROW);
    video_print_at(x + 1, TASKBAR_ROW, "Inicio", start_color);
    video_put_char_at(']', start_color, x + 7, TASKBAR_ROW);
    x += 9;

    for (int i = 0; i < button_count; i++) {
        if (x > 68) break;

        video_put_char_at(' ', TASKBAR_COLOR_BG, x, TASKBAR_ROW);
        x++;

        tb_button_t* btn = &buttons[i];
        uint8_t color = btn->active ? TASKBAR_COLOR_BTN_ACTIVE : TASKBAR_COLOR_BTN;

        int name_len = 0;
        while (btn->name[name_len]) name_len++;

        video_put_char_at('[', color, x, TASKBAR_ROW);
        for (int j = 0; j < name_len && j < 10; j++) {
            video_put_char_at(btn->name[j], color, x + 1 + j, TASKBAR_ROW);
        }
        video_put_char_at(']', color, x + 1 + name_len, TASKBAR_ROW);

        x += 2 + name_len;
    }

    taskbar_update_clock();
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

    char time_str[9];
    char num_buf[4];

    int_to_str(hours, num_buf);
    time_str[0] = (hours < 10) ? '0' : num_buf[0];
    time_str[1] = (hours < 10) ? num_buf[0] : num_buf[1];
    time_str[2] = ':';

    int_to_str(minutes, num_buf);
    time_str[3] = (minutes < 10) ? '0' : num_buf[0];
    time_str[4] = (minutes < 10) ? num_buf[0] : num_buf[1];
    time_str[5] = ':';

    int_to_str(seconds, num_buf);
    time_str[6] = (seconds < 10) ? '0' : num_buf[0];
    time_str[7] = (seconds < 10) ? num_buf[0] : num_buf[1];
    time_str[8] = '\0';

    video_print_at(70, TASKBAR_ROW, time_str, TASKBAR_COLOR_CLOCK);
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
    return menu_open;
}

static void taskbar_draw_menu(void) {
    if (!menu_open) {
        taskbar_draw();
        return;
    }

    int menu_x = 1;
    int menu_y = TASKBAR_ROW - MENU_ITEM_COUNT - 1;

    video_fill_rect(menu_x, menu_y, 16, MENU_ITEM_COUNT + 2, ' ', TASKBAR_COLOR_MENU_BG);
    video_draw_box(menu_x, menu_y, 16, MENU_ITEM_COUNT + 2, TASKBAR_COLOR_MENU_BORDER);

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        uint8_t color = (menu_selection == i) ? TASKBAR_COLOR_MENU_HIGHLIGHT : TASKBAR_COLOR_MENU_TEXT;

        if (menu_selection == i) {
            video_fill_rect(menu_x + 1, menu_y + 1 + i, 14, 1, ' ', TASKBAR_COLOR_MENU_HIGHLIGHT);
        }

        video_print_at(menu_x + 2, menu_y + 1 + i, menu_items[i], color);
    }
}

static void taskbar_close_menu(void) {
    menu_open = 0;
    menu_selection = 0;
    taskbar_draw();
}

int taskbar_handle_key(uint8_t scancode) {
    if (scancode & 0x80) return 0;

    if (menu_open) {
        if (scancode == 0x01) {
            taskbar_close_menu();
            return 1;
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
                case 0: return 2;
                case 1: return 3;
                case 2: return 4;
                case 3: return 5;
                case 4: return 6;
            }
            return 1;
        }

        return 1;
    }

    if (scancode == 0x38) {
        menu_open = 1;
        menu_selection = 0;
        taskbar_draw_menu();
        return 1;
    }

    return 0;
}
