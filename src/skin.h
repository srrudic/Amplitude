/*
 * A skin is a set of sprite sheets laid out like the classic 2.x skin
 * format (.wsz archives of BMP files). The built-in look uses the same
 * layout, but its "sheets" are drawing code rather than bitmaps: each
 * sprite is painted straight onto the window at its final resolution, so it
 * stays sharp at any magnification.
 */
#ifndef SKIN_H
#define SKIN_H

#include "gfx.h"

enum {
    SKIN_MAIN,
    SKIN_TITLEBAR,
    SKIN_CBUTTONS,
    SKIN_NUMBERS,
    SKIN_PLAYPAUS,
    SKIN_TEXT,
    SKIN_MONOSTER,
    SKIN_POSBAR,
    SKIN_VOLUME,
    SKIN_BALANCE,
    SKIN_SHUFREP,
    SKIN_EQMAIN,
    SKIN_PLEDIT,
    SKIN_SHEET_COUNT
};

#define SKIN_VIS_COLORS 24
#define SKIN_EQ_GRAPH_H 19
#define SKIN_TEXT_COLS  31
#define SKIN_TEXT_ROWS  3

typedef struct {
    /* For a built-in sheet only the size is set and px is NULL. */
    Bitmap sheet[SKIN_SHEET_COUNT];
    int builtin[SKIN_SHEET_COUNT];
    int text_w, text_h;                 /* character cell in the text sheet */
    uint32_t text_color;                /* for text drawn with our own fonts */
    uint32_t text_background;
    uint32_t slider_fill;               /* level bar inside the built-in sliders */
    uint32_t eq_line[SKIN_EQ_GRAPH_H];  /* equaliser curve colour for each graph row */
    uint32_t vis[SKIN_VIS_COLORS];      /* visualiser palette (viscolor.txt) */
    /* Playlist colours (pledit.txt) */
    uint32_t pl_normal, pl_current, pl_background, pl_selected;
} Skin;

/* Fills *skin with the built-in look. */
void skin_init_default(Skin *skin);
/* Replaces every part of an initialised skin that the archive provides.
 * Returns the number of sheets loaded; 0 means the file was unusable. */
int  skin_load(Skin *skin, const char *path);
void skin_free(Skin *skin);

/* Draws the w*h sprite at (sx, sy) of a sheet to (dx, dy) on the canvas. */
void skin_blit(Canvas *c, const Skin *skin, int sheet, int sx, int sy, int w, int h, int dx, int dy);
/* Paints a whole built-in sheet with its top left corner at the canvas origin. */
void skin_paint_builtin(Canvas *c, int sheet);
/* Forgets the remembered sprites of the built-in skin (see skin.c). Happens
 * by itself when the size or colour changes and in skin_free(). */
void skin_cache_clear(void);
/* Set to draw every sprite afresh instead. The tests use it to check that
 * the remembered sprites match what would have been drawn. */
extern int skin_cache_disabled;
/* The logo in the theme colour, `height` tall, with its top left at x, y. */
void skin_draw_logo(Canvas *c, float x, float y, float height);
/* Locates a character in the text sheet. Returns 0 if it has no glyph,
 * in which case the blank cell is returned. */
int  skin_text_cell(int ch, int *col, int *row);

#endif
