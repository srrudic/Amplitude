/* Drawing primitives: coordinate mapping, clipping, text metrics. */
#include "gfx.h"
#include "test.h"

#define SENTINEL 0xDEADBEEFu

static const int scales[] = { 100, 125, 133, 150, 175, 187, 200, 250, 300, 400 };
#define SCALE_COUNT ((int)(sizeof scales / sizeof scales[0]))

/* Tiling a canvas with touching rectangles must cover every pixel exactly:
 * this is what keeps scaled layouts free of gaps and seams. */
static void test_rects_tile_without_gaps(void)
{
    int s, x, y, i;

    for (s = 0; s < SCALE_COUNT; s++) {
        int w = GFX_SCALED(40, scales[s]), h = GFX_SCALED(30, scales[s]);
        uint32_t *px = malloc(sizeof *px * w * h);
        Canvas c;

        for (i = 0; i < w * h; i++)
            px[i] = SENTINEL;
        gfx_init(&c, px, 40, 30, scales[s]);
        CHECK_INT(c.w, w);
        CHECK_INT(c.h, h);
        for (y = 0; y < 30; y += 3)
            for (x = 0; x < 40; x += 1)
                gfx_rect(&c, x, y, 1, 3, (uint32_t)(x + y));
        for (i = 0; i < w * h && px[i] != SENTINEL; i++)
            ;
        CHECK_INT(i, w * h);
        free(px);
    }
}

/* Nothing may be drawn outside the clip rectangle or the canvas. */
static void test_clipping(void)
{
    int s, i;

    for (s = 0; s < SCALE_COUNT; s++) {
        int w = GFX_SCALED(50, scales[s]), h = GFX_SCALED(20, scales[s]), outside = 0;
        uint32_t *px = calloc((size_t)w * h + 64, sizeof *px);     /* slack catches overruns */
        Bitmap bmp;
        Canvas c;
        uint32_t pattern[16 * 16];
        const float line[] = { -20, -20, 80, 40, 10, 10 };

        for (i = 0; i < 16 * 16; i++)
            pattern[i] = 0xFFFFFF;
        bmp.px = pattern;
        bmp.w = bmp.h = 16;
        gfx_init(&c, px, 50, 20, scales[s]);
        gfx_set_clip(&c, 10, 5, 20, 10);
        gfx_rect(&c, -100, -100, 500, 500, 0xFFFFFF);
        gfx_bevel(&c, -5, -5, 200, 200, 0xFFFFFF, 0xFFFFFF);
        gfx_hline(&c, -50, 8, 500, 0xFFFFFF);
        gfx_vline(&c, 15, -50, 500, 0xFFFFFF);
        gfx_triangle(&c, -30, -30, 90, 0, 20, 60, 0xFFFFFF);
        gfx_polyline(&c, line, 3, 6, 0xFFFFFF);
        gfx_cross(&c, 25, 0, 30, 0xFFFFFF);
        gfx_text(&c, 0, 6, "CLIPPED TEXT", 0xFFFFFF);
        gfx_utext(&c, &font_list, -10, 4, "Čaj Шећер ünï", 0xFFFFFF);
        gfx_blit(&c, &bmp, 0, 0, 16, 16, 5, 0);
        gfx_blit(&c, &bmp, 0, 0, 16, 16, 25, 10);

        for (i = 0; i < w * h; i++) {
            int x = i % w, y = i / w;

            if (px[i] && (x < c.clip_x0 || x >= c.clip_x1 || y < c.clip_y0 || y >= c.clip_y1))
                outside++;
        }
        CHECK_INT(outside, 0);
        for (i = w * h; i < w * h + 64; i++)
            CHECK(px[i] == 0);
        CHECK(px[c.clip_y0 * w + c.clip_x0] == 0xFFFFFF);  /* and the inside was drawn */
        free(px);
    }
}

/* One-pixel lines must have the same thickness wherever they are. */
static void test_lines_are_even(void)
{
    int s, y;

    for (s = 0; s < SCALE_COUNT; s++) {
        int w = GFX_SCALED(4, scales[s]), h = GFX_SCALED(40, scales[s]), first = -1, run = 0, i;
        uint32_t *px = calloc((size_t)w * h, sizeof *px);
        Canvas c;

        gfx_init(&c, px, 4, 40, scales[s]);
        for (y = 1; y < 40; y += 4)
            gfx_hline(&c, 0, y, 4, 1);
        /* Measure every run of lit rows in the first column. */
        for (i = 0; i <= h; i++) {
            if (i < h && px[i * w]) {
                run++;
            } else if (run) {
                if (first < 0)
                    first = run;
                CHECK_INT(run, first);
                run = 0;
            }
        }
        CHECK(first >= 1);
        free(px);
    }
}

static void test_text_metrics(void)
{
    int s;

    CHECK_INT(gfx_text_width("ABC"), 3 * GFX_FONT_ADVANCE);
    /* At 100% the design fonts are used as they are. */
    CHECK_INT(gfx_utext_width(100, &font_list, "abcd"), 4 * 6);
    CHECK_INT(gfx_utext_width(100, &font_small, "abcd"), 4 * 5);
    /* Width counts characters, not bytes. */
    CHECK_INT(gfx_utext_width(100, &font_list, "Šećer"), 5 * 6);
    /* At 200% a 10x20 font is used at its own size: 10 real pixels a letter. */
    CHECK_INT(gfx_utext_width(200, &font_list, "abcd"), 4 * 5);
    for (s = 0; s < SCALE_COUNT; s++) {
        int one = gfx_utext_width(scales[s], &font_list, "a"), ten = gfx_utext_width(scales[s], &font_list, "aaaaaaaaaa");

        CHECK(one >= 3 && one <= 7);                /* close to its 6px design cell */
        /* Widths are rounded up to whole logical pixels, once per string. */
        CHECK(ten <= one * 10 && ten > (one - 1) * 10);
    }
    CHECK(gfx_unaccent(0x10C) == 'C');      /* C with caron */
    CHECK(gfx_unaccent(0xE9) == 'e');
    CHECK(gfx_unaccent(0x416) == 0x416);    /* Cyrillic is left alone */
}

/* A whole-factor blit replicates pixels exactly. */
static void test_blit_whole_factor(void)
{
    uint32_t src[4] = { 0x111111, 0x222222, 0x333333, 0x444444 }, dst[6 * 6];
    Bitmap bmp = { src, 2, 2 };
    Canvas c;
    int x, y;

    gfx_init(&c, dst, 2, 2, 300);
    gfx_blit(&c, &bmp, 0, 0, 2, 2, 0, 0);
    for (y = 0; y < 6; y++)
        for (x = 0; x < 6; x++)
            CHECK(dst[y * 6 + x] == src[(y / 3) * 2 + x / 3]);
}

int main(void)
{
    test_begin("gfx");
    test_rects_tile_without_gaps();
    test_clipping();
    test_lines_are_even();
    test_text_metrics();
    test_blit_whole_factor();
    return test_end();
}
