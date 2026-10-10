#include "player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "abs_client.h"
#include "dbg.h"
#include "engine.h"
#include "httpsrc.h"
#include "osal.h"
#include "strutil.h"
#include "timeline.h"
#include "worker.h"

static Config *g_cfg;
static ProgressQueue *g_pq;
static void (*g_save_queue)(void);
static char g_model[32];

static struct {
    Mutex lock;
    volatile PlayerPhase phase;
    char error[96];
    BookDetail book;        /* owned, valid when has_book */
    bool has_book;
    char session[48];       /* ABS play session id */
    SyncPolicy sync;
    SleepTimer sleep;
    float speed;
    bool was_playing;
    int open_gen;           /* ignores stale open results */
    uint64_t last_flush;
} P;

/* ---- open ------------------------------------------------------------------ */

typedef struct {
    int gen;
    BookSummary hint;
    double start;
} OpenJob;

typedef struct {
    int gen;
    int rc;
    char msg[96];
    BookDetail book;
    char session[48];
    double start;
} OpenResult;

static OpenResult *g_open_result;

static double resume_position(const char *id, double server_t, uint64_t server_updated) {
    mutex_lock(&P.lock);
    const QEntry *q = pq_get(g_pq, id);
    double t = server_t;
    if (q && (q->updated_ms > server_updated || server_updated == 0) && (q->pending || server_t <= 0)) t = q->current_time;
    mutex_unlock(&P.lock);
    return t;
}

static void open_job(void *ud) {
    OpenJob *j = ud;
    OpenResult *r = calloc(1, sizeof *r);
    if (!r) { free(j); return; }
    r->gen = j->gen;
    const char *id = j->hint.id;
    PlaySession s;
    int prc = absc_have_session() ? absc_play(id, g_model, &s) : ABS_ERR_AUTH;
    dbg("play %s -> %d", id, prc);
    if (prc == ABS_OK) {
        dbg("session %s: %d tracks, %.0f s, server position %.0f", s.id, s.book.ntracks, s.book.duration, s.current_time);
        for (int i = 0; i < s.book.ntracks && i < 4; i++)
            dbg("  track %d: %s codec '%s' %llu bytes", i, s.book.tracks[i].ext, s.book.tracks[i].codec,
                (unsigned long long)s.book.tracks[i].size);
        r->book = s.book;
        str_copy(r->session, sizeof r->session, s.id);
        r->start = j->start >= 0 ? j->start : resume_position(id, s.current_time, j->hint.progress_updated);
    } else {
        r->rc = prc;
        str_copy(r->msg, sizeof r->msg, prc == ABS_ERR_AUTH ? "Please log in again" :
                 prc == ABS_ERR_NET ? "Can't reach the server" :
                 prc == ABS_ERR_NO_AUDIO ? "This book has no audio files" : "The server sent something unexpected");
    }
    if (r->rc == 0 && r->book.duration > 0 && r->start > r->book.duration - 5) r->start = 0; /* finished: restart */
    mutex_lock(&P.lock);
    if (g_open_result) { book_detail_free(&g_open_result->book); free(g_open_result); }
    g_open_result = r;
    mutex_unlock(&P.lock);
    free(j);
}

static void report(bool close);

void player_open(const BookSummary *b, double start) {
    if (P.has_book && !strcmp(P.book.id, b->id) && P.phase == PL_READY) {
        if (start >= 0) player_jump(start);
        if (!engine_is_playing()) engine_play();
        return;
    }
    if (P.has_book) {
        engine_stop();
        report(true);
        P.session[0] = 0;
    }
    OpenJob *j = calloc(1, sizeof *j);
    if (!j) return;
    j->gen = ++P.open_gen;
    j->hint = *b;
    j->start = start;
    P.phase = PL_OPENING;
    P.error[0] = 0;
    if (!worker_post(g_api, open_job, j)) {
        free(j);
        P.phase = PL_FAILED;
        str_copy(P.error, sizeof P.error, "Busy, try again");
    }
}

