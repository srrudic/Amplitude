/* Ogg Vorbis via stb_vorbis. */
#include "codec.h"
#include "platform.h"

#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb_vorbis.c"

typedef struct {
    stb_vorbis *vorbis;
    int channels;
} Vorbis;

static size_t vorbis_read(void *state, short *out, size_t frames)
{
    Vorbis *v = state;
    size_t done = 0;

    while (done < frames) {
        int got = stb_vorbis_get_samples_short_interleaved(v->vorbis, v->channels, out + done * v->channels,
                                                           (int)(frames - done) * v->channels);

        if (got <= 0)
            break;
        done += (size_t)got;
    }
    return done;
}

static int vorbis_seek(void *state, uint64_t frame)
{
    Vorbis *v = state;

    return stb_vorbis_seek(v->vorbis, (unsigned)frame);
}

static void vorbis_close(void *state)
{
    Vorbis *v = state;

    stb_vorbis_close(v->vorbis);
    free(v);
}

int codec_open_vorbis(const char *path, Codec *codec)
{
    Vorbis *v = calloc(1, sizeof *v);
    FILE *f = plat_fopen(path, "rb");
    stb_vorbis_info info;

    if (!v || !f) {
        if (f)
            fclose(f);
        free(v);
        return 0;
    }
    /* From here the file is stb_vorbis's: it closes it when the stream is
     * closed, and also when opening fails, so it must not be closed again. */
    v->vorbis = stb_vorbis_open_file(f, 1, NULL, NULL);
    if (!v->vorbis) {
        free(v);
        return 0;
    }
    info = stb_vorbis_get_info(v->vorbis);
    /* stb_vorbis downmixes surround streams to stereo for us. */
    v->channels = info.channels > 2 ? 2 : info.channels;

    codec->state = v;
    codec->channels = v->channels;
    codec->rate = (int)info.sample_rate;
    codec->length = stb_vorbis_stream_length_in_samples(v->vorbis);
    codec->read = vorbis_read;
    codec->seek = vorbis_seek;
    codec->close = vorbis_close;
    return 1;
}

/* --- From a stream ---------------------------------------------------------------
 * stb_vorbis's "push" interface is handed the bytes as they arrive. It knows
 * nothing of a station starting a new logical stream for each song, whose
 * headers (and possibly different settings) it would take for damaged audio;
 * so the Ogg pages are followed here as well, and at a page that begins a
 * stream the decoder is closed and opened afresh. */

#define STREAM_BUFFER   (128 * 1024)    /* several times the largest headers */
#define OGG_HEADER      27
#define OGG_BEGINS      0x02            /* header flag: first page of a logical stream */

typedef struct {
    CodecFeed feed;
    void *feed_user;
    stb_vorbis *vorbis;
    int channels, rate;
    int dead;                   /* the station changed format; nothing more is decoded */

    unsigned char *data;        /* received and not yet decoded */
    size_t have;
    size_t safe;                /* how much of it belongs to the stream being decoded
                                 * (can run past `have`: the rest of a page still to come) */
    int starting;               /* the next page may begin a stream without it being a change */
    int chained;                /* a new stream begins at data[safe] */

    float **pcm;                /* decoded, not yet handed out */
    int pcm_frames, pcm_pos;
    char title[256];
    int title_new;
} VorbisStream;

static size_t stream_fill(VorbisStream *v)
{
    size_t got = v->feed(v->feed_user, v->data + v->have, STREAM_BUFFER - v->have);

    v->have += got;
    return got;
}

static void stream_consume(VorbisStream *v, size_t size)
{
    v->have -= size;
    v->safe -= size;
    memmove(v->data, v->data + size, v->have);
}

/* Follows the pages as far as they have arrived. Returns how many bytes
 * may be given to the decoder. */
static size_t stream_scan(VorbisStream *v)
{
    while (!v->chained && v->safe + OGG_HEADER <= v->have) {
        const unsigned char *page = v->data + v->safe;
        size_t size = OGG_HEADER + page[26], i;

        if (memcmp(page, "OggS", 4) != 0) {
            v->safe++;          /* lost our place: look for the next page */
            continue;
        }
        if (v->safe + size > v->have)
            break;
        if ((page[5] & OGG_BEGINS) && !v->starting) {
            v->chained = 1;
            break;
        }
        for (i = 0; i < page[26]; i++)
            size += page[OGG_HEADER + i];
        v->safe += size;
        v->starting = 0;
    }
    return v->safe < v->have ? v->safe : v->have;
}

/* "KEY=value": the value if the key is the one asked for. */
static const char *comment_value(const char *entry, const char *key)
{
    for (; *key; entry++, key++)
        if ((*entry >= 'a' && *entry <= 'z' ? *entry - 32 : *entry) != *key)
            return NULL;
    return *entry == '=' ? entry + 1 : NULL;
}

