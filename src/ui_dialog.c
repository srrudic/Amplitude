/* What the small windows drawn in the built-in look have in common (jump to
 * file, about): the frame, the title bar and its close button. */
#include "ui.h"

#include "theme.h"

#define TITLE_H     DIALOG_TITLE_H
#define CLOSE_SIZE  9
#define CLOSE_Y     3
#define CAPTION_X   6

/* Left edge of the close button in a window `w` wide. */
static int close_x(int w)
{
    return w - CLOSE_SIZE - 2;
}

int dialog_hit(int w, int x, int y, int titlebar, int close)
{
    if (x >= close_x(w) && x < close_x(w) + CLOSE_SIZE && y >= CLOSE_Y && y < CLOSE_Y + CLOSE_SIZE)
        return close;
    return y < TITLE_H ? titlebar : UI_NONE;
}

void dialog_frame(Canvas *c, int w, int h, const char *caption, int close_pressed)
{
    int grip_x = CAPTION_X + gfx_text_width(caption) + CAPTION_X, x = close_x(w), i;

    gfx_rect(c, 0, 0, w, h, COL_BODY);
    gfx_bevel(c, 0, 0, w, h, COL_LIGHT, COL_DARK);

    /* Title bar with grip lines and a close button */
    gfx_rect(c, 1, 1, w - 2, TITLE_H - 1, COL_TITLEBAR);
    gfx_text(c, CAPTION_X, 4, caption, theme_get()->base);
    for (i = 0; i < 3; i++)
        gfx_hline(c, grip_x, 5 + i * 2, x - CAPTION_X - grip_x, COL_LIGHT);
    gfx_rect(c, x, CLOSE_Y, CLOSE_SIZE, CLOSE_SIZE, COL_BODY);
    gfx_bevel(c, x, CLOSE_Y, CLOSE_SIZE, CLOSE_SIZE, close_pressed ? COL_DARK : COL_LIGHT,
              close_pressed ? COL_LIGHT : COL_DARK);
    gfx_cross(c, x + 2, CLOSE_Y + 2, 5, COL_ICON);
}
