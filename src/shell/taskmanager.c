#include "apps/taskmanager.h"
#include "core/video.h"
#include "core/keyboard.h"
#include "core/timer.h"
#include "core/memory.h"
#include "process/process.h"
#include "process/thread.h"
#include "core/panic.h"
#include "apps/shell.h"
#include "ui/taskbar.h"
#include "ui/desktop.h"
#include "ui/filemanager.h"
#include "ui/settings.h"
#include "drivers/ata.h"
#include "drivers/vesa.h"
#include "drivers/font.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/errors.h"
#include "ui/gui.h"

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
#define COLOR_SELECTION 0x1F

#define TSKMGR_TICKS_PER_SECOND 50U
#define TSKMGR_CLASSIC_PROCESS_ROWS 13
#define TSKMGR_CLASSIC_THREAD_ROWS 11

#define TSKMGR_GUI_TASKBAR_HEIGHT 24
#define TSKMGR_GUI_MIN_WIDTH 600
#define TSKMGR_GUI_MIN_HEIGHT 420
#define TSKMGR_GUI_DEFAULT_WIDTH 760
#define TSKMGR_GUI_DEFAULT_HEIGHT 520
#define TSKMGR_GUI_TITLE_HEIGHT 22
#define TSKMGR_GUI_CONTROL_SIZE 16
#define TSKMGR_GUI_MARGIN 12
#define TSKMGR_GUI_TAB_HEIGHT 24
#define TSKMGR_GUI_ROW_HEIGHT 24
#define TSKMGR_GUI_MAX_VISIBLE_ROWS 14
#define TSKMGR_GUI_DETAIL_WIDTH 250
#define TSKMGR_GUI_DETAIL_MIN_WIDTH 210
#define TSKMGR_GUI_LIST_MIN_WIDTH 360

static int is_open = 0;
static int selected_tab = 0;
static int selected_row = 0;
static int scroll_offset = 0;
static int sort_column = 0;
static int show_properties = 0;
static int prop_pid = 0;

static uint32_t last_cpu_ticks = 0;
static uint32_t last_proc_ticks[64] = {0};
static uint32_t cpu_usage[64] = {0};

static int gui_open = 0;
static int gui_minimized = 0;
static int gui_maximized = 0;
static int gui_drag_active = 0;
static int gui_drag_offset_x = 0;
static int gui_drag_offset_y = 0;
static int gui_x = 40;
static int gui_y = 36;
static int gui_width = TSKMGR_GUI_DEFAULT_WIDTH;
static int gui_height = TSKMGR_GUI_DEFAULT_HEIGHT;
static int gui_restore_x = 40;
static int gui_restore_y = 36;
static int gui_restore_width = TSKMGR_GUI_DEFAULT_WIDTH;
static int gui_restore_height = TSKMGR_GUI_DEFAULT_HEIGHT;
static uint32_t gui_last_tick = 0;

extern process_t processes[];
extern uint32_t process_count;

static void taskmgr_gui_draw(void);
static void taskmgr_gui_draw_tabs(void);
static void taskmgr_gui_draw_processes(void);
static void taskmgr_gui_draw_memory(void);
static void taskmgr_gui_draw_threads(void);
static void taskmgr_gui_draw_properties(void);
static void taskmgr_gui_copy_text(char* destination, int size, const char* source);
static void taskmgr_update_cpu_metrics(void);
static int taskmgr_get_work_bottom(void);
static int taskmgr_get_work_top(void);
static void taskmgr_clamp_window(void);

static const char* taskmgr_process_type(const process_t* process);
static const char* taskmgr_process_state_name(process_state_t state);
static const char* taskmgr_thread_state_name(thread_state_t state);
static void taskmgr_count_process_states(uint32_t* ready, uint32_t* running,
                                         uint32_t* blocked, uint32_t* zombie);
static void taskmgr_print_hex_at(int x, int y, uint32_t value, uint8_t color);
static void taskmgr_gui_draw_hex(int x, int y, uint32_t value, uint32_t color);
static uint32_t taskmgr_pages_used(uint32_t total, uint32_t free_pages);
static uint32_t taskmgr_percent(uint32_t value, uint32_t total);
static thread_t* taskmgr_find_thread_by_row(int row);

static vesa_color_t taskmgr_gui_color(uint32_t raw) {
    vesa_color_t color;
    color.raw = raw;
    return color;
}

void taskmgr_init(void) {
    is_open = 0;
    gui_open = 0;
    gui_minimized = 0;
    gui_maximized = 0;
    gui_drag_active = 0;
    selected_tab = 0;
    selected_row = 0;
    scroll_offset = 0;
    show_properties = 0;
    gui_last_tick = 0;
}

void taskmgr_open(void) {
    if (gui_open) {
        taskmgr_close();
    }

    if (!recovery_is_enabled(RECOVERY_COMPONENT_TASKMANAGER)) {
        LOG_WARN("TSKMGR", "Task Manager indisponivel; abertura ignorada");
        is_open = 0;
        return;
    }

    is_open = 1;
    selected_tab = 0;
    selected_row = 0;
    scroll_offset = 0;
    show_properties = 0;
    taskbar_add_app(TB_APP_TASKMGR, "TaskMgr");
    video_clear();
    taskmgr_refresh();
}

void taskmgr_close(void) {
    int was_open = is_open || gui_open;

    is_open = 0;
    gui_open = 0;
    gui_minimized = 0;
    gui_maximized = 0;
    gui_drag_active = 0;
    show_properties = 0;
    taskbar_remove_app(TB_APP_TASKMGR);
    if (!was_open) return;

    desktop_set_active(1);
    desktop_draw();
    taskbar_draw();
}

static void draw_hline(int x, int y, int w, uint8_t color) {
    for (int i = 0; i < w; i++) {
        video_put_char_at(0xC4, color, x + i, y);
    }
}

static void draw_vline(int x, int y, int h, uint8_t color) {
    for (int i = 0; i < h; i++) {
        video_put_char_at(0xB3, color, x, y + i);
    }
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

static const char* taskmgr_process_type(const process_t* process) {
    if (!process) return "N/D";
    return process->pid <= 2 ? "Sistema" : "App";
}

static const char* taskmgr_process_state_name(process_state_t state) {
    switch (state) {
        case PROCESS_STATE_READY: return "Pronto";
        case PROCESS_STATE_RUNNING: return "Rodando";
        case PROCESS_STATE_BLOCKED: return "Bloqueado";
        case PROCESS_STATE_ZOMBIE: return "Zombie";
        default: return "Desconhecido";
    }
}

static const char* taskmgr_thread_state_name(thread_state_t state) {
    switch (state) {
        case THREAD_RUNNING: return "Rodando";
        case THREAD_BLOCKED: return "Bloqueado";
        case THREAD_FINISHED: return "Finalizado";
        default: return "Unused";
    }
}

static void taskmgr_count_process_states(uint32_t* ready, uint32_t* running,
                                         uint32_t* blocked, uint32_t* zombie) {
    *ready = 0;
    *running = 0;
    *blocked = 0;
    *zombie = 0;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        switch (processes[i].state) {
            case PROCESS_STATE_READY: (*ready)++; break;
            case PROCESS_STATE_RUNNING: (*running)++; break;
            case PROCESS_STATE_BLOCKED: (*blocked)++; break;
            case PROCESS_STATE_ZOMBIE: (*zombie)++; break;
            default: break;
        }
    }
}

static void taskmgr_print_hex_at(int x, int y, uint32_t value, uint8_t color) {
    static const char digits[] = "0123456789ABCDEF";
    char buffer[11];

    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buffer[2 + i] = digits[(value >> (28 - (i * 4))) & 0x0F];
    }
    buffer[10] = '\0';
    print_at(x, y, buffer, color);
}

static uint32_t taskmgr_pages_used(uint32_t total, uint32_t free_pages) {
    if (free_pages > total) return 0;
    return total - free_pages;
}

