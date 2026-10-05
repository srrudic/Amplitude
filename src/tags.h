/* Artist/title metadata: ID3v1/v2, Vorbis comments (FLAC, Ogg Vorbis, Opus),
 * MP4 and tracker module names. */
#ifndef TAGS_H
#define TAGS_H

#include <stddef.h>

enum { TEXT_LATIN1, TEXT_UTF8, TEXT_UTF16, TEXT_UTF16BE };

/* Converts text to clean, NUL-terminated UTF-8. Bytes that are not valid
 * UTF-8 are taken as Latin-1; UTF-16 without a byte order mark is taken as
 * little endian. Control characters become spaces, and leading and trailing
 * blanks are dropped. */
void text_to_utf8(char *out, size_t out_size, const unsigned char *text, size_t len, int encoding);

/* Fills artist and/or title (each may come back empty). Returns 1 if a
 * title was found. */
int tags_read(const char *path, char *artist, size_t artist_size, char *title, size_t title_size);
/* The genre as written in the file: a name, or an old-style number such as
 * "(17)". Empty if there is none. */
void tags_read_genre(const char *path, char *genre, size_t genre_size);

#endif
