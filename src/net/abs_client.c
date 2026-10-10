#include "abs_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "abs_json.h"
#include "dbg.h"
#include "http.h"
#include "osal.h"
#include "strutil.h"
#include "url.h"

static Config *g_cfg;
static void (*g_save)(void);
static char g_access[1024];
static Mutex g_lock;
static Mutex g_refresh_lock; /* refresh tokens rotate: only one refresh at a time */
static volatile int g_gen;       /* bumped whenever a new access token is stored */
static volatile uint64_t g_offline_until; /* fail fast after a network error */
static char g_last_detail[64];
static volatile int g_last_status;
const char *absc_last_detail(void) { return g_last_detail; }

#define OFFLINE_BACKOFF_MS 20000
static bool offline_now(void) { return now_ms() < g_offline_until; }
static void note_net(int ok) { g_offline_until = ok ? 0 : now_ms() + OFFLINE_BACKOFF_MS; }
bool absc_reachable(void) { return !offline_now(); }

void absc_init(Config *cfg, void (*save_cfg)(void)) {
    g_cfg = cfg;
    g_save = save_cfg;
    g_access[0] = 0;
    mutex_init(&g_lock);
    mutex_init(&g_refresh_lock);
}

int absc_url(const char *path, char *out, size_t n) {
    return url_join(g_cfg->server, path, out, n) < 0 ? -1 : 0;
}

void absc_auth_header(char *out, size_t n) {
    mutex_lock(&g_lock);
    if (g_access[0]) snprintf(out, n, "Authorization: Bearer %s", g_access);
    else out[0] = 0;
    mutex_unlock(&g_lock);
}

static int refresh_token(int seen_gen);

void absc_stream_auth(char *out, unsigned n, int refresh) {
    char cur[1100];
    absc_auth_header(cur, sizeof cur);
    /* refresh only if the rejected header is still the current one (another
     * thread may have renewed it already) */
    if (!cur[0] || (refresh && !strcmp(out, cur))) refresh_token(g_gen);
    absc_auth_header(out, n);
}

int absc_have_session(void) {
    return g_cfg->refresh[0] != 0;
}

void absc_logout(void) {
    mutex_lock(&g_lock);
    g_access[0] = 0;
    g_cfg->refresh[0] = 0;
    mutex_unlock(&g_lock);
    if (g_save) g_save();
}

int absc_status(const char *server) {
    char url[256];
    if (url_join(server, "/status", url, sizeof url) < 0) return ABS_ERR_SERVER;
    HttpResp r;
    if (http_request("GET", url, NULL, NULL, 0, 64 * 1024, &r) != 0) {
        snprintf(g_last_detail, sizeof g_last_detail, "%s", r.err);
        note_net(0);
        return ABS_ERR_NET;
    }
    note_net(1);
    int ok = r.status == 200 && r.data && strstr(r.data, "audiobookshelf");
    if (!ok) snprintf(g_last_detail, sizeof g_last_detail, "HTTP %d", r.status);
    dbg("status %s -> %d", url, r.status);
    http_resp_free(&r);
    return ok ? ABS_OK : ABS_ERR_SERVER;
}

static int store_tokens(const char *json) {
    char access[1024], refresh[1024], uid[48];
    if (abs_parse_login(json, access, sizeof access, refresh, sizeof refresh, uid, sizeof uid) != 0) return ABS_ERR_SERVER;
    mutex_lock(&g_lock);
    str_copy(g_access, sizeof g_access, access);
    if (refresh[0]) str_copy(g_cfg->refresh, sizeof g_cfg->refresh, refresh);
    if (uid[0]) str_copy(g_cfg->user_id, sizeof g_cfg->user_id, uid);
    g_gen++;
    mutex_unlock(&g_lock);
    if (g_save) g_save();
    return ABS_OK;
}

int absc_login(const char *user, const char *pass) {
    char url[256], body[512];
    if (absc_url("/login", url, sizeof url) < 0) return ABS_ERR_SERVER;
    if (abs_build_login_body(user, pass, body, sizeof body) < 0) return ABS_ERR_SERVER;
    const char *hdr[] = {"x-return-tokens: true", NULL};
    HttpResp r;
    if (http_request("POST", url, hdr, body, strlen(body), 1 << 20, &r) != 0) {
        snprintf(g_last_detail, sizeof g_last_detail, "%s", r.err);
        note_net(0);
        return ABS_ERR_NET;
    }
    note_net(1);
    snprintf(g_last_detail, sizeof g_last_detail, "HTTP %d", r.status);
    dbg("login -> %d", r.status);
    int rc = r.status == 200 ? store_tokens(r.data) : (r.status == 401 || r.status == 400 ? ABS_ERR_AUTH : ABS_ERR_SERVER);
    http_resp_free(&r);
    if (rc == ABS_OK) str_copy(g_cfg->username, sizeof g_cfg->username, user);
    return rc;
}

