/* Offline progress queue: the latest position per book, saved to SD and
 * flushed to the server (PATCH /api/me/progress) when back online. */
#ifndef ABSVITA_PROGRESS_QUEUE_H
#define ABSVITA_PROGRESS_QUEUE_H
#include <stdbool.h>
#include <stdint.h>

#define PQ_MAX 64

typedef struct {
    char item_id[40];
    double current_time, duration;
    uint64_t updated_ms;
    bool finished;
    bool pending; /* not yet confirmed by the server */
} QEntry;

typedef struct {
    QEntry e[PQ_MAX];
    int n;
} ProgressQueue;

/* Inserts or replaces the entry for the same item if e is at least as new.
 * When full, the oldest entry is evicted. */
void pq_put(ProgressQueue *q, const QEntry *e);
const QEntry *pq_get(const ProgressQueue *q, const char *item_id);
void pq_mark_synced(ProgressQueue *q, const char *item_id, uint64_t updated_ms);
/* A missing or corrupt file yields an empty queue (returns -1). */
int pq_load(ProgressQueue *q, const char *path);
int pq_save(const ProgressQueue *q, const char *path);

#endif
