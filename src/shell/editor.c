#include "editor.h"
#include "video.h"
#include "keyboard.h"
#include "fs.h"
#include "memory.h"
#include "timer.h"

static editor_t editor;
static uint8_t shift_pressed = 0;
static uint8_t ctrl_pressed = 0;

static void memset(void* dst, uint8_t val, uint32_t size) {
    uint8_t* d = (uint8_t*)dst;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = val;
    }
}

static void str_copy(char* dst, const char* src, uint32_t max) {
    uint32_t i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_len(const char* str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

static void str_insert(char* str, int pos, char c) {
    int len = str_len(str);
    for (int i = len; i >= pos; i--) {
        str[i + 1] = str[i];
    }
    str[pos] = c;
}

static void str_remove(char* str, int pos) {
    int len = str_len(str);
    for (int i = pos; i < len; i++) {
        str[i] = str[i + 1];
    }
}

static int str_compare(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return *a - *b;
        a++;
        b++;
    }
    return *a - *b;
}

static char* alloc_line(void) {
    char* line = (char*)kmalloc(EDITOR_MAX_LINE_LENGTH);
    if (line) {
        line[0] = '\0';
    }
    return line;
}

static uint8_t detect_encoding(const uint8_t* data, uint32_t size) {
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        return EDITOR_ENCODING_UTF8;
    }

    uint32_t non_ascii = 0;
    uint32_t utf8_sequences = 0;

    for (uint32_t i = 0; i < size; i++) {
        if (data[i] > 127) {
            non_ascii++;
            if (i + 1 < size && (data[i] & 0xE0) == 0xC0 && (data[i + 1] & 0xC0) == 0x80) {
                utf8_sequences++;
                i++;
            }
        }
    }

    if (utf8_sequences > non_ascii / 2) return EDITOR_ENCODING_UTF8;
    if (non_ascii > 0) return EDITOR_ENCODING_LATIN1;
    return EDITOR_ENCODING_ASCII;
}

static uint8_t detect_line_ending(const uint8_t* data, uint32_t size) {
    uint32_t lf = 0, cr = 0, crlf = 0;

    for (uint32_t i = 0; i < size; i++) {
        if (data[i] == '\r') {
            if (i + 1 < size && data[i + 1] == '\n') {
                crlf++;
                i++;
            } else {
                cr++;
            }
        } else if (data[i] == '\n') {
            lf++;
        }
    }

    if (crlf > lf && crlf > cr) return EDITOR_CRLF;
    if (cr > lf && cr > crlf) return EDITOR_CR;
    return EDITOR_LF;
}

static uint8_t detect_syntax(const char* filename) {
    int len = str_len(filename);
    if (len < 3) return EDITOR_SYNTAX_NONE;

    const char* ext = filename + len - 1;
    while (ext > filename && *(ext - 1) != '.') ext--;

    if (str_compare(ext, "C") == 0 || str_compare(ext, "H") == 0 ||
        str_compare(ext, "c") == 0 || str_compare(ext, "h") == 0) {
        return EDITOR_SYNTAX_C;
    }
    if (str_compare(ext, "PY") == 0 || str_compare(ext, "py") == 0) {
        return EDITOR_SYNTAX_PYTHON;
    }
    if (str_compare(ext, "ASM") == 0 || str_compare(ext, "asm") == 0 ||
        str_compare(ext, "S") == 0 || str_compare(ext, "s") == 0) {
        return EDITOR_SYNTAX_ASM;
    }
    if (str_compare(ext, "MD") == 0 || str_compare(ext, "md") == 0) {
        return EDITOR_SYNTAX_MARKDOWN;
    }

    return EDITOR_SYNTAX_NONE;
}

static uint8_t is_keyword_c(const char* word) {
    const char* keywords[] = {
        "int", "char", "void", "return", "if", "else", "while", "for",
        "do", "switch", "case", "break", "continue", "struct", "typedef",
        "static", "extern", "const", "unsigned", "signed", "long", "short",
        "float", "double", "enum", "union", "sizeof", "NULL", "true", "false", 0
    };
    for (int i = 0; keywords[i]; i++) {
        if (str_compare(word, keywords[i]) == 0) return 1;
    }
    return 0;
}

