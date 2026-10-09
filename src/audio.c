#include "audio.h"

#include "codec.h"
#include "platform.h"
#include "stream.h"
#include "cd.h"
#include "util.h"

/* Only the device and decoding parts of miniaudio are needed. */
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MINIAUDIO_IMPLEMENTATION
#include "../third_party/miniaudio.h"

/* Everything is converted to one fixed output format by the decoder. */
#define OUT_FORMAT   ma_format_s16
#define OUT_CHANNELS 2
#define OUT_RATE     44100
/* How much audio is handed to the device at a time. The library's default
 * of about 10 ms means some 90 wake-ups a second, each a round trip to the
 * sound server, which was most of what the audio thread cost. At 50 ms the
 * player still reacts at once to the ear and the visualiser stays fluid. */
#define PERIOD_MS    50
/* MP3 has no index: without a table of positions, seeking means decoding
 * from the start of the file to the target, which took most of a second
 * for a long track and was heard as a stutter. The table is made when the
 * file is opened, at the cost of one more quick pass over it. */
#define SEEK_POINTS  256
/* Streams. Playback starts once about two seconds of audio are buffered,
 * pauses to refill when less than one device period's worth is left, and
 * resumes at one second's worth. */
#define STREAM_LOW_BYTES   4096
#define STREAM_DEFAULT_PREBUFFER (32 * 1024)    /* when the station does not state its bitrate */
#define STREAM_HEAD        64       /* enough of the start to tell the formats apart */
#define STREAM_INIT_WAIT_MS 3000
#define STREAM_RATE_FIRST_S 8       /* when the bitrate of a stream that states none is measured, */
#define STREAM_RATE_AGAIN_S 60      /* and measured again */    /* longest wait for the start of a file while its format is read */
#define GAIN_UNITY   256        /* balance gains are 8.8 fixed point */
#define EQ_Q         1.2f       /* width of each equaliser band */
#define EQ_FLAT_DB   0.05f      /* a band set closer to 0 dB than this is skipped */

static ma_device device;
static int have_device;     /* the device was opened; without one nothing plays, but nothing breaks */
static int have_lock;
static ma_mutex lock;       /* guards everything below */

/* Two decoder slots: the track being played and, for gapless playback, the
 * one queued to follow it. The audio thread switches slots the moment the
 * current track runs out, without waiting for the main thread. */
typedef struct {
    ma_decoder decoder;
    int loaded;
    ma_uint64 length;       /* frames at OUT_RATE, 0 if unknown */
    int rate, channels;     /* of the source file */
    /* For a web address: */
    Stream *stream;
    int pending;            /* still connecting; the decoder is set up by audio_update() */
    int buffering;          /* ran dry: silence until the buffer has refilled */
    int waiting;            /* reads may wait for data (while the decoder is being set up) */
    int refused;            /* not what it claimed to be: reads yield nothing */
    int measured_kbps, measured_twice;  /* the bitrate of a stream that does not state one */
    int own_codec;          /* decoded by one of ours (see stream_kind), which may know titles */
    unsigned char head[STREAM_HEAD];    /* the first bytes, read ahead to see what it is */
    size_t head_len, head_pos;
} Slot;

static Slot slots[2];
static int current;         /* index of the slot being played */
static int queued;          /* the other slot holds the next track */
static int advanced;        /* playback moved on to the queued track */
static int state = AUDIO_STOPPED;
static int finished;
static int failed;          /* a stream could not be opened or understood */
static int seeking;         /* the main thread is moving the decoder: play silence meanwhile */
static int gain_left = GAIN_UNITY, gain_right = GAIN_UNITY;
static float vis[AUDIO_VIS_SAMPLES];
static unsigned vis_pos;

/* Equaliser: one peaking biquad per band, per channel. */
typedef struct {
    float b0, b1, b2, a1, a2;
    float z1[OUT_CHANNELS], z2[OUT_CHANNELS];
    int active;
} Biquad;

static const float eq_freqs[AUDIO_EQ_BANDS] = {
    60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000 };
static Biquad eq[AUDIO_EQ_BANDS];
static int eq_enabled;
static float eq_preamp = 1.0f;

