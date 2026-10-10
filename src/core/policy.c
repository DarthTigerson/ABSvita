#include "policy.h"

void sync_init(SyncPolicy *p, double interval) {
    p->interval = interval > 1 ? interval : 15;
    p->since_sync = 0;
    p->listened = 0;
}

bool sync_tick(SyncPolicy *p, double dt, bool playing) {
    if (!playing || dt <= 0) return false;
    p->since_sync += dt;
    p->listened += dt;
    return p->since_sync >= p->interval;
}

double sync_take_listened(SyncPolicy *p) {
    double l = p->listened;
    p->listened = 0;
    p->since_sync = 0;
    return l;
}

void sleep_set(SleepTimer *s, SleepMode mode, double minutes, double chapter_end) {
    s->mode = mode;
    s->remaining = minutes * 60.0;
    s->chapter_end = chapter_end;
}

static double left(const SleepTimer *s, double pos, float speed) {
    if (s->mode == SLEEP_MINUTES) return s->remaining;
    if (s->mode == SLEEP_END_CHAPTER) return (s->chapter_end - pos) / (speed > 0 ? speed : 1);
    return 1e9;
}

int sleep_tick(SleepTimer *s, double dt, double pos) {
    if (s->mode == SLEEP_OFF) return SLEEP_NONE;
    if (s->mode == SLEEP_MINUTES) s->remaining -= dt;
    double l = left(s, pos, 1);
    if (l <= 0) {
        s->mode = SLEEP_OFF;
        return SLEEP_STOP;
    }
    return l <= SLEEP_FADE_SECONDS ? SLEEP_FADING : SLEEP_NONE;
}

float sleep_volume(const SleepTimer *s, double pos, float speed) {
    if (s->mode == SLEEP_OFF) return 1.0f;
    double l = left(s, pos, speed);
    if (l >= SLEEP_FADE_SECONDS) return 1.0f;
    if (l <= 0) return 0.0f;
    return (float)(l / SLEEP_FADE_SECONDS);
}
