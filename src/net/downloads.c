#include "downloads.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "abs_client.h"
#include "abs_json.h"
#include "dbg.h"
#include "http.h"
#include "osal.h"
#include "strutil.h"
#include "worker.h"

#define MAXDL 128
#define SPACE_MARGIN (50ull << 20)
#define RETRIES 5

typedef struct {
    bool used;
    int gen;
    unsigned seq;              /* order added */
    DlInfo info;
    volatile bool pause;       /* stop at the next chunk */
    uint64_t base_done;        /* bytes of completed tracks (while active) */
} Entry;

static Entry g_e[MAXDL];
static unsigned g_seq;
static Mutex g_lock;
static Worker *g_dl;
static char g_dir[200];
static int64_t (*g_free_fn)(const char *);
static volatile bool g_quit;

/* ---- files -------------------------------------------------------------------- */

static void book_dir(const char *id, char *out, size_t n) { snprintf(out, n, "%s/%s", g_dir, id); }

void dl_track_path(const char *id, int i, const char *ext, char *out, size_t n) {
    snprintf(out, n, "%s/%s/%02d%s", g_dir, id, i + 1, ext);
}

static uint64_t file_size(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 ? (uint64_t)st.st_size : 0;
}

static bool exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = n > 0 ? malloc(n + 1) : NULL;
    if (s && fread(s, 1, n, f) == (size_t)n) s[n] = 0;
    else { free(s); s = NULL; }
    fclose(f);
    return s;
}

int dl_load_meta(const char *id, BookDetail *out) {
    char p[300];
    snprintf(p, sizeof p, "%s/%s/meta.json", g_dir, id);
    char *j = slurp(p);
    if (!j) return -1;
    int rc = abs_parse_item(j, out);
    free(j);
    return rc;
}

static int save_meta(const BookDetail *b) {
    char dir[260], p[300];
    book_dir(b->id, dir, sizeof dir);
    mkdir(dir, 0777);
    char *json = abs_detail_to_json(b);
    if (!json) return -1;
    snprintf(p, sizeof p, "%s/meta.json", dir);
    FILE *f = fopen(p, "wb");
    int ok = f && fputs(json, f) >= 0;
    if (f && fclose(f) != 0) ok = 0;
    free(json);
    return ok ? 0 : -1;
}

/* bytes on disk for track i (complete or partial) */
static uint64_t track_bytes(const char *id, const Track *t, int i) {
    char p[300], pp[310];
    dl_track_path(id, i, t->ext, p, sizeof p);
    uint64_t s = file_size(p);
    if (s) return s;
    snprintf(pp, sizeof pp, "%s.part", p);
    return file_size(pp);
}

/* ---- entries (call with g_lock held) ---------------------------------------------- */

static Entry *find(const char *id) {
    for (int i = 0; i < MAXDL; i++)
        if (g_e[i].used && !strcmp(g_e[i].info.id, id)) return &g_e[i];
    return NULL;
}

static Entry *add(const char *id) {
    for (int i = 0; i < MAXDL; i++)
        if (!g_e[i].used) {
            Entry *e = &g_e[i];
            int gen = e->gen + 1;
            memset(e, 0, sizeof *e);
            e->used = true;
            e->gen = gen;
            e->seq = ++g_seq;
            str_copy(e->info.id, sizeof e->info.id, id);
            return e;
        }
    return NULL;
}

static void fill_info(Entry *e, const BookDetail *b) {
    str_copy(e->info.title, sizeof e->info.title, b->title);
    str_copy(e->info.author, sizeof e->info.author, b->author);
    e->info.duration = b->duration;
    e->info.total_bytes = 0;
    for (int i = 0; i < b->ntracks; i++) e->info.total_bytes += b->tracks[i].size;
}

