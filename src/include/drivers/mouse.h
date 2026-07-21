#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

typedef struct {
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
} mouse_packet_t;

typedef void (*mouse_callback_t)(mouse_packet_t*);

void mouse_init(void);
void mouse_process_events(void);
mouse_callback_t mouse_set_callback(mouse_callback_t cb);
int mouse_get_x(void);
int mouse_get_y(void);
uint8_t mouse_get_buttons(void);

#endif
