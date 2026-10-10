#include "audio.h"
#include "cd.h"
#include "cdnames.h"
#include "config.h"
#include "platform.h"
#include "playlist.h"
#include "presets.h"
#include "skin.h"
#include "tags.h"
#include "theme.h"
#include "ui.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE_SCALE      120     /* percent; multiplied by the desktop's scaling */
#define SCALE_MIN       100     /* percent */
#define SCALE_MAX       400
/* 25 frames a second while something moves on screen. When that includes a
 * scrolling title, frames are instead timed to fall on its steps (see the
 * end of main()): a frame rate of its own would show some steps late and
 * the title would stutter. */
#define FRAME_MS        40
#define IDLE_MS         250     /* otherwise */
#define FIRST_FRAME_MS  500     /* longest wait for the windows to come on screen at start-up */
#define FIRST_FRAME_POLL_MS 10
#define SEEK_STEP       5.0     /* seconds, arrow keys */
#define VOLUME_STEP     0.05f
#define SNAP_DISTANCE   10      /* screen pixels */
#define DOCK_TOLERANCE  2       /* screen pixels between windows that count as touching */
#define BALANCE_SNAP    0.15f   /* the balance slider sticks to the centre within this */
#define JUMP_OFFSET     20      /* where the dialogs first open, from the main window's corner */
#define ABOUT_OFFSET    12
#define URL_OFFSET      16
#define STREAM_TITLE_MS 500     /* how often a station is asked what it is playing */
#define QUERY_MAX       128     /* characters compared when searching */
#define TITLE_MAX       256
#define DOUBLE_CLICK_MS 400
#define READOUT_MS      1000    /* how long the volume stays in the title display after a key or wheel step */
#define MULTI_OPEN_MS   1000    /* starts this close together are one "open these files" */
#define WHEEL_ROWS      3
#define MAX_FOLDER_DEPTH 16
#define HISTORY_MAX     256     /* tracks that "previous" can step back through in shuffle */
#define PL_MAX_W        (PL_MIN_W + 40 * PL_STEP_W)
#define PL_MAX_H        (PL_MIN_H + 30 * PL_STEP_H)

enum { WIN_MAIN, WIN_EQ, WIN_PL, WIN_JUMP, WIN_ABOUT, WIN_URL, WIN_COUNT };

typedef struct {
    PlatWindow *plat;
    int w, h;           /* framebuffer size */
    int x, y;           /* screen position */
    int rel_x, rel_y;   /* offset from the main window while hidden */
    int visible;
    int placed;         /* has been shown at least once */
    uint32_t *fb;       /* what is drawn into, at the window's real resolution */
    int redraw;         /* draw it even if its model looks unchanged */
    int exposed;        /* the system has asked for it to be painted, so it is on screen */
} AppWindow;

static AppWindow wins[WIN_COUNT] = {
    [WIN_MAIN] = { .w = UI_W, .h = UI_H, .x = 100, .y = 100 },
    [WIN_EQ]   = { .w = EQ_W, .h = EQ_H },
    [WIN_PL]   = { .w = PL_MIN_W, .h = PL_MIN_H },
    [WIN_JUMP] = { .w = JUMP_W, .h = JUMP_H },
    [WIN_ABOUT] = { .w = ABOUT_W, .h = ABOUT_H },
    [WIN_URL] = { .w = URL_W, .h = URL_H },
};
static int scale = 100;         /* magnification in percent */
static Skin skin;
static int running = 1;
static Config config;
static char config_dir[1024];

/* Playback */
static int track_index;
static char title[640] = "AMPLITUDE - NO FILE LOADED";
static int track_loaded, track_kbps;
static int track_chosen;            /* the track was picked by the user, not reached by Next or at a track's end */
static int stream_failures;         /* streams in a row that could not be opened */
static float volume = 0.8f;
static float balance;
static uint32_t volume_readout_until;   /* ticks until which the title display shows the volume */
static int volume_readout;
static int shuffle, repeat;
static int cd_names_on;             /* look CD track names up on the internet when the disc has none */
static int vis_mode;
static int queued_index = -1;       /* track prepared for a gapless hand-over */
/* The tracks played before the current one, oldest first, so that with
 * shuffle on "previous" can retrace them. */
static int history[HISTORY_MAX], history_count;
static int last_started = -1;       /* the track most recently started, or -1 */
static int going_back;              /* "previous" is at work: do not record its step */
static int queue_dirty;             /* the queued track needs choosing again */
static int batch_play;              /* play the first file of the batch being received */
static uint32_t opened_ticks;       /* when files last arrived from a start of the program */
static int opened_before;

/* Equaliser */
static int eq_on, eq_auto;
static float eq_sliders[EQ_SLIDERS];    /* -1 .. 1 */

/* Playlist view */
static int pl_cursor = -1, pl_anchor = -1, pl_scroll;
static int click_row = -1;
static uint32_t click_ticks;

/* Jump to file */
static char jump_query[QUERY_MAX];
static int *jump_matches;           /* playlist indices of the matching tracks */
static int jump_count, jump_selected, jump_scroll;
static int jump_skip_text;          /* the key that opened the window also types a letter */

/* Open location */
static char url_text[1024];         /* the address being typed */
static char stream_song[256];       /* what the station says it is playing */

/* Mouse interaction */
static int pressed;                 /* UI_* element held down */
static int pressed_slider;          /* for UI_EQ_SLIDER */
static float seek_drag;             /* 0..1 while dragging UI_SEEK */
static int drag_sx, drag_sy;
static int drag_win;
static int drag_start_x[WIN_COUNT], drag_start_y[WIN_COUNT];
static int drag_group[WIN_COUNT];   /* windows moving together */
static int resize_start_w, resize_start_h;

/* Popup menu */
/* Menu commands share do_action() with the UI_* element ids, so they start
 * well clear of them. */
enum {
    CMD_ADD_FILES = 1000, CMD_ADD_FOLDER, CMD_EQ, CMD_PL, CMD_SHUFFLE, CMD_REPEAT,
    CMD_SKINS, CMD_SKIN_BUILTIN, CMD_SKIN_MORE, CMD_BACK, CMD_EXIT, CMD_SIZES, CMD_SIZE_AUTO,
    CMD_COLORS,
    CMD_REMOVE_SELECTED, CMD_CROP, CMD_REMOVE_ALL, CMD_REMOVE_MISSING,
    CMD_SELECT_ALL, CMD_SELECT_NONE, CMD_SELECT_INVERT,
    CMD_SORT_TITLE, CMD_SORT_FILENAME, CMD_SORT_PATH, CMD_REVERSE, CMD_RANDOMIZE,
    CMD_LIST_NEW, CMD_LIST_OPEN, CMD_LIST_SAVE, CMD_ABOUT, CMD_SKIN_LOAD,
    CMD_QUEUE_SELECTED, CMD_QUEUE_CLEAR, CMD_JUMP, CMD_OPEN_URL, CMD_PLAY_CD, CMD_ADD_CD, CMD_CD_NAMES,
    CMD_SIZE_FIRST = 1500,      /* + index into size_choices */
    CMD_PRESET_FIRST = 1600,    /* + index into presets */
    CMD_COLOR_FIRST = 1700,     /* + index into theme_presets */
    CMD_SKIN_FIRST = 2000       /* + index into skin_paths */
};
enum { MENU_MAIN, MENU_SKINS, MENU_SIZES, MENU_PRESETS, MENU_COLORS,
       MENU_ADD, MENU_REMOVE, MENU_SELECT, MENU_MISC, MENU_LIST };   /* the playlist's buttons, in order */

static const int size_choices[] = { 100, 125, 150, 175, 200, 250, 300, 400 };
#define SIZE_COUNT ARRAY_LEN(size_choices)
#define MENU_MAX  32
#define SKIN_PAGE 16
#define SKIN_MAX  1024

static PlatWindow *menu_win;
static uint32_t *menu_fb;
static MenuItem menu_items[MENU_MAX];
static char menu_labels[MENU_MAX][64];
static int menu_count, menu_w, menu_h, menu_hover = -1, menu_sx, menu_sy;
static char *skin_paths[SKIN_MAX];
static int skin_count, skin_page;

static void add_path(const char *path, int depth);
static void play_track(int index);
static void keep_on_screen(int *x, int *y, int w, int h);
static void apply_auto_preset(void);

/* --- Windows ---------------------------------------------------------------- */

/* Framebuffer pixels to screen pixels and back. */
static int to_real(int v)
{
    return v >= 0 ? PLAT_SCALED(v, scale) : -PLAT_SCALED(-v, scale);
}

static int to_logical(int v)
{
    return (v >= 0 ? v * 100 + scale / 2 : v * 100 - scale / 2) / scale;
}

static int window_index(const PlatWindow *plat)
{
    int i;

    for (i = 0; i < WIN_COUNT; i++)
        if (wins[i].plat == plat)
            return i;
    return -1;
}

static void sync_position(AppWindow *win)
{
    if (win->visible)
        plat_window_get_pos(win->plat, &win->x, &win->y);
}

/* The player's three windows are normally stacked, and at large sizes the
 * stack can be taller than the screen. Two things keep it reachable. */

/* How far the main, equaliser and playlist windows reach below the usable
 * area of the main window's monitor (negative if they do not), and how much
 * room there is above the topmost of them. `extra` is a window about to be
 * shown, or NULL. Real pixels. */
static int stack_overflow(const AppWindow *extra, int *room_above)
{
    int sx, sy, sw, sh, i, top, bottom;

    plat_screen_rect(wins[WIN_MAIN].x, wins[WIN_MAIN].y, &sx, &sy, &sw, &sh);
    top = wins[WIN_MAIN].y;
    bottom = top + to_real(wins[WIN_MAIN].h);
    for (i = WIN_MAIN; i <= WIN_PL; i++) {
        const AppWindow *win = &wins[i];

        if (!win->visible && win != extra)
            continue;
        if (win->y < top)
            top = win->y;
        if (win->y + to_real(win->h) > bottom)
            bottom = win->y + to_real(win->h);
    }
    *room_above = top - sy;
    return bottom - (sy + sh);
}

/* Moves the stack up, as far as there is room, when part of it is below
 * the screen. */
static void lift_stack(void)
{
    int room, over, i;

    for (i = WIN_MAIN; i <= WIN_PL; i++)
        sync_position(&wins[i]);
    over = stack_overflow(NULL, &room);
    if (over <= 0 || room <= 0)
        return;
    if (over > room)
        over = room;
    for (i = WIN_MAIN; i <= WIN_PL; i++) {
        wins[i].y -= over;
        if (wins[i].visible)
            plat_window_set_pos(wins[i].plat, wins[i].x, wins[i].y);
    }
}

/* A window about to be docked under the others for the first time: if the
 * stack would not fit on the screen even when moved up, it goes beside the
 * main window instead, on whichever side has room. */
static void dock_beside_if_too_tall(AppWindow *win)
{
    const AppWindow *main_win = &wins[WIN_MAIN];
    int sx, sy, sw, sh, room, w = to_real(win->w);

    if (stack_overflow(win, &room) <= room)
        return;
    plat_screen_rect(main_win->x, main_win->y, &sx, &sy, &sw, &sh);
    if (main_win->x + to_real(main_win->w) + w <= sx + sw)
        win->x = main_win->x + to_real(main_win->w);
    else if (main_win->x - w >= sx)
        win->x = main_win->x - w;
    else
        return;     /* no room on either side: under the others after all */
    win->y = main_win->y;
}

