/* Skin loading: the ZIP reader, inflate, the BMP decoder and the text files
 * of a classic skin. The archive is assembled here; the deflate streams in
 * inflate_vectors.h were produced once with zlib:
 *
 *   c = zlib.compressobj(level, zlib.DEFLATED, -15, 9, strategy)
 *   fixed:   "hello hello hello hello, amplitude!", level 9, Z_FIXED
 *   dynamic: bytes((i*7 + i//13) % 251 for i in range(600)) + b"abcabcabc"*40, level 9
 *   stored:  bytes(range(256)) * 2, level 0
 */
#include "skin.h"
#include "zip.h"
#include "inflate_vectors.h"
#include "test.h"

/* --- A tiny ZIP writer -------------------------------------------------------- */

typedef struct {
    Bytes file, directory;
    int entries;
} ZipWriter;

static void zip_add(ZipWriter *zip, const char *name, int method, const void *data, size_t packed, size_t unpacked)
{
    unsigned long offset = zip->file.size;
    int i;

    for (i = 0; i < 2; i++) {       /* local header, then the directory entry */
        Bytes *b = i ? &zip->directory : &zip->file;

        bytes_le32(b, i ? 0x02014b50 : 0x04034b50);
        if (i)
            bytes_le16(b, 20);
        bytes_le16(b, 20);
        bytes_le16(b, 0);
        bytes_le16(b, (unsigned)method);
        bytes_le32(b, 0);           /* time, date */
        bytes_le32(b, 0);           /* CRC: not checked by the reader */
        bytes_le32(b, packed);
        bytes_le32(b, unpacked);
        bytes_le16(b, (unsigned)strlen(name));
        bytes_le16(b, 0);
        if (i) {
            bytes_le16(b, 0);
            bytes_le32(b, 0);
            bytes_le32(b, 0);
            bytes_le32(b, offset);
        }
        bytes_str(b, name);
    }
    bytes_add(&zip->file, data, packed);
    zip->entries++;
}

static void zip_save(ZipWriter *zip, const char *path)
{
    unsigned long start = zip->file.size;

    bytes_add(&zip->file, zip->directory.data, zip->directory.size);
    bytes_le32(&zip->file, 0x06054b50);
    bytes_le32(&zip->file, 0);
    bytes_le16(&zip->file, (unsigned)zip->entries);
    bytes_le16(&zip->file, (unsigned)zip->entries);
    bytes_le32(&zip->file, zip->directory.size);
    bytes_le32(&zip->file, start);
    bytes_le16(&zip->file, 7);
    bytes_str(&zip->file, "comment");
    free(zip->directory.data);
    bytes_save(&zip->file, path);
    memset(zip, 0, sizeof *zip);
}

/* --- BMP writers -------------------------------------------------------------- */

/* The test pattern every bitmap is filled with, as 0x00RRGGBB. Only eight
 * distinct colours, so palette formats can hold it too. */
static uint32_t pattern(int x, int y)
{
    static const uint32_t colours[8] = {
        0x000000, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0x00FFFF, 0xFF00FF, 0xFFFFFF };

    return colours[(x / 3 + y / 2) % 8];
}

static int colour_index(uint32_t c)
{
    int i;

    for (i = 0; pattern(i * 3, 0) != c; i++)
        ;
    return i;
}

static void bmp_header(Bytes *b, int w, int h, int bpp, int compression, int colours, int extra)
{
    bytes_str(b, "BM");
    bytes_le32(b, 0);
    bytes_le32(b, 0);
    bytes_le32(b, 54 + (unsigned long)colours * 4 + (unsigned long)extra);   /* pixel data offset */
    bytes_le32(b, 40);
    bytes_le32(b, (unsigned long)w);
    bytes_le32(b, (unsigned long)h);
    bytes_le16(b, 1);
    bytes_le16(b, (unsigned)bpp);
    bytes_le32(b, (unsigned long)compression);
    bytes_le32(b, 0);
    bytes_le32(b, 0);
    bytes_le32(b, 0);
    bytes_le32(b, (unsigned long)colours);
    bytes_le32(b, 0);
}

