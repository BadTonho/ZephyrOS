#include "ui/wm.h"
#include "core/video.h"
#include "core/keyboard.h"
#include "core/timer.h"
#include "ui/icons.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "drivers/font.h"
#include "drivers/mouse.h"
#include "drivers/vesa.h"
#include "ui/desktop.h"
#include "ui/gui.h"
#include "ui/taskbar.h"
#include "ui/display.h"

static wm_manager_t wm;
static int wm_active = 0;

#define WM_GUI_WINDOW_COUNT WM_MAX_WINDOWS
#define WM_GUI_ID_BASE 100
#define WM_GUI_BASE_MARGIN 24
#define WM_GUI_MIN_WIDTH WM_HOSTED_MIN_WIDTH
#define WM_GUI_MIN_HEIGHT WM_HOSTED_MIN_HEIGHT
#define WM_GUI_BASE_TITLE_HEIGHT 24
#define WM_GUI_BASE_CONTROL_SIZE 24
#define WM_GUI_BASE_CONTROL_GAP 2
#define WM_GUI_BASE_FRAME_INSET 2
#define WM_GUI_BASE_RESIZE_ZONE 8
#define WM_GUI_BASE_CONTROL_MARGIN 6
#define WM_GUI_BASE_TITLE_MARGIN 10
#define WM_GUI_MARGIN ((int)display_scale_px(WM_GUI_BASE_MARGIN))
#define WM_GUI_TITLE_HEIGHT \
    ((int)display_scale_px(WM_GUI_BASE_TITLE_HEIGHT))
#define WM_GUI_CONTROL_SIZE \
    ((int)display_scale_px(WM_GUI_BASE_CONTROL_SIZE))
#define WM_GUI_CONTROL_GAP \
    ((int)display_scale_px(WM_GUI_BASE_CONTROL_GAP))
#define WM_GUI_FRAME_INSET \
    ((int)display_scale_px(WM_GUI_BASE_FRAME_INSET))
#define WM_GUI_CONTENT_TOP (WM_GUI_FRAME_INSET + WM_GUI_TITLE_HEIGHT)
#define WM_GUI_CONTENT_BOTTOM WM_GUI_FRAME_INSET
#define WM_GUI_RESIZE_ZONE \
    ((int)display_scale_px(WM_GUI_BASE_RESIZE_ZONE))
#define WM_GUI_FOCUS_RING_WIDTH 2
#define WM_GUI_RESIZE_LEFT 0x01
#define WM_GUI_RESIZE_RIGHT 0x02
#define WM_GUI_RESIZE_TOP 0x04
#define WM_GUI_RESIZE_BOTTOM 0x08
#define WM_GUI_SHIFT_LEFT_MASK 0x01
#define WM_GUI_SHIFT_RIGHT_MASK 0x02
#define WM_SCANCODE_EXTENDED 0xE0
#define WM_SCANCODE_ALT 0x38
#define WM_SCANCODE_ALT_RELEASE 0xB8
#define WM_SCANCODE_SHIFT_LEFT 0x2A
#define WM_SCANCODE_SHIFT_RIGHT 0x36
#define WM_SCANCODE_SHIFT_LEFT_RELEASE 0xAA
#define WM_SCANCODE_SHIFT_RIGHT_RELEASE 0xB6
#define WM_SCANCODE_TAB 0x0F
#define WM_SCANCODE_F4 0x3E
#define WM_SCANCODE_F9 0x43
#define WM_SCANCODE_F10 0x44

typedef enum {
    WM_GUI_CONTROL_CLOSE = 0,
    WM_GUI_CONTROL_MINIMIZE,
    WM_GUI_CONTROL_MAXIMIZE
} wm_gui_control_t;

typedef struct {
    int id;
    const wm_hosted_app_t* app;
    int x;
    int y;
    int width;
    int height;
    int restore_x;
    int restore_y;
    int restore_width;
    int restore_height;
    wm_window_state_t state;
    int visible;
    int focused;
    int z_order;
} wm_gui_window_t;

static wm_gui_window_t wm_gui_windows[WM_GUI_WINDOW_COUNT];
static int wm_gui_window_count = 0;
static int wm_gui_focused = -1;
static int wm_gui_z_counter = 0;
static int wm_gui_drag_active = 0;
static int wm_gui_resize_active = 0;
static int wm_gui_capture_index = -1;
static int wm_gui_capture_x = 0;
static int wm_gui_capture_y = 0;
static int wm_gui_capture_width = 0;
static int wm_gui_capture_height = 0;
static int wm_gui_drag_offset_x = 0;
static int wm_gui_drag_offset_y = 0;
static int wm_gui_resize_edges = 0;
static int wm_gui_dispatching_input = 0;
static int wm_gui_reflow_pending = 0;
static uint8_t wm_gui_alt_down = 0;
static uint8_t wm_gui_shift_mask = 0;
static uint8_t wm_gui_extended_scancode = 0;

static int wm_gui_enabled(void);
static int wm_gui_get_work_area(tb_rect_t* work_area);
static void wm_gui_reset(void);
static void wm_gui_draw_all(void);
static void wm_gui_sync_taskbar(void);
static void wm_gui_focus(int index);
static void wm_gui_focus_next(void);
static void wm_gui_focus_prev(void);
static void wm_gui_minimize(int index);
static void wm_gui_maximize_or_restore(int index);
static void wm_gui_close(int index, int notify_app);
static void wm_gui_clear_interaction(void);
static int wm_gui_handle_mouse(mouse_event_t* event);
static int wm_gui_handle_wheel(mouse_event_t* event);
static int wm_gui_handle_key(uint8_t scancode);
static int wm_gui_find_app(wm_app_type_t app_type);
static int wm_gui_has_live_windows(void);
static int wm_gui_return_to_desktop_if_empty(void);
static int wm_gui_window_min_width(const wm_gui_window_t* window);
static int wm_gui_window_min_height(const wm_gui_window_t* window);
static void wm_gui_constrain_window(wm_gui_window_t* window);

static int wm_gui_enabled(void) {
    vesa_mode_t* mode = vesa_get_mode();

    return desktop_get_mode() == DESKTOP_MODE_MODERN && mode &&
           mode->initialized && vesa_has_backbuffer();
}

static int wm_gui_get_work_area(tb_rect_t* work_area) {
    vesa_mode_t* mode = vesa_get_mode();

    if (!work_area || !mode || !mode->initialized) return 0;
    work_area->x = 0;
    work_area->y = 0;
    work_area->width = mode->width;
    work_area->height = mode->height;
    taskbar_get_work_area(work_area);
    return work_area->width > 0 && work_area->height > 0;
}

static int wm_gui_find_app(wm_app_type_t app_type) {
    for (int i = 0; i < wm_gui_window_count; i++) {
        if (wm_gui_windows[i].app &&
            wm_gui_windows[i].app->app_type == app_type) return i;
    }
    return -1;
}

