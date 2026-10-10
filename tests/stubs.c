/* Stand-ins for the platform functions that portable modules call, so unit
 * tests need neither a display nor the real platform layer. */
#include "platform.h"

#include <stdio.h>
#include <time.h>

FILE *plat_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

uint32_t plat_ticks_ms(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
