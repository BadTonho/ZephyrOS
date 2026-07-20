#include "ui/filemanager.h"
#include "core/video.h"
#include "core/keyboard.h"
#include "fs/fs.h"
#include "core/memory.h"
#include "ui/taskbar.h"
#include "ui/icons.h"
#include "core/errors.h"
#include "core/log.h"

static fm_state_t state;
static char input_buffer[32];
static int input_pos = 0;
static int input_mode = 0;
static int rename_mode = 0;
static int confirm_delete = 0;
static char old_name[FM_NAME_LEN];

static void fm_refresh_files(void);
static void fm_draw_all(void);
static void fm_draw_address_bar(void);
static void fm_draw_file_list(void);
static void fm_draw_status_bar(void);
static void fm_draw_help(void);
static void fm_draw_create_file(void);
static void fm_draw_rename_file(void);
static void fm_draw_confirm_delete(void);
static void fm_draw_view_file(void);

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

static int str_len(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void str_copy(char* dst, const char* src) {
    int i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int fm_join_path(char* dst, const char* base, const char* name) {
    if (!dst || !base || !name) return ERR_NULL;

    int base_len = str_len(base);
    int name_len = str_len(name);
    int separator = (base_len > 0) ? 1 : 0;
    if (base_len + separator + name_len >= FM_MAX_PATH) {
        LOG_ERROR("FM", "Caminho excede o limite do File Manager");
        return ERR_OVERFLOW;
    }

    int pos = 0;
    for (int i = 0; i < base_len; i++) dst[pos++] = base[i];
    if (separator) dst[pos++] = '/';
    for (int i = 0; i < name_len; i++) dst[pos++] = name[i];
    dst[pos] = '\0';
    return OK;
}

static int str_equal(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void fm_push_history(void) {
    if (state.history_count < FM_MAX_HISTORY) {
        str_copy(state.history[state.history_count], state.current_path);
        state.history_count++;
        state.history_pos = state.history_count - 1;
    } else {
        for (int i = 0; i < FM_MAX_HISTORY - 1; i++) {
            str_copy(state.history[i], state.history[i + 1]);
        }
        str_copy(state.history[FM_MAX_HISTORY - 1], state.current_path);
        state.history_pos = FM_MAX_HISTORY - 1;
    }
}

static void fm_navigate_to(const char* path) {
    fm_push_history();
    str_copy(state.current_path, path);
    state.selected = 0;
    state.scroll_offset = 0;
    fm_refresh_files();
    fm_draw_all();
}

static void fm_navigate_to_no_history(const char* path) {
    str_copy(state.current_path, path);
    state.selected = 0;
    state.scroll_offset = 0;
    fm_refresh_files();
    fm_draw_all();
}

static void fm_go_up(void) {
    if (state.current_path[0] == '\0') return;

    int len = str_len(state.current_path);
    if (len > 0 && state.current_path[len - 1] == '/') {
        state.current_path[len - 1] = '\0';
        len--;
    }

    int last_slash = -1;
    for (int i = 0; i < len; i++) {
        if (state.current_path[i] == '/') last_slash = i;
    }

    if (last_slash < 0) {
        state.current_path[0] = '\0';
    } else if (last_slash == 0) {
        state.current_path[0] = '/';
        state.current_path[1] = '\0';
    } else {
        state.current_path[last_slash] = '\0';
    }

    state.selected = 0;
    state.scroll_offset = 0;
    fm_refresh_files();
    fm_draw_all();
}

static void fm_go_back(void) {
    if (state.history_pos > 0) {
        state.history_pos--;
        str_copy(state.current_path, state.history[state.history_pos]);
        state.selected = 0;
        state.scroll_offset = 0;
        fm_refresh_files();
        fm_draw_all();
    }
}

static void fm_go_forward(void) {
    if (state.history_pos < state.history_count - 1) {
        state.history_pos++;
        str_copy(state.current_path, state.history[state.history_pos]);
        state.selected = 0;
        state.scroll_offset = 0;
        fm_refresh_files();
        fm_draw_all();
    }
}

static void fm_draw_title_bar(void) {
    video_fill_rect(0, 0, SCREEN_COLS, 1, ' ', FM_TITLE_BAR_COLOR);
    video_print_at((SCREEN_COLS - 20) / 2, 0, " ZephyrOS Explorer ", FM_TITLE_BAR_COLOR);
}

static void fm_draw_menu_bar(void) {
    video_fill_rect(0, 1, SCREEN_COLS, 1, ' ', 0x70);
    video_print_at(1, 1, "F1=Ajuda F3=Ver F5=Atualizar F7=Novo F8=Excluir Esc=Sair", 0x70);
}

static void fm_draw_address_bar(void) {
    video_fill_rect(0, 2, SCREEN_COLS, 1, ' ', FM_ADDRESS_COLOR);
    video_print_at(1, 2, ">", FM_ADDRESS_COLOR);

    if (state.address_mode) {
        video_fill_rect(3, 2, SCREEN_COLS - 4, 1, ' ', FM_ADDRESS_INPUT_COLOR);
        video_print_at(3, 2, state.address_buffer, FM_ADDRESS_INPUT_COLOR);
    } else {
        char display_path[78];
        display_path[0] = 'C';
        display_path[1] = ':';

        if (state.current_path[0] == '\0') {
            display_path[2] = '\\';
            display_path[3] = '\0';
        } else {
            display_path[2] = '\\';
            int di = 3;
            for (int i = 0; state.current_path[i] && di < SCREEN_COLS - 4; i++) {
                display_path[di++] = state.current_path[i];
            }
            display_path[di] = '\0';
        }

        int plen = str_len(display_path);
        video_fill_rect(3, 2, SCREEN_COLS - 4, 1, ' ', FM_ADDRESS_COLOR);
        video_print_at(3, 2, display_path, FM_ADDRESS_COLOR);

        if (state.current_path[0] != '\0') {
            video_print_at(3 + plen, 2, "\\", FM_ADDRESS_COLOR);
        }
    }
}

static void fm_draw_column_headers(void) {
    video_fill_rect(0, 3, SCREEN_COLS, 1, ' ', 0x07);
    video_print_at(2, 3, "Nome", 0x0F);
    video_print_at(22, 3, "Tamanho", 0x0F);
    video_print_at(35, 3, "Tipo", 0x0F);
    video_draw_hline(0, 4, SCREEN_COLS, 0xC4, 0x07);
}

static void fm_draw_separator_bottom(void) {
    video_draw_hline(0, SCREEN_ROWS - 3, SCREEN_COLS, 0xC4, 0x07);
}

static void fm_draw_status_bar(void) {
    video_fill_rect(0, SCREEN_ROWS - 2, SCREEN_COLS, 1, ' ', FM_STATUS_COLOR);

    if (state.file_count == 0) {
        video_print_at(2, SCREEN_ROWS - 2, "Nenhum arquivo encontrado", FM_STATUS_COLOR);
        return;
    }

    video_print_at(2, SCREEN_ROWS - 2, "Arquivo: ", FM_STATUS_COLOR);

    if (state.selected >= 0 && state.selected < state.file_count) {
        fm_file_entry_t* f = &state.files[state.selected];
        video_print_at(11, SCREEN_ROWS - 2, f->name, FM_STATUS_COLOR);

        if (f->is_dir) {
            video_print_at(24, SCREEN_ROWS - 2, "| Pasta", FM_STATUS_COLOR);
        } else {
            video_print_at(24, SCREEN_ROWS - 2, "| Arquivo | ", FM_STATUS_COLOR);
            char buf[16];
            int_to_str(f->size, buf);
            video_print_at(36, SCREEN_ROWS - 2, buf, FM_STATUS_COLOR);
            int blen = str_len(buf);
            video_print_at(36 + blen, SCREEN_ROWS - 2, " bytes", FM_STATUS_COLOR);
        }
    }

    video_print_at(60, SCREEN_ROWS - 2, "Sel: ", FM_STATUS_COLOR);
    char buf[8];
    int_to_str(state.selected + 1, buf);
    video_print_at(65, SCREEN_ROWS - 2, buf, FM_STATUS_COLOR);
    int blen = str_len(buf);
    video_print_at(65 + blen, SCREEN_ROWS - 2, "/", FM_STATUS_COLOR);
    int_to_str(state.file_count, buf);
    video_print_at(66 + blen, SCREEN_ROWS - 2, buf, FM_STATUS_COLOR);
}

static void fm_refresh_files(void) {
    state.file_count = fs_get_file_count_at(state.current_path);
    if (state.file_count > FM_MAX_FILES) {
        state.file_count = FM_MAX_FILES;
    }

    for (int i = 0; i < state.file_count; i++) {
        uint8_t attr;
        fs_get_file_info_at(state.current_path, i, state.files[i].name, &state.files[i].size, &attr);
        state.files[i].attributes = attr;
        state.files[i].is_dir = (attr & 0x10) ? 1 : 0;
    }

    if (state.selected >= state.file_count) {
        state.selected = state.file_count - 1;
    }
    if (state.selected < 0) {
        state.selected = 0;
    }
}

static void fm_draw_file_list(void) {
    int visible_start = state.scroll_offset;
    int visible_count = 17;

    for (int i = 0; i < visible_count; i++) {
        int row = 5 + i;
        int file_idx = visible_start + i;

        video_fill_rect(2, row, 76, 1, ' ', 0x07);

        if (file_idx < state.file_count) {
            fm_file_entry_t* f = &state.files[file_idx];
            icon_entry_t* icon = icons_get_fm(f->is_dir ? ICON_FM_FOLDER : ICON_FM_FILE);
            uint8_t name_color = icon->color;

            if (file_idx == state.selected) {
                name_color = FM_SELECTED_COLOR;
                video_fill_rect(0, row, SCREEN_COLS, 1, ' ', FM_SELECTED_COLOR);
            }

            video_put_char_at(icon->ch, name_color, 2, row);
            video_put_char_at(icon->ch, name_color, 3, row);

            char display_name[FM_NAME_LEN + 4];
            int d = 0;

            if (f->is_dir) {
                display_name[d++] = '[';
                for (int j = 0; f->name[j] && d < 10; j++) {
                    display_name[d++] = f->name[j];
                }
                display_name[d++] = ']';
                while (d < 12) display_name[d++] = ' ';
            } else {
                for (int j = 0; f->name[j] && d < 12; j++) {
                    display_name[d++] = f->name[j];
                }
                while (d < 12) display_name[d++] = ' ';
            }
            display_name[d] = '\0';

            video_print_at(5, row, display_name, name_color);

            if (!f->is_dir) {
                char buf[12];
                int_to_str(f->size, buf);
                video_print_at(22, row, buf, file_idx == state.selected ? FM_SELECTED_COLOR : FM_SIZE_COLOR);
                video_print_at(35, row, "Arquivo", file_idx == state.selected ? FM_SELECTED_COLOR : 0x08);
            } else {
                video_print_at(35, row, "Pasta", file_idx == state.selected ? FM_SELECTED_COLOR : 0x0B);
            }
        }
    }
}

static void fm_draw_help(void) {
    video_clear();
    video_draw_box(10, 3, 60, 19, 0x07);
    video_fill_rect(11, 4, 58, 17, ' ', 0x07);

    video_print_at(28, 4, "ZephyrOS Explorer - Ajuda", 0x0F);
    video_draw_hline(11, 5, 58, 0xC4, 0x07);

    video_print_at(13, 7,  "Setas      Navegar na lista", 0x07);
    video_print_at(13, 8,  "Enter      Abrir pasta/arquivo", 0x07);
    video_print_at(13, 9,  "Backspace  Subir um nivel", 0x07);
    video_print_at(13, 10, "Alt+<-     Voltar", 0x07);
    video_print_at(13, 11, "Alt+->     Avancar", 0x07);
    video_print_at(13, 12, "F2         Renomear arquivo", 0x07);
    video_print_at(13, 13, "F3         Visualizar conteudo", 0x07);
    video_print_at(13, 14, "F5         Atualizar lista", 0x07);
    video_print_at(13, 15, "F7         Criar novo arquivo", 0x07);
    video_print_at(13, 16, "F8         Excluir arquivo", 0x07);
    video_print_at(13, 17, "Ctrl+L     Digitar caminho", 0x07);
    video_print_at(13, 18, "Esc        Sair do Explorer", 0x07);

    video_draw_hline(11, 20, 58, 0xC4, 0x07);
    video_print_at(22, 21, "Pressione Esc para voltar...", 0x08);
}

static void fm_draw_create_file(void) {
    video_fill_rect(10, 10, 60, 3, ' ', 0x17);
    video_draw_box(10, 10, 60, 3, 0x17);
    video_print_at(12, 11, "Novo arquivo: ", 0x17);
}

static void fm_draw_rename_file(void) {
    video_fill_rect(10, 10, 60, 3, ' ', 0x17);
    video_draw_box(10, 10, 60, 3, 0x17);
    video_print_at(12, 11, "Renomear para: ", 0x17);
}

static void fm_draw_confirm_delete(void) {
    video_fill_rect(15, 10, 50, 3, ' ', 0x47);
    video_draw_box(15, 10, 50, 3, 0x47);
    video_print_at(17, 11, "Excluir? (S/N)", 0x47);
}

static void fm_draw_view_file(void) {
    video_clear();
    video_fill_rect(0, 0, SCREEN_COLS, 1, ' ', 0x1F);
    video_print_at((SCREEN_COLS - 22) / 2, 0, " Visualizando Arquivo ", 0x1F);

    if (state.selected < 0 || state.selected >= state.file_count) return;

    fm_file_entry_t* f = &state.files[state.selected];
    if (f->is_dir) {
        video_print_at(2, 2, "Nao e possivel visualizar pastas.", 0x0C);
        video_print_at(2, 4, "Pressione Esc para voltar.", 0x08);
        return;
    }

    uint8_t* buffer = (uint8_t*)kmalloc(4096);
    if (!buffer) {
        video_print_at(2, 2, "Erro: sem memoria!", 0x0C);
        return;
    }

    char file_path[FM_MAX_PATH];
    if (fm_join_path(file_path, state.current_path, f->name) != OK) {
        video_print_at(2, 2, "Erro: caminho muito longo.", 0x0C);
        kfree(buffer);
        return;
    }

    int bytes = fs_read_file_at(file_path, buffer, 4095);
    if (bytes < 0) {
        video_print_at(2, 2, "Erro ao ler arquivo: ", 0x0C);
        video_print_at(22, 2, f->name, 0x0C);
        video_print_at(2, 4, "Pressione Esc para voltar.", 0x08);
        kfree(buffer);
        return;
    }

    if (bytes == 0) {
        video_print_at(2, 2, "(arquivo vazio)", 0x08);
        video_print_at(2, 4, "Pressione Esc para voltar.", 0x08);
        kfree(buffer);
        return;
    }

    buffer[bytes] = '\0';
    int row = 2;
    int col = 0;
    for (int i = 0; i < bytes && row < SCREEN_ROWS - 2; i++) {
        if (buffer[i] == '\n') {
            col = 0;
            row++;
        } else if (buffer[i] == '\r') {
            col = 0;
        } else if (buffer[i] == '\t') {
            col = (col + 8) & ~7;
            if (col >= SCREEN_COLS - 2) { col = 0; row++; }
        } else {
            video_put_char_at(buffer[i], 0x07, col, row);
            col++;
            if (col >= SCREEN_COLS - 2) {
                col = 0;
                row++;
            }
        }
    }

    video_fill_rect(0, SCREEN_ROWS - 2, SCREEN_COLS, 1, ' ', 0x70);
    video_print_at(2, SCREEN_ROWS - 2, "Pressione Esc para voltar", 0x70);

    kfree(buffer);
}

static void fm_draw_all(void) {
    video_clear();
    fm_draw_title_bar();
    fm_draw_menu_bar();
    fm_draw_address_bar();
    fm_draw_column_headers();
    fm_draw_separator_bottom();
    fm_draw_status_bar();
    fm_draw_file_list();
}

void fm_init(void) {
    state.selected = 0;
    state.scroll_offset = 0;
    state.view_mode = 0;
    state.running = 1;
    state.current_path[0] = '\0';
    state.history_count = 0;
    state.history_pos = 0;
    state.address_mode = 0;
    state.address_pos = 0;
    input_pos = 0;
    input_mode = 0;
    rename_mode = 0;
    confirm_delete = 0;
    old_name[0] = '\0';

    fm_push_history();
    fm_refresh_files();
}

static keyboard_callback_t fm_prev_callback = 0;

void fm_open(void) {
    fm_init();
    fm_prev_callback = keyboard_set_callback(fm_handle_key);
    taskbar_add_app(TB_APP_EXPLORER, "Explorer");
    fm_draw_all();
}

void fm_close(void) {
    state.running = 0;
    taskbar_remove_app(TB_APP_EXPLORER);
    keyboard_set_callback(fm_prev_callback);
    video_clear();
    video_print("ZephyrOS Shell\n\n", 0x0B);
    video_print("zephyr> ", 0x0A);
    taskbar_draw();
}

int fm_is_running(void) {
    return state.running;
}

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

static int alt_pressed = 0;

void fm_handle_key(uint8_t scancode) {
    if (taskbar_handle_config_key(scancode)) return;
    if (taskbar_handle_key(scancode)) return;

    if (scancode == 0x38) {
        alt_pressed = 1;
        return;
    }
    if (scancode == 0x38 + 0x80) {
        alt_pressed = 0;
        return;
    }

    if (scancode & 0x80) return;

    if (state.address_mode) {
        char c = scancode_table[scancode];

        if (scancode == 0x0E) {
            if (state.address_pos > 0) {
                state.address_pos--;
                state.address_buffer[state.address_pos] = '\0';
                int cx = 3 + state.address_pos;
                video_put_char_at(' ', FM_ADDRESS_INPUT_COLOR, cx, 2);
            }
            return;
        }

        if (scancode == 0x1C) {
            state.address_buffer[state.address_pos] = '\0';
            state.address_mode = 0;

            if (state.address_pos > 0) {
                char new_path[FM_MAX_PATH];
                int pi = 0;

                if (state.address_buffer[0] == 'c' || state.address_buffer[0] == 'C') {
                    if (state.address_buffer[1] == ':') {
                        pi = 2;
                        if (state.address_buffer[2] == '\\') pi = 3;
                    }
                }

                for (int i = pi; state.address_buffer[i]; i++) {
                    new_path[pi - pi + i - pi] = state.address_buffer[i];
                }
                int len = str_len(state.address_buffer) - pi;
                new_path[len] = '\0';

                if (len == 0 || str_equal(new_path, "\\")) {
                    new_path[0] = '\0';
                }

                if (new_path[0] == '\\') {
                    int shifted = 0;
                    for (int i = 1; new_path[i]; i++) {
                        new_path[shifted++] = new_path[i];
                    }
                    new_path[shifted] = '\0';
                }

                fm_navigate_to(new_path);
            } else {
                fm_draw_all();
            }
            return;
        }

        if (scancode == 0x01) {
            state.address_mode = 0;
            fm_draw_all();
            return;
        }

        if (c && state.address_pos < FM_MAX_PATH - 1) {
            state.address_buffer[state.address_pos++] = c;
            state.address_buffer[state.address_pos] = '\0';
            int cx = 3 + state.address_pos - 1;
            video_put_char_at(c, FM_ADDRESS_INPUT_COLOR, cx, 2);
        }
        return;
    }

    if (input_mode) {
        char c = scancode_table[scancode];

        if (scancode == 0x0E) {
            if (input_pos > 0) {
                input_pos--;
                input_buffer[input_pos] = '\0';
                int cx = 12 + input_pos;
                video_put_char_at(' ', 0x17, cx, 11);
            }
            return;
        }

        if (scancode == 0x1C) {
            input_buffer[input_pos] = '\0';

            if (confirm_delete) {
                if (input_buffer[0] == 's' || input_buffer[0] == 'S') {
                    if (state.selected >= 0 && state.selected < state.file_count) {
                        fs_delete_file_in_dir(state.current_path, state.files[state.selected].name);
                        fm_refresh_files();
                    }
                }
                confirm_delete = 0;
                input_mode = 0;
                fm_draw_all();
                return;
            }

            if (rename_mode) {
                if (input_pos > 0 && state.selected >= 0 && state.selected < state.file_count) {
                    uint8_t* content = 0;
                    uint32_t size = 0;

                    if (!state.files[state.selected].is_dir && state.files[state.selected].size > 0) {
                        content = (uint8_t*)kmalloc(state.files[state.selected].size);
                        if (content) {
                            char file_path[FM_MAX_PATH];
                            if (fm_join_path(file_path, state.current_path, old_name) == OK) {
                                int bytes = fs_read_file_at(file_path, content, state.files[state.selected].size);
                                if (bytes > 0) size = bytes;
                            }
                            if (size == 0) {
                                kfree(content);
                                content = 0;
                            }
                        }
                    }

                    fs_delete_file_in_dir(state.current_path, old_name);

                    if (content && size > 0) {
                        fs_write_file_in_dir(state.current_path, input_buffer, content, size);
                        kfree(content);
                    } else {
                        uint8_t empty[1] = {0};
                        fs_write_file_in_dir(state.current_path, input_buffer, empty, 0);
                    }

                    fm_refresh_files();
                }
                rename_mode = 0;
                input_mode = 0;
                fm_draw_all();
                return;
            }

            if (input_pos > 0) {
                uint8_t empty_data[1] = {0};
                fs_write_file_in_dir(state.current_path, input_buffer, empty_data, 0);
                fm_refresh_files();
            }

            input_mode = 0;
            fm_draw_all();
            return;
        }

        if (scancode == 0x01) {
            input_mode = 0;
            rename_mode = 0;
            confirm_delete = 0;
            fm_draw_all();
            return;
        }

        if (c && input_pos < 31) {
            input_buffer[input_pos++] = c;
            input_buffer[input_pos] = '\0';
            int cx = 12 + input_pos - 1;
            video_put_char_at(c, 0x17, cx, 11);
        }
        return;
    }

    if (alt_pressed) {
        if (scancode == 0x4B) {
            alt_pressed = 0;
            fm_go_back();
            return;
        }
        if (scancode == 0x4D) {
            alt_pressed = 0;
            fm_go_forward();
            return;
        }
    }

    if (scancode == 0x01) {
        fm_close();
        return;
    }

    if (scancode == 0x26) {
        state.address_mode = 1;
        state.address_pos = 0;
        state.address_buffer[0] = '\0';
        fm_draw_address_bar();
        return;
    }

    if (scancode == 0x3B) {
        fm_draw_help();
        return;
    }

    if (scancode == 0x3D) {
        fm_draw_view_file();
        return;
    }

    if (scancode == 0x3E) {
        fm_refresh_files();
        fm_draw_all();
        return;
    }

    if (scancode == 0x40) {
        input_mode = 1;
        input_pos = 0;
        input_buffer[0] = '\0';
        fm_draw_create_file();
        return;
    }

    if (scancode == 0x42) {
        if (state.selected >= 0 && state.selected < state.file_count) {
            input_mode = 1;
            confirm_delete = 1;
            input_pos = 0;
            input_buffer[0] = '\0';
            fm_draw_confirm_delete();
        }
        return;
    }

    if (scancode == 0x3C) {
        if (state.selected >= 0 && state.selected < state.file_count) {
            input_mode = 1;
            rename_mode = 1;
            input_pos = 0;
            input_buffer[0] = '\0';

            int i = 0;
            for (; state.files[state.selected].name[i]; i++) {
                old_name[i] = state.files[state.selected].name[i];
            }
            old_name[i] = '\0';

            fm_draw_rename_file();
            video_print_at(27, 11, state.files[state.selected].name, 0x17);
            input_pos = 0;
            for (i = 0; state.files[state.selected].name[i]; i++) {
                input_buffer[input_pos++] = state.files[state.selected].name[i];
            }
            input_buffer[input_pos] = '\0';
        }
        return;
    }

    if (scancode == 0x1C) {
        if (state.selected >= 0 && state.selected < state.file_count) {
            fm_file_entry_t* f = &state.files[state.selected];
            if (f->is_dir) {
                char new_path[FM_MAX_PATH];
                if (fm_join_path(new_path, state.current_path, f->name) != OK) {
                    video_print_at(2, SCREEN_ROWS - 2, "Erro: caminho muito longo.", 0x0C);
                    return;
                }
                fm_navigate_to(new_path);
            } else {
                fm_draw_view_file();
            }
        }
        return;
    }

    if (scancode == 0x0E) {
        fm_go_up();
        return;
    }

    if (state.file_count == 0) return;

    if (scancode == 0x48) {
        if (state.selected > 0) {
            state.selected--;
            if (state.selected < state.scroll_offset) {
                state.scroll_offset = state.selected;
            }
        }
        fm_draw_file_list();
        fm_draw_status_bar();
        return;
    }

    if (scancode == 0x50) {
        if (state.selected < state.file_count - 1) {
            state.selected++;
            if (state.selected >= state.scroll_offset + 17) {
                state.scroll_offset = state.selected - 16;
            }
        }
        fm_draw_file_list();
        fm_draw_status_bar();
        return;
    }

    if (scancode == 0x49) {
        state.selected -= 17;
        if (state.selected < 0) state.selected = 0;
        if (state.selected < state.scroll_offset) {
            state.scroll_offset = state.selected;
        }
        fm_draw_file_list();
        fm_draw_status_bar();
        return;
    }

    if (scancode == 0x51) {
        state.selected += 17;
        if (state.selected >= state.file_count) {
            state.selected = state.file_count - 1;
        }
        if (state.selected >= state.scroll_offset + 17) {
            state.scroll_offset = state.selected - 16;
        }
        fm_draw_file_list();
        fm_draw_status_bar();
        return;
    }

    if (scancode == 0x47) {
        state.selected = 0;
        state.scroll_offset = 0;
        fm_draw_file_list();
        fm_draw_status_bar();
        return;
    }

    if (scancode == 0x4F) {
        state.selected = state.file_count - 1;
        if (state.selected >= 17) {
            state.scroll_offset = state.selected - 16;
        }
        fm_draw_file_list();
        fm_draw_status_bar();
        return;
    }
}