static int wm_gui_has_live_windows(void) {
    for (int i = 0; i < wm_gui_window_count; i++) {
        if (wm_gui_windows[i].visible ||
            wm_gui_windows[i].state == WM_STATE_MINIMIZED) {
            return 1;
        }
    }
    return 0;
}

static int wm_gui_return_to_desktop_if_empty(void) {
    if (!wm_active || !desktop_is_active() || wm_gui_has_live_windows()) {
        return 0;
    }

    wm_active = 0;
    wm_gui_reset();
    desktop_draw();
    return 1;
}

static int wm_gui_window_min_width(const wm_gui_window_t* window) {
    if (window->app && window->app->min_width > WM_GUI_MIN_WIDTH) {
        return window->app->min_width;
    }
    return WM_GUI_MIN_WIDTH;
}

static int wm_gui_window_min_height(const wm_gui_window_t* window) {
    if (window->app && window->app->min_height > WM_GUI_MIN_HEIGHT) {
        return window->app->min_height;
    }
    return WM_GUI_MIN_HEIGHT;
}

static void wm_gui_constrain_window(wm_gui_window_t* window) {
    tb_rect_t work_area;
    int max_x;
    int max_y;

    if (!window || !wm_gui_get_work_area(&work_area)) return;
    if (window->state == WM_STATE_MAXIMIZED) {
        window->x = work_area.x;
        window->y = work_area.y;
        window->width = work_area.width;
        window->height = work_area.height;
        return;
    }
    if (window->width > work_area.width) window->width = work_area.width;
    if (window->height > work_area.height) window->height = work_area.height;
    max_x = work_area.x + work_area.width - window->width;
    max_y = work_area.y + work_area.height - window->height;
    if (window->x < work_area.x) window->x = work_area.x;
    if (window->x > max_x) window->x = max_x;
    if (window->y < work_area.y) window->y = work_area.y;
    if (window->y > max_y) window->y = max_y;
}

static void wm_gui_sync_taskbar(void) {
    for (int i = 0; i < wm_gui_window_count; i++) {
        taskbar_set_window_active(wm_gui_windows[i].id,
                                  wm_gui_windows[i].visible &&
                                  wm_gui_windows[i].focused);
    }
}

static void wm_gui_focus(int index) {
    if (index < 0 || index >= wm_gui_window_count ||
        !wm_gui_windows[index].visible) return;

    for (int i = 0; i < wm_gui_window_count; i++) {
        wm_gui_windows[i].focused = 0;
    }
    wm_gui_windows[index].focused = 1;
    wm_gui_windows[index].z_order = wm_gui_z_counter++;
    wm_gui_focused = index;
    wm_gui_sync_taskbar();
}

static void wm_gui_focus_next(void) {
    int start = wm_gui_focused;

    if (wm_gui_window_count == 0) return;
    for (int offset = 1; offset <= wm_gui_window_count; offset++) {
        int index = (start + offset + wm_gui_window_count) % wm_gui_window_count;
        if (wm_gui_windows[index].visible) {
            wm_gui_focus(index);
            return;
        }
    }
}

static void wm_gui_focus_prev(void) {
    int start = wm_gui_focused;

    if (wm_gui_window_count == 0) return;
    if (start < 0 || start >= wm_gui_window_count) start = 0;
    for (int offset = 1; offset <= wm_gui_window_count; offset++) {
        int index = (start - offset + wm_gui_window_count) %
                    wm_gui_window_count;

        if (wm_gui_windows[index].visible) {
            wm_gui_focus(index);
            return;
        }
    }
}

static void wm_gui_reset(void) {
    wm_gui_clear_interaction();
    wm_gui_dispatching_input = 0;
    wm_gui_reflow_pending = 0;
    for (int i = 0; i < WM_GUI_WINDOW_COUNT; i++) {
        taskbar_remove_window(wm_gui_windows[i].id);
        wm_gui_windows[i].app = 0;
        wm_gui_windows[i].visible = 0;
        wm_gui_windows[i].focused = 0;
    }
    wm_gui_window_count = 0;
    wm_gui_focused = -1;
    wm_gui_z_counter = 0;
    wm_gui_alt_down = 0;
    wm_gui_shift_mask = 0;
    wm_gui_extended_scancode = 0;
}

static vesa_color_t wm_gui_color(uint32_t raw) {
    vesa_color_t color;
    color.raw = raw;
    return color;
}

static wm_gui_control_t wm_gui_control_at(int slot) {
    static const wm_gui_control_t orders[6][3] = {
        {WM_GUI_CONTROL_CLOSE, WM_GUI_CONTROL_MINIMIZE, WM_GUI_CONTROL_MAXIMIZE},
        {WM_GUI_CONTROL_CLOSE, WM_GUI_CONTROL_MAXIMIZE, WM_GUI_CONTROL_MINIMIZE},
        {WM_GUI_CONTROL_MINIMIZE, WM_GUI_CONTROL_MAXIMIZE, WM_GUI_CONTROL_CLOSE},
        {WM_GUI_CONTROL_MAXIMIZE, WM_GUI_CONTROL_MINIMIZE, WM_GUI_CONTROL_CLOSE},
        {WM_GUI_CONTROL_MINIMIZE, WM_GUI_CONTROL_CLOSE, WM_GUI_CONTROL_MAXIMIZE},
        {WM_GUI_CONTROL_MAXIMIZE, WM_GUI_CONTROL_CLOSE, WM_GUI_CONTROL_MINIMIZE}
    };

    return orders[wm.config.btn_order][slot];
}

static void wm_gui_control_rect(const wm_gui_window_t* window, int slot,
                                 tb_rect_t* rect) {
    int offset = slot * (WM_GUI_CONTROL_SIZE + WM_GUI_CONTROL_GAP);
    int margin = (int)display_scale_px(WM_GUI_BASE_CONTROL_MARGIN);

    rect->y = window->y + WM_GUI_FRAME_INSET +
              (WM_GUI_TITLE_HEIGHT - WM_GUI_CONTROL_SIZE) / 2;
    rect->width = WM_GUI_CONTROL_SIZE;
    rect->height = WM_GUI_CONTROL_SIZE;
    if (wm.config.btn_position == WM_BTNS_LEFT) {
        rect->x = window->x + margin + offset;
    } else {
        rect->x = window->x + window->width - margin -
                  WM_GUI_CONTROL_SIZE - offset;
    }
}

static int wm_gui_point_in_window(const wm_gui_window_t* window, int x, int y) {
    return x >= window->x && x < window->x + window->width &&
           y >= window->y && y < window->y + window->height;
}

static int wm_gui_resize_edges_at(const wm_gui_window_t* window, int x, int y) {
    int edges = 0;

    if (x < window->x + WM_GUI_RESIZE_ZONE) edges |= WM_GUI_RESIZE_LEFT;
    if (x >= window->x + window->width - WM_GUI_RESIZE_ZONE) {
        edges |= WM_GUI_RESIZE_RIGHT;
    }
    if (y < window->y + WM_GUI_RESIZE_ZONE) edges |= WM_GUI_RESIZE_TOP;
    if (y >= window->y + window->height - WM_GUI_RESIZE_ZONE) {
        edges |= WM_GUI_RESIZE_BOTTOM;
    }
    return edges;
}

