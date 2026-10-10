#include "worker.h"

#include <stdlib.h>

#include "osal.h"

#define QMAX 64

typedef struct { JobFn fn; void *ud; } Job;

struct Worker {
    Mutex lock;
    Cond cv;
    Thread th;
    Job q[QMAX];
    int head, n;
    volatile int busy, quit;
};

Worker *g_api, *g_sync, *g_covers;

static void *run(void *arg) {
    Worker *w = arg;
    for (;;) {
        mutex_lock(&w->lock);
        while (!w->n && !w->quit) cond_wait_ms(&w->cv, &w->lock, 500);
        if (w->quit) { mutex_unlock(&w->lock); break; }
        Job j = w->q[w->head];
        w->head = (w->head + 1) % QMAX;
        w->n--;
        w->busy = 1;
        mutex_unlock(&w->lock);
        j.fn(j.ud);
        w->busy = 0;
    }
    return NULL;
}

Worker *worker_create(const char *name, int stack_kb) {
    (void)name;
    Worker *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    mutex_init(&w->lock);
    cond_init(&w->cv);
    if (!thread_start(&w->th, run, w, stack_kb, -1)) { free(w); return NULL; }
    return w;
}

void worker_destroy(Worker *w) {
    if (!w) return;
    mutex_lock(&w->lock);
    w->quit = 1;
    cond_broadcast(&w->cv);
    mutex_unlock(&w->lock);
    thread_join(&w->th);
    while (w->n) {
        free(w->q[w->head].ud);
        w->head = (w->head + 1) % QMAX;
        w->n--;
    }
    mutex_destroy(&w->lock);
    cond_destroy(&w->cv);
    free(w);
}

bool worker_post(Worker *w, JobFn fn, void *ud) {
    if (!w) return false;
    mutex_lock(&w->lock);
    bool ok = w->n < QMAX;
    if (ok) {
        w->q[(w->head + w->n) % QMAX] = (Job){fn, ud};
        w->n++;
        cond_broadcast(&w->cv);
    }
    mutex_unlock(&w->lock);
    return ok;
}

int worker_pending(Worker *w) {
    if (!w) return 0;
    mutex_lock(&w->lock);
    int n = w->n + w->busy;
    mutex_unlock(&w->lock);
    return n;
}