static void apply_eq(ma_int16 *samples, ma_uint32 frame_count)
{
    ma_uint32 i;
    int ch, band;

    for (i = 0; i < frame_count; i++) {
        for (ch = 0; ch < OUT_CHANNELS; ch++) {
            /* The tiny offset keeps filter state out of slow denormals. */
            float x = (float)samples[i * OUT_CHANNELS + ch] * eq_preamp + 1e-18f;

            for (band = 0; band < AUDIO_EQ_BANDS; band++) {
                Biquad *f = &eq[band];
                float y;

                if (!f->active)
                    continue;
                y = f->b0 * x + f->z1[ch];
                f->z1[ch] = f->b1 * x - f->a1 * y + f->z2[ch];
                f->z2[ch] = f->b2 * x - f->a2 * y;
                x = y;
            }
            samples[i * OUT_CHANNELS + ch] = (ma_int16)(x > 32767.0f ? 32767.0f : x < -32768.0f ? -32768.0f : x);
        }
    }
}

/* --- Extra decoders, plugged into miniaudio as a custom backend ------------ */

typedef struct {
    ma_data_source_base base;
    Codec codec;
    ma_uint64 cursor;
} CodecSource;

static ma_result codec_source_read(ma_data_source *source, void *out, ma_uint64 frame_count, ma_uint64 *frames_read)
{
    CodecSource *s = (CodecSource *)source;
    size_t got = s->codec.read(s->codec.state, out, (size_t)frame_count);

    s->cursor += got;
    if (frames_read)
        *frames_read = got;
    return got ? MA_SUCCESS : MA_AT_END;
}

static ma_result codec_source_seek(ma_data_source *source, ma_uint64 frame)
{
    CodecSource *s = (CodecSource *)source;

    if (!s->codec.seek(s->codec.state, frame))
        return MA_ERROR;
    s->cursor = frame;
    return MA_SUCCESS;
}

static ma_result codec_source_format(ma_data_source *source, ma_format *format, ma_uint32 *channels,
                                     ma_uint32 *rate, ma_channel *channel_map, size_t channel_map_cap)
{
    CodecSource *s = (CodecSource *)source;

    if (format)
        *format = ma_format_s16;
    if (channels)
        *channels = (ma_uint32)s->codec.channels;
    if (rate)
        *rate = (ma_uint32)s->codec.rate;
    if (channel_map)
        ma_channel_map_init_standard(ma_standard_channel_map_default, channel_map, channel_map_cap,
                                     (ma_uint32)s->codec.channels);
    return MA_SUCCESS;
}

static ma_result codec_source_cursor(ma_data_source *source, ma_uint64 *cursor)
{
    *cursor = ((CodecSource *)source)->cursor;
    return MA_SUCCESS;
}

static ma_result codec_source_length(ma_data_source *source, ma_uint64 *length)
{
    *length = ((CodecSource *)source)->codec.length;
    return MA_SUCCESS;
}

static ma_data_source_vtable codec_source_vtable = {
    codec_source_read, codec_source_seek, codec_source_format, codec_source_cursor, codec_source_length,
    NULL, 0
};

/* miniaudio hands a custom backend read/seek callbacks, but our codecs open
 * the file themselves; the path travels in the user data pointer. */
static ma_result codec_backend_init(void *user_data, ma_read_proc on_read, ma_seek_proc on_seek, ma_tell_proc on_tell,
                                    void *io_user_data, const ma_decoding_backend_config *config,
                                    const ma_allocation_callbacks *alloc, ma_data_source **backend)
{
    ma_data_source_config source_config = ma_data_source_config_init();
    CodecSource *s = calloc(1, sizeof *s);

    (void)on_read;
    (void)on_seek;
    (void)on_tell;
    (void)io_user_data;
    (void)config;
    (void)alloc;
    if (!s)
        return MA_OUT_OF_MEMORY;
    source_config.vtable = &codec_source_vtable;
    if (!codec_open(user_data, &s->codec) || ma_data_source_init(&source_config, &s->base) != MA_SUCCESS) {
        if (s->codec.close)
            s->codec.close(s->codec.state);
        free(s);
        return MA_NO_BACKEND;
    }
    *backend = s;
    return MA_SUCCESS;
}

static void codec_backend_uninit(void *user_data, ma_data_source *backend, const ma_allocation_callbacks *alloc)
{
    CodecSource *s = (CodecSource *)backend;

    (void)user_data;
    (void)alloc;
    s->codec.close(s->codec.state);
    ma_data_source_uninit(&s->base);
    free(s);
}

