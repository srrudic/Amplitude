/* Tag reading: every supported container, text encodings, and robustness
 * against damaged files. All inputs are assembled here, byte by byte. */
#include "tags.h"
#include "test.h"

static char artist[256], title[256], genre[64];

static int read_tags(const char *path)
{
    int found = tags_read(path, artist, sizeof artist, title, sizeof title);

    tags_read_genre(path, genre, sizeof genre);
    return found;
}

/* --- ID3 --------------------------------------------------------------------- */

static void syncsafe(Bytes *b, unsigned long n)
{
    bytes_u8(b, n >> 21 & 127);
    bytes_u8(b, n >> 14 & 127);
    bytes_u8(b, n >> 7 & 127);
    bytes_u8(b, n & 127);
}

/* One ID3v2 frame; `version` selects the header layout. */
static void id3_frame(Bytes *b, int version, const char *id, const void *body, size_t size)
{
    bytes_str(b, id);
    if (version == 2) {
        bytes_u8(b, (unsigned)(size >> 16));
        bytes_u8(b, (unsigned)(size >> 8));
        bytes_u8(b, (unsigned)size);
    } else {
        if (version == 4)
            syncsafe(b, size);
        else
            bytes_be32(b, size);
        bytes_le16(b, 0);
    }
    bytes_add(b, body, size);
}

/* Wraps frames in an ID3v2 header, follows them with a little fake audio
 * and optionally an ID3v1 tag, and saves the result. */
static void id3_save(const char *path, int version, Bytes *frames, const unsigned char *v1)
{
    static const unsigned char audio[] = { 0xFF, 0xFB, 0x90, 0x00, 0, 0, 0, 0, 0, 0, 0, 0 };
    Bytes b = { NULL, 0, 0 };

    if (version) {
        bytes_str(&b, "ID3");
        bytes_u8(&b, (unsigned)version);
        bytes_le16(&b, 0);
        syncsafe(&b, frames->size);
        bytes_add(&b, frames->data, frames->size);
    }
    bytes_add(&b, audio, sizeof audio);
    if (v1)
        bytes_add(&b, v1, 128);
    free(frames->data);
    memset(frames, 0, sizeof *frames);
    bytes_save(&b, path);
}

