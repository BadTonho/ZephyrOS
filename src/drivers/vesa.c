#include "drivers/vesa.h"
#include "drivers/font.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/errors.h"
#include "core/string.h"

static vesa_mode_t current_mode;
static uint8_t* backbuffer = NULL;
static uint32_t frame_depth = 0;
static uint8_t frame_dirty = 0;
static uint32_t dirty_x;
static uint32_t dirty_y;
static uint32_t dirty_width;
static uint32_t dirty_height;

static void vesa_accumulate_region(uint32_t x, uint32_t y,
                                   uint32_t width, uint32_t height) {
    uint32_t left;
    uint32_t top;
    uint32_t right;
    uint32_t bottom;
    uint32_t old_right;
    uint32_t old_bottom;

    if (!current_mode.initialized || !width || !height ||
        x >= current_mode.width || y >= current_mode.height) {
        return;
    }

    if (width > current_mode.width - x) width = current_mode.width - x;
    if (height > current_mode.height - y) height = current_mode.height - y;

    right = x + width;
    bottom = y + height;
    if (!frame_dirty) {
        dirty_x = x;
        dirty_y = y;
        dirty_width = width;
        dirty_height = height;
        frame_dirty = 1;
        return;
    }

    left = x < dirty_x ? x : dirty_x;
    top = y < dirty_y ? y : dirty_y;
    old_right = dirty_x + dirty_width;
    old_bottom = dirty_y + dirty_height;
    if (right < old_right) right = old_right;
    if (bottom < old_bottom) bottom = old_bottom;
    dirty_x = left;
    dirty_y = top;
    dirty_width = right - left;
    dirty_height = bottom - top;
}

static void* memset_simple(void* dst, uint8_t val, uint32_t size) {
    uint8_t* d = (uint8_t*)dst;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = val;
    }
    return dst;
}

void vesa_init(uint32_t boot_info_addr) {
    LOG_INFO("VESA", "Inicializando suporte VESA");
    frame_depth = 0;
    frame_dirty = 0;
    if (backbuffer) {
        kfree(backbuffer);
        backbuffer = NULL;
    }
    memset_simple(&current_mode, 0, sizeof(vesa_mode_t));

    if (!boot_info_addr) {
        LOG_ERROR("VESA", "Endereco do boot info nulo");
        return;
    }

    vesa_boot_info_t* boot = (vesa_boot_info_t*)boot_info_addr;

    if (!boot->initialized) {
        LOG_WARN("VESA", "VESA nao disponivel no boot");
        return;
    }

    if (!boot->framebuffer_addr || !boot->width || !boot->height ||
        !boot->pitch || (boot->bpp != 24 && boot->bpp != 32)) {
        LOG_ERROR("VESA", "Parametros de framebuffer invalidos");
        return;
    }

    current_mode.width = boot->width;
    current_mode.height = boot->height;
    current_mode.bpp = boot->bpp;
    current_mode.pitch = boot->pitch;
    current_mode.framebuffer = (uint32_t*)(uint32_t)boot->framebuffer_addr;
    current_mode.initialized = 1;

    LOG_INFO("VESA", "Framebuffer detectado");
}

int vesa_init_backbuffer(void) {
    if (!current_mode.initialized) {
        LOG_WARN("VESA", "Backbuffer solicitado sem modo VESA");
        return ERR_NOT_FOUND;
    }

    if (current_mode.height != 0 &&
        current_mode.pitch > 0xFFFFFFFFU / current_mode.height) {
        LOG_ERROR("VESA", "Tamanho do backbuffer excede o limite");
        return ERR_OVERFLOW;
    }

    if (backbuffer) {
        kfree(backbuffer);
        backbuffer = NULL;
    }

    uint32_t size = current_mode.height * current_mode.pitch;
    backbuffer = (uint8_t*)kmalloc(size);
    if (backbuffer) {
        LOG_INFO("VESA", "Backbuffer alocado com sucesso");
        return OK;
    }

    LOG_ERROR("VESA", "Falha ao alocar backbuffer");
    return ERR_MEM;
}

