#ifndef STRING_H
#define STRING_H

#include "types.h"

void kmemset(void* dst, uint8_t val, uint32_t size);
void kmemcpy(void* dst, const void* src, uint32_t size);
int kstrcmp(const char* a, const char* b);
uint32_t kstrlen(const char* str);

#endif