static ma_decoding_backend_vtable codec_backend = {
    codec_backend_init, NULL, NULL, NULL, codec_backend_uninit
};
static ma_decoding_backend_vtable *custom_backends[] = { &codec_backend };

/* The same for a station sending AAC, Vorbis or Opus; the user data is the
 * Slot, whose stream the decoder draws on through stream_feed(). */
static size_t stream_feed(void *user, void *out, size_t size);
static int stream_codec_open(void *slot, Codec *codec);

static ma_result stream_backend_init(void *user_data, ma_read_proc on_read, ma_seek_proc on_seek,
                                     ma_tell_proc on_tell, void *io_user_data,
                                     const ma_decoding_backend_config *config,
                                     const ma_allocation_callbacks *alloc, ma_data_source **backend)
{
    ma_data_source_config source_config = ma_data_source_config_init();
    CodecSource *s = calloc(1, sizeof *s);

    (void)on_read;
    (void)on_seek;
    (void)on_tell;
    (void)io_user_data;
    (void)config;
    (void)alloc;
    if (!s)
        return MA_OUT_OF_MEMORY;
    source_config.vtable = &codec_source_vtable;
    if (!stream_codec_open(user_data, &s->codec)) {
        free(s);
        return MA_NO_BACKEND;
    }
    if (ma_data_source_init(&source_config, &s->base) != MA_SUCCESS) {
        s->codec.close(s->codec.state);
        free(s);
        return MA_NO_BACKEND;
    }
    *backend = s;
    return MA_SUCCESS;
}

static ma_decoding_backend_vtable stream_backend = {
    stream_backend_init, NULL, NULL, NULL, codec_backend_uninit
};
static ma_decoding_backend_vtable *stream_backends[] = { &stream_backend };

/* --- File access for miniaudio's own decoders -------------------------------
 * A minimal read-only "virtual file system" over plat_fopen(), so WAV, FLAC
 * and MP3 files open by their UTF-8 path on every platform. */

static ma_result vfs_open(ma_vfs *vfs, const char *path, ma_uint32 mode, ma_vfs_file *file)
{
    (void)vfs;
    if (mode & MA_OPEN_MODE_WRITE)
        return MA_ACCESS_DENIED;
    *file = plat_fopen(path, "rb");
    return *file ? MA_SUCCESS : MA_DOES_NOT_EXIST;
}

static ma_result vfs_close(ma_vfs *vfs, ma_vfs_file file)
{
    (void)vfs;
    fclose(file);
    return MA_SUCCESS;
}

static ma_result vfs_read(ma_vfs *vfs, ma_vfs_file file, void *dst, size_t size, size_t *bytes_read)
{
    size_t got = fread(dst, 1, size, file);

    (void)vfs;
    if (bytes_read)
        *bytes_read = got;
    return got == size ? MA_SUCCESS : feof((FILE *)file) ? MA_AT_END : MA_IO_ERROR;
}

static ma_result vfs_seek(ma_vfs *vfs, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin)
{
    (void)vfs;
    return fseek(file, (long)offset, origin == ma_seek_origin_start ? SEEK_SET :
                                     origin == ma_seek_origin_end ? SEEK_END : SEEK_CUR) == 0 ? MA_SUCCESS : MA_ERROR;
}

static ma_result vfs_tell(ma_vfs *vfs, ma_vfs_file file, ma_int64 *cursor)
{
    long pos = ftell(file);

    (void)vfs;
    *cursor = pos;
    return pos < 0 ? MA_ERROR : MA_SUCCESS;
}

static ma_result vfs_info(ma_vfs *vfs, ma_vfs_file file, ma_file_info *info)
{
    long pos = ftell(file), size;

    (void)vfs;
    if (pos < 0 || fseek(file, 0, SEEK_END) != 0)
        return MA_ERROR;
    size = ftell(file);
    fseek(file, pos, SEEK_SET);
    info->sizeInBytes = (ma_uint64)(size < 0 ? 0 : size);
    return MA_SUCCESS;
}

static ma_vfs_callbacks file_vfs = {
    vfs_open, NULL, vfs_close, vfs_read, NULL, vfs_seek, vfs_tell, vfs_info
};

/* --- Streams -----------------------------------------------------------------
 * A web address is fetched by a Stream (stream.c) into a buffer, and the
 * decoder reads from that buffer through these callbacks. The audio thread
 * must never wait for the network, so it only decodes while the buffer
 * holds enough (see data_callback); the reads here never wait, except while
 * the decoder is first being set up on the main thread. */

