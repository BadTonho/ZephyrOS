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
void gui_draw_scaled_text(uint32_t x, uint32_t y, const char* text,
                          uint32_t color);
int gui_measure_scaled_text(const char* text, uint32_t* width,
                            uint32_t* height);

/* Painel 3D reutilizavel por componentes da interface grafica. */
void gui_draw_panel(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint32_t background, int pressed);

/* Primitivas flat preparadas para a futura interface Modern. */
void gui_draw_rounded_rect(uint32_t x, uint32_t y, uint32_t width,
                           uint32_t height, uint32_t radius,
                           uint32_t color);
void gui_draw_flat_border(uint32_t x, uint32_t y, uint32_t width,
                          uint32_t height, uint32_t color);
void gui_draw_vertical_gradient(uint32_t x, uint32_t y, uint32_t width,
                                uint32_t height, uint32_t top_color,
                                uint32_t bottom_color);

// Button drawing
void gui_draw_button(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* text, int pressed);
void gui_draw_scaled_button(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            const char* text, int pressed);

// Window frame drawing
void gui_draw_window_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* title, int active);
void gui_draw_scaled_window_frame(uint32_t x, uint32_t y, uint32_t w,
                                  uint32_t h, const char* title, int active);

#endif
