#include "gfx.h"

#include <stdlib.h>
#include <string.h>

/* 5x7 font covering ASCII 32..95. One byte per row, bit 4 = leftmost pixel. */
static const unsigned char font[64][GFX_FONT_H] = {
    ['!' - 32] = { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 },
    ['"' - 32] = { 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00 },
    ['#' - 32] = { 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A },
    ['%' - 32] = { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 },
    ['&' - 32] = { 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D },
    ['\'' - 32] = { 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 },
    ['(' - 32] = { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 },
    [')' - 32] = { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 },
    ['*' - 32] = { 0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00 },
    ['+' - 32] = { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 },
    [',' - 32] = { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08 },
    ['-' - 32] = { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 },
    ['.' - 32] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C },
    ['/' - 32] = { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00 },
    ['0' - 32] = { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },
    ['1' - 32] = { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
    ['2' - 32] = { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },
    ['3' - 32] = { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },
    ['4' - 32] = { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },
    ['5' - 32] = { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
    ['6' - 32] = { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E },
    ['7' - 32] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    ['8' - 32] = { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },
    ['9' - 32] = { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },
    [':' - 32] = { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 },
    ['=' - 32] = { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 },
    ['?' - 32] = { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 },
    ['A' - 32] = { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
    ['B' - 32] = { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },
    ['C' - 32] = { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },
    ['D' - 32] = { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },
    ['E' - 32] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F },
    ['F' - 32] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },
    ['G' - 32] = { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F },
    ['H' - 32] = { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
    ['I' - 32] = { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E },
    ['J' - 32] = { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C },
    ['K' - 32] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
    ['L' - 32] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },
    ['M' - 32] = { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 },
    ['N' - 32] = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
    ['O' - 32] = { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
    ['P' - 32] = { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 },
    ['Q' - 32] = { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D },
    ['R' - 32] = { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },
    ['S' - 32] = { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E },
    ['T' - 32] = { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
    ['U' - 32] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
    ['V' - 32] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 },
    ['W' - 32] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 },
    ['X' - 32] = { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 },
    ['Y' - 32] = { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 },
    ['Z' - 32] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },
    ['[' - 32] = { 0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E },
    [']' - 32] = { 0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E },
    ['_' - 32] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F },
};

/* --- Coordinates ------------------------------------------------------------ */

/* Logical coordinate -> real pixel, rounding to the nearest edge. Because
 * both ends of every shape go through this one mapping, shapes that touch
 * in logical space also touch on screen, with no gaps or overlaps. */
static int real(const Canvas *c, int v)
{
    int n = v * c->scale + 50;

    return n >= 0 ? n / 100 : -((99 - n) / 100);
}

/* Thickness of a one-pixel line. */
static int line_width(const Canvas *c)
{
    int t = (c->scale + 50) / 100;

    return t < 1 ? 1 : t;
}

void gfx_init(Canvas *c, uint32_t *px, int w, int h, int scale)
{
    c->px = px;
    c->scale = scale;
    c->ox = c->oy = c->shift_x = 0;
    c->w = GFX_SCALED(w, scale);
    c->h = GFX_SCALED(h, scale);
    gfx_reset_clip(c);
}

void gfx_reset_clip(Canvas *c)
{
    c->clip_x0 = c->clip_y0 = 0;
    c->clip_x1 = c->w;
    c->clip_y1 = c->h;
}

void gfx_intersect_clip(Canvas *c, int x, int y, int w, int h)
{
    int x0 = real(c, c->ox + x), y0 = real(c, c->oy + y);
    int x1 = real(c, c->ox + x + w), y1 = real(c, c->oy + y + h);

    if (x0 > c->clip_x0)
        c->clip_x0 = x0;
    if (y0 > c->clip_y0)
        c->clip_y0 = y0;
    if (x1 < c->clip_x1)
        c->clip_x1 = x1;
    if (y1 < c->clip_y1)
        c->clip_y1 = y1;
}

void gfx_set_clip(Canvas *c, int x, int y, int w, int h)
{
    gfx_reset_clip(c);
    gfx_intersect_clip(c, x, y, w, h);
}

void gfx_real_rect(const Canvas *c, int x, int y, int w, int h, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = real(c, c->ox + x);
    *y0 = real(c, c->oy + y);
    *x1 = real(c, c->ox + x + w);
    *y1 = real(c, c->oy + y + h);
}

/* --- Shapes ----------------------------------------------------------------- */

static uint32_t mix(uint32_t a, uint32_t b, unsigned w);

