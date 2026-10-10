/* Playback domain logic: opening books, resume position, chapters and skips,
 * speed, sleep timer, progress sync and the offline progress queue.
 * Called from the UI thread. */
#ifndef ABSVITA_PLAYER_H
#define ABSVITA_PLAYER_H
#include <stdbool.h>

#include "config.h"
#include "models.h"
#include "policy.h"
#include "progress_queue.h"

typedef enum { PL_IDLE, PL_OPENING, PL_READY, PL_FAILED } PlayerPhase;

/* save_queue is called with the player's queue lock held. model: device name for ABS. */
void player_init(Config *cfg, ProgressQueue *pq, void (*save_queue)(void), const char *model);
void player_exit(void);  /* syncs and closes the session (blocking, short) */

/* Starts opening a book in the background. start < 0 resumes where the user left off. */
void player_open(const BookSummary *b, double start);
PlayerPhase player_phase(void);
const char *player_error(void);
bool player_has_book(void);
const BookDetail *player_book(void);
const char *player_book_id(void);
bool player_is_local(void);   /* playing a downloaded copy */

void player_toggle(void);
void player_pause(void);
bool player_playing(void);
void player_skip(double seconds);
void player_chapter(int dir);
void player_jump(double t);
void player_cycle_speed(void);
void player_set_speed(float s);
float player_speed(void);
void player_cycle_sleep(void);
const SleepTimer *player_sleep(void);
void player_sleep_label(char *out, int n);

double player_position(void);
double player_duration(void);
int player_chapter_index(void);

/* per frame: sync policy, sleep timer, offline queue */
void player_tick(double dt);
/* push pending offline progress to the server (background) */
void player_flush_queue(void);
/* status line for the UI ("Reconnecting…", "Buffering…", ...) or NULL */
const char *player_status(void);

#endif
