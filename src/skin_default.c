/* The built-in skin. Each "sheet" of the classic layout is a function that
 * draws it; skin_blit() calls these with the canvas shifted and clipped to
 * the sprite wanted, so everything is rendered at the window's real
 * resolution and no artwork has to be shipped or embedded. */
#include "skin.h"

#include <math.h>

#include "theme.h"
#include "util.h"

#include <math.h>
#include <string.h>

/* Everything below follows the colour chosen by the user (theme.h). */
#define COL_LCD_BG    (theme_get()->display)
#define COL_LCD_FG    (theme_get()->base)       /* indicators, curves, visualiser */
#define COL_LCD_DIM   (theme_get()->dim)
#define COL_LCD_GRID  (theme_get()->grid)
#define COL_LCD_PEAK  (theme_get()->peak)
#define COL_ACCENT    (theme_get()->base)       /* window titles, slider marks, the logo */
#define COL_TEXT      (theme_get()->base)       /* all lettering and digits */
#define COL_TEXT_ON   (theme_get()->text_on)

enum { ICON_PREV, ICON_PLAY, ICON_PAUSE, ICON_STOP, ICON_NEXT, ICON_EJECT };

/* Size of each sheet, matching the classic bitmaps. */
static const struct { int w, h; } sheet_sizes[SKIN_SHEET_COUNT] = {
    [SKIN_MAIN]     = { 275, 116 },
    [SKIN_TITLEBAR] = { 302, 29 },
    [SKIN_CBUTTONS] = { 136, 36 },
    [SKIN_NUMBERS]  = { 99, 13 },
    [SKIN_PLAYPAUS] = { 42, 9 },
    [SKIN_TEXT]     = { SKIN_TEXT_COLS * GFX_FONT_ADVANCE, SKIN_TEXT_ROWS * GFX_FONT_H },
    [SKIN_MONOSTER] = { 56, 24 },
    [SKIN_POSBAR]   = { 307, 10 },
    [SKIN_VOLUME]   = { 68, 433 },
    [SKIN_BALANCE]  = { 47, 433 },
    [SKIN_SHUFREP]  = { 158, 85 },      /* classic 92 wide, plus the small icon buttons */
    [SKIN_EQMAIN]   = { 275, 315 },
    [SKIN_PLEDIT]   = { 280, 186 },
};

static void button(Canvas *c, int x, int y, int w, int h, int pressed)
{
    gfx_rect(c, x, y, w, h, COL_BODY);
    if (pressed)
        gfx_bevel(c, x, y, w, h, COL_DARK, COL_LIGHT);
    else
        gfx_bevel(c, x, y, w, h, COL_LIGHT, COL_DARK);
}

/* Sunken display area with a bevel drawn inside the given rectangle. */
static void groove(Canvas *c, int x, int y, int w, int h)
{
    gfx_rect(c, x, y, w, h, COL_LCD_BG);
    gfx_bevel(c, x, y, w, h, COL_DARK, COL_LIGHT);
}

/* Triangle pointing right (dir = 1) or left (dir = -1), 5 wide, 9 tall. */
static void triangle_h(Canvas *c, int x, int y, int dir, uint32_t color)
{
    float base = (float)(dir > 0 ? x : x + 5), tip = (float)(dir > 0 ? x + 5 : x);

    gfx_triangle(c, base, (float)y, base, (float)y + 9, tip, (float)y + 4.5f, color);
}

/* All icons fit a 9x9 box. */
static void icon(Canvas *c, int which, int x, int y, uint32_t color)
{
    switch (which) {
    case ICON_PREV:
        gfx_rect(c, x, y, 2, 9, color);
        triangle_h(c, x + 3, y, -1, color);
        break;
    case ICON_PLAY:
        triangle_h(c, x + 2, y, 1, color);
        break;
    case ICON_PAUSE:
        gfx_rect(c, x + 1, y, 3, 9, color);
        gfx_rect(c, x + 5, y, 3, 9, color);
        break;
    case ICON_STOP:
        gfx_rect(c, x + 1, y + 1, 7, 7, color);
        break;
    case ICON_NEXT:
        triangle_h(c, x + 1, y, 1, color);
        gfx_rect(c, x + 7, y, 2, 9, color);
        break;
    case ICON_EJECT:
        gfx_triangle(c, (float)x, (float)y + 5, (float)x + 9, (float)y + 5, (float)x + 4.5f, (float)y, color);
        gfx_rect(c, x, y + 7, 9, 2, color);
        break;
    }
}