static int refresh_token_locked(void) {
    dbg("refreshing token");
    if (!g_cfg->refresh[0]) return ABS_ERR_AUTH;
    char url[256], hdr[1100];
    if (absc_url("/auth/refresh", url, sizeof url) < 0) return ABS_ERR_SERVER;
    snprintf(hdr, sizeof hdr, "x-refresh-token: %s", g_cfg->refresh);
    const char *hdrs[] = {hdr, NULL};
    HttpResp r;
    if (http_request("POST", url, hdrs, NULL, 0, 1 << 20, &r) != 0) {
        dbg("refresh failed: %s", r.err);
        note_net(0);
        return ABS_ERR_NET;
    }
    note_net(1);
    dbg("refresh -> %d", r.status);
    int rc;
    if (r.status == 200) {
        rc = store_tokens(r.data);
        if (rc != ABS_OK) dbg("refresh: unexpected reply: %.120s", r.data ? r.data : "");
    }
    else if (r.status == 401 || r.status == 403) {
        rc = ABS_ERR_AUTH;
        absc_logout();
    } else rc = ABS_ERR_SERVER;
    http_resp_free(&r);
    return rc;
}

/* Refreshes unless another thread already did since `seen_gen`. */
static int refresh_token(int seen_gen) {
    mutex_lock(&g_refresh_lock);
    int rc = g_gen != seen_gen ? ABS_OK : refresh_token_locked();
    mutex_unlock(&g_refresh_lock);
    return rc;
}

/* Authenticated request with one refresh-and-retry on 401. */
static int api(const char *method, const char *path, const char *body, uint32_t max, HttpResp *out) {
    char url[512];
    if (absc_url(path, url, sizeof url) < 0) return ABS_ERR_SERVER;
    if (offline_now()) { dbg("%s %s: offline, skipped", method, path); return ABS_ERR_NET; }
    if (!g_access[0]) {
        int rc = refresh_token(g_gen);
        if (rc != ABS_OK) return rc;
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        char auth[1100];
        int gen = g_gen;
        absc_auth_header(auth, sizeof auth);
        const char *hdrs[] = {auth, NULL};
        if (http_request(method, url, hdrs, body, body ? strlen(body) : 0, max, out) != 0) {
            snprintf(g_last_detail, sizeof g_last_detail, "%s", out->err);
            note_net(0);
            return ABS_ERR_NET;
        }
        note_net(1);
        if (out->status != 401 || attempt == 1) break;
        http_resp_free(out);
        int rc = refresh_token(gen);
        if (rc != ABS_OK) return rc;
    }
    if (out->status == 401) { http_resp_free(out); return ABS_ERR_AUTH; }
    g_last_status = out->status;
    if (out->status < 200 || out->status >= 300) {
        dbg("%s %s -> HTTP %d", method, path, out->status);
        http_resp_free(out);
        return ABS_ERR_SERVER;
    }
    return ABS_OK;
}

static int ensure_library_id(void) {
    if (g_cfg->library_id[0]) return ABS_OK;
    HttpResp r;
    int rc = api("GET", "/api/libraries", NULL, 1 << 20, &r);
    if (rc) return rc;
    rc = abs_parse_libraries(r.data, g_cfg->library_id, sizeof g_cfg->library_id) == 0 ? ABS_OK : ABS_ERR_SERVER;
    http_resp_free(&r);
    if (rc == ABS_OK && g_save) g_save();
    return rc;
}

