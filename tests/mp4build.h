/* Test helper: builds synthetic MP4 files in memory. */
#ifndef MP4BUILD_H
#define MP4BUILD_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *d; size_t n, cap; size_t stack[16]; int sp; } Buf;

static void b_put(Buf *b, const void *p, size_t n) {
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2 + 1024; b->d = realloc(b->d, b->cap); }
    memcpy(b->d + b->n, p, n); b->n += n;
}
static void b_u8(Buf *b, uint8_t v) { b_put(b, &v, 1); }
static void b_u16(Buf *b, uint16_t v) { uint8_t x[2] = {v >> 8, v}; b_put(b, x, 2); }
static void b_u32(Buf *b, uint32_t v) { uint8_t x[4] = {v >> 24, v >> 16, v >> 8, v}; b_put(b, x, 4); }
static void b_u64(Buf *b, uint64_t v) { b_u32(b, v >> 32); b_u32(b, (uint32_t)v); }
static void b_zero(Buf *b, size_t n) { while (n--) b_u8(b, 0); }
static void b_open(Buf *b, const char *type) { b->stack[b->sp++] = b->n; b_u32(b, 0); b_put(b, type, 4); }
static void b_full(Buf *b, const char *type, uint32_t verflags) { b_open(b, type); b_u32(b, verflags); }
static void b_close(Buf *b) {
    size_t at = b->stack[--b->sp]; uint32_t sz = (uint32_t)(b->n - at);
    b->d[at] = sz >> 24; b->d[at + 1] = sz >> 16; b->d[at + 2] = sz >> 8; b->d[at + 3] = sz;
}
static __attribute__((unused)) void b_set32(Buf *b, size_t at, uint32_t v) { b->d[at] = v >> 24; b->d[at+1] = v >> 16; b->d[at+2] = v >> 8; b->d[at+3] = v; }

/* sample size pattern used by the synthetic files */
static uint32_t synth_size(uint32_t i) { return 10 + (i * 7) % 13; }

/* Layout: ftyp, mdat (64-bit largesize), moov{ trak(text), trak(soun) }.
 * Chunking: stsc runs {1: 3 spc}, {3: 2 spc}, {6: 1 spc}. */
static Buf build_synth_mp4(uint32_t nsamples, int use_co64, uint32_t delta) {
    Buf b = {0};
    b_open(&b, "ftyp"); b_put(&b, "M4B ", 4); b_u32(&b, 0); b_put(&b, "isom", 4); b_close(&b);
    /* mdat with largesize */
    size_t mdat_at = b.n;
    b_u32(&b, 1); b_put(&b, "mdat", 4); b_u64(&b, 0);
    size_t data_start = b.n;
    for (uint32_t i = 0; i < nsamples; i++) { uint32_t s = synth_size(i); for (uint32_t k = 0; k < s; k++) b_u8(&b, (uint8_t)i); }
    uint64_t mdat_size = b.n - mdat_at;
    for (int k = 0; k < 8; k++) b.d[mdat_at + 8 + k] = (uint8_t)(mdat_size >> (56 - 8 * k));

    /* chunk layout */
    uint32_t nchunks = 0, s = 0;
    uint64_t *chunk_off = malloc(sizeof(uint64_t) * (nsamples + 1));
    uint64_t off = data_start;
    for (uint32_t c = 1; s < nsamples; c++) {
        uint32_t spc = c < 3 ? 3 : (c < 6 ? 2 : 1);
        chunk_off[nchunks++] = off;
        for (uint32_t k = 0; k < spc && s < nsamples; k++, s++) off += synth_size(s);
    }

    b_open(&b, "moov");
    b_full(&b, "mvhd", 0); b_zero(&b, 96); b_close(&b);
    /* a text (chapter) track first: must be skipped */
    b_open(&b, "trak"); b_open(&b, "mdia");
    b_full(&b, "hdlr", 0); b_u32(&b, 0); b_put(&b, "text", 4); b_zero(&b, 13); b_close(&b);
    b_close(&b); b_close(&b);
    /* audio track */
    b_open(&b, "trak"); b_open(&b, "mdia");
    b_full(&b, "mdhd", 0); b_u32(&b, 0); b_u32(&b, 0); b_u32(&b, 44100); b_u32(&b, nsamples * delta); b_u32(&b, 0); b_close(&b);
    b_full(&b, "hdlr", 0); b_u32(&b, 0); b_put(&b, "soun", 4); b_zero(&b, 13); b_close(&b);
    b_open(&b, "minf"); b_open(&b, "stbl");
    b_full(&b, "stsd", 0); b_u32(&b, 1);
      b_open(&b, "mp4a"); b_zero(&b, 6); b_u16(&b, 1); b_zero(&b, 8); b_u16(&b, 2); b_u16(&b, 16); b_u16(&b, 0); b_u16(&b, 0); b_u32(&b, 44100u << 16);
        b_full(&b, "esds", 0);
          b_u8(&b, 0x03); b_u8(&b, 0x80); b_u8(&b, 0x80); b_u8(&b, 0x80); b_u8(&b, 25); b_u16(&b, 1); b_u8(&b, 0);
          b_u8(&b, 0x04); b_u8(&b, 17); b_u8(&b, 0x40); b_u8(&b, 0x15); b_zero(&b, 3); b_u32(&b, 96000); b_u32(&b, 96000);
          b_u8(&b, 0x05); b_u8(&b, 2); b_u8(&b, 0x12); b_u8(&b, 0x10);
          b_u8(&b, 0x06); b_u8(&b, 1); b_u8(&b, 2);
        b_close(&b);
      b_close(&b);
    b_close(&b);
    b_full(&b, "stts", 0); b_u32(&b, 1); b_u32(&b, nsamples); b_u32(&b, delta); b_close(&b);
    b_full(&b, "stsc", 0); b_u32(&b, 3);
      b_u32(&b, 1); b_u32(&b, 3); b_u32(&b, 1);
      b_u32(&b, 3); b_u32(&b, 2); b_u32(&b, 1);
      b_u32(&b, 6); b_u32(&b, 1); b_u32(&b, 1);
    b_close(&b);
    b_full(&b, "stsz", 0); b_u32(&b, 0); b_u32(&b, nsamples); for (uint32_t i = 0; i < nsamples; i++) b_u32(&b, synth_size(i)); b_close(&b);
    if (use_co64) { b_full(&b, "co64", 0); b_u32(&b, nchunks); for (uint32_t c = 0; c < nchunks; c++) b_u64(&b, chunk_off[c]); }
    else { b_full(&b, "stco", 0); b_u32(&b, nchunks); for (uint32_t c = 0; c < nchunks; c++) b_u32(&b, (uint32_t)chunk_off[c]); }
    b_close(&b);
    b_close(&b); b_close(&b); /* stbl, minf */
    b_close(&b); b_close(&b); /* mdia, trak */
    b_close(&b); /* moov */
    free(chunk_off);
    return b;
}

/* offset of sample i in the synthetic file */
static uint64_t synth_offset(const Buf *b, uint32_t i) {
    (void)b;
    uint64_t off = 20 + 16; /* ftyp + mdat largesize header */
    for (uint32_t k = 0; k < i; k++) off += synth_size(k);
    return off;
}
#endif
