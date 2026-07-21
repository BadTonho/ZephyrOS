#include "ui/desktop.h"
#include "core/video.h"
#include "core/timer.h"
#include "core/errors.h"
#include "core/log.h"
#include "drivers/vesa.h"
#include "drivers/font.h"
#include "drivers/mouse.h"
#include "ui/gui.h"
#include "ui/taskbar.h"
#include "ui/icons.h"

#define DESKTOP_MODERN_MARGIN 24
#define DESKTOP_MODERN_TOP_MARGIN 24
#define DESKTOP_MODERN_CARD_WIDTH 112
#define DESKTOP_MODERN_CARD_HEIGHT 96
#define DESKTOP_MODERN_GAP_X 16
#define DESKTOP_MODERN_GAP_Y 16
#define DESKTOP_MODERN_TASKBAR_HEIGHT 24
#define DESKTOP_MODERN_TASKBAR_GAP 8
#define DESKTOP_MODERN_MAX_COLUMNS 5
#define DESKTOP_MODERN_SYMBOL_SCALE 2
#define DESKTOP_DOUBLE_CLICK_TICKS 25

static desktop_icon_t desktop_icons[DESKTOP_MAX_ICONS];
static int icon_count = 0;
static int selected_icon = 0;
static int desktop_active = 0;
static desktop_mode_t desktop_mode = DESKTOP_MODE_CLASSIC;
static int modern_columns = 1;
static int last_click_icon = -1;
static uint32_t last_click_ticks = 0;

static icon_entry_t* desktop_get_icon_entry(desktop_app_type_t type) {
    switch (type) {
        case DESKTOP_APP_SHELL:
            return icons_get_desktop(ICON_DESKTOP_SHELL);
        case DESKTOP_APP_EXPLORER:
            return icons_get_desktop(ICON_DESKTOP_EXPLORER);
        case DESKTOP_APP_TASKMGR:
            return icons_get_desktop(ICON_DESKTOP_TASKMGR);
        default:
            LOG_ERROR("DESKTOP", "Tipo de icone invalido");
            return 0;
    }
}

static int desktop_text_length(const char* text) {
    int length = 0;
    if (!text) return 0;

    while (text[length]) length++;
    return length;
}

static uint32_t desktop_modern_background(void) {
    /* O fundo reutiliza o azul escuro da identidade visual atual. */
    return vesa_rgb(0, 64, 96);
}

static int desktop_modern_get_columns(const vesa_mode_t* mode) {
    int available_width;
    int columns;

    if (!mode || !mode->initialized) return 1;

    available_width = (int)mode->width - (DESKTOP_MODERN_MARGIN * 2);
    columns = (available_width + DESKTOP_MODERN_GAP_X) /
              (DESKTOP_MODERN_CARD_WIDTH + DESKTOP_MODERN_GAP_X);

    if (columns < 1) columns = 1;
    if (columns > DESKTOP_MODERN_MAX_COLUMNS) {
        columns = DESKTOP_MODERN_MAX_COLUMNS;
    }
    if (icon_count > 0 && columns > icon_count) columns = icon_count;

    return columns;
}

static void desktop_layout_modern(void) {
    vesa_mode_t* mode = vesa_get_mode();
    tb_config_t* taskbar_config = taskbar_get_config();
    int start_y = DESKTOP_MODERN_TOP_MARGIN;

    if (!mode || !mode->initialized) {
        LOG_ERROR("DESKTOP", "Modo VESA indisponivel para layout moderno");
        return;
    }

    modern_columns = desktop_modern_get_columns(mode);

    if (taskbar_config && taskbar_config->position == TB_POS_TOP) {
        start_y += DESKTOP_MODERN_TASKBAR_HEIGHT + DESKTOP_MODERN_TASKBAR_GAP;
    }

    for (int i = 0; i < icon_count; i++) {
        int col = i % modern_columns;
        int row = i / modern_columns;

        desktop_icons[i].modern_x = DESKTOP_MODERN_MARGIN +
            col * (DESKTOP_MODERN_CARD_WIDTH + DESKTOP_MODERN_GAP_X);
        desktop_icons[i].modern_y = start_y +
            row * (DESKTOP_MODERN_CARD_HEIGHT + DESKTOP_MODERN_GAP_Y);
        desktop_icons[i].modern_width = DESKTOP_MODERN_CARD_WIDTH;
        desktop_icons[i].modern_height = DESKTOP_MODERN_CARD_HEIGHT;
    }
}

