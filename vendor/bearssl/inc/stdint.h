#ifndef ZEPHYROS_BEARSSL_STDINT_H
#define ZEPHYROS_BEARSSL_STDINT_H

#include "types.h"

/* BearSSL uses uintptr_t for alignment checks in its i15 RSA backend. */
typedef uint32_t uintptr_t;
typedef int32_t intptr_t;

#endif
