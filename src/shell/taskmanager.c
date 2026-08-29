#include "apps/taskmanager.h"
#include "core/video.h"
#include "core/keyboard.h"
#include "core/timer.h"
#include "core/memory.h"
#include "core/string.h"
#include "process/process.h"
#include "process/signal.h"
#include "process/thread.h"
#include "core/panic.h"
#include "apps/shell.h"
#include "ui/taskbar.h"
#include "ui/desktop.h"
#include "ui/filemanager.h"
#include "ui/settings.h"
#include "ui/wm.h"
#include "drivers/ata.h"
#include "drivers/vesa.h"
#include "drivers/font.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/errors.h"
#include "core/power.h"
#include "apps/shell_command_utils.h"
#include "ui/gui.h"
#include "ui/display.h"

/* O Task Manager usa estas primitivas apenas na interface Classic grafica. */
#define gui_draw_text gui_draw_scaled_text

/* Mantem a logica legada legivel, mas remove as faces 3D do caminho Classic. */
#undef GUI_COLOR_BG
#undef GUI_COLOR_BORDER_D
#undef GUI_COLOR_TEXT
#undef GUI_COLOR_TEXT_W
#undef GUI_COLOR_TITLE_BG
#define GUI_COLOR_BG GUI_MODERN_COLOR_BG
#define GUI_COLOR_BORDER_D GUI_MODERN_COLOR_BORDER_INACTIVE
#define GUI_COLOR_TEXT GUI_MODERN_COLOR_TEXT
#define GUI_COLOR_TEXT_W GUI_MODERN_COLOR_TEXT
#define GUI_COLOR_TITLE_BG GUI_MODERN_COLOR_HOVER

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
#define TSKMGR_UI_FRAME_TICKS 1U
#define TSKMGR_METRICS_TICKS 5U
#define TSKMGR_SIMPLE_PROCESS_ROWS 13
#define TSKMGR_SIMPLE_THREAD_ROWS 11

#define TSKMGR_GUI_MIN_WIDTH 720
#define TSKMGR_GUI_MIN_HEIGHT 500
#define TSKMGR_GUI_DEFAULT_WIDTH 760
#define TSKMGR_GUI_DEFAULT_HEIGHT 520
#define TSKMGR_GUI_PX(value) ((int)display_scale_px(value))
#define TSKMGR_GUI_TITLE_HEIGHT ((int)display_scale_px(24))
#define TSKMGR_GUI_CONTROL_SIZE ((int)display_scale_px(24))
#define TSKMGR_GUI_MARGIN ((int)display_scale_px(12))
#define TSKMGR_GUI_TAB_HEIGHT ((int)display_scale_px(24))
#define TSKMGR_GUI_ROW_HEIGHT ((int)display_scale_px(24))
#define TSKMGR_GUI_LIST_HEADER_HEIGHT ((int)display_scale_px(36))
#define TSKMGR_GUI_THREADS_ROW_OFFSET ((int)display_scale_px(70))
#define TSKMGR_GUI_TABS_TOP ((int)display_scale_px(32))
#define TSKMGR_GUI_CONTENT_TOP ((int)display_scale_px(68))
#define TSKMGR_GUI_PROCESS_LIST_TOP \
    (TSKMGR_GUI_CONTENT_TOP + (int)display_scale_px(40))
#define TSKMGR_GUI_PANEL_BOTTOM ((int)display_scale_px(142))
#define TSKMGR_GUI_MAX_VISIBLE_ROWS 14
#define TSKMGR_GUI_DETAIL_WIDTH TSKMGR_GUI_PX(250)
#define TSKMGR_GUI_DETAIL_MIN_WIDTH TSKMGR_GUI_PX(210)
#define TSKMGR_GUI_LIST_MIN_WIDTH TSKMGR_GUI_PX(450)
#define TSKMGR_GUI_DETAIL_GAP TSKMGR_GUI_PX(8)
#define TSKMGR_GUI_HISTORY_SAMPLES 60U
#define TSKMGR_MEMORY_STATS_TICKS 50U

static int is_open = 0;
static int selected_tab = 0;
static int selected_row = 0;
static int scroll_offset = 0;
static int sort_column = 0;
static int show_properties = 0;
static int prop_pid = 0;

static uint32_t last_tick_sample = 0;
static uint32_t last_process_ticks[64] = {0};
static uint32_t tick_usage[64] = {0};
static uint8_t taskmgr_memory_history[TSKMGR_GUI_HISTORY_SAMPLES] = {0};
static uint8_t taskmgr_load_history[TSKMGR_GUI_HISTORY_SAMPLES] = {0};
static uint32_t taskmgr_history_head = 0;
static uint32_t taskmgr_history_count = 0;
static memory_detailed_stats_t taskmgr_memory_stats;
static uint32_t taskmgr_memory_stats_tick = 0;
static uint8_t taskmgr_memory_stats_valid = 0;

static int gui_open = 0;
static int gui_minimized = 0;
static int gui_maximized = 0;
static int gui_drag_active = 0;
static int gui_drag_offset_x = 0;
static int gui_drag_offset_y = 0;
static int gui_drag_previous_x = 40;
static int gui_drag_previous_y = 36;
static int gui_x = 40;
static int gui_y = 36;
static int gui_width = TSKMGR_GUI_DEFAULT_WIDTH;
static int gui_height = TSKMGR_GUI_DEFAULT_HEIGHT;
static int gui_restore_x = 40;
static int gui_restore_y = 36;
static int gui_restore_width = TSKMGR_GUI_DEFAULT_WIDTH;
static int gui_restore_height = TSKMGR_GUI_DEFAULT_HEIGHT;
static uint32_t gui_last_tick = 0;
static uint32_t gui_last_metrics_tick = 0;
static int gui_redraw_pending = 0;
static int taskmgr_hosted = 0;

extern process_t* processes[];
extern uint32_t process_count;

static void taskmgr_gui_draw(void);
static void taskmgr_gui_draw_window(void);
static void taskmgr_gui_draw_drag_region(void);
static void taskmgr_gui_draw_tabs(void);
static void taskmgr_gui_draw_processes(void);
static void taskmgr_gui_draw_memory(void);
static void taskmgr_gui_draw_threads(void);
static void taskmgr_gui_draw_properties(void);
static void taskmgr_hosted_draw(int x, int y, int width, int height);
static int taskmgr_hosted_mouse(mouse_event_t* event, int x, int y,
                                int width, int height);
static void taskmgr_hosted_close(void);
static void taskmgr_gui_copy_text(char* destination, int size, const char* source);
static void taskmgr_update_cpu_metrics(void);
static int taskmgr_get_work_area(tb_rect_t* work_area);
static void taskmgr_clamp_window(void);
static void taskmgr_gui_reset_history(void);
static void taskmgr_gui_sample_history(void);
static void taskmgr_update_memory_stats(void);
static void taskmgr_gui_draw_surface(int x, int y, int width, int height,
                                     uint32_t background, uint32_t border);
static void taskmgr_gui_draw_history_graph(int x, int y, int width, int height,
                                           const char* title,
                                           const uint8_t* history,
                                           uint32_t color);

static const wm_hosted_app_t taskmgr_hosted_app = {
    WM_APP_TASKMGR, "ZephyrOS Task Manager", "TaskMgr",
    TSKMGR_GUI_MIN_WIDTH + WM_HOSTED_FRAME_MAX_WIDTH,
    TSKMGR_GUI_MIN_HEIGHT + WM_HOSTED_FRAME_MAX_HEIGHT,
    TSKMGR_GUI_DEFAULT_WIDTH + WM_HOSTED_FRAME_MAX_WIDTH,
    TSKMGR_GUI_DEFAULT_HEIGHT + WM_HOSTED_FRAME_MAX_HEIGHT,
    WM_KEY_REDRAW_WINDOW_MANAGER,
    taskmgr_hosted_draw, taskmgr_gui_handle_key, taskmgr_hosted_mouse,
    taskmgr_hosted_close
};

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

static uint32_t taskmgr_process_tick_usage(const process_t* process) {
    if (!process) return 0U;
    for (int index = 0; index < MAX_PROCESSES; index++) {
        if (processes[index] == process) return tick_usage[index];
    }
    return 0U;
}

static vesa_color_t taskmgr_gui_color(uint32_t raw) {
    vesa_color_t color;
    color.raw = raw;
    return color;
}

static void taskmgr_gui_draw_surface(int x, int y, int width, int height,
                                     uint32_t background, uint32_t border) {
    if (width <= 0 || height <= 0) return;
    gui_draw_rounded_rect((uint32_t)x, (uint32_t)y, (uint32_t)width,
                          (uint32_t)height,
                          display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                          background);
    gui_draw_flat_border((uint32_t)x, (uint32_t)y, (uint32_t)width,
                         (uint32_t)height, border);
}

static void taskmgr_gui_reset_history(void) {
    for (uint32_t index = 0; index < TSKMGR_GUI_HISTORY_SAMPLES; index++) {
        taskmgr_memory_history[index] = 0;
        taskmgr_load_history[index] = 0;
    }
    taskmgr_history_head = 0;
    taskmgr_history_count = 0;
}

static void taskmgr_update_memory_stats(void) {
    uint32_t now = timer_get_ticks();

    if (taskmgr_memory_stats_valid &&
        now - taskmgr_memory_stats_tick < TSKMGR_MEMORY_STATS_TICKS) {
        return;
    }
    taskmgr_memory_stats_tick = now;
    taskmgr_memory_stats_valid =
        memory_get_detailed_stats(&taskmgr_memory_stats) == OK;
}

