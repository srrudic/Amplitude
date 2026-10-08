/* The names of the songs on an audio CD, looked up on the internet.
 *
 * A disc says nothing about itself but where its tracks begin, and that is
 * what identifies it: from those positions a "disc ID" is worked out and
 * sent to a public database, which answers with the album. That database
 * is MusicBrainz, which needs a secure connection. A second one can be
 * asked after it: gnudb, the successor of freedb/CDDB, which works over a
 * plain connection and so also where the system's encryption is too old.
 * It is switched off, because it wants an e-mail address of the program's
 * author sent with every request (see GNUDB_CONTACT in cdnames.c).
 *
 * Looking up happens on a thread of its own; the result is collected with
 * cd_names_take(). */
#ifndef CDNAMES_H
#define CDNAMES_H

#include "cd.h"

typedef struct {
    char device[CD_DEVICE_MAX];     /* the disc these names are for */
    char artist[128], album[160];
    int count;
    struct {
        int number;                 /* as on the disc */
        char artist[128];           /* empty when it is the album's */
        char title[200];
    } track[CD_MAX_TRACKS];
} CdNames;

/* Starts looking up the disc in `device` (a drive or a CUE sheet), unless
 * that very disc was looked up last or a lookup is under way. */
void cd_names_request(const char *device);
/* Returns 1, once, when names have been found, and fills `out`. */
int  cd_names_take(CdNames *out);

/* The pieces, exposed for the tests. */
void cd_names_musicbrainz_id(const CdToc *toc, char id[29]);
unsigned long cd_names_cddb_id(const CdToc *toc);
int  cd_names_parse_musicbrainz(const char *json, const char *id, const CdToc *toc, CdNames *out);
/* The first match in the answer to a CDDB query: its category and ID. */
int  cd_names_parse_cddb_query(const char *text, char *category, size_t category_size, char *id, size_t id_size);
int  cd_names_parse_cddb(const char *text, const CdToc *toc, CdNames *out);

#endif
