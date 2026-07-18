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

static char* alloc_line(void) {
    char* line = (char*)kmalloc(EDITOR_MAX_LINE_LENGTH);
    if (line) {
        line[0] = '\0';
    }
    return line;
}

void editor_init(void) {
    memset(&editor, 0, sizeof(editor_t));
    editor.running = 0;
    editor.view_width = 80;
    editor.view_height = 23;
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

    str_copy(editor.filename, filename, 64);

    uint8_t* buffer = (uint8_t*)kmalloc(65536);
    if (!buffer) {
        editor.line_count = 1;
        editor.lines[0] = alloc_line();
        return;
    }

    int bytes = fs_read_file(filename, buffer, 65535);
    if (bytes <= 0) {
        editor.line_count = 1;
        editor.lines[0] = alloc_line();
        kfree(buffer);
        return;
    }

    buffer[bytes] = '\0';

    uint32_t line = 0;
    uint32_t pos = 0;

    editor.lines[line] = alloc_line();
    if (!editor.lines[line]) {
        kfree(buffer);
        return;
    }

    for (int i = 0; i < bytes; i++) {
        if (buffer[i] == '\n') {
            editor.lines[line][pos] = '\0';
            line++;
            pos = 0;
            if (line >= EDITOR_MAX_LINES) break;
            editor.lines[line] = alloc_line();
            if (!editor.lines[line]) break;
        } else if (buffer[i] == '\r') {
            continue;
        } else {
            if (pos < EDITOR_MAX_LINE_LENGTH - 1) {
                editor.lines[line][pos++] = buffer[i];
            }
        }
    }
    editor.lines[line][pos] = '\0';
    editor.line_count = line + 1;

    kfree(buffer);
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
        if (editor.lines[i]) {
            total_size += str_len(editor.lines[i]);
        }
        if (i < editor.line_count - 1) {
            total_size++;
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
            buffer[pos++] = '\n';
        }
    }
    buffer[pos] = '\0';

    int result = fs_write_file(editor.filename, buffer, pos);
    kfree(buffer);

    if (result >= 0) {
        editor.modified = 0;
        video_print("Arquivo salvo: ", 0x0A);
        video_print(editor.filename, 0x0A);
        video_print("\n", 0x0A);
    } else {
        video_print("Erro ao salvar arquivo!\n", 0x0C);
    }
}

static void editor_draw(void) {
    video_clear();

    video_print(editor.filename, 0x0B);
    if (editor.modified) {
        video_print(" *", 0x0C);
    }
    video_print("  [F2=Salvar] [ESC=Sair]\n", 0x08);

    uint32_t view_h = editor.view_height - 2;

    for (uint32_t y = 0; y < view_h; y++) {
        uint32_t line_idx = y + editor.scroll_y;

        if (line_idx < editor.line_count && editor.lines[line_idx]) {
            char* line = editor.lines[line_idx];
            uint32_t len = str_len(line);

            uint32_t display_x = 0;
            for (uint32_t x = 0; x < editor.view_width - 5; x++) {
                uint32_t char_idx = x + editor.scroll_x;

                if (char_idx < len) {
                    video_put_char_at(line[char_idx], 0x07, 5 + display_x, 1 + y);
                } else {
                    video_put_char_at(' ', 0x07, 5 + display_x, 1 + y);
                }
                display_x++;
            }
        }

        char line_num[8];
        uint32_t num = line_idx + 1;
        int i = 0;
        if (num == 0) { line_num[i++] = '0'; }
        else {
            char tmp[8];
            int j = 0;
            while (num > 0) { tmp[j++] = '0' + (num % 10); num /= 10; }
            while (j > 0) { line_num[i++] = tmp[--j]; }
        }
        line_num[i] = '\0';

        int padding = 4 - i;
        for (int p = 0; p < padding; p++) {
            video_put_char_at(' ', 0x08, 0, 1 + y);
        }
        video_print_at(0, 1 + y, line_num, 0x08);
        video_put_char_at(' ', 0x08, 4, 1 + y);
    }

    uint32_t cursor_screen_y = editor.cursor_y - editor.scroll_y;
    uint32_t cursor_screen_x = editor.cursor_x - editor.scroll_x + 5;
    video_set_cursor(cursor_screen_x, 1 + cursor_screen_y);

    char status[64];
    uint32_t pos = 0;
    status[pos++] = 'L';
    status[pos++] = 'i';
    status[pos++] = 'n';
    status[pos++] = 'h';
    status[pos++] = 'a';
    status[pos++] = ' ';

    uint32_t num = editor.cursor_y + 1;
    char tmp[16];
    int t = 0;
    if (num == 0) { tmp[t++] = '0'; }
    else {
        while (num > 0) { tmp[t++] = '0' + (num % 10); num /= 10; }
    }
    while (t > 0) { status[pos++] = tmp[--t]; }

    status[pos++] = ':';
    status[pos++] = 'C';
    status[pos++] = 'o';
    status[pos++] = 'l';
    status[pos++] = ' ';

    num = editor.cursor_x + 1;
    t = 0;
    if (num == 0) { tmp[t++] = '0'; }
    else {
        while (num > 0) { tmp[t++] = '0' + (num % 10); num /= 10; }
    }
    while (t > 0) { status[pos++] = tmp[--t]; }

    status[pos++] = ' ';
    status[pos++] = '|';
    status[pos++] = ' ';

    num = editor.line_count;
    t = 0;
    if (num == 0) { tmp[t++] = '0'; }
    else {
        while (num > 0) { tmp[t++] = '0' + (num % 10); num /= 10; }
    }
    while (t > 0) { status[pos++] = tmp[--t]; }

    status[pos++] = ' ';
    status[pos++] = 'l';
    status[pos++] = 'i';
    status[pos++] = 'n';
    status[pos++] = 'h';
    status[pos++] = 'a';
    status[pos++] = 's';
    status[pos] = '\0';

    video_print_at(0, 24, "                                                                                ", 0x08);
    video_print_at(0, 24, status, 0x08);
}

