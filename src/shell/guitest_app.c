#include "apps/guitest.h"
#include "apps/shell.h"
#include "ui/gui.h"
#include "ui/display.h"
#include "ui/desktop.h"
#include "ui/taskbar.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/video.h"
#include "drivers/speaker.h"

#define GUITEST_CLASSIC_X 200
#define GUITEST_CLASSIC_Y 150
#define GUITEST_CLASSIC_WIDTH 400
#define GUITEST_CLASSIC_HEIGHT 300
#define GUITEST_CLASSIC_CLOSE_SIZE 16
#define GUITEST_CLASSIC_CLOSE_INSET 4
#define GUITEST_CLASSIC_CLOSE_TOP 3
#define GUITEST_CLASSIC_TEXT_FIRST_Y 260
#define GUITEST_CLASSIC_TEXT_SECOND_Y 280

#define GUITEST_BUTTON_X 250
#define GUITEST_BUTTON_Y 200
#define GUITEST_BUTTON_WIDTH 120
#define GUITEST_BUTTON_HEIGHT 30
#define GUITEST_BUTTON_BEEP_HZ 1000
#define GUITEST_BUTTON_BEEP_MS 50

#define GUITEST_ESCAPE_SCANCODE 0x01U
#define GUITEST_MODERN_COLUMN_COUNT 3
#define GUITEST_MODERN_GAP_COUNT 4
#define GUITEST_MODERN_MARGIN_FACTOR 2
#define GUITEST_MODERN_RADIUS_SMALL 4U
#define GUITEST_MODERN_RADIUS_LARGE 14U
#define GUITEST_MODERN_SHAPE_HEIGHT 54U
#define GUITEST_MODERN_CLIP_SIZE 72U
#define GUITEST_MODERN_PALETTE_COUNT 7
#define GUITEST_MODERN_SWATCH_HEIGHT 44U
#define GUITEST_MODERN_BUTTON_WIDTH 180U
#define GUITEST_MODERN_BUTTON_HEIGHT 36U
#define GUITEST_MODERN_TOP_RATIO_NUMERATOR 3
#define GUITEST_MODERN_TOP_RATIO_DENOMINATOR 5

static const uint32_t guitest_modern_palette[
    GUITEST_MODERN_PALETTE_COUNT] = {
    GUI_MODERN_COLOR_BG,
    GUI_MODERN_COLOR_WINDOW,
    GUI_MODERN_COLOR_ACCENT,
    GUI_MODERN_COLOR_TEXT,
    GUI_MODERN_COLOR_BORDER_INACTIVE,
    GUI_MODERN_COLOR_HOVER,
    GUI_MODERN_COLOR_PRESSED
};

typedef enum {
    GUITEST_SCENE_CLASSIC = 0,
    GUITEST_SCENE_MODERN
} guitest_scene_t;

typedef struct {
    int x;
    int y;
    int width;
    int height;
    int close_x;
    int close_y;
    int close_size;
    int modern_button_x;
    int modern_button_y;
    int modern_button_width;
    int modern_button_height;
} guitest_layout_t;

static int guitest_active = 0;
static int button_pressed = 0;
static guitest_scene_t guitest_scene = GUITEST_SCENE_CLASSIC;
static guitest_layout_t guitest_layout;
static display_metrics_t guitest_metrics;
static gui_button_state_t modern_button_state = GUI_BUTTON_STATE_NORMAL;
static int modern_button_captured = 0;

static void guitest_prepare_classic_layout(void) {
    guitest_layout.x = GUITEST_CLASSIC_X;
    guitest_layout.y = GUITEST_CLASSIC_Y;
    guitest_layout.width = GUITEST_CLASSIC_WIDTH;
    guitest_layout.height = GUITEST_CLASSIC_HEIGHT;
    guitest_layout.close_size = GUITEST_CLASSIC_CLOSE_SIZE;
    guitest_layout.close_x = GUITEST_CLASSIC_X + GUITEST_CLASSIC_WIDTH -
                             GUITEST_CLASSIC_CLOSE_SIZE -
                             GUITEST_CLASSIC_CLOSE_INSET;
    guitest_layout.close_y = GUITEST_CLASSIC_Y + GUITEST_CLASSIC_CLOSE_TOP;
}

