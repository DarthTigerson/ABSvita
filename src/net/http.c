#include "http.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "dbg.h"
#include "osal.h"

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif
#define UA "ABSvita/" APP_VERSION " (PlayStation Vita)"
#define CONNECT_TIMEOUT_S 10
#define STALL_MS 15000

static volatile long long g_clock_offset;
static volatile bool g_clock_known, g_clock_fresh;

long long http_clock_offset(bool *known) {
    if (known) *known = g_clock_known;
    return g_clock_offset;
}

void http_set_clock_offset(long long ms) {
    g_clock_offset = ms;
    g_clock_known = true;
}

/* "Fri, 09 Oct 2026 17:01:02 GMT" -> unix seconds (0 on failure) */
static long long parse_http_date(const char *s) {
    static const char *mon = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char m[4] = {0};
    int d, y, hh, mm, ss;
    const char *comma = strchr(s, ',');
    if (!comma || sscanf(comma + 1, " %d %3s %d %d:%d:%d", &d, m, &y, &hh, &mm, &ss) != 6) return 0;
    const char *f = strstr(mon, m);
    if (!f) return 0;
    int mo = (int)(f - mon) / 3 + 1;
    y -= mo <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long days = era * 146097 + (long long)doe - 719468;
    return days * 86400 + hh * 3600 + mm * 60 + ss;
}

int http_init(void) {
    return curl_global_init(CURL_GLOBAL_ALL) == CURLE_OK ? 0 : -1;
}

void http_exit(void) { curl_global_cleanup(); }

/* ---- shared transfer state ------------------------------------------------ */

typedef struct {
    /* buffered mode */
    char *buf;
    uint32_t len, cap, max;
    bool overflow;
    /* sink mode */
    HttpSink sink;
    HttpAbort abort_cb;
    void *ud;
    CURL *h;
    long status;
    uint64_t total, received;
    bool aborted;
    uint64_t last_data;
} Xfer;

static size_t on_header(char *p, size_t sz, size_t n, void *ud) {
    Xfer *x = ud;
    size_t len = sz * n;
    char line[256];
    size_t l = len < sizeof line - 1 ? len : sizeof line - 1;
    memcpy(line, p, l);
    line[l] = 0;
    if (!strncmp(line, "HTTP/", 5)) {
        /* a new response (e.g. after a redirect): reset what we learned */
        x->total = 0;
        x->status = 0;
        sscanf(line, "HTTP/%*s %ld", &x->status);
    } else if (!strncasecmp(line, "Content-Range:", 14)) {
        const char *slash = strchr(line, '/');
        if (slash && slash[1] != '*') x->total = strtoull(slash + 1, NULL, 10);
    } else if (!strncasecmp(line, "Content-Length:", 15) && x->status == 200) {
        x->total = strtoull(line + 15, NULL, 10);
    } else if (!strncasecmp(line, "Date:", 5) && !g_clock_fresh) {
        long long server = parse_http_date(line + 5);
        if (server) {
            g_clock_offset = server * 1000 - (long long)wall_ms();
            g_clock_known = g_clock_fresh = true;
        }
    }
    return len;
}

static size_t on_body(char *p, size_t sz, size_t n, void *ud) {
    Xfer *x = ud;
    size_t len = sz * n;
    x->last_data = now_ms();
    if (x->sink) {
        if (x->status < 200 || x->status >= 300) return len; /* error page: discard */
        if (x->sink(x->ud, (const uint8_t *)p, (uint32_t)len) < 0) { x->aborted = true; return 0; }
        x->received += len;
        x->last_data = now_ms(); /* time spent blocked in the sink is not a stall */
        return len;
    }
    if (x->len + len > x->max) { x->overflow = true; return 0; }
    if (x->len + len + 1 > x->cap) {
        uint32_t nc = x->cap ? x->cap : 16384;
        while (nc < x->len + len + 1) nc *= 2;
        char *nb = realloc(x->buf, nc);
        if (!nb) { x->overflow = true; return 0; }
        x->buf = nb;
        x->cap = nc;
    }
    memcpy(x->buf + x->len, p, len);
    x->len += (uint32_t)len;
    return len;
}

static int on_progress(void *ud, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un) {
    (void)dt; (void)dn; (void)ut; (void)un;
    Xfer *x = ud;
    if (x->abort_cb && x->abort_cb(x->ud)) { x->aborted = true; return 1; }
    if (now_ms() - x->last_data > STALL_MS) return 1; /* no bytes for too long */
    return 0;
}

