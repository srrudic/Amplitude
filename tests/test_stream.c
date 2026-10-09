/* Streams: the HTTP client (addresses, redirects, station playlists, station
 * metadata) against a small local server, tests/stream_server.py, and then
 * playback of a stream through the audio engine.
 *
 *     build/test/test_stream tests/media
 *
 * Needs python3 for the server and uses a port on 127.0.0.1. Like the audio
 * test it opens the sound device, muted. */
#include "audio.h"
#include "platform.h"
#include "stream.h"
#include "test.h"

#include <sys/stat.h>

static int port;
static unsigned char *file_data, *aac_data;
static size_t file_size, aac_size;

static const char *address(const char *path)
{
    static char urls[4][128];
    static int next;
    char *url = urls[next++ % 4];

    snprintf(url, sizeof urls[0], "http://127.0.0.1:%d%s", port, path);
    return url;
}

/* Waits for a stream to leave the "connecting" state. */
static int settle(Stream *s)
{
    int waited;

    for (waited = 0; stream_state(s) == STREAM_CONNECTING && waited < 5000; waited += 10)
        plat_sleep_ms(10);
    return stream_state(s);
}

static void test_addresses(void)
{
    char host[64], path[128];
    int secure, port_out;

    CHECK(stream_parse_url("http://example.org/a/b.mp3?x=1#frag", &secure, host, sizeof host, &port_out, path, sizeof path));
    CHECK(!secure && port_out == 80);
    CHECK_STR(host, "example.org");
    CHECK_STR(path, "/a/b.mp3?x=1");
    CHECK(stream_parse_url("HTTPS://Radio.Example:8443", &secure, host, sizeof host, &port_out, path, sizeof path));
    CHECK(secure && port_out == 8443);
    CHECK_STR(host, "Radio.Example");
    CHECK_STR(path, "/");
    CHECK(stream_parse_url("http://h?q=1", &secure, host, sizeof host, &port_out, path, sizeof path));
    CHECK_STR(path, "/?q=1");
    CHECK(!stream_parse_url("ftp://example.org/x", &secure, host, sizeof host, &port_out, path, sizeof path));
    CHECK(!stream_parse_url("http://", &secure, host, sizeof host, &port_out, path, sizeof path));
    CHECK(!stream_parse_url("http://host:0/", &secure, host, sizeof host, &port_out, path, sizeof path));
    CHECK(!stream_parse_url("http://user:pw@host/", &secure, host, sizeof host, &port_out, path, sizeof path));
    CHECK(!stream_parse_url("/home/me/song.mp3", &secure, host, sizeof host, &port_out, path, sizeof path));
}

/* A file on a web server arrives whole and unchanged. */
static void test_file(void)
{
    Stream *s = stream_open(address("/file.mp3"));
    unsigned char *got = malloc(file_size + 16);
    size_t n;

    CHECK(s != NULL);
    CHECK(settle(s) != STREAM_FAILED);
    n = stream_read_wait(s, got, file_size + 16, 3000);
    CHECK_INT((int)n, (int)file_size);
    CHECK(memcmp(got, file_data, file_size) == 0);
    CHECK_INT(stream_state(s), STREAM_ENDED);
    CHECK_STR(stream_content_type(s), "audio/mpeg");
    CHECK_STR(stream_name(s), "");
    free(got);
    stream_close(s);
}

/* A station: the audio comes out with the metadata blocks removed, and the
 * titles are reported once each, as UTF-8 whatever the station sent. */