static uint8_t is_keyword_python(const char* word) {
    const char* keywords[] = {
        "def", "class", "return", "if", "elif", "else", "while", "for",
        "in", "import", "from", "as", "try", "except", "finally", "raise",
        "with", "yield", "lambda", "pass", "break", "continue", "and",
        "or", "not", "is", "None", "True", "False", "self", "print", 0
    };
    for (int i = 0; keywords[i]; i++) {
        if (str_compare(word, keywords[i]) == 0) return 1;
    }
    return 0;
}

static uint8_t is_keyword_asm(const char* word) {
    const char* keywords[] = {
        "mov", "push", "pop", "call", "ret", "jmp", "je", "jne", "jg",
        "jl", "jge", "jle", "cmp", "test", "add", "sub", "mul", "div",
        "and", "or", "xor", "not", "shl", "shr", "inc", "dec", "int",
        "global", "extern", "section", "db", "dw", "dd", "dq", "times",
        "nop", "hlt", "cli", "sti", "in", "out", "align", 0
    };
    for (int i = 0; keywords[i]; i++) {
        if (str_compare(word, keywords[i]) == 0) return 1;
    }
    return 0;
}

static uint8_t get_format_color(const char* line, uint32_t pos) {
    uint8_t in_bold = 0;
    uint8_t in_italic = 0;

    for (uint32_t i = 0; i <= pos; i++) {
        if (line[i] == '*' && i + 1 < str_len(line) && line[i + 1] == '*') {
            in_bold = !in_bold;
            i++;
        } else if (line[i] == '*' && !in_bold) {
            in_italic = !in_italic;
        }
    }

    if (in_bold && in_italic) return 0x0F;
    if (in_bold) return 0x0F;
    if (in_italic) return 0x0B;
    return 0;
}

static uint8_t get_syntax_color(const char* line, uint32_t pos, uint8_t syntax) {
    if (syntax == EDITOR_SYNTAX_MARKDOWN) {
        uint8_t fmt = get_format_color(line, pos);
        if (fmt) return fmt;

        char c = line[pos];
        if (c == '#') return 0x0D;
        if (c == '`') return 0x0B;
        if (c == '[' || c == ']') return 0x09;
        if (c == '(' || c == ')') return 0x09;
        if (c >= '0' && c <= '9') return 0x0E;
        return 0x07;
    }

    if (syntax == EDITOR_SYNTAX_NONE) return 0x07;

    char c = line[pos];

    if (c == '#' && syntax == EDITOR_SYNTAX_C) return 0x0B;
    if (c == '#' && syntax == EDITOR_SYNTAX_PYTHON) return 0x0B;
    if (c == ';' && syntax == EDITOR_SYNTAX_ASM) return 0x0B;

    if ((c == '"' || c == '\'') && syntax != EDITOR_SYNTAX_ASM) return 0x0A;

    if (syntax == EDITOR_SYNTAX_C) {
        if (c == '/' && pos + 1 < str_len(line)) {
            if (line[pos + 1] == '/' || line[pos + 1] == '*') return 0x08;
        }
    }
    if (syntax == EDITOR_SYNTAX_PYTHON && c == '#') return 0x08;
    if (syntax == EDITOR_SYNTAX_ASM && c == ';') return 0x08;

    if (c >= '0' && c <= '9') return 0x0E;

    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        char word[64];
        int w = 0;
        int start = pos;
        while (start > 0 && ((line[start-1] >= 'a' && line[start-1] <= 'z') ||
               (line[start-1] >= 'A' && line[start-1] <= 'Z') ||
               (line[start-1] >= '0' && line[start-1] <= '9') ||
               line[start-1] == '_')) start--;
        while (pos < str_len(line) && w < 63 &&
               ((line[pos] >= 'a' && line[pos] <= 'z') ||
                (line[pos] >= 'A' && line[pos] <= 'Z') ||
                (line[pos] >= '0' && line[pos] <= '9') ||
                line[pos] == '_')) word[w++] = line[pos++];
        word[w] = '\0';

        if (syntax == EDITOR_SYNTAX_C && is_keyword_c(word)) return 0x0D;
        if (syntax == EDITOR_SYNTAX_PYTHON && is_keyword_python(word)) return 0x0D;
        if (syntax == EDITOR_SYNTAX_ASM && is_keyword_asm(word)) return 0x0D;

        return 0x07;
    }

    return 0x07;
}

static void int_to_str(uint32_t num, char* buf) {
    int i = 0;
    if (num == 0) { buf[i++] = '0'; }
    else {
        char tmp[16];
        int j = 0;
        while (num > 0) { tmp[j++] = '0' + (num % 10); num /= 10; }
        while (j > 0) { buf[i++] = tmp[--j]; }
    }
    buf[i] = '\0';
}