static void bmp_palette(Bytes *b)
{
    int i;

    for (i = 0; i < 8; i++) {
        uint32_t c = pattern(i * 3, 0);

        bytes_u8(b, c & 255);
        bytes_u8(b, c >> 8 & 255);
        bytes_u8(b, c >> 16 & 255);
        bytes_u8(b, 0);
    }
}

/* `kind`: 24 or 32 bits, 8 (palette), 4 (palette), 16 (5-6-5 with masks),
 * or -8 for run-length encoded 8-bit. Rows are stored bottom-up. */
static void bmp_make(Bytes *b, int w, int h, int kind)
{
    int x, y;

    if (kind == 16) {
        bmp_header(b, w, h, 16, 3, 0, 12);
        bytes_le32(b, 0xF800);
        bytes_le32(b, 0x07E0);
        bytes_le32(b, 0x001F);
    } else if (kind == 24 || kind == 32) {
        bmp_header(b, w, h, kind, 0, 0, 0);
    } else {
        bmp_header(b, w, h, kind == 4 ? 4 : 8, kind == -8 ? 1 : 0, 8, 0);
        bmp_palette(b);
    }
    for (y = h - 1; y >= 0; y--) {
        size_t row_start = b->size;

        for (x = 0; x < w; x++) {
            uint32_t c = pattern(x, y);

            if (kind == 24 || kind == 32) {
                bytes_u8(b, c & 255);
                bytes_u8(b, c >> 8 & 255);
                bytes_u8(b, c >> 16 & 255);
                if (kind == 32)
                    bytes_u8(b, 0);
            } else if (kind == 16) {
                bytes_le16(b, (unsigned)((c >> 16 & 255) >> 3 << 11 | (c >> 8 & 255) >> 2 << 5 | (c & 255) >> 3));
            } else if (kind == 8) {
                bytes_u8(b, (unsigned)colour_index(c));
            } else if (kind == 4) {
                if (x % 2 == 0)
                    bytes_u8(b, (unsigned)(colour_index(c) << 4 | (x + 1 < w ? colour_index(pattern(x + 1, y)) : 0)));
            } else {        /* RLE8: runs of equal pixels, then end of line */
                int run = 1;

                while (x + run < w && run < 255 && pattern(x + run, y) == c)
                    run++;
                bytes_u8(b, (unsigned)run);
                bytes_u8(b, (unsigned)colour_index(c));
                x += run - 1;
            }
        }
        if (kind == -8) {
            bytes_u8(b, 0);
            bytes_u8(b, y ? 0 : 1);     /* end of line, or end of bitmap */
        } else {
            while ((b->size - row_start) % 4)
                bytes_u8(b, 0);
        }
    }
}

static void check_sheet(const Skin *skin, int sheet, int w, int h)
{
    const Bitmap *bmp = &skin->sheet[sheet];
    int x, y, wrong = 0;

    CHECK(!skin->builtin[sheet]);
    CHECK_INT(bmp->w, w);
    CHECK_INT(bmp->h, h);
    if (skin->builtin[sheet] || bmp->w != w || bmp->h != h)
        return;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            wrong += bmp->px[y * w + x] != pattern(x, y);
    CHECK_INT(wrong, 0);
}

/* --- Tests -------------------------------------------------------------------- */

