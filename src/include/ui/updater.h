#ifndef UPDATER_H
#define UPDATER_H

#include "types.h"
#include "drivers/mouse.h"

typedef enum {
    UPDATER_MODE_SIMPLE = 0,
    UPDATER_MODE_CLASSIC
} updater_mode_t;

int updater_init(void);
int updater_open(void);
void updater_close(void);
void updater_draw(void);
void updater_handle_key(uint8_t scancode);
int updater_handle_mouse(mouse_event_t* event);
int updater_is_open(void);
updater_mode_t updater_get_mode(void);

#endif
