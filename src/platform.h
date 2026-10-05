/*
 * Platform layer: windows, input, timing. One implementation per OS
 * (platform_x11.c, platform_win32.c). Everything above this layer is
 * portable C and draws into plain 0x00RRGGBB framebuffers.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct PlatWindow PlatWindow;

enum {
    PEV_NONE,
    PEV_QUIT,
    PEV_EXPOSE,
    PEV_MOUSE_DOWN,
    PEV_MOUSE_UP,
    PEV_MOUSE_MOVE,
    PEV_WHEEL,
    PEV_KEY_DOWN,
    PEV_TEXT,       /* a character was typed (follows the PEV_KEY_DOWN of the key) */
    PEV_FILE        /* a file was dropped on a window or sent by another instance */
};

/* Keys are reported as uppercase ASCII where possible, otherwise PK_*. */
enum {
    PK_LEFT = 256,
    PK_RIGHT,
    PK_UP,
    PK_DOWN,
    PK_ESCAPE,
    PK_ENTER,
    PK_DELETE,
    PK_BACKSPACE,
    /* The media keys of a keyboard, Bluetooth headphones and the like, and
     * the desktop's media controls. They reach the main window whichever
     * program has the keyboard. */
    PK_MEDIA_PLAY,
    PK_MEDIA_PAUSE,
    PK_MEDIA_PLAY_PAUSE,
    PK_MEDIA_STOP,
    PK_MEDIA_NEXT,
    PK_MEDIA_PREV
};

/* PlatEvent.mods */
#define PMOD_SHIFT 1
#define PMOD_CTRL  2

/* PlatEvent.flags for PEV_FILE */
#define PFILE_FIRST   1     /* first file of a batch */
#define PFILE_ENQUEUE 2     /* the sender asked not to start playback */
#define PFILE_REMOTE  4     /* sent by another instance rather than dropped */

typedef struct {
    int type;
    PlatWindow *win;
    int x, y;       /* window coordinates, in unscaled framebuffer pixels */
    int sx, sy;     /* screen coordinates, in real pixels */
    int button;     /* 1 = left, 2 = middle, 3 = right */
    int wheel;      /* PEV_WHEEL: 1 = away from the user, -1 = towards */
    int key;
    int mods;       /* PMOD_* held during a mouse or key event */
    int flags;      /* PFILE_* */
    const char *path;   /* PEV_FILE; valid until the next plat_poll_event() */
    char text[8];       /* PEV_TEXT: the character as UTF-8 */
} PlatEvent;

/* Magnification is given in percent. A w*h framebuffer is shown at
 * PLAT_SCALED(w) x PLAT_SCALED(h) real pixels; both sides of the platform
 * layer use this macro so their sizes agree to the pixel. */
#define PLAT_SCALED(v, percent) (((v) * (percent) + 50) / 100)

typedef void (*PlatPathFn)(const char *path, void *user);

int  plat_init(void);
void plat_shutdown(void);

/* Creates a hidden, borderless, fixed-size window for a w*h layout drawn at
 * `scale` percent. A window with an owner stays above it and gets no
 * taskbar entry of its own. */
PlatWindow *plat_window_create(const char *title, int w, int h, int scale, PlatWindow *owner);
/* Creates and shows a menu-style window at a screen position. It grabs the
 * mouse, so clicks anywhere are reported relative to it until it is destroyed. */
PlatWindow *plat_popup_create(int w, int h, int scale, int sx, int sy, PlatWindow *owner);
void plat_window_destroy(PlatWindow *win);
void plat_window_show(PlatWindow *win, int visible);
/* Changes the layout size (unscaled pixels) of the window. */
void plat_window_resize(PlatWindow *win, int w, int h);
/* Shows a frame of PLAT_SCALED(w) x PLAT_SCALED(h) real pixels. The window
 * keeps a copy, and only the rows that differ from it are sent to the
 * screen, so presenting an unchanged frame costs next to nothing. */