static void set_window_visible(int index, int visible)
{
    AppWindow *win = &wins[index], *main_win = &wins[WIN_MAIN];

    if (win->visible == visible)
        return;
    sync_position(main_win);
    if (!visible) {
        /* A hidden window keeps its place relative to the main window. */
        sync_position(win);
        win->rel_x = win->x - main_win->x;
        win->rel_y = win->y - main_win->y;
    } else if (win->placed) {
        win->x = main_win->x + win->rel_x;
        win->y = main_win->y + win->rel_y;
    } else {
        /* First appearance: dock below the main window (or the equaliser). */
        AppWindow *above = index == WIN_PL && wins[WIN_EQ].visible ? &wins[WIN_EQ] : main_win;

        sync_position(above);
        win->x = above->x;
        win->y = above->y + to_real(above->h);
        dock_beside_if_too_tall(win);
        win->placed = 1;
    }
    if (visible && index > WIN_PL)      /* a dialog: wherever it opens, all of it shows */
        keep_on_screen(&win->x, &win->y, to_real(win->w), to_real(win->h));
    if (visible)
        plat_window_set_pos(win->plat, win->x, win->y);
    win->visible = visible;
    win->redraw = 1;
    plat_window_show(win->plat, visible);
    if (visible && index <= WIN_PL)
        lift_stack();
}

/* Brings the stored positions up to date, and with them each visible
 * window's offset from the main one. */
static void sync_layout(void)
{
    int i;

    for (i = 0; i < WIN_COUNT; i++) {
        sync_position(&wins[i]);
        if (i != WIN_MAIN && wins[i].visible) {
            wins[i].rel_x = wins[i].x - wins[WIN_MAIN].x;
            wins[i].rel_y = wins[i].y - wins[WIN_MAIN].y;
        }
    }
}

/* Shows a dialog-like window over the main one and gives it the keyboard.
 * The first time it opens `offset` logical pixels in from the corner. */
static void open_dialog(int index, int offset)
{
    AppWindow *win = &wins[index];

    if (!win->placed) {
        sync_position(&wins[WIN_MAIN]);
        win->rel_x = win->rel_y = to_real(offset);
        win->placed = 1;
    }
    set_window_visible(index, 1);
    plat_window_focus(win->plat);
}

/* For changes a window's model does not show: a new skin, colour or size. */
static void redraw_all(void)
{
    int i;

    for (i = 0; i < WIN_COUNT; i++)
        wins[i].redraw = 1;
}

/* Do two windows share an edge (give or take a couple of pixels)? */
static int touching(const AppWindow *a, const AppWindow *b)
{
    int ax1 = a->x + to_real(a->w), ay1 = a->y + to_real(a->h);
    int bx1 = b->x + to_real(b->w), by1 = b->y + to_real(b->h);
    int overlap_x = a->x < bx1 && b->x < ax1, overlap_y = a->y < by1 && b->y < ay1;

    return ((abs(ax1 - b->x) <= DOCK_TOLERANCE || abs(bx1 - a->x) <= DOCK_TOLERANCE) && overlap_y) ||
           ((abs(ay1 - b->y) <= DOCK_TOLERANCE || abs(by1 - a->y) <= DOCK_TOLERANCE) && overlap_x);
}

static void begin_drag(int index, const PlatEvent *ev)
{
    int i, j, changed;

    drag_win = index;
    drag_sx = ev->sx;
    drag_sy = ev->sy;
    for (i = 0; i < WIN_COUNT; i++) {
        sync_position(&wins[i]);
        drag_start_x[i] = wins[i].x;
        drag_start_y[i] = wins[i].y;
        drag_group[i] = i == index;
    }
    if (index != WIN_MAIN)
        return;
    /* The main window carries along everything docked to it, directly or not. */
    do {
        changed = 0;
        for (i = 0; i < WIN_COUNT; i++) {
            for (j = 0; j < WIN_COUNT; j++) {
                if (drag_group[i] && !drag_group[j] && wins[i].visible && wins[j].visible &&
                    touching(&wins[i], &wins[j])) {
                    drag_group[j] = 1;
                    changed = 1;
                }
            }
        }
    } while (changed);
}

/* Returns `edge` moved onto `target` if the two are within snapping range. */
static int snap(int edge, int target, int *snapped)
{
    if (!*snapped && abs(edge - target) < SNAP_DISTANCE) {
        *snapped = 1;
        return target;
    }
    return edge;
}

static void update_drag(const PlatEvent *ev)
{
    const AppWindow *win = &wins[drag_win];
    int w = to_real(win->w), h = to_real(win->h);
    int x = drag_start_x[drag_win] + ev->sx - drag_sx;
    int y = drag_start_y[drag_win] + ev->sy - drag_sy;
    int snapped_x = 0, snapped_y = 0, i;

    for (i = 0; i < WIN_COUNT; i++) {
        const AppWindow *other = &wins[i];
        int ox1 = other->x + to_real(other->w), oy1 = other->y + to_real(other->h);

        if (drag_group[i] || !other->visible)
            continue;
        if (y < oy1 + SNAP_DISTANCE && other->y < y + h + SNAP_DISTANCE) {
            x = snap(x, ox1, &snapped_x);
            x = snap(x + w, other->x, &snapped_x) - w;
            x = snap(x, other->x, &snapped_x);
            x = snap(x + w, ox1, &snapped_x) - w;
        }
        if (x < ox1 + SNAP_DISTANCE && other->x < x + w + SNAP_DISTANCE) {
            y = snap(y, oy1, &snapped_y);
            y = snap(y + h, other->y, &snapped_y) - h;
            y = snap(y, other->y, &snapped_y);
            y = snap(y + h, oy1, &snapped_y) - h;
        }
    }
    for (i = 0; i < WIN_COUNT; i++) {
        if (!drag_group[i])
            continue;
        wins[i].x = drag_start_x[i] + x - drag_start_x[drag_win];
        wins[i].y = drag_start_y[i] + y - drag_start_y[drag_win];
        if (wins[i].visible)
            plat_window_set_pos(wins[i].plat, wins[i].x, wins[i].y);
    }
}

/* --- Playback --------------------------------------------------------------- */

static long file_size(const char *path)
{
    FILE *f = plat_fopen(path, "rb");
    long size = 0;

    if (f) {
        if (fseek(f, 0, SEEK_END) == 0)
            size = ftell(f);
        fclose(f);
    }
    return size < 0 ? 0 : size;
}

static void clamp_scroll(void)
{
    int limit = playlist_count() - pl_visible_rows();

    if (pl_scroll > limit)
        pl_scroll = limit;
    if (pl_scroll < 0)
        pl_scroll = 0;
}

static void scroll_into_view(int index)
{
    if (index < pl_scroll)
        pl_scroll = index;
    else if (index >= pl_scroll + pl_visible_rows())
        pl_scroll = index - pl_visible_rows() + 1;
    clamp_scroll();
}

/* Sets the playlist window size (unscaled pixels). */
static void resize_playlist(int w, int h)
{
    AppWindow *win = &wins[WIN_PL];
    uint32_t *grown;

    w = CLAMP(w, PL_MIN_W, PL_MAX_W);
    h = CLAMP(h, PL_MIN_H, PL_MAX_H);
    if (win->fb && w == win->w && h == win->h)
        return;
    grown = realloc(win->fb, sizeof(uint32_t) * to_real(w) * to_real(h));
    if (!grown)
        return;
    win->fb = grown;
    win->w = w;
    win->h = h;
    win->redraw = 1;
    pl_set_size(w, h);
    if (win->plat)
        plat_window_resize(win->plat, w, h);
    clamp_scroll();
}

/* The loaded track's line in the main window: number, title and length. */
static void show_title(void)
{
    const Track *track = playlist_get(track_index);
    int length = (int)audio_length();

    if (!track)
        return;
    if (audio_is_stream() && stream_song[0])        /* a station: the song, then the station */
        snprintf(title, sizeof title, "%d. %s (%s)", track_index + 1, stream_song, track->title);
    else if (length > 0)
        snprintf(title, sizeof title, "%d. %s (%d:%02d)", track_index + 1, track->title, length / 60, length % 60);
    else
        snprintf(title, sizeof title, "%d. %s", track_index + 1, track->title);
}

/* Updates what is shown about a track the audio engine has just started on. */
static void track_started(int index)
{
    const Track *track = playlist_get(index);
    double length = audio_length();

    if (!going_back && last_started >= 0 && last_started != index) {
        if (history_count == HISTORY_MAX)       /* full: the oldest goes */
            memmove(history, history + 1, sizeof history[0] * (size_t)--history_count);
        history[history_count++] = last_started;
    }
    last_started = index;

    track_index = index;
    track_loaded = 1;
    track_kbps = 0;
    stream_song[0] = '\0';
    playlist_dequeue(index);        /* its turn has come */
    if (length > 0)
        playlist_set_length(index, (int)length);
    show_title();
    if (path_is_cd(track->path)) {
        char device[CD_DEVICE_MAX];
        int number;

        track_kbps = CD_RATE * 2 * 16 / 1000;       /* always the same on a CD */
        /* Still nameless (a playlist from an earlier run, say): ask. */
        if (strncmp(track->title, "CD Track ", 9) == 0 && cd_split_path(track->path, device, sizeof device, &number))
            cd_names_request(device, cd_names_on);
    }
    else if (length > 0)
        track_kbps = (int)((double)file_size(track->path) * 8.0 / length / 1000.0 + 0.5);
    scroll_into_view(index);
    queue_dirty = 1;
    apply_auto_preset();
}

static int load_track(int index)
{
    const Track *track = playlist_get(index);

    if (!track)
        return 0;
    track_index = index;
    queued_index = -1;
    if (!audio_open(track->path)) {
        playlist_dequeue(index);    /* or the queue would offer it again and again */
        track_loaded = 0;
        snprintf(title, sizeof title, "CANNOT PLAY: %s", track->title);
        scroll_into_view(index);
        return 0;
    }
    track_started(index);
    return 1;
}

static void play_track(int index)
{
    if (load_track(index))
        audio_play();
    track_chosen = 1;       /* asked for, as opposed to arrived at (see play_next) */
}

/* Returns the track to play after the current one, or -1 at the end. */
static int next_track(void)
{
    int count = playlist_count(), queued = playlist_queue_head();

    if (queued >= 0)
        return queued;      /* tracks picked to play next come before any other order */
    if (shuffle && count > 1) {
        int index = rand() % (count - 1);

        return index >= track_index ? index + 1 : index;
    }
    if (track_index + 1 < count)
        return track_index + 1;
    return repeat && count ? 0 : -1;
}

/* Moves on to the following track, passing over files that cannot be
 * played. Returns 0 at the end of the list, or if nothing in it will play
 * (so a list of broken files cannot go round for ever). */
static int play_next(void)
{
    int tries = playlist_count();

    while (tries-- > 0) {
        int next = next_track();

        if (next < 0)
            return 0;
        if (load_track(next)) {     /* a failure leaves track_index there, so the search goes on from it */
            audio_play();
            track_chosen = 0;
            return 1;
        }
    }
    return 0;
}

/* Picks the following track and has the audio engine open it in advance,
 * so the change-over happens without a gap. */
static void update_queue(void)
{
    int next = track_loaded ? next_track() : -1;
    int result = audio_queue_next(next >= 0 ? playlist_get(next)->path : NULL);

    if (result < 0)
        return;     /* a hand-over just happened; retry once it is recorded */
    queue_dirty = 0;
    queued_index = result ? next : -1;
}

/* Call before track numbers shift: drops the gapless hand-over that was
 * prepared, which would otherwise point at the wrong track. It is chosen
 * again once the playlist has settled. */
static void forget_queued(void)
{
    if (queued_index >= 0 && audio_queue_next(NULL) >= 0)
        queued_index = -1;
}

/* Call when the track at `index` leaves the list: takes it out of the play
 * history and renumbers the tracks that came after it. */
static void history_forget(int index)
{
    int i, kept = 0;

    for (i = 0; i < history_count; i++)
        if (history[i] != index)
            history[kept++] = history[i] > index ? history[i] - 1 : history[i];
    history_count = kept;
    if (last_started == index)
        last_started = -1;
    else if (last_started > index)
        last_started--;
}

