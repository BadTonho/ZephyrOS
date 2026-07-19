#ifndef TASKBAR_H
#define TASKBAR_H

#include "types.h"
#include "core/video.h"

#define TASKBAR_BUTTON_MAX 8
#define TASKBAR_DEFAULT_ROW (SCREEN_ROWS - 1)

typedef enum {
    TB_POS_BOTTOM = 0,
    TB_POS_TOP,
    TB_POS_LEFT,
    TB_POS_RIGHT,
    TB_POS_CUSTOM
} tb_position_t;

typedef enum {
    TB_SIZE_SMALL = 0,
    TB_SIZE_MEDIUM,
    TB_SIZE_LARGE
} tb_icon_size_t;

typedef enum {
    TB_APP_NONE = 0,
    TB_APP_SHELL,
    TB_APP_EXPLORER,
    TB_APP_TASKMGR,
    TB_APP_DESKTOP
} tb_app_type_t;

typedef struct {
    const char* name;
    tb_app_type_t type;
    int active;
} tb_button_t;

typedef struct {
    tb_position_t position;
    tb_icon_size_t icon_size;
    int pinned;
    int custom_x;
    int custom_y;
    int width;
    int height;
} tb_config_t;

void taskbar_init(void);
void taskbar_draw(void);
void taskbar_update_clock(void);
void taskbar_add_app(tb_app_type_t type, const char* name);
void taskbar_remove_app(tb_app_type_t type);
int  taskbar_handle_key(uint8_t scancode);
int  taskbar_is_menu_open(void);

void taskbar_set_position(tb_position_t pos);
void taskbar_set_icon_size(tb_icon_size_t size);
void taskbar_set_pinned(int pinned);
void taskbar_set_custom_position(int x, int y);
tb_config_t* taskbar_get_config(void);
void taskbar_draw_config_menu(void);
int  taskbar_handle_config_key(uint8_t scancode);

#endif
