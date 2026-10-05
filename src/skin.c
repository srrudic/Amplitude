#include "skin.h"

#include "theme.h"
#include "zip.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BMP_DIM 4096

static const char *const sheet_files[SKIN_SHEET_COUNT] = {
    [SKIN_MAIN]     = "main.bmp",
    [SKIN_TITLEBAR] = "titlebar.bmp",
    [SKIN_CBUTTONS] = "cbuttons.bmp",
    [SKIN_NUMBERS]  = "numbers.bmp",
    [SKIN_PLAYPAUS] = "playpaus.bmp",
    [SKIN_TEXT]     = "text.bmp",
    [SKIN_MONOSTER] = "monoster.bmp",
    [SKIN_POSBAR]   = "posbar.bmp",
    [SKIN_VOLUME]   = "volume.bmp",
    [SKIN_BALANCE]  = "balance.bmp",
    [SKIN_SHUFREP]  = "shufrep.bmp",
    [SKIN_EQMAIN]   = "eqmain.bmp",
    [SKIN_PLEDIT]   = "pledit.bmp",
};

/* --- Painted sprites, remembered -------------------------------------------
 * The built-in skin has no bitmaps: a sprite is produced by running the
 * sheet's drawing code with everything outside the sprite clipped away.
 * That is far too slow to repeat for every sprite of every frame, so the
 * pixels are kept the first time and copied from then on.
 *
 * At a fractional magnification the same sprite can come out a pixel
 * different depending on where it lands, so an entry also records the
 * position's remainder ("phase") in both directions; positions with the
 * same phase give identical pixels. The cache is thrown away whenever the
 * magnification or the theme colour changes. */

#define CACHE_SLOTS      384
#define CACHE_MAX_PIXELS (3 * 1024 * 1024)      /* 12 MB; reached only at the largest sizes */

typedef struct {
    int sheet, sx, sy, w, h, phase_x, phase_y;
    int real_w, real_h;
    uint32_t *px;
} CachedSprite;

static CachedSprite cache[CACHE_SLOTS];
static int cache_count, cache_scale;
static long cache_pixels;
static uint32_t cache_color;
int skin_cache_disabled;

void skin_cache_clear(void)
{
    while (cache_count)
        free(cache[--cache_count].px);
    cache_pixels = 0;
}

/* Remainder of a logical offset's real position, in hundredths of a pixel. */
static int phase(int offset, int scale)
{
    int p = offset * scale % 100;

    return p < 0 ? p + 100 : p;
}

/* Copies between a sprite's pixels and its place on the canvas (whose top
 * left real pixel is x0, y0), leaving out what the clip excludes. */
static void copy_sprite(Canvas *c, CachedSprite *sprite, int x0, int y0, int to_canvas)
{
    int left = x0 < c->clip_x0 ? c->clip_x0 : x0, top = y0 < c->clip_y0 ? c->clip_y0 : y0;
    int right = x0 + sprite->real_w > c->clip_x1 ? c->clip_x1 : x0 + sprite->real_w;
    int bottom = y0 + sprite->real_h > c->clip_y1 ? c->clip_y1 : y0 + sprite->real_h, y;

    for (y = top; y < bottom && left < right; y++) {
        uint32_t *on_canvas = &c->px[y * c->w + left];
        uint32_t *in_sprite = &sprite->px[(y - y0) * sprite->real_w + (left - x0)];

        if (to_canvas)
            memcpy(on_canvas, in_sprite, sizeof(uint32_t) * (size_t)(right - left));
        else
            memcpy(in_sprite, on_canvas, sizeof(uint32_t) * (size_t)(right - left));
    }
}

