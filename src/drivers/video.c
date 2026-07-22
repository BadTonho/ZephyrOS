#include "core/video.h"
#include "core/spinlock.h"
#include "drivers/vesa.h"
#include "drivers/font.h"
#include "drivers/mouse.h"
#include "core/log.h"

#define TERMINAL_TAB_WIDTH 8
#define TERMINAL_RESERVED_ROWS 2 /* rodape e margem acima da taskbar */
#define TERMINAL_FOOTER_COLOR 0x08
#define TERMINAL_FOOTER_SIZE 96

spinlock_t video_lock;
static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t current_color = 0x07;
static uint8_t use_framebuffer = 0;
static uint32_t video_update_depth = 0;

static char text_buffer[SCREEN_COLS * SCREEN_ROWS];
static uint8_t color_buffer[SCREEN_COLS * SCREEN_ROWS];

/* O historico pertence somente ao terminal; interfaces desenhadas por
   coordenadas continuam usando exclusivamente o buffer visual. */
static char terminal_text[VIDEO_SCROLLBACK_LINES * SCREEN_COLS];
static uint8_t terminal_color[VIDEO_SCROLLBACK_LINES * SCREEN_COLS];
static uint32_t terminal_oldest_line = 0;
static uint32_t terminal_line_count = 1;
static uint32_t terminal_current_line = 0;
static uint32_t terminal_view_offset = 0;
static int terminal_cursor_x = 0;
static uint8_t terminal_active = 0;

static const uint32_t vga_palette[16] = {
    0x00000000,
    0x00AA0000,
    0x0000AA00,
    0x00AAAA00,
    0x000000AA,
    0x00AA00AA,
    0x000055AA,
    0x00AAAAAA,
    0x00555555,
    0x005555FF,
    0x0055FF55,
    0x0055FFFF,
    0x00FF5555,
    0x00FF55FF,
    0x00FFFF55,
    0x00FFFFFF
};

static uint32_t vga_color_to_rgb(uint8_t vga_color) {
    uint8_t fg = vga_color & 0x0F;
    uint8_t bg = (vga_color >> 4) & 0x0F;
    (void)bg;
    return vga_palette[fg];
}

static uint32_t vga_bg_to_rgb(uint8_t vga_color) {
    uint8_t bg = (vga_color >> 4) & 0x0F;
    return vga_palette[bg];
}

static int video_can_batch_updates(void) {
    vesa_mode_t* mode = vesa_get_mode();

    return use_framebuffer && mode && mode->initialized &&
           vesa_has_backbuffer();
}

static void video_present_region(int x, int y, int width, int height) {
    if (!use_framebuffer || width <= 0 || height <= 0) return;

    vesa_flip_region((uint32_t)x, (uint32_t)y,
                     (uint32_t)width, (uint32_t)height);
}

static void render_char_at(int x, int y, char c, uint8_t color) {
    if (!use_framebuffer) return;

    vesa_mode_t* mode = vesa_get_mode();
    if (!mode || !mode->initialized) return;

    int pixel_x = x * FONT_WIDTH;
    int pixel_y = y * FONT_HEIGHT;

    vesa_frame_mark_region((uint32_t)pixel_x, (uint32_t)pixel_y,
                           FONT_WIDTH, FONT_HEIGHT);

    vesa_color_t bg_color;
    bg_color.raw = vga_bg_to_rgb(color);
    vesa_fill_rect(pixel_x, pixel_y, FONT_WIDTH, FONT_HEIGHT, bg_color);

    if (c == ' ' || c == '\0') return;

    vesa_color_t fg_color;
    fg_color.raw = vga_color_to_rgb(color);
    const uint8_t* glyph = font_get_glyph(c);
    if (!glyph) return;

    for (uint32_t row = 0; row < FONT_HEIGHT; row++) {
        for (uint32_t col = 0; col < FONT_WIDTH; col++) {
            if (glyph[row] & (0x80 >> col)) {
                vesa_put_pixel(pixel_x + col, pixel_y + row, fg_color);
            }
        }
    }
}

