#include "osal.h"

#include <errno.h>
#include <sys/time.h>
#include <time.h>

void cond_wait_ms(Cond *c, Mutex *m, int ms) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct timespec ts;
    long long ns = (long long)tv.tv_usec * 1000 + (long long)ms * 1000000LL;
    ts.tv_sec = tv.tv_sec + (time_t)(ns / 1000000000LL);
    ts.tv_nsec = (long)(ns % 1000000000LL);
    pthread_cond_timedwait(c, m, &ts);
}

uint64_t now_ms(void) {
#ifdef CLOCK_MONOTONIC
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
    return wall_ms();
}

uint64_t wall_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void sleep_ms(int ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <stdlib.h>
typedef struct { void *(*fn)(void *); void *arg; int prio; } Trampoline;
static void *trampoline(void *p) {
    Trampoline t = *(Trampoline *)p;
    free(p);
    /* relative to the default user priority (0x10000100); lower runs first */
    sceKernelChangeThreadPriority(0, 0x10000100 + (t.prio > 0 ? -20 : 20));
    return t.fn(t.arg);
}
#endif

bool thread_start(Thread *t, void *(*fn)(void *), void *arg, int stack_kb, int prio) {
    pthread_attr_t a;
    pthread_attr_init(&a);
    if (stack_kb > 0) pthread_attr_setstacksize(&a, (size_t)stack_kb * 1024);
#ifdef __vita__
    if (prio) {
        Trampoline *tr = malloc(sizeof *tr);
        if (tr) {
            tr->fn = fn;
            tr->arg = arg;
            tr->prio = prio;
            fn = trampoline;
            arg = tr;
        }
    }
#else
    (void)prio;
#endif
    t->ok = pthread_create(&t->t, &a, fn, arg) == 0;
    pthread_attr_destroy(&a);
    return t->ok;
}

void thread_join(Thread *t) {
    if (t->ok) pthread_join(t->t, NULL);
    t->ok = false;
}
