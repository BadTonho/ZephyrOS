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
#include "ui/display.h"

#define DESKTOP_CLASSIC_MARGIN 24
#define DESKTOP_CLASSIC_TOP_MARGIN 24
#define DESKTOP_CLASSIC_CARD_WIDTH 112
#define DESKTOP_CLASSIC_CARD_HEIGHT 96
#define DESKTOP_CLASSIC_GAP_X 16
#define DESKTOP_CLASSIC_GAP_Y 16
#define DESKTOP_CLASSIC_MAX_COLUMNS 5
#define DESKTOP_CLASSIC_SYMBOL_SCALE 2
#define DESKTOP_DOUBLE_CLICK_TICKS 25
#define DESKTOP_DRAG_THRESHOLD 4
#define DESKTOP_CLASSIC_MAX_SLOTS (DESKTOP_MAX_ICONS * 2)

typedef struct {
    int x;
    int y;
    int grid_index;
    int available;
} desktop_classic_slot_t;

static desktop_icon_t desktop_icons[DESKTOP_MAX_ICONS];
static desktop_classic_slot_t desktop_slots[DESKTOP_CLASSIC_MAX_SLOTS];
static int desktop_icon_slots[DESKTOP_MAX_ICONS];
static int icon_count = 0;
static int selected_icon = 0;
static int desktop_active = 0;
static desktop_mode_t desktop_mode = DESKTOP_MODE_SIMPLE;
static int classic_columns = 1;
static int classic_slot_count = 0;
static int last_click_icon = -1;
static uint32_t last_click_ticks = 0;
static int desktop_drag_icon = -1;
static int desktop_drag_start_x = 0;
static int desktop_drag_start_y = 0;
static int desktop_drag_active = 0;
static int desktop_drag_preview_slot = -1;

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

static int desktop_draw_classic_bitmap(desktop_app_type_t type, int x, int y,
                                      uint32_t size) {
    icon_desktop_id_t id;

    switch (type) {
        case DESKTOP_APP_SHELL: id = ICON_DESKTOP_SHELL; break;
        case DESKTOP_APP_EXPLORER: id = ICON_DESKTOP_EXPLORER; break;
        case DESKTOP_APP_TASKMGR: id = ICON_DESKTOP_TASKMGR; break;
        default:
            LOG_ERROR("DESKTOP", "Tipo de bitmap Classic invalido");
            return ERR_NOT_FOUND;
    }
    return icons_draw_desktop_bitmap_resized(id, x, y, size);
}

static int desktop_text_length(const char* text) {
    int length = 0;
    if (!text) return 0;

    while (text[length]) length++;
    return length;
}

static uint32_t desktop_classic_background(void) {
    /* O fundo reutiliza o azul escuro da identidade visual atual. */
    return vesa_rgb(0, 64, 96);
}

static int desktop_classic_get_columns(const tb_rect_t* work_area) {
    int available_width;
    int columns;
    int margin = (int)display_scale_px(DESKTOP_CLASSIC_MARGIN);
    int gap_x = (int)display_scale_px(DESKTOP_CLASSIC_GAP_X);
    int card_width = (int)display_scale_px(DESKTOP_CLASSIC_CARD_WIDTH);

    if (!work_area) return 1;

    available_width = work_area->width - (margin * 2);
    columns = (available_width + gap_x) / (card_width + gap_x);

    if (columns < 1) columns = 1;
    if (columns > DESKTOP_CLASSIC_MAX_COLUMNS) {
        columns = DESKTOP_CLASSIC_MAX_COLUMNS;
    }

    return columns;
}

static int desktop_rects_intersect(int ax, int ay, int aw, int ah,
                                   const tb_rect_t* b) {
    if (!b) return 0;
    return ax < b->x + b->width && ax + aw > b->x &&
           ay < b->y + b->height && ay + ah > b->y;
}

