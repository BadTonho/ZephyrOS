#ifndef APPSTORE_H
#define APPSTORE_H

#include "types.h"
#include "drivers/mouse.h"

typedef enum {
    APPSTORE_MODE_SIMPLE = 0,
    APPSTORE_MODE_CLASSIC
} appstore_mode_t;

int appstore_init(void);
int appstore_open(void);
void appstore_close(void);
void appstore_draw(void);
void appstore_handle_key(uint8_t scancode);
int appstore_handle_mouse(mouse_event_t* event);
int appstore_is_open(void);
appstore_mode_t appstore_get_mode(void);

#endif
