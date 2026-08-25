#include "types.h"
#include "core/log.h"

void kmemset(void* destination, uint8_t value, uint32_t size) {
    uint8_t* output = (uint8_t*)destination;
    for (uint32_t index = 0U; index < size; index++) output[index] = value;
}

void kmemcpy(void* destination, const void* source, uint32_t size) {
    uint8_t* output = (uint8_t*)destination;
    const uint8_t* input = (const uint8_t*)source;
    for (uint32_t index = 0U; index < size; index++) output[index] = input[index];
}

uint32_t kstrlen(const char* value) {
    uint32_t size = 0U;
    while (value && value[size]) size++;
    return size;
}

int kstrcmp(const char* first, const char* second) {
    while (*first && *first == *second) { first++; second++; }
    return (uint8_t)*first - (uint8_t)*second;
}

/* O loader nao possui log persistente; o diagnostico usa VGA texto local. */
void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}
