#ifndef FONT_H
#define FONT_H

#include "types.h"

#define FONT_WIDTH  8
#define FONT_HEIGHT 16

void font_init(void);
const uint8_t* font_get_glyph(char c);
uint32_t font_get_width(void);
uint32_t font_get_height(void);

#endif
