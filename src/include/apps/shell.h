#ifndef SHELL_H
#define SHELL_H

#include "types.h"

#define SHELL_BUFFER_SIZE 256
#define SHELL_MAX_ARGS 16
#define SHELL_PROMPT "zephyr> "

void shell_init(void);
void shell_handle_key(uint8_t scancode);
void shell_handle_app_request(uint32_t request);
void shell_report_user_test_result(void);
void shell_report_app_loader_result(void);
void shell_print_prompt(void);
int  shell_process_command(const char* input);

#endif