static int desktop_build_classic_slots(const tb_rect_t* work_area) {
    tb_config_t* taskbar_config = taskbar_get_config();
    tb_rect_t reserved_area;
    int has_reserved_area = 0;
    int start_x;
    int start_y;
    int limit_x;
    int limit_y;
    int row = 0;
    int available_slots = 0;
    int margin = (int)display_scale_px(DESKTOP_CLASSIC_MARGIN);
    int top_margin = (int)display_scale_px(DESKTOP_CLASSIC_TOP_MARGIN);
    int card_width = (int)display_scale_px(DESKTOP_CLASSIC_CARD_WIDTH);
    int card_height = (int)display_scale_px(DESKTOP_CLASSIC_CARD_HEIGHT);
    int gap_x = (int)display_scale_px(DESKTOP_CLASSIC_GAP_X);
    int gap_y = (int)display_scale_px(DESKTOP_CLASSIC_GAP_Y);

    if (!work_area) {
        LOG_ERROR("DESKTOP", "Area de trabalho nula para icones Classic");
        return ERR_NULL;
    }
    if (taskbar_config && taskbar_config->position == TB_POS_CUSTOM &&
        taskbar_get_bounds(&reserved_area)) {
        has_reserved_area = 1;
    }

    start_x = work_area->x + margin;
    start_y = work_area->y + top_margin;
    limit_x = work_area->x + work_area->width - margin;
    limit_y = work_area->y + work_area->height - margin;
    classic_slot_count = 0;

    for (int y = start_y;
         y + card_height <= limit_y &&
         classic_slot_count < DESKTOP_CLASSIC_MAX_SLOTS;
         y += card_height + gap_y, row++) {
        for (int col = 0; col < classic_columns &&
             classic_slot_count < DESKTOP_CLASSIC_MAX_SLOTS; col++) {
            int x = start_x + col * (card_width + gap_x);

            if (x + card_width > limit_x) continue;
            desktop_slots[classic_slot_count].x = x;
            desktop_slots[classic_slot_count].y = y;
            desktop_slots[classic_slot_count].grid_index =
                row * classic_columns + col;
            desktop_slots[classic_slot_count].available =
                !has_reserved_area || !desktop_rects_intersect(
                    x, y, card_width, card_height, &reserved_area);
            if (desktop_slots[classic_slot_count].available) available_slots++;
            classic_slot_count++;
        }
    }

    if (available_slots < icon_count) {
        LOG_ERROR("DESKTOP", "Area insuficiente para os icones Classic");
        return ERR_OVERFLOW;
    }
    return OK;
}

static int desktop_find_slot_by_grid(int grid_index) {
    for (int i = 0; i < classic_slot_count; i++) {
        if (desktop_slots[i].grid_index == grid_index) return i;
    }
    return -1;
}

static int desktop_find_free_slot(const uint8_t* used, int start_slot) {
    if (!used || classic_slot_count <= 0) return -1;
    if (start_slot < 0 || start_slot >= classic_slot_count) start_slot = 0;

    for (int offset = 0; offset < classic_slot_count; offset++) {
        int slot = (start_slot + offset) % classic_slot_count;

        if (desktop_slots[slot].available && !used[slot]) return slot;
    }
    return -1;
}

static void desktop_assign_classic_slots(void) {
    uint8_t used[DESKTOP_CLASSIC_MAX_SLOTS] = {0};
    int card_width = (int)display_scale_px(DESKTOP_CLASSIC_CARD_WIDTH);
    int card_height = (int)display_scale_px(DESKTOP_CLASSIC_CARD_HEIGHT);

    for (int i = 0; i < icon_count; i++) {
        int slot = desktop_find_slot_by_grid(desktop_icon_slots[i]);

        if (slot < 0 || !desktop_slots[slot].available || used[slot]) {
            slot = desktop_find_free_slot(used, i);
            if (slot < 0) {
                LOG_ERROR("DESKTOP", "Nenhum slot livre para icone Classic");
                return;
            }
            desktop_icon_slots[i] = desktop_slots[slot].grid_index;
        }

        used[slot] = 1;
        desktop_icons[i].classic_x = desktop_slots[slot].x;
        desktop_icons[i].classic_y = desktop_slots[slot].y;
        desktop_icons[i].classic_width = card_width;
        desktop_icons[i].classic_height = card_height;
    }
}

static void desktop_apply_drag_preview(void) {
    desktop_icon_t* icon;

    if (!desktop_drag_active || desktop_drag_icon < 0 ||
        desktop_drag_icon >= icon_count || desktop_drag_preview_slot < 0 ||
        desktop_drag_preview_slot >= classic_slot_count) return;

    icon = &desktop_icons[desktop_drag_icon];
    icon->classic_x = desktop_slots[desktop_drag_preview_slot].x;
    icon->classic_y = desktop_slots[desktop_drag_preview_slot].y;
}

