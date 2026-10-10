/* Series and author groupings built from the library list (the minified
 * items carry "seriesName" like "Dune #1, Other #2" and "authorName" like
 * "A, B"). Pure C. */
#ifndef ABSVITA_GROUPS_H
#define ABSVITA_GROUPS_H
#include <stddef.h>

#include "models.h"

typedef enum { GROUP_SERIES, GROUP_AUTHOR } GroupKind;

typedef struct {
    char name[96];
    int count;
    int *books;      /* indices into the book array: series in sequence order, authors by title */
    double *seq;     /* series: sequence per book (-1 unknown); NULL for authors */
} Group;

/* "Discworld #3" -> "Discworld", 3.0; "Dune #1.5" -> 1.5; "Earthsea" -> -1. Returns 0. */
int series_split(const char *s, char *name, size_t n, double *seq);
/* Calls fn for each ", "-separated part of list (trimmed, non-empty). */
void list_each(const char *list, void (*fn)(const char *part, void *ud), void *ud);
/* Groups sorted by name (case-insensitive, leading "The " ignored). Returns the count, -1 on OOM. */
int groups_build(const BookSummary *books, int n, GroupKind kind, Group **out);
void groups_free(Group *g, int n);

#endif