/* Opens the decoder on the stream that starts at the front of the buffer.
 * Returns 1, 0 if more data is needed first, -1 if it cannot be decoded. */
static int stream_start(VorbisStream *v)
{
    const char *artist = NULL, *title = NULL;
    stb_vorbis_comment comment;
    stb_vorbis_info info;
    int used = 0, error = 0, i;

    v->starting = 1;
    v->vorbis = stb_vorbis_open_pushdata(v->data, (int)stream_scan(v), &used, &error, NULL);
    if (!v->vorbis)
        return error == VORBIS_need_more_data ? 0 : -1;
    stream_consume(v, (size_t)used);
    info = stb_vorbis_get_info(v->vorbis);
    if (info.channels > 2 || (v->rate && (info.channels != v->channels || (int)info.sample_rate != v->rate)))
        return -1;
    v->channels = info.channels;
    v->rate = (int)info.sample_rate;

    comment = stb_vorbis_get_comment(v->vorbis);
    for (i = 0; i < comment.comment_list_length; i++) {
        const char *value;

        if ((value = comment_value(comment.comment_list[i], "ARTIST")) != NULL)
            artist = value;
        else if ((value = comment_value(comment.comment_list[i], "TITLE")) != NULL)
            title = value;
    }
    codec_format_title(v->title, sizeof v->title, artist, title);
    v->title_new = v->title[0] != '\0';
    return 1;
}

/* Decodes until there is audio to hand out. Returns 0 when none can be had
 * for now. */
static int stream_decode(VorbisStream *v)
{
    for (;;) {
        size_t usable;
        int used, channels;

        if (v->dead)
            return 0;
        if (!v->vorbis) {
            int started = stream_start(v);

            if (started < 0)
                v->dead = 1;
            else if (!started && (v->have == STREAM_BUFFER || !stream_fill(v)))
                return 0;
            continue;
        }
        usable = stream_scan(v);
        if (!usable && v->chained) {            /* the next song: its own headers follow */
            stb_vorbis_close(v->vorbis);
            v->vorbis = NULL;
            v->chained = 0;
            v->safe = 0;
            continue;
        }
        used = usable ? stb_vorbis_decode_frame_pushdata(v->vorbis, v->data, (int)usable, &channels, &v->pcm,
                                                         &v->pcm_frames) : 0;
        if (!used) {
            if (v->chained) {
                stream_consume(v, usable);      /* the odd bytes left of the old stream */
            } else {
                if (v->have == STREAM_BUFFER)
                    v->have = v->safe = 0;      /* nothing decodable in all of it */
                if (!stream_fill(v))
                    return 0;
            }
            continue;
        }
        stream_consume(v, (size_t)used);
        v->pcm_pos = 0;
        if (v->pcm_frames > 0)
            return 1;
    }
}

static size_t stream_read(void *state, short *out, size_t frames)
{
    VorbisStream *v = state;
    size_t done = 0;
    int ch;

    while (done < frames) {
        if (v->pcm_pos >= v->pcm_frames) {
            v->pcm_frames = 0;
            if (!stream_decode(v))
                break;
            continue;
        }
        for (ch = 0; ch < v->channels; ch++) {
            float sample = v->pcm[ch][v->pcm_pos] * 32768.0f;

            out[done * v->channels + ch] = (short)(sample > 32767.0f ? 32767.0f : sample < -32768.0f ? -32768.0f : sample);
        }
        v->pcm_pos++;
        done++;
    }
    return done;
}

static int stream_seek(void *state, uint64_t frame)
{
    (void)state;
    (void)frame;
    return 0;
}

static int stream_take_title(void *state, char *out, size_t size)
{
    VorbisStream *v = state;

    if (!v->title_new)
        return 0;
    v->title_new = 0;
    snprintf(out, size, "%s", v->title);
    return 1;
}

static void stream_close(void *state)
{
    VorbisStream *v = state;

    if (v->vorbis)
        stb_vorbis_close(v->vorbis);
    free(v->data);
    free(v);
}

int codec_open_vorbis_stream(CodecFeed feed, void *user, Codec *codec)
{
    VorbisStream *v = calloc(1, sizeof *v);
    int started = 0;

    if (!v)
        return 0;
    v->feed = feed;
    v->feed_user = user;
    v->data = malloc(STREAM_BUFFER);
    while (v->data && (started = stream_start(v)) == 0)
        if (v->have == STREAM_BUFFER || !stream_fill(v))
            break;
    if (started <= 0) {
        stream_close(v);
        return 0;
    }
    codec->state = v;
    codec->channels = v->channels;
    codec->rate = v->rate;
    codec->length = 0;
    codec->read = stream_read;
    codec->seek = stream_seek;
    codec->close = stream_close;
    codec->take_title = stream_take_title;
    return 1;
}
