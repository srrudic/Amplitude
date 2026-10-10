/* See cd.h. Drives are the platform's business; disc images are read here. */
#include "cd.h"

#include "platform.h"
#include "tags.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>

struct Cd {
    PlatCd *drive;          /* a real disc, or */
    FILE *image;            /* the raw file of an image */
    CdToc toc;
    CdNames *named;         /* what an image's sheet says the songs are called */
};

#define TEXT_PACK       18
#define TEXT_PAYLOAD    12
#define TEXT_MAX        (4 + 8 * 255 * TEXT_PACK)   /* eight languages of 255 packs */
#define TEXT_TITLE      0x80        /* kinds of pack */
#define TEXT_PERFORMER  0x81

/* --- CUE sheets ---------------------------------------------------------------
 * A text file. The lines that matter here:
 *     FILE "disc.bin" BINARY
 *       TRACK 01 AUDIO
 *         INDEX 01 00:00:00        (minutes:seconds:sectors into the file)
 * Only sheets with a single raw file are understood; those that name one
 * WAV or FLAC file per track describe an album, not a disc image. */

/* The next word of a line, or the text between quotes. */
static const char *cue_word(const char *p, char *out, size_t size)
{
    size_t len = 0;
    char stop = ' ';

    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '"') {
        stop = '"';
        p++;
    }
    for (; *p && *p != stop && *p != '\r' && *p != '\n' && !(stop == ' ' && *p == '\t'); p++)
        if (len + 1 < size)
            out[len++] = *p;
    out[len] = '\0';
    return *p == stop && *p ? p + 1 : p;
}

static int open_image(Cd *cd, const char *sheet)
{
    char line[1024], word[1024], name[CD_DEVICE_MAX] = "", path[CD_DEVICE_MAX * 2];
    FILE *f = plat_fopen(sheet, "r");
    CdToc *toc = &cd->toc;
    long sectors;
    int i;

    if (!f)
        return 0;
    cd->named = calloc(1, sizeof *cd->named);
    while (fgets(line, sizeof line, f)) {
        const char *p = cue_word(line, word, sizeof word);
        int is_title = strcmp(word, "TITLE") == 0;

        if (cd->named && (is_title || strcmp(word, "PERFORMER") == 0)) {
            /* Before the first track these are the album's, then each track's. */
            CdNames *names = cd->named;
            char *into = !toc->count ? (is_title ? names->album : names->artist)
                       : is_title ? names->track[toc->count - 1].title : names->track[toc->count - 1].artist;
            size_t room = !toc->count ? (is_title ? sizeof names->album : sizeof names->artist)
                        : is_title ? sizeof names->track[0].title : sizeof names->track[0].artist;

            cue_word(p, word, sizeof word);
            text_to_utf8(into, room, (const unsigned char *)word, strlen(word), TEXT_UTF8);
        } else if (strcmp(word, "FILE") == 0) {
            if (name[0]) {
                toc->count = 0;     /* a file per track: not an image */
                break;
            }
            p = cue_word(p, name, sizeof name);
            cue_word(p, word, sizeof word);
            if (strcmp(word, "BINARY") != 0)
                break;
        } else if (strcmp(word, "TRACK") == 0 && toc->count < CD_MAX_TRACKS) {
            CdTrack *track = &toc->track[toc->count++];

            p = cue_word(p, word, sizeof word);
            track->number = atoi(word);
            cue_word(p, word, sizeof word);
            track->audio = strcmp(word, "AUDIO") == 0;
            track->start = -1;
        } else if (strcmp(word, "INDEX") == 0 && toc->count) {
            char *seconds, *frames;

            p = cue_word(p, word, sizeof word);
            if (atoi(word) != 1)
                continue;
            cue_word(p, word, sizeof word);         /* "mm:ss:ff" */
            seconds = strchr(word, ':');
            frames = seconds ? strchr(seconds + 1, ':') : NULL;
            if (frames)
                toc->track[toc->count - 1].start =
                    ((long)atoi(word) * 60 + atoi(seconds + 1)) * CD_SECTORS_PER_S + atoi(frames + 1);
        }
    }
    fclose(f);
    if (!name[0] || !toc->count)
        return 0;

    /* The raw file lies beside the sheet unless it says otherwise. */
    if (name[0] == '/' || name[0] == '\\' || (name[0] && name[1] == ':')) {
        snprintf(path, sizeof path, "%s", name);
    } else {
        size_t dir = (size_t)(path_basename(sheet) - sheet);

        snprintf(path, sizeof path, "%.*s%s", (int)dir, sheet, name);
    }
    cd->image = plat_fopen(path, "rb");
    if (!cd->image || fseek(cd->image, 0, SEEK_END) != 0)
        return 0;
    sectors = ftell(cd->image) / CD_SECTOR;
    for (i = 0; i < toc->count; i++) {
        long end = i + 1 < toc->count ? toc->track[i + 1].start : sectors;

        if (toc->track[i].start < 0 || end > sectors || end <= toc->track[i].start)
            return 0;
        toc->track[i].sectors = end - toc->track[i].start;
    }
    return 1;
}

