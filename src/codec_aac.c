/* AAC via libfaad2, from an MP4/M4A container (demuxed with minimp4), a raw
 * ADTS file, or ADTS frames arriving over the network. */
#include "codec.h"
#include "platform.h"

#include "../third_party/minimp4.h"

#include <neaacdec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADTS_HEADER_SIZE 7
#define ADTS_MAX_FRAME   8191       /* the length field has 13 bits */
#define MAX_BAD_FRAMES   32
#define MAX_RESYNC_BYTES (64 * 1024)    /* searched for a frame before a stream is given up */

typedef struct {
    FILE *file;
    NeAACDecHandle decoder;
    int channels, rate;

    /* MP4 */
    int is_mp4;
    MP4D_demux_t mp4;
    unsigned track, sample, sample_count, timescale;

    /* ADTS */
    long data_start;            /* first frame, after any ID3v2 tag */
    long next_frame;            /* file offset of the next frame */
    unsigned frame_samples;     /* output frames per AAC frame: 1024, or 2048 with SBR */

    /* Network stream */
    CodecFeed feed;
    void *feed_user;
    size_t fed;                 /* bytes of a->frame filled so far */

    unsigned char *frame;       /* compressed frame being decoded */
    size_t frame_capacity;
    const short *pcm;           /* decoded output not yet handed out */
    size_t pcm_frames, pcm_pos;
} Aac;

static int mp4_read(int64_t offset, void *buffer, size_t size, void *token)
{
    FILE *f = token;

    return fseek(f, (long)offset, SEEK_SET) != 0 || fread(buffer, 1, size, f) != size;
}

static int reserve_frame(Aac *a, size_t size)
{
    unsigned char *grown;

    if (size <= a->frame_capacity)
        return 1;
    grown = realloc(a->frame, size);
    if (!grown)
        return 0;
    a->frame = grown;
    a->frame_capacity = size;
    return 1;
}

/* Returns the length of the ADTS frame at `offset` (0 if there is none). */
static size_t adts_frame_size(FILE *f, long offset, unsigned char header[ADTS_HEADER_SIZE])
{
    size_t size;

    if (fseek(f, offset, SEEK_SET) != 0 || fread(header, 1, ADTS_HEADER_SIZE, f) != ADTS_HEADER_SIZE)
        return 0;
    if (header[0] != 0xFF || (header[1] & 0xF6) != 0xF0)
        return 0;
    size = (size_t)(header[3] & 3) << 11 | (size_t)header[4] << 3 | header[5] >> 5;
    return size < ADTS_HEADER_SIZE ? 0 : size;
}

static int adts_sync(const unsigned char *p)
{
    return p[0] == 0xFF && (p[1] & 0xF6) == 0xF0;
}

/* Tops a->frame up to `size` bytes from the network. Returns 0 if that much
 * is not to be had now; what did arrive is kept for the next call. */
static int feed_to(Aac *a, size_t size)
{
    if (a->fed < size)
        a->fed += a->feed(a->feed_user, a->frame + a->fed, size - a->fed);
    return a->fed >= size;
}

/* The next frame of a network stream, left at the start of a->frame. A
 * stream may be joined, or resume after lost data, in the middle of a
 * frame, so what looks like a header counts only if another follows where
 * the frame should end. */
static size_t next_stream_frame(Aac *a)
{
    size_t skipped = 0, size;

    for (;;) {
        if (!feed_to(a, ADTS_HEADER_SIZE))
            return 0;
        size = (size_t)(a->frame[3] & 3) << 11 | (size_t)a->frame[4] << 3 | a->frame[5] >> 5;
        if (adts_sync(a->frame) && size >= ADTS_HEADER_SIZE) {
            if (!feed_to(a, size))
                return 0;
            /* Without enough in hand to see the next header, trust this one. */
            if (!feed_to(a, size + 2) || adts_sync(a->frame + size))
                return size;
        }
        if (++skipped > MAX_RESYNC_BYTES)
            return 0;
        memmove(a->frame, a->frame + 1, --a->fed);
    }
}

/* Drops the frame just decoded, keeping what was read beyond it. */
static void stream_frame_done(Aac *a, size_t size)
{
    a->fed -= size;
    memmove(a->frame, a->frame + size, a->fed);
}

