#include "ui/display.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "drivers/vesa.h"
#include "ui/desktop.h"
#include "ui/taskbar.h"
#include "ui/wm.h"

static const display_metrics_t display_presets[DISPLAY_SCALE_COUNT] = {
    {
        DISPLAY_SCALE_SMALL, 1, 1, 8, 16, 8, 24, 96, 60, 24, 32, 24, 24,
        800, 600, 0
    },
    {
        DISPLAY_SCALE_NORMAL, 5, 4, 10, 20, 10, 30, 120, 75, 30, 40, 30, 30,
        800, 600, 0
    },
    {
        DISPLAY_SCALE_LARGE, 3, 2, 12, 24, 12, 36, 144, 90, 36, 48, 36, 36,
        1024, 768, 0
    }
};

static display_metrics_t current_metrics;
static int display_initialized = 0;

static int display_mode_supports(const display_metrics_t* metrics) {
    vesa_mode_t* mode = vesa_get_mode();

    return metrics && mode && mode->initialized && vesa_has_backbuffer() &&
           mode->width >= metrics->min_width &&
           mode->height >= metrics->min_height;
}

static int display_refresh_scene(void) {
    if (desktop_get_mode() == DESKTOP_MODE_MODERN && wm_is_active()) {
        return wm_reflow_display();
    }
    if (desktop_is_active()) {
        desktop_draw();
        return OK;
    }
    taskbar_draw();
    return OK;
}

int display_init(void) {
    LOG_INFO("GUI", "Inicializando metricas de layout e escala");

    current_metrics = display_presets[DISPLAY_SCALE_NORMAL];
    display_initialized = 1;
    if (!display_mode_supports(&current_metrics)) {
        current_metrics.available = 0;
        LOG_ERROR("GUI", "VESA, backbuffer ou area insuficiente para layout");
        return ERR_UNAVAILABLE;
    }

    current_metrics.available = 1;
    LOG_INFO("GUI", "Metricas de layout inicializadas em escala normal");
    return OK;
}

int display_get_metrics(display_metrics_t* metrics) {
    if (!metrics) {
        LOG_ERROR("GUI", "Destino de metricas de display nulo");
        return ERR_NULL;
    }
    if (!display_initialized) {
        LOG_ERROR("GUI", "Metricas consultadas antes da inicializacao");
        return ERR_STATE;
    }

    *metrics = current_metrics;
    return OK;
}

int display_apply_scale(display_scale_t scale) {
    display_metrics_t previous;
    int result;

    if (!display_initialized) {
        LOG_ERROR("GUI", "Escala alterada antes da inicializacao");
        return ERR_STATE;
    }
    if (scale < DISPLAY_SCALE_SMALL || scale >= DISPLAY_SCALE_COUNT) {
        LOG_ERROR("GUI", "Escala de display invalida");
        return ERR_INVALID;
    }
    if (!display_mode_supports(&display_presets[scale])) {
        LOG_WARN("GUI", "VESA, backbuffer ou area insuficiente para escala");
        return ERR_OVERFLOW;
    }
    if (current_metrics.scale == scale && current_metrics.available) return OK;

    previous = current_metrics;
    current_metrics = display_presets[scale];
    current_metrics.available = 1;
    result = display_refresh_scene();
    if (result != OK) {
        current_metrics = previous;
        (void)display_refresh_scene();
        LOG_ERROR("GUI", "Falha ao reorganizar cena; escala restaurada");
        return result;
    }

    LOG_INFO("GUI", scale == DISPLAY_SCALE_SMALL ? "Escala pequena aplicada" :
                    scale == DISPLAY_SCALE_NORMAL ? "Escala normal aplicada" :
                    "Escala grande aplicada");
    return OK;
}

int display_parse_scale(const char* name, display_scale_t* scale) {
    if (!name || !scale) {
        LOG_ERROR("GUI", "Entrada nula ao interpretar escala");
        return ERR_NULL;
    }
    if (kstrcmp(name, "pequena") == 0) {
        *scale = DISPLAY_SCALE_SMALL;
        return OK;
    }
    if (kstrcmp(name, "normal") == 0) {
        *scale = DISPLAY_SCALE_NORMAL;
        return OK;
    }
    if (kstrcmp(name, "grande") == 0) {
        *scale = DISPLAY_SCALE_LARGE;
        return OK;
    }

    LOG_WARN("GUI", "Nome de escala invalido");
    return ERR_INVALID;
}

const char* display_scale_name(display_scale_t scale) {
    if (scale == DISPLAY_SCALE_SMALL) return "pequena";
    if (scale == DISPLAY_SCALE_NORMAL) return "normal";
    if (scale == DISPLAY_SCALE_LARGE) return "grande";
    LOG_ERROR("GUI", "Nome solicitado para escala invalida");
    return "invalida";
}

uint32_t display_scale_px(uint32_t base_value) {
    uint32_t numerator;

    if (!display_initialized || !current_metrics.available ||
        !current_metrics.factor_denominator) {
        return base_value;
    }
    numerator = (uint32_t)base_value * current_metrics.factor_numerator;
    return (numerator + current_metrics.factor_denominator - 1U) /
           current_metrics.factor_denominator;
}
