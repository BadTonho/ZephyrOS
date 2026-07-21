#ifndef TASKMANAGER_H
#define TASKMANAGER_H

#include "types.h"
#include "drivers/mouse.h"

void taskmgr_init(void);
void taskmgr_open(void);
void taskmgr_close(void);
void taskmgr_refresh(void);
void taskmgr_handle_key(uint8_t scancode);
int taskmgr_is_open(void);

/* A interface grafica e' uma frente separada da TUI do Shell. */
int taskmgr_open_gui(void);
int taskmgr_is_gui_open(void);
int taskmgr_is_gui_minimized(void);
void taskmgr_gui_restore(void);
void taskmgr_gui_update(void);
void taskmgr_gui_handle_key(uint8_t scancode);
int taskmgr_gui_handle_mouse(mouse_event_t* event);

void taskmgr_run(void);
#endif
