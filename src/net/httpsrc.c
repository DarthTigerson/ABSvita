#include "httpsrc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dbg.h"
#include "osal.h"

#define RING_SIZE (4u << 20)
#define KEEP_BEHIND (256u * 1024)  /* bytes kept before the read point (small seeks back) */
#define AHEAD_REACH (512u * 1024)  /* reads this far past the fill point wait instead of restarting */
#define NO_WANT UINT64_MAX

struct HttpSrc {
    char url[512];
    AuthFn auth;
    volatile uint64_t size;
    uint8_t *ring;
    uint64_t win_start, win_end; /* valid file range held in the ring */
    uint64_t rd;                 /* end of the last read */
    volatile uint64_t want;      /* restart request (or NO_WANT) */
    volatile int quit, interrupted, state;
    Mutex lock;
    Cond cv;
    Thread th;
};

/* Called by curl with body bytes; copies them in at win_end, waiting for space. */
static int ring_sink(void *ud, const uint8_t *data, uint32_t n) {
    HttpSrc *s = ud;
    mutex_lock(&s->lock);
    while (n) {
        if (s->quit || s->want != NO_WANT) { mutex_unlock(&s->lock); return -1; }
        uint64_t keep_from = s->rd > KEEP_BEHIND ? s->rd - KEEP_BEHIND : 0;
        if (keep_from > s->win_start) s->win_start = keep_from < s->win_end ? keep_from : s->win_end;
        uint32_t space = RING_SIZE - (uint32_t)(s->win_end - s->win_start);
        if (!space) {
            cond_wait_ms(&s->cv, &s->lock, 100);
            continue;
        }
        uint32_t k = n < space ? n : space;
        uint32_t at = (uint32_t)(s->win_end % RING_SIZE);
        uint32_t first = RING_SIZE - at < k ? RING_SIZE - at : k;
        memcpy(s->ring + at, data, first);
        if (k > first) memcpy(s->ring, data + first, k - first);
        s->win_end += k;
        data += k;
        n -= k;
        s->state = HSRC_OK;
        cond_broadcast(&s->cv);
    }
    mutex_unlock(&s->lock);
    return 0;
}

static int should_abort(void *ud) {
    HttpSrc *s = ud;
    return s->quit || s->want != NO_WANT;
}

/* Sleeps up to ms, waking early for quit or a restart request. */
static void backoff_wait(HttpSrc *s, int ms) {
    for (int t = 0; t < ms && !s->quit && s->want == NO_WANT; t += 50) sleep_ms(50);
}

static void *fetch_thread(void *arg) {
    HttpSrc *s = arg;
    HttpConn *conn = http_conn_new();
    int backoff = 0, refresh = 0, failures = 0;
    char auth[1100] = "";
    while (!s->quit) {
        mutex_lock(&s->lock);
        if (s->want != NO_WANT) {
            s->win_start = s->win_end = s->rd = s->want;
            s->want = NO_WANT;
            backoff = 0;
        }
        uint64_t keep_from = s->rd > KEEP_BEHIND ? s->rd - KEEP_BEHIND : 0;
        if (keep_from > s->win_start) s->win_start = keep_from < s->win_end ? keep_from : s->win_end;
        uint32_t space = RING_SIZE - (uint32_t)(s->win_end - s->win_start);
        int at_end = s->size && s->win_end >= s->size;
        if (at_end || space < 64 * 1024) {
            if (at_end && s->state != HSRC_FAILED) s->state = HSRC_OK;
            cond_wait_ms(&s->cv, &s->lock, 100);
            mutex_unlock(&s->lock);
            continue;
        }
        uint64_t pos = s->win_end;
        mutex_unlock(&s->lock);

        if (!refresh) auth[0] = 0;
        if (s->auth) s->auth(auth, sizeof auth, refresh);
        refresh = 0;
        const char *hdrs[] = {auth, NULL};
        HttpRangeResult res;
        int rc = http_get_range(conn, s->url, hdrs, pos, 0, ring_sink, should_abort, s, &res);
        if (s->quit) break;
        if (res.aborted && s->want != NO_WANT) continue; /* seek: restart at the new offset */
        if (!s->size && res.total && res.status == 206) s->size = res.total;
        if (res.status == 401 && failures < 3) {
            refresh = 1;
            failures++;
            continue;
        }
        if (res.status == 416) { /* past the end: the size we were told was wrong */
            s->size = pos;
            continue;
        }
        if (rc == 0 && (res.status == 206 || (res.status == 200 && pos == 0))) {
            if (!s->size && res.status == 200) s->size = pos + res.received;
            failures = 0;
            backoff = 0;
            continue; /* completed (or closed early): the loop resumes from win_end */
        }
        /* transport error, stall, wrong status, or the server ignored the Range */
        failures++;
        if (res.status == 200 && pos != 0) dbg("stream: server ignored Range at %llu", (unsigned long long)pos);
        else dbg("stream: GET at %llu -> %d %s", (unsigned long long)pos, res.status, res.err);
        int hard = res.status == 404 || res.status == 403 || res.status == 401 || (res.status == 200 && pos != 0);
        s->state = hard && failures > 3 ? HSRC_FAILED : HSRC_RECONNECTING;
        if (res.received) {
            backoff = 0; /* made progress before dropping: retry at once */
        } else {
            backoff_wait(s, backoff < 4 ? 500 << backoff : 8000);
            backoff++;
        }
    }
    http_conn_free(conn);
    return NULL;
}

