#include "wm.h"
#include "video.h"
#include "keyboard.h"
#include "timer.h"

static wm_manager_t wm;
static int wm_active = 0;

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

static int str_len(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

void wm_init(void) {
    wm.window_count = 0;
    wm.focused_id = -1;
    wm.z_counter = 0;
    wm.drag_active = 0;
    wm.resize_active = 0;
    wm_active = 0;

    wm.config.btn_position = WM_BTNS_RIGHT;
    wm.config.btn_order = WM_BTN_CLOSE_MIN_MAX;
    wm.config.show_title_text = 1;
    wm.config.title_bar_height = 1;
    wm.config.border_style = 0;
}

wm_config_t* wm_get_config(void) {
    return &wm.config;
}

void wm_set_btn_position(wm_btn_position_t pos) {
    wm.config.btn_position = pos;
    wm_draw_all();
}

void wm_set_btn_order(wm_btn_order_t order) {
    wm.config.btn_order = order;
    wm_draw_all();
}

void wm_set_show_title(int show) {
    wm.config.show_title_text = show;
    wm_draw_all();
}

void wm_set_border_style(int style) {
    wm.config.border_style = style;
    wm_draw_all();
}

void wm_draw_desktop(void) {
    for (int y = 0; y < 24; y++) {
        for (int x = 0; x < 80; x++) {
            if ((x + y) % 2 == 0) {
                video_put_char_at(' ', 0x17, x, y);
            } else {
                video_put_char_at(' ', 0x1F, x, y);
            }
        }
    }
}

static void draw_buttons_right(wm_window_t* win) {
    uint8_t title_color = win->focused ? 0x1F : 0x07;
    char btn_chars[3];
    uint8_t btn_colors[3];

    switch (wm.config.btn_order) {
        case WM_BTN_CLOSE_MIN_MAX:
            btn_chars[0] = 'x'; btn_colors[0] = 0x4F;
            btn_chars[1] = '_'; btn_colors[1] = win->focused ? 0x1F : 0x08;
            btn_chars[2] = 0x10; btn_colors[2] = win->focused ? 0x1F : 0x08;
            break;
        case WM_BTN_CLOSE_MAX_MIN:
            btn_chars[0] = 'x'; btn_colors[0] = 0x4F;
            btn_chars[1] = 0x10; btn_colors[1] = win->focused ? 0x1F : 0x08;
            btn_chars[2] = '_'; btn_colors[2] = win->focused ? 0x1F : 0x08;
            break;
        case WM_BTN_MIN_MAX_CLOSE:
            btn_chars[0] = '_'; btn_colors[0] = win->focused ? 0x1F : 0x08;
            btn_chars[1] = 0x10; btn_colors[1] = win->focused ? 0x1F : 0x08;
            btn_chars[2] = 'x'; btn_colors[2] = 0x4F;
            break;
        case WM_BTN_MAX_MIN_CLOSE:
            btn_chars[0] = 0x10; btn_colors[0] = win->focused ? 0x1F : 0x08;
            btn_chars[1] = '_'; btn_colors[1] = win->focused ? 0x1F : 0x08;
            btn_chars[2] = 'x'; btn_colors[2] = 0x4F;
            break;
        case WM_BTN_MIN_CLOSE_MAX:
            btn_chars[0] = '_'; btn_colors[0] = win->focused ? 0x1F : 0x08;
            btn_chars[1] = 'x'; btn_colors[1] = 0x4F;
            btn_chars[2] = 0x10; btn_colors[2] = win->focused ? 0x1F : 0x08;
            break;
        case WM_BTN_MAX_CLOSE_MIN:
            btn_chars[0] = 0x10; btn_colors[0] = win->focused ? 0x1F : 0x08;
            btn_chars[1] = 'x'; btn_colors[1] = 0x4F;
            btn_chars[2] = '_'; btn_colors[2] = win->focused ? 0x1F : 0x08;
            break;
    }

    int btn_start = win->x + win->width - 4;
    video_put_char_at(btn_chars[0], btn_colors[0], btn_start, win->y);
    video_put_char_at(btn_chars[1], btn_colors[1], btn_start + 1, win->y);
    video_put_char_at(btn_chars[2], btn_colors[2], btn_start + 2, win->y);
}

static void draw_buttons_left(wm_window_t* win) {
    char btn_chars[3];
    uint8_t btn_colors[3];

    switch (wm.config.btn_order) {
        case WM_BTN_CLOSE_MIN_MAX:
            btn_chars[0] = 'x'; btn_colors[0] = 0x4F;
            btn_chars[1] = '_'; btn_colors[1] = win->focused ? 0x1F : 0x08;
            btn_chars[2] = 0x10; btn_colors[2] = win->focused ? 0x1F : 0x08;
            break;
        case WM_BTN_CLOSE_MAX_MIN:
            btn_chars[0] = 'x'; btn_colors[0] = 0x4F;
            btn_chars[1] = 0x10; btn_colors[1] = win->focused ? 0x1F : 0x08;
            btn_chars[2] = '_'; btn_colors[2] = win->focused ? 0x1F : 0x08;
            break;
        case WM_BTN_MIN_MAX_CLOSE:
            btn_chars[0] = '_'; btn_colors[0] = win->focused ? 0x1F : 0x08;
            btn_chars[1] = 0x10; btn_colors[1] = win->focused ? 0x1F : 0x08;
            btn_chars[2] = 'x'; btn_colors[2] = 0x4F;
            break;
        case WM_BTN_MAX_MIN_CLOSE:
            btn_chars[0] = 0x10; btn_colors[0] = win->focused ? 0x1F : 0x08;
            btn_chars[1] = '_'; btn_colors[1] = win->focused ? 0x1F : 0x08;
            btn_chars[2] = 'x'; btn_colors[2] = 0x4F;
            break;
        case WM_BTN_MIN_CLOSE_MAX:
            btn_chars[0] = '_'; btn_colors[0] = win->focused ? 0x1F : 0x08;
            btn_chars[1] = 'x'; btn_colors[1] = 0x4F;
            btn_chars[2] = 0x10; btn_colors[2] = win->focused ? 0x1F : 0x08;
            break;
        case WM_BTN_MAX_CLOSE_MIN:
            btn_chars[0] = 0x10; btn_colors[0] = win->focused ? 0x1F : 0x08;
            btn_chars[1] = 'x'; btn_colors[1] = 0x4F;
            btn_chars[2] = '_'; btn_colors[2] = win->focused ? 0x1F : 0x08;
            break;
    }

    video_put_char_at(btn_chars[0], btn_colors[0], win->x + 1, win->y);
    video_put_char_at(btn_chars[1], btn_colors[1], win->x + 2, win->y);
    video_put_char_at(btn_chars[2], btn_colors[2], win->x + 3, win->y);
}

void wm_draw_title_bar(wm_window_t* win) {
    uint8_t title_color = win->focused ? 0x1F : 0x07;

    video_fill_rect(win->x, win->y, win->width, 1, ' ', title_color);

    if (wm.config.btn_position == WM_BTNS_LEFT) {
        draw_buttons_left(win);

        if (wm.config.show_title_text) {
            int title_x = win->x + 5;
            int title_len = str_len(win->title);
            int max_title = win->width - 6;
            if (title_len > max_title) title_len = max_title;

            for (int i = 0; i < title_len; i++) {
                video_put_char_at(win->title[i], title_color, title_x + i, win->y);
            }
        }
    } else {
        if (wm.config.show_title_text) {
            int title_x = win->x + 1;
            int title_len = str_len(win->title);
            int max_title = win->width - 5;
            if (title_len > max_title) title_len = max_title;

            for (int i = 0; i < title_len; i++) {
                video_put_char_at(win->title[i], title_color, title_x + i, win->y);
            }
        }

        draw_buttons_right(win);
    }
}

void wm_draw_window(int id) {
    wm_window_t* win = &wm.windows[id];
    if (!win->visible) return;

    if (win->state == WM_STATE_MAXIMIZED) {
        win->x = 0;
        win->y = 0;
        win->width = 80;
        win->height = 24;
    }

    uint8_t border_color = win->focused ? 0x08 : 0x07;

    if (wm.config.border_style == 0) {
        video_draw_box(win->x, win->y, win->width, win->height, border_color);
    } else {
        for (int i = 0; i < win->width; i++) {
            video_put_char_at(0xC4, border_color, win->x + i, win->y);
            video_put_char_at(0xC4, border_color, win->x + i, win->y + win->height - 1);
        }
        for (int i = 0; i < win->height; i++) {
            video_put_char_at(0xB3, border_color, win->x, win->y + i);
            video_put_char_at(0xB3, border_color, win->x + win->width - 1, win->y + i);
        }
        video_put_char_at(0xDA, border_color, win->x, win->y);
        video_put_char_at(0xBF, border_color, win->x + win->width - 1, win->y);
        video_put_char_at(0xC0, border_color, win->x, win->y + win->height - 1);
        video_put_char_at(0xD9, border_color, win->x + win->width - 1, win->y + win->height - 1);
    }

    wm_draw_title_bar(win);

    for (int y = win->y + 1; y < win->y + win->height - 1; y++) {
        for (int x = win->x + 1; x < win->x + win->width - 1; x++) {
            if (x < 80 && y < 24) {
                video_put_char_at(' ', 0x07, x, y);
            }
        }
    }

    if (win->on_redraw) {
        win->on_redraw(win->x + 1, win->y + 1, win->width - 2, win->height - 2);
    }
}

void wm_draw_all(void) {
    wm_draw_desktop();

    for (int z = wm.z_counter - 1; z >= 0; z--) {
        for (int i = 0; i < wm.window_count; i++) {
            if (wm.windows[i].visible && wm.windows[i].z_order == z) {
                wm_draw_window(i);
            }
        }
    }
}

int wm_create_window(const char* title, int x, int y, int w, int h,
                      wm_app_type_t app_type, wm_key_handler_t on_key, wm_redraw_handler_t on_redraw) {
    if (wm.window_count >= WM_MAX_WINDOWS) return -1;

    int id = wm.window_count;
    wm_window_t* win = &wm.windows[id];

    win->id = id;
    int i = 0;
    while (title[i] && i < 31) {
        win->title[i] = title[i];
        i++;
    }
    win->title[i] = '\0';

    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h;
    win->min_width = WM_MIN_WIDTH;
    win->min_height = WM_MIN_HEIGHT;
    win->state = WM_STATE_NORMAL;
    win->app_type = app_type;
    win->visible = 1;
    win->focused = 0;
    win->z_order = wm.z_counter++;
    win->on_key = on_key;
    win->on_redraw = on_redraw;
    win->cpu_ticks = 0;
    win->last_cpu_sample = 0;

    wm.window_count++;

    wm_focus_window(id);
    wm_draw_all();

    return id;
}

void wm_destroy_window(int id) {
    if (id < 0 || id >= wm.window_count) return;

    wm.windows[id].visible = 0;
    wm.windows[id].focused = 0;

    if (wm.focused_id == id) {
        wm.focused_id = -1;
        wm_focus_prev();
    }

    wm_draw_all();
}

void wm_close_focused(void) {
    if (wm.focused_id >= 0) {
        wm_destroy_window(wm.focused_id);
    }
}

void wm_focus_window(int id) {
    if (id < 0 || id >= wm.window_count) return;
    if (!wm.windows[id].visible) return;

    for (int i = 0; i < wm.window_count; i++) {
        wm.windows[i].focused = 0;
    }

    wm.windows[id].focused = 1;
    wm.windows[id].z_order = wm.z_counter++;
    wm.focused_id = id;

    wm_draw_all();
}

void wm_focus_next(void) {
    if (wm.window_count == 0) return;

    int start = wm.focused_id;
    int next = (start + 1) % wm.window_count;

    while (next != start) {
        if (wm.windows[next].visible) {
            wm_focus_window(next);
            return;
        }
        next = (next + 1) % wm.window_count;
    }

    if (wm.windows[start].visible) {
        wm_focus_window(start);
    }
}

void wm_focus_prev(void) {
    if (wm.window_count == 0) return;

    int start = wm.focused_id;
    int prev = (start - 1 + wm.window_count) % wm.window_count;

    while (prev != start) {
        if (wm.windows[prev].visible) {
            wm_focus_window(prev);
            return;
        }
        prev = (prev - 1 + wm.window_count) % wm.window_count;
    }
}

int wm_get_focused_id(void) {
    return wm.focused_id;
}

wm_window_t* wm_get_window(int id) {
    if (id < 0 || id >= wm.window_count) return 0;
    return &wm.windows[id];
}

wm_window_t* wm_get_focused(void) {
    if (wm.focused_id < 0) return 0;
    return &wm.windows[wm.focused_id];
}

void wm_minimize_window(int id) {
    if (id < 0 || id >= wm.window_count) return;
    wm.windows[id].state = WM_STATE_MINIMIZED;
    wm.windows[id].visible = 0;
    wm.windows[id].focused = 0;

    if (wm.focused_id == id) {
        wm.focused_id = -1;
        wm_focus_prev();
    }

    wm_draw_all();
}

void wm_maximize_window(int id) {
    if (id < 0 || id >= wm.window_count) return;
    wm.windows[id].state = WM_STATE_MAXIMIZED;
    wm_draw_all();
}

void wm_restore_window(int id) {
    if (id < 0 || id >= wm.window_count) return;
    wm.windows[id].state = WM_STATE_NORMAL;
    wm.windows[id].visible = 1;
    wm_focus_window(id);
    wm_draw_all();
}

void wm_move_window(int id, int x, int y) {
    if (id < 0 || id >= wm.window_count) return;
    wm.windows[id].x = x;
    wm.windows[id].y = y;
    wm_draw_all();
}

void wm_resize_window(int id, int w, int h) {
    if (id < 0 || id >= wm.window_count) return;
    if (w < wm.windows[id].min_width) w = wm.windows[id].min_width;
    if (h < wm.windows[id].min_height) h = wm.windows[id].min_height;
    wm.windows[id].width = w;
    wm.windows[id].height = h;
    wm_draw_all();
}

void wm_handle_key(uint8_t scancode) {
    if (!wm_active) return;

    if (scancode & 0x80) return;

    if (scancode == 0x0F) {
        wm_focus_next();
        return;
    }

    if (scancode == 0x01) {
        wm_close_focused();
        return;
    }

    if (scancode == 0x3B) {
        if (wm.focused_id >= 0) {
            wm_minimize_window(wm.focused_id);
        }
        return;
    }

    if (scancode == 0x3C) {
        if (wm.focused_id >= 0) {
            if (wm.windows[wm.focused_id].state == WM_STATE_MAXIMIZED) {
                wm_restore_window(wm.focused_id);
            } else {
                wm_maximize_window(wm.focused_id);
            }
        }
        return;
    }

    if (wm.focused_id >= 0 && wm.windows[wm.focused_id].on_key) {
        wm.windows[wm.focused_id].on_key(scancode);
    }
}

int wm_is_active(void) {
    return wm_active;
}

void wm_set_active(int active) {
    wm_active = active;
    if (active) {
        wm_draw_all();
    }
}

void wm_update_cpu_stats(void) {
    uint32_t current_ticks = timer_get_ticks();

    for (int i = 0; i < wm.window_count; i++) {
        if (wm.windows[i].visible && wm.windows[i].focused) {
            uint32_t delta = current_ticks - wm.windows[i].last_cpu_sample;
            wm.windows[i].cpu_ticks += delta;
            wm.windows[i].last_cpu_sample = current_ticks;
        }
    }
}
