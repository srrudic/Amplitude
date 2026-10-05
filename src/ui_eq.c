/* Equaliser window, laid out like the classic eqmain.bmp sheet expects. */
#include "ui.h"

#include <string.h>

#define SLIDER_Y      38
#define SLIDER_W      14
#define SLIDER_H      63
#define THUMB_SIZE    11
#define THUMB_TRAVEL  (SLIDER_H - THUMB_SIZE - 1)
#define SLIDER_FRAMES 28
#define CENTRE_SNAP   0.08f     /* a slider this close to 0 dB sticks to it */

#define GRAPH_X 86
#define GRAPH_Y 17
#define GRAPH_W 113
#define GRAPH_H 19

static int slider_x(int index)
{
    return index == 0 ? 21 : 78 + (index - 1) * 18;
}

static int inside(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

int eq_hit(int x, int y, int *slider)
{
    int i;

    if (inside(x, y, 264, 3, 9, 9))
        return UI_EQ_CLOSE;
    if (inside(x, y, 14, 18, 26, 12))
        return UI_EQ_ON;
    if (inside(x, y, 40, 18, 32, 12))
        return UI_EQ_AUTO;
    if (inside(x, y, EQ_PRESETS_X, EQ_PRESETS_Y - 12, 44, 12))
        return UI_EQ_PRESETS;
    /* The dB labels left of the band sliders set every band at once. */
    if (inside(x, y, 42, 36, 26, 10))
        return UI_EQ_MAX;
    if (inside(x, y, 42, 63, 26, 11))
        return UI_EQ_ZERO;
    if (inside(x, y, 42, 91, 26, 11))
        return UI_EQ_MIN;
    for (i = 0; i < EQ_SLIDERS; i++) {
        if (inside(x, y, slider_x(i), SLIDER_Y, SLIDER_W, SLIDER_H)) {
            *slider = i;
            return UI_EQ_SLIDER;
        }
    }
    return y < 14 ? UI_EQ_TITLEBAR : UI_NONE;
}

float eq_slider_value(int y)
{
    float v = 1.0f - 2.0f * (float)(y - SLIDER_Y - THUMB_SIZE / 2) / (float)THUMB_TRAVEL;

    if (v > -CENTRE_SNAP && v < CENTRE_SNAP)
        return 0;       /* snap to the centre */
    return v < -1 ? -1 : v > 1 ? 1 : v;
}

/* Maps a slider value to a row of the 19px graph (0 = top = +max). */
static int graph_row(float value)
{
    int row = (int)((1.0f - value) / 2.0f * (GRAPH_H - 1) + 0.5f);

    return row < 0 ? 0 : row > GRAPH_H - 1 ? GRAPH_H - 1 : row;
}

static void draw_graph(Canvas *c, const Skin *skin, const EqModel *m)
{
    const Bitmap *sheet = &skin->sheet[SKIN_EQMAIN];
    int x, y, previous = -1;

    if (sheet->h < 313 || sheet->w < 116)
        return;         /* older skins have no graph artwork */
    skin_blit(c, skin, SKIN_EQMAIN, 0, 294, GRAPH_W, GRAPH_H, GRAPH_X, GRAPH_Y);
    skin_blit(c, skin, SKIN_EQMAIN, 0, 314, GRAPH_W, 1, GRAPH_X, GRAPH_Y + graph_row(m->sliders[0]));

    /* Response curve through the band values; each row has its own colour. */
    for (x = 0; x < GRAPH_W; x++) {
        float pos = (float)x * (AUDIO_EQ_BANDS - 1) / (GRAPH_W - 1);
        int band = (int)pos < AUDIO_EQ_BANDS - 1 ? (int)pos : AUDIO_EQ_BANDS - 2;
        float frac = pos - (float)band;
        int row = graph_row(m->sliders[1 + band] * (1 - frac) + m->sliders[2 + band] * frac);
        int from = previous < 0 ? row : previous < row ? previous + 1 : previous > row ? previous - 1 : row;
        int step = from <= row ? 1 : -1;

        for (y = from; y != row + step; y += step)
            gfx_pixel(c, GRAPH_X + x, GRAPH_Y + y, skin->eq_line[y]);
        previous = row;
    }
}

void eq_draw(uint32_t *framebuffer, int scale, const Skin *skin, const EqModel *m)
{
    Canvas canvas, *c = &canvas;
    int i, pressed = m->pressed == UI_EQ_ON;

    gfx_init(c, framebuffer, EQ_W, EQ_H, scale);
    gfx_rect(c, 0, 0, EQ_W, EQ_H, 0);
    skin_blit(c, skin, SKIN_EQMAIN, 0, 0, EQ_W, EQ_H, 0, 0);
    skin_blit(c, skin, SKIN_EQMAIN, 0, 134, EQ_W, 14, 0, 0);
    if (m->pressed == UI_EQ_CLOSE)
        skin_blit(c, skin, SKIN_EQMAIN, 0, 125, 9, 9, 264, 3);

    /* ON and AUTO toggles (off, on, off pressed, on pressed), then PRESETS */
    skin_blit(c, skin, SKIN_EQMAIN, m->on ? (pressed ? 187 : 69) : (pressed ? 128 : 10), 119, 26, 12, 14, 18);
    pressed = m->pressed == UI_EQ_AUTO;
    skin_blit(c, skin, SKIN_EQMAIN, m->auto_on ? (pressed ? 213 : 95) : (pressed ? 154 : 36), 119, 32, 12, 40, 18);
    skin_blit(c, skin, SKIN_EQMAIN, 224, m->pressed == UI_EQ_PRESETS ? 176 : 164, 44, 12, 217, 18);

    draw_graph(c, skin, m);

    for (i = 0; i < EQ_SLIDERS; i++) {
        float v = m->sliders[i];
        int frame = (int)((v + 1) / 2 * (SLIDER_FRAMES - 1) + 0.5f);
        int held = m->pressed == UI_EQ_SLIDER && m->pressed_slider == i;

        skin_blit(c, skin, SKIN_EQMAIN, 13 + (frame % 14) * 15, 164 + (frame / 14) * 65, SLIDER_W, SLIDER_H,
                 slider_x(i), SLIDER_Y);
        skin_blit(c, skin, SKIN_EQMAIN, 0, held ? 176 : 164, THUMB_SIZE, THUMB_SIZE, slider_x(i) + 1,
                 SLIDER_Y + (int)((1 - v) / 2 * THUMB_TRAVEL + 0.5f));
    }
}
