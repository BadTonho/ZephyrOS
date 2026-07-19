#include "drivers/vesa.h"
#include "core/memory.h"
#include "core/video.h"
#include "core/log.h"
#include "drivers/font.h"
#include "core/panic.h"

static vesa_mode_t current_mode;

static void memset(void* dst, uint8_t val, uint32_t size) {
    uint8_t* d = (uint8_t*)dst;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = val;
    }
}

static uint8_t vesa_info_block[512];
static uint8_t vesa_mode_block[256];

static uint16_t best_mode = 0;
static uint32_t best_width = 0;
static uint32_t best_height = 0;

static void (*bios_call)(void) = 0;

static void try_mode(uint16_t mode, uint32_t width, uint32_t height, uint32_t bpp) {
    uint16_t* mode_info = (uint16_t*)vesa_mode_block;

    mode_info[0] = mode;

    asm volatile(
        "int $0x10"
        :
        : "a"(0x4F01), "c"(mode), "D"(vesa_mode_block)
    );

    uint8_t supported = mode_info[0] & 0x01;
    if (!supported) return;

    uint8_t mem_model = (mode_info[28] >> 4) & 0x0F;
    if (mem_model != 4 && mem_model != 6) return;

    if (width >= best_width && height >= best_height) {
        best_mode = mode;
        best_width = width;
        best_height = height;
    }
}

static void vesa_scan_modes(void) {
    best_mode = 0;
    best_width = 0;
    best_height = 0;

    asm volatile(
        "int $0x10"
        :
        : "a"(0x4F00), "D"(vesa_info_block)
    );

    uint16_t* info = (uint16_t*)vesa_info_block;
    if (info[0] != 0x4F55) return;

    uint32_t mode_ptr = info[2] | ((uint32_t)info[3] << 16);
    uint16_t* modes = (uint16_t*)mode_ptr;

    for (int i = 0; modes[i] != 0xFFFF; i++) {
        uint16_t mode = modes[i] & 0x1FF;

        if (mode == 0x101) try_mode(mode, 640, 480, 32);
        else if (mode == 0x103) try_mode(mode, 800, 600, 32);
        else if (mode == 0x105) try_mode(mode, 1024, 768, 32);
        else if (mode == 0x107) try_mode(mode, 1280, 1024, 32);
        else if (mode == 0x112) try_mode(mode, 640, 480, 32);
        else if (mode == 0x114) try_mode(mode, 800, 600, 32);
        else if (mode == 0x115) try_mode(mode, 1024, 768, 32);
        else if (mode == 0x118) try_mode(mode, 1024, 768, 32);
        else if (mode == 0x11B) try_mode(mode, 1280, 1024, 32);
        else if (mode == 0x164) try_mode(mode, 1280, 1024, 32);
        else if (mode == 0x165) try_mode(mode, 1600, 1200, 32);
        else if (mode == 0x166) try_mode(mode, 1600, 1200, 32);
        else if (mode == 0x167) try_mode(mode, 1600, 1200, 32);
        else if (mode == 0x168) try_mode(mode, 1600, 1200, 32);
        else if (mode == 0x169) try_mode(mode, 1600, 1200, 32);
        else if (mode == 0x16C) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x16D) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x16E) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x16F) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x170) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x171) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x172) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x173) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x174) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x175) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x176) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x177) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x178) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x179) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x17A) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x17B) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x17C) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x17D) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x17E) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x17F) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x180) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x181) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x182) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x183) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x184) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x185) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x186) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x187) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x188) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x189) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x18A) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x18B) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x18C) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x18D) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x18E) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x18F) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x190) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x191) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x192) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x193) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x194) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x195) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x196) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x197) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x198) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x199) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x19A) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x19B) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x19C) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x19D) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x19E) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x19F) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1A0) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1A1) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1A2) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1A3) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1A4) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1A5) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1A6) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1A7) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1A8) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1A9) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1AA) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1AB) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1AC) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1AD) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1AE) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1AF) try_mode(mode, 1920, 1200, 32);
        else if (mode == 0x1B0) try_mode(mode, 1920, 1200, 32);
    }
}

void vesa_init(void) {
    LOG_INFO("VESA", "Inicializando suporte VESA");
    memset(&current_mode, 0, sizeof(vesa_mode_t));

    LOG_WARN("VESA", "BIOS VESA indisponivel em modo protegido");
}

void vesa_set_mode(uint32_t width, uint32_t height, uint32_t bpp) {
    (void)width;
    (void)height;
    (void)bpp;
    LOG_WARN("VESA", "Troca de modo desabilitada sem thunk BIOS");
}

void vesa_put_pixel(uint32_t x, uint32_t y, vesa_color_t color) {
    if (!current_mode.initialized) return;
    if (x >= current_mode.width || y >= current_mode.height) return;

    uint32_t offset = y * (current_mode.pitch / 4) + x;
    current_mode.framebuffer[offset] = color.raw;
}

