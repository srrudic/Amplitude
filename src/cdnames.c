/* See cdnames.h. */
#include "cdnames.h"

#include "platform.h"
#include "stream.h"
#include "tags.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef AMPLITUDE_VERSION
#define AMPLITUDE_VERSION "dev"
#endif

#define ANSWER_MAX      (2 * 1024 * 1024)
#define RETRY_SECONDS   60              /* before a disc that was not found is asked about again */
#define LEAD_IN         150             /* sectors before sector 0, which disc IDs count in */
#define MUSICBRAINZ     "https://musicbrainz.org/ws/2/discid/"
#define GNUDB           "http://gnudb.gnudb.org/~cddb/cddb.cgi"
/* gnudb answers only programs whose author has given it an e-mail address,
 * sent with every request as "name+domain". Until one is filled in here,
 * gnudb is not asked. */
#define GNUDB_CONTACT   ""

/* --- Disc IDs ---------------------------------------------------------------- */

/* SHA-1, for the MusicBrainz ID. */
static void sha1(const unsigned char *data, size_t size, unsigned char digest[20])
{
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    uint64_t bits = (uint64_t)size * 8;
    size_t total = (size + 9 + 63) / 64 * 64, at, i;
    unsigned char block[64];

    for (at = 0; at < total; at += 64) {
        uint32_t w[80], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

        /* The message, a 1 bit, zeros, and the length in the last 8 bytes. */
        for (i = 0; i < 64; i++) {
            size_t pos = at + i;

            block[i] = pos < size ? data[pos] : pos == size ? 0x80
                     : pos >= total - 8 ? (unsigned char)(bits >> (8 * (total - 1 - pos))) : 0;
        }
        for (i = 0; i < 16; i++)
            w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
                   (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
        for (i = 16; i < 80; i++) {
            uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];

            w[i] = x << 1 | x >> 31;
        }
        for (i = 0; i < 80; i++) {
            uint32_t f = i < 20 ? ((b & c) | (~b & d)) + 0x5A827999
                       : i < 40 ? (b ^ c ^ d) + 0x6ED9EBA1
                       : i < 60 ? ((b & c) | (b & d) | (c & d)) + 0x8F1BBCDC
                       : (b ^ c ^ d) + 0xCA62C1D6;
            uint32_t next = (a << 5 | a >> 27) + f + e + w[i];

            e = d;
            d = c;
            c = b << 30 | b >> 2;
            b = a;
            a = next;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    for (i = 0; i < 20; i++)
        digest[i] = (unsigned char)(h[i / 4] >> (8 * (3 - i % 4)));
}

/* The audio tracks, which is all MusicBrainz counts: the index of the last. */
static int last_audio(const CdToc *toc)
{
    int i, last = -1;

    for (i = 0; i < toc->count; i++)
        if (toc->track[i].audio)
            last = i;
    return last;
}

/* First and last track number, the end of the last track and where each of
 * up to 99 tracks begins, written out in hexadecimal; the SHA-1 of that, in
 * a kind of base 64 that can stand in an address. */
void cd_names_musicbrainz_id(const CdToc *toc, char id[29])
{
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._";
    char text[2 + 2 + 100 * 8 + 1];
    unsigned char digest[21];
    int i, last = last_audio(toc), len;

    id[0] = '\0';
    if (last < 0)
        return;
    len = sprintf(text, "%02X%02X%08lX", toc->track[0].number, toc->track[last].number,
                  (unsigned long)(toc->track[last].start + toc->track[last].sectors + LEAD_IN));
    for (i = 1; i < 100; i++) {
        int t;

        for (t = 0; t <= last && toc->track[t].number != i; t++)
            ;
        len += sprintf(text + len, "%08lX", t <= last ? (unsigned long)(toc->track[t].start + LEAD_IN) : 0ul);
    }
    sha1((const unsigned char *)text, (size_t)len, digest);
    digest[20] = 0;
    for (i = 0; i < 7; i++) {
        const unsigned char *p = digest + i * 3;

        id[i * 4] = digits[p[0] >> 2];
        id[i * 4 + 1] = digits[(p[0] & 3) << 4 | p[1] >> 4];
        id[i * 4 + 2] = digits[(p[1] & 15) << 2 | p[2] >> 6];
        id[i * 4 + 3] = digits[p[2] & 63];
    }
    id[27] = '-';       /* the padding of the last, incomplete group */
    id[28] = '\0';
}

/* The older ID: the digits of every track's starting second added up, the
 * disc's length in seconds, and the number of tracks. */
unsigned long cd_names_cddb_id(const CdToc *toc)
{
    const CdTrack *last = &toc->track[toc->count - 1];
    unsigned long sum = 0, seconds;
    int i;

    if (!toc->count)
        return 0;
    for (i = 0; i < toc->count; i++)
        for (seconds = (unsigned long)(toc->track[i].start + LEAD_IN) / CD_SECTORS_PER_S; seconds; seconds /= 10)
            sum += seconds % 10;
    seconds = (unsigned long)(last->start + last->sectors + LEAD_IN) / CD_SECTORS_PER_S -
              (unsigned long)(toc->track[0].start + LEAD_IN) / CD_SECTORS_PER_S;
    return (sum % 255) << 24 | seconds << 8 | (unsigned long)toc->count;
}

/* --- MusicBrainz's answer: JSON ---------------------------------------------- */

static const char *js_space(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

/* Steps over one value of any kind. */
static const char *js_skip(const char *p)
{
    int depth = 0;

    p = js_space(p);
    do {
        if (*p == '"') {
            for (p++; *p && *p != '"'; p++)
                if (*p == '\\' && p[1])
                    p++;
        } else if (*p == '{' || *p == '[') {
            depth++;
        } else if (*p == '}' || *p == ']') {
            depth--;
        } else if (!depth) {            /* a number or a word */
            while (*p && *p != ',' && *p != '}' && *p != ']')
                p++;
            return p;
        }
        if (!*p)
            return p;
        p++;
    } while (depth > 0);
    return p;
}

/* The value of `key` in the object at `p`, or NULL. */
static const char *js_get(const char *p, const char *key)
{
    size_t len = strlen(key);

    p = p ? js_space(p) : "";
    if (*p != '{')
        return NULL;
    p = js_space(p + 1);
    while (*p == '"') {
        int found = strncmp(p + 1, key, len) == 0 && p[1 + len] == '"';

        p = js_space(js_skip(p));
        if (*p != ':')
            return NULL;
        p = js_space(p + 1);
        if (found)
            return p;
        p = js_space(js_skip(p));
        if (*p != ',')
            return NULL;
        p = js_space(p + 1);
    }
    return NULL;
}

/* The first element of the array at `p`, and the one after `element`. */
static const char *js_first(const char *p)
{
    p = p ? js_space(p) : "";
    if (*p != '[')
        return NULL;
    p = js_space(p + 1);
    return *p == ']' || !*p ? NULL : p;
}

static const char *js_next(const char *element)
{
    const char *p = js_space(js_skip(element));

    return *p == ',' ? js_space(p + 1) : NULL;
}

/* Copies the string at `p` as UTF-8; adds to what `out` holds already. */
static void js_append(const char *p, char *out, size_t size)
{
    size_t len = strlen(out);

    p = p ? js_space(p) : "";
    if (*p != '"')
        return;
    for (p++; *p && *p != '"' && len + 4 < size; p++) {
        unsigned code = (unsigned char)*p;

        if (*p == '\\' && p[1]) {
            p++;
            code = *p == 'n' || *p == 't' || *p == 'r' ? ' ' : (unsigned char)*p;
            if (*p == 'u') {
                char hex[5] = { 0 };

                strncpy(hex, p + 1, 4);
                code = (unsigned)strtoul(hex, NULL, 16);
                p += strlen(hex);
                if (code >= 0xD800 && code < 0xE000)
                    code = '?';         /* half of a pair: beyond what titles need */
                if (code >= 0x800) {
                    out[len++] = (char)(0xE0 | code >> 12);
                    out[len++] = (char)(0x80 | (code >> 6 & 0x3F));
                    out[len++] = (char)(0x80 | (code & 0x3F));
                    continue;
                }
                if (code >= 0x80) {
                    out[len++] = (char)(0xC0 | code >> 6);
                    out[len++] = (char)(0x80 | (code & 0x3F));
                    continue;
                }
            }
        }
        out[len++] = (char)code;
    }
    out[len] = '\0';
}

/* "artist-credit": the names, with whatever joins them ("A feat. B"). */
static void js_artists(const char *credit, char *out, size_t size)
{
    const char *p;

    out[0] = '\0';
    for (p = js_first(credit); p; p = js_next(p)) {
        js_append(js_get(p, "name"), out, size);
        js_append(js_get(p, "joinphrase"), out, size);
    }
}

int cd_names_parse_musicbrainz(const char *json, const char *id, const CdToc *toc, CdNames *out)
{
    const char *release, *medium, *disc, *track, *found = NULL, *found_release = NULL;
    int audio = 0, i, exact = 0;
    char text[64];

    for (i = 0; i < toc->count; i++)
        audio += toc->track[i].audio;
    /* The disc is one of perhaps several in a release: the one listing our
     * ID, or failing that (a match by track positions alone) the first
     * with as many tracks. */
    for (release = js_first(js_get(json, "releases")); release && !exact; release = js_next(release))
        for (medium = js_first(js_get(release, "media")); medium && !exact; medium = js_next(medium)) {
            int tracks = 0;

            for (disc = js_first(js_get(medium, "discs")); disc && !exact; disc = js_next(disc)) {
                text[0] = '\0';
                js_append(js_get(disc, "id"), text, sizeof text);
                exact = strcmp(text, id) == 0;
            }
            for (track = js_first(js_get(medium, "tracks")); track; track = js_next(track))
                tracks++;
            if (exact || (!found && tracks == audio)) {
                found = medium;
                found_release = release;
            }
        }
    if (!found)
        return 0;

    memset(out, 0, sizeof *out);
    js_append(js_get(found_release, "title"), out->album, sizeof out->album);
    js_artists(js_get(found_release, "artist-credit"), out->artist, sizeof out->artist);
    for (track = js_first(js_get(found, "tracks")); track && out->count < CD_MAX_TRACKS; track = js_next(track)) {
        const char *position = js_get(track, "position");

        out->track[out->count].number = position ? atoi(position) : out->count + 1;
        js_append(js_get(track, "title"), out->track[out->count].title, sizeof out->track[0].title);
        js_artists(js_get(track, "artist-credit"), out->track[out->count].artist, sizeof out->track[0].artist);
        if (strcmp(out->track[out->count].artist, out->artist) == 0)
            out->track[out->count].artist[0] = '\0';
        if (out->track[out->count].title[0])
            out->count++;
    }
    return out->count > 0;
}

/* --- gnudb's answer: the CDDB protocol ----------------------------------------
 * A query is answered with a line  "200 category id Artist / Album", or
 * "210 ..." (or 211) and a list of such matches, one a line; reading one
 * gives lines of  KEY=value  ending with a full stop:  DTITLE=Artist / Album,
 * TTITLE0=..., TTITLE1=... (a long value continues on a line with the same
 * key). */

int cd_names_parse_cddb_query(const char *text, char *category, size_t category_size, char *id, size_t id_size)
{
    int code = atoi(text);
    size_t len;

    if (code == 210 || code == 211) {
        text = strchr(text, '\n');
        if (!text)
            return 0;
        text++;
    } else if (code == 200) {
        text += 4;
    } else {
        return 0;
    }
    len = strcspn(text, " \r\n");
    if (!len || len >= category_size || text[0] == '.' || text[len] != ' ')
        return 0;
    memcpy(category, text, len);
    category[len] = '\0';
    text += len + 1;
    len = strcspn(text, " \r\n");
    if (!len || len >= id_size)
        return 0;
    memcpy(id, text, len);
    id[len] = '\0';
    return 1;
}

/* Splits "Artist / Title" at the first " / ". */
static void split_title(const char *text, char *artist, size_t artist_size, char *title, size_t title_size)
{
    const char *slash = strstr(text, " / ");

    if (slash) {
        snprintf(artist, artist_size, "%.*s", (int)(slash - text), text);
        snprintf(title, title_size, "%s", slash + 3);
    } else {
        artist[0] = '\0';
        snprintf(title, title_size, "%s", text);
    }
}

int cd_names_parse_cddb(const char *text, const CdToc *toc, CdNames *out)
{
    static char titles[CD_MAX_TRACKS][sizeof out->track[0].title];
    char disc[sizeof out->artist + sizeof out->album] = "", raw[400], value[400];
    int i;

    if (atoi(text) != 210)
        return 0;
    memset(titles, 0, sizeof titles);
    while ((text = strchr(text, '\n')) != NULL && *++text && *text != '.') {
        size_t len = strcspn(text, "\r\n");
        const char *equals = memchr(text, '=', len);
        char *into = NULL;
        size_t room = 0;

        if (!equals)
            continue;
        if (strncmp(text, "DTITLE=", 7) == 0) {
            into = disc;
            room = sizeof disc;
        } else if (strncmp(text, "TTITLE", 6) == 0 && atoi(text + 6) >= 0 && atoi(text + 6) < CD_MAX_TRACKS) {
            into = titles[atoi(text + 6)];
            room = sizeof titles[0];
        }
        if (!into)
            continue;
        snprintf(raw, sizeof raw, "%.*s", (int)(len - (size_t)(equals + 1 - text)), equals + 1);
        /* UTF-8 was asked for; old entries may be in Latin-1 all the same. */
        text_to_utf8(value, sizeof value, (const unsigned char *)raw, strlen(raw), TEXT_UTF8);
        snprintf(into + strlen(into), room - strlen(into), "%s", value);
    }

    memset(out, 0, sizeof *out);
    split_title(disc, out->artist, sizeof out->artist, out->album, sizeof out->album);
    for (i = 0; i < toc->count; i++) {
        if (!toc->track[i].audio || !titles[i][0])
            continue;
        out->track[out->count].number = toc->track[i].number;
        /* On a compilation each title names its own artist. */
        split_title(titles[i], out->track[out->count].artist, sizeof out->track[0].artist,
                    out->track[out->count].title, sizeof out->track[0].title);
        out->count++;
    }
    return out->count > 0;
}

/* --- Asking -------------------------------------------------------------------- */

static int ask_musicbrainz(const CdToc *toc, CdNames *out)
{
    char url[2048], id[29];
    int i, last = last_audio(toc), len;
    char *answer;
    size_t size;
    int ok;

    cd_names_musicbrainz_id(toc, id);
    /* With the track positions sent along, a disc nobody has registered is
     * still matched to releases of the same lengths. */
    len = snprintf(url, sizeof url, MUSICBRAINZ "%s?fmt=json&inc=recordings+artist-credits&toc=%d+%d+%ld", id,
                   toc->track[0].number, toc->track[last].number,
                   toc->track[last].start + toc->track[last].sectors + LEAD_IN);
    for (i = 0; i <= last; i++)
        len += snprintf(url + len, sizeof url - (size_t)len, "+%ld", toc->track[i].start + LEAD_IN);
    answer = stream_fetch(url, ANSWER_MAX, &size);
    ok = answer && cd_names_parse_musicbrainz(answer, id, toc, out);
    free(answer);
    return ok;
}

static int ask_gnudb(const CdToc *toc, CdNames *out)
{
    static const char hello[] = "&hello=" GNUDB_CONTACT "+Amplitude+" AMPLITUDE_VERSION "&proto=6";
    const CdTrack *last = &toc->track[toc->count - 1];
    char url[2048], category[32], id[16];
    char *answer;
    size_t size;
    int i, len, ok;

    if (!GNUDB_CONTACT[0])
        return 0;
    len = snprintf(url, sizeof url, GNUDB "?cmd=cddb+query+%08lx+%d", cd_names_cddb_id(toc), toc->count);
    for (i = 0; i < toc->count; i++)
        len += snprintf(url + len, sizeof url - (size_t)len, "+%ld", toc->track[i].start + LEAD_IN);
    snprintf(url + len, sizeof url - (size_t)len, "+%ld%s", (last->start + last->sectors + LEAD_IN) / CD_SECTORS_PER_S,
             hello);
    answer = stream_fetch(url, ANSWER_MAX, &size);
    ok = answer && cd_names_parse_cddb_query(answer, category, sizeof category, id, sizeof id);
    free(answer);
    if (!ok)
        return 0;
    snprintf(url, sizeof url, GNUDB "?cmd=cddb+read+%s+%s%s", category, id, hello);
    answer = stream_fetch(url, ANSWER_MAX, &size);
    ok = answer && cd_names_parse_cddb(answer, toc, out);
    free(answer);
    return ok;
}

static volatile int busy;           /* a lookup is under way */
static volatile int ready;          /* `found` holds a result nobody has taken */
static char asked_device[CD_DEVICE_MAX];
static unsigned long asked_id;      /* the disc last looked up, so as not to ask twice */
static time_t asked_at;
static int asked_online;            /* may the internet be asked? */
static CdNames found;               /* kept, to answer at once when asked for the same disc again */
static int found_any;

static void lookup(void *arg)
{
    Cd *cd = cd_open(asked_device);
    unsigned long id = cd ? cd_names_cddb_id(cd_toc(cd)) : 0;

    (void)arg;
    /* The same disc as last time needs no asking: its names are known, or
     * were not to be had a moment ago. */
    if (cd && (id != asked_id || (!found_any && time(NULL) - asked_at > RETRY_SECONDS))) {
        asked_id = id;
        asked_at = time(NULL);
        found_any = cd_read_names(cd, &found) ||
                    (asked_online && (ask_musicbrainz(cd_toc(cd), &found) || ask_gnudb(cd_toc(cd), &found)));
    }
    if (cd && found_any) {
        snprintf(found.device, sizeof found.device, "%s", asked_device);
        ready = 1;
    }
    if (cd)
        cd_close(cd);
    busy = 0;
}

void cd_names_request(const char *device, int online)
{
    if (busy || ready)
        return;
    busy = 1;
    asked_online = online;
    snprintf(asked_device, sizeof asked_device, "%s", device);
    if (!plat_thread_start(lookup, NULL))
        busy = 0;
}

const CdNames *cd_names_take(void)
{
    if (!ready || busy)
        return NULL;
    ready = 0;
    return &found;
}