static void take_open_result(void) {
    mutex_lock(&P.lock);
    OpenResult *r = g_open_result;
    g_open_result = NULL;
    mutex_unlock(&P.lock);
    if (!r) return;
    if (r->gen != P.open_gen) { book_detail_free(&r->book); free(r); return; }
    const char *fail = NULL;
    if (r->rc != 0) fail = r->msg;
    else if (r->book.ntracks <= 0) fail = "This book has no audio";
    else
        for (int i = 0; i < r->book.ntracks; i++)
            if (!stream_supported(r->book.tracks[i].ext, r->book.tracks[i].codec)) fail = "Format not supported";
    if (fail) {
        dbg("open failed: %s", fail);
        P.phase = PL_FAILED;
        str_copy(P.error, sizeof P.error, fail);
        book_detail_free(&r->book);
        free(r);
        return;
    }
    if (P.has_book) book_detail_free(&P.book);
    P.book = r->book;
    P.has_book = true;
    str_copy(P.session, sizeof P.session, r->session);
    sync_init(&P.sync, g_cfg->sync_interval);
    sleep_set(&P.sleep, SLEEP_OFF, 0, 0);
    engine_set_volume(1.0f);

    PlayableBook pb = {r->book.ntracks, r->book.tracks, calloc(r->book.ntracks, sizeof(TrackSource)), r->book.duration};
    if (!pb.sources) {
        P.phase = PL_FAILED;
        str_copy(P.error, sizeof P.error, "Out of memory");
        free(r);
        return;
    }
    for (int i = 0; i < pb.ntracks; i++) {
        const Track *t = &r->book.tracks[i];
        TrackSource *s = &pb.sources[i];
        s->size = t->size;
        str_copy(s->ext, sizeof s->ext, t->ext);
        str_copy(s->codec, sizeof s->codec, t->codec);
        absc_url(t->content_url, s->url, sizeof s->url);
    }
    engine_set_speed(P.speed);
    engine_load(&pb, r->start, true);
    free(pb.sources);
    P.phase = PL_READY;
    free(r);
}

/* ---- progress ------------------------------------------------------------------ */

typedef struct {
    char session[48];
    QEntry e;
    double listened;
    bool close;
} SyncJob;

static void sync_job(void *ud) {
    SyncJob *j = ud;
    int rc = ABS_ERR_NET;
    if (j->session[0])
        rc = j->close ? absc_close(j->session, j->e.current_time, j->listened, j->e.duration)
                      : absc_sync(j->session, j->e.current_time, j->listened, j->e.duration);
    if (rc != ABS_OK && absc_have_session()) rc = absc_sync_local(&j->e, j->listened);
    if (rc == ABS_OK) {
        mutex_lock(&P.lock);
        pq_mark_synced(g_pq, j->e.item_id, j->e.updated_ms);
        if (g_save_queue) g_save_queue();
        mutex_unlock(&P.lock);
    }
    free(j);
}

/* Records the position locally and sends it (background). */
static void report(bool close) {
    if (!P.has_book) return;
    QEntry e = {0};
    str_copy(e.item_id, sizeof e.item_id, P.book.id);
    e.current_time = engine_position();
    e.duration = P.book.duration;
    bool known;
    long long off = http_clock_offset(&known);
    e.updated_ms = wall_ms() + (known ? off : 0);
    e.finished = e.duration > 0 && e.current_time >= e.duration - 1;
    e.pending = true;
    mutex_lock(&P.lock);
    pq_put(g_pq, &e);
    if (g_save_queue) g_save_queue();
    mutex_unlock(&P.lock);
    SyncJob *j = calloc(1, sizeof *j);
    if (!j) return;
    str_copy(j->session, sizeof j->session, P.session);
    j->e = e;
    j->listened = sync_take_listened(&P.sync);
    j->close = close;
    if (!worker_post(g_sync, sync_job, j)) free(j);
}

static void flush_job(void *ud) {
    (void)ud;
    static QEntry pending[PQ_MAX];
    int n = 0;
    mutex_lock(&P.lock);
    for (int i = 0; i < g_pq->n; i++)
        if (g_pq->e[i].pending) pending[n++] = g_pq->e[i];
    mutex_unlock(&P.lock);
    for (int i = 0; i < n; i++) {
        if (P.has_book && P.session[0] && !strcmp(pending[i].item_id, P.book.id)) continue; /* its session syncs it */
        if (absc_sync_local(&pending[i], 0) != ABS_OK) break; /* still offline */
        mutex_lock(&P.lock);
        pq_mark_synced(g_pq, pending[i].item_id, pending[i].updated_ms);
        mutex_unlock(&P.lock);
    }
    mutex_lock(&P.lock);
    if (g_save_queue) g_save_queue();
    mutex_unlock(&P.lock);
}

void player_flush_queue(void) {
    P.last_flush = now_ms();
    if (absc_have_session() && worker_pending(g_sync) < 4) worker_post(g_sync, flush_job, NULL);
}

/* ---- public -------------------------------------------------------------------- */

void player_init(Config *cfg, ProgressQueue *pq, void (*save_queue)(void), const char *model) {
    memset(&P, 0, sizeof P);
    mutex_init(&P.lock);
    g_cfg = cfg;
    g_pq = pq;
    g_save_queue = save_queue;
    str_copy(g_model, sizeof g_model, model);
    P.speed = cfg->speed > 0 ? cfg->speed : 1.0f;
    P.last_flush = now_ms();
}

void player_exit(void) {
    if (!P.has_book) return;
    engine_pause();
    report(true);
    for (int i = 0; i < 150 && worker_pending(g_sync); i++) sleep_ms(20); /* let the close go out */
    engine_stop();
    book_detail_free(&P.book);
    P.has_book = false;
}

