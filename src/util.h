/* Small helpers shared by the portable code. */
#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

#define PI_F 3.14159265f
#define ARRAY_LEN(a) ((int)(sizeof (a) / sizeof (a)[0]))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))

/* The part of a path after the last / or \. */
static inline const char *path_basename(const char *path)
{
    const char *name = path, *p;

    for (p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            name = p + 1;
    return name;
}

/* Does the path end in `ext` (given in lower case, with its dot)? ASCII
 * upper case in the path matches too. */
static inline int path_has_extension(const char *path, const char *ext)
{
    size_t len = 0, ext_len = 0, i;

    while (path[len])
        len++;
    while (ext[ext_len])
        ext_len++;
    if (len < ext_len)
        return 0;
    for (i = 0; i < ext_len; i++) {
        int ch = path[len - ext_len + i];

        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
        if (ch != ext[i])
            return 0;
    }
    return 1;
}

#endif