static ma_result stream_on_read(ma_decoder *decoder, void *out, size_t size, size_t *bytes_read)
{
    size_t got = stream_feed(decoder->pUserData, out, size);

    if (bytes_read)
        *bytes_read = got;
    return got == size ? MA_SUCCESS : MA_AT_END;
}

static size_t stream_feed(void *user, void *out, size_t size)
{
    Slot *slot = user;
    size_t done = slot->head_len - slot->head_pos;

    if (slot->refused)
        return 0;
    if (done) {             /* what was read ahead comes first */
        if (done > size)
            done = size;
        memcpy(out, slot->head + slot->head_pos, done);
        slot->head_pos += done;
    }
    if (done < size)
        done += slot->waiting ? stream_read_wait(slot->stream, (char *)out + done, size - done, STREAM_INIT_WAIT_MS)
                              : stream_read(slot->stream, (char *)out + done, size - done);
    return done;
}

enum { KIND_MINIAUDIO, KIND_AAC, KIND_VORBIS, KIND_OPUS, KIND_UNKNOWN };

/* What a stream holds. Ogg is told by its first page, whatever the server
 * calls it; AAC by the server's word or the address, since a station may be
 * joined in the middle of a frame. */
static int stream_kind(const Slot *slot)
{
    const char *type = stream_content_type(slot->stream);

    if (slot->head_len >= 36 && memcmp(slot->head, "OggS", 4) == 0) {
        if (memcmp(slot->head + 28, "OpusHead", 8) == 0)
            return KIND_OPUS;
        if (memcmp(slot->head + 29, "vorbis", 6) == 0)
            return KIND_VORBIS;
        return KIND_UNKNOWN;        /* FLAC or something else in Ogg */
    }
    if (strstr(type, "aac") || (!type[0] && path_has_extension(stream_url(slot->stream), ".aac")))
        return KIND_AAC;
    return KIND_MINIAUDIO;
}

static int stream_codec_open(void *user, Codec *codec)
{
    Slot *slot = user;
    int kind = stream_kind(slot);
    int ok = kind == KIND_AAC ? codec_open_aac_stream(stream_feed, slot, codec)
           : kind == KIND_VORBIS ? codec_open_vorbis_stream(stream_feed, slot, codec)
           : kind == KIND_OPUS && codec_open_opus_stream(stream_feed, slot, codec);

    /* If not, miniaudio goes on to try its own decoders; they get nothing. */
    slot->refused = !ok;
    return ok;
}

static ma_result stream_on_seek(ma_decoder *decoder, ma_int64 offset, ma_seek_origin origin)
{
    (void)decoder;
    (void)offset;
    (void)origin;
    return MA_ERROR;        /* what has been played is gone */
}

/* Bytes to have in hand before playing: two seconds' worth. */
static size_t stream_prebuffer(const Stream *stream)
{
    int kbps = stream_bitrate(stream);
    size_t bytes = kbps > 0 ? (size_t)kbps * 250 : STREAM_DEFAULT_PREBUFFER;

    return bytes < 8 * 1024 ? 8 * 1024 : bytes > 64 * 1024 ? 64 * 1024 : bytes;
}

/* Which of miniaudio's decoders a stream is for, from what the server calls
 * it and, failing that, from the address. MP4 files are not supported: they
 * cannot be read front to back. */
static ma_encoding_format stream_format(const Stream *stream)
{
    const char *type = stream_content_type(stream), *url = stream_url(stream);

    if (strstr(type, "mp4") || strstr(type, "m4a"))
        return ma_encoding_format_unknown;
    if (strstr(type, "flac") || (!type[0] && path_has_extension(url, ".flac")))
        return ma_encoding_format_flac;
    if (strstr(type, "wav") || (!type[0] && path_has_extension(url, ".wav")))
        return ma_encoding_format_wav;
    return ma_encoding_format_mp3;
}

