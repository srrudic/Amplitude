/* Audio CD tracks (see cd.h): 44.1 kHz 16-bit stereo, read off the disc.
 *
 * A drive can take seconds to answer (it has to spin up, or try again on a
 * scratch), and the audio thread cannot wait that long. So each open track
 * has a thread that reads ahead into a buffer, and reading from the codec
 * only empties that. Opening and seeking wait for the first sound to
 * arrive; should the buffer run dry later, silence is played meanwhile.
 *
 * A drive reads in one place at a time, and moving between two is slow and
 * noisy. But two tracks are usually open at once: the one playing and the
 * one to follow, opened in advance for a change-over without a gap. So the
 * threads take turns: the track being listened to has the drive until it
 * is read to its end, and only then is the next one read ahead (which, for
 * the following track of the disc, carries straight on). */
#include "codec.h"

#include "cd.h"
#include "cdnames.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AHEAD_SECTORS   300     /* four seconds */
#define CHUNK_SECTORS   15      /* read at a time: a fifth of a second */
#define AHEAD_BYTES     ((size_t)AHEAD_SECTORS * CD_SECTOR)
#define FRAME_BYTES     4
#define START_SECTORS   150     /* in hand before sound is given out: two seconds (see `starved`) */
#define QUICK_SPEED     3       /* times playing speed: a drive that has got up to speed reads at least so fast */
#define QUICK_CHUNKS    8       /* so many reads in a row at that speed, and it has */
#define FIRST_WAIT_MS   8000    /* longest wait for a drive to deliver its first sound */
#define READ_RETRIES    3

