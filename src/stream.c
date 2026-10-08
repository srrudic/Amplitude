/* See stream.h. A small HTTP client: one GET per connection, no keep-alive,
 * no compression, which is all a stream needs.
 *
 * HTTP Live Streaming ("HLS", an address ending in .m3u8): instead of one
 * endless response there is a playlist, fetched again every few seconds,
 * naming short files of audio that are fetched one after the other. Their
 * contents are put into the buffer as if they had come as one stream. The
 * files hold bare AAC or MP3, an MPEG transport stream, or fragments of an
 * MP4 file; from the last two the audio is picked out (and for MP4, where
 * AAC frames are stored bare, each given the header a decoder of a plain
 * AAC stream expects). Encrypted streams are refused, as are those that
 * keep all segments in one file and name them by byte range.
 *
 * Station metadata ("ICY"): asked for with the Icy-MetaData header. A
 * server that agrees says how many bytes of audio come between two blocks
 * of metadata (icy-metaint); each block is a length byte (in units of 16)
 * followed by text such as  StreamTitle='Artist - Song';  which is taken
 * out of the audio here. */
#include "stream.h"

#include "platform.h"
#include "tags.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

#ifndef AMPLITUDE_VERSION
#define AMPLITUDE_VERSION "dev"
#endif

#define RING_SIZE       (256 * 1024)    /* about 6 s at 320 kbit/s, 16 s at 128 */
#define HEADER_MAX      8192
#define PLAYLIST_MAX    (256 * 1024)
#define SEGMENT_MAX     (16 * 1024 * 1024)
#define HLS_LIVE_BACK   3               /* a live stream is joined this many segments from its end */
#define HLS_MAX_FAILURES 6              /* fetches in a row */
#define TS_PACKET       188
#define TS_SYNC         0x47
#define ADTS_HEADER     7
#define ADTS_MAX_FRAME  8191
#define MAX_REDIRECTS   5
#define META_MAX        (255 * 16)

struct Stream {
    char url[1024];
    volatile int lock;          /* guards conn, title */
    PlatConn *conn;
    volatile int state;
    volatile int quit;          /* stream_close() was called */
    volatile int released;      /* ... and has finished touching the stream */

    /* The buffer: head and tail only ever grow; their difference is the fill. */
    unsigned char *ring;
    volatile size_t head, tail;

    char content_type[64], name[128];
    int bitrate;

    /* ICY metadata */
    int meta_interval;          /* audio bytes between blocks; 0 = no metadata */
    int until_meta;             /* audio bytes left before the next block */
    int meta_left;              /* bytes of the current block still to come; -1 = at its length byte */
    int meta_len;
    char meta[META_MAX + 1];
    char title[256];
    volatile int title_new;

    /* HTTP Live Streaming */
    char *hls_playlist;         /* the playlist as first fetched; s->url is its address */
    int hls_open;               /* the first audio has been seen */
    int ts_pmt_pid, ts_audio_pid;   /* transport stream: where the audio is; 0 = not known yet */
    char mp4_map[1024];         /* MP4 fragments: the address of the file describing the tracks, */
    unsigned long mp4_track;    /* the AAC track in it (0 = none found), */
    int mp4_profile, mp4_rate_index, mp4_channels;  /* and what its frame headers must say */
};

static void lock(Stream *s)
{
    while (__sync_lock_test_and_set(&s->lock, 1))
        plat_sleep_ms(1);
}

static void unlock(Stream *s)
{
    __sync_lock_release(&s->lock);
}

/* --- Addresses and headers ---------------------------------------------------- */

static int starts_with(const char *text, const char *prefix)
{
    for (; *prefix; text++, prefix++) {
        int a = *text, b = *prefix;

        if (a >= 'A' && a <= 'Z')
            a += 'a' - 'A';
        if (a != b)
            return 0;
    }
    return 1;
}

int stream_parse_url(const char *url, int *secure, char *host, size_t host_size, int *port,
                     char *path, size_t path_size)
{
    const char *p, *end, *colon = NULL;
    size_t len;

    if (starts_with(url, "http://"))
        *secure = 0;
    else if (starts_with(url, "https://"))
        *secure = 1;
    else
        return 0;
    p = url + (*secure ? 8 : 7);
    for (end = p; *end && *end != '/' && *end != '?' && *end != '#'; end++)
        if (*end == ':')
            colon = end;
        else if (*end == '@')
            return 0;       /* user:password@ is not supported */
    len = (size_t)((colon ? colon : end) - p);
    if (!len || len >= host_size)
        return 0;
    memcpy(host, p, len);
    host[len] = '\0';
    *port = colon ? atoi(colon + 1) : *secure ? 443 : 80;
    if (*port <= 0 || *port > 65535)
        return 0;
    /* "http://host?x" has no slash; the path is everything from the host on */
    snprintf(path, path_size, "%s%s", *end == '/' ? "" : "/", end);
    len = strcspn(path, "#");
    path[len] = '\0';
    return 1;
}

/* The value of a header in a block of header lines, or NULL. The name is
 * given in lower case with its colon. */
static const char *header_value(const char *headers, const char *name, char *out, size_t out_size)
{
    const char *line = headers;

    while (line && *line) {
        const char *next = strstr(line, "\r\n");

        if (starts_with(line, name)) {
            const char *value = line + strlen(name);
            size_t len;

            while (*value == ' ' || *value == '\t')
                value++;
            len = next ? (size_t)(next - value) : strlen(value);
            if (len >= out_size)
                len = out_size - 1;
            memcpy(out, value, len);
            out[len] = '\0';
            return out;
        }
        line = next ? next + 2 : NULL;
    }
    return NULL;
}

