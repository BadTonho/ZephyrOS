#ifndef BMP_H
#define BMP_H

#include "types.h"
#include "drivers/vesa.h"

typedef struct {
    char     signature[2];
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t data_offset;
} __attribute__((packed)) bmp_file_header_t;

typedef struct {
    uint32_t header_size;
    int32_t  width;
    int32_t  height;
    uint16_t planes;
    uint16_t bits_per_pixel;
    uint32_t compression;
    uint32_t image_size;
    int32_t  x_ppm;
    int32_t  y_ppm;
    uint32_t colors_used;
    uint32_t colors_important;
} __attribute__((packed)) bmp_info_header_t;

typedef struct {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t reserved;
} __attribute__((packed)) bmp_color_table_t;

typedef struct {
    bmp_file_header_t file_header;
    bmp_info_header_t info_header;
    bmp_color_table_t* color_table;
    uint8_t* pixel_data;
    uint32_t width;
    uint32_t height;
    uint16_t bpp;
    uint8_t  initialized;
} bmp_image_t;

void bmp_init(void);
int  bmp_load(const uint8_t* raw_data, uint32_t size, bmp_image_t* out);
void bmp_draw(bmp_image_t* img, int x, int y);
void bmp_draw_scaled(bmp_image_t* img, int x, int y, uint32_t scale);
void bmp_free(bmp_image_t* out);

#endif
