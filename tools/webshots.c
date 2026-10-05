/* Renders the screenshots used on the website, straight from the player's
 * own drawing code, as PPM files. tools/genwebsite.py turns them into PNGs.
 *
 *     build/webshots <output directory>          (see "make website")
 *
 * Nothing here needs a display: the windows are drawn into memory exactly
 * as the player would draw them. The track names are invented. */
#include "playlist.h"
#include "theme.h"
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BACKDROP 0x12151F

typedef struct {
    uint32_t *px;
    int w, h;
} Image;

static const char *out_dir;

static Image image_new(int w, int h)
{
    Image img = { malloc(sizeof(uint32_t) * w * h), w, h };
    int i;

    if (!img.px)
        exit(1);
    for (i = 0; i < w * h; i++)
        img.px[i] = BACKDROP;
    return img;
}

static void image_paste(Image *dst, const uint32_t *src, int w, int h, int x, int y)
{
    int row;

    for (row = 0; row < h; row++)
        memcpy(dst->px + (y + row) * dst->w + x, src + row * w, sizeof(uint32_t) * w);
}

static void image_save(Image *img, const char *name)
{
    char path[512];
    FILE *f;
    int i;

    snprintf(path, sizeof path, "%s/%s.ppm", out_dir, name);
    f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", img->w, img->h);
    for (i = 0; i < img->w * img->h; i++) {
        fputc((int)(img->px[i] >> 16 & 255), f);
        fputc((int)(img->px[i] >> 8 & 255), f);
        fputc((int)(img->px[i] & 255), f);
    }
    fclose(f);
    free(img->px);
}

/* A plausible burst of music for the visualiser: a handful of tones that
 * get quieter towards the top, as real recordings do. */
static void fake_audio(float *vis)
{
    static const float tones[][2] = {
        { 70, 0.30f }, { 150, 0.26f }, { 320, 0.20f }, { 650, 0.16f }, { 1300, 0.12f },
        { 2600, 0.09f }, { 5200, 0.06f }, { 9000, 0.04f }, { 14000, 0.02f } };
    size_t t;
    int i;

    for (i = 0; i < AUDIO_VIS_SAMPLES; i++) {
        vis[i] = 0;
        for (t = 0; t < sizeof tones / sizeof tones[0]; t++)
            vis[i] += tones[t][1] * sinf((float)i * 2.0f * 3.14159265f * tones[t][0] / 44100.0f + (float)t);
    }
}

/* The main window in mid-song. Drawn a few times so the spectrum bars and
 * their peaks settle. */
static uint32_t *draw_main(const Skin *skin, int scale, const char *title)
{
    static float vis[AUDIO_VIS_SAMPLES];
    uint32_t *px = malloc(sizeof(uint32_t) * GFX_SCALED(UI_W, scale) * GFX_SCALED(UI_H, scale));
    UiModel m;
    int i;

    fake_audio(vis);
    memset(&m, 0, sizeof m);
    m.title = title;
    m.state = AUDIO_PLAYING;
    m.loaded = 1;
    m.position = 97;
    m.length = 222;
    m.kbps = 320;
    m.khz = 44;
    m.channels = 2;
    m.volume = 0.75f;
    m.shuffle = 1;
    m.eq_visible = m.pl_visible = 1;
    m.vis = vis;
    m.vis_mode = VIS_SPECTRUM;
    for (i = 0; i < 12; i++)
        ui_draw(px, scale, skin, &m);
    return px;
}

static void add_tracks(void)
{
    static const struct { const char *path; int seconds; } tracks[] = {
        { "/music/Aurora Lane - Night Drive.flac", 251 },
        { "/music/Aurora Lane - Glass Harbour.flac", 198 },
        { "/music/Kestrel & Vine - Paper Lanterns.mp3", 233 },
        { "/music/Kestrel & Vine - Low Tide.mp3", 305 },
        { "/music/Chloé Marchand - Café de Nuit.ogg", 222 },
        { "/music/Chloé Marchand - L'été Dernier.ogg", 187 },
        { "/music/Los Faroles - Corazón de Papel.opus", 264 },
        { "/music/Halden Row - Static Bloom.m4a", 209 },
        { "/music/Halden Row - Seventeen Rooms.m4a", 342 },
        { "/music/Óskar Thal - Vetrarljós.flac", 276 },
        { "/music/Jürgen Weiß - Über den Dächern.mp3", 174 },
        { "/music/Tin Sparrow - Afterglow.mp3", 241 },
        { "/music/Second Reality - Unreal II.s3m", 318 },
    };
    size_t i;

    for (i = 0; i < sizeof tracks / sizeof tracks[0]; i++)
        playlist_set_length(playlist_add(tracks[i].path), tracks[i].seconds);
}

