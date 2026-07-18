#ifndef VESA_H
#define VESA_H

#include "types.h"

#define VESA_WIDTH_640   640
#define VESA_HEIGHT_480  480
#define VESA_WIDTH_800   800
#define VESA_HEIGHT_600  600
#define VESA_WIDTH_1024  1024
#define VESA_HEIGHT_768  768
#define VESA_WIDTH_1280  1280
#define VESA_HEIGHT_720  720
#define VESA_WIDTH_1920  1920
#define VESA_HEIGHT_1080 1080

#define VESA_BPP_16  16
#define VESA_BPP_24  24
#define VESA_BPP_32  32

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint32_t* framebuffer;
    uint8_t  initialized;
} vesa_mode_t;

typedef struct {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t alpha;
} __attribute__((packed)) vesa_pixel_t;

typedef union {
    uint32_t raw;
    vesa_pixel_t channels;
} vesa_color_t;

void vesa_init(void);
void vesa_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
void vesa_put_pixel(uint32_t x, uint32_t y, vesa_color_t color);
vesa_color_t vesa_get_pixel(uint32_t x, uint32_t y);
void vesa_clear(vesa_color_t color);
void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, vesa_color_t color);
void vesa_draw_hline(uint32_t x, uint32_t y, uint32_t w, vesa_color_t color);
void vesa_draw_vline(uint32_t x, uint32_t y, uint32_t h, vesa_color_t color);
void vesa_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, vesa_color_t color);
void vesa_draw_line(int x0, int y0, int x1, int y1, vesa_color_t color);
void vesa_draw_circle(int cx, int cy, int r, vesa_color_t color);
void vesa_fill_circle(int cx, int cy, int r, vesa_color_t color);
void vesa_draw_char(int x, int y, char c, vesa_color_t color, uint32_t scale);
void vesa_draw_string(int x, int y, const char* str, vesa_color_t color, uint32_t scale);
void vesa_draw_bitmap(int x, int y, const uint8_t* bitmap, uint32_t w, uint32_t h, vesa_color_t color);

vesa_mode_t* vesa_get_mode(void);
uint32_t vesa_rgb(uint8_t r, uint8_t g, uint8_t b);
uint32_t vesa_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

#endif