/* Sets up the decoder of a stream that has connected. Main thread. */
static int open_stream_decoder(Slot *slot)
{
    ma_decoder_config config = ma_decoder_config_init(OUT_FORMAT, OUT_CHANNELS, OUT_RATE);
    ma_format format;
    ma_uint32 channels, rate;
    ma_result result;

    int kind;

    slot->refused = 0;
    slot->waiting = 1;
    slot->head_pos = 0;
    slot->head_len = stream_read_wait(slot->stream, slot->head, sizeof slot->head, STREAM_INIT_WAIT_MS);
    kind = stream_kind(slot);
    slot->own_codec = kind != KIND_MINIAUDIO;
    if (kind == KIND_UNKNOWN) {
        slot->waiting = 0;
        return 0;
    } else if (slot->own_codec) {
        config.ppCustomBackendVTables = stream_backends;
        config.customBackendCount = 1;
        config.pCustomBackendUserData = slot;
    } else {
        config.encodingFormat = stream_format(slot->stream);
        if (config.encodingFormat == ma_encoding_format_unknown)
            return 0;
    }
    slot->waiting = 1;
    result = ma_decoder_init(stream_on_read, stream_on_seek, slot, &config, &slot->decoder);
    slot->waiting = 0;
    if (result != MA_SUCCESS)
        return 0;
    slot->rate = OUT_RATE;
    slot->channels = OUT_CHANNELS;
    if (ma_data_source_get_data_format(slot->decoder.pBackend, &format, &channels, &rate, NULL, 0) == MA_SUCCESS) {
        slot->rate = (int)rate;
        slot->channels = (int)channels;
    }
    slot->length = 0;       /* a station has none; a file's is not worked out */
    return 1;
}

/* --- Playback --------------------------------------------------------------- */

static void data_callback(ma_device *dev, void *output, const void *input, ma_uint32 frame_count)
{
    ma_int16 *samples = output;
    ma_uint64 got = 0;
    ma_uint32 i;
    int ready;

    (void)dev;
    (void)input;
    ma_mutex_lock(&lock);
    ready = slots[current].loaded && state == AUDIO_PLAYING && !seeking;
    if (ready && slots[current].stream) {
        /* Never decode from a buffer that might run out part way: wait
         * (in silence) until it has refilled, unless no more is coming. */
        Slot *slot = &slots[current];
        size_t have = stream_buffered(slot->stream);
        int over = stream_state(slot->stream) >= STREAM_ENDED;

        /* Half the opening amount is enough to resume on: the decoder took
         * a good part of that for itself when it started. */
        slot->buffering = !over && have < (slot->buffering ? stream_prebuffer(slot->stream) / 2 : STREAM_LOW_BYTES);
        ready = !slot->buffering;
    }
    if (ready) {
        ma_decoder_read_pcm_frames(&slots[current].decoder, output, frame_count, &got);
        if (got < frame_count && queued) {
            /* Gapless hand-over: fill the rest of this buffer from the next track. */
            ma_uint64 more = 0;

            current ^= 1;
            queued = 0;
            advanced = 1;
            ma_decoder_read_pcm_frames(&slots[current].decoder, samples + got * OUT_CHANNELS,
                                       frame_count - got, &more);
            got += more;
        } else if (got < frame_count && slots[current].stream &&
                   stream_state(slots[current].stream) < STREAM_ENDED) {
            slots[current].buffering = 1;       /* ran dry within this very period */
        } else if (got < frame_count) {
            state = AUDIO_STOPPED;
            finished = 1;
            if (!slots[current].stream)
                ma_decoder_seek_to_pcm_frame(&slots[current].decoder, 0);
        }
    }
    if (eq_enabled)
        apply_eq(samples, (ma_uint32)got);
    for (i = 0; i < (ma_uint32)got; i++) {
        vis[vis_pos] = (samples[i * 2] + samples[i * 2 + 1]) / 65536.0f;    /* mono, -1..1 */
        vis_pos = (vis_pos + 1) % AUDIO_VIS_SAMPLES;
    }
    if (gain_left != GAIN_UNITY || gain_right != GAIN_UNITY) {
        for (i = 0; i < (ma_uint32)got; i++) {
            samples[i * 2] = (ma_int16)(samples[i * 2] * gain_left / GAIN_UNITY);
            samples[i * 2 + 1] = (ma_int16)(samples[i * 2 + 1] * gain_right / GAIN_UNITY);
        }
    }
    ma_mutex_unlock(&lock);
}