static void render_output_cell_locked(int x, int y, char c, uint8_t color) {
    if (x < 0 || x >= SCREEN_COLS || y < 0 || y >= SCREEN_ROWS) return;

    int index = y * SCREEN_COLS + x;
    text_buffer[index] = c;
    color_buffer[index] = color;

    if (use_framebuffer) {
        render_char_at(x, y, c, color);
        return;
    }

    if (x < VGA_WIDTH && y < VGA_HEIGHT) {
        uint16_t* vm = (uint16_t*)VIDEO_MEMORY;
        vm[y * VGA_WIDTH + x] = (uint16_t)c | ((uint16_t)color << 8);
    }
}

static void update_cursor(void) {
    if (use_framebuffer) return;

    int vga_x = cursor_x;
    int vga_y = cursor_y;
    if (vga_x >= VGA_WIDTH) vga_x = VGA_WIDTH - 1;
    if (vga_y >= VGA_HEIGHT) vga_y = VGA_HEIGHT - 1;
    if (vga_x < 0) vga_x = 0;
    if (vga_y < 0) vga_y = 0;

    uint16_t pos = vga_y * VGA_WIDTH + vga_x;
    asm volatile("outb %0, %1" : : "a"((uint8_t)(pos & 0xFF)), "Nd"((uint16_t)0x3D4));
    asm volatile("outb %0, %1" : : "a"((uint8_t)((pos >> 8) & 0xFF)), "Nd"((uint16_t)0x3D5));
    asm volatile("outb %0, %1" : : "a"((uint8_t)0x0E), "Nd"((uint16_t)0x3D4));
    asm volatile("outb %0, %1" : : "a"((uint8_t)((pos >> 8) & 0xFF)), "Nd"((uint16_t)0x3D5));
}

static int terminal_columns(void) {
    return use_framebuffer ? SCREEN_COLS : VGA_WIDTH;
}

static int terminal_content_rows(void) {
    int rows = use_framebuffer ? SCREEN_ROWS : VGA_HEIGHT;

    if (rows <= TERMINAL_RESERVED_ROWS) return 1;
    return rows - 1;
}

static int terminal_view_rows(void) {
    int rows = terminal_content_rows() - TERMINAL_RESERVED_ROWS;
    return rows > 0 ? rows : 1;
}

static uint32_t terminal_physical_line(uint32_t logical_line) {
    return (terminal_oldest_line + logical_line) % VIDEO_SCROLLBACK_LINES;
}

static uint32_t terminal_max_view_offset(void) {
    uint32_t rows = (uint32_t)terminal_view_rows();
    return terminal_line_count > rows ? terminal_line_count - rows : 0;
}

static uint32_t terminal_view_start_line(void) {
    uint32_t rows = (uint32_t)terminal_view_rows();

    if (terminal_line_count <= rows) return 0;
    return terminal_line_count - rows - terminal_view_offset;
}

static void terminal_clear_line_locked(uint32_t line, uint8_t color) {
    uint32_t start = line * SCREEN_COLS;

    for (int x = 0; x < SCREEN_COLS; x++) {
        terminal_text[start + x] = ' ';
        terminal_color[start + x] = color;
    }
}

static void terminal_reset_locked(void) {
    for (uint32_t line = 0; line < VIDEO_SCROLLBACK_LINES; line++) {
        terminal_clear_line_locked(line, current_color);
    }

    terminal_oldest_line = 0;
    terminal_line_count = 1;
    terminal_current_line = 0;
    terminal_view_offset = 0;
    terminal_cursor_x = 0;
}

static void terminal_append_number(char* text, int* pos, uint32_t value) {
    char digits[16];
    int count = 0;

    if (value == 0) {
        text[(*pos)++] = '0';
        return;
    }

    while (value > 0 && count < (int)sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count > 0) text[(*pos)++] = digits[--count];
}