static void test_zip_and_inflate(void)
{
    static const char fixed_text[] = "hello hello hello hello, amplitude!";
    unsigned char dynamic_text[960], stored_text[512], *data;
    const char *path = test_path("vectors.zip");
    ZipWriter writer;
    ZipArchive zip;
    size_t size = 0;
    int i;

    memset(&writer, 0, sizeof writer);
    for (i = 0; i < 600; i++)
        dynamic_text[i] = (unsigned char)((i * 7 + i / 13) % 251);
    for (i = 0; i < 360; i++)
        dynamic_text[600 + i] = (unsigned char)"abc"[i % 3];
    for (i = 0; i < 512; i++)
        stored_text[i] = (unsigned char)i;

    zip_add(&writer, "plain.txt", 0, "not compressed", 14, 14);
    zip_add(&writer, "Sub Dir/FIXED.TXT", 8, deflate_fixed, sizeof deflate_fixed, sizeof fixed_text - 1);
    zip_add(&writer, "dynamic.bin", 8, deflate_dynamic, sizeof deflate_dynamic, sizeof dynamic_text);
    zip_add(&writer, "deep\\er\\stored.bin", 8, deflate_stored, sizeof deflate_stored, sizeof stored_text);
    zip_add(&writer, "short.bin", 8, deflate_dynamic, sizeof deflate_dynamic / 2, sizeof dynamic_text);   /* cut off */
    zip_add(&writer, "wrongsize.bin", 8, deflate_fixed, sizeof deflate_fixed, 999);
    zip_add(&writer, "method.bin", 12, "xx", 2, 2);
    zip_save(&writer, path);

    CHECK(zip_open(&zip, path));
    data = zip_read(&zip, "plain.txt", &size);
    CHECK(data && size == 14 && memcmp(data, "not compressed", 14) == 0 && data[14] == '\0');
    free(data);
    /* Names match without the directory part and without regard to case. */
    data = zip_read(&zip, "fixed.txt", &size);
    CHECK(data && size == sizeof fixed_text - 1 && memcmp(data, fixed_text, size) == 0);
    free(data);
    data = zip_read(&zip, "dynamic.bin", &size);
    CHECK(data && size == sizeof dynamic_text && memcmp(data, dynamic_text, size) == 0);
    free(data);
    data = zip_read(&zip, "STORED.BIN", &size);
    CHECK(data && size == sizeof stored_text && memcmp(data, stored_text, size) == 0);
    free(data);
    /* Damaged or unsupported entries are refused, not half-returned. */
    CHECK(zip_read(&zip, "short.bin", &size) == NULL);
    CHECK(zip_read(&zip, "wrongsize.bin", &size) == NULL);
    CHECK(zip_read(&zip, "method.bin", &size) == NULL);
    CHECK(zip_read(&zip, "missing.txt", &size) == NULL);
    zip_close(&zip);

    test_write(path, "PK not really a zip file at all", 31);
    CHECK(!zip_open(&zip, path));
    CHECK(!zip_open(&zip, test_path("missing.zip")));
}

static void add_bmp(ZipWriter *zip, const char *name, int w, int h, int kind)
{
    Bytes b = { NULL, 0, 0 };

    bmp_make(&b, w, h, kind);
    zip_add(zip, name, 0, b.data, b.size, b.size);
    free(b.data);
}

