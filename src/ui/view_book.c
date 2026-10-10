/* Book details: cover, title, author, narrator, series, length, progress,
 * Resume / Play from start, and the description (scrollable). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "abs_client.h"
#include "covers.h"
#include "libmodel.h"
#include "osal.h"
#include "player.h"
#include "strutil.h"
#include "views.h"
#include "worker.h"

#define COVER 250.0f
#define RX (CX + COVER + 32)
#define RW (CX + CW - RX)

typedef struct {
    int index;
    char id[40];
    int focus;            /* 0 resume/play, 1 from start, 2 description */
    Scroll desc;
    /* details fetched in the background */
    Mutex lock;
    volatile int state;   /* 0 loading, 1 ready, -1 failed */
    BookDetail d;
    int refs;             /* the job holds a reference */
    bool dead;
} Book;

static Book *g_book;

static void release(Book *b) {
    mutex_lock(&b->lock);
    bool free_it = --b->refs == 0;
    mutex_unlock(&b->lock);
    if (free_it) {
        book_detail_free(&b->d);
        mutex_destroy(&b->lock);
        free(b);
    }
}

static void fetch_job(void *ud) {
    Book *b = ud;
    BookDetail d;
    memset(&d, 0, sizeof d);
    int rc = b->dead ? ABS_ERR_NET : absc_fetch_item(b->id, &d);
    mutex_lock(&b->lock);
    if (rc == ABS_OK) {
        if (d.description) str_strip_html(d.description);
        b->d = d;
        b->state = 1;
    } else {
        b->state = -1;
    }
    mutex_unlock(&b->lock);
    release(b);
}

static const BookSummary *summary(Book *b) {
    int i = lib_find_id(b->id);
    return lib_book(i >= 0 ? i : b->index);
}

static void act(Book *b, int which) {
    const BookSummary *s = summary(b);
    if (!s) return;
    if (which == 0) app_play(s, -1);
    else if (which == 1) app_play(s, 0);
}

static void on_tap(int which, int x) {
    (void)x;
    if (!g_book) return;
    g_book->focus = which;
    act(g_book, which);
}

static float desc_top(void) { return 380; }

static void book_update(View *v, const Input *in, float dt) {
    Book *b = v->data;
    scroll_step(&b->desc, dt);
    int lines = b->state == 1 && b->d.description ? gfx_wrap_count(F_REG, 15, RW, b->d.description) : 0;
    float max = lines * 22 - (CONTENT_BOTTOM - desc_top() - 12);
    if (max < 0) max = 0;
    if (in->down & B_BACK) { app_pop(); return; }
    if (b->focus < 2) {
        if (in->repeat & B_LEFT) { if (b->focus == 0) { app_focus_sidebar(); return; } b->focus--; }
        if (in->repeat & B_RIGHT && b->focus == 0) b->focus = 1;
        if (in->repeat & B_DOWN && max > 0) b->focus = 2;
        if (in->down & B_OK) act(b, b->focus);
    } else {
        if (in->repeat & B_DOWN) b->desc.target = b->desc.target + 66 > max ? max : b->desc.target + 66;
        if (in->repeat & B_UP) {
            if (b->desc.target <= 0) b->focus = 0;
            else b->desc.target = b->desc.target - 66 < 0 ? 0 : b->desc.target - 66;
        }
        if (in->repeat & B_LEFT) { app_focus_sidebar(); return; }
    }
    if (in->down & B_SQUARE) act(b, 0);
    if (in->dragging && in->start_y > desc_top()) scroll_drag(&b->desc, in->drag_dy, max);
}

static void button(float x, float y, float w, float h, int icon, const char *label, bool primary, bool focus, int which) {
    gfx_rrect(x, y, w, h, h / 2, primary ? C_ACCENT : C_PANEL2);
    if (focus) draw_focus_ring(x, y, w, h, h / 2);
    uint32_t c = primary ? C_ON_ACCENT : C_TEXT;
    float lw = gfx_ink_w(F_BOLD, 17, label), iw = 18, gap = 10;
    float sx = x + (w - (iw + gap + lw)) / 2;
    gfx_icon(icon, sx + iw / 2, y + h / 2, iw, c);
    gfx_text_vc(F_BOLD, 17, sx + iw + gap, y + h / 2, c, label);
    hit_add(x, y, w, h, on_tap, which, 0);
}

