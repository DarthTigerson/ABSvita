/* MP3 helpers: ID3v2 skipping, frame headers, Xing/Info seek tables. */
#ifndef ABSVITA_MP3_H
#define ABSVITA_MP3_H
#include <stdbool.h>
#include <stdint.h>

#include "reader.h"

typedef struct {
    int version;     /* 1, 2 or 25 (MPEG 2.5) */
    int layer;       /* 3 */
    int bitrate;     /* kbps */
    int sample_rate;
    int channels;
    int frame_bytes;
    int samples;     /* per frame */
} Mp3Frame;

typedef struct {
    uint64_t audio_start; /* first frame after ID3v2 (and after the Xing frame) */
    uint64_t audio_bytes;
    int sample_rate, channels, bitrate;
    double duration;      /* 0 if unknown */
    bool has_toc;
    uint8_t toc[100];
} Mp3Info;

/* Size of an ID3v2 tag at the start of buf (0 if none). */
uint32_t mp3_id3_size(const uint8_t *buf, uint32_t n);
/* Parses a 4-byte frame header. 0 ok. */
int mp3_parse_header(const uint8_t *h, Mp3Frame *f);
/* Reads the start of the file and fills info (uses Xing/Info when present,
 * else CBR estimate from the first frame). 0 ok. */
int mp3_probe(const Reader *r, Mp3Info *info);
/* Byte offset for time t (seconds). */
uint64_t mp3_offset_for_time(const Mp3Info *info, double t);

#endif