static void remove_track(int index)
{
    if (!playlist_get(index))
        return;
    forget_queued();
    history_forget(index);
    playlist_remove(index);
    if (index == track_index && track_loaded) {
        audio_stop();
        track_loaded = 0;
        snprintf(title, sizeof title, "AMPLITUDE");
    }
    if (index < track_index || track_index >= playlist_count())
        track_index = track_index > 0 ? track_index - 1 : 0;
    if (pl_cursor >= playlist_count())
        pl_cursor = playlist_count() - 1;
    clamp_scroll();
    queue_dirty = 1;
}

/* Removes the selected tracks, or with keep_selected the others (crop). */
static void remove_selected(int keep_selected)
{
    int i;

    for (i = playlist_count() - 1; i >= 0; i--)
        if ((!playlist_get(i)->selected) == keep_selected)
            remove_track(i);
}

static void remove_all(void)
{
    while (playlist_count())
        remove_track(playlist_count() - 1);
}

/* Removes the tracks whose files are gone. */
static void remove_missing(void)
{
    int i;

    for (i = playlist_count() - 1; i >= 0; i--) {
        const char *path = playlist_get(i)->path;
        FILE *f = path_is_url(path) || path_is_cd(path) ? NULL : plat_fopen(path, "rb");

        if (path_is_url(path) || path_is_cd(path))
            continue;       /* not a file; whether it answers is not checked here */
        if (f)
            fclose(f);
        else
            remove_track(i);
    }
}

/* Puts the selected tracks at the end of the play queue, in list order, or
 * takes out those already in it. */
static void queue_selected(void)
{
    int i;

    for (i = 0; i < playlist_count(); i++)
        if (playlist_get(i)->selected)
            playlist_queue_toggle(i);
    queue_dirty = 1;
}

/* Sorts, reverses or shuffles the list. The track being played keeps
 * playing; only its number changes. */
static void reorder(int command)
{
    forget_queued();
    switch (command) {
    case CMD_SORT_TITLE:    track_index = playlist_sort(PLAYLIST_BY_TITLE, track_index); break;
    case CMD_SORT_FILENAME: track_index = playlist_sort(PLAYLIST_BY_FILENAME, track_index); break;
    case CMD_SORT_PATH:     track_index = playlist_sort(PLAYLIST_BY_PATH, track_index); break;
    case CMD_REVERSE:       track_index = playlist_reverse(track_index); break;
    default:                track_index = playlist_randomize(track_index); break;
    }
    /* Every track has a new number; the history does not survive that. */
    history_count = 0;
    last_started = track_loaded ? track_index : -1;
    pl_cursor = pl_anchor = track_index;
    if (track_loaded) {
        show_title();
        scroll_into_view(track_index);
    }
    queue_dirty = 1;
}

static void set_volume(float v)
{
    volume = CLAMP(v, 0, 1);
    audio_set_volume(volume);
}

/* A step from the keyboard or the mouse wheel. There is no held slider to
 * show the new value, so it is put in the title display for a moment. */
static void step_volume(float by)
{
    set_volume(volume + by);
    volume_readout = 1;
    volume_readout_until = plat_ticks_ms() + READOUT_MS;
}

/* Takes a 0..1 slider position; snaps to the centre when close to it. */
static void set_balance(float slider)
{
    balance = slider * 2 - 1;
    if (balance > -BALANCE_SNAP && balance < BALANCE_SNAP)
        balance = 0;
    audio_set_balance(balance);
}

static void apply_eq(void)
{
    float bands[AUDIO_EQ_BANDS];
    int i;

    for (i = 0; i < AUDIO_EQ_BANDS; i++)
        bands[i] = eq_sliders[1 + i] * AUDIO_EQ_MAX_DB;
    audio_set_eq(eq_on, eq_sliders[0] * AUDIO_EQ_MAX_DB, bands);
}

static void set_preset(int index)
{
    int i;

    if (index < 0 || index >= preset_count)
        return;
    for (i = 0; i < AUDIO_EQ_BANDS; i++)
        eq_sliders[1 + i] = (float)presets[index].gain[i] / (10.0f * AUDIO_EQ_MAX_DB);
    apply_eq();
}

/* AUTO: choose a preset to suit the genre tag of the current track. Tracks
 * without a recognisable genre leave the sliders alone. */
static void apply_auto_preset(void)
{
    const Track *track = playlist_get(track_index);
    char genre[64];

    if (!eq_auto || !track_loaded || !track)
        return;
    tags_read_genre(track->path, genre, sizeof genre);
    set_preset(preset_for_genre(genre));
}

/* --- Skins and adding files ------------------------------------------------- */

/* Switches skin; NULL selects the built-in one. */
static void set_skin(const char *path)
{
    char full[sizeof config.skin] = "";

    if (path && !plat_absolute_path(path, full, sizeof full))
        snprintf(full, sizeof full, "%s", path);
    skin_free(&skin);
    skin_init_default(&skin);
    if (full[0] && !skin_load(&skin, full)) {
        fprintf(stderr, "amplitude: cannot load skin %s\n", full);
        full[0] = '\0';
    }
    snprintf(config.skin, sizeof config.skin, "%s", full);
    redraw_all();
}

/* Changes the colour of the built-in look. The skin holds colours derived
 * from it, so it is set up again. */
static void set_color(uint32_t color)
{
    char current[sizeof config.skin];

    config.color = (int)(color & 0xFFFFFF);
    theme_set(color);
    snprintf(current, sizeof current, "%s", config.skin);
    set_skin(current[0] ? current : NULL);
}

typedef struct {
    char **items;
    int count, capacity;
} PathList;

static void collect_path(const char *path, void *user)
{
    PathList *list = user;
    char *copy = malloc(strlen(path) + 1);

    if (list->count == list->capacity) {
        int capacity = list->capacity ? list->capacity * 2 : 64;
        char **grown = realloc(list->items, sizeof *grown * capacity);

        if (!grown) {
            free(copy);
            return;
        }
        list->items = grown;
        list->capacity = capacity;
    }
    if (copy) {
        strcpy(copy, path);
        list->items[list->count++] = copy;
    }
}