int audio_init(void)
{
    ma_device_config config = ma_device_config_init(ma_device_type_playback);

    config.playback.format = OUT_FORMAT;
    config.playback.channels = OUT_CHANNELS;
    config.sampleRate = OUT_RATE;
    config.periodSizeInMilliseconds = PERIOD_MS;
    config.dataCallback = data_callback;
    if (ma_mutex_init(&lock) != MA_SUCCESS)
        return 0;
    have_lock = 1;
    /* The lock stays even if there is no device: the player carries on
     * (showing "no audio device") and the other functions still take it. */
    have_device = ma_device_init(NULL, &config, &device) == MA_SUCCESS;
    return have_device;
}

static ma_result nothing_to_read(ma_decoder *decoder, void *out, size_t size, size_t *bytes_read)
{
    (void)decoder;
    (void)out;
    (void)size;
    if (bytes_read)
        *bytes_read = 0;
    return MA_AT_END;
}

static ma_result nothing_to_seek(ma_decoder *decoder, ma_int64 offset, ma_seek_origin origin)
{
    (void)decoder;
    (void)offset;
    (void)origin;
    return MA_ERROR;
}

/* Opens a file into a slot the audio thread is not using. */
static int open_slot(Slot *slot, const char *path)
{
    ma_decoder_config config = ma_decoder_config_init(OUT_FORMAT, OUT_CHANNELS, OUT_RATE);
    ma_format format;
    ma_uint32 channels, rate;

    /* miniaudio tries our codecs first, then its built-in WAV/FLAC/MP3. */
    config.ppCustomBackendVTables = custom_backends;
    config.customBackendCount = sizeof custom_backends / sizeof custom_backends[0];
    config.pCustomBackendUserData = (void *)path;
    config.seekPointCount = SEEK_POINTS;
    /* A CD track is no file for miniaudio to open: only our codec, which
     * goes by the path alone, can do anything with it. */
    if ((path_is_cd(path) ? ma_decoder_init(nothing_to_read, nothing_to_seek, NULL, &config, &slot->decoder)
                          : ma_decoder_init_vfs(&file_vfs, path, &config, &slot->decoder)) != MA_SUCCESS)
        return 0;

    slot->rate = OUT_RATE;
    slot->channels = OUT_CHANNELS;
    if (ma_data_source_get_data_format(slot->decoder.pBackend, &format, &channels, &rate, NULL, 0) == MA_SUCCESS) {
        slot->rate = (int)rate;
        slot->channels = (int)channels;
    }
    if (ma_decoder_get_length_in_pcm_frames(&slot->decoder, &slot->length) != MA_SUCCESS)
        slot->length = 0;
    slot->loaded = 1;
    return 1;
}

static void close_slot(Slot *slot)
{
    if (slot->loaded)
        ma_decoder_uninit(&slot->decoder);
    if (slot->stream)
        stream_close(slot->stream);
    slot->stream = NULL;
    slot->loaded = slot->pending = slot->buffering = slot->own_codec = 0;
    slot->measured_kbps = slot->measured_twice = 0;
    slot->head_len = slot->head_pos = 0;
    slot->length = 0;
}

static void close_all(void)
{
    ma_device_stop(&device);
    ma_mutex_lock(&lock);
    close_slot(&slots[0]);
    close_slot(&slots[1]);
    queued = 0;
    advanced = 0;
    state = AUDIO_STOPPED;
    finished = 0;
    failed = 0;
    memset(vis, 0, sizeof vis);
    ma_mutex_unlock(&lock);
}

void audio_shutdown(void)
{
    if (!have_lock)
        return;
    close_all();
    if (have_device)
        ma_device_uninit(&device);
    ma_mutex_uninit(&lock);
    have_device = have_lock = 0;
}

int audio_open(const char *path)
{
    Slot *slot = &slots[current];

    close_all();    /* the device is stopped, so the slots are ours */
    if (!path_is_url(path))
        return open_slot(slot, path);
    /* A web address: connecting happens in the background, and the rest
     * follows in audio_update(). */
    slot->stream = stream_open(path);
    slot->pending = slot->stream != NULL;
    slot->rate = slot->channels = 0;
    return slot->pending;
}

void audio_update(void)
{
    Slot *slot = &slots[current];
    int status, ok;

    if (!slot->pending)
        return;
    status = stream_state(slot->stream);
    if (status == STREAM_CONNECTING)
        return;
    if (status == STREAM_OPEN && stream_buffered(slot->stream) < stream_prebuffer(slot->stream))
        return;     /* connected; filling up */
    ok = status != STREAM_FAILED && open_stream_decoder(slot);
    ma_mutex_lock(&lock);
    slot->pending = 0;
    if (ok) {
        slot->loaded = 1;
    } else {
        stream_close(slot->stream);
        slot->stream = NULL;
        state = AUDIO_STOPPED;
        failed = 1;
    }
    ma_mutex_unlock(&lock);
    if (!ok)
        ma_device_stop(&device);
}

