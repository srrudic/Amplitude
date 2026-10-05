#include "zip.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARCHIVE_SIZE (64u << 20)
#define MAX_ENTRY_SIZE   (32u << 20)

/* --- Inflate (RFC 1951) ------------------------------------------------------
 * A compact bit-at-a-time decoder: slow by zlib standards, but skins are
 * a few hundred kilobytes and this keeps the executable small. */

typedef struct {
    const unsigned char *in;
    size_t in_len, in_pos;
    uint32_t bit_buf;
    int bit_count;
    unsigned char *out;
    size_t out_len, out_pos;
    int error;
} Inflater;

typedef struct {
    short count[16];        /* number of codes of each bit length */
    short symbol[288];      /* symbols ordered by code */
} Huffman;

static int get_bits(Inflater *s, int need)
{
    uint32_t value = s->bit_buf;

    while (s->bit_count < need) {
        if (s->in_pos == s->in_len) {
            s->error = 1;
            return 0;
        }
        value |= (uint32_t)s->in[s->in_pos++] << s->bit_count;
        s->bit_count += 8;
    }
    s->bit_buf = value >> need;
    s->bit_count -= need;
    return (int)(value & ((1u << need) - 1));
}

static void huffman_build(Huffman *h, const short *lengths, int n)
{
    short offsets[16];
    int i;

    memset(h->count, 0, sizeof h->count);
    for (i = 0; i < n; i++)
        h->count[lengths[i]]++;
    offsets[1] = 0;
    for (i = 1; i < 15; i++)
        offsets[i + 1] = offsets[i] + h->count[i];
    for (i = 0; i < n; i++)
        if (lengths[i])
            h->symbol[offsets[lengths[i]]++] = (short)i;
}