void vesa_disable(void) {
    if (backbuffer) {
        kfree(backbuffer);
        backbuffer = NULL;
    }
    current_mode.initialized = 0;
    LOG_WARN("VESA", "VESA desabilitado; usando fallback VGA");
}

int vesa_has_backbuffer(void) {
    return backbuffer != NULL;
}

static void vesa_copy_region(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint32_t bytes_per_pixel;
    uint32_t row_bytes;
    uint8_t* source;
    uint8_t* target;

    if (!current_mode.initialized || !backbuffer || !w || !h) return;
    if (x >= current_mode.width || y >= current_mode.height) return;

    if (w > current_mode.width - x) w = current_mode.width - x;
    if (h > current_mode.height - y) h = current_mode.height - y;

    bytes_per_pixel = current_mode.bpp == VESA_BPP_24 ? 3 : 4;
    row_bytes = w * bytes_per_pixel;
    source = backbuffer + y * current_mode.pitch + x * bytes_per_pixel;
    target = (uint8_t*)current_mode.framebuffer +
             y * current_mode.pitch + x * bytes_per_pixel;

    for (uint32_t row = 0; row < h; row++) {
        if (current_mode.bpp == VESA_BPP_32) {
            uint32_t* source32 = (uint32_t*)source;
            uint32_t* target32 = (uint32_t*)target;
            uint32_t pixels = row_bytes / sizeof(uint32_t);
            for (uint32_t col = 0; col < pixels; col++) {
                target32[col] = source32[col];
            }
        } else {
            kmemcpy(target, source, row_bytes);
        }
        source += current_mode.pitch;
        target += current_mode.pitch;
    }
}

void vesa_flip(void) {
    if (frame_depth > 0) return;
    vesa_copy_region(0, 0, current_mode.width, current_mode.height);
}

void vesa_flip_region(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (frame_depth > 0) return;
    vesa_copy_region(x, y, w, h);
}

void vesa_frame_begin(void) {
    vesa_frame_begin_region(0, 0, current_mode.width, current_mode.height);
}

void vesa_frame_begin_region(uint32_t x, uint32_t y,
                             uint32_t width, uint32_t height) {
    if (frame_depth == 0) frame_dirty = 0;
    frame_depth++;
    vesa_accumulate_region(x, y, width, height);
}

void vesa_frame_mark_region(uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height) {
    if (frame_depth == 0) return;
    vesa_accumulate_region(x, y, width, height);
}

void vesa_frame_end(void) {
    if (frame_depth == 0) {
        LOG_ERROR("VESA", "Finalizacao de frame sem inicio correspondente");
        return;
    }

    frame_depth--;
    if (frame_depth == 0 && frame_dirty) {
        vesa_copy_region(dirty_x, dirty_y, dirty_width, dirty_height);
        frame_dirty = 0;
    }
}

void vesa_set_mode(uint32_t width, uint32_t height, uint32_t bpp) {
    (void)width;
    (void)height;
    (void)bpp;
    LOG_WARN("VESA", "Troca de modo desabilitada em runtime");
}

void vesa_put_pixel(uint32_t x, uint32_t y, vesa_color_t color) {
    if (!current_mode.initialized) return;
    if (x >= current_mode.width || y >= current_mode.height) return;

    uint8_t* target = backbuffer ? backbuffer : (uint8_t*)current_mode.framebuffer;
    uint8_t* row = target + y * current_mode.pitch;
    if (current_mode.bpp == 24) {
        uint8_t* pixel = row + x * 3;
        pixel[0] = color.channels.blue;
        pixel[1] = color.channels.green;
        pixel[2] = color.channels.red;
        return;
    }

    ((uint32_t*)row)[x] = color.raw;
}