static void close_button(Canvas *c, int x, int y, int pressed)
{
    button(c, x, y, 9, 9, pressed);
    gfx_cross(c, x + 2, y + 2, 5, COL_ICON);
}

static void minimize_button(Canvas *c, int x, int y, int pressed)
{
    button(c, x, y, 9, 9, pressed);
    gfx_hline(c, x + 2, y + 6, 5, COL_ICON);
}

/* Three grip lines across a title bar. */
static void grip(Canvas *c, int x, int y, int w)
{
    int i;

    for (i = 0; i < 3; i++)
        gfx_hline(c, x, y + i * 2, w, COL_LIGHT);
}

/* Point `t` (0..1) along a cubic curve given by four control points. */
static void cubic(const float p[4][2], float t, float *x, float *y)
{
    float u = 1 - t, a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;

    *x = a * p[0][0] + b * p[1][0] + c * p[2][0] + d * p[3][0];
    *y = a * p[0][1] + b * p[1][1] + c * p[2][1] + d * p[3][1];
}

/* The logo: a waveform that dips, swells into one tall peak (the two legs of
 * a letter A) and dips again, with a small sine wave as the crossbar. The
 * shape matches tools/genlogo.py, which draws the application icon; `height`
 * is the distance from the baseline to the peak in logical pixels. */
static void logo(Canvas *c, float x, float y, float height)
{
    enum { TAIL_STEPS = 5, LEG_STEPS = 10, BAR_STEPS = 12, HALF = TAIL_STEPS + LEG_STEPS + 1 };
    /* The left half in the icon's 256-unit space, as two cubic curves: the
     * tail sinking into a trough at the foot, then the leg rising from it
     * to the peak. These are the numbers of tools/genlogo.py. */
    static const float tail[4][2] = { { 30, 181 }, { 42, 181 }, { 44, 190 }, { 58, 190 } };
    static const float leg[4][2] = { { 58, 190 }, { 80, 190 }, { 106, 52 }, { 128, 52 } };
    float wave[(2 * HALF - 1) * 2], bar[(BAR_STEPS + 1) * 2];
    float f = height / 132.0f, bar_left = 128, bar_y = 138, px, py;
    int i, n = 0;

#define LOGO_X(v) (x + ((v) - 30) * f)
#define LOGO_Y(v) (y + ((v) - 52) * f)
    for (i = 0; i < TAIL_STEPS; i++) {
        cubic(tail, (float)i / TAIL_STEPS, &px, &py);
        wave[n++] = LOGO_X(px);
        wave[n++] = LOGO_Y(py);
    }
    for (i = 0; i <= LEG_STEPS; i++) {
        cubic(leg, (float)i / LEG_STEPS, &px, &py);
        wave[n++] = LOGO_X(px);
        wave[n++] = LOGO_Y(py);
        if (py >= bar_y)
            bar_left = px;      /* where the leg crosses the crossbar's height */
    }
    for (i = HALF - 2; i >= 0; i--) {       /* mirror image of everything but the peak */
        wave[n] = LOGO_X(256) - (wave[i * 2] - LOGO_X(0));
        wave[n + 1] = wave[i * 2 + 1];
        n += 2;
    }
    for (i = 0; i <= BAR_STEPS; i++) {
        float t = (float)i / BAR_STEPS;

        bar[i * 2] = LOGO_X(bar_left + (256 - 2 * bar_left) * t);
        bar[i * 2 + 1] = LOGO_Y(bar_y - 9 * sinf(2 * PI_F * t));
    }
#undef LOGO_X
#undef LOGO_Y
    gfx_polyline(c, bar, BAR_STEPS + 1, 12 * f, COL_LCD_FG);
    gfx_polyline(c, wave, n / 2, 20 * f, COL_ACCENT);
}