static int compare_paths(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int is_audio_file(const char *path)
{
    static const char *const extensions[] = {
        ".mp3", ".flac", ".wav", ".ogg", ".oga", ".opus", ".m4a", ".mp4", ".m4b", ".aac",
        ".mod", ".xm", ".s3m", ".it",
    };
    int i;

    for (i = 0; i < ARRAY_LEN(extensions); i++)
        if (path_has_extension(path, extensions[i]))
            return 1;
    return 0;
}

/* Adds the audio tracks of the disc in a drive, or of a disc image (a CUE
 * sheet). Returns the playlist index of the first, or -1 if there is no
 * such disc. */
static int add_cd(const char *device)
{
    Cd *cd = cd_open(device);
    const CdToc *toc;
    int i, first = -1;

    if (!cd)
        return -1;
    toc = cd_toc(cd);
    for (i = 0; i < toc->count; i++) {
        char path[CD_DEVICE_MAX + 32];
        int index;

        if (!toc->track[i].audio)
            continue;
        cd_make_path(path, sizeof path, device, toc->track[i].number);
        index = playlist_add(path);
        if (index < 0)
            break;
        playlist_set_length(index, (int)(toc->track[i].sectors / CD_SECTORS_PER_S));
        if (first < 0)
            first = index;
    }
    cd_close(cd);
    queue_dirty = 1;
    if (first >= 0)
        cd_names_request(device, cd_names_on);      /* the names follow when they are found */
    return first;
}

/* Windows shows the tracks of an audio CD as files, "D:\Track03.cda": a
 * few bytes each that say which track is meant. */
static void add_cda(const char *path)
{
    unsigned char head[24];
    char device[3] = { path[0], ':', '\0' }, track[32];
    FILE *f = plat_fopen(path, "rb");
    size_t got = f ? fread(head, 1, sizeof head, f) : 0;

    if (f)
        fclose(f);
    if (got != sizeof head || memcmp(head, "RIFF", 4) != 0 || memcmp(head + 8, "CDDA", 4) != 0 || path[1] != ':')
        return;
    cd_make_path(track, sizeof track, device, head[22] | head[23] << 8);
    playlist_add(track);
}

/* The disc in the first drive that has one: its tracks join the playlist,
 * and with `play` the first of them starts. */
static void open_cd(int play)
{
    char device[PLAT_CD_NAME];
    int first = cd_find_disc(device, sizeof device) ? add_cd(device) : -1;

    if (first < 0)
        snprintf(title, sizeof title, "NO AUDIO CD FOUND");
    else if (play)
        play_track(first);
}

/* A station's .pls file: "File1=address" lines, among others. */
static void add_pls(const char *path, int depth)
{
    char line[2048];
    FILE *f = plat_fopen(path, "r");

    if (!f)
        return;
    while (depth < MAX_FOLDER_DEPTH && fgets(line, sizeof line, f)) {
        char *value = strchr(line, '=');

        line[strcspn(line, "\r\n")] = '\0';
        if (value && (line[0] == 'F' || line[0] == 'f') && strncmp(line + 1, "ile", 3) == 0 && value[1])
            add_path(value + 1, depth + 1);
    }
    fclose(f);
}

/* Adds whatever `path` is: a track, a web address, a playlist, a folder
 * (recursively, in name order, audio files only) or a skin, which is loaded
 * instead. */
static void add_path(const char *path, int depth)
{
    char full[2048];
    PathList list = { NULL, 0, 0 };
    int i;

    if (path_has_extension(path, ".wsz")) {
        set_skin(path);
        return;
    }
    if (path_is_url(path) || path_is_cd(path)) {    /* a station, a file on the web, a CD track: taken as it is */
        playlist_add(path);
        queue_dirty = 1;
        return;
    }
    if (path_has_extension(path, ".pls")) {
        add_pls(path, depth);
        return;
    }
    /* Absolute paths keep the saved playlist valid from any directory. */
    if (!plat_absolute_path(path, full, sizeof full))
        snprintf(full, sizeof full, "%s", path);

    if (plat_is_dir(full)) {
        if (depth < MAX_FOLDER_DEPTH)
            plat_list_dir(full, collect_path, &list);
        qsort(list.items, (size_t)list.count, sizeof *list.items, compare_paths);
        for (i = 0; i < list.count; i++) {
            if (plat_is_dir(list.items[i]) || is_audio_file(list.items[i]))
                add_path(list.items[i], depth + 1);
            free(list.items[i]);
        }
        free(list.items);
    } else if (path_has_extension(full, ".m3u") || path_has_extension(full, ".m3u8")) {
        playlist_load(full, NULL);
    } else if (path_has_extension(full, ".cue")) {
        add_cd(full);       /* an image of a disc */
    } else if (path_has_extension(full, ".cda")) {
        add_cda(full);
    } else {
        playlist_add(full);
    }
    queue_dirty = 1;
}

static void add_from_dialog(const char *path, void *user)
{
    (void)user;
    add_path(path, 0);
}

/* For "Load skin...": the file dialog is the one for music, so anything
 * that is not a skin is ignored rather than added to the playlist. */
static void skin_from_dialog(const char *path, void *user)
{
    (void)user;
    if (path_has_extension(path, ".wsz") || path_has_extension(path, ".zip"))
        set_skin(path);
}

/* For "Open list": the list is emptied when the first chosen file arrives,
 * so cancelling the dialog leaves it as it was. */
static void replace_from_dialog(const char *path, void *user)
{
    int *cleared = user;

    if (!*cleared)
        remove_all();
    *cleared = 1;
    add_path(path, 0);
}

static void save_list(void)
{
    static const char extension[] = ".m3u";
    char path[1024];

    if (!plat_save_file_dialog(path, sizeof path - sizeof extension))
        return;
    if (!strchr(path_basename(path), '.'))      /* typed without one */
        strcat(path, extension);
    playlist_save(path);
}

static void collect_skin(const char *path, void *user)
{
    (void)user;
    if (skin_count < SKIN_MAX && (path_has_extension(path, ".wsz") || path_has_extension(path, ".zip"))) {
        skin_paths[skin_count] = malloc(strlen(path) + 1);
        if (skin_paths[skin_count])
            strcpy(skin_paths[skin_count++], path);
    }
}

/* Finds skins in <settings dir>/skins and next to the skin in use. */
static void scan_skins(void)
{
    char dir[sizeof config.skin + 16], *slash;
    int i, kept = 0;

    while (skin_count)
        free(skin_paths[--skin_count]);
    if (config_dir[0]) {
        snprintf(dir, sizeof dir, "%sskins", config_dir);
        plat_list_dir(dir, collect_skin, NULL);
    }
    snprintf(dir, sizeof dir, "%s", config.skin);
    for (slash = dir + strlen(dir); slash > dir && *slash != '/' && *slash != '\\'; slash--)
        ;
    if (slash > dir) {
        *slash = '\0';
        plat_list_dir(dir, collect_skin, NULL);
    }
    qsort(skin_paths, (size_t)skin_count, sizeof *skin_paths, compare_paths);
    for (i = 0; i < skin_count; i++) {      /* drop duplicates */
        if (kept && strcmp(skin_paths[kept - 1], skin_paths[i]) == 0)
            free(skin_paths[i]);
        else
            skin_paths[kept++] = skin_paths[i];
    }
    skin_count = kept;
}

/* --- Settings --------------------------------------------------------------- */

/* Works out the magnification from the settings and sizes the framebuffers
 * for it. Returns 0 if memory runs out. */
static int apply_scale(void)
{
    AppWindow *pl = &wins[WIN_PL];
    int i;

    /* Unless a size was chosen, the player is drawn at 1.2x on top of
     * whatever scaling the desktop itself asks for. */
    scale = config.scale ? config.scale : BASE_SCALE * plat_default_scale() / 100;
    scale = CLAMP(scale, SCALE_MIN, SCALE_MAX);

    for (i = 0; i < WIN_COUNT; i++) {
        free(wins[i].fb);
        wins[i].fb = i == WIN_PL ? NULL : malloc(sizeof(uint32_t) * to_real(wins[i].w) * to_real(wins[i].h));
    }
    resize_playlist(pl->w, pl->h);      /* allocates its own, after checking the size */
    redraw_all();
    for (i = 0; i < WIN_COUNT; i++)
        if (!wins[i].fb)
            return 0;
    return 1;
}

static int create_windows(void)
{
    static const char *const titles[WIN_COUNT] = {
        "Amplitude", "Amplitude Equalizer", "Amplitude Playlist", "Jump to file", "About Amplitude", "Open location" };
    int i;

    for (i = 0; i < WIN_COUNT; i++) {
        wins[i].plat = plat_window_create(titles[i], wins[i].w, wins[i].h, scale,
                                          i == WIN_MAIN ? NULL : wins[WIN_MAIN].plat);
        if (!wins[i].plat)
            return 0;
        wins[i].visible = 0;
        wins[i].exposed = 0;
    }
    plat_instance_claim(wins[WIN_MAIN].plat);
    plat_window_set_pos(wins[WIN_MAIN].plat, wins[WIN_MAIN].x, wins[WIN_MAIN].y);
    wins[WIN_MAIN].visible = wins[WIN_MAIN].placed = 1;
    plat_window_show(wins[WIN_MAIN].plat, 1);
    return 1;
}

/* Changes the size of the whole player while it runs; 0 means automatic. */
static void set_scale(int percent)
{
    int was_visible[WIN_COUNT], rel_x[WIN_COUNT], rel_y[WIN_COUNT], i;

    /* Remember the layout in unscaled units, rebuild every window at the
     * new size, then put the layout back. */
    sync_layout();
    for (i = 0; i < WIN_COUNT; i++) {
        was_visible[i] = wins[i].visible;
        rel_x[i] = to_logical(wins[i].rel_x);
        rel_y[i] = to_logical(wins[i].rel_y);
    }
    for (i = WIN_COUNT - 1; i >= 0; i--) {
        plat_window_destroy(wins[i].plat);
        wins[i].plat = NULL;
        wins[i].visible = 0;
    }
    pressed = UI_NONE;

    config.scale = percent;
    if (!apply_scale() || !create_windows()) {
        running = 0;
        return;
    }
    for (i = WIN_MAIN + 1; i < WIN_COUNT; i++) {
        wins[i].rel_x = to_real(rel_x[i]);
        wins[i].rel_y = to_real(rel_y[i]);
        if (was_visible[i])
            set_window_visible(i, 1);
    }
    /* At the new size the stack may be taller than the screen: then the
     * playlist goes beside the main window, as it would on first opening. */
    if (wins[WIN_PL].visible) {
        AppWindow *pl = &wins[WIN_PL];
        int x = pl->x, y = pl->y;

        dock_beside_if_too_tall(pl);
        if (pl->x != x || pl->y != y) {
            plat_window_set_pos(pl->plat, pl->x, pl->y);
            lift_stack();
        }
    }
}

/* Returns 0 if memory runs out. */
static int apply_config(void)
{
    int i;

    volume = (float)config.volume / 100.0f;
    balance = (float)config.balance / 100.0f;
    shuffle = config.shuffle;
    cd_names_on = config.cd_names;
    repeat = config.repeat;
    eq_on = config.eq_on;
    eq_auto = config.eq_auto;
    for (i = 0; i < EQ_SLIDERS; i++)
        eq_sliders[i] = (float)config.eq[i] / 1000.0f;
    vis_mode = config.vis_mode;
    theme_set((uint32_t)config.color);
    wins[WIN_PL].w = config.pl_w;
    wins[WIN_PL].h = config.pl_h;
    return apply_scale();
}

/* Moves a w*h rectangle (real pixels) the least distance needed to lie
 * inside the monitor it is on, or nearest to. */
static void keep_on_screen(int *x, int *y, int w, int h)
{
    int sx, sy, sw, sh;

    plat_screen_rect(*x, *y, &sx, &sy, &sw, &sh);
    if (*x > sx + sw - w)
        *x = sx + sw - w;
    if (*y > sy + sh - h)
        *y = sy + sh - h;
    if (*x < sx)
        *x = sx;
    if (*y < sy)
        *y = sy;
}

/* Puts the windows where they were last time, keeping the main window on screen. */
static void restore_layout(void)
{
    AppWindow *main_win = &wins[WIN_MAIN];

    if (!config.has_layout)
        return;
    main_win->x = config.main_x;
    main_win->y = config.main_y;
    keep_on_screen(&main_win->x, &main_win->y, to_real(main_win->w), to_real(main_win->h));

    /* An offset of 0,0 means the window was never shown, so it has no place yet. */
    wins[WIN_EQ].rel_x = to_real(config.eq_x);
    wins[WIN_EQ].rel_y = to_real(config.eq_y);
    wins[WIN_EQ].placed = config.eq_x || config.eq_y;
    wins[WIN_PL].rel_x = to_real(config.pl_x);
    wins[WIN_PL].rel_y = to_real(config.pl_y);
    wins[WIN_PL].placed = config.pl_x || config.pl_y;
}

static void collect_config(void)
{
    int i;

    config.volume = (int)(volume * 100.0f + 0.5f);
    config.balance = (int)(balance * 100.0f + (balance < 0 ? -0.5f : 0.5f));
    config.shuffle = shuffle;
    config.cd_names = cd_names_on;
    config.repeat = repeat;
    config.eq_on = eq_on;
    config.eq_auto = eq_auto;
    for (i = 0; i < EQ_SLIDERS; i++)
        config.eq[i] = (int)(eq_sliders[i] * 1000.0f + (eq_sliders[i] < 0 ? -0.5f : 0.5f));
    config.track = track_index;
    config.vis_mode = vis_mode;
    config.pl_w = wins[WIN_PL].w;
    config.pl_h = wins[WIN_PL].h;

    sync_layout();
    config.has_layout = 1;
    config.main_x = wins[WIN_MAIN].x;
    config.main_y = wins[WIN_MAIN].y;
    config.eq_visible = wins[WIN_EQ].visible;
    config.eq_x = to_logical(wins[WIN_EQ].rel_x);
    config.eq_y = to_logical(wins[WIN_EQ].rel_y);
    config.pl_visible = wins[WIN_PL].visible;
    config.pl_x = to_logical(wins[WIN_PL].rel_x);
    config.pl_y = to_logical(wins[WIN_PL].rel_y);
}

/* --- Jump to file ----------------------------------------------------------- */

/* Form of a character used for searching: accents removed, then lower
 * case, for the scripts our fonts cover. */
static unsigned long fold_case(unsigned long cp)
{
    cp = gfx_unaccent(cp);
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 0x391 && cp <= 0x3A9) || (cp >= 0x410 && cp <= 0x42F))
        return cp + 0x20;
    if (cp >= 0x400 && cp <= 0x40F)
        return cp + 0x50;
    return cp;
}

/* Decodes UTF-8 into search-folded code points. Returns how many. */
static int fold_text(const char *text, unsigned long *out, int max)
{
    const unsigned char *p = (const unsigned char *)text;
    int n = 0;

    while (*p && n < max) {
        int extra = *p >= 0xF0 ? 3 : *p >= 0xE0 ? 2 : *p >= 0xC0 ? 1 : 0;
        unsigned long cp = extra == 3 ? *p & 0x07 : extra == 2 ? *p & 0x0F : extra ? *p & 0x1F : *p;

        for (p++; extra && (*p & 0xC0) == 0x80; extra--, p++)
            cp = cp << 6 | (*p & 0x3F);
        out[n++] = fold_case(cp);
    }
    return n;
}

/* Does the title contain every word of the query, ignoring case and accents? */
static int jump_matches_title(const unsigned long *query, int query_len, const char *text)
{
    unsigned long folded[TITLE_MAX];
    int title_len = fold_text(text, folded, TITLE_MAX), start = 0, i, j;

    while (start < query_len) {
        int end = start, found = 0;

        while (end < query_len && query[end] != ' ')
            end++;
        for (i = 0; end > start && i + (end - start) <= title_len && !found; i++) {
            for (j = 0; j < end - start && folded[i + j] == query[start + j]; j++)
                ;
            found = j == end - start;
        }
        if (end > start && !found)
            return 0;
        start = end + 1;
    }
    return 1;
}

static void clamp_jump_scroll(void)
{
    if (jump_scroll > jump_count - JUMP_ROWS)
        jump_scroll = jump_count - JUMP_ROWS;
    if (jump_scroll < 0)
        jump_scroll = 0;
}

static void jump_scroll_to_selection(void)
{
    if (jump_selected < jump_scroll)
        jump_scroll = jump_selected;
    else if (jump_selected >= jump_scroll + JUMP_ROWS)
        jump_scroll = jump_selected - JUMP_ROWS + 1;
    clamp_jump_scroll();
}

/* Rebuilds the list of matches for the current query. */
static void jump_update(void)
{
    unsigned long query[QUERY_MAX];
    int query_len = fold_text(jump_query, query, QUERY_MAX), count = playlist_count(), i;
    int *grown = realloc(jump_matches, sizeof *grown * (count ? count : 1));

    if (!grown)
        return;
    jump_matches = grown;
    jump_count = 0;
    for (i = 0; i < count; i++)
        if (jump_matches_title(query, query_len, playlist_get(i)->title))
            jump_matches[jump_count++] = i;
    jump_selected = jump_scroll = 0;
}

static void open_jump(void)
{
    int i;

    jump_query[0] = '\0';
    jump_update();
    /* Start on the track that is playing. */
    for (i = 0; i < jump_count; i++)
        if (jump_matches[i] == track_index)
            jump_selected = i;
    jump_scroll_to_selection();
    open_dialog(WIN_JUMP, JUMP_OFFSET);
}

static void jump_play_selected(void)
{
    if (jump_selected >= 0 && jump_selected < jump_count)
        play_track(jump_matches[jump_selected]);
    set_window_visible(WIN_JUMP, 0);
}

/* Puts the highlighted match in the play queue (or takes it back out) and
 * leaves the window open, so that several can be queued in a row. */