void vesa_draw_glyph8x16(uint32_t x, uint32_t y, const uint8_t* glyph,
                          vesa_color_t color) {
    uint8_t* target;
    uint32_t visible_width;
    uint32_t visible_height;

    if (!glyph || !current_mode.initialized ||
        x >= current_mode.width || y >= current_mode.height) {
        return;
    }

    visible_width = current_mode.width - x;
    if (visible_width > FONT_WIDTH) visible_width = FONT_WIDTH;
    visible_height = current_mode.height - y;
    if (visible_height > FONT_HEIGHT) visible_height = FONT_HEIGHT;
    target = backbuffer ? backbuffer : (uint8_t*)current_mode.framebuffer;

    for (uint32_t row = 0; row < visible_height; row++) {
        uint8_t bits = glyph[row];

        if (current_mode.bpp == VESA_BPP_24) {
            uint8_t* pixels = target + (y + row) * current_mode.pitch + x * 3U;
            for (uint32_t col = 0; col < visible_width; col++) {
                if (bits & (0x80U >> col)) {
                    uint8_t* pixel = pixels + col * 3U;
                    pixel[0] = color.channels.blue;
                    pixel[1] = color.channels.green;
                    pixel[2] = color.channels.red;
                }
            }
        } else {
            uint32_t* pixels = (uint32_t*)(target +
                (y + row) * current_mode.pitch + x * 4U);
            for (uint32_t col = 0; col < visible_width; col++) {
                if (bits & (0x80U >> col)) pixels[col] = color.raw;
            }
        }
    }
}

vesa_color_t vesa_get_pixel(uint32_t x, uint32_t y) {
    vesa_color_t c;
    c.raw = 0;
    if (!current_mode.initialized) return c;
    if (x >= current_mode.width || y >= current_mode.height) return c;

    uint8_t* target = backbuffer ? backbuffer : (uint8_t*)current_mode.framebuffer;
    uint8_t* row = target + y * current_mode.pitch;
    if (current_mode.bpp == 24) {
        uint8_t* pixel = row + x * 3;
        c.channels.blue = pixel[0];
        c.channels.green = pixel[1];
        c.channels.red = pixel[2];
        c.channels.alpha = 0xFF;
        return c;
    }

    c.raw = ((uint32_t*)row)[x];
    return c;
}

void vesa_clear(vesa_color_t color) {
    if (!current_mode.initialized) return;

    uint8_t* target = backbuffer ? backbuffer : (uint8_t*)current_mode.framebuffer;
    for (uint32_t y = 0; y < current_mode.height; y++) {
        uint8_t* row = target + y * current_mode.pitch;
        if (current_mode.bpp == 24) {
            for (uint32_t x = 0; x < current_mode.width; x++) {
                uint8_t* pixel = row + x * 3;
                pixel[0] = color.channels.blue;
                pixel[1] = color.channels.green;
                pixel[2] = color.channels.red;
            }
        } else {
            uint32_t* pixels = (uint32_t*)row;
            for (uint32_t x = 0; x < current_mode.width; x++) {
                pixels[x] = color.raw;
            }
        }
    }
}

void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, vesa_color_t color) {
    uint8_t* target;

    if (!current_mode.initialized || !w || !h) return;
    if (x >= current_mode.width || y >= current_mode.height) return;

    if (w > current_mode.width - x) w = current_mode.width - x;
    if (h > current_mode.height - y) h = current_mode.height - y;

    target = backbuffer ? backbuffer : (uint8_t*)current_mode.framebuffer;

    if (current_mode.bpp == VESA_BPP_24) {
        for (uint32_t row = 0; row < h; row++) {
            uint8_t* pixel = target + (y + row) * current_mode.pitch + x * 3;
            for (uint32_t col = 0; col < w; col++) {
                pixel[0] = color.channels.blue;
                pixel[1] = color.channels.green;
                pixel[2] = color.channels.red;
                pixel += 3;
            }
        }
        return;
    }

    for (uint32_t row = 0; row < h; row++) {
        uint32_t* pixels = (uint32_t*)(target +
                         (y + row) * current_mode.pitch + x * 4);
        for (uint32_t col = 0; col < w; col++) {
            pixels[col] = color.raw;
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