void skin_blit(Canvas *c, const Skin *skin, int sheet, int sx, int sy, int w, int h, int dx, int dy)
{
    int x0, y0, x1, y1, phase_x, phase_y, i;
    CachedSprite *sprite = NULL;
    Canvas saved;

    if (!skin->builtin[sheet]) {
        gfx_blit(c, &skin->sheet[sheet], sx, sy, w, h, dx, dy);
        return;
    }
    if (cache_scale != c->scale || cache_color != theme_get()->base) {
        skin_cache_clear();
        cache_scale = c->scale;
        cache_color = theme_get()->base;
    }
    gfx_real_rect(c, dx, dy, w, h, &x0, &y0, &x1, &y1);
    phase_x = phase(c->ox + dx - sx, c->scale);
    phase_y = phase(c->oy + dy - sy, c->scale);
    for (i = 0; i < cache_count && !skin_cache_disabled; i++) {
        const CachedSprite *e = &cache[i];

        if (e->sheet == sheet && e->sx == sx && e->sy == sy && e->w == w && e->h == h &&
            e->phase_x == phase_x && e->phase_y == phase_y) {
            copy_sprite(c, &cache[i], x0, y0, 1);
            return;
        }
    }
    /* Not seen before. It can be kept if all of it is about to be drawn,
     * which is so unless the caller's clip cuts into it. */
    if (!skin_cache_disabled && cache_count < CACHE_SLOTS && x1 > x0 && y1 > y0 && x0 >= c->clip_x0 && y0 >= c->clip_y0 &&
        x1 <= c->clip_x1 && y1 <= c->clip_y1 && cache_pixels + (long)(x1 - x0) * (y1 - y0) <= CACHE_MAX_PIXELS) {
        sprite = &cache[cache_count];
        sprite->px = malloc(sizeof(uint32_t) * (size_t)(x1 - x0) * (size_t)(y1 - y0));
        if (!sprite->px)
            sprite = NULL;
    }

    /* Run the sheet's drawing code shifted so that (sx, sy) lands on
     * (dx, dy), with everything outside the sprite clipped away. */
    saved = *c;
    gfx_intersect_clip(c, dx, dy, w, h);
    c->ox += dx - sx;
    c->oy += dy - sy;
    if (c->clip_x0 < c->clip_x1 && c->clip_y0 < c->clip_y1)
        skin_paint_builtin(c, sheet);
    *c = saved;

    if (sprite) {
        sprite->sheet = sheet;
        sprite->sx = sx;
        sprite->sy = sy;
        sprite->w = w;
        sprite->h = h;
        sprite->phase_x = phase_x;
        sprite->phase_y = phase_y;
        sprite->real_w = x1 - x0;
        sprite->real_h = y1 - y0;
        copy_sprite(c, sprite, x0, y0, 0);
        cache_count++;
        cache_pixels += (long)sprite->real_w * sprite->real_h;
    }
}

void skin_free(Skin *skin)
{
    int i;

    for (i = 0; i < SKIN_SHEET_COUNT; i++)
        free(skin->sheet[i].px);
    memset(skin, 0, sizeof *skin);
    skin_cache_clear();
}

int skin_text_cell(int ch, int *col, int *row)
{
    static const char row1[] = "0123456789\x7f.:()-'!_+\\/[]^&%,=$#";
    const char *p;

    if (ch >= 'a' && ch <= 'z')
        ch -= 'a' - 'A';
    if (ch >= 'A' && ch <= 'Z') {
        *row = 0;
        *col = ch - 'A';
    } else if (ch == '"' || ch == '@') {
        *row = 0;
        *col = ch == '"' ? 26 : 27;
    } else if (ch == '?' || ch == '*') {
        *row = 2;
        *col = ch == '?' ? 3 : 4;
    } else if (ch > ' ' && ch < 0x7f && (p = strchr(row1, ch)) != NULL) {
        *row = 1;
        *col = (int)(p - row1);
    } else {
        *row = 0;
        *col = 30;      /* blank */
        return ch == ' ';
    }
    return 1;
}

/* Expands RLE8 (or RLE4 when `nibbles` is set) pixel data into one byte per
 * pixel, keeping the file's bottom-up row order. */
static unsigned char *rle_expand(const unsigned char *p, const unsigned char *end, int w, int h, int nibbles)
{
    unsigned char *out = calloc((size_t)w, (size_t)h);
    int x = 0, y = 0, i;

    if (!out)
        return NULL;
    while (end - p >= 2 && y < h) {
        int count = p[0], value = p[1];

        p += 2;
        if (count) {                    /* run */
            for (i = 0; i < count; i++, x++) {
                int v = !nibbles ? value : i % 2 ? value & 15 : value >> 4;

                if (x < w)
                    out[(size_t)y * w + x] = (unsigned char)v;
            }
        } else if (value == 0) {        /* end of line */
            x = 0;
            y++;
        } else if (value == 1) {        /* end of bitmap */
            break;
        } else if (value == 2) {        /* skip */
            if (end - p < 2)
                break;
            x += p[0];
            y += p[1];
            p += 2;
        } else {                        /* literal pixels, padded to 16 bits */
            int bytes = nibbles ? (value + 1) / 2 : value;

            if (end - p < bytes)
                break;
            for (i = 0; i < value; i++, x++) {
                int v = !nibbles ? p[i] : i % 2 ? p[i / 2] & 15 : p[i / 2] >> 4;

                if (x < w && y < h)
                    out[(size_t)y * w + x] = (unsigned char)v;
            }
            p += bytes + (bytes & 1);
        }
    }
    return out;
}

/* Decodes a 1/4/8/16/24/32-bit BMP, including RLE-compressed ones. Truncated
 * files, which are common in old skins, simply leave the missing rows black. */
