/* Audio fetched over HTTP: internet radio (Shoutcast and Icecast stations)
 * and audio files on a web server.
 *
 * Opening a stream starts a thread that connects, follows redirects and
 * station playlist files, and then keeps a buffer filled. Nothing here
 * blocks the caller except stream_read_wait(), and that only as long as it
 * is told to. */
#ifndef STREAM_H
#define STREAM_H

#include <stddef.h>

typedef struct Stream Stream;

enum { STREAM_CONNECTING, STREAM_OPEN, STREAM_ENDED, STREAM_FAILED };

/* Starts fetching. Returns NULL only if memory or a thread is lacking; a
 * bad address or a refused connection shows later as STREAM_FAILED. */
Stream *stream_open(const char *url);
/* Stops fetching and frees everything, at once or when the thread lets go. */
void    stream_close(Stream *stream);

/* STREAM_ENDED: the server closed the connection; what is buffered can
 * still be read. */
int     stream_state(const Stream *stream);
size_t  stream_buffered(const Stream *stream);
/* Bytes of audio read out of the buffer so far. */
size_t  stream_consumed(const Stream *stream);
/* Copies up to `size` bytes of audio data out of the buffer; never waits. */
size_t  stream_read(Stream *stream, void *out, size_t size);
/* The same, but waits up to timeout_ms for all of it while the stream is
 * still coming. */
size_t  stream_read_wait(Stream *stream, void *out, size_t size, int timeout_ms);

/* What the server said about itself, valid once the state is STREAM_OPEN:
 * the content type in lower case ("audio/mpeg"), the station's name and
 * bitrate in kbit/s (empty and 0 if not given). */
const char *stream_content_type(const Stream *stream);
const char *stream_name(const Stream *stream);
int     stream_bitrate(const Stream *stream);
const char *stream_url(const Stream *stream);
/* The title of what a station is playing, when it has changed since the
 * last call: fills `out` (UTF-8) and returns 1. */
int     stream_take_title(Stream *stream, char *out, size_t size);

/* Fetches the whole of what is at an address (following redirects), for
 * things other than audio. Waits for it; meant for a thread of one's own.
 * Returns memory to free(), with a zero byte after the end, or NULL if it
 * could not be had or is longer than `max`. */
char   *stream_fetch(const char *url, size_t max, size_t *size);

/* Parsing helpers, exposed for the tests. Splits an http or https address;
 * returns 0 if it is not one. */
int     stream_parse_url(const char *url, int *secure, char *host, size_t host_size, int *port,
                         char *path, size_t path_size);

#endif