static void draw_single_icon_classic(desktop_icon_t* icon) {
    icon_entry_t* entry = desktop_get_icon_entry(icon->type);
    if (!entry) {
        LOG_ERROR("DESKTOP", "Registro do icone nao encontrado");
        return;
    }

    uint8_t bg = icon->selected ? entry->color_selected : DESKTOP_BG_COLOR;
    uint8_t fg = icon->selected ? 0x17 : entry->color;

    video_fill_rect(icon->x, icon->y, DESKTOP_ICON_WIDTH, 5, ' ', bg);

    video_put_char_at(0xC9, fg, icon->x, icon->y);
    video_put_char_at(0xBB, fg, icon->x + DESKTOP_ICON_WIDTH - 1, icon->y);
    video_put_char_at(0xC8, fg, icon->x, icon->y + 3);
    video_put_char_at(0xBC, fg, icon->x + DESKTOP_ICON_WIDTH - 1, icon->y + 3);

    for (int i = 1; i < DESKTOP_ICON_WIDTH - 1; i++) {
        video_put_char_at(0xCD, fg, icon->x + i, icon->y);
        video_put_char_at(0xCD, fg, icon->x + i, icon->y + 3);
    }
    for (int i = 1; i < 3; i++) {
        video_put_char_at(0xBA, fg, icon->x, icon->y + i);
        video_put_char_at(0xBA, fg, icon->x + DESKTOP_ICON_WIDTH - 1,
                          icon->y + i);
    }

    video_put_char_at(entry->ch, fg, icon->x + 4, icon->y + 1);
    video_put_char_at(entry->ch, fg, icon->x + 4, icon->y + 2);

    int name_len = desktop_text_length(icon->name);
    int text_x = icon->x + (DESKTOP_ICON_WIDTH - name_len) / 2;
    video_print_at(text_x, icon->y + 4, icon->name, fg);
}

static void draw_single_icon_modern(desktop_icon_t* icon) {
    icon_entry_t* entry = desktop_get_icon_entry(icon->type);
    if (!entry) {
        LOG_ERROR("DESKTOP", "Registro do icone moderno nao encontrado");
        return;
    }

    uint32_t background = icon->selected ? GUI_COLOR_TITLE_BG : GUI_COLOR_BG;
    uint32_t foreground = icon->selected ? GUI_COLOR_TEXT_W : GUI_COLOR_TEXT;
    int symbol_width = FONT_WIDTH * DESKTOP_MODERN_SYMBOL_SCALE;
    int symbol_x = icon->modern_x + (icon->modern_width - symbol_width) / 2;
    int symbol_y = icon->modern_y + 14;
    int label_width = desktop_text_length(icon->name) * FONT_WIDTH;
    int label_x = icon->modern_x + (icon->modern_width - label_width) / 2;
    int label_y = icon->modern_y + icon->modern_height - FONT_HEIGHT - 10;

    gui_draw_panel(icon->modern_x, icon->modern_y,
                   icon->modern_width, icon->modern_height,
                   background, icon->selected);

    vesa_color_t symbol_color;
    symbol_color.raw = foreground;
    vesa_draw_char(symbol_x, symbol_y, entry->ch, symbol_color,
                   DESKTOP_MODERN_SYMBOL_SCALE);

    if (label_x < icon->modern_x + 4) label_x = icon->modern_x + 4;
    gui_draw_text(label_x, label_y, icon->name, foreground);
}

static void desktop_draw_icons_classic(void) {
    for (int i = 0; i < icon_count; i++) {
        draw_single_icon_classic(&desktop_icons[i]);
    }
}

static void desktop_draw_icons_modern(void) {
    for (int i = 0; i < icon_count; i++) {
        draw_single_icon_modern(&desktop_icons[i]);
    }
}

static void desktop_draw_classic(void) {
    vesa_mode_t* mode = vesa_get_mode();

    if (mode && mode->initialized) mouse_invalidate_cursor();

    video_fill_rect(0, 0, SCREEN_COLS, SCREEN_ROWS - 1, ' ', DESKTOP_BG_COLOR);
    video_fill_rect(0, 0, SCREEN_COLS, 1, ' ', 0x1F);
    video_print_at((SCREEN_COLS - 20) / 2, 0, " ZephyrOS Desktop ", 0x1F);
    desktop_draw_icons_classic();

    if (mode && mode->initialized) vesa_flip();
}

