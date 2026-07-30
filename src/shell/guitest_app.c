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

/* Cores locais de diagnostico; a paleta global pertence ao MV2. */
#define GUITEST_MODERN_CANVAS 0x001E1E24U
#define GUITEST_MODERN_PANEL 0x002A2B36U
#define GUITEST_MODERN_ACCENT 0x0000ADB5U
#define GUITEST_MODERN_TEXT 0x00EEEEEEU
#define GUITEST_MODERN_GRADIENT_TOP 0x003C6173U
#define GUITEST_MODERN_GRADIENT_BOTTOM 0x00162934U

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
} guitest_layout_t;

static int guitest_active = 0;
static int button_pressed = 0;
static guitest_scene_t guitest_scene = GUITEST_SCENE_CLASSIC;
static guitest_layout_t guitest_layout;
static display_metrics_t guitest_metrics;

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
        LOG_ERROR("GUITEST", "Modo VESA indisponivel para cena MV1");
        return ERR_STATE;
    }
    if (display_get_metrics(&guitest_metrics) != OK ||
        !guitest_metrics.available) {
        LOG_ERROR("GUITEST", "Metricas indisponiveis para cena MV1");
        return ERR_STATE;
    }

    work_area.x = 0;
    work_area.y = 0;
    work_area.width = (int)mode->width;
    work_area.height = (int)mode->height;
    if (!taskbar_get_work_area(&work_area)) {
        LOG_ERROR("GUITEST", "Area util indisponivel para cena MV1");
        return ERR_STATE;
    }

    margin = (int)guitest_metrics.spacing * GUITEST_MODERN_MARGIN_FACTOR;
    minimum_height = (int)guitest_metrics.title_bar_height +
                     (int)guitest_metrics.font_height +
                     margin * GUITEST_MODERN_GAP_COUNT;
    if (work_area.width <= margin * 2 * GUITEST_MODERN_COLUMN_COUNT ||
        work_area.height <= minimum_height) {
        LOG_ERROR("GUITEST", "Area util insuficiente para cena MV1");
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
                          (uint32_t)height, radius, GUITEST_MODERN_PANEL);
    gui_draw_flat_border((uint32_t)x, (uint32_t)y, (uint32_t)width,
                         (uint32_t)height, GUITEST_MODERN_ACCENT);
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
                         "Cantos arredondados", GUITEST_MODERN_TEXT);
    gui_draw_rounded_rect((uint32_t)shape_x, (uint32_t)shape_y,
                          (uint32_t)shape_width, (uint32_t)shape_height,
                          small_radius, GUITEST_MODERN_ACCENT);
    shape_y += shape_height + gap;
    gui_draw_rounded_rect((uint32_t)shape_x, (uint32_t)shape_y,
                          (uint32_t)shape_width, (uint32_t)shape_height,
                          large_radius, GUITEST_MODERN_GRADIENT_TOP);
    shape_y += shape_height + gap;
    gui_draw_rounded_rect((uint32_t)shape_x, (uint32_t)shape_y,
                          (uint32_t)shape_width, (uint32_t)shape_height,
                          (uint32_t)shape_height, GUITEST_MODERN_TEXT);
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
                         "Borda flat e clipping", GUITEST_MODERN_TEXT);
    gui_draw_flat_border((uint32_t)sample_x, (uint32_t)sample_y,
                         (uint32_t)sample_width, (uint32_t)sample_height,
                         GUITEST_MODERN_ACCENT);
    gui_draw_flat_border((uint32_t)(sample_x + gap),
                         (uint32_t)(sample_y + gap),
                         (uint32_t)(sample_width - gap * 2), 1U,
                         GUITEST_MODERN_TEXT);
    gui_draw_flat_border((uint32_t)(sample_x + gap),
                         (uint32_t)(sample_y + gap * 2), 1U,
                         display_scale_px(GUITEST_MODERN_SHAPE_HEIGHT),
                         GUITEST_MODERN_TEXT);

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
        GUITEST_MODERN_GRADIENT_TOP);
    vesa_reset_clip_rect();
    gui_draw_flat_border((uint32_t)clip_x, (uint32_t)clip_y,
                         (uint32_t)clip_size, (uint32_t)clip_size,
                         GUITEST_MODERN_TEXT);
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
                         "Gradiente vertical", GUITEST_MODERN_TEXT);
    gui_draw_vertical_gradient(
        (uint32_t)left_x, (uint32_t)gradient_y,
        (uint32_t)gradient_width, (uint32_t)gradient_height,
        GUITEST_MODERN_GRADIENT_TOP, GUITEST_MODERN_GRADIENT_BOTTOM);
    gui_draw_flat_border((uint32_t)left_x, (uint32_t)gradient_y,
                         (uint32_t)gradient_width,
                         (uint32_t)gradient_height, GUITEST_MODERN_TEXT);
    gui_draw_vertical_gradient(
        (uint32_t)right_x, (uint32_t)gradient_y,
        (uint32_t)gradient_width, (uint32_t)gradient_height,
        GUITEST_MODERN_GRADIENT_BOTTOM, GUITEST_MODERN_GRADIENT_TOP);
    gui_draw_flat_border((uint32_t)right_x, (uint32_t)gradient_y,
                         (uint32_t)gradient_width,
                         (uint32_t)gradient_height, GUITEST_MODERN_TEXT);
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
    int card_height = canvas_height - gap * 3 -
                      (int)guitest_metrics.font_height;
    int card_width;

    if (canvas_width <= gap * GUITEST_MODERN_GAP_COUNT ||
        card_height <= gap * 2) {
        LOG_ERROR("GUITEST", "Layout interno insuficiente para cena MV1");
        return;
    }

    gui_draw_rounded_rect((uint32_t)canvas_x, (uint32_t)canvas_y,
                          (uint32_t)canvas_width, (uint32_t)canvas_height,
                          display_scale_px(GUITEST_MODERN_RADIUS_LARGE),
                          GUITEST_MODERN_CANVAS);
    gui_draw_flat_border((uint32_t)canvas_x, (uint32_t)canvas_y,
                         (uint32_t)canvas_width, (uint32_t)canvas_height,
                         GUITEST_MODERN_ACCENT);
    gui_draw_scaled_text((uint32_t)(canvas_x + gap),
                         (uint32_t)(canvas_y + gap),
                         "MV1 - Primitivas Modern", GUITEST_MODERN_TEXT);

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
        "GUI Test - MV1", 1);
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
            LOG_WARN("GUITEST", "Cena MV1 requer o modo Classic");
            return;
        }
        if (guitest_update_modern_layout() != OK) return;
    } else {
        guitest_prepare_classic_layout();
    }

    guitest_scene = scene;
    guitest_active = 1;
    button_pressed = 0;
    vesa_frame_begin();
    mouse_invalidate_cursor();
    video_clear();
    guitest_draw();
    vesa_frame_end();
    LOG_INFO("GUITEST", scene == GUITEST_SCENE_MODERN ?
             "Cena MV1 aberta" : "App Classic aberto");
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

void guitest_handle_mouse(mouse_event_t* event) {
    if (!guitest_active) return;
    if (!event) {
        LOG_ERROR("GUITEST", "Evento de mouse nulo");
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