static uint32_t taskmgr_percent(uint32_t value, uint32_t total) {
    if (!total || value > total) return 0;
    return (value * 100) / total;
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
    print_at(TSKMGR_START_X + 2, TSKMGR_START_Y + 1, "ZephyrOS Task Manager", COLOR_TITLE);
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

static void taskmgr_update_cpu_metrics(void) {
    uint32_t current_ticks = timer_get_ticks();

    if (current_ticks > last_cpu_ticks) {
        uint32_t delta_total = current_ticks - last_cpu_ticks;
        for (int i = 0; i < 64; i++) {
            uint32_t delta_proc = processes[i].total_ticks - last_proc_ticks[i];
            cpu_usage[i] = delta_total ? (delta_proc * 100) / delta_total : 0;
            if (cpu_usage[i] > 100) cpu_usage[i] = 100;
        }
    }

    last_cpu_ticks = current_ticks;
    for (int i = 0; i < 64; i++) {
        last_proc_ticks[i] = processes[i].total_ticks;
    }
}

static void draw_processes(void) {
    int start_x = TSKMGR_START_X + 2;
    int start_y = TSKMGR_START_Y + 5;
    int table_w = TSKMGR_WIDTH - 4;
    uint32_t ready;
    uint32_t running;
    uint32_t blocked;
    uint32_t zombie;

    draw_hline(start_x, start_y - 1, table_w, COLOR_BORDER);

    print_at(start_x, start_y, "PID", sort_column == 0 ? COLOR_HIGHLIGHT : COLOR_HEADER);
    print_at(start_x + 5, start_y, "Nome", sort_column == 1 ? COLOR_HIGHLIGHT : COLOR_HEADER);
    print_at(start_x + 19, start_y, "Estado", sort_column == 2 ? COLOR_HIGHLIGHT : COLOR_HEADER);
    print_at(start_x + 30, start_y, "CPU%", sort_column == 3 ? COLOR_HIGHLIGHT : COLOR_HEADER);
    print_at(start_x + 37, start_y, "Tipo", COLOR_HEADER);
    print_at(start_x + 46, start_y, "Tempo", COLOR_HEADER);
    print_at(start_x + 56, start_y, "Espera", COLOR_HEADER);

    draw_hline(start_x, start_y + 1, table_w, COLOR_BORDER);

    uint32_t total_active = 0;

    // Collect active processes
    int active_pids[64];
    for (int i = 0; i < 64; i++) {
        if (processes[i].state != PROCESS_STATE_UNUSED) {
            
            active_pids[total_active++] = i;
        }
    }

    // Sort active_pids based on sort_column (Insertion Sort)
    for (int i = 1; i < total_active; i++) {
        int key = active_pids[i];
        int j = i - 1;
        
        while (j >= 0) {
            int p1 = active_pids[j];
            int p2 = key;
            int swap = 0;
            
            if (sort_column == 0) {
                swap = (processes[p1].pid > processes[p2].pid);
            } else if (sort_column == 1) {
                int k = 0;
                while (processes[p1].name[k] && processes[p2].name[k] && processes[p1].name[k] == processes[p2].name[k]) k++;
                swap = (processes[p1].name[k] > processes[p2].name[k]);
            } else if (sort_column == 2) {
                swap = (processes[p1].state > processes[p2].state);
            } else if (sort_column == 3) {
                swap = (cpu_usage[p1] < cpu_usage[p2]); // Descending for CPU
            }
            
            if (swap) {
                active_pids[j + 1] = active_pids[j];
                j--;
            } else {
                break;
            }
        }
        active_pids[j + 1] = key;
    }

    if (selected_row >= (int)total_active) {
        selected_row = total_active ? (int)total_active - 1 : 0;
    }

    int row = 0;
    int visible_row = 0;
    for (uint32_t k = 0; k < total_active; k++) {
        int i = active_pids[k];

        if (row >= scroll_offset && visible_row < TSKMGR_CLASSIC_PROCESS_ROWS) {
            int y = start_y + 2 + visible_row;
            uint8_t row_color = COLOR_TEXT;

            if (selected_row == row) {
                for (int x = 0; x < table_w; x++) {
                    video_put_char_at(0x20, COLOR_SELECTION, start_x + x, y);
                }
                row_color = COLOR_SELECTION;
                prop_pid = processes[i].pid;
            }

            print_num_at(start_x, y, processes[i].pid, row_color);

            char name[13];
            int n = 0;
            while (processes[i].name[n] && n < 12) {
                name[n] = processes[i].name[n];
                n++;
            }
            name[n] = 0;
            print_at(start_x + 5, y, name, row_color);

            const char* state_str = "?";
            uint8_t state_color = COLOR_TEXT;
            switch (processes[i].state) {
                case PROCESS_STATE_READY:    state_str = "Pronto";    state_color = COLOR_BAR_FG; break;
                case PROCESS_STATE_RUNNING:  state_str = "Rodando";   state_color = COLOR_HIGHLIGHT; break;
                case PROCESS_STATE_BLOCKED:  state_str = "Bloqueado"; state_color = COLOR_BAR_WARN; break;
                case PROCESS_STATE_ZOMBIE:   state_str = "Zombie";    state_color = COLOR_DEAD; break;
                default: state_str = "?"; break;
            }
            print_at(start_x + 19, y, state_str,
                     selected_row == row ? COLOR_SELECTION : state_color);

            uint8_t cpu_color = COLOR_TEXT;
            if (cpu_usage[i] > 80) cpu_color = COLOR_DEAD;
            else if (cpu_usage[i] > 50) cpu_color = COLOR_BAR_WARN;

            if (selected_row == row) cpu_color = COLOR_SELECTION;
            print_num_at(start_x + 30, y, cpu_usage[i], cpu_color);
            print_at(start_x + 33, y, "%", cpu_color);
            print_at(start_x + 37, y, taskmgr_process_type(&processes[i]), row_color);

            uint32_t ticks = processes[i].total_ticks;
            uint32_t secs = ticks / TSKMGR_TICKS_PER_SECOND;
            print_num_at(start_x + 46, y, secs, row_color);
            print_at(start_x + 46 + num_digits(secs), y, "s", row_color);
            print_num_at(start_x + 56, y, processes[i].wait_ticks, row_color);
            visible_row++;
        }
        row++;
    }

    taskmgr_count_process_states(&ready, &running, &blocked, &zombie);
    int info_y = TSKMGR_START_Y + TSKMGR_HEIGHT - 3;
    draw_hline(start_x, info_y, table_w, COLOR_BORDER);
    print_at(start_x, info_y + 1, "P:", COLOR_TEXT);
    print_num_at(start_x + 2, info_y + 1, total_active, COLOR_HIGHLIGHT);
    print_at(start_x + 5, info_y + 1, "R:", COLOR_TEXT);
    print_num_at(start_x + 7, info_y + 1, ready, COLOR_BAR_FG);
    print_at(start_x + 10, info_y + 1, "X:", COLOR_TEXT);
    print_num_at(start_x + 12, info_y + 1, running, COLOR_HIGHLIGHT);
    print_at(start_x + 15, info_y + 1, "B:", COLOR_TEXT);
    print_num_at(start_x + 17, info_y + 1, blocked, COLOR_BAR_WARN);
    print_at(start_x + 20, info_y + 1, "Z:", COLOR_TEXT);
    print_num_at(start_x + 22, info_y + 1, zombie, COLOR_DEAD);
    print_at(start_x + 25, info_y + 1, "T:", COLOR_TEXT);
    print_num_at(start_x + 27, info_y + 1, thread_get_count(), COLOR_HIGHLIGHT);

    if (show_properties && prop_pid > 0) {
        process_t* p = 0;
        for (uint32_t i = 0; i < 64; i++) {
            if (processes[i].pid == (uint32_t)prop_pid) p = &processes[i];
        }
        if (p) {
            int px = TSKMGR_START_X + 10;
            int py = TSKMGR_START_Y + 3;
            draw_box(px, py, 58, 17, COLOR_HEADER);
            for (int i = 1; i < 57; i++) {
                for (int j = 1; j < 16; j++) {
                    video_put_char_at(0x20, COLOR_SELECTION, px + i, py + j);
                }
            }
            print_at(px + 2, py + 1, "Propriedades do Processo", COLOR_SELECTION);
            draw_hline(px + 1, py + 2, 56, COLOR_HEADER);

            print_at(px + 2, py + 3, "PID:", COLOR_SELECTION);
            print_num_at(px + 10, py + 3, p->pid, 0x1E);
            print_at(px + 29, py + 3, "Estado:", COLOR_SELECTION);
            print_at(px + 38, py + 3, taskmgr_process_state_name(p->state), 0x1E);
            print_at(px + 2, py + 4, "Nome:", COLOR_SELECTION);
            print_at(px + 10, py + 4, p->name, 0x1E);
            print_at(px + 29, py + 4, "Tipo:", COLOR_SELECTION);
            print_at(px + 38, py + 4, taskmgr_process_type(p), 0x1E);
            print_at(px + 2, py + 5, "CPU:", COLOR_SELECTION);
            print_num_at(px + 10, py + 5, cpu_usage[p - processes], 0x1E);
            print_at(px + 13, py + 5, "%", 0x1E);
            print_at(px + 29, py + 5, "Espera:", COLOR_SELECTION);
            print_num_at(px + 38, py + 5, p->wait_ticks, 0x1E);
            print_at(px + 2, py + 6, "Ticks:", COLOR_SELECTION);
            print_num_at(px + 10, py + 6, p->total_ticks, 0x1E);
            print_at(px + 29, py + 6, "Tempo:", COLOR_SELECTION);
            print_num_at(px + 38, py + 6, p->total_ticks / TSKMGR_TICKS_PER_SECOND, 0x1E);
            print_at(px + 2, py + 7, "Proximo PID:", COLOR_SELECTION);
            print_num_at(px + 15, py + 7, p->next_pid, 0x1E);
            print_at(px + 29, py + 7, "Page dir:", COLOR_SELECTION);
            print_at(px + 39, py + 7, p->page_directory ? "OK" : "N/D", 0x1E);
            print_at(px + 2, py + 8, "EIP:", COLOR_SELECTION);
            taskmgr_print_hex_at(px + 10, py + 8, p->context.eip, 0x1E);
            print_at(px + 29, py + 8, "ESP:", COLOR_SELECTION);
            taskmgr_print_hex_at(px + 38, py + 8, p->context.esp, 0x1E);
            print_at(px + 2, py + 9, "CR3:", COLOR_SELECTION);
            taskmgr_print_hex_at(px + 10, py + 9, p->context.cr3, 0x1E);
            print_at(px + 29, py + 9, "KStack:", COLOR_SELECTION);
            taskmgr_print_hex_at(px + 38, py + 9, p->kernel_stack_top, 0x1E);
            print_at(px + 2, py + 12, "[ ESC para fechar ]", 0x1A);
        }
    }
}

static void draw_memory(void) {
    int start_x = TSKMGR_START_X + 2;
    int start_y = TSKMGR_START_Y + 5;

    uint32_t total = memory_get_total();
    uint32_t used = memory_get_used();
    uint32_t free = memory_get_free();
    uint32_t total_pages = memory_get_total_pages();
    uint32_t free_pages = memory_get_free_pages();
    uint32_t used_pages = taskmgr_pages_used(total_pages, free_pages);
    uint32_t used_pct = taskmgr_percent(used, total);
    ata_device_t* device = ata_get_device();

    print_at(start_x, start_y, "Uso de Memoria", COLOR_HEADER);
    draw_hline(start_x, start_y + 1, TSKMGR_WIDTH - 4, COLOR_BORDER);

    print_at(start_x, start_y + 3, "Total:", COLOR_TEXT);
    print_num_at(start_x + 12, start_y + 3, total / 1024, COLOR_HIGHLIGHT);
    print_at(start_x + 12 + num_digits(total / 1024), start_y + 3, " KB", COLOR_TEXT);

    print_at(start_x, start_y + 4, "Usado:", COLOR_TEXT);
    print_num_at(start_x + 12, start_y + 4, used / 1024, COLOR_HIGHLIGHT);
    print_at(start_x + 12 + num_digits(used / 1024), start_y + 4, " KB", COLOR_TEXT);

    print_at(start_x, start_y + 5, "Livre:", COLOR_TEXT);
    print_num_at(start_x + 12, start_y + 5, free / 1024, COLOR_BAR_FG);
    print_at(start_x + 12 + num_digits(free / 1024), start_y + 5, " KB", COLOR_TEXT);

    uint8_t bar_color = COLOR_BAR_FG;
    if (used_pct > 80) bar_color = COLOR_BAR_CRIT;
    else if (used_pct > 60) bar_color = COLOR_BAR_WARN;

    draw_bar(start_x, start_y + 7, 42, used, total, bar_color, COLOR_BAR_BG);
    print_at(start_x + 46, start_y + 7, "Uso:", COLOR_TEXT);
    print_num_at(start_x + 51, start_y + 7, used_pct, bar_color);
    print_at(start_x + 54, start_y + 7, "%", bar_color);

    draw_hline(start_x, start_y + 9, TSKMGR_WIDTH - 4, COLOR_BORDER);
    print_at(start_x, start_y + 10, "Paginas:", COLOR_HEADER);
    print_at(start_x, start_y + 11, "Total", COLOR_TEXT);
    print_num_at(start_x + 6, start_y + 11, total_pages, COLOR_HIGHLIGHT);
    print_at(start_x + 23, start_y + 11, "Usadas", COLOR_TEXT);
    print_num_at(start_x + 30, start_y + 11, used_pages, COLOR_HIGHLIGHT);
    print_at(start_x + 48, start_y + 11, "Livres", COLOR_TEXT);
    print_num_at(start_x + 55, start_y + 11, free_pages, COLOR_BAR_FG);

    print_at(start_x, start_y + 13, "ATA:", COLOR_HEADER);
    print_at(start_x + 6, start_y + 13, "R", COLOR_TEXT);
    print_num_at(start_x + 8, start_y + 13, ata_get_read_ops(), COLOR_HIGHLIGHT);
    print_at(start_x + 20, start_y + 13, "W", COLOR_TEXT);
    print_num_at(start_x + 22, start_y + 13, ata_get_write_ops(), COLOR_HIGHLIGHT);
    print_at(start_x + 38, start_y + 13, "Estado", COLOR_TEXT);
    print_at(start_x + 45, start_y + 13,
             device && device->present ? "READY" : "N/D", COLOR_HIGHLIGHT);

    print_at(start_x, start_y + 14, "Modelo:", COLOR_TEXT);
    if (device && device->present) {
        char model[31];
        int i = 0;
        while (device->model[i] && i < 30) {
            model[i] = device->model[i];
            i++;
        }
        model[i] = '\0';
        print_at(start_x + 8, start_y + 14, model, COLOR_TEXT);
    } else {
        print_at(start_x + 8, start_y + 14, "N/D", COLOR_DEAD);
    }

    print_at(start_x, start_y + 15, "Setor ATA:", COLOR_TEXT);
    if (device && device->present) {
        print_num_at(start_x + 11, start_y + 15, device->sectors, COLOR_HIGHLIGHT);
    } else {
        print_at(start_x + 11, start_y + 15, "N/D", COLOR_DEAD);
    }
    print_at(start_x + 35, start_y + 15, "Pagina: 4 KB", COLOR_TEXT);

    if (!total) {
        print_at(start_x, start_y + 16, "Metricas de memoria indisponiveis", COLOR_DEAD);
    }
}

static void draw_threads(void) {
    int start_x = TSKMGR_START_X + 2;
    int start_y = TSKMGR_START_Y + 5;
    int table_w = TSKMGR_WIDTH - 4;
    uint32_t total_threads = 0;
    uint32_t running_threads = 0;
    uint32_t blocked_threads = 0;

    draw_hline(start_x, start_y - 1, table_w, COLOR_BORDER);

    print_at(start_x, start_y, "TID", COLOR_HEADER);
    print_at(start_x + 6, start_y, "Nome", COLOR_HEADER);
    print_at(start_x + 21, start_y, "Estado", COLOR_HEADER);
    print_at(start_x + 32, start_y, "Espera", COLOR_HEADER);
    print_at(start_x + 42, start_y, "EIP", COLOR_HEADER);
    print_at(start_x + 54, start_y, "ESP", COLOR_HEADER);

    draw_hline(start_x, start_y + 1, table_w, COLOR_BORDER);

    for (uint32_t i = 0; i < MAX_THREADS; i++) {
        thread_t* t = thread_get_by_id(i + 1);
        if (!t) continue;
        total_threads++;
        if (t->state == THREAD_RUNNING) running_threads++;
        if (t->state == THREAD_BLOCKED) blocked_threads++;
    }

    if (selected_row >= (int)total_threads) {
        selected_row = total_threads ? (int)total_threads - 1 : 0;
    }
    if (selected_row < scroll_offset) scroll_offset = selected_row;
    if (selected_row >= scroll_offset + TSKMGR_CLASSIC_THREAD_ROWS) {
        scroll_offset = selected_row - TSKMGR_CLASSIC_THREAD_ROWS + 1;
    }

    int row = 0;
    for (int visible = 0; visible < TSKMGR_CLASSIC_THREAD_ROWS &&
         scroll_offset + visible < (int)total_threads; visible++) {
        thread_t* t = taskmgr_find_thread_by_row(scroll_offset + visible);
        if (t) {
            int y = start_y + 2 + row;
            int absolute_row = scroll_offset + visible;
            uint8_t row_color = selected_row == absolute_row ? COLOR_SELECTION : COLOR_TEXT;

            if (selected_row == absolute_row) {
                for (int x = 0; x < table_w; x++) {
                    video_put_char_at(0x20, COLOR_SELECTION, start_x + x, y);
                }
            }

            print_num_at(start_x, y, t->id, row_color);

            char name[16];
            int n = 0;
            while (t->name[n] && n < 15) {
                name[n] = t->name[n];
                n++;
            }
            name[n] = '\0';
            print_at(start_x + 6, y, name, row_color);

            const char* state_str = taskmgr_thread_state_name(t->state);
            uint8_t state_color = COLOR_TEXT;
            switch (t->state) {
                case THREAD_RUNNING:  state_str = "Rodando";   state_color = COLOR_HIGHLIGHT; break;
                case THREAD_BLOCKED:  state_str = "Bloqueado"; state_color = COLOR_BAR_WARN; break;
                case THREAD_FINISHED: state_str = "Finalizado"; state_color = COLOR_DEAD; break;
                default: state_str = "Unused"; break;
            }
            print_at(start_x + 21, y, state_str,
                     selected_row == absolute_row ? COLOR_SELECTION : state_color);
            print_num_at(start_x + 32, y, t->wait_ticks, row_color);
            taskmgr_print_hex_at(start_x + 42, y, t->eip, row_color);
            taskmgr_print_hex_at(start_x + 54, y, t->esp, row_color);

            row++;
        }
    }

    if (row == 0) {
        print_at(start_x + 10, start_y + 4, "Nenhuma thread ativa", COLOR_BORDER);
    }

    int info_y = TSKMGR_START_Y + TSKMGR_HEIGHT - 3;
    draw_hline(start_x, info_y, table_w, COLOR_BORDER);
    print_at(start_x, info_y + 1, "T:", COLOR_TEXT);
    print_num_at(start_x + 2, info_y + 1, total_threads, COLOR_HIGHLIGHT);
    print_at(start_x + 6, info_y + 1, "R:", COLOR_TEXT);
    print_num_at(start_x + 8, info_y + 1, running_threads, COLOR_HIGHLIGHT);
    print_at(start_x + 12, info_y + 1, "B:", COLOR_TEXT);
    print_num_at(start_x + 14, info_y + 1, blocked_threads, COLOR_BAR_WARN);
    print_at(start_x + 18, info_y + 1, "Stack/EIP/ESP exibidos", COLOR_BORDER);
}

void taskmgr_refresh(void) {
    if (!is_open) return;

    taskmgr_update_cpu_metrics();

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

static void taskmgr_redraw_after_menu_close(void) {
    video_clear();
    taskmgr_refresh();
    taskbar_draw();
}

static void taskmgr_handle_taskbar_action(int result) {
    switch (result) {
        case 2:
            taskmgr_close();
            desktop_set_active(0);
            video_clear();
            shell_print_prompt();
            taskbar_draw();
            break;
        case 3:
            taskmgr_close();
            fm_run();
            break;
        case 4:
            taskmgr_redraw_after_menu_close();
            break;
        case 5:
            taskmgr_close();
            asm volatile("cli");
            asm volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
            for (;;) asm volatile("hlt");
            break;
        case 6:
            taskmgr_close();
            asm volatile("cli");
            for (;;) asm volatile("hlt");
            break;
        case 7:
            taskmgr_close();
            video_clear();
            desktop_set_active(1);
            desktop_draw();
            taskbar_draw();
            break;
        case 8:
            taskmgr_close();
            settings_open();
            break;
        case 9:
            taskmgr_redraw_after_menu_close();
            break;
    }
}

void taskmgr_handle_key(uint8_t scancode) {
    if (!is_open) return;

    int config_result = taskbar_handle_config_key(scancode);
    if (config_result) {
        if (config_result == 9) taskmgr_redraw_after_menu_close();
        return;
    }

    int taskbar_result = taskbar_handle_key(scancode);
    if (taskbar_result) {
        taskmgr_handle_taskbar_action(taskbar_result);
        return;
    }

    if (show_properties) {
        if (scancode == 0x01) {
            show_properties = 0;
            taskmgr_refresh();
        }
        return;
    }

    if (scancode == 0x01) {
        taskmgr_close();
        return;
    }

    // S = Sort (0x1F)
    if (scancode == 0x1F && selected_tab == 0) {
        sort_column = (sort_column + 1) % 4;
        taskmgr_refresh();
        return;
    }

    // R = Restart (0x13)
    if (scancode == 0x13 && selected_tab == 0 && prop_pid > 2) {
        // Find process and if it's explorer/taskmgr, restart it
        for (uint32_t i = 0; i < 64; i++) {
            if (processes[i].pid == (uint32_t)prop_pid) {
                int is_explorer = 0;
                int is_taskmgr = 0;
                char* n = processes[i].name;
                if (n[0]=='E' && n[1]=='x' && n[2]=='p') is_explorer = 1;
                if (n[0]=='T' && n[1]=='a' && n[2]=='s' && n[3]=='k') is_taskmgr = 1;
                
                if (is_explorer || is_taskmgr) {
                    process_destroy(&processes[i]);
                    // Process creation logic should be implemented properly
                    // e.g. shell_run(cmd) or similar.
                    LOG_INFO("TSKMGR", "Processo critico destruido (restart)");
                }
                break;
            }
        }
        taskmgr_refresh();
        return;
    }

    // Enter = Properties (0x1C)
    if (scancode == 0x1C && selected_tab == 0) {
        show_properties = 1;
        taskmgr_refresh();
        return;
    }

    // F = Focus window (0x21)
    if (scancode == 0x21 && selected_tab == 0 && prop_pid > 2) {
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
        int count = 1;
        if (selected_tab == 0) {
            count = 0;
            for (int i = 0; i < MAX_PROCESSES; i++) {
                if (processes[i].state != PROCESS_STATE_UNUSED) count++;
            }
        } else if (selected_tab == 2) {
            count = (int)thread_get_count();
        }
        if (selected_row + 1 < count) {
            selected_row++;
        }
        if (selected_tab == 0 &&
            selected_row >= scroll_offset + TSKMGR_CLASSIC_PROCESS_ROWS) {
            scroll_offset++;
        }
        if (selected_tab == 2 &&
            selected_row >= scroll_offset + TSKMGR_CLASSIC_THREAD_ROWS) {
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

static void taskmgr_gui_draw_num(int x, int y, uint32_t value, uint32_t color) {
    char buffer[16];
    int pos = 0;
    char reverse[16];

    if (value == 0) {
        buffer[pos++] = '0';
    } else {
        int reverse_pos = 0;
        while (value && reverse_pos < 15) {
            reverse[reverse_pos++] = (char)('0' + (value % 10));
            value /= 10;
        }
        while (reverse_pos > 0) buffer[pos++] = reverse[--reverse_pos];
    }
    buffer[pos] = '\0';
    gui_draw_text((uint32_t)x, (uint32_t)y, buffer, color);
}

static void taskmgr_gui_draw_hex(int x, int y, uint32_t value, uint32_t color) {
    static const char digits[] = "0123456789ABCDEF";
    char buffer[11];

    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buffer[2 + i] = digits[(value >> (28 - (i * 4))) & 0x0F];
    }
    buffer[10] = '\0';
    gui_draw_text((uint32_t)x, (uint32_t)y, buffer, color);
}

static void taskmgr_gui_copy_text(char* destination, int size, const char* source) {
    int i = 0;

    if (!destination || size < 1) return;
    if (source) {
        while (source[i] && i < size - 1) {
            destination[i] = source[i];
            i++;
        }
    }
    destination[i] = '\0';
}

static int taskmgr_get_work_top(void) {
    tb_config_t* config = taskbar_get_config();
    if (config && config->position == TB_POS_TOP) return TSKMGR_GUI_TASKBAR_HEIGHT;
    return 0;
}

static int taskmgr_get_work_bottom(void) {
    vesa_mode_t* mode = vesa_get_mode();
    if (!mode) return 0;
    if (taskbar_get_config()->position == TB_POS_TOP) return (int)mode->height;
    if (mode->height <= TSKMGR_GUI_TASKBAR_HEIGHT) return 0;
    return (int)mode->height - TSKMGR_GUI_TASKBAR_HEIGHT;
}

static void taskmgr_clamp_window(void) {
    vesa_mode_t* mode = vesa_get_mode();
    int top = taskmgr_get_work_top();
    int bottom = taskmgr_get_work_bottom();

    if (!mode) return;
    if (gui_width > (int)mode->width) gui_width = (int)mode->width;
    if (gui_height > bottom - top) gui_height = bottom - top;
    if (gui_width < 1 || gui_height < 1) return;

    if (gui_x < 0) gui_x = 0;
    if (gui_y < top) gui_y = top;
    if (gui_x + gui_width > (int)mode->width) {
        gui_x = (int)mode->width - gui_width;
    }
    if (gui_y + gui_height > bottom) gui_y = bottom - gui_height;
}

static int taskmgr_gui_visible_rows(void) {
    int rows = (gui_height - 250) / TSKMGR_GUI_ROW_HEIGHT;
    if (rows < 1) rows = 1;
    if (rows > TSKMGR_GUI_MAX_VISIBLE_ROWS) rows = TSKMGR_GUI_MAX_VISIBLE_ROWS;
    return rows;
}

static int taskmgr_gui_has_side_details(void) {
    int content_width = gui_width - (TSKMGR_GUI_MARGIN * 2);
    return content_width >= TSKMGR_GUI_LIST_MIN_WIDTH +
           TSKMGR_GUI_DETAIL_MIN_WIDTH + 12;
}

static int taskmgr_gui_process_list_y(void) {
    return gui_y + 104;
}

static int taskmgr_gui_process_list_height(void) {
    int height = gui_height - 142 - 36;
    if (!taskmgr_gui_has_side_details()) height -= 90;
    return height;
}

static int taskmgr_gui_process_visible_rows(void) {
    int rows = (taskmgr_gui_process_list_height() - 36) / TSKMGR_GUI_ROW_HEIGHT;
    if (rows < 1) rows = 1;
    if (rows > TSKMGR_GUI_MAX_VISIBLE_ROWS) rows = TSKMGR_GUI_MAX_VISIBLE_ROWS;
    return rows;
}

static int taskmgr_collect_processes(int* process_indexes) {
    int count = 0;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state != PROCESS_STATE_UNUSED) {
            process_indexes[count++] = i;
        }
    }

    for (int i = 1; i < count; i++) {
        int key = process_indexes[i];
        int j = i - 1;
        while (j >= 0) {
            int left = process_indexes[j];
            int swap = 0;
            if (sort_column == 0) swap = processes[left].pid > processes[key].pid;
            if (sort_column == 1) {
                int k = 0;
                while (processes[left].name[k] && processes[key].name[k] &&
                       processes[left].name[k] == processes[key].name[k]) k++;
                swap = processes[left].name[k] > processes[key].name[k];
            }
            if (sort_column == 2) swap = processes[left].state > processes[key].state;
            if (sort_column == 3) swap = cpu_usage[left] < cpu_usage[key];
            if (!swap) break;
            process_indexes[j + 1] = process_indexes[j];
            j--;
        }
        process_indexes[j + 1] = key;
    }
    return count;
}

static process_t* taskmgr_find_process_by_row(int row) {
    int indexes[MAX_PROCESSES];
    int count = taskmgr_collect_processes(indexes);
    if (row < 0 || row >= count) return 0;
    return &processes[indexes[row]];
}

static process_t* taskmgr_find_process_by_pid(int pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state != PROCESS_STATE_UNUSED &&
            processes[i].pid == (uint32_t)pid) return &processes[i];
    }
    return 0;
}

