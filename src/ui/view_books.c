/* Book grids: the Library, a series' or author's books, and Search. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ime.h"
#include "libmodel.h"
#include "player.h"
#include "strutil.h"
#include "views.h"

#define COLS 5
#define CELL_W ((float)CW / COLS)
#define COVER 122.0f
#define CELL_H 194.0f
#define HEAD_H 76.0f

typedef enum { G_LIBRARY, G_FIXED, G_SEARCH } GridKind;

typedef struct {
    GridKind kind;
    char title[96], subtitle[96];
    int *idx;          /* G_FIXED / G_SEARCH */
    double *seq;       /* G_FIXED series numbers (or NULL) */
    int n;
    int focus;         /* -1 = search field */
    Scroll sc;
    char query[128];
    bool want_keyboard;
} Grid;

static SortMode g_sort = SORT_TITLE;
static Grid *g_tapped; /* the grid that registered this frame's hit rects */

static const int *items(Grid *g, int *n) {
    if (g->kind == G_LIBRARY) return lib_sorted(g_sort, n);
    *n = g->n;
    return g->idx;
}

static float grid_top(Grid *g) { return HEAD_H + (g->kind == G_SEARCH ? 64 : 0); }

static void open_focused(Grid *g, bool play) {
    int n;
    const int *idx = items(g, &n);
    if (!idx || g->focus < 0 || g->focus >= n) return;
    if (play) app_play(lib_book(idx[g->focus]), -1);
    else app_open_book(idx[g->focus]);
}

static void on_tap_cell(int i, int b) {
    (void)b;
    if (!g_tapped) return;
    g_tapped->focus = i;
    open_focused(g_tapped, false);
}

static void run_search(Grid *g) {
    free(g->idx);
    g->idx = malloc(sizeof(int) * (lib_count() ? lib_count() : 1));
    g->n = g->idx ? lib_search(g->query, g->idx, lib_count()) : 0;
}

static void on_tap_field(int a, int b) {
    (void)a; (void)b;
    if (g_tapped) { g_tapped->focus = -1; ime_open("Search", g_tapped->query, 100, IME_TEXT); }
}

static void grid_update(View *v, const Input *in, float dt) {
    Grid *g = v->data;
    scroll_step(&g->sc, dt);
    if (g->kind == G_SEARCH) {
        if (g->want_keyboard) { g->want_keyboard = false; ime_open("Search", g->query, 100, IME_TEXT); }
        if (ime_active()) {
            char out[128];
            int r = ime_poll(out, sizeof out);
            if (r == 1) {
                str_copy(g->query, sizeof g->query, out);
                run_search(g);
                g->focus = g->n ? 0 : -1;
                g->sc.target = 0;
            }
            return;
        }
    }
    int n;
    const int *idx = items(g, &n);
    (void)idx;
    if (!n && g->kind == G_LIBRARY && lib_state() == LIB_ERROR && in->down & B_OK) lib_refresh();
    int f = g->focus;
    if (f < 0) { /* search field */
        if (in->down & B_OK) ime_open("Search", g->query, 100, IME_TEXT);
        if (in->repeat & B_DOWN && n) g->focus = 0;
        if (in->repeat & B_LEFT || in->down & B_BACK) app_focus_sidebar();
        return;
    }
    if (in->repeat & B_RIGHT && f + 1 < n && (f + 1) % COLS) f++;
    if (in->repeat & B_LEFT) { if (f % COLS == 0) { app_focus_sidebar(); return; } f--; }
    if (in->repeat & B_DOWN) f = f + COLS < n ? f + COLS : (f / COLS < (n - 1) / COLS ? n - 1 : f);
    if (in->repeat & B_UP) {
        if (f >= COLS) f -= COLS;
        else if (g->kind == G_SEARCH) f = -1;
    }
    if (n && f >= n) f = n - 1;
    g->focus = f;
    if (in->down & B_OK) open_focused(g, false);
    if (in->down & B_SQUARE) open_focused(g, true);
    if (in->down & B_BACK) app_pop();
    if (in->down & B_SELECT && g->kind == G_LIBRARY) {
        g_sort = (SortMode)((g_sort + 1) % SORT_COUNT);
        g->focus = 0;
        g->sc.target = 0;
    }
    if (in->dragging) {
        int rows = (n + COLS - 1) / COLS;
        float max = rows * CELL_H - (CONTENT_BOTTOM - grid_top(g)) + 16;
        scroll_drag(&g->sc, in->drag_dy, max > 0 ? max : 0);
    } else if (g->focus >= 0) {
        int row = g->focus / COLS, rows = (n + COLS - 1) / COLS;
        float max = rows * CELL_H - (CONTENT_BOTTOM - grid_top(g)) + 16;
        scroll_show(&g->sc, row * CELL_H - (row ? 24 : 0), (row + 1) * CELL_H + 8, CONTENT_BOTTOM - grid_top(g), max > 0 ? max : 0);
    }
}

static void draw_state(Grid *g) {
    float cy = (grid_top(g) + CONTENT_BOTTOM) / 2, cx = CX + CW / 2;
    LibState st = lib_state();
    if (g->kind == G_LIBRARY && (st == LIB_LOADING || st == LIB_EMPTY)) {
        draw_spinner(cx, cy - 30, 18, C_ACCENT);
        int done, total;
        lib_progress(&done, &total);
        char t[64];
        if (total) snprintf(t, sizeof t, "Loading your library\xE2\x80\xA6 %d / %d", done, total);
        else snprintf(t, sizeof t, "Loading your library\xE2\x80\xA6");
        gfx_text_center(F_REG, 17, cx, cy + 20, C_SUB, t);
    } else if (g->kind == G_LIBRARY && st == LIB_ERROR) {
        gfx_icon(IC_CLOUD_OFF, cx, cy - 34, 40, C_ERROR);
        gfx_text_center(F_BOLD, 19, cx, cy + 14, C_TEXT, lib_error());
        gfx_text_center(F_REG, 16, cx, cy + 44, C_SUB, "Check your Wi-Fi, then try again");
    } else if (g->kind == G_SEARCH) {
        gfx_text_center(F_REG, 17, cx, cy, C_SUB, g->query[0] ? "No books match" : "Search titles, authors and series");
    } else {
        gfx_text_center(F_REG, 17, cx, cy, C_SUB, "Nothing here yet");
    }
}

