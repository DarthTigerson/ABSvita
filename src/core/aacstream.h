/* AAC-in-MP4 track decoder: MP4 demux + Helix AAC (LC and HE-AAC/SBR).
 * Pure C, shared by the host tests and the Vita audio engine. */
#ifndef ABSVITA_AACSTREAM_H
#define ABSVITA_AACSTREAM_H
#include <stdint.h>

#include "mp4.h"
#include "reader.h"

/* Max PCM samples (all channels) produced by one frame: 2048 (SBR) x 2 ch */
#define AACS_MAX_PCM 4096

typedef struct {
    Mp4 mp4;
    void *dec;
    const Reader *media;
    uint8_t *frame;
    uint32_t frame_cap;
    int out_rate, out_ch; /* known after the first decoded frame */
    int errors;           /* consecutive decode errors */
    int table_errors;     /* consecutive sample-table read failures */
} AacStream;

/* media serves the audio bytes; tables serves the moov (may be the same reader). */
int aacs_open(AacStream *s, const Reader *media, const Reader *tables);
/* moov_size == 0 means "search the tables reader for the moov". */
int aacs_open_at(AacStream *s, const Reader *media, const Reader *tables, uint64_t moov_off, uint64_t moov_size);
int aacs_seek(AacStream *s, double t, double *actual);
/* Decodes one AAC frame into pcm (interleaved). *frames = samples per channel.
 * 0 ok, 1 EOF, -2 I/O error / -3 interrupted (retryable: the frame is not
 * consumed; sample-table read failures count as I/O until 50 in a row), other <0 fatal. Corrupt frames are skipped (output silence). */
int aacs_decode(AacStream *s, int16_t *pcm, int *frames, int *channels, int *rate);
void aacs_close(AacStream *s);

#endif
