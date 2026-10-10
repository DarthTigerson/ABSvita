/* Playback policies: when to sync progress, and the sleep timer. */
#ifndef ABSVITA_POLICY_H
#define ABSVITA_POLICY_H
#include <stdbool.h>

typedef struct {
    double interval;   /* seconds of playback between syncs */
    double since_sync; /* playback seconds since the last sync */
    double listened;   /* wall seconds listened since the last sync */
} SyncPolicy;

void sync_init(SyncPolicy *p, double interval);
/* Advance by dt wall seconds; true when a periodic sync is due. */
bool sync_tick(SyncPolicy *p, double dt, bool playing);
/* Returns the listened seconds to report and resets the counters. */
double sync_take_listened(SyncPolicy *p);

typedef enum { SLEEP_OFF, SLEEP_MINUTES, SLEEP_END_CHAPTER } SleepMode;
enum { SLEEP_NONE = 0, SLEEP_FADING = 1, SLEEP_STOP = 2 };
#define SLEEP_FADE_SECONDS 5.0

typedef struct {
    SleepMode mode;
    double remaining;   /* SLEEP_MINUTES: wall seconds left */
    double chapter_end; /* SLEEP_END_CHAPTER: book time to stop at */
} SleepTimer;

void sleep_set(SleepTimer *s, SleepMode mode, double minutes, double chapter_end);
/* dt in wall seconds (only while playing), pos = current book time. */
int sleep_tick(SleepTimer *s, double dt, double pos);
/* Volume multiplier: 1.0, ramping to 0 over the last SLEEP_FADE_SECONDS. */
float sleep_volume(const SleepTimer *s, double pos, float speed);

#endif