static void check_station(const char *path)
{
    Stream *s = stream_open(address(path));
    unsigned char got[5000];
    char title[128];
    size_t i, n;

    CHECK(s != NULL);
    CHECK_INT(settle(s), STREAM_OPEN);
    CHECK_STR(stream_name(s), "Test Radio");
    CHECK_INT(stream_bitrate(s), 128);
    n = stream_read_wait(s, got, sizeof got, 3000);
    CHECK_INT((int)n, (int)sizeof got);
    for (i = 0; i < n && got[i] == file_data[i % file_size]; i++)
        ;
    CHECK_INT((int)i, (int)n);          /* the file, over and over, nothing else */
    CHECK(stream_take_title(s, title, sizeof title));
    /* Five thousand bytes in, the second title (sent as Latin-1) has arrived. */
    CHECK_STR(title, "\xC3\x84rtist - S\xC3\xAB" "cond Song");
    CHECK(!stream_take_title(s, title, sizeof title));
    CHECK_INT(stream_state(s), STREAM_OPEN);
    stream_close(s);
}

/* HTTP Live Streaming: the segments' audio arrives as one stream. */
static void test_hls(void)
{
    unsigned char got[12000];
    Stream *s;
    size_t i, n, tag = 0;

    /* The tag at the front of a bare audio segment is not audio. */
    if (memcmp(file_data, "ID3", 3) == 0)
        tag = 10 + ((size_t)file_data[6] << 21 | (size_t)file_data[7] << 14 | (size_t)file_data[8] << 7 | file_data[9]);

    /* A finished recording, its three segments addressed in three ways. */
    s = stream_open(address("/hls/vod.m3u8"));
    CHECK(settle(s) != STREAM_FAILED);      /* so short it may be over already */
    CHECK_STR(stream_content_type(s), "audio/mpeg");
    n = stream_read_wait(s, got, sizeof got, 5000);
    CHECK_INT((int)n, (int)(file_size - tag) * 3);
    for (i = 0; i < n && got[i] == file_data[tag + i % (file_size - tag)]; i++)
        ;
    CHECK_INT((int)i, (int)n);
    CHECK_INT(stream_state(s), STREAM_ENDED);
    stream_close(s);

    /* A broadcast in transport stream segments: the best quality that
     * answers is taken, and the audio picked out from among tables, headers
     * and video. */
    s = stream_open(address("/hls/ts.m3u8"));
    CHECK_INT(settle(s), STREAM_OPEN);
    CHECK_STR(stream_content_type(s), "audio/mpeg");
    CHECK_INT(stream_bitrate(s), 96);
    n = stream_read_wait(s, got, sizeof got, 8000);     /* more than the list first holds */
    CHECK_INT((int)n, (int)sizeof got);
    for (i = 0; i < n && got[i] == file_data[i % file_size]; i++)
        ;
    CHECK_INT((int)i, (int)n);
    CHECK_INT(stream_state(s), STREAM_OPEN);
    stream_close(s);

    /* Bare AAC segments: the tag in front of each is left out. */
    s = stream_open(address("/hls/aac.m3u8"));
    CHECK_INT(settle(s), STREAM_OPEN);
    CHECK_STR(stream_content_type(s), "audio/aac");
    n = stream_read_wait(s, got, 4, 3000);
    CHECK(n == 4 && got[0] == 0xFF && (got[1] & 0xF6) == 0xF0);
    CHECK(stream_take_title(s, (char *)got, sizeof got));
    CHECK_STR((char *)got, "HLS Artist - HLS Song");
    stream_close(s);

    /* A video: its separately listed sound is what gets played. */
    s = stream_open(address("/hls/video.m3u8"));
    CHECK_INT(settle(s), STREAM_OPEN);
    CHECK_STR(stream_content_type(s), "audio/aac");
    stream_close(s);

    /* MP4 fragments: the AAC frames of the sound track come out, each
     * behind a header of its own, with the video left aside. */
    s = stream_open(address("/hls/mp4.m3u8"));
    CHECK_INT(settle(s), STREAM_OPEN);
    CHECK_STR(stream_content_type(s), "audio/aac");
    for (i = 0, n = 0; i < 40; i++) {       /* forty frames: the file's, and round again */
        unsigned char header[7];
        size_t size, original;

        if (n + 7 > aac_size)
            n = 0;
        original = (size_t)(aac_data[n + 3] & 3) << 11 | (size_t)aac_data[n + 4] << 3 | aac_data[n + 5] >> 5;
        if (stream_read_wait(s, header, sizeof header, 5000) != sizeof header)
            break;
        size = (size_t)(header[3] & 3) << 11 | (size_t)header[4] << 3 | header[5] >> 5;
        if (header[0] != 0xFF || header[1] != 0xF1 || size != original || (header[2] & 0xFC) != (aac_data[n + 2] & 0xFC) ||
            stream_read_wait(s, got, size - 7, 5000) != size - 7 || memcmp(got, aac_data + n + 7, size - 7) != 0)
            break;
        n += original;
    }
    CHECK_INT((int)i, 40);
    stream_close(s);
}