static void scan(void) {
    DIR *d = opendir(g_dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        BookDetail b;
        if (dl_load_meta(de->d_name, &b) != 0) continue;
        Entry *e = add(b.id[0] ? b.id : de->d_name);
        if (!e) { book_detail_free(&b); break; }
        fill_info(e, &b);
        char p[512];
        snprintf(p, sizeof p, "%s/%s/.done", g_dir, de->d_name);
        e->info.state = exists(p) ? DL_DONE : DL_PAUSED;
        for (int i = 0; i < b.ntracks; i++) e->info.done_bytes += track_bytes(e->info.id, &b.tracks[i], i);
        if (e->info.state == DL_DONE) e->info.done_bytes = e->info.total_bytes;
        book_detail_free(&b);
    }
    closedir(d);
}

/* ---- the download job ------------------------------------------------------------ */

typedef struct { char id[40]; int slot, gen; } Job;

/* the job's entry, if it still exists (lock held on return when non-NULL) */
static Entry *job_entry(const Job *j) {
    mutex_lock(&g_lock);
    Entry *e = &g_e[j->slot];
    if (e->used && e->gen == j->gen) return e;
    mutex_unlock(&g_lock);
    return NULL;
}

static void finish(const Job *j, DlState s, const char *err) {
    Entry *e = job_entry(j);
    if (!e) return;
    e->info.state = s;
    str_copy(e->info.error, sizeof e->info.error, err ? err : "");
    if (s == DL_DONE) e->info.done_bytes = e->info.total_bytes;
    mutex_unlock(&g_lock);
    if (err) dbg("download %s: %s", j->id, err);
}

typedef struct {
    FILE *f;
    const Job *job;
    bool write_error;
} Sink;

static int file_sink(void *ud, const uint8_t *data, uint32_t n) {
    Sink *s = ud;
    if (fwrite(data, 1, n, s->f) != n) { s->write_error = true; return -1; }
    Entry *e = job_entry(s->job);
    if (e) {
        e->info.done_bytes += n;
        mutex_unlock(&g_lock);
    }
    return 0;
}

static int should_stop(void *ud) {
    Sink *s = ud;
    if (g_quit) return 1;
    Entry *e = job_entry(s->job);
    if (!e) return 1;
    bool p = e->pause;
    mutex_unlock(&g_lock);
    return p;
}

enum { T_OK = 0, T_NET = -1, T_DISK = -2, T_STOP = -3, T_GONE = -4 };

/* One attempt at track i; resumes from the .part file. */
static int fetch_track_once(const Job *j, const Track *t, int i, HttpConn *conn) {
    char path[300], part[310], url[512], auth[1100] = "";
    dl_track_path(j->id, i, t->ext, path, sizeof path);
    if (exists(path)) return T_OK;
    snprintf(part, sizeof part, "%s.part", path);
    if (absc_url(t->content_url, url, sizeof url) < 0) return T_NET;
    int refresh = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        uint64_t have = file_size(part);
        if (t->size && have > t->size) { remove(part); have = 0; }
        if (t->size && have == t->size) break; /* complete already */
        if (!refresh) auth[0] = 0;
        absc_stream_auth(auth, sizeof auth, refresh);
        refresh = 0;
        Sink s = {fopen(part, have ? "ab" : "wb"), j, false};
        if (!s.f) return T_DISK;
        setvbuf(s.f, NULL, _IOFBF, 64 * 1024);
        const char *hdrs[] = {auth, NULL};
        HttpRangeResult res;
        int rc = http_get_range(conn, url, hdrs, have, 0, file_sink, should_stop, &s, &res);
        if (fclose(s.f) != 0) s.write_error = true;
        if (s.write_error) return T_DISK;
        if (res.aborted) return job_entry(j) ? (mutex_unlock(&g_lock), T_STOP) : T_GONE;
        if (res.status == 401) { refresh = 1; continue; }
        if (res.status == 200 && have > 0) { /* server ignored Range: start the file again */
            Entry *e = job_entry(j);
            if (e) { e->info.done_bytes -= e->info.done_bytes >= have ? have : e->info.done_bytes; mutex_unlock(&g_lock); }
            remove(part);
            continue;
        }
        if (rc != 0 || (res.status != 200 && res.status != 206)) {
            dbg("download %s track %d: %d %s", j->id, i, res.status, res.err);
            return T_NET;
        }
        if (t->size && file_size(part) < t->size) return T_NET; /* closed early: retry resumes */
        break;
    }
    return rename(part, path) == 0 ? T_OK : T_DISK;
}

