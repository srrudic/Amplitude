/* See cd.h. Drives are the platform's business; disc images are read here. */
#include "cd.h"

#include "platform.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>

struct Cd {
    PlatCd *drive;          /* a real disc, or */
    FILE *image;            /* the raw file of an image */
    CdToc toc;
};

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
    while (fgets(line, sizeof line, f)) {
        const char *p = cue_word(line, word, sizeof word);

        if (strcmp(word, "FILE") == 0) {
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

int cd_read(Cd *cd, long sector, int count, void *out)
{
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
    free(cd);
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
