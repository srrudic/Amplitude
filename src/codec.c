#include "codec.h"
#include "platform.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

int codec_open(const char *path, Codec *codec)
{
    unsigned char head[64];
    FILE *f = plat_fopen(path, "rb");
    size_t got;

    if (!f)
        return 0;
    memset(head, 0, sizeof head);
    got = fread(head, 1, sizeof head, f);
    fclose(f);
    if (got < 16)
        return 0;

    /* Ogg: the first packet names the codec. */
    if (memcmp(head, "OggS", 4) == 0) {
        if (memcmp(head + 28, "OpusHead", 8) == 0)
            return codec_open_opus(path, codec);
        if (memcmp(head + 29, "vorbis", 6) == 0)
            return codec_open_vorbis(path, codec);
        return 0;
    }
    if (memcmp(head + 4, "ftyp", 4) == 0 || path_has_extension(path, ".aac"))
        return codec_open_aac(path, codec);
    if (path_has_extension(path, ".mod") || path_has_extension(path, ".xm") ||
        path_has_extension(path, ".s3m") || path_has_extension(path, ".it"))
        return codec_open_module(path, codec);
    return 0;
}

void codec_format_title(char *out, size_t size, const char *artist, const char *title)
{
    if (artist && *artist && title && *title)
        snprintf(out, size, "%s - %s", artist, title);
    else
        snprintf(out, size, "%s", title && *title ? title : artist ? artist : "");
}
