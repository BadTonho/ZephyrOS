#include "ui/gui.h"
#include "drivers/vesa.h"
#include "drivers/font.h"
#include "core/string.h"
#include "core/log.h"

void gui_init(void) {
    // Inicializacoes futuras (ex: double buffer, etc)
}

void gui_draw_text(uint32_t x, uint32_t y, const char* text, uint32_t color) {
    if (!text) return;
    
    vesa_color_t fg;
    fg.raw = color;

    uint32_t curr_x = x;
    uint32_t curr_y = y;

    while (*text) {
        char c = *text++;
        
        if (c == '\n') {
            curr_x = x;
            curr_y += FONT_HEIGHT;
            continue;
        }

        const uint8_t* glyph = font_get_glyph(c);
        if (glyph) {
            for (uint32_t row = 0; row < FONT_HEIGHT; row++) {
                for (uint32_t col = 0; col < FONT_WIDTH; col++) {
                    if (glyph[row] & (0x80 >> col)) {
                        vesa_put_pixel(curr_x + col, curr_y + row, fg);
                    }
                }
            }
        }
        curr_x += FONT_WIDTH;
    }
}

void gui_draw_panel(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint32_t background, int pressed) {
    if (w < 4 || h < 4) {
        LOG_ERROR("GUI", "Dimensoes invalidas para painel");
        return;
    }

    vesa_color_t bg_color, light_border, dark_border;
    bg_color.raw = background;

    if (pressed) {
        light_border.raw = GUI_COLOR_BORDER_D;
        dark_border.raw = GUI_COLOR_BORDER_L;
    } else {
        light_border.raw = GUI_COLOR_BORDER_L;
        dark_border.raw = GUI_COLOR_BORDER_D;
    }

    vesa_fill_rect(x, y, w, h, bg_color);

    vesa_draw_hline(x, y, w, light_border);
    vesa_draw_hline(x, y + 1, w - 1, light_border);
    vesa_draw_vline(x, y, h, light_border);
    vesa_draw_vline(x + 1, y, h - 1, light_border);

    vesa_draw_hline(x, y + h - 1, w, dark_border);
    vesa_draw_hline(x + 1, y + h - 2, w - 2, dark_border);
    vesa_draw_vline(x + w - 1, y, h, dark_border);
    vesa_draw_vline(x + w - 2, y + 1, h - 2, dark_border);
}

void gui_draw_button(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* text, int pressed) {
    gui_draw_panel(x, y, w, h, GUI_COLOR_BG, pressed);

    // Center text
    if (text) {
        uint32_t text_len = kstrlen(text);
        uint32_t text_w = text_len * FONT_WIDTH;
        uint32_t text_h = FONT_HEIGHT;
        
        int32_t text_x = x + ((int32_t)w - (int32_t)text_w) / 2;
        int32_t text_y = y + ((int32_t)h - (int32_t)text_h) / 2;
        
        if (pressed) {
            text_x++;
            text_y++;
        }
        
        gui_draw_text(text_x, text_y, text, GUI_COLOR_TEXT);
    }
}

void gui_draw_window_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* title, int active) {
    vesa_color_t bg_color, light_border, dark_border, title_bg;
    bg_color.raw = GUI_COLOR_BG;
    light_border.raw = GUI_COLOR_BORDER_L;
    dark_border.raw = GUI_COLOR_BORDER_D;
    
    if (active) {
        title_bg.raw = GUI_COLOR_TITLE_BG; // Dark Blue
    } else {
        title_bg.raw = 0x00808080; // Gray
    }

    // Fill main window background
    vesa_fill_rect(x, y, w, h, bg_color);

    // Draw window 3D borders (outer)
    vesa_draw_hline(x, y, w, light_border);
    vesa_draw_vline(x, y, h, light_border);
    vesa_draw_hline(x, y+h-1, w, dark_border);
    vesa_draw_vline(x+w-1, y, h, dark_border);

    // Draw title bar
    uint32_t title_bar_h = 20;
    vesa_fill_rect(x+2, y+2, w-4, title_bar_h, title_bg);

    // Draw title text
    if (title) {
        gui_draw_text(x+6, y+5, title, GUI_COLOR_TEXT_W);
    }

    // Draw close button
    uint32_t btn_size = 16;
    gui_draw_button(x + w - btn_size - 4, y + 3, btn_size, btn_size, "X", 0);
}
