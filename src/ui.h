/* The three windows (main, equaliser, playlist): classic layouts drawn from
 * the active skin, plus hit testing. Input handling lives in main.c. */
#ifndef AMPLITUDE_UI_H
#define AMPLITUDE_UI_H

#include "audio.h"
#include "skin.h"

#include <stdint.h>

#define UI_W 275
#define UI_H 116
#define EQ_W 275
#define EQ_H 116
/* The playlist grows from its minimum size in steps of one frame tile. */
#define PL_MIN_W  275
#define PL_MIN_H  116
#define PL_STEP_W 25
#define PL_STEP_H 29

/* Element ids are unique across all windows. */
enum {
    UI_NONE,
    /* Main window */
    UI_TITLEBAR,
    UI_MINIMIZE,
    UI_CLOSE,
    UI_PREV,
    UI_PLAY,
    UI_PAUSE,
    UI_STOP,
    UI_NEXT,
    UI_OPEN,
    UI_SHUFFLE,
    UI_REPEAT,
    UI_MENU,                /* the cog; built-in skin only */
    UI_LOGO,                /* opens the About window */
    UI_EQ_TOGGLE,
    UI_PL_TOGGLE,
    UI_VIS,
    UI_SEEK,
    UI_VOLUME,
    UI_BALANCE,
    /* Equaliser window */
    UI_EQ_TITLEBAR,
    UI_EQ_CLOSE,
    UI_EQ_ON,
    UI_EQ_AUTO,
    UI_EQ_PRESETS,
    UI_EQ_MAX,              /* the +12, 0 and -12 dB labels */
    UI_EQ_ZERO,
    UI_EQ_MIN,
    UI_EQ_SLIDER,
    /* Playlist window */
    UI_PL_TITLEBAR,
    UI_PL_CLOSE,
    UI_PL_LIST,
    UI_PL_SCROLL,
    UI_PL_ADD,
    UI_PL_REMOVE,
    UI_PL_SELECT,
    UI_PL_MISC,
    UI_PL_LISTOPTS,
    UI_PL_JUMP,             /* the magnifier: opens jump to file; built-in skin only */
    UI_PL_PREV,             /* the small transport controls */
    UI_PL_PLAY,
    UI_PL_PAUSE,
    UI_PL_STOP,
    UI_PL_NEXT,
    UI_PL_OPEN,
    UI_PL_RESIZE,
    /* Jump to file window */
    UI_JUMP_TITLEBAR,
    UI_JUMP_CLOSE,
    UI_JUMP_LIST,
    UI_JUMP_PLAY,           /* the JUMP button: play the highlighted track */
    UI_JUMP_ENQUEUE,        /* the ENQUEUE button: put it in the play queue */
    UI_JUMP_DISMISS,        /* the CLOSE button, as opposed to the one in the title bar */
    /* About window */
    UI_ABOUT_TITLEBAR,
    UI_ABOUT_CLOSE,
    UI_ABOUT_LINK           /* the website address */
};

enum { VIS_SPECTRUM, VIS_SCOPE, VIS_OFF, VIS_MODES };

#define UI_BLINK_MS 500     /* half period of the paused clock and the text cursor */

/* Bottom left corner of the cog button, where its menu opens. */
#define UI_MENU_X 214
#define UI_MENU_Y 105

/* --- Main window ---------------------------------------------------------- */

typedef struct {
    const char *title;
    int state;              /* AUDIO_* */
    int loaded;             /* a track is loaded */
    double position;        /* seconds */
    double length;          /* seconds, 0 if unknown */
    int kbps, khz, channels;
    float volume;           /* 0..1 */
    float balance;          /* -1 (left) .. 1 (right) */
    int shuffle, repeat;
    int eq_visible, pl_visible;
    int pressed;            /* UI_* element currently held down */
    uint32_t ticks;         /* drives the title marquee */
    const float *vis;       /* AUDIO_VIS_SAMPLES samples */
    int vis_mode;           /* VIS_* */
} UiModel;

/* Every *_draw() function renders at `scale` percent into a framebuffer of
 * GFX_SCALED(width) x GFX_SCALED(height) real pixels. */
void  ui_draw(uint32_t *framebuffer, int scale, const Skin *skin, const UiModel *model);
int   ui_hit(const Skin *skin, int x, int y);
/* Maps an x coordinate to 0..1 along the UI_SEEK, UI_VOLUME or UI_BALANCE slider. */
float ui_slider_value(int element, int x);