void skin_draw_logo(Canvas *c, float x, float y, float height)
{
    logo(c, x, y, height);
}

static void paint_main(Canvas *c)
{
    gfx_rect(c, 0, 0, 275, 116, COL_BODY);
    gfx_bevel(c, 0, 0, 275, 116, COL_LIGHT, COL_DARK);
    /* Time and visualiser display */
    groove(c, 9, 22, 95, 40);
    gfx_rect(c, 72, 30, 2, 2, COL_TEXT);
    gfx_rect(c, 72, 35, 2, 2, COL_TEXT);
    /* Title, bitrate and sample rate displays */
    groove(c, 109, 24, 158, 12);
    /* Four digits of bitrate (uncompressed audio passes 1000), two of kHz */
    gfx_rect(c, 110, 42, 26, 9, COL_LCD_BG);
    gfx_text(c, 137, 43, "KBPS", COL_TEXT);
    gfx_rect(c, 161, 42, 14, 9, COL_LCD_BG);
    gfx_text(c, 177, 43, "KHZ", COL_TEXT);
    logo(c, 242, 88, 17);
}

static void paint_titlebar(Canvas *c)
{
    int row, pressed;

    gfx_rect(c, 0, 0, 302, 29, COL_TITLEBAR);
    /* Focused bar on top, unfocused below; both carry their buttons in the
     * normal state. */
    for (row = 0; row <= 15; row += 15) {
        gfx_hline(c, 27, row, 275, COL_LIGHT);
        gfx_vline(c, 27, row, 14, COL_LIGHT);
        gfx_bevel(c, 27, row, 275, 14, COL_LIGHT, COL_TITLEBAR);
        gfx_text(c, 27 + 6, row + 4, "AMPLITUDE", row ? COL_LIGHT : COL_ACCENT);
        grip(c, 27 + 66, row + 5, 182);
        /* With no maximize button, minimize sits right next to close. */
        minimize_button(c, 27 + 254, row + 3, 0);
        close_button(c, 27 + 264, row + 3, 0);
    }
    /* Minimize and close sprites; the menu and shade button cells stay blank. */
    for (pressed = 0; pressed <= 1; pressed++) {
        minimize_button(c, 9, pressed * 9, pressed);
        close_button(c, 18, pressed * 9, pressed);
    }
}

static void paint_cbuttons(Canvas *c)
{
    int i, pressed;

    gfx_rect(c, 0, 0, 136, 36, COL_BODY);
    for (pressed = 0; pressed <= 1; pressed++) {
        for (i = ICON_PREV; i <= ICON_NEXT; i++) {
            button(c, i * 23, pressed * 18, i == ICON_NEXT ? 22 : 23, 18, pressed);
            icon(c, i, i * 23 + 7 + pressed, pressed * 18 + 4 + pressed, COL_ICON);
        }
        button(c, 114, pressed * 16, 22, 16, pressed);
        icon(c, ICON_EJECT, 114 + 6 + pressed, pressed * 16 + 3 + pressed, COL_ICON);
    }
}

/* Seven-segment digits in 9x13 cells. */
static void paint_numbers(Canvas *c)
{
    /* Segment bits: a=top, b/c=right, d=bottom, e/f=left, g=middle. */
    static const unsigned char segments[10] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
    int d;

    gfx_rect(c, 0, 0, 99, 13, COL_LCD_BG);
    for (d = 0; d < 10; d++) {
        int x = d * 9, s = segments[d];

        if (s & 0x01) gfx_rect(c, x, 0, 9, 2, COL_TEXT);
        if (s & 0x02) gfx_rect(c, x + 7, 0, 2, 7, COL_TEXT);
        if (s & 0x04) gfx_rect(c, x + 7, 6, 2, 7, COL_TEXT);
        if (s & 0x08) gfx_rect(c, x, 11, 9, 2, COL_TEXT);
        if (s & 0x10) gfx_rect(c, x, 6, 2, 7, COL_TEXT);
        if (s & 0x20) gfx_rect(c, x, 0, 2, 7, COL_TEXT);
        if (s & 0x40) gfx_rect(c, x, 5, 9, 2, COL_TEXT);
    }
}