static void terminal_draw_footer_locked(int row, int columns) {
    char footer[TERMINAL_FOOTER_SIZE];
    int pos = 0;

    for (int x = 0; x < columns; x++) {
        render_output_cell_locked(x, row, ' ', current_color);
    }

    if (terminal_view_offset == 0) return;

    footer[pos++] = 'H'; footer[pos++] = 'i'; footer[pos++] = 's';
    footer[pos++] = 't'; footer[pos++] = ':'; footer[pos++] = ' ';
    terminal_append_number(footer, &pos, terminal_view_offset);
    footer[pos++] = '/';
    terminal_append_number(footer, &pos, terminal_max_view_offset());
    footer[pos++] = ' '; footer[pos++] = 'U'; footer[pos++] = 'p';
    footer[pos++] = '/'; footer[pos++] = 'D'; footer[pos++] = 'o';
    footer[pos++] = 'w'; footer[pos++] = 'n'; footer[pos++] = ' ';
    footer[pos++] = 'P'; footer[pos++] = 'g'; footer[pos++] = 'U';
    footer[pos++] = 'p'; footer[pos++] = '/'; footer[pos++] = 'P';
    footer[pos++] = 'g'; footer[pos++] = 'D'; footer[pos++] = 'n';
    footer[pos++] = ' '; footer[pos++] = 'H'; footer[pos++] = 'o';
    footer[pos++] = 'm'; footer[pos++] = 'e'; footer[pos++] = '/';
    footer[pos++] = 'E'; footer[pos++] = 'n'; footer[pos++] = 'd';

    if (pos >= TERMINAL_FOOTER_SIZE) pos = TERMINAL_FOOTER_SIZE - 1;
    footer[pos] = '\0';
    for (int x = 0; x < pos && x < columns; x++) {
        render_output_cell_locked(x, row, footer[x], TERMINAL_FOOTER_COLOR);
    }
}

static void terminal_render_view_locked(void) {
    int columns = terminal_columns();
    int rows = terminal_view_rows();
    int content_rows = terminal_content_rows();
    uint32_t start_line = terminal_view_start_line();
    uint32_t available = terminal_line_count - start_line;
    if (available > (uint32_t)rows) available = (uint32_t)rows;

    for (int y = 0; y < rows; y++) {
        uint32_t source = y < (int)available ?
            terminal_physical_line(start_line + (uint32_t)y) : 0;
        uint32_t source_index = source * SCREEN_COLS;

        for (int x = 0; x < columns; x++) {
            char c = y < (int)available ? terminal_text[source_index + x] : ' ';
            uint8_t color = y < (int)available ? terminal_color[source_index + x] : current_color;
            render_output_cell_locked(x, y, c, color);
        }
    }

    terminal_draw_footer_locked(rows, columns);
    for (int y = rows + 1; y < content_rows; y++) {
        for (int x = 0; x < columns; x++) {
            render_output_cell_locked(x, y, ' ', current_color);
        }
    }
    if (terminal_view_offset == 0) {
        cursor_x = terminal_cursor_x;
        cursor_y = terminal_line_count > (uint32_t)rows ? rows - 1 :
                   (int)terminal_line_count - 1;
    }
}

static int terminal_advance_line_locked(void) {
    int redraw_tail = 0;

    if (terminal_line_count < VIDEO_SCROLLBACK_LINES) {
        terminal_current_line = terminal_physical_line(terminal_line_count);
        terminal_line_count++;
    } else {
        terminal_oldest_line = (terminal_oldest_line + 1) % VIDEO_SCROLLBACK_LINES;
        terminal_current_line = (terminal_current_line + 1) % VIDEO_SCROLLBACK_LINES;
        if (terminal_view_offset > 0 &&
            terminal_view_offset < terminal_max_view_offset()) {
            terminal_view_offset++;
        }
    }

    terminal_clear_line_locked(terminal_current_line, current_color);
    terminal_cursor_x = 0;
    if (terminal_view_offset == 0 &&
        terminal_line_count > (uint32_t)terminal_view_rows()) {
        redraw_tail = 1;
    }
    return redraw_tail;
}

