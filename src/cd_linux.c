/* CD drives on Linux (see cd.h): the kernel's CD-ROM ioctls. The drive
 * hands over the sound as it is on the disc, so no sound card cable or
 * mixer is involved. */
#include "cd.h"

#include <fcntl.h>
#include <linux/cdrom.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define SESSION_GAP 11400       /* sectors between the sound and the data of a mixed disc */

struct PlatCd {
    int fd;
};

int plat_cd_drives(char names[][PLAT_CD_NAME], int max)
{
    static const char *const candidates[] = { "/dev/cdrom", "/dev/sr0", "/dev/sr1", "/dev/sr2", "/dev/sr3" };
    struct stat first, st;
    int count = 0, have_first = 0;
    size_t i;

    for (i = 0; i < sizeof candidates / sizeof candidates[0] && count < max; i++) {
        if (stat(candidates[i], &st) != 0)
            continue;
        /* /dev/cdrom is usually another name for one of the others. */
        if (have_first && st.st_rdev == first.st_rdev)
            continue;
        if (!have_first) {
            first = st;
            have_first = 1;
        }
        strcpy(names[count++], candidates[i]);
    }
    return count;
}

PlatCd *plat_cd_open(const char *device)
{
    /* Non-blocking, or opening would wait on an empty or closing tray. */
    int fd = open(device, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    PlatCd *cd;

    if (fd < 0)
        return NULL;
    cd = malloc(sizeof *cd);
    if (!cd) {
        close(fd);
        return NULL;
    }
    cd->fd = fd;
    return cd;
}

static int entry(PlatCd *cd, int track, long *start, int *audio)
{
    struct cdrom_tocentry e;

    memset(&e, 0, sizeof e);
    e.cdte_track = (unsigned char)track;
    e.cdte_format = CDROM_LBA;
    if (ioctl(cd->fd, CDROMREADTOCENTRY, &e) != 0)
        return 0;
    *start = e.cdte_addr.lba;
    *audio = !(e.cdte_ctrl & CDROM_DATA_TRACK);
    return 1;
}

int plat_cd_toc(PlatCd *cd, CdToc *toc)
{
    struct cdrom_tochdr header;
    long end;
    int i, audio;

    if (ioctl(cd->fd, CDROMREADTOCHDR, &header) != 0 || !entry(cd, CDROM_LEADOUT, &end, &audio))
        return 0;
    toc->count = 0;
    for (i = header.cdth_trk0; i <= header.cdth_trk1 && toc->count < CD_MAX_TRACKS; i++) {
        CdTrack *track = &toc->track[toc->count];

        if (!entry(cd, i, &track->start, &track->audio))
            return 0;
        track->number = i;
        toc->count++;
    }
    for (i = 0; i < toc->count; i++) {
        CdTrack *track = &toc->track[i];
        long next = i + 1 < toc->count ? toc->track[i + 1].start : end;

        if (track->audio && i + 1 < toc->count && !toc->track[i + 1].audio)
            next -= SESSION_GAP;
        track->sectors = next > track->start ? next - track->start : 0;
    }
    return toc->count > 0;
}

int plat_cd_read(PlatCd *cd, long sector, int count, void *out)
{
    struct cdrom_read_audio request;

    memset(&request, 0, sizeof request);
    request.addr.lba = (int)sector;
    request.addr_format = CDROM_LBA;
    request.nframes = count;
    request.buf = out;
    return ioctl(cd->fd, CDROMREADAUDIO, &request) == 0;
}

void plat_cd_close(PlatCd *cd)
{
    close(cd->fd);
    free(cd);
}