static int wm_gui_point_in_title_bar(const wm_gui_window_t* window, int x, int y) {
    return x >= window->x && x < window->x + window->width &&
           y >= window->y + WM_GUI_FRAME_INSET &&
           y < window->y + WM_GUI_FRAME_INSET + WM_GUI_TITLE_HEIGHT;
}

static int wm_gui_point_in_content(const wm_gui_window_t* window, int x, int y) {
    return x >= window->x + WM_GUI_FRAME_INSET &&
           x < window->x + window->width - WM_GUI_FRAME_INSET &&
           y >= window->y + WM_GUI_CONTENT_TOP &&
           y < window->y + window->height - WM_GUI_CONTENT_BOTTOM;
}

static void wm_gui_draw_window(const wm_gui_window_t* window) {
    vesa_color_t title_color = wm_gui_color(window->focused ?
                                               GUI_COLOR_TITLE_BG : 0x00606060);
    vesa_color_t focus_color = wm_gui_color(GUI_COLOR_TITLE_BG);
    display_metrics_t metrics;
    int title_x;
    int title_y;

    if (display_get_metrics(&metrics) != OK) return;

    gui_draw_panel((uint32_t)window->x, (uint32_t)window->y,
                   (uint32_t)window->width, (uint32_t)window->height,
                   GUI_COLOR_BG, wm.config.border_style != 0);
    if (window->focused) {
        vesa_draw_rect((uint32_t)window->x, (uint32_t)window->y,
                       (uint32_t)window->width, (uint32_t)window->height,
                       focus_color);
        vesa_draw_rect((uint32_t)(window->x + WM_GUI_FOCUS_RING_WIDTH - 1),
                       (uint32_t)(window->y + WM_GUI_FOCUS_RING_WIDTH - 1),
                       (uint32_t)(window->width - WM_GUI_FOCUS_RING_WIDTH),
                       (uint32_t)(window->height - WM_GUI_FOCUS_RING_WIDTH),
                       focus_color);
    }
    vesa_fill_rect((uint32_t)(window->x + WM_GUI_FRAME_INSET),
                   (uint32_t)(window->y + WM_GUI_FRAME_INSET),
                   (uint32_t)(window->width - WM_GUI_FRAME_INSET * 2),
                   WM_GUI_TITLE_HEIGHT,
                   title_color);
    if (wm.config.btn_position == WM_BTNS_LEFT) {
        title_x = window->x +
                  (int)display_scale_px(WM_GUI_BASE_CONTROL_MARGIN) +
                  3 * (WM_GUI_CONTROL_SIZE + WM_GUI_CONTROL_GAP) +
                  metrics.spacing;
    } else {
        title_x = window->x +
                  (int)display_scale_px(WM_GUI_BASE_TITLE_MARGIN);
    }
    title_y = window->y + WM_GUI_FRAME_INSET +
              (WM_GUI_TITLE_HEIGHT - metrics.font_height) / 2;
    if (wm.config.show_title_text) {
        gui_draw_scaled_text((uint32_t)title_x, (uint32_t)title_y,
                             window->app->title, GUI_COLOR_TEXT_W);
    }
    for (int i = 0; i < 3; i++) {
        tb_rect_t rect;
        wm_gui_control_t control = wm_gui_control_at(i);
        const char* label = control == WM_GUI_CONTROL_CLOSE ? "X" :
                            control == WM_GUI_CONTROL_MINIMIZE ? "_" :
                            window->state == WM_STATE_MAXIMIZED ? "R" : "M";
        wm_gui_control_rect(window, i, &rect);
        gui_draw_scaled_button((uint32_t)rect.x, (uint32_t)rect.y,
                               (uint32_t)rect.width, (uint32_t)rect.height,
                               label, 0);
    }
    if (window->app->on_draw) {
        vesa_set_clip_rect(window->x + WM_GUI_FRAME_INSET,
                           window->y + WM_GUI_CONTENT_TOP,
                           window->width - WM_GUI_FRAME_INSET * 2,
                           window->height - WM_GUI_CONTENT_TOP -
                           WM_GUI_CONTENT_BOTTOM);
        window->app->on_draw(window->x + WM_GUI_FRAME_INSET,
                             window->y + WM_GUI_CONTENT_TOP,
                             window->width - WM_GUI_FRAME_INSET * 2,
                             window->height - WM_GUI_CONTENT_TOP -
                             WM_GUI_CONTENT_BOTTOM);
        vesa_reset_clip_rect();
    }
}

static void wm_gui_draw_all(void) {
    vesa_color_t background = wm_gui_color(vesa_rgb(0, 64, 96));

    wm_gui_reflow_pending = 0;
    vesa_frame_begin();
    mouse_invalidate_cursor();
    if (desktop_is_active()) desktop_draw_workspace();
    else vesa_clear(background);
    for (int z = 0; z < wm_gui_z_counter; z++) {
        for (int i = 0; i < wm_gui_window_count; i++) {
            if (wm_gui_windows[i].visible && wm_gui_windows[i].z_order == z) {
                wm_gui_constrain_window(&wm_gui_windows[i]);
                wm_gui_draw_window(&wm_gui_windows[i]);
            }
        }
    }
    taskbar_draw();
    vesa_frame_end();
}

static void wm_gui_minimize(int index) {
    if (index < 0 || index >= wm_gui_window_count || !wm_gui_windows[index].visible) return;
    wm_gui_clear_interaction();
    wm_gui_windows[index].state = WM_STATE_MINIMIZED;
    wm_gui_windows[index].visible = 0;
    wm_gui_windows[index].focused = 0;
    if (wm_gui_focused == index) wm_gui_focused = -1;
    wm_gui_focus_next();
    wm_gui_sync_taskbar();
}

static void wm_gui_maximize_or_restore(int index) {
    tb_rect_t work_area;
    wm_gui_window_t* window;

    wm_gui_clear_interaction();
    if (index < 0 || index >= wm_gui_window_count ||
        !wm_gui_get_work_area(&work_area)) return;
    window = &wm_gui_windows[index];
    if (window->state == WM_STATE_MAXIMIZED) {
        window->x = window->restore_x;
        window->y = window->restore_y;
        window->width = window->restore_width;
        window->height = window->restore_height;
        window->state = WM_STATE_NORMAL;
    } else {
        window->restore_x = window->x;
        window->restore_y = window->y;
        window->restore_width = window->width;
        window->restore_height = window->height;
        window->x = work_area.x;
        window->y = work_area.y;
        window->width = work_area.width;
        window->height = work_area.height;
        window->state = WM_STATE_MAXIMIZED;
    }
    wm_gui_focus(index);
}