static int guitest_update_modern_layout(void) {
    vesa_mode_t* mode = vesa_get_mode();
    tb_rect_t work_area;
    int margin;
    int frame_inset;
    int minimum_height;

    if (!mode || !mode->initialized) {
        LOG_ERROR("GUITEST", "Modo VESA indisponivel para cena MV2");
        return ERR_STATE;
    }
    if (display_get_metrics(&guitest_metrics) != OK ||
        !guitest_metrics.available) {
        LOG_ERROR("GUITEST", "Metricas indisponiveis para cena MV2");
        return ERR_STATE;
    }

    work_area.x = 0;
    work_area.y = 0;
    work_area.width = (int)mode->width;
    work_area.height = (int)mode->height;
    if (!taskbar_get_work_area(&work_area)) {
        LOG_ERROR("GUITEST", "Area util indisponivel para cena MV2");
        return ERR_STATE;
    }

    margin = (int)guitest_metrics.spacing * GUITEST_MODERN_MARGIN_FACTOR;
    minimum_height = (int)guitest_metrics.title_bar_height +
                     (int)guitest_metrics.font_height +
                     margin * GUITEST_MODERN_GAP_COUNT;
    if (work_area.width <= margin * 2 * GUITEST_MODERN_COLUMN_COUNT ||
        work_area.height <= minimum_height) {
        LOG_ERROR("GUITEST", "Area util insuficiente para cena MV2");
        return ERR_OVERFLOW;
    }

    guitest_layout.x = work_area.x + margin;
    guitest_layout.y = work_area.y + margin;
    guitest_layout.width = work_area.width - margin * 2;
    guitest_layout.height = work_area.height - margin * 2;
    frame_inset = (int)display_scale_px(2U);
    guitest_layout.close_size = (int)guitest_metrics.title_bar_height;
    guitest_layout.close_x = guitest_layout.x + guitest_layout.width -
                             frame_inset - guitest_layout.close_size;
    guitest_layout.close_y = guitest_layout.y + frame_inset;
    return OK;
}

static void guitest_draw_modern_card(int x, int y, int width, int height) {
    uint32_t radius = display_scale_px(GUITEST_MODERN_RADIUS_SMALL);

    gui_draw_rounded_rect((uint32_t)x, (uint32_t)y, (uint32_t)width,
                          (uint32_t)height, radius,
                          GUI_MODERN_COLOR_WINDOW);
    gui_draw_flat_border((uint32_t)x, (uint32_t)y, (uint32_t)width,
                         (uint32_t)height, GUI_MODERN_COLOR_ACCENT);
}

static void guitest_draw_rounded_card(const display_metrics_t* metrics,
                                      int x, int y, int width, int height) {
    int gap = (int)metrics->spacing;
    int shape_x = x + gap;
    int shape_y = y + gap + (int)metrics->font_height + gap;
    int shape_width = width - gap * 2;
    int shape_height = (int)display_scale_px(GUITEST_MODERN_SHAPE_HEIGHT);
    uint32_t small_radius = display_scale_px(GUITEST_MODERN_RADIUS_SMALL);
    uint32_t large_radius = display_scale_px(GUITEST_MODERN_RADIUS_LARGE);

    (void)height;
    gui_draw_scaled_text((uint32_t)(x + gap), (uint32_t)(y + gap),
                         "Cantos arredondados", GUI_MODERN_COLOR_TEXT);
    gui_draw_rounded_rect((uint32_t)shape_x, (uint32_t)shape_y,
                          (uint32_t)shape_width, (uint32_t)shape_height,
                          small_radius, GUI_MODERN_COLOR_ACCENT);
    shape_y += shape_height + gap;
    gui_draw_rounded_rect((uint32_t)shape_x, (uint32_t)shape_y,
                          (uint32_t)shape_width, (uint32_t)shape_height,
                          large_radius,
                          GUI_MODERN_COLOR_BORDER_INACTIVE);
    shape_y += shape_height + gap;
    gui_draw_rounded_rect((uint32_t)shape_x, (uint32_t)shape_y,
                          (uint32_t)shape_width, (uint32_t)shape_height,
                          (uint32_t)shape_height, GUI_MODERN_COLOR_TEXT);
}

