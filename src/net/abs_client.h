/* Audiobookshelf API client. Owns the access token; every call refreshes it
 * once on 401 (rotating the stored refresh token). Thread-safe. */
#ifndef ABSVITA_ABS_CLIENT_H
#define ABSVITA_ABS_CLIENT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "http.h"
#include "models.h"
#include "progress_queue.h"

enum {
    ABS_OK = 0,
    ABS_ERR_NET = -1,    /* server unreachable */
    ABS_ERR_AUTH = -2,   /* bad credentials / refresh failed -> login screen */
    ABS_ERR_SERVER = -3, /* unexpected status or JSON */
    ABS_ERR_NOMEM = -4,
    ABS_ERR_NO_AUDIO = -5, /* the book has no playable files */
};

/* cfg is kept by pointer; refresh tokens are written back into it and saved
 * via the save callback. */
void absc_init(Config *cfg, void (*save_cfg)(void));
int absc_status(const char *server);                      /* GET /status */
int absc_login(const char *user, const char *pass);        /* stores tokens */
int absc_have_session(void);                               /* refresh token present */
bool absc_reachable(void);                                 /* false for a while after a network error */
const char *absc_last_detail(void);                        /* e.g. "Couldn't connect (7)" / "HTTP 404" */
void absc_logout(void);
/* "Authorization: Bearer <token>" (empty string if none) */
void absc_auth_header(char *out, size_t n);
/* AuthFn for streams. With refresh != 0, out holds the header that was rejected;
 * the token is renewed unless another thread already did. */
void absc_stream_auth(char *out, unsigned n, int refresh);
int absc_url(const char *path, char *out, size_t n);       /* server + path */

int absc_fetch_library(BookSummary **out, int *n, void (*progress)(int done, int total));
int absc_fetch_progress(BookSummary *books, int n);        /* /api/me merge */
int absc_fetch_in_progress(char ids[][40], int max);
int absc_fetch_item(const char *id, BookDetail *out);
int absc_play(const char *id, const char *model, PlaySession *out);
int absc_sync(const char *session_id, double t, double listened, double dur);
int absc_close(const char *session_id, double t, double listened, double dur);
int absc_sync_local(const QEntry *e, double listened);
/* JPEG bytes of the cover (malloc'd). */
int absc_fetch_cover(const char *id, int width, uint8_t **data, uint32_t *len);

#endif