static void test_skin_load(void)
{
    static const char viscolor[] = "1,2,3, // background\r\n\r\n 10 , 20 , 30\n// comment only\n255,255,255\n";
    static const char pledit[] = "[Text]\r\nNormal=#00FF00\r\nCurrent = #ffffff\r\nNormalBG=#102030\r\nSelectedBG=#0000C6\r\nFont=Arial\r\n";
    const char *path = test_path("test.wsz");
    ZipWriter writer;
    Skin skin;
    Bytes b = { NULL, 0, 0 };
    int i;

    memset(&writer, 0, sizeof writer);
    add_bmp(&writer, "MAIN.BMP", 275, 116, 24);
    add_bmp(&writer, "Skin/CButtons.bmp", 136, 36, 8);
    add_bmp(&writer, "numbers.bmp", 99, 13, 4);
    add_bmp(&writer, "TEXT.BMP", 155, 18, -8);
    add_bmp(&writer, "titlebar.bmp", 344, 87, 16);
    add_bmp(&writer, "volume.bmp", 68, 433, 32);
    zip_add(&writer, "posbar.bmp", 0, "BM this is not a bitmap", 23, 23);
    zip_add(&writer, "VISCOLOR.TXT", 0, viscolor, sizeof viscolor - 1, sizeof viscolor - 1);
    zip_add(&writer, "pledit.txt", 0, pledit, sizeof pledit - 1, sizeof pledit - 1);
    zip_save(&writer, path);

    skin_init_default(&skin);
    for (i = 0; i < SKIN_SHEET_COUNT; i++)
        CHECK(skin.builtin[i] && skin.sheet[i].w > 0 && skin.sheet[i].px == NULL);

    /* Six sheets load; balance borrows the volume artwork; the rest stay built-in. */
    CHECK_INT(skin_load(&skin, path), 6);
    check_sheet(&skin, SKIN_MAIN, 275, 116);
    check_sheet(&skin, SKIN_CBUTTONS, 136, 36);
    check_sheet(&skin, SKIN_NUMBERS, 99, 13);
    check_sheet(&skin, SKIN_TEXT, 155, 18);
    check_sheet(&skin, SKIN_VOLUME, 68, 433);
    check_sheet(&skin, SKIN_BALANCE, 68, 433);
    CHECK(skin.builtin[SKIN_POSBAR] && skin.builtin[SKIN_EQMAIN] && skin.builtin[SKIN_PLEDIT]);
    CHECK_INT(skin.text_w, 5);
    CHECK_INT(skin.text_h, 6);
    /* 5-6-5 pixels lose precision, so compare the top bits only. */
    CHECK(!skin.builtin[SKIN_TITLEBAR] && skin.sheet[SKIN_TITLEBAR].w == 344);
    CHECK((skin.sheet[SKIN_TITLEBAR].px[3] & 0xF8FCF8) == (pattern(3, 0) & 0xF8FCF8));

    CHECK(skin.vis[0] == 0x010203 && skin.vis[1] == 0x0A141E && skin.vis[2] == 0xFFFFFF);
    CHECK(skin.pl_normal == 0x00FF00 && skin.pl_current == 0xFFFFFF);
    CHECK(skin.pl_background == 0x102030 && skin.pl_selected == 0x0000C6);
    skin_free(&skin);

    /* Unusable files leave the skin fully built-in. */
    skin_init_default(&skin);
    test_write(path, "garbage", 7);
    CHECK_INT(skin_load(&skin, path), 0);
    CHECK_INT(skin_load(&skin, test_path("missing.wsz")), 0);
    for (i = 0; i < SKIN_SHEET_COUNT; i++)
        CHECK(skin.builtin[i]);
    skin_free(&skin);

    /* A bitmap cut short still loads, with the missing rows left black. */
    bmp_make(&b, 275, 116, 24);
    memset(&writer, 0, sizeof writer);
    zip_add(&writer, "main.bmp", 0, b.data, b.size / 2, b.size / 2);
    zip_save(&writer, path);
    free(b.data);
    skin_init_default(&skin);
    CHECK_INT(skin_load(&skin, path), 1);
    CHECK(skin.sheet[SKIN_MAIN].px[0] == 0);
    CHECK(skin.sheet[SKIN_MAIN].px[115 * 275 + 6] == pattern(6, 115));
    skin_free(&skin);
}

static void test_text_cells(void)
{
    int col, row;

    CHECK(skin_text_cell('A', &col, &row) && col == 0 && row == 0);
    CHECK(skin_text_cell('z', &col, &row) && col == 25 && row == 0);
    CHECK(skin_text_cell('7', &col, &row) && col == 7 && row == 1);
    CHECK(skin_text_cell(':', &col, &row) && col == 12 && row == 1);
    CHECK(skin_text_cell('?', &col, &row) && col == 3 && row == 2);
    CHECK(skin_text_cell(' ', &col, &row) && col == 30 && row == 0);
    CHECK(!skin_text_cell(0xE9, &col, &row) && col == 30);     /* no glyph: blank cell */
}

int main(void)
{
    test_begin("skin");
    test_zip_and_inflate();
    test_skin_load();
    test_text_cells();
    return test_end();
}
