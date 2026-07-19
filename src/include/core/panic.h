#ifndef PANIC_H
#define PANIC_H

#include "types.h"

void panic(const char* message);
void panic_halt(void);

#endif
