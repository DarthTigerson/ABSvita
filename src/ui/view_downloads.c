/* Downloads: every saved or saving book, its status and size, and the space left.
 * Reads the download manager (not the server library), so it works offline. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "covers.h"
#include "downloads.h"
#include "libmodel.h"
#include "player.h"
#include "progress_queue.h"
#include "strutil.h"
#include "views.h"

#define ROW_H 84.0f
#define TOP 76.0f
#define MAX 128

typedef struct {
    int focus;
    Scroll sc;
    DlInfo list[MAX];
    int n;
} Downloads;

static Downloads *g_dls;

/* What the rest of the UI needs to show or play a downloaded book. */
static void summary_of(const DlInfo *d, BookSummary *out) {
    int i = lib_find_id(d->id);
    if (i >= 0) { *out = *lib_book(i); return; }
    memset(out, 0, sizeof *out);
    str_copy(out->id, sizeof out->id, d->id);
    str_copy(out->title, sizeof out->title, d->title);
    str_copy(out->author, sizeof out->author, d->author);
    out->duration = d->duration;
}

static void refresh(Downloads *d) {
    d->n = dl_list(d->list, MAX);
    if (d->focus >= d->n) d->focus = d->n ? d->n - 1 : 0;
}

static void open_focused(Downloads *d, bool play) {
    if (d->focus >= d->n) return;
    BookSummary b;
    summary_of(&d->list[d->focus], &b);
    if (play) {
        if (d->list[d->focus].state == DL_DONE) app_play(&b, -1);
        else app_toast("This book hasn't finished downloading");
    } else {
        app_open_book_summary(&b);
    }
}

static void on_tap(int i, int b) {
    (void)b;
    if (!g_dls) return;
    g_dls->focus = i;
    open_focused(g_dls, false);
}

static void dls_update(View *v, const Input *in, float dt) {
    Downloads *d = v->data;
    refresh(d);
    scroll_step(&d->sc, dt);
    if (in->repeat & B_UP && d->focus > 0) d->focus--;
    if (in->repeat & B_DOWN && d->focus + 1 < d->n) d->focus++;
    if (in->repeat & B_LEFT || in->down & B_BACK) { app_focus_sidebar(); return; }
    if (in->down & B_OK) open_focused(d, false);
    if (in->down & B_SQUARE) open_focused(d, true);
    float max = d->n * ROW_H - (CONTENT_BOTTOM - TOP) + 12;
    if (max < 0) max = 0;
    if (in->dragging) scroll_drag(&d->sc, in->drag_dy, max);
    else scroll_show(&d->sc, d->focus * ROW_H - (d->focus ? 30 : 0), (d->focus + 1) * ROW_H + 30, CONTENT_BOTTOM - TOP, max);
}

static void fmt_bytes(uint64_t b, char *out, size_t n) {
    if (b >= (1ull << 30)) snprintf(out, n, "%.1f GB", b / 1073741824.0);
    else snprintf(out, n, "%.0f MB", b / 1048576.0);
}