static void editor_do_word_wrap(uint32_t line_idx) {
    if (!editor.word_wrap) return;
    if (line_idx >= editor.line_count) return;
    if (!editor.lines[line_idx]) return;

    uint32_t len = str_len(editor.lines[line_idx]);
    if (len <= editor.wrap_width) return;

    uint32_t break_pos = editor.wrap_width;
    while (break_pos > 0 && editor.lines[line_idx][break_pos] != ' ') {
        break_pos--;
    }
    if (break_pos == 0) break_pos = editor.wrap_width;

    if (editor.line_count >= EDITOR_MAX_LINES) return;

    for (uint32_t i = editor.line_count; i > line_idx + 1; i--) {
        editor.lines[i] = editor.lines[i - 1];
    }

    editor.lines[line_idx + 1] = alloc_line();
    uint32_t remaining = len - break_pos;
    for (uint32_t i = 0; i < remaining; i++) {
        editor.lines[line_idx + 1][i] = editor.lines[line_idx][break_pos + i];
    }
    editor.lines[line_idx + 1][remaining] = '\0';
    editor.lines[line_idx][break_pos] = '\0';
    editor.line_count++;
}

void editor_init(void) {
    memset(&editor, 0, sizeof(editor_t));
    editor.running = 0;
    editor.view_width = 80;
    editor.view_height = 23;
    editor.encoding = EDITOR_ENCODING_ASCII;
    editor.line_ending = EDITOR_LF;
    editor.syntax_mode = EDITOR_SYNTAX_NONE;
    editor.word_wrap = 0;
    editor.show_formatting = 1;
    editor.wrap_width = 72;
    editor.bold_active = 0;
    editor.italic_active = 0;
}

void editor_new(void) {
    for (uint32_t i = 0; i < editor.line_count; i++) {
        if (editor.lines[i]) {
            kfree(editor.lines[i]);
            editor.lines[i] = 0;
        }
    }

    editor.line_count = 1;
    editor.lines[0] = alloc_line();
    editor.cursor_x = 0;
    editor.cursor_y = 0;
    editor.scroll_x = 0;
    editor.scroll_y = 0;
    editor.modified = 0;
    editor.total_chars = 0;
    editor.total_bytes = 0;
    editor.encoding = EDITOR_ENCODING_ASCII;
    editor.line_ending = EDITOR_LF;
    editor.syntax_mode = EDITOR_SYNTAX_NONE;
    editor.filename[0] = '\0';
    str_copy(editor.filename, "UNNAMED.TXT", 64);
}

void editor_open(const char* filename) {
    for (uint32_t i = 0; i < editor.line_count; i++) {
        if (editor.lines[i]) {
            kfree(editor.lines[i]);
            editor.lines[i] = 0;
        }
    }

    editor.line_count = 0;
    editor.cursor_x = 0;
    editor.cursor_y = 0;
    editor.scroll_x = 0;
    editor.scroll_y = 0;
    editor.modified = 0;
    editor.total_chars = 0;
    editor.total_bytes = 0;

    str_copy(editor.filename, filename, 64);
    editor.syntax_mode = detect_syntax(filename);

    uint8_t* buffer = (uint8_t*)kmalloc(131072);
    if (!buffer) {
        editor.line_count = 1;
        editor.lines[0] = alloc_line();
        return;
    }

    int bytes = fs_read_file(filename, buffer, 131071);
    if (bytes <= 0) {
        editor.line_count = 1;
        editor.lines[0] = alloc_line();
        kfree(buffer);
        return;
    }

    editor.total_bytes = bytes;
    editor.encoding = detect_encoding(buffer, bytes);
    editor.line_ending = detect_line_ending(buffer, bytes);

    uint32_t start_offset = 0;
    if (editor.encoding == EDITOR_ENCODING_UTF8 && bytes >= 3) {
        if (buffer[0] == 0xEF && buffer[1] == 0xBB && buffer[2] == 0xBF) {
            start_offset = 3;
        }
    }

    uint32_t line = 0;
    uint32_t pos = 0;

    editor.lines[line] = alloc_line();
    if (!editor.lines[line]) {
        kfree(buffer);
        return;
    }

    for (uint32_t i = start_offset; i < (uint32_t)bytes; i++) {
        if (buffer[i] == '\n') {
            editor.lines[line][pos] = '\0';
            editor.total_chars += pos;
            line++;
            pos = 0;
            if (line >= EDITOR_MAX_LINES) break;
            editor.lines[line] = alloc_line();
            if (!editor.lines[line]) break;
        } else if (buffer[i] == '\r') {
            if (i + 1 < (uint32_t)bytes && buffer[i + 1] == '\n') i++;
        } else {
            if (pos < EDITOR_MAX_LINE_LENGTH - 1) {
                editor.lines[line][pos++] = buffer[i];
            }
        }
    }
    editor.lines[line][pos] = '\0';
    editor.total_chars += pos;
    editor.line_count = line + 1;

    kfree(buffer);

    if (editor.word_wrap) {
        for (uint32_t i = 0; i < editor.line_count; i++) {
            editor_do_word_wrap(i);
        }
    }
}

