#include "playlist.h"

#include "cd.h"

#include "platform.h"
#include "tags.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Track *tracks;
static int count, capacity;
static unsigned revision;
static int queue_length;

unsigned playlist_revision(void)
{
    return revision;
}

int playlist_count(void)
{
    return count;
}

const Track *playlist_get(int index)
{
    return index >= 0 && index < count ? &tracks[index] : NULL;
}

static char *dup_range(const char *s, size_t len)
{
    char *copy = malloc(len + 1);

    if (copy) {
        memcpy(copy, s, len);
        copy[len] = '\0';
    }
    return copy;
}

int playlist_add(const char *path)
{
    const char *name = path_basename(path), *dot;
    char artist[256], title[256], text[600];
    Track track;

    if (count == capacity) {
        int grown_capacity = capacity ? capacity * 2 : 32;
        Track *grown = realloc(tracks, sizeof *tracks * grown_capacity);

        if (!grown)
            return -1;
        tracks = grown;
        capacity = grown_capacity;
    }

    if (path_is_cd(path)) {
        char device[CD_DEVICE_MAX];
        int number = 0;

        cd_split_path(path, device, sizeof device, &number);
        snprintf(text, sizeof text, "CD Track %02d", number);
    } else if (path_is_url(path)) {
        /* A web address: nothing to read tags from. It is shown without its
         * "http://" until the station gives its name. */
        const char *shown = strstr(path, "://");

        text_to_utf8(text, sizeof text, (const unsigned char *)(shown ? shown + 3 : path),
                     strlen(shown ? shown + 3 : path), TEXT_UTF8);
    } else if (tags_read(path, artist, sizeof artist, title, sizeof title)) {
        if (artist[0])
            snprintf(text, sizeof text, "%s - %s", artist, title);
        else
            snprintf(text, sizeof text, "%s", title);
    } else {
        /* No usable tags: "dir/Some Song.mp3" -> "Some Song" */
        dot = strrchr(name, '.');
        text_to_utf8(text, sizeof text, (const unsigned char *)name,
                     dot && dot != name ? (size_t)(dot - name) : strlen(name), TEXT_UTF8);
    }
    track.path = dup_range(path, strlen(path));
    track.title = dup_range(text, strlen(text));
    track.length = -1;
    track.selected = 0;
    track.queue = 0;
    if (!track.path || !track.title) {
        free(track.path);
        free(track.title);
        return -1;
    }
    tracks[count] = track;
    revision++;
    return count++;
}

void playlist_remove(int index)
{
    if (index < 0 || index >= count)
        return;
    playlist_dequeue(index);
    free(tracks[index].path);
    free(tracks[index].title);
    memmove(&tracks[index], &tracks[index + 1], sizeof *tracks * (count - index - 1));
    count--;
    revision++;
}

void playlist_set_length(int index, int seconds)
{
    if (index >= 0 && index < count && tracks[index].length != seconds) {
        tracks[index].length = seconds;
        revision++;
    }
}

void playlist_set_title(int index, const char *title)
{
    char *copy;

    if (index < 0 || index >= count || strcmp(tracks[index].title, title) == 0)
        return;
    copy = dup_range(title, strlen(title));
    if (!copy)
        return;
    free(tracks[index].title);
    tracks[index].title = copy;
    revision++;
}

void playlist_select(int index, int selected)
{
    if (index >= 0 && index < count) {
        tracks[index].selected = selected;
        revision++;
    }
}

void playlist_select_range(int a, int b)
{
    int i, lo = a < b ? a : b, hi = a < b ? b : a;

    for (i = 0; i < count; i++)
        tracks[i].selected = lo >= 0 && i >= lo && i <= hi;
    revision++;
}

void playlist_select_all(void)
{
    int i;

    for (i = 0; i < count; i++)
        tracks[i].selected = 1;
    revision++;
}

void playlist_dequeue(int index)
{
    int place, i;

    if (index < 0 || index >= count || !tracks[index].queue)
        return;
    place = tracks[index].queue;
    tracks[index].queue = 0;
    for (i = 0; i < count; i++)     /* everything behind it moves up one */
        if (tracks[i].queue > place)
            tracks[i].queue--;
    queue_length--;
    revision++;
}

void playlist_queue_toggle(int index)
{
    if (index < 0 || index >= count)
        return;
    if (tracks[index].queue) {
        playlist_dequeue(index);
    } else {
        tracks[index].queue = ++queue_length;
        revision++;
    }
}

void playlist_queue_clear(void)
{
    int i;

    for (i = 0; i < count; i++)
        tracks[i].queue = 0;
    queue_length = 0;
    revision++;
}

int playlist_queue_head(void)
{
    int i;

    for (i = 0; queue_length && i < count; i++)
        if (tracks[i].queue == 1)
            return i;
    return -1;
}

void playlist_invert_selection(void)
{
    int i;

    for (i = 0; i < count; i++)
        tracks[i].selected = !tracks[i].selected;
    revision++;
}

/* --- Reordering ------------------------------------------------------------- */

/* Rearranges the list so that position i holds what was at order[i]. */
static int apply_order(const int *order, int follow)
{
    Track *moved = malloc(sizeof *tracks * (count ? count : 1));
    int i, result = follow;

    if (!moved)
        return follow;
    for (i = 0; i < count; i++) {
        moved[i] = tracks[order[i]];
        if (order[i] == follow)
            result = i;
    }
    memcpy(tracks, moved, sizeof *tracks * count);
    revision++;
    free(moved);
    return result;
}