static void wm_gui_close(int index, int notify_app) {
    wm_close_handler_t on_close;

    if (index < 0 || index >= wm_gui_window_count) return;
    wm_gui_clear_interaction();
    on_close = wm_gui_windows[index].app ? wm_gui_windows[index].app->on_close : 0;
    wm_gui_windows[index].visible = 0;
    wm_gui_windows[index].focused = 0;
    taskbar_remove_window(wm_gui_windows[index].id);
    if (wm_gui_focused == index) wm_gui_focused = -1;
    wm_gui_focus_next();
    wm_gui_sync_taskbar();
    if (notify_app && on_close) on_close();
}

static void wm_gui_clear_interaction(void) {
    wm_gui_drag_active = 0;
    wm_gui_resize_active = 0;
    wm_gui_capture_index = -1;
    wm_gui_resize_edges = 0;
}

static void wm_gui_begin_drag(int index, const mouse_event_t* event) {
    wm_gui_window_t* window = &wm_gui_windows[index];

    wm_gui_clear_interaction();
    wm_gui_drag_active = 1;
    wm_gui_capture_index = index;
    wm_gui_capture_x = window->x;
    wm_gui_capture_y = window->y;
    wm_gui_capture_width = window->width;
    wm_gui_capture_height = window->height;
    wm_gui_drag_offset_x = event->x - window->x;
    wm_gui_drag_offset_y = event->y - window->y;
}

static void wm_gui_begin_resize(int index, int edges) {
    wm_gui_window_t* window = &wm_gui_windows[index];

    wm_gui_clear_interaction();
    wm_gui_resize_active = 1;
    wm_gui_capture_index = index;
    wm_gui_capture_x = window->x;
    wm_gui_capture_y = window->y;
    wm_gui_capture_width = window->width;
    wm_gui_capture_height = window->height;
    wm_gui_resize_edges = edges;
}

static void wm_gui_update_drag(wm_gui_window_t* window,
                               const mouse_event_t* event) {
    tb_rect_t work_area;
    int max_x;
    int max_y;
    int x;
    int y;

    if (!wm_gui_get_work_area(&work_area)) return;
    x = event->x - wm_gui_drag_offset_x;
    y = event->y - wm_gui_drag_offset_y;
    max_x = work_area.x + work_area.width - window->width;
    max_y = work_area.y + work_area.height - window->height;
    if (max_x < work_area.x) max_x = work_area.x;
    if (max_y < work_area.y) max_y = work_area.y;
    if (x < work_area.x) x = work_area.x;
    if (x > max_x) x = max_x;
    if (y < work_area.y) y = work_area.y;
    if (y > max_y) y = max_y;
    window->x = x;
    window->y = y;
}

static void wm_gui_update_resize(wm_gui_window_t* window,
                                 const mouse_event_t* event) {
    tb_rect_t work_area;
    int left = wm_gui_capture_x;
    int top = wm_gui_capture_y;
    int right = wm_gui_capture_x + wm_gui_capture_width;
    int bottom = wm_gui_capture_y + wm_gui_capture_height;
    int work_right;
    int work_bottom;

    if (!wm_gui_get_work_area(&work_area)) return;
    work_right = work_area.x + work_area.width;
    work_bottom = work_area.y + work_area.height;
    if (wm_gui_resize_edges & WM_GUI_RESIZE_LEFT) {
        left = event->x;
        if (left < work_area.x) left = work_area.x;
        if (left > right - wm_gui_window_min_width(window)) {
            left = right - wm_gui_window_min_width(window);
        }
    }
    if (wm_gui_resize_edges & WM_GUI_RESIZE_RIGHT) {
        right = event->x;
        if (right > work_right) right = work_right;
        if (right < left + wm_gui_window_min_width(window)) {
            right = left + wm_gui_window_min_width(window);
        }
    }
    if (wm_gui_resize_edges & WM_GUI_RESIZE_TOP) {
        top = event->y;
        if (top < work_area.y) top = work_area.y;
        if (top > bottom - wm_gui_window_min_height(window)) {
            top = bottom - wm_gui_window_min_height(window);
        }
    }
    if (wm_gui_resize_edges & WM_GUI_RESIZE_BOTTOM) {
        bottom = event->y;
        if (bottom > work_bottom) bottom = work_bottom;
        if (bottom < top + wm_gui_window_min_height(window)) {
            bottom = top + wm_gui_window_min_height(window);
        }
    }
    window->x = left;
    window->y = top;
    window->width = right - left;
    window->height = bottom - top;
}

static int wm_gui_update_interaction(const mouse_event_t* event) {
    wm_gui_window_t* window;

    if (wm_gui_capture_index < 0 ||
        wm_gui_capture_index >= wm_gui_window_count) return 0;
    window = &wm_gui_windows[wm_gui_capture_index];
    if (!window->visible || window->state == WM_STATE_MAXIMIZED) {
        wm_gui_clear_interaction();
        return 1;
    }
    if (wm_gui_drag_active) wm_gui_update_drag(window, event);
    if (wm_gui_resize_active) wm_gui_update_resize(window, event);
    wm_gui_draw_all();
    return 1;
}

static int wm_gui_handle_wheel(mouse_event_t* event) {
    int selected = -1;
    int highest_z = -1;
    int handled;

    if (!event || event->wheel == 0) return 0;
    for (int i = 0; i < wm_gui_window_count; i++) {
        if (wm_gui_windows[i].visible &&
            wm_gui_point_in_content(&wm_gui_windows[i], event->x, event->y) &&
            wm_gui_windows[i].z_order > highest_z) {
            selected = i;
            highest_z = wm_gui_windows[i].z_order;
        }
    }
    if (selected < 0 || !wm_gui_windows[selected].app->on_mouse) return 0;

    wm_gui_dispatching_input = 1;
    handled = wm_gui_windows[selected].app->on_mouse(
        event, wm_gui_windows[selected].x + WM_GUI_FRAME_INSET,
        wm_gui_windows[selected].y + WM_GUI_CONTENT_TOP,
        wm_gui_windows[selected].width - WM_GUI_FRAME_INSET * 2,
        wm_gui_windows[selected].height - WM_GUI_CONTENT_TOP -
        WM_GUI_CONTENT_BOTTOM);
    wm_gui_dispatching_input = 0;
    if (handled) wm_gui_draw_all();
    return 1;
}

