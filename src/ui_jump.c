/* "Jump to file": a search box over the playlist. Drawn in the built-in
 * look whatever skin is loaded, like the popup menu. */
#include "ui.h"

#include "playlist.h"
#include "theme.h"

#include <stdio.h>
#include <string.h>

#define COL_FIELD     (theme_get()->display)
#define COL_TEXT      (theme_get()->base)
#define COL_HOVER     (theme_get()->highlight)

#define FIELD_X   8
#define FIELD_Y   20
#define FIELD_W   (JUMP_W - 16)
#define FIELD_H   14
#define LIST_Y    38
#define ROW_H     10

int jump_hit(int x, int y)
{
    int element = dialog_hit(JUMP_W, x, y, UI_JUMP_TITLEBAR, UI_JUMP_CLOSE);

    if (element)
        return element;
    if (x >= FIELD_X && x < FIELD_X + FIELD_W && y >= LIST_Y && y < LIST_Y + JUMP_ROWS * ROW_H)
        return UI_JUMP_LIST;
    return UI_NONE;
}

int jump_row_at(int y)
{
    return (y - LIST_Y) / ROW_H;
}

void jump_draw(uint32_t *framebuffer, int scale, const JumpModel *m)
{
    Canvas canvas, *c = &canvas;
    int row, query_w;

    gfx_init(c, framebuffer, JUMP_W, JUMP_H, scale);
    dialog_frame(c, JUMP_W, JUMP_H, "JUMP TO FILE", m->pressed == UI_JUMP_CLOSE);

    /* Search field with a blinking cursor after the text */
    gfx_rect(c, FIELD_X, FIELD_Y, FIELD_W, FIELD_H, COL_FIELD);
    gfx_bevel(c, FIELD_X, FIELD_Y, FIELD_W, FIELD_H, COL_DARK, COL_LIGHT);
    gfx_set_clip(c, FIELD_X + 2, FIELD_Y, FIELD_W - 4, FIELD_H);
    query_w = gfx_utext_width(scale, &font_list, m->query);
    gfx_utext(c, &font_list, FIELD_X + 4, FIELD_Y + 2, m->query, COL_TEXT);
    if ((m->ticks / UI_BLINK_MS) % 2 == 0)
        gfx_vline(c, FIELD_X + 5 + query_w, FIELD_Y + 3, FIELD_H - 6, COL_ICON);
    gfx_reset_clip(c);

    /* Matching tracks */
    gfx_rect(c, FIELD_X, LIST_Y, FIELD_W, JUMP_ROWS * ROW_H + 2, COL_FIELD);
    gfx_set_clip(c, FIELD_X, LIST_Y, FIELD_W, JUMP_ROWS * ROW_H + 2);
    for (row = 0; row < JUMP_ROWS; row++) {
        int index = m->scroll + row, y = LIST_Y + 1 + row * ROW_H;
        const Track *track = index < m->match_count ? playlist_get(m->matches[index]) : NULL;
        char text[640];

        if (!track)
            break;
        if (index == m->selected)
            gfx_rect(c, FIELD_X, y, FIELD_W, ROW_H, COL_HOVER);
        snprintf(text, sizeof text, "%d. %s", m->matches[index] + 1, track->title);
        gfx_utext(c, &font_list, FIELD_X + 3, y, text, COL_TEXT);
        if (track->queue) {     /* its place in the play queue, at the right edge */
            int mark_w;

            snprintf(text, sizeof text, "[%d]", track->queue);
            mark_w = gfx_utext_width(scale, &font_list, text);
            gfx_rect(c, FIELD_X + FIELD_W - mark_w - 6, y, mark_w + 6, ROW_H,
                     index == m->selected ? COL_HOVER : COL_FIELD);
            gfx_utext(c, &font_list, FIELD_X + FIELD_W - mark_w - 3, y, text, COL_TEXT);
        }
    }
    gfx_reset_clip(c);
}