static void guitest_draw_border_card(const display_metrics_t* metrics,
                                     int x, int y, int width, int height) {
    int gap = (int)metrics->spacing;
    int sample_x = x + gap;
    int sample_y = y + gap + (int)metrics->font_height + gap;
    int sample_width = width - gap * 2;
    int sample_height = height - (sample_y - y) - gap;
    int clip_size = (int)display_scale_px(GUITEST_MODERN_CLIP_SIZE);
    int clip_x;
    int clip_y;

    gui_draw_scaled_text((uint32_t)(x + gap), (uint32_t)(y + gap),
                         "Borda flat e clipping", GUI_MODERN_COLOR_TEXT);
    gui_draw_flat_border((uint32_t)sample_x, (uint32_t)sample_y,
                         (uint32_t)sample_width, (uint32_t)sample_height,
                         GUI_MODERN_COLOR_ACCENT);
    gui_draw_flat_border((uint32_t)(sample_x + gap),
                         (uint32_t)(sample_y + gap),
                         (uint32_t)(sample_width - gap * 2), 1U,
                         GUI_MODERN_COLOR_TEXT);
    gui_draw_flat_border((uint32_t)(sample_x + gap),
                         (uint32_t)(sample_y + gap * 2), 1U,
                         display_scale_px(GUITEST_MODERN_SHAPE_HEIGHT),
                         GUI_MODERN_COLOR_TEXT);

    if (clip_size > sample_width - gap * 2) {
        clip_size = sample_width - gap * 2;
    }
    if (clip_size > sample_height - gap * 2) {
        clip_size = sample_height - gap * 2;
    }
    if (clip_size < 1) return;
    clip_x = sample_x + sample_width - clip_size - gap;
    clip_y = sample_y + sample_height - clip_size - gap;
    vesa_set_clip_rect((uint32_t)clip_x, (uint32_t)clip_y,
                       (uint32_t)clip_size, (uint32_t)clip_size);
    gui_draw_rounded_rect(
        (uint32_t)(clip_x + clip_size / 2),
        (uint32_t)(clip_y + clip_size / 4),
        (uint32_t)clip_size, (uint32_t)clip_size,
        display_scale_px(GUITEST_MODERN_RADIUS_LARGE),
        GUI_MODERN_COLOR_BORDER_INACTIVE);
    vesa_reset_clip_rect();
    gui_draw_flat_border((uint32_t)clip_x, (uint32_t)clip_y,
                         (uint32_t)clip_size, (uint32_t)clip_size,
                         GUI_MODERN_COLOR_TEXT);
}

static void guitest_draw_gradient_card(const display_metrics_t* metrics,
                                       int x, int y, int width, int height) {
    int gap = (int)metrics->spacing;
    int gradient_y = y + gap + (int)metrics->font_height + gap;
    int gradient_height = height - (gradient_y - y) - gap;
    int gradient_width = (width - gap * 3) / 2;
    int left_x = x + gap;
    int right_x = left_x + gradient_width + gap;

    gui_draw_scaled_text((uint32_t)(x + gap), (uint32_t)(y + gap),
                         "Gradiente vertical", GUI_MODERN_COLOR_TEXT);
    gui_draw_vertical_gradient(
        (uint32_t)left_x, (uint32_t)gradient_y,
        (uint32_t)gradient_width, (uint32_t)gradient_height,
        GUI_MODERN_COLOR_HOVER, GUI_MODERN_COLOR_BG);
    gui_draw_flat_border((uint32_t)left_x, (uint32_t)gradient_y,
                         (uint32_t)gradient_width,
                         (uint32_t)gradient_height,
                         GUI_MODERN_COLOR_TEXT);
    gui_draw_vertical_gradient(
        (uint32_t)right_x, (uint32_t)gradient_y,
        (uint32_t)gradient_width, (uint32_t)gradient_height,
        GUI_MODERN_COLOR_BG, GUI_MODERN_COLOR_HOVER);
    gui_draw_flat_border((uint32_t)right_x, (uint32_t)gradient_y,
                         (uint32_t)gradient_width,
                         (uint32_t)gradient_height,
                         GUI_MODERN_COLOR_TEXT);
}