static void jump_enqueue_selected(void)
{
    if (jump_selected >= 0 && jump_selected < jump_count) {
        playlist_queue_toggle(jump_matches[jump_selected]);
        queue_dirty = 1;
    }
}

/* --- Open location ------------------------------------------------------------ */

static void open_url_window(void)
{
    url_text[0] = '\0';
    open_dialog(WIN_URL, URL_OFFSET);
}

/* Adds the typed address and plays it. */
static void url_accept(void)
{
    char *start = url_text, *end = url_text + strlen(url_text);
    int before = playlist_count();

    while (*start == ' ')
        start++;
    while (end > start && end[-1] == ' ')
        *--end = '\0';
    set_window_visible(WIN_URL, 0);
    if (!*start)
        return;
    if (!path_is_url(start) && !strstr(start, "://")) {     /* "radio.example/live" */
        char full[sizeof url_text + 8];

        snprintf(full, sizeof full, "http://%s", start);
        add_path(full, 0);
    } else {
        add_path(start, 0);
    }
    if (playlist_count() > before)
        play_track(before);
}

/* Keyboard input while the Open location window is open. */
static void url_input(const PlatEvent *ev)
{
    size_t len = strlen(url_text);

    if (ev->type == PEV_TEXT) {
        if (len + strlen(ev->text) < sizeof url_text)
            strcat(url_text, ev->text);
        return;
    }
    switch (ev->key) {
    case PK_ESCAPE:
        set_window_visible(WIN_URL, 0);
        break;
    case PK_ENTER:
        url_accept();
        break;
    case PK_BACKSPACE:
        while (len && ((unsigned char)url_text[len - 1] & 0xC0) == 0x80)
            len--;
        if (len)
            url_text[len - 1] = '\0';
        break;
    case 'V':
        if (ev->mods & PMOD_CTRL) {     /* paste, keeping only what can be an address */
            char pasted[sizeof url_text];
            size_t i;

            if (!plat_clipboard_text(pasted, sizeof pasted))
                break;
            for (i = 0; pasted[i] && len + 1 < sizeof url_text; i++)
                if ((unsigned char)pasted[i] > ' ')
                    url_text[len++] = pasted[i];
            url_text[len] = '\0';
        }
        break;
    }
}

/* Keyboard input while the jump window is open. */
static void jump_input(const PlatEvent *ev)
{
    size_t len = strlen(jump_query);

    if (ev->type == PEV_TEXT) {
        if (jump_skip_text) {
            jump_skip_text = 0;
            return;
        }
        if (len + strlen(ev->text) < sizeof jump_query) {
            strcat(jump_query, ev->text);
            jump_update();
        }
        return;
    }
    jump_skip_text = 0;
    switch (ev->key) {
    case PK_ESCAPE:
        set_window_visible(WIN_JUMP, 0);
        break;
    case PK_ENTER:
        /* Enter plays it now; Shift+Enter queues it to play next. */
        if (ev->mods & PMOD_SHIFT)
            jump_enqueue_selected();
        else
            jump_play_selected();
        break;
    case 'Q':
        if (ev->mods & PMOD_CTRL)       /* a plain Q is a letter of the search */
            jump_enqueue_selected();
        break;
    case PK_UP:
    case PK_DOWN:
        jump_selected += ev->key == PK_UP ? -1 : 1;
        jump_selected = jump_selected >= jump_count ? jump_count - 1 : jump_selected;
        jump_selected = jump_selected < 0 ? 0 : jump_selected;
        jump_scroll_to_selection();
        break;
    case PK_BACKSPACE:
        /* Remove one whole character, however many bytes it takes. */
        while (len && ((unsigned char)jump_query[len - 1] & 0xC0) == 0x80)
            len--;
        if (len)
            jump_query[len - 1] = '\0';
        jump_update();
        break;
    }
}

/* --- Popup menu ------------------------------------------------------------- */

static void close_menu(void)
{
    if (menu_win)
        plat_window_destroy(menu_win);
    free(menu_fb);
    menu_win = NULL;
    menu_fb = NULL;
    menu_hover = -1;
}

static void menu_add(const char *label, int id, int checked)
{
    if (menu_count == MENU_MAX)
        return;
    if (label) {
        snprintf(menu_labels[menu_count], sizeof menu_labels[0], "%s", label);
        menu_items[menu_count].label = menu_labels[menu_count];
    } else {
        menu_items[menu_count].label = NULL;
    }
    menu_items[menu_count].id = id;
    menu_items[menu_count].checked = checked;
    menu_items[menu_count].swatch = 0;
    menu_count++;
}

/* Opens the main menu, the size list or one page of the skin list at
 * menu_sx/menu_sy. */
static void open_menu(int which)
{
    int i, x = menu_sx, y = menu_sy;

    close_menu();
    menu_count = 0;
    if (which == MENU_COLORS) {
        menu_add("< Back", CMD_BACK, 0);
        menu_add(NULL, 0, 0);
        for (i = 0; i < theme_preset_count; i++) {
            menu_add(theme_presets[i].name, CMD_COLOR_FIRST + i, (uint32_t)config.color == theme_presets[i].color);
            menu_items[menu_count - 1].swatch = theme_presets[i].color + 1;
        }
    } else if (which == MENU_ADD) {
        menu_add("Add files...", CMD_ADD_FILES, 0);
        menu_add("Add folder...", CMD_ADD_FOLDER, 0);
        menu_add("Add location...", CMD_OPEN_URL, 0);
        menu_add("Add audio CD", CMD_ADD_CD, 0);
    } else if (which == MENU_REMOVE) {
        menu_add("Remove selected", CMD_REMOVE_SELECTED, 0);
        menu_add("Crop to selected", CMD_CROP, 0);
        menu_add("Remove missing files", CMD_REMOVE_MISSING, 0);
        menu_add(NULL, 0, 0);
        menu_add("Remove all", CMD_REMOVE_ALL, 0);
    } else if (which == MENU_SELECT) {
        menu_add("Select all", CMD_SELECT_ALL, 0);
        menu_add("Select none", CMD_SELECT_NONE, 0);
        menu_add("Invert selection", CMD_SELECT_INVERT, 0);
    } else if (which == MENU_MISC) {
        menu_add("Sort by title", CMD_SORT_TITLE, 0);
        menu_add("Sort by file name", CMD_SORT_FILENAME, 0);
        menu_add("Sort by folder and file", CMD_SORT_PATH, 0);
        menu_add(NULL, 0, 0);
        menu_add("Reverse list", CMD_REVERSE, 0);
        menu_add("Randomize list", CMD_RANDOMIZE, 0);
        menu_add(NULL, 0, 0);
        menu_add("Queue selected to play next", CMD_QUEUE_SELECTED, 0);
        menu_add("Clear queue", CMD_QUEUE_CLEAR, 0);
        menu_add(NULL, 0, 0);
        menu_add("Look up CD names online", CMD_CD_NAMES, cd_names_on);
    } else if (which == MENU_LIST) {
        menu_add("New list", CMD_LIST_NEW, 0);
        menu_add("Open list...", CMD_LIST_OPEN, 0);
        menu_add("Save list...", CMD_LIST_SAVE, 0);
    } else if (which == MENU_PRESETS) {
        for (i = 0; i < preset_count; i++)
            menu_add(presets[i].name, CMD_PRESET_FIRST + i, 0);
    } else if (which == MENU_SIZES) {
        menu_add("< Back", CMD_BACK, 0);
        menu_add(NULL, 0, 0);
        menu_add("Automatic", CMD_SIZE_AUTO, !config.scale);
        for (i = 0; i < SIZE_COUNT; i++) {
            char label[16];

            snprintf(label, sizeof label, "%d%%", size_choices[i]);
            menu_add(label, CMD_SIZE_FIRST + i, config.scale == size_choices[i]);
        }
    } else if (which == MENU_MAIN) {
        menu_add("Add files...", CMD_ADD_FILES, 0);
        menu_add("Add folder...", CMD_ADD_FOLDER, 0);
        menu_add("Open location... (Ctrl+L)", CMD_OPEN_URL, 0);
        menu_add("Play audio CD", CMD_PLAY_CD, 0);
        menu_add("Jump to file... (Ctrl+J)", CMD_JUMP, 0);
        menu_add(NULL, 0, 0);
        menu_add("Equalizer", CMD_EQ, wins[WIN_EQ].visible);
        menu_add("Playlist", CMD_PL, wins[WIN_PL].visible);
        menu_add(NULL, 0, 0);
        menu_add("Shuffle", CMD_SHUFFLE, shuffle);
        menu_add("Repeat", CMD_REPEAT, repeat);
        menu_add(NULL, 0, 0);
        menu_add("Skins...", CMD_SKINS, 0);
        menu_add("Size...", CMD_SIZES, 0);
        menu_add("Color...", CMD_COLORS, 0);
        menu_add(NULL, 0, 0);
        menu_add("About...", CMD_ABOUT, 0);
        menu_add("Exit", CMD_EXIT, 0);
    } else {
        menu_add("< Back", CMD_BACK, 0);
        menu_add(NULL, 0, 0);
        menu_add("Load skin...", CMD_SKIN_LOAD, 0);
        menu_add(NULL, 0, 0);
        menu_add("Default skin", CMD_SKIN_BUILTIN, !config.skin[0]);
        for (i = skin_page * SKIN_PAGE; i < skin_count && i < (skin_page + 1) * SKIN_PAGE; i++) {
            /* "dir/Some Skin.wsz" -> "Some Skin", as UTF-8 that fits the label */
            char name[1024], label[sizeof menu_labels[0]], *dot;

            snprintf(name, sizeof name, "%s", path_basename(skin_paths[i]));
            dot = strrchr(name, '.');
            if (dot)
                *dot = '\0';
            text_to_utf8(label, sizeof label, (const unsigned char *)name, strlen(name), TEXT_UTF8);
            menu_add(label, CMD_SKIN_FIRST + i, strcmp(skin_paths[i], config.skin) == 0);
        }
        if (skin_count > (skin_page + 1) * SKIN_PAGE) {
            menu_add(NULL, 0, 0);
            menu_add("More...", CMD_SKIN_MORE, 0);
        }
    }

    menu_measure(menu_items, menu_count, scale, &menu_w, &menu_h);
    menu_fb = malloc(sizeof(uint32_t) * to_real(menu_w) * to_real(menu_h));
    if (!menu_fb)
        return;
    if (which >= MENU_ADD)
        y -= to_real(menu_h);       /* pops up above the button */
    keep_on_screen(&x, &y, to_real(menu_w), to_real(menu_h));
    menu_win = plat_popup_create(menu_w, menu_h, scale, x, y, wins[WIN_MAIN].plat);
    if (!menu_win)
        close_menu();
}

/* --- Input ------------------------------------------------------------------ */

