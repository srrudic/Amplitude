/* Decoders for formats miniaudio does not handle itself. Each one delivers
 * interleaved 16-bit samples; audio.c wraps them as a miniaudio backend. */
#ifndef CODEC_H
#define CODEC_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *state;
    int channels;           /* 1 or 2 */
    int rate;               /* Hz */
    uint64_t length;        /* frames, 0 if unknown */
    /* Decodes up to `frames` frames; returns the number written, 0 at the end. */
    size_t (*read)(void *state, short *out, size_t frames);
    /* Returns 1 on success. */
    int    (*seek)(void *state, uint64_t frame);
    void   (*close)(void *state);
} Codec;

/* Opens `path` with whichever decoder recognises it. Returns 0 if none does. */
int codec_open(const char *path, Codec *codec);

int codec_open_vorbis(const char *path, Codec *codec);
int codec_open_opus(const char *path, Codec *codec);
int codec_open_aac(const char *path, Codec *codec);     /* MP4/M4A and raw ADTS */
int codec_open_module(const char *path, Codec *codec);  /* MOD, XM, S3M, IT */

#endif