/* "HTTP/1.1 200 OK" or the old Shoutcast "ICY 200 OK": the number. */
static int status_code(const char *headers)
{
    const char *space = strchr(headers, ' ');

    if (!space || (!starts_with(headers, "http/") && !starts_with(headers, "icy")))
        return 0;
    return atoi(space + 1);
}

/* --- The buffer --------------------------------------------------------------- */

size_t stream_buffered(const Stream *s)
{
    return s->head - s->tail;
}

/* Adds audio bytes, waiting for room. Returns 0 if the stream was closed
 * meanwhile. */
static int ring_write(Stream *s, const unsigned char *data, size_t size)
{
    while (size) {
        size_t room = RING_SIZE - (s->head - s->tail), at = s->head % RING_SIZE;
        size_t n = size < room ? size : room;

        if (s->quit)
            return 0;
        if (!n) {
            plat_sleep_ms(10);      /* full: the player is paused or slower than the server */
            continue;
        }
        if (n > RING_SIZE - at)
            n = RING_SIZE - at;
        memcpy(s->ring + at, data, n);
        __sync_synchronize();
        s->head += n;
        data += n;
        size -= n;
    }
    return 1;
}

size_t stream_read(Stream *s, void *out, size_t size)
{
    unsigned char *dst = out;
    size_t done = 0;

    while (done < size) {
        size_t have = s->head - s->tail, at = s->tail % RING_SIZE, n = size - done;

        if (n > have)
            n = have;
        if (n > RING_SIZE - at)
            n = RING_SIZE - at;
        if (!n)
            break;
        memcpy(dst + done, s->ring + at, n);
        __sync_synchronize();
        s->tail += n;
        done += n;
    }
    return done;
}

size_t stream_read_wait(Stream *s, void *out, size_t size, int timeout_ms)
{
    size_t done = 0;
    int waited = 0;

    for (;;) {
        done += stream_read(s, (unsigned char *)out + done, size - done);
        if (done == size || s->state >= STREAM_ENDED || waited >= timeout_ms)
            return done;
        plat_sleep_ms(10);
        waited += 10;
    }
}

/* --- Metadata ------------------------------------------------------------------ */

static void set_title(Stream *s, const char *text)
{
    lock(s);
    if (strcmp(text, s->title) != 0) {
        snprintf(s->title, sizeof s->title, "%s", text);
        s->title_new = 1;
    }
    unlock(s);
}

static void metadata_block(Stream *s)
{
    const char *start = strstr(s->meta, "StreamTitle='"), *end;
    char text[sizeof s->title];

    if (!start)
        return;
    start += 13;
    end = strstr(start, "';");
    if (!end)
        end = start + strlen(start);
    text_to_utf8(text, sizeof text, (const unsigned char *)start, (size_t)(end - start), TEXT_UTF8);
    set_title(s, text);
}

/* Passes received bytes on to the buffer, taking metadata blocks out. */
static int feed(Stream *s, const unsigned char *data, size_t size)
{
    if (!s->meta_interval)
        return ring_write(s, data, size);
    while (size) {
        if (s->until_meta > 0) {
            size_t n = size < (size_t)s->until_meta ? size : (size_t)s->until_meta;

            if (!ring_write(s, data, n))
                return 0;
            s->until_meta -= (int)n;
            data += n;
            size -= n;
        } else if (s->meta_left < 0) {      /* the length byte */
            s->meta_left = *data * 16;
            s->meta_len = 0;
            data++;
            size--;
            if (!s->meta_left) {            /* an empty block: nothing changed */
                s->until_meta = s->meta_interval;
                s->meta_left = -1;
            }
        } else {
            size_t n = size < (size_t)s->meta_left ? size : (size_t)s->meta_left;

            memcpy(s->meta + s->meta_len, data, n);
            s->meta_len += (int)n;
            s->meta_left -= (int)n;
            data += n;
            size -= n;
            if (!s->meta_left) {
                s->meta[s->meta_len] = '\0';
                metadata_block(s);
                s->until_meta = s->meta_interval;
                s->meta_left = -1;
            }
        }
    }
    return 1;
}

/* --- Connecting ---------------------------------------------------------------- */

static void set_conn(Stream *s, PlatConn *conn)
{
    lock(s);
    s->conn = conn;
    unlock(s);
}

static void drop_conn(Stream *s)
{
    lock(s);
    if (s->conn)
        plat_net_close(s->conn);
    s->conn = NULL;
    unlock(s);
}

/* Sends the request for one address and reads the response's header lines
 * into `headers`. Bytes of the body that arrived with them are returned in
 * `extra`. Returns 0 on any failure. */
