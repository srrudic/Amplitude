/* CD drives on Windows (see cd.h): the drive is opened as a device and asked
 * for its table of contents and for raw sectors. That is how Windows NT and
 * everything since does it; Windows 95, 98 and Me have no such interface,
 * so audio CDs do not play there. Only kernel32 is needed. */
#include "cd.h"

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

/* From ntddcdrm.h, which not every toolchain has. */
#define CD_IOCTL_READ_TOC   0x00024000
#define CD_IOCTL_RAW_READ   0x0002403E
#define CD_RAW_MODE_AUDIO   2
#define CD_DATA_TRACK       0x04
#define CD_MSF_OFFSET       150         /* the two seconds before sector 0 */
#define SESSION_GAP         11400

typedef struct {
    UCHAR reserved, control_adr, number, reserved1;
    UCHAR address[4];                   /* 0, minutes, seconds, sectors */
} TocEntry;

typedef struct {
    UCHAR length[2], first, last;
    TocEntry track[100];                /* the last is the end of the disc */
} Toc;

typedef struct {
    LARGE_INTEGER offset;               /* the sector times 2048, whatever its real size */
    ULONG sectors;
    int mode;
} RawRead;

struct PlatCd {
    HANDLE drive;
};

int plat_cd_drives(char names[][PLAT_CD_NAME], int max)
{
    DWORD present = GetLogicalDrives();
    int letter, count = 0;

    for (letter = 0; letter < 26 && count < max; letter++) {
        char root[] = "A:\\";

        root[0] = (char)('A' + letter);
        if ((present & (1u << letter)) && GetDriveTypeA(root) == DRIVE_CDROM) {
            names[count][0] = root[0];
            names[count][1] = ':';
            names[count][2] = '\0';
            count++;
        }
    }
    return count;
}

PlatCd *plat_cd_open(const char *device)
{
    char name[16];
    PlatCd *cd;
    HANDLE drive;

    if (!device[0] || device[1] != ':' || device[2])
        return NULL;
    snprintf(name, sizeof name, "\\\\.\\%c:", device[0]);
    drive = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (drive == INVALID_HANDLE_VALUE)
        return NULL;
    cd = malloc(sizeof *cd);
    if (!cd) {
        CloseHandle(drive);
        return NULL;
    }
    cd->drive = drive;
    return cd;
}

static long sector_of(const TocEntry *entry)
{
    return ((long)entry->address[1] * 60 + entry->address[2]) * CD_SECTORS_PER_S + entry->address[3] - CD_MSF_OFFSET;
}

int plat_cd_toc(PlatCd *cd, CdToc *toc)
{
    Toc raw;
    DWORD got = 0;
    int i, tracks;

    memset(&raw, 0, sizeof raw);
    if (!DeviceIoControl(cd->drive, CD_IOCTL_READ_TOC, NULL, 0, &raw, sizeof raw, &got, NULL))
        return 0;
    tracks = raw.last - raw.first + 1;
    if (tracks < 1 || tracks > CD_MAX_TRACKS)
        return 0;
    toc->count = tracks;
    for (i = 0; i < tracks; i++) {
        CdTrack *track = &toc->track[i];
        long next = sector_of(&raw.track[i + 1]);

        track->number = raw.track[i].number;
        track->start = sector_of(&raw.track[i]);
        track->audio = !(raw.track[i].control_adr & CD_DATA_TRACK);
        if (track->audio && i + 1 < tracks && (raw.track[i + 1].control_adr & CD_DATA_TRACK))
            next -= SESSION_GAP;
        track->sectors = next > track->start ? next - track->start : 0;
    }
    return 1;
}

int plat_cd_read(PlatCd *cd, long sector, int count, void *out)
{
    RawRead request;
    DWORD got = 0;

    memset(&request, 0, sizeof request);
    request.offset.QuadPart = (LONGLONG)sector * 2048;
    request.sectors = (ULONG)count;
    request.mode = CD_RAW_MODE_AUDIO;
    return DeviceIoControl(cd->drive, CD_IOCTL_RAW_READ, &request, sizeof request, out, (DWORD)count * CD_SECTOR,
                           &got, NULL) && got == (DWORD)count * CD_SECTOR;
}

void plat_cd_close(PlatCd *cd)
{
    CloseHandle(cd->drive);
    free(cd);
}
