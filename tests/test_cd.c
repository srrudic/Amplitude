/* Audio CDs, tried on an image of a disc (a CUE sheet and a raw file), which
 * goes through everything a real disc does except the drive itself: the
 * track list, track paths, the decoder with its read-ahead thread, seeking,
 * and playback through the audio engine (muted, like the audio test).
 *
 *     build/test/test_cd
 */
#include "audio.h"
#include "cd.h"
#include "cdnames.h"
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
             "PERFORMER \"The Testers\"\r\nTITLE \"Caf\xE9 Album\"\r\n"
             "FILE \"disc.bin\" BINARY\r\n"
             "  TRACK 01 AUDIO\r\n    TITLE \"Opening\"\r\n    PERFORMER \"The Testers\"\r\n    INDEX 01 00:00:00\r\n"
             "  TRACK 02 AUDIO\r\n    TITLE \"Duet\"\r\n    PERFORMER \"A Guest\"\r\n"
             "    INDEX 00 00:02:50\r\n    INDEX 01 00:03:00\r\n"
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

/* Lays texts out as CD-Text packs of one kind: the album's first, then
 * each track's, twelve characters to a pack. Returns the bytes written. */
static size_t text_packs(unsigned char *out, int kind, int block, const char *const *texts, int count)
{
    unsigned char *pack = NULL;
    size_t size = 0;
    int used = 12, i;
    const char *c;

    for (i = 0; i < count; i++)
        for (c = texts[i];; c++) {
            if (used == 12) {
                pack = out + size;
                memset(pack, 0, 18);
                pack[0] = (unsigned char)kind;
                pack[1] = (unsigned char)i;
                pack[2] = (unsigned char)(size / 18);
                pack[3] = (unsigned char)(block << 4);
                size += 18;
                used = 0;
            }
            pack[4 + used++] = (unsigned char)*c;
            if (!*c)
                break;
        }
    return size;
}

/* What a disc says of itself: CD-Text, and the titles in an image's sheet. */
/* Two tracks open at once, as in playback (the one heard and the one to
 * follow): they must not read turn and turn about, which on a drive is
 * the head rushing to and fro. On a disc of its own, with tracks longer
 * than what is read ahead. */
static void test_turns(void)
{
    enum { TRACK_S = 6 };
    static short sound[CD_RATE / 10 * 2];
    static char silence[CD_SECTOR * CD_SECTORS_PER_S];
    char sheet[256], paths[2][600];
    Codec heard, next;
    long jumps;
    FILE *f;
    int i;

    f = fopen(test_path("long.bin"), "wb");
    for (i = 0; f && i < 2 * TRACK_S; i++)
        fwrite(silence, 1, sizeof silence, f);
    if (f)
        fclose(f);
    snprintf(sheet, sizeof sheet, "FILE \"long.bin\" BINARY\n TRACK 01 AUDIO\n  INDEX 01 00:00:00\n"
                                  " TRACK 02 AUDIO\n  INDEX 01 00:%02d:00\n", TRACK_S);
    test_write(test_path("long.cue"), sheet, strlen(sheet));
    cd_make_path(paths[0], sizeof paths[0], test_path("long.cue"), 1);
    cd_make_path(paths[1], sizeof paths[1], test_path("long.cue"), 2);

    memset(&heard, 0, sizeof heard);
    memset(&next, 0, sizeof next);
    usleep(100 * 1000);         /* threads of the tests before have let go */
    cd_image_jumps = 0;
    CHECK(codec_open(paths[1], &heard));
    CHECK(codec_open(paths[0], &next));     /* returns at once: it is not waited for */
    if (!heard.state || !next.state)
        return;
    CHECK(heard.seek(heard.state, 0));      /* as a decoder does after opening: nothing is read again */
    usleep(300 * 1000);
    /* Only the track heard has been read, as far as is read ahead. */
    CHECK_INT((int)cd_image_jumps, 1);
    /* Nor does listening change that, or a seek within what is in hand... */
    for (i = 0; i < 5; i++)
        CHECK_INT((int)heard.read(heard.state, sound, CD_RATE / 10), CD_RATE / 10);
    CHECK(heard.seek(heard.state, CD_RATE));
    usleep(100 * 1000);
    CHECK_INT((int)cd_image_jumps, 1);
    /* ...until the track has been read to its end: then the other one is. */
    for (i = 0; i < 15; i++) {
        CHECK_INT((int)heard.read(heard.state, sound, CD_RATE / 10), CD_RATE / 10);
        usleep(30 * 1000);
    }
    usleep(200 * 1000);
    CHECK_INT((int)cd_image_jumps, 2);
    /* A seek to somewhere not in hand is one more, and takes the drive back. */
    jumps = cd_image_jumps;
    CHECK(heard.seek(heard.state, 0));
    usleep(100 * 1000);
    CHECK_INT((int)(cd_image_jumps - jumps), 1);
    /* When the other becomes the one heard, it finds its sound waiting. */
    CHECK_INT((int)next.read(next.state, sound, CD_RATE / 10), CD_RATE / 10);
    heard.close(heard.state);
    next.close(next.state);
    usleep(100 * 1000);
}