static uint32_t taskmgr_gui_aggregate_load(void) {
    uint32_t total = 0;

    for (int index = 0; index < MAX_PROCESSES; index++) {
        if (processes[index]) {
            total += tick_usage[index];
        }
    }
    return total > 100U ? 100U : total;
}

static void taskmgr_gui_sample_history(void) {
    uint32_t memory_percent = taskmgr_percent(memory_get_used(),
                                               memory_get_total());

    taskmgr_memory_history[taskmgr_history_head] = (uint8_t)memory_percent;
    taskmgr_load_history[taskmgr_history_head] =
        (uint8_t)taskmgr_gui_aggregate_load();
    taskmgr_history_head = (taskmgr_history_head + 1U) %
                           TSKMGR_GUI_HISTORY_SAMPLES;
    if (taskmgr_history_count < TSKMGR_GUI_HISTORY_SAMPLES) {
        taskmgr_history_count++;
    }
}

static void taskmgr_gui_draw_history_graph(int x, int y, int width, int height,
                                           const char* title,
                                           const uint8_t* history,
                                           uint32_t color) {
    int plot_x = x + TSKMGR_GUI_PX(8);
    int plot_y = y + TSKMGR_GUI_PX(24);
    int plot_width = width - TSKMGR_GUI_PX(16);
    int plot_height = height - TSKMGR_GUI_PX(32);
    uint32_t oldest;
    vesa_color_t line_color;

    taskmgr_gui_draw_surface(x, y, width, height, GUI_MODERN_COLOR_WINDOW,
                             GUI_MODERN_COLOR_BORDER_INACTIVE);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(8)),
                  (uint32_t)(y + TSKMGR_GUI_PX(6)), title,
                  GUI_MODERN_COLOR_TEXT);
    if (!history || taskmgr_history_count < 2U || plot_width < 2 ||
        plot_height < 2) return;

    vesa_draw_hline((uint32_t)plot_x,
                    (uint32_t)(plot_y + plot_height / 2),
                    (uint32_t)plot_width,
                    taskmgr_gui_color(GUI_MODERN_COLOR_BORDER_INACTIVE));
    oldest = (taskmgr_history_head + TSKMGR_GUI_HISTORY_SAMPLES -
              taskmgr_history_count) % TSKMGR_GUI_HISTORY_SAMPLES;
    line_color.raw = color;
    for (uint32_t point = 1U; point < taskmgr_history_count; point++) {
        uint32_t previous_index = (oldest + point - 1U) %
                                  TSKMGR_GUI_HISTORY_SAMPLES;
        uint32_t current_index = (oldest + point) % TSKMGR_GUI_HISTORY_SAMPLES;
        int previous_x = plot_x + (int)((point - 1U) * (uint32_t)(plot_width - 1) /
                                        (taskmgr_history_count - 1U));
        int current_x = plot_x + (int)(point * (uint32_t)(plot_width - 1) /
                                       (taskmgr_history_count - 1U));
        int previous_y = plot_y + plot_height - 1 -
                         ((int)history[previous_index] * (plot_height - 1) / 100);
        int current_y = plot_y + plot_height - 1 -
                        ((int)history[current_index] * (plot_height - 1) / 100);

        vesa_draw_line(previous_x, previous_y, current_x, current_y, line_color);
    }
}

void taskmgr_init(void) {
    is_open = 0;
    gui_open = 0;
    gui_minimized = 0;
    gui_maximized = 0;
    gui_drag_active = 0;
    gui_drag_previous_x = gui_x;
    gui_drag_previous_y = gui_y;
    selected_tab = 0;
    selected_row = 0;
    scroll_offset = 0;
    show_properties = 0;
    gui_last_tick = 0;
    gui_last_metrics_tick = 0;
    gui_redraw_pending = 0;
    taskmgr_hosted = 0;
    taskmgr_memory_stats_tick = 0;
    taskmgr_memory_stats_valid = 0;
    kmemset(&taskmgr_memory_stats, 0, sizeof(taskmgr_memory_stats));
    taskmgr_gui_reset_history();
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

    if (taskmgr_hosted) {
        wm_close_hosted_app(WM_APP_TASKMGR);
        return;
    }
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
}