static void desktop_draw_modern(void) {
    vesa_mode_t* mode = vesa_get_mode();
    vesa_color_t background;

    if (!mode || !mode->initialized) {
        LOG_ERROR("DESKTOP", "VESA indisponivel para Desktop moderno");
        return;
    }

    mouse_invalidate_cursor();
    background.raw = desktop_modern_background();
    vesa_clear(background);
    desktop_layout_modern();
    desktop_draw_icons_modern();

    /* A taskbar faz parte da mesma cena e usa o mesmo backbuffer. */
    taskbar_draw();
}

static int desktop_find_modern_icon(int px, int py) {
    for (int i = 0; i < icon_count; i++) {
        desktop_icon_t* icon = &desktop_icons[i];

        if (px >= icon->modern_x &&
            px < icon->modern_x + icon->modern_width &&
            py >= icon->modern_y &&
            py < icon->modern_y + icon->modern_height) {
            return i;
        }
    }

    return -1;
}

void desktop_init(void) {
    LOG_INFO("DESKTOP", "Inicializando Desktop");

    icon_count = 0;
    selected_icon = 0;
    desktop_active = 0;
    last_click_icon = -1;
    last_click_ticks = 0;

    vesa_mode_t* mode = vesa_get_mode();
    desktop_mode = (mode && mode->initialized) ?
        DESKTOP_MODE_MODERN : DESKTOP_MODE_CLASSIC;

    desktop_add_icon("Shell", DESKTOP_APP_SHELL);
    desktop_add_icon("Explorer", DESKTOP_APP_EXPLORER);
    desktop_add_icon("TaskMgr", DESKTOP_APP_TASKMGR);

    if (desktop_mode == DESKTOP_MODE_CLASSIC) {
        LOG_WARN("DESKTOP", "VESA indisponivel, usando modo classico");
    }

    LOG_INFO("DESKTOP", "Desktop inicializado com sucesso");
}

void desktop_draw(void) {
    if (desktop_mode == DESKTOP_MODE_MODERN) {
        vesa_mode_t* mode = vesa_get_mode();
        if (mode && mode->initialized) {
            desktop_draw_modern();
            return;
        }

        LOG_WARN("DESKTOP", "Modo moderno sem VESA, alternando para classico");
        desktop_mode = DESKTOP_MODE_CLASSIC;
    }

    desktop_draw_classic();
}

void desktop_draw_icons(void) {
    if (desktop_mode == DESKTOP_MODE_MODERN) {
        vesa_mode_t* mode = vesa_get_mode();
        if (!mode || !mode->initialized) {
            LOG_ERROR("DESKTOP", "Nao foi possivel desenhar icones modernos");
            return;
        }

        mouse_invalidate_cursor();
        desktop_layout_modern();
        desktop_draw_icons_modern();
        vesa_flip();
        return;
    }

    desktop_draw_icons_classic();

    vesa_mode_t* mode = vesa_get_mode();
    if (mode && mode->initialized) vesa_flip();
}

void desktop_add_icon(const char* name, desktop_app_type_t type) {
    if (!name) {
        LOG_ERROR("DESKTOP", "Nome de icone nulo");
        return;
    }
    if (icon_count >= DESKTOP_MAX_ICONS) {
        LOG_ERROR("DESKTOP", "Limite de icones atingido");
        return;
    }

    int col = icon_count % 5;
    int row = icon_count / 5;

    desktop_icons[icon_count].name = name;
    desktop_icons[icon_count].type = type;
    desktop_icons[icon_count].x = DESKTOP_START_X + col * DESKTOP_ICON_SPACING_X;
    desktop_icons[icon_count].y = DESKTOP_START_Y + row * DESKTOP_ICON_SPACING_Y;
    desktop_icons[icon_count].modern_x = 0;
    desktop_icons[icon_count].modern_y = 0;
    desktop_icons[icon_count].modern_width = DESKTOP_MODERN_CARD_WIDTH;
    desktop_icons[icon_count].modern_height = DESKTOP_MODERN_CARD_HEIGHT;
    desktop_icons[icon_count].selected = (icon_count == selected_icon) ? 1 : 0;

    icon_count++;
}

void desktop_update_selection(void) {
    for (int i = 0; i < icon_count; i++) {
        desktop_icons[i].selected = (i == selected_icon) ? 1 : 0;
    }

    desktop_draw_icons();
}

