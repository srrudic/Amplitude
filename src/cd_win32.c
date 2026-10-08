/* CD drives on Windows (see cd.h), in two ways.
 *
 * Windows NT and everything since: the drive is opened as a device and
 * asked for its table of contents and for raw sectors. Only kernel32 is
 * needed.
 *
 * Windows 95, 98 and Me have no such interface. There the drive is given
 * SCSI commands through ASPI, a library (wnaspi32.dll) that those systems
 * have in a basic form and that CD writing programs replaced with a better
 * one. It is loaded when a CD is first asked for; without it, audio CDs do
 * not play there. ASPI numbers devices by adapter and target and knows
 * nothing of drive letters, so the first CD drive it lists is taken to be
 * the first drive letter that is a CD drive, and so on. */
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
    HANDLE drive;                       /* INVALID_HANDLE_VALUE when ASPI is used: */
    BYTE adapter, target;
};

/* --- ASPI ------------------------------------------------------------------- */

#define ASPI_GET_DEVICE_TYPE 1
#define ASPI_EXECUTE         2
#define ASPI_PENDING         0
#define ASPI_DONE            1
#define ASPI_DATA_IN         0x08
#define ASPI_TYPE_CDROM      5
#define ASPI_TARGETS         8
#define ASPI_SENSE           14
#define ASPI_TIMEOUT_MS      15000
#define SCSI_READ_TOC        0x43
#define SCSI_READ_CD         0xBE

#pragma pack(push, 1)
typedef struct {
    BYTE command, status, adapter, flags;
    DWORD reserved;
    BYTE target, lun, type, reserved1;
} AspiDeviceType;

typedef struct {
    BYTE command, status, adapter, flags;
    DWORD reserved;
    BYTE target, lun;
    WORD reserved1;
    DWORD length;
    BYTE *buffer;
    BYTE sense_length, cdb_length, adapter_status, target_status;
    void *post;
    BYTE reserved2[20];
    BYTE cdb[16];
    BYTE sense[ASPI_SENSE + 2];
} AspiExecute;
#pragma pack(pop)

static DWORD (__cdecl *aspi_info)(void);
static DWORD (__cdecl *aspi_send)(void *request);

/* Is this Windows 95, 98 or Me, with a working ASPI? Returns the number of
 * adapters, 0 if not. */
static int aspi_adapters(void)
{
    static int count = -1;

    if (count < 0) {
        HMODULE library = (GetVersion() & 0x80000000u) ? LoadLibraryA("wnaspi32.dll") : NULL;
        DWORD info;

        count = 0;
        if (library) {
            FARPROC get = GetProcAddress(library, "GetASPI32SupportInfo");
            FARPROC send = GetProcAddress(library, "SendASPI32Command");

            memcpy(&aspi_info, &get, sizeof get);
            memcpy(&aspi_send, &send, sizeof send);
            info = get && send ? aspi_info() : 0;
            if (((info >> 8) & 0xFF) == ASPI_DONE)
                count = (int)(info & 0xFF);
        }
    }
    return count;
}

/* Finds the CD drive that ASPI lists in place `rank` (from 0). */
static int aspi_find(int rank, BYTE *adapter, BYTE *target)
{
    int a, t, adapters = aspi_adapters();

    for (a = 0; a < adapters; a++)
        for (t = 0; t < ASPI_TARGETS; t++) {
            AspiDeviceType request;

            memset(&request, 0, sizeof request);
            request.command = ASPI_GET_DEVICE_TYPE;
            request.adapter = (BYTE)a;
            request.target = (BYTE)t;
            aspi_send(&request);
            if (request.status == ASPI_DONE && request.type == ASPI_TYPE_CDROM && rank-- == 0) {
                *adapter = (BYTE)a;
                *target = (BYTE)t;
                return 1;
            }
        }
    return 0;
}

/* Sends the drive a command that returns data. */
static int aspi_command(PlatCd *cd, const BYTE *cdb, int cdb_length, void *out, DWORD size)
{
    AspiExecute *request = calloc(1, sizeof *request);
    int waited, done;

    if (!request)
        return 0;
    request->command = ASPI_EXECUTE;
    request->adapter = cd->adapter;
    request->target = cd->target;
    request->flags = ASPI_DATA_IN;
    request->length = size;
    request->buffer = out;
    request->sense_length = ASPI_SENSE;
    request->cdb_length = (BYTE)cdb_length;
    memcpy(request->cdb, cdb, (size_t)cdb_length);
    aspi_send(request);
    for (waited = 0; request->status == ASPI_PENDING && waited < ASPI_TIMEOUT_MS; waited++)
        Sleep(1);
    if (request->status == ASPI_PENDING)
        return 0;       /* still ASPI's to write to, so it is not freed */
    done = request->status == ASPI_DONE;
    free(request);
    return done;
}

/* --- The interface ------------------------------------------------------------ */

static int uses_aspi(const PlatCd *cd)
{
    return cd->drive == INVALID_HANDLE_VALUE;
}

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
    cd = calloc(1, sizeof *cd);
    if (!cd)
        return NULL;
    drive = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    cd->drive = drive;
    if (drive == INVALID_HANDLE_VALUE) {
        /* Not possible on Windows 95, 98 and Me: ASPI instead, where the
         * drive is found by its place among the CD drives. */
        char names[26][PLAT_CD_NAME];
        int count = aspi_adapters() ? plat_cd_drives(names, 26) : 0, rank;

        for (rank = 0; rank < count && (names[rank][0] | 0x20) != (device[0] | 0x20); rank++)
            ;
        if (rank >= count || !aspi_find(rank, &cd->adapter, &cd->target)) {
            free(cd);
            return NULL;
        }
    }
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
    if (uses_aspi(cd)) {
        /* The same table, in the same layout, straight from the drive
         * (the 2 asks for positions as minutes, seconds and sectors). */
        BYTE cdb[10] = { SCSI_READ_TOC, 0x02, 0, 0, 0, 0, 0, (BYTE)(sizeof raw >> 8), (BYTE)sizeof raw, 0 };

        if (!aspi_command(cd, cdb, sizeof cdb, &raw, sizeof raw))
            return 0;
    } else if (!DeviceIoControl(cd->drive, CD_IOCTL_READ_TOC, NULL, 0, &raw, sizeof raw, &got, NULL)) {
        return 0;
    }
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

    if (uses_aspi(cd)) {
        /* READ CD: any kind of sector, the 2352 bytes of user data only. */
        BYTE cdb[12] = { SCSI_READ_CD, 0, (BYTE)(sector >> 24), (BYTE)(sector >> 16), (BYTE)(sector >> 8), (BYTE)sector,
                         (BYTE)(count >> 16), (BYTE)(count >> 8), (BYTE)count, 0x10, 0, 0 };

        return aspi_command(cd, cdb, sizeof cdb, out, (DWORD)count * CD_SECTOR);
    }
    memset(&request, 0, sizeof request);
    request.offset.QuadPart = (LONGLONG)sector * 2048;
    request.sectors = (ULONG)count;
    request.mode = CD_RAW_MODE_AUDIO;
    return DeviceIoControl(cd->drive, CD_IOCTL_RAW_READ, &request, sizeof request, out, (DWORD)count * CD_SECTOR,
                           &got, NULL) && got == (DWORD)count * CD_SECTOR;
}

void plat_cd_close(PlatCd *cd)
{
    if (!uses_aspi(cd))
        CloseHandle(cd->drive);
    free(cd);
}