typedef struct {
    Cd *cd;
    char device[CD_DEVICE_MAX];
    long first, sectors;        /* the track */
    uint64_t bytes;             /* its length */

    volatile int lock;          /* guards everything below */
    unsigned char *ahead;       /* a ring: `fill` bytes from `at` on */
    size_t at, fill;
    long next;                  /* sector of the track the thread reads next */
    uint64_t position;          /* bytes of the track handed out so far */
    int generation;             /* goes up at every seek, so a read under way is dropped */
    /* No sound is given out until a fair amount is in hand: at the start,
     * after a seek, and whenever the buffer has run dry. A drive woken from
     * standstill reads slowly for a few seconds and then not at all while
     * it gets up to speed; playing those seconds only to fall silent is
     * worse than starting a little later. So an amount in hand is not
     * enough by itself: the drive must also be seen to read quickly (the
     * count of reads in a row that were quick), or else the buffer be full,
     * which is the most that can be asked of a drive that is simply slow. */
    int starved, quick;
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

/* The track being listened to: the one last read from. */
static CdTrackState *front;
static volatile int front_lock;

/* Puts `t` in front; if `instead_of` is given, only in place of that one
 * (which may be NULL: only if nobody is). */
static void set_front(CdTrackState *t, CdTrackState **instead_of)
{
    while (__sync_lock_test_and_set(&front_lock, 1))
        plat_sleep_ms(0);
    if (!instead_of || front == *instead_of)
        front = t;
    __sync_lock_release(&front_lock);
}

/* Is the drive another track's for now? */
static int others_turn(CdTrackState *t)
{
    int wait;

    while (__sync_lock_test_and_set(&front_lock, 1))
        plat_sleep_ms(0);
    /* (Its progress is read without its lock: a number a moment old is as good.) */
    wait = front && front != t && !front->quit && front->next < front->sectors &&
           strcmp(front->device, t->device) == 0;
    __sync_lock_release(&front_lock);
    return wait;
}

/* Or busy with a track's turn or with the disc's names being read? */
static int must_wait(CdTrackState *t)
{
    return cd_names_reading() || others_turn(t);
}

static void reader(void *arg)
{
    CdTrackState *t = arg;
    unsigned char *chunk = malloc(CHUNK_SECTORS * CD_SECTOR);

    while (chunk && !t->quit) {
        int generation, count, tries, ok = 0, quick;
        uint32_t started;
        size_t size, i;
        long sector;

        lock(t);
        generation = t->generation;
        sector = t->next;
        count = t->sectors - sector < CHUNK_SECTORS ? (int)(t->sectors - sector) : CHUNK_SECTORS;
        if (AHEAD_BYTES - t->fill < (size_t)count * CD_SECTOR)
            count = 0;
        unlock(t);
        if (count <= 0 || must_wait(t)) {
            plat_sleep_ms(20);      /* enough in hand, the track is all read, or it is not our turn */
            continue;
        }
        started = plat_ticks_ms();
        for (tries = 0; tries < READ_RETRIES && !ok && !t->quit; tries++)
            ok = cd_read(t->cd, t->first + sector, count, chunk);
        /* (Sound lasting count / 75 s, read in a third of that or less?) */
        quick = (plat_ticks_ms() - started) * QUICK_SPEED * CD_SECTORS_PER_S <= (uint32_t)count * 1000;
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
            t->quick = quick ? t->quick + 1 : 0;
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

/* Is enough in hand to give sound out (again)? Call with the lock held. */
static int enough(const CdTrackState *t)
{
    if (t->next >= t->sectors || AHEAD_BYTES - t->fill < (size_t)CHUNK_SECTORS * CD_SECTOR)
        return 1;       /* all there is, or all there is room for */
    return t->fill >= (size_t)START_SECTORS * CD_SECTOR && t->quick >= QUICK_CHUNKS;
}

/* Waits until the thread has enough to start playing on. Not
 * while the drive is another track's, though: this one is then only being
 * made ready, and will have its sound by the time its turn comes. */
static void wait_for_sound(CdTrackState *t)
{
    int waited;

    for (waited = 0; waited < FIRST_WAIT_MS; waited += 5) {
        int ready;

        lock(t);
        ready = enough(t);
        unlock(t);
        if (ready || others_turn(t))
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

    if (front != t)
        set_front(t, NULL);
    lock(t);
    left = t->bytes - t->position;
    if (want > left)
        want = (size_t)left;
    if (t->starved && enough(t))
        t->starved = 0;
    while (done < want && t->fill && !t->starved) {
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
    if (done < want)
        t->starved = 1;
    unlock(t);
    /* The drive has fallen behind (or has yet to deliver): silence, so
     * that the track does not seem to have ended. It is not counted as part
     * of the track. */
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
    byte -= byte % CD_SECTOR;           /* to the start of that sector: 1/75 s at most */
    if (front != t)
        set_front(t, NULL);             /* asked to move: this is the track that matters now */
    lock(t);
    if (byte >= t->position && byte - t->position <= t->fill) {
        /* Already in hand (always so for the rewind that follows opening):
         * the drive is left where it is. */
        size_t skip = (size_t)(byte - t->position);

        t->at = (t->at + skip) % AHEAD_BYTES;
        t->fill -= skip;
    } else {
        t->generation++;
        t->next = (long)(byte / CD_SECTOR);
        t->at = t->fill = 0;
        t->starved = 1;
        t->quick = 0;
    }
    t->position = byte;
    unlock(t);
    wait_for_sound(t);
    return 1;
}

static void cd_track_close(void *state)
{
    CdTrackState *t = state;

    t->quit = 1;
    set_front(NULL, &t);    /* if it was the one listened to, nobody is now */
    t->released = 1;        /* the thread frees everything */
}

int codec_open_cd(const char *path, Codec *codec)
{
    char device[CD_DEVICE_MAX];
    CdTrackState *t, *nobody = NULL;
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
    t->starved = 1;
    snprintf(t->device, sizeof t->device, "%s", device);
    t->first = toc->track[i].start;
    t->sectors = toc->track[i].sectors;
    t->bytes = (uint64_t)t->sectors * CD_SECTOR;
    set_front(t, &nobody);      /* the first to be opened is the one to be heard, until another is read from */
    if (!plat_thread_start(reader, t)) {
        set_front(NULL, &t);
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
