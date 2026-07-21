#ifndef GUI_H
#define GUI_H

#include "types.h"
#include "drivers/vesa.h"

#define GUI_COLOR_BG       0x00C0C0C0 // Classic Gray
#define GUI_COLOR_BORDER_L 0x00FFFFFF // White highlight
#define GUI_COLOR_BORDER_D 0x00404040 // Dark gray shadow
#define GUI_COLOR_TEXT     0x00000000 // Black
#define GUI_COLOR_TEXT_W   0x00FFFFFF // White
#define GUI_COLOR_TITLE_BG 0x00000080 // Dark Blue for active title

void gui_init(void);

// Primitive text rendering (pixel accurate)
void gui_draw_text(uint32_t x, uint32_t y, const char* text, uint32_t color);

// Button drawing
void gui_draw_button(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* text, int pressed);

// Window frame drawing
void gui_draw_window_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* title, int active);

#endif
