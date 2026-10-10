/* Background job threads (network/API work off the UI thread). */
#ifndef ABSVITA_WORKER_H
#define ABSVITA_WORKER_H
#include <stdbool.h>

typedef void (*JobFn)(void *ud);
typedef struct Worker Worker;

Worker *worker_create(const char *name, int stack_kb);
/* Finishes the job in progress, drops queued ones (their ud is freed), joins. */
void worker_destroy(Worker *w);
/* FIFO. The job owns ud (free it inside fn). Returns false (ud untouched) when full. */
bool worker_post(Worker *w, JobFn fn, void *ud);
int worker_pending(Worker *w);

/* the shared API worker (UI-driven requests), the progress-sync worker and the cover worker */
extern Worker *g_api;
extern Worker *g_sync;
extern Worker *g_covers;

#endif
