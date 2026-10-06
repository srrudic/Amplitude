/* Playlist window: a frame tiled from the classic pledit.bmp pieces around
 * a text list drawn with the built-in Unicode font in the skin's colours. */
#include "ui.h"

#include "playlist.h"

#include <stdio.h>
#include <string.h>

#define TOP_H     20
#define BOTTOM_H  38
#define LEFT_W    12
#define RIGHT_W   20
#define ROW_H     10
#define HANDLE_W  8
#define HANDLE_H  18
#define BUTTON_W  25
#define BUTTON_H  18
#define MINI_W    9         /* one small transport control */
#define MINI_H    9
#define GRIP_SIZE 20        /* resize area in the bottom right corner */
#define GRIP_SIZE_BUILTIN 12    /* smaller in the built-in look, to leave room for the magnifier above it */
#define JUMP_BUTTON_W 16
#define JUMP_BUTTON_X (pl_w - 19)   /* between LIST and the right edge */

/* Current window size; everything else is derived from it. */
static int pl_w = PL_MIN_W, pl_h = 232;

#define LIST_X    LEFT_W
#define LIST_Y    TOP_H
#define LIST_W    (pl_w - LEFT_W - RIGHT_W)
#define LIST_H    (pl_h - TOP_H - BOTTOM_H)
#define SCROLL_X  (pl_w - 15)
#define BUTTON_Y  (pl_h - PL_BUTTONS_FROM_BOTTOM)
/* The bottom strip has a fixed left part (ADD, REM, SEL, MISC) and a fixed
 * right part, 150 wide, whose contents are placed from RIGHT_X: the total
 * time, the small transport controls, the clock and LIST. The positions
 * follow the classic pledit.bmp, so skins line up. */
#define RIGHT_X   (pl_w - 150)
#define MINI_X    (RIGHT_X + 3)
#define MINI_Y    (pl_h - 16)

void pl_set_size(int w, int h)
{
    pl_w = w;
    pl_h = h;
}