/* Main window and equaliser on the left, the playlist beside them. */
static void shot_player(int scale)
{
    static const float rock[EQ_SLIDERS] = {
        0.1f, 0.67f, 0.40f, -0.47f, -0.67f, -0.27f, 0.33f, 0.73f, 0.93f, 0.93f, 0.93f };
    int w = GFX_SCALED(UI_W, scale), h = GFX_SCALED(UI_H, scale);
    Image img = image_new(w * 2, h * 2);
    uint32_t *px;
    EqModel eq;
    PlModel pl;
    Skin skin;

    theme_set(THEME_DEFAULT_COLOR);
    skin_init_default(&skin);

    px = draw_main(&skin, scale, "5. Chloé Marchand - Café de Nuit (3:42)");
    image_paste(&img, px, w, h, 0, 0);
    free(px);

    memset(&eq, 0, sizeof eq);
    eq.on = 1;
    eq.auto_on = 0;
    memcpy(eq.sliders, rock, sizeof rock);
    px = malloc(sizeof(uint32_t) * w * h);
    eq_draw(px, scale, &skin, &eq);
    image_paste(&img, px, w, h, 0, h);
    free(px);

    memset(&pl, 0, sizeof pl);
    pl.current = 4;
    pl.position = 97;
    playlist_select_range(4, 4);
    pl_set_size(PL_MIN_W, 2 * UI_H);
    px = malloc(sizeof(uint32_t) * w * h * 2);
    pl_draw(px, scale, &skin, &pl);
    image_paste(&img, px, w, h * 2, w, 0);
    free(px);

    skin_free(&skin);
    image_save(&img, "player");
}

/* The main window once in each colour preset, four to a row. */
static void shot_colors(int scale)
{
    int w = GFX_SCALED(UI_W, scale), h = GFX_SCALED(UI_H, scale), gap = 12, i;
    Image img = image_new(4 * w + 3 * gap, 2 * h + gap);

    for (i = 0; i < 8 && i < theme_preset_count; i++) {
        Skin skin;
        uint32_t *px;

        theme_set(theme_presets[i].color);
        skin_init_default(&skin);
        px = draw_main(&skin, scale, theme_presets[i].name);
        image_paste(&img, px, w, h, (i % 4) * (w + gap), (i / 4) * (h + gap));
        free(px);
        skin_free(&skin);
    }
    theme_set(THEME_DEFAULT_COLOR);
    image_save(&img, "colors");
}

/* Jump to file with a search under way, and the right-click menu. */
static void shot_jump(int scale)
{
    static const MenuItem items[] = {
        { "Add files...", 1, 0, 0 }, { "Add folder...", 2, 0, 0 }, { NULL, 0, 0, 0 },
        { "Equalizer", 3, 1, 0 }, { "Playlist", 4, 1, 0 }, { NULL, 0, 0, 0 },
        { "Shuffle", 5, 1, 0 }, { "Repeat", 6, 0, 0 }, { NULL, 0, 0, 0 },
        { "Skins...", 7, 0, 0 }, { "Size...", 8, 0, 0 }, { "Color...", 9, 0, 0 }, { NULL, 0, 0, 0 },
        { "About...", 10, 0, 0 }, { "Exit", 11, 0, 0 } };
    static const int matches[] = { 4, 5 };      /* what "chloe" finds: accents are ignored */
    int count = (int)(sizeof items / sizeof items[0]), menu_w, menu_h, gap = 24;
    int jw = GFX_SCALED(JUMP_W, scale), jh = GFX_SCALED(JUMP_H, scale), mw, mh;
    JumpModel jump;
    uint32_t *px;
    Image img;

    theme_set(THEME_DEFAULT_COLOR);
    menu_measure(items, count, scale, &menu_w, &menu_h);
    mw = GFX_SCALED(menu_w, scale);
    mh = GFX_SCALED(menu_h, scale);
    img = image_new(jw + gap + mw, jh > mh ? jh : mh);

    memset(&jump, 0, sizeof jump);
    jump.query = "chloe";
    jump.matches = matches;
    jump.match_count = 2;
    jump.selected = 0;
    px = malloc(sizeof(uint32_t) * jw * jh);
    jump_draw(px, scale, &jump);
    image_paste(&img, px, jw, jh, 0, 0);
    free(px);

    px = malloc(sizeof(uint32_t) * mw * mh);
    menu_draw(px, menu_w, menu_h, scale, items, count, 11);
    image_paste(&img, px, mw, mh, jw + gap, 0);
    free(px);
    image_save(&img, "details");
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: webshots <output directory>\n");
        return 2;
    }
    out_dir = argv[1];
    add_tracks();
    shot_player(200);
    shot_colors(150);
    shot_jump(200);
    playlist_free();
    return 0;
}
