#ifndef SHELL_DISPATCH_H
#define SHELL_DISPATCH_H

#include "types.h"

typedef void (*shell_dispatch_handler_t)(const char* arguments);

typedef enum {
    SHELL_DISPATCH_FLAG_NONE = 0x00,
    SHELL_DISPATCH_FLAG_MAY_BLOCK = 0x01,
    SHELL_DISPATCH_FLAG_OPENS_SCENE = 0x02
} shell_dispatch_flags_t;

typedef struct {
    const char* name;
    shell_dispatch_handler_t handler;
    uint8_t flags;
} shell_dispatch_entry_t;

int shell_dispatch_execute(const char* input);

#endif