void plat_window_present(PlatWindow *win, const uint32_t *pixels);

/* For the platform layers: the first row in which two frames of w x h
 * pixels differ, and the row after the last such; equal if they are alike. */
static inline void plat_changed_rows(const uint32_t *a, const uint32_t *b, int w, int h, int *top, int *bottom)
{
    size_t row = sizeof(uint32_t) * (size_t)w;

    *top = 0;
    *bottom = h;
    while (*top < *bottom && memcmp(a + (size_t)*top * w, b + (size_t)*top * w, row) == 0)
        ++*top;
    while (*bottom > *top && memcmp(a + (size_t)(*bottom - 1) * w, b + (size_t)(*bottom - 1) * w, row) == 0)
        --*bottom;
}
void plat_window_get_pos(PlatWindow *win, int *sx, int *sy);
void plat_window_set_pos(PlatWindow *win, int sx, int sy);
void plat_window_minimize(PlatWindow *win);
/* Brings a visible window to the front and gives it the keyboard. */
void plat_window_focus(PlatWindow *win);

/* Returns 1 and fills *ev if an event was pending, 0 otherwise. */
int  plat_poll_event(PlatEvent *ev);
/* Blocks until an event arrives or timeout_ms elapses. */
void plat_wait(int timeout_ms);

uint32_t plat_ticks_ms(void);

/* Native dialogs. The file dialog allows several files and reports each
 * through `fn`; it returns how many were chosen. */
int  plat_open_files_dialog(PlatPathFn fn, void *user);
int  plat_open_folder_dialog(char *out, size_t out_size);
/* Tells the desktop what is playing, for its media controls where it has
 * any: state 0 stopped, 1 playing, 2 paused; title in UTF-8; times in
 * seconds. Cheap to call every frame. */
void plat_media_update(int state, const char *title, double position, double length);

/* Opens a web address in the user's browser. */
void plat_open_url(const char *url);
/* Asks where to save a playlist. Returns 0 if cancelled. The name comes
 * back as typed, so it may lack an extension. */
int  plat_save_file_dialog(char *out, size_t out_size);

int  plat_is_dir(const char *path);
/* Calls `fn` with the full path of every entry in a directory, in no
 * particular order. Returns 0 if it cannot be read. */
int  plat_list_dir(const char *path, PlatPathFn fn, void *user);

/* Single instance. plat_instance_send() hands the paths to an already
 * running player (bringing it to the front) and returns 1, or returns 0 if
 * there is none; the first instance then calls plat_instance_claim(). */
int  plat_instance_send(const char *const *paths, int count, int enqueue);
void plat_instance_claim(PlatWindow *main_window);

/* The desktop's display scaling in percent (100 = no scaling). Call after
 * plat_init(). */
int  plat_default_scale(void);
/* The usable area, in real pixels, of the monitor that contains (or is
 * nearest to) a screen position. With several monitors their coordinates
 * continue from one to the next and can be negative, so the area does not
 * always start at 0,0. Call after plat_init(). */
void plat_screen_rect(int sx, int sy, int *x, int *y, int *w, int *h);
/* Directory for the settings files, with a trailing separator; it is
 * created if necessary. Returns 0 if there is nowhere to store them. */
int  plat_config_dir(char *out, size_t out_size);
/* File names.
 *
 * Every path the program handles is UTF-8, on every system: the paths the
 * platform layer hands out (command line, dialogs, dropped files, directory
 * listings) and the paths it accepts. Portable code must therefore open
 * files with plat_fopen() and never with fopen(), which on Windows would
 * misread any name outside the system's legacy code page. */
FILE *plat_fopen(const char *path, const char *mode);
/* Replaces argc/argv with the command line as UTF-8. The strings stay valid
 * for the life of the program. */
void plat_args(int *argc, char ***argv);
/* Turns a possibly relative path into an absolute one. Returns 0 on failure. */
int  plat_absolute_path(const char *path, char *out, size_t out_size);

#endif
