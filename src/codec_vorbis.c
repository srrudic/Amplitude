/* Ogg Vorbis via stb_vorbis. */
#include "codec.h"
#include "platform.h"

#include <stdlib.h>

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