int audio_take_failed(void)
{
    int was = failed;

    failed = 0;
    return was;
}

int audio_is_stream(void)
{
    return slots[current].stream != NULL;
}

int audio_buffering(void)
{
    return slots[current].pending || (slots[current].buffering && state == AUDIO_PLAYING);
}

int audio_stream_title(char *out, size_t size)
{
    Slot *slot = &slots[current];
    int got = 0;

    if (!slot->stream || slot->pending)
        return 0;
    if (stream_take_title(slot->stream, out, size))
        return 1;
    /* Ogg stations put the title in the audio itself, where the decoder
     * finds it (on the audio thread, hence the lock). */
    ma_mutex_lock(&lock);
    if (slot->loaded && slot->own_codec) {
        Codec *codec = &((CodecSource *)slot->decoder.pBackend)->codec;

        got = codec->take_title && codec->take_title(codec->state, out, size);
    }
    ma_mutex_unlock(&lock);
    return got;
}

const char *audio_stream_name(void)
{
    return slots[current].stream && !slots[current].pending ? stream_name(slots[current].stream) : "";
}

int audio_stream_bitrate(void)
{
    Slot *slot = &slots[current];
    double seconds;
    int stated;

    if (!slot->stream || slot->pending)
        return 0;
    stated = stream_bitrate(slot->stream);
    if (stated || !slot->loaded)
        return stated;
    /* Not stated (Ogg stations seldom do): what has been taken from the
     * stream, over the time it has played for. Worked out twice, early and
     * again when the figure has settled, and then left alone. */
    seconds = audio_position();
    if ((seconds >= STREAM_RATE_FIRST_S && !slot->measured_kbps) ||
        (seconds >= STREAM_RATE_AGAIN_S && !slot->measured_twice)) {
        slot->measured_twice = slot->measured_kbps != 0;
        slot->measured_kbps = (int)((double)stream_consumed(slot->stream) * 8.0 / seconds / 1000.0 + 0.5);
    }
    return slot->measured_kbps;
}

int audio_queue_next(const char *path)
{
    Slot *other;

    /* Once `queued` is clear the audio thread leaves the other slot alone. */
    ma_mutex_lock(&lock);
    if (advanced) {
        /* The queued track has just started; the caller must take note of
         * that before it replaces what it believes is still queued. */
        ma_mutex_unlock(&lock);
        return -1;
    }
    queued = 0;
    other = &slots[current ^ 1];
    ma_mutex_unlock(&lock);

    close_slot(other);      /* a cancelled track, or the one that just finished */
    /* Streams take no part in gapless hand-overs: one cannot be opened
     * ahead of time, and a station does not end. */
    if (!path || path_is_url(path) || slots[current].stream || !slots[current].loaded || !open_slot(other, path))
        return 0;

    ma_mutex_lock(&lock);
    /* If the current track ended meanwhile, `other` is still the right slot:
     * `current` only ever changes while `queued` is set. */
    queued = 1;
    ma_mutex_unlock(&lock);
    return 1;
}

int audio_take_advanced(void)
{
    int was;

    ma_mutex_lock(&lock);
    was = advanced;
    advanced = 0;
    ma_mutex_unlock(&lock);
    return was;
}

void audio_play(void)
{
    int ok;

    ma_mutex_lock(&lock);
    ok = slots[current].loaded || slots[current].pending;
    if (ok) {
        if (state != AUDIO_PAUSED && slots[current].loaded && !slots[current].stream)
            ma_decoder_seek_to_pcm_frame(&slots[current].decoder, 0);
        state = AUDIO_PLAYING;
        finished = 0;
    }
    ma_mutex_unlock(&lock);
    if (ok)
        ma_device_start(&device);
}

void audio_pause(void)
{
    int now;

    ma_mutex_lock(&lock);
    if (state == AUDIO_PLAYING)
        state = AUDIO_PAUSED;
    else if (state == AUDIO_PAUSED)
        state = AUDIO_PLAYING;
    now = state;
    ma_mutex_unlock(&lock);

    if (now == AUDIO_PAUSED)
        ma_device_stop(&device);
    else if (now == AUDIO_PLAYING)
        ma_device_start(&device);
}

