#ifndef ZEPHYROS_BEARSSL_STDDEF_H
#define ZEPHYROS_BEARSSL_STDDEF_H

#include "types.h"

/* The freestanding profile has no hosted stddef.h, but BearSSL relies on
 * offsetof() in generated codec and X.509 state-machine tables. */
#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#endif