static void test_refusals(void)
{
    Stream *s = stream_open(address("/nothing-here"));
    char url[64];

    CHECK_INT(settle(s), STREAM_FAILED);
    stream_close(s);
    s = stream_open(address("/hls.m3u8"));          /* segments that do not exist */
    CHECK_INT(settle(s), STREAM_FAILED);
    stream_close(s);
    s = stream_open(address("/hls/key.m3u8"));      /* encrypted: refused */
    CHECK_INT(settle(s), STREAM_FAILED);
    stream_close(s);
    snprintf(url, sizeof url, "http://127.0.0.1:%d/", port + 1);    /* nobody listening */
    s = stream_open(url);
    CHECK_INT(settle(s), STREAM_FAILED);
    stream_close(s);
    s = stream_open("not an address");
    CHECK_INT(settle(s), STREAM_FAILED);
    stream_close(s);
    /* Closing at any moment is safe: while connecting, and mid-stream. */
    s = stream_open(address("/radio"));
    stream_close(s);
    s = stream_open(address("/radio"));
    settle(s);
    stream_close(s);
}

/* Stations in the formats our own decoders handle, fed from the stream.
 * The short test files come round again and again (as new songs, for Ogg),
 * so playing on for `play_ms` crosses several joins. */
static void check_decoded(const char *path, const char *name, int play_ms)
{
    int waited;

    CHECK(audio_open(address(path)));
    audio_play();
    for (waited = 0; audio_buffering() && waited < 8000 && !audio_take_failed(); waited += 20) {
        audio_update();
        plat_sleep_ms(20);
    }
    CHECK(!audio_buffering());
    CHECK_INT(audio_state(), AUDIO_PLAYING);
    CHECK_STR(audio_stream_name(), name);
    CHECK(audio_sample_rate() > 0);
    CHECK(strstr(path, "ogg") || strstr(path, "opus") ? audio_stream_bitrate() == 0 : 1);   /* none stated; too early to tell */
    plat_sleep_ms(play_ms);
    printf("  %s: playing after %d ms, %d Hz, position %.2f s\n", path, waited, audio_sample_rate(), audio_position());
    CHECK(audio_position() > play_ms / 1000.0 * 0.6);
    CHECK_INT(audio_state(), AUDIO_PLAYING);
}

static void test_aac_playback(void)
{
    int waited;

    check_decoded("/radio.ogg", "Test Radio Ogg", 1500);
    check_decoded("/radio.opus", "Test Radio Ogg", 1500);
    check_decoded("/hls/aac.m3u8", "", 800);
    check_decoded("/hls/ts.m3u8", "", 800);
    check_decoded("/hls/mp4.m3u8", "", 800);
    CHECK(audio_open(address("/radio.aac")));
    audio_play();
    for (waited = 0; audio_buffering() && waited < 8000 && !audio_take_failed(); waited += 20) {
        audio_update();
        plat_sleep_ms(20);
    }
    CHECK(!audio_buffering());
    CHECK_INT(audio_state(), AUDIO_PLAYING);
    CHECK_STR(audio_stream_name(), "Test Radio AAC");
    CHECK(audio_sample_rate() > 0);
    plat_sleep_ms(1500);
    printf("  AAC station: playing after %d ms, %d Hz, position %.2f s\n", waited, audio_sample_rate(),
           audio_position());
    CHECK(audio_position() > 1.0);          /* and still going, past the loop point of the short file */
    CHECK_INT(audio_state(), AUDIO_PLAYING);
    audio_seek(10);
    CHECK(audio_position() < 5.0);
}

