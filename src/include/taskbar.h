#ifndef TASKBAR_H
#define TASKBAR_H

#include "types.h"

#define TASKBAR_ROW 24
#define TASKBAR_BUTTON_MAX 8

#define TASKBAR_COLOR_BG 0x07
#define TASKBAR_COLOR_BTN 0x07
#define TASKBAR_COLOR_BTN_ACTIVE 0x1F
#define TASKBAR_COLOR_BTN_HOVER 0x70
#define TASKBAR_COLOR_TEXT 0x07
#define TASKBAR_COLOR_CLOCK 0x07
#define TASKBAR_COLOR_MENU_BG 0x17
#define TASKBAR_COLOR_MENU_BORDER 0x01
#define TASKBAR_COLOR_MENU_TEXT 0x17
#define TASKBAR_COLOR_MENU_HIGHLIGHT 0x1F

typedef enum {
    TB_APP_NONE = 0,
    TB_APP_SHELL,
    TB_APP_EXPLORER,
    TB_APP_TASKMGR
} tb_app_type_t;

typedef struct {
    const char* name;
    tb_app_type_t type;
    int active;
} tb_button_t;

void taskbar_init(void);
void taskbar_draw(void);
void taskbar_update_clock(void);
void taskbar_add_app(tb_app_type_t type, const char* name);
void taskbar_remove_app(tb_app_type_t type);
int  taskbar_handle_key(uint8_t scancode);
int  taskbar_is_menu_open(void);

#endif