static void paint_playpaus(Canvas *c)
{
    gfx_rect(c, 0, 0, 42, 9, COL_LCD_BG);
    icon(c, ICON_PLAY, 0, 0, COL_LCD_FG);
    icon(c, ICON_PAUSE, 9, 0, COL_LCD_FG);
    icon(c, ICON_STOP, 18, 0, COL_LCD_DIM);
}

static void paint_monoster(Canvas *c)
{
    int active;

    gfx_rect(c, 0, 0, 56, 24, COL_BODY);
    for (active = 0; active <= 1; active++) {
        int y = active ? 0 : 12;
        uint32_t color = active ? COL_TEXT : COL_LIGHT;

        gfx_text(c, 3, y + 2, "STER", color);
        gfx_text(c, 29 + 2, y + 2, "MONO", color);
    }
}

static void paint_posbar(Canvas *c)
{
    int pressed;

    gfx_rect(c, 0, 0, 307, 10, COL_BODY);
    groove(c, 0, 2, 248, 6);
    for (pressed = 0; pressed <= 1; pressed++) {
        int x = pressed ? 278 : 248;

        button(c, x, 0, 29, 10, pressed);
        gfx_rect(c, x + 13, 2, 3, 6, COL_ICON);
    }
}

/* Volume and balance sheets share a layout: 28 background frames stacked
 * 15px apart, then the normal and pressed thumbs at y = 422. */
static void paint_slider(Canvas *c, int sheet_w, int x, int w, int centred)
{
    int i, pressed;

    gfx_rect(c, 0, 0, sheet_w, 433, COL_BODY);
    for (i = 0; i < 28; i++) {
        int y = i * 15, fill = i * (w - 2) / 27;

        groove(c, x, y + 3, w, 7);
        /* A centred slider's bar depends on which way it is pushed, which a
         * frame cannot know; the UI draws it instead. */
        if (!centred)
            gfx_rect(c, x + 1, y + 5, fill, 3, COL_LCD_DIM);
    }
    for (pressed = 0; pressed <= 1; pressed++) {
        int tx = pressed ? 0 : 15;

        button(c, tx, 422, 14, 11, pressed);
        gfx_rect(c, tx + 6, 424, 2, 7, COL_ACCENT);
    }
}

/* A button that stays on or off. A 3x3 light in its top left corner shows
 * the state: dark when off, lit in the theme colour when on. The label, if
 * there is one, starts at tx, ty. */
static void toggle(Canvas *c, int x, int y, int w, int h, int pressed, int on, int tx, int ty, const char *label)
{
    button(c, x, y, w, h, pressed);
    gfx_rect(c, x + 2 + pressed, y + 2 + pressed, 3, 3, on ? COL_ACCENT : COL_LCD_BG);
    if (label)
        gfx_text(c, tx, ty, label, COL_TEXT);
}

/* A filled arrowhead with its tip at tx, ty, pointing right (dir = 1) or
 * left (dir = -1). */
static void arrowhead(Canvas *c, float tx, float ty, int dir, uint32_t color)
{
    float base = tx - 3.0f * (float)dir;

    gfx_triangle(c, base, ty - 2, base, ty + 2, tx, ty, color);
}

/* Shuffle: two paths that swap heights as they cross. 14 wide, 9 tall. */
static void shuffle_icon(Canvas *c, int x, int y, uint32_t color)
{
    float fx = (float)x, fy = (float)y;
    float down[] = { fx, fy + 2, fx + 3.5f, fy + 2, fx + 8, fy + 7, fx + 11.5f, fy + 7 };
    float up[] = { fx, fy + 7, fx + 3.5f, fy + 7, fx + 8, fy + 2, fx + 11.5f, fy + 2 };

    gfx_polyline(c, down, 4, 1.3f, color);
    gfx_polyline(c, up, 4, 1.3f, color);
    arrowhead(c, fx + 14, fy + 7, 1, color);
    arrowhead(c, fx + 14, fy + 2, 1, color);
}

