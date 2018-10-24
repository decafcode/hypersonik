#pragma once

#include <stddef.h>
#include <stdint.h>

#define containerof(ptr, outer_t, member) \
        ((void *) (((uint8_t *) ptr) - offsetof(outer_t, member)))
#define lengthof(x) (sizeof(x) / sizeof(x[0]))