static void do_action(int element)
{
    char path[1024];
    int next, before = playlist_count(), cleared = 0;

    switch (element) {
    case UI_PREV:
        /* In list order, the track above. With shuffle on there is no such
         * order, so it goes back to what was actually played before. */
        if (shuffle && history_count) {
            /* A track played earlier may have vanished from the disk since;
             * such a one is passed over, as when moving forward. */
            going_back = 1;
            while (history_count && !load_track(history[--history_count]))
                ;
            if (track_loaded)
                audio_play();
            going_back = 0;
        } else {
            play_track(track_index > 0 ? track_index - 1 : 0);
        }
        break;
    case UI_NEXT:
        if (queued_index >= 0)
            play_track(queued_index);
        else
            play_next();
        break;
    case UI_MENU:
        /* The same menu as a right click, opening under the cog. */
        sync_position(&wins[WIN_MAIN]);
        menu_sx = wins[WIN_MAIN].x + to_real(UI_MENU_X);
        menu_sy = wins[WIN_MAIN].y + to_real(UI_MENU_Y);
        open_menu(MENU_MAIN);
        break;
    case UI_SHUFFLE:
        shuffle = !shuffle;
        queue_dirty = 1;
        break;
    case UI_REPEAT:
        repeat = !repeat;
        queue_dirty = 1;
        break;
    case UI_PLAY:
        if (audio_state() == AUDIO_PAUSED)
            audio_pause();
        else if (track_loaded && !audio_is_stream())
            audio_play();       /* a stopped station is connected afresh, below */
        else
            play_track(track_index);
        break;
    case UI_PAUSE:
        audio_pause();
        break;
    case UI_STOP:
        audio_stop();
        break;
    case UI_PL_ADD:
    case UI_PL_REMOVE:
    case UI_PL_SELECT:
    case UI_PL_MISC:
    case UI_PL_LISTOPTS:
        /* Each of the playlist's buttons opens a menu just above itself. */
        sync_position(&wins[WIN_PL]);
        menu_sx = wins[WIN_PL].x +
                  to_real(element == UI_PL_LISTOPTS ? pl_list_button_x() : PL_BUTTON_X(element - UI_PL_ADD));
        menu_sy = wins[WIN_PL].y + to_real(wins[WIN_PL].h - PL_BUTTONS_FROM_BOTTOM);
        open_menu(element == UI_PL_LISTOPTS ? MENU_LIST : MENU_ADD + (element - UI_PL_ADD));
        break;
    case UI_PL_PREV:
    case UI_PL_PLAY:
    case UI_PL_PAUSE:
    case UI_PL_STOP:
    case UI_PL_NEXT:
        do_action(UI_PREV + (element - UI_PL_PREV));    /* same order as the main window's */
        break;
    case UI_PL_OPEN:
        do_action(UI_OPEN);
        break;
    case UI_OPEN:
    case CMD_ADD_FILES:
        plat_open_files_dialog(add_from_dialog, NULL);
        if (element == UI_OPEN && playlist_count() > before)
            play_track(before);
        break;
    case CMD_ADD_FOLDER:
        if (plat_open_folder_dialog(path, sizeof path))
            add_path(path, 0);
        break;
    case CMD_REMOVE_SELECTED:
        remove_selected(0);
        break;
    case CMD_CROP:
        remove_selected(1);
        break;
    case CMD_REMOVE_ALL:
    case CMD_LIST_NEW:
        remove_all();
        break;
    case CMD_REMOVE_MISSING:
        remove_missing();
        break;
    case CMD_SELECT_ALL:
        playlist_select_all();
        break;
    case CMD_SELECT_NONE:
        playlist_select_range(-1, -1);
        break;
    case CMD_SELECT_INVERT:
        playlist_invert_selection();
        break;
    case CMD_SORT_TITLE:
    case CMD_SORT_FILENAME:
    case CMD_SORT_PATH:
    case CMD_REVERSE:
    case CMD_RANDOMIZE:
        reorder(element);
        break;
    case CMD_QUEUE_SELECTED:
        queue_selected();
        break;
    case CMD_QUEUE_CLEAR:
        playlist_queue_clear();
        queue_dirty = 1;
        break;
    case CMD_LIST_OPEN:
        plat_open_files_dialog(replace_from_dialog, &cleared);
        break;
    case CMD_LIST_SAVE:
        save_list();
        break;
    case UI_EQ_TOGGLE:
    case CMD_EQ:
        set_window_visible(WIN_EQ, !wins[WIN_EQ].visible);
        break;
    case UI_PL_TOGGLE:
    case CMD_PL:
        set_window_visible(WIN_PL, !wins[WIN_PL].visible);
        break;
    case UI_EQ_CLOSE:
        set_window_visible(WIN_EQ, 0);
        break;
    case UI_PL_CLOSE:
        set_window_visible(WIN_PL, 0);
        break;
    case UI_JUMP_CLOSE:
    case UI_JUMP_DISMISS:
        set_window_visible(WIN_JUMP, 0);
        break;
    case UI_PL_JUMP:
    case CMD_JUMP:
        open_jump();
        jump_skip_text = 0;     /* opened by the mouse: no key press to swallow */
        break;
    case UI_JUMP_PLAY:
        jump_play_selected();
        break;
    case CMD_OPEN_URL:
        open_url_window();
        break;
    case CMD_CD_NAMES:
        cd_names_on = !cd_names_on;
        break;
    case CMD_PLAY_CD:
    case CMD_ADD_CD:
        open_cd(element == CMD_PLAY_CD);
        break;
    case UI_URL_OPEN:
        url_accept();
        break;
    case UI_URL_CLOSE:
    case UI_URL_CANCEL:
        set_window_visible(WIN_URL, 0);
        break;
    case UI_JUMP_ENQUEUE:
        jump_enqueue_selected();
        break;
    case UI_LOGO:
    case CMD_ABOUT:
        open_dialog(WIN_ABOUT, ABOUT_OFFSET);
        break;
    case UI_ABOUT_CLOSE:
        set_window_visible(WIN_ABOUT, 0);
        break;
    case UI_ABOUT_LINK:
        plat_open_url(ABOUT_URL);
        break;
    case UI_EQ_ON:
        eq_on = !eq_on;
        apply_eq();
        break;
    case UI_EQ_AUTO:
        eq_auto = !eq_auto;
        apply_auto_preset();
        break;
    case UI_EQ_PRESETS:
        /* The list drops down from the button. */
        sync_position(&wins[WIN_EQ]);
        menu_sx = wins[WIN_EQ].x + to_real(EQ_PRESETS_X);
        menu_sy = wins[WIN_EQ].y + to_real(EQ_PRESETS_Y);
        open_menu(MENU_PRESETS);
        break;
    case UI_EQ_ZERO:        /* resets the preamp as well */
        memset(eq_sliders, 0, sizeof eq_sliders);
        apply_eq();
        break;
    case UI_EQ_MAX:
    case UI_EQ_MIN:
        for (next = 1; next < EQ_SLIDERS; next++)
            eq_sliders[next] = element == UI_EQ_MAX ? 1.0f : -1.0f;
        apply_eq();
        break;
    case UI_VIS:
        vis_mode = (vis_mode + 1) % VIS_MODES;
        break;
    case UI_MINIMIZE:
        plat_window_minimize(wins[WIN_MAIN].plat);
        break;
    case UI_CLOSE:
    case CMD_EXIT:
        running = 0;
        break;
    }
}

static void menu_command(int id)
{
    switch (id) {
    case CMD_SHUFFLE:
        do_action(UI_SHUFFLE);
        break;
    case CMD_REPEAT:
        do_action(UI_REPEAT);
        break;
    case CMD_SKINS:
        scan_skins();
        skin_page = 0;
        open_menu(MENU_SKINS);
        break;
    case CMD_SKIN_MORE:
        skin_page++;
        open_menu(MENU_SKINS);
        break;
    case CMD_SIZES:
        open_menu(MENU_SIZES);
        break;
    case CMD_COLORS:
        open_menu(MENU_COLORS);
        break;
    case CMD_SIZE_AUTO:
        set_scale(0);
        break;
    case CMD_BACK:
        open_menu(MENU_MAIN);
        break;
    case CMD_SKIN_BUILTIN:
        set_skin(NULL);
        break;
    case CMD_SKIN_LOAD:
        plat_open_files_dialog(skin_from_dialog, NULL);
        break;
    default:
        if (id >= CMD_SKIN_FIRST && id - CMD_SKIN_FIRST < skin_count)
            set_skin(skin_paths[id - CMD_SKIN_FIRST]);
        else if (id >= CMD_COLOR_FIRST && id - CMD_COLOR_FIRST < theme_preset_count)
            set_color(theme_presets[id - CMD_COLOR_FIRST].color);
        else if (id >= CMD_PRESET_FIRST && id - CMD_PRESET_FIRST < preset_count)
            set_preset(id - CMD_PRESET_FIRST);
        else if (id >= CMD_SIZE_FIRST && id - CMD_SIZE_FIRST < SIZE_COUNT)
            set_scale(size_choices[id - CMD_SIZE_FIRST]);
        else
            do_action(id);
        break;
    }
}

/* A media key, or the same request from the desktop's media controls. */
static void media_key(int key)
{
    int playing = audio_state() == AUDIO_PLAYING;

    switch (key) {
    case PK_MEDIA_PLAY:
        if (!playing)
            do_action(UI_PLAY);
        break;
    case PK_MEDIA_PAUSE:
        if (playing)
            do_action(UI_PAUSE);
        break;
    case PK_MEDIA_PLAY_PAUSE:
        do_action(playing ? UI_PAUSE : UI_PLAY);    /* UI_PLAY also resumes from pause */
        break;
    case PK_MEDIA_STOP: do_action(UI_STOP); break;
    case PK_MEDIA_NEXT: do_action(UI_NEXT); break;
    case PK_MEDIA_PREV: do_action(UI_PREV); break;
    }
}

/* Keyboard: moves the playlist cursor and selects just that track. */
static void select_track(int index)
{
    int count = playlist_count();

    if (!count)
        return;
    pl_cursor = pl_anchor = CLAMP(index, 0, count - 1);
    playlist_select_range(pl_cursor, pl_cursor);
    scroll_into_view(pl_cursor);
}

static void handle_key(int win, int key, int mods)
{
    if (win == WIN_PL) {
        switch (key) {
        case PK_UP:     select_track(pl_cursor - 1); return;
        case PK_DOWN:   select_track(pl_cursor + 1); return;
        case PK_ENTER:  play_track(pl_cursor); return;
        case PK_DELETE: remove_selected(0); return;
        case 'Q':       queue_selected(); return;
        case 'A':
            if (mods & PMOD_CTRL) {
                playlist_select_all();
                return;
            }
            break;
        }
    }
    switch (key) {
    case 'Z': do_action(UI_PREV); break;
    case 'X': do_action(UI_PLAY); break;
    case 'C': do_action(UI_PAUSE); break;
    case 'V': do_action(UI_STOP); break;
    case 'B': do_action(UI_NEXT); break;
    case 'L': do_action(mods & PMOD_CTRL ? CMD_OPEN_URL : UI_OPEN); break;
    case 'S': do_action(UI_SHUFFLE); break;
    case 'R': do_action(UI_REPEAT); break;
    case 'G': do_action(UI_EQ_TOGGLE); break;
    case 'E': do_action(UI_PL_TOGGLE); break;
    case 'J':                           /* with or without Ctrl */
        open_jump();
        jump_skip_text = !(mods & PMOD_CTRL);
        break;
    case PK_LEFT:  audio_seek(audio_position() - SEEK_STEP); break;
    case PK_RIGHT: audio_seek(audio_position() + SEEK_STEP); break;
    case PK_UP:    step_volume(VOLUME_STEP); break;
    case PK_DOWN:  step_volume(-VOLUME_STEP); break;
    }
}

static int hit_test(int win, int x, int y, int *slider)
{
    switch (win) {
    case WIN_EQ: return eq_hit(x, y, slider);
    case WIN_PL: return pl_hit(&skin, x, y);
    case WIN_JUMP: return jump_hit(x, y);
    case WIN_ABOUT: return about_hit(x, y);
    case WIN_URL: return url_hit(x, y);
    default:     return ui_hit(&skin, x, y);
    }
}

/* Rounds a pixel distance to a whole number of steps. */
static int round_to_step(int distance, int step)
{
    return (distance >= 0 ? distance + step / 2 : distance - step / 2) / step * step;
}

