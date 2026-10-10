#include "engine.h"

#include <stdlib.h>
#include <string.h>

#include "dbg.h"
#include "osal.h"
#include "sonic.h"
#include "timeline.h"

#define OUT_FRAMES 1024 /* per sink write: ~23 ms at 44.1 kHz */

typedef enum { C_LOAD, C_STOP, C_SEEK, C_SPEED } CmdType;
typedef struct { CmdType type; double arg; bool autoplay; } Cmd;

static struct {
    Thread th;
    volatile bool quit;
    Mutex lock;
    Cmd q[16];
    int qn;
    PlayableBook pending_book;
    bool has_pending_book;
    AudioSink sink;
    AuthFn auth;

    /* published state */
    volatile EngineState state;
    volatile double pos;
    volatile float cpu;
    volatile int error;
    volatile bool want_play;
    volatile float speed, volume;

    /* engine-thread private */
    PlayableBook book;
    int cur;
    Stream *st;
    int rate, ch;        /* sink format (0 = not open) */
    sonicStream son;
    float son_speed;
    double anchor_t;     /* book time of the first output frame since the anchor */
    double out_frames;   /* output frames since the anchor */
    bool end_of_book, flushed;
    int16_t dec[STREAM_MAX_FRAMES * 2];
    int dec_frames, dec_pos, dec_ch, dec_rate;
    int16_t out[OUT_FRAMES * 2];
    uint64_t busy_ms, window_start;
} E;

static void set_state(EngineState s) { E.state = s; }

static void close_stream(void) {
    mutex_lock(&E.lock);
    Stream *s = E.st;
    E.st = NULL;
    mutex_unlock(&E.lock);
    stream_close(s);
}

static void reset_output(void) {
    if (E.son) { sonicDestroyStream(E.son); E.son = NULL; }
    E.son_speed = 1.0f;
    E.dec_frames = E.dec_pos = 0;
    E.end_of_book = E.flushed = false;
}

static void open_sink(int rate, int ch) {
    if (E.rate) E.sink.close(E.sink.ud);
    E.rate = E.ch = 0;
    if (E.sink.open(E.sink.ud, rate, ch) == 0) {
        E.rate = rate;
        E.ch = ch;
    } else {
        dbg("engine: audio output %d Hz x%d failed", rate, ch);
    }
}

/* Opens track i at local time t and anchors the clock there. */
static int open_track(int i, double t) {
    close_stream();
    int err = 0;
    const TrackSource *src = &E.book.sources[i];
    dbg("engine: open track %d at %.1f (%s)", i, t, src->local ? src->path : src->url);
    Stream *s = stream_open(src, E.auth, &err);
    if (!s) {
        dbg("engine: track %d failed to open (%d)", i, err);
        return err ? err : STREAM_ERR;
    }
    double actual = t;
    if (t > 0.05 && stream_seek(s, t, &actual) != 0) actual = 0;
    mutex_lock(&E.lock);
    E.st = s;
    mutex_unlock(&E.lock);
    E.cur = i;
    E.anchor_t = E.book.tracks[i].start_offset + actual;
    E.out_frames = 0;
    E.end_of_book = false;
    return 0;
}

static void reanchor(void) {
    if (E.rate) E.anchor_t += E.out_frames * E.son_speed / E.rate;
    E.out_frames = 0;
}

static void ensure_sonic(void) {
    float sp = E.speed;
    if (sp > 0.99f && sp < 1.01f) {
        if (E.son) {
            reanchor();
            sonicDestroyStream(E.son);
            E.son = NULL;
        }
        E.son_speed = 1.0f;
        return;
    }
    if (!E.son || E.son_speed != sp) {
        reanchor();
        if (!E.son) E.son = sonicCreateStream(E.rate, E.ch);
        if (E.son) sonicSetSpeed(E.son, sp);
        E.son_speed = E.son ? sp : 1.0f;
    }
}

/* More decoded PCM into E.dec. 1 ok, 0 nothing now (new track / wait / end), <0 error. */
static int pull_decoded(void) {
    while (E.st) {
        int ch, rate;
        int n = stream_read(E.st, E.dec, STREAM_MAX_FRAMES, &ch, &rate);
        if (n > 0) {
            E.dec_frames = n;
            E.dec_pos = 0;
            E.dec_ch = ch;
            E.dec_rate = rate;
            return 1;
        }
        if (n == STREAM_EOF) {
            if (E.cur + 1 < E.book.ntracks) {
                int rc = open_track(E.cur + 1, 0);
                return rc ? rc : 0;
            }
            E.end_of_book = true;
            return 0;
        }
        if (n == STREAM_IO || n == STREAM_INTERRUPTED) return 0;
        return n;
    }
    return 0;
}

/* Fills E.out. Returns frames (0 = nothing available now), <0 on error.
 * *t0 gets the book time of the first frame. */
