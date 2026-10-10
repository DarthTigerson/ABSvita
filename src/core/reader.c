#include "reader.h"

#include <string.h>

int reader_read_full(const Reader *r, uint64_t off, void *buf, uint32_t n) {
    uint32_t got = 0;
    while (got < n) {
        int k = r->read_at(r->ctx, off + got, (uint8_t *)buf + got, n - got);
        if (k < 0) return k < -1 ? k : -1; /* pass specific error codes through */
        if (k == 0) return -1;
        got += (uint32_t)k;
    }
    return 0;
}

static int mem_read(void *ctx, uint64_t off, void *buf, uint32_t n) {
    MemReaderCtx *m = ctx;
    if (off >= m->size) return 0;
    if (off + n > m->size) n = (uint32_t)(m->size - off);
    memcpy(buf, m->data + off, n);
    return (int)n;
}

void reader_mem(Reader *r, MemReaderCtx *ctx, const void *data, uint64_t size) {
    ctx->data = data;
    ctx->size = size;
    r->ctx = ctx;
    r->read_at = mem_read;
    r->size = size;
}

static int file_read(void *ctx, uint64_t off, void *buf, uint32_t n) {
    FileReaderCtx *f = ctx;
    if (off < f->base) return -1;
    uint64_t p = off - f->base;
    if (p != f->pos) {
        if (fseeko(f->f, (off_t)p, SEEK_SET) != 0) return -1;
        f->pos = p;
    }
    size_t k = fread(buf, 1, n, f->f);
    f->pos += k;
    if (k == 0 && ferror(f->f)) return -1;
    return (int)k;
}

int reader_file_open(Reader *r, FileReaderCtx *ctx, const char *path, uint64_t base, uint64_t logical_size) {
    memset(ctx, 0, sizeof *ctx);
    ctx->f = fopen(path, "rb");
    if (!ctx->f) return -1;
    setvbuf(ctx->f, NULL, _IOFBF, 32 * 1024);
    ctx->base = base;
    if (!logical_size) {
        fseeko(ctx->f, 0, SEEK_END);
        logical_size = base + (uint64_t)ftello(ctx->f);
        fseeko(ctx->f, 0, SEEK_SET);
    }
    r->ctx = ctx;
    r->read_at = file_read;
    r->size = logical_size;
    return 0;
}

void reader_file_close(FileReaderCtx *ctx) {
    if (ctx->f) fclose(ctx->f);
    ctx->f = NULL;
}