/* The audio engine plays a station: it connects in the background, starts
 * once enough is buffered, and reports the station's details. */
static void test_playback(void)
{
    char title[128];
    int waited;

    CHECK(audio_init());
    audio_set_volume(0);
    test_aac_playback();
    CHECK(audio_open(address("/radio")));
    CHECK(audio_is_stream() && audio_buffering());
    audio_play();
    for (waited = 0; audio_buffering() && waited < 8000 && !audio_take_failed(); waited += 20) {
        audio_update();
        plat_sleep_ms(20);
    }
    CHECK(!audio_buffering());
    CHECK_INT(audio_state(), AUDIO_PLAYING);
    CHECK_STR(audio_stream_name(), "Test Radio");
    CHECK_INT(audio_stream_bitrate(), 128);
    CHECK_INT(audio_sample_rate(), 48000);
    CHECK(audio_length() == 0.0);
    plat_sleep_ms(700);
    printf("  station: playing after %d ms, position %.2f s\n", waited, audio_position());
    CHECK(audio_position() > 0.3);
    CHECK(audio_stream_title(title, sizeof title));
    audio_seek(10);                                 /* ignored: a stream cannot be wound */
    CHECK(audio_position() < 5.0);
    CHECK_INT(audio_queue_next(address("/radio")), 0);

    /* An address that leads nowhere fails a moment after opening, not at it. */
    CHECK(audio_open(address("/nothing-here")));
    audio_play();
    for (waited = 0; waited < 5000 && !audio_take_failed(); waited += 20) {
        audio_update();
        plat_sleep_ms(20);
    }
    CHECK(waited < 5000);
    CHECK_INT(audio_state(), AUDIO_STOPPED);
    CHECK(!audio_is_stream());
    audio_shutdown();
}

int main(int argc, char **argv)
{
    char command[1024], path[512];
    struct stat st;
    Stream *probe;
    FILE *f;
    int tries, up = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: test_stream <media directory>\n");
        return 2;
    }
    test_begin("stream");
    snprintf(path, sizeof path, "%s/sfx.mp3", argv[1]);
    f = fopen(path, "rb");
    if (!f || stat(path, &st) != 0) {
        perror(path);
        return 2;
    }
    file_size = (size_t)st.st_size;
    file_data = malloc(file_size);
    CHECK(fread(file_data, 1, file_size, f) == file_size);
    fclose(f);
    snprintf(command, sizeof command, "%s/sfx.aac", argv[1]);
    f = fopen(command, "rb");
    aac_data = malloc(65536);
    aac_size = f ? fread(aac_data, 1, 65536, f) : 0;
    CHECK(aac_size > 0);
    if (f)
        fclose(f);

    port = 20000 + (int)(getpid() % 20000);
    snprintf(command, sizeof command, "python3 tests/stream_server.py %d '%s' '%s' >/dev/null 2>&1 &", port, path,
             argv[1]);
    CHECK(system(command) == 0);
    for (tries = 0; tries < 100 && !up; tries++) {      /* until the server answers */
        probe = stream_open(address("/station.pls"));
        up = settle(probe) == STREAM_OPEN;
        stream_close(probe);
        if (!up)
            plat_sleep_ms(50);
    }
    CHECK(up);

    test_addresses();
    if (up) {
        test_file();
        check_station("/radio");
        check_station("/old");              /* the old "ICY 200 OK" status line */
        check_station("/redirect");
        check_station("/station.pls");
        test_hls();
        test_refusals();
        test_playback();
        probe = stream_open(address("/quit"));
        settle(probe);
        stream_close(probe);
    }
    plat_sleep_ms(200);     /* let the reader threads finish, so nothing looks leaked */
    free(file_data);
    free(aac_data);
    return test_end();
}
