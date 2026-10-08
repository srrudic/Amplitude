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
    /* Streams only, and optional: the title of what is playing, when it has
     * changed since the last call (UTF-8). Returns 1 if `out` was filled. */
    int    (*take_title)(void *state, char *out, size_t size);
} Codec;

/* Opens `path` with whichever decoder recognises it. Returns 0 if none does. */
int codec_open(const char *path, Codec *codec);

int codec_open_vorbis(const char *path, Codec *codec);
int codec_open_opus(const char *path, Codec *codec);
int codec_open_aac(const char *path, Codec *codec);     /* MP4/M4A and raw ADTS */
int codec_open_module(const char *path, Codec *codec);  /* MOD, XM, S3M, IT */

/* Decoding from a stream (an internet radio station) rather than a file.
 * The input is fetched through `feed`, which returns the bytes it could
 * supply, fewer than asked when no more are to be had for now; reading then
 * stops short and can be taken up again later. Such a codec has no length
 * and cannot seek. Ogg stations start a new logical stream for every song,
 * which is where their titles come from. */
typedef size_t (*CodecFeed)(void *user, void *out, size_t size);
int codec_open_aac_stream(CodecFeed feed, void *user, Codec *codec);     /* raw ADTS frames */
int codec_open_vorbis_stream(CodecFeed feed, void *user, Codec *codec);
int codec_open_opus_stream(CodecFeed feed, void *user, Codec *codec);

/* "Artist - Title" from whichever of the two is given (either may be NULL). */
void codec_format_title(char *out, size_t size, const char *artist, const char *title);

#endif
