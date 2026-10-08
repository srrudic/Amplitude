/* "Open location": a box to type or paste a web address into. Drawn in the
 * built-in look whatever skin is loaded, like the jump window. */
#include "ui.h"

#include "theme.h"

#define COL_FIELD     (theme_get()->display)
#define COL_TEXT      (theme_get()->base)

#define FIELD_X   8
#define FIELD_Y   30
#define FIELD_W   (URL_W - 16)
#define FIELD_H   14
#define BUTTON_Y  (URL_H - 26)
#define BUTTON_H  18
#define OPEN_X    FIELD_X
#define OPEN_W    38
#define CANCEL_W  50
#define CANCEL_X  (FIELD_X + FIELD_W - CANCEL_W)

int url_hit(int x, int y)
{
    int element = dialog_hit(URL_W, x, y, UI_URL_TITLEBAR, UI_URL_CLOSE);

    if (element)
        return element;
    if (y >= BUTTON_Y && y < BUTTON_Y + BUTTON_H) {
        if (x >= OPEN_X && x < OPEN_X + OPEN_W)
            return UI_URL_OPEN;
        if (x >= CANCEL_X && x < CANCEL_X + CANCEL_W)
            return UI_URL_CANCEL;
    }
    return UI_NONE;
}

static void button(Canvas *c, int x, int w, const char *label, int pressed)
{
    gfx_rect(c, x, BUTTON_Y, w, BUTTON_H, COL_BODY);
    gfx_bevel(c, x, BUTTON_Y, w, BUTTON_H, pressed ? COL_DARK : COL_LIGHT, pressed ? COL_LIGHT : COL_DARK);
    gfx_text(c, x + (w + 1 - gfx_text_width(label)) / 2 + pressed, BUTTON_Y + 6 + pressed, label, COL_TEXT);
}

void url_draw(uint32_t *framebuffer, int scale, const char *text, int pressed, uint32_t ticks)
{
    Canvas canvas, *c = &canvas;
    int text_w = gfx_utext_width(scale, &font_list, text), shift;

    gfx_init(c, framebuffer, URL_W, URL_H, scale);
    dialog_frame(c, URL_W, URL_H, "OPEN LOCATION", pressed == UI_URL_CLOSE);
    gfx_utext(c, &font_list, FIELD_X, 17, "Address of a station or an audio file:", COL_TEXT);

    /* The field shows the end of an address too long for it, where typing happens. */
    gfx_rect(c, FIELD_X, FIELD_Y, FIELD_W, FIELD_H, COL_FIELD);
    gfx_bevel(c, FIELD_X, FIELD_Y, FIELD_W, FIELD_H, COL_DARK, COL_LIGHT);
    gfx_set_clip(c, FIELD_X + 2, FIELD_Y, FIELD_W - 4, FIELD_H);
    shift = text_w > FIELD_W - 12 ? text_w - (FIELD_W - 12) : 0;
    gfx_utext(c, &font_list, FIELD_X + 4 - shift, FIELD_Y + 2, text, COL_TEXT);
    if ((ticks / UI_BLINK_MS) % 2 == 0)
        gfx_vline(c, FIELD_X + 5 + text_w - shift, FIELD_Y + 3, FIELD_H - 6, COL_ICON);
    gfx_reset_clip(c);

    button(c, OPEN_X, OPEN_W, "OPEN", pressed == UI_URL_OPEN);
    button(c, CANCEL_X, CANCEL_W, "CANCEL", pressed == UI_URL_CANCEL);
}
