#ifndef DESKTOP_H
#define DESKTOP_H

#include "types.h"
#include "drivers/mouse.h"

#define DESKTOP_MAX_ICONS 16
#define DESKTOP_ICON_WIDTH 10
#define DESKTOP_ICON_HEIGHT 4
#define DESKTOP_ICON_SPACING_X 14
#define DESKTOP_ICON_SPACING_Y 6
#define DESKTOP_START_X 2
#define DESKTOP_START_Y 2

#define DESKTOP_BG_COLOR 0x17
#define DESKTOP_ICON_COLOR 0x0F
#define DESKTOP_ICON_SELECTED 0x70
#define DESKTOP_ICON_TEXT_COLOR 0x0F
#define DESKTOP_TITLE_COLOR 0x0F

typedef enum {
    DESKTOP_APP_NONE = 0,
    DESKTOP_APP_SHELL,
    DESKTOP_APP_EXPLORER,
    DESKTOP_APP_TASKMGR,
    DESKTOP_APP_FILE
} desktop_app_type_t;

typedef enum {
    DESKTOP_MODE_CLASSIC = 0,
    DESKTOP_MODE_MODERN
} desktop_mode_t;

typedef struct {
    const char* name;
    desktop_app_type_t type;
    int x;
    int y;
    int modern_x;
    int modern_y;
    int modern_width;
    int modern_height;
    int selected;
} desktop_icon_t;

void desktop_init(void);
void desktop_draw(void);
void desktop_draw_icons(void);
void desktop_add_icon(const char* name, desktop_app_type_t type);
void desktop_update_selection(void);
int  desktop_handle_key(uint8_t scancode);
int  desktop_get_selected_app(void);
void desktop_set_active(int active);
int  desktop_is_active(void);
int  desktop_set_mode(desktop_mode_t mode);
desktop_mode_t desktop_get_mode(void);
int  desktop_handle_mouse(mouse_event_t* event);
int  desktop_handle_click(int px, int py);

#endif