static int wm_gui_handle_mouse(mouse_event_t* event) {
    int selected = -1;
    int highest_z = -1;

    if (!event) return 0;
    if (event->event == MOUSE_EVENT_RELEASE) {
        int was_captured = wm_gui_capture_index >= 0;

        wm_gui_clear_interaction();
        return was_captured;
    }
    if (event->event == MOUSE_EVENT_MOVE) return wm_gui_update_interaction(event);
    if (event->event == MOUSE_EVENT_WHEEL) return wm_gui_handle_wheel(event);
    if (event->event != MOUSE_EVENT_PRESS || !(event->changed & MOUSE_BTN_LEFT)) {
        return 0;
    }

    wm_gui_clear_interaction();
    for (int i = 0; i < wm_gui_window_count; i++) {
        if (wm_gui_windows[i].visible &&
            wm_gui_point_in_window(&wm_gui_windows[i], event->x, event->y) &&
            wm_gui_windows[i].z_order > highest_z) {
            selected = i;
            highest_z = wm_gui_windows[i].z_order;
        }
    }
    if (selected < 0) return 0;
    wm_gui_focus(selected);
    for (int i = 0; i < 3; i++) {
        tb_rect_t rect;
        wm_gui_control_rect(&wm_gui_windows[selected], i, &rect);
        if (event->x >= rect.x && event->x < rect.x + rect.width &&
            event->y >= rect.y && event->y < rect.y + rect.height) {
            wm_gui_control_t control = wm_gui_control_at(i);

            if (control == WM_GUI_CONTROL_CLOSE) wm_gui_close(selected, 1);
            if (control == WM_GUI_CONTROL_MINIMIZE) wm_gui_minimize(selected);
            if (control == WM_GUI_CONTROL_MAXIMIZE) wm_gui_maximize_or_restore(selected);
            if (control == WM_GUI_CONTROL_CLOSE &&
                wm_gui_return_to_desktop_if_empty()) {
                return 1;
            }
            wm_gui_draw_all();
            return 1;
        }
    }
    if (wm_gui_windows[selected].state != WM_STATE_MAXIMIZED) {
        int edges = wm_gui_resize_edges_at(&wm_gui_windows[selected],
                                           event->x, event->y);

        if (edges) {
            wm_gui_begin_resize(selected, edges);
            wm_gui_draw_all();
            return 1;
        }
        if (wm_gui_point_in_title_bar(&wm_gui_windows[selected],
                                      event->x, event->y)) {
            wm_gui_begin_drag(selected, event);
            wm_gui_draw_all();
            return 1;
        }
    }
    if (wm_gui_point_in_content(&wm_gui_windows[selected], event->x, event->y) &&
        wm_gui_windows[selected].app->on_mouse) {
        wm_gui_dispatching_input = 1;
        wm_gui_windows[selected].app->on_mouse(
            event, wm_gui_windows[selected].x + WM_GUI_FRAME_INSET,
            wm_gui_windows[selected].y + WM_GUI_CONTENT_TOP,
            wm_gui_windows[selected].width - WM_GUI_FRAME_INSET * 2,
            wm_gui_windows[selected].height - WM_GUI_CONTENT_TOP -
            WM_GUI_CONTENT_BOTTOM);
        wm_gui_dispatching_input = 0;
    }
    wm_gui_draw_all();
    return 1;
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
    wm_gui_window_count = 0;
    wm_gui_focused = -1;
    wm_gui_z_counter = 0;
    wm_gui_reflow_pending = 0;
    wm_gui_clear_interaction();
}

wm_config_t* wm_get_config(void) {
    return &wm.config;
}

void wm_set_btn_position(wm_btn_position_t pos) {
    wm.config.btn_position = pos;
    if (wm_active) wm_draw_all();
}

void wm_set_btn_order(wm_btn_order_t order) {
    wm.config.btn_order = order;
    if (wm_active) wm_draw_all();
}

void wm_set_show_title(int show) {
    wm.config.show_title_text = show;
    if (wm_active) wm_draw_all();
}

void wm_set_border_style(int style) {
    wm.config.border_style = style;
    if (wm_active) wm_draw_all();
}

static void wm_gui_initialize_window(int index, const wm_hosted_app_t* app,
                                     const tb_rect_t* work_area) {
    wm_gui_window_t* window = &wm_gui_windows[index];
    int offset = index * WM_GUI_MARGIN;

    window->id = WM_GUI_ID_BASE + index;
    window->app = app;
    window->width = app->default_width;
    window->height = app->default_height;
    if (window->width < wm_gui_window_min_width(window)) {
        window->width = wm_gui_window_min_width(window);
    }
    if (window->height < wm_gui_window_min_height(window)) {
        window->height = wm_gui_window_min_height(window);
    }
    if (window->width > work_area->width) window->width = work_area->width;
    if (window->height > work_area->height) window->height = work_area->height;
    window->x = work_area->x + WM_GUI_MARGIN + offset;
    window->y = work_area->y + WM_GUI_MARGIN + offset;
    window->restore_x = window->x;
    window->restore_y = window->y;
    window->restore_width = window->width;
    window->restore_height = window->height;
    window->state = WM_STATE_NORMAL;
    window->visible = 1;
    window->focused = 0;
    window->z_order = wm_gui_z_counter++;
    wm_gui_constrain_window(window);
    taskbar_add_window(window->id, app->taskbar_label);
}

int wm_register_hosted_app(const wm_hosted_app_t* app) {
    tb_rect_t work_area;
    int index;
    int min_width;
    int min_height;

    if (!app || !app->title || !app->taskbar_label || !app->on_draw ||
        !app->on_key || !app->on_close) {
        LOG_ERROR("WM", "Descritor de aplicativo hospedado invalido");
        return ERR_NULL;
    }
    if (app->min_width <= 0 || app->min_height <= 0 ||
        app->default_width <= 0 || app->default_height <= 0) {
        LOG_ERROR("WM", "Dimensoes de aplicativo hospedado invalidas");
        return ERR_INVALID;
    }
    if (!wm_active || !wm_gui_enabled()) {
        LOG_WARN("WM", "Workspace grafico indisponivel para aplicativo");
        return ERR_STATE;
    }
    if (!wm_gui_get_work_area(&work_area)) {
        LOG_ERROR("WM", "Area de trabalho grafica indisponivel");
        return ERR_UNAVAILABLE;
    }
    min_width = app->min_width > WM_GUI_MIN_WIDTH ?
                app->min_width : WM_GUI_MIN_WIDTH;
    min_height = app->min_height > WM_GUI_MIN_HEIGHT ?
                 app->min_height : WM_GUI_MIN_HEIGHT;
    if (work_area.width < min_width || work_area.height < min_height) {
        LOG_WARN("WM", "Area de trabalho insuficiente para aplicativo");
        return ERR_OVERFLOW;
    }
    index = wm_gui_find_app(app->app_type);
    if (index >= 0 && wm_gui_windows[index].visible) {
        wm_gui_focus(index);
        wm_gui_draw_all();
        return OK;
    }
    if (index < 0) {
        if (wm_gui_window_count >= WM_GUI_WINDOW_COUNT) {
            LOG_WARN("WM", "Limite de janelas hospedadas atingido");
            return ERR_OVERFLOW;
        }
        index = wm_gui_window_count++;
    }
    wm_gui_initialize_window(index, app, &work_area);
    wm_gui_focus(index);
    wm_gui_draw_all();
    return OK;
}

int wm_close_hosted_app(wm_app_type_t app_type) {
    int index = wm_gui_find_app(app_type);

    if (index < 0 || !wm_gui_windows[index].visible) {
        LOG_WARN("WM", "Aplicativo hospedado nao encontrado");
        return ERR_NOT_FOUND;
    }
    wm_gui_close(index, 1);
    if (wm_gui_dispatching_input) {
        return OK;
    }
    if (wm_gui_return_to_desktop_if_empty()) return OK;
    wm_gui_draw_all();
    return OK;
}