/* Repeat: two arrows chasing each other around a loop. 14 wide, 9 tall. */
static void repeat_icon(Canvas *c, int x, int y, uint32_t color)
{
    float fx = (float)x, fy = (float)y;
    float top[] = { fx + 1, fy + 5, fx + 1, fy + 2, fx + 11.5f, fy + 2 };
    float bottom[] = { fx + 13, fy + 4, fx + 13, fy + 7, fx + 2.5f, fy + 7 };

    gfx_polyline(c, top, 3, 1.3f, color);
    gfx_polyline(c, bottom, 3, 1.3f, color);
    arrowhead(c, fx + 14, fy + 2, 1, color);
    arrowhead(c, fx, fy + 7, -1, color);
}

/* A cog wheel, 10 across, centred on cx, cy. */
static void cog_icon(Canvas *c, float cx, float cy, uint32_t color)
{
    float ring[13 * 2], tooth[4];
    int i;

    for (i = 0; i < 8; i++) {
        float a = (float)i * PI_F / 4;

        tooth[0] = cx + 3.0f * cosf(a);
        tooth[1] = cy + 3.0f * sinf(a);
        tooth[2] = cx + 4.6f * cosf(a);
        tooth[3] = cy + 4.6f * sinf(a);
        gfx_polyline(c, tooth, 2, 1.8f, color);
    }
    for (i = 0; i < 13; i++) {
        float a = (float)i * PI_F / 6;

        ring[i * 2] = cx + 2.6f * cosf(a);
        ring[i * 2 + 1] = cy + 2.6f * sinf(a);
    }
    gfx_polyline(c, ring, 13, 1.7f, color);
}

static void paint_shufrep(Canvas *c)
{
    int state;

    gfx_rect(c, 0, 0, 158, 85, COL_BODY);
    /* The small buttons of the built-in layout, to the right of the classic
     * sheet: shuffle, repeat and the menu cog. Rows as below; the cog has
     * no light and only uses the first two. */
    for (state = 0; state < 4; state++) {
        int y = state * 16, pressed = state & 1, on = state >= 2;

        toggle(c, 92, y, 22, 16, pressed, on, 0, 0, NULL);
        shuffle_icon(c, 92 + 5 + pressed, y + 4 + pressed, COL_TEXT);
        toggle(c, 114, y, 22, 16, pressed, on, 0, 0, NULL);
        repeat_icon(c, 114 + 5 + pressed, y + 4 + pressed, COL_TEXT);
        button(c, 136, y, 22, 16, pressed);
        cog_icon(c, 136 + 11 + (float)pressed, (float)y + 8 + (float)pressed, COL_TEXT);
    }
    /* Rows: off, off pressed, on, on pressed. */
    for (state = 0; state < 4; state++) {
        int y = state * 15, pressed = state & 1, on = state >= 2;

        toggle(c, 0, y, 28, 15, pressed, on, 0, 0, NULL);
        repeat_icon(c, 8 + pressed, y + 3 + pressed, COL_TEXT);
        toggle(c, 28, y, 47, 15, pressed, on, 0, 0, NULL);
        shuffle_icon(c, 28 + 17 + pressed, y + 3 + pressed, COL_TEXT);
    }
    /* Equaliser and playlist toggles: off/on rows, normal/pressed columns. */
    for (state = 0; state < 4; state++) {
        int x = (state & 1) * 46, y = 61 + (state >> 1) * 12, on = state >= 2;

        toggle(c, x, y, 23, 12, state & 1, on, x + 6, y + 3, "EQ");
        toggle(c, x + 23, y, 23, 12, state & 1, on, x + 23 + 6, y + 3, "PL");
    }
}

/* Title strip shared by the equaliser and playlist: caption, grip lines
 * and (when close_x >= 0) a close button. */
