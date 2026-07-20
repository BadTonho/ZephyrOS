#include "core/video.h"
#include "drivers/vesa.h"
#include "drivers/font.h"
#include "core/log.h"

static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t current_color = 0x07;
static uint8_t use_framebuffer = 0;

static char text_buffer[SCREEN_COLS * SCREEN_ROWS];
static uint8_t color_buffer[SCREEN_COLS * SCREEN_ROWS];

static const uint32_t vga_palette[16] = {
    0x00000000,
    0x00AA0000,
    0x0000AA00,
    0x00AAAA00,
    0x000000AA,
    0x00AA00AA,
    0x000055AA,
    0x00AAAAAA,
    0x00555555,
    0x005555FF,
    0x0055FF55,
    0x0055FFFF,
    0x00FF5555,
    0x00FF55FF,
    0x00FFFF55,
    0x00FFFFFF
};

static uint32_t vga_color_to_rgb(uint8_t vga_color) {
    uint8_t fg = vga_color & 0x0F;
    uint8_t bg = (vga_color >> 4) & 0x0F;
    (void)bg;
    return vga_palette[fg];
}

static uint32_t vga_bg_to_rgb(uint8_t vga_color) {
    uint8_t bg = (vga_color >> 4) & 0x0F;
    return vga_palette[bg];
}

static void render_char_at(int x, int y, char c, uint8_t color) {
    if (!use_framebuffer) return;

    vesa_mode_t* mode = vesa_get_mode();
    if (!mode || !mode->initialized) return;

    int pixel_x = x * FONT_WIDTH;
    int pixel_y = y * FONT_HEIGHT;

    uint32_t bg = vga_bg_to_rgb(color);
    vesa_color_t bg_color;
    bg_color.raw = bg;
    vesa_fill_rect(pixel_x, pixel_y, FONT_WIDTH, FONT_HEIGHT, bg_color);

    if (c == ' ' || c == '\0') return;

    uint32_t fg = vga_color_to_rgb(color);
    vesa_color_t fg_color;
    fg_color.raw = fg;

    const uint8_t* glyph = font_get_glyph(c);
    if (!glyph) return;

    for (uint32_t row = 0; row < FONT_HEIGHT; row++) {
        for (uint32_t col = 0; col < FONT_WIDTH; col++) {
            if (glyph[row] & (0x80 >> col)) {
                vesa_put_pixel(pixel_x + col, pixel_y + row, fg_color);
            }
        }
    }
}

static void render_cell(int x, int y) {
    if (x >= 0 && x < SCREEN_COLS && y >= 0 && y < SCREEN_ROWS) {
        int idx = y * SCREEN_COLS + x;
        render_char_at(x, y, text_buffer[idx], color_buffer[idx]);
    }
}

static void update_cursor(void) {
    if (use_framebuffer) return;

    int vga_x = cursor_x;
    int vga_y = cursor_y;
    if (vga_x >= VGA_WIDTH) vga_x = VGA_WIDTH - 1;
    if (vga_y >= VGA_HEIGHT) vga_y = VGA_HEIGHT - 1;
    if (vga_x < 0) vga_x = 0;
    if (vga_y < 0) vga_y = 0;

    uint16_t pos = vga_y * VGA_WIDTH + vga_x;
    asm volatile("outb %0, %1" : : "a"((uint8_t)(pos & 0xFF)), "Nd"((uint16_t)0x3D4));
    asm volatile("outb %0, %1" : : "a"((uint8_t)((pos >> 8) & 0xFF)), "Nd"((uint16_t)0x3D5));
    asm volatile("outb %0, %1" : : "a"((uint8_t)0x0E), "Nd"((uint16_t)0x3D4));
    asm volatile("outb %0, %1" : : "a"((uint8_t)((pos >> 8) & 0xFF)), "Nd"((uint16_t)0x3D5));
}