/* Fills a rectangle given in real pixels (x1/y1 exclusive). */
static void fill(Canvas *c, int x0, int y0, int x1, int y1, uint32_t color)
{
    int x, y;

    if (x0 < c->clip_x0)
        x0 = c->clip_x0;
    if (y0 < c->clip_y0)
        y0 = c->clip_y0;
    if (x1 > c->clip_x1)
        x1 = c->clip_x1;
    if (y1 > c->clip_y1)
        y1 = c->clip_y1;
    if (x0 >= x1)
        return;
    for (y = y0; y < y1; y++) {
        uint32_t *row = &c->px[y * c->w + x0];

        for (x = 0; x < x1 - x0; x++)
            row[x] = color;
    }
}

void gfx_rect(Canvas *c, int x, int y, int w, int h, uint32_t color)
{
    fill(c, real(c, c->ox + x), real(c, c->oy + y), real(c, c->ox + x + w), real(c, c->oy + y + h), color);
}

void gfx_pixel(Canvas *c, int x, int y, uint32_t color)
{
    gfx_rect(c, x, y, 1, 1, color);
}

void gfx_hline(Canvas *c, int x, int y, int w, uint32_t color)
{
    int y0 = real(c, c->oy + y);

    fill(c, real(c, c->ox + x), y0, real(c, c->ox + x + w), y0 + line_width(c), color);
}

void gfx_vline(Canvas *c, int x, int y, int h, uint32_t color)
{
    int x0 = real(c, c->ox + x);

    fill(c, x0, real(c, c->oy + y), x0 + line_width(c), real(c, c->oy + y + h), color);
}

void gfx_bevel(Canvas *c, int x, int y, int w, int h, uint32_t top_left, uint32_t bottom_right)
{
    int x0 = real(c, c->ox + x), y0 = real(c, c->oy + y);
    int x1 = real(c, c->ox + x + w), y1 = real(c, c->oy + y + h), t = line_width(c);

    fill(c, x0, y0, x1, y0 + t, top_left);
    fill(c, x0, y0, x0 + t, y1, top_left);
    fill(c, x0, y1 - t, x1, y1, bottom_right);
    fill(c, x1 - t, y0, x1, y1, bottom_right);
}

void gfx_triangle(Canvas *c, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color)
{
    float s = (float)c->scale / 100.0f;
    float px[3], py[3], top, bottom;
    int i, y;

    px[0] = ((float)c->ox + x0) * s; py[0] = ((float)c->oy + y0) * s;
    px[1] = ((float)c->ox + x1) * s; py[1] = ((float)c->oy + y1) * s;
    px[2] = ((float)c->ox + x2) * s; py[2] = ((float)c->oy + y2) * s;
    top = bottom = py[0];
    for (i = 1; i < 3; i++) {
        if (py[i] < top)
            top = py[i];
        if (py[i] > bottom)
            bottom = py[i];
    }
    /* One scanline per pixel row, sampled at the pixel centres. */
    for (y = (int)top; y <= (int)bottom; y++) {
        float cy = (float)y + 0.5f, left = 1e9f, right = -1e9f;

        for (i = 0; i < 3; i++) {
            int j = (i + 1) % 3;

            if ((py[i] <= cy) != (py[j] <= cy)) {
                float x = px[i] + (cy - py[i]) * (px[j] - px[i]) / (py[j] - py[i]);

                if (x < left)
                    left = x;
                if (x > right)
                    right = x;
            }
        }
        if (left <= right)
            fill(c, (int)(left + 0.5f), y, (int)(right + 0.5f), y + 1, color);
    }
}

/* Squared distance from a point to the nearest of the line's segments. */
static float polyline_distance2(const float *p, int count, float x, float y)
{
    float best = 1e18f;
    int i;

    for (i = 0; i + 1 < count; i++, p += 2) {
        float dx = p[2] - p[0], dy = p[3] - p[1], px = x - p[0], py = y - p[1];
        float len2 = dx * dx + dy * dy, t = len2 > 0 ? (px * dx + py * dy) / len2 : 0, d2;

        t = t < 0 ? 0 : t > 1 ? 1 : t;
        px -= t * dx;
        py -= t * dy;
        d2 = px * px + py * py;
        if (d2 < best)
            best = d2;
    }
    return best;
}