static void test_id3(void)
{
    /* "Čaj – “Žaba”" as UTF-16 little endian with a byte order mark */
    static const unsigned char utf16_title[] = {
        1, 0xFF, 0xFE, 0x0C, 0x01, 'a', 0, 'j', 0, ' ', 0, 0x13, 0x20, ' ', 0,
        0x1C, 0x20, 0x7D, 0x01, 'a', 0, 'b', 0, 'a', 0, 0x1D, 0x20 };
    static const unsigned char utf16be_artist[] = { 2, 0x04, 0x1A, 0x04, 0x38, 0x04, 0x3D, 0x04, 0x3E };  /* Кино */
    unsigned char picture[3000], v1[128];
    Bytes frames = { NULL, 0, 0 };
    const char *path;

    /* v2.3: a large picture frame before the text frames must be skipped. */
    memset(picture, 0x55, sizeof picture);
    id3_frame(&frames, 3, "APIC", picture, sizeof picture);
    id3_frame(&frames, 3, "TIT2", utf16_title, sizeof utf16_title);
    id3_frame(&frames, 3, "TPE1", "\0Bj\xF6rk", 6);                 /* Latin-1 */
    id3_frame(&frames, 3, "TCON", "\0Techno", 7);
    path = test_path("v23.mp3");
    id3_save(path, 3, &frames, NULL);
    CHECK(read_tags(path));
    CHECK_STR(title, "Čaj – “Žaba”");
    CHECK_STR(artist, "Björk");
    CHECK_STR(genre, "Techno");

    /* v2.4: syncsafe frame sizes, UTF-8 and UTF-16BE, trailing NUL. */
    id3_frame(&frames, 4, "TPE1", utf16be_artist, sizeof utf16be_artist);
    id3_frame(&frames, 4, "TIT2", "\3Se\xC3\xB1orita\0", 12);
    path = test_path("v24.mp3");
    id3_save(path, 4, &frames, NULL);
    CHECK(read_tags(path));
    CHECK_STR(artist, "Кино");
    CHECK_STR(title, "Señorita");
    CHECK_STR(genre, "");

    /* v2.2: three-letter frame ids and three-byte sizes. */
    id3_frame(&frames, 2, "TT2", "\0  Old Style  ", 14);
    id3_frame(&frames, 2, "TP1", "\0Someone", 8);
    id3_frame(&frames, 2, "TCO", "\0(17)", 5);
    path = test_path("v22.mp3");
    id3_save(path, 2, &frames, NULL);
    CHECK(read_tags(path));
    CHECK_STR(title, "Old Style");        /* blanks trimmed */
    CHECK_STR(artist, "Someone");
    CHECK_STR(genre, "(17)");

    /* v1 only, with a numbered genre. */
    memset(v1, 0, sizeof v1);
    memcpy(v1, "TAG", 3);
    memcpy(v1 + 3, "Thirty Char Title", 17);
    memcpy(v1 + 33, "V1 Artist                     ", 30);
    v1[127] = 32;
    path = test_path("v1.mp3");
    id3_save(path, 0, &frames, v1);
    CHECK(read_tags(path));
    CHECK_STR(title, "Thirty Char Title");
    CHECK_STR(artist, "V1 Artist");
    CHECK_STR(genre, "(32)");

    /* v2 wins over v1, but v1 still supplies a genre v2 lacks. */
    id3_frame(&frames, 3, "TIT2", "\0From v2", 8);
    path = test_path("both.mp3");
    id3_save(path, 3, &frames, v1);
    CHECK(read_tags(path));
    CHECK_STR(title, "From v2");
    CHECK_STR(artist, "");
    CHECK_STR(genre, "(32)");

    path = test_path("none.mp3");
    id3_save(path, 0, &frames, NULL);
    CHECK(!read_tags(path));
    CHECK(!read_tags(test_path("does-not-exist.mp3")));
    CHECK_STR(title, "");
}

/* --- Vorbis comments: FLAC, Ogg Vorbis, Opus ---------------------------------- */

static void vorbis_comments(Bytes *b, const char *const *entries, int count, size_t padding)
{
    int i;

    bytes_le32(b, 6);
    bytes_str(b, "vendor");
    bytes_le32(b, (unsigned long)count + (padding ? 1 : 0));
    for (i = 0; i < count; i++) {
        bytes_le32(b, strlen(entries[i]));
        bytes_str(b, entries[i]);
    }
    if (padding) {              /* stands in for embedded cover art */
        bytes_le32(b, padding);
        bytes_add(b, NULL, padding);
    }
}

/* Splits a packet into Ogg pages of at most 255 segments. */
static void ogg_packet(Bytes *out, const Bytes *packet, int first)
{
    size_t pos = 0;
    int done = 0;

    while (!done) {
        unsigned char lacing[255];
        int segments = 0, i;
        size_t page_bytes = 0;

        while (segments < 255 && !done) {
            size_t left = packet->size - pos - page_bytes;

            lacing[segments] = (unsigned char)(left >= 255 ? 255 : left);
            page_bytes += lacing[segments];
            done = lacing[segments++] < 255;
        }
        bytes_str(out, "OggS");
        bytes_u8(out, 0);
        bytes_u8(out, first ? 2 : 0);
        bytes_add(out, NULL, 20);
        bytes_u8(out, (unsigned)segments);
        for (i = 0; i < segments; i++)
            bytes_u8(out, lacing[i]);
        bytes_add(out, packet->data + pos, page_bytes);
        pos += page_bytes;
        first = 0;
    }
}