static int bmp_decode(const unsigned char *d, size_t size, Bitmap *out)
{
    const unsigned char *palette;
    unsigned char *expanded = NULL;
    uint32_t pixel_offset, header, compression = 0;
    size_t stride, row_bytes, palette_size;
    int w, h, bpp, top_down = 0, green_bits = 5, palette_entry, x, y;

    if (size < 26 || d[0] != 'B' || d[1] != 'M')
        return 0;
    pixel_offset = rd32(d + 10);
    header = rd32(d + 14);
    if (header == 12) {             /* OS/2 style */
        w = (int)rd16(d + 18);
        h = (int)rd16(d + 20);
        bpp = (int)rd16(d + 24);
        palette_entry = 3;
    } else if (header >= 40 && size >= 54) {
        w = (int32_t)rd32(d + 18);
        h = (int32_t)rd32(d + 22);
        bpp = (int)rd16(d + 28);
        compression = rd32(d + 30);
        palette_entry = 4;
    } else {
        return 0;
    }
    if (h < 0) {
        h = -h;
        top_down = 1;
    }
    if (w <= 0 || h <= 0 || w > MAX_BMP_DIM || h > MAX_BMP_DIM)
        return 0;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
        return 0;
    if (compression != 0 && !(compression == 1 && bpp == 8) && !(compression == 2 && bpp == 4) &&
        !(compression == 3 && (bpp == 16 || bpp == 32)))
        return 0;
    /* 16-bit pixels are 5-5-5 unless the bit masks say 5-6-5. */
    if (compression == 3 && bpp == 16 && size >= 66 && rd32(d + 58) == 0x07E0)
        green_bits = 6;
    if (header > size - 14 || pixel_offset > size)
        return 0;
    palette = d + 14 + header;
    palette_size = (size - 14 - header) / palette_entry;

    stride = (((size_t)w * bpp + 31) / 32) * 4;
    row_bytes = ((size_t)w * bpp + 7) / 8;
    if (compression == 1 || compression == 2) {
        expanded = rle_expand(d + pixel_offset, d + size, w, h, compression == 2);
        if (!expanded)
            return 0;
        d = expanded;
        size = (size_t)w * h;
        pixel_offset = 0;
        bpp = 8;
        stride = row_bytes = (size_t)w;
    }

    out->px = calloc((size_t)w * h, sizeof(uint32_t));
    if (!out->px) {
        free(expanded);
        return 0;
    }
    out->w = w;
    out->h = h;

    for (y = 0; y < h; y++) {
        size_t offset = pixel_offset + (size_t)(top_down ? y : h - 1 - y) * stride;
        const unsigned char *row = d + offset;
        uint32_t *dst = out->px + (size_t)y * w;

        if (offset > size || size - offset < row_bytes)
            continue;
        for (x = 0; x < w; x++) {
            if (bpp >= 24) {
                const unsigned char *p = row + (size_t)x * (bpp / 8);

                dst[x] = p[0] | p[1] << 8 | (uint32_t)p[2] << 16;
            } else if (bpp == 16) {
                unsigned v = rd16(row + (size_t)x * 2);
                unsigned r = (v >> (5 + green_bits)) & 31, b = v & 31;
                unsigned g = (v >> 5) & ((1u << green_bits) - 1);

                g = green_bits == 6 ? g << 2 | g >> 4 : g << 3 | g >> 2;
                dst[x] = (uint32_t)(r << 3 | r >> 2) << 16 | g << 8 | (b << 3 | b >> 2);
            } else {
                unsigned index;

                if (bpp == 8)
                    index = row[x];
                else if (bpp == 4)
                    index = (row[x / 2] >> (x % 2 ? 0 : 4)) & 15;
                else
                    index = (row[x / 8] >> (7 - x % 8)) & 1;
                if (index < palette_size) {
                    const unsigned char *p = palette + index * palette_entry;

                    dst[x] = p[0] | p[1] << 8 | (uint32_t)p[2] << 16;
                }
            }
        }
    }
    free(expanded);
    return 1;
}

/* The skin's text colour: whichever pixel of the letter and digit rows
 * differs most from the background of the blank cell. */
static uint32_t text_foreground(const Bitmap *sheet)
{
    uint32_t background, best;
    int x, y, best_distance = -1;

    if (sheet->w < 155 || sheet->h < 12)
        return 0xFFFFFF;
    background = best = sheet->px[152];     /* inside the blank cell */
    for (y = 0; y < 12; y++) {
        for (x = 0; x < 130; x++) {
            uint32_t c = sheet->px[y * sheet->w + x];
            int distance = abs((int)(c >> 16 & 255) - (int)(background >> 16 & 255)) +
                           abs((int)(c >> 8 & 255) - (int)(background >> 8 & 255)) +
                           abs((int)(c & 255) - (int)(background & 255));

            if (distance > best_distance) {
                best_distance = distance;
                best = c;
            }
        }
    }
    return best;
}

