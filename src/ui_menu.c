/* Popup menu: a plain list in the built-in look, drawn by us so it works
 * the same on every platform. */
#include "ui.h"

#include "theme.h"

#include <string.h>

#define COL_TEXT   (theme_get()->base)
#define COL_HOVER  (theme_get()->highlight)
#define COL_CHECK  (theme_get()->base)

#define ITEM_H      13
#define SEPARATOR_H 5
#define PAD_LEFT    14      /* room for the check mark */
#define PAD_RIGHT   8
#define MIN_W       110

static int item_height(const MenuItem *item)
{
    return item->label ? ITEM_H : SEPARATOR_H;
}

void menu_measure(const MenuItem *items, int count, int scale, int *w, int *h)
{
    int i;

    *w = MIN_W;
    *h = 4;
    for (i = 0; i < count; i++) {
        int width = items[i].label ? PAD_LEFT + gfx_utext_width(scale, &font_list, items[i].label) + PAD_RIGHT : 0;

        if (items[i].swatch)
            width += 14;

        if (width > *w)
            *w = width;
        *h += item_height(&items[i]);
    }
}

int menu_item_at(const MenuItem *items, int count, int w, int x, int y)
{
    int i, top = 2;

    if (x < 0 || x >= w)
        return -1;
    for (i = 0; i < count; i++) {
        int bottom = top + item_height(&items[i]);

        if (y >= top && y < bottom)
            return items[i].label ? i : -1;
        top = bottom;
    }
    return -1;
}

void menu_draw(uint32_t *framebuffer, int w, int h, int scale, const MenuItem *items, int count, int hover)
{
    Canvas canvas, *c = &canvas;
    int i, y = 2;

    gfx_init(c, framebuffer, w, h, scale);
    gfx_rect(c, 0, 0, w, h, COL_BODY);
    gfx_bevel(c, 0, 0, w, h, COL_LIGHT, COL_DARK);
    for (i = 0; i < count; i++) {
        if (!items[i].label) {
            gfx_hline(c, 4, y + SEPARATOR_H / 2, w - 8, COL_DARK);
        } else {
            if (i == hover)
                gfx_rect(c, 2, y, w - 4, ITEM_H, COL_HOVER);
            if (items[i].checked)
                gfx_rect(c, 5, y + 4, 5, 5, COL_CHECK);
            gfx_utext(c, &font_list, PAD_LEFT, y + 2, items[i].label, COL_TEXT);
            if (items[i].swatch) {      /* colour sample at the right edge */
                gfx_rect(c, w - 16, y + 3, 10, 7, items[i].swatch - 1);
                gfx_bevel(c, w - 16, y + 3, 10, 7, COL_DARK, COL_LIGHT);
            }
        }
        y += item_height(&items[i]);
    }
}
