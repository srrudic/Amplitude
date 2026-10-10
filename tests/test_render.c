/* Draws every window at many magnifications. The checks are structural
 * (every pixel written, nothing outside the buffer); run with a directory
 * argument to also save the images as PPM files for looking at:
 *
 *     build/test/test_render tests/media /tmp/renders
 */
#include "playlist.h"
#include "theme.h"
#include "ui.h"
#include "test.h"

#define SENTINEL 0xDEADBEEFu
#define GUARD    256

static const char *dump_dir;

static uint32_t *make_buffer(int w, int h, int scale)
{
    size_t n = (size_t)GFX_SCALED(w, scale) * GFX_SCALED(h, scale) + GUARD, i;
    uint32_t *px = malloc(sizeof *px * n);

    for (i = 0; i < n; i++)
        px[i] = SENTINEL;
    return px;
}

/* Every pixel of the window must have been drawn, and none after it. */
static void finish(uint32_t *px, int w, int h, int scale, const char *name)
{
    int rw = GFX_SCALED(w, scale), rh = GFX_SCALED(h, scale), unwritten = 0, overrun = 0, i;

    for (i = 0; i < rw * rh; i++)
        unwritten += px[i] == SENTINEL;
    for (i = rw * rh; i < rw * rh + GUARD; i++)
        overrun += px[i] != SENTINEL;
    CHECK_INT(unwritten, 0);
    CHECK_INT(overrun, 0);

    if (dump_dir) {
        char path[512];
        FILE *f;

        snprintf(path, sizeof path, "%s/%s-%d.ppm", dump_dir, name, scale);
        f = fopen(path, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", rw, rh);
            for (i = 0; i < rw * rh; i++) {
                fputc((int)(px[i] >> 16 & 255), f);
                fputc((int)(px[i] >> 8 & 255), f);
                fputc((int)(px[i] & 255), f);
            }
            fclose(f);
        }
    }
    free(px);
}

/* The built-in skin remembers the sprites it has painted. Whatever it copies
 * from memory must be, pixel for pixel, what painting again would give: in
 * the frame that fills the cache (where repeated pieces are already reused)
 * and in the frames after it. */
static void test_sprite_cache(const Skin *skin, int scale)
{
    static float vis[AUDIO_VIS_SAMPLES];
    int sizes[3][2] = { { UI_W, UI_H }, { EQ_W, EQ_H }, { PL_MIN_W + 3 * PL_STEP_W, PL_MIN_H + 2 * PL_STEP_H } };
    UiModel ui;
    EqModel eq;
    PlModel pl;
    int win, pass;

    memset(&ui, 0, sizeof ui);
    ui.title = "cache";
    ui.state = AUDIO_PLAYING;
    ui.loaded = ui.shuffle = ui.eq_visible = 1;
    ui.length = 100;
    ui.position = 40;
    ui.volume = 0.5f;
    ui.vis = vis;
    memset(&eq, 0, sizeof eq);
    eq.on = 1;
    memset(&pl, 0, sizeof pl);
    pl_set_size(sizes[2][0], sizes[2][1]);

    for (win = 0; win < 3; win++) {
        size_t bytes = sizeof(uint32_t) * (size_t)GFX_SCALED(sizes[win][0], scale) * GFX_SCALED(sizes[win][1], scale);
        uint32_t *frames[3];

        /* 0: everything painted; 1: the cache being filled; 2: all from the cache */
        for (pass = 0; pass < 3; pass++) {
            frames[pass] = malloc(bytes);
            skin_cache_disabled = pass == 0;
            if (pass == 1)
                skin_cache_clear();
            if (win == 0)
                ui_draw(frames[pass], scale, skin, &ui);
            else if (win == 1)
                eq_draw(frames[pass], scale, skin, &eq);
            else
                pl_draw(frames[pass], scale, skin, &pl);
        }
        CHECK(memcmp(frames[0], frames[1], bytes) == 0);
        CHECK(memcmp(frames[0], frames[2], bytes) == 0);
        for (pass = 0; pass < 3; pass++)
            free(frames[pass]);
    }
    skin_cache_disabled = 0;
}