static int huffman_decode(Inflater *s, const Huffman *h)
{
    int code = 0, first = 0, index = 0, len;

    for (len = 1; len <= 15; len++) {
        int count = h->count[len];

        code |= get_bits(s, 1);
        if (s->error)
            return -1;
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    s->error = 1;
    return -1;
}

static void inflate_codes(Inflater *s, const Huffman *lit, const Huffman *dist)
{
    static const short len_base[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const short len_extra[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const short dist_base[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
        1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const short dist_extra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
        9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

    while (!s->error) {
        int sym = huffman_decode(s, lit);

        if (sym < 0)
            return;
        if (sym < 256) {
            if (s->out_pos == s->out_len) {
                s->error = 1;
                return;
            }
            s->out[s->out_pos++] = (unsigned char)sym;
        } else if (sym == 256) {
            return;
        } else {
            size_t len, distance;

            sym -= 257;
            if (sym >= 29)
                break;
            len = (size_t)(len_base[sym] + get_bits(s, len_extra[sym]));
            sym = huffman_decode(s, dist);
            if (sym < 0 || sym >= 30)
                break;
            distance = (size_t)(dist_base[sym] + get_bits(s, dist_extra[sym]));
            if (s->error || distance > s->out_pos || len > s->out_len - s->out_pos)
                break;
            /* Byte by byte: source and destination may overlap. */
            for (; len; len--, s->out_pos++)
                s->out[s->out_pos] = s->out[s->out_pos - distance];
        }
    }
    s->error = 1;
}

static void inflate_stored(Inflater *s)
{
    size_t len;

    s->bit_buf = 0;
    s->bit_count = 0;
    if (s->in_len - s->in_pos < 4) {
        s->error = 1;
        return;
    }
    len = rd16(s->in + s->in_pos);
    if ((len ^ 0xFFFF) != rd16(s->in + s->in_pos + 2)) {
        s->error = 1;
        return;
    }
    s->in_pos += 4;
    if (len > s->in_len - s->in_pos || len > s->out_len - s->out_pos) {
        s->error = 1;
        return;
    }
    memcpy(s->out + s->out_pos, s->in + s->in_pos, len);
    s->in_pos += len;
    s->out_pos += len;
}

static void inflate_fixed(Inflater *s)
{
    Huffman lit, dist;
    short lengths[288];
    int i;

    for (i = 0; i < 288; i++)
        lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
    huffman_build(&lit, lengths, 288);
    for (i = 0; i < 30; i++)
        lengths[i] = 5;
    huffman_build(&dist, lengths, 30);
    inflate_codes(s, &lit, &dist);
}

static void inflate_dynamic(Inflater *s)
{
    static const unsigned char order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    Huffman lit, dist;
    short lengths[288 + 32];
    int nlit = get_bits(s, 5) + 257;
    int ndist = get_bits(s, 5) + 1;
    int ncode = get_bits(s, 4) + 4;
    int i;

    if (s->error || nlit > 286 || ndist > 30) {
        s->error = 1;
        return;
    }
    memset(lengths, 0, sizeof lengths);
    for (i = 0; i < ncode; i++)
        lengths[order[i]] = (short)get_bits(s, 3);
    huffman_build(&lit, lengths, 19);

    for (i = 0; i < nlit + ndist; ) {
        int sym = huffman_decode(s, &lit);
        int repeat, value = 0;

        if (sym < 0)
            return;
        if (sym < 16) {
            lengths[i++] = (short)sym;
            continue;
        }
        if (sym == 16) {
            if (i == 0) {
                s->error = 1;
                return;
            }
            value = lengths[i - 1];
            repeat = 3 + get_bits(s, 2);
        } else if (sym == 17) {
            repeat = 3 + get_bits(s, 3);
        } else {
            repeat = 11 + get_bits(s, 7);
        }
        if (s->error || i + repeat > nlit + ndist) {
            s->error = 1;
            return;
        }
        while (repeat--)
            lengths[i++] = (short)value;
    }
    huffman_build(&lit, lengths, nlit);
    huffman_build(&dist, lengths + nlit, ndist);
    inflate_codes(s, &lit, &dist);
}

/* Returns 1 if exactly out_len bytes were produced. */
static int inflate_raw(const unsigned char *in, size_t in_len, unsigned char *out, size_t out_len)
{
    Inflater s;
    int last;

    memset(&s, 0, sizeof s);
    s.in = in;
    s.in_len = in_len;
    s.out = out;
    s.out_len = out_len;
    do {
        last = get_bits(&s, 1);
        switch (get_bits(&s, 2)) {
        case 0:  inflate_stored(&s); break;
        case 1:  inflate_fixed(&s); break;
        case 2:  inflate_dynamic(&s); break;
        default: s.error = 1; break;
        }
    } while (!last && !s.error);
    return !s.error && s.out_pos == out_len;
}

/* --- ZIP container ----------------------------------------------------------- */

#define SIG_END_OF_DIR   0x06054b50
#define SIG_DIR_ENTRY    0x02014b50
#define SIG_LOCAL_HEADER 0x04034b50

int zip_open(ZipArchive *zip, const char *path)
{
    FILE *f = plat_fopen(path, "rb");
    long size;
    size_t pos;

    memset(zip, 0, sizeof *zip);
    if (!f)
        return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 22 ||
        (unsigned long)size > MAX_ARCHIVE_SIZE || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    zip->size = (size_t)size;
    zip->data = malloc(zip->size);
    if (!zip->data || fread(zip->data, 1, zip->size, f) != zip->size) {
        fclose(f);
        zip_close(zip);
        return 0;
    }
    fclose(f);

    /* The end-of-directory record sits at the end, before an optional comment. */
    for (pos = zip->size - 22; ; pos--) {
        if (rd32(zip->data + pos) == SIG_END_OF_DIR) {
            zip->entry_count = rd16(zip->data + pos + 10);
            zip->dir_offset = rd32(zip->data + pos + 16);
            if (zip->dir_offset < zip->size)
                return 1;
            break;
        }
        if (pos == 0 || zip->size - pos >= 22 + 65535)
            break;
    }
    zip_close(zip);
    return 0;
}

void zip_close(ZipArchive *zip)
{
    free(zip->data);
    memset(zip, 0, sizeof *zip);
}

static int lower(int ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

static int name_matches(const unsigned char *entry, size_t len, const char *name)
{
    size_t i, start = 0;

    for (i = 0; i < len; i++)
        if (entry[i] == '/' || entry[i] == '\\')
            start = i + 1;
    if (len - start != strlen(name))
        return 0;
    for (i = start; i < len; i++)
        if (lower(entry[i]) != lower((unsigned char)name[i - start]))
            return 0;
    return 1;
}

static unsigned char *extract(const ZipArchive *zip, const unsigned char *entry, size_t *size)
{
    unsigned method = rd16(entry + 10);
    size_t packed = rd32(entry + 20);
    size_t unpacked = rd32(entry + 24);
    size_t local = rd32(entry + 42);
    size_t start;
    unsigned char *out;

    if (local > zip->size || zip->size - local < 30 || rd32(zip->data + local) != SIG_LOCAL_HEADER)
        return NULL;
    start = local + 30 + rd16(zip->data + local + 26) + rd16(zip->data + local + 28);
    if (start > zip->size || packed > zip->size - start || unpacked > MAX_ENTRY_SIZE)
        return NULL;
    out = malloc(unpacked + 1);
    if (!out)
        return NULL;

    if (method == 0 && packed == unpacked) {
        memcpy(out, zip->data + start, unpacked);
    } else if (method != 8 || !inflate_raw(zip->data + start, packed, out, unpacked)) {
        free(out);
        return NULL;
    }
    out[unpacked] = '\0';
    *size = unpacked;
    return out;
}

unsigned char *zip_read(const ZipArchive *zip, const char *name, size_t *size)
{
    size_t pos = zip->dir_offset;
    unsigned i;

    for (i = 0; i < zip->entry_count; i++) {
        const unsigned char *entry = zip->data + pos;
        size_t name_len;

        if (zip->size - pos < 46 || rd32(entry) != SIG_DIR_ENTRY)
            break;
        name_len = rd16(entry + 28);
        if (zip->size - pos - 46 < name_len)
            break;
        if (name_matches(entry + 46, name_len, name))
            return extract(zip, entry, size);
        pos += 46 + name_len + rd16(entry + 30) + rd16(entry + 32);
        if (pos > zip->size)
            break;
    }
    return NULL;
}