static thread_t* taskmgr_find_thread_by_row(int row) {
    int current_row = 0;
    for (int i = 0; i < MAX_THREADS; i++) {
        thread_t* thread = thread_get_by_id((uint32_t)i + 1);
        if (thread) {
            if (current_row == row) return thread;
            current_row++;
        }
    }
    return 0;
}

static void taskmgr_gui_draw_bar(int x, int y, int width, uint32_t percent,
                                 uint32_t color) {
    vesa_color_t bg;
    vesa_color_t fill;
    int fill_width;

    if (percent > 100) percent = 100;
    if (width < 8) return;
    bg.raw = GUI_COLOR_BORDER_D;
    fill.raw = color;
    vesa_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)width, 14, bg);
    fill_width = (width - 2) * (int)percent / 100;
    if (fill_width > 0) {
        vesa_fill_rect((uint32_t)(x + 1), (uint32_t)(y + 1),
                       (uint32_t)fill_width, 12, fill);
    }
}

static uint32_t taskmgr_gui_state_color(process_state_t state) {
    switch (state) {
        case PROCESS_STATE_RUNNING: return 0x00008000;
        case PROCESS_STATE_BLOCKED: return 0x00808000;
        case PROCESS_STATE_ZOMBIE: return 0x00800000;
        default: return GUI_COLOR_TEXT;
    }
}

