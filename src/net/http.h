/* HTTP over libcurl: small requests and ranged streaming GETs.
 * Every transfer has a 10 s connect timeout and aborts after 15 s without data. */
#ifndef ABSVITA_HTTP_H
#define ABSVITA_HTTP_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int status;      /* HTTP status, or <0 for a transport error */
    char *data;      /* malloc'd, NUL-terminated (NULL on error) */
    uint32_t len;
    char err[64];    /* transport error text when status < 0 */
} HttpResp;

/* Writes the current "Authorization: Bearer ..." header into out (or "").
 * refresh != 0 means the request made with the header still in out got a 401. */
typedef void (*AuthFn)(char *out, unsigned n, int refresh);

int http_init(void);
void http_exit(void);
/* Offset (ms) to add to the local clock to get server time (from Date headers). */
long long http_clock_offset(bool *known);
void http_set_clock_offset(long long ms);

/* headers: NULL-terminated "Name: value" strings (may be NULL). Follows up to 3
 * redirects. Body capped at max_body (0 = 8 MB). Returns 0 when a response was
 * received (check out->status), <0 on transport error. */
int http_request(const char *method, const char *url, const char *const *headers,
                 const void *body, uint32_t body_len, uint32_t max_body, HttpResp *out);
void http_resp_free(HttpResp *r);

/* A reusable connection (keeps the TCP connection alive between range requests).
 * Not thread-safe: one user at a time. */
typedef struct HttpConn HttpConn;
HttpConn *http_conn_new(void);
void http_conn_free(HttpConn *c);

/* Receives body bytes of a 2xx response. Return 0 to continue, <0 to abort. */
typedef int (*HttpSink)(void *ud, const uint8_t *data, uint32_t n);
/* Polled during the transfer; return nonzero to abort. */
typedef int (*HttpAbort)(void *ud);

typedef struct {
    int status;        /* HTTP status (0 if none) */
    uint64_t total;    /* full resource size from Content-Range / Content-Length (0 unknown) */
    uint64_t received; /* body bytes delivered to the sink */
    bool aborted;      /* sink or abort callback stopped it */
    char err[64];      /* transport error text ("" on success) */
} HttpRangeResult;

/* GET url with "Range: bytes=from-to" (to == 0: open-ended; from == 0 && to == 0:
 * no Range header). conn may be NULL. Returns 0 when the transfer completed
 * (check res->status), <0 on transport error or abort. */
int http_get_range(HttpConn *conn, const char *url, const char *const *headers, uint64_t from, uint64_t to,
                   HttpSink sink, HttpAbort abort_cb, void *ud, HttpRangeResult *res);

#endif
