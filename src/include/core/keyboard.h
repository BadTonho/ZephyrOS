#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"
#include "drivers/idt.h"

typedef void (*keyboard_callback_t)(uint8_t scancode);
typedef int (*keyboard_focus_cancel_filter_t)(uint8_t scancode);

typedef struct {
    uint32_t queued;
    uint32_t capacity;
    uint32_t dropped;
    uint32_t processed;
    uint32_t peak_queued;
} keyboard_metrics_t;

void keyboard_init(void);
void keyboard_handler(registers_t* regs);
void keyboard_process_events(void);
void keyboard_set_focus_cancel_filter(keyboard_focus_cancel_filter_t filter);
uint8_t keyboard_controller_reset_available(void);
int keyboard_controller_reset(void);
char keyboard_scancode_to_ascii(uint8_t scancode);
char keyboard_scancode_to_ascii_shifted(uint8_t scancode, uint8_t shifted);
void keyboard_get_metrics(keyboard_metrics_t* metrics);

#endif