/* Applies the held slider or drag to the current mouse position. */
static void track_mouse(const PlatEvent *ev)
{
    switch (pressed) {
    case UI_TITLEBAR:
    case UI_EQ_TITLEBAR:
    case UI_PL_TITLEBAR:
    case UI_JUMP_TITLEBAR:
    case UI_ABOUT_TITLEBAR:
    case UI_URL_TITLEBAR:
        update_drag(ev);
        break;
    case UI_VOLUME:
        set_volume(ui_slider_value(UI_VOLUME, ev->x));
        break;
    case UI_BALANCE:
        set_balance(ui_slider_value(UI_BALANCE, ev->x));
        break;
    case UI_SEEK:
        seek_drag = ui_slider_value(UI_SEEK, ev->x);
        break;
    case UI_EQ_SLIDER:
        eq_sliders[pressed_slider] = eq_slider_value(ev->y);
        apply_eq();
        break;
    case UI_PL_SCROLL:
        pl_scroll = pl_scroll_at(ev->y, playlist_count());
        break;
    case UI_PL_RESIZE:
        resize_playlist(resize_start_w + round_to_step(to_logical(ev->sx - drag_sx), PL_STEP_W),
                        resize_start_h + round_to_step(to_logical(ev->sy - drag_sy), PL_STEP_H));
        break;
    }
}

/* Click: select one track. Ctrl+click: toggle it. Shift+click: select the
 * range from the last plain click. Double click: play. */
static void click_playlist_row(int row, int mods)
{
    const Track *track = playlist_get(row);
    uint32_t now = plat_ticks_ms();

    if (!track) {
        playlist_select_range(-1, -1);
        return;
    }
    pl_cursor = row;
    if (mods & PMOD_CTRL) {
        playlist_select(row, !track->selected);
        pl_anchor = row;
    } else if ((mods & PMOD_SHIFT) && pl_anchor >= 0) {
        playlist_select_range(pl_anchor, row);
    } else {
        playlist_select_range(row, row);
        pl_anchor = row;
        if (row == click_row && now - click_ticks < DOUBLE_CLICK_MS) {
            play_track(row);
            click_row = -1;
            return;
        }
    }
    click_row = row;
    click_ticks = now;
}

static void handle_event(const PlatEvent *ev)
{
    int win = window_index(ev->win), slider = 0, before;

    if (ev->type == PEV_FOCUS_OUT) {
        close_menu();       /* a click elsewhere may never be reported; see the platform layers */
        return;
    }
    if (menu_win) {
        if (ev->win == menu_win) {
            int item = menu_item_at(menu_items, menu_count, menu_w, ev->x, ev->y);

            if (ev->type == PEV_MOUSE_MOVE) {
                menu_hover = item;
            } else if (ev->type == PEV_MOUSE_DOWN) {
                int id = item >= 0 ? menu_items[item].id : 0;

                close_menu();
                if (id)
                    menu_command(id);
            }
            return;
        }
        if (ev->type == PEV_MOUSE_DOWN || ev->type == PEV_KEY_DOWN) {
            close_menu();
            if (ev->type == PEV_KEY_DOWN)
                return;
        }
    }
    if (win < 0)
        return;
    /* Media keys act whatever window or program has the keyboard. */
    if (ev->type == PEV_KEY_DOWN && ev->key >= PK_MEDIA_PLAY && ev->key <= PK_MEDIA_PREV) {
        media_key(ev->key);
        return;
    }
    /* While the jump window is open it gets everything typed, whichever of
     * our windows has the keyboard. */
    if (wins[WIN_URL].visible && (ev->type == PEV_KEY_DOWN || ev->type == PEV_TEXT)) {
        url_input(ev);
        return;
    }
    if (wins[WIN_JUMP].visible && (ev->type == PEV_KEY_DOWN || ev->type == PEV_TEXT)) {
        jump_input(ev);
        return;
    }
    if (wins[WIN_ABOUT].visible && ev->type == PEV_KEY_DOWN && ev->key == PK_ESCAPE) {
        set_window_visible(WIN_ABOUT, 0);
        return;
    }

    switch (ev->type) {
    case PEV_EXPOSE:
        wins[win].redraw = 1;       /* the screen lost what the window showed */
        wins[win].exposed = 1;
        break;
    case PEV_QUIT:
        if (win == WIN_MAIN)
            running = 0;
        else
            set_window_visible(win, 0);
        break;
    case PEV_KEY_DOWN:
        handle_key(win, ev->key, ev->mods);
        break;
    case PEV_FILE:
        /* Files dropped on the playlist are only added; elsewhere, and from
         * another instance unless it asked to enqueue, the first one plays. */
        if (ev->flags & PFILE_FIRST) {
            batch_play = !(ev->flags & PFILE_ENQUEUE) && ((ev->flags & PFILE_REMOTE) || win != WIN_PL);
            /* A file manager asked to open several files may start the
             * program once per file. Those starts arrive moments apart and
             * are one request: the first file plays, the rest are added. */
            if (ev->flags & PFILE_REMOTE) {
                uint32_t now = plat_ticks_ms();

                if (opened_before && now - opened_ticks < MULTI_OPEN_MS)
                    batch_play = 0;
                opened_ticks = now;
                opened_before = 1;
            }
        }
        before = playlist_count();
        add_path(ev->path, 0);
        if (batch_play && playlist_count() > before) {
            play_track(before);
            batch_play = 0;
        }
        break;
    case PEV_WHEEL:
        if (win == WIN_PL) {
            pl_scroll -= ev->wheel * WHEEL_ROWS;
            clamp_scroll();
        } else if (win == WIN_JUMP) {
            jump_scroll -= ev->wheel * WHEEL_ROWS;
            clamp_jump_scroll();
        } else {
            step_volume((float)ev->wheel * VOLUME_STEP);
        }
        break;
    case PEV_MOUSE_DOWN:
        if (ev->button != 1)
            break;
        pressed = hit_test(win, ev->x, ev->y, &slider);
        pressed_slider = slider;
        if (pressed == UI_JUMP_LIST) {
            int row = jump_scroll + jump_row_at(ev->y);
            uint32_t now = plat_ticks_ms();

            if (row < jump_count) {
                if (row == jump_selected && row == click_row && now - click_ticks < DOUBLE_CLICK_MS)
                    jump_play_selected();
                jump_selected = click_row = row;
                click_ticks = now;
            }
        }
        if (pressed == UI_TITLEBAR || pressed == UI_EQ_TITLEBAR || pressed == UI_PL_TITLEBAR ||
            pressed == UI_JUMP_TITLEBAR || pressed == UI_ABOUT_TITLEBAR || pressed == UI_URL_TITLEBAR) {
            begin_drag(win, ev);
        } else if (pressed == UI_PL_LIST) {
            click_playlist_row(pl_scroll + pl_row_at(ev->y), ev->mods);
        } else if (pressed == UI_PL_RESIZE) {
            drag_sx = ev->sx;
            drag_sy = ev->sy;
            resize_start_w = wins[WIN_PL].w;
            resize_start_h = wins[WIN_PL].h;
        }
        track_mouse(ev);
        break;
    case PEV_MOUSE_MOVE:
        track_mouse(ev);
        break;
    case PEV_MOUSE_UP:
        if (ev->button == 3) {
            menu_sx = ev->sx;
            menu_sy = ev->sy;
            open_menu(MENU_MAIN);
            break;
        }
        if (ev->button != 1)
            break;
        if (pressed == UI_SEEK)
            audio_seek(seek_drag * audio_length());
        else if (pressed && pressed == hit_test(win, ev->x, ev->y, &slider))
            do_action(pressed);
        pressed = UI_NONE;
        break;
    }
}

/* --- Rendering -------------------------------------------------------------- */

/* What the title display shows in place of the track name while a slider
 * is held: its value, as the classic players did. NULL when none is. */
static const char *slider_readout(void)
{
    static const char *const bands[EQ_SLIDERS] = {
        "Preamp", "60 Hz", "170 Hz", "310 Hz", "600 Hz", "1 kHz", "3 kHz", "6 kHz", "12 kHz", "14 kHz", "16 kHz" };
    static char text[64];
    int percent, to, length;

    /* After a step by key or wheel; the difference is taken as signed so
     * that the tick counter wrapping round does no harm. */
    if (volume_readout && (int32_t)(volume_readout_until - plat_ticks_ms()) <= 0)
        volume_readout = 0;

    switch (pressed) {
    case UI_NONE:
        if (!volume_readout)
            return NULL;
        /* fall through */
    case UI_VOLUME:
        snprintf(text, sizeof text, "Volume: %d%%", (int)(volume * 100.0f + 0.5f));
        return text;
    case UI_BALANCE:
        percent = (int)((balance < 0 ? -balance : balance) * 100.0f + 0.5f);
        if (percent)
            snprintf(text, sizeof text, "Balance: %d%% %s", percent, balance < 0 ? "left" : "right");
        else
            snprintf(text, sizeof text, "Balance: center");
        return text;
    case UI_SEEK:
        length = (int)audio_length();
        if (!track_loaded || length <= 0)
            return NULL;
        to = (int)(seek_drag * (float)length);
        snprintf(text, sizeof text, "Seek: %d:%02d / %d:%02d (%d%%)", to / 60, to % 60, length / 60, length % 60,
                 (int)(seek_drag * 100.0f + 0.5f));
        return text;
    case UI_EQ_SLIDER:
        snprintf(text, sizeof text, "EQ: %s: %+.1f dB", bands[pressed_slider],
                 (double)(eq_sliders[pressed_slider] * AUDIO_EQ_MAX_DB));
        return text;
    }
    return NULL;
}

static void render(void)
{
    static float vis[AUDIO_VIS_SAMPLES];
    UiModel model;
    EqModel eq_model;
    PlModel pl_model;

    audio_get_vis(vis);
    memset(&model, 0, sizeof model);
    model.title = slider_readout();
    if (!model.title)
        model.title = audio_buffering() ? "Buffering..." : title;
    model.state = audio_state();
    model.length = audio_length();
    model.position = model.state == AUDIO_STOPPED ? 0 : audio_position();
    if (pressed == UI_SEEK)
        model.position = seek_drag * model.length;
    if (model.position > model.length && model.length > 0)
        model.position = model.length;
    model.loaded = track_loaded;
    model.balance = balance;
    model.shuffle = shuffle;
    model.repeat = repeat;
    model.eq_visible = wins[WIN_EQ].visible;
    model.pl_visible = wins[WIN_PL].visible;
    if (track_loaded) {
        model.kbps = track_kbps;
        model.khz = audio_sample_rate() / 1000;
        model.channels = audio_channels();
    }
    model.volume = volume;
    model.pressed = pressed;
    model.ticks = plat_ticks_ms();
    model.vis = vis;
    model.vis_mode = vis_mode;
    /* The desktop's media controls show the track's own title, not the
     * numbered line or a slider's readout. */
    plat_media_update(model.state, track_loaded && playlist_get(track_index) ? playlist_get(track_index)->title : "",
                      model.position, model.length);
    /* The main window changes a little in most frames (the title moves,
     * the spectrum, the clock) and all over only rarely: only what changed
     * is painted again and looked at for sending to the screen. */
    {
        int top, bottom;

        if (wins[WIN_MAIN].redraw)
            ui_invalidate();
        wins[WIN_MAIN].redraw = 0;
        ui_update(wins[WIN_MAIN].fb, scale, &skin, &model, &top, &bottom);
        plat_window_present_rows(wins[WIN_MAIN].plat, wins[WIN_MAIN].fb, top, bottom);
    }

    /* The equaliser and playlist change rarely, so they are drawn only
     * when their model differs from the one on screen. */
    if (wins[WIN_EQ].visible) {
        static EqModel shown;

        memset(&eq_model, 0, sizeof eq_model);
        eq_model.on = eq_on;
        eq_model.auto_on = eq_auto;
        memcpy(eq_model.sliders, eq_sliders, sizeof eq_sliders);
        eq_model.pressed = pressed;
        eq_model.pressed_slider = pressed_slider;
        if (wins[WIN_EQ].redraw || memcmp(&eq_model, &shown, sizeof shown) != 0) {
            eq_draw(wins[WIN_EQ].fb, scale, &skin, &eq_model);
            plat_window_present(wins[WIN_EQ].plat, wins[WIN_EQ].fb);
            shown = eq_model;
            wins[WIN_EQ].redraw = 0;
        }
    }
    if (wins[WIN_PL].visible) {
        static PlModel shown;

        memset(&pl_model, 0, sizeof pl_model);
        pl_model.current = track_loaded ? track_index : -1;
        pl_model.scroll = pl_scroll;
        pl_model.pressed = pressed;
        pl_model.position = track_loaded ? (int)audio_position() : -1;
        pl_model.revision = playlist_revision();
        if (wins[WIN_PL].redraw || memcmp(&pl_model, &shown, sizeof shown) != 0) {
            pl_draw(wins[WIN_PL].fb, scale, &skin, &pl_model);
            plat_window_present(wins[WIN_PL].plat, wins[WIN_PL].fb);
            shown = pl_model;
            wins[WIN_PL].redraw = 0;
        }
    }
    if (wins[WIN_JUMP].visible) {
        JumpModel jump_model;

        jump_model.query = jump_query;
        jump_model.matches = jump_matches;
        jump_model.match_count = jump_count;
        jump_model.selected = jump_selected;
        jump_model.scroll = jump_scroll;
        jump_model.pressed = pressed;
        jump_model.ticks = plat_ticks_ms();
        jump_draw(wins[WIN_JUMP].fb, scale, &jump_model);
        plat_window_present(wins[WIN_JUMP].plat, wins[WIN_JUMP].fb);
    }
    if (wins[WIN_URL].visible) {
        url_draw(wins[WIN_URL].fb, scale, url_text, pressed, plat_ticks_ms());
        plat_window_present(wins[WIN_URL].plat, wins[WIN_URL].fb);
    }
    if (wins[WIN_ABOUT].visible) {
        about_draw(wins[WIN_ABOUT].fb, scale, pressed);
        plat_window_present(wins[WIN_ABOUT].plat, wins[WIN_ABOUT].fb);
    }
    if (menu_win) {
        menu_draw(menu_fb, menu_w, menu_h, scale, menu_items, menu_count, menu_hover);
        plat_window_present(menu_win, menu_fb);
    }
}

