#ifndef DISPLAY_H
#define DISPLAY_H

#include "types.h"

typedef enum {
    DISPLAY_SCALE_SMALL = 0,
    DISPLAY_SCALE_NORMAL,
    DISPLAY_SCALE_LARGE,
    DISPLAY_SCALE_COUNT
} display_scale_t;

typedef struct {
    display_scale_t scale;
    uint16_t factor_numerator;
    uint16_t factor_denominator;
    uint16_t font_width;
    uint16_t font_height;
    uint16_t spacing;
    uint16_t taskbar_height;
    uint16_t taskbar_side_width;
    uint16_t button_min_width;
    uint16_t button_min_height;
    uint16_t icon_size;
    uint16_t title_bar_height;
    uint16_t row_height;
    uint16_t min_width;
    uint16_t min_height;
    uint8_t available;
} display_metrics_t;

int display_init(void);
int display_get_metrics(display_metrics_t* metrics);
int display_apply_scale(display_scale_t scale);
int display_parse_scale(const char* name, display_scale_t* scale);
const char* display_scale_name(display_scale_t scale);
uint32_t display_scale_px(uint32_t base_value);

#endif