static int produce(double *t0) {
    int frames = 0;
    *t0 = E.anchor_t + (E.rate ? E.out_frames * E.son_speed / E.rate : 0);
    while (frames < OUT_FRAMES) {
        if (E.son && E.rate) {
            int k = sonicReadShortFromStream(E.son, E.out + frames * E.ch, OUT_FRAMES - frames);
            if (k > 0) { frames += k; continue; }
        }
        if (E.dec_pos < E.dec_frames) {
            if (E.dec_ch != E.ch || E.dec_rate != E.rate) {
                if (frames > 0) break; /* finish this chunk in the old format first */
                open_sink(E.dec_rate, E.dec_ch);
                if (!E.rate) return STREAM_ERR;
                if (E.son) { sonicDestroyStream(E.son); E.son = NULL; }
                E.son_speed = 1.0f;
                *t0 = E.anchor_t + E.out_frames / E.rate;
            }
            ensure_sonic();
            int n = E.dec_frames - E.dec_pos;
            if (E.son) {
                sonicWriteShortToStream(E.son, E.dec + E.dec_pos * E.ch, n);
                E.dec_pos = E.dec_frames;
            } else {
                if (n > OUT_FRAMES - frames) n = OUT_FRAMES - frames;
                memcpy(E.out + frames * E.ch, E.dec + E.dec_pos * E.ch, (size_t)n * E.ch * 2);
                E.dec_pos += n;
                frames += n;
            }
            continue;
        }
        if (E.end_of_book) {
            if (E.son && !E.flushed) { sonicFlushStream(E.son); E.flushed = true; continue; }
            break;
        }
        int cur_before = E.cur;
        int rc = pull_decoded();
        if (rc < 0) return rc;
        if (rc == 0) {
            if (E.cur != cur_before && frames == 0) { /* fresh track, fresh anchor */
                *t0 = E.anchor_t;
                continue;
            }
            break;
        }
    }
    return frames;
}

static void apply_volume(int frames) {
    float v = E.volume;
    if (v >= 0.999f) return;
    int n = frames * E.ch;
    int g = (int)(v * 32768.0f);
    for (int i = 0; i < n; i++) E.out[i] = (int16_t)((E.out[i] * g) >> 15);
}

/* ---- commands ------------------------------------------------------------------ */

static void do_load(PlayableBook *b, double t, bool autoplay) {
    set_state(ENG_LOADING);
    close_stream();
    reset_output();
    free(E.book.tracks);
    free(E.book.sources);
    E.book = *b;
    E.want_play = autoplay;
    E.pos = t;
    E.error = 0;
    double local;
    int i = tl_locate(E.book.tracks, E.book.ntracks, t, &local);
    int rc = i < 0 ? STREAM_ERR : open_track(i, local);
    if (rc) { E.error = rc; set_state(ENG_ERROR); return; }
    E.pos = E.anchor_t;
    set_state(autoplay ? ENG_BUFFERING : ENG_PAUSED);
}

static void do_seek(double t) {
    if (!E.book.ntracks) return;
    if (t > E.book.duration - 0.5) t = E.book.duration - 0.5;
    if (t < 0) t = 0;
    reset_output();
    double local;
    int i = tl_locate(E.book.tracks, E.book.ntracks, t, &local);
    int rc;
    if (i == E.cur && E.st) {
        double actual = local;
        rc = stream_seek(E.st, local, &actual);
        E.anchor_t = E.book.tracks[i].start_offset + actual;
        E.out_frames = 0;
    } else {
        rc = open_track(i, local);
    }
    if (rc) { E.error = rc; set_state(ENG_ERROR); return; }
    dbg("engine: seek %.2f -> track %d at %.2f", t, i, E.anchor_t);
    E.pos = E.anchor_t;
    set_state(E.want_play ? ENG_BUFFERING : ENG_PAUSED);
}

static void process_commands(void) {
    for (;;) {
        mutex_lock(&E.lock);
        if (!E.qn) { mutex_unlock(&E.lock); return; }
        Cmd c = E.q[0];
        memmove(E.q, E.q + 1, sizeof(Cmd) * (--E.qn));
        PlayableBook b = E.pending_book;
        bool hb = E.has_pending_book;
        if (c.type == C_LOAD) E.has_pending_book = false;
        if (E.st && !E.qn) stream_interrupt(E.st, 0);
        mutex_unlock(&E.lock);
        switch (c.type) {
        case C_LOAD: if (hb) do_load(&b, c.arg, c.autoplay); break;
        case C_STOP:
            close_stream();
            reset_output();
            set_state(ENG_IDLE);
            break;
        case C_SEEK: do_seek(c.arg); break;
        case C_SPEED: break; /* applied by ensure_sonic() on the next chunk */
        }
    }
}

/* ---- thread ------------------------------------------------------------------- */