static int request(Stream *s, const char *url, char *headers, unsigned char *extra, size_t *extra_len)
{
    static const char ours[] = "User-Agent: Amplitude/" AMPLITUDE_VERSION " ( https://amplitude.cr.rs )\r\n"
                               "Icy-MetaData: 1\r\nAccept: */*\r\n";
    char host[256], path[1024], text[1600];
    int secure, port, len = 0, got;
    PlatConn *conn;
    char *end;

    *extra_len = 0;
    drop_conn(s);       /* of an earlier request */
    if (!stream_parse_url(url, &secure, host, sizeof host, &port, path, sizeof path))
        return 0;
    conn = plat_net_connect(host, port, secure);
    if (!conn && secure) {
        /* No way to make a secure connection ourselves; perhaps the system
         * can fetch the address as a whole. */
        conn = plat_https_get(url, ours, headers, HEADER_MAX);
        if (conn)
            set_conn(s, conn);
        return conn != NULL;
    }
    if (!conn)
        return 0;
    set_conn(s, conn);
    snprintf(text, sizeof text, "GET %s HTTP/1.0\r\nHost: %s\r\n%sConnection: close\r\n\r\n", path, host, ours);
    if (!plat_net_send(conn, text, (int)strlen(text)))
        return 0;
    /* The header lines end at the first empty line. */
    for (;;) {
        if (len >= HEADER_MAX - 1 || s->quit)
            return 0;
        got = plat_net_recv(conn, headers + len, HEADER_MAX - 1 - len);
        if (got <= 0)
            return 0;
        len += got;
        headers[len] = '\0';
        end = strstr(headers, "\r\n\r\n");
        if (end)
            break;
    }
    *extra_len = (size_t)(headers + len - (end + 4));
    memcpy(extra, end + 4, *extra_len);
    end[2] = '\0';
    return 1;
}

/* An address found at `base`, which may be given in full, from the root of
 * the same server, or relative to where `base` is. */
static int resolve_url(const char *base, const char *ref, char *out, size_t out_size)
{
    char host[256], path[1024];
    int secure, port, len;

    if (starts_with(ref, "http://") || starts_with(ref, "https://")) {
        len = snprintf(out, out_size, "%s", ref);
    } else {
        if (!stream_parse_url(base, &secure, host, sizeof host, &port, path, sizeof path))
            return 0;
        if (ref[0] == '/' && ref[1] == '/') {
            len = snprintf(out, out_size, "%s:%s", secure ? "https" : "http", ref);
        } else {
            char *slash;

            path[strcspn(path, "?")] = '\0';
            slash = strrchr(path, '/');
            if (ref[0] == '/' || !slash)
                path[0] = '\0';
            else
                slash[1] = '\0';
            len = snprintf(out, out_size, "%s://%s:%d%s%s", secure ? "https" : "http", host, port, path, ref);
        }
    }
    return len > 0 && (size_t)len < out_size;
}

/* Requests `url`, following redirects (`url`, of the size of s->url, ends
 * up as where it led). Returns 1 with the response's header lines in
 * `headers` if the server answered "200 OK"; see request(). */
static int open_url(Stream *s, char *url, char *headers, unsigned char *extra, size_t *extra_len)
{
    char value[1024], next[sizeof s->url];
    int hops, code;

    for (hops = 0; hops <= MAX_REDIRECTS && !s->quit; hops++) {
        if (!request(s, url, headers, extra, extra_len))
            return 0;
        code = status_code(headers);
        if (code == 200)
            return 1;
        if (code < 301 || code > 308 || code == 304 || !header_value(headers, "location:", value, sizeof value) ||
            !resolve_url(url, value, next, sizeof next))
            return 0;
        memcpy(url, next, sizeof next);
    }
    return 0;
}

/* Reads the rest of a response, up to `max` bytes, into memory that the
 * caller frees. `extra` is what request() already took of it. A zero byte
 * is added after the end. NULL if it is too long or the stream was closed. */
static unsigned char *read_body(Stream *s, const unsigned char *extra, size_t extra_len, size_t max, size_t *size)
{
    size_t capacity = extra_len + HEADER_MAX, len = extra_len;
    unsigned char *data = malloc(capacity + 1), *grown;
    int got;

    if (data)
        memcpy(data, extra, extra_len);
    while (data && !s->quit) {
        if (capacity - len < HEADER_MAX) {
            capacity *= 2;
            grown = capacity > max * 2 ? NULL : realloc(data, capacity + 1);
            if (!grown)
                break;
            data = grown;
        }
        got = plat_net_recv(s->conn, data + len, (int)(capacity - len));
        if (got <= 0) {
            data[len] = '\0';
            *size = len;
            return data;
        }
        len += (size_t)got;
    }
    free(data);
    return NULL;
}

/* Fetches the whole of what is at `url`. */
static unsigned char *fetch(Stream *s, char *url, size_t max, size_t *size)
{
    unsigned char *work = malloc(HEADER_MAX * 2), *data = NULL;
    size_t extra_len;

    if (work && open_url(s, url, (char *)work, work + HEADER_MAX, &extra_len))
        data = read_body(s, work + HEADER_MAX, extra_len, max, size);
    free(work);
    drop_conn(s);
    return data;
}

/* A station's .pls or .m3u names the real stream: the first web address in
 * it. */
static int address_in_playlist(const char *text, char *out, size_t out_size)
{
    const char *start, *end;

    for (start = text; *start; start++)
        if (starts_with(start, "http://") || starts_with(start, "https://"))
            break;
    if (!*start)
        return 0;
    end = start + strcspn(start, "\r\n");
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
        end--;
    if ((size_t)(end - start) >= out_size)
        return 0;
    memcpy(out, start, (size_t)(end - start));
    out[end - start] = '\0';
    return 1;
}

enum { FOUND_NOTHING, FOUND_AUDIO, FOUND_HLS };

/* Works its way to the audio: follows redirects and playlist files, then
 * notes what the server said. For an ordinary stream the connection is
 * left open at the first byte of audio (some of which may already be in
 * `extra`); for HLS the playlist is left in s->hls_playlist. */