static void taskmgr_gui_draw_tabs(void) {
    const char* names[] = {"Processos", "Memoria", "Threads"};
    int x = gui_x + TSKMGR_GUI_MARGIN;
    int y = gui_y + 32;

    for (int i = 0; i < 3; i++) {
        uint32_t background = selected_tab == i ? GUI_COLOR_TITLE_BG : GUI_COLOR_BG;
        uint32_t text_color = selected_tab == i ? GUI_COLOR_TEXT_W : GUI_COLOR_TEXT;
        gui_draw_panel((uint32_t)x, (uint32_t)y, 112, TSKMGR_GUI_TAB_HEIGHT,
                       background, selected_tab == i);
        gui_draw_text((uint32_t)(x + 16), (uint32_t)(y + 4), names[i], text_color);
        x += 116;
    }
}

static void taskmgr_gui_draw_process_details(process_t* process, int x, int y,
                                             int width, int height) {
    char name[13];

    gui_draw_panel((uint32_t)x, (uint32_t)y, (uint32_t)width, (uint32_t)height,
                   GUI_COLOR_BG, 0);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 8),
                  "Detalhes selecionados", GUI_COLOR_TEXT);
    if (!process) {
        gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 38),
                      "Nenhum processo", GUI_COLOR_TEXT);
        return;
    }
    taskmgr_gui_copy_text(name, sizeof(name), process->name);

    if (height < 130) {
        gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 32), "PID:", GUI_COLOR_TEXT);
        taskmgr_gui_draw_num(x + 48, y + 32, process->pid, GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 112), (uint32_t)(y + 32), "CPU:", GUI_COLOR_TEXT);
        taskmgr_gui_draw_num(x + 152, y + 32, cpu_usage[process - processes], GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 176), (uint32_t)(y + 32), "%", GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 56), "Nome:", GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 48), (uint32_t)(y + 56), name, GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 80), "Estado:", GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 66), (uint32_t)(y + 80),
                      taskmgr_process_state_name(process->state), GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 112), (uint32_t)(y + 80), "Espera:", GUI_COLOR_TEXT);
        taskmgr_gui_draw_num(x + 176, y + 80, process->wait_ticks, GUI_COLOR_TEXT);
        return;
    }

    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 34), "PID:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 48, y + 34, process->pid, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 128), (uint32_t)(y + 34), "CPU:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 168, y + 34, cpu_usage[process - processes], GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 192), (uint32_t)(y + 34), "%", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 58), "Nome:", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 48), (uint32_t)(y + 58), name, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 128), (uint32_t)(y + 58), "Tipo:", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 168), (uint32_t)(y + 58),
                  taskmgr_process_type(process), GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 82), "Estado:", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 66), (uint32_t)(y + 82),
                  taskmgr_process_state_name(process->state), GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 128), (uint32_t)(y + 82), "Espera:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 168, y + 82, process->wait_ticks, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 106), "Tempo:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 60, y + 106,
                         process->total_ticks / TSKMGR_TICKS_PER_SECOND,
                         GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 84), (uint32_t)(y + 106), "s", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 128), (uint32_t)(y + 106), "Ticks:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 168, y + 106, process->total_ticks, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 130), "EIP:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_hex(x + 48, y + 130, process->context.eip, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 128), (uint32_t)(y + 130), "ESP:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_hex(x + 168, y + 130, process->context.esp, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 154), "CR3:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_hex(x + 48, y + 154, process->context.cr3, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 128), (uint32_t)(y + 154), "Stk:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_hex(x + 168, y + 154, process->kernel_stack_top, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 178), "Page dir:", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 72), (uint32_t)(y + 178),
                  process->page_directory ? "OK" : "N/D", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 128), (uint32_t)(y + 178), "Prox PID:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 192, y + 178, process->next_pid, GUI_COLOR_TEXT);
}

