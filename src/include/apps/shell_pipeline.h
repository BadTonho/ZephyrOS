#ifndef SHELL_PIPELINE_H
#define SHELL_PIPELINE_H

#include "types.h"

int shell_pipeline_try_execute(const char* input, uint8_t* handled);
int shell_pipeline_is_active(void);
int shell_pipeline_read(void* buffer, uint32_t size, uint32_t* bytes_read);
int shell_pipeline_write(const char* text, uint8_t color);
void shell_pipeline_print_num(uint32_t value);
int shell_pipeline_self_test(void);

#endif
