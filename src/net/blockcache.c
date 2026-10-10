#include "blockcache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dbg.h"

typedef struct {
    uint64_t index; /* block number, UINT64_MAX when empty */
    uint32_t len;
    uint32_t used;  /* LRU stamp */
    uint8_t *data;
} Block;

struct BlockCache {
    char url[512];
    uint64_t size;
    AuthFn auth;
    HttpConn *conn;
    Block *blocks;
    int n;
    uint32_t clock;
    volatile int interrupted;
    int requests;
    uint64_t bytes;
};

typedef struct { uint8_t *dst; uint32_t len, cap; } Fill;

static int fill_sink(void *ud, const uint8_t *data, uint32_t n) {
    Fill *f = ud;
    if (f->len + n > f->cap) return -1;
    memcpy(f->dst + f->len, data, n);
    f->len += n;
    return 0;
}

static int interrupted(void *ud) { return ((BlockCache *)ud)->interrupted; }

typedef struct { Fill f; BlockCache *c; } FillCtx;
static int ctx_sink(void *ud, const uint8_t *d, uint32_t n) { return fill_sink(&((FillCtx *)ud)->f, d, n); }
static int ctx_abort(void *ud) { return interrupted(((FillCtx *)ud)->c); }

static Block *fetch(BlockCache *c, uint64_t index) {
    Block *victim = &c->blocks[0];
    for (int i = 0; i < c->n; i++) {
        if (c->blocks[i].index == index) {
            c->blocks[i].used = ++c->clock;
            return &c->blocks[i];
        }
        if (c->blocks[i].used < victim->used) victim = &c->blocks[i];
    }
    uint64_t from = index * BC_BLOCK;
    uint64_t to = from + BC_BLOCK - 1;
    if (c->size && to >= c->size) to = c->size - 1;
    if (c->size && from >= c->size) return NULL;
    victim->index = UINT64_MAX;
    char auth[1100] = "";
    int refresh = 0;
    for (int attempt = 0; attempt < 4; attempt++) {
        if (c->interrupted) return NULL;
        if (!refresh) auth[0] = 0;
        if (c->auth) c->auth(auth, sizeof auth, refresh);
        refresh = 0;
        const char *hdrs[] = {auth, NULL};
        FillCtx fc = {{victim->data, 0, BC_BLOCK}, c};
        HttpRangeResult res;
        int rc = http_get_range(c->conn, c->url, hdrs, from, to, ctx_sink, ctx_abort, &fc, &res);
        c->requests++;
        c->bytes += fc.f.len;
        if (rc == 0 && res.status == 206 && fc.f.len == to - from + 1) {
            if (!c->size && res.total) c->size = res.total;
            victim->index = index;
            victim->len = fc.f.len;
            victim->used = ++c->clock;
            return victim;
        }
        if (res.aborted && c->interrupted) return NULL;
        if (res.status == 401) { refresh = 1; continue; }
        dbg("tables: range %llu-%llu -> %d %s", (unsigned long long)from, (unsigned long long)to, res.status, res.err);
        if (res.status == 404 || res.status == 403 || res.status == 416) return NULL;
    }
    return NULL;
}

static int bc_read(void *ctx, uint64_t off, void *buf, uint32_t n) {
    BlockCache *c = ctx;
    if (c->size && off >= c->size) return 0;
    if (c->size && off + n > c->size) n = (uint32_t)(c->size - off);
    uint32_t done = 0;
    while (done < n) {
        uint64_t at = off + done;
        Block *b = fetch(c, at / BC_BLOCK);
        if (!b) return c->interrupted ? -3 : (done ? (int)done : -1);
        uint32_t in = (uint32_t)(at % BC_BLOCK);
        if (in >= b->len) break;
        uint32_t k = b->len - in;
        if (k > n - done) k = n - done;
        memcpy((uint8_t *)buf + done, b->data + in, k);
        done += k;
    }
    return (int)done;
}

BlockCache *bc_open(const char *url, uint64_t size, AuthFn auth, int nblocks) {
    BlockCache *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    snprintf(c->url, sizeof c->url, "%s", url);
    c->size = size;
    c->auth = auth;
    c->n = nblocks > 0 ? nblocks : 16;
    c->conn = http_conn_new();
    c->blocks = calloc(c->n, sizeof(Block));
    if (!c->conn || !c->blocks) { bc_close(c); return NULL; }
    for (int i = 0; i < c->n; i++) {
        c->blocks[i].index = UINT64_MAX;
        if (!(c->blocks[i].data = malloc(BC_BLOCK))) { bc_close(c); return NULL; }
    }
    return c;
}

Reader bc_reader(BlockCache *c) {
    Reader r = {c, bc_read, c->size};
    return r;
}

void bc_interrupt(BlockCache *c, int on) { c->interrupted = on; }

void bc_stats(const BlockCache *c, int *requests, uint64_t *bytes) {
    if (requests) *requests = c->requests;
    if (bytes) *bytes = c->bytes;
}

void bc_close(BlockCache *c) {
    if (!c) return;
    if (c->blocks)
        for (int i = 0; i < c->n; i++) free(c->blocks[i].data);
    free(c->blocks);
    http_conn_free(c->conn);
    free(c);
}
