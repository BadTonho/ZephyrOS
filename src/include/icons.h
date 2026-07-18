#ifndef ICONS_H
#define ICONS_H

#include "types.h"

typedef enum {
    ICON_DESKTOP_SHELL = 0,
    ICON_DESKTOP_EXPLORER,
    ICON_DESKTOP_TASKMGR,
    ICON_DESKTOP_COUNT
} icon_desktop_id_t;

typedef enum {
    ICON_WM_CLOSE = 0,
    ICON_WM_MINIMIZE,
    ICON_WM_MAXIMIZE,
    ICON_WM_COUNT
} icon_wm_id_t;

typedef enum {
    ICON_FM_FOLDER = 0,
    ICON_FM_FILE,
    ICON_FM_COUNT
} icon_fm_id_t;

typedef enum {
    ICON_TB_START = 0,
    ICON_TB_COUNT
} icon_tb_id_t;

typedef struct {
    char ch;
    uint8_t color;
    uint8_t color_selected;
} icon_entry_t;

typedef struct {
    icon_entry_t desktop[ICON_DESKTOP_COUNT];
    icon_entry_t wm[ICON_WM_COUNT];
    icon_entry_t fm[ICON_FM_COUNT];
    icon_entry_t tb[ICON_TB_COUNT];
} icon_registry_t;

void icons_init(void);
icon_registry_t* icons_get_registry(void);

icon_entry_t* icons_get_desktop(icon_desktop_id_t id);
icon_entry_t* icons_get_wm(icon_wm_id_t id);
icon_entry_t* icons_get_fm(icon_fm_id_t id);
icon_entry_t* icons_get_tb(icon_tb_id_t id);

void icons_set_desktop(icon_desktop_id_t id, char ch, uint8_t color, uint8_t color_sel);
void icons_set_wm(icon_wm_id_t id, char ch, uint8_t color, uint8_t color_sel);
void icons_set_fm(icon_fm_id_t id, char ch, uint8_t color, uint8_t color_sel);
void icons_set_tb(icon_tb_id_t id, char ch, uint8_t color, uint8_t color_sel);

void icons_reset_defaults(void);

#endif