static int *identity_order(void)
{
    int *order = malloc(sizeof *order * (count ? count : 1)), i;

    for (i = 0; order && i < count; i++)
        order[i] = i;
    return order;
}

/* Byte order with ASCII letters compared without regard to case. */
static int compare_text(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;

        if (ca >= 'A' && ca <= 'Z')
            ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z')
            cb += 'a' - 'A';
        if (ca != cb || !ca)
            return ca - cb;
    }
}

static int sort_by;     /* qsort() has no way to pass this along */

static int compare_tracks(const void *pa, const void *pb)
{
    int a = *(const int *)pa, b = *(const int *)pb, result;

    if (sort_by == PLAYLIST_BY_TITLE)
        result = compare_text(tracks[a].title, tracks[b].title);
    else if (sort_by == PLAYLIST_BY_FILENAME)
        result = compare_text(path_basename(tracks[a].path), path_basename(tracks[b].path));
    else
        result = compare_text(tracks[a].path, tracks[b].path);
    return result ? result : a - b;     /* equal tracks keep their order */
}

int playlist_sort(int by, int follow)
{
    int *order = identity_order();

    if (!order)
        return follow;
    sort_by = by;
    qsort(order, (size_t)count, sizeof *order, compare_tracks);
    follow = apply_order(order, follow);
    free(order);
    return follow;
}

int playlist_reverse(int follow)
{
    int *order = identity_order(), i;

    if (!order)
        return follow;
    for (i = 0; i < count; i++)
        order[i] = count - 1 - i;
    follow = apply_order(order, follow);
    free(order);
    return follow;
}

int playlist_randomize(int follow)
{
    int *order = identity_order(), i;

    if (!order)
        return follow;
    for (i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1), swap = order[i];

        order[i] = order[j];
        order[j] = swap;
    }
    follow = apply_order(order, follow);
    free(order);
    return follow;
}

void playlist_free(void)
{
    while (count)
        playlist_remove(count - 1);
    free(tracks);
    queue_length = 0;
    tracks = NULL;
    capacity = 0;
}

static int is_absolute(const char *path)
{
    return path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':') || path_is_url(path) || path_is_cd(path);
}

#define BREATHE_EVERY 16    /* tracks */

int playlist_load(const char *path, void (*breathe)(void))
{
    char line[2048], full[4096];
    const char *dir_end = path_basename(path);
    FILE *f = plat_fopen(path, "r");
    int added = 0;

    if (!f)
        return 0;
    while (fgets(line, sizeof line, f)) {
        char *entry = line;
        size_t len = strlen(line);

        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (memcmp(entry, "\xEF\xBB\xBF", 3) == 0)
            entry += 3;     /* UTF-8 byte order mark */
        if (!*entry || *entry == '#')
            continue;
        if (is_absolute(entry)) {
            snprintf(full, sizeof full, "%s", entry);
        } else {
            /* Relative to where the playlist is. */
            snprintf(full, sizeof full, "%.*s%s", (int)(dir_end - path), path, entry);
#ifndef _WIN32
            /* One written on Windows may use its separator; here that is
             * an ordinary character, so it is only taken for one when no
             * file has the name as it stands. */
            if (strchr(entry, '\\')) {
                FILE *there = plat_fopen(full, "rb");
                char *p;

                if (there)
                    fclose(there);
                else
                    for (p = full + (dir_end - path); *p; p++)
                        if (*p == '\\')
                            *p = '/';
            }
#endif
        }
        if (playlist_add(full) >= 0)
            added++;
        if (breathe && added % BREATHE_EVERY == 0)
            breathe();
    }
    fclose(f);
    return added;
}

/* If `path` lies in the folder `dir` (given with its closing separator, as
 * `dir_len` characters) or below it: the rest of it. Otherwise NULL. On
 * Windows, where names are so, the comparison ignores the case of ASCII
 * letters and takes either separator for the other. */
static const char *below(const char *dir, size_t dir_len, const char *path)
{
    size_t i;

    if (!dir_len || path_is_url(path) || path_is_cd(path))
        return NULL;
    for (i = 0; i < dir_len; i++) {
        int a = (unsigned char)dir[i], b = (unsigned char)path[i];

#ifdef _WIN32
        a = a == '\\' ? '/' : a >= 'A' && a <= 'Z' ? a + 32 : a;
        b = b == '\\' ? '/' : b >= 'A' && b <= 'Z' ? b + 32 : b;
#endif
        if (a != b)
            return NULL;    /* (also where the path ends first) */
    }
    return path[dir_len] ? path + dir_len : NULL;
}

/* Tracks in the playlist's own folder or below it are written relative to
 * it, the way .m3u files usually are: such a list goes on working when the
 * folder is moved, renamed or carried to another machine. Anything else
 * keeps its full path. */
int playlist_save(const char *path)
{
    FILE *f = plat_fopen(path, "w");
    size_t dir_len = (size_t)(path_basename(path) - path);
    int i;

    if (!f)
        return 0;
    for (i = 0; i < count; i++) {
        const char *rest = below(path, dir_len, tracks[i].path);

        if (!rest) {
            fprintf(f, "%s\n", tracks[i].path);
            continue;
        }
        for (; *rest; rest++) {
#ifdef _WIN32
            fputc(*rest == '\\' ? '/' : *rest, f);    /* the separator every system reads */
#else
            fputc(*rest, f);
#endif
        }
        fputc('\n', f);
    }
    return fclose(f) == 0;
}