static void title_strip(Canvas *c, int x, int y, int w, int h, const char *caption, int focused, int close_x)
{
    int text_w = gfx_text_width(caption);

    gfx_rect(c, x, y, w, h, COL_TITLEBAR);
    gfx_hline(c, x, y, w, COL_LIGHT);
    if (*caption) {
        gfx_text(c, x + 6, y + (h - GFX_FONT_H) / 2 + 1, caption, focused ? COL_ACCENT : COL_LIGHT);
        text_w += 12;
    }
    grip(c, x + text_w, y + h / 2 - 2, (close_x >= 0 ? close_x - 6 : w) - text_w);
    if (close_x >= 0)
        close_button(c, x + close_x, y + 3, 0);
}

static void paint_eqmain(Canvas *c)
{
    /* The centre frequency of each band, as in audio.c. */
    static const char *const labels[10] = { "60", "170", "310", "600", "1K", "3K", "6K", "12K", "14K", "16K" };
    int i, state;

    gfx_rect(c, 0, 0, 275, 315, COL_BODY);
    /* Background */
    gfx_bevel(c, 0, 0, 275, 116, COL_LIGHT, COL_DARK);
    gfx_text(c, 44, 38, "+12", COL_TEXT);
    gfx_text(c, 56, 66, "0", COL_TEXT);
    gfx_text(c, 44, 94, "-12", COL_TEXT);
    /* The sliders are 18 apart, too close for the label font, so every
     * slider is named in the small one. */
    gfx_utext(c, &font_small, 21 + 7 - gfx_utext_width(c->scale, &font_small, "PRE") / 2, 104, "PRE", COL_TEXT);
    for (i = 0; i < 10; i++)
        gfx_utext(c, &font_small, 78 + i * 18 + 7 - gfx_utext_width(c->scale, &font_small, labels[i]) / 2, 104,
                  labels[i], COL_TEXT);

    /* Title bars (focused, unfocused) with the close button and its pressed state */
    title_strip(c, 0, 134, 275, 14, "EQUALIZER", 1, 264);
    title_strip(c, 0, 149, 275, 14, "EQUALIZER", 0, 264);
    gfx_vline(c, 0, 134, 14, COL_LIGHT);
    close_button(c, 0, 116, 0);
    close_button(c, 0, 125, 1);

    /* ON and AUTO: off, on, off pressed, on pressed */
    for (state = 0; state < 4; state++) {
        static const int on_x[4] = { 10, 69, 128, 187 };
        int x = on_x[state], pressed = state >= 2;

        toggle(c, x, 119, 26, 12, pressed, state & 1, x + 7 + pressed, 122, "ON");
        toggle(c, x + 26, 119, 32, 12, pressed, state & 1, x + 26 + 6 + pressed, 122, "AUTO");
    }
    for (state = 0; state < 2; state++) {
        button(c, 224, 164 + state * 12, 44, 12, state);
        gfx_text(c, 224 + 1 + state, 164 + state * 12 + 3, "PRESETS", COL_TEXT);
    }

    /* Slider backgrounds: 28 levels in two rows of 14, lowest first */
    for (i = 0; i < 28; i++) {
        int x = 13 + (i % 14) * 15, y = 164 + (i / 14) * 65;
        int level = y + 5 + (27 - i) * 51 / 27;     /* thumb centre */

        groove(c, x + 4, y, 6, 63);
        if (level < y + 31)
            gfx_rect(c, x + 5, level, 4, y + 31 - level, COL_LCD_DIM);
        else
            gfx_rect(c, x + 5, y + 31, 4, level - (y + 31), COL_LCD_DIM);
        gfx_hline(c, x + 2, y + 31, 10, COL_LIGHT);
    }
    for (state = 0; state < 2; state++) {
        button(c, 0, 164 + state * 12, 11, 11, state);
        gfx_hline(c, 2, 164 + state * 12 + 5, 7, COL_ACCENT);
    }

    /* Response graph background and the preamp line */
    gfx_rect(c, 0, 294, 113, 19, COL_LCD_BG);
    gfx_hline(c, 0, 303, 113, COL_LCD_GRID);
    gfx_hline(c, 0, 314, 113, COL_LCD_DIM);
}