void audio_stop(void)
{
    ma_device_stop(&device);
    ma_mutex_lock(&lock);
    if (slots[current].loaded && !slots[current].stream)
        ma_decoder_seek_to_pcm_frame(&slots[current].decoder, 0);
    state = AUDIO_STOPPED;
    memset(vis, 0, sizeof vis);
    ma_mutex_unlock(&lock);
}

int audio_state(void)
{
    return state;
}

int audio_take_finished(void)
{
    int was;

    ma_mutex_lock(&lock);
    was = finished;
    finished = 0;
    ma_mutex_unlock(&lock);
    return was;
}

void audio_set_volume(float volume)
{
    ma_device_set_master_volume(&device, volume);
}

void audio_set_balance(float balance)
{
    ma_mutex_lock(&lock);
    gain_left = balance > 0 ? (int)((1 - balance) * GAIN_UNITY) : GAIN_UNITY;
    gain_right = balance < 0 ? (int)((1 + balance) * GAIN_UNITY) : GAIN_UNITY;
    ma_mutex_unlock(&lock);
}

void audio_set_eq(int enabled, float preamp_db, const float bands_db[AUDIO_EQ_BANDS])
{
    int band;

    ma_mutex_lock(&lock);
    eq_enabled = enabled;
    eq_preamp = powf(10.0f, preamp_db / 20.0f);
    for (band = 0; band < AUDIO_EQ_BANDS; band++) {
        /* Peaking filter from the RBJ audio EQ cookbook. */
        Biquad *f = &eq[band];
        float a = powf(10.0f, bands_db[band] / 40.0f);
        float w0 = 2.0f * PI_F * eq_freqs[band] / OUT_RATE;
        float alpha = sinf(w0) / (2.0f * EQ_Q);
        float a0 = 1.0f + alpha / a;

        f->b0 = (1.0f + alpha * a) / a0;
        f->b1 = f->a1 = -2.0f * cosf(w0) / a0;
        f->b2 = (1.0f - alpha * a) / a0;
        f->a2 = (1.0f - alpha / a) / a0;
        f->active = bands_db[band] > EQ_FLAT_DB || bands_db[band] < -EQ_FLAT_DB;
        if (!f->active || !enabled) {
            memset(f->z1, 0, sizeof f->z1);
            memset(f->z2, 0, sizeof f->z2);
        }
    }
    ma_mutex_unlock(&lock);
}

void audio_seek(double seconds)
{
    ma_uint64 frame;

    if (seconds < 0)
        seconds = 0;
    frame = (ma_uint64)(seconds * OUT_RATE);
    ma_mutex_lock(&lock);
    if (!slots[current].loaded || slots[current].stream) {      /* a stream cannot be wound */
        ma_mutex_unlock(&lock);
        return;
    }
    if (slots[current].length && frame >= slots[current].length)
        frame = slots[current].length - 1;
    /* The seek itself runs without the lock, so that a slow one cannot
     * hold up the audio thread (the device would repeat its last buffer
     * until it got an answer). While `seeking` is set the audio thread
     * leaves the decoder alone and plays silence; every other use of the
     * decoder is on this thread. */
    seeking = 1;
    ma_mutex_unlock(&lock);
    ma_decoder_seek_to_pcm_frame(&slots[current].decoder, frame);
    ma_mutex_lock(&lock);
    seeking = 0;
    ma_mutex_unlock(&lock);
}

double audio_position(void)
{
    ma_uint64 cursor = 0;

    ma_mutex_lock(&lock);
    if (slots[current].loaded)
        ma_decoder_get_cursor_in_pcm_frames(&slots[current].decoder, &cursor);
    ma_mutex_unlock(&lock);
    return (double)cursor / OUT_RATE;
}

double audio_length(void)
{
    return (double)slots[current].length / OUT_RATE;
}

int audio_sample_rate(void)
{
    return slots[current].rate;
}

int audio_channels(void)
{
    return slots[current].channels;
}

void audio_get_vis(float out[AUDIO_VIS_SAMPLES])
{
    unsigned i;

    ma_mutex_lock(&lock);
    for (i = 0; i < AUDIO_VIS_SAMPLES; i++)
        out[i] = vis[(vis_pos + i) % AUDIO_VIS_SAMPLES];
    ma_mutex_unlock(&lock);
}