static int fetch_track(const Job *j, const Track *t, int i, HttpConn *conn) {
    for (int attempt = 0;; attempt++) {
        int rc = fetch_track_once(j, t, i, conn);
        if (rc != T_NET || attempt + 1 >= RETRIES) return rc;
        int wait = 1000 << (attempt < 3 ? attempt : 3);
        for (int w = 0; w < wait; w += 50) {
            if (should_stop(&(Sink){NULL, j, false})) return job_entry(j) ? (mutex_unlock(&g_lock), T_STOP) : T_GONE;
            sleep_ms(50);
        }
    }
}

static void download_job(void *ud) {
    Job *j = ud;
    Entry *e = job_entry(j);
    if (!e) { free(j); return; }
    if (e->info.state != DL_QUEUED || e->pause) {
        if (e->info.state == DL_QUEUED) e->info.state = DL_PAUSED;
        mutex_unlock(&g_lock);
        free(j);
        return;
    }
    e->info.state = DL_ACTIVE;
    e->info.error[0] = 0;
    mutex_unlock(&g_lock);

    /* the book's tracks: from the server, else the saved meta (resume) */
    BookDetail b;
    memset(&b, 0, sizeof b);
    int rc = absc_fetch_item(j->id, &b);
    if (rc == ABS_OK) {
        if (save_meta(&b) != 0) { finish(j, DL_FAILED, "Can't write to the memory card"); book_detail_free(&b); free(j); return; }
    } else if (dl_load_meta(j->id, &b) != 0) {
        finish(j, DL_FAILED, rc == ABS_ERR_NET ? "Can't reach the server" : "The server sent something unexpected");
        free(j);
        return;
    }
    if (b.ntracks <= 0) { finish(j, DL_FAILED, "This book has no audio files"); book_detail_free(&b); free(j); return; }

    uint64_t have = 0;
    for (int i = 0; i < b.ntracks; i++) have += track_bytes(j->id, &b.tracks[i], i);
    if ((e = job_entry(j))) {
        fill_info(e, &b);
        e->info.done_bytes = have;
        mutex_unlock(&g_lock);
    }
    uint64_t total = 0;
    for (int i = 0; i < b.ntracks; i++) total += b.tracks[i].size;
    int64_t free_b = dl_free_bytes();
    if (free_b >= 0 && total > have && (uint64_t)free_b < total - have + SPACE_MARGIN) {
        finish(j, DL_FAILED, "Not enough space on the memory card");
        book_detail_free(&b);
        free(j);
        return;
    }

    HttpConn *conn = http_conn_new();
    int tr = conn ? T_OK : T_NET;
    for (int i = 0; i < b.ntracks && tr == T_OK; i++) tr = fetch_track(j, &b.tracks[i], i, conn);
    http_conn_free(conn);
    book_detail_free(&b);

    if (tr == T_OK) {
        char p[300];
        snprintf(p, sizeof p, "%s/%s/.done", g_dir, j->id);
        FILE *f = fopen(p, "w");
        if (f) fclose(f);
        finish(j, DL_DONE, NULL);
        dbg("download %s: done", j->id);
    } else if (tr == T_STOP) {
        finish(j, DL_PAUSED, NULL);
    } else if (tr == T_DISK) {
        finish(j, DL_FAILED, "Memory card full or write error");
    } else if (tr == T_NET) {
        finish(j, DL_FAILED, "Network error");
    }
    free(j);
}

/* ---- public -------------------------------------------------------------------------- */

void dl_init(const char *dir, int64_t (*free_space)(const char *dir)) {
    mutex_init(&g_lock);
    str_copy(g_dir, sizeof g_dir, dir);
    g_free_fn = free_space;
    g_quit = false;
    memset(g_e, 0, sizeof g_e);
    mkdir(g_dir, 0777);
    scan();
    g_dl = worker_create("downloads", 128);
}