/* One of the playlist's 25x18 buttons, with its label centred. */
static void list_button(Canvas *c, int x, int y, const char *label)
{
    button(c, x, y, 25, 18, 0);
    gfx_text(c, x + (26 - gfx_text_width(label)) / 2, y + 6, label, COL_TEXT);
}

/* The transport icons again, small: each fits a 7x7 box. In the order of
 * the playlist's controls: previous, play, pause, stop, next, open. */
static void mini_icon(Canvas *c, int which, int x, int y, uint32_t color)
{
    float fx = (float)x, fy = (float)y;

    switch (which) {
    case 0:
        gfx_rect(c, x, y, 1, 7, color);
        gfx_triangle(c, fx + 6, fy, fx + 6, fy + 7, fx + 2, fy + 3.5f, color);
        break;
    case 1:
        gfx_triangle(c, fx + 1, fy, fx + 1, fy + 7, fx + 6, fy + 3.5f, color);
        break;
    case 2:
        gfx_rect(c, x + 1, y, 2, 7, color);
        gfx_rect(c, x + 4, y, 2, 7, color);
        break;
    case 3:
        gfx_rect(c, x + 1, y + 1, 5, 5, color);
        break;
    case 4:
        gfx_triangle(c, fx + 1, fy, fx + 1, fy + 7, fx + 5, fy + 3.5f, color);
        gfx_rect(c, x + 6, y, 1, 7, color);
        break;
    default:
        gfx_triangle(c, fx, fy + 4, fx + 7, fy + 4, fx + 3.5f, fy, color);
        gfx_rect(c, x, y + 5, 7, 2, color);
        break;
    }
}

static void paint_pledit(Canvas *c)
{
    static const char *const labels[] = { "ADD", "REM", "SEL", "MISC" };
    int focused, pressed, i;

    gfx_rect(c, 0, 0, 280, 186, COL_BODY);
    /* Title bar pieces: left corner, caption, filler, right corner */
    for (focused = 1; focused >= 0; focused--) {
        int y = focused ? 0 : 21;

        title_strip(c, 0, y, 25, 20, "", focused, -1);
        gfx_vline(c, 0, y, 20, COL_LIGHT);
        title_strip(c, 26, y, 100, 20, "", focused, -1);
        gfx_rect(c, 26 + 20, y + 2, 60, 18, COL_TITLEBAR);
        gfx_text(c, 26 + 26, y + 7, "PLAYLIST", focused ? COL_ACCENT : COL_LIGHT);
        title_strip(c, 127, y, 25, 20, "", focused, -1);
        title_strip(c, 153, y, 25, 20, "", focused, 14);
        gfx_bevel(c, 153, y, 25, 20, COL_LIGHT, COL_DARK);
        gfx_rect(c, 153, y + 2, 2, 18, COL_TITLEBAR);   /* keep only the top and right edges */
        grip(c, 153, y + 8, 8);
        gfx_hline(c, 153, y + 19, 24, COL_TITLEBAR);
    }
    close_button(c, 52, 42, 1);

    /* Side borders; the right one carries the scrollbar track */
    gfx_vline(c, 0, 42, 29, COL_LIGHT);
    gfx_rect(c, 31 + 4, 42, 10, 29, COL_LCD_BG);
    gfx_bevel(c, 31, 41, 20, 31, COL_BODY, COL_DARK);
    gfx_hline(c, 31, 70, 20, COL_BODY);
    gfx_rect(c, 31 + 4, 69, 10, 2, COL_LCD_BG);
    for (pressed = 0; pressed <= 1; pressed++) {
        button(c, 52 + pressed * 9, 53, 8, 18, pressed);
        gfx_rect(c, 52 + pressed * 9 + 2, 61, 4, 2, COL_ACCENT);
    }

    /* Bottom: left part with the list buttons, filler, right part */
    gfx_bevel(c, 0, 71, 125, 39, COL_LIGHT, COL_DARK);
    gfx_rect(c, 2, 71, 123, 2, COL_BODY);           /* keep only the left and bottom edges */
    gfx_rect(c, 123, 72, 2, 36, COL_BODY);
    for (i = 0; i < 4; i++)
        list_button(c, 12 + i * 29, 72 + 8, labels[i]);
    gfx_bevel(c, 178, -1, 27, 39, COL_BODY, COL_DARK);  /* filler: bottom edge only */
    gfx_rect(c, 203, 0, 1, 36, COL_BODY);
    gfx_bevel(c, 125, 71, 151, 39, COL_BODY, COL_DARK); /* right part: right and bottom edges */

    /* Right part: total time, small transport controls, clock and LIST.
     * ui_playlist.c writes the two times into the displays. */
    groove(c, 126 + 6, 72 + 8, 93, 11);
    for (i = 0; i < 6; i++)
        mini_icon(c, i, 126 + 4 + i * 9, 72 + 23, COL_TEXT);
    groove(c, 126 + 63, 72 + 21, 36, 11);
    list_button(c, 126 + 104, 72 + 8, "LIST");
}

