/* Audio CDs, tried on an image of a disc (a CUE sheet and a raw file), which
 * goes through everything a real disc does except the drive itself: the
 * track list, track paths, the decoder with its read-ahead thread, seeking,
 * and playback through the audio engine (muted, like the audio test).
 *
 *     build/test/test_cd
 */
#include "audio.h"
#include "cd.h"
#include "codec.h"
#include "test.h"

/* The disc: track 1 is 3 seconds, track 2 is 2 seconds, track 3 is data,
 * track 4 is 1 second. Every sample says where on the disc it is. */
#define SECTORS_1   (3 * CD_SECTORS_PER_S)
#define SECTORS_2   (2 * CD_SECTORS_PER_S)
#define SECTORS_3   20
#define SECTORS_4   (1 * CD_SECTORS_PER_S)
#define SECTORS     (SECTORS_1 + SECTORS_2 + SECTORS_3 + SECTORS_4)

static short sample_at(long frame, int channel)
{
    /* Quiet (a few hundred at most), and different on every frame for a while. */
    return (short)((frame * 7 + channel * 3) % 301 - 150);
}

static void make_disc(void)
{
    static short sound[SECTORS * CD_SECTOR_FRAMES * 2];
    char sheet[512];
    long frame;

    for (frame = 0; frame < SECTORS * CD_SECTOR_FRAMES; frame++) {
        sound[frame * 2] = sample_at(frame, 0);
        sound[frame * 2 + 1] = sample_at(frame, 1);
    }
    test_write(test_path("disc.bin"), sound, sizeof sound);
    snprintf(sheet, sizeof sheet,
             "REM made for the test\r\n"
             "FILE \"disc.bin\" BINARY\r\n"
             "  TRACK 01 AUDIO\r\n    INDEX 01 00:00:00\r\n"
             "  TRACK 02 AUDIO\r\n    INDEX 00 00:02:50\r\n    INDEX 01 00:03:00\r\n"
             "  TRACK 03 MODE1/2352\r\n    INDEX 01 00:05:00\r\n"
             "  TRACK 04 AUDIO\r\n    INDEX 01 00:05:20\r\n");
    test_write(test_path("disc.cue"), sheet, strlen(sheet));
    /* An album with a file for each track is not a disc image. */
    snprintf(sheet, sizeof sheet, "FILE \"one.wav\" WAVE\n TRACK 01 AUDIO\n  INDEX 01 00:00:00\n"
                                  "FILE \"two.wav\" WAVE\n TRACK 02 AUDIO\n  INDEX 01 00:00:00\n");
    test_write(test_path("album.cue"), sheet, strlen(sheet));
}

static const char *track_path(int number)
{
    static char paths[4][600];
    static int next;
    char *path = paths[next++ % 4];

    cd_make_path(path, sizeof paths[0], test_path("disc.cue"), number);
    return path;
}

static void test_toc(void)
{
    char device[CD_DEVICE_MAX];
    unsigned char sector[CD_SECTOR];
    const CdToc *toc;
    int number = 0;
    Cd *cd = cd_open(test_path("disc.cue"));

    CHECK(cd != NULL);
    if (!cd)
        return;
    toc = cd_toc(cd);
    CHECK_INT(toc->count, 4);
    CHECK_INT(toc->track[0].number, 1);
    CHECK_INT((int)toc->track[0].start, 0);
    CHECK_INT((int)toc->track[0].sectors, SECTORS_1);
    CHECK_INT((int)toc->track[1].start, SECTORS_1);         /* at INDEX 01, not the gap before it */
    CHECK_INT((int)toc->track[1].sectors, SECTORS_2);
    CHECK(toc->track[0].audio && toc->track[1].audio && !toc->track[2].audio && toc->track[3].audio);
    CHECK_INT((int)toc->track[3].sectors, SECTORS_4);       /* to the end of the file */
    CHECK(cd_read(cd, SECTORS_1, 1, sector));
    CHECK_INT(((short *)sector)[0], sample_at((long)SECTORS_1 * CD_SECTOR_FRAMES, 0));
    CHECK(!cd_read(cd, SECTORS, 1, sector));                /* past the end */
    cd_close(cd);

    CHECK(cd_open(test_path("album.cue")) == NULL);
    CHECK(cd_open(test_path("nothing.cue")) == NULL);
    CHECK(cd_open("/dev/null") == NULL);                    /* a device that is no CD drive */

    CHECK(path_is_cd("cdda:///dev/sr0/7") && !path_is_cd("/music/cdda.mp3"));
    CHECK(cd_split_path("cdda:///dev/sr0/7", device, sizeof device, &number));
    CHECK_STR(device, "/dev/sr0");
    CHECK_INT(number, 7);
    CHECK(cd_split_path("cdda://D:/12", device, sizeof device, &number));
    CHECK_STR(device, "D:");
    CHECK_INT(number, 12);
    CHECK(!cd_split_path("cdda://D:", device, sizeof device, &number));
    CHECK(!cd_split_path("cdda:///dev/sr0/0", device, sizeof device, &number));
}

