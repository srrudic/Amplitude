/* Audio CDs: the list of tracks on a disc and the raw sound in its sectors.
 *
 * A disc is named by its device ("/dev/sr0", "D:"), or by a CUE sheet that
 * describes an image of a disc in one raw file (a .bin of 2352-byte audio
 * sectors), which plays the same way. A track on it is written as a path of
 * its own,  cdda://<device or sheet>/<track number>,  so that it can stand
 * in a playlist like a file. */
#ifndef CD_H
#define CD_H

#include <stddef.h>
#include <string.h>

#define CD_PREFIX         "cdda://"
#define CD_SECTOR         2352      /* bytes of sound in a sector: */
#define CD_SECTOR_FRAMES  588       /* that many 16-bit stereo frames */
#define CD_SECTORS_PER_S  75
#define CD_RATE           44100
#define CD_MAX_TRACKS     99
#define CD_DEVICE_MAX     1024

typedef struct {
    int number;             /* as on the disc, from 1 */
    long start, sectors;    /* where it lies */
    int audio;              /* 0 for the data track of a mixed disc */
} CdTrack;

typedef struct {
    int count;
    CdTrack track[CD_MAX_TRACKS];
} CdToc;

typedef struct Cd Cd;

/* Opens a drive with an audio disc in it, or a CUE sheet. NULL if there is
 * no such thing or no audio track on it. */
Cd  *cd_open(const char *device);
const CdToc *cd_toc(const Cd *cd);
/* Reads `count` sectors of sound, CD_SECTOR bytes each. Returns 1 if it could. */
int  cd_read(Cd *cd, long sector, int count, void *out);
void cd_close(Cd *cd);

/* The first drive that holds an audio disc. Returns 0 if none does. */
int  cd_find_disc(char *device, size_t size);

/* Track paths. */
static inline int path_is_cd(const char *path)
{
    return strncmp(path, CD_PREFIX, sizeof CD_PREFIX - 1) == 0;
}
void cd_make_path(char *out, size_t size, const char *device, int track);
int  cd_split_path(const char *path, char *device, size_t size, int *track);

/* --- What the platform provides (cd_linux.c, cd_win32.c) --- */

typedef struct PlatCd PlatCd;

#define PLAT_CD_NAME 32
/* The names of the machine's CD drives; returns how many. */
int     plat_cd_drives(char names[][PLAT_CD_NAME], int max);
PlatCd *plat_cd_open(const char *device);
int     plat_cd_toc(PlatCd *cd, CdToc *toc);
int     plat_cd_read(PlatCd *cd, long sector, int count, void *out);
void    plat_cd_close(PlatCd *cd);

#endif
