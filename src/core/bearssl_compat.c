#include "types.h"

/* BearSSL is compiled with -nostdinc/-nodefaultlibs.  These symbols keep its
 * upstream implementation independent from a hosted C runtime while using
 * the same byte-wise semantics as the kernel string helpers. */
void* memcpy(void* destination, const void* source, size_t length) {
    uint8_t* out = (uint8_t*)destination;
    const uint8_t* in = (const uint8_t*)source;

    for (size_t index = 0; index < length; index++) out[index] = in[index];
    return destination;
}

void* memmove(void* destination, const void* source, size_t length) {
    uint8_t* out = (uint8_t*)destination;
    const uint8_t* in = (const uint8_t*)source;

    if (out < in) {
        for (size_t index = 0; index < length; index++) out[index] = in[index];
    } else if (out > in) {
        for (size_t index = length; index; index--) {
            out[index - 1U] = in[index - 1U];
        }
    }
    return destination;
}

void* memset(void* destination, int value, size_t length) {
    uint8_t* out = (uint8_t*)destination;

    for (size_t index = 0; index < length; index++) {
        out[index] = (uint8_t)value;
    }
    return destination;
}

int memcmp(const void* first, const void* second, size_t length) {
    const uint8_t* left = (const uint8_t*)first;
    const uint8_t* right = (const uint8_t*)second;

    for (size_t index = 0; index < length; index++) {
        if (left[index] != right[index]) {
            return (int)left[index] - (int)right[index];
        }
    }
    return 0;
}

size_t strlen(const char* text) {
    size_t length = 0;

    if (!text) return 0;
    while (text[length]) length++;
    return length;
}