static int connect_to_audio(Stream *s, unsigned char *extra, size_t *extra_len)
{
    char *headers = malloc(HEADER_MAX), url[sizeof s->url], value[1024];
    int hops, found = FOUND_NOTHING;

    if (!headers)
        return FOUND_NOTHING;
    snprintf(url, sizeof url, "%s", s->url);
    for (hops = 0; hops <= MAX_REDIRECTS && !s->quit; hops++) {
        int is_playlist;

        if (!open_url(s, url, headers, extra, extra_len))
            break;
        s->content_type[0] = '\0';
        if (header_value(headers, "content-type:", value, sizeof value)) {
            size_t i;

            for (i = 0; value[i] && value[i] != ';' && value[i] != ' ' && i + 1 < sizeof s->content_type; i++)
                s->content_type[i] = value[i] >= 'A' && value[i] <= 'Z' ? (char)(value[i] + 32) : value[i];
            s->content_type[i] = '\0';
        }
        is_playlist = strstr(s->content_type, "mpegurl") || strstr(s->content_type, "scpls") ||
                      (!strstr(s->content_type, "audio/mpeg") &&
                       (path_has_extension(url, ".m3u") || path_has_extension(url, ".pls") ||
                        path_has_extension(url, ".m3u8")));
        if (is_playlist) {
            size_t size;
            char *body = (char *)read_body(s, extra, *extra_len, PLAYLIST_MAX, &size);
            int named;

            drop_conn(s);
            if (!body)
                break;
            if (strstr(body, "#EXT-X-")) {
                s->hls_playlist = body;
                s->content_type[0] = '\0';      /* known when the first audio arrives */
                snprintf(s->url, sizeof s->url, "%s", url);
                found = FOUND_HLS;
                break;
            }
            named = address_in_playlist(body, value, sizeof value);
            free(body);
            if (!named)
                break;
            snprintf(url, sizeof url, "%s", value);
            continue;
        }

        if (header_value(headers, "icy-name:", value, sizeof value))
            text_to_utf8(s->name, sizeof s->name, (const unsigned char *)value, strlen(value), TEXT_UTF8);
        if (header_value(headers, "icy-br:", value, sizeof value))
            s->bitrate = atoi(value);
        if (header_value(headers, "icy-metaint:", value, sizeof value) && atoi(value) > 0) {
            s->meta_interval = s->until_meta = atoi(value);
            s->meta_left = -1;
        }
        snprintf(s->url, sizeof s->url, "%s", url);     /* where it really is */
        found = FOUND_AUDIO;
        break;
    }
    free(headers);
    return found;
}

/* --- HTTP Live Streaming ------------------------------------------------------- */

/* Audio from a segment goes into the buffer; the first of it also says what
 * kind it is, upon which the stream counts as open. */
static int hls_audio(Stream *s, const char *type, const unsigned char *data, size_t size)
{
    if (!s->hls_open) {
        if (type != s->content_type)
            snprintf(s->content_type, sizeof s->content_type, "%s", type);
        s->hls_open = 1;
        s->state = STREAM_OPEN;
    }
    return ring_write(s, data, size);
}

/* One packet of an MPEG transport stream. The programme tables say which
 * packets carry the audio; from those the payload is taken, less the header
 * that starts each group of frames. */
static int ts_packet(Stream *s, const unsigned char *p)
{
    int pid = (p[1] & 0x1F) << 8 | p[2], starts = p[1] & 0x40, fields = p[3] >> 4 & 3, at = 4;
    const unsigned char *end = p + TS_PACKET;

    if (p[0] != TS_SYNC || !(fields & 1))
        return 1;
    if (fields & 2)
        at += 1 + p[4];         /* an adaptation field comes first */
    if (at >= TS_PACKET)
        return 1;
    if (pid == 0 || pid == s->ts_pmt_pid) {
        const unsigned char *table, *q, *stop;

        if (!starts)
            return 1;
        table = p + at + 1 + p[at];
        if (table + 12 > end)
            return 1;
        stop = table + 3 + ((table[1] & 0x0F) << 8 | table[2]) - 4;     /* less the checksum */
        if (stop > end)
            stop = end;
        if (pid == 0 && table[0] == 0x00) {             /* the list of programmes: take the first */
            for (q = table + 8; q + 4 <= stop; q += 4)
                if (q[0] || q[1]) {
                    s->ts_pmt_pid = (q[2] & 0x1F) << 8 | q[3];
                    break;
                }
        } else if (pid != 0 && table[0] == 0x02) {      /* what the programme consists of */
            for (q = table + 12 + ((table[10] & 0x0F) << 8 | table[11]); q + 5 <= stop;
                 q += 5 + ((q[3] & 0x0F) << 8 | q[4]))
                if (q[0] == 0x0F || q[0] == 0x03 || q[0] == 0x04) {     /* AAC; MPEG audio */
                    s->ts_audio_pid = (q[1] & 0x1F) << 8 | q[2];
                    if (!s->hls_open)
                        snprintf(s->content_type, sizeof s->content_type, "%s",
                                 q[0] == 0x0F ? "audio/aac" : "audio/mpeg");
                    break;
                }
        }
        return 1;
    }
    if (!s->ts_audio_pid || pid != s->ts_audio_pid)
        return 1;
    if (starts) {
        if (at + 9 > TS_PACKET || p[at] || p[at + 1] || p[at + 2] != 1)
            return 1;
        at += 9 + p[at + 8];
        if (at >= TS_PACKET)
            return 1;
    }
    return hls_audio(s, s->content_type, p + at, (size_t)(TS_PACKET - at));
}