static void draw_hline(int x, int y, int w, uint8_t color) {
    for (int i = 0; i < w; i++) {
        video_put_char_at(0xC4, color, x + i, y);
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
        if (!processes[i]) continue;
        switch (processes[i]->state) {
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

    if (current_ticks > last_tick_sample) {
        uint32_t delta_total = current_ticks - last_tick_sample;
        for (int i = 0; i < 64; i++) {
            uint32_t delta_proc = processes[i] ?
                processes[i]->total_ticks - last_process_ticks[i] : 0U;
            tick_usage[i] = delta_total ? (delta_proc * 100) / delta_total : 0;
            if (tick_usage[i] > 100) tick_usage[i] = 100;
        }
    }

    last_tick_sample = current_ticks;
    for (int i = 0; i < 64; i++) {
        last_process_ticks[i] = processes[i] ? processes[i]->total_ticks : 0U;
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
    print_at(start_x + 30, start_y, "TCK%", sort_column == 3 ? COLOR_HIGHLIGHT : COLOR_HEADER);
    print_at(start_x + 37, start_y, "Tipo", COLOR_HEADER);
    print_at(start_x + 46, start_y, "Tempo", COLOR_HEADER);
    print_at(start_x + 56, start_y, "Espera", COLOR_HEADER);

    draw_hline(start_x, start_y + 1, table_w, COLOR_BORDER);

    uint32_t total_active = 0;

    // Collect active processes
    int active_pids[64];
    for (int i = 0; i < 64; i++) {
        if (processes[i]) {
            
            active_pids[total_active++] = i;
        }
    }

    // Sort active_pids based on sort_column (Insertion Sort)
    for (uint32_t i = 1U; i < total_active; i++) {
        int key = active_pids[i];
        int j = i - 1;
        
        while (j >= 0) {
            int p1 = active_pids[j];
            int p2 = key;
            int swap = 0;
            
            if (sort_column == 0) {
                swap = (processes[p1]->pid > processes[p2]->pid);
            } else if (sort_column == 1) {
                int k = 0;
                while (processes[p1]->name[k] && processes[p2]->name[k] && processes[p1]->name[k] == processes[p2]->name[k]) k++;
                swap = (processes[p1]->name[k] > processes[p2]->name[k]);
            } else if (sort_column == 2) {
                swap = (processes[p1]->state > processes[p2]->state);
            } else if (sort_column == 3) {
                swap = (tick_usage[p1] < tick_usage[p2]); // Estimativa por ticks.
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

        if (row >= scroll_offset && visible_row < TSKMGR_SIMPLE_PROCESS_ROWS) {
            int y = start_y + 2 + visible_row;
            uint8_t row_color = COLOR_TEXT;

            if (selected_row == row) {
                for (int x = 0; x < table_w; x++) {
                    video_put_char_at(0x20, COLOR_SELECTION, start_x + x, y);
                }
                row_color = COLOR_SELECTION;
                prop_pid = processes[i]->pid;
            }

            print_num_at(start_x, y, processes[i]->pid, row_color);

            char name[13];
            int n = 0;
            while (processes[i]->name[n] && n < 12) {
                name[n] = processes[i]->name[n];
                n++;
            }
            name[n] = 0;
            print_at(start_x + 5, y, name, row_color);

            const char* state_str = "?";
            uint8_t state_color = COLOR_TEXT;
            switch (processes[i]->state) {
                case PROCESS_STATE_READY:    state_str = "Pronto";    state_color = COLOR_BAR_FG; break;
                case PROCESS_STATE_RUNNING:  state_str = "Rodando";   state_color = COLOR_HIGHLIGHT; break;
                case PROCESS_STATE_BLOCKED:  state_str = "Bloqueado"; state_color = COLOR_BAR_WARN; break;
                case PROCESS_STATE_ZOMBIE:   state_str = "Zombie";    state_color = COLOR_DEAD; break;
                default: state_str = "?"; break;
            }
            print_at(start_x + 19, y, state_str,
                     selected_row == row ? COLOR_SELECTION : state_color);

            uint8_t cpu_color = COLOR_TEXT;
            if (tick_usage[i] > 80) cpu_color = COLOR_DEAD;
            else if (tick_usage[i] > 50) cpu_color = COLOR_BAR_WARN;

            if (selected_row == row) cpu_color = COLOR_SELECTION;
            print_num_at(start_x + 30, y, tick_usage[i], cpu_color);
            print_at(start_x + 33, y, "%", cpu_color);
            print_at(start_x + 37, y, taskmgr_process_type(processes[i]), row_color);

            uint32_t ticks = processes[i]->total_ticks;
            uint32_t secs = ticks / TSKMGR_TICKS_PER_SECOND;
            print_num_at(start_x + 46, y, secs, row_color);
            print_at(start_x + 46 + num_digits(secs), y, "s", row_color);
            print_num_at(start_x + 56, y, processes[i]->wait_ticks, row_color);
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
            if (processes[i] && processes[i]->pid == (uint32_t)prop_pid) p = processes[i];
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
            print_at(px + 2, py + 5, "TCK:", COLOR_SELECTION);
            print_num_at(px + 10, py + 5, taskmgr_process_tick_usage(p), 0x1E);
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

    taskmgr_update_memory_stats();

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

    if (taskmgr_memory_stats_valid) {
        print_at(start_x, start_y + 13, "Kernel:", COLOR_HEADER);
        print_num_at(start_x + 8, start_y + 13,
                     taskmgr_memory_stats.zone_pages[MEMORY_ZONE_KERNEL] *
                     (PAGE_SIZE / 1024U), COLOR_TEXT);
        print_at(start_x + 20, start_y + 13, "Heap:", COLOR_HEADER);
        print_num_at(start_x + 26, start_y + 13,
                     taskmgr_memory_stats.zone_pages[MEMORY_ZONE_HEAP] *
                     (PAGE_SIZE / 1024U), COLOR_TEXT);
        print_at(start_x + 38, start_y + 13, "SLAB:", COLOR_HEADER);
        print_num_at(start_x + 44, start_y + 13,
                     taskmgr_memory_stats.zone_pages[MEMORY_ZONE_SLAB] *
                     (PAGE_SIZE / 1024U), COLOR_TEXT);
        print_at(start_x, start_y + 14, "Process:", COLOR_HEADER);
        print_num_at(start_x + 9, start_y + 14,
                     taskmgr_memory_stats.zone_pages[MEMORY_ZONE_PROCESS] *
                     (PAGE_SIZE / 1024U), COLOR_TEXT);
        print_at(start_x + 25, start_y + 14, "Buffer:", COLOR_HEADER);
        print_num_at(start_x + 33, start_y + 14,
                     taskmgr_memory_stats.zone_pages[MEMORY_ZONE_BUFFER] *
                     (PAGE_SIZE / 1024U), COLOR_TEXT);
        print_at(start_x + 45, start_y + 14, "Livre:", COLOR_HEADER);
        print_num_at(start_x + 52, start_y + 14,
                     taskmgr_memory_stats.zone_pages[MEMORY_ZONE_FREE] *
                     (PAGE_SIZE / 1024U), COLOR_BAR_FG);
        print_at(start_x, start_y + 15, "Runs:", COLOR_HEADER);
        print_num_at(start_x + 6, start_y + 15,
                     taskmgr_memory_stats.free_runs, COLOR_TEXT);
        print_at(start_x + 18, start_y + 15, "Maior:", COLOR_HEADER);
        print_num_at(start_x + 25, start_y + 15,
                     taskmgr_memory_stats.largest_free_run, COLOR_TEXT);
        print_at(start_x + 39, start_y + 15, "Iso:", COLOR_HEADER);
        print_num_at(start_x + 44, start_y + 15,
                     taskmgr_memory_stats.isolated_free_pages, COLOR_TEXT);
        print_at(start_x + 57, start_y + 15, "F:", COLOR_HEADER);
        print_num_at(start_x + 60, start_y + 15,
                     taskmgr_memory_stats.fragmentation_percent, COLOR_TEXT);
        print_at(start_x + 63, start_y + 15, "%", COLOR_TEXT);
    } else {
        print_at(start_x, start_y + 13, "Metricas MM4 indisponiveis", COLOR_DEAD);
    }

    print_at(start_x, start_y + 12, "ATA:", COLOR_HEADER);
    print_at(start_x + 6, start_y + 12, "R", COLOR_TEXT);
    print_num_at(start_x + 8, start_y + 12, ata_get_read_ops(), COLOR_HIGHLIGHT);
    print_at(start_x + 20, start_y + 12, "W", COLOR_TEXT);
    print_num_at(start_x + 22, start_y + 12, ata_get_write_ops(), COLOR_HIGHLIGHT);
    print_at(start_x + 38, start_y + 12, "Estado", COLOR_TEXT);
    print_at(start_x + 45, start_y + 12,
             device && device->present ? "READY" : "N/D", COLOR_HIGHLIGHT);
    print_at(start_x, start_y + 16, "Modelo:", COLOR_TEXT);
    if (device && device->present) {
        char model[21];
        taskmgr_gui_copy_text(model, sizeof(model), device->model);
        print_at(start_x + 8, start_y + 16, model, COLOR_TEXT);
    } else {
        print_at(start_x + 8, start_y + 16, "N/D", COLOR_DEAD);
    }
    print_at(start_x + 45, start_y + 16, "Set:", COLOR_TEXT);
    if (device && device->present) {
        print_num_at(start_x + 50, start_y + 16, device->sectors, COLOR_HIGHLIGHT);
    } else {
        print_at(start_x + 50, start_y + 16, "N/D", COLOR_DEAD);
    }

    if (!total) {
        print_at(start_x, start_y + 12, "Metricas de memoria indisponiveis", COLOR_DEAD);
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
    if (selected_row >= scroll_offset + TSKMGR_SIMPLE_THREAD_ROWS) {
        scroll_offset = selected_row - TSKMGR_SIMPLE_THREAD_ROWS + 1;
    }

    int row = 0;
    for (int visible = 0; visible < TSKMGR_SIMPLE_THREAD_ROWS &&
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
    taskbar_draw();
}

static void taskmgr_redraw_after_menu_close(void) {
    video_clear();
    taskmgr_refresh();
}

static void taskmgr_handle_taskbar_action(int result) {
    int prepare_result;

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
            power_reboot();
            break;
        case 6:
            prepare_result = power_shutdown_prepare();
            if (prepare_result != OK) {
                video_print("Desligamento recusado: sync falhou (codigo ",
                            0x0C);
                shell_command_print_num((uint32_t)prepare_result);
                video_print(").\n", 0x0C);
                break;
            }
            taskmgr_close();
            power_shutdown();
            break;
        case 7:
            taskmgr_close();
            video_clear();
            desktop_set_active(1);
            desktop_draw();
            break;
        case 8:
            taskmgr_close();
            settings_open();
            break;
        case TB_ACTION_UPDATER:
            taskmgr_close();
            shell_handle_app_request(IPC_APP_OPEN_UPDATER);
            break;
        case TB_ACTION_APPSTORE:
            taskmgr_close();
            shell_handle_app_request(IPC_APP_OPEN_APP_STORE);
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
            if (processes[i] && processes[i]->pid == (uint32_t)prop_pid) {
                int is_explorer = 0;
                int is_taskmgr = 0;
                char* n = processes[i]->name;
                if (n[0]=='E' && n[1]=='x' && n[2]=='p') is_explorer = 1;
                if (n[0]=='T' && n[1]=='a' && n[2]=='s' && n[3]=='k') is_taskmgr = 1;
                
                if (is_explorer || is_taskmgr) {
                    process_destroy(processes[i]);
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
                if (processes[i]) count++;
            }
        } else if (selected_tab == 2) {
            count = (int)thread_get_count();
        }
        if (selected_row + 1 < count) {
            selected_row++;
        }
        if (selected_tab == 0 &&
            selected_row >= scroll_offset + TSKMGR_SIMPLE_PROCESS_ROWS) {
            scroll_offset++;
        }
        if (selected_tab == 2 &&
            selected_row >= scroll_offset + TSKMGR_SIMPLE_THREAD_ROWS) {
            scroll_offset++;
        }
        taskmgr_refresh();
        return;
    }

    if (scancode == 0x53 && selected_tab == 0) {
        int row = 0;
        for (int i = 0; i < 64; i++) {
            if (processes[i]) {
                if (row == selected_row) {
                    if (process_is_user(processes[i])) {
                        if (process_signal_send(processes[i]->pid,
                                                APP_SIGNAL_KILL) != OK) {
                            LOG_WARN("SHELL", "SIGKILL recusado pelo Task Manager");
                        }
                        if (selected_row > 0) selected_row--;
                    } else {
                        LOG_WARN("SHELL", "Task Manager protegeu processo nativo");
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

static int taskmgr_get_work_area(tb_rect_t* work_area) {
    vesa_mode_t* mode = vesa_get_mode();
    if (!work_area || !mode || !mode->initialized) return 0;
    work_area->x = 0;
    work_area->y = 0;
    work_area->width = mode->width;
    work_area->height = mode->height;
    taskbar_get_work_area(work_area);
    return work_area->width > 0 && work_area->height > 0;
}

static void taskmgr_clamp_window(void) {
    tb_rect_t work_area;

    if (!taskmgr_get_work_area(&work_area)) return;
    if (gui_width > work_area.width) gui_width = work_area.width;
    if (gui_height > work_area.height) gui_height = work_area.height;
    if (gui_width < 1 || gui_height < 1) return;

    if (gui_x < work_area.x) gui_x = work_area.x;
    if (gui_y < work_area.y) gui_y = work_area.y;
    if (gui_x + gui_width > work_area.x + work_area.width) {
        gui_x = work_area.x + work_area.width - gui_width;
    }
    if (gui_y + gui_height > work_area.y + work_area.height) {
        gui_y = work_area.y + work_area.height - gui_height;
    }
}

static int taskmgr_gui_visible_rows(void) {
    int rows = (gui_height - (int)display_scale_px(250)) /
               TSKMGR_GUI_ROW_HEIGHT;
    if (rows < 1) rows = 1;
    if (rows > TSKMGR_GUI_MAX_VISIBLE_ROWS) rows = TSKMGR_GUI_MAX_VISIBLE_ROWS;
    return rows;
}

static int taskmgr_gui_has_side_details(void) {
    int content_width = gui_width - (TSKMGR_GUI_MARGIN * 2);
    return content_width >= TSKMGR_GUI_LIST_MIN_WIDTH +
           TSKMGR_GUI_DETAIL_MIN_WIDTH + TSKMGR_GUI_PX(16);
}

static int taskmgr_gui_process_list_y(void) {
    return gui_y + TSKMGR_GUI_PROCESS_LIST_TOP;
}

static int taskmgr_gui_process_list_height(void) {
    display_metrics_t metrics;
    int height = gui_height + TSKMGR_GUI_CONTENT_TOP -
                 TSKMGR_GUI_PANEL_BOTTOM - TSKMGR_GUI_PROCESS_LIST_TOP;
    if (!taskmgr_gui_has_side_details() &&
        (display_get_metrics(&metrics) != OK ||
         metrics.scale != DISPLAY_SCALE_LARGE)) {
        height -= (int)display_scale_px(90);
    }
    return height;
}

static int taskmgr_gui_process_visible_rows(void) {
    int rows = (taskmgr_gui_process_list_height() -
                TSKMGR_GUI_LIST_HEADER_HEIGHT) / TSKMGR_GUI_ROW_HEIGHT;
    if (rows < 1) rows = 1;
    if (rows > TSKMGR_GUI_MAX_VISIBLE_ROWS) rows = TSKMGR_GUI_MAX_VISIBLE_ROWS;
    return rows;
}

static int taskmgr_collect_processes(int* process_indexes) {
    int count = 0;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i]) {
            process_indexes[count++] = i;
        }
    }

    for (int i = 1; i < count; i++) {
        int key = process_indexes[i];
        int j = i - 1;
        while (j >= 0) {
            int left = process_indexes[j];
            int swap = 0;
            if (sort_column == 0) swap = processes[left]->pid > processes[key]->pid;
            if (sort_column == 1) {
                int k = 0;
                while (processes[left]->name[k] && processes[key]->name[k] &&
                       processes[left]->name[k] == processes[key]->name[k]) k++;
                swap = processes[left]->name[k] > processes[key]->name[k];
            }
            if (sort_column == 2) swap = processes[left]->state > processes[key]->state;
            if (sort_column == 3) swap = tick_usage[left] < tick_usage[key];
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
    return processes[indexes[row]];
}

static process_t* taskmgr_find_process_by_pid(int pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i] && processes[i]->pid == (uint32_t)pid) {
            return processes[i];
        }
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
    int height = TSKMGR_GUI_PX(14);
    int fill_width;

    if (percent > 100) percent = 100;
    if (width < 8 || height < 3) return;
    bg.raw = GUI_MODERN_COLOR_BORDER_INACTIVE;
    fill.raw = color;
    vesa_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)width,
                   (uint32_t)height, bg);
    fill_width = (width - 2) * (int)percent / 100;
    if (fill_width > 0) {
        vesa_fill_rect((uint32_t)(x + 1), (uint32_t)(y + 1),
                       (uint32_t)fill_width, (uint32_t)(height - 2), fill);
    }
}

static uint32_t taskmgr_gui_state_color(process_state_t state) {
    switch (state) {
        case PROCESS_STATE_RUNNING: return 0x005FBF7FU;
        case PROCESS_STATE_BLOCKED: return 0x00E0A850U;
        case PROCESS_STATE_ZOMBIE: return 0x00D65A5AU;
        default: return GUI_MODERN_COLOR_TEXT;
    }
}

static void taskmgr_gui_draw_tabs(void) {
    const char* names[] = {"Processos", "Memoria", "Threads"};
    int tab_width = (int)display_scale_px(112);
    int tab_gap = (int)display_scale_px(4);
    int x = gui_x + TSKMGR_GUI_MARGIN;
    int y = gui_y + TSKMGR_GUI_TABS_TOP;

    for (int i = 0; i < 3; i++) {
        gui_draw_modern_button((uint32_t)x, (uint32_t)y,
                               (uint32_t)tab_width, TSKMGR_GUI_TAB_HEIGHT,
                               names[i], selected_tab == i ?
                               GUI_BUTTON_STATE_PRESSED : GUI_BUTTON_STATE_NORMAL);
        x += tab_width + tab_gap;
    }
}

static void taskmgr_gui_draw_process_details(process_t* process, int x, int y,
                                             int width, int height) {
    char name[19];
    int right_x = x + width / 2;

    taskmgr_gui_draw_surface(x, y, width, height, GUI_MODERN_COLOR_WINDOW,
                             GUI_MODERN_COLOR_BORDER_INACTIVE);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 8),
                  "Detalhes selecionados", GUI_MODERN_COLOR_TEXT);
    if (!process) {
        gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 38),
                      "Nenhum processo", GUI_MODERN_COLOR_BORDER_INACTIVE);
        return;
    }
    taskmgr_gui_copy_text(name, sizeof(name), process->name);
    if (width < TSKMGR_GUI_PX(400)) name[8] = '\0';

    if (height < 130) {
        gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 32), "PID:", GUI_COLOR_TEXT);
        taskmgr_gui_draw_num(x + 48, y + 32, process->pid, GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)right_x, (uint32_t)(y + 32),
                      "TCK:", GUI_COLOR_TEXT);
        taskmgr_gui_draw_num(right_x + TSKMGR_GUI_PX(40), y + 32,
                             taskmgr_process_tick_usage(process), GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(right_x + TSKMGR_GUI_PX(64)),
                      (uint32_t)(y + 32), "%", GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 56), "Nome:", GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 48), (uint32_t)(y + 56), name, GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 80), "Estado:", GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 66), (uint32_t)(y + 80),
                      taskmgr_process_state_name(process->state), GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)right_x, (uint32_t)(y + 80),
                      "Wait:", GUI_COLOR_TEXT);
        taskmgr_gui_draw_num(right_x + TSKMGR_GUI_PX(48), y + 80,
                             process->wait_ticks, GUI_COLOR_TEXT);
        return;
    }

    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 34), "PID:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 48, y + 34, process->pid, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)right_x, (uint32_t)(y + 34),
                  "TCK:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(right_x + TSKMGR_GUI_PX(40), y + 34,
                         taskmgr_process_tick_usage(process), GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(right_x + TSKMGR_GUI_PX(64)),
                  (uint32_t)(y + 34), "%", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 58), "Nome:", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 48), (uint32_t)(y + 58), name, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)right_x, (uint32_t)(y + 58),
                  "Tipo:", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(right_x + TSKMGR_GUI_PX(40)),
                  (uint32_t)(y + 58),
                  taskmgr_process_type(process), GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 82), "Estado:", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 66), (uint32_t)(y + 82),
                  taskmgr_process_state_name(process->state), GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)right_x, (uint32_t)(y + 82),
                  "Wait:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(right_x + TSKMGR_GUI_PX(48), y + 82,
                         process->wait_ticks, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 106), "Tipo:", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 50), (uint32_t)(y + 106),
                  taskmgr_process_type(process), GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)right_x, (uint32_t)(y + 106),
                  "Tempo:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(right_x + TSKMGR_GUI_PX(56), y + 106,
                         process->total_ticks / TSKMGR_TICKS_PER_SECOND,
                         GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(right_x + TSKMGR_GUI_PX(80)),
                  (uint32_t)(y + 106), "s", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 130), "Ticks:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 56, y + 130, process->total_ticks, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)right_x, (uint32_t)(y + 130),
                  "Prox:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(right_x + TSKMGR_GUI_PX(48), y + 130,
                         process->next_pid, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 154), "EIP:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_hex(x + 50, y + 154, process->context.eip, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 178), "ESP:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_hex(x + 50, y + 178, process->context.esp, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 202), "CR3:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_hex(x + 50, y + 202, process->context.cr3, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 226), "Stk:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_hex(x + 50, y + 226, process->kernel_stack_top, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 250), "Page dir:", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 82), (uint32_t)(y + 250),
                  process->page_directory ? "OK" : "N/D", GUI_COLOR_TEXT);
}