static const char* guitest_modern_button_state_name(void) {
    if (modern_button_state == GUI_BUTTON_STATE_HOVER) return "Hover";
    if (modern_button_state == GUI_BUTTON_STATE_PRESSED) return "Pressed";
    return "Normal";
}

static void guitest_draw_palette_swatches(int x, int y,
                                          int width, int height,
                                          int gap) {
    int swatch_width =
        (width - gap * (GUITEST_MODERN_PALETTE_COUNT - 1)) /
        GUITEST_MODERN_PALETTE_COUNT;

    if (swatch_width < 1 || height < 1) return;
    for (int index = 0; index < GUITEST_MODERN_PALETTE_COUNT; index++) {
        int swatch_x = x + index * (swatch_width + gap);

        gui_draw_rounded_rect(
            (uint32_t)swatch_x, (uint32_t)y,
            (uint32_t)swatch_width, (uint32_t)height,
            display_scale_px(GUITEST_MODERN_RADIUS_SMALL),
            guitest_modern_palette[index]);
        gui_draw_flat_border(
            (uint32_t)swatch_x, (uint32_t)y,
            (uint32_t)swatch_width, (uint32_t)height,
            GUI_MODERN_COLOR_BORDER_INACTIVE);
    }
}

static void guitest_draw_palette_card(const display_metrics_t* metrics,
                                      int x, int y, int width, int height) {
    int gap = (int)metrics->spacing;
    int content_y = y + gap + (int)metrics->font_height + gap;
    int palette_width = width * GUITEST_MODERN_TOP_RATIO_NUMERATOR /
                        GUITEST_MODERN_TOP_RATIO_DENOMINATOR - gap * 2;
    int details_x = x + palette_width + gap * 3;
    int details_width = x + width - gap - details_x;
    int swatch_height =
        (int)display_scale_px(GUITEST_MODERN_SWATCH_HEIGHT);
    int button_width =
        (int)display_scale_px(GUITEST_MODERN_BUTTON_WIDTH);
    int button_height =
        (int)display_scale_px(GUITEST_MODERN_BUTTON_HEIGHT);
    int state_y;

    gui_draw_scaled_text((uint32_t)(x + gap), (uint32_t)(y + gap),
                         "Paleta oficial e botao Modern",
                         GUI_MODERN_COLOR_TEXT);
    if (swatch_height > height - (content_y - y) - gap) {
        swatch_height = height - (content_y - y) - gap;
    }
    guitest_draw_palette_swatches(x + gap, content_y, palette_width,
                                  swatch_height, gap);

    gui_draw_scaled_text((uint32_t)details_x, (uint32_t)content_y,
                         "Tema ativo:", GUI_MODERN_COLOR_TEXT);
    state_y = content_y + (int)metrics->font_height + gap;
    gui_draw_scaled_text((uint32_t)details_x, (uint32_t)state_y,
                         gui_theme_name(gui_get_theme()),
                         GUI_MODERN_COLOR_ACCENT);

    if (button_width > details_width) button_width = details_width;
    guitest_layout.modern_button_width = button_width;
    guitest_layout.modern_button_height = button_height;
    guitest_layout.modern_button_x =
        details_x + (details_width - button_width) / 2;
    guitest_layout.modern_button_y =
        y + height - gap - button_height;
    gui_draw_modern_button(
        (uint32_t)guitest_layout.modern_button_x,
        (uint32_t)guitest_layout.modern_button_y,
        (uint32_t)guitest_layout.modern_button_width,
        (uint32_t)guitest_layout.modern_button_height,
        guitest_modern_button_state_name(), modern_button_state);
}

