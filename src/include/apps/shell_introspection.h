#ifndef SHELL_INTROSPECTION_H
#define SHELL_INTROSPECTION_H

#include "types.h"

#define SHELL_INTROSPECTION_MAX_VALUE 256U

int shell_introspection_read_file(const char* path, uint8_t* buffer,
                                  uint32_t capacity, uint32_t* out_size);
int shell_introspection_find_value(const uint8_t* buffer, uint32_t size,
                                   const char* key, char* value,
                                   uint32_t value_capacity);
int shell_introspection_parse_u32(const char* text, uint32_t* value);
int shell_introspection_parse_hex_u32(const char* text, uint32_t* value);

#endif
