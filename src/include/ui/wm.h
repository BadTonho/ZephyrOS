#ifndef WM_H
#define WM_H

#include "types.h"

#define WM_MAX_WINDOWS 16
#define WM_TITLE_HEIGHT 1
#define WM_BORDER_SIZE 1
#define WM_MIN_WIDTH 10
#define WM_MIN_HEIGHT 5

typedef enum {
    WM_STATE_NORMAL = 0,
    WM_STATE_MINIMIZED,
    WM_STATE_MAXIMIZED
} wm_window_state_t;

typedef enum {
    WM_APP_SHELL = 0,
    WM_APP_EXPLORER,
    WM_APP_TASKMGR,
    WM_APP_SETTINGS,
    WM_APP_CUSTOM
} wm_app_type_t;

typedef enum {
    WM_BTNS_RIGHT = 0,
    WM_BTNS_LEFT
} wm_btn_position_t;

typedef enum {
    WM_BTN_CLOSE_MIN_MAX = 0,
    WM_BTN_CLOSE_MAX_MIN,
    WM_BTN_MIN_MAX_CLOSE,
    WM_BTN_MAX_MIN_CLOSE,
    WM_BTN_MIN_CLOSE_MAX,
    WM_BTN_MAX_CLOSE_MIN
} wm_btn_order_t;

typedef void (*wm_key_handler_t)(uint8_t scancode);
typedef void (*wm_redraw_handler_t)(int x, int y, int w, int h);

typedef struct {
    int id;
    char title[32];
    int x, y;
    int width, height;
    int min_width, min_height;
    wm_window_state_t state;
    wm_app_type_t app_type;
    int visible;
    int focused;
    int z_order;

    wm_key_handler_t on_key;
    wm_redraw_handler_t on_redraw;

    uint32_t cpu_ticks;
    uint32_t last_cpu_sample;
} wm_window_t;

typedef struct {
    wm_btn_position_t btn_position;
    wm_btn_order_t btn_order;
    int show_title_text;
    int title_bar_height;
    int border_style;
} wm_config_t;

typedef struct {
    wm_window_t windows[WM_MAX_WINDOWS];
    wm_config_t config;
    int window_count;
    int focused_id;
    int z_counter;
    int drag_active;
    int drag_win_id;
    int drag_offset_x;
    int drag_offset_y;
    int resize_active;
    int resize_win_id;
} wm_manager_t;

void wm_init(void);
void wm_draw_all(void);
void wm_draw_window(int id);
void wm_draw_title_bar(wm_window_t* win);
void wm_draw_desktop(void);

int  wm_create_window(const char* title, int x, int y, int w, int h,
                       wm_app_type_t app_type, wm_key_handler_t on_key, wm_redraw_handler_t on_redraw);
void wm_destroy_window(int id);
void wm_close_focused(void);

void wm_focus_window(int id);
void wm_focus_next(void);
void wm_focus_prev(void);
int  wm_get_focused_id(void);
wm_window_t* wm_get_window(int id);
wm_window_t* wm_get_focused(void);

void wm_minimize_window(int id);
void wm_maximize_window(int id);
void wm_restore_window(int id);

void wm_move_window(int id, int x, int y);
void wm_resize_window(int id, int w, int h);

void wm_handle_key(uint8_t scancode);
int  wm_handle_click(int px, int py);
int  wm_is_active(void);
void wm_set_active(int active);

void wm_update_cpu_stats(void);

wm_config_t* wm_get_config(void);
void wm_set_btn_position(wm_btn_position_t pos);
void wm_set_btn_order(wm_btn_order_t order);
void wm_set_show_title(int show);
void wm_set_border_style(int style);

#endif
