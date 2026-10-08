/* Audio CD tracks (see cd.h): 44.1 kHz 16-bit stereo, read off the disc.
 *
 * A drive can take seconds to answer (it has to spin up, or try again on a
 * scratch), and the audio thread cannot wait that long. So each open track
 * has a thread that reads ahead into a buffer, and reading from the codec
 * only empties that. Opening and seeking wait for the first sound to
 * arrive; should the buffer run dry later, silence is played meanwhile. */
#include "codec.h"

#include "cd.h"
#include "platform.h"

#include <stdlib.h>
#include <string.h>

#define AHEAD_SECTORS   300     /* four seconds */
#define CHUNK_SECTORS   15      /* read at a time: a fifth of a second */
#define AHEAD_BYTES     ((size_t)AHEAD_SECTORS * CD_SECTOR)
#define FRAME_BYTES     4
#define FIRST_WAIT_MS   8000    /* longest wait for a drive to deliver its first sound */
#define READ_RETRIES    3

typedef struct {
    Cd *cd;
    long first, sectors;        /* the track */
    uint64_t bytes;             /* its length */

    volatile int lock;          /* guards everything below */
    unsigned char *ahead;       /* a ring: `fill` bytes from `at` on */
    size_t at, fill;
    long next;                  /* sector of the track the thread reads next */
    uint64_t position;          /* bytes of the track handed out so far */
    int generation;             /* goes up at every seek, so a read under way is dropped */
    volatile int quit, released;
} CdTrackState;

static void lock(CdTrackState *t)
{
    /* Held only for a copy; but on a machine with one processor the holder
     * cannot finish while we spin, so it is given the processor. */
    while (__sync_lock_test_and_set(&t->lock, 1))
        plat_sleep_ms(0);
}

static void unlock(CdTrackState *t)
{
    __sync_lock_release(&t->lock);
}

static void reader(void *arg)
{
    CdTrackState *t = arg;
    unsigned char *chunk = malloc(CHUNK_SECTORS * CD_SECTOR);

    while (chunk && !t->quit) {
        int generation, count, tries, ok = 0;
        size_t size, i;
        long sector;

        lock(t);
        generation = t->generation;
        sector = t->next;
        count = t->sectors - sector < CHUNK_SECTORS ? (int)(t->sectors - sector) : CHUNK_SECTORS;
        if (AHEAD_BYTES - t->fill < (size_t)count * CD_SECTOR)
            count = 0;
        unlock(t);
        if (count <= 0) {
            plat_sleep_ms(20);      /* enough in hand, or the track is all read */
            continue;
        }
        for (tries = 0; tries < READ_RETRIES && !ok && !t->quit; tries++)
            ok = cd_read(t->cd, t->first + sector, count, chunk);
        size = (size_t)count * CD_SECTOR;
        if (!ok)
            memset(chunk, 0, size);     /* unreadable: silence in its place */

        lock(t);
        if (generation == t->generation) {
            for (i = 0; i < size;) {
                size_t to = (t->at + t->fill) % AHEAD_BYTES, n = size - i;

                if (n > AHEAD_BYTES - to)
                    n = AHEAD_BYTES - to;
                memcpy(t->ahead + to, chunk + i, n);
                t->fill += n;
                i += n;
            }
            t->next = sector + count;
        }
        unlock(t);
    }
    free(chunk);
    /* The state is the codec's until it lets go of it. */
    while (!t->released)
        plat_sleep_ms(20);
    cd_close(t->cd);
    free(t->ahead);
    free(t);
}

/* Waits until the thread has something to play. */
static void wait_for_sound(CdTrackState *t)
{
    int waited;

    for (waited = 0; waited < FIRST_WAIT_MS; waited += 5) {
        int ready;

        lock(t);
        ready = t->fill > 0 || t->next >= t->sectors;
        unlock(t);
        if (ready)
            return;
        plat_sleep_ms(5);
    }
}

static size_t cd_track_read(void *state, short *out, size_t frames)
{
    CdTrackState *t = state;
    size_t want = frames * FRAME_BYTES, done = 0;
    unsigned char *dst = (unsigned char *)out;
    uint64_t left;

    lock(t);
    left = t->bytes - t->position;
    if (want > left)
        want = (size_t)left;
    while (done < want && t->fill) {
        size_t n = want - done;

        if (n > t->fill)
            n = t->fill;
        if (n > AHEAD_BYTES - t->at)
            n = AHEAD_BYTES - t->at;
        memcpy(dst + done, t->ahead + t->at, n);
        t->at = (t->at + n) % AHEAD_BYTES;
        t->fill -= n;
        done += n & ~(size_t)(FRAME_BYTES - 1);
    }
    t->position += done;
    unlock(t);
    /* The drive has fallen behind: silence, so that the track does not
     * seem to have ended. It is not counted as part of the track. */
    if (done < want)
        memset(dst + done, 0, want - done);
    return want / FRAME_BYTES;
}

static int cd_track_seek(void *state, uint64_t frame)
{
    CdTrackState *t = state;
    uint64_t byte = frame * FRAME_BYTES;

    if (byte > t->bytes)
        byte = t->bytes;
    lock(t);
    t->generation++;
    t->next = (long)(byte / CD_SECTOR);
    t->position = (uint64_t)t->next * CD_SECTOR;    /* to the start of that sector: 1/75 s at most */
    t->at = t->fill = 0;
    unlock(t);
    wait_for_sound(t);
    return 1;
}

static void cd_track_close(void *state)
{
    CdTrackState *t = state;

    t->quit = 1;
    t->released = 1;        /* the thread frees everything */
}

int codec_open_cd(const char *path, Codec *codec)
{
    char device[CD_DEVICE_MAX];
    CdTrackState *t;
    const CdToc *toc;
    int number, i;
    Cd *cd;

    if (!cd_split_path(path, device, sizeof device, &number))
        return 0;
    cd = cd_open(device);
    if (!cd)
        return 0;
    toc = cd_toc(cd);
    for (i = 0; i < toc->count && toc->track[i].number != number; i++)
        ;
    t = i < toc->count && toc->track[i].audio && toc->track[i].sectors > 0 ? calloc(1, sizeof *t) : NULL;
    if (t)
        t->ahead = malloc(AHEAD_BYTES);
    if (!t || !t->ahead) {
        free(t);
        cd_close(cd);
        return 0;
    }
    t->cd = cd;
    t->first = toc->track[i].start;
    t->sectors = toc->track[i].sectors;
    t->bytes = (uint64_t)t->sectors * CD_SECTOR;
    if (!plat_thread_start(reader, t)) {
        free(t->ahead);
        free(t);
        cd_close(cd);
        return 0;
    }
    wait_for_sound(t);

    codec->state = t;
    codec->channels = 2;
    codec->rate = CD_RATE;
    codec->length = t->bytes / FRAME_BYTES;
    codec->read = cd_track_read;
    codec->seek = cd_track_seek;
    codec->close = cd_track_close;
    return 1;
}
