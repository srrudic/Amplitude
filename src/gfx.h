/* Software drawing into a 0x00RRGGBB framebuffer.
 *
 * Callers work in "logical" pixels: the fixed coordinates of the classic
 * 275x116 layout. A canvas carries a magnification (in percent) and maps
 * those coordinates onto the real pixels of its buffer, so shapes and text
 * are rendered at the final resolution rather than drawn small and then
 * enlarged. */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#define GFX_FONT_W 5
#define GFX_FONT_H 7
#define GFX_FONT_ADVANCE 6

/* Logical length -> real pixels at a magnification in percent. */
#define GFX_SCALED(v, percent) (((v) * (percent) + 50) / 100)

typedef struct {
    uint32_t *px;
    int w, h;                   /* buffer size in real pixels */
    int scale;                  /* percent */
    int ox, oy;                 /* logical offset added to every coordinate */
    int shift_x;                /* real pixels added to where gfx_utext() and gfx_blit() draw,
                                 * for moving text by less than a logical pixel */
    int clip_x0, clip_y0, clip_x1, clip_y1;     /* real pixels, x1/y1 exclusive */
    int bound_x0, bound_y0, bound_x1, bound_y1; /* what no clip can reach beyond; see gfx_set_bound() */
} Canvas;

typedef struct {
    uint32_t *px;
    int w, h;
} Bitmap;

/* Fixed-cell Unicode bitmap font (see font_data.c). Glyphs are sorted by
 * code point; each is `h` rows of (w + 7) / 8 bytes, leftmost pixel in the
 * most significant bit. */
typedef struct {
    int w, h;
    int ascent;                 /* rows above the baseline */
    int cap_height;             /* height of a capital letter */
    int count;
    const unsigned short *codes;
    const unsigned char *rows;
} GfxFont;

extern const GfxFont font_5x7, font_6x10, font_7x13, font_9x15, font_10x20;

/* The two text sizes of the layout, named by their size at 100%. At other
 * magnifications gfx_utext() substitutes whichever font fits best. */
#define font_small font_5x7     /* main window title */
#define font_list  font_6x10    /* playlist, menus */

/* `px` must hold GFX_SCALED(w) x GFX_SCALED(h) pixels. */
void gfx_init(Canvas *c, uint32_t *px, int w, int h, int scale);
void gfx_set_clip(Canvas *c, int x, int y, int w, int h);
/* Narrows the current clip rectangle instead of replacing it. */
void gfx_intersect_clip(Canvas *c, int x, int y, int w, int h);
void gfx_reset_clip(Canvas *c);
/* Confines all drawing to a rectangle from now on, whatever clips are set
 * and reset later: for painting one part of a picture again by running
 * the code that paints all of it. Also resets the clip. */
void gfx_set_bound(Canvas *c, int x, int y, int w, int h);

void gfx_pixel(Canvas *c, int x, int y, uint32_t color);
void gfx_rect(Canvas *c, int x, int y, int w, int h, uint32_t color);
/* A pixel of the layout moved down by `shift_y` real pixels, for placing
 * one more finely than the layout's grid allows. */
void gfx_pixel_shifted(Canvas *c, int x, int y, int shift_y, uint32_t color);
/* One-pixel lines. Their real thickness is the same everywhere on the
 * canvas, which a scaled gfx_rect() of height or width 1 cannot promise. */
void gfx_hline(Canvas *c, int x, int y, int w, uint32_t color);
void gfx_vline(Canvas *c, int x, int y, int h, uint32_t color);
/* Rectangle outline with separate top/left and bottom/right colours. */
void gfx_bevel(Canvas *c, int x, int y, int w, int h, uint32_t top_left, uint32_t bottom_right);
/* Filled triangle; the corners may fall between pixels. */
void gfx_triangle(Canvas *c, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color);
/* Smooth-edged line through `count` points (x0, y0, x1, y1, ...), `width`
 * logical pixels thick with round ends and corners. */
void gfx_polyline(Canvas *c, const float *points, int count, float width, uint32_t color);
/* An "x" inside the size*size square at (x, y). */
void gfx_cross(Canvas *c, int x, int y, int size, uint32_t color);

/* For UI labels: chunky uppercase ASCII (lowercase letters are folded) in
 * 6-pixel-wide cells. */
/* The real pixels a logical rectangle covers (x1/y1 exclusive), unclipped. */
void gfx_real_rect(const Canvas *c, int x, int y, int w, int h, int *x0, int *y0, int *x1, int *y1);

void gfx_text(Canvas *c, int x, int y, const char *text, uint32_t color);
int  gfx_text_width(const char *text);
/* For titles: draws UTF-8 text. Characters the font lacks fall back to an
 * unaccented letter where one exists, otherwise to a placeholder. */
void gfx_utext(Canvas *c, const GfxFont *font, int x, int y, const char *text, uint32_t color);
/* The plain letter behind an accented Latin one ("e" for an e-acute, "d"
 * for a d-stroke) and plain forms of typographic quotes and dashes; other
 * characters are returned unchanged. */
unsigned long gfx_unaccent(unsigned long cp);
/* Logical width that text takes at a given magnification, and the exact
 * number of real pixels. */
int  gfx_utext_width(int scale, const GfxFont *font, const char *text);
int  gfx_utext_real_width(int scale, const GfxFont *font, const char *text);

/* Copies a w*h region of src at (sx, sy) to (dx, dy), magnified to the
 * canvas scale: pixels are replicated at whole factors and averaged by area
 * at fractional ones. */
void gfx_blit(Canvas *c, const Bitmap *src, int sx, int sy, int w, int h, int dx, int dy);

#endif