int absc_fetch_library(BookSummary **out, int *n, void (*progress)(int, int)) {
    *out = NULL;
    *n = 0;
    int rc = ensure_library_id();
    if (rc) return rc;
    int cap = 128, count = 0, total = 0;
    BookSummary *books = malloc(sizeof *books * cap);
    if (!books) return ABS_ERR_NOMEM;
    for (int page = 0; page < 200; page++) {
        char path[256];
        snprintf(path, sizeof path, "/api/libraries/%s/items?minified=1&limit=100&page=%d&sort=media.metadata.title", g_cfg->library_id, page);
        HttpResp r;
        rc = api("GET", path, NULL, 4 << 20, &r);
        if (rc) break;
        if (count + 100 > cap) {
            cap = (count + 100) * 2;
            BookSummary *nb = realloc(books, sizeof *books * cap);
            if (!nb) { http_resp_free(&r); rc = ABS_ERR_NOMEM; break; }
            books = nb;
        }
        int before = count;
        rc = abs_parse_items_page(r.data, books, cap, &count, &total) == 0 ? ABS_OK : ABS_ERR_SERVER;
        http_resp_free(&r);
        if (rc) break;
        if (progress) progress(count, total);
        if (count == before || count >= total) break;
    }
    if (rc && rc != ABS_ERR_NOMEM && count == 0) {
        free(books);
        return rc;
    }
    *out = books;
    *n = count;
    absc_fetch_progress(books, count);
    return ABS_OK;
}

int absc_fetch_progress(BookSummary *books, int n) {
    HttpResp r;
    int rc = api("GET", "/api/me", NULL, 8 << 20, &r);
    if (rc) return rc;
    abs_parse_me_progress(r.data, books, n);
    http_resp_free(&r);
    return ABS_OK;
}

int absc_fetch_in_progress(char ids[][40], int max) {
    HttpResp r;
    char path[64];
    snprintf(path, sizeof path, "/api/me/items-in-progress?limit=%d", max);
    int rc = api("GET", path, NULL, 4 << 20, &r);
    if (rc) return rc;
    int n = abs_parse_in_progress(r.data, ids, max);
    http_resp_free(&r);
    return n;
}

int absc_fetch_item(const char *id, BookDetail *out) {
    char path[128];
    snprintf(path, sizeof path, "/api/items/%s?expanded=1", id);
    HttpResp r;
    int rc = api("GET", path, NULL, 4 << 20, &r);
    if (rc) return rc;
    rc = abs_parse_item(r.data, out) == 0 ? ABS_OK : ABS_ERR_SERVER;
    http_resp_free(&r);
    return rc;
}

int absc_play(const char *id, const char *model, PlaySession *out) {
    char path[128], body[1024];
    snprintf(path, sizeof path, "/api/items/%s/play", id);
    abs_build_play_body(g_cfg->device_id, model, APP_VERSION, body, sizeof body);
    HttpResp r;
    int rc = api("POST", path, body, 4 << 20, &r);
    if (rc == ABS_ERR_SERVER && g_last_status == 404) return ABS_ERR_NO_AUDIO;
    if (rc) return rc;
    int pr = abs_parse_play(r.data, out);
    rc = pr == 0 ? ABS_OK : pr == -2 ? ABS_ERR_NO_AUDIO : ABS_ERR_SERVER;
    http_resp_free(&r);
    return rc;
}

static int session_post(const char *sid, const char *what, double t, double listened, double dur) {
    char path[128], body[256];
    snprintf(path, sizeof path, "/api/session/%s/%s", sid, what);
    abs_build_sync_body(t, listened, dur, body, sizeof body);
    HttpResp r;
    int rc = api("POST", path, body, 64 * 1024, &r);
    if (rc == ABS_OK) http_resp_free(&r);
    return rc;
}

int absc_sync(const char *sid, double t, double listened, double dur) { return session_post(sid, "sync", t, listened, dur); }
int absc_close(const char *sid, double t, double listened, double dur) { return session_post(sid, "close", t, listened, dur); }

int absc_sync_local(const QEntry *e, double listened) {
    char body[768];
    if (abs_build_local_session_body(e->item_id, g_cfg->device_id, e->current_time, e->duration, listened, e->updated_ms, body, sizeof body) < 0)
        return ABS_ERR_SERVER;
    HttpResp r;
    int rc = api("POST", "/api/session/local", body, 64 * 1024, &r);
    if (rc == ABS_OK) http_resp_free(&r);
    return rc;
}

int absc_fetch_cover(const char *id, int width, uint8_t **data, uint32_t *len) {
    char path[128];
    snprintf(path, sizeof path, "/api/items/%s/cover?width=%d&format=jpeg", id, width);
    HttpResp r;
    int rc = api("GET", path, NULL, 2 << 20, &r);
    if (rc) return rc;
    *data = (uint8_t *)r.data;
    *len = r.len;
    return ABS_OK;
}