static void taskmgr_gui_draw_processes(void) {
    int indexes[MAX_PROCESSES];
    int count = taskmgr_collect_processes(indexes);
    int x = gui_x + TSKMGR_GUI_MARGIN;
    int y = gui_y + TSKMGR_GUI_CONTENT_TOP;
    int width = gui_width - (TSKMGR_GUI_MARGIN * 2);
    int panel_height = gui_height - TSKMGR_GUI_PANEL_BOTTOM;
    int list_y = taskmgr_gui_process_list_y();
    int list_height = taskmgr_gui_process_list_height();
    int list_width = width;
    int detail_x = 0;
    uint32_t ready;
    uint32_t running;
    uint32_t blocked;
    uint32_t zombie;
    uint32_t tick_sum = 0;

    taskmgr_count_process_states(&ready, &running, &blocked, &zombie);
    for (int i = 0; i < count; i++) tick_sum += tick_usage[indexes[i]];
    if (selected_row >= count) selected_row = count ? count - 1 : 0;
    if (selected_row < scroll_offset) scroll_offset = selected_row;
    if (selected_row >= scroll_offset + taskmgr_gui_process_visible_rows()) {
        scroll_offset = selected_row - taskmgr_gui_process_visible_rows() + 1;
    }

    taskmgr_gui_draw_surface(x, y, width, panel_height,
                             GUI_MODERN_COLOR_WINDOW,
                             GUI_MODERN_COLOR_BORDER_INACTIVE);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(10)),
                  (uint32_t)(y + TSKMGR_GUI_PX(8)), "Processos:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + TSKMGR_GUI_PX(94), y + TSKMGR_GUI_PX(8),
                         (uint32_t)count, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(140)),
                  (uint32_t)(y + TSKMGR_GUI_PX(8)), "Prontos:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + TSKMGR_GUI_PX(212), y + TSKMGR_GUI_PX(8),
                         ready, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(250)),
                  (uint32_t)(y + TSKMGR_GUI_PX(8)), "Rodando:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + TSKMGR_GUI_PX(322), y + TSKMGR_GUI_PX(8),
                         running, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(350)),
                  (uint32_t)(y + TSKMGR_GUI_PX(8)), "TCK soma:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + TSKMGR_GUI_PX(430), y + TSKMGR_GUI_PX(8),
                         tick_sum, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(454)),
                  (uint32_t)(y + TSKMGR_GUI_PX(8)), "%", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(10)),
                  (uint32_t)(y + TSKMGR_GUI_PX(24)), "Bloq:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + TSKMGR_GUI_PX(58), y + TSKMGR_GUI_PX(24),
                         blocked, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(110)),
                  (uint32_t)(y + TSKMGR_GUI_PX(24)), "Zombie:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + TSKMGR_GUI_PX(174), y + TSKMGR_GUI_PX(24),
                         zombie, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(230)),
                  (uint32_t)(y + TSKMGR_GUI_PX(24)), "Threads:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + TSKMGR_GUI_PX(302), y + TSKMGR_GUI_PX(24),
                         thread_get_count(), GUI_COLOR_TEXT);

    if (taskmgr_gui_has_side_details()) {
        list_width = width - TSKMGR_GUI_DETAIL_WIDTH - TSKMGR_GUI_DETAIL_GAP;
        detail_x = x + list_width + TSKMGR_GUI_DETAIL_GAP;
    }
    taskmgr_gui_draw_surface(x, list_y, list_width, list_height,
                             GUI_MODERN_COLOR_WINDOW,
                             GUI_MODERN_COLOR_BORDER_INACTIVE);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(10)),
                  (uint32_t)(list_y + TSKMGR_GUI_PX(8)), "PID", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(56)),
                  (uint32_t)(list_y + TSKMGR_GUI_PX(8)), "Nome", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(170)),
                  (uint32_t)(list_y + TSKMGR_GUI_PX(8)), "Estado", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(260)),
                  (uint32_t)(list_y + TSKMGR_GUI_PX(8)), "CPU", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(310)),
                  (uint32_t)(list_y + TSKMGR_GUI_PX(8)), "Tipo", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(370)),
                  (uint32_t)(list_y + TSKMGR_GUI_PX(8)), "Tempo", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(440)),
                  (uint32_t)(list_y + TSKMGR_GUI_PX(8)), "Wait", GUI_COLOR_TEXT);
    vesa_draw_hline((uint32_t)(x + TSKMGR_GUI_PX(6)),
                    (uint32_t)(list_y + TSKMGR_GUI_PX(30)),
                    (uint32_t)(list_width - TSKMGR_GUI_PX(12)),
                    taskmgr_gui_color(GUI_COLOR_BORDER_D));

    for (int row = 0; row < taskmgr_gui_process_visible_rows() &&
         scroll_offset + row < count; row++) {
        int absolute_row = scroll_offset + row;
        process_t* process = processes[indexes[absolute_row]];
        int row_y = list_y + TSKMGR_GUI_LIST_HEADER_HEIGHT +
                    row * TSKMGR_GUI_ROW_HEIGHT;
        uint32_t text_color = absolute_row == selected_row ? GUI_COLOR_TEXT_W : GUI_COLOR_TEXT;
        uint32_t seconds = process->total_ticks / TSKMGR_TICKS_PER_SECOND;
        char name[14];
        uint32_t state_color = absolute_row == selected_row ? GUI_COLOR_TEXT_W :
                               taskmgr_gui_state_color(process->state);

        if (absolute_row == selected_row) {
            taskmgr_gui_draw_surface(x + TSKMGR_GUI_PX(5), row_y,
                                     list_width - TSKMGR_GUI_PX(10),
                                     TSKMGR_GUI_ROW_HEIGHT,
                                     GUI_MODERN_COLOR_HOVER,
                                     GUI_MODERN_COLOR_ACCENT);
            prop_pid = (int)process->pid;
        }

        taskmgr_gui_copy_text(name, sizeof(name), process->name);
        taskmgr_gui_draw_num(x + TSKMGR_GUI_PX(10),
                             row_y + TSKMGR_GUI_PX(4),
                             process->pid, text_color);
        gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(56)),
                      (uint32_t)(row_y + TSKMGR_GUI_PX(4)), name, text_color);
        gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(170)),
                      (uint32_t)(row_y + TSKMGR_GUI_PX(4)),
                      taskmgr_process_state_name(process->state), state_color);
        taskmgr_gui_draw_num(x + TSKMGR_GUI_PX(260),
                             row_y + TSKMGR_GUI_PX(4),
                             tick_usage[indexes[absolute_row]], text_color);
        gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(284)),
                      (uint32_t)(row_y + TSKMGR_GUI_PX(4)), "%", text_color);
        gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(310)),
                      (uint32_t)(row_y + TSKMGR_GUI_PX(4)),
                      taskmgr_process_type(process), text_color);
        taskmgr_gui_draw_num(x + TSKMGR_GUI_PX(370),
                             row_y + TSKMGR_GUI_PX(4), seconds, text_color);
        gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(370) +
                      num_digits(seconds) * TSKMGR_GUI_PX(8)),
                      (uint32_t)(row_y + TSKMGR_GUI_PX(4)), "s", text_color);
        taskmgr_gui_draw_num(x + TSKMGR_GUI_PX(440),
                             row_y + TSKMGR_GUI_PX(4),
                             process->wait_ticks, text_color);
    }

    if (!count) {
        gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(16)),
                      (uint32_t)(list_y + TSKMGR_GUI_PX(48)),
                      "Nenhum processo ativo", GUI_COLOR_TEXT);
    }

    process_t* selected = taskmgr_find_process_by_row(selected_row);
    if (taskmgr_gui_has_side_details()) {
        taskmgr_gui_draw_process_details(selected, detail_x, list_y,
                                         TSKMGR_GUI_DETAIL_WIDTH, list_height);
    } else {
        int detail_y = list_y + list_height + TSKMGR_GUI_PX(6);
        int detail_height = y + panel_height - detail_y;
        if (detail_height >= TSKMGR_GUI_PX(80)) {
            taskmgr_gui_draw_process_details(selected, x, detail_y,
                                             list_width, detail_height);
        }
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
    int panel_x = gui_x + TSKMGR_GUI_MARGIN;
    int panel_y = gui_y + TSKMGR_GUI_CONTENT_TOP;
    int panel_width = gui_width - TSKMGR_GUI_MARGIN * 2;
    int panel_height = gui_height - TSKMGR_GUI_PANEL_BOTTOM;
    int x = panel_x + TSKMGR_GUI_PX(18);
    int y = gui_y + TSKMGR_GUI_CONTENT_TOP + TSKMGR_GUI_PX(14);
    uint32_t bar_color = used_percent > 80 ? 0x00D65A5AU :
                         (used_percent > 60 ? 0x00E0A850U : 0x005FBF7FU);

    taskmgr_update_memory_stats();

    taskmgr_gui_draw_surface(panel_x, panel_y, panel_width, panel_height,
                             GUI_MODERN_COLOR_WINDOW,
                             GUI_MODERN_COLOR_BORDER_INACTIVE);
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

    if (taskmgr_memory_stats_valid) {
        gui_draw_text((uint32_t)x, (uint32_t)(y + 196), "Zonas KB", GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 324), (uint32_t)(y + 196), "Frag:", GUI_COLOR_TEXT);
        taskmgr_gui_draw_num(x + 374, y + 196,
                             taskmgr_memory_stats.fragmentation_percent,
                             GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 406), (uint32_t)(y + 196), "%", GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)x, (uint32_t)(y + 220), "Kernel:", GUI_COLOR_TEXT);
        taskmgr_gui_draw_num(x + 96, y + 220,
                             taskmgr_memory_stats.zone_pages[MEMORY_ZONE_KERNEL] *
                             (PAGE_SIZE / 1024U), GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 176), (uint32_t)(y + 220), "Heap:", GUI_COLOR_TEXT);
        taskmgr_gui_draw_num(x + 244, y + 220,
                             taskmgr_memory_stats.zone_pages[MEMORY_ZONE_HEAP] *
                             (PAGE_SIZE / 1024U), GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 324), (uint32_t)(y + 220), "SLAB:", GUI_COLOR_TEXT);
        taskmgr_gui_draw_num(x + 400, y + 220,
                             taskmgr_memory_stats.zone_pages[MEMORY_ZONE_SLAB] *
                             (PAGE_SIZE / 1024U), GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)x, (uint32_t)(y + 244), "Processos:", GUI_COLOR_TEXT);
        taskmgr_gui_draw_num(x + 96, y + 244,
                             taskmgr_memory_stats.zone_pages[MEMORY_ZONE_PROCESS] *
                             (PAGE_SIZE / 1024U), GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 176), (uint32_t)(y + 244), "Buffers:", GUI_COLOR_TEXT);
        taskmgr_gui_draw_num(x + 244, y + 244,
                             taskmgr_memory_stats.zone_pages[MEMORY_ZONE_BUFFER] *
                             (PAGE_SIZE / 1024U), GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 324), (uint32_t)(y + 244), "Livre:", GUI_COLOR_TEXT);
        taskmgr_gui_draw_num(x + 400, y + 244,
                             taskmgr_memory_stats.zone_pages[MEMORY_ZONE_FREE] *
                             (PAGE_SIZE / 1024U), GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)x, (uint32_t)(y + 268), "PMM runs:", GUI_COLOR_TEXT);
        taskmgr_gui_draw_num(x + 96, y + 268,
                             taskmgr_memory_stats.free_runs, GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 176), (uint32_t)(y + 268), "Maior:", GUI_COLOR_TEXT);
        taskmgr_gui_draw_num(x + 244, y + 268,
                             taskmgr_memory_stats.largest_free_run, GUI_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + 324), (uint32_t)(y + 268), "Iso:", GUI_COLOR_TEXT);
        taskmgr_gui_draw_num(x + 372, y + 268,
                             taskmgr_memory_stats.isolated_free_pages, GUI_COLOR_TEXT);
    } else {
        gui_draw_text((uint32_t)x, (uint32_t)(y + 196),
                      "Metricas MM4 indisponiveis", GUI_COLOR_TEXT);
    }

    gui_draw_text((uint32_t)(x + 460), (uint32_t)(y + 196), "Disco ATA", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 460), (uint32_t)(y + 220), "R:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 484, y + 220, ata_get_read_ops(), GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 550), (uint32_t)(y + 220), "W:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 574, y + 220, ata_get_write_ops(), GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 460), (uint32_t)(y + 244), "Estado:", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 530), (uint32_t)(y + 244),
                  device && device->present ? "READY" : "N/D", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 460), (uint32_t)(y + 268), "Modelo:", GUI_COLOR_TEXT);
    if (device && device->present) {
        char model[9];
        taskmgr_gui_copy_text(model, sizeof(model), device->model);
        gui_draw_text((uint32_t)(x + 530), (uint32_t)(y + 268), model, GUI_COLOR_TEXT);
    } else {
        gui_draw_text((uint32_t)(x + 530), (uint32_t)(y + 268), "N/D", GUI_COLOR_TEXT);
    }
    gui_draw_text((uint32_t)(x + 600), (uint32_t)(y + 268), "Set:", GUI_COLOR_TEXT);
    if (device && device->present) {
        taskmgr_gui_draw_num(x + 644, y + 268, device->sectors, GUI_COLOR_TEXT);
    } else {
        gui_draw_text((uint32_t)(x + 644), (uint32_t)(y + 268), "N/D", GUI_COLOR_TEXT);
    }
    if (!total) {
        gui_draw_text((uint32_t)(x + 196), (uint32_t)(y + 34),
                      "N/D", 0x00D65A5AU);
    }
    {
        int graph_y = y + TSKMGR_GUI_PX(276);
        int graph_height = panel_y + panel_height - graph_y - TSKMGR_GUI_PX(8);
        int graph_gap = TSKMGR_GUI_PX(8);
        int graph_width = (panel_width - TSKMGR_GUI_PX(36) - graph_gap) / 2;

        if (graph_height >= TSKMGR_GUI_PX(44) && graph_width >= TSKMGR_GUI_PX(120)) {
            taskmgr_gui_draw_history_graph(x, graph_y, graph_width, graph_height,
                                           "Memoria usada", taskmgr_memory_history,
                                           GUI_MODERN_COLOR_ACCENT);
            taskmgr_gui_draw_history_graph(x + graph_width + graph_gap, graph_y,
                                           graph_width, graph_height,
                                           "Carga agregada", taskmgr_load_history,
                                           0x005FBF7FU);
        }
    }
}

