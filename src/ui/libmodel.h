/* The library as the UI sees it: all books (loaded in the background), sorted
 * views, series and author groups, continue-listening and recently-added lists. */
#ifndef ABSVITA_LIBMODEL_H
#define ABSVITA_LIBMODEL_H
#include <stdbool.h>

#include "groups.h"
#include "library.h"
#include "models.h"

typedef enum { LIB_EMPTY, LIB_LOADING, LIB_READY, LIB_ERROR } LibState;

void lib_init(void);
void lib_clear(void);                 /* on logout */
void lib_refresh(void);               /* reload in the background */
void lib_tick(void);                  /* adopt finished loads (UI thread) */
LibState lib_state(void);
bool lib_loading(void);               /* a refresh is running (data may already be shown) */
const char *lib_error(void);
void lib_progress(int *done, int *total);

int lib_count(void);
const BookSummary *lib_book(int i);
int lib_find_id(const char *id);
/* Indices of all books in this order. */
const int *lib_sorted(SortMode m, int *n);
const int *lib_continue(int *n);      /* in progress, most recent first */
const int *lib_recent(int *n);        /* recently added */
const Group *lib_groups(GroupKind k, int *n);
/* Matches for a query (title, author or series), in title order. */
int lib_search(const char *q, int *out, int max);
/* Local progress update after listening (keeps Continue current between refreshes). */
void lib_note_progress(const char *id, double t, double duration);

#endif