static void render_all(const Skin *skin, int scale, const char *prefix)
{
    static const MenuItem items[] = {
        { "Add files...", 1, 0, 0 }, { NULL, 0, 0, 0 }, { "Equalizer", 2, 1, 0 },
        { "Skin: Ünïcödé Имя", 3, 0, 0xFFB347 + 1 } };
    static const int matches[] = { 0, 2, 3 };
    static float vis[AUDIO_VIS_SAMPLES];
    UiModel ui;
    EqModel eq;
    PlModel pl;
    JumpModel jump;
    uint32_t *px;
    char name[64];
    int i, state, menu_w, menu_h;

    for (i = 0; i < AUDIO_VIS_SAMPLES; i++)
        vis[i] = 0.6f * sinf((float)i * 0.05f) + 0.3f * sinf((float)i * 0.7f);

    /* Main window in each playback state and visualiser mode. */
    for (state = 0; state < 3; state++) {
        memset(&ui, 0, sizeof ui);
        ui.title = state ? "7. Đorđe Balašević - Život je more, a very long title that scrolls (3:58)" : "AMPLITUDE";
        ui.state = state;
        ui.loaded = state != AUDIO_STOPPED;
        ui.position = 83;
        ui.length = 240;
        ui.kbps = 192;
        ui.khz = 44;
        ui.channels = state;
        ui.volume = 0.5f * (float)state;
        ui.balance = (float)state - 1.0f;
        ui.shuffle = state & 1;
        ui.repeat = state >> 1;
        ui.eq_visible = state & 1;
        ui.pl_visible = state >> 1;
        ui.pressed = UI_PLAY + state;
        ui.ticks = 12345u * (unsigned)state;
        ui.vis = vis;
        ui.vis_mode = state;
        px = make_buffer(UI_W, UI_H, scale);
        ui_draw(px, scale, skin, &ui);
        snprintf(name, sizeof name, "%s-main%d", prefix, state);
        finish(px, UI_W, UI_H, scale, name);
    }

    memset(&eq, 0, sizeof eq);
    eq.on = eq.auto_on = 1;
    for (i = 0; i < EQ_SLIDERS; i++)
        eq.sliders[i] = sinf((float)i * 0.7f);
    eq.sliders[3] = 1;
    eq.sliders[4] = -1;
    eq.pressed = UI_EQ_SLIDER;
    eq.pressed_slider = 4;
    px = make_buffer(EQ_W, EQ_H, scale);
    eq_draw(px, scale, skin, &eq);
    snprintf(name, sizeof name, "%s-eq", prefix);
    finish(px, EQ_W, EQ_H, scale, name);

    /* Playlist at its smallest, at an in-between size and at a large one. */
    for (i = 0; i < 3; i++) {
        int w = PL_MIN_W + i * 3 * PL_STEP_W, h = PL_MIN_H + i * 4 * PL_STEP_H;

        memset(&pl, 0, sizeof pl);
        pl.current = 1;
        pl.scroll = i;
        pl.pressed = i ? UI_PL_SCROLL : UI_NONE;
        pl_set_size(w, h);
        px = make_buffer(w, h, scale);
        pl_draw(px, scale, skin, &pl);
        snprintf(name, sizeof name, "%s-playlist%d", prefix, i);
        finish(px, w, h, scale, name);
    }

    memset(&jump, 0, sizeof jump);
    jump.query = "bala šećer";
    jump.matches = matches;
    jump.match_count = 3;
    jump.selected = 1;
    px = make_buffer(JUMP_W, JUMP_H, scale);
    jump_draw(px, scale, &jump);
    snprintf(name, sizeof name, "%s-jump", prefix);
    finish(px, JUMP_W, JUMP_H, scale, name);

    menu_measure(items, 4, scale, &menu_w, &menu_h);
    CHECK(menu_w >= 100 && menu_h > 30);
    CHECK_INT(menu_item_at(items, 4, menu_w, 10, 5), 0);
    CHECK_INT(menu_item_at(items, 4, menu_w, 10, 17), -1);     /* the separator */
    CHECK_INT(menu_item_at(items, 4, menu_w, 10, 25), 2);
    CHECK_INT(menu_item_at(items, 4, menu_w, -1, 5), -1);
    px = make_buffer(menu_w, menu_h, scale);
    menu_draw(px, menu_w, menu_h, scale, items, 4, 2);
    snprintf(name, sizeof name, "%s-menu", prefix);
    finish(px, menu_w, menu_h, scale, name);
}