static void terminal_write_char_locked(char c, uint8_t color, int* dirty_x,
                                       int* dirty_y, int* redraw_tail) {
    int columns = terminal_columns();
    uint32_t index = terminal_current_line * SCREEN_COLS;

    if (c == '\n') {
        *redraw_tail = terminal_advance_line_locked();
        return;
    }
    if (c == '\r') {
        terminal_cursor_x = 0;
        return;
    }
    if (c == '\t') {
        terminal_cursor_x = (terminal_cursor_x + TERMINAL_TAB_WIDTH) &
                            ~(TERMINAL_TAB_WIDTH - 1);
        if (terminal_cursor_x >= columns) *redraw_tail = terminal_advance_line_locked();
        return;
    }
    if (c == '\b') {
        if (terminal_cursor_x > 0) {
            terminal_cursor_x--;
            terminal_text[index + terminal_cursor_x] = ' ';
            terminal_color[index + terminal_cursor_x] = color;
            if (terminal_view_offset == 0) {
                int row = terminal_line_count > (uint32_t)terminal_view_rows() ?
                          terminal_view_rows() - 1 : (int)terminal_line_count - 1;
                render_output_cell_locked(terminal_cursor_x, row, ' ', color);
                *dirty_x = terminal_cursor_x;
                *dirty_y = row;
            }
        }
        return;
    }

    if (terminal_cursor_x >= columns) *redraw_tail = terminal_advance_line_locked();
    index = terminal_current_line * SCREEN_COLS;
    terminal_text[index + terminal_cursor_x] = c;
    terminal_color[index + terminal_cursor_x] = color;

    if (terminal_view_offset == 0) {
        int row = terminal_line_count > (uint32_t)terminal_view_rows() ?
                  terminal_view_rows() - 1 : (int)terminal_line_count - 1;
        render_output_cell_locked(terminal_cursor_x, row, c, color);
        *dirty_x = terminal_cursor_x;
        *dirty_y = row;
    }

    terminal_cursor_x++;
    if (terminal_cursor_x >= columns) *redraw_tail = terminal_advance_line_locked();
}

static void clear_visual_buffer_locked(void) {
    for (int i = 0; i < SCREEN_COLS * SCREEN_ROWS; i++) {
        text_buffer[i] = ' ';
        color_buffer[i] = current_color;
    }
}

void video_init(void) {
    LOG_INFO("VIDEO", "Inicializando video");
    spinlock_init(&video_lock);
    video_update_depth = 0;
    vesa_mode_t* mode = vesa_get_mode();
    use_framebuffer = (mode && mode->initialized) ? 1 : 0;

    clear_visual_buffer_locked();
    terminal_reset_locked();
    video_clear();
    current_color = 0x07;
    update_cursor();
    LOG_INFO("VIDEO", "Video inicializado com sucesso");
}

void video_disable_framebuffer(void) {
    use_framebuffer = 0;
    video_update_depth = 0;
    update_cursor();
}

void video_clear(void) {
    spinlock_acquire(&video_lock);
    clear_visual_buffer_locked();
    cursor_x = 0;
    cursor_y = 0;

    if (use_framebuffer) {
        vesa_mode_t* mode = vesa_get_mode();
        if (mode && mode->initialized) {
            vesa_color_t c;
            c.raw = vga_bg_to_rgb(current_color);
            vesa_clear(c);
            vesa_frame_mark_region(0, 0, mode->width, mode->height);
            vesa_flip();
        }
    } else {
        uint16_t* vm = (uint16_t*)VIDEO_MEMORY;
        uint16_t entry = (uint16_t)(' ') | ((uint16_t)current_color << 8);
        for (int i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) vm[i] = entry;
    }

    update_cursor();
    spinlock_release(&video_lock);
}

void video_begin_update(void) {
    if (!video_can_batch_updates()) return;

    vesa_frame_begin_region((uint32_t)(cursor_x * FONT_WIDTH),
                            (uint32_t)(cursor_y * FONT_HEIGHT),
                            FONT_WIDTH, FONT_HEIGHT);
    video_update_depth++;
}

void video_end_update(void) {
    if (video_update_depth == 0) return;

    video_update_depth--;
    vesa_frame_end();
}

