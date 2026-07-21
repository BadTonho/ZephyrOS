#ifndef SETTINGS_H
#define SETTINGS_H

#include "types.h"
#include "drivers/mouse.h"

#define SETTINGS_MAX_CATEGORIES 6
#define SETTINGS_MAX_OPTIONS 8

typedef enum {
    SETTINGS_CAT_DISPLAY = 0,
    SETTINGS_CAT_TASKBAR,
    SETTINGS_CAT_WINDOWS,
    SETTINGS_CAT_ICONS,
    SETTINGS_CAT_SYSTEM,
    SETTINGS_CAT_SOUND,
    SETTINGS_CAT_ABOUT,
    SETTINGS_CAT_COUNT
} settings_category_t;

typedef enum {
    SETTINGS_OPT_NONE = 0,
    SETTINGS_OPT_TOGGLE,
    SETTINGS_OPT_LIST,
    SETTINGS_OPT_ACTION
} settings_option_type_t;

typedef enum {
    SETTINGS_MODE_CLASSIC = 0,
    SETTINGS_MODE_MODERN
} settings_mode_t;

typedef struct {
    const char* name;
    settings_option_type_t type;
    int value;
    int max_value;
    const char** list_values;
    int list_count;
} settings_option_t;

typedef struct {
    const char* name;
    settings_option_t options[SETTINGS_MAX_OPTIONS];
    int option_count;
} settings_page_t;

void settings_init(void);
void settings_open(void);
void settings_close(void);
void settings_draw(void);
int  settings_handle_key(uint8_t scancode);
int  settings_is_open(void);
settings_mode_t settings_get_mode(void);
int  settings_handle_mouse(mouse_event_t* event);

#endif