/* Hit testing: every control answers at its centre, in both layouts. */
/* Painting only what changed must give the picture that painting everything
 * gives. A long run of frames in which things change the way they do in
 * use (time passing, the title scrolling, the oscilloscope moving, buttons
 * pressed, sliders moved, tracks and titles changing) is drawn both ways
 * and compared frame by frame. (With the oscilloscope, not the spectrum:
 * the spectrum's bars depend on how often they have been drawn.) */
static void test_incremental(const Skin *skin, int scale)
{
    static const char *const titles[] = {
        "1. Short (0:10)", "2. A Title That Is Far Too Long For The Display And So Has To Scroll Along (3:45)",
        "3. \xD0\x9A\xD0\xB8\xD0\xBD\xD0\xBE - \xD0\x93\xD1\x80\xD1\x83\xD0\xBF\xD0\xBF\xD0\xB0 \xD0\xBA\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB8 and on and on and on and on (4:44)",
        "VOLUME: 67%", "" };
    static float vis[AUDIO_VIS_SAMPLES];
    size_t size = sizeof(uint32_t) * (size_t)GFX_SCALED(UI_W, scale) * (size_t)GFX_SCALED(UI_H, scale);
    uint32_t *full = malloc(size), *kept = malloc(size);
    unsigned seed = 12345u + (unsigned)scale;
    int frame, wrong = 0, partial = 0, top, bottom, i;
    UiModel m;

    memset(&m, 0, sizeof m);
    memset(kept, 0x55, size);       /* rubbish: the first frame must replace all of it */
    m.title = titles[1];
    m.vis = vis;
    m.vis_mode = VIS_SCOPE;
    m.volume = 0.5f;
    m.length = 200;
    ui_invalidate();
    for (frame = 0; frame < 600; frame++) {
        seed = seed * 1103515245u + 12345u;
        m.ticks += 7 + (seed >> 16) % 60;
        if (m.state == AUDIO_PLAYING)
            m.position += 0.04;
        for (i = 0; i < AUDIO_VIS_SAMPLES; i++)
            vis[i] = m.state == AUDIO_PLAYING ? (float)((seed >> 8) + (unsigned)i * 37u) / 4294967296.0f - 0.5f : 0;
        switch ((seed >> 20) % 40) {    /* now and then, something else changes */
        case 0: m.title = titles[(seed >> 8) % 5]; break;
        case 1: m.state = (int)((seed >> 8) % 3); break;
        case 2: m.pressed = (seed >> 8) % 2 ? UI_PLAY : UI_NONE; break;
        case 3: m.volume = (float)((seed >> 8) % 100) / 100.0f; break;
        case 4: m.loaded = !m.loaded; m.kbps = 128; m.khz = 44; m.channels = 2; break;
        case 5: m.position = (double)((seed >> 8) % 200); break;
        case 6: m.shuffle = !m.shuffle; break;
        case 7: m.vis_mode = (seed >> 8) % 2 ? VIS_SCOPE : VIS_OFF; break;
        case 8: m.pressed = (seed >> 8) % 2 ? UI_SEEK : UI_NONE; break;
        case 9: m.balance = (float)((seed >> 8) % 200) / 100.0f - 1.0f; break;
        default: break;
        }
        ui_update(kept, scale, skin, &m, &top, &bottom);
        ui_draw(full, scale, skin, &m);
        wrong += memcmp(kept, full, size) != 0;
        partial += bottom - top < GFX_SCALED(UI_H, scale);
    }
    CHECK_INT(wrong, 0);
    CHECK(partial > 400);           /* and most frames were indeed painted in part */
    /* Told that the framebuffer is lost, everything is painted again. */
    memset(kept, 0xAA, size);
    ui_invalidate();
    ui_update(kept, scale, skin, &m, &top, &bottom);
    ui_draw(full, scale, skin, &m);
    CHECK(memcmp(kept, full, size) == 0 && top == 0 && bottom == GFX_SCALED(UI_H, scale));
    free(full);
    free(kept);
}