static void *engine_thread(void *arg) {
    (void)arg;
    E.window_start = now_ms();
    while (!E.quit) {
        process_commands();
        EngineState s = E.state;
        bool active = (s == ENG_PLAYING || s == ENG_BUFFERING) && E.want_play;
        if (s == ENG_PAUSED && E.want_play) { set_state(ENG_BUFFERING); active = true; }
        if (!active) {
            if (s == ENG_PLAYING || s == ENG_BUFFERING) set_state(ENG_PAUSED);
            sleep_ms(10);
        } else {
            uint64_t t_start = now_ms();
            double t0;
            int frames = produce(&t0);
            E.busy_ms += now_ms() - t_start;
            if (frames < 0) {
                dbg("engine: decode error %d", frames);
                E.error = frames;
                set_state(ENG_ERROR);
            } else if (frames == 0) {
                if (E.end_of_book) {
                    E.pos = E.book.duration > 0 ? E.book.duration : E.pos;
                    E.want_play = false;
                    set_state(ENG_ENDED);
                } else {
                    set_state(ENG_BUFFERING);
                    sleep_ms(10);
                }
            } else {
                apply_volume(frames);
                double step = (double)E.son_speed / E.rate;
                E.out_frames += frames;
                E.pos = t0;
                if (E.state != ENG_PLAYING && E.qn == 0) set_state(ENG_PLAYING);
                E.sink.write(E.sink.ud, E.out, frames);
                E.pos = t0 + frames * step;
            }
        }
        uint64_t now = now_ms();
        if (now - E.window_start >= 1000) {
            E.cpu = (float)E.busy_ms / (float)(now - E.window_start);
            E.busy_ms = 0;
            E.window_start = now;
        }
    }
    close_stream();
    reset_output();
    if (E.rate) E.sink.close(E.sink.ud);
    return NULL;
}

int engine_init(const AudioSink *sink, AuthFn auth) {
    memset(&E, 0, sizeof E);
    mutex_init(&E.lock);
    E.sink = *sink;
    E.auth = auth;
    E.speed = 1.0f;
    E.son_speed = 1.0f;
    E.volume = 1.0f;
    return thread_start(&E.th, engine_thread, NULL, 128, 1) ? 0 : -1;
}

void engine_exit(void) {
    mutex_lock(&E.lock);
    if (E.st) stream_interrupt(E.st, 1);
    mutex_unlock(&E.lock);
    E.quit = true;
    thread_join(&E.th);
    free(E.book.tracks);
    free(E.book.sources);
    if (E.has_pending_book) { free(E.pending_book.tracks); free(E.pending_book.sources); }
    memset(&E.book, 0, sizeof E.book);
    mutex_destroy(&E.lock);
}

static void push(Cmd c) {
    mutex_lock(&E.lock);
    /* collapse repeated seeks/speed changes */
    if (E.qn && E.q[E.qn - 1].type == c.type && (c.type == C_SEEK || c.type == C_SPEED)) E.q[E.qn - 1] = c;
    else if (E.qn < 16) E.q[E.qn++] = c;
    if (E.st) stream_interrupt(E.st, 1); /* unblock a waiting network read */
    mutex_unlock(&E.lock);
}

void engine_load(const PlayableBook *b, double start_t, bool autoplay) {
    PlayableBook copy = *b;
    copy.tracks = malloc(sizeof(Track) * b->ntracks);
    copy.sources = malloc(sizeof(TrackSource) * b->ntracks);
    if (!copy.tracks || !copy.sources) { free(copy.tracks); free(copy.sources); return; }
    memcpy(copy.tracks, b->tracks, sizeof(Track) * b->ntracks);
    memcpy(copy.sources, b->sources, sizeof(TrackSource) * b->ntracks);
    if (copy.duration <= 0) copy.duration = tl_duration(copy.tracks, copy.ntracks);
    mutex_lock(&E.lock);
    if (E.has_pending_book) { free(E.pending_book.tracks); free(E.pending_book.sources); }
    E.pending_book = copy;
    E.has_pending_book = true;
    E.state = ENG_LOADING;
    E.pos = start_t;
    E.want_play = autoplay;
    mutex_unlock(&E.lock);
    E.book.duration = copy.duration;
    push((Cmd){C_LOAD, start_t, autoplay});
}

void engine_stop(void) {
    E.want_play = false;
    push((Cmd){C_STOP, 0, false});
}

void engine_play(void) {
    if (E.state == ENG_ENDED) {
        engine_seek(0);
        E.state = ENG_BUFFERING; /* reads as playing at once, before the thread catches up */
    }
    E.want_play = true;
}

void engine_pause(void) { E.want_play = false; }

void engine_seek(double t) {
    E.pos = t < 0 ? 0 : t;
    push((Cmd){C_SEEK, t, false});
}

void engine_set_speed(float speed) {
    if (speed < 0.5f) speed = 0.5f;
    if (speed > 3.0f) speed = 3.0f;
    E.speed = speed;
    push((Cmd){C_SPEED, speed, false});
}

void engine_set_volume(float v) { E.volume = v < 0 ? 0 : v > 1 ? 1 : v; }

EngineState engine_state(void) { return E.state; }
bool engine_is_playing(void) {
    return E.want_play && (E.state == ENG_PLAYING || E.state == ENG_BUFFERING || E.state == ENG_LOADING);
}
double engine_position(void) { return E.pos; }
double engine_duration(void) { return E.book.duration; }
float engine_cpu_load(void) { return E.cpu; }
int engine_error(void) { return E.error; }
float engine_speed(void) { return E.speed; }

int engine_net_state(void) {
    mutex_lock(&E.lock);
    int s = E.st ? stream_net_state(E.st) : 0;
    mutex_unlock(&E.lock);
    return s;
}