/* --- The interface ------------------------------------------------------------- */

Cd *cd_open(const char *device)
{
    Cd *cd = calloc(1, sizeof *cd);
    int i, ok, audio = 0;

    if (!cd)
        return NULL;
    if (path_has_extension(device, ".cue")) {
        ok = open_image(cd, device);
    } else {
        cd->drive = plat_cd_open(device);
        ok = cd->drive && plat_cd_toc(cd->drive, &cd->toc);
    }
    for (i = 0; ok && i < cd->toc.count; i++)
        audio += cd->toc.track[i].audio;
    if (!audio) {
        cd_close(cd);
        return NULL;
    }
    return cd;
}

const CdToc *cd_toc(const Cd *cd)
{
    return &cd->toc;
}

long cd_image_jumps;

int cd_read(Cd *cd, long sector, int count, void *out)
{
    static long expected = -1;

    if (!cd->drive && sector != expected)
        cd_image_jumps++;       /* a drive would have had to move its head */
    expected = sector + count;
    if (cd->drive)
        return plat_cd_read(cd->drive, sector, count, out);
    return fseek(cd->image, sector * CD_SECTOR, SEEK_SET) == 0 &&
           fread(out, CD_SECTOR, (size_t)count, cd->image) == (size_t)count;
}

void cd_close(Cd *cd)
{
    if (cd->drive)
        plat_cd_close(cd->drive);
    if (cd->image)
        fclose(cd->image);
    free(cd->named);
    free(cd);
}

/* --- Names on the disc ----------------------------------------------------------
 * CD-Text comes in packs of 18 bytes: the kind of text (title, performer...),
 * the track that the text at the start of the pack belongs to (0 is the
 * album), a running number, a byte of flags, twelve characters and a
 * checksum. The texts of one kind follow one another through the packs,
 * each ended by a zero byte, in the order of the tracks. Up to eight
 * blocks hold the same in different languages; the first is used, unless it
 * is in two-byte (Japanese) characters. */

