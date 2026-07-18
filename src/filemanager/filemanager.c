#include "filemanager.h"
#include "video.h"
#include "keyboard.h"
#include "fat12.h"
#include "memory.h"
#include "speaker.h"
#include "timer.h"
#include "taskbar.h"

static fm_state_t state;
static char input_buffer[32];
static int input_pos = 0;
static int input_mode = 0;
static int rename_mode = 0;
static int confirm_delete = 0;
static char old_name[FM_NAME_LEN];

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

static void name_to_fat12(const char* name, char* fat12_name) {
    int i, j;
    for (i = 0; i < 8; i++) fat12_name[i] = ' ';
    for (i = 8; i < 11; i++) fat12_name[i] = ' ';

    i = 0;
    j = 0;
    while (name[i] && name[i] != '.' && j < 8) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        fat12_name[j++] = c;
        i++;
    }

    if (name[i] == '.') {
        i++;
        j = 0;
        while (name[i] && j < 3) {
            char c = name[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            fat12_name[8 + j++] = c;
            i++;
        }
    }
}

static void fm_draw_title_bar(void) {
    video_fill_rect(0, 0, 80, 1, ' ', FM_TITLE_BAR_COLOR);
    video_print_at(30, 0, " MiniOS Explorer ", FM_TITLE_BAR_COLOR);
}

static void fm_draw_menu_bar(void) {
    video_fill_rect(0, 1, 80, 1, ' ', 0x70);
    video_print_at(1, 1, "F1=Ajuda F3=Ver F5=Atualizar F7=Novo F8=Excluir Esc=Sair", 0x70);
}

static void fm_draw_column_headers(void) {
    video_fill_rect(0, 3, 80, 1, ' ', 0x07);
    video_print_at(2, 3, "Nome", 0x0F);
    video_print_at(22, 3, "Tamanho", 0x0F);
    video_print_at(35, 3, "Tipo", 0x0F);
    video_draw_hline(0, 2, 80, 0xC4, 0x07);
}

static void fm_draw_separator_bottom(void) {
    video_draw_hline(0, 22, 80, 0xC4, 0x07);
}

static void fm_draw_status_bar(void) {
    video_fill_rect(0, 23, 80, 1, ' ', FM_STATUS_COLOR);

    if (state.file_count == 0) {
        video_print_at(2, 23, "Nenhum arquivo encontrado", FM_STATUS_COLOR);
        return;
    }

    video_print_at(2, 23, "Arquivo: ", FM_STATUS_COLOR);

    if (state.selected >= 0 && state.selected < state.file_count) {
        fm_file_entry_t* f = &state.files[state.selected];
        video_print_at(11, 23, f->name, FM_STATUS_COLOR);

        if (f->is_dir) {
            video_print_at(24, 23, "| Pasta", FM_STATUS_COLOR);
        } else {
            video_print_at(24, 23, "| Arquivo | ", FM_STATUS_COLOR);
            char buf[16];
            int_to_str(f->size, buf);
            video_print_at(36, 23, buf, FM_STATUS_COLOR);
            video_print_at(36 + (int)sizeof(buf), 23, " bytes", FM_STATUS_COLOR);
        }
    }

    video_print_at(60, 23, "Sel: ", FM_STATUS_COLOR);
    char buf[8];
    int_to_str(state.selected + 1, buf);
    video_print_at(65, 23, buf, FM_STATUS_COLOR);
    video_print_at(65 + (int)sizeof(buf), 23, "/", FM_STATUS_COLOR);
    int_to_str(state.file_count, buf);
    video_print_at(67 + (int)sizeof(buf), 23, buf, FM_STATUS_COLOR);
}

