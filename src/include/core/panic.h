#ifndef PANIC_H
#define PANIC_H

#include "types.h"

void panic(const char* message);
void panic_memory(const char* message, uint32_t mmap_entries,
                  uint32_t total_memory, uint32_t free_memory,
                  uint32_t free_pages);
void panic_halt(void);

#endif