void video_put_char(char c, uint8_t color) {
    int dirty_x = -1;
    int dirty_y = -1;
    int redraw_tail = 0;

    spinlock_acquire(&video_lock);
    if (terminal_active) {
        terminal_write_char_locked(c, color, &dirty_x, &dirty_y, &redraw_tail);
        if (redraw_tail && terminal_view_offset == 0) {
            mouse_invalidate_cursor();
            terminal_render_view_locked();
        }
        if (terminal_view_offset == 0) {
            int rows = terminal_view_rows();
            cursor_x = terminal_cursor_x;
            cursor_y = terminal_line_count > (uint32_t)rows ? rows - 1 :
                       (int)terminal_line_count - 1;
        }
    } else if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        cursor_x = (cursor_x + TERMINAL_TAB_WIDTH) & ~(TERMINAL_TAB_WIDTH - 1);
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            render_output_cell_locked(cursor_x, cursor_y, ' ', color);
            dirty_x = cursor_x;
            dirty_y = cursor_y;
        }
    } else if (cursor_x < SCREEN_COLS && cursor_y < SCREEN_ROWS) {
        render_output_cell_locked(cursor_x, cursor_y, c, color);
        dirty_x = cursor_x;
        dirty_y = cursor_y;
        cursor_x++;
        if (cursor_x >= SCREEN_COLS) {
            cursor_x = 0;
            cursor_y++;
        }
    }

    if (!terminal_active && cursor_y >= SCREEN_ROWS - 1) {
        for (int y = 0; y < SCREEN_ROWS - 2; y++) {
            for (int x = 0; x < SCREEN_COLS; x++) {
                int from = (y + 1) * SCREEN_COLS + x;
                render_output_cell_locked(x, y, text_buffer[from], color_buffer[from]);
            }
        }
        for (int x = 0; x < SCREEN_COLS; x++) {
            render_output_cell_locked(x, SCREEN_ROWS - 2, ' ', current_color);
        }
        cursor_y = SCREEN_ROWS - 2;
        redraw_tail = 1;
    }

    update_cursor();
    spinlock_release(&video_lock);

    if (redraw_tail) {
        video_present_region(0, 0, terminal_columns() * FONT_WIDTH,
                             terminal_content_rows() * FONT_HEIGHT);
    } else if (dirty_x >= 0) {
        video_present_region(dirty_x * FONT_WIDTH, dirty_y * FONT_HEIGHT,
                             FONT_WIDTH, FONT_HEIGHT);
    }
}

void video_print(const char* str, uint8_t color) {
    if (!str) {
        LOG_ERROR("VIDEO", "Tentativa de imprimir texto nulo");
        return;
    }

    video_begin_update();
    for (int i = 0; str[i] != '\0'; i++) video_put_char(str[i], color);
    video_end_update();
}

void video_set_color(uint8_t fg, uint8_t bg) {
    current_color = fg | (bg << 4);
}

void video_newline(void) {
    video_put_char('\n', current_color);
}

void video_backspace(void) {
    video_put_char('\b', current_color);
}

void video_set_cursor(int x, int y) {
    spinlock_acquire(&video_lock);
    cursor_x = x;
    cursor_y = y;
    update_cursor();
    spinlock_release(&video_lock);
}

int video_get_cursor_x(void) {
    return cursor_x;
}

int video_get_cursor_y(void) {
    return cursor_y;
}

void video_put_char_at(char c, uint8_t color, int x, int y) {
    if (x < 0 || x >= SCREEN_COLS || y < 0 || y >= SCREEN_ROWS) return;

    spinlock_acquire(&video_lock);
    int index = y * SCREEN_COLS + x;
    if (text_buffer[index] != c || color_buffer[index] != color) {
        render_output_cell_locked(x, y, c, color);
    }
    spinlock_release(&video_lock);

    video_present_region(x * FONT_WIDTH, y * FONT_HEIGHT,
                         FONT_WIDTH, FONT_HEIGHT);
}

void video_print_at(int x, int y, const char* str, uint8_t color) {
    if (!str) {
        LOG_ERROR("VIDEO", "Tentativa de imprimir texto nulo em posicao fixa");
        return;
    }

    video_begin_update();
    int cx = x;
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            cx = x;
            y++;
            if (y >= SCREEN_ROWS) break;
            continue;
        }
        video_put_char_at(str[i], color, cx, y);
        cx++;
        if (cx >= SCREEN_COLS) {
            cx = x;
            y++;
            if (y >= SCREEN_ROWS) break;
        }
    }
    video_end_update();
}

void video_fill_rect(int x, int y, int w, int h, char c, uint8_t color) {
    video_begin_update();
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            video_put_char_at(c, color, x + col, y + row);
        }
    }
    video_end_update();
}

void video_draw_hline(int x, int y, int w, char c, uint8_t color) {
    video_begin_update();
    for (int i = 0; i < w; i++) video_put_char_at(c, color, x + i, y);
    video_end_update();
}