static void test_hit_testing(const Skin *skin)
{
    int slider = -1;

    CHECK_INT(ui_hit(skin, 100, 5), UI_TITLEBAR);
    CHECK_INT(ui_hit(skin, 268, 7), UI_CLOSE);
    CHECK_INT(ui_hit(skin, 258, 7), UI_MINIMIZE);       /* built-in: next to close */
    CHECK_INT(ui_hit(skin, 248, 7), UI_TITLEBAR);
    CHECK_INT(ui_hit(skin, 50, 97), UI_PLAY);
    CHECK_INT(ui_hit(skin, 146, 97), UI_OPEN);
    CHECK_INT(ui_hit(skin, 175, 97), UI_SHUFFLE);       /* built-in: small icon buttons */
    CHECK_INT(ui_hit(skin, 197, 97), UI_REPEAT);
    CHECK_INT(ui_hit(skin, 211, 97), UI_NONE);
    CHECK_INT(ui_hit(skin, 225, 97), UI_MENU);
    CHECK_INT(ui_hit(skin, 255, 96), UI_LOGO);
    {
        /* With a classic main window the emblem is smaller and elsewhere. */
        Skin classic = *skin;

        classic.builtin[SKIN_MAIN] = 0;
        CHECK_INT(ui_hit(&classic, 259, 98), UI_LOGO);
        CHECK_INT(ui_hit(&classic, 245, 98), UI_NONE);
    }
    CHECK_INT(about_hit(ABOUT_W - 6, 7), UI_ABOUT_CLOSE);
    CHECK_INT(about_hit(100, 7), UI_ABOUT_TITLEBAR);
    CHECK_INT(about_hit(100, 60), UI_NONE);
    CHECK_INT(about_hit(60, 127), UI_ABOUT_LINK);
    CHECK_INT(ui_hit(skin, 230, 64), UI_EQ_TOGGLE);
    CHECK_INT(ui_hit(skin, 60, 50), UI_VIS);
    CHECK_INT(ui_hit(skin, 140, 76), UI_SEEK);
    CHECK_INT(ui_hit(skin, 200, 110), UI_NONE);
    CHECK(ui_slider_value(UI_VOLUME, 0) == 0.0f && ui_slider_value(UI_VOLUME, 500) == 1.0f);

    CHECK_INT(eq_hit(20, 24, &slider), UI_EQ_ON);
    CHECK_INT(eq_hit(50, 24, &slider), UI_EQ_AUTO);
    CHECK_INT(eq_hit(230, 24, &slider), UI_EQ_PRESETS);
    CHECK_INT(eq_hit(50, 68, &slider), UI_EQ_ZERO);
    CHECK_INT(eq_hit(25, 60, &slider), UI_EQ_SLIDER);
    CHECK_INT(slider, 0);
    CHECK_INT(eq_hit(78 + 9 * 18 + 5, 60, &slider), UI_EQ_SLIDER);
    CHECK_INT(slider, 10);
    CHECK(eq_slider_value(0) == 1.0f && eq_slider_value(500) == -1.0f && eq_slider_value(69) == 0.0f);

    pl_set_size(PL_MIN_W, 232);
    CHECK_INT(pl_hit(skin, 100, 10), UI_PL_TITLEBAR);
    CHECK_INT(pl_hit(skin, 100, 60), UI_PL_LIST);
    CHECK_INT(pl_hit(skin, 24, 210), UI_PL_ADD);
    CHECK_INT(pl_hit(skin, 54, 210), UI_PL_REMOVE);
    CHECK_INT(pl_hit(skin, 83, 210), UI_PL_SELECT);
    CHECK_INT(pl_hit(skin, 112, 210), UI_PL_MISC);
    CHECK_INT(pl_hit(skin, 39, 210), UI_NONE);                /* the gap between ADD and REM */
    CHECK_INT(pl_hit(skin, pl_list_button_x() + 10, 210), UI_PL_LISTOPTS);
    CHECK_INT(pl_hit(skin, 129, 220), UI_PL_PREV);            /* small controls, 9 wide each */
    CHECK_INT(pl_hit(skin, 138, 220), UI_PL_PLAY);
    CHECK_INT(pl_hit(skin, 147, 220), UI_PL_PAUSE);
    CHECK_INT(pl_hit(skin, 156, 220), UI_PL_STOP);
    CHECK_INT(pl_hit(skin, 165, 220), UI_PL_NEXT);
    CHECK_INT(pl_hit(skin, 174, 220), UI_PL_OPEN);
    CHECK_INT(pl_hit(skin, 183, 220), UI_NONE);
    CHECK_INT(pl_hit(skin, 270, 228), UI_PL_RESIZE);
    CHECK_INT(pl_hit(skin, 264, 210), UI_PL_JUMP);      /* the magnifier, right of LIST */
    CHECK_INT(pl_hit(skin, 258, 225), UI_NONE);         /* below it, left of the grip */
    {
        /* A classic playlist has no magnifier, and its larger resize corner. */
        Skin classic = *skin;

        classic.builtin[SKIN_PLEDIT] = 0;
        CHECK_INT(pl_hit(&classic, 264, 207), UI_NONE);
        CHECK_INT(pl_hit(&classic, 258, 225), UI_PL_RESIZE);
    }
    CHECK_INT(pl_visible_rows(), 17);
    CHECK_INT(pl_row_at(22), 0);
    CHECK_INT(pl_row_at(45), 2);
    CHECK_INT(pl_scroll_at(0, 100), 0);
    CHECK_INT(pl_scroll_at(1000, 100), 83);

    CHECK_INT(jump_hit(268, 7), UI_JUMP_CLOSE);
    CHECK_INT(jump_hit(100, 7), UI_JUMP_TITLEBAR);
    CHECK_INT(jump_hit(100, 60), UI_JUMP_LIST);
    CHECK_INT(jump_hit(100, 190), UI_JUMP_LIST);        /* the sixteenth row */
    CHECK_INT(jump_hit(20, JUMP_H - 17), UI_JUMP_PLAY);
    CHECK_INT(jump_hit(70, JUMP_H - 17), UI_JUMP_ENQUEUE);
    CHECK_INT(jump_hit(200, JUMP_H - 17), UI_NONE);
    CHECK_INT(jump_hit(JUMP_W - 20, JUMP_H - 17), UI_JUMP_DISMISS);
    CHECK_INT(jump_row_at(60), 2);
}