void editor_close(void) {
    for (uint32_t i = 0; i < editor.line_count; i++) {
        if (editor.lines[i]) {
            kfree(editor.lines[i]);
            editor.lines[i] = 0;
        }
    }
    editor.running = 0;
}

static void editor_save(void) {
    uint32_t total_size = 0;
    for (uint32_t i = 0; i < editor.line_count; i++) {
        if (editor.lines[i]) total_size += str_len(editor.lines[i]);
        if (i < editor.line_count - 1) {
            if (editor.line_ending == EDITOR_CRLF) total_size += 2;
            else total_size++;
        }
    }

    uint8_t* buffer = (uint8_t*)kmalloc(total_size + 1);
    if (!buffer) {
        video_print("Erro: sem memoria para salvar!\n", 0x0C);
        return;
    }

    uint32_t pos = 0;
    for (uint32_t i = 0; i < editor.line_count; i++) {
        if (editor.lines[i]) {
            uint32_t len = str_len(editor.lines[i]);
            for (uint32_t j = 0; j < len; j++) {
                buffer[pos++] = editor.lines[i][j];
            }
        }
        if (i < editor.line_count - 1) {
            if (editor.line_ending == EDITOR_CRLF) {
                buffer[pos++] = '\r';
                buffer[pos++] = '\n';
            } else if (editor.line_ending == EDITOR_CR) {
                buffer[pos++] = '\r';
            } else {
                buffer[pos++] = '\n';
            }
        }
    }

    int result = fs_write_file(editor.filename, buffer, pos);
    kfree(buffer);

    if (result >= 0) {
        editor.modified = 0;
        editor.total_bytes = pos;
    }
}

static void editor_draw_button(int x, int y, const char* label, uint8_t active) {
    uint8_t bg = active ? 0x0A : 0x08;
    uint8_t fg = active ? 0x00 : 0x07;

    video_put_char_at('[', fg, x, y);
    uint32_t len = str_len(label);
    for (uint32_t i = 0; i < len; i++) {
        video_put_char_at(label[i], fg, x + 1 + i, y);
    }
    video_put_char_at(']', fg, x + 1 + len, y);
}