static void book_draw(View *v, bool focused) {
    Book *b = v->data;
    g_book = b;
    const BookSummary *s = summary(b);
    if (!s) return;
    float top = 30;
    covers_draw(s->id, CX, top, COVER, 1);
    char line[256];
    int tl = gfx_wrap(F_BOLD, 26, RX, top + 2, RW, 34, 2, C_TEXT, s->title);
    float y = top + 2 + tl * 34 + 4;
    gfx_fit(F_REG, 18, s->author[0] ? s->author : "Unknown author", RW, line, sizeof line);
    gfx_text(F_REG, 18, RX, y, C_ACCENT, line);
    y += 34;
    char dur[32];
    fmt_duration(s->duration, dur, sizeof dur);
    mutex_lock(&b->lock);
    const char *narr = b->state == 1 && b->d.narrator[0] ? b->d.narrator : NULL;
    mutex_unlock(&b->lock);
    if (narr) {
        snprintf(line, sizeof line, "Narrated by %s", narr);
        char fit[256];
        gfx_fit(F_REG, 15, line, RW, fit, sizeof fit);
        gfx_text(F_REG, 15, RX, y, C_SUB, fit);
        y += 24;
    }
    if (s->series[0]) {
        gfx_fit(F_REG, 15, s->series, RW, line, sizeof line);
        gfx_text(F_REG, 15, RX, y, C_SUB, line);
        y += 24;
    }
    gfx_text(F_REG, 15, RX, y, C_SUB, dur);
    y += 30;
    if (s->finished) {
        gfx_text(F_BOLD, 15, RX, y, C_ACCENT, "Finished");
        y += 26;
    } else if (s->progress > 0.005f) {
        draw_progress_bar(RX, y + 4, RW * 0.6f, 6, s->progress, C_ACCENT, C_LINE);
        char left[32];
        fmt_duration(s->duration - s->current_time, left, sizeof left);
        snprintf(line, sizeof line, "%s left", left);
        gfx_text_vc(F_REG, 15, RX + RW * 0.6f + 14, y + 7, C_SUB, line);
        y += 26;
    }
    bool resume = s->progress > 0.005f && !s->finished;
    float by = top + COVER - 52;
    button(RX, by, 190, 52, IC_PLAY, resume ? "Resume" : "Play", true, focused && b->focus == 0, 0);
    button(RX + 206, by, 200, 52, IC_BACK, "From start", false, focused && b->focus == 1, 1);

    /* description */
    float dy = desc_top();
    gfx_rect(CX, dy - 22, CW, 1, C_LINE);
    mutex_lock(&b->lock);
    if (b->state == 1 && b->d.description && b->d.description[0]) {
        clip_content(dy - 4);
        gfx_wrap(F_REG, 15, CX, dy - b->desc.y, CW, 22, 400, C_SUB, b->d.description);
        clip_off();
        if (focused && b->focus == 2) gfx_rect(CX - 10, dy - 4, 3, CONTENT_BOTTOM - dy - 8, C_ACCENT);
    } else if (b->state == 0) {
        gfx_text(F_REG, 15, CX, dy, C_DIM, "Loading details\xE2\x80\xA6");
    } else {
        gfx_text(F_REG, 15, CX, dy, C_DIM, "No description");
    }
    mutex_unlock(&b->lock);
    if (focused) {
        hint(G_DPAD, b->focus == 2 ? "Scroll" : "Move");
        if (b->focus < 2) hint(g_ok(), b->focus == 0 ? (resume ? "Resume" : "Play") : "Play from start");
        hint(g_back(), "Back");
    }
}

static void book_destroy(View *v) {
    Book *b = v->data;
    if (g_book == b) g_book = NULL;
    b->dead = true;
    release(b);
    free(v);
}

View *view_book_new(int lib_index) {
    const BookSummary *s = lib_book(lib_index);
    if (!s) return NULL;
    View *v = calloc(1, sizeof *v);
    Book *b = calloc(1, sizeof *b);
    if (!v || !b) { free(v); free(b); return NULL; }
    b->index = lib_index;
    str_copy(b->id, sizeof b->id, s->id);
    mutex_init(&b->lock);
    b->refs = 2;
    if (!worker_post(g_api, fetch_job, b)) { b->refs = 1; b->state = -1; }
    v->update = book_update;
    v->draw = book_draw;
    v->destroy = book_destroy;
    v->data = b;
    return v;
}
