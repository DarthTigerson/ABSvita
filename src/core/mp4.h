/* Minimal ISO-BMFF (MP4/M4A/M4B) audio demuxer. Sample tables are read on
 * demand through small cached cursors, so 25 h books with millions of
 * samples need only a few KB of RAM. */
#ifndef ABSVITA_MP4_H
#define ABSVITA_MP4_H
#include <stdbool.h>
#include <stdint.h>

#include "reader.h"

enum { MP4_UNKNOWN = 0, MP4_AAC = 1, MP4_MP3 = 2 };

typedef struct {
    uint64_t off;   /* file offset of first entry */
    uint32_t count; /* number of entries */
    uint32_t esize; /* entry size in bytes */
    uint8_t cache[4096];
    uint32_t cfirst, cn; /* cached entry range */
} Mp4Table;

typedef struct {
    int codec;
    uint8_t asc[64];
    int asc_len;
    uint32_t sample_rate, channels, timescale;
    uint32_t nsamples;
    uint64_t duration_ts;
    uint64_t moov_off, moov_size;

    /* private */
    const Reader *r;
    uint32_t const_size; /* stsz sample_size when constant */
    Mp4Table stsz, stco, stsc, stts;
    uint32_t nchunks;
    /* cursor */
    uint32_t s;         /* next sample index (0-based) */
    uint32_t chunk;     /* current chunk (1-based) */
    uint32_t sic;       /* sample index within chunk */
    uint32_t spc;       /* samples per chunk for the current stsc run */
    uint32_t stsc_i;    /* current stsc entry */
    uint32_t next_run;  /* first chunk of next stsc entry (UINT32_MAX if none) */
    uint64_t off;       /* byte offset of sample s */
} Mp4;

/* Walks top-level atoms (handles 64-bit sizes and size==0). 0 on success. */
int mp4_find_moov(const Reader *r, uint64_t *off, uint64_t *size);
/* Parses the first audio ('soun') track. The reader only needs to serve the
 * moov range (at its original offsets). 0 on success. */
int mp4_open(Mp4 *m, const Reader *tables);
/* Same, when the moov location is already known (cached moov files). */
int mp4_open_at(Mp4 *m, const Reader *tables, uint64_t moov_off, uint64_t moov_size);
/* Positions the cursor on the sample containing t seconds; *actual gets the
 * start time of that sample. 0 on success. */
int mp4_seek(Mp4 *m, double seconds, double *actual);
typedef struct { uint32_t s, chunk, sic, spc, stsc_i, next_run; uint64_t off; } Mp4Cursor;
Mp4Cursor mp4_tell(const Mp4 *m);
void mp4_restore(Mp4 *m, const Mp4Cursor *c);
/* Returns the next sample's file offset/size and advances. 0 ok, 1 EOF, <0 error. */
int mp4_next(Mp4 *m, uint64_t *off, uint32_t *size);
double mp4_duration(const Mp4 *m);

/* AudioSpecificConfig: audio object type, sample rate, channel config. 0 ok. */
int aac_parse_asc(const uint8_t *asc, int len, int *aot, int *rate, int *chans);

#endif
