#include "ui.h"

#include "util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int x, y, w, h; } Rect;

/* Element positions of the classic main window. */
static const Rect rects[] = {
    [UI_TITLEBAR] = { 0, 0, UI_W, 14 },
    [UI_MINIMIZE] = { 244, 3, 9, 9 },
    [UI_CLOSE]    = { 264, 3, 9, 9 },
    [UI_PREV]     = { 16, 88, 23, 18 },
    [UI_PLAY]     = { 39, 88, 23, 18 },
    [UI_PAUSE]    = { 62, 88, 23, 18 },
    [UI_STOP]     = { 85, 88, 23, 18 },
    [UI_NEXT]     = { 108, 88, 22, 18 },
    [UI_OPEN]     = { 136, 89, 22, 16 },
    [UI_SHUFFLE]  = { 164, 89, 46, 15 },
    [UI_REPEAT]   = { 210, 89, 28, 15 },
    [UI_EQ_TOGGLE] = { 219, 58, 23, 12 },
    [UI_PL_TOGGLE] = { 242, 58, 23, 12 },
    [UI_VIS]      = { 24, 43, 76, 16 },
    [UI_SEEK]     = { 16, 72, 248, 10 },
    [UI_VOLUME]   = { 107, 57, 68, 13 },
    [UI_BALANCE]  = { 177, 57, 38, 13 },
};

/* The built-in skin shows shuffle and repeat as icons on small buttons,
 * which leaves room for a third: the cog that opens the menu. Classic skins
 * bring pictures of the wide lettered buttons, so they keep the positions
 * above and have no cog. */
static const Rect compact_rects[] = {
    { 164, 89, 22, 16 },                            /* UI_SHUFFLE */
    { 186, 89, 22, 16 },                            /* UI_REPEAT */
    { UI_MENU_X, UI_MENU_Y - 16, 22, 16 },          /* UI_MENU */
};

/* The logo painted on the built-in main window; a click opens About. A
 * classic skin draws its own emblem in the place its format reserves for
 * that, and a click there does the same. */
static const Rect logo_rect = { 240, 86, 33, 21 };
static const Rect classic_logo_rect = { 253, 91, 13, 15 };

static int compact(const Skin *skin)
{
    return skin->builtin[SKIN_SHUFREP] && skin->builtin[SKIN_MAIN];
}

static const Rect *element_rect(const Skin *skin, int element)
{
    if (element >= UI_SHUFFLE && element <= UI_MENU && compact(skin))
        return &compact_rects[element - UI_SHUFFLE];
    if (element == UI_LOGO)
        return skin->builtin[SKIN_MAIN] ? &logo_rect : &classic_logo_rect;
    return &rects[element];     /* all zero for the cog: it is never hit */
}

static const Rect title_field = { 111, 27, 154, 6 };

static int title_scrolls;       /* see ui_title_scrolls() */

int ui_title_scrolls(void)
{
    return title_scrolls;
}
#define SEEK_THUMB_W   29
#define SLIDER_THUMB_W 14
#define SLIDER_FRAMES  28
#define SLIDER_THUMB_Y 422      /* thumb sprites inside volume/balance sheets */