int cd_text_parse(const unsigned char *packs, size_t size, const CdToc *toc, CdNames *out)
{
    /* [0] titles, [1] performers; for the album and tracks 1 to 99 */
    char (*text[2])[sizeof out->track[0].title];
    size_t at, lengths[2][CD_MAX_TRACKS + 1];
    int kind, i;

    text[0] = calloc(2 * (CD_MAX_TRACKS + 1), sizeof *text[0]);
    if (!text[0])
        return 0;
    text[1] = text[0] + CD_MAX_TRACKS + 1;
    memset(lengths, 0, sizeof lengths);
    for (at = 0; at + TEXT_PACK <= size; at += TEXT_PACK) {
        const unsigned char *pack = packs + at;
        int track = pack[1] & 0x7F;

        kind = pack[0] - TEXT_TITLE;
        if (kind < 0 || kind > 1 || (pack[3] & 0xF0))       /* another kind, language or alphabet */
            continue;
        for (i = 0; i < TEXT_PAYLOAD && track <= CD_MAX_TRACKS; i++) {
            unsigned char c = pack[4 + i];

            if (!c)
                track++;        /* the next track's text follows */
            else if (lengths[kind][track] + 1 < sizeof text[0][0])
                text[kind][track][lengths[kind][track]++] = (char)c;
        }
    }
    for (kind = 0; kind < 2; kind++)
        for (i = 0; i <= CD_MAX_TRACKS; i++) {
            text[kind][i][lengths[kind][i]] = '\0';
            /* A lone tab stands for "the same as the track before". */
            if (i > 0 && strcmp(text[kind][i], "\t") == 0)
                memcpy(text[kind][i], text[kind][i - 1], sizeof text[0][0]);
        }

    memset(out, 0, sizeof *out);
    /* Meant to be Latin-1; home-made discs hold whatever the program wrote. */
    text_to_utf8(out->album, sizeof out->album, (const unsigned char *)text[0][0], strlen(text[0][0]), TEXT_UTF8);
    text_to_utf8(out->artist, sizeof out->artist, (const unsigned char *)text[1][0], strlen(text[1][0]), TEXT_UTF8);
    for (i = 0; i < toc->count; i++) {
        int number = toc->track[i].number;

        if (!toc->track[i].audio || number < 1 || number > CD_MAX_TRACKS || !text[0][number][0])
            continue;
        out->track[out->count].number = number;
        text_to_utf8(out->track[out->count].title, sizeof out->track[0].title,
                     (const unsigned char *)text[0][number], strlen(text[0][number]), TEXT_UTF8);
        text_to_utf8(out->track[out->count].artist, sizeof out->track[0].artist,
                     (const unsigned char *)text[1][number], strlen(text[1][number]), TEXT_UTF8);
        if (strcmp(out->track[out->count].artist, out->artist) == 0)
            out->track[out->count].artist[0] = '\0';
        out->count++;
    }
    free(text[0]);
    return out->count > 0;
}

int cd_read_names(Cd *cd, CdNames *out)
{
    unsigned char head[4], *answer;
    int size, ok, i;

    if (!cd->drive) {
        /* An image: the tracks the sheet gives a title. */
        if (!cd->named)
            return 0;
        *out = *cd->named;
        out->count = 0;
        for (i = 0; i < cd->toc.count; i++) {
            if (!cd->toc.track[i].audio || !cd->named->track[i].title[0])
                continue;
            out->track[out->count] = cd->named->track[i];
            out->track[out->count].number = cd->toc.track[i].number;
            if (strcmp(out->track[out->count].artist, out->artist) == 0)
                out->track[out->count].artist[0] = '\0';
            out->count++;
        }
        return out->count > 0;
    }
    /* First how long the answer is, then all of it: asking for more than
     * there is upsets some drives. */
    if (!plat_cd_text(cd->drive, head, sizeof head))
        return 0;
    size = (head[0] << 8 | head[1]) + 2;
    if (size < 4 + TEXT_PACK || size > TEXT_MAX)
        return 0;
    answer = malloc((size_t)size);
    ok = answer && plat_cd_text(cd->drive, answer, size) &&
         cd_text_parse(answer + 4, (size_t)size - 4, &cd->toc, out);
    free(answer);
    return ok;
}

int cd_find_disc(char *device, size_t size)
{
    char names[8][PLAT_CD_NAME];
    int i, count = plat_cd_drives(names, 8);

    for (i = 0; i < count; i++) {
        Cd *cd = cd_open(names[i]);

        if (cd) {
            cd_close(cd);
            snprintf(device, size, "%s", names[i]);
            return 1;
        }
    }
    return 0;
}

void cd_make_path(char *out, size_t size, const char *device, int track)
{
    snprintf(out, size, CD_PREFIX "%s/%d", device, track);
}

int cd_split_path(const char *path, char *device, size_t size, int *track)
{
    const char *slash;
    size_t len;

    if (!path_is_cd(path))
        return 0;
    path += sizeof CD_PREFIX - 1;
    slash = strrchr(path, '/');
    len = slash ? (size_t)(slash - path) : 0;
    if (!len || len >= size || atoi(slash + 1) < 1)
        return 0;
    memcpy(device, path, len);
    device[len] = '\0';
    *track = atoi(slash + 1);
    return 1;
}
