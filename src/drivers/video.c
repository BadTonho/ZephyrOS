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
    if (cursor_y >= VGA_HEIGHT - 1) {
        for (int i = 0; i < (VGA_HEIGHT - 2) * VGA_WIDTH; i++) {
            video_memory[i] = video_memory[i + VGA_WIDTH];
        }
        for (int i = (VGA_HEIGHT - 2) * VGA_WIDTH; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
            video_memory[i] = make_entry(' ', current_color);
        }
        cursor_y = VGA_HEIGHT - 2;
    }
}

void video_init(void) {
    video_clear();
    current_color = make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    update_cursor();
}

void video_clear(void) {
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
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

void video_set_cursor(int x, int y) {
    cursor_x = x;
    cursor_y = y;
    update_cursor();
}

int video_get_cursor_x(void) {
    return cursor_x;
}

int video_get_cursor_y(void) {
    return cursor_y;
}

void video_put_char_at(char c, uint8_t color, int x, int y) {
    if (x >= 0 && x < VGA_WIDTH && y >= 0 && y < VGA_HEIGHT) {
        video_memory[y * VGA_WIDTH + x] = (uint16_t)c | ((uint16_t)color << 8);
    }
}

void video_print_at(int x, int y, const char* str, uint8_t color) {
    int cx = x;
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            cx = x;
            y++;
            if (y >= VGA_HEIGHT) break;
            continue;
        }
        video_put_char_at(str[i], color, cx, y);
        cx++;
        if (cx >= VGA_WIDTH) {
            cx = x;
            y++;
            if (y >= VGA_HEIGHT) break;
        }
    }
}

void video_fill_rect(int x, int y, int w, int h, char c, uint8_t color) {
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            video_put_char_at(c, color, x + col, y + row);
        }
    }
}

void video_draw_hline(int x, int y, int w, char c, uint8_t color) {
    for (int i = 0; i < w; i++) {
        video_put_char_at(c, color, x + i, y);
    }
}

void video_draw_vline(int x, int y, int h, char c, uint8_t color) {
    for (int i = 0; i < h; i++) {
        video_put_char_at(c, color, x, y + i);
    }
}

void video_draw_box(int x, int y, int w, int h, uint8_t color) {
    for (int i = 0; i < w; i++) {
        video_put_char_at(0xCD, color, x + i, y);
        video_put_char_at(0xCD, color, x + i, y + h - 1);
    }
    for (int i = 0; i < h; i++) {
        video_put_char_at(0xBA, color, x, y + i);
        video_put_char_at(0xBA, color, x + w - 1, y + i);
    }
    video_put_char_at(0xC9, color, x, y);
    video_put_char_at(0xBB, color, x + w - 1, y);
    video_put_char_at(0xC8, color, x, y + h - 1);
    video_put_char_at(0xBC, color, x + w - 1, y + h - 1);
}