/* Reads `frames` frames and checks them against the disc from `at` on. */
static int reads_right(Codec *codec, long at, size_t frames)
{
    static short got[CD_RATE * 2];
    size_t i, n = codec->read(codec->state, got, frames);

    if (n != frames)
        return 0;
    for (i = 0; i < n; i++)
        if (got[i * 2] != sample_at(at + (long)i, 0) || got[i * 2 + 1] != sample_at(at + (long)i, 1))
            return 0;
    return 1;
}

static void test_codec(void)
{
    static short rest[CD_RATE * 2];
    long start = (long)SECTORS_1 * CD_SECTOR_FRAMES;       /* of track 2 */
    Codec codec;
    int i;

    memset(&codec, 0, sizeof codec);
    CHECK(codec_open(track_path(2), &codec));
    if (!codec.state)
        return;
    CHECK_INT(codec.rate, CD_RATE);
    CHECK_INT(codec.channels, 2);
    CHECK_INT((int)codec.length, SECTORS_2 * CD_SECTOR_FRAMES);
    /* In pieces of the size the audio thread asks for, quicker than any
     * drive could follow, across the buffer's wrap-around. */
    CHECK(reads_right(&codec, start, 1000));
    CHECK(reads_right(&codec, start + 1000, 2205));
    CHECK(reads_right(&codec, start + 3205, 441));
    /* Seeking lands on the start of a sector. */
    CHECK(codec.seek(codec.state, 30 * CD_SECTOR_FRAMES + 100));
    CHECK(reads_right(&codec, start + 30 * CD_SECTOR_FRAMES, 5000));
    CHECK(codec.seek(codec.state, 0));
    CHECK(reads_right(&codec, start, 100));
    /* The end: the last frames, then nothing, and nothing of track 3. */
    CHECK(codec.seek(codec.state, (SECTORS_2 - 1) * CD_SECTOR_FRAMES));
    CHECK_INT((int)codec.read(codec.state, rest, CD_RATE), CD_SECTOR_FRAMES);
    CHECK_INT(rest[(CD_SECTOR_FRAMES - 1) * 2], sample_at(start + (long)SECTORS_2 * CD_SECTOR_FRAMES - 1, 0));
    CHECK_INT((int)codec.read(codec.state, rest, 100), 0);
    codec.close(codec.state);

    /* Opening and closing at once, many times, leaves nothing behind. */
    for (i = 0; i < 20; i++) {
        memset(&codec, 0, sizeof codec);
        CHECK(codec_open(track_path(1), &codec));
        codec.close(codec.state);
    }
    memset(&codec, 0, sizeof codec);
    CHECK(!codec_open(track_path(3), &codec));      /* the data track */
    CHECK(!codec_open(track_path(9), &codec));      /* no such track */
    CHECK(!codec_open("cdda:///dev/null/1", &codec));
}

static void test_playback(void)
{
    int waited;

    CHECK(audio_init());
    audio_set_volume(0);
    CHECK(audio_open(track_path(4)));
    CHECK_INT(audio_sample_rate(), CD_RATE);
    CHECK_INT(audio_channels(), 2);
    CHECK(audio_length() > 0.99 && audio_length() < 1.01);
    /* The next track is taken up without a gap when this one ends. */
    CHECK(audio_queue_next(track_path(2)));
    audio_play();
    usleep(400 * 1000);
    CHECK(audio_position() > 0.2 && audio_position() < 0.7);
    for (waited = 0; waited < 3000 && !audio_take_advanced(); waited += 20)
        usleep(20 * 1000);
    CHECK(waited < 3000);
    CHECK(audio_length() > 1.99 && audio_length() < 2.01);
    audio_seek(1.5);
    usleep(200 * 1000);
    CHECK(audio_position() > 1.5 && audio_position() < 1.95);
    for (waited = 0; waited < 3000 && !audio_take_finished(); waited += 20)
        usleep(20 * 1000);
    CHECK(waited < 3000);
    CHECK(!audio_open(track_path(3)));
    audio_shutdown();
}

int main(void)
{
    test_begin("cd");
    make_disc();
    test_toc();
    test_codec();
    test_playback();
    usleep(200 * 1000);     /* let the reader threads finish, so nothing looks leaked */
    return test_end();
}