int desktop_handle_key(uint8_t scancode) {
    int columns;

    if (!desktop_active) return 0;

    if (taskbar_handle_config_key(scancode)) {
        return 1;
    }

    if (scancode & 0x80) return 0;

    columns = (desktop_mode == DESKTOP_MODE_MODERN) ? modern_columns : 5;
    if (columns < 1) columns = 1;

    if (selected_icon < 0 && scancode != 0x1C) selected_icon = 0;

    if (scancode == 0x48) {
        if (selected_icon >= columns) selected_icon -= columns;
        desktop_update_selection();
        return 0;
    }

    if (scancode == 0x50) {
        if (selected_icon + columns < icon_count) selected_icon += columns;
        desktop_update_selection();
        return 0;
    }

    if (scancode == 0x4B) {
        if (selected_icon > 0) selected_icon--;
        desktop_update_selection();
        return 0;
    }

    if (scancode == 0x4D) {
        if (selected_icon < icon_count - 1) selected_icon++;
        desktop_update_selection();
        return 0;
    }

    if (scancode == 0x1C) {
        return desktop_get_selected_app();
    }

    return 0;
}

int desktop_get_selected_app(void) {
    if (selected_icon < 0 || selected_icon >= icon_count) return 0;
    return desktop_icons[selected_icon].type;
}

void desktop_set_active(int active) {
    desktop_active = active;
    if (!active) {
        last_click_icon = -1;
        last_click_ticks = 0;
    }
}

int desktop_is_active(void) {
    return desktop_active;
}

int desktop_set_mode(desktop_mode_t mode) {
    vesa_mode_t* vesa_mode = vesa_get_mode();

    if (mode != DESKTOP_MODE_CLASSIC && mode != DESKTOP_MODE_MODERN) {
        LOG_ERROR("DESKTOP", "Modo de Desktop invalido");
        return ERR_INVALID;
    }

    if (mode == DESKTOP_MODE_MODERN &&
        (!vesa_mode || !vesa_mode->initialized)) {
        LOG_ERROR("DESKTOP", "Modo moderno requer VESA");
        desktop_mode = DESKTOP_MODE_CLASSIC;
        return ERR_NOT_FOUND;
    }

    desktop_mode = mode;
    if (selected_icon < 0) selected_icon = 0;
    last_click_icon = -1;
    last_click_ticks = 0;

    if (desktop_active) {
        desktop_draw();
    }

    LOG_INFO("DESKTOP", mode == DESKTOP_MODE_MODERN ?
             "Modo moderno ativado" : "Modo classico ativado");
    return OK;
}

desktop_mode_t desktop_get_mode(void) {
    return desktop_mode;
}

int desktop_handle_mouse(mouse_event_t* event) {
    uint32_t now;
    int hit_index;

    if (!event) {
        LOG_ERROR("DESKTOP", "Evento de mouse nulo");
        return 0;
    }
    if (!desktop_active || desktop_mode != DESKTOP_MODE_MODERN) return 0;
    if (event->event != MOUSE_EVENT_PRESS ||
        !(event->changed & MOUSE_BTN_LEFT)) return 0;

    hit_index = desktop_find_modern_icon(event->x, event->y);
    if (hit_index < 0) {
        selected_icon = -1;
        last_click_icon = -1;
        desktop_update_selection();
        return 0;
    }

    now = timer_get_ticks();
    if (selected_icon == hit_index && last_click_icon == hit_index &&
        now - last_click_ticks <= DESKTOP_DOUBLE_CLICK_TICKS) {
        selected_icon = hit_index;
        last_click_icon = -1;
        last_click_ticks = 0;
        desktop_update_selection();
        return desktop_icons[hit_index].type;
    }

    selected_icon = hit_index;
    last_click_icon = hit_index;
    last_click_ticks = now;
    desktop_update_selection();
    return 0;
}

int desktop_handle_click(int px, int py) {
    if (!desktop_active || desktop_mode == DESKTOP_MODE_MODERN) return 0;

    /* Converte coordenadas de pixel para coordenadas de texto (fonte 8x16). */
    int col = px / 8;
    int row = py / 16;

    for (int i = 0; i < icon_count; i++) {
        int ix = desktop_icons[i].x;
        int iy = desktop_icons[i].y;

        if (col >= ix && col < ix + DESKTOP_ICON_WIDTH &&
            row >= iy && row < iy + 5) {
            selected_icon = i;
            desktop_update_selection();
            return desktop_icons[i].type;
        }
    }

    return 0;
}