void wm_request_hosted_redraw(wm_app_type_t app_type) {
    int index = wm_gui_find_app(app_type);

    if (!wm_active || !wm_gui_enabled() || index < 0 ||
        !wm_gui_windows[index].visible) return;
    if (wm_gui_dispatching_input) {
        return;
    }
    wm_gui_draw_all();
}

int wm_reflow_display(void) {
    tb_rect_t work_area;

    if (!wm_active) return OK;
    if (!wm_gui_enabled() || !wm_gui_get_work_area(&work_area)) {
        LOG_ERROR("WM", "Area grafica indisponivel para reorganizacao");
        return ERR_UNAVAILABLE;
    }
    for (int i = 0; i < wm_gui_window_count; i++) {
        int min_width = wm_gui_window_min_width(&wm_gui_windows[i]);
        int min_height = wm_gui_window_min_height(&wm_gui_windows[i]);

        if (min_width > work_area.width || min_height > work_area.height) {
            LOG_WARN("WM", "Escala nao comporta uma janela hospedada");
            return ERR_OVERFLOW;
        }
    }

    wm_gui_clear_interaction();
    for (int i = 0; i < wm_gui_window_count; i++) {
        wm_gui_window_t* window = &wm_gui_windows[i];
        int min_width = wm_gui_window_min_width(window);
        int min_height = wm_gui_window_min_height(window);

        if (window->state != WM_STATE_MAXIMIZED) {
            if (window->width < min_width) window->width = min_width;
            if (window->height < min_height) window->height = min_height;
        }
        wm_gui_constrain_window(window);
        if (window->state == WM_STATE_MAXIMIZED) {
            int max_restore_x;
            int max_restore_y;

            if (window->restore_width < min_width) {
                window->restore_width = min_width;
            }
            if (window->restore_height < min_height) {
                window->restore_height = min_height;
            }
            if (window->restore_width > work_area.width) {
                window->restore_width = work_area.width;
            }
            if (window->restore_height > work_area.height) {
                window->restore_height = work_area.height;
            }
            max_restore_x = work_area.x + work_area.width -
                            window->restore_width;
            max_restore_y = work_area.y + work_area.height -
                            window->restore_height;
            if (window->restore_x < work_area.x) {
                window->restore_x = work_area.x;
            }
            if (window->restore_y < work_area.y) {
                window->restore_y = work_area.y;
            }
            if (window->restore_x > max_restore_x) {
                window->restore_x = max_restore_x;
            }
            if (window->restore_y > max_restore_y) {
                window->restore_y = max_restore_y;
            }
        } else {
            window->restore_x = window->x;
            window->restore_y = window->y;
            window->restore_width = window->width;
            window->restore_height = window->height;
        }
    }
    if (wm_gui_dispatching_input) {
        wm_gui_reflow_pending = 1;
    } else {
        wm_gui_draw_all();
    }
    return OK;
}

int wm_is_hosted_app_focused(wm_app_type_t app_type) {
    return wm_active && wm_gui_enabled() && wm_gui_focused >= 0 &&
           wm_gui_windows[wm_gui_focused].visible &&
           wm_gui_windows[wm_gui_focused].app &&
           wm_gui_windows[wm_gui_focused].app->app_type == app_type;
}

void wm_draw_desktop(void) {
    for (int y = 0; y < SCREEN_ROWS - 1; y++) {
        for (int x = 0; x < SCREEN_COLS; x++) {
            if ((x + y) % 2 == 0) {
                video_put_char_at(' ', 0x17, x, y);
            } else {
                video_put_char_at(' ', 0x1F, x, y);
            }
        }
    }
}

static void draw_buttons_right(wm_window_t* win) {
    icon_entry_t* close = icons_get_wm(ICON_WM_CLOSE);
    icon_entry_t* minimize = icons_get_wm(ICON_WM_MINIMIZE);
    icon_entry_t* maximize = icons_get_wm(ICON_WM_MAXIMIZE);

    char btn_chars[3];
    uint8_t btn_colors[3];

    switch (wm.config.btn_order) {
        case WM_BTN_CLOSE_MIN_MAX:
            btn_chars[0] = close->ch; btn_colors[0] = win->focused ? close->color : close->color_selected;
            btn_chars[1] = minimize->ch; btn_colors[1] = win->focused ? minimize->color : minimize->color_selected;
            btn_chars[2] = maximize->ch; btn_colors[2] = win->focused ? maximize->color : maximize->color_selected;
            break;
        case WM_BTN_CLOSE_MAX_MIN:
            btn_chars[0] = close->ch; btn_colors[0] = win->focused ? close->color : close->color_selected;
            btn_chars[1] = maximize->ch; btn_colors[1] = win->focused ? maximize->color : maximize->color_selected;
            btn_chars[2] = minimize->ch; btn_colors[2] = win->focused ? minimize->color : minimize->color_selected;
            break;
        case WM_BTN_MIN_MAX_CLOSE:
            btn_chars[0] = minimize->ch; btn_colors[0] = win->focused ? minimize->color : minimize->color_selected;
            btn_chars[1] = maximize->ch; btn_colors[1] = win->focused ? maximize->color : maximize->color_selected;
            btn_chars[2] = close->ch; btn_colors[2] = win->focused ? close->color : close->color_selected;
            break;
        case WM_BTN_MAX_MIN_CLOSE:
            btn_chars[0] = maximize->ch; btn_colors[0] = win->focused ? maximize->color : maximize->color_selected;
            btn_chars[1] = minimize->ch; btn_colors[1] = win->focused ? minimize->color : minimize->color_selected;
            btn_chars[2] = close->ch; btn_colors[2] = win->focused ? close->color : close->color_selected;
            break;
        case WM_BTN_MIN_CLOSE_MAX:
            btn_chars[0] = minimize->ch; btn_colors[0] = win->focused ? minimize->color : minimize->color_selected;
            btn_chars[1] = close->ch; btn_colors[1] = win->focused ? close->color : close->color_selected;
            btn_chars[2] = maximize->ch; btn_colors[2] = win->focused ? maximize->color : maximize->color_selected;
            break;
        case WM_BTN_MAX_CLOSE_MIN:
            btn_chars[0] = maximize->ch; btn_colors[0] = win->focused ? maximize->color : maximize->color_selected;
            btn_chars[1] = close->ch; btn_colors[1] = win->focused ? close->color : close->color_selected;
            btn_chars[2] = minimize->ch; btn_colors[2] = win->focused ? minimize->color : minimize->color_selected;
            break;
    }

    int btn_start = win->x + win->width - 4;
    video_put_char_at(btn_chars[0], btn_colors[0], btn_start, win->y);
    video_put_char_at(btn_chars[1], btn_colors[1], btn_start + 1, win->y);
    video_put_char_at(btn_chars[2], btn_colors[2], btn_start + 2, win->y);
}