/* Loads the next compressed frame into a->frame. Returns its size, 0 at the end. */
static size_t next_frame(Aac *a)
{
    unsigned char header[ADTS_HEADER_SIZE];
    unsigned bytes = 0, timestamp, duration;
    size_t size;

    if (a->feed)
        return next_stream_frame(a);
    if (a->is_mp4) {
        long offset;

        if (a->sample >= a->sample_count)
            return 0;
        offset = (long)MP4D_frame_offset(&a->mp4, a->track, a->sample++, &bytes, &timestamp, &duration);
        if (!bytes || !reserve_frame(a, bytes) || fseek(a->file, offset, SEEK_SET) != 0 ||
            fread(a->frame, 1, bytes, a->file) != bytes)
            return 0;
        return bytes;
    }

    size = adts_frame_size(a->file, a->next_frame, header);
    if (!size || !reserve_frame(a, size) || fseek(a->file, a->next_frame, SEEK_SET) != 0 ||
        fread(a->frame, 1, size, a->file) != size)
        return 0;
    a->next_frame += (long)size;
    return size;
}

/* Decodes frames until one yields audio. Returns 0 at the end of the stream. */
static int decode_frame(Aac *a)
{
    NeAACDecFrameInfo info;
    int bad = 0;

    for (;;) {
        size_t size = next_frame(a);
        const short *pcm;

        if (!size)
            return 0;
        pcm = NeAACDecDecode(a->decoder, &info, a->frame, (unsigned long)size);
        if (a->feed)
            stream_frame_done(a, size);     /* the output is the decoder's own; the input is done with */
        if (info.error || !pcm || !info.samples || !info.channels) {
            if (info.error && ++bad > MAX_BAD_FRAMES)
                return 0;
            continue;
        }
        if (!a->channels) {
            a->channels = info.channels;
            a->rate = (int)info.samplerate;
        } else if (info.channels != a->channels) {
            return 0;       /* mid-stream format changes are not supported */
        }
        a->pcm = pcm;
        a->pcm_frames = info.samples / info.channels;
        a->pcm_pos = 0;
        return 1;
    }
}

static size_t aac_read(void *state, short *out, size_t frames)
{
    Aac *a = state;
    size_t done = 0;

    while (done < frames) {
        size_t n = a->pcm_frames - a->pcm_pos;

        if (!n) {
            if (!decode_frame(a))
                break;
            continue;
        }
        if (n > frames - done)
            n = frames - done;
        memcpy(out + done * a->channels, a->pcm + a->pcm_pos * a->channels, n * a->channels * sizeof(short));
        a->pcm_pos += n;
        done += n;
    }
    return done;
}

static int aac_seek(void *state, uint64_t frame)
{
    Aac *a = state;
    unsigned char header[ADTS_HEADER_SIZE];
    unsigned bytes, timestamp, duration;

    if (a->feed)
        return 0;       /* what has been played is gone */
    a->pcm_frames = a->pcm_pos = 0;
    if (a->is_mp4) {
        /* Binary search for the last sample starting at or before the target. */
        uint64_t target = frame * a->timescale / (unsigned)a->rate;
        unsigned lo = 0, hi = a->sample_count;

        while (hi - lo > 1) {
            unsigned mid = lo + (hi - lo) / 2;

            MP4D_frame_offset(&a->mp4, a->track, mid, &bytes, &timestamp, &duration);
            if (timestamp <= target)
                lo = mid;
            else
                hi = mid;
        }
        a->sample = lo;
        NeAACDecPostSeekReset(a->decoder, (long)lo);
    } else {
        uint64_t index = frame / a->frame_samples, i;
        long offset = a->data_start;

        for (i = 0; i < index; i++) {
            size_t size = adts_frame_size(a->file, offset, header);

            if (!size)
                break;
            offset += (long)size;
        }
        a->next_frame = offset;
        NeAACDecPostSeekReset(a->decoder, (long)index);
    }
    return 1;
}

static void aac_close(void *state)
{
    Aac *a = state;

    if (a->decoder)
        NeAACDecClose(a->decoder);
    if (a->is_mp4)
        MP4D_close(&a->mp4);
    if (a->file)
        fclose(a->file);
    free(a->frame);
    free(a);
}

static int open_mp4(Aac *a, long file_size, uint64_t *length)
{
    unsigned long rate;
    unsigned char channels;
    unsigned i;

    if (!MP4D_open(&a->mp4, mp4_read, a->file, file_size))
        return 0;
    a->is_mp4 = 1;
    for (i = 0; i < a->mp4.track_count; i++) {
        const MP4D_track_t *track = &a->mp4.track[i];

        if (track->handler_type == MP4D_HANDLER_TYPE_SOUN &&
            track->object_type_indication == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3 &&
            track->dsi && track->dsi_bytes && track->timescale)
            break;
    }
    if (i == a->mp4.track_count)
        return 0;       /* no AAC track (e.g. ALAC, or video only) */
    a->track = i;
    a->sample_count = a->mp4.track[i].sample_count;
    a->timescale = a->mp4.track[i].timescale;
    if (NeAACDecInit2(a->decoder, a->mp4.track[i].dsi, a->mp4.track[i].dsi_bytes, &rate, &channels) < 0)
        return 0;
    *length = ((uint64_t)a->mp4.track[i].duration_hi << 32 | a->mp4.track[i].duration_lo);
    return 1;
}