static void test_disc_names(void)
{
    static const char *const titles[] = { "An Album With a Long Name", "First Song of Them", "Caf\xE9", "\t", "Last" };
    static const char *const artists[] = { "The Band", "The Band", "\t", "Somebody Else", "" };
    static const char *const other[] = { "Ein Album", "Erstes", "Zweites", "Drittes", "Viertes" };
    static const char *const genre[] = { "Not a title" };
    static CdNames names;
    unsigned char packs[2048];
    size_t size = 0;
    CdToc toc;
    Cd *cd;
    int i;

    memset(&toc, 0, sizeof toc);
    toc.count = 5;                      /* four songs and a data track */
    for (i = 0; i < 5; i++) {
        toc.track[i].number = i + 1;
        toc.track[i].audio = i < 4;
    }
    size += text_packs(packs + size, 0x80, 0, titles, 5);
    size += text_packs(packs + size, 0x81, 0, artists, 5);
    size += text_packs(packs + size, 0x87, 0, genre, 1);
    size += text_packs(packs + size, 0x80, 1, other, 5);        /* a second language: not used */
    CHECK(cd_text_parse(packs, size, &toc, &names));
    CHECK_STR(names.album, "An Album With a Long Name");
    CHECK_STR(names.artist, "The Band");
    CHECK_INT(names.count, 4);
    CHECK_STR(names.track[0].title, "First Song of Them");
    CHECK_STR(names.track[0].artist, "");                       /* the album's */
    CHECK_STR(names.track[1].title, "Caf\xC3\xA9");
    CHECK_STR(names.track[1].artist, "");                       /* "as before" */
    CHECK_STR(names.track[2].title, "Caf\xC3\xA9");             /* "as before" */
    CHECK_STR(names.track[2].artist, "Somebody Else");
    CHECK_INT(names.track[3].number, 4);
    CHECK_STR(names.track[3].title, "Last");
    CHECK(!cd_text_parse(packs, 0, &toc, &names));
    CHECK(!cd_text_parse(packs + 18 * 12, 18, &toc, &names));   /* a scrap with no track title in it */
    packs[3] |= 0x80;
    size = text_packs(packs, 0x80, 0, titles, 5);
    for (i = 0; (size_t)i < size; i += 18)
        packs[i + 3] |= 0x80;                                   /* two-byte characters: not read */
    CHECK(!cd_text_parse(packs, size, &toc, &names));

    cd = cd_open(test_path("disc.cue"));
    CHECK(cd != NULL && cd_read_names(cd, &names));
    CHECK_STR(names.artist, "The Testers");
    CHECK_STR(names.album, "Caf\xC3\xA9 Album");
    CHECK_INT(names.count, 2);                                  /* tracks 3 and 4 have no title */
    CHECK_STR(names.track[0].title, "Opening");
    CHECK_STR(names.track[0].artist, "");
    CHECK_INT(names.track[1].number, 2);
    CHECK_STR(names.track[1].title, "Duet");
    CHECK_STR(names.track[1].artist, "A Guest");
    if (cd)
        cd_close(cd);
}

/* Looking names up, without the looking: the IDs a disc is known by, and
 * reading what the two databases answer. */
