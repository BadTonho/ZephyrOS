#include "taskmanager.h"
#include "video.h"
#include "keyboard.h"
#include "timer.h"
#include "memory.h"
#include "process.h"
#include "thread.h"
#include "panic.h"

#define TSKMGR_WIDTH  78
#define TSKMGR_HEIGHT 23
#define TSKMGR_START_X 1
#define TSKMGR_START_Y 1

#define COLOR_BG       0x07
#define COLOR_BORDER   0x08
#define COLOR_TITLE    0x0F
#define COLOR_HEADER   0x0B
#define COLOR_TEXT     0x07
#define COLOR_HIGHLIGHT 0x0E
#define COLOR_BAR_BG   0x08
#define COLOR_BAR_FG   0x0A
#define COLOR_BAR_WARN 0x0E
#define COLOR_BAR_CRIT 0x0C
#define COLOR_DEAD     0x0C

static int is_open = 0;
static int selected_tab = 0;
static int selected_row = 0;
static int scroll_offset = 0;

static uint32_t last_cpu_ticks = 0;
static uint32_t last_proc_ticks[64] = {0};
static uint32_t cpu_usage[64] = {0};

static void draw_box(int x, int y, int w, int h, uint8_t color);
static void draw_hline(int x, int y, int w, uint8_t color);
static void draw_vline(int x, int y, int h, uint8_t color);
static void draw_bar(int x, int y, int w, uint32_t filled, uint32_t total, uint8_t color_fg, uint8_t color_bg);
static void print_at(int x, int y, const char* str, uint8_t color);
static void print_num_at(int x, int y, uint32_t num, uint8_t color);
static int num_digits(uint32_t num);

extern process_t processes[];
extern uint32_t process_count;

void taskmgr_init(void) {
    is_open = 0;
    selected_tab = 0;
    selected_row = 0;
    scroll_offset = 0;
}

void taskmgr_open(void) {
    is_open = 1;
    selected_tab = 0;
    selected_row = 0;
    scroll_offset = 0;
    video_clear();
    taskmgr_refresh();
}

void taskmgr_close(void) {
    is_open = 0;
    video_clear();
    video_print("MiniOS Shell\n\n", 0x0B);
    video_print("minios> ", 0x0A);
}

static void draw_box(int x, int y, int w, int h, uint8_t color) {
    video_put_char_at(0xDA, color, x, y);
    video_put_char_at(0xC4, color, x + w - 1, y);
    video_put_char_at(0xB0, color, x, y + h - 1);
    video_put_char_at(0xD9, color, x + w - 1, y + h - 1);

    for (int i = 1; i < w - 1; i++) {
        video_put_char_at(0xC4, color, x + i, y);
        video_put_char_at(0xC4, color, x + i, y + h - 1);
    }
    for (int i = 1; i < h - 1; i++) {
        video_put_char_at(0xB3, color, x, y + i);
        video_put_char_at(0xB3, color, x + w - 1, y + i);
    }
}

static void print_at(int x, int y, const char* str, uint8_t color) {
    int i = 0;
    while (str[i]) {
        video_put_char_at(str[i], color, x + i, y);
        i++;
    }
}

static void print_num_at(int x, int y, uint32_t num, uint8_t color) {
    char buf[16];
    int i = 0;
    if (num == 0) { buf[i++] = '0'; }
    else {
        char tmp[16];
        int j = 0;
        while (num > 0) { tmp[j++] = '0' + (num % 10); num /= 10; }
        while (j > 0) { buf[i++] = tmp[--j]; }
    }
    buf[i] = '\0';
    print_at(x, y, buf, color);
}

static int num_digits(uint32_t num) {
    if (num == 0) return 1;
    int count = 0;
    while (num > 0) { count++; num /= 10; }
    return count;
}

static void draw_bar(int x, int y, int w, uint32_t filled, uint32_t total, uint8_t color_fg, uint8_t color_bg) {
    uint32_t bar_width = 0;
    if (total > 0) {
        bar_width = (filled * (w - 2)) / total;
        if (bar_width > (uint32_t)(w - 2)) bar_width = w - 2;
    }

    video_put_char_at(0xDB, color_bg, x, y);
    for (int i = 0; i < w - 2; i++) {
        if ((uint32_t)i < bar_width) {
            video_put_char_at(0xDB, color_fg, x + 1 + i, y);
        } else {
            video_put_char_at(0xB0, color_bg, x + 1 + i, y);
        }
    }
    video_put_char_at(0xDB, color_bg, x + w - 1, y);
}