static int inside(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

int pl_visible_rows(void)
{
    return LIST_H / ROW_H;
}

int pl_row_at(int y)
{
    return (y - LIST_Y - 2) / ROW_H;
}

int pl_hit(const Skin *skin, int x, int y)
{
    int builtin = skin->builtin[SKIN_PLEDIT], grip = builtin ? GRIP_SIZE_BUILTIN : GRIP_SIZE;

    if (inside(x, y, pl_w - 11, 3, 9, 9))
        return UI_PL_CLOSE;
    if (y < TOP_H)
        return UI_PL_TITLEBAR;
    if (inside(x, y, LIST_X, LIST_Y + 2, LIST_W, pl_visible_rows() * ROW_H))
        return UI_PL_LIST;
    if (inside(x, y, SCROLL_X - 2, LIST_Y, HANDLE_W + 4, LIST_H))
        return UI_PL_SCROLL;
    if (inside(x, y, pl_w - grip, pl_h - grip, grip, grip))
        return UI_PL_RESIZE;
    /* Classic skins have no picture for the magnifier, so no button either. */
    if (builtin && inside(x, y, JUMP_BUTTON_X, BUTTON_Y, JUMP_BUTTON_W, BUTTON_H))
        return UI_PL_JUMP;
    if (inside(x, y, PL_BUTTON_X(0), BUTTON_Y, 4 * PL_BUTTON_PITCH, BUTTON_H) &&
        (x - PL_BUTTON_X(0)) % PL_BUTTON_PITCH < BUTTON_W)
        return UI_PL_ADD + (x - PL_BUTTON_X(0)) / PL_BUTTON_PITCH;       /* ADD, REM, SEL, MISC */
    if (inside(x, y, pl_list_button_x(), BUTTON_Y, BUTTON_W, BUTTON_H))
        return UI_PL_LISTOPTS;
    if (inside(x, y, MINI_X, MINI_Y, 6 * MINI_W, MINI_H))
        return UI_PL_PREV + (x - MINI_X) / MINI_W;          /* prev, play, pause, stop, next, open */
    return UI_NONE;
}

int pl_list_button_x(void)
{
    return RIGHT_X + 104;
}

static int max_scroll(int count)
{
    return count > pl_visible_rows() ? count - pl_visible_rows() : 0;
}

int pl_scroll_at(int y, int count)
{
    int travel = LIST_H - HANDLE_H;
    int pos = y - LIST_Y - HANDLE_H / 2;

    if (travel <= 0)
        return 0;
    pos = pos < 0 ? 0 : pos > travel ? travel : pos;
    return (pos * max_scroll(count) + travel / 2) / travel;
}

static void draw_frame(Canvas *c, const Skin *skin)
{
    int x, y;

    /* Top: corners, filler tiles, then the caption centred over them */
    skin_blit(c, skin, SKIN_PLEDIT, 0, 0, 25, TOP_H, 0, 0);
    for (x = 25; x < pl_w - 25; x += 25)
        skin_blit(c, skin, SKIN_PLEDIT, 127, 0, 25, TOP_H, x, 0);
    skin_blit(c, skin, SKIN_PLEDIT, 26, 0, 100, TOP_H, (pl_w - 100) / 2, 0);
    skin_blit(c, skin, SKIN_PLEDIT, 153, 0, 25, TOP_H, pl_w - 25, 0);

    gfx_set_clip(c, 0, TOP_H, pl_w, LIST_H);
    for (y = TOP_H; y < pl_h - BOTTOM_H; y += 29) {
        skin_blit(c, skin, SKIN_PLEDIT, 0, 42, LEFT_W, 29, 0, y);
        skin_blit(c, skin, SKIN_PLEDIT, 31, 42, RIGHT_W, 29, pl_w - RIGHT_W, y);
    }
    gfx_reset_clip(c);

    /* Bottom: fixed left and right parts, filler in between on wider windows */
    skin_blit(c, skin, SKIN_PLEDIT, 0, 72, 125, BOTTOM_H, 0, pl_h - BOTTOM_H);
    for (x = 125; x < pl_w - 150; x += 25)
        skin_blit(c, skin, SKIN_PLEDIT, 179, 0, 25, BOTTOM_H, x, pl_h - BOTTOM_H);
    skin_blit(c, skin, SKIN_PLEDIT, 126, 72, 150, BOTTOM_H, pl_w - 150, pl_h - BOTTOM_H);
}

/* "3:42", or "1:02:05" from an hour up. */
static void format_time(char *out, size_t size, long seconds)
{
    if (seconds >= 3600)
        snprintf(out, size, "%ld:%02ld:%02ld", seconds / 3600, seconds / 60 % 60, seconds % 60);
    else
        snprintf(out, size, "%ld:%02ld", seconds / 60, seconds % 60);
}

/* Length of the selected tracks over the length of the whole list, and the
 * clock of the current track. A "+" marks a total that leaves out tracks
 * whose length is not known yet. */
static void draw_times(Canvas *c, const Skin *skin, const PlModel *m)
{
    char selected[24], total[24], text[64];
    long selected_s = 0, total_s = 0;
    int i, unknown = 0, selected_unknown = 0;

    for (i = 0; i < playlist_count(); i++) {
        const Track *track = playlist_get(i);

        if (track->length < 0) {
            unknown = 1;
            selected_unknown |= track->selected;
            continue;
        }
        total_s += track->length;
        if (track->selected)
            selected_s += track->length;
    }
    format_time(selected, sizeof selected, selected_s);
    format_time(total, sizeof total, total_s);
    snprintf(text, sizeof text, "%s%s/%s%s", selected, selected_unknown ? "+" : "", total, unknown ? "+" : "");
    gfx_set_clip(c, RIGHT_X + 7, pl_h - 28, 90, 9);
    gfx_utext(c, &font_small, RIGHT_X + 9, pl_h - 27, text, skin->pl_normal);
    gfx_reset_clip(c);

    if (m->position >= 0) {
        snprintf(text, sizeof text, "%02d:%02d", m->position / 60 % 100, m->position % 60);
        gfx_utext(c, &font_small, RIGHT_X + 66, pl_h - 15, text, skin->pl_normal);
    }
}

void pl_draw(uint32_t *framebuffer, int scale, const Skin *skin, const PlModel *m)
{
    Canvas canvas, *c = &canvas;
    int count = playlist_count(), row, travel = LIST_H - HANDLE_H;
    int limit = max_scroll(count);

    gfx_init(c, framebuffer, pl_w, pl_h, scale);
    gfx_rect(c, 0, 0, pl_w, pl_h, 0);
    draw_frame(c, skin);
    draw_times(c, skin, m);
    if (m->pressed == UI_PL_CLOSE)
        skin_blit(c, skin, SKIN_PLEDIT, 52, 42, 9, 9, pl_w - 11, 3);
    if (travel >= 0)
        skin_blit(c, skin, SKIN_PLEDIT, m->pressed == UI_PL_SCROLL ? 61 : 52, 53, HANDLE_W, HANDLE_H, SCROLL_X,
                 LIST_Y + (limit ? m->scroll * travel / limit : 0));

    gfx_rect(c, LIST_X, LIST_Y, LIST_W, LIST_H, skin->pl_background);
    gfx_set_clip(c, LIST_X, LIST_Y, LIST_W, LIST_H);
    for (row = 0; row < pl_visible_rows(); row++) {
        int index = m->scroll + row, y = LIST_Y + 2 + row * ROW_H;
        const Track *track = playlist_get(index);
        uint32_t color = index == m->current ? skin->pl_current : skin->pl_normal;
        char text[640], time[32] = "";
        int time_w, used = 0;

        if (!track)
            break;
        if (track->selected)
            gfx_rect(c, LIST_X, y, LIST_W, ROW_H, skin->pl_selected);
        /* A queued track shows its place before its length: "[2] 3:42" */
        if (track->queue)
            used = snprintf(time, sizeof time, "[%d] ", track->queue);
        if (track->length >= 0)
            snprintf(time + used, sizeof time - (size_t)used, "%d:%02d", track->length / 60, track->length % 60);
        time_w = gfx_utext_width(c->scale, &font_list, time);
        gfx_utext(c, &font_list, LIST_X + LIST_W - 2 - time_w, y, time, color);

        /* The title is cut off where the duration column begins. */
        snprintf(text, sizeof text, "%d. %s", index + 1, track->title);
        gfx_set_clip(c, LIST_X, LIST_Y, LIST_W - 8 - time_w, LIST_H);
        gfx_utext(c, &font_list, LIST_X + 2, y, text, color);
        gfx_set_clip(c, LIST_X, LIST_Y, LIST_W, LIST_H);
    }
    gfx_reset_clip(c);
}