static int open_adts(Aac *a, uint64_t *frame_count)
{
    unsigned char header[ADTS_HEADER_SIZE + 3];
    unsigned long rate;
    unsigned char channels;
    size_t size;
    long offset;

    /* Skip an ID3v2 tag if present. */
    if (fseek(a->file, 0, SEEK_SET) == 0 && fread(header, 1, 10, a->file) == 10 &&
        memcmp(header, "ID3", 3) == 0)
        a->data_start = 10 + ((long)(header[6] & 0x7F) << 21 | (long)(header[7] & 0x7F) << 14 |
                              (long)(header[8] & 0x7F) << 7 | (long)(header[9] & 0x7F));

    size = adts_frame_size(a->file, a->data_start, header);
    if (!size || !reserve_frame(a, size) || fseek(a->file, a->data_start, SEEK_SET) != 0 ||
        fread(a->frame, 1, size, a->file) != size)
        return 0;
    if (NeAACDecInit(a->decoder, a->frame, (unsigned long)size, &rate, &channels) < 0)
        return 0;
    a->next_frame = a->data_start;

    /* Count the frames to learn the length. */
    for (offset = a->data_start; (size = adts_frame_size(a->file, offset, header)) != 0; offset += (long)size)
        (*frame_count)++;
    return 1;
}

static void configure(Aac *a)
{
    NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(a->decoder);

    config->outputFormat = FAAD_FMT_16BIT;
    config->downMatrix = 1;     /* fold 5.1 down to stereo */
    NeAACDecSetConfiguration(a->decoder, config);
}

int codec_open_aac_stream(CodecFeed feed, void *user, Codec *codec)
{
    Aac *a = calloc(1, sizeof *a);
    unsigned long rate;
    unsigned char channels;
    size_t size;

    if (!a)
        return 0;
    a->feed = feed;
    a->feed_user = user;
    a->decoder = NeAACDecOpen();
    /* Room for the largest frame and the start of the one after it. */
    if (!a->decoder || !reserve_frame(a, ADTS_MAX_FRAME + ADTS_HEADER_SIZE)) {
        aac_close(a);
        return 0;
    }
    configure(a);
    /* The first frame configures the decoder and stays in place to be
     * decoded; that reveals the real output format (see codec_open_aac). */
    size = next_stream_frame(a);
    if (!size || NeAACDecInit(a->decoder, a->frame, (unsigned long)size, &rate, &channels) < 0 ||
        !decode_frame(a) || a->channels > 2) {
        aac_close(a);
        return 0;
    }
    codec->state = a;
    codec->channels = a->channels;
    codec->rate = a->rate;
    codec->length = 0;
    codec->read = aac_read;
    codec->seek = aac_seek;
    codec->close = aac_close;
    return 1;
}

int codec_open_aac(const char *path, Codec *codec)
{
    Aac *a = calloc(1, sizeof *a);
    unsigned char head[8];
    uint64_t length = 0;
    long file_size;
    int ok;

    if (!a)
        return 0;
    a->file = plat_fopen(path, "rb");
    a->decoder = NeAACDecOpen();
    if (!a->file || !a->decoder || fread(head, 1, sizeof head, a->file) != sizeof head ||
        fseek(a->file, 0, SEEK_END) != 0 || (file_size = ftell(a->file)) <= 0) {
        aac_close(a);
        return 0;
    }
    configure(a);

    if (memcmp(head + 4, "ftyp", 4) == 0)
        ok = open_mp4(a, file_size, &length);
    else
        ok = open_adts(a, &length);

    /* Decoding the first frame reveals the real output format; it can
     * differ from the headers when SBR or parametric stereo is in use. */
    if (!ok || !decode_frame(a) || a->channels > 2) {
        aac_close(a);
        return 0;
    }
    a->frame_samples = (unsigned)a->pcm_frames;
    if (a->is_mp4)
        length = length * (unsigned)a->rate / a->timescale;
    else if (length)
        length = (length - 1) * a->frame_samples;   /* the decoder swallows the first frame */

    codec->state = a;
    codec->channels = a->channels;
    codec->rate = a->rate;
    codec->length = length;
    codec->read = aac_read;
    codec->seek = aac_seek;
    codec->close = aac_close;
    return 1;
}