static void editor_draw(void) {
    video_clear();

    video_print(editor.filename, 0x0B);
    if (editor.modified) {
        video_print(" *", 0x0C);
    }

    video_print("  ", 0x08);

    if (editor.encoding == EDITOR_ENCODING_UTF8) video_print("UTF-8 ", 0x08);
    else if (editor.encoding == EDITOR_ENCODING_LATIN1) video_print("LATIN1 ", 0x08);
    else video_print("ASCII ", 0x08);

    if (editor.line_ending == EDITOR_CRLF) video_print("CRLF ", 0x08);
    else if (editor.line_ending == EDITOR_CR) video_print("CR ", 0x08);
    else video_print("LF ", 0x08);

    int btn_x = 42;
    editor_draw_button(btn_x, 0, "F2:Salvar", 1);
    editor_draw_button(btn_x + 11, 0, "F3:Wrap", editor.word_wrap);
    editor_draw_button(btn_x + 19, 0, "F4:Bold", editor.bold_active);
    editor_draw_button(btn_x + 27, 0, "F5:Italic", editor.italic_active);
    editor_draw_button(btn_x + 37, 0, "ESC:Sair", 0);

    uint32_t view_h = editor.view_height - 2;

    for (uint32_t y = 0; y < view_h; y++) {
        uint32_t line_idx = y + editor.scroll_y;

        if (line_idx < editor.line_count && editor.lines[line_idx]) {
            char* line = editor.lines[line_idx];
            uint32_t len = str_len(line);

            uint32_t display_x = 0;
            for (uint32_t x = 0; x < editor.view_width - 7; x++) {
                uint32_t char_idx = x + editor.scroll_x;

                if (char_idx < len) {
                    uint8_t color;
                    if (editor.syntax_mode == EDITOR_SYNTAX_MARKDOWN) {
                        color = get_syntax_color(line, char_idx, editor.syntax_mode);
                    } else {
                        color = get_syntax_color(line, char_idx, editor.syntax_mode);
                    }

                    char c = line[char_idx];
                    if (c == '*' && editor.show_formatting) {
                        video_put_char_at('.', 0x08, 7 + display_x, 1 + y);
                    } else {
                        video_put_char_at(c, color, 7 + display_x, 1 + y);
                    }
                } else {
                    video_put_char_at(' ', 0x07, 7 + display_x, 1 + y);
                }
                display_x++;
            }
        }

        char line_num[8];
        uint32_t num = line_idx + 1;
        int_to_str(num, line_num);
        int len = str_len(line_num);
        int padding = 6 - len;
        for (int p = 0; p < padding; p++) {
            video_put_char_at(' ', 0x08, 0, 1 + y);
        }
        video_print_at(0, 1 + y, line_num, 0x08);
        video_put_char_at(' ', 0x08, 6, 1 + y);
    }

    uint32_t cursor_screen_y = editor.cursor_y - editor.scroll_y;
    uint32_t cursor_screen_x = editor.cursor_x - editor.scroll_x + 7;
    video_set_cursor(cursor_screen_x, 1 + cursor_screen_y);

    char status[80];
    uint32_t pos = 0;

    status[pos++] = 'L'; status[pos++] = 'i'; status[pos++] = 'n';
    status[pos++] = 'h'; status[pos++] = 'a'; status[pos++] = ' ';

    char buf[16];
    int_to_str(editor.cursor_y + 1, buf);
    for (int i = 0; buf[i]; i++) status[pos++] = buf[i];

    status[pos++] = ':'; status[pos++] = 'C'; status[pos++] = 'o';
    status[pos++] = 'l'; status[pos++] = ' ';

    int_to_str(editor.cursor_x + 1, buf);
    for (int i = 0; buf[i]; i++) status[pos++] = buf[i];

    status[pos++] = ' '; status[pos++] = '|'; status[pos++] = ' ';

    int_to_str(editor.line_count, buf);
    for (int i = 0; buf[i]; i++) status[pos++] = buf[i];

    status[pos++] = ' '; status[pos++] = 'l'; status[pos++] = 'i';
    status[pos++] = 'n'; status[pos++] = 'h'; status[pos++] = 'a';
    status[pos++] = 's'; status[pos++] = ' '; status[pos++] = '|';
    status[pos++] = ' ';

    int_to_str(editor.total_chars, buf);
    for (int i = 0; buf[i]; i++) status[pos++] = buf[i];

    status[pos++] = ' '; status[pos++] = 'c'; status[pos++] = 'a';
    status[pos++] = 'r'; status[pos++] = 's';

    if (editor.word_wrap) {
        status[pos++] = ' '; status[pos++] = '|'; status[pos++] = ' ';
        status[pos++] = 'W'; status[pos++] = 'R'; status[pos++] = 'A';
        status[pos++] = 'P';
    }

    status[pos] = '\0';

    video_print_at(0, 24, "                                                                                          ", 0x08);
    video_print_at(0, 24, status, 0x08);
}

static void editor_insert_char(char c) {
    if (editor.cursor_y >= editor.line_count) return;
    if (!editor.lines[editor.cursor_y]) return;

    str_insert(editor.lines[editor.cursor_y], editor.cursor_x, c);
    editor.cursor_x++;
    editor.modified = 1;
    editor.total_chars++;

    if (editor.word_wrap) {
        uint32_t len = str_len(editor.lines[editor.cursor_y]);
        if (len > editor.wrap_width) {
            editor_do_word_wrap(editor.cursor_y);
        }
    }
}