static void setup(CURL *h, Xfer *x, const char *url, struct curl_slist *hl) {
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_USERAGENT, UA);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hl);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, (long)CONNECT_TIMEOUT_S);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L); /* home servers often use self-signed certs */
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(h, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(h, CURLOPT_HEADERDATA, x);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, on_body);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, x);
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, on_progress);
    curl_easy_setopt(h, CURLOPT_XFERINFODATA, x);
}

static struct curl_slist *header_list(const char *const *headers) {
    struct curl_slist *hl = curl_slist_append(NULL, "Accept: */*");
    for (int i = 0; headers && headers[i]; i++)
        if (headers[i][0]) hl = curl_slist_append(hl, headers[i]);
    return hl;
}

static void describe(CURLcode rc, char *out, size_t n) {
    snprintf(out, n, "%s (%d)", curl_easy_strerror(rc), (int)rc);
}

int http_request(const char *method, const char *url, const char *const *headers,
                 const void *body, uint32_t body_len, uint32_t max_body, HttpResp *out) {
    memset(out, 0, sizeof *out);
    CURL *h = curl_easy_init();
    if (!h) { out->status = -1; snprintf(out->err, sizeof out->err, "curl init"); return -1; }
    Xfer x = {0};
    x.max = max_body ? max_body : 8u << 20;
    x.last_data = now_ms();
    struct curl_slist *hl = header_list(headers);
    if (body && body_len) hl = curl_slist_append(hl, "Content-Type: application/json");
    setup(h, &x, url, hl);
    curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");
    if (!strcmp(method, "POST")) {
        curl_easy_setopt(h, CURLOPT_POST, 1L);
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, body ? (const char *)body : "");
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)(body ? body_len : 0));
        curl_easy_setopt(h, CURLOPT_POSTREDIR, (long)CURL_REDIR_POST_ALL);
    } else if (strcmp(method, "GET")) {
        curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, method);
        if (body && body_len) {
            curl_easy_setopt(h, CURLOPT_POSTFIELDS, (const char *)body);
            curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)body_len);
        }
    }
    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(h);
    curl_slist_free_all(hl);
    if (rc != CURLE_OK) {
        if (x.overflow) snprintf(out->err, sizeof out->err, "response too large");
        else describe(rc, out->err, sizeof out->err);
        dbg("%s %s failed: %s", method, url, out->err);
        free(x.buf);
        out->status = -1;
        return -1;
    }
    if (!x.buf) x.buf = calloc(1, 1);
    else x.buf[x.len] = 0;
    out->status = (int)status;
    out->data = x.buf;
    out->len = x.len;
    return 0;
}

void http_resp_free(HttpResp *r) {
    free(r->data);
    r->data = NULL;
    r->len = 0;
}

struct HttpConn { CURL *h; };

HttpConn *http_conn_new(void) {
    HttpConn *c = calloc(1, sizeof *c);
    if (c && !(c->h = curl_easy_init())) { free(c); c = NULL; }
    return c;
}

void http_conn_free(HttpConn *c) {
    if (!c) return;
    curl_easy_cleanup(c->h);
    free(c);
}

int http_get_range(HttpConn *conn, const char *url, const char *const *headers, uint64_t from, uint64_t to,
                   HttpSink sink, HttpAbort abort_cb, void *ud, HttpRangeResult *res) {
    memset(res, 0, sizeof *res);
    CURL *h = conn ? conn->h : curl_easy_init();
    if (!h) { snprintf(res->err, sizeof res->err, "curl init"); return -1; }
    if (conn) curl_easy_reset(h); /* keeps the connection cache */
    Xfer x = {0};
    x.sink = sink;
    x.abort_cb = abort_cb;
    x.ud = ud;
    x.last_data = now_ms();
    struct curl_slist *hl = header_list(headers);
    setup(h, &x, url, hl);
    char range[64];
    if (from || to) {
        if (to) snprintf(range, sizeof range, "%llu-%llu", (unsigned long long)from, (unsigned long long)to);
        else snprintf(range, sizeof range, "%llu-", (unsigned long long)from);
        curl_easy_setopt(h, CURLOPT_RANGE, range);
    }
    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    if (!conn) curl_easy_cleanup(h);
    curl_slist_free_all(hl);
    res->status = (int)status;
    res->total = x.total;
    res->received = x.received;
    res->aborted = x.aborted;
    if (rc != CURLE_OK) {
        if (!x.aborted) describe(rc, res->err, sizeof res->err);
        return -1;
    }
    return 0;
}
