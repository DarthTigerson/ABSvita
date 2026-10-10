#include "library.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "strutil.h"

const char *lib_sort_name(SortMode m) {
    switch (m) {
    case SORT_TITLE: return "Title";
    case SORT_AUTHOR: return "Author";
    case SORT_RECENT: return "Recently added";
    default: return "";
    }
}

static const char *skip_article(const char *s) {
    if (!strncasecmp(s, "the ", 4)) return s + 4;
    if (!strncasecmp(s, "an ", 3)) return s + 3;
    if (!strncasecmp(s, "a ", 2)) return s + 2;
    return s;
}

static const BookSummary *g_books;
static SortMode g_mode;

static int cmp_title(const BookSummary *a, const BookSummary *b) {
    return strcasecmp(skip_article(a->title), skip_article(b->title));
}

static int cmp(const void *pa, const void *pb) {
    int ia = *(const int *)pa, ib = *(const int *)pb;
    const BookSummary *a = &g_books[ia], *b = &g_books[ib];
    int c = 0;
    if (g_mode == SORT_AUTHOR) {
        /* empty authors go last */
        if (!a->author[0] != !b->author[0]) return a->author[0] ? -1 : 1;
        c = strcasecmp(a->author, b->author);
        if (!c) c = strcasecmp(a->series, b->series);
    } else if (g_mode == SORT_RECENT) {
        c = a->added_at < b->added_at ? 1 : a->added_at > b->added_at ? -1 : 0;
    }
    if (!c) c = cmp_title(a, b);
    if (!c) c = ia - ib;
    return c;
}

void lib_sort(const BookSummary *books, int *idx, int n, SortMode m) {
    g_books = books;
    g_mode = m;
    qsort(idx, n, sizeof *idx, cmp);
}

int lib_filter(const BookSummary *books, const int *idx, int n, const char *query, int *out) {
    int k = 0;
    for (int i = 0; i < n; i++) {
        const BookSummary *b = &books[idx[i]];
        if (!query || !*query || str_icontains(b->title, query) || str_icontains(b->author, query) || str_icontains(b->series, query))
            out[k++] = idx[i];
    }
    return k;
}

int lib_find(const BookSummary *books, int n, const char *id) {
    for (int i = 0; i < n; i++)
        if (strcmp(books[i].id, id) == 0) return i;
    return -1;
}