static void taskmgr_gui_draw_processes(void) {
    int indexes[MAX_PROCESSES];
    int count = taskmgr_collect_processes(indexes);
    int x = gui_x + TSKMGR_GUI_MARGIN;
    int y = gui_y + 68;
    int width = gui_width - (TSKMGR_GUI_MARGIN * 2);
    int panel_height = gui_height - 142;
    int list_y = taskmgr_gui_process_list_y();
    int list_height = taskmgr_gui_process_list_height();
    int list_width = width;
    int detail_x = 0;
    uint32_t ready;
    uint32_t running;
    uint32_t blocked;
    uint32_t zombie;
    uint32_t cpu_sum = 0;

    taskmgr_count_process_states(&ready, &running, &blocked, &zombie);
    for (int i = 0; i < count; i++) cpu_sum += cpu_usage[indexes[i]];
    if (selected_row >= count) selected_row = count ? count - 1 : 0;
    if (selected_row < scroll_offset) scroll_offset = selected_row;
    if (selected_row >= scroll_offset + taskmgr_gui_process_visible_rows()) {
        scroll_offset = selected_row - taskmgr_gui_process_visible_rows() + 1;
    }

    gui_draw_panel((uint32_t)x, (uint32_t)y, (uint32_t)width,
                   (uint32_t)panel_height, GUI_COLOR_BG, 0);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 8), "Processos:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 82, y + 8, (uint32_t)count, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 122), (uint32_t)(y + 8), "Prontos:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 186, y + 8, ready, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 232), (uint32_t)(y + 8), "Rodando:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 302, y + 8, running, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 350), (uint32_t)(y + 8), "CPU soma:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 430, y + 8, cpu_sum, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 454), (uint32_t)(y + 8), "%", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 24), "Bloq:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 50, y + 24, blocked, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 96), (uint32_t)(y + 24), "Zombie:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 158, y + 24, zombie, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 204), (uint32_t)(y + 24), "Threads:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 270, y + 24, thread_get_count(), GUI_COLOR_TEXT);

    if (taskmgr_gui_has_side_details()) {
        list_width = width - TSKMGR_GUI_DETAIL_WIDTH - 8;
        detail_x = x + list_width + 8;
    }
    gui_draw_panel((uint32_t)x, (uint32_t)list_y, (uint32_t)list_width,
                   (uint32_t)list_height, GUI_COLOR_BG, 0);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(list_y + 8), "PID", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 56), (uint32_t)(list_y + 8), "Nome", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 184), (uint32_t)(list_y + 8), "Estado", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 276), (uint32_t)(list_y + 8), "CPU", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 330), (uint32_t)(list_y + 8), "Tipo", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 380), (uint32_t)(list_y + 8), "Tempo", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 440), (uint32_t)(list_y + 8), "Wait", GUI_COLOR_TEXT);
    vesa_draw_hline((uint32_t)(x + 6), (uint32_t)(list_y + 30),
                    (uint32_t)(list_width - 12), taskmgr_gui_color(GUI_COLOR_BORDER_D));

    for (int row = 0; row < taskmgr_gui_process_visible_rows() &&
         scroll_offset + row < count; row++) {
        int absolute_row = scroll_offset + row;
        process_t* process = &processes[indexes[absolute_row]];
        int row_y = list_y + 36 + row * TSKMGR_GUI_ROW_HEIGHT;
        uint32_t text_color = absolute_row == selected_row ? GUI_COLOR_TEXT_W : GUI_COLOR_TEXT;
        char name[18];
        uint32_t state_color = absolute_row == selected_row ? GUI_COLOR_TEXT_W :
                               taskmgr_gui_state_color(process->state);

        if (absolute_row == selected_row) {
            vesa_color_t selection;
            selection.raw = GUI_COLOR_TITLE_BG;
            vesa_fill_rect((uint32_t)(x + 5), (uint32_t)row_y,
                           (uint32_t)(list_width - 10), TSKMGR_GUI_ROW_HEIGHT, selection);
            prop_pid = (int)process->pid;
        }

        taskmgr_gui_copy_text(name, sizeof(name), process->name);
        taskmgr_gui_draw_num(x + 10, row_y + 4, process->pid, text_color);
        gui_draw_text((uint32_t)(x + 56), (uint32_t)(row_y + 4), name, text_color);
        gui_draw_text((uint32_t)(x + 184), (uint32_t)(row_y + 4),
                      taskmgr_process_state_name(process->state), state_color);
        taskmgr_gui_draw_num(x + 276, row_y + 4, cpu_usage[indexes[absolute_row]], text_color);
        gui_draw_text((uint32_t)(x + 300), (uint32_t)(row_y + 4), "%", text_color);
        gui_draw_text((uint32_t)(x + 330), (uint32_t)(row_y + 4),
                      taskmgr_process_type(process), text_color);
        taskmgr_gui_draw_num(x + 380, row_y + 4,
                             process->total_ticks / TSKMGR_TICKS_PER_SECOND, text_color);
        gui_draw_text((uint32_t)(x + 404), (uint32_t)(row_y + 4), "s", text_color);
        taskmgr_gui_draw_num(x + 440, row_y + 4, process->wait_ticks, text_color);
    }

    if (!count) {
        gui_draw_text((uint32_t)(x + 16), (uint32_t)(list_y + 48),
                      "Nenhum processo ativo", GUI_COLOR_TEXT);
    }

    process_t* selected = taskmgr_find_process_by_row(selected_row);
    if (taskmgr_gui_has_side_details()) {
        taskmgr_gui_draw_process_details(selected, detail_x, list_y,
                                         TSKMGR_GUI_DETAIL_WIDTH, list_height);
    } else {
        int detail_y = list_y + list_height + 6;
        int detail_height = y + panel_height - detail_y;
        taskmgr_gui_draw_process_details(selected, x, detail_y, list_width, detail_height);
    }
}

