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
/* For the tests: how often reading from an image went on somewhere other
 * than where the read before it ended, whichever handle did the reading. */
extern long cd_image_jumps;
/* And how long each read from an image is to take, to stand in for a drive. */
extern int cd_image_delay_ms;

/* Names of the album and its songs. */
typedef struct {
    char device[CD_DEVICE_MAX];     /* the disc these names are for */
    char artist[128], album[160];
    int count;
    struct {
        int number;                 /* as on the disc */
        char artist[128];           /* empty when it is the album's */
        char title[200];
    } track[CD_MAX_TRACKS];
} CdNames;

/* The names a disc carries itself: "CD-Text", which some pressed albums and
 * many home-made discs have and which most but not all drives can read; for
 * an image, the TITLE and PERFORMER lines of its CUE sheet. Returns 0 if
 * there are none. `out->device` is left to the caller. */
int  cd_read_names(Cd *cd, CdNames *out);
/* Decodes CD-Text as the drive delivers it, less the four bytes in front:
 * "packs" of 18 bytes. Exposed for the tests. */
int  cd_text_parse(const unsigned char *packs, size_t size, const CdToc *toc, CdNames *out);

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
/* Asks the drive for the disc's CD-Text: the first `size` bytes of its
 * answer, which begins with its own length (two bytes, not counting
 * themselves) and two more bytes. Returns 1 if the drive answered. */
int     plat_cd_text(PlatCd *cd, unsigned char *out, int size);
void    plat_cd_close(PlatCd *cd);

#endif
