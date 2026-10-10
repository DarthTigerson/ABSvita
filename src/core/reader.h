/* Random-access byte reader. Implemented over memory, stdio files (SD card)
 * and the HTTP ring buffer (src/plat/httpsrc.c). */
#ifndef ABSVITA_READER_H
#define ABSVITA_READER_H
#include <stdint.h>
#include <stdio.h>

typedef struct Reader {
    void *ctx;
    /* returns bytes read (may be short only at EOF), <0 on error */
    int (*read_at)(void *ctx, uint64_t off, void *buf, uint32_t n);
    uint64_t size;
} Reader;

/* 0 when exactly n bytes were read; -1 on short read/error, or the reader's
 * own negative code (e.g. -3 = interrupted) */
int reader_read_full(const Reader *r, uint64_t off, void *buf, uint32_t n);

typedef struct { const uint8_t *data; uint64_t size; } MemReaderCtx;
void reader_mem(Reader *r, MemReaderCtx *ctx, const void *data, uint64_t size);

/* stdio-backed reader; base is added to every offset (used for moov cache
 * files that hold bytes [base, base+len) of a remote file). */
typedef struct { FILE *f; uint64_t base; uint64_t pos; } FileReaderCtx;
int reader_file_open(Reader *r, FileReaderCtx *ctx, const char *path, uint64_t base, uint64_t logical_size);
void reader_file_close(FileReaderCtx *ctx);

static inline uint32_t rd_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline uint16_t rd_be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint64_t rd_be64(const uint8_t *p) { return (uint64_t)rd_be32(p) << 32 | rd_be32(p + 4); }

#endif
