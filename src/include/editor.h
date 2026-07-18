#ifndef EDITOR_H
#define EDITOR_H

#include "types.h"

#define EDITOR_MAX_LINES 1000
#define EDITOR_MAX_LINE_LENGTH 256
#define EDITOR_TAB_SIZE 4

typedef struct {
    char* lines[EDITOR_MAX_LINES];
    uint32_t line_count;
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t scroll_x;
    uint32_t scroll_y;
    uint32_t view_width;
    uint32_t view_height;
    char filename[64];
    uint8_t modified;
    uint8_t running;
} editor_t;

void editor_init(void);
void editor_open(const char* filename);
void editor_new(void);
void editor_run(void);
void editor_close(void);
void editor_handle_key(uint8_t scancode);
uint8_t editor_is_running(void);

#endif