vesa_color_t vesa_get_pixel(uint32_t x, uint32_t y) {
    vesa_color_t c;
    c.raw = 0;
    if (!current_mode.initialized) return c;
    if (x >= current_mode.width || y >= current_mode.height) return c;

    uint32_t offset = y * (current_mode.pitch / 4) + x;
    c.raw = current_mode.framebuffer[offset];
    return c;
}

void vesa_clear(vesa_color_t color) {
    if (!current_mode.initialized) return;

    uint32_t pixels_per_row = current_mode.pitch / 4;
    for (uint32_t y = 0; y < current_mode.height; y++) {
        for (uint32_t x = 0; x < current_mode.width; x++) {
            current_mode.framebuffer[y * pixels_per_row + x] = color.raw;
        }
    }
}

void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, vesa_color_t color) {
    if (!current_mode.initialized) return;

    for (uint32_t row = y; row < y + h && row < current_mode.height; row++) {
        for (uint32_t col = x; col < x + w && col < current_mode.width; col++) {
            uint32_t offset = row * (current_mode.pitch / 4) + col;
            current_mode.framebuffer[offset] = color.raw;
        }
    }
}

void vesa_draw_hline(uint32_t x, uint32_t y, uint32_t w, vesa_color_t color) {
    for (uint32_t i = 0; i < w; i++) {
        vesa_put_pixel(x + i, y, color);
    }
}

void vesa_draw_vline(uint32_t x, uint32_t y, uint32_t h, vesa_color_t color) {
    for (uint32_t i = 0; i < h; i++) {
        vesa_put_pixel(x, y + i, color);
    }
}

void vesa_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, vesa_color_t color) {
    vesa_draw_hline(x, y, w, color);
    vesa_draw_hline(x, y + h - 1, w, color);
    vesa_draw_vline(x, y, h, color);
    vesa_draw_vline(x + w - 1, y, h, color);
}

void vesa_draw_line(int x0, int y0, int x1, int y1, vesa_color_t color) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int sx = (dx > 0) ? 1 : -1;
    int sy = (dy > 0) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        vesa_put_pixel(x0, y0, color);

        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void vesa_draw_circle(int cx, int cy, int r, vesa_color_t color) {
    int x = r;
    int y = 0;
    int err = 1 - r;

    while (x >= y) {
        vesa_put_pixel(cx + x, cy + y, color);
        vesa_put_pixel(cx + y, cy + x, color);
        vesa_put_pixel(cx - y, cy + x, color);
        vesa_put_pixel(cx - x, cy + y, color);
        vesa_put_pixel(cx - x, cy - y, color);
        vesa_put_pixel(cx - y, cy - x, color);
        vesa_put_pixel(cx + y, cy - x, color);
        vesa_put_pixel(cx + x, cy - y, color);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void vesa_fill_circle(int cx, int cy, int r, vesa_color_t color) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                vesa_put_pixel(cx + x, cy + y, color);
            }
        }
    }
}

void vesa_draw_bitmap(int x, int y, const uint8_t* bitmap, uint32_t w, uint32_t h, vesa_color_t color) {
    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            uint32_t byte_idx = (row * w + col) / 8;
            uint32_t bit_idx = (row * w + col) % 8;
            if (bitmap[byte_idx] & (0x80 >> bit_idx)) {
                vesa_put_pixel(x + col, y + row, color);
            }
        }
    }
}

void vesa_draw_char(int x, int y, char c, vesa_color_t color, uint32_t scale) {
    const uint8_t* glyph = font_get_glyph(c);
    if (!glyph) return;

    for (uint32_t row = 0; row < FONT_HEIGHT; row++) {
        for (uint32_t col = 0; col < FONT_WIDTH; col++) {
            if (glyph[row] & (0x80 >> col)) {
                for (uint32_t sy = 0; sy < scale; sy++) {
                    for (uint32_t sx = 0; sx < scale; sx++) {
                        vesa_put_pixel(x + col * scale + sx, y + row * scale + sy, color);
                    }
                }
            }
        }
    }
}

void vesa_draw_string(int x, int y, const char* str, vesa_color_t color, uint32_t scale) {
    uint32_t offset = 0;
    while (*str) {
        vesa_draw_char(x + offset, y, *str, color, scale);
        offset += FONT_WIDTH * scale;
        str++;
    }
}

vesa_mode_t* vesa_get_mode(void) {
    return &current_mode;
}

uint32_t vesa_rgb(uint8_t r, uint8_t g, uint8_t b) {
    vesa_color_t c;
    c.channels.red = r;
    c.channels.green = g;
    c.channels.blue = b;
    c.channels.alpha = 0xFF;
    return c.raw;
}

uint32_t vesa_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    vesa_color_t c;
    c.channels.red = r;
    c.channels.green = g;
    c.channels.blue = b;
    c.channels.alpha = a;
    return c.raw;
}
