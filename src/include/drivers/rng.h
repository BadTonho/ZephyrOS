#ifndef RNG_H
#define RNG_H

#include "types.h"

typedef struct {
    uint8_t initialized;
    uint8_t cpuid_available;
    uint8_t rdrand_available;
    uint32_t words_generated;
    uint32_t hardware_failures;
    int last_error;
} rng_status_t;

int rng_init(void);
int rng_get_bytes(uint8_t* output, uint32_t length);
int rng_get_status(rng_status_t* output);
int rng_validate_state(void);

#endif