static void guitest_draw_modern_samples(void) {
    int gap = (int)guitest_metrics.spacing;
    int frame_inset = (int)display_scale_px(2U);
    int canvas_x = guitest_layout.x + gap * 2;
    int canvas_y = guitest_layout.y + frame_inset +
                   (int)guitest_metrics.title_bar_height + gap;
    int canvas_width = guitest_layout.width - gap * 4;
    int canvas_height = guitest_layout.height -
                        (canvas_y - guitest_layout.y) - gap * 2;
    int card_y = canvas_y + gap * 2 + (int)guitest_metrics.font_height;
    int content_height = canvas_height - gap * 3 -
                         (int)guitest_metrics.font_height;
    int minimum_card_height =
        (int)guitest_metrics.font_height + gap * 5 +
        (int)display_scale_px(GUITEST_MODERN_SHAPE_HEIGHT) * 3;
    int minimum_palette_height =
        (int)guitest_metrics.font_height * 3 + gap * 5 +
        (int)display_scale_px(GUITEST_MODERN_BUTTON_HEIGHT);
    int card_height;
    int palette_y;
    int palette_height;
    int card_width;

    if (canvas_width <= gap * GUITEST_MODERN_GAP_COUNT ||
        content_height < minimum_card_height + gap +
                         minimum_palette_height) {
        LOG_ERROR("GUITEST", "Layout interno insuficiente para cena MV2");
        return;
    }

    gui_draw_rounded_rect((uint32_t)canvas_x, (uint32_t)canvas_y,
                          (uint32_t)canvas_width, (uint32_t)canvas_height,
                          display_scale_px(GUITEST_MODERN_RADIUS_LARGE),
                          GUI_MODERN_COLOR_BG);
    gui_draw_flat_border((uint32_t)canvas_x, (uint32_t)canvas_y,
                         (uint32_t)canvas_width, (uint32_t)canvas_height,
                         GUI_MODERN_COLOR_ACCENT);
    gui_draw_scaled_text((uint32_t)(canvas_x + gap),
                         (uint32_t)(canvas_y + gap),
                         "MV2 - Base Visual Modern",
                         GUI_MODERN_COLOR_TEXT);

    card_height = content_height * GUITEST_MODERN_TOP_RATIO_NUMERATOR /
                  GUITEST_MODERN_TOP_RATIO_DENOMINATOR;
    if (card_height < minimum_card_height) {
        card_height = minimum_card_height;
    }
    if (content_height - card_height - gap < minimum_palette_height) {
        card_height = content_height - gap - minimum_palette_height;
    }
    card_width = (canvas_width - gap * GUITEST_MODERN_GAP_COUNT) /
                 GUITEST_MODERN_COLUMN_COUNT;
    for (int column = 0; column < GUITEST_MODERN_COLUMN_COUNT; column++) {
        int card_x = canvas_x + gap + column * (card_width + gap);
        guitest_draw_modern_card(card_x, card_y, card_width, card_height);
        if (column == 0) {
            guitest_draw_rounded_card(&guitest_metrics, card_x, card_y,
                                      card_width, card_height);
        } else if (column == 1) {
            guitest_draw_border_card(&guitest_metrics, card_x, card_y,
                                     card_width, card_height);
        } else {
            guitest_draw_gradient_card(&guitest_metrics, card_x, card_y,
                                       card_width, card_height);
        }
    }
    palette_y = card_y + card_height + gap;
    palette_height = content_height - card_height - gap;
    guitest_draw_modern_card(canvas_x + gap, palette_y,
                             canvas_width - gap * 2, palette_height);
    guitest_draw_palette_card(
        &guitest_metrics, canvas_x + gap, palette_y,
        canvas_width - gap * 2, palette_height);
}

