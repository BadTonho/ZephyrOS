#ifndef GUITEST_H
#define GUITEST_H

#include "types.h"
#include "drivers/mouse.h"

void guitest_open(void);
void guitest_close(void);
int guitest_is_active(void);
void guitest_draw(void);
void guitest_handle_mouse(mouse_event_t* event);

#endif