static void fm_refresh_files(void) {
    state.file_count = fat12_get_file_count();
    if (state.file_count > FM_MAX_FILES) {
        state.file_count = FM_MAX_FILES;
    }

    for (int i = 0; i < state.file_count; i++) {
        uint8_t attr;
        fat12_get_file_info(i, state.files[i].name, &state.files[i].size, &attr);
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
    int visible_count = 18;

    for (int i = 0; i < visible_count; i++) {
        int row = 4 + i;
        int file_idx = visible_start + i;

        video_fill_rect(2, row, 76, 1, ' ', 0x07);

        if (file_idx < state.file_count) {
            fm_file_entry_t* f = &state.files[file_idx];
            uint8_t name_color = f->is_dir ? FM_DIR_COLOR : FM_FILE_COLOR;

            if (file_idx == state.selected) {
                name_color = FM_SELECTED_COLOR;
                video_fill_rect(0, row, 80, 1, ' ', FM_SELECTED_COLOR);
            }

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

            video_print_at(2, row, display_name, name_color);

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
    video_draw_box(10, 3, 60, 18, 0x07);
    video_fill_rect(11, 4, 58, 16, ' ', 0x07);

    video_print_at(28, 4, "MiniOS Explorer - Ajuda", 0x0F);
    video_draw_hline(11, 5, 58, 0xC4, 0x07);

    video_print_at(13, 7,  "Setas      Navegar na lista", 0x07);
    video_print_at(13, 8,  "Enter      Abrir pasta/arquivo", 0x07);
    video_print_at(13, 9,  "Backspace  Atualizar lista", 0x07);
    video_print_at(13, 10, "F3         Visualizar conteudo", 0x07);
    video_print_at(13, 11, "F5         Atualizar lista", 0x07);
    video_print_at(13, 12, "F7         Criar novo arquivo", 0x07);
    video_print_at(13, 13, "F8         Excluir arquivo", 0x07);
    video_print_at(13, 14, "F2         Renomear arquivo", 0x07);
    video_print_at(13, 15, "Esc        Sair do Explorer", 0x07);

    video_draw_hline(11, 17, 58, 0xC4, 0x07);
    video_print_at(22, 18, "Pressione Esc para voltar...", 0x08);
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
    video_fill_rect(0, 0, 80, 1, ' ', 0x1F);
    video_print_at(30, 0, " Visualizando Arquivo ", 0x1F);

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

    char fat12_name[11];
    name_to_fat12(f->name, fat12_name);

    int bytes = fat12_read_file(fat12_name, buffer, 4095);
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
    for (int i = 0; i < bytes && row < 23; i++) {
        if (buffer[i] == '\n') {
            col = 0;
            row++;
        } else if (buffer[i] == '\r') {
            col = 0;
        } else if (buffer[i] == '\t') {
            col = (col + 8) & ~7;
            if (col >= 78) { col = 0; row++; }
        } else {
            video_put_char_at(buffer[i], 0x07, col, row);
            col++;
            if (col >= 78) {
                col = 0;
                row++;
            }
        }
    }

    video_fill_rect(0, 23, 80, 1, ' ', 0x70);
    video_print_at(2, 23, "Pressione Esc para voltar", 0x70);

    kfree(buffer);
}

static void fm_draw_all(void) {
    video_clear();
    fm_draw_title_bar();
    fm_draw_menu_bar();
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
    input_pos = 0;
    input_mode = 0;
    rename_mode = 0;
    confirm_delete = 0;
    old_name[0] = '\0';

    fm_refresh_files();
}

void fm_run(void) {
    fm_init();
    keyboard_callback_t prev_callback = keyboard_set_callback(fm_handle_key);
    taskbar_add_app(TB_APP_EXPLORER, "Explorer");
    fm_draw_all();

    while (state.running) {
        taskbar_update_clock();
        asm volatile("hlt");
    }

    taskbar_remove_app(TB_APP_EXPLORER);
    video_clear();
    keyboard_set_callback(prev_callback);
    taskbar_draw();
}

void fm_handle_key(uint8_t scancode) {
    if (taskbar_handle_key(scancode)) {
        return;
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

    if (scancode & 0x80) return;

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
                        char fat12_name[11];
                        name_to_fat12(state.files[state.selected].name, fat12_name);
                        fat12_delete_file(fat12_name);
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
                    char new_fat12_name[11];
                    name_to_fat12(input_buffer, new_fat12_name);

                    uint8_t* content = 0;
                    uint32_t size = 0;

                    if (!state.files[state.selected].is_dir && state.files[state.selected].size > 0) {
                        content = (uint8_t*)kmalloc(state.files[state.selected].size);
                        if (content) {
                            char old_fat12[11];
                            name_to_fat12(old_name, old_fat12);
                            int bytes = fat12_read_file(old_fat12, content, state.files[state.selected].size);
                            if (bytes > 0) {
                                size = bytes;
                            } else {
                                kfree(content);
                                content = 0;
                            }
                        }
                    }

                    char old_fat12[11];
                    name_to_fat12(old_name, old_fat12);
                    fat12_delete_file(old_fat12);

                    if (content && size > 0) {
                        fat12_write_file(new_fat12_name, content, size);
                        kfree(content);
                    }

                    fm_refresh_files();
                }
                rename_mode = 0;
                input_mode = 0;
                fm_draw_all();
                return;
            }

            if (input_pos > 0) {
                char fat12_name[11];
                name_to_fat12(input_buffer, fat12_name);
                uint8_t empty_data[1] = {0};
                fat12_write_file(fat12_name, empty_data, 0);
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

    if (scancode == 0x01) {
        state.running = 0;
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
                fm_refresh_files();
                fm_draw_all();
            } else {
                fm_draw_view_file();
            }
        }
        return;
    }

    if (scancode == 0x0E) {
        fm_refresh_files();
        fm_draw_all();
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
            if (state.selected >= state.scroll_offset + 18) {
                state.scroll_offset = state.selected - 17;
            }
        }
        fm_draw_file_list();
        fm_draw_status_bar();
        return;
    }

    if (scancode == 0x49) {
        state.selected -= 18;
        if (state.selected < 0) state.selected = 0;
        if (state.selected < state.scroll_offset) {
            state.scroll_offset = state.selected;
        }
        fm_draw_file_list();
        fm_draw_status_bar();
        return;
    }

    if (scancode == 0x51) {
        state.selected += 18;
        if (state.selected >= state.file_count) {
            state.selected = state.file_count - 1;
        }
        if (state.selected >= state.scroll_offset + 18) {
            state.scroll_offset = state.selected - 17;
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
        if (state.selected >= 18) {
            state.scroll_offset = state.selected - 17;
        }
        fm_draw_file_list();
        fm_draw_status_bar();
        return;
    }
}