void video_draw_vline(int x, int y, int h, char c, uint8_t color) {
    video_begin_update();
    for (int i = 0; i < h; i++) video_put_char_at(c, color, x, y + i);
    video_end_update();
}

void video_draw_box(int x, int y, int w, int h, uint8_t color) {
    video_begin_update();
    for (int i = 0; i < w; i++) {
        video_put_char_at(0xCD, color, x + i, y);
        video_put_char_at(0xCD, color, x + i, y + h - 1);
    }
    for (int i = 0; i < h; i++) {
        video_put_char_at(0xBA, color, x, y + i);
        video_put_char_at(0xBA, color, x + w - 1, y + i);
    }
    video_put_char_at(0xC9, color, x, y);
    video_put_char_at(0xBB, color, x + w - 1, y);
    video_put_char_at(0xC8, color, x, y + h - 1);
    video_put_char_at(0xBC, color, x + w - 1, y + h - 1);
    video_end_update();
}

void video_terminal_begin(void) {
    spinlock_acquire(&video_lock);
    terminal_active = 1;
    terminal_view_offset = 0;
    mouse_invalidate_cursor();
    terminal_render_view_locked();
    update_cursor();
    spinlock_release(&video_lock);

    video_present_region(0, 0, terminal_columns() * FONT_WIDTH,
                         terminal_content_rows() * FONT_HEIGHT);
}

void video_terminal_suspend(void) {
    spinlock_acquire(&video_lock);
    terminal_active = 0;
    terminal_view_offset = 0;
    spinlock_release(&video_lock);
}

int video_terminal_is_active(void) {
    return terminal_active ? 1 : 0;
}

void video_terminal_clear(void) {
    spinlock_acquire(&video_lock);
    terminal_reset_locked();
    terminal_view_offset = 0;
    mouse_invalidate_cursor();
    terminal_render_view_locked();
    update_cursor();
    spinlock_release(&video_lock);

    video_present_region(0, 0, terminal_columns() * FONT_WIDTH,
                         terminal_content_rows() * FONT_HEIGHT);
}

int video_terminal_scroll(int lines) {
    int changed = 0;

    spinlock_acquire(&video_lock);
    if (terminal_active && lines != 0) {
        uint32_t max_offset = terminal_max_view_offset();
        uint32_t next_offset = terminal_view_offset;

        if (lines > 0) {
            uint32_t amount = (uint32_t)lines;
            next_offset = amount > max_offset - next_offset ?
                          max_offset : next_offset + amount;
        } else {
            uint32_t amount = (uint32_t)(-lines);
            next_offset = amount > next_offset ? 0 : next_offset - amount;
        }

        if (next_offset != terminal_view_offset) {
            terminal_view_offset = next_offset;
            mouse_invalidate_cursor();
            terminal_render_view_locked();
            changed = 1;
        }
    }
    update_cursor();
    spinlock_release(&video_lock);

    if (changed) {
        video_present_region(0, 0, terminal_columns() * FONT_WIDTH,
                             terminal_content_rows() * FONT_HEIGHT);
    }
    return changed;
}

void video_terminal_scroll_home(void) {
    spinlock_acquire(&video_lock);
    if (terminal_active) {
        terminal_view_offset = terminal_max_view_offset();
        mouse_invalidate_cursor();
        terminal_render_view_locked();
    }
    update_cursor();
    spinlock_release(&video_lock);

    video_present_region(0, 0, terminal_columns() * FONT_WIDTH,
                         terminal_content_rows() * FONT_HEIGHT);
}

void video_terminal_scroll_end(void) {
    int changed = 0;

    spinlock_acquire(&video_lock);
    if (terminal_active && terminal_view_offset != 0) {
        terminal_view_offset = 0;
        mouse_invalidate_cursor();
        terminal_render_view_locked();
        changed = 1;
    }
    update_cursor();
    spinlock_release(&video_lock);

    if (changed) {
        video_present_region(0, 0, terminal_columns() * FONT_WIDTH,
                             terminal_content_rows() * FONT_HEIGHT);
    }
}

int video_terminal_is_scrolled(void) {
    return terminal_view_offset != 0 ? 1 : 0;
}