static void editor_insert_char(char c) {
    if (editor.cursor_y >= editor.line_count) return;
    if (!editor.lines[editor.cursor_y]) return;

    str_insert(editor.lines[editor.cursor_y], editor.cursor_x, c);
    editor.cursor_x++;
    editor.modified = 1;
}

static void editor_backspace(void) {
    if (editor.cursor_x > 0) {
        str_remove(editor.lines[editor.cursor_y], editor.cursor_x - 1);
        editor.cursor_x--;
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
    }
    editor.modified = 1;
}

static void editor_delete(void) {
    if (editor.cursor_x < str_len(editor.lines[editor.cursor_y])) {
        str_remove(editor.lines[editor.cursor_y], editor.cursor_x);
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
}

static void editor_tab(void) {
    for (int i = 0; i < EDITOR_TAB_SIZE; i++) {
        editor_insert_char(' ');
    }
}

void editor_handle_key(uint8_t scancode) {
    if (!editor.running) return;

    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = 0;
        return;
    }
    if (scancode == 0x1D || scancode == 0xE0) {
        ctrl_pressed = 1;
        return;
    }
    if (scancode == 0x9D || scancode == 0xE0) {
        ctrl_pressed = 0;
        return;
    }

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

    if (scancode == 0x01) {
        editor_close();
        return;
    }

    if (scancode == 0x3C) {
        editor_save();
        return;
    }

    if (scancode == 0x47) {
        editor.cursor_x = 0;
        editor_draw();
        return;
    }
    if (scancode == 0x4F) {
        editor.cursor_x = str_len(editor.lines[editor.cursor_y]);
        editor_draw();
        return;
    }

    if (scancode == 0x48) {
        if (editor.cursor_y > 0) {
            editor.cursor_y--;
            uint32_t len = str_len(editor.lines[editor.cursor_y]);
            if (editor.cursor_x > len) {
                editor.cursor_x = len;
            }
        }
        editor_draw();
        return;
    }
    if (scancode == 0x50) {
        if (editor.cursor_y < editor.line_count - 1) {
            editor.cursor_y++;
            uint32_t len = str_len(editor.lines[editor.cursor_y]);
            if (editor.cursor_x > len) {
                editor.cursor_x = len;
            }
        }
        editor_draw();
        return;
    }
    if (scancode == 0x4B) {
        if (editor.cursor_x > 0) {
            editor.cursor_x--;
        } else if (editor.cursor_y > 0) {
            editor.cursor_y--;
            editor.cursor_x = str_len(editor.lines[editor.cursor_y]);
        }
        editor_draw();
        return;
    }
    if (scancode == 0x4D) {
        if (editor.cursor_x < str_len(editor.lines[editor.cursor_y])) {
            editor.cursor_x++;
        } else if (editor.cursor_y < editor.line_count - 1) {
            editor.cursor_y++;
            editor.cursor_x = 0;
        }
        editor_draw();
        return;
    }

    if (scancode == 0x49) {
        for (int i = 0; i < editor.view_height - 2; i++) {
            if (editor.cursor_y > 0) {
                editor.cursor_y--;
            }
        }
        uint32_t len = str_len(editor.lines[editor.cursor_y]);
        if (editor.cursor_x > len) editor.cursor_x = len;
        editor_draw();
        return;
    }
    if (scancode == 0x51) {
        for (int i = 0; i < editor.view_height - 2; i++) {
            if (editor.cursor_y < editor.line_count - 1) {
                editor.cursor_y++;
            }
        }
        uint32_t len = str_len(editor.lines[editor.cursor_y]);
        if (editor.cursor_x > len) editor.cursor_x = len;
        editor_draw();
        return;
    }

    if (scancode == 0x0E) {
        editor_backspace();
        editor_draw();
        return;
    }
    if (scancode == 0x53) {
        editor_delete();
        editor_draw();
        return;
    }

    if (scancode == 0x1C) {
        editor_newline();
        editor_draw();
        return;
    }

    if (scancode == 0x0F) {
        editor_tab();
        editor_draw();
        return;
    }

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