PlayerPhase player_phase(void) { return P.phase; }
const char *player_error(void) { return P.error; }
bool player_has_book(void) { return P.has_book && P.phase == PL_READY; }
const BookDetail *player_book(void) { return P.has_book ? &P.book : NULL; }
const char *player_book_id(void) { return P.has_book ? P.book.id : ""; }
bool player_playing(void) { return P.has_book && engine_is_playing(); }
double player_position(void) { return engine_position(); }
double player_duration(void) { return P.has_book ? P.book.duration : 0; }
float player_speed(void) { return P.speed; }
const SleepTimer *player_sleep(void) { return &P.sleep; }

int player_chapter_index(void) {
    return P.has_book ? tl_chapter_at(P.book.chapters, P.book.nchapters, engine_position()) : -1;
}

void player_pause(void) {
    if (!P.has_book || !engine_is_playing()) return;
    engine_pause();
    report(false);
}

void player_toggle(void) {
    if (!P.has_book) return;
    if (engine_is_playing()) player_pause();
    else engine_play();
}

void player_jump(double t) {
    if (!P.has_book) return;
    if (t < 0) t = 0;
    if (t > P.book.duration) t = P.book.duration;
    engine_seek(t);
    report(false);
}

void player_skip(double seconds) { player_jump(engine_position() + seconds); }

void player_chapter(int dir) {
    if (!P.has_book) return;
    double t = engine_position();
    double target = dir > 0 ? tl_next_chapter(P.book.chapters, P.book.nchapters, t)
                            : tl_prev_chapter(P.book.chapters, P.book.nchapters, t);
    if (dir > 0 && target < 0) return;
    player_jump(target);
}

void player_set_speed(float s) {
    P.speed = s;
    g_cfg->speed = s;
    engine_set_speed(s);
}

void player_cycle_speed(void) { player_set_speed(cfg_next_speed(P.speed)); }

void player_cycle_sleep(void) {
    /* off -> 15 -> 30 -> 45 -> 60 min -> end of chapter -> off */
    static const int mins[] = {15, 30, 45, 60};
    SleepTimer *s = &P.sleep;
    if (s->mode == SLEEP_OFF) { sleep_set(s, SLEEP_MINUTES, mins[0], 0); return; }
    if (s->mode == SLEEP_MINUTES) {
        double cur = s->remaining / 60.0;
        for (int i = 0; i < 4; i++)
            if (cur < mins[i] - 0.5) { sleep_set(s, SLEEP_MINUTES, mins[i], 0); return; }
        int ci = player_chapter_index();
        double end = ci >= 0 ? P.book.chapters[ci].end : P.book.duration;
        sleep_set(s, SLEEP_END_CHAPTER, 0, end);
        return;
    }
    sleep_set(s, SLEEP_OFF, 0, 0);
    engine_set_volume(1.0f);
}

void player_sleep_label(char *out, int n) {
    const SleepTimer *s = &P.sleep;
    if (s->mode == SLEEP_OFF) snprintf(out, n, "Off");
    else if (s->mode == SLEEP_END_CHAPTER) snprintf(out, n, "End of chapter");
    else {
        char t[16];
        fmt_time(s->remaining, t, sizeof t);
        snprintf(out, n, "%s", t);
    }
}

const char *player_status(void) {
    if (P.phase == PL_OPENING) return "Opening\xE2\x80\xA6";
    if (P.phase == PL_FAILED) return P.error;
    if (!P.has_book) return NULL;
    static char buf[64];
    EngineState s = engine_state();
    if (s == ENG_ERROR) {
        int e = engine_error();
        if (e == STREAM_UNSUPPORTED) return "Format not supported";
        snprintf(buf, sizeof buf, "Playback error (%d)", e);
        return buf;
    }
    int ns = engine_net_state();
    if (ns == HSRC_RECONNECTING) return "Reconnecting\xE2\x80\xA6";
    if (ns == HSRC_FAILED) return "Can't load the audio";
    if (s == ENG_LOADING) return "Loading\xE2\x80\xA6";
    if (s == ENG_BUFFERING && engine_is_playing()) return "Buffering\xE2\x80\xA6";
    return NULL;
}

void player_tick(double dt) {
    take_open_result();
    if (!P.has_book || P.phase != PL_READY) return;
    bool playing = engine_is_playing();
    double pos = engine_position();

    if (sync_tick(&P.sync, dt, playing && engine_state() == ENG_PLAYING)) report(false);
    if (P.was_playing && !playing && engine_state() == ENG_ENDED) report(false);
    P.was_playing = playing;

    if (playing && engine_state() == ENG_PLAYING) {
        int r = sleep_tick(&P.sleep, dt, pos);
        if (r == SLEEP_STOP) {
            player_pause();
            engine_set_volume(1.0f);
        } else if (P.sleep.mode != SLEEP_OFF) {
            engine_set_volume(sleep_volume(&P.sleep, pos, P.speed));
        }
    }
    if (now_ms() - P.last_flush > 120000) player_flush_queue();
}
