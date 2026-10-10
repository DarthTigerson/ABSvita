/* Tiny portable threading/time helpers over pthreads (VitaSDK and POSIX hosts). */
#ifndef ABSVITA_OSAL_H
#define ABSVITA_OSAL_H
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

typedef pthread_mutex_t Mutex;
typedef pthread_cond_t Cond;

static inline void mutex_init(Mutex *m) { pthread_mutex_init(m, NULL); }
static inline void mutex_lock(Mutex *m) { pthread_mutex_lock(m); }
static inline void mutex_unlock(Mutex *m) { pthread_mutex_unlock(m); }
static inline void mutex_destroy(Mutex *m) { pthread_mutex_destroy(m); }
static inline void cond_init(Cond *c) { pthread_cond_init(c, NULL); }
static inline void cond_broadcast(Cond *c) { pthread_cond_broadcast(c); }
static inline void cond_destroy(Cond *c) { pthread_cond_destroy(c); }
/* Waits at most ms (spurious wakeups possible: callers re-check). */
void cond_wait_ms(Cond *c, Mutex *m, int ms);

/* Monotonic milliseconds. */
uint64_t now_ms(void);
/* Wall clock, ms since the Unix epoch. */
uint64_t wall_ms(void);
void sleep_ms(int ms);

typedef struct { pthread_t t; bool ok; } Thread;
/* prio: 0 = default; >0 = higher than the caller (audio), <0 = lower (background). */
bool thread_start(Thread *t, void *(*fn)(void *), void *arg, int stack_kb, int prio);
void thread_join(Thread *t);

#endif