static size_t id3_size(const unsigned char *p)
{
    return (size_t)(p[0] & 0x7F) << 21 | (size_t)(p[1] & 0x7F) << 14 | (size_t)(p[2] & 0x7F) << 7 | (size_t)(p[3] & 0x7F);
}

/* The frames of an ID3v2.4 tag in front of a segment: artist and title, if
 * they are there, become the stream's title. */
static void hls_tag(Stream *s, const unsigned char *frames, size_t size)
{
    char artist[120] = "", title[120] = "", text[sizeof s->title];
    size_t at = 0;

    while (at + 10 <= size && frames[at]) {
        size_t len = id3_size(frames + at + 4);
        char *into = memcmp(frames + at, "TPE1", 4) == 0 ? artist : memcmp(frames + at, "TIT2", 4) == 0 ? title : NULL;

        if (len > size - at - 10)
            break;
        /* Text is given as UTF-8 (3) or Latin-1 (0); text_to_utf8 tells them apart. */
        if (into && len > 1 && (frames[at + 10] == 3 || frames[at + 10] == 0))
            text_to_utf8(into, sizeof artist, frames + at + 11, len - 1, TEXT_UTF8);
        at += 10 + len;
    }
    /* (Between songs some broadcasters send markers like  text="Spot Block End"  there.) */
    if (!title[0] || strstr(title, "=\""))
        return;
    if (artist[0])
        snprintf(text, sizeof text, "%s - %s", artist, title);
    else
        snprintf(text, sizeof text, "%s", title);
    set_title(s, text);
}

/* --- MP4 fragments ---
 * An MP4 file is a tree of "boxes": a length, a four-letter name, contents.
 * One file (the "map") describes the tracks; each segment then holds one or
 * more pairs of a moof box, listing the sizes of the frames, and an mdat
 * box with the frames themselves. */

