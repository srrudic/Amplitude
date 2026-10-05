/* Read-only ZIP archive access (stored and deflated entries). */
#ifndef ZIP_H
#define ZIP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    unsigned char *data;
    size_t size;
    size_t dir_offset;
    unsigned entry_count;
} ZipArchive;

static inline unsigned rd16(const unsigned char *p)
{
    return p[0] | p[1] << 8;
}

static inline uint32_t rd32(const unsigned char *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

int  zip_open(ZipArchive *zip, const char *path);
void zip_close(ZipArchive *zip);

/* Extracts the first entry whose file name (ignoring any directory part and
 * letter case) equals `name`. Returns a malloc'd buffer with a terminating
 * NUL appended after *size bytes, or NULL. */
unsigned char *zip_read(const ZipArchive *zip, const char *name, size_t *size);

#endif