static int load_sheet(Skin *skin, const ZipArchive *zip, int sheet, const char *name)
{
    Bitmap bmp;
    size_t size;
    unsigned char *data = zip_read(zip, name, &size);
    int ok;

    if (!data)
        return 0;
    ok = bmp_decode(data, size, &bmp);
    free(data);
    if (!ok)
        return 0;
    free(skin->sheet[sheet].px);
    skin->sheet[sheet] = bmp;
    skin->builtin[sheet] = 0;
    return 1;
}

/* viscolor.txt: one "r,g,b" triple per line, followed by free-form comments. */
static void parse_viscolor(Skin *skin, const char *p)
{
    int i = 0;

    while (i < SKIN_VIS_COLORS && *p) {
        long rgb[3];
        int k;

        for (k = 0; k < 3; k++) {
            while (*p == ' ' || *p == '\t' || *p == ',')
                p++;
            if (!isdigit((unsigned char)*p))
                break;
            rgb[k] = strtol(p, (char **)&p, 10) & 255;
        }
        if (k == 3)
            skin->vis[i++] = (uint32_t)(rgb[0] << 16 | rgb[1] << 8 | rgb[2]);
        while (*p && *p != '\n')
            p++;
        if (*p)
            p++;
    }
}

/* pledit.txt is an INI file with lines such as "Normal=#00FF00". */
static void parse_pledit_color(const char *p, const char *key, uint32_t *out)
{
    size_t len = strlen(key), i;

    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;
        for (i = 0; i < len && tolower((unsigned char)p[i]) == key[i]; i++)
            ;
        if (i == len) {
            const char *q = p + len;

            while (*q == ' ' || *q == '\t')
                q++;
            if (*q == '=') {
                q++;
                while (*q == ' ' || *q == '\t' || *q == '#')
                    q++;
                if (isxdigit((unsigned char)*q)) {
                    *out = (uint32_t)strtoul(q, NULL, 16) & 0xFFFFFF;
                    return;
                }
            }
        }
        while (*p && *p != '\n')
            p++;
        if (*p)
            p++;
    }
}

int skin_load(Skin *skin, const char *path)
{
    ZipArchive zip;
    unsigned char *text;
    size_t size;
    int i, loaded = 0, have_volume = 0, have_balance = 0;

    if (!zip_open(&zip, path))
        return 0;

    for (i = 0; i < SKIN_SHEET_COUNT; i++) {
        int ok = load_sheet(skin, &zip, i, sheet_files[i]);

        /* nums_ex.bmp is the extended digit sheet; it wins when present. */
        if (i == SKIN_NUMBERS && load_sheet(skin, &zip, i, "nums_ex.bmp"))
            ok = 1;
        if (ok && i == SKIN_TEXT) {
            skin->text_w = 5;
            skin->text_h = 6;
            skin->text_color = text_foreground(&skin->sheet[i]);
            skin->text_background = skin->sheet[i].w > 152 ? skin->sheet[i].px[152] : 0;
        }
        if (ok && i == SKIN_EQMAIN && skin->sheet[i].h >= 313 && skin->sheet[i].w > 115) {
            int row;

            for (row = 0; row < SKIN_EQ_GRAPH_H; row++)
                skin->eq_line[row] = skin->sheet[i].px[(294 + row) * skin->sheet[i].w + 115];
        }
        if (ok && i == SKIN_VOLUME)
            have_volume = 1;
        if (ok && i == SKIN_BALANCE)
            have_balance = 1;
        loaded += ok;
    }
    /* Skins without balance.bmp reuse the volume artwork for it. */
    if (have_volume && !have_balance)
        load_sheet(skin, &zip, SKIN_BALANCE, sheet_files[SKIN_VOLUME]);

    text = zip_read(&zip, "viscolor.txt", &size);
    if (text) {
        parse_viscolor(skin, (const char *)text);
        free(text);
    }
    text = zip_read(&zip, "pledit.txt", &size);
    if (text) {
        parse_pledit_color((const char *)text, "normal", &skin->pl_normal);
        parse_pledit_color((const char *)text, "current", &skin->pl_current);
        parse_pledit_color((const char *)text, "normalbg", &skin->pl_background);
        parse_pledit_color((const char *)text, "selectedbg", &skin->pl_selected);
        free(text);
    }
    zip_close(&zip);
    return loaded;
}