static void taskmgr_gui_draw_threads(void) {
    int x = gui_x + TSKMGR_GUI_MARGIN;
    int y = gui_y + TSKMGR_GUI_CONTENT_TOP;
    int width = gui_width - TSKMGR_GUI_MARGIN * 2;
    int panel_height = gui_height - TSKMGR_GUI_PANEL_BOTTOM;
    int visible_rows = taskmgr_gui_visible_rows();
    int total_threads = 0;
    int running_threads = 0;
    int blocked_threads = 0;

    taskmgr_gui_draw_surface(x, y, width, panel_height,
                             GUI_MODERN_COLOR_WINDOW,
                             GUI_MODERN_COLOR_BORDER_INACTIVE);
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
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(10)),
                  (uint32_t)(y + TSKMGR_GUI_PX(8)), "Threads:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + TSKMGR_GUI_PX(76), y + TSKMGR_GUI_PX(8),
                         (uint32_t)total_threads, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(130)),
                  (uint32_t)(y + TSKMGR_GUI_PX(8)), "Rodando:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + TSKMGR_GUI_PX(198), y + TSKMGR_GUI_PX(8),
                         (uint32_t)running_threads, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(260)),
                  (uint32_t)(y + TSKMGR_GUI_PX(8)), "Bloqueadas:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + TSKMGR_GUI_PX(350), y + TSKMGR_GUI_PX(8),
                         (uint32_t)blocked_threads, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(10)),
                  (uint32_t)(y + TSKMGR_GUI_PX(42)), "TID", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(70)),
                  (uint32_t)(y + TSKMGR_GUI_PX(42)), "Nome", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(260)),
                  (uint32_t)(y + TSKMGR_GUI_PX(42)), "Estado", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(350)),
                  (uint32_t)(y + TSKMGR_GUI_PX(42)), "Espera", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(420)),
                  (uint32_t)(y + TSKMGR_GUI_PX(42)), "EIP", GUI_COLOR_TEXT);
    if (width > TSKMGR_GUI_PX(620)) {
        gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(540)),
                      (uint32_t)(y + TSKMGR_GUI_PX(42)), "ESP", GUI_COLOR_TEXT);
    }
    vesa_draw_hline((uint32_t)(x + TSKMGR_GUI_PX(6)),
                    (uint32_t)(y + TSKMGR_GUI_PX(64)),
                    (uint32_t)(width - TSKMGR_GUI_PX(12)),
                    taskmgr_gui_color(GUI_COLOR_BORDER_D));

    for (int row = 0; row < visible_rows && scroll_offset + row < total_threads; row++) {
        int absolute_row = scroll_offset + row;
        thread_t* thread = taskmgr_find_thread_by_row(absolute_row);
        int row_y = y + TSKMGR_GUI_THREADS_ROW_OFFSET +
                    row * TSKMGR_GUI_ROW_HEIGHT;
        uint32_t text_color = absolute_row == selected_row ? GUI_COLOR_TEXT_W : GUI_COLOR_TEXT;
        char name[24];
        if (!thread) continue;
        taskmgr_gui_copy_text(name, sizeof(name), thread->name);
        name[15] = '\0';
        if (absolute_row == selected_row) {
            taskmgr_gui_draw_surface(x + TSKMGR_GUI_PX(5), row_y,
                                     width - TSKMGR_GUI_PX(10),
                                     TSKMGR_GUI_ROW_HEIGHT,
                                     GUI_MODERN_COLOR_HOVER,
                                     GUI_MODERN_COLOR_ACCENT);
        }
        taskmgr_gui_draw_num(x + TSKMGR_GUI_PX(10),
                             row_y + TSKMGR_GUI_PX(4),
                             thread->id, text_color);
        gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(70)),
                      (uint32_t)(row_y + TSKMGR_GUI_PX(4)), name, text_color);
        gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(260)),
                      (uint32_t)(row_y + TSKMGR_GUI_PX(4)),
                      taskmgr_thread_state_name(thread->state), text_color);
        taskmgr_gui_draw_num(x + TSKMGR_GUI_PX(350),
                             row_y + TSKMGR_GUI_PX(4),
                             thread->wait_ticks, text_color);
        taskmgr_gui_draw_hex(x + TSKMGR_GUI_PX(420),
                             row_y + TSKMGR_GUI_PX(4),
                             thread->eip, text_color);
        if (width > TSKMGR_GUI_PX(620)) {
            taskmgr_gui_draw_hex(x + TSKMGR_GUI_PX(540),
                                 row_y + TSKMGR_GUI_PX(4),
                                 thread->esp, text_color);
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
        if (width > TSKMGR_GUI_PX(620)) {
            gui_draw_text((uint32_t)(x + 420), (uint32_t)(y + panel_height - 38),
                          "ESP", GUI_COLOR_TEXT);
            taskmgr_gui_draw_hex(x + 452, y + panel_height - 38,
                                 selected_thread->esp, GUI_COLOR_TEXT);
        }
    }
}