void skin_paint_builtin(Canvas *c, int sheet)
{
    switch (sheet) {
    case SKIN_MAIN:     paint_main(c); break;
    case SKIN_TITLEBAR: paint_titlebar(c); break;
    case SKIN_CBUTTONS: paint_cbuttons(c); break;
    case SKIN_NUMBERS:  paint_numbers(c); break;
    case SKIN_PLAYPAUS: paint_playpaus(c); break;
    case SKIN_MONOSTER: paint_monoster(c); break;
    case SKIN_POSBAR:   paint_posbar(c); break;
    case SKIN_VOLUME:   paint_slider(c, 68, 0, 68, 0); break;
    case SKIN_BALANCE:  paint_slider(c, 47, 9, 38, 1); break;
    case SKIN_SHUFREP:  paint_shufrep(c); break;
    case SKIN_EQMAIN:   paint_eqmain(c); break;
    case SKIN_PLEDIT:   paint_pledit(c); break;
    default:            /* the text sheet is drawn with real fonts instead */
        gfx_rect(c, 0, 0, sheet_sizes[sheet].w, sheet_sizes[sheet].h, COL_LCD_BG);
        break;
    }
}

void skin_init_default(Skin *skin)
{
    int i;

    memset(skin, 0, sizeof *skin);
    for (i = 0; i < SKIN_SHEET_COUNT; i++) {
        skin->builtin[i] = 1;
        skin->sheet[i].w = sheet_sizes[i].w;
        skin->sheet[i].h = sheet_sizes[i].h;
    }
    skin->text_w = GFX_FONT_ADVANCE;
    skin->text_h = GFX_FONT_H;
    skin->text_color = COL_TEXT;
    skin->text_background = COL_LCD_BG;
    skin->slider_fill = COL_LCD_DIM;
    for (i = 0; i < SKIN_EQ_GRAPH_H; i++)
        skin->eq_line[i] = COL_LCD_FG;

    skin->vis[0] = COL_LCD_BG;
    skin->vis[1] = COL_LCD_GRID;
    /* Spectrum bars: a pale tint at the top (2) deepening to the display colour (17). */
    for (i = 2; i <= 17; i++) {
        int t = (i - 2) * 256 / 15, shift;

        skin->vis[i] = 0;
        for (shift = 0; shift <= 16; shift += 8)
            skin->vis[i] |= (uint32_t)(((COL_LCD_PEAK >> shift & 255) * (256 - t) +
                                         (COL_LCD_FG >> shift & 255) * t) >> 8) << shift;
    }
    for (i = 18; i <= 20; i++)      /* oscilloscope, centre outwards */
        skin->vis[i] = COL_LCD_FG;
    skin->vis[21] = skin->vis[22] = COL_LCD_DIM;
    skin->vis[23] = COL_ICON;       /* spectrum peaks */

    skin->pl_normal = COL_TEXT;
    skin->pl_current = COL_TEXT_ON;
    skin->pl_background = COL_LCD_BG;
    skin->pl_selected = theme_get()->highlight;
}