static void draw_header(void) {
    print_at(TSKMGR_START_X + 2, TSKMGR_START_Y + 1, "MiniOS Task Manager", COLOR_TITLE);
    print_at(TSKMGR_START_X + TSKMGR_WIDTH - 22, TSKMGR_START_Y + 1, "Fechar: ESC", COLOR_BORDER);

    const char* tabs[] = {"[Processos]", "[Memoria]", "[Threads]"};
    int tab_x = TSKMGR_START_X + 3;
    for (int i = 0; i < 3; i++) {
        uint8_t color = (selected_tab == i) ? COLOR_HIGHLIGHT : COLOR_TEXT;
        if (selected_tab == i) {
            video_put_char_at(0x10, color, tab_x, TSKMGR_START_Y + 3);
        }
        print_at(tab_x + 1, TSKMGR_START_Y + 3, tabs[i], color);
        tab_x += 14;
    }
}

static void draw_processes(void) {
    int start_x = TSKMGR_START_X + 2;
    int start_y = TSKMGR_START_Y + 5;
    int table_w = TSKMGR_WIDTH - 4;

    draw_hline(start_x, start_y - 1, table_w, COLOR_BORDER);

    print_at(start_x, start_y, "PID", COLOR_HEADER);
    print_at(start_x + 6, start_y, "Nome", COLOR_HEADER);
    print_at(start_x + 22, start_y, "Estado", COLOR_HEADER);
    print_at(start_x + 34, start_y, "CPU%", COLOR_HEADER);
    print_at(start_x + 42, start_y, "Tempo", COLOR_HEADER);

    draw_hline(start_x, start_y + 1, table_w, COLOR_BORDER);

    uint32_t current_ticks = timer_get_ticks();
    uint32_t total_active = 0;

    for (int i = 0; i < 64; i++) {
        if (processes[i].state != PROCESS_STATE_UNUSED) {
            total_active++;
        }
    }

    int row = 0;
    for (int i = 0; i < 64 && row < 14; i++) {
        if (processes[i].state == PROCESS_STATE_UNUSED) continue;

        if (row >= scroll_offset) {
            int y = start_y + 2 + (row - scroll_offset);
            uint8_t name_color = COLOR_TEXT;

            if (selected_row == row) {
                for (int x = 0; x < table_w; x++) {
                    video_put_char_at(0x20, 0x10, start_x + x, y);
                }
                name_color = COLOR_HIGHLIGHT;
            }

            print_num_at(start_x, y, processes[i].pid, name_color);

            char name[13];
            int n = 0;
            while (processes[i].name[n] && n < 12) {
                name[n] = processes[i].name[n];
                n++;
            }
            name[n] = '\0';
            print_at(start_x + 6, y, name, name_color);

            const char* state_str = "?";
            uint8_t state_color = COLOR_TEXT;
            switch (processes[i].state) {
                case PROCESS_STATE_READY:    state_str = "Pronto";    state_color = COLOR_BAR_FG; break;
                case PROCESS_STATE_RUNNING:  state_str = "Rodando";   state_color = COLOR_HIGHLIGHT; break;
                case PROCESS_STATE_BLOCKED:  state_str = "Bloqueado"; state_color = COLOR_BAR_WARN; break;
                case PROCESS_STATE_ZOMBIE:   state_str = "Zombie";    state_color = COLOR_DEAD; break;
                default: state_str = "?"; break;
            }
            print_at(start_x + 22, y, state_str, state_color);

            if (current_ticks > last_cpu_ticks) {
                uint32_t delta_total = current_ticks - last_cpu_ticks;
                uint32_t delta_proc = processes[i].total_ticks - last_proc_ticks[i];
                if (delta_total > 0) {
                    cpu_usage[i] = (delta_proc * 100) / delta_total;
                }
            }

            uint8_t cpu_color = COLOR_TEXT;
            if (cpu_usage[i] > 80) cpu_color = COLOR_DEAD;
            else if (cpu_usage[i] > 50) cpu_color = COLOR_BAR_WARN;

            print_num_at(start_x + 34, y, cpu_usage[i], cpu_color);
            print_at(start_x + 37, y, "%", cpu_color);

            uint32_t ticks = processes[i].total_ticks;
            uint32_t secs = ticks / 50;
            print_num_at(start_x + 42, y, secs, COLOR_TEXT);
            print_at(start_x + 42 + num_digits(secs), y, "s", COLOR_TEXT);
        }
        row++;
    }

    last_cpu_ticks = current_ticks;
    for (int i = 0; i < 64; i++) {
        last_proc_ticks[i] = processes[i].total_ticks;
    }

    int info_y = start_y + 17;
    draw_hline(start_x, info_y, table_w, COLOR_BORDER);
    print_at(start_x, info_y + 1, "Processos: ", COLOR_TEXT);
    print_num_at(start_x + 12, info_y + 1, total_active, COLOR_HIGHLIGHT);

    print_at(start_x + 20, info_y + 1, "Navegar: Seta Cima/Baixo", COLOR_BORDER);
    print_at(start_x + 48, info_y + 1, "Matar: Del", COLOR_BORDER);
}

