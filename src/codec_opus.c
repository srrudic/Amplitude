/* Ogg Opus via libopusfile. Opus always decodes at 48 kHz. */
#include "codec.h"
#include "platform.h"

#include <opusfile.h>
#include <stdio.h>

/* Plain stdio callbacks: opusfile's own file layer needs wide-character
 * APIs that Windows 9x lacks. */
static int file_read(void *stream, unsigned char *ptr, int bytes)
{
    return (int)fread(ptr, 1, (size_t)bytes, stream);
}

static int file_seek(void *stream, opus_int64 offset, int whence)
{
    return fseek(stream, (long)offset, whence);
}

static opus_int64 file_tell(void *stream)
{
    return ftell(stream);
}

static int file_close(void *stream)
{
    return fclose(stream);
}

/* opusfile's stream.c is left out of the build for the same reason, so the
 * two entry points the library still references are stubbed here. */
void *op_fopen(OpusFileCallbacks *cb, const char *path, const char *mode)
{
    (void)cb;
    (void)path;
    (void)mode;
    return NULL;
}

void *op_mem_stream_create(OpusFileCallbacks *cb, const unsigned char *data, size_t size)
{
    (void)cb;
    (void)data;
    (void)size;
    return NULL;
}

static size_t opus_read(void *state, short *out, size_t frames)
{
    size_t done = 0;

    while (done < frames) {
        int got = op_read_stereo(state, out + done * 2, (int)(frames - done) * 2);

        if (got == OP_HOLE)
            continue;       /* gap in the stream: keep going */
        if (got <= 0)
            break;
        done += (size_t)got;
    }
    return done;
}

static int opus_seek(void *state, uint64_t frame)
{
    return op_pcm_seek(state, (ogg_int64_t)frame) == 0;
}

static void opus_close(void *state)
{
    op_free(state);
}

int codec_open_opus(const char *path, Codec *codec)
{
    static const OpusFileCallbacks callbacks = { file_read, file_seek, file_tell, file_close };
    FILE *f = plat_fopen(path, "rb");
    OggOpusFile *opus;
    ogg_int64_t total;

    if (!f)
        return 0;
    opus = op_open_callbacks(f, &callbacks, NULL, 0, NULL);
    if (!opus) {
        fclose(f);
        return 0;
    }
    total = op_pcm_total(opus, -1);

    codec->state = opus;
    codec->channels = 2;
    codec->rate = 48000;
    codec->length = total > 0 ? (uint64_t)total : 0;
    codec->read = opus_read;
    codec->seek = opus_seek;
    codec->close = opus_close;
    return 1;
}
