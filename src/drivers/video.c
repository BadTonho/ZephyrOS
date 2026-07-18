#include "video.h"

static uint16_t* video_memory = (uint16_t*)VIDEO_MEMORY;
static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t current_color = 0x07;

static uint8_t make_color(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

static uint16_t make_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static void update_cursor(void) {
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    uint8_t high = (uint8_t)((pos >> 8) & 0xFF);
    uint8_t low = (uint8_t)(pos & 0xFF);

    uint8_t* ports = (uint8_t*)0x3D4;
    ports[0] = 0x0F;
    ports[1] = low;
    ports[0] = 0x0E;
    ports[1] = high;
}

static void scroll(void) {
    if (cursor_y >= VGA_HEIGHT) {
        for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
            video_memory[i] = video_memory[i + VGA_WIDTH];
        }
        for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
            video_memory[i] = make_entry(' ', current_color);
        }
        cursor_y = VGA_HEIGHT - 1;
    }
}

void video_init(void) {
    video_clear();
    current_color = make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    update_cursor();
}

void video_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        video_memory[i] = make_entry(' ', current_color);
    }
    cursor_x = 0;
    cursor_y = 0;
    update_cursor();
}

void video_put_char(char c, uint8_t color) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            video_memory[cursor_y * VGA_WIDTH + cursor_x] = make_entry(' ', color);
        }
    } else {
        video_memory[cursor_y * VGA_WIDTH + cursor_x] = make_entry(c, color);
        cursor_x++;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    scroll();
    update_cursor();
}

void video_print(const char* str, uint8_t color) {
    for (int i = 0; str[i] != '\0'; i++) {
        video_put_char(str[i], color);
    }
}

void video_set_color(uint8_t fg, uint8_t bg) {
    current_color = make_color(fg, bg);
}

void video_newline(void) {
    video_put_char('\n', current_color);
}

void video_backspace(void) {
    video_put_char('\b', current_color);
}
