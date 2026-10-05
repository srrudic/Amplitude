/* The decoders in codec_*.c, run on the sample files in tests/media and on
 * a module generated here. No sound device is involved.
 *
 *     build/test/test_codecs tests/media
 */
#include "codec.h"
#include "test.h"

/* Opens a file, decodes all of it, seeks back and forth, and compares what
 * came out with what is expected. `frames` is the exact length where it is
 * known, or 0 to only require something plausible. */
static void check_file(const char *path, int channels, int rate, uint64_t frames)
{
    static short buffer[4096 * 2];
    uint64_t decoded = 0, again = 0;
    double energy = 0;
    Codec codec;
    size_t n, i;

    printf("  %s\n", strrchr(path, '/') + 1);
    memset(&codec, 0, sizeof codec);
    CHECK(codec_open(path, &codec));
    if (!codec.read)
        return;
    CHECK_INT(codec.channels, channels);
    CHECK_INT(codec.rate, rate);
    if (frames)
        CHECK_INT(codec.length, frames);

    while ((n = codec.read(codec.state, buffer, 4096)) > 0 && decoded < 10000000) {
        for (i = 0; i < n * (size_t)codec.channels; i++)
            energy += (double)buffer[i] * buffer[i];
        decoded += n;
    }
    /* The declared length is right to within a couple of codec frames. */
    CHECK(decoded > 0 && decoded + 2304 >= codec.length && decoded <= codec.length + 4096);
    CHECK(energy > 0);

    /* Seeking to the middle leaves about half; seeking to 0 starts over. */
    CHECK(codec.seek(codec.state, codec.length / 2));
    while ((n = codec.read(codec.state, buffer, 4096)) > 0 && again < 10000000)
        again += n;
    CHECK(again > 0 && again <= decoded);
    CHECK(codec.seek(codec.state, 0));
    CHECK(codec.read(codec.state, buffer, 1024) == 1024 || decoded < 1024);
    codec.close(codec.state);
}

int main(int argc, char **argv)
{
    char path[512];
    const char *media = argc > 1 ? argv[1] : "tests/media", *mod;
    Codec codec;

    test_begin("codecs");

    snprintf(path, sizeof path, "%s/sfx-opus.ogg", media);
    check_file(path, 2, 48000, 9288);       /* Opus always decodes to 48 kHz stereo */
    snprintf(path, sizeof path, "%s/sfx-vorbis.ogg", media);
    check_file(path, 1, 48000, 4096);
    snprintf(path, sizeof path, "%s/sfx-aac.mp4", media);
    check_file(path, 2, 48000, 9216);
    snprintf(path, sizeof path, "%s/sfx.aac", media);
    check_file(path, 2, 48000, 9216);

    mod = test_path("generated.mod");
    test_write_mod(mod, "Generated");
    check_file(mod, 2, 44100, 0);

    /* Formats the codec layer leaves to miniaudio, and junk, are declined. */
    memset(&codec, 0, sizeof codec);
    snprintf(path, sizeof path, "%s/sfx.mp3", media);
    CHECK(!codec_open(path, &codec));
    snprintf(path, sizeof path, "%s/sfx.flac", media);
    CHECK(!codec_open(path, &codec));
    CHECK(!codec_open(test_path("missing.ogg"), &codec));
    test_write(test_path("junk.ogg"), "OggS and then nothing useful at all, really", 43);
    CHECK(!codec_open(test_path("junk.ogg"), &codec));
    /* Recognised as Vorbis or Opus from the first page, but broken beyond
     * it: the decoder gives up, and the file must be closed exactly once. */
    {
        static const char vorbis[] =
            "OggS\0\2\0\0\0\0\0\0\0\0\1\0\0\0\0\0\0\0\0\0\0\0\1\x1E\1vorbis and no more than this, sadly";
        static const char opus[] =
            "OggS\0\2\0\0\0\0\0\0\0\0\1\0\0\0\0\0\0\0\0\0\0\0\1\x13OpusHead and no more than this, sadly";

        test_write(test_path("broken-vorbis.ogg"), vorbis, sizeof vorbis - 1);
        CHECK(!codec_open(test_path("broken-vorbis.ogg"), &codec));
        test_write(test_path("broken-opus.ogg"), opus, sizeof opus - 1);
        CHECK(!codec_open(test_path("broken-opus.ogg"), &codec));
    }
    test_write(test_path("junk.mod"), "not a module", 12);
    CHECK(!codec_open(test_path("junk.mod"), &codec));
    test_write(test_path("junk.m4a"), "\0\0\0\x18" "ftypM4A \0\0\0\0junkjunkjunk", 24);
    CHECK(!codec_open(test_path("junk.m4a"), &codec));
    test_write(test_path("junk.aac"), "\xFF\xF1 not really adts", 18);
    CHECK(!codec_open(test_path("junk.aac"), &codec));
    return test_end();
}
