/* Ogg Opus via libopusfile. Opus always decodes at 48 kHz. */
#include "codec.h"
#include "platform.h"

#include <opusfile.h>
#include <stdio.h>
#include <stdlib.h>

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

/* --- From a stream ---------------------------------------------------------------
 * opusfile reads a source it cannot seek in as it comes, and follows a
 * station from one song's logical stream to the next by itself. */

typedef struct {
    OggOpusFile *opus;
    CodecFeed feed;
    void *feed_user;
    opus_uint32 serial;         /* of the logical stream whose title was last taken */
    int titled;
    char title[256];
    int title_new;
} OpusStream;

static int feed_read(void *stream, unsigned char *ptr, int bytes)
{
    OpusStream *s = stream;

    /* Nothing to be had looks like the end to opusfile, but it asks again
     * on the next read. */
    return (int)s->feed(s->feed_user, ptr, (size_t)bytes);
}

static void note_title(OpusStream *s)
{
    opus_uint32 serial = op_serialno(s->opus, -1);
    const OpusTags *tags;

    if (s->titled && serial == s->serial)
        return;
    s->titled = 1;
    s->serial = serial;
    tags = op_tags(s->opus, -1);
    if (!tags)
        return;
    codec_format_title(s->title, sizeof s->title, opus_tags_query(tags, "ARTIST", 0),
                       opus_tags_query(tags, "TITLE", 0));
    s->title_new = s->title[0] != '\0';
}

static size_t opus_stream_read(void *state, short *out, size_t frames)
{
    OpusStream *s = state;
    size_t done = 0;

    while (done < frames) {
        int got = op_read_stereo(s->opus, out + done * 2, (int)(frames - done) * 2);

        if (got == OP_HOLE)
            continue;
        if (got <= 0)
            break;
        done += (size_t)got;
    }
    if (done)
        note_title(s);
    return done;
}

static int opus_stream_seek(void *state, uint64_t frame)
{
    (void)state;
    (void)frame;
    return 0;
}

static int opus_stream_take_title(void *state, char *out, size_t size)
{
    OpusStream *s = state;

    if (!s->title_new)
        return 0;
    s->title_new = 0;
    snprintf(out, size, "%s", s->title);
    return 1;
}

static void opus_stream_close(void *state)
{
    OpusStream *s = state;

    if (s->opus)
        op_free(s->opus);
    free(s);
}

int codec_open_opus_stream(CodecFeed feed, void *user, Codec *codec)
{
    static const OpusFileCallbacks callbacks = { feed_read, NULL, NULL, NULL };
    OpusStream *s = calloc(1, sizeof *s);

    if (!s)
        return 0;
    s->feed = feed;
    s->feed_user = user;
    s->opus = op_open_callbacks(s, &callbacks, NULL, 0, NULL);
    if (!s->opus) {
        free(s);
        return 0;
    }
    note_title(s);
    codec->state = s;
    codec->channels = 2;
    codec->rate = 48000;
    codec->length = 0;
    codec->read = opus_stream_read;
    codec->seek = opus_stream_seek;
    codec->close = opus_stream_close;
    codec->take_title = opus_stream_take_title;
    return 1;
}