static void editor_backspace(void) {
    if (editor.cursor_x > 0) {
        str_remove(editor.lines[editor.cursor_y], editor.cursor_x - 1);
        editor.cursor_x--;
        editor.total_chars--;
    } else if (editor.cursor_y > 0) {
        uint32_t prev_len = str_len(editor.lines[editor.cursor_y - 1]);
        uint32_t cur_len = str_len(editor.lines[editor.cursor_y]);

        for (uint32_t i = 0; i < cur_len; i++) {
            if (prev_len + i < EDITOR_MAX_LINE_LENGTH - 1) {
                editor.lines[editor.cursor_y - 1][prev_len + i] = editor.lines[editor.cursor_y][i];
            }
        }
        editor.lines[editor.cursor_y - 1][prev_len + cur_len] = '\0';

        kfree(editor.lines[editor.cursor_y]);
        for (uint32_t i = editor.cursor_y; i < editor.line_count - 1; i++) {
            editor.lines[i] = editor.lines[i + 1];
        }
        editor.lines[editor.line_count - 1] = 0;
        editor.line_count--;

        editor.cursor_y--;
        editor.cursor_x = prev_len;
        editor.total_chars--;
    }
    editor.modified = 1;
}

static void editor_delete(void) {
    if (editor.cursor_x < str_len(editor.lines[editor.cursor_y])) {
        str_remove(editor.lines[editor.cursor_y], editor.cursor_x);
        editor.total_chars--;
    } else if (editor.cursor_y < editor.line_count - 1) {
        uint32_t cur_len = str_len(editor.lines[editor.cursor_y]);
        uint32_t next_len = str_len(editor.lines[editor.cursor_y + 1]);

        for (uint32_t i = 0; i < next_len; i++) {
            if (cur_len + i < EDITOR_MAX_LINE_LENGTH - 1) {
                editor.lines[editor.cursor_y][cur_len + i] = editor.lines[editor.cursor_y + 1][i];
            }
        }
        editor.lines[editor.cursor_y][cur_len + next_len] = '\0';

        kfree(editor.lines[editor.cursor_y + 1]);
        for (uint32_t i = editor.cursor_y + 1; i < editor.line_count - 1; i++) {
            editor.lines[i] = editor.lines[i + 1];
        }
        editor.lines[editor.line_count - 1] = 0;
        editor.line_count--;
        editor.total_chars--;
    }
    editor.modified = 1;
}

static void editor_newline(void) {
    if (editor.line_count >= EDITOR_MAX_LINES) return;

    uint32_t cur_len = str_len(editor.lines[editor.cursor_y]);

    for (uint32_t i = editor.line_count; i > editor.cursor_y + 1; i--) {
        editor.lines[i] = editor.lines[i - 1];
    }

    editor.lines[editor.cursor_y + 1] = alloc_line();

    uint32_t split_pos = editor.cursor_x;
    for (uint32_t i = split_pos; i < cur_len; i++) {
        editor.lines[editor.cursor_y + 1][i - split_pos] = editor.lines[editor.cursor_y][i];
    }
    editor.lines[editor.cursor_y + 1][cur_len - split_pos] = '\0';
    editor.lines[editor.cursor_y][split_pos] = '\0';

    editor.line_count++;
    editor.cursor_y++;
    editor.cursor_x = 0;
    editor.modified = 1;
    editor.total_chars++;
}

static void editor_insert_bold(void) {
    editor_insert_char('*');
    editor_insert_char('*');
    editor.cursor_x -= 2;
    editor_insert_char(' ');
    editor_insert_char(' ');
    editor.cursor_x--;
    editor.modified = 1;
}

static void editor_insert_italic(void) {
    editor_insert_char('*');
    editor_insert_char(' ');
    editor_insert_char('*');
    editor.cursor_x--;
    editor.modified = 1;
}

static void editor_tab(void) {
    for (int i = 0; i < EDITOR_TAB_SIZE; i++) {
        editor_insert_char(' ');
    }
}