static void draw_buttons_left(wm_window_t* win) {
    icon_entry_t* close = icons_get_wm(ICON_WM_CLOSE);
    icon_entry_t* minimize = icons_get_wm(ICON_WM_MINIMIZE);
    icon_entry_t* maximize = icons_get_wm(ICON_WM_MAXIMIZE);

    char btn_chars[3];
    uint8_t btn_colors[3];

    switch (wm.config.btn_order) {
        case WM_BTN_CLOSE_MIN_MAX:
            btn_chars[0] = close->ch; btn_colors[0] = win->focused ? close->color : close->color_selected;
            btn_chars[1] = minimize->ch; btn_colors[1] = win->focused ? minimize->color : minimize->color_selected;
            btn_chars[2] = maximize->ch; btn_colors[2] = win->focused ? maximize->color : maximize->color_selected;
            break;
        case WM_BTN_CLOSE_MAX_MIN:
            btn_chars[0] = close->ch; btn_colors[0] = win->focused ? close->color : close->color_selected;
            btn_chars[1] = maximize->ch; btn_colors[1] = win->focused ? maximize->color : maximize->color_selected;
            btn_chars[2] = minimize->ch; btn_colors[2] = win->focused ? minimize->color : minimize->color_selected;
            break;
        case WM_BTN_MIN_MAX_CLOSE:
            btn_chars[0] = minimize->ch; btn_colors[0] = win->focused ? minimize->color : minimize->color_selected;
            btn_chars[1] = maximize->ch; btn_colors[1] = win->focused ? maximize->color : maximize->color_selected;
            btn_chars[2] = close->ch; btn_colors[2] = win->focused ? close->color : close->color_selected;
            break;
        case WM_BTN_MAX_MIN_CLOSE:
            btn_chars[0] = maximize->ch; btn_colors[0] = win->focused ? maximize->color : maximize->color_selected;
            btn_chars[1] = minimize->ch; btn_colors[1] = win->focused ? minimize->color : minimize->color_selected;
            btn_chars[2] = close->ch; btn_colors[2] = win->focused ? close->color : close->color_selected;
            break;
        case WM_BTN_MIN_CLOSE_MAX:
            btn_chars[0] = minimize->ch; btn_colors[0] = win->focused ? minimize->color : minimize->color_selected;
            btn_chars[1] = close->ch; btn_colors[1] = win->focused ? close->color : close->color_selected;
            btn_chars[2] = maximize->ch; btn_colors[2] = win->focused ? maximize->color : maximize->color_selected;
            break;
        case WM_BTN_MAX_CLOSE_MIN:
            btn_chars[0] = maximize->ch; btn_colors[0] = win->focused ? maximize->color : maximize->color_selected;
            btn_chars[1] = close->ch; btn_colors[1] = win->focused ? close->color : close->color_selected;
            btn_chars[2] = minimize->ch; btn_colors[2] = win->focused ? minimize->color : minimize->color_selected;
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
        win->width = SCREEN_COLS;
        win->height = SCREEN_ROWS - 1;
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
            if (x < SCREEN_COLS && y < SCREEN_ROWS - 1) {
                video_put_char_at(' ', 0x07, x, y);
            }
        }
    }

    if (win->on_redraw) {
        win->on_redraw(win->x + 1, win->y + 1, win->width - 2, win->height - 2);
    }
}

void wm_draw_all(void) {
    if (!wm_active) return;
    if (wm_gui_enabled()) {
        wm_gui_draw_all();
        return;
    }

    wm_draw_desktop();

    for (int z = wm.z_counter - 1; z >= 0; z--) {
        for (int i = 0; i < wm.window_count; i++) {
            if (wm.windows[i].visible && wm.windows[i].z_order == z) {
                wm_draw_window(i);
            }
        }
    }
    taskbar_draw();
}

int wm_create_window(const char* title, int x, int y, int w, int h,
                      wm_app_type_t app_type, wm_key_handler_t on_key, wm_redraw_handler_t on_redraw) {
    if (!recovery_is_enabled(RECOVERY_COMPONENT_WM)) {
        LOG_ERROR("WM", "Window Manager indisponivel");
        return ERR_STATE;
    }
    if (!title) {
        LOG_ERROR("WM", "Titulo de janela nulo");
        recovery_mark_degraded(RECOVERY_COMPONENT_WM, ERR_NULL,
                               "Tentativa de criar janela sem titulo");
        return ERR_NULL;
    }
    if (w < WM_MIN_WIDTH || h < WM_MIN_HEIGHT) {
        LOG_ERROR("WM", "Dimensoes de janela invalidas");
        recovery_mark_degraded(RECOVERY_COMPONENT_WM, ERR_INVALID,
                               "Tentativa de criar janela com dimensoes invalidas");
        return ERR_INVALID;
    }
    if (wm.window_count >= WM_MAX_WINDOWS) {
        LOG_WARN("WM", "Limite de janelas atingido");
        recovery_mark_degraded(RECOVERY_COMPONENT_WM, ERR_OVERFLOW,
                               "Limite de janelas atingido");
        return ERR_OVERFLOW;
    }

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

static void wm_gui_dispatch_key(uint8_t scancode) {
    if (wm_gui_focused < 0 ||
        !wm_gui_windows[wm_gui_focused].app ||
        !wm_gui_windows[wm_gui_focused].app->on_key) return;

    wm_gui_dispatching_input = 1;
    wm_gui_windows[wm_gui_focused].app->on_key(scancode);
    wm_gui_dispatching_input = 0;
}

static void wm_gui_dispatch_key_sequence(int extended, uint8_t scancode) {
    const wm_hosted_app_t* original_app = wm_gui_focused >= 0 ?
        wm_gui_windows[wm_gui_focused].app : 0;
    int application_redraw = wm_gui_focused >= 0 &&
        original_app && original_app->key_redraw ==
            WM_KEY_REDRAW_APPLICATION;

    if (extended) wm_gui_dispatch_key(WM_SCANCODE_EXTENDED);
    wm_gui_dispatch_key(scancode);
    if (wm_active && wm_gui_enabled() &&
        (wm_gui_reflow_pending || !application_redraw ||
         wm_gui_focused < 0 ||
         wm_gui_windows[wm_gui_focused].app != original_app)) {
        wm_gui_reflow_pending = 0;
        wm_gui_draw_all();
    }
}

static int wm_gui_handle_key(uint8_t scancode) {
    int extended = wm_gui_extended_scancode;

    if (scancode == WM_SCANCODE_EXTENDED) {
        wm_gui_extended_scancode = 1;
        return WM_RESULT_NONE;
    }
    wm_gui_extended_scancode = 0;
    if (scancode == WM_SCANCODE_ALT) {
        wm_gui_alt_down = 1;
        return WM_RESULT_NONE;
    }
    if (scancode == WM_SCANCODE_ALT_RELEASE) {
        wm_gui_alt_down = 0;
        return WM_RESULT_NONE;
    }
    if (scancode == WM_SCANCODE_SHIFT_LEFT) {
        wm_gui_shift_mask |= WM_GUI_SHIFT_LEFT_MASK;
        wm_gui_dispatch_key(scancode);
        return WM_RESULT_NONE;
    }
    if (scancode == WM_SCANCODE_SHIFT_RIGHT) {
        wm_gui_shift_mask |= WM_GUI_SHIFT_RIGHT_MASK;
        wm_gui_dispatch_key(scancode);
        return WM_RESULT_NONE;
    }
    if (scancode == WM_SCANCODE_SHIFT_LEFT_RELEASE) {
        wm_gui_shift_mask &= (uint8_t)~WM_GUI_SHIFT_LEFT_MASK;
        wm_gui_dispatch_key(scancode);
        return WM_RESULT_NONE;
    }
    if (scancode == WM_SCANCODE_SHIFT_RIGHT_RELEASE) {
        wm_gui_shift_mask &= (uint8_t)~WM_GUI_SHIFT_RIGHT_MASK;
        wm_gui_dispatch_key(scancode);
        return WM_RESULT_NONE;
    }
    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;

        if (wm_gui_alt_down &&
            (released == WM_SCANCODE_TAB || released == WM_SCANCODE_F4 ||
             released == WM_SCANCODE_F9 || released == WM_SCANCODE_F10)) {
            return WM_RESULT_NONE;
        }
        wm_gui_dispatch_key_sequence(extended, scancode);
        return WM_RESULT_NONE;
    }

    if (wm_gui_alt_down) {
        if (scancode == WM_SCANCODE_TAB) {
            if (wm_gui_shift_mask) wm_gui_focus_prev();
            else wm_gui_focus_next();
            wm_gui_draw_all();
            return WM_RESULT_NONE;
        }
        if (scancode == WM_SCANCODE_F4 && wm_gui_focused >= 0) {
            wm_gui_close(wm_gui_focused, 1);
            if (!wm_gui_return_to_desktop_if_empty()) wm_gui_draw_all();
            return WM_RESULT_NONE;
        }
        if (scancode == WM_SCANCODE_F9 && wm_gui_focused >= 0) {
            wm_gui_minimize(wm_gui_focused);
            wm_gui_draw_all();
            return WM_RESULT_NONE;
        }
        if (scancode == WM_SCANCODE_F10 && wm_gui_focused >= 0) {
            wm_gui_maximize_or_restore(wm_gui_focused);
            wm_gui_draw_all();
            return WM_RESULT_NONE;
        }
    }

    wm_gui_dispatch_key_sequence(extended, scancode);
    return WM_RESULT_NONE;
}

