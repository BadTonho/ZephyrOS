#ifndef FONT_H
#define FONT_H

#include "types.h"

#define FONT_WIDTH  8
#define FONT_HEIGHT 16
#define FONT_UI_FAMILY_NAME "Zephyr UI"

typedef struct {
    const uint8_t* glyphs;
    uint16_t width;
    uint16_t height;
    uint16_t row_stride;
    uint16_t glyph_stride;
    uint8_t first_character;
    uint8_t last_character;
} font_face_t;

void font_init(void);
const uint8_t* font_get_glyph(char c);
uint32_t font_get_width(void);
uint32_t font_get_height(void);
int font_get_face(uint16_t width, uint16_t height, const font_face_t** face);
int font_get_face_glyph(const font_face_t* face, char character,
                        const uint8_t** glyph);

#endif