static void desktop_layout_classic(void) {
    vesa_mode_t* mode = vesa_get_mode();
    tb_rect_t work_area;

    if (!mode || !mode->initialized) {
        LOG_ERROR("DESKTOP", "Modo VESA indisponivel para layout Classic");
        return;
    }

    work_area.x = 0;
    work_area.y = 0;
    work_area.width = mode->width;
    work_area.height = mode->height;
    if (!taskbar_get_work_area(&work_area)) {
        LOG_ERROR("DESKTOP", "Area de trabalho da taskbar indisponivel");
        return;
    }
    classic_columns = desktop_classic_get_columns(&work_area);
    if (desktop_build_classic_slots(&work_area) != OK) return;
    desktop_assign_classic_slots();
    desktop_apply_drag_preview();
}

static void draw_single_icon_simple(desktop_icon_t* icon) {
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

static void draw_single_icon_classic(desktop_icon_t* icon) {
    icon_entry_t* entry = desktop_get_icon_entry(icon->type);
    display_metrics_t metrics;
    uint32_t label_width;
    uint32_t label_height;

    if (!entry) {
        LOG_ERROR("DESKTOP", "Registro do icone Classic nao encontrado");
        return;
    }
    if (display_get_metrics(&metrics) != OK) return;

    uint32_t background = icon->selected ? GUI_COLOR_TITLE_BG : GUI_COLOR_BG;
    uint32_t foreground = icon->selected ? GUI_COLOR_TEXT_W : GUI_COLOR_TEXT;
    int symbol_scale = metrics.scale == DISPLAY_SCALE_LARGE ? 3 :
                       DESKTOP_CLASSIC_SYMBOL_SCALE;
    int symbol_width = FONT_WIDTH * symbol_scale;
    int symbol_x = icon->classic_x + (icon->classic_width - symbol_width) / 2;
    int symbol_y = icon->classic_y + (int)display_scale_px(14);
    int bitmap_x = icon->classic_x +
                   (icon->classic_width - metrics.icon_size) / 2;
    int bitmap_y = icon->classic_y + (int)display_scale_px(10);
    if (gui_measure_scaled_text(icon->name, &label_width,
                                &label_height) != OK) return;
    int label_x = icon->classic_x +
                  (icon->classic_width - (int)label_width) / 2;
    int label_y = icon->classic_y + icon->classic_height -
                  (int)label_height - (int)display_scale_px(10);

    gui_draw_panel(icon->classic_x, icon->classic_y,
                   icon->classic_width, icon->classic_height,
                   background, icon->selected);

    vesa_color_t symbol_color;
    symbol_color.raw = foreground;
    if (desktop_draw_classic_bitmap(icon->type, bitmap_x, bitmap_y,
                                   metrics.icon_size) != OK) {
        vesa_draw_char(symbol_x, symbol_y, entry->ch, symbol_color,
                       symbol_scale);
    }

    if (label_x < icon->classic_x + (int)display_scale_px(4)) {
        label_x = icon->classic_x + (int)display_scale_px(4);
    }
    gui_draw_scaled_text(label_x, label_y, icon->name, foreground);
}

static void desktop_draw_icons_simple(void) {
    for (int i = 0; i < icon_count; i++) {
        draw_single_icon_simple(&desktop_icons[i]);
    }
}

static void desktop_draw_icons_classic(void) {
    for (int i = 0; i < icon_count; i++) {
        draw_single_icon_classic(&desktop_icons[i]);
    }
}

static void desktop_draw_simple(void) {
    vesa_mode_t* mode = vesa_get_mode();

    if (mode && mode->initialized) mouse_invalidate_cursor();

    video_fill_rect(0, 0, SCREEN_COLS, SCREEN_ROWS - 1, ' ', DESKTOP_BG_COLOR);
    video_fill_rect(0, 0, SCREEN_COLS, 1, ' ', 0x1F);
    video_print_at((SCREEN_COLS - 20) / 2, 0, " ZephyrOS Desktop ", 0x1F);
    desktop_draw_icons_simple();

    if (mode && mode->initialized) vesa_flip();
}

static int desktop_draw_classic_workspace(void) {
    vesa_mode_t* mode = vesa_get_mode();
    vesa_color_t background;

    if (!mode || !mode->initialized) {
        LOG_ERROR("DESKTOP", "VESA indisponivel para Desktop Classic");
        return 0;
    }

    background.raw = desktop_classic_background();
    vesa_clear(background);
    desktop_layout_classic();
    desktop_draw_icons_classic();

    return 1;
}

static void desktop_draw_classic(void) {
    mouse_invalidate_cursor();
    /* Limpa tambem o estado TUI para que logs antigos nao retornem ao frame. */
    video_clear();
    if (!desktop_draw_classic_workspace()) return;

    /* A taskbar faz parte da mesma cena e usa o mesmo backbuffer. */
    taskbar_draw();
}

static int desktop_find_classic_icon(int px, int py) {
    for (int i = 0; i < icon_count; i++) {
        desktop_icon_t* icon = &desktop_icons[i];

        if (px >= icon->classic_x &&
            px < icon->classic_x + icon->classic_width &&
            py >= icon->classic_y &&
            py < icon->classic_y + icon->classic_height) {
            return i;
        }
    }

    return -1;
}

static void desktop_reset_drag(void) {
    desktop_drag_icon = -1;
    desktop_drag_start_x = 0;
    desktop_drag_start_y = 0;
    desktop_drag_active = 0;
    desktop_drag_preview_slot = -1;
}

static int desktop_drag_threshold_reached(const mouse_event_t* event) {
    int delta_x;
    int delta_y;

    if (!event) return 0;
    delta_x = event->x - desktop_drag_start_x;
    delta_y = event->y - desktop_drag_start_y;
    if (delta_x < 0) delta_x = -delta_x;
    if (delta_y < 0) delta_y = -delta_y;
    return delta_x >= DESKTOP_DRAG_THRESHOLD ||
           delta_y >= DESKTOP_DRAG_THRESHOLD;
}

static int desktop_slot_is_occupied(int slot_index, int ignored_icon) {
    if (slot_index < 0 || slot_index >= classic_slot_count) return 1;

    for (int i = 0; i < icon_count; i++) {
        int icon_slot;

        if (i == ignored_icon) continue;
        icon_slot = desktop_find_slot_by_grid(desktop_icon_slots[i]);
        if (icon_slot == slot_index) return 1;
    }
    return 0;
}

static int desktop_find_drop_slot(int preferred_slot, int ignored_icon) {
    if (preferred_slot < 0 || preferred_slot >= classic_slot_count) return -1;

    for (int offset = 0; offset < classic_slot_count; offset++) {
        int slot = (preferred_slot + offset) % classic_slot_count;

        if (desktop_slots[slot].available &&
            !desktop_slot_is_occupied(slot, ignored_icon)) {
            return slot;
        }
    }
    return -1;
}

static int desktop_find_nearest_slot(int px, int py) {
    int nearest_slot = -1;
    int nearest_distance = 0;

    for (int i = 0; i < classic_slot_count; i++) {
        int center_x;
        int center_y;
        int delta_x;
        int delta_y;
        int distance;

        if (!desktop_slots[i].available) continue;
        center_x = desktop_slots[i].x +
                   (int)display_scale_px(DESKTOP_CLASSIC_CARD_WIDTH) / 2;
        center_y = desktop_slots[i].y +
                   (int)display_scale_px(DESKTOP_CLASSIC_CARD_HEIGHT) / 2;
        delta_x = center_x - px;
        delta_y = center_y - py;
        distance = delta_x * delta_x + delta_y * delta_y;
        if (nearest_slot < 0 || distance < nearest_distance) {
            nearest_slot = i;
            nearest_distance = distance;
        }
    }
    return nearest_slot;
}

static void desktop_update_drag_preview(const mouse_event_t* event) {
    int nearest_slot;
    int preview_slot;

    if (!event || desktop_drag_icon < 0) return;
    nearest_slot = desktop_find_nearest_slot(event->x, event->y);
    preview_slot = desktop_find_drop_slot(nearest_slot, desktop_drag_icon);
    if (preview_slot < 0 || preview_slot == desktop_drag_preview_slot) return;

    desktop_drag_preview_slot = preview_slot;
    desktop_draw();
}

void desktop_init(void) {
    LOG_INFO("DESKTOP", "Inicializando Desktop");

    icon_count = 0;
    selected_icon = 0;
    desktop_active = 0;
    last_click_icon = -1;
    last_click_ticks = 0;
    desktop_reset_drag();
    for (int i = 0; i < DESKTOP_MAX_ICONS; i++) {
        desktop_icon_slots[i] = -1;
    }

    vesa_mode_t* mode = vesa_get_mode();
    desktop_mode = (mode && mode->initialized) ?
        DESKTOP_MODE_CLASSIC : DESKTOP_MODE_SIMPLE;

    desktop_add_icon("Shell", DESKTOP_APP_SHELL);
    desktop_add_icon("Explorer", DESKTOP_APP_EXPLORER);
    desktop_add_icon("TaskMgr", DESKTOP_APP_TASKMGR);

    if (desktop_mode == DESKTOP_MODE_SIMPLE) {
        LOG_WARN("DESKTOP", "VESA indisponivel, usando modo simple");
    }

    LOG_INFO("DESKTOP", "Desktop inicializado com sucesso");
}

void desktop_draw(void) {
    vesa_frame_begin();

    if (desktop_mode == DESKTOP_MODE_CLASSIC) {
        vesa_mode_t* mode = vesa_get_mode();
        if (mode && mode->initialized) {
            desktop_draw_classic();
            vesa_frame_end();
            return;
        }

        LOG_WARN("DESKTOP", "Modo classic sem VESA, alternando para simple");
        desktop_mode = DESKTOP_MODE_SIMPLE;
    }

    desktop_draw_simple();
    taskbar_draw();
    vesa_frame_end();
}

void desktop_draw_workspace(void) {
    if (desktop_mode != DESKTOP_MODE_CLASSIC) return;
    (void)desktop_draw_classic_workspace();
}

void desktop_draw_icons(void) {
    if (desktop_mode == DESKTOP_MODE_CLASSIC) {
        vesa_mode_t* mode = vesa_get_mode();
        if (!mode || !mode->initialized) {
            LOG_ERROR("DESKTOP", "Nao foi possivel desenhar icones Classic");
            return;
        }

        int left = (int)mode->width;
        int top = (int)mode->height;
        int right = 0;
        int bottom = 0;

        desktop_layout_classic();
        if (icon_count == 0) return;
        for (int i = 0; i < icon_count; i++) {
            desktop_icon_t* icon = &desktop_icons[i];
            if (icon->classic_x < left) left = icon->classic_x;
            if (icon->classic_y < top) top = icon->classic_y;
            if (icon->classic_x + icon->classic_width > right) {
                right = icon->classic_x + icon->classic_width;
            }
            if (icon->classic_y + icon->classic_height > bottom) {
                bottom = icon->classic_y + icon->classic_height;
            }
        }

        vesa_frame_begin_region((uint32_t)left, (uint32_t)top,
                                (uint32_t)(right - left),
                                (uint32_t)(bottom - top));
        mouse_invalidate_cursor();
        desktop_draw_icons_classic();
        vesa_frame_end();
        return;
    }

    vesa_frame_begin();
    desktop_draw_icons_simple();
    vesa_frame_end();
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
    desktop_icons[icon_count].classic_x = 0;
    desktop_icons[icon_count].classic_y = 0;
    desktop_icons[icon_count].classic_width =
        (int)display_scale_px(DESKTOP_CLASSIC_CARD_WIDTH);
    desktop_icons[icon_count].classic_height =
        (int)display_scale_px(DESKTOP_CLASSIC_CARD_HEIGHT);
    desktop_icons[icon_count].selected = (icon_count == selected_icon) ? 1 : 0;
    desktop_icon_slots[icon_count] = icon_count;

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

    columns = (desktop_mode == DESKTOP_MODE_CLASSIC) ? classic_columns : 5;
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
        desktop_reset_drag();
    }
}

