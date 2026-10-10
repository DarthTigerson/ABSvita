/* Streaming HTTP byte source: a ring buffer filled by a background thread with
 * one open-ended Range GET at a time. Reads near the fill point wait; far reads
 * restart the request at the new offset. Dropped connections reconnect from
 * where they stopped, with backoff. */
#ifndef ABSVITA_HTTPSRC_H
#define ABSVITA_HTTPSRC_H
#include <stdint.h>

#include "http.h"
#include "reader.h"

enum { HSRC_OK = 0, HSRC_BUFFERING = 1, HSRC_RECONNECTING = 2, HSRC_FAILED = 3 };
#define HSRC_INTERRUPTED (-3)

typedef struct HttpSrc HttpSrc;

/* url: absolute; size: total file size (0 = learn it from the response). */
HttpSrc *httpsrc_open(const char *url, uint64_t size, AuthFn auth);
Reader httpsrc_reader(HttpSrc *s);
void httpsrc_close(HttpSrc *s);
/* Makes blocked/future reads return HSRC_INTERRUPTED until cleared. */
void httpsrc_interrupt(HttpSrc *s, int on);
int httpsrc_state(HttpSrc *s);
/* Fraction of the read-ahead ring that is filled, 0..1 */
float httpsrc_fill(HttpSrc *s);

#endif
