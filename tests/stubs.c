/* Stand-ins for the platform functions that portable modules call, so unit
 * tests need neither a display nor the real platform layer. */
#include "platform.h"

#include <stdio.h>

FILE *plat_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}
