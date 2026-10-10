/* One playable track: byte source (HTTP or file) + demux + decoder -> PCM. */
#ifndef ABSVITA_STREAM_H
#define ABSVITA_STREAM_H
#include <stdbool.h>
#include <stdint.h>

#include "http.h"

typedef struct {
    bool local;
    char path[256];  /* local file */
    char url[512];   /* remote file (absolute) */
    uint64_t size;   /* bytes, 0 if unknown */
    char ext[8];
    char codec[16];
} TrackSource;

enum { STREAM_EOF = 1, STREAM_ERR = -1, STREAM_IO = -2, STREAM_INTERRUPTED = -3, STREAM_UNSUPPORTED = -4 };

#define STREAM_MAX_FRAMES 4096

typedef struct Stream Stream;

/* Blocking (network). On failure returns NULL and *err gets a STREAM_* code. */
Stream *stream_open(const TrackSource *src, AuthFn auth, int *err);
/* Seeks to local time t; *actual gets the real position. 0 ok. */
int stream_seek(Stream *s, double t, double *actual);
/* Decodes PCM (interleaved s16). Returns frames (>0), STREAM_EOF, or a STREAM_* error.
 * STREAM_IO and STREAM_INTERRUPTED are retryable (nothing was consumed). */
int stream_read(Stream *s, int16_t *out, int max_frames, int *channels, int *rate);
void stream_interrupt(Stream *s, int on);
int stream_net_state(Stream *s); /* HSRC_* or 0 for local */
void stream_close(Stream *s);
/* True when the codec/container is something this player can decode. */
bool stream_supported(const char *ext, const char *codec);
/* Range requests and bytes used for the MP4 tables so far (0 for others). */
void stream_table_stats(Stream *s, int *requests, uint64_t *bytes);

#endif
