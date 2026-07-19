#include "fs/bmp.h"
#include "core/memory.h"
#include "core/video.h"

static void memset(void* dst, uint8_t val, uint32_t size) {
    uint8_t* d = (uint8_t*)dst;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = val;
    }
}

static void memcpy(void* dst, const void* src, uint32_t size) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

static uint16_t read_u16(const uint8_t* p) {
    return p[0] | (p[1] << 8);
}

static uint32_t read_u32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static int32_t read_i32(const uint8_t* p) {
    return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

void bmp_init(void) {
}

int bmp_load(const uint8_t* raw_data, uint32_t size, bmp_image_t* out) {
    if (!raw_data || !out || size < 54) return -1;

    memset(out, 0, sizeof(bmp_image_t));

    if (raw_data[0] != 'B' || raw_data[1] != 'M') {
        return -1;
    }

    out->file_header.signature[0] = raw_data[0];
    out->file_header.signature[1] = raw_data[1];
    out->file_header.file_size = read_u32(raw_data + 2);
    out->file_header.reserved1 = read_u16(raw_data + 6);
    out->file_header.reserved2 = read_u16(raw_data + 8);
    out->file_header.data_offset = read_u32(raw_data + 10);

    out->info_header.header_size = read_u32(raw_data + 14);
    out->info_header.width = read_i32(raw_data + 18);
    out->info_header.height = read_i32(raw_data + 22);
    out->info_header.planes = read_u16(raw_data + 26);
    out->info_header.bits_per_pixel = read_u16(raw_data + 28);
    out->info_header.compression = read_u32(raw_data + 30);
    out->info_header.image_size = read_u32(raw_data + 34);
    out->info_header.x_ppm = read_i32(raw_data + 38);
    out->info_header.y_ppm = read_i32(raw_data + 42);
    out->info_header.colors_used = read_u32(raw_data + 46);
    out->info_header.colors_important = read_u32(raw_data + 50);

    out->width = (uint32_t)out->info_header.width;
    out->height = out->info_header.height > 0 ? (uint32_t)out->info_header.height : (uint32_t)(-out->info_header.height);
    out->bpp = out->info_header.bits_per_pixel;

    if (out->info_header.compression != 0) return -1;
    if (out->bpp != 24 && out->bpp != 8 && out->bpp != 4 && out->bpp != 1) return -1;

    uint32_t color_table_size = 0;
    if (out->bpp <= 8) {
        color_table_size = (1 << out->bpp) * 4;
        out->color_table = (bmp_color_table_t*)kmalloc(color_table_size);
        if (!out->color_table) return -1;
        memcpy(out->color_table, raw_data + 54, color_table_size);
    }

    uint32_t data_offset = out->file_header.data_offset;
    uint32_t row_size = ((out->bpp * out->width + 31) / 32) * 4;
    uint32_t pixel_data_size = row_size * out->height;

    out->pixel_data = (uint8_t*)kmalloc(pixel_data_size);
    if (!out->pixel_data) {
        if (out->color_table) kfree(out->color_table);
        return -1;
    }

    memcpy(out->pixel_data, raw_data + data_offset, pixel_data_size);

    out->initialized = 1;
    return 0;
}

static vesa_color_t bmp_get_pixel(bmp_image_t* img, uint32_t x, uint32_t y) {
    vesa_color_t color;
    color.raw = 0;

    uint32_t row_size = ((img->bpp * img->width + 31) / 32) * 4;

    uint32_t actual_y;
    if (img->info_header.height > 0) {
        actual_y = img->height - 1 - y;
    } else {
        actual_y = y;
    }

    if (img->bpp == 24) {
        uint32_t offset = actual_y * row_size + x * 3;
        color.channels.blue = img->pixel_data[offset];
        color.channels.green = img->pixel_data[offset + 1];
        color.channels.red = img->pixel_data[offset + 2];
        color.channels.alpha = 0xFF;
    }
    else if (img->bpp == 8) {
        uint32_t offset = actual_y * row_size + x;
        uint8_t idx = img->pixel_data[offset];
        if (img->color_table) {
            color.channels.blue = img->color_table[idx].blue;
            color.channels.green = img->color_table[idx].green;
            color.channels.red = img->color_table[idx].red;
        }
        color.channels.alpha = 0xFF;
    }
    else if (img->bpp == 4) {
        uint32_t offset = actual_y * row_size + x / 2;
        uint8_t byte = img->pixel_data[offset];
        uint8_t idx;
        if (x & 1) {
            idx = byte & 0x0F;
        } else {
            idx = (byte >> 4) & 0x0F;
        }
        if (img->color_table) {
            color.channels.blue = img->color_table[idx].blue;
            color.channels.green = img->color_table[idx].green;
            color.channels.red = img->color_table[idx].red;
        }
        color.channels.alpha = 0xFF;
    }
    else if (img->bpp == 1) {
        uint32_t offset = actual_y * row_size + x / 8;
        uint8_t byte = img->pixel_data[offset];
        uint8_t bit = 7 - (x % 8);
        uint8_t idx = (byte >> bit) & 1;
        if (img->color_table) {
            color.channels.blue = img->color_table[idx].blue;
            color.channels.green = img->color_table[idx].green;
            color.channels.red = img->color_table[idx].red;
        }
        color.channels.alpha = 0xFF;
    }

    return color;
}

void bmp_draw(bmp_image_t* img, int x, int y) {
    if (!img || !img->initialized) return;

    for (uint32_t row = 0; row < img->height; row++) {
        for (uint32_t col = 0; col < img->width; col++) {
            vesa_color_t color = bmp_get_pixel(img, col, row);
            vesa_put_pixel(x + col, y + row, color);
        }
    }
}

void bmp_draw_scaled(bmp_image_t* img, int x, int y, uint32_t scale) {
    if (!img || !img->initialized) return;

    for (uint32_t row = 0; row < img->height; row++) {
        for (uint32_t col = 0; col < img->width; col++) {
            vesa_color_t color = bmp_get_pixel(img, col, row);
            for (uint32_t sy = 0; sy < scale; sy++) {
                for (uint32_t sx = 0; sx < scale; sx++) {
                    vesa_put_pixel(x + col * scale + sx, y + row * scale + sy, color);
                }
            }
        }
    }
}

void bmp_free(bmp_image_t* out) {
    if (!out) return;

    if (out->pixel_data) {
        kfree(out->pixel_data);
        out->pixel_data = 0;
    }
    if (out->color_table) {
        kfree(out->color_table);
        out->color_table = 0;
    }
    out->initialized = 0;
}
