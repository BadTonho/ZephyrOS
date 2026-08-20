#ifndef SHELL_INPUT_H
#define SHELL_INPUT_H

#include "types.h"

typedef enum {
    SHELL_INPUT_EVENT_NONE = 0,
    SHELL_INPUT_EVENT_COMMAND_READY = 1
} shell_input_event_t;

void shell_input_init(void);
void shell_input_reset(void);
void shell_input_cancel_extended(void);
void shell_input_reset_modifiers(void);
shell_input_event_t shell_input_handle_key(uint8_t scancode,
                                           uint8_t window_manager_active,
                                           uint8_t input_blocked);
const char* shell_input_get_buffer(void);
void shell_input_print_prompt(uint8_t window_manager_active);
void shell_input_resume_terminal(uint8_t window_manager_active);

#endif