void dl_exit(void) {
    g_quit = true;
    worker_destroy(g_dl);
    g_dl = NULL;
    mutex_destroy(&g_lock);
}

int dl_start(const char *id) {
    mutex_lock(&g_lock);
    Entry *e = find(id);
    if (!e) e = add(id);
    if (!e) { mutex_unlock(&g_lock); return -1; }
    if (e->info.state == DL_QUEUED || e->info.state == DL_ACTIVE || e->info.state == DL_DONE) {
        e->pause = false;
        mutex_unlock(&g_lock);
        return 0;
    }
    e->info.state = DL_QUEUED;
    e->info.error[0] = 0;
    e->pause = false;
    Job *j = calloc(1, sizeof *j);
    if (j) {
        str_copy(j->id, sizeof j->id, id);
        j->slot = (int)(e - g_e);
        j->gen = e->gen;
    }
    mutex_unlock(&g_lock);
    if (!j || !worker_post(g_dl, download_job, j)) {
        free(j);
        mutex_lock(&g_lock);
        if ((e = find(id))) { e->info.state = DL_FAILED; str_copy(e->info.error, sizeof e->info.error, "Busy, try again"); }
        mutex_unlock(&g_lock);
        return -1;
    }
    return 0;
}

void dl_pause(const char *id) {
    mutex_lock(&g_lock);
    Entry *e = find(id);
    if (e) {
        e->pause = true;
        if (e->info.state == DL_QUEUED) e->info.state = DL_PAUSED;
    }
    mutex_unlock(&g_lock);
}

static void rm_tree(const char *dir) {
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            char p[600];
            snprintf(p, sizeof p, "%s/%s", dir, de->d_name);
            remove(p);
        }
        closedir(d);
    }
    rmdir(dir);
}

int dl_delete(const char *id) {
    dl_pause(id);
    for (int i = 0; i < 300 && dl_state(id, NULL) == DL_ACTIVE; i++) sleep_ms(10);
    if (dl_state(id, NULL) == DL_ACTIVE) return -1;
    mutex_lock(&g_lock);
    Entry *e = find(id);
    if (e) {
        e->used = false;
        e->gen++;
    }
    mutex_unlock(&g_lock);
    char dir[260];
    book_dir(id, dir, sizeof dir);
    rm_tree(dir);
    return 0;
}

DlState dl_state(const char *id, float *frac) {
    mutex_lock(&g_lock);
    Entry *e = find(id);
    DlState s = e ? e->info.state : DL_NONE;
    if (frac) *frac = e && e->info.total_bytes ? (float)((double)e->info.done_bytes / e->info.total_bytes) : 0;
    mutex_unlock(&g_lock);
    return s;
}

bool dl_is_done(const char *id) { return dl_state(id, NULL) == DL_DONE; }

bool dl_busy(void) {
    mutex_lock(&g_lock);
    bool b = false;
    for (int i = 0; i < MAXDL && !b; i++)
        b = g_e[i].used && (g_e[i].info.state == DL_QUEUED || g_e[i].info.state == DL_ACTIVE);
    mutex_unlock(&g_lock);
    return b;
}

int dl_list(DlInfo *out, int max) {
    mutex_lock(&g_lock);
    int n = 0;
    unsigned last = 0;
    /* by the order they were added */
    for (;;) {
        int best = -1;
        for (int i = 0; i < MAXDL; i++)
            if (g_e[i].used && g_e[i].seq > last && (best < 0 || g_e[i].seq < g_e[best].seq)) best = i;
        if (best < 0 || n >= max) break;
        out[n++] = g_e[best].info;
        last = g_e[best].seq;
    }
    mutex_unlock(&g_lock);
    return n;
}

uint64_t dl_used_bytes(void) {
    mutex_lock(&g_lock);
    uint64_t t = 0;
    for (int i = 0; i < MAXDL; i++)
        if (g_e[i].used) t += g_e[i].info.done_bytes;
    mutex_unlock(&g_lock);
    return t;
}

int64_t dl_free_bytes(void) { return g_free_fn ? g_free_fn(g_dir) : -1; }
