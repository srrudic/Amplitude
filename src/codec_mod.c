/* Tracker modules (MOD, XM, S3M, IT) via libxmp-lite. */
#include "codec.h"
#include "platform.h"

#include <stdio.h>
#include <xmp.h>

#define MODULE_RATE 44100

static size_t module_read(void *state, short *out, size_t frames)
{
    /* Stops after one pass through the song instead of looping forever. */
    if (xmp_play_buffer(state, out, (int)(frames * 2 * sizeof(short)), 1) < 0)
        return 0;
    return frames;
}

static int module_seek(void *state, uint64_t frame)
{
    /* Restart the buffer-level playback state along with the position. */
    xmp_play_buffer(state, NULL, 0, 0);
    return xmp_seek_time(state, (int)(frame * 1000 / MODULE_RATE)) >= 0;
}

static void module_close(void *state)
{
    xmp_end_player(state);
    xmp_release_module(state);
    xmp_free_context(state);
}

int codec_open_module(const char *path, Codec *codec)
{
    xmp_context ctx = xmp_create_context();
    struct xmp_frame_info info;
    FILE *f = plat_fopen(path, "rb");
    int loaded;

    /* The module is read completely into memory, so the file can be closed
     * straight away (libxmp leaves a handle it was given open). */
    loaded = ctx && f && xmp_load_module_from_file(ctx, f, 0) == 0;
    if (f)
        fclose(f);
    if (!loaded) {
        if (ctx)
            xmp_free_context(ctx);
        return 0;
    }
    if (xmp_start_player(ctx, MODULE_RATE, 0) != 0) {
        xmp_release_module(ctx);
        xmp_free_context(ctx);
        return 0;
    }
    xmp_get_frame_info(ctx, &info);

    codec->state = ctx;
    codec->channels = 2;
    codec->rate = MODULE_RATE;
    codec->length = (uint64_t)info.total_time * MODULE_RATE / 1000;
    codec->read = module_read;
    codec->seek = module_seek;
    codec->close = module_close;
    return 1;
}