static void test_vorbis_comments(void)
{
    static const char *const entries[] = {
        "ALBUM=Takk", "artist=Sigur R\xC3\xB3s", "TITLE=Hopp\xC3\xADpolla", "TITLE=second is ignored", "Genre=Post-Rock" };
    Bytes b = { NULL, 0, 0 }, packet = { NULL, 0, 0 }, head = { NULL, 0, 0 };
    const char *path;

    /* FLAC: stream info block, then the comment block marked as last. */
    vorbis_comments(&packet, entries, 5, 0);
    bytes_str(&b, "fLaC");
    bytes_be32(&b, 34);
    bytes_add(&b, NULL, 34);
    bytes_be32(&b, 0x84000000ul | packet.size);
    bytes_add(&b, packet.data, packet.size);
    path = test_path("a.flac");
    bytes_save(&b, path);
    CHECK(read_tags(path));
    CHECK_STR(artist, "Sigur Rós");
    CHECK_STR(title, "Hoppípolla");
    CHECK_STR(genre, "Post-Rock");
    free(packet.data);
    memset(&packet, 0, sizeof packet);

    /* Ogg Vorbis: a comment packet large enough to span several pages. */
    bytes_str(&head, "\x01vorbis");
    bytes_add(&head, NULL, 23);
    ogg_packet(&b, &head, 1);
    bytes_str(&packet, "\x03vorbis");
    vorbis_comments(&packet, entries, 5, 150000);
    ogg_packet(&b, &packet, 0);
    path = test_path("a.ogg");
    bytes_save(&b, path);
    CHECK(read_tags(path));
    CHECK_STR(artist, "Sigur Rós");
    CHECK_STR(title, "Hoppípolla");
    free(packet.data);
    free(head.data);
    memset(&packet, 0, sizeof packet);
    memset(&head, 0, sizeof head);

    /* Opus: same comments under a different packet signature. */
    bytes_str(&head, "OpusHead");
    bytes_add(&head, NULL, 11);
    ogg_packet(&b, &head, 1);
    bytes_str(&packet, "OpusTags");
    vorbis_comments(&packet, entries, 5, 0);
    ogg_packet(&b, &packet, 0);
    path = test_path("a.opus");
    bytes_save(&b, path);
    CHECK(read_tags(path));
    CHECK_STR(title, "Hoppípolla");
    CHECK_STR(genre, "Post-Rock");
    free(packet.data);
    free(head.data);
}

/* --- MP4 --------------------------------------------------------------------- */

static void atom(Bytes *out, const char *type, const Bytes *body)
{
    bytes_be32(out, body->size + 8);
    bytes_add(out, type, 4);
    bytes_add(out, body->data, body->size);
}

static void mp4_item(Bytes *ilst, const char *type, const char *text)
{
    Bytes data = { NULL, 0, 0 }, item = { NULL, 0, 0 };

    bytes_be32(&data, 1);       /* UTF-8 */
    bytes_be32(&data, 0);
    bytes_str(&data, text);
    atom(&item, "data", &data);
    atom(ilst, type, &item);
    free(data.data);
    free(item.data);
}

static void test_mp4(void)
{
    Bytes ilst = { NULL, 0, 0 }, meta = { NULL, 0, 0 }, udta = { NULL, 0, 0 }, moov = { NULL, 0, 0 };
    Bytes file = { NULL, 0, 0 }, body = { NULL, 0, 0 };
    const char *path = test_path("a.m4a");

    mp4_item(&ilst, "\xA9" "alb", "Album");
    mp4_item(&ilst, "\xA9nam", "D\xC3\xA9j\xC3\xA0 Vu");
    mp4_item(&ilst, "\xA9" "ART", "Beyonc\xC3\xA9");
    mp4_item(&ilst, "\xA9gen", "Pop");
    bytes_be32(&meta, 0);                       /* version and flags */
    bytes_add(&body, NULL, 25);
    atom(&meta, "hdlr", &body);
    atom(&meta, "ilst", &ilst);
    atom(&udta, "meta", &meta);
    body.size = 0;
    bytes_add(&body, NULL, 100);
    atom(&moov, "mvhd", &body);
    atom(&moov, "udta", &udta);

    body.size = 0;
    bytes_str(&body, "M4A ");
    bytes_be32(&body, 0);
    atom(&file, "ftyp", &body);
    body.size = 0;
    bytes_add(&body, NULL, 3000);               /* audio comes before the metadata */
    atom(&file, "mdat", &body);
    atom(&file, "moov", &moov);
    bytes_save(&file, path);
    free(ilst.data);
    free(meta.data);
    free(udta.data);
    free(moov.data);
    free(body.data);

    CHECK(read_tags(path));
    CHECK_STR(title, "Déjà Vu");
    CHECK_STR(artist, "Beyoncé");
    CHECK_STR(genre, "Pop");
}

