/* Playback engine: one output device, one decoder at a time. */
#ifndef AUDIO_H
#define AUDIO_H

#include <stddef.h>

enum { AUDIO_STOPPED, AUDIO_PLAYING, AUDIO_PAUSED };

#define AUDIO_VIS_SAMPLES 512
#define AUDIO_EQ_BANDS    10
#define AUDIO_EQ_MAX_DB   12.0f

int    audio_init(void);
void   audio_shutdown(void);

/* Loads a file, replacing whatever was loaded. Does not start playback. */
int    audio_open(const char *path);
void   audio_play(void);
void   audio_pause(void);       /* toggles between paused and playing */
void   audio_stop(void);
int    audio_state(void);
/* Returns 1 once after the current track has played to its end. */
int    audio_take_finished(void);

/* Web addresses (http and https) open like files, but connecting and
 * buffering happen in the background: audio_open() succeeds at once and
 * audio_update(), called every frame, completes the job when enough has
 * arrived. If that fails, audio_take_failed() returns 1 once. Streams have
 * no length and cannot be wound or queued for a gapless change. */
void   audio_update(void);
int    audio_take_failed(void);
int    audio_is_stream(void);
int    audio_buffering(void);       /* connecting, or run dry and refilling */
/* What a station says it is playing, when that has changed (UTF-8). */
int    audio_stream_title(char *out, size_t size);
const char *audio_stream_name(void);    /* the station's name, or "" */
int    audio_stream_bitrate(void);      /* kbit/s as stated by the station, or 0 */

/* Gapless playback: prepares `path` to start the instant the current track
 * ends (NULL cancels). Returns 1 if it was queued, 0 if not, and -1 if the
 * previously queued track has started and audio_take_advanced() must be
 * called first. */
int    audio_queue_next(const char *path);
/* Returns 1 once after playback has moved on to the queued track. */
int    audio_take_advanced(void);

void   audio_set_volume(float volume);  /* 0.0 .. 1.0 */
void   audio_set_balance(float balance);/* -1.0 (left) .. 1.0 (right) */
/* Ten-band equaliser; preamp and band gains in dB (+-AUDIO_EQ_MAX_DB). */
void   audio_set_eq(int enabled, float preamp_db, const float bands_db[AUDIO_EQ_BANDS]);
void   audio_seek(double seconds);
double audio_position(void);            /* seconds */
double audio_length(void);              /* seconds, 0 if unknown */
int    audio_sample_rate(void);         /* of the source file, Hz */
int    audio_channels(void);            /* of the source file */

/* Copies the most recently played mono samples (-1..1) for visualisation. */
void   audio_get_vis(float out[AUDIO_VIS_SAMPLES]);

#endif
