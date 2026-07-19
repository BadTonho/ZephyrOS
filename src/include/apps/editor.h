#ifndef EDITOR_H
#define EDITOR_H

#include "types.h"

#define EDITOR_MAX_LINES 2000
#define EDITOR_MAX_LINE_LENGTH 512
#define EDITOR_TAB_SIZE 4

#define EDITOR_ENCODING_ASCII   0
#define EDITOR_ENCODING_UTF8    1
#define EDITOR_ENCODING_LATIN1  2

#define EDITOR_LF      0
#define EDITOR_CRLF    1
#define EDITOR_CR      2

#define EDITOR_SYNTAX_NONE     0
#define EDITOR_SYNTAX_C        1
#define EDITOR_SYNTAX_PYTHON   2
#define EDITOR_SYNTAX_ASM      3
#define EDITOR_SYNTAX_MARKDOWN 4

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
    uint8_t encoding;
    uint8_t line_ending;
    uint8_t syntax_mode;
    uint8_t word_wrap;
    uint8_t show_formatting;
    uint32_t wrap_width;
    uint32_t total_chars;
    uint32_t total_bytes;
    uint8_t bold_active;
    uint8_t italic_active;
} editor_t;

void editor_init(void);
void editor_open(const char* filename);
void editor_new(void);
void editor_run(void);
void editor_run_file(const char* filename);
void editor_close(void);
void editor_handle_key(uint8_t scancode);
uint8_t editor_is_running(void);

#endif