static void grid_draw(View *v, bool focused) {
    Grid *g = v->data;
    g_tapped = g;
    int n;
    const int *idx = items(g, &n);
    gfx_text(F_BOLD, 30, CX, 26, C_TEXT, g->title);
    char sub[128];
    if (g->kind == G_LIBRARY) snprintf(sub, sizeof sub, "%d books \xC2\xB7 by %s", n, lib_sort_name(g_sort));
    else if (g->kind == G_SEARCH) snprintf(sub, sizeof sub, g->query[0] ? "%d results" : "", n);
    else str_copy(sub, sizeof sub, g->subtitle);
    gfx_text_right(F_REG, 16, CX + CW, 36, C_SUB, sub);

    if (g->kind == G_SEARCH) {
        float y = HEAD_H - 4, h = 48;
        bool foc = focused && g->focus < 0;
        gfx_rrect(CX, y, CW, h, h / 2, C_PANEL2);
        if (foc) draw_focus_ring(CX, y, CW, h, h / 2);
        gfx_icon(IC_SEARCH, CX + 28, y + h / 2, 20, C_SUB);
        char q[160];
        gfx_fit(F_REG, 18, g->query[0] ? g->query : "Title, author or series", CW - 80, q, sizeof q);
        gfx_text_vc(F_REG, 18, CX + 50, y + h / 2, g->query[0] ? C_TEXT : C_DIM, q);
        hit_add(CX, y, CW, h, on_tap_field, 0, 0);
    }

    float top = grid_top(g);
    if (!n || !idx) {
        draw_state(g);
    } else {
        clip_content(top - 6);
        int first_row = (int)(g->sc.y / CELL_H), last_row = (int)((g->sc.y + CONTENT_BOTTOM - top) / CELL_H) + 1;
        for (int r = first_row; r <= last_row; r++)
            for (int c = 0; c < COLS; c++) {
                int i = r * COLS + c;
                if (i >= n) break;
                float x = CX + c * CELL_W, y = top + r * CELL_H - g->sc.y;
                const BookSummary *b = lib_book(idx[i]);
                if (!b) continue;
                draw_book_cell(b, x, y, COVER, focused && g->focus == i);
                if (g->seq && g->seq[i] >= 0) {
                    char s[16];
                    snprintf(s, sizeof s, "%g", g->seq[i]);
                    float w = gfx_ink_w(F_BOLD, 14, s) + 16;
                    gfx_rrect(x + 6, y + 6, w < 26 ? 26 : w, 26, 13, gfx_alpha(0xFF000000, 0.7f));
                    gfx_text_center(F_BOLD, 14, x + 6 + (w < 26 ? 26 : w) / 2, y + 19, C_TEXT, s);
                }
                if (y + CELL_H > top && y < CONTENT_BOTTOM) hit_add(x, y, COVER, COVER + 50, on_tap_cell, i, 0);
            }
        clip_off();
    }
    if (focused) {
        hint(G_DPAD, "Move");
        if (g->focus < 0) hint(g_ok(), "Type");
        else if (n) {
            hint(g_ok(), "Details");
            hint(G_SQUARE, "Play");
        }
        if (g->kind == G_LIBRARY && !n && lib_state() == LIB_ERROR) hint(g_ok(), "Try again");
        if (g->kind == G_LIBRARY) hint(G_SELECT, "Sort");
        hint(g_back(), "Back");
    }
}

static void grid_destroy(View *v) {
    Grid *g = v->data;
    if (g_tapped == g) g_tapped = NULL;
    free(g->idx);
    free(g->seq);
    free(g);
    free(v);
}

static View *grid_view(Grid *g) {
    View *v = calloc(1, sizeof *v);
    if (!v) { free(g); return NULL; }
    v->update = grid_update;
    v->draw = grid_draw;
    v->destroy = grid_destroy;
    v->data = g;
    return v;
}

View *view_library_new(void) {
    Grid *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->kind = G_LIBRARY;
    str_copy(g->title, sizeof g->title, "Library");
    return grid_view(g);
}

View *view_books_new(const char *title, const char *subtitle, const int *idx, int n, bool numbered, const double *seq) {
    Grid *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->kind = G_FIXED;
    str_copy(g->title, sizeof g->title, title);
    str_copy(g->subtitle, sizeof g->subtitle, subtitle);
    g->idx = malloc(sizeof(int) * (n ? n : 1));
    if (g->idx) { memcpy(g->idx, idx, sizeof(int) * n); g->n = n; }
    if (numbered && seq) {
        g->seq = malloc(sizeof(double) * (n ? n : 1));
        if (g->seq) memcpy(g->seq, seq, sizeof(double) * n);
    }
    return grid_view(g);
}

View *view_search_new(bool open_keyboard) {
    Grid *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->kind = G_SEARCH;
    g->focus = -1;
    g->want_keyboard = open_keyboard;
    str_copy(g->title, sizeof g->title, "Search");
    return grid_view(g);
}
