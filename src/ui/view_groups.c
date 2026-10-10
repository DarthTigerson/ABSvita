/* Series and Authors: a list of names with book counts; opening one shows its books. */
#include <stdio.h>
#include <stdlib.h>

#include "covers.h"
#include "libmodel.h"
#include "strutil.h"
#include "views.h"

#define ROW_H 76.0f
#define TOP 76.0f

typedef struct {
    GroupKind kind;
    int focus;
    Scroll sc;
} Groups;

static Groups *g_groups;

static void open_group(Groups *gr) {
    int n;
    const Group *g = lib_groups(gr->kind, &n);
    if (gr->focus < 0 || gr->focus >= n) return;
    const Group *x = &g[gr->focus];
    char sub[48];
    snprintf(sub, sizeof sub, "%d book%s", x->count, x->count == 1 ? "" : "s");
    app_push(view_books_new(x->name, sub, x->books, x->count, gr->kind == GROUP_SERIES, x->seq));
}

static void on_tap(int i, int b) {
    (void)b;
    if (!g_groups) return;
    g_groups->focus = i;
    open_group(g_groups);
}

static void groups_update(View *v, const Input *in, float dt) {
    Groups *gr = v->data;
    int n;
    lib_groups(gr->kind, &n);
    scroll_step(&gr->sc, dt);
    if (in->repeat & B_UP && gr->focus > 0) gr->focus--;
    if (in->repeat & B_DOWN && gr->focus + 1 < n) gr->focus++;
    if (in->repeat & B_LEFT || in->down & B_BACK) { app_focus_sidebar(); return; }
    if (in->down & B_OK) open_group(gr);
    float max = n * ROW_H - (CONTENT_BOTTOM - TOP) + 12;
    if (max < 0) max = 0;
    if (in->dragging) scroll_drag(&gr->sc, in->drag_dy, max);
    else scroll_show(&gr->sc, gr->focus * ROW_H - (gr->focus ? 30 : 0), (gr->focus + 1) * ROW_H + 30, CONTENT_BOTTOM - TOP, max);
}

static void groups_draw(View *v, bool focused) {
    Groups *gr = v->data;
    g_groups = gr;
    int n;
    const Group *g = lib_groups(gr->kind, &n);
    const char *title = gr->kind == GROUP_SERIES ? "Series" : "Authors";
    gfx_text(F_BOLD, 30, CX, 26, C_TEXT, title);
    char sub[48];
    snprintf(sub, sizeof sub, "%d %s", n, gr->kind == GROUP_SERIES ? "series" : (n == 1 ? "author" : "authors"));
    gfx_text_right(F_REG, 16, CX + CW, 36, C_SUB, sub);
    if (!n) {
        const char *msg = lib_state() == LIB_READY ? (gr->kind == GROUP_SERIES ? "No series in this library" : "No authors yet")
                                                   : "Loading your library\xE2\x80\xA6";
        gfx_text_center(F_REG, 17, CX + CW / 2, (TOP + CONTENT_BOTTOM) / 2, C_SUB, msg);
        return;
    }
    clip_content(TOP - 6);
    int first = (int)(gr->sc.y / ROW_H), last = first + (int)((CONTENT_BOTTOM - TOP) / ROW_H) + 2;
    for (int i = first; i <= last && i < n; i++) {
        float y = TOP + i * ROW_H - gr->sc.y, h = ROW_H - 8;
        bool foc = focused && gr->focus == i;
        gfx_rrect(CX, y, CW, h, 12, foc ? C_PANEL2 : C_PANEL);
        if (foc) draw_focus_ring(CX, y, CW, h, 12);
        /* up to three covers, fanned */
        int k = g[i].count < 3 ? g[i].count : 3;
        for (int j = k - 1; j >= 0; j--) {
            const BookSummary *b = lib_book(g[i].books[j]);
            if (b) covers_draw(b->id, CX + 10 + j * 18, y + 7, h - 14, 1);
        }
        float tx = CX + 10 + (h - 14) + 2 * 18 + 18;
        char line[160];
        gfx_fit(F_BOLD, 19, g[i].name, CW - (tx - CX) - 60, line, sizeof line);
        gfx_text(F_BOLD, 19, tx, y + 16, C_TEXT, line);
        snprintf(line, sizeof line, "%d book%s", g[i].count, g[i].count == 1 ? "" : "s");
        gfx_text(F_REG, 15, tx, y + 42, C_SUB, line);
        gfx_icon(IC_CHEVRON, CX + CW - 28, y + h / 2, 18, foc ? C_TEXT : C_DIM);
        hit_add(CX, y, CW, h, on_tap, i, 0);
    }
    clip_off();
    if (focused) {
        hint(G_DPAD, "Move");
        hint(g_ok(), "Open");
        hint(g_back(), "Back");
    }
}

static void groups_destroy(View *v) {
    if (g_groups == v->data) g_groups = NULL;
    free(v->data);
    free(v);
}

View *view_groups_new(GroupKind kind) {
    View *v = calloc(1, sizeof *v);
    Groups *g = calloc(1, sizeof *g);
    if (!v || !g) { free(v); free(g); return NULL; }
    g->kind = kind;
    v->update = groups_update;
    v->draw = groups_draw;
    v->destroy = groups_destroy;
    v->data = g;
    return v;
}
