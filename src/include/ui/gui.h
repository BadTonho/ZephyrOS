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

#define GUI_MODERN_COLOR_BG              0x001E1E24U
#define GUI_MODERN_COLOR_WINDOW          0x002A2B36U
#define GUI_MODERN_COLOR_ACCENT          0x0000ADB5U
#define GUI_MODERN_COLOR_TEXT            0x00EEEEEEU
#define GUI_MODERN_COLOR_BORDER_INACTIVE 0x004B4D5AU
#define GUI_MODERN_COLOR_HOVER           0x00343746U
#define GUI_MODERN_COLOR_PRESSED         0x00007F86U
#define GUI_MODERN_BUTTON_RADIUS_BASE    4U

typedef enum {
    GUI_THEME_CLASSIC = 0,
    GUI_THEME_MODERN_DARK,
    GUI_THEME_COUNT
} gui_theme_t;

typedef enum {
    GUI_BUTTON_STATE_NORMAL = 0,
    GUI_BUTTON_STATE_HOVER,
    GUI_BUTTON_STATE_PRESSED,
    GUI_BUTTON_STATE_COUNT
} gui_button_state_t;

void gui_init(void);

/* Modern Dark e a aparencia ativa da GUI Classic; o tema legado e reservado. */
int gui_set_theme(gui_theme_t theme);
gui_theme_t gui_get_theme(void);
const char* gui_theme_name(gui_theme_t theme);

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
void gui_draw_modern_button(uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height,
                            const char* text, gui_button_state_t state);

// Window frame drawing
void gui_draw_window_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* title, int active);
void gui_draw_scaled_window_frame(uint32_t x, uint32_t y, uint32_t w,
                                  uint32_t h, const char* title, int active);

#endif
