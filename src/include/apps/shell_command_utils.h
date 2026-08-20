#ifndef SHELL_COMMAND_UTILS_H
#define SHELL_COMMAND_UTILS_H

#include "types.h"

void shell_command_uppercase(char* text);
void shell_command_print_num(uint32_t value);
uint32_t shell_command_parse_number(const char* text);
void shell_command_print_hex(uint32_t value, uint32_t digits);

int shell_command_args_equal(const char* args, const char* expected);
const char* shell_command_match_subcommand(const char* args,
                                           const char* subcommand);
int shell_command_read_single_arg(const char* args, char* output,
                                  uint32_t output_size);
int shell_command_read_token(const char** cursor, char* output,
                             uint32_t output_size);
int shell_command_read_two_args(const char* args, char* first,
                                uint32_t first_size, char* second,
                                uint32_t second_size);
int shell_command_read_four_args(const char* args, char* first,
                                 uint32_t first_size, char* second,
                                 uint32_t second_size, char* third,
                                 uint32_t third_size, char* fourth,
                                 uint32_t fourth_size);

#endif
