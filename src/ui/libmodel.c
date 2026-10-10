#include "libmodel.h"

#include <stdlib.h>
#include <string.h>

#include "abs_client.h"
#include "osal.h"
#include "strutil.h"
#include "worker.h"

#define MAX_CONT 24
#define MAX_RECENT 24

typedef struct {
    int rc;
    BookSummary *books;
    int n;
    char ids[MAX_CONT][40];
    int nids;
} Result;

static struct {
    LibState state;
    bool loading;
    char error[96];
    BookSummary *books;
    int n;
    int *sorted[SORT_COUNT];
    int cont[MAX_CONT], ncont;
    int recent[MAX_RECENT], nrecent;
    Group *groups[2];
    int ngroups[2];
    char cont_ids[MAX_CONT][40];
    int ncont_ids;
} L;

static Mutex g_lock;
static Result *g_result;
static volatile int g_done, g_total;

static void on_progress(int done, int total) { g_done = done; g_total = total; }

static void load_job(void *ud) {
    (void)ud;
    Result *r = calloc(1, sizeof *r);
    if (!r) return;
    r->rc = absc_fetch_library(&r->books, &r->n, on_progress);
    if (r->rc == ABS_OK) {
        int k = absc_fetch_in_progress(r->ids, MAX_CONT);
        r->nids = k > 0 ? k : 0;
    }
    mutex_lock(&g_lock);
    if (g_result) { free(g_result->books); free(g_result); }
    g_result = r;
    mutex_unlock(&g_lock);
}

void lib_init(void) { mutex_init(&g_lock); }

static void free_views(void) {
    for (int i = 0; i < SORT_COUNT; i++) { free(L.sorted[i]); L.sorted[i] = NULL; }
    for (int k = 0; k < 2; k++) { groups_free(L.groups[k], L.ngroups[k]); L.groups[k] = NULL; L.ngroups[k] = 0; }
}

void lib_clear(void) {
    free_views();
    free(L.books);
    memset(&L, 0, sizeof L);
}

void lib_refresh(void) {
    if (L.loading) return;
    L.loading = true;
    g_done = g_total = 0;
    if (L.state != LIB_READY) L.state = LIB_LOADING;
    if (!worker_post(g_api, load_job, NULL)) L.loading = false;
}

static void build_continue(void) {
    L.ncont = 0;
    for (int i = 0; i < L.ncont_ids && L.ncont < MAX_CONT; i++) {
        int k = lib_find(L.books, L.n, L.cont_ids[i]);
        if (k >= 0 && !L.books[k].finished) L.cont[L.ncont++] = k;
    }
    /* anything else with progress, most recently updated first */
    for (int pass = 0; pass < L.n && L.ncont < MAX_CONT; pass++) {
        int best = -1;
        for (int i = 0; i < L.n; i++) {
            const BookSummary *b = &L.books[i];
            if (b->finished || b->current_time <= 0) continue;
            bool listed = false;
            for (int j = 0; j < L.ncont; j++) if (L.cont[j] == i) { listed = true; break; }
            if (listed) continue;
            if (best < 0 || b->progress_updated > L.books[best].progress_updated) best = i;
        }
        if (best < 0) break;
        L.cont[L.ncont++] = best;
    }
}

static void build_views(void) {
    free_views();
    int *idx = malloc(sizeof(int) * (L.n ? L.n : 1));
    if (idx) {
        for (int i = 0; i < L.n; i++) idx[i] = i;
        lib_sort(L.books, idx, L.n, SORT_RECENT);
        L.nrecent = L.n < MAX_RECENT ? L.n : MAX_RECENT;
        memcpy(L.recent, idx, sizeof(int) * L.nrecent);
        free(idx);
    }
    for (int k = 0; k < 2; k++) {
        int n = groups_build(L.books, L.n, (GroupKind)k, &L.groups[k]);
        L.ngroups[k] = n > 0 ? n : 0;
    }
    build_continue();
}

void lib_tick(void) {
    mutex_lock(&g_lock);
    Result *r = g_result;
    g_result = NULL;
    mutex_unlock(&g_lock);
    if (!r) return;
    L.loading = false;
    if (r->rc != ABS_OK) {
        str_copy(L.error, sizeof L.error, r->rc == ABS_ERR_AUTH ? "Please log in again" :
                 r->rc == ABS_ERR_NET ? "Can't reach the server" : "The server sent something unexpected");
        if (L.state != LIB_READY) L.state = LIB_ERROR;
        free(r->books);
        free(r);
        return;
    }
    free(L.books);
    L.books = r->books;
    L.n = r->n;
    memcpy(L.cont_ids, r->ids, sizeof r->ids);
    L.ncont_ids = r->nids;
    L.error[0] = 0;
    L.state = LIB_READY;
    build_views();
    free(r);
}

LibState lib_state(void) { return L.state; }
bool lib_loading(void) { return L.loading; }
const char *lib_error(void) { return L.error; }
void lib_progress(int *done, int *total) { *done = g_done; *total = g_total; }
int lib_count(void) { return L.n; }
const BookSummary *lib_book(int i) { return i >= 0 && i < L.n ? &L.books[i] : NULL; }
int lib_find_id(const char *id) { return lib_find(L.books, L.n, id); }

const int *lib_sorted(SortMode m, int *n) {
    *n = L.n;
    if (!L.sorted[m] && L.n) {
        L.sorted[m] = malloc(sizeof(int) * L.n);
        if (!L.sorted[m]) { *n = 0; return NULL; }
        for (int i = 0; i < L.n; i++) L.sorted[m][i] = i;
        lib_sort(L.books, L.sorted[m], L.n, m);
    }
    return L.sorted[m];
}

const int *lib_continue(int *n) { *n = L.ncont; return L.cont; }
const int *lib_recent(int *n) { *n = L.nrecent; return L.recent; }
const Group *lib_groups(GroupKind k, int *n) { *n = L.ngroups[k]; return L.groups[k]; }

int lib_search(const char *q, int *out, int max) {
    int n;
    const int *idx = lib_sorted(SORT_TITLE, &n);
    if (!idx || !q || !q[0]) return 0;
    int *tmp = malloc(sizeof(int) * n);
    if (!tmp) return 0;
    int k = lib_filter(L.books, idx, n, q, tmp);
    if (k > max) k = max;
    memcpy(out, tmp, sizeof(int) * k);
    free(tmp);
    return k;
}

void lib_note_progress(const char *id, double t, double duration) {
    int i = lib_find(L.books, L.n, id);
    if (i < 0) return;
    BookSummary *b = &L.books[i];
    b->current_time = t;
    if (duration > 0) b->progress = (float)(t / duration);
    b->finished = duration > 0 && t >= duration - 1;
    b->progress_updated = wall_ms();
    /* move it to the front of Continue */
    int j = 0;
    while (j < L.ncont_ids && strcmp(L.cont_ids[j], id)) j++;
    if (j == L.ncont_ids && L.ncont_ids < MAX_CONT) L.ncont_ids++;
    if (j >= MAX_CONT) j = MAX_CONT - 1;
    memmove(L.cont_ids[1], L.cont_ids[0], sizeof L.cont_ids[0] * j);
    str_copy(L.cont_ids[0], sizeof L.cont_ids[0], id);
    build_continue();
}
