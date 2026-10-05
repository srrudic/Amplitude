/* Minimal test support: a CHECK macro, a scratch directory and generators
 * for the input files the tests need. Each test is a standalone program
 * that returns non-zero if any check failed. */
#ifndef TEST_H
#define TEST_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int test_failures, test_checks;

#define CHECK(cond) \
    do { \
        test_checks++; \
        if (!(cond)) { \
            test_failures++; \
            printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

/* Like CHECK for two integers, printing both values when they differ. */
#define CHECK_INT(actual, expected) \
    do { \
        long long a_ = (long long)(actual), e_ = (long long)(expected); \
        test_checks++; \
        if (a_ != e_) { \
            test_failures++; \
            printf("  FAILED %s:%d: %s is %lld, expected %lld\n", __FILE__, __LINE__, #actual, a_, e_); \
        } \
    } while (0)

#define CHECK_STR(actual, expected) \
    do { \
        test_checks++; \
        if (strcmp((actual), (expected)) != 0) { \
            test_failures++; \
            printf("  FAILED %s:%d: %s is \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #actual, \
                   (actual), (expected)); \
        } \
    } while (0)

static char test_dir[256];

/* Creates a scratch directory; test_path() names files inside it. */
static inline void test_begin(const char *name)
{
    snprintf(test_dir, sizeof test_dir, "/tmp/amplitude-%s-XXXXXX", name);
    if (!mkdtemp(test_dir)) {
        perror("mkdtemp");
        exit(2);
    }
}

static inline const char *test_path(const char *file)
{
    static char paths[8][512];
    static int next;
    char *path = paths[next++ % 8];

    snprintf(path, sizeof paths[0], "%s/%s", test_dir, file);
    return path;
}

/* Removes the scratch directory and reports. Use as `return test_end();`. */
static inline int test_end(void)
{
    char command[300];

    snprintf(command, sizeof command, "rm -rf '%s'", test_dir);
    if (system(command) != 0)
        printf("  (could not remove %s)\n", test_dir);
    printf("  %d checks, %d failed\n", test_checks, test_failures);
    return test_failures != 0;
}

static inline void test_write(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");

    if (!f || fwrite(data, 1, size, f) != size) {
        perror(path);
        exit(2);
    }
    fclose(f);
}

/* Growable byte buffer for assembling binary files. */
typedef struct {
    unsigned char *data;
    size_t size, capacity;
} Bytes;

static inline void bytes_add(Bytes *b, const void *data, size_t size)
{
    if (b->size + size > b->capacity) {
        b->capacity = (b->size + size) * 2 + 64;
        b->data = realloc(b->data, b->capacity);
        if (!b->data)
            exit(2);
    }
    if (data)
        memcpy(b->data + b->size, data, size);
    else
        memset(b->data + b->size, 0, size);
    b->size += size;
}

static inline void bytes_str(Bytes *b, const char *s)
{
    bytes_add(b, s, strlen(s));
}

static inline void bytes_u8(Bytes *b, unsigned v)
{
    unsigned char c = (unsigned char)v;

    bytes_add(b, &c, 1);
}

static inline void bytes_le16(Bytes *b, unsigned v)
{
    bytes_u8(b, v);
    bytes_u8(b, v >> 8);
}

static inline void bytes_le32(Bytes *b, unsigned long v)
{
    bytes_le16(b, (unsigned)(v & 0xFFFF));
    bytes_le16(b, (unsigned)(v >> 16));
}

static inline void bytes_be32(Bytes *b, unsigned long v)
{
    bytes_u8(b, (unsigned)(v >> 24));
    bytes_u8(b, (unsigned)(v >> 16));
    bytes_u8(b, (unsigned)(v >> 8));
    bytes_u8(b, (unsigned)v);
}

static inline void bytes_save(Bytes *b, const char *path)
{
    test_write(path, b->data, b->size);
    free(b->data);
    memset(b, 0, sizeof *b);
}

/* A 44.1 kHz 16-bit stereo sine tone. Keep `amplitude` tiny (below 100) for
 * anything that is actually played, so tests are inaudible. */
static inline void test_write_wav(const char *path, double seconds, double hz, int amplitude)
{
    Bytes b = { NULL, 0, 0 };
    unsigned long frames = (unsigned long)(seconds * 44100.0), i;

    bytes_str(&b, "RIFF");
    bytes_le32(&b, 36 + frames * 4);
    bytes_str(&b, "WAVEfmt ");
    bytes_le32(&b, 16);
    bytes_le16(&b, 1);          /* PCM */
    bytes_le16(&b, 2);
    bytes_le32(&b, 44100);
    bytes_le32(&b, 44100 * 4);
    bytes_le16(&b, 4);
    bytes_le16(&b, 16);
    bytes_str(&b, "data");
    bytes_le32(&b, frames * 4);
    for (i = 0; i < frames; i++) {
        int s = (int)(amplitude * sin((double)i * 2.0 * 3.14159265358979 * hz / 44100.0));

        bytes_le16(&b, (unsigned)s & 0xFFFF);
        bytes_le16(&b, (unsigned)s & 0xFFFF);
    }
    bytes_save(&b, path);
}

/* A minimal 4-channel ProTracker module: one looping square-wave sample,
 * one pattern with a single note, played at one tick per row (64 rows of
 * 20 ms, about 1.3 seconds). */
static inline void test_write_mod(const char *path, const char *title)
{
    Bytes b = { NULL, 0, 0 };
    char name[22];
    int i;

    memset(name, 0, sizeof name);
    strncpy(name, title, 20);
    bytes_add(&b, name, 20);
    for (i = 0; i < 31; i++) {
        bytes_add(&b, NULL, 22);                    /* sample name */
        bytes_u8(&b, 0);
        bytes_u8(&b, i == 0 ? 32 : 0);              /* length in words */
        bytes_u8(&b, 0);                            /* finetune */
        bytes_u8(&b, i == 0 ? 64 : 0);              /* volume */
        bytes_u8(&b, 0);
        bytes_u8(&b, 0);                            /* loop start */
        bytes_u8(&b, 0);
        bytes_u8(&b, i == 0 ? 32 : 1);              /* loop length */
    }
    bytes_u8(&b, 1);                                /* song length */
    bytes_u8(&b, 127);
    bytes_add(&b, NULL, 128);                       /* pattern order: all pattern 0 */
    bytes_str(&b, "M.K.");
    for (i = 0; i < 64 * 4; i++) {
        if (i == 0) {                               /* row 0, channel 0: note C-2, sample 1 */
            bytes_u8(&b, 0x01);
            bytes_u8(&b, 0xAC);
            bytes_u8(&b, 0x10);
            bytes_u8(&b, 0x00);
        } else if (i == 1) {                        /* row 0, channel 1: F01 = one tick per row */
            bytes_u8(&b, 0x00);
            bytes_u8(&b, 0x00);
            bytes_u8(&b, 0x0F);
            bytes_u8(&b, 0x01);
        } else {
            bytes_add(&b, NULL, 4);
        }
    }
    for (i = 0; i < 64; i++)
        bytes_u8(&b, i < 32 ? 0x02 : 0xFE);         /* very quiet square wave */
    bytes_save(&b, path);
}

#endif