static void guitest_draw_classic_scene(void) {
    guitest_prepare_classic_layout();
    gui_draw_window_frame(
        GUITEST_CLASSIC_X, GUITEST_CLASSIC_Y,
        GUITEST_CLASSIC_WIDTH, GUITEST_CLASSIC_HEIGHT,
        "Meu Primeiro App GUI (C)", 1);
    gui_draw_button(GUITEST_BUTTON_X, GUITEST_BUTTON_Y,
                    GUITEST_BUTTON_WIDTH, GUITEST_BUTTON_HEIGHT,
                    "Aperte-me", button_pressed);
    gui_draw_text(GUITEST_BUTTON_X, GUITEST_CLASSIC_TEXT_FIRST_Y,
                  "Clique no botao acima!", GUI_COLOR_TEXT);
    gui_draw_text(GUITEST_BUTTON_X, GUITEST_CLASSIC_TEXT_SECOND_Y,
                  "Clique no [X] para fechar.", GUI_COLOR_TEXT);
}

static void guitest_draw_modern_scene(void) {
    if (guitest_update_modern_layout() != OK) return;
    gui_draw_scaled_window_frame(
        (uint32_t)guitest_layout.x, (uint32_t)guitest_layout.y,
        (uint32_t)guitest_layout.width, (uint32_t)guitest_layout.height,
        "GUI Test - MV2", 1);
    guitest_draw_modern_samples();
}

static void guitest_open_scene(guitest_scene_t scene) {
    if (!recovery_is_enabled(RECOVERY_COMPONENT_GUITEST) ||
        !recovery_is_enabled(RECOVERY_COMPONENT_VESA)) {
        LOG_WARN("GUITEST", "GUI Test requer VESA; abertura ignorada");
        return;
    }
    if (guitest_active) return;
    if (scene == GUITEST_SCENE_MODERN) {
        if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
            LOG_WARN("GUITEST", "Cena MV2 requer o modo Classic");
            return;
        }
        if (guitest_update_modern_layout() != OK) return;
    } else {
        guitest_prepare_classic_layout();
    }

    guitest_scene = scene;
    guitest_active = 1;
    button_pressed = 0;
    modern_button_state = GUI_BUTTON_STATE_NORMAL;
    modern_button_captured = 0;
    vesa_frame_begin();
    mouse_invalidate_cursor();
    video_clear();
    guitest_draw();
    vesa_frame_end();
    LOG_INFO("GUITEST", scene == GUITEST_SCENE_MODERN ?
             "Cena MV2 aberta" : "App Classic aberto");
}

void guitest_open(void) {
    guitest_open_scene(GUITEST_SCENE_CLASSIC);
}

void guitest_open_modern(void) {
    guitest_open_scene(GUITEST_SCENE_MODERN);
}

void guitest_close(void) {
    if (!guitest_active) return;
    guitest_active = 0;

    vesa_frame_begin();
    mouse_invalidate_cursor();
    shell_print_prompt();
    taskbar_draw();
    vesa_frame_end();
    LOG_INFO("GUITEST", "App fechado");
}

int guitest_is_active(void) {
    return guitest_active;
}

void guitest_draw(void) {
    if (!guitest_active) return;
    if (!recovery_is_enabled(RECOVERY_COMPONENT_GUITEST) ||
        !recovery_is_enabled(RECOVERY_COMPONENT_VESA)) {
        LOG_WARN("GUITEST", "GUI Test perdeu suporte VESA; encerrando");
        guitest_close();
        return;
    }

    vesa_frame_begin();
    mouse_invalidate_cursor();
    if (guitest_scene == GUITEST_SCENE_MODERN) {
        guitest_draw_modern_scene();
    } else {
        guitest_draw_classic_scene();
    }
    taskbar_draw();
    vesa_frame_end();
}

void guitest_handle_key(uint8_t scancode) {
    if (!guitest_active) return;
    if (scancode == GUITEST_ESCAPE_SCANCODE) guitest_close();
}