static void test_module_and_text(void)
{
    const char *path = test_path("song.mod");
    char out[16];

    test_write_mod(path, "Test Module");
    CHECK(read_tags(path));
    CHECK_STR(title, "Test Module");
    CHECK_STR(artist, "");

    /* Invalid UTF-8 falls back to Latin-1; controls become blanks; output
     * never ends in the middle of a character. */
    text_to_utf8(out, sizeof out, (const unsigned char *)"a\xE9\x01z", 4, TEXT_UTF8);
    CHECK_STR(out, "a\xC3\xA9 z");
    text_to_utf8(out, 6, (const unsigned char *)"\xC4\x8C\xC4\x8C\xC4\x8C\xC4\x8C", 8, TEXT_UTF8);
    CHECK_STR(out, "\xC4\x8C\xC4\x8C");
    text_to_utf8(out, sizeof out, (const unsigned char *)"\x3D\xD8\x00\xDE" "A\0", 6, TEXT_UTF16);
    CHECK_STR(out, "\xEF\xBF\xBD" "A");     /* surrogate pair -> replacement character */
}

/* --- Damaged input ------------------------------------------------------------ */

/* Corrupts and truncates copies of the valid files many times over. The
 * only requirement is to survive; the sanitizers do the checking. */
static void test_fuzz(void)
{
    static const char *const sources[] = { "v23.mp3", "v24.mp3", "v22.mp3", "v1.mp3", "a.flac", "a.ogg", "a.opus", "a.m4a" };
    static const char *const names[] = { "fuzz.mp3", "fuzz.flac", "fuzz.ogg", "fuzz.m4a", "fuzz.it", "fuzz.xm" };
    unsigned long seed = 12345;
    int round;

    for (round = 0; round < 1500; round++) {
        const char *path = test_path(names[round % 6]);
        FILE *f = fopen(test_path(sources[round % 8]), "rb");
        unsigned char *data;
        long size;
        int i, edits;

        CHECK(f != NULL);
        if (!f)
            return;
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
        data = malloc((size_t)size);
        CHECK(fread(data, 1, (size_t)size, f) == (size_t)size);
        fclose(f);

#define NEXT() (seed = seed * 1103515245ul + 12345ul, (seed >> 16) & 0x7FFF)
        edits = 1 + (int)(NEXT() % 12);
        for (i = 0; i < edits; i++) {
            static const unsigned char nasty[] = { 0x00, 0x01, 0x7F, 0x80, 0xFF };
            long where = (long)(NEXT() % (unsigned long)(size < 400 ? size : 400));

            data[where] = NEXT() % 2 ? nasty[NEXT() % 5] : (unsigned char)NEXT();
        }
        if (NEXT() % 5 < 2)
            size = 1 + (long)(NEXT() % (unsigned long)size);
#undef NEXT
        test_write(path, data, (size_t)size);
        free(data);
        read_tags(path);
    }
}

int main(void)
{
    test_begin("tags");
    test_id3();
    test_vorbis_comments();
    test_mp4();
    test_module_and_text();
    test_fuzz();
    return test_end();
}