static void dls_draw(View *v, bool focused) {
    Downloads *d = v->data;
    g_dls = d;
    refresh(d);
    gfx_text(F_BOLD, 30, CX, 26, C_TEXT, "Downloads");
    char used[24], freeb[24], sub[96];
    fmt_bytes(dl_used_bytes(), used, sizeof used);
    int64_t fr = dl_free_bytes();
    if (fr >= 0) {
        fmt_bytes((uint64_t)fr, freeb, sizeof freeb);
        snprintf(sub, sizeof sub, "%d book%s \xC2\xB7 %s used \xC2\xB7 %s free", d->n, d->n == 1 ? "" : "s", used, freeb);
    } else {
        snprintf(sub, sizeof sub, "%d book%s \xC2\xB7 %s used", d->n, d->n == 1 ? "" : "s", used);
    }
    gfx_text_right(F_REG, 16, CX + CW, 36, C_SUB, sub);
    if (!d->n) {
        float cx = CX + CW / 2, cy = (TOP + CONTENT_BOTTOM) / 2;
        gfx_icon(IC_DOWNLOAD, cx, cy - 46, 40, C_DIM);
        gfx_text_center(F_BOLD, 19, cx, cy + 4, C_TEXT, "No downloads yet");
        gfx_text_center(F_REG, 16, cx, cy + 34, C_SUB, "Open a book and choose Download to listen without Wi-Fi");
        if (focused) hint(g_back(), "Back");
        return;
    }
    clip_content(TOP - 6);
    for (int i = 0; i < d->n; i++) {
        float y = TOP + i * ROW_H - d->sc.y, h = ROW_H - 8;
        if (y + h < TOP - 6 || y > CONTENT_BOTTOM) continue;
        const DlInfo *x = &d->list[i];
        bool foc = focused && d->focus == i;
        gfx_rrect(CX, y, CW, h, 12, foc ? C_PANEL2 : C_PANEL);
        if (foc) draw_focus_ring(CX, y, CW, h, 12);
        float cs = h - 16;
        covers_draw(x->id, CX + 8, y + 8, cs, 1);
        float tx = CX + 8 + cs + 16, tw = CW - (tx - CX) - 20;
        char line[256], fit[256], a[24], t[24];
        gfx_fit(F_BOLD, 18, x->title[0] ? x->title : x->id, tw, fit, sizeof fit);
        gfx_text(F_BOLD, 18, tx, y + 14, C_TEXT, fit);
        fmt_bytes(x->done_bytes, a, sizeof a);
        fmt_bytes(x->total_bytes, t, sizeof t);
        uint32_t c = C_SUB;
        float frac = x->total_bytes ? (float)((double)x->done_bytes / x->total_bytes) : 0;
        switch (x->state) {
        case DL_DONE: snprintf(line, sizeof line, "Downloaded \xC2\xB7 %s", t); c = C_ACCENT; break;
        case DL_ACTIVE: snprintf(line, sizeof line, "Downloading %d%% \xC2\xB7 %s of %s", (int)(frac * 100), a, t); break;
        case DL_QUEUED: snprintf(line, sizeof line, "Waiting to download"); break;
        case DL_PAUSED: snprintf(line, sizeof line, "Paused \xC2\xB7 %s of %s", a, t); break;
        case DL_FAILED: snprintf(line, sizeof line, "Failed: %s", x->error); c = C_ERROR; break;
        default: line[0] = 0;
        }
        if (x->state == DL_DONE && x->author[0]) {
            char both[300];
            snprintf(both, sizeof both, "%s \xC2\xB7 %s", x->author, line);
            gfx_fit(F_REG, 15, both, tw, fit, sizeof fit);
        } else {
            gfx_fit(F_REG, 15, line, tw, fit, sizeof fit);
        }
        gfx_text(F_REG, 15, tx, y + 40, c, fit);
        if (x->state == DL_ACTIVE || x->state == DL_PAUSED)
            draw_progress_bar(tx, y + h - 16, tw, 5, frac, x->state == DL_ACTIVE ? C_ACCENT : C_DIM, C_LINE);
        if (player_book() && !strcmp(player_book_id(), x->id))
            gfx_icon(player_playing() ? IC_HEADPHONES : IC_PAUSE, CX + CW - 28, y + 24, 18, C_ACCENT);
        hit_add(CX, y, CW, h, on_tap, i, 0);
    }
    clip_off();
    if (focused) {
        hint(G_DPAD, "Move");
        hint(g_ok(), "Details");
        if (d->focus < d->n && d->list[d->focus].state == DL_DONE) hint(G_SQUARE, "Play");
        hint(g_back(), "Back");
    }
}

static void dls_destroy(View *v) {
    if (g_dls == v->data) g_dls = NULL;
    free(v->data);
    free(v);
}

View *view_downloads_new(void) {
    View *v = calloc(1, sizeof *v);
    Downloads *d = calloc(1, sizeof *d);
    if (!v || !d) { free(v); free(d); return NULL; }
    v->update = dls_update;
    v->draw = dls_draw;
    v->destroy = dls_destroy;
    v->data = d;
    return v;
}