int desktop_is_active(void) {
    return desktop_active;
}

int desktop_set_mode(desktop_mode_t mode) {
    vesa_mode_t* vesa_mode = vesa_get_mode();
    display_metrics_t metrics;

    if (mode != DESKTOP_MODE_SIMPLE && mode != DESKTOP_MODE_CLASSIC &&
        mode != DESKTOP_MODE_MODERN) {
        LOG_ERROR("DESKTOP", "Modo de Desktop invalido");
        return ERR_INVALID;
    }

    if (mode == DESKTOP_MODE_MODERN) {
        LOG_WARN("DESKTOP", "Modo modern reservado para implementacao futura");
        return ERR_UNAVAILABLE;
    }

    if (mode == DESKTOP_MODE_CLASSIC &&
        (!vesa_mode || !vesa_mode->initialized ||
         !vesa_has_backbuffer() ||
         display_get_metrics(&metrics) != OK || !metrics.available)) {
        LOG_ERROR("DESKTOP", "Modo classic requer display VESA acessivel");
        desktop_mode = DESKTOP_MODE_SIMPLE;
        return ERR_NOT_FOUND;
    }

    desktop_mode = mode;
    if (selected_icon < 0) selected_icon = 0;
    last_click_icon = -1;
    last_click_ticks = 0;
    desktop_reset_drag();

    if (desktop_active) {
        desktop_draw();
    }

    LOG_INFO("DESKTOP", mode == DESKTOP_MODE_CLASSIC ?
             "Modo classic ativado" : "Modo simple ativado");
    return OK;
}