static void taskmgr_gui_draw_memory(void) {
    uint32_t total = memory_get_total();
    uint32_t used = memory_get_used();
    uint32_t free_memory = memory_get_free();
    uint32_t total_pages = memory_get_total_pages();
    uint32_t free_pages = memory_get_free_pages();
    uint32_t used_pages = taskmgr_pages_used(total_pages, free_pages);
    uint32_t used_percent = taskmgr_percent(used, total);
    ata_device_t* device = ata_get_device();
    int x = gui_x + TSKMGR_GUI_MARGIN + 18;
    int y = gui_y + 82;
    uint32_t bar_color = used_percent > 80 ? 0x00800000 :
                         (used_percent > 60 ? 0x00808000 : 0x00008000);

    gui_draw_panel((uint32_t)(gui_x + TSKMGR_GUI_MARGIN), (uint32_t)(gui_y + 68),
                   (uint32_t)(gui_width - TSKMGR_GUI_MARGIN * 2),
                   (uint32_t)(gui_height - 142), GUI_COLOR_BG, 0);
    gui_draw_text((uint32_t)x, (uint32_t)y, "Memoria fisica", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)x, (uint32_t)(y + 34), "Total:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 120, y + 34, total / 1024, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 184), (uint32_t)(y + 34), "KB", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)x, (uint32_t)(y + 62), "Usada:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 120, y + 62, used / 1024, bar_color);
    gui_draw_text((uint32_t)(x + 184), (uint32_t)(y + 62), "KB", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)x, (uint32_t)(y + 90), "Livre:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 120, y + 90, free_memory / 1024, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 184), (uint32_t)(y + 90), "KB", GUI_COLOR_TEXT);
    taskmgr_gui_draw_bar(x + 260, y + 34, gui_width - 320, used_percent, bar_color);
    taskmgr_gui_draw_num(x + 260, y + 58, used_percent, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 284), (uint32_t)(y + 58), "% usado", GUI_COLOR_TEXT);

    gui_draw_text((uint32_t)x, (uint32_t)(y + 120), "Paginas", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)x, (uint32_t)(y + 148), "Total:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 120, y + 148, total_pages, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 260), (uint32_t)(y + 148), "Usadas:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 380, y + 148, used_pages, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)x, (uint32_t)(y + 176), "Livres:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 120, y + 176, free_pages, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 260), (uint32_t)(y + 176), "Pagina:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 380, y + 176, PAGE_SIZE, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 420), (uint32_t)(y + 176), "bytes", GUI_COLOR_TEXT);

    gui_draw_text((uint32_t)x, (uint32_t)(y + 196), "Disco ATA", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)x, (uint32_t)(y + 220), "Leituras:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 120, y + 220, ata_get_read_ops(), GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 260), (uint32_t)(y + 220), "Escritas:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 380, y + 220, ata_get_write_ops(), GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)x, (uint32_t)(y + 248), "Modelo:", GUI_COLOR_TEXT);
    if (device && device->present) {
        char model[17];
        taskmgr_gui_copy_text(model, sizeof(model), device->model);
        gui_draw_text((uint32_t)(x + 120), (uint32_t)(y + 248), model, GUI_COLOR_TEXT);
    } else {
        gui_draw_text((uint32_t)(x + 120), (uint32_t)(y + 248), "N/D", 0x00800000);
    }
    gui_draw_text((uint32_t)(x + 260), (uint32_t)(y + 248), "Setores:", GUI_COLOR_TEXT);
    if (device && device->present) {
        taskmgr_gui_draw_num(x + 380, y + 248, device->sectors, GUI_COLOR_TEXT);
    } else {
        gui_draw_text((uint32_t)(x + 380), (uint32_t)(y + 248), "N/D", 0x00800000);
    }
    if (!total) {
        gui_draw_text((uint32_t)(x + 196), (uint32_t)(y + 34),
                      "N/D", 0x00800000);
    }
}

static void taskmgr_gui_draw_threads(void) {
    int x = gui_x + TSKMGR_GUI_MARGIN;
    int y = gui_y + 68;
    int width = gui_width - TSKMGR_GUI_MARGIN * 2;
    int panel_height = gui_height - 142;
    int visible_rows = taskmgr_gui_visible_rows();
    int total_threads = 0;
    int running_threads = 0;
    int blocked_threads = 0;

    gui_draw_panel((uint32_t)x, (uint32_t)y, (uint32_t)width,
                   (uint32_t)panel_height, GUI_COLOR_BG, 0);
    for (int i = 0; i < MAX_THREADS; i++) {
        thread_t* thread = thread_get_by_id((uint32_t)i + 1);
        if (!thread) continue;
        total_threads++;
        if (thread->state == THREAD_RUNNING) running_threads++;
        if (thread->state == THREAD_BLOCKED) blocked_threads++;
    }
    if (selected_row >= total_threads) selected_row = total_threads ? total_threads - 1 : 0;
    if (selected_row < scroll_offset) scroll_offset = selected_row;
    if (selected_row >= scroll_offset + visible_rows) {
        scroll_offset = selected_row - visible_rows + 1;
    }
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 8), "Threads:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 76, y + 8, (uint32_t)total_threads, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 130), (uint32_t)(y + 8), "Rodando:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 198, y + 8, (uint32_t)running_threads, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 260), (uint32_t)(y + 8), "Bloqueadas:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 350, y + 8, (uint32_t)blocked_threads, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 42), "TID", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 70), (uint32_t)(y + 42), "Nome", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 260), (uint32_t)(y + 42), "Estado", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 350), (uint32_t)(y + 42), "Espera", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 420), (uint32_t)(y + 42), "EIP", GUI_COLOR_TEXT);
    if (width > 620) {
        gui_draw_text((uint32_t)(x + 540), (uint32_t)(y + 42), "ESP", GUI_COLOR_TEXT);
    }
    vesa_draw_hline((uint32_t)(x + 6), (uint32_t)(y + 64),
                    (uint32_t)(width - 12), taskmgr_gui_color(GUI_COLOR_BORDER_D));

    for (int row = 0; row < visible_rows && scroll_offset + row < total_threads; row++) {
        int absolute_row = scroll_offset + row;
        thread_t* thread = taskmgr_find_thread_by_row(absolute_row);
        int row_y = y + 70 + row * TSKMGR_GUI_ROW_HEIGHT;
        uint32_t text_color = absolute_row == selected_row ? GUI_COLOR_TEXT_W : GUI_COLOR_TEXT;
        char name[24];
        if (!thread) continue;
        taskmgr_gui_copy_text(name, sizeof(name), thread->name);
        if (absolute_row == selected_row) {
            vesa_color_t selection;
            selection.raw = GUI_COLOR_TITLE_BG;
            vesa_fill_rect((uint32_t)(x + 5), (uint32_t)row_y,
                           (uint32_t)(width - 10), TSKMGR_GUI_ROW_HEIGHT, selection);
        }
        taskmgr_gui_draw_num(x + 10, row_y + 4, thread->id, text_color);
        gui_draw_text((uint32_t)(x + 70), (uint32_t)(row_y + 4), name, text_color);
        gui_draw_text((uint32_t)(x + 260), (uint32_t)(row_y + 4),
                      taskmgr_thread_state_name(thread->state), text_color);
        taskmgr_gui_draw_num(x + 350, row_y + 4, thread->wait_ticks, text_color);
        taskmgr_gui_draw_hex(x + 420, row_y + 4, thread->eip, text_color);
        if (width > 620) {
            taskmgr_gui_draw_hex(x + 540, row_y + 4, thread->esp, text_color);
        }
    }

    if (total_threads == 0) {
        gui_draw_text((uint32_t)(x + 16), (uint32_t)(y + 82),
                      "Nenhuma thread ativa", GUI_COLOR_TEXT);
    }
    thread_t* selected_thread = taskmgr_find_thread_by_row(selected_row);
    if (selected_thread) {
        char selected_name[20];
        taskmgr_gui_copy_text(selected_name, sizeof(selected_name), selected_thread->name);
        gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + panel_height - 38),
                      "Selecionada:", GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 92), (uint32_t)(y + panel_height - 38),
                      selected_name, GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 260), (uint32_t)(y + panel_height - 38),
                      "EIP", GUI_COLOR_TEXT);
        taskmgr_gui_draw_hex(x + 292, y + panel_height - 38,
                             selected_thread->eip, GUI_COLOR_TEXT);
        if (width > 620) {
            gui_draw_text((uint32_t)(x + 420), (uint32_t)(y + panel_height - 38),
                          "ESP", GUI_COLOR_TEXT);
            taskmgr_gui_draw_hex(x + 452, y + panel_height - 38,
                                 selected_thread->esp, GUI_COLOR_TEXT);
        }
    }
}

