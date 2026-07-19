#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

typedef void (*keyboard_callback_t)(uint8_t scancode);

void keyboard_init(void);
void keyboard_handler(registers_t* regs);
keyboard_callback_t keyboard_set_callback(keyboard_callback_t cb);
char scancode_to_ascii(uint8_t scancode);

#endif