void gfx_polyline(Canvas *c, const float *points, int count, float width, uint32_t color)
{
    enum { MAX_POINTS = 64, SUB = 4 };
    float real_points[MAX_POINTS * 2], s = (float)c->scale / 100.0f, r = width * s / 2;
    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
    int i, x, y, x0, y0, x1, y1;

    if (count < 2 || count > MAX_POINTS)
        return;
    for (i = 0; i < count; i++) {
        float px = ((float)c->ox + points[i * 2]) * s, py = ((float)c->oy + points[i * 2 + 1]) * s;

        real_points[i * 2] = px;
        real_points[i * 2 + 1] = py;
        min_x = px < min_x ? px : min_x;
        max_x = px > max_x ? px : max_x;
        min_y = py < min_y ? py : min_y;
        max_y = py > max_y ? py : max_y;
    }
    x0 = (int)(min_x - r) - 1 < c->clip_x0 ? c->clip_x0 : (int)(min_x - r) - 1;
    y0 = (int)(min_y - r) - 1 < c->clip_y0 ? c->clip_y0 : (int)(min_y - r) - 1;
    x1 = (int)(max_x + r) + 2 > c->clip_x1 ? c->clip_x1 : (int)(max_x + r) + 2;
    y1 = (int)(max_y + r) + 2 > c->clip_y1 ? c->clip_y1 : (int)(max_y + r) + 2;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            float d2 = polyline_distance2(real_points, count, (float)x + 0.5f, (float)y + 0.5f);
            uint32_t *px = &c->px[y * c->w + x];
            int covered = 0, sx, sy;

            if (d2 >= (r + 1) * (r + 1))
                continue;                   /* clearly outside */
            if (r > 1 && d2 <= (r - 1) * (r - 1)) {
                *px = color;                /* clearly inside */
                continue;
            }
            /* On the edge: the share of the pixel inside the line sets the blend. */
            for (sy = 0; sy < SUB; sy++)
                for (sx = 0; sx < SUB; sx++)
                    if (polyline_distance2(real_points, count, (float)x + ((float)sx + 0.5f) / SUB,
                                           (float)y + ((float)sy + 0.5f) / SUB) <= r * r)
                        covered++;
            if (covered)
                *px = mix(*px, color, (unsigned)covered * 256 / (SUB * SUB));
        }
    }
}

void gfx_cross(Canvas *c, int x, int y, int size, uint32_t color)
{
    int x0 = real(c, c->ox + x), y0 = real(c, c->oy + y);
    int n = real(c, c->ox + x + size) - x0, t = line_width(c), i;

    for (i = 0; i + t <= n; i++) {
        fill(c, x0 + i, y0 + i, x0 + i + t, y0 + i + t, color);
        fill(c, x0 + n - t - i, y0 + i, x0 + n - i, y0 + i + t, color);
    }
}

/* --- Text ------------------------------------------------------------------- */

/* Decodes one UTF-8 sequence and advances *text. Malformed bytes are passed
 * through one at a time so bad input can never stall or overrun. */
static unsigned long utf8_next(const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    int extra = p[0] >= 0xF0 ? 3 : p[0] >= 0xE0 ? 2 : p[0] >= 0xC0 ? 1 : 0, i;
    unsigned long cp = extra == 3 ? p[0] & 0x07 : extra == 2 ? p[0] & 0x0F : p[0] & 0x1F;

    if (p[0] < 0x80 || !extra) {
        *text += 1;
        return p[0];
    }
    for (i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *text += 1;
            return p[0];
        }
        cp = cp << 6 | (p[i] & 0x3F);
    }
    *text += extra + 1;
    return cp;
}

