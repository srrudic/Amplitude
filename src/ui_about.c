/* The About window: logo, version, authorship and licence. Drawn in the
 * built-in look whatever skin is loaded, like the popup menu. */
#include "ui.h"

#include "theme.h"

#ifndef AMPLITUDE_VERSION       /* set by the Makefile */
#define AMPLITUDE_VERSION "dev"
#endif

#define COL_TEXT      (theme_get()->base)
#define COL_LINK      (theme_get()->text_on)

#define TEXT_X    104       /* to the right of the logo */
#define LINK_X    16
#define LINK_Y    122
#define LINK_W    ((int)(sizeof ABOUT_URL - 1) * 6)

int about_hit(int x, int y)
{
    int element = dialog_hit(ABOUT_W, x, y, UI_ABOUT_TITLEBAR, UI_ABOUT_CLOSE);

    if (element)
        return element;
    if (x >= LINK_X && x < LINK_X + LINK_W && y >= LINK_Y && y < LINK_Y + 11)
        return UI_ABOUT_LINK;
    return UI_NONE;
}

void about_draw(uint32_t *framebuffer, int scale, int pressed)
{
    Canvas canvas, *c = &canvas;

    gfx_init(c, framebuffer, ABOUT_W, ABOUT_H, scale);
    dialog_frame(c, ABOUT_W, ABOUT_H, "ABOUT", pressed == UI_ABOUT_CLOSE);

    skin_draw_logo(c, 16, 27, 44);
    gfx_text(c, TEXT_X, 26, "AMPLITUDE PLAYER", COL_TEXT);
    gfx_utext(c, &font_list, TEXT_X, 37, "Version " AMPLITUDE_VERSION, COL_TEXT);
    gfx_utext(c, &font_list, TEXT_X, 53, "Inspired by the past.", COL_TEXT);
    gfx_utext(c, &font_list, TEXT_X, 63, "Built for the future.", COL_TEXT);

    gfx_hline(c, 12, 82, ABOUT_W - 24, COL_DARK);
    gfx_hline(c, 12, 83, ABOUT_W - 24, COL_LIGHT);
    gfx_utext(c, &font_list, 16, 89, "\xC2\xA9 2026 Sr\xC4\x91" "an Rudi\xC4\x87", COL_TEXT);
    gfx_utext(c, &font_list, 16, 100, "Free software: GPL version 3 or later.", COL_TEXT);
    gfx_utext(c, &font_list, 16, 111, "Provided as is, without warranty.", COL_TEXT);
    /* The address is underlined, as a link: a click opens it. */
    gfx_utext(c, &font_list, LINK_X, LINK_Y, ABOUT_URL, COL_LINK);
    gfx_hline(c, LINK_X, LINK_Y + 10, LINK_W, COL_LINK);
}