static void draw_memory(void) {
    int start_x = TSKMGR_START_X + 2;
    int start_y = TSKMGR_START_Y + 5;

    uint32_t total = memory_get_total();
    uint32_t used = memory_get_used();
    uint32_t free = memory_get_free();

    print_at(start_x, start_y, "Uso de Memoria", COLOR_HEADER);
    draw_hline(start_x, start_y + 1, TSKMGR_WIDTH - 4, COLOR_BORDER);

    print_at(start_x, start_y + 3, "Total:", COLOR_TEXT);
    print_num_at(start_x + 20, start_y + 3, total / 1024, COLOR_HIGHLIGHT);
    print_at(start_x + 20 + num_digits(total / 1024), start_y + 3, " KB", COLOR_TEXT);

    uint8_t bar_color = COLOR_BAR_FG;
    if (used * 100 / total > 80) bar_color = COLOR_BAR_CRIT;
    else if (used * 100 / total > 60) bar_color = COLOR_BAR_WARN;

    draw_bar(start_x, start_y + 5, 50, used, total, bar_color, COLOR_BAR_BG);

    print_at(start_x + 52, start_y + 5, "Usado:", COLOR_TEXT);
    print_num_at(start_x + 60, start_y + 5, used / 1024, bar_color);
    print_at(start_x + 60 + num_digits(used / 1024), start_y + 5, " KB", COLOR_TEXT);

    print_at(start_x, start_y + 7, "Livre:", COLOR_TEXT);
    print_num_at(start_x + 20, start_y + 7, free / 1024, COLOR_BAR_FG);
    print_at(start_x + 20 + num_digits(free / 1024), start_y + 7, " KB", COLOR_TEXT);

    draw_bar(start_x, start_y + 9, 50, free, total, COLOR_BAR_FG, COLOR_BAR_BG);

    uint32_t used_pct = (used * 100) / total;
    print_at(start_x, start_y + 11, "Porcentagem: ", COLOR_TEXT);
    print_num_at(start_x + 14, start_y + 11, used_pct, bar_color);
    print_at(start_x + 14 + num_digits(used_pct), start_y + 11, "% usado", COLOR_TEXT);

    draw_hline(start_x, start_y + 13, TSKMGR_WIDTH - 4, COLOR_BORDER);

    print_at(start_x, start_y + 15, "Heap:", COLOR_HEADER);
    print_at(start_x, start_y + 17, "  Inicio: 0x20000", COLOR_TEXT);
    print_at(start_x, start_y + 18, "  Tamanho: 1 MB", COLOR_TEXT);

    draw_hline(start_x, start_y + 20, TSKMGR_WIDTH - 4, COLOR_BORDER);
    print_at(start_x, start_y + 21, "Total de paginas: ", COLOR_TEXT);
    print_num_at(start_x + 19, start_y + 21, total / 4096, COLOR_HIGHLIGHT);
    print_at(start_x + 19 + num_digits(total / 4096), start_y + 21, " (4 KB cada)", COLOR_TEXT);
}