static const unsigned char *find_glyph(const GfxFont *font, unsigned long cp)
{
    int lo = 0, hi = font->count - 1;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;

        if (font->codes[mid] == cp)
            return font->rows + mid * font->h * ((font->w + 7) / 8);
        if (font->codes[mid] < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

unsigned long gfx_unaccent(unsigned long cp)
{
    /* U+00C0..U+00FF and U+0100..U+017F with their accents removed. */
    static const char latin1[] =
        "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    static const char extended_a[] =
        "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlL"
        "lLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";

    if (cp >= 0xC0 && cp <= 0xFF)
        return (unsigned char)latin1[cp - 0xC0];
    if (cp >= 0x100 && cp <= 0x17F)
        return (unsigned char)extended_a[cp - 0x100];
    switch (cp) {
    case 0xA0:                  return ' ';
    case 0x2018: case 0x2019:   return '\'';
    case 0x201C: case 0x201D:   return '"';
    case 0x2010: case 0x2013: case 0x2014: return '-';
    case 0x2026:                return '.';
    }
    return cp;
}

/* Draws one glyph with its cell's top left corner at a real pixel position,
 * each font pixel becoming a k*k block. */
static void draw_glyph(Canvas *c, const GfxFont *font, int k, int x, int y, unsigned long cp, uint32_t color)
{
    const unsigned char *glyph = find_glyph(font, cp);
    int row_bytes = (font->w + 7) / 8, row, col;

    if (x >= c->clip_x1 || x + font->w * k <= c->clip_x0 || y >= c->clip_y1 || y + font->h * k <= c->clip_y0)
        return;
    if (!glyph)
        glyph = find_glyph(font, gfx_unaccent(cp));
    if (!glyph)
        glyph = find_glyph(font, 0xFFFD);   /* replacement character */
    if (!glyph)
        glyph = find_glyph(font, '?');
    for (row = 0; glyph && row < font->h; row++)
        for (col = 0; col < font->w; col++)
            if (glyph[row * row_bytes + col / 8] & (0x80 >> col % 8))
                fill(c, x + col * k, y + row * k, x + (col + 1) * k, y + (row + 1) * k, color);
}

static const GfxFont *const fonts[] = { &font_5x7, &font_6x10, &font_7x13, &font_9x15, &font_10x20 };

/* Picks the font for text that should be `target` real pixels high (cell
 * height, or capital height with `by_caps`), optionally limited to `max_w`
 * pixels per character. Text is always made of whole pixels:
 *
 * - A font used at its own size is preferred, since that is as sharp as a
 *   bitmap font gets, as long as it reaches most of the height wanted.
 * - Otherwise a smaller font is magnified by a whole factor (`*multiple`),
 *   which fills the height but looks blocky. */
static const GfxFont *pick_font(int target, int by_caps, int max_w, int *multiple)
{
    const GfxFont *best = fonts[0], *best_plain = NULL;
    int best_size = 0, best_k = 1, plain_size = 0, k;
    size_t i;

    for (i = 0; i < sizeof fonts / sizeof fonts[0]; i++) {
        int unit = by_caps ? fonts[i]->cap_height : fonts[i]->h;

        if (unit <= target && unit > plain_size && !(max_w && fonts[i]->w > max_w)) {
            best_plain = fonts[i];
            plain_size = unit;
        }
        k = target / unit;
        while (k > 1 && max_w && fonts[i]->w * k > max_w)
            k--;
        if (k < 1 || (max_w && fonts[i]->w * k > max_w))
            continue;
        /* Prefer the larger size; between equals, the font needing less magnifying. */
        if (unit * k > best_size || (unit * k == best_size && k < best_k)) {
            best = fonts[i];
            best_size = unit * k;
            best_k = k;
        }
    }
    if (best_plain && plain_size * 100 >= best_size * 65) {
        best = best_plain;
        best_k = 1;
    }
    *multiple = best_k;
    return best;
}

void gfx_utext(Canvas *c, const GfxFont *font, int x, int y, const char *text, uint32_t color)
{
    int target = GFX_SCALED(font->h, c->scale), k;
    const GfxFont *actual = pick_font(target, 0, 0, &k);
    int rx = real(c, c->ox + x) + c->shift_x;
    int ry = real(c, c->oy + y) + (target - actual->h * k) / 2;    /* centred in the logical row */

    while (*text) {
        draw_glyph(c, actual, k, rx, ry, utf8_next(&text), color);
        rx += actual->w * k;
    }
}

int gfx_utext_real_width(int scale, const GfxFont *font, const char *text)
{
    int k, count = 0;
    const GfxFont *actual = pick_font(GFX_SCALED(font->h, scale), 0, 0, &k);

    while (*text) {
        utf8_next(&text);
        count++;
    }
    return count * actual->w * k;
}

int gfx_utext_width(int scale, const GfxFont *font, const char *text)
{
    return (gfx_utext_real_width(scale, font, text) * 100 + scale - 1) / scale;
}

void gfx_text(Canvas *c, int x, int y, const char *text, uint32_t color)
{
    int k = 0, i;
    const GfxFont *substitute = NULL;

    /* The chunky label font is used at 100% only. Magnified, its pixels
     * would show as blocks, so a regular font with capitals of about the
     * same height stands in, set in the same 6-pixel-wide cells so layouts
     * still fit. */
    if (c->scale != 100)
        substitute = pick_font(GFX_SCALED(GFX_FONT_H, c->scale), 1, GFX_SCALED(GFX_FONT_ADVANCE, c->scale), &k);

    for (i = 0; text[i]; i++) {
        int ch = (unsigned char)text[i], lx = x + i * GFX_FONT_ADVANCE, row, col;

        if (ch >= 'a' && ch <= 'z')
            ch -= 'a' - 'A';
        if (substitute) {
            int cell_x0 = real(c, c->ox + lx), cell_x1 = real(c, c->ox + lx + GFX_FONT_ADVANCE);
            int baseline = real(c, c->oy + y + GFX_FONT_H);

            draw_glyph(c, substitute, k, cell_x0 + (cell_x1 - cell_x0 - substitute->w * k) / 2,
                       baseline - substitute->ascent * k, (unsigned long)ch, color);
            continue;
        }
        if (ch < 32 || ch > 95)
            ch = '_';
        for (row = 0; row < GFX_FONT_H; row++)
            for (col = 0; col < GFX_FONT_W; col++)
                if (font[ch - 32][row] & (0x10 >> col))
                    gfx_rect(c, lx + col, y + row, 1, 1, color);
    }
}

int gfx_text_width(const char *text)
{
    return (int)strlen(text) * GFX_FONT_ADVANCE;
}

/* --- Bitmaps ---------------------------------------------------------------- */

/* Blends two pixels; w is the weight of b out of 256. */
static uint32_t mix(uint32_t a, uint32_t b, unsigned w)
{
    uint32_t rb = ((a & 0xFF00FF) * (256 - w) + (b & 0xFF00FF) * w) >> 8 & 0xFF00FF;
    uint32_t g = ((a & 0x00FF00) * (256 - w) + (b & 0x00FF00) * w) >> 8 & 0x00FF00;

    return rb | g;
}

/* For output pixel i of `dst` covering a `src`-pixel-long strip: the source
 * pixel it starts in, and how much of it (out of 256) lies in the next one. */
static void sample(int i, int src, int dst, int *first, unsigned *weight)
{
    long start = (long)i * src, end = (long)(i + 1) * src;     /* in units of 1/dst */
    long boundary;

    *first = (int)(start / dst);
    boundary = (long)(*first + 1) * dst;
    *weight = end <= boundary || *first + 1 >= src ? 0 : (unsigned)((end - boundary) * 256 / src);
}

void gfx_blit(Canvas *c, const Bitmap *src, int sx, int sy, int w, int h, int dx, int dy)
{
    int x0, y0, x1, y1, rw, rh, x, y;

    if (!src->px || sx < 0 || sy < 0)
        return;
    if (sx + w > src->w)
        w = src->w - sx;
    if (sy + h > src->h)
        h = src->h - sy;
    if (w <= 0 || h <= 0)
        return;

    /* The sprite's place on screen, then the part of it inside the clip. */
    x0 = real(c, c->ox + dx);
    y0 = real(c, c->oy + dy);
    rw = real(c, c->ox + dx + w) - x0;
    rh = real(c, c->oy + dy + h) - y0;
    x0 += c->shift_x;
    x1 = x0 + rw < c->clip_x1 ? x0 + rw : c->clip_x1;
    y1 = y0 + rh < c->clip_y1 ? y0 + rh : c->clip_y1;

    if (rw == w && rh == h) {       /* no magnification: straight copy */
        int left = x0 < c->clip_x0 ? c->clip_x0 : x0;

        for (y = y0 < c->clip_y0 ? c->clip_y0 : y0; y < y1 && left < x1; y++)
            memcpy(c->px + y * c->w + left, src->px + (sy + y - y0) * src->w + sx + left - x0,
                   sizeof(uint32_t) * (x1 - left));
        return;
    }
    for (y = y0 < c->clip_y0 ? c->clip_y0 : y0; y < y1; y++) {
        const uint32_t *row, *below;
        uint32_t *out = c->px + y * c->w;
        unsigned wx, wy;
        int row_index, col;

        sample(y - y0, h, rh, &row_index, &wy);
        row = src->px + (sy + row_index) * src->w + sx;
        below = wy ? row + src->w : row;
        for (x = x0 < c->clip_x0 ? c->clip_x0 : x0; x < x1; x++) {
            uint32_t p;

            sample(x - x0, w, rw, &col, &wx);
            p = wx ? mix(row[col], row[col + 1], wx) : row[col];
            if (wy)
                p = mix(p, wx ? mix(below[col], below[col + 1], wx) : below[col], wy);
            out[x] = p;
        }
    }
}