desktop_mode_t desktop_get_mode(void) {
    return desktop_mode;
}

int desktop_handle_mouse(mouse_event_t* event) {
    uint32_t now;
    int hit_index;
    int dragged_icon;
    int preview_slot;

    if (!event) {
        LOG_ERROR("DESKTOP", "Evento de mouse nulo");
        return 0;
    }
    if (!desktop_active || desktop_mode != DESKTOP_MODE_CLASSIC) return 0;

    if (event->event == MOUSE_EVENT_PRESS &&
        (event->changed & MOUSE_BTN_LEFT)) {
        hit_index = desktop_find_classic_icon(event->x, event->y);
        if (hit_index < 0) {
            desktop_reset_drag();
            selected_icon = -1;
            last_click_icon = -1;
            desktop_update_selection();
            return 0;
        }

        selected_icon = hit_index;
        desktop_drag_icon = hit_index;
        desktop_drag_start_x = event->x;
        desktop_drag_start_y = event->y;
        desktop_drag_active = 0;
        desktop_drag_preview_slot = -1;
        desktop_update_selection();
        return 0;
    }

    if (event->event == MOUSE_EVENT_MOVE && desktop_drag_icon >= 0 &&
        (event->buttons & MOUSE_BTN_LEFT)) {
        if (!desktop_drag_active && desktop_drag_threshold_reached(event)) {
            desktop_drag_active = 1;
            last_click_icon = -1;
            last_click_ticks = 0;
        }
        if (desktop_drag_active) desktop_update_drag_preview(event);
        return 0;
    }

    if (event->event != MOUSE_EVENT_RELEASE ||
        !(event->changed & MOUSE_BTN_LEFT) || desktop_drag_icon < 0) {
        return 0;
    }

    dragged_icon = desktop_drag_icon;
    preview_slot = desktop_drag_preview_slot;
    if (desktop_drag_active) {
        desktop_reset_drag();
        if (preview_slot >= 0 && preview_slot < classic_slot_count) {
            desktop_icon_slots[dragged_icon] =
                desktop_slots[preview_slot].grid_index;
        }
        selected_icon = dragged_icon;
        last_click_icon = -1;
        last_click_ticks = 0;
        desktop_draw();
        return 0;
    }

    desktop_reset_drag();
    now = timer_get_ticks();
    if (selected_icon == dragged_icon && last_click_icon == dragged_icon &&
        now - last_click_ticks <= DESKTOP_DOUBLE_CLICK_TICKS) {
        last_click_icon = -1;
        last_click_ticks = 0;
        return desktop_icons[dragged_icon].type;
    }

    last_click_icon = dragged_icon;
    last_click_ticks = now;
    return 0;
}

int desktop_handle_click(int px, int py) {
    if (!desktop_active || desktop_mode == DESKTOP_MODE_CLASSIC) return 0;

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