static int inside(const Rect *r, int x, int y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* The built-in skin has no shade button, so its minimize button takes that
 * place, right next to close. */
static int minimize_x(const Skin *skin)
{
    return skin->builtin[SKIN_TITLEBAR] ? 254 : rects[UI_MINIMIZE].x;
}

int ui_hit(const Skin *skin, int x, int y)
{
    Rect minimize = rects[UI_MINIMIZE];
    int i;

    minimize.x = minimize_x(skin);
    if (inside(&minimize, x, y))
        return UI_MINIMIZE;
    /* The title bar covers its own buttons, so test it last. */
    for (i = UI_CLOSE; i <= UI_BALANCE; i++)
        if (inside(element_rect(skin, i), x, y))
            return i;
    return inside(&rects[UI_TITLEBAR], x, y) ? UI_TITLEBAR : UI_NONE;
}

static int thumb_width(int element)
{
    return element == UI_SEEK ? SEEK_THUMB_W : SLIDER_THUMB_W;
}

float ui_slider_value(int element, int x)
{
    const Rect *r = &rects[element];
    int thumb = thumb_width(element);
    float v = (float)(x - r->x - thumb / 2) / (float)(r->w - thumb);

    return v < 0 ? 0 : v > 1 ? 1 : v;
}

static int thumb_x(int element, float value)
{
    const Rect *r = &rects[element];

    return r->x + (int)(value * (float)(r->w - thumb_width(element)) + 0.5f);
}

static void sprite(Canvas *c, const Skin *skin, int sheet, int sx, int sy, int element)
{
    const Rect *r = &rects[element];

    skin_blit(c, skin, sheet, sx, sy, r->w, r->h, r->x, r->y);
}

static void draw_text(Canvas *c, const Skin *skin, int x, int y, const char *text)
{
    int col, row;

    if (skin->builtin[SKIN_TEXT]) {
        /* The built-in skin has no lettering of its own: use a real font. */
        gfx_rect(c, x, y, (int)strlen(text) * skin->text_w, skin->text_h, skin->text_background);
        gfx_text(c, x, y, text, skin->text_color);
        return;
    }

    for (; *text; text++, x += skin->text_w) {
        skin_text_cell((unsigned char)*text, &col, &row);
        skin_blit(c, skin, SKIN_TEXT, col * skin->text_w, row * skin->text_h,
                 skin->text_w, skin->text_h, x, y);
    }
}

/* A classic skin's own lettering is used when it can spell the whole title
 * and would stay crisp, which bitmap letters only do at whole magnifications.
 * Otherwise (and always with the built-in skin) a real font takes over. */
static int skin_font_covers(const Canvas *c, const Skin *skin, const char *text)
{
    int col, row;

    if (skin->builtin[SKIN_TEXT] || c->scale % 100)
        return 0;
    for (; *text; text++)
        if ((unsigned char)*text >= 0x80 || !skin_text_cell((unsigned char)*text, &col, &row))
            return 0;
    return 1;
}

#define MARQUEE_MIN_FRAME_US 20000   /* no more than 50 frames a second */

/* How many real pixels the title moves in each frame. */
static int marquee_step(int scale)
{
    int pixel_us = UI_MARQUEE_MS * 1000 * 100 / scale;      /* the time one real pixel takes */

    return (MARQUEE_MIN_FRAME_US + pixel_us - 1) / pixel_us;
}

int ui_marquee_frame_us(int scale)
{
    return marquee_step(scale) * (UI_MARQUEE_MS * 1000 * 100 / scale);
}

static void draw_title(Canvas *c, const Skin *skin, const UiModel *m)
{
    static const char separator[] = "  ***  ";
    const Rect *r = &title_field;
    int unicode = !skin_font_covers(c, skin, m->title), pass;
    /* Our own fonts are taller than the classic 6px slot: the 7px one sits a
     * row higher, and the built-in skin's display has room for the 10px one. */
    const GfxFont *font = skin->builtin[SKIN_MAIN] ? &font_list : &font_small;
    int y = !unicode ? r->y : skin->builtin[SKIN_MAIN] ? r->y - 2 : r->y - 1;
    int width = unicode ? gfx_utext_width(c->scale, font, m->title)
                        : (int)strlen(m->title) * skin->text_w;
    /* The same in real pixels, in which the scrolling is reckoned. */
    int real_width = unicode ? gfx_utext_real_width(c->scale, font, m->title)
                             : GFX_SCALED((int)strlen(m->title) * skin->text_w, c->scale);
    int real_gap = unicode ? gfx_utext_real_width(c->scale, font, separator)
                           : GFX_SCALED((int)(sizeof separator - 1) * skin->text_w, c->scale);
    int loop = real_width + real_gap, scrolling = width > r->w, shift = 0;

    gfx_set_clip(c, r->x, y, r->w, unicode ? font->h : skin->text_h);
    /* Marquee: text that does not fit chases its own tail around a loop. */
    title_scrolls = scrolling;
    if (scrolling) {
        uint64_t frame = (uint64_t)m->ticks * 1000 / (uint64_t)ui_marquee_frame_us(c->scale);

        shift = -(int)(frame * (uint64_t)marquee_step(c->scale) % (uint64_t)loop);
    }
    for (pass = 0; pass <= scrolling; pass++, shift += loop) {
        const char *tail = scrolling && !pass ? separator : "";

        c->shift_x = shift;
        if (unicode)
            gfx_utext(c, font, r->x, y, m->title, skin->text_color);
        else
            draw_text(c, skin, r->x, y, m->title);
        c->shift_x = shift + real_width;
        if (unicode)
            gfx_utext(c, font, r->x, y, tail, skin->text_color);
        else
            draw_text(c, skin, r->x, y, tail);
    }
    c->shift_x = 0;
    gfx_reset_clip(c);
}

static void draw_time(Canvas *c, const Skin *skin, const UiModel *m)
{
    static const int digit_x[4] = { 48, 60, 78, 90 };
    int seconds = (int)m->position, minutes = (seconds / 60) % 100;
    int digits[4], i;

    switch (m->state) {
    case AUDIO_PLAYING:
        skin_blit(c, skin, SKIN_PLAYPAUS, 36, 0, 3, 9, 24, 28);
        skin_blit(c, skin, SKIN_PLAYPAUS, 1, 0, 8, 9, 27, 28);
        break;
    case AUDIO_PAUSED:
        skin_blit(c, skin, SKIN_PLAYPAUS, 9, 0, 9, 9, 26, 28);
        break;
    default:
        skin_blit(c, skin, SKIN_PLAYPAUS, 18, 0, 9, 9, 26, 28);
        return;     /* no time shown while stopped */
    }
    /* Blink the time while paused. */
    if (m->state == AUDIO_PAUSED && (m->ticks / UI_BLINK_MS) % 2)
        return;
    digits[0] = minutes / 10;
    digits[1] = minutes % 10;
    digits[2] = seconds % 60 / 10;
    digits[3] = seconds % 10;
    for (i = 0; i < 4; i++)
        skin_blit(c, skin, SKIN_NUMBERS, digits[i] * 9, 0, 9, 13, digit_x[i], 26);
}

static void vis_background(Canvas *c, const Skin *skin)
{
    const Rect *r = &rects[UI_VIS];
    int x, y;

    gfx_rect(c, r->x, r->y, r->w, r->h, skin->vis[0]);
    for (y = 0; y < r->h; y += 2)
        for (x = 0; x < r->w; x += 4)
            gfx_pixel(c, r->x + x, r->y + y, skin->vis[1]);
}

static void draw_scope(Canvas *c, const Skin *skin, const UiModel *m)
{
    const Rect *r = &rects[UI_VIS];
    int x, half = r->h / 2;

    vis_background(c, skin);
    for (x = 0; x < r->w; x++) {
        int dy = (int)(m->vis[x * AUDIO_VIS_SAMPLES / r->w] * (float)half);
        int shade;

        if (dy > half - 1)
            dy = half - 1;
        if (dy < -half)
            dy = -half;
        /* Colours 18..22 run from the centre line outwards. */
        shade = (dy < 0 ? -dy : dy) * 5 / (half + 1);
        gfx_pixel(c, r->x + x, r->y + half - dy, skin->vis[18 + shade]);
    }
}

/* In-place radix-2 FFT; n must be a power of two. */
static void fft(float *re, float *im, int n)
{
    int i, j = 0, len;

    for (i = 1; i < n; i++) {
        int bit = n >> 1;

        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i];

            re[i] = re[j];
            re[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * PI_F / (float)len;
        float step_re = cosf(angle), step_im = sinf(angle);

        for (i = 0; i < n; i += len) {
            float w_re = 1, w_im = 0;

            for (j = 0; j < len / 2; j++) {
                float *a_re = &re[i + j], *a_im = &im[i + j];
                float *b_re = &re[i + j + len / 2], *b_im = &im[i + j + len / 2];
                float t_re = *b_re * w_re - *b_im * w_im, t_im = *b_re * w_im + *b_im * w_re;
                float next = w_re * step_re - w_im * step_im;

                *b_re = *a_re - t_re;
                *b_im = *a_im - t_im;
                *a_re += t_re;
                *a_im += t_im;
                w_im = w_re * step_im + w_im * step_re;
                w_re = next;
            }
        }
    }
}

#define SPECTRUM_BARS   19      /* 3 pixels wide, 1 apart */
#define SPECTRUM_BINS   (AUDIO_VIS_SAMPLES / 2)
#define SPECTRUM_DB     60.0f   /* range from the bottom of the display to full scale */
#define SPECTRUM_TILT   0.75f   /* dB added per bar, lifting the quieter highs */
#define BAR_FALL        0.06f   /* how far a bar and its peak marker drop per frame, */
#define PEAK_FALL       0.012f  /* as a share of the display's height */

/* Classic bar analyser: logarithmic bands, bars that fall back gradually
 * and a peak marker that lingers above each one. */
static void draw_spectrum(Canvas *c, const Skin *skin, const UiModel *m)
{
    static float bar[SPECTRUM_BARS], peak[SPECTRUM_BARS];
    /* Worked out on the first call: the Hann window (against spectral
     * leakage) and the band edges, which grow geometrically from bin 1 to
     * the last bin. */
    static float window[AUDIO_VIS_SAMPLES];
    static int edge[SPECTRUM_BARS + 1];
    float re[AUDIO_VIS_SAMPLES], im[AUDIO_VIS_SAMPLES];
    const Rect *r = &rects[UI_VIS];
    int i, k;

    if (!edge[SPECTRUM_BARS]) {
        for (i = 0; i < AUDIO_VIS_SAMPLES; i++)
            window[i] = 0.5f - 0.5f * cosf(2.0f * PI_F * (float)i / (AUDIO_VIS_SAMPLES - 1));
        for (i = 0; i <= SPECTRUM_BARS; i++)
            edge[i] = (int)powf((float)SPECTRUM_BINS, (float)i / SPECTRUM_BARS);
    }
    for (i = 0; i < AUDIO_VIS_SAMPLES; i++) {
        re[i] = m->vis[i] * window[i];
        im[i] = 0;
    }
    fft(re, im, AUDIO_VIS_SAMPLES);

    vis_background(c, skin);
    for (i = 0; i < SPECTRUM_BARS; i++) {
        int lo = edge[i], hi = edge[i + 1] > edge[i] ? edge[i + 1] : edge[i] + 1;
        float power = 0, level;
        int height, top;

        for (k = lo; k < hi && k < SPECTRUM_BINS; k++) {
            float p = re[k] * re[k] + im[k] * im[k];

            if (p > power)
                power = p;
        }
        /* dB relative to a full-scale sine */
        level = 10.0f * log10f(power / ((float)SPECTRUM_BINS * SPECTRUM_BINS / 4.0f) + 1e-12f) +
                SPECTRUM_TILT * (float)i;
        level = (level + SPECTRUM_DB) / SPECTRUM_DB;
        level = CLAMP(level, 0, 1);

        bar[i] = level > bar[i] ? level : bar[i] - BAR_FALL;
        if (bar[i] < 0)
            bar[i] = 0;
        peak[i] = bar[i] > peak[i] ? bar[i] : peak[i] - PEAK_FALL;

        height = (int)(bar[i] * (float)r->h + 0.5f);
        for (k = 0; k < height; k++)    /* colour 17 at the bottom up to 2 at the top */
            gfx_rect(c, r->x + i * 4, r->y + r->h - 1 - k, 3, 1, skin->vis[17 - k]);
        top = (int)(peak[i] * (float)r->h + 0.5f);
        if (top > 0)
            gfx_rect(c, r->x + i * 4, r->y + r->h - (top > r->h ? r->h : top), 3, 1, skin->vis[23]);
    }
}

static void draw_vis(Canvas *c, const Skin *skin, const UiModel *m)
{
    if (m->state == AUDIO_STOPPED || !m->vis)
        return;
    if (m->vis_mode == VIS_SPECTRUM)
        draw_spectrum(c, skin, m);
    else if (m->vis_mode == VIS_SCOPE)
        draw_scope(c, skin, m);
}

/* Volume and balance: the background frame shows the level, the thumb
 * (which some skins leave out) sits on top. */
static void draw_slider(Canvas *c, const Skin *skin, int element, int sheet, int sx,
                        float level, float thumb_pos, int pressed)
{
    const Rect *r = &rects[element];
    const Bitmap *bmp = &skin->sheet[sheet];
    int frame = (int)(level * (SLIDER_FRAMES - 1) + 0.5f);

    skin_blit(c, skin, sheet, sx, frame * 15, r->w, r->h, r->x, r->y);
    if (element == UI_BALANCE && skin->builtin[sheet]) {
        /* Level bar from the centre towards the thumb. */
        int centre = r->x + r->w / 2, thumb = thumb_x(element, thumb_pos) + SLIDER_THUMB_W / 2;

        gfx_rect(c, thumb < centre ? thumb : centre, r->y + 5, abs(thumb - centre), 3, skin->slider_fill);
    }
    if (bmp->h > SLIDER_THUMB_Y)
        skin_blit(c, skin, sheet, pressed ? 0 : 15, SLIDER_THUMB_Y, SLIDER_THUMB_W, 11,
                 thumb_x(element, thumb_pos), r->y + 1);
}

void ui_draw(uint32_t *framebuffer, int scale, const Skin *skin, const UiModel *m)
{
    static const int cbutton_x[] = { 0, 23, 46, 69, 92 };
    const Bitmap *titlebar = &skin->sheet[SKIN_TITLEBAR];
    Canvas canvas, *c = &canvas;
    char text[16];
    float balance = m->balance < 0 ? -m->balance : m->balance;
    int i;

    gfx_init(c, framebuffer, UI_W, UI_H, scale);
    /* A skin's background normally covers the window; only one that is too
     * small leaves anything to clear. */
    if (skin->sheet[SKIN_MAIN].w < UI_W || skin->sheet[SKIN_MAIN].h < UI_H)
        gfx_rect(c, 0, 0, UI_W, UI_H, 0);
    skin_blit(c, skin, SKIN_MAIN, 0, 0, UI_W, UI_H, 0, 0);

    /* Title bar; its strip already shows the buttons in their normal state. */
    skin_blit(c, skin, SKIN_TITLEBAR, 27, 0, UI_W, 14, 0, 0);
    if (m->pressed == UI_MINIMIZE)
        skin_blit(c, skin, SKIN_TITLEBAR, 9, 9, 9, 9, minimize_x(skin), rects[UI_MINIMIZE].y);
    if (m->pressed == UI_CLOSE)
        sprite(c, skin, SKIN_TITLEBAR, 18, 9, UI_CLOSE);
    if (titlebar->w >= 312)     /* option letters left of the display */
        skin_blit(c, skin, SKIN_TITLEBAR, 304, 0, 8, 43, 10, 22);

    draw_time(c, skin, m);
    draw_vis(c, skin, m);
    draw_title(c, skin, m);

    if (m->loaded) {
        /* The built-in window has room for four digits of bitrate. A classic
         * skin's field holds three, so there four-digit rates are shown the
         * way those players did: in hundreds, as "14H" for 1411. */
        int wide = skin->builtin[SKIN_MAIN];

        if (wide)
            snprintf(text, sizeof text, "%4d", m->kbps > 9999 ? 9999 : m->kbps);
        else if (m->kbps > 999)
            snprintf(text, sizeof text, "%2dH", m->kbps > 9999 ? 99 : m->kbps / 100);
        else
            snprintf(text, sizeof text, "%3d", m->kbps);
        draw_text(c, skin, 111, 43, text);
        snprintf(text, sizeof text, "%2d", m->khz > 99 ? 99 : m->khz);
        draw_text(c, skin, wide ? 162 : 156, 43, text);
    }
    skin_blit(c, skin, SKIN_MONOSTER, 29, m->loaded && m->channels == 1 ? 0 : 12, 27, 12, 212, 41);
    skin_blit(c, skin, SKIN_MONOSTER, 0, m->loaded && m->channels >= 2 ? 0 : 12, 29, 12, 239, 41);

    draw_slider(c, skin, UI_VOLUME, SKIN_VOLUME, 0, m->volume, m->volume, m->pressed == UI_VOLUME);
    draw_slider(c, skin, UI_BALANCE, SKIN_BALANCE, 9, balance, (m->balance + 1) / 2,
                m->pressed == UI_BALANCE);

    /* Equaliser and playlist toggles: off/on rows, normal/pressed columns */
    sprite(c, skin, SKIN_SHUFREP, m->pressed == UI_EQ_TOGGLE ? 46 : 0, m->eq_visible ? 73 : 61, UI_EQ_TOGGLE);
    sprite(c, skin, SKIN_SHUFREP, m->pressed == UI_PL_TOGGLE ? 69 : 23, m->pl_visible ? 73 : 61, UI_PL_TOGGLE);

    sprite(c, skin, SKIN_POSBAR, 0, 0, UI_SEEK);
    if (m->loaded && m->length > 0)
        skin_blit(c, skin, SKIN_POSBAR, m->pressed == UI_SEEK ? 278 : 248, 0, SEEK_THUMB_W, 10,
                 thumb_x(UI_SEEK, (float)(m->position / m->length)), rects[UI_SEEK].y);

    for (i = UI_PREV; i <= UI_NEXT; i++)
        sprite(c, skin, SKIN_CBUTTONS, cbutton_x[i - UI_PREV], m->pressed == i ? 18 : 0, i);
    sprite(c, skin, SKIN_CBUTTONS, 114, m->pressed == UI_OPEN ? 16 : 0, UI_OPEN);
    if (compact(skin)) {
        /* Small buttons from the right-hand part of the built-in sheet:
         * rows off, off pressed, on, on pressed. */
        int on[3] = { m->shuffle, m->repeat, 0 };

        for (i = UI_SHUFFLE; i <= UI_MENU; i++) {
            const Rect *r = element_rect(skin, i);

            skin_blit(c, skin, SKIN_SHUFREP, 92 + (i - UI_SHUFFLE) * 22,
                      (on[i - UI_SHUFFLE] ? 32 : 0) + (m->pressed == i ? 16 : 0), r->w, r->h, r->x, r->y);
        }
        return;
    }
    skin_blit(c, skin, SKIN_SHUFREP, 28, (m->shuffle ? 30 : 0) + (m->pressed == UI_SHUFFLE ? 15 : 0),
             47, 15, 164, 89);
    sprite(c, skin, SKIN_SHUFREP, 0, (m->repeat ? 30 : 0) + (m->pressed == UI_REPEAT ? 15 : 0), UI_REPEAT);
}