static int src_read(void *ctx, uint64_t off, void *buf, uint32_t n) {
    HttpSrc *s = ctx;
    if (s->size && off >= s->size) return 0;
    mutex_lock(&s->lock);
    int rc;
    for (;;) {
        if (s->interrupted) { rc = HSRC_INTERRUPTED; break; }
        if (s->state == HSRC_FAILED) { rc = -1; break; }
        if (off >= s->win_start && off < s->win_end) {
            uint32_t avail = (uint32_t)(s->win_end - off);
            if (n > avail) n = avail;
            uint32_t at = (uint32_t)(off % RING_SIZE);
            uint32_t first = RING_SIZE - at < n ? RING_SIZE - at : n;
            memcpy(buf, s->ring + at, first);
            if (n > first) memcpy((uint8_t *)buf + first, s->ring, n - first);
            s->rd = off + n;
            cond_broadcast(&s->cv);
            rc = (int)n;
            break;
        }
        if (s->size && off >= s->size) { rc = 0; break; }
        int near = off >= s->win_start && off <= s->win_end + AHEAD_REACH;
        if (!near && s->want != off) {
            s->want = off;
            cond_broadcast(&s->cv);
        }
        if (s->state == HSRC_OK) s->state = HSRC_BUFFERING;
        cond_wait_ms(&s->cv, &s->lock, 100);
    }
    mutex_unlock(&s->lock);
    return rc;
}

HttpSrc *httpsrc_open(const char *url, uint64_t size, AuthFn auth) {
    HttpSrc *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->ring = malloc(RING_SIZE);
    if (!s->ring) { free(s); return NULL; }
    snprintf(s->url, sizeof s->url, "%s", url);
    s->auth = auth;
    s->size = size;
    s->want = NO_WANT;
    s->state = HSRC_BUFFERING;
    mutex_init(&s->lock);
    cond_init(&s->cv);
    if (!thread_start(&s->th, fetch_thread, s, 64, -1)) {
        free(s->ring);
        free(s);
        return NULL;
    }
    return s;
}

Reader httpsrc_reader(HttpSrc *s) {
    Reader r = {s, src_read, s->size};
    return r;
}

void httpsrc_interrupt(HttpSrc *s, int on) {
    mutex_lock(&s->lock);
    s->interrupted = on;
    cond_broadcast(&s->cv);
    mutex_unlock(&s->lock);
}

int httpsrc_state(HttpSrc *s) { return s->state; }

float httpsrc_fill(HttpSrc *s) {
    mutex_lock(&s->lock);
    uint64_t ahead = s->win_end > s->rd ? s->win_end - s->rd : 0;
    mutex_unlock(&s->lock);
    return (float)ahead / (RING_SIZE - KEEP_BEHIND);
}

void httpsrc_close(HttpSrc *s) {
    if (!s) return;
    s->quit = 1;
    mutex_lock(&s->lock);
    cond_broadcast(&s->cv);
    mutex_unlock(&s->lock);
    thread_join(&s->th);
    mutex_destroy(&s->lock);
    cond_destroy(&s->cv);
    free(s->ring);
    free(s);
}
