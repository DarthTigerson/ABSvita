/* Sorting and searching the in-memory library list. */
#ifndef ABSVITA_LIBRARY_H
#define ABSVITA_LIBRARY_H
#include "models.h"

typedef enum { SORT_TITLE, SORT_AUTHOR, SORT_RECENT, SORT_COUNT } SortMode;
const char *lib_sort_name(SortMode m);
/* Sorts an index array (idx[0..n)) into books. Title sort ignores a leading
 * "The "/"A "/"An ". Stable on ties via title. */
void lib_sort(const BookSummary *books, int *idx, int n, SortMode m);
/* Writes indices of books matching query (case-insensitive, title or author
 * or series) into out, in the order of idx. Returns the count. */
int lib_filter(const BookSummary *books, const int *idx, int n, const char *query, int *out);
/* Index of the book with this id, or -1. */
int lib_find(const BookSummary *books, int n, const char *id);

#endif