static void draw_threads(void) {
    int start_x = TSKMGR_START_X + 2;
    int start_y = TSKMGR_START_Y + 5;
    int table_w = TSKMGR_WIDTH - 4;

    draw_hline(start_x, start_y - 1, table_w, COLOR_BORDER);

    print_at(start_x, start_y, "TID", COLOR_HEADER);
    print_at(start_x + 6, start_y, "Nome", COLOR_HEADER);
    print_at(start_x + 24, start_y, "Estado", COLOR_HEADER);
    print_at(start_x + 36, start_y, "Pai", COLOR_HEADER);

    draw_hline(start_x, start_y + 1, table_w, COLOR_BORDER);

    int row = 0;
    for (uint32_t i = 0; i < 32; i++) {
        thread_t* t = thread_get_by_id(i + 1);
        if (t && row < 16) {
            int y = start_y + 2 + row;

            if (selected_row == row) {
                for (int x = 0; x < table_w; x++) {
                    video_put_char_at(0x20, 0x10, start_x + x, y);
                }
            }

            uint8_t name_color = (selected_row == row) ? COLOR_HIGHLIGHT : COLOR_TEXT;

            print_num_at(start_x, y, t->id, name_color);

            char name[17];
            int n = 0;
            while (t->name[n] && n < 16) {
                name[n] = t->name[n];
                n++;
            }
            name[n] = '\0';
            print_at(start_x + 6, y, name, name_color);

            const char* state_str = "?";
            uint8_t state_color = COLOR_TEXT;
            switch (t->state) {
                case THREAD_RUNNING:  state_str = "Rodando";   state_color = COLOR_HIGHLIGHT; break;
                case THREAD_BLOCKED:  state_str = "Bloqueado"; state_color = COLOR_BAR_WARN; break;
                case THREAD_FINISHED: state_str = "Finalizado"; state_color = COLOR_DEAD; break;
                default: state_str = "Unused"; break;
            }
            print_at(start_x + 24, y, state_str, state_color);

            print_at(start_x + 36, y, "-", COLOR_TEXT);

            row++;
        }
    }

    if (row == 0) {
        print_at(start_x + 10, start_y + 4, "Nenhuma thread ativa", COLOR_BORDER);
    }

    draw_hline(start_x, start_y + 2 + row + 1, table_w, COLOR_BORDER);
    print_at(start_x, start_y + 4 + row, "Total: ", COLOR_TEXT);
    print_num_at(start_x + 8, start_y + 4 + row, thread_get_count(), COLOR_HIGHLIGHT);
    print_at(start_x + 8 + num_digits(thread_get_count()), start_y + 4 + row, " threads", COLOR_TEXT);
}

void taskmgr_refresh(void) {
    if (!is_open) return;

    draw_box(TSKMGR_START_X, TSKMGR_START_Y, TSKMGR_WIDTH, TSKMGR_HEIGHT, COLOR_BORDER);

    for (int y = TSKMGR_START_Y + 1; y < TSKMGR_START_Y + TSKMGR_HEIGHT - 1; y++) {
        for (int x = TSKMGR_START_X + 1; x < TSKMGR_START_X + TSKMGR_WIDTH - 1; x++) {
            video_put_char_at(0x20, COLOR_BG, x, y);
        }
    }

    draw_header();

    switch (selected_tab) {
        case 0: draw_processes(); break;
        case 1: draw_memory(); break;
        case 2: draw_threads(); break;
    }

    video_put_char_at(0x18, COLOR_BORDER, TSKMGR_START_X + 2, TSKMGR_START_Y + TSKMGR_HEIGHT - 1);
    video_put_char_at(0x19, COLOR_BORDER, TSKMGR_START_X + TSKMGR_WIDTH - 3, TSKMGR_START_Y + TSKMGR_HEIGHT - 1);
    print_at(TSKMGR_START_X + 4, TSKMGR_START_Y + TSKMGR_HEIGHT - 1, "Navegar: Tab + Setas  |  Fechar: ESC", COLOR_BORDER);
}

void taskmgr_handle_key(uint8_t scancode) {
    if (!is_open) return;

    if (scancode == 0x01) {
        taskmgr_close();
        return;
    }

    if (scancode == 0x0F) {
        selected_tab = (selected_tab + 1) % 3;
        selected_row = 0;
        scroll_offset = 0;
        taskmgr_refresh();
        return;
    }

    if (scancode == 0x48) {
        if (selected_row > 0) {
            selected_row--;
            if (selected_row < scroll_offset) {
                scroll_offset = selected_row;
            }
        }
        taskmgr_refresh();
        return;
    }

    if (scancode == 0x50) {
        selected_row++;
        if (selected_row > 14) {
            scroll_offset++;
        }
        taskmgr_refresh();
        return;
    }

    if (scancode == 0x53 && selected_tab == 0) {
        int row = 0;
        for (int i = 0; i < 64; i++) {
            if (processes[i].state != PROCESS_STATE_UNUSED) {
                if (row == selected_row) {
                    if (processes[i].pid != 1) {
                        process_destroy(&processes[i]);
                        if (selected_row > 0) selected_row--;
                    }
                    break;
                }
                row++;
            }
        }
        taskmgr_refresh();
        return;
    }
}

int taskmgr_is_open(void) {
    return is_open;
}