int wm_handle_key(uint8_t scancode) {
    if (!wm_active) return WM_RESULT_NONE;

    if (wm_gui_enabled()) return wm_gui_handle_key(scancode);

    if ((scancode & 0x80) && scancode != WM_SCANCODE_EXTENDED) {
        return WM_RESULT_NONE;
    }

    if (scancode == WM_SCANCODE_EXTENDED) return WM_RESULT_NONE;

    if (scancode == 0x0F) {
        wm_focus_next();
        return WM_RESULT_NONE;
    }

    if (scancode == 0x01) {
        wm_close_focused();
        return WM_RESULT_NONE;
    }

    if (scancode == 0x3B) {
        if (wm.focused_id >= 0) {
            wm_minimize_window(wm.focused_id);
        }
        return WM_RESULT_NONE;
    }

    if (scancode == 0x3C) {
        if (wm.focused_id >= 0) {
            if (wm.windows[wm.focused_id].state == WM_STATE_MAXIMIZED) {
                wm_restore_window(wm.focused_id);
            } else {
                wm_maximize_window(wm.focused_id);
            }
        }
        return WM_RESULT_NONE;
    }

    if (wm.focused_id >= 0 && wm.windows[wm.focused_id].on_key) {
        wm.windows[wm.focused_id].on_key(scancode);
    }
    return WM_RESULT_NONE;
}

int wm_is_active(void) {
    return wm_active;
}

void wm_set_active(int active) {
    if (!active && wm_active) {
        wm_gui_clear_interaction();
        for (int i = 0; i < wm_gui_window_count; i++) {
            if (wm_gui_windows[i].app) wm_gui_close(i, 1);
        }
        wm_gui_reset();
    }
    if (active && wm_active) {
        wm_draw_all();
        return;
    }
    wm_active = active;
    if (active) {
        if (wm_gui_enabled()) {
            wm_gui_reset();
            wm_gui_draw_all();
        } else {
            wm_draw_all();
        }
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

int wm_handle_click(int px, int py) {
    if (!wm_active) return 0;
    if (wm_gui_enabled()) {
        mouse_event_t event;
        event.x = px;
        event.y = py;
        event.event = MOUSE_EVENT_PRESS;
        event.changed = MOUSE_BTN_LEFT;
        event.buttons = MOUSE_BTN_LEFT;
        event.wheel = 0;
        return wm_gui_handle_mouse(&event);
    }
    int col = px / 8;
    int row = py / 16;

    int highest_z = -1;
    int win_idx = -1;

    for (int i = 0; i < wm.window_count; i++) {
        if (!wm.windows[i].visible || wm.windows[i].state == WM_STATE_MINIMIZED) continue;
        int wx = wm.windows[i].x;
        int wy = wm.windows[i].y;
        int ww = wm.windows[i].width;
        int wh = wm.windows[i].height;

        if (col >= wx && col < wx + ww && row >= wy && row < wy + wh) {
            if (wm.windows[i].z_order > highest_z) {
                highest_z = wm.windows[i].z_order;
                win_idx = i;
            }
        }
    }

    if (win_idx >= 0) {
        wm_focus_window(win_idx);
        return 1;
    }
    return 0;
}

int wm_handle_mouse(mouse_event_t* event) {
    if (!event || !wm_active) return 0;
    if (wm_gui_enabled()) return wm_gui_handle_mouse(event);
    if (event->event != MOUSE_EVENT_PRESS ||
        !(event->changed & MOUSE_BTN_LEFT)) return 0;
    return wm_handle_click(event->x, event->y);
}

void wm_toggle_window(int id) {
    int index = -1;

    if (!wm_active || !wm_gui_enabled()) return;
    for (int i = 0; i < wm_gui_window_count; i++) {
        if (wm_gui_windows[i].id == id) {
            index = i;
            break;
        }
    }
    if (index < 0) return;
    if (!wm_gui_windows[index].visible &&
        wm_gui_windows[index].state == WM_STATE_MINIMIZED) {
        wm_gui_windows[index].visible = 1;
        wm_gui_windows[index].state = WM_STATE_NORMAL;
        wm_gui_focus(index);
    } else if (wm_gui_windows[index].focused) {
        wm_gui_minimize(index);
    } else {
        wm_gui_focus(index);
    }
    wm_gui_draw_all();
}