static void taskmgr_gui_draw_properties(void) {
    process_t* process = taskmgr_find_process_by_pid(prop_pid);
    int width = gui_width >= 560 ? 500 : gui_width - 40;
    int height = gui_height >= 360 ? 300 : gui_height - 40;
    int x = gui_x + (gui_width - width) / 2;
    int y = gui_y + (gui_height - height) / 2;

    if (!process) {
        show_properties = 0;
        return;
    }

    gui_draw_window_frame((uint32_t)x, (uint32_t)y, width, height,
                          "Propriedades do processo", 1);
    gui_draw_text((uint32_t)(x + 18), (uint32_t)(y + 42), "PID:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 104, y + 42, process->pid, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 270), (uint32_t)(y + 42), "Tipo:", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 350), (uint32_t)(y + 42),
                  taskmgr_process_type(process), GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 18), (uint32_t)(y + 72), "Nome:", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 104), (uint32_t)(y + 72), process->name, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 18), (uint32_t)(y + 102), "Estado:", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 104), (uint32_t)(y + 102),
                  taskmgr_process_state_name(process->state), GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 270), (uint32_t)(y + 102), "Espera:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 350, y + 102, process->wait_ticks, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 18), (uint32_t)(y + 132), "CPU:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 104, y + 132, cpu_usage[process - processes], GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 128), (uint32_t)(y + 132), "%", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 270), (uint32_t)(y + 132), "Tempo:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 350, y + 132,
                         process->total_ticks / TSKMGR_TICKS_PER_SECOND,
                         GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 374), (uint32_t)(y + 132), "s", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 18), (uint32_t)(y + 162), "Ticks:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 104, y + 162, process->total_ticks, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 270), (uint32_t)(y + 162), "Prox PID:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 350, y + 162, process->next_pid, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 18), (uint32_t)(y + 192), "EIP:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_hex(x + 104, y + 192, process->context.eip, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 270), (uint32_t)(y + 192), "ESP:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_hex(x + 350, y + 192, process->context.esp, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 18), (uint32_t)(y + 222), "CR3:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_hex(x + 104, y + 222, process->context.cr3, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 270), (uint32_t)(y + 222), "KStack:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_hex(x + 350, y + 222, process->kernel_stack_top, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 18), (uint32_t)(y + 252), "Page dir:", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 104), (uint32_t)(y + 252),
                  process->page_directory ? "OK" : "N/D", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 18), (uint32_t)(y + 276),
                  "Enter ou Esc fecha esta janela", GUI_COLOR_TEXT);
}

static void taskmgr_gui_draw(void) {
    vesa_mode_t* mode = vesa_get_mode();

    if (!gui_open || !mode || !mode->initialized || !vesa_has_backbuffer()) return;
    taskmgr_clamp_window();
    mouse_invalidate_cursor();
    vesa_frame_begin();

    if (gui_minimized) {
        desktop_draw();
        taskbar_draw();
        vesa_frame_end();
        return;
    }

    {
        vesa_color_t background;
        background.raw = GUI_COLOR_BG;
        vesa_clear(background);
    }
    gui_draw_window_frame((uint32_t)gui_x, (uint32_t)gui_y,
                          (uint32_t)gui_width, (uint32_t)gui_height,
                          "ZephyrOS Task Manager", 1);
    gui_draw_button((uint32_t)(gui_x + gui_width - 40), (uint32_t)(gui_y + 3),
                    TSKMGR_GUI_CONTROL_SIZE, TSKMGR_GUI_CONTROL_SIZE, "_", 0);
    gui_draw_button((uint32_t)(gui_x + gui_width - 58), (uint32_t)(gui_y + 3),
                    TSKMGR_GUI_CONTROL_SIZE, TSKMGR_GUI_CONTROL_SIZE,
                    gui_maximized ? "R" : "M", 0);
    taskmgr_gui_draw_tabs();

    switch (selected_tab) {
        case 0: taskmgr_gui_draw_processes(); break;
        case 1: taskmgr_gui_draw_memory(); break;
        case 2: taskmgr_gui_draw_threads(); break;
        default: selected_tab = 0; taskmgr_gui_draw_processes(); break;
    }

    gui_draw_text((uint32_t)(gui_x + 14), (uint32_t)(gui_y + gui_height - 48),
                  "Tab  Setas  S:ord  Enter:det  Del:mata", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(gui_x + gui_width - 238),
                  (uint32_t)(gui_y + gui_height - 48),
                  "Atualizacao: 1s | ZephyrOS", GUI_COLOR_TEXT);
    if (show_properties) taskmgr_gui_draw_properties();
    taskbar_draw();
    vesa_frame_end();
}

int taskmgr_open_gui(void) {
    vesa_mode_t* mode = vesa_get_mode();

    if (!recovery_is_enabled(RECOVERY_COMPONENT_TASKMANAGER)) {
        LOG_WARN("TSKMGR", "GUI bloqueada pelo recovery");
        return ERR_UNAVAILABLE;
    }
    if (gui_open) {
        taskmgr_gui_restore();
        return OK;
    }
    if (desktop_get_mode() != DESKTOP_MODE_MODERN || !mode ||
        !mode->initialized || !vesa_has_backbuffer()) {
        LOG_WARN("TSKMGR", "GUI indisponivel; usando TUI");
        return ERR_UNAVAILABLE;
    }
    if (mode->width < TSKMGR_GUI_MIN_WIDTH ||
        mode->height < TSKMGR_GUI_MIN_HEIGHT) {
        LOG_WARN("TSKMGR", "Resolucao insuficiente para a janela grafica");
        return ERR_OVERFLOW;
    }
    if (is_open) taskmgr_close();

    gui_open = 1;
    gui_minimized = 0;
    gui_maximized = 0;
    gui_drag_active = 0;
    selected_tab = 0;
    selected_row = 0;
    scroll_offset = 0;
    show_properties = 0;
    gui_width = mode->width < TSKMGR_GUI_DEFAULT_WIDTH ?
                (int)mode->width - 24 : TSKMGR_GUI_DEFAULT_WIDTH;
    gui_height = mode->height < TSKMGR_GUI_DEFAULT_HEIGHT ?
                 (int)mode->height - 48 : TSKMGR_GUI_DEFAULT_HEIGHT;
    gui_x = ((int)mode->width - gui_width) / 2;
    gui_y = taskmgr_get_work_top() + 12;
    gui_restore_x = gui_x;
    gui_restore_y = gui_y;
    gui_restore_width = gui_width;
    gui_restore_height = gui_height;
    taskmgr_clamp_window();
    desktop_set_active(0);
    taskbar_add_app(TB_APP_TASKMGR, "TaskMgr");
    taskmgr_update_cpu_metrics();
    gui_last_tick = timer_get_ticks();
    taskmgr_gui_draw();
    LOG_INFO("TSKMGR", "Task Manager grafico aberto");
    return OK;
}

int taskmgr_is_gui_open(void) {
    return gui_open;
}

int taskmgr_is_gui_minimized(void) {
    return gui_open && gui_minimized;
}

void taskmgr_gui_restore(void) {
    if (!gui_open) return;
    gui_minimized = 0;
    desktop_set_active(0);
    taskmgr_gui_draw();
}

static void taskmgr_gui_minimize(void) {
    if (!gui_open) return;
    gui_minimized = 1;
    gui_drag_active = 0;
    desktop_draw();
    taskbar_draw();
}

void taskmgr_gui_update(void) {
    uint32_t now;

    if (!gui_open || gui_minimized) return;
    now = timer_get_ticks();
    if (now - gui_last_tick < 50) return;
    gui_last_tick = now;
    taskmgr_update_cpu_metrics();
    taskmgr_gui_draw();
}