int main(int argc, char **argv)
{
    static const int scales[] = { 100, 125, 133, 150, 187, 200, 250, 300, 400 };
    Skin skin;
    size_t i;

    dump_dir = argc > 2 ? argv[2] : NULL;
    test_begin("render");
    playlist_add("/music/Björk - Jóga.mp3");
    playlist_add("/music/Đorđe Balašević - Život je more.mp3");
    playlist_add("/music/Кино - Группа крови.mp3");
    playlist_add("/music/坂本龍一 - no glyphs for this.mp3");
    for (i = 4; i < 60; i++)
        playlist_add("/music/Filler track with a fairly long name to overflow the row.flac");
    playlist_set_length(1, 238);
    playlist_select_range(1, 3);

    skin_init_default(&skin);
    test_hit_testing(&skin);
    for (i = 0; i < sizeof scales / sizeof scales[0]; i++)
        render_all(&skin, scales[i], "builtin");
    for (i = 0; i < sizeof scales / sizeof scales[0]; i++)
        test_sprite_cache(&skin, scales[i]);
    for (i = 0; i < sizeof scales / sizeof scales[0]; i++)
        test_incremental(&skin, scales[i]);
    skin_free(&skin);

    /* The same in another colour: the skin must pick the new shades up. */
    theme_set(0xFFB347);
    skin_init_default(&skin);
    CHECK(skin.text_color == 0xFFB347 && skin.pl_normal == 0xFFB347);
    render_all(&skin, 187, "amber");
    skin_free(&skin);

    playlist_free();
    return test_end();
}
