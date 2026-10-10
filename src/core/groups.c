#include "groups.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "strutil.h"

int series_split(const char *s, char *name, size_t n, double *seq) {
    *seq = -1;
    const char *hash = strrchr(s, '#');
    size_t len = strlen(s);
    if (hash && hash > s && hash[-1] == ' ') {
        char *end;
        double v = strtod(hash + 1, &end);
        if (end != hash + 1) {
            *seq = v;
            len = (size_t)(hash - 1 - s);
        }
    }
    while (len && isspace((unsigned char)s[len - 1])) len--;
    if (len >= n) len = n - 1;
    memcpy(name, s, len);
    name[len] = 0;
    return 0;
}

void list_each(const char *list, void (*fn)(const char *part, void *ud), void *ud) {
    if (!list) return;
    char part[160];
    const char *p = list;
    while (*p) {
        const char *sep = strstr(p, ", ");
        size_t len = sep ? (size_t)(sep - p) : strlen(p);
        while (len && isspace((unsigned char)*p)) { p++; len--; }
        size_t l = len < sizeof part - 1 ? len : sizeof part - 1;
        memcpy(part, p, l);
        part[l] = 0;
        while (l && isspace((unsigned char)part[l - 1])) part[--l] = 0;
        if (part[0]) fn(part, ud);
        if (!sep) break;
        p = sep + 2;
    }
}

static const char *skip_article(const char *s) {
    if (!strncasecmp(s, "The ", 4)) return s + 4;
    return s;
}

typedef struct {
    Group *g;
    int n, cap;
    GroupKind kind;
    int book;
    const BookSummary *books;
    int oom;
} Build;

static Group *find_or_add(Build *b, const char *name) {
    for (int i = 0; i < b->n; i++)
        if (!strcasecmp(b->g[i].name, name)) return &b->g[i];
    if (b->n == b->cap) {
        int nc = b->cap ? b->cap * 2 : 32;
        Group *ng = realloc(b->g, sizeof *ng * nc);
        if (!ng) { b->oom = 1; return NULL; }
        b->g = ng;
        b->cap = nc;
    }
    Group *g = &b->g[b->n++];
    memset(g, 0, sizeof *g);
    str_copy(g->name, sizeof g->name, name);
    return g;
}

static void add_book(Group *g, int book, double seq, int with_seq, int *oom) {
    int *nb = realloc(g->books, sizeof(int) * (g->count + 1));
    if (!nb) { *oom = 1; return; }
    g->books = nb;
    if (with_seq) {
        double *ns = realloc(g->seq, sizeof(double) * (g->count + 1));
        if (!ns) { *oom = 1; return; }
        g->seq = ns;
        g->seq[g->count] = seq;
    }
    g->books[g->count++] = book;
}

static void on_part(const char *part, void *ud) {
    Build *b = ud;
    char name[96];
    double seq = -1;
    if (b->kind == GROUP_SERIES) series_split(part, name, sizeof name, &seq);
    else str_copy(name, sizeof name, part);
    if (!name[0]) return;
    Group *g = find_or_add(b, name);
    if (!g) return;
    for (int i = 0; i < g->count; i++)
        if (g->books[i] == b->book) return; /* listed twice */
    add_book(g, b->book, seq, b->kind == GROUP_SERIES, &b->oom);
}

static int cmp_name(const void *a, const void *b) {
    return strcasecmp(skip_article(((const Group *)a)->name), skip_article(((const Group *)b)->name));
}

/* sorts the parallel books/seq arrays of one group */
static void sort_group(Group *g, const BookSummary *books, GroupKind kind) {
    for (int i = 1; i < g->count; i++) {
        int bk = g->books[i];
        double sq = g->seq ? g->seq[i] : 0;
        int j = i - 1;
        while (j >= 0) {
            int less;
            if (kind == GROUP_SERIES && g->seq) {
                double a = sq < 0 ? 1e9 : sq, c = g->seq[j] < 0 ? 1e9 : g->seq[j];
                less = a < c || (a == c && strcasecmp(skip_article(books[bk].title), skip_article(books[g->books[j]].title)) < 0);
            } else {
                less = strcasecmp(skip_article(books[bk].title), skip_article(books[g->books[j]].title)) < 0;
            }
            if (!less) break;
            g->books[j + 1] = g->books[j];
            if (g->seq) g->seq[j + 1] = g->seq[j];
            j--;
        }
        g->books[j + 1] = bk;
        if (g->seq) g->seq[j + 1] = sq;
    }
}

int groups_build(const BookSummary *books, int n, GroupKind kind, Group **out) {
    Build b = {0};
    b.kind = kind;
    b.books = books;
    for (int i = 0; i < n && !b.oom; i++) {
        b.book = i;
        list_each(kind == GROUP_SERIES ? books[i].series : books[i].author, on_part, &b);
    }
    if (b.oom) { groups_free(b.g, b.n); *out = NULL; return -1; }
    qsort(b.g, b.n, sizeof *b.g, cmp_name);
    for (int i = 0; i < b.n; i++) sort_group(&b.g[i], books, kind);
    *out = b.g;
    return b.n;
}

void groups_free(Group *g, int n) {
    if (!g) return;
    for (int i = 0; i < n; i++) {
        free(g[i].books);
        free(g[i].seq);
    }
    free(g);
}
