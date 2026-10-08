/* The list of tracks queued for playback. */
#ifndef PLAYLIST_H
#define PLAYLIST_H

typedef struct {
    char *path;
    char *title;    /* UTF-8: "Artist - Title" from the tags, else the file name */
    int length;     /* seconds, -1 until the track has been opened */
    int selected;   /* highlighted in the playlist window */
    int queue;      /* place in the play queue: 1 is played next, 0 is not queued */
} Track;

int          playlist_count(void);
/* A number that changes whenever anything a list view shows does: tracks
 * added, removed or moved, a selection, a length that became known. */
unsigned     playlist_revision(void);
const Track *playlist_get(int index);
/* Returns the new track's index, or -1 on failure. */
int          playlist_add(const char *path);
void         playlist_remove(int index);
void         playlist_set_length(int index, int seconds);
/* Replaces a track's title (a station's name, once it is known). */
void         playlist_set_title(int index, const char *title);
void         playlist_free(void);

void         playlist_select(int index, int selected);
/* Selects exactly the tracks from a to b (in either order); -1, -1 selects none. */
void         playlist_select_range(int a, int b);
void         playlist_select_all(void);
void         playlist_invert_selection(void);

/* The play queue: tracks picked to be played next, in the order they were
 * picked, before the list's normal order resumes. A track's place is kept
 * in the track itself, so it survives sorting and removals. */
void         playlist_queue_toggle(int index);  /* adds at the end, or takes out */
void         playlist_dequeue(int index);       /* takes out; nothing if not queued */
void         playlist_queue_clear(void);
int          playlist_queue_head(void);         /* the track to play next, or -1 */

/* Reordering. Each takes the index of one track to keep an eye on (the one
 * being played) and returns where that track ended up; pass -1 for none. */
enum { PLAYLIST_BY_TITLE, PLAYLIST_BY_FILENAME, PLAYLIST_BY_PATH };
int          playlist_sort(int by, int follow);
int          playlist_reverse(int follow);
int          playlist_randomize(int follow);

/* Plain M3U: one path per line. Relative entries are resolved against the
 * playlist's own directory. Loading appends; returns the number added.
 * Reading the tags of a long list from a disk that is not cached yet can
 * take seconds, so `breathe` (if not NULL) is called every few tracks to
 * let the caller keep its windows alive meanwhile. */
int          playlist_load(const char *path, void (*breathe)(void));
int          playlist_save(const char *path);

#endif
