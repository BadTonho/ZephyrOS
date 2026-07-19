#ifndef SHELL_H
#define SHELL_H

#include "types.h"

#define SHELL_BUFFER_SIZE 256
#define SHELL_MAX_ARGS 16
#define SHELL_PROMPT "minios> "

void shell_init(void);
void shell_handle_key(uint8_t scancode);
void shell_print_prompt(void);
int  shell_process_command(const char* input);

#endif