static void taskmgr_gui_handle_taskbar_action(int result) {
    switch (result) {
        case 2: taskmgr_close(); shell_handle_app_request(IPC_APP_OPEN_SHELL); break;
        case 3: taskmgr_close(); shell_handle_app_request(IPC_APP_OPEN_EXPLORER); break;
        case 4: taskmgr_gui_restore(); break;
        case 5:
            taskmgr_close();
            asm volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
            break;
        case 6:
            taskmgr_close();
            asm volatile("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0xB004));
            break;
        case 7: taskmgr_close(); shell_handle_app_request(IPC_APP_OPEN_DESKTOP); break;
        case 8: taskmgr_close(); shell_handle_app_request(IPC_APP_OPEN_SETTINGS); break;
        case 9: taskmgr_gui_draw(); break;
        default: break;
    }
}

static void taskmgr_gui_restart_selected(void) {
    process_t* process = taskmgr_find_process_by_row(selected_row);
    if (!process || process->pid <= 2) return;
    if ((process->name[0] == 'E' && process->name[1] == 'x' && process->name[2] == 'p') ||
        (process->name[0] == 'T' && process->name[1] == 'a' && process->name[2] == 's')) {
        process_destroy(process);
        LOG_INFO("TSKMGR", "Processo selecionado destruido para reinicio");
    }
}

static void taskmgr_gui_delete_selected(void) {
    process_t* process = taskmgr_find_process_by_row(selected_row);
    if (!process || process->pid == 1) return;
    process_destroy(process);
    if (selected_row > 0) selected_row--;
}

void taskmgr_gui_handle_key(uint8_t scancode) {
    int config_result;
    int taskbar_result;
    int count;
    int process_indexes[MAX_PROCESSES];

    if (!gui_open) return;
    config_result = taskbar_handle_config_key(scancode);
    if (config_result) {
        if (config_result == 9) taskmgr_gui_draw();
        return;
    }
    taskbar_result = taskbar_handle_key(scancode);
    if (taskbar_result) {
        taskmgr_gui_handle_taskbar_action(taskbar_result);
        return;
    }
    if (show_properties) {
        if (scancode == 0x01 || scancode == 0x1C) {
            show_properties = 0;
            taskmgr_gui_draw();
        }
        return;
    }
    if (scancode == 0x01) {
        taskmgr_close();
        return;
    }
    if (scancode == 0x0F) {
        selected_tab = (selected_tab + 1) % 3;
        selected_row = 0;
        scroll_offset = 0;
        taskmgr_gui_draw();
        return;
    }
    if (scancode == 0x1F && selected_tab == 0) {
        sort_column = (sort_column + 1) % 4;
        taskmgr_gui_draw();
        return;
    }
    if (scancode == 0x1C && selected_tab == 0) {
        if (taskmgr_find_process_by_row(selected_row)) {
            show_properties = 1;
            taskmgr_gui_draw();
        }
        return;
    }
    if (scancode == 0x21 && selected_tab == 0 && prop_pid > 2) {
        taskmgr_close();
        return;
    }
    if (scancode == 0x13 && selected_tab == 0) {
        taskmgr_gui_restart_selected();
        taskmgr_gui_draw();
        return;
    }
    if (scancode == 0x53 && selected_tab == 0) {
        taskmgr_gui_delete_selected();
        taskmgr_gui_draw();
        return;
    }
    if (scancode == 0x48 || scancode == 0x50) {
        if (selected_tab == 0) {
            count = taskmgr_collect_processes(process_indexes);
        } else {
            count = selected_tab == 2 ? (int)thread_get_count() : 1;
        }
        if (scancode == 0x48 && selected_row > 0) selected_row--;
        if (scancode == 0x50 && selected_row + 1 < count) selected_row++;
        taskmgr_gui_draw();
    }
}

static int taskmgr_gui_hit(int x, int y, int left, int top, int width, int height) {
    return x >= left && x < left + width && y >= top && y < top + height;
}

int taskmgr_gui_handle_mouse(mouse_event_t* event) {
    int close_x;
    int maximize_x;
    int minimize_x;
    int process_list_width;

    if (!event) {
        LOG_ERROR("TSKMGR", "Evento de mouse nulo");
        return 0;
    }
    if (!gui_open) return 0;
    if (gui_minimized) return 1;
    if (event->event == MOUSE_EVENT_MOVE && gui_drag_active) {
        gui_x = event->x - gui_drag_offset_x;
        gui_y = event->y - gui_drag_offset_y;
        taskmgr_clamp_window();
        taskmgr_gui_draw();
        return 1;
    }
    if (event->event == MOUSE_EVENT_RELEASE) {
        gui_drag_active = 0;
        return 1;
    }
    if (event->event != MOUSE_EVENT_PRESS ||
        !(event->changed & MOUSE_BTN_LEFT)) return 1;
    if (!taskmgr_gui_hit(event->x, event->y, gui_x, gui_y, gui_width, gui_height)) return 1;

    close_x = gui_x + gui_width - 20;
    maximize_x = gui_x + gui_width - 58;
    minimize_x = gui_x + gui_width - 40;
    if (taskmgr_gui_hit(event->x, event->y, close_x, gui_y + 3, 16, 16)) {
        taskmgr_close();
        return 1;
    }
    if (taskmgr_gui_hit(event->x, event->y, maximize_x, gui_y + 3, 16, 16)) {
        gui_minimized = 0;
        if (gui_maximized) {
            gui_maximized = 0;
            gui_x = gui_restore_x;
            gui_y = gui_restore_y;
            gui_width = gui_restore_width;
            gui_height = gui_restore_height;
        } else {
            gui_maximized = 1;
            gui_restore_x = gui_x;
            gui_restore_y = gui_y;
            gui_restore_width = gui_width;
            gui_restore_height = gui_height;
            gui_x = 0;
            gui_y = taskmgr_get_work_top();
            gui_width = (int)vesa_get_mode()->width;
            gui_height = taskmgr_get_work_bottom() - gui_y;
        }
        taskmgr_gui_draw();
        return 1;
    }
    if (taskmgr_gui_hit(event->x, event->y, minimize_x, gui_y + 3, 16, 16)) {
        taskmgr_gui_minimize();
        return 1;
    }
    if (taskmgr_gui_hit(event->x, event->y, gui_x + 2, gui_y + 2,
                        gui_width - 64, TSKMGR_GUI_TITLE_HEIGHT)) {
        gui_drag_active = 1;
        gui_drag_offset_x = event->x - gui_x;
        gui_drag_offset_y = event->y - gui_y;
        return 1;
    }

    if (taskmgr_gui_hit(event->x, event->y, gui_x + TSKMGR_GUI_MARGIN,
                        gui_y + 32, 336, TSKMGR_GUI_TAB_HEIGHT)) {
        selected_tab = (event->x - (gui_x + TSKMGR_GUI_MARGIN)) / 116;
        if (selected_tab > 2) selected_tab = 2;
        selected_row = 0;
        scroll_offset = 0;
        taskmgr_gui_draw();
        return 1;
    }
    process_list_width = gui_width - TSKMGR_GUI_MARGIN * 2;
    if (taskmgr_gui_has_side_details()) {
        process_list_width -= TSKMGR_GUI_DETAIL_WIDTH + 8;
    }
    if (selected_tab == 0 && taskmgr_gui_hit(event->x, event->y,
            gui_x + TSKMGR_GUI_MARGIN, taskmgr_gui_process_list_y() + 36,
            process_list_width, taskmgr_gui_process_visible_rows() * TSKMGR_GUI_ROW_HEIGHT)) {
        int row = (event->y - taskmgr_gui_process_list_y() - 36) /
                  TSKMGR_GUI_ROW_HEIGHT;
        if (row < 0) return 1;
        selected_row = scroll_offset + row;
        if (taskmgr_find_process_by_row(selected_row)) prop_pid = taskmgr_find_process_by_row(selected_row)->pid;
        taskmgr_gui_draw();
        return 1;
    }
    if (selected_tab == 2 && taskmgr_gui_hit(event->x, event->y,
            gui_x + TSKMGR_GUI_MARGIN, gui_y + 68 + 70,
            gui_width - TSKMGR_GUI_MARGIN * 2,
            taskmgr_gui_visible_rows() * TSKMGR_GUI_ROW_HEIGHT)) {
        int row = (event->y - (gui_y + 68 + 70)) / TSKMGR_GUI_ROW_HEIGHT;
        if (row < 0) return 1;
        selected_row = scroll_offset + row;
        if (taskmgr_find_thread_by_row(selected_row)) taskmgr_gui_draw();
        return 1;
    }
    return 1;
}

int taskmgr_is_open(void) {
    return is_open || gui_open;
}

void taskmgr_run(void) {
    taskmgr_open();
    ipc_msg_t msg;
    uint32_t last_tick = timer_get_ticks();
    while (is_open) {
        if (ipc_receive(&msg)) {
            if (msg.type == IPC_MSG_KEYBOARD) {
                taskmgr_handle_key((uint8_t)msg.data1);
            } else if (msg.type == IPC_MSG_APP_REQUEST) {
                taskmgr_close();
                if (msg.data1 == IPC_APP_OPEN_TASKMANAGER_GUI) {
                    if (taskmgr_open_gui() != OK) taskmgr_open();
                } else {
                    shell_handle_app_request(msg.data1);
                }
            }
        } else {
            uint32_t current_tick = timer_get_ticks();
            if (current_tick - last_tick >= 50) { // 1 second if 50Hz
                taskmgr_refresh();
                last_tick = current_tick;
            }
            process_yield();
        }
    }
}
