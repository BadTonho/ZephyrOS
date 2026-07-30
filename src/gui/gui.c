#include "ui/gui.h"
#include "drivers/vesa.h"
#include "drivers/font.h"
#include "core/string.h"
#include "core/log.h"
#include "core/errors.h"
#include "ui/display.h"

#define GUI_BITMAP_BYTE_BITS 8U

static int gui_clamp_to_screen(uint32_t* x, uint32_t* y,
                               uint32_t* width, uint32_t* height) {
    vesa_mode_t* mode = vesa_get_mode();

    if (!x || !y || !width || !height || !mode || !mode->initialized ||
        !*width || !*height || *x >= mode->width || *y >= mode->height) {
        return 0;
    }
    if (*width > mode->width - *x) *width = mode->width - *x;
    if (*height > mode->height - *y) *height = mode->height - *y;
    return *width && *height;
}

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
        if (glyph) vesa_draw_glyph8x16(curr_x, curr_y, glyph, fg);
        curr_x += FONT_WIDTH;
    }
}

static void gui_draw_resampled_glyph(uint32_t x, uint32_t y,
                                     const uint8_t* glyph,
                                     vesa_color_t color,
                                     const display_metrics_t* metrics) {
    for (uint32_t row = 0; row < metrics->font_height; row++) {
        uint32_t source_row = row * FONT_HEIGHT / metrics->font_height;
        uint8_t bits = glyph[source_row];

        for (uint32_t col = 0; col < metrics->font_width; col++) {
            uint32_t source_col = col * FONT_WIDTH / metrics->font_width;
            if (bits & (0x80U >> source_col)) {
                vesa_put_pixel(x + col, y + row, color);
            }
        }
    }
}

static void gui_draw_native_glyph(uint32_t x, uint32_t y,
                                  const uint8_t* glyph,
                                  vesa_color_t color,
                                  const font_face_t* face) {
    for (uint32_t row = 0; row < face->height; row++) {
        const uint8_t* bits = glyph + row * face->row_stride;

        for (uint32_t col = 0; col < face->width; col++) {
            uint32_t byte_index = col / GUI_BITMAP_BYTE_BITS;
            uint32_t bit_index = col % GUI_BITMAP_BYTE_BITS;
            if (bits[byte_index] & (0x80U >> bit_index)) {
                vesa_put_pixel(x + col, y + row, color);
            }
        }
    }
}

void gui_draw_scaled_text(uint32_t x, uint32_t y, const char* text,
                          uint32_t color) {
    display_metrics_t metrics;
    const font_face_t* face;
    vesa_color_t foreground;
    uint32_t current_x = x;
    uint32_t current_y = y;
    int native_face_available;

    if (!text) {
        LOG_ERROR("GUI", "Texto escalado nulo");
        return;
    }
    if (display_get_metrics(&metrics) != OK || !metrics.available) {
        gui_draw_text(x, y, text, color);
        return;
    }

    native_face_available =
        font_get_face(metrics.font_width, metrics.font_height, &face) == OK;
    foreground.raw = color;
    while (*text) {
        char character = *text++;
        const uint8_t* glyph;

        if (character == '\n') {
            current_x = x;
            current_y += metrics.font_height;
            continue;
        }
        if (native_face_available) {
            if (font_get_face_glyph(face, character, &glyph) == OK) {
                gui_draw_native_glyph(current_x, current_y, glyph, foreground,
                                      face);
            } else {
                native_face_available = 0;
            }
        }
        if (!native_face_available) {
            glyph = font_get_glyph(character);
            if (glyph) {
                gui_draw_resampled_glyph(current_x, current_y, glyph,
                                         foreground, &metrics);
            }
        }
        current_x += metrics.font_width;
    }
}

int gui_measure_scaled_text(const char* text, uint32_t* width,
                            uint32_t* height) {
    display_metrics_t metrics;
    uint32_t line_width = 0;
    uint32_t max_width = 0;
    uint32_t line_count = 1;

    if (!text || !width || !height) {
        LOG_ERROR("GUI", "Entrada nula ao medir texto escalado");
        return ERR_NULL;
    }
    if (display_get_metrics(&metrics) != OK) return ERR_STATE;

    while (*text) {
        if (*text++ == '\n') {
            if (line_width > max_width) max_width = line_width;
            line_width = 0;
            line_count++;
        } else {
            line_width += metrics.font_width;
        }
    }
    if (line_width > max_width) max_width = line_width;
    *width = max_width;
    *height = line_count * metrics.font_height;
    return OK;
}

void gui_draw_panel(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint32_t background, int pressed) {
    if (!gui_clamp_to_screen(&x, &y, &w, &h)) return;
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
    if (!gui_clamp_to_screen(&x, &y, &w, &h)) return;
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

void gui_draw_scaled_button(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            const char* text, int pressed) {
    uint32_t text_width;
    uint32_t text_height;
    int32_t text_x;
    int32_t text_y;

    if (!gui_clamp_to_screen(&x, &y, &w, &h)) return;
    gui_draw_panel(x, y, w, h, GUI_COLOR_BG, pressed);
    if (!text) return;
    if (gui_measure_scaled_text(text, &text_width, &text_height) != OK) return;

    text_x = (int32_t)x + ((int32_t)w - (int32_t)text_width) / 2;
    text_y = (int32_t)y + ((int32_t)h - (int32_t)text_height) / 2;
    if (pressed) {
        text_x++;
        text_y++;
    }
    if (text_x < 0 || text_y < 0) {
        LOG_WARN("GUI", "Texto escalado nao cabe no botao");
        return;
    }
    gui_draw_scaled_text((uint32_t)text_x, (uint32_t)text_y, text,
                         GUI_COLOR_TEXT);
}

void gui_draw_scaled_window_frame(uint32_t x, uint32_t y, uint32_t w,
                                  uint32_t h, const char* title, int active) {
    display_metrics_t metrics;
    vesa_color_t background;
    vesa_color_t light_border;
    vesa_color_t dark_border;
    vesa_color_t title_background;
    uint32_t inset;
    uint32_t title_y;
    uint32_t button_x;

    if (!gui_clamp_to_screen(&x, &y, &w, &h) || w < 20 || h < 20) return;
    if (display_get_metrics(&metrics) != OK) return;

    inset = display_scale_px(2);
    background.raw = GUI_COLOR_BG;
    light_border.raw = GUI_COLOR_BORDER_L;
    dark_border.raw = GUI_COLOR_BORDER_D;
    title_background.raw = active ? GUI_COLOR_TITLE_BG : 0x00606060;

    vesa_fill_rect(x, y, w, h, background);
    vesa_draw_hline(x, y, w, light_border);
    vesa_draw_vline(x, y, h, light_border);
    vesa_draw_hline(x, y + h - 1, w, dark_border);
    vesa_draw_vline(x + w - 1, y, h, dark_border);
    vesa_fill_rect(x + inset, y + inset, w - inset * 2,
                   metrics.title_bar_height, title_background);

    title_y = y + inset +
              (metrics.title_bar_height - metrics.font_height) / 2;
    if (title) {
        gui_draw_scaled_text(x + metrics.spacing, title_y, title,
                             GUI_COLOR_TEXT_W);
    }
    button_x = x + w - inset - metrics.title_bar_height;
    gui_draw_scaled_button(button_x, y + inset, metrics.title_bar_height,
                           metrics.title_bar_height, "X", 0);
}

void gui_draw_window_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* title, int active) {
    vesa_color_t bg_color, light_border, dark_border, title_bg;

    if (!gui_clamp_to_screen(&x, &y, &w, &h) || w < 20 || h < 20) return;
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