static void taskmgr_gui_draw_properties(void) {
    process_t* process = taskmgr_find_process_by_pid(prop_pid);
    int width = TSKMGR_GUI_PX(500);
    int height = TSKMGR_GUI_PX(300);
    int x = gui_x + (gui_width - width) / 2;
    int y = gui_y + (gui_height - height) / 2;

    if (width > gui_width - TSKMGR_GUI_PX(40)) {
        width = gui_width - TSKMGR_GUI_PX(40);
        x = gui_x + (gui_width - width) / 2;
    }
    if (height > gui_height - TSKMGR_GUI_PX(40)) {
        height = gui_height - TSKMGR_GUI_PX(40);
        y = gui_y + (gui_height - height) / 2;
    }

    if (!process) {
        show_properties = 0;
        return;
    }

    taskmgr_gui_draw_surface(x, y, width, height, GUI_MODERN_COLOR_WINDOW,
                             GUI_MODERN_COLOR_ACCENT);
    gui_draw_text((uint32_t)(x + TSKMGR_GUI_PX(18)),
                  (uint32_t)(y + TSKMGR_GUI_PX(14)),
                  "Propriedades do processo", GUI_MODERN_COLOR_TEXT);
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
    gui_draw_text((uint32_t)(x + 18), (uint32_t)(y + 132), "TCK:", GUI_COLOR_TEXT);
    taskmgr_gui_draw_num(x + 104, y + 132,
                         taskmgr_process_tick_usage(process), GUI_COLOR_TEXT);
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

static void taskmgr_gui_draw_window(void) {
    const char* update_text = "100ms | ZephyrOS";
    uint32_t update_width = 0;
    uint32_t update_height = 0;

    if (!taskmgr_hosted) {
        int control_gap = TSKMGR_GUI_PX(2);
        int control_y = gui_y + TSKMGR_GUI_PX(2);
        int close_x = gui_x + gui_width - TSKMGR_GUI_PX(2) -
                      TSKMGR_GUI_CONTROL_SIZE;
        int minimize_x = close_x - control_gap - TSKMGR_GUI_CONTROL_SIZE;
        int maximize_x = minimize_x - control_gap - TSKMGR_GUI_CONTROL_SIZE;

        gui_draw_scaled_window_frame((uint32_t)gui_x, (uint32_t)gui_y,
                                     (uint32_t)gui_width,
                                     (uint32_t)gui_height,
                                     "ZephyrOS Task Manager", 1);
        gui_draw_modern_button((uint32_t)minimize_x, (uint32_t)control_y,
                               TSKMGR_GUI_CONTROL_SIZE,
                               TSKMGR_GUI_CONTROL_SIZE, "_",
                               GUI_BUTTON_STATE_NORMAL);
        gui_draw_modern_button((uint32_t)maximize_x, (uint32_t)control_y,
                               TSKMGR_GUI_CONTROL_SIZE,
                               TSKMGR_GUI_CONTROL_SIZE,
                               gui_maximized ? "R" : "M",
                               GUI_BUTTON_STATE_NORMAL);
    }
    taskmgr_gui_draw_tabs();

    switch (selected_tab) {
        case 0: taskmgr_gui_draw_processes(); break;
        case 1: taskmgr_gui_draw_memory(); break;
        case 2: taskmgr_gui_draw_threads(); break;
        default: selected_tab = 0; taskmgr_gui_draw_processes(); break;
    }

    (void)gui_measure_scaled_text(update_text, &update_width, &update_height);
    gui_draw_text((uint32_t)(gui_x + TSKMGR_GUI_PX(14)),
                  (uint32_t)(gui_y + gui_height - TSKMGR_GUI_PX(48)),
                  "Tab Setas S:ord Enter:det Del:mata", GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(gui_x + gui_width - (int)update_width -
                             TSKMGR_GUI_PX(14)),
                  (uint32_t)(gui_y + gui_height - TSKMGR_GUI_PX(48)),
                  update_text, GUI_COLOR_TEXT);
    if (show_properties) taskmgr_gui_draw_properties();
}

static void taskmgr_gui_draw(void) {
    vesa_mode_t* mode = vesa_get_mode();

    if (taskmgr_hosted) {
        wm_request_hosted_redraw(WM_APP_TASKMGR);
        return;
    }
    if (!gui_open || !mode || !mode->initialized || !vesa_has_backbuffer()) return;
    taskmgr_clamp_window();
    mouse_invalidate_cursor();
    vesa_frame_begin();

    if (gui_minimized) {
        desktop_draw();
        vesa_frame_end();
        return;
    }

    {
        vesa_color_t background;
        background.raw = GUI_COLOR_BG;
        vesa_clear(background);
    }
    taskmgr_gui_draw_window();
    taskbar_draw();
    vesa_frame_end();
}

static void taskmgr_hosted_draw(int x, int y, int width, int height) {
    gui_x = x;
    gui_y = y;
    gui_width = width;
    gui_height = height;
    taskmgr_gui_draw_window();
}

static int taskmgr_hosted_mouse(mouse_event_t* event, int x, int y,
                                int width, int height) {
    gui_x = x;
    gui_y = y;
    gui_width = width;
    gui_height = height;
    return taskmgr_gui_handle_mouse(event);
}

static void taskmgr_hosted_close(void) {
    is_open = 0;
    gui_open = 0;
    gui_minimized = 0;
    gui_maximized = 0;
    gui_drag_active = 0;
    gui_redraw_pending = 0;
    show_properties = 0;
    taskmgr_hosted = 0;
}

static void taskmgr_gui_draw_drag_region(void) {
    vesa_mode_t* mode = vesa_get_mode();
    int left;
    int top;
    int right;
    int bottom;
    vesa_color_t background;

    if (!gui_open || gui_minimized || !gui_redraw_pending ||
        !mode || !mode->initialized || !vesa_has_backbuffer()) return;

    left = gui_drag_previous_x < gui_x ? gui_drag_previous_x : gui_x;
    top = gui_drag_previous_y < gui_y ? gui_drag_previous_y : gui_y;
    right = gui_drag_previous_x + gui_width;
    if (gui_x + gui_width > right) right = gui_x + gui_width;
    bottom = gui_drag_previous_y + gui_height;
    if (gui_y + gui_height > bottom) bottom = gui_y + gui_height;

    vesa_frame_begin_region((uint32_t)left, (uint32_t)top,
                            (uint32_t)(right - left),
                            (uint32_t)(bottom - top));
    mouse_invalidate_cursor();
    background.raw = GUI_COLOR_BG;
    vesa_fill_rect((uint32_t)left, (uint32_t)top,
                   (uint32_t)(right - left),
                   (uint32_t)(bottom - top), background);
    taskmgr_gui_draw_window();
    vesa_frame_end();
    gui_drag_previous_x = gui_x;
    gui_drag_previous_y = gui_y;
    gui_redraw_pending = 0;
}

int taskmgr_open_gui(void) {
    vesa_mode_t* mode = vesa_get_mode();
    int result;

    if (!recovery_is_enabled(RECOVERY_COMPONENT_TASKMANAGER)) {
        LOG_WARN("TSKMGR", "GUI bloqueada pelo recovery");
        return ERR_UNAVAILABLE;
    }
    if (gui_open) {
        if (taskmgr_hosted) return wm_register_hosted_app(&taskmgr_hosted_app);
        taskmgr_gui_restore();
        return OK;
    }
    if (desktop_get_mode() != DESKTOP_MODE_CLASSIC || !mode ||
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
    gui_drag_previous_x = gui_x;
    gui_drag_previous_y = gui_y;
    gui_redraw_pending = 0;
    selected_tab = 0;
    selected_row = 0;
    scroll_offset = 0;
    show_properties = 0;
    taskmgr_gui_reset_history();
    taskmgr_update_cpu_metrics();
    taskmgr_gui_sample_history();
    gui_last_tick = timer_get_ticks();
    gui_last_metrics_tick = gui_last_tick;
    taskmgr_hosted = 1;
    wm_set_active(1);
    result = wm_register_hosted_app(&taskmgr_hosted_app);
    if (result != OK) {
        taskmgr_hosted_close();
        LOG_WARN("TSKMGR", "Workspace nao comporta o Task Manager grafico");
        return result;
    }
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
    if (taskmgr_hosted) {
        wm_register_hosted_app(&taskmgr_hosted_app);
        return;
    }
    gui_minimized = 0;
    desktop_set_active(0);
    taskmgr_gui_draw();
}

static void taskmgr_gui_minimize(void) {
    if (!gui_open) return;
    gui_minimized = 1;
    gui_drag_active = 0;
    gui_redraw_pending = 0;
    desktop_draw();
}

void taskmgr_gui_update(void) {
    uint32_t now;
    int redraw = 0;

    if (!gui_open || gui_minimized) return;
    now = timer_get_ticks();
    if (now - gui_last_tick < TSKMGR_UI_FRAME_TICKS) return;
    gui_last_tick = now;

    if (now - gui_last_metrics_tick >= TSKMGR_METRICS_TICKS) {
        gui_last_metrics_tick = now;
        taskmgr_update_cpu_metrics();
        taskmgr_gui_sample_history();
        redraw = 1;
    }
    if (gui_redraw_pending && !redraw) {
        taskmgr_gui_draw_drag_region();
        return;
    }
    if (redraw) {
        gui_redraw_pending = 0;
        gui_drag_previous_x = gui_x;
        gui_drag_previous_y = gui_y;
        taskmgr_gui_draw();
    }
}

static void taskmgr_gui_handle_taskbar_action(int result) {
    int prepare_result;

    switch (result) {
        case 2: taskmgr_close(); shell_handle_app_request(IPC_APP_OPEN_SHELL); break;
        case 3: taskmgr_close(); shell_handle_app_request(IPC_APP_OPEN_EXPLORER); break;
        case 4: taskmgr_gui_restore(); break;
        case 5:
            taskmgr_close();
            power_reboot();
            break;
        case 6:
            prepare_result = power_shutdown_prepare();
            if (prepare_result != OK) {
                video_print("Desligamento recusado: sync falhou (codigo ",
                            0x0C);
                shell_command_print_num((uint32_t)prepare_result);
                video_print(").\n", 0x0C);
                break;
            }
            taskmgr_close();
            power_shutdown();
            break;
        case 7: taskmgr_close(); shell_handle_app_request(IPC_APP_OPEN_DESKTOP); break;
        case 8: taskmgr_close(); shell_handle_app_request(IPC_APP_OPEN_SETTINGS); break;
        case TB_ACTION_UPDATER:
            taskmgr_close();
            shell_handle_app_request(IPC_APP_OPEN_UPDATER);
            break;
        case TB_ACTION_APPSTORE:
            taskmgr_close();
            shell_handle_app_request(IPC_APP_OPEN_APP_STORE);
            break;
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
    if (!process) return;
    if (!process_is_user(process)) {
        LOG_WARN("SHELL", "Task Manager protegeu processo nativo");
        return;
    }
    if (process_signal_send(process->pid, APP_SIGNAL_KILL) != OK) {
        LOG_WARN("SHELL", "SIGKILL recusado pelo Task Manager");
        return;
    }
    if (selected_row > 0) selected_row--;
}

void taskmgr_gui_handle_key(uint8_t scancode) {
    int config_result;
    int taskbar_result;
    int count;
    int process_indexes[MAX_PROCESSES];

    if (!gui_open) return;
    if (!taskmgr_hosted) {
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
    }
    if (show_properties) {
        if (scancode == 0x01 || scancode == 0x1C) {
            show_properties = 0;
            taskmgr_gui_draw();
        }
        return;
    }
    if (scancode == 0x01) {
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

static int taskmgr_gui_scroll_selected(int delta) {
    int indexes[MAX_PROCESSES];
    int count;
    int visible_rows;
    int previous = selected_row;

    if (selected_tab == 0) {
        count = taskmgr_collect_processes(indexes);
        visible_rows = taskmgr_gui_process_visible_rows();
    } else if (selected_tab == 2) {
        count = (int)thread_get_count();
        visible_rows = taskmgr_gui_visible_rows();
    } else {
        return 0;
    }
    if (count <= 0 || delta == 0) return 0;

    while (delta > 0 && selected_row > 0) {
        selected_row--;
        delta--;
    }
    while (delta < 0 && selected_row < count - 1) {
        selected_row++;
        delta++;
    }
    if (selected_row == previous) return 0;

    if (selected_row < scroll_offset) {
        scroll_offset = selected_row;
    } else if (selected_row >= scroll_offset + visible_rows) {
        scroll_offset = selected_row - visible_rows + 1;
    }
    if (scroll_offset < 0) scroll_offset = 0;
    if (scroll_offset > count - visible_rows) {
        scroll_offset = count > visible_rows ? count - visible_rows : 0;
    }
    if (selected_tab == 0 && taskmgr_find_process_by_row(selected_row)) {
        prop_pid = taskmgr_find_process_by_row(selected_row)->pid;
    }
    return 1;
}

static int taskmgr_gui_handle_wheel(mouse_event_t* event) {
    int process_list_width;

    if (!event || event->wheel == 0 || show_properties) return 0;
    process_list_width = gui_width - TSKMGR_GUI_MARGIN * 2;
    if (taskmgr_gui_has_side_details()) {
        process_list_width -= TSKMGR_GUI_DETAIL_WIDTH + TSKMGR_GUI_DETAIL_GAP;
    }
    if (selected_tab == 0 && taskmgr_gui_hit(event->x, event->y,
            gui_x + TSKMGR_GUI_MARGIN,
            taskmgr_gui_process_list_y() + TSKMGR_GUI_LIST_HEADER_HEIGHT,
            process_list_width, taskmgr_gui_process_visible_rows() *
            TSKMGR_GUI_ROW_HEIGHT)) {
        return taskmgr_gui_scroll_selected(event->wheel);
    }
    if (selected_tab == 2 && taskmgr_gui_hit(event->x, event->y,
            gui_x + TSKMGR_GUI_MARGIN,
            gui_y + TSKMGR_GUI_CONTENT_TOP +
            TSKMGR_GUI_THREADS_ROW_OFFSET,
            gui_width - TSKMGR_GUI_MARGIN * 2,
            taskmgr_gui_visible_rows() * TSKMGR_GUI_ROW_HEIGHT)) {
        return taskmgr_gui_scroll_selected(event->wheel);
    }
    return 0;
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
    if (event->event == MOUSE_EVENT_WHEEL) {
        return taskmgr_gui_handle_wheel(event);
    }
    if (event->event == MOUSE_EVENT_MOVE && gui_drag_active) {
        gui_x = event->x - gui_drag_offset_x;
        gui_y = event->y - gui_drag_offset_y;
        taskmgr_clamp_window();
        gui_redraw_pending = 1;
        return 1;
    }
    if (event->event == MOUSE_EVENT_RELEASE) {
        gui_drag_active = 0;
        return 1;
    }
    if (event->event != MOUSE_EVENT_PRESS ||
        !(event->changed & MOUSE_BTN_LEFT)) return 1;
    if (!taskmgr_gui_hit(event->x, event->y, gui_x, gui_y, gui_width, gui_height)) return 1;

    if (!taskmgr_hosted) {
        int control_y = gui_y + TSKMGR_GUI_PX(2);
        int control_gap = TSKMGR_GUI_PX(2);

        close_x = gui_x + gui_width - TSKMGR_GUI_PX(2) -
                  TSKMGR_GUI_CONTROL_SIZE;
        minimize_x = close_x - control_gap - TSKMGR_GUI_CONTROL_SIZE;
        maximize_x = minimize_x - control_gap - TSKMGR_GUI_CONTROL_SIZE;
        if (taskmgr_gui_hit(event->x, event->y, close_x, control_y,
                            TSKMGR_GUI_CONTROL_SIZE,
                            TSKMGR_GUI_CONTROL_SIZE)) {
            taskmgr_close();
            return 1;
        }
        if (taskmgr_gui_hit(event->x, event->y, maximize_x, control_y,
                            TSKMGR_GUI_CONTROL_SIZE,
                            TSKMGR_GUI_CONTROL_SIZE)) {
            gui_minimized = 0;
            if (gui_maximized) {
                gui_maximized = 0;
                gui_x = gui_restore_x;
                gui_y = gui_restore_y;
                gui_width = gui_restore_width;
                gui_height = gui_restore_height;
            } else {
                tb_rect_t work_area;
                gui_maximized = 1;
                gui_restore_x = gui_x;
                gui_restore_y = gui_y;
                gui_restore_width = gui_width;
                gui_restore_height = gui_height;
                if (!taskmgr_get_work_area(&work_area)) return 1;
                gui_x = work_area.x;
                gui_y = work_area.y;
                gui_width = work_area.width;
                gui_height = work_area.height;
            }
            taskmgr_gui_draw();
            return 1;
        }
        if (taskmgr_gui_hit(event->x, event->y, minimize_x, control_y,
                            TSKMGR_GUI_CONTROL_SIZE,
                            TSKMGR_GUI_CONTROL_SIZE)) {
            taskmgr_gui_minimize();
            return 1;
        }
        if (taskmgr_gui_hit(
                event->x, event->y, gui_x + TSKMGR_GUI_PX(2),
                gui_y + TSKMGR_GUI_PX(2),
                gui_width - 3 * (TSKMGR_GUI_CONTROL_SIZE + control_gap) -
                TSKMGR_GUI_PX(4), TSKMGR_GUI_TITLE_HEIGHT)) {
            gui_drag_active = 1;
            gui_drag_offset_x = event->x - gui_x;
            gui_drag_offset_y = event->y - gui_y;
            gui_drag_previous_x = gui_x;
            gui_drag_previous_y = gui_y;
            return 1;
        }
    }

    if (taskmgr_gui_hit(event->x, event->y, gui_x + TSKMGR_GUI_MARGIN,
            gui_y + TSKMGR_GUI_TABS_TOP,
            3 * ((int)display_scale_px(112) + (int)display_scale_px(4)),
            TSKMGR_GUI_TAB_HEIGHT)) {
        selected_tab = (event->x - (gui_x + TSKMGR_GUI_MARGIN)) /
                       ((int)display_scale_px(112) +
                        (int)display_scale_px(4));
        if (selected_tab > 2) selected_tab = 2;
        selected_row = 0;
        scroll_offset = 0;
        taskmgr_gui_draw();
        return 1;
    }
    process_list_width = gui_width - TSKMGR_GUI_MARGIN * 2;
    if (taskmgr_gui_has_side_details()) {
        process_list_width -= TSKMGR_GUI_DETAIL_WIDTH + TSKMGR_GUI_DETAIL_GAP;
    }
    if (selected_tab == 0 && taskmgr_gui_hit(event->x, event->y,
            gui_x + TSKMGR_GUI_MARGIN,
            taskmgr_gui_process_list_y() + TSKMGR_GUI_LIST_HEADER_HEIGHT,
            process_list_width, taskmgr_gui_process_visible_rows() * TSKMGR_GUI_ROW_HEIGHT)) {
        int row = (event->y - taskmgr_gui_process_list_y() -
                   TSKMGR_GUI_LIST_HEADER_HEIGHT) /
                  TSKMGR_GUI_ROW_HEIGHT;
        if (row < 0) return 1;
        selected_row = scroll_offset + row;
        if (taskmgr_find_process_by_row(selected_row)) prop_pid = taskmgr_find_process_by_row(selected_row)->pid;
        taskmgr_gui_draw();
        return 1;
    }
    if (selected_tab == 2 && taskmgr_gui_hit(event->x, event->y,
            gui_x + TSKMGR_GUI_MARGIN,
            gui_y + TSKMGR_GUI_CONTENT_TOP +
            TSKMGR_GUI_THREADS_ROW_OFFSET,
            gui_width - TSKMGR_GUI_MARGIN * 2,
            taskmgr_gui_visible_rows() * TSKMGR_GUI_ROW_HEIGHT)) {
        int row = (event->y - (gui_y + TSKMGR_GUI_CONTENT_TOP +
                               TSKMGR_GUI_THREADS_ROW_OFFSET)) /
                  TSKMGR_GUI_ROW_HEIGHT;
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
            uint32_t elapsed = current_tick - last_tick;
            if (current_tick - last_tick >= TSKMGR_METRICS_TICKS) {
                taskmgr_refresh();
                last_tick = current_tick;
            } else {
                wait_reason_t reason = WAIT_REASON_NONE;
                uint32_t remaining = TSKMGR_METRICS_TICKS - elapsed;

                if (ipc_wait(remaining, &reason) != OK) {
                    LOG_WARN("TSKMGR", "Falha ao aguardar entrada por IPC");
                    process_yield();
                }
            }
        }
    }
}
