/* The playback engine: opening each format, position and seeking, the
 * equaliser path and gapless hand-over. This test opens the sound device
 * and plays for a few seconds, with the volume at zero and near-silent
 * input, so nothing is heard. On a machine without sound hardware
 * miniaudio falls back to its null device and the test still runs.
 *
 *     build/test/test_audio tests/media
 */
#include "audio.h"
#include "test.h"

static void sleep_ms(int ms)
{
    usleep((useconds_t)ms * 1000);
}

static void test_formats(const char *media)
{
    static const struct { const char *file; int rate, channels; double seconds; } files[] = {
        { "sfx.mp3", 48000, 1, 0.264 }, { "sfx.flac", 48000, 1, 0.21 }, { "sfx-opus.ogg", 48000, 2, 0.19 },
        { "sfx-vorbis.ogg", 48000, 1, 0.085 }, { "sfx-aac.mp4", 48000, 2, 0.19 }, { "sfx.aac", 48000, 2, 0.19 },
    };
    char path[512];
    size_t i;

    for (i = 0; i < sizeof files / sizeof files[0]; i++) {
        snprintf(path, sizeof path, "%s/%s", media, files[i].file);
        CHECK(audio_open(path));
        printf("  %-16s %.3f s\n", files[i].file, audio_length());
        CHECK_INT(audio_sample_rate(), files[i].rate);
        CHECK_INT(audio_channels(), files[i].channels);
        CHECK(fabs(audio_length() - files[i].seconds) < 0.03);
        CHECK_INT(audio_state(), AUDIO_STOPPED);
    }
    CHECK(!audio_open(test_path("missing.mp3")));
    test_write(test_path("junk.mp3"), "this is not audio in any format", 31);
    CHECK(!audio_open(test_path("junk.mp3")));
}

static void test_transport(void)
{
    const char *wav = test_path("tone.wav");
    static const float bands[AUDIO_EQ_BANDS] = { 12, -12, 6, 0, 0, -6, 3, 0, 12, -12 };
    float vis[AUDIO_VIS_SAMPLES];
    double before;

    test_write_wav(wav, 3.0, 440, 60);
    CHECK(audio_open(wav));
    CHECK(fabs(audio_length() - 3.0) < 0.01);
    audio_set_eq(1, 3.0f, bands);
    audio_set_balance(-0.5f);
    audio_play();
    CHECK_INT(audio_state(), AUDIO_PLAYING);
    sleep_ms(400);
    CHECK(audio_position() > 0.2 && audio_position() < 0.8);
    audio_get_vis(vis);

    audio_pause();
    CHECK_INT(audio_state(), AUDIO_PAUSED);
    before = audio_position();
    sleep_ms(200);
    CHECK(audio_position() == before);
    audio_pause();                          /* resumes */
    CHECK_INT(audio_state(), AUDIO_PLAYING);

    audio_seek(2.0);
    sleep_ms(200);
    CHECK(audio_position() > 2.0 && audio_position() < 2.6);
    audio_seek(-5);
    CHECK(audio_position() < 0.3);
    audio_seek(1000);                       /* past the end: clamped, then finishes */
    sleep_ms(300);
    CHECK(audio_take_finished());
    CHECK(!audio_take_finished());          /* reported once */
    CHECK_INT(audio_state(), AUDIO_STOPPED);

    audio_stop();
    audio_set_eq(0, 0, bands);
    audio_set_balance(0);
}

/* Two tones of 1.0 and 1.2 seconds queued back to back must take 2.2
 * seconds in total: any gap between them would show up as extra time. */
static void test_gapless(void)
{
    const char *first = test_path("first.wav"), *second = test_path("second.wav");
    int advanced_at = -1, finished_at = -1, tick;

    test_write_wav(first, 1.0, 440, 60);
    test_write_wav(second, 1.2, 880, 60);
    CHECK(audio_open(first));
    CHECK_INT(audio_queue_next(second), 1);
    CHECK(!audio_take_advanced());
    audio_play();
    for (tick = 0; tick < 400 && finished_at < 0; tick++) {    /* 10 ms steps */
        sleep_ms(10);
        if (audio_take_advanced()) {
            advanced_at = tick;
            CHECK(fabs(audio_length() - 1.2) < 0.01);       /* now reporting the second track */
            CHECK(audio_position() < 0.3);
            CHECK_INT(audio_state(), AUDIO_PLAYING);
            CHECK_INT(audio_queue_next(NULL), 0);
        }
        if (audio_take_finished())
            finished_at = tick;
    }
    printf("  advanced after %.2f s, finished after %.2f s\n", advanced_at / 100.0, finished_at / 100.0);
    CHECK(advanced_at >= 85 && advanced_at <= 125);
    CHECK(finished_at >= 205 && finished_at <= 250);

    /* A queued track can be cancelled or replaced at any moment. */
    CHECK(audio_open(first));
    audio_play();
    for (tick = 0; tick < 60; tick++) {
        audio_queue_next(tick % 3 ? second : NULL);
        audio_take_advanced();
        sleep_ms(3);
    }
    audio_stop();
    CHECK_INT(audio_queue_next(test_path("missing.wav")), 0);
}

int main(int argc, char **argv)
{
    const char *media = argc > 1 ? argv[1] : "tests/media";

    test_begin("audio");
    if (!audio_init()) {
        printf("  no audio device available: skipped\n");
        return test_end();
    }
    audio_set_volume(0);
    test_formats(media);
    test_transport();
    test_gapless();
    audio_shutdown();
    return test_end();
}