void editor_handle_key(uint8_t scancode) {
    if (!editor.running) return;

    if (scancode == 0x2A || scancode == 0x36) { shift_pressed = 1; return; }
    if (scancode == 0xAA || scancode == 0xB6) { shift_pressed = 0; return; }
    if (scancode == 0x1D || scancode == 0xE0) { ctrl_pressed = 1; return; }
    if (scancode == 0x9D || scancode == 0xE0) { ctrl_pressed = 0; return; }

    if (scancode & 0x80) return;

    static const char scancode_table[128] = {
        0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
        '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
        0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
        0,  '\\','z','x','c','v','b','n','m',',','.','/',0,
        '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };

    static const char scancode_shift_table[128] = {
        0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
        '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
        0,  'A','S','D','F','G','H','J','K','L',':','"','~',
        0,  '|','Z','X','C','V','B','N','M','<','>','?',0,
        '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };

    if (scancode == 0x01) { editor_close(); return; }

    if (scancode == 0x3C) {
        editor_save();
        editor_draw();
        return;
    }

    if (scancode == 0x3D) {
        editor.word_wrap = !editor.word_wrap;
        editor_draw();
        return;
    }

    if (scancode == 0x3E) {
        editor.bold_active = !editor.bold_active;
        if (editor.bold_active) {
            editor_insert_bold();
        }
        editor_draw();
        return;
    }

    if (scancode == 0x3F) {
        editor.italic_active = !editor.italic_active;
        if (editor.italic_active) {
            editor_insert_italic();
        }
        editor_draw();
        return;
    }

    if (scancode == 0x47) { editor.cursor_x = 0; editor_draw(); return; }
    if (scancode == 0x4F) {
        editor.cursor_x = str_len(editor.lines[editor.cursor_y]);
        editor_draw();
        return;
    }

    if (scancode == 0x48) {
        if (editor.cursor_y > 0) {
            editor.cursor_y--;
            uint32_t len = str_len(editor.lines[editor.cursor_y]);
            if (editor.cursor_x > len) editor.cursor_x = len;
        }
        editor_draw();
        return;
    }
    if (scancode == 0x50) {
        if (editor.cursor_y < editor.line_count - 1) {
            editor.cursor_y++;
            uint32_t len = str_len(editor.lines[editor.cursor_y]);
            if (editor.cursor_x > len) editor.cursor_x = len;
        }
        editor_draw();
        return;
    }
    if (scancode == 0x4B) {
        if (editor.cursor_x > 0) editor.cursor_x--;
        else if (editor.cursor_y > 0) {
            editor.cursor_y--;
            editor.cursor_x = str_len(editor.lines[editor.cursor_y]);
        }
        editor_draw();
        return;
    }
    if (scancode == 0x4D) {
        if (editor.cursor_x < str_len(editor.lines[editor.cursor_y])) editor.cursor_x++;
        else if (editor.cursor_y < editor.line_count - 1) {
            editor.cursor_y++;
            editor.cursor_x = 0;
        }
        editor_draw();
        return;
    }

    if (scancode == 0x49) {
        for (int i = 0; i < editor.view_height - 2; i++) {
            if (editor.cursor_y > 0) editor.cursor_y--;
        }
        uint32_t len = str_len(editor.lines[editor.cursor_y]);
        if (editor.cursor_x > len) editor.cursor_x = len;
        editor_draw();
        return;
    }
    if (scancode == 0x51) {
        for (int i = 0; i < editor.view_height - 2; i++) {
            if (editor.cursor_y < editor.line_count - 1) editor.cursor_y++;
        }
        uint32_t len = str_len(editor.lines[editor.cursor_y]);
        if (editor.cursor_x > len) editor.cursor_x = len;
        editor_draw();
        return;
    }

    if (scancode == 0x0E) { editor_backspace(); editor_draw(); return; }
    if (scancode == 0x53) { editor_delete(); editor_draw(); return; }
    if (scancode == 0x1C) { editor_newline(); editor_draw(); return; }
    if (scancode == 0x0F) { editor_tab(); editor_draw(); return; }

    char c = shift_pressed ? scancode_shift_table[scancode] : scancode_table[scancode];
    if (c) {
        editor_insert_char(c);
        editor_draw();
    }
}

void editor_run(void) {
    editor_init();
    editor_new();
    editor.running = 1;

    keyboard_set_callback(editor_handle_key);
    editor_draw();

    while (editor.running) {
        asm volatile("hlt");
    }

    keyboard_set_callback(0);
}

void editor_run_file(const char* filename) {
    editor_init();
    editor_open(filename);
    editor.running = 1;

    keyboard_set_callback(editor_handle_key);
    editor_draw();

    while (editor.running) {
        asm volatile("hlt");
    }

    keyboard_set_callback(0);
}

void editor_close_app(void) {
    for (uint32_t i = 0; i < editor.line_count; i++) {
        if (editor.lines[i]) {
            kfree(editor.lines[i]);
            editor.lines[i] = 0;
        }
    }
    editor.line_count = 0;
    editor.running = 0;
}

uint8_t editor_is_running(void) {
    return editor.running;
}