static unsigned long be32(const unsigned char *p)
{
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

/* Reads a number at *p if it lies before `end`; otherwise 0, and *p is
 * moved to the end so that everything after fails likewise. */
static unsigned long take32(const unsigned char **p, const unsigned char *end)
{
    unsigned long value;

    if (end - *p < 4) {
        *p = end;
        return 0;
    }
    value = be32(*p);
    *p += 4;
    return value;
}

/* The next box called `type` among those in `data`, from *at on, which is
 * moved past it. Returns its contents and their length, or NULL. */
static const unsigned char *mp4_box(const unsigned char *data, size_t size, size_t *at, const char *type, size_t *len)
{
    while (*at + 8 <= size) {
        const unsigned char *box = data + *at;
        size_t whole = be32(box), head = 8;

        if (whole == 1) {               /* a 64-bit length follows the name */
            if (*at + 16 > size || be32(box + 8))
                return NULL;
            whole = be32(box + 12);
            head = 16;
        } else if (whole == 0) {
            whole = size - *at;         /* to the end */
        }
        if (whole < head || whole > size - *at)
            return NULL;
        *at += whole;
        if (memcmp(box + 4, type, 4) == 0) {
            *len = whole - head;
            return box + head;
        }
    }
    return NULL;
}

/* The first box of that name. */
static const unsigned char *mp4_find(const unsigned char *data, size_t size, const char *type, size_t *len)
{
    size_t at = 0;

    return data ? mp4_box(data, size, &at, type, len) : NULL;
}

/* The decoder settings inside an esds box: nested descriptors, each a tag
 * and a length of one to four bytes. Returns the first two bytes' worth of
 * settings in `config`. */
static int mp4_decoder_config(const unsigned char *p, size_t size, unsigned char config[2])
{
    const unsigned char *end = p + size;
    static const int skip_after[] = { 3, 13 };      /* fixed parts of descriptors 3 and 4 */
    int tag;

    p += 4;
    for (tag = 3; tag <= 5; tag++) {
        size_t len = 0;
        int i;

        if (end - p < 2 || *p++ != tag)
            return 0;
        for (i = 0; i < 4 && p < end; i++) {
            len = len << 7 | (*p & 0x7F);
            if (!(*p++ & 0x80))
                break;
        }
        if (tag == 5) {
            if (len < 2 || end - p < 2)
                return 0;
            memcpy(config, p, 2);
            return 1;
        }
        if (end - p < skip_after[tag - 3])
            return 0;
        if (tag == 3 && (p[2] & 0xE0))
            return 0;       /* optional fields nobody uses */
        p += skip_after[tag - 3];
    }
    return 0;
}

/* Reads the map: finds the AAC track and what kind of AAC it is. */
static int mp4_map(Stream *s, const unsigned char *data, size_t size)
{
    size_t moov_len, trak_len, len, at = 0;
    const unsigned char *moov = mp4_find(data, size, "moov", &moov_len), *trak;

    s->mp4_track = 0;
    while (moov && (trak = mp4_box(moov, moov_len, &at, "trak", &trak_len)) != NULL) {
        size_t tkhd_len, entry_len;
        const unsigned char *tkhd = mp4_find(trak, trak_len, "tkhd", &tkhd_len), *entry, *esds;
        const unsigned char *stsd = mp4_find(trak, trak_len, "mdia", &len);
        unsigned char config[2];
        int type;

        stsd = mp4_find(stsd, len, "minf", &len);
        stsd = mp4_find(stsd, len, "stbl", &len);
        stsd = mp4_find(stsd, len, "stsd", &len);
        if (!tkhd || tkhd_len < 24 || !stsd || len < 8)
            continue;
        /* The description of the track's one kind of sample; for sound, 28
         * bytes of fixed fields come before the boxes inside it. */
        entry = mp4_find(stsd + 8, len - 8, "mp4a", &entry_len);
        if (!entry || entry_len < 28)
            continue;       /* video, or sound in another format, or encrypted */
        esds = mp4_find(entry + 28, entry_len - 28, "esds", &len);
        if (!esds || !mp4_decoder_config(esds, len, config))
            continue;
        type = config[0] >> 3;
        if (type == 5 || type == 29)
            type = 2;       /* AAC+ is plain AAC with extras the decoder finds for itself */
        s->mp4_rate_index = (config[0] & 7) << 1 | config[1] >> 7;
        s->mp4_channels = config[1] >> 3 & 15;
        if (type < 1 || type > 4 || s->mp4_rate_index > 12 || s->mp4_channels < 1 || s->mp4_channels > 7)
            continue;       /* a kind of AAC a frame header cannot describe */
        s->mp4_profile = type - 1;
        s->mp4_track = be32(tkhd + (tkhd[0] == 1 ? 20 : 12));
        return s->mp4_track != 0;
    }
    return 0;
}

/* One frame, with its header, into the buffer. */
static int mp4_frame(Stream *s, const unsigned char *frame, size_t size)
{
    size_t whole = size + ADTS_HEADER;
    unsigned char header[ADTS_HEADER];

    header[0] = 0xFF;
    header[1] = 0xF1;
    header[2] = (unsigned char)(s->mp4_profile << 6 | s->mp4_rate_index << 2 | s->mp4_channels >> 2);
    header[3] = (unsigned char)((s->mp4_channels & 3) << 6 | whole >> 11);
    header[4] = (unsigned char)(whole >> 3);
    header[5] = (unsigned char)((whole & 7) << 5 | 0x1F);
    header[6] = 0xFC;
    return hls_audio(s, "audio/aac", header, sizeof header) && hls_audio(s, "audio/aac", frame, size);
}

/* The frames of our track in one segment. */
static int mp4_segment(Stream *s, const unsigned char *data, size_t size)
{
    size_t moof_len, traf_len, len, at = 0;
    const unsigned char *moof, *traf;

    while ((moof = mp4_box(data, size, &at, "moof", &moof_len)) != NULL) {
        size_t in_moof = 0, after = 0;      /* after: where the previous run of frames ended */

        while ((traf = mp4_box(moof, moof_len, &in_moof, "traf", &traf_len)) != NULL) {
            const unsigned char *p = mp4_find(traf, traf_len, "tfhd", &len), *end, *run;
            /* Frames are addressed from the start of the moof box, unless
             * the track says otherwise. */
            size_t base = at - moof_len - 8, in_traf = 0;
            unsigned long flags, track, default_size = 0;

            if (!p)
                continue;
            end = p + len;
            flags = take32(&p, end);
            track = take32(&p, end);
            if (flags & 0x01) {
                if (take32(&p, end))
                    return 1;
                base = take32(&p, end);
            }
            if (flags & 0x02)
                take32(&p, end);
            if (flags & 0x08)
                take32(&p, end);
            if (flags & 0x10)
                default_size = take32(&p, end);
            if (track != s->mp4_track)
                continue;
            while ((run = mp4_box(traf, traf_len, &in_traf, "trun", &len)) != NULL) {
                unsigned long count, i;
                size_t where;

                end = run + len;
                flags = take32(&run, end);
                count = take32(&run, end);
                where = flags & 0x001 ? base + (size_t)(long)(int)take32(&run, end) : after ? after : at + 8;
                if (flags & 0x004)
                    take32(&run, end);
                for (i = 0; i < count && run <= end; i++) {
                    size_t frame = default_size;

                    if (flags & 0x100)
                        take32(&run, end);
                    if (flags & 0x200)
                        frame = take32(&run, end);
                    if (flags & 0x400)
                        take32(&run, end);
                    if (flags & 0x800)
                        take32(&run, end);
                    if (!frame || frame > ADTS_MAX_FRAME - ADTS_HEADER || where > size || frame > size - where)
                        return 1;       /* damaged: leave the rest of the segment */
                    if (!mp4_frame(s, data + where, frame))
                        return 0;
                    where += frame;
                }
                after = where;
            }
        }
    }
    return 1;
}

/* Puts the audio of one segment into the buffer. Returns 0 if it is of a
 * kind not supported, or the stream was closed. */
static int hls_segment(Stream *s, const unsigned char *data, size_t size)
{
    size_t at = 0;

    if (s->mp4_track)
        return mp4_segment(s, data, size);
    if (size >= 2 * TS_PACKET && data[0] == TS_SYNC && data[TS_PACKET] == TS_SYNC) {
        for (; at + TS_PACKET <= size; at += TS_PACKET)
            if (!ts_packet(s, data + at))
                return 0;
        return 1;
    }
    if (size >= 12 && (memcmp(data + 4, "ftyp", 4) == 0 || memcmp(data + 4, "styp", 4) == 0 ||
                       memcmp(data + 4, "moof", 4) == 0))
        return 0;       /* MP4 fragments without a map */
    /* Bare audio, behind one or more tags: one carries the time, and some
     * broadcasters add another with the song. */
    while (at + 10 <= size && memcmp(data + at, "ID3", 3) == 0) {
        size_t tag = id3_size(data + at + 6);

        if (tag <= size - at - 10)
            hls_tag(s, data + at + 10, tag);
        at += 10 + tag;
    }
    if (at + 2 > size)
        return 1;
    return hls_audio(s, data[at] == 0xFF && (data[at + 1] & 0xF6) == 0xF0 ? "audio/aac" : "audio/mpeg",
                     data + at, size - at);
}

/* Copies the line at `text` to `line` and returns where the next starts,
 * or NULL after the last. */
static const char *next_line(const char *text, char *line, size_t line_size)
{
    size_t len = strcspn(text, "\r\n"), kept = len < line_size ? len : line_size - 1;

    if (!*text)
        return NULL;
    memcpy(line, text, kept);
    line[kept] = '\0';
    text += len;
    return text + strspn(text, "\r\n");
}

/* A playlist of playlists, one for each quality (and often the same again
 * from a second server): the address of the best one, or with `rank` 1, 2...
 * of the next best, for when that does not answer. */
static int hls_variant(const char *text, int rank, char *out, size_t out_size, int *bandwidth)
{
    char line[sizeof ((Stream *)0)->url];
    long limit = -1, limit_index = -1;      /* the one chosen in the round before */
    int round;

    for (round = 0; round <= rank; round++) {
        long best = -1, best_index = -1, current = -1, index = 0;
        const char *p = text;

        while ((p = next_line(p, line, sizeof line)) != NULL) {
            if (starts_with(line, "#ext-x-stream-inf:")) {
                const char *value = strstr(line, "BANDWIDTH=");

                current = value ? atol(value + 10) : 0;
            } else if (line[0] && line[0] != '#' && current >= 0) {
                int after_limit = limit < 0 || current < limit || (current == limit && index > limit_index);

                if (after_limit && current > best && strlen(line) < out_size) {
                    best = current;
                    best_index = index;
                    if (round == rank)
                        memcpy(out, line, strlen(line) + 1);
                }
                current = -1;
                index++;
            }
        }
        if (best < 0)
            return 0;
        limit = best;
        limit_index = best_index;
    }
    *bandwidth = (int)limit;
    return 1;
}

/* When the qualities on offer are of a video, its sound is listed apart:
 * the address of the last such listing, which tends to be the best. */
static int hls_sound_of_video(const char *text, char *out, size_t out_size)
{
    char line[sizeof ((Stream *)0)->url];
    int found = 0;

    if (!strstr(text, "RESOLUTION="))
        return 0;
    while ((text = next_line(text, line, sizeof line)) != NULL) {
        const char *uri = strstr(line, "URI=\"");
        size_t len = uri ? strcspn(uri + 5, "\"") : 0;

        if (starts_with(line, "#ext-x-media:") && strstr(line, "TYPE=AUDIO") && len && len < out_size) {
            memcpy(out, uri + 5, len);
            out[len] = '\0';
            found = 1;
        }
    }
    return found;
}

static void hls_wait(Stream *s, int ms)
{
    for (; ms > 0 && !s->quit; ms -= 50)
        plat_sleep_ms(50);
}

/* Plays an HLS stream into the buffer until it ends, fails or is closed. */
static void hls_run(Stream *s)
{
    char url[sizeof s->url], line[sizeof s->url], address[sizeof s->url];
    char *playlist = s->hls_playlist;
    long next = -1;             /* number of the next segment to fetch */
    int failures = 0, bandwidth;

    address[0] = '\0';

    s->hls_playlist = NULL;
    snprintf(url, sizeof url, "%s", s->url);
    if (strstr(playlist, "#EXT-X-STREAM-INF")) {
        char *chosen = NULL;
        size_t size;
        int rank;

        if (hls_sound_of_video(playlist, line, sizeof line) && resolve_url(url, line, address, sizeof address))
            chosen = (char *)fetch(s, address, PLAYLIST_MAX, &size);
        for (rank = 0; !chosen && !s->quit && rank < HLS_MAX_FAILURES &&
                       hls_variant(playlist, rank, line, sizeof line, &bandwidth); rank++) {
            if (resolve_url(url, line, address, sizeof address))
                chosen = (char *)fetch(s, address, PLAYLIST_MAX, &size);
            if (chosen && bandwidth > 0)
                s->bitrate = bandwidth / 1000;
        }
        free(playlist);
        playlist = chosen;
        memcpy(url, address, sizeof url);
    }

    while (playlist && !s->quit) {
        long first = 0, number;
        int target = 6, ended = 0, count = 0, fetched = 0, refused = 0;
        const char *text = playlist;
        char map[sizeof s->mp4_map] = "";
        size_t size;

        while ((text = next_line(text, line, sizeof line)) != NULL) {
            if (starts_with(line, "#ext-x-media-sequence:"))
                first = atol(line + 22);
            else if (starts_with(line, "#ext-x-targetduration:"))
                target = atoi(line + 22);
            else if (starts_with(line, "#ext-x-endlist"))
                ended = 1;
            else if (starts_with(line, "#ext-x-byterange") ||
                     (starts_with(line, "#ext-x-key:") && !strstr(line, "METHOD=NONE")))
                refused = 1;    /* segments as parts of one file; encryption */
            else if (starts_with(line, "#ext-x-map:") && !map[0]) {
                const char *uri = strstr(line, "URI=\"");
                size_t len = uri ? strcspn(uri + 5, "\"") : 0;

                if (!len || len >= sizeof map || strstr(line, "BYTERANGE"))
                    refused = 1;
                else
                    memcpy(map, uri + 5, len);      /* (map is all zeros beyond) */
            } else if (line[0] && line[0] != '#')
                count++;
        }
        /* MP4 fragments: the file that describes them comes first, and
         * again should the broadcaster change it. */
        if (!refused && map[0] && strcmp(map, s->mp4_map) != 0) {
            unsigned char *data = resolve_url(url, map, address, sizeof address) ? fetch(s, address, SEGMENT_MAX, &size)
                                                                               : NULL;

            refused = !data || !mp4_map(s, data, size);
            free(data);
            memcpy(s->mp4_map, map, sizeof map);
        }
        if (refused)
            break;
        /* A broadcast is joined near its end, a finished recording at its
         * start; and if we have fallen behind what is on offer, skip ahead. */
        if (next < 0 && !ended && count > HLS_LIVE_BACK)
            next = first + count - HLS_LIVE_BACK;
        if (next < first)
            next = first;
        for (text = playlist, number = first; (text = next_line(text, line, sizeof line)) != NULL && !s->quit;) {
            unsigned char *data;
            int ok;

            if (!line[0] || line[0] == '#' || number++ < next)
                continue;
            data = resolve_url(url, line, address, sizeof address) ? fetch(s, address, SEGMENT_MAX, &size) : NULL;
            ok = data && hls_segment(s, data, size);
            refused = data && !ok;
            free(data);
            /* A broadcast may lose a segment now and then, but one that
             * cannot even begin is not worth waiting for. */
            if (!ok && !s->hls_open)
                refused = 1;
            if (refused || (!ok && ++failures >= HLS_MAX_FAILURES))
                break;
            next = number;
            if (ok) {
                failures = 0;
                fetched++;
            }
        }
        if (ended || refused || failures >= HLS_MAX_FAILURES || s->quit || (fetched && !s->hls_open))
            break;
        /* The list gains a segment every `target` seconds; look again
         * sooner if it had nothing new. */
        hls_wait(s, (target > 0 ? target : 1) * (fetched ? 500 : 250));
        free(playlist);
        memcpy(address, url, sizeof address);
        while ((playlist = (char *)fetch(s, address, PLAYLIST_MAX, &size)) == NULL && !s->quit &&
               ++failures < HLS_MAX_FAILURES) {
            hls_wait(s, 1000);
            memcpy(address, url, sizeof address);
        }
    }
    free(playlist);
}

static void reader(void *arg)
{
    Stream *s = arg;
    unsigned char *chunk = malloc(HEADER_MAX);
    size_t have = 0;
    int got, found = chunk ? connect_to_audio(s, chunk, &have) : FOUND_NOTHING;

    if (found == FOUND_HLS) {
        hls_run(s);
        s->state = s->hls_open ? STREAM_ENDED : STREAM_FAILED;
    } else if (found == FOUND_AUDIO && feed(s, chunk, have)) {
        s->state = STREAM_OPEN;
        while (!s->quit) {
            got = plat_net_recv(s->conn, chunk, HEADER_MAX);
            if (got <= 0 || !feed(s, chunk, (size_t)got))
                break;
        }
        s->state = STREAM_ENDED;
    } else {
        s->state = STREAM_FAILED;
    }
    free(chunk);
    drop_conn(s);
    /* The stream is the caller's until it lets go of it. */
    while (!s->released)
        plat_sleep_ms(20);
    free(s->ring);
    free(s);
}

/* --- The interface ------------------------------------------------------------- */

char *stream_fetch(const char *url, size_t max, size_t *size)
{
    Stream *s = calloc(1, sizeof *s);       /* only its connection is used */
    char address[sizeof s->url];
    char *data = NULL;

    if (s && strlen(url) < sizeof address) {
        memcpy(address, url, strlen(url) + 1);
        data = (char *)fetch(s, address, max, size);
    }
    free(s);
    return data;
}

Stream *stream_open(const char *url)
{
    Stream *s = calloc(1, sizeof *s);

    if (!s)
        return NULL;
    s->ring = malloc(RING_SIZE);
    snprintf(s->url, sizeof s->url, "%s", url);
    if (!s->ring || !plat_thread_start(reader, s)) {
        free(s->ring);
        free(s);
        return NULL;
    }
    return s;
}

void stream_close(Stream *s)
{
    s->quit = 1;
    lock(s);
    if (s->conn)
        plat_net_abort(s->conn);    /* wakes the thread if it is waiting for data */
    unlock(s);
    s->released = 1;                /* nothing may touch the stream after this */
}

int stream_state(const Stream *s)
{
    return s->state;
}

const char *stream_content_type(const Stream *s)
{
    return s->content_type;
}

const char *stream_name(const Stream *s)
{
    return s->name;
}

int stream_bitrate(const Stream *s)
{
    return s->bitrate;
}

const char *stream_url(const Stream *s)
{
    return s->url;
}

int stream_take_title(Stream *s, char *out, size_t size)
{
    int fresh;

    lock(s);
    fresh = s->title_new;
    if (fresh) {
        snprintf(out, size, "%s", s->title);
        s->title_new = 0;
    }
    unlock(s);
    return fresh;
}
