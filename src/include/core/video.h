#ifndef VIDEO_H
#define VIDEO_H

#include "types.h"

#define VIDEO_MEMORY 0xB8000

#define VGA_WIDTH  100
#define VGA_HEIGHT 37

/* O modo VESA 0x118 usado pelo stage2 tem 1024x768 pixels. */
#define SCREEN_COLS 128
#define SCREEN_ROWS 48

#define VGA_COLOR_BLACK 0
#define VGA_COLOR_BLUE 1
#define VGA_COLOR_GREEN 2
#define VGA_COLOR_CYAN 3
#define VGA_COLOR_RED 4
#define VGA_COLOR_MAGENTA 5
#define VGA_COLOR_BROWN 6
#define VGA_COLOR_LIGHT_GREY 7
#define VGA_COLOR_DARK_GREY 8
#define VGA_COLOR_LIGHT_BLUE 9
#define VGA_COLOR_LIGHT_GREEN 10
#define VGA_COLOR_LIGHT_CYAN 11
#define VGA_COLOR_LIGHT_RED 12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_YELLOW 14
#define VGA_COLOR_WHITE 15

void video_init(void);
void video_clear(void);
void video_put_char(char c, uint8_t color);
void video_print(const char* str, uint8_t color);
void video_set_color(uint8_t fg, uint8_t bg);
void video_newline(void);
void video_backspace(void);

void video_set_cursor(int x, int y);
int  video_get_cursor_x(void);
int  video_get_cursor_y(void);
void video_put_char_at(char c, uint8_t color, int x, int y);
void video_print_at(int x, int y, const char* str, uint8_t color);
void video_fill_rect(int x, int y, int w, int h, char c, uint8_t color);
void video_draw_hline(int x, int y, int w, char c, uint8_t color);
void video_draw_vline(int x, int y, int h, char c, uint8_t color);
void video_draw_box(int x, int y, int w, int h, uint8_t color);

#endif