static void free_paths(char **paths, int count)
{
    while (count)
        free(paths[--count]);
    free(paths);
}

/* Releases everything, in the reverse order of main()'s setting up. Safe
 * whichever stage was reached. */
static void shutdown_all(void)
{
    int i;

    close_menu();
    audio_shutdown();
    for (i = WIN_COUNT - 1; i >= 0; i--) {
        if (wins[i].plat)
            plat_window_destroy(wins[i].plat);
        wins[i].plat = NULL;
        free(wins[i].fb);
        wins[i].fb = NULL;
    }
    plat_shutdown();
    skin_free(&skin);
    playlist_free();
    while (skin_count)
        free(skin_paths[--skin_count]);
    free(jump_matches);
    jump_matches = NULL;
}

/* Called now and then during the slow part of starting up (reading the
 * saved playlist): handles what the system and the user have sent and
 * repaints, so the windows are never left blank or unresponsive. Limited to
 * the pace of ordinary frames, since it may be called very often. */
static void keep_alive(void)
{
    static uint32_t last;
    uint32_t now = plat_ticks_ms();
    PlatEvent ev;

    if (now - last < FRAME_MS)
        return;
    last = now;
    while (plat_poll_event(&ev))
        handle_event(&ev);
    render();
}

/* Has every window that is shown been put on screen by the system? */
static int all_exposed(void)
{
    int i;

    for (i = 0; i < WIN_COUNT; i++)
        if (wins[i].visible && !wins[i].exposed)
            return 0;
    return 1;
}

/* Names for the tracks of a CD have arrived: they replace "CD Track 01"
 * and so on, wherever in the playlist tracks of that disc stand. */
static void cd_names_news(void)
{
    const CdNames *names = cd_names_take();
    char device[CD_DEVICE_MAX], text[400];
    int i, t, number;

    if (!names)
        return;
    for (i = 0; i < playlist_count(); i++) {
        const char *path = playlist_get(i)->path;

        if (!path_is_cd(path) || !cd_split_path(path, device, sizeof device, &number) ||
            strcmp(device, names->device) != 0)
            continue;
        for (t = 0; t < names->count && names->track[t].number != number; t++)
            ;
        if (t == names->count)
            continue;
        if (names->track[t].artist[0] || names->artist[0])
            snprintf(text, sizeof text, "%s - %s", names->track[t].artist[0] ? names->track[t].artist : names->artist,
                     names->track[t].title);
        else
            snprintf(text, sizeof text, "%s", names->track[t].title);
        playlist_set_title(i, text);
    }
    if (track_loaded)
        show_title();
}

/* What a stream has to report since the last frame: that it could not be
 * opened after all, the station's name and bitrate, the song it plays. */
static void stream_news(void)
{
    static uint32_t title_asked;
    const Track *track = playlist_get(track_index);
    const char *name;

    audio_update();
    if (audio_take_failed() && track) {
        track_loaded = 0;
        snprintf(title, sizeof title, "CANNOT PLAY: %s", track->title);
        /* Like a file that cannot be played: passed over when the list
         * came to it by itself, left standing when it was asked for. A
         * list of nothing but dead stations must not go round for ever. */
        if (!track_chosen && ++stream_failures < playlist_count() && !play_next())
            audio_stop();
        return;
    }
    if (track_loaded && !audio_buffering())
        stream_failures = 0;        /* something plays */
    if (!track_loaded || !audio_is_stream() || audio_buffering())
        return;
    name = audio_stream_name();
    if (name[0] && track && strcmp(track->title, name) != 0) {
        playlist_set_title(track_index, name);      /* in place of the bare address */
        show_title();
    }
    /* The song changes every few minutes, and asking (like measuring the
     * bitrate of a station that states none) can mean waiting for the
     * audio thread: twice a second is plenty. */
    if (plat_ticks_ms() - title_asked >= STREAM_TITLE_MS) {
        int kbps = audio_stream_bitrate();

        if (kbps)
            track_kbps = kbps;
        title_asked = plat_ticks_ms();
        if (audio_stream_title(stream_song, sizeof stream_song))
            show_title();
    }
}

/* Paints the windows as soon as they exist and waits, briefly, until the
 * system has said they are on screen and they have been painted there.
 * Drawing into a window before that can be thrown away, which showed as a
 * black window whenever the rest of starting up was slow. */
static void first_frame(void)
{
    uint32_t start = plat_ticks_ms();
    PlatEvent ev;

    render();
    while (running && !all_exposed() && plat_ticks_ms() - start < FIRST_FRAME_MS) {
        plat_wait(FIRST_FRAME_POLL_MS);
        while (plat_poll_event(&ev))
            handle_event(&ev);
        render();
    }
}

int main(int argc, char **argv)
{
    char config_path[1100] = "", playlist_path[1100] = "";
    const char *skin_arg = NULL;
    char **paths;
    int path_count = 0, enqueue = 0, arg_scale = -1, i;

    plat_args(&argc, &argv);        /* may replace both, so nothing is sized from them before this */
    paths = calloc((size_t)argc + 1, sizeof *paths);
    if (!paths)
        return 1;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            /* A factor such as 2 or 1.5; "auto" parses as 0. */
            arg_scale = (int)(atof(argv[++i]) * 100.0 + 0.5);
        } else if (strcmp(argv[i], "--skin") == 0 && i + 1 < argc) {
            skin_arg = argv[++i];
        } else if (strcmp(argv[i], "--enqueue") == 0) {
            enqueue = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: amplitude [--scale N|auto] [--skin file.wsz|default] [--enqueue] [file|folder...]\n");
            free_paths(paths, path_count);
            return 0;
        } else {
            char full[2048];

            if (path_is_url(argv[i]) || path_is_cd(argv[i]) || !plat_absolute_path(argv[i], full, sizeof full))
                snprintf(full, sizeof full, "%s", argv[i]);
            paths[path_count] = malloc(strlen(full) + 1);
            if (paths[path_count])
                strcpy(paths[path_count++], full);
        }
    }

    if (!plat_init()) {
        free_paths(paths, path_count);
        return 1;
    }
    /* A player that is already running takes the files and we are done. */
    if (plat_instance_send((const char *const *)paths, path_count, enqueue)) {
        free_paths(paths, path_count);
        plat_shutdown();
        return 0;
    }

    /* Saved settings first; the command line then overrides them. */
    config_defaults(&config);
    if (plat_config_dir(config_dir, sizeof config_dir)) {
        snprintf(config_path, sizeof config_path, "%samplitude.ini", config_dir);
        snprintf(playlist_path, sizeof playlist_path, "%samplitude.m3u", config_dir);
        config_load(&config, config_path);
    }
    /* A scale given on the command line is remembered; "auto" forgets it. */
    if (arg_scale == 0 || (arg_scale >= SCALE_MIN && arg_scale <= SCALE_MAX))
        config.scale = arg_scale;
    if (!apply_config()) {
        free_paths(paths, path_count);
        shutdown_all();
        return 1;
    }

    /* A skin named on the command line becomes the remembered one. */
    skin_init_default(&skin);
    if (skin_arg && strcmp(skin_arg, "default") == 0)
        set_skin(NULL);
    else
        set_skin(skin_arg ? skin_arg : config.skin);
    srand((unsigned)plat_ticks_ms());

    /* Sound comes up before the windows do. Reaching the sound server can
     * take a while just after the computer has started, and nothing should
     * be on screen, unpainted, during that. It also means the windows are
     * never open while the audio functions are not ready to be called. */
    if (!audio_init())
        snprintf(title, sizeof title, "NO AUDIO DEVICE");
    set_volume(volume);
    audio_set_balance(balance);
    apply_eq();

    restore_layout();
    if (!create_windows()) {
        free_paths(paths, path_count);
        shutdown_all();
        return 1;
    }
    /* The equaliser and playlist come back as they were left. On the very
     * first run there is nothing saved, and both are shown, so that a new
     * user sees the whole player and not just its smallest part. */
    if (!config.has_layout || config.eq_visible)
        set_window_visible(WIN_EQ, 1);
    if (!config.has_layout || config.pl_visible)
        set_window_visible(WIN_PL, 1);
    /* The playlist is read next, and with a long list on a disk that has
     * not been read yet that takes time too. The windows get their picture
     * first, and keep_alive() looks after them while the list is read. */
    first_frame();

    /* Without files (or when only enqueueing) the last playlist comes back,
     * paused at the track that was current. */
    if ((!path_count || enqueue) && playlist_path[0] && playlist_load(playlist_path, keep_alive)) {
        track_index = config.track < playlist_count() ? config.track : 0;
        scroll_into_view(track_index);
    }
    for (i = 0; i < path_count; i++) {
        int before = playlist_count();

        add_path(paths[i], 0);
        if (!enqueue && !track_loaded && playlist_count() > before)
            play_track(before);
    }
    if (path_count) {       /* later starts in the same moment belong with these */
        opened_ticks = plat_ticks_ms();
        opened_before = 1;
    }
    free_paths(paths, path_count);

    while (running) {
        PlatEvent ev;

        while (plat_poll_event(&ev))
            handle_event(&ev);
        if (!running)
            break;      /* the windows may be gone already (see set_scale()) */

        if (audio_take_advanced() && playlist_get(queued_index))
            track_started(queued_index);
        if (audio_take_finished() && !play_next())
            audio_stop();
        stream_news();
        cd_names_news();
        if (queue_dirty)
            update_queue();

        render();
        /* Until the next beat of the frame clock, which runs by the wall
         * clock and not from the end of this frame, so that every frame
         * shows a scrolling title exactly one step further. */
        if (audio_state() == AUDIO_PLAYING || pressed || menu_win || ui_title_scrolls()) {
            uint64_t beat = ui_title_scrolls() ? (uint64_t)ui_marquee_frame_us(scale) : FRAME_MS * 1000;
            uint64_t into = (uint64_t)plat_ticks_ms() * 1000 % beat;

            plat_wait((int)((beat - into + 999) / 1000));
        } else {
            plat_wait(IDLE_MS);
        }
    }

    collect_config();
    if (config_path[0]) {
        config_save(&config, config_path);
        playlist_save(playlist_path);
    }

    shutdown_all();
    return 0;
}