static void scroll(void) {
    if (cursor_y >= SCREEN_ROWS - 1) {
        for (int i = 0; i < (SCREEN_ROWS - 2) * SCREEN_COLS; i++) {
            text_buffer[i] = text_buffer[i + SCREEN_COLS];
            color_buffer[i] = color_buffer[i + SCREEN_COLS];
        }
        for (int i = (SCREEN_ROWS - 2) * SCREEN_COLS; i < (SCREEN_ROWS - 1) * SCREEN_COLS; i++) {
            text_buffer[i] = ' ';
            color_buffer[i] = current_color;
        }
        cursor_y = SCREEN_ROWS - 2;

        if (use_framebuffer) {
            for (int y = 0; y < SCREEN_ROWS - 1; y++) {
                for (int x = 0; x < SCREEN_COLS; x++) {
                    render_char_at(x, y, text_buffer[y * SCREEN_COLS + x], color_buffer[y * SCREEN_COLS + x]);
                }
            }
        }
    }
}

void video_init(void) {
    LOG_INFO("VIDEO", "Inicializando video");
    vesa_mode_t* mode = vesa_get_mode();
    use_framebuffer = (mode && mode->initialized) ? 1 : 0;

    for (int i = 0; i < SCREEN_COLS * SCREEN_ROWS; i++) {
        text_buffer[i] = ' ';
        color_buffer[i] = 0x07;
    }

    video_clear();
    current_color = 0x07;
    update_cursor();
    LOG_INFO("VIDEO", "Video inicializado com sucesso");
}

void video_clear(void) {
    for (int i = 0; i < SCREEN_COLS * SCREEN_ROWS; i++) {
        text_buffer[i] = ' ';
        color_buffer[i] = current_color;
    }
    cursor_x = 0;
    cursor_y = 0;

    if (use_framebuffer) {
        vesa_mode_t* mode = vesa_get_mode();
        if (mode && mode->initialized) {
            uint32_t bg = vga_bg_to_rgb(current_color);
            vesa_color_t c;
            c.raw = bg;
            vesa_clear(c);
        }
    } else {
        uint16_t* vm = (uint16_t*)VIDEO_MEMORY;
        uint16_t entry = (uint16_t)(' ') | ((uint16_t)current_color << 8);
        for (int i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) {
            vm[i] = entry;
        }
    }

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
            text_buffer[cursor_y * SCREEN_COLS + cursor_x] = ' ';
            color_buffer[cursor_y * SCREEN_COLS + cursor_x] = color;
            render_cell(cursor_x, cursor_y);
        }
    } else {
        if (cursor_x < SCREEN_COLS && cursor_y < SCREEN_ROWS) {
            int idx = cursor_y * SCREEN_COLS + cursor_x;
            text_buffer[idx] = c;
            color_buffer[idx] = color;
            render_cell(cursor_x, cursor_y);
            cursor_x++;
            if (cursor_x >= SCREEN_COLS) {
                cursor_x = 0;
                cursor_y++;
            }
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
    current_color = fg | (bg << 4);
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
    if (x < 0 || x >= SCREEN_COLS || y < 0 || y >= SCREEN_ROWS) return;

    int idx = y * SCREEN_COLS + x;
    text_buffer[idx] = c;
    color_buffer[idx] = color;

    render_char_at(x, y, c, color);

    if (!use_framebuffer) {
        uint16_t* vm = (uint16_t*)VIDEO_MEMORY;
        if (x < VGA_WIDTH && y < VGA_HEIGHT) {
            vm[y * VGA_WIDTH + x] = (uint16_t)c | ((uint16_t)color << 8);
        }
    }
}

void video_print_at(int x, int y, const char* str, uint8_t color) {
    int cx = x;
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            cx = x;
            y++;
            if (y >= SCREEN_ROWS) break;
            continue;
        }
        video_put_char_at(str[i], color, cx, y);
        cx++;
        if (cx >= SCREEN_COLS) {
            cx = x;
            y++;
            if (y >= SCREEN_ROWS) break;
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
