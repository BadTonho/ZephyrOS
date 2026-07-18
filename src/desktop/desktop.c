#include "desktop.h"
#include "video.h"
#include "keyboard.h"
#include "speaker.h"
#include "taskbar.h"
#include "icons.h"

static desktop_icon_t desktop_icons[DESKTOP_MAX_ICONS];
static int icon_count = 0;
static int selected_icon = 0;
static int desktop_active = 0;

static void draw_single_icon(desktop_icon_t* icon) {
    icon_entry_t* entry = icons_get_desktop((icon_desktop_id_t)icon->type);
    uint8_t bg = icon->selected ? entry->color_selected : DESKTOP_BG_COLOR;
    uint8_t fg = icon->selected ? 0x17 : entry->color;

    video_fill_rect(icon->x, icon->y, DESKTOP_ICON_WIDTH, 5, ' ', bg);

    video_put_char_at(0xC9, fg, icon->x, icon->y);
    video_put_char_at(0xBB, fg, icon->x + DESKTOP_ICON_WIDTH - 1, icon->y);
    video_put_char_at(0xC8, fg, icon->x, icon->y + 3);
    video_put_char_at(0xBC, fg, icon->x + DESKTOP_ICON_WIDTH - 1, icon->y + 3);

    for (int i = 1; i < DESKTOP_ICON_WIDTH - 1; i++) {
        video_put_char_at(0xCD, fg, icon->x + i, icon->y);
        video_put_char_at(0xCD, fg, icon->x + i, icon->y + 3);
    }
    for (int i = 1; i < 3; i++) {
        video_put_char_at(0xBA, fg, icon->x, icon->y + i);
        video_put_char_at(0xBA, fg, icon->x + DESKTOP_ICON_WIDTH - 1, icon->y + i);
    }

    video_put_char_at(entry->ch, fg, icon->x + 4, icon->y + 1);
    video_put_char_at(entry->ch, fg, icon->x + 4, icon->y + 2);

    int name_len = 0;
    while (icon->name[name_len]) name_len++;

    int text_x = icon->x + (DESKTOP_ICON_WIDTH - name_len) / 2;
    video_print_at(text_x, icon->y + 4, icon->name, fg);
}

void desktop_init(void) {
    icon_count = 0;
    selected_icon = 0;
    desktop_active = 0;

    desktop_add_icon("Shell", DESKTOP_APP_SHELL);
    desktop_add_icon("Explorer", DESKTOP_APP_EXPLORER);
    desktop_add_icon("TaskMgr", DESKTOP_APP_TASKMGR);
}

void desktop_draw(void) {
    video_fill_rect(0, 0, 80, 24, ' ', DESKTOP_BG_COLOR);

    video_fill_rect(0, 0, 80, 1, ' ', 0x1F);
    video_print_at(30, 0, " MiniOS Desktop ", 0x1F);

    desktop_draw_icons();
}

void desktop_draw_icons(void) {
    for (int i = 0; i < icon_count; i++) {
        draw_single_icon(&desktop_icons[i]);
    }
}

void desktop_add_icon(const char* name, desktop_app_type_t type) {
    if (icon_count >= DESKTOP_MAX_ICONS) return;

    int col = icon_count % 5;
    int row = icon_count / 5;

    desktop_icons[icon_count].name = name;
    desktop_icons[icon_count].type = type;
    desktop_icons[icon_count].x = DESKTOP_START_X + col * DESKTOP_ICON_SPACING_X;
    desktop_icons[icon_count].y = DESKTOP_START_Y + row * DESKTOP_ICON_SPACING_Y;
    desktop_icons[icon_count].selected = (icon_count == selected_icon) ? 1 : 0;

    icon_count++;
}

void desktop_update_selection(void) {
    for (int i = 0; i < icon_count; i++) {
        desktop_icons[i].selected = (i == selected_icon) ? 1 : 0;
    }
    desktop_draw_icons();
}

int desktop_handle_key(uint8_t scancode) {
    if (!desktop_active) return 0;

    if (taskbar_handle_config_key(scancode)) {
        return 1;
    }

    if (scancode & 0x80) return 0;

    if (scancode == 0x01) {
        return -1;
    }

    if (scancode == 0x48) {
        if (selected_icon >= 5) {
            selected_icon -= 5;
        }
        desktop_update_selection();
        return 1;
    }

    if (scancode == 0x50) {
        if (selected_icon + 5 < icon_count) {
            selected_icon += 5;
        }
        desktop_update_selection();
        return 1;
    }

    if (scancode == 0x4B) {
        if (selected_icon > 0) {
            selected_icon--;
        }
        desktop_update_selection();
        return 1;
    }

    if (scancode == 0x4D) {
        if (selected_icon < icon_count - 1) {
            selected_icon++;
        }
        desktop_update_selection();
        return 1;
    }

    if (scancode == 0x1C) {
        return desktop_get_selected_app();
    }

    return 0;
}

int desktop_get_selected_app(void) {
    if (selected_icon < 0 || selected_icon >= icon_count) {
        return 0;
    }
    return desktop_icons[selected_icon].type;
}

void desktop_set_active(int active) {
    desktop_active = active;
}

int desktop_is_active(void) {
    return desktop_active;
}
