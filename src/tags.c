#include "tags.h"
#include "platform.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TEXT      512           /* bytes read from any one text field */
#define MAX_COMMENTS  (64 * 1024)   /* bytes read from a Vorbis comment block */

/* --- Text conversion -------------------------------------------------------- */

/* Decodes one UTF-8 sequence; bytes that are not valid UTF-8 are taken as
 * Latin-1, which is what legacy tags and file names usually are. */
static unsigned long utf8_next(const unsigned char *p, size_t len, size_t *used)
{
    int extra = p[0] >= 0xF0 ? 3 : p[0] >= 0xE0 ? 2 : p[0] >= 0xC0 ? 1 : 0, i;
    unsigned long cp = extra == 3 ? p[0] & 0x07 : extra == 2 ? p[0] & 0x0F : p[0] & 0x1F;

    *used = 1;
    if (p[0] < 0x80 || !extra || (size_t)extra >= len)
        return p[0];
    for (i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0) != 0x80)
            return p[0];
        cp = cp << 6 | (p[i] & 0x3F);
    }
    *used = (size_t)extra + 1;
    return cp;
}

void text_to_utf8(char *out, size_t out_size, const unsigned char *text, size_t len, int encoding)
{
    size_t pos = 0, n = 0, used;
    int big_endian = encoding == TEXT_UTF16BE;

    if (!out_size)
        return;
    if (encoding == TEXT_UTF16 && len >= 2) {
        if (text[0] == 0xFE && text[1] == 0xFF) {
            big_endian = 1;
            pos = 2;
        } else if (text[0] == 0xFF && text[1] == 0xFE) {
            pos = 2;
        }
    }
    while (pos < len && n + 1 < out_size) {
        unsigned long cp;

        if (encoding == TEXT_UTF16 || encoding == TEXT_UTF16BE) {
            if (len - pos < 2)
                break;
            cp = big_endian ? (unsigned long)text[pos] << 8 | text[pos + 1]
                            : (unsigned long)text[pos + 1] << 8 | text[pos];
            pos += 2;
            if (cp >= 0xD800 && cp <= 0xDFFF) {     /* surrogate pair: not displayable */
                if (cp < 0xDC00 && len - pos >= 2)
                    pos += 2;
                cp = 0xFFFD;
            }
        } else if (encoding == TEXT_UTF8) {
            cp = utf8_next(text + pos, len - pos, &used);
            pos += used;
        } else {
            cp = text[pos++];
        }
        if (!cp)
            break;
        if (cp == 0xFEFF)
            continue;
        if (cp < 0x20 || cp == 0x7F)
            cp = ' ';
        if (cp == ' ' && !n)
            continue;       /* skip leading blanks */

        /* Re-encode as UTF-8. */
        if (cp < 0x80) {
            out[n++] = (char)cp;
        } else if (cp < 0x800 && n + 2 < out_size) {
            out[n++] = (char)(0xC0 | cp >> 6);
            out[n++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp >= 0x800 && cp < 0x10000 && n + 3 < out_size) {
            out[n++] = (char)(0xE0 | cp >> 12);
            out[n++] = (char)(0x80 | (cp >> 6 & 0x3F));
            out[n++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp >= 0x10000 && cp < 0x110000 && n + 4 < out_size) {
            out[n++] = (char)(0xF0 | cp >> 18);
            out[n++] = (char)(0x80 | (cp >> 12 & 0x3F));
            out[n++] = (char)(0x80 | (cp >> 6 & 0x3F));
            out[n++] = (char)(0x80 | (cp & 0x3F));
        } else {
            break;          /* no room left */
        }
    }
    while (n && out[n - 1] == ' ')
        n--;
    out[n] = '\0';
}

/* --- Helpers ---------------------------------------------------------------- */

typedef struct {
    char *artist, *title;
    size_t artist_size, title_size;
    char genre[64];
} Tags;

static unsigned long be32(const unsigned char *p)
{
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[3] << 24 | (unsigned long)p[2] << 16 | (unsigned long)p[1] << 8 | p[0];
}

static unsigned long syncsafe32(const unsigned char *p)
{
    return (unsigned long)(p[0] & 0x7F) << 21 | (unsigned long)(p[1] & 0x7F) << 14 |
           (unsigned long)(p[2] & 0x7F) << 7 | (p[3] & 0x7F);
}

static int read_at(FILE *f, long offset, void *buffer, size_t size)
{
    return fseek(f, offset, SEEK_SET) == 0 && fread(buffer, 1, size, f) == size;
}

/* --- ID3 -------------------------------------------------------------------- */

/* Returns the total size of an ID3v2 tag at the start of the file (0 if none). */
static long id3v2_read(FILE *f, Tags *tags)
{
    static const int encodings[4] = { TEXT_LATIN1, TEXT_UTF16, TEXT_UTF16BE, TEXT_UTF8 };
    unsigned char header[10], text[MAX_TEXT];
    long pos = 10, end;
    int version, id_len, header_len;

    if (!read_at(f, 0, header, 10) || memcmp(header, "ID3", 3) != 0)
        return 0;
    version = header[3];
    end = 10 + (long)syncsafe32(header + 6);
    if (version < 2 || version > 4)
        return end;
    id_len = version == 2 ? 3 : 4;
    header_len = version == 2 ? 6 : 10;
    if (version >= 3 && (header[5] & 0x40)) {       /* extended header */
        if (!read_at(f, pos, text, 4))
            return end;
        pos += version == 3 ? 4 + (long)be32(text) : (long)syncsafe32(text);
    }

    while (pos + header_len <= end && read_at(f, pos, header, (size_t)header_len) && header[0]) {
        unsigned long size = version == 2 ? be32(header + 2) & 0xFFFFFF :
                             version == 3 ? be32(header + 4) : syncsafe32(header + 4);
        int is_title = memcmp(header, version == 2 ? "TT2" : "TIT2", (size_t)id_len) == 0;
        int is_artist = memcmp(header, version == 2 ? "TP1" : "TPE1", (size_t)id_len) == 0;

        pos += header_len;
        if (size > (unsigned long)(end - pos))
            break;
        int is_genre = memcmp(header, version == 2 ? "TCO" : "TCON", (size_t)id_len) == 0;

        if ((is_title || is_artist || is_genre) && size > 1) {
            size_t len = size > MAX_TEXT ? MAX_TEXT : size;

            if (fread(text, 1, len, f) == len && text[0] < 4) {
                if (is_genre)
                    text_to_utf8(tags->genre, sizeof tags->genre, text + 1, len - 1, encodings[text[0]]);
                else if (is_title)
                    text_to_utf8(tags->title, tags->title_size, text + 1, len - 1, encodings[text[0]]);
                else
                    text_to_utf8(tags->artist, tags->artist_size, text + 1, len - 1, encodings[text[0]]);
            }
        }
        pos += (long)size;
    }
    return end;
}

static void id3v1_read(FILE *f, Tags *tags)
{
    unsigned char tag[128];

    if (fseek(f, -128, SEEK_END) != 0 || fread(tag, 1, 128, f) != 128 || memcmp(tag, "TAG", 3) != 0)
        return;
    /* Only fills what the newer tag formats left empty. */
    if (!tags->title[0] && !tags->artist[0]) {
        text_to_utf8(tags->title, tags->title_size, tag + 3, 30, TEXT_LATIN1);
        text_to_utf8(tags->artist, tags->artist_size, tag + 33, 30, TEXT_LATIN1);
    }
    if (!tags->genre[0] && tag[127] != 255)
        snprintf(tags->genre, sizeof tags->genre, "(%d)", tag[127]);     /* numbered genre */
}

/* --- Vorbis comments (FLAC, Ogg Vorbis, Opus) ------------------------------- */

static int key_matches(const unsigned char *entry, size_t len, const char *key)
{
    size_t i, key_len = strlen(key);

    if (len <= key_len || entry[key_len] != '=')
        return 0;
    for (i = 0; i < key_len; i++)
        if ((entry[i] & ~0x20) != key[i])
            return 0;
    return 1;
}

/* `data` may be a truncated block; everything is bounds-checked. */
static void vorbis_comments_read(const unsigned char *data, size_t size, Tags *tags)
{
    unsigned long vendor, count, i;
    size_t pos;

    if (size < 8)
        return;
    vendor = le32(data);
    if (vendor > size - 8)
        return;
    pos = 4 + vendor;
    count = le32(data + pos);
    pos += 4;
    for (i = 0; i < count && size - pos >= 4; i++) {
        unsigned long len = le32(data + pos);

        pos += 4;
        if (len > size - pos)
            break;
        if (key_matches(data + pos, len, "TITLE") && !tags->title[0])
            text_to_utf8(tags->title, tags->title_size, data + pos + 6, len - 6, TEXT_UTF8);
        else if (key_matches(data + pos, len, "ARTIST") && !tags->artist[0])
            text_to_utf8(tags->artist, tags->artist_size, data + pos + 7, len - 7, TEXT_UTF8);
        else if (key_matches(data + pos, len, "GENRE") && !tags->genre[0])
            text_to_utf8(tags->genre, sizeof tags->genre, data + pos + 6, len - 6, TEXT_UTF8);
        pos += len;
    }
}

static void flac_read(FILE *f, long pos, Tags *tags)
{
    unsigned char header[4], *block;

    pos += 4;       /* "fLaC" */
    while (read_at(f, pos, header, 4)) {
        size_t size = be32(header) & 0xFFFFFF, len = size > MAX_COMMENTS ? MAX_COMMENTS : size;

        if ((header[0] & 0x7F) == 4) {      /* VORBIS_COMMENT */
            block = malloc(len ? len : 1);
            if (block && fread(block, 1, len, f) == len)
                vorbis_comments_read(block, len, tags);
            free(block);
            return;
        }
        if (header[0] & 0x80)
            return;     /* last block */
        pos += 4 + (long)size;
    }
}

/* The comments are the second packet of an Ogg stream; it starts on the
 * second page and may continue over several more. */
static void ogg_read(FILE *f, Tags *tags)
{
    unsigned char header[27], lacing[255], *packet = malloc(MAX_COMMENTS);
    size_t len = 0, skip;
    long pos = 0;
    int page, i, done = 0;

    if (!packet)
        return;
    for (page = 0; !done && read_at(f, pos, header, 27) && memcmp(header, "OggS", 4) == 0; page++) {
        if (fread(lacing, 1, header[26], f) != header[26])
            break;
        pos += 27 + header[26];
        for (i = 0; i < header[26]; i++) {
            if (page > 0 && !done) {
                size_t want = lacing[i] > MAX_COMMENTS - len ? MAX_COMMENTS - len : lacing[i];

                if (!read_at(f, pos, packet + len, want)) {
                    done = 1;
                    break;
                }
                len += want;
                if (lacing[i] < 255)
                    done = 1;       /* end of the packet */
            }
            pos += lacing[i];
        }
    }
    skip = len >= 7 && memcmp(packet, "\x03vorbis", 7) == 0 ? 7 :
           len >= 8 && memcmp(packet, "OpusTags", 8) == 0 ? 8 : 0;
    if (skip)
        vorbis_comments_read(packet + skip, len - skip, tags);
    free(packet);
}

/* --- MP4 -------------------------------------------------------------------- */

/* Finds a child atom of the given type inside [start, end). On success the
 * range is narrowed to that atom's body. */
static int mp4_find(FILE *f, long *start, long *end, const char *type)
{
    unsigned char header[16];
    long pos = *start;

    while (pos + 8 <= *end && read_at(f, pos, header, 8)) {
        unsigned long size = be32(header);
        long body = pos + 8;

        if (size == 1) {            /* 64-bit size */
            if (!read_at(f, pos + 8, header + 8, 8) || be32(header + 8) != 0)
                return 0;
            size = be32(header + 12);
            body += 8;
        } else if (size == 0) {     /* extends to the end */
            size = (unsigned long)(*end - pos);
        }
        if (size < (unsigned long)(body - pos) || size > (unsigned long)(*end - pos))
            return 0;
        if (memcmp(header + 4, type, 4) == 0) {
            *start = body;
            *end = pos + (long)size;
            return 1;
        }
        pos += (long)size;
    }
    return 0;
}

static void mp4_text(FILE *f, long start, long end, const char *type, char *out, size_t out_size)
{
    unsigned char text[MAX_TEXT];
    size_t len;

    /* item -> "data" atom: 4 bytes type, 4 bytes locale, then UTF-8 text */
    if (!mp4_find(f, &start, &end, type) || !mp4_find(f, &start, &end, "data") || end - start <= 8)
        return;
    len = (size_t)(end - start - 8);
    if (len > MAX_TEXT)
        len = MAX_TEXT;
    if (read_at(f, start + 8, text, len))
        text_to_utf8(out, out_size, text, len, TEXT_UTF8);
}

static void mp4_read(FILE *f, Tags *tags)
{
    long start = 0, end;

    if (fseek(f, 0, SEEK_END) != 0 || (end = ftell(f)) <= 0)
        return;
    if (!mp4_find(f, &start, &end, "moov") || !mp4_find(f, &start, &end, "udta") ||
        !mp4_find(f, &start, &end, "meta"))
        return;
    start += 4;     /* "meta" carries a version/flags word before its children */
    if (!mp4_find(f, &start, &end, "ilst"))
        return;
    mp4_text(f, start, end, "\xA9nam", tags->title, tags->title_size);
    mp4_text(f, start, end, "\xA9" "ART", tags->artist, tags->artist_size);
    mp4_text(f, start, end, "\xA9gen", tags->genre, sizeof tags->genre);
}

/* --- Tracker modules -------------------------------------------------------- */

static void module_read(FILE *f, const char *path, Tags *tags)
{
    static const struct { const char *ext; long offset; size_t len; } names[] = {
        { ".mod", 0, 20 }, { ".xm", 17, 20 }, { ".s3m", 0, 28 }, { ".it", 4, 26 },
    };
    unsigned char name[32];
    int i;

    for (i = 0; i < ARRAY_LEN(names); i++) {
        if (path_has_extension(path, names[i].ext) && read_at(f, names[i].offset, name, names[i].len)) {
            text_to_utf8(tags->title, tags->title_size, name, names[i].len, TEXT_LATIN1);
            return;
        }
    }
}

/* --- Entry point ------------------------------------------------------------ */

static void read_all(const char *path, Tags *tags)
{
    unsigned char magic[8];
    FILE *f;
    long start;

    tags->artist[0] = tags->title[0] = tags->genre[0] = '\0';
    f = plat_fopen(path, "rb");
    if (!f)
        return;

    start = id3v2_read(f, tags);
    memset(magic, 0, sizeof magic);
    if (read_at(f, start, magic, sizeof magic)) {
        if (memcmp(magic, "fLaC", 4) == 0)
            flac_read(f, start, tags);
        else if (memcmp(magic, "OggS", 4) == 0)
            ogg_read(f, tags);
        else if (memcmp(magic + 4, "ftyp", 4) == 0)
            mp4_read(f, tags);
        else
            module_read(f, path, tags);
    }
    if ((!tags->title[0] && !tags->artist[0]) || !tags->genre[0])
        id3v1_read(f, tags);
    fclose(f);
}

int tags_read(const char *path, char *artist, size_t artist_size, char *title, size_t title_size)
{
    Tags tags = { artist, title, artist_size, title_size, "" };

    read_all(path, &tags);
    return title[0] != '\0';
}

void tags_read_genre(const char *path, char *genre, size_t genre_size)
{
    char artist[4], title[4];
    Tags tags = { artist, title, sizeof artist, sizeof title, "" };

    read_all(path, &tags);
    snprintf(genre, genre_size, "%s", tags.genre);
}
