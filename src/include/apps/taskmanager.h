#ifndef TASKMANAGER_H
#define TASKMANAGER_H

#include "types.h"

void taskmgr_init(void);
void taskmgr_open(void);
void taskmgr_close(void);
void taskmgr_refresh(void);
void taskmgr_handle_key(uint8_t scancode);
int taskmgr_is_open(void);

#endif