static int guitest_is_inside(int px, int py, int x, int y,
                             int width, int height) {
    return width > 0 && height > 0 && px >= x && py >= y &&
           px < x + width && py < y + height;
}

static void guitest_set_modern_button_state(gui_button_state_t state) {
    if (modern_button_state == state) return;
    modern_button_state = state;
    guitest_draw();
}

static int guitest_handle_modern_button_mouse(mouse_event_t* event) {
    int inside = guitest_is_inside(
        event->x, event->y,
        guitest_layout.modern_button_x,
        guitest_layout.modern_button_y,
        guitest_layout.modern_button_width,
        guitest_layout.modern_button_height);
    gui_button_state_t next_state;

    if (event->event == MOUSE_EVENT_PRESS &&
        (event->changed & MOUSE_BTN_LEFT)) {
        if (!inside) {
            guitest_set_modern_button_state(GUI_BUTTON_STATE_NORMAL);
            return 0;
        }
        modern_button_captured = 1;
        guitest_set_modern_button_state(GUI_BUTTON_STATE_PRESSED);
        return 1;
    }
    if (event->event == MOUSE_EVENT_MOVE) {
        if (modern_button_captured && (event->buttons & MOUSE_BTN_LEFT)) {
            next_state = inside ? GUI_BUTTON_STATE_PRESSED :
                         GUI_BUTTON_STATE_NORMAL;
        } else {
            modern_button_captured = 0;
            next_state = inside ? GUI_BUTTON_STATE_HOVER :
                         GUI_BUTTON_STATE_NORMAL;
        }
        guitest_set_modern_button_state(next_state);
        return 1;
    }
    if (event->event != MOUSE_EVENT_RELEASE ||
        !(event->changed & MOUSE_BTN_LEFT)) {
        return 0;
    }

    next_state = inside ? GUI_BUTTON_STATE_HOVER :
                 GUI_BUTTON_STATE_NORMAL;
    if (!modern_button_captured) {
        guitest_set_modern_button_state(next_state);
        return 0;
    }
    modern_button_captured = 0;
    guitest_set_modern_button_state(next_state);
    return 1;
}

void guitest_handle_mouse(mouse_event_t* event) {
    if (!guitest_active) return;
    if (!event) {
        LOG_ERROR("GUITEST", "Evento de mouse nulo");
        return;
    }

    if (guitest_scene == GUITEST_SCENE_MODERN) {
        if (guitest_handle_modern_button_mouse(event)) return;
        if (event->event == MOUSE_EVENT_RELEASE &&
            (event->changed & MOUSE_BTN_LEFT) &&
            guitest_is_inside(event->x, event->y,
                              guitest_layout.close_x,
                              guitest_layout.close_y,
                              guitest_layout.close_size,
                              guitest_layout.close_size)) {
            guitest_close();
        }
        return;
    }

    if (guitest_scene == GUITEST_SCENE_CLASSIC &&
        event->event == MOUSE_EVENT_PRESS &&
        (event->changed & MOUSE_BTN_LEFT) &&
        guitest_is_inside(event->x, event->y, GUITEST_BUTTON_X,
                          GUITEST_BUTTON_Y, GUITEST_BUTTON_WIDTH,
                          GUITEST_BUTTON_HEIGHT)) {
        if (!button_pressed) {
            button_pressed = 1;
            guitest_draw();
        }
        return;
    }

    if (event->event != MOUSE_EVENT_RELEASE ||
        !(event->changed & MOUSE_BTN_LEFT)) {
        return;
    }
    if (guitest_scene == GUITEST_SCENE_CLASSIC && button_pressed) {
        button_pressed = 0;
        guitest_draw();
        speaker_beep(GUITEST_BUTTON_BEEP_HZ, GUITEST_BUTTON_BEEP_MS);
    }
    if (guitest_is_inside(event->x, event->y, guitest_layout.close_x,
                          guitest_layout.close_y,
                          guitest_layout.close_size,
                          guitest_layout.close_size)) {
        guitest_close();
    }
}