/* --- Equaliser window ----------------------------------------------------- */

#define EQ_SLIDERS (AUDIO_EQ_BANDS + 1)     /* slider 0 is the preamp */

typedef struct {
    int on;
    int auto_on;
    float sliders[EQ_SLIDERS];      /* -1 .. 1 */
    int pressed;
    int pressed_slider;
} EqModel;

/* Bottom left corner of the PRESETS button, where its list drops down. */
#define EQ_PRESETS_X 217
#define EQ_PRESETS_Y 30

void  eq_draw(uint32_t *framebuffer, int scale, const Skin *skin, const EqModel *model);
/* For UI_EQ_SLIDER, *slider receives the slider index. */
int   eq_hit(int x, int y, int *slider);
/* Maps a y coordinate to a slider value of -1 .. 1. */
float eq_slider_value(int y);

/* --- Playlist window ------------------------------------------------------ */

typedef struct {
    int current;            /* track being played */
    int scroll;             /* index of the first visible track */
    int pressed;
    int position;           /* seconds into the current track, -1 if none */
    unsigned revision;      /* playlist_revision(), so a model says when the list changed */
} PlModel;

/* Sets the window size (unscaled pixels) used by every pl_* function. */
void pl_set_size(int w, int h);
void pl_draw(uint32_t *framebuffer, int scale, const Skin *skin, const PlModel *model);
int  pl_hit(const Skin *skin, int x, int y);
/* Left edge of the ADD, REM, SEL and MISC buttons (n = 0..3) and of LIST,
 * for placing their menus. */
#define PL_BUTTON_PITCH 29
#define PL_BUTTON_X(n) (12 + (n) * PL_BUTTON_PITCH)
/* Their top edge, measured up from the bottom of the window. */
#define PL_BUTTONS_FROM_BOTTOM 30
int  pl_list_button_x(void);
int  pl_visible_rows(void);
/* Row (0 = first visible) under a y coordinate inside UI_PL_LIST. */
int  pl_row_at(int y);
/* Scroll position for a scrollbar drag at y, given the track count. */
int  pl_scroll_at(int y, int count);

/* --- Small windows in the built-in look ------------------------------------ */

#define DIALOG_TITLE_H 14

/* Body, border and title bar with a caption and a close button. */
void dialog_frame(Canvas *c, int w, int h, const char *caption, int close_pressed);
/* The given element id for the title bar or the close button, else UI_NONE. */
int  dialog_hit(int w, int x, int y, int titlebar, int close);

/* --- Jump to file window -------------------------------------------------- */

#define JUMP_W    275
#define JUMP_H    232       /* twice the main window */
#define JUMP_ROWS 16

typedef struct {
    const char *query;      /* UTF-8 text typed so far */
    const int *matches;     /* playlist indices of the tracks that match */
    int match_count;
    int selected;           /* index into matches */
    int scroll;             /* first visible match */
    int pressed;
    uint32_t ticks;         /* blinks the cursor */
} JumpModel;

void jump_draw(uint32_t *framebuffer, int scale, const JumpModel *model);
int  jump_hit(int x, int y);
/* Row (0 = first visible) under a y coordinate inside UI_JUMP_LIST. */
int  jump_row_at(int y);

/* --- About window --------------------------------------------------------- */

#define ABOUT_W   250
#define ABOUT_H   138
#define ABOUT_URL "https://amplitude.cr.rs"

void about_draw(uint32_t *framebuffer, int scale, int pressed);
int  about_hit(int x, int y);

/* --- Popup menu ----------------------------------------------------------- */

typedef struct {
    const char *label;      /* UTF-8; NULL draws a separator */
    int id;
    int checked;
    uint32_t swatch;        /* 0, or a colour sample to show: 0xRRGGBB + 1 */
} MenuItem;

/* Size in unscaled pixels; text widths depend on the magnification. */
void menu_measure(const MenuItem *items, int count, int scale, int *w, int *h);
void menu_draw(uint32_t *framebuffer, int w, int h, int scale, const MenuItem *items, int count, int hover);
/* Index of the item under a point, or -1. */
int  menu_item_at(const MenuItem *items, int count, int w, int x, int y);

#endif