static void test_names(void)
{
    /* The example from MusicBrainz's description of its disc ID. */
    static const long starts[] = { 0, 15213, 32164, 46442, 63264, 80339 };
    static const char musicbrainz[] =
        "{\"id\":\"x\",\"releases\":[{\"title\":\"Other\",\"media\":[{\"position\":1,\"discs\":[{\"id\":\"nope\"}],"
        "\"tracks\":[{\"position\":1,\"title\":\"Wrong\"}]}]},"
        "{\"artist-credit\":[{\"name\":\"Sigur R\\u00f3s\",\"joinphrase\":\"\"}],\"title\":\"The \\\"Album\\\"\","
        "\"media\":[{\"position\":1,\"discs\":[],\"tracks\":[]},"
        "{\"discs\":[{\"id\":\"other\"},{\"sectors\":95462,\"id\":\"49HHV7Eb8UKF3aQiNmu1GR8vKTY-\"}],\"position\":2,"
        "\"tracks\":[{\"number\":\"A1\",\"position\":1,\"title\":\"First [x], {y}\","
        "\"artist-credit\":[{\"name\":\"Sigur R\\u00f3s\",\"joinphrase\":\"\"}]},"
        "{\"position\":2,\"title\":\"Second\",\"artist-credit\":[{\"name\":\"A\",\"joinphrase\":\" feat. \"},"
        "{\"name\":\"\\u65e5\\u672c\",\"joinphrase\":\"\"}],\"recording\":{\"title\":\"not this\"}}]}]}]}";
    static const char cddb[] =
        "210 rock 3f04f806 CD database entry follows (until terminating `.')\r\n# xmcd\r\nDISCID=3f04f806\r\n"
        "DTITLE=Various / A Long Alb\r\nDTITLE=um Name\r\nDYEAR=1999\r\nTTITLE0=One Artist / One\r\n"
        "TTITLE1=Tw\xF6\r\nTTITLE5=Six\r\nEXTD=\r\n.\r\n";
    static CdNames names;
    char id[29], category[16], disc[16];
    CdToc toc;
    int i;

    memset(&toc, 0, sizeof toc);
    toc.count = 6;
    for (i = 0; i < 6; i++) {
        toc.track[i].number = i + 1;
        toc.track[i].audio = 1;
        toc.track[i].start = starts[i];
        toc.track[i].sectors = (i < 5 ? starts[i + 1] : 95462 - 150) - starts[i];
    }
    cd_names_musicbrainz_id(&toc, id);
    CHECK_STR(id, "49HHV7Eb8UKF3aQiNmu1GR8vKTY-");
    CHECK_INT((int)(cd_names_cddb_id(&toc) & 0xFFFFFF), (1272 - 2) << 8 | 6);   /* seconds and tracks */
    CHECK_INT((int)(cd_names_cddb_id(&toc) >> 24), (2 + (2+0+4) + (4+3+0) + (6+2+1) + (8+4+5) + (1+0+7+3)) % 255);

    CHECK(cd_names_parse_musicbrainz(musicbrainz, id, &toc, &names));
    CHECK_STR(names.artist, "Sigur R\xC3\xB3s");
    CHECK_STR(names.album, "The \"Album\"");
    CHECK_INT(names.count, 2);
    CHECK_INT(names.track[0].number, 1);
    CHECK_STR(names.track[0].title, "First [x], {y}");
    CHECK_STR(names.track[0].artist, "");                   /* the album's */
    CHECK_STR(names.track[1].title, "Second");
    CHECK_STR(names.track[1].artist, "A feat. \xE6\x97\xA5\xE6\x9C\xAC");
    CHECK(!cd_names_parse_musicbrainz("{\"error\":\"Not Found\"}", id, &toc, &names));
    CHECK(!cd_names_parse_musicbrainz("", id, &toc, &names));
    CHECK(!cd_names_parse_musicbrainz("{\"releases\":[{\"media\":[{\"tracks\":[{\"title\":\"unfinished", id, &toc, &names));

    CHECK(cd_names_parse_cddb_query("200 rock 3f04f806 Various / Album\r\n", category, sizeof category, disc, sizeof disc));
    CHECK_STR(category, "rock");
    CHECK_STR(disc, "3f04f806");
    CHECK(cd_names_parse_cddb_query("211 Found inexact matches, list follows\r\nmisc 12345678 A / B\r\njazz 1 C\r\n.\r\n",
                                    category, sizeof category, disc, sizeof disc));
    CHECK_STR(category, "misc");
    CHECK_STR(disc, "12345678");
    CHECK(!cd_names_parse_cddb_query("202 No match found\r\n", category, sizeof category, disc, sizeof disc));
    CHECK(!cd_names_parse_cddb_query("210 Found exact matches\r\n.\r\n", category, sizeof category, disc, sizeof disc));
    CHECK(cd_names_parse_cddb(cddb, &toc, &names));
    CHECK_STR(names.artist, "Various");
    CHECK_STR(names.album, "A Long Album Name");
    CHECK_INT(names.count, 3);
    CHECK_STR(names.track[0].artist, "One Artist");
    CHECK_STR(names.track[0].title, "One");
    CHECK_STR(names.track[1].title, "Tw\xC3\xB6");          /* sent as Latin-1 */
    CHECK_INT(names.track[2].number, 6);
    CHECK_STR(names.track[2].title, "Six");
    CHECK(!cd_names_parse_cddb("401 Specified CDDB entry not found\r\n", &toc, &names));
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
    test_turns();
    test_disc_names();
    test_names();
    test_playback();
    usleep(200 * 1000);     /* let the reader threads finish, so nothing looks leaked */
    return test_end();
}
