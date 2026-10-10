/* Book details: cover, title, author, narrator, series, length, progress,
 * Resume / Play from start, and the description (scrollable). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "abs_client.h"
#include <vita2d.h>

#include "covers.h"
#include "downloads.h"
#include "libmodel.h"
#include "osal.h"
#include "player.h"
#include "strutil.h"
#include "views.h"
#include "worker.h"

#define COVER 250.0f
#define RX (CX + COVER + 32)
#define RW (CX + CW - RX)

enum { F_PLAY, F_START, F_DOWNLOAD, F_DESC };

typedef struct {
    BookSummary copy;     /* used when the book isn't in the library (offline) */
    char id[40];
    int focus;            /* F_* */
    bool confirm_delete;
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
    return i >= 0 ? lib_book(i) : &b->copy;
}

static void download_action(Book *b) {
    DlState st = dl_state(b->id, NULL);
    switch (st) {
    case DL_NONE:
    case DL_PAUSED:
    case DL_FAILED:
        b->confirm_delete = false;
        if (dl_start(b->id) != 0) app_toast("Couldn't start the download");
        break;
    case DL_QUEUED:
    case DL_ACTIVE:
        dl_pause(b->id);
        break;
    case DL_DONE:
        if (player_book() && !strcmp(player_book_id(), b->id)) { app_toast("Stop playing this book first"); return; }
        if (!b->confirm_delete) { b->confirm_delete = true; return; }
        b->confirm_delete = false;
        dl_delete(b->id);
        app_toast("Download deleted");
        break;
    }
}

static void act(Book *b, int which) {
    const BookSummary *s = summary(b);
    if (!s) return;
    if (which != F_DOWNLOAD) b->confirm_delete = false;
    if (which == F_PLAY) app_play(s, -1);
    else if (which == F_START) app_play(s, 0);
    else if (which == F_DOWNLOAD) download_action(b);
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
    if (b->focus < F_DESC) {
        int before = b->focus;
        if (in->repeat & B_LEFT) { if (b->focus == F_PLAY) { app_focus_sidebar(); return; } b->focus--; }
        if (in->repeat & B_RIGHT && b->focus < F_DOWNLOAD) b->focus++;
        if (in->repeat & B_DOWN && max > 0) b->focus = F_DESC;
        if (b->focus != before) b->confirm_delete = false;
        if (in->down & B_OK) act(b, b->focus);
    } else {
        if (in->repeat & B_DOWN) b->desc.target = b->desc.target + 66 > max ? max : b->desc.target + 66;
        if (in->repeat & B_UP) {
            if (b->desc.target <= 0) b->focus = F_PLAY;
            else b->desc.target = b->desc.target - 66 < 0 ? 0 : b->desc.target - 66;
        }
        if (in->repeat & B_LEFT) { app_focus_sidebar(); return; }
    }
    if (in->down & B_SQUARE) act(b, 0);
    if (in->dragging && in->start_y > desc_top()) scroll_drag(&b->desc, in->drag_dy, max);
}

/* fill: 0..1 progress shown inside the button (<0: none) */
static void button(float x, float y, float w, float h, int icon, const char *label, bool primary, bool focus, int which,
                   float fill, uint32_t text) {
    gfx_rrect(x, y, w, h, h / 2, primary ? C_ACCENT : C_PANEL2);
    if (fill > 0) {
        vita2d_enable_clipping();
        vita2d_set_clip_rectangle((int)x, (int)y, (int)(x + w * (fill > 1 ? 1 : fill)), (int)(y + h));
        gfx_rrect(x, y, w, h, h / 2, gfx_alpha(C_ACCENT, 0.35f));
        vita2d_disable_clipping();
    }
    if (focus) draw_focus_ring(x, y, w, h, h / 2);
    uint32_t c = text ? text : primary ? C_ON_ACCENT : C_TEXT;
    float lw = gfx_ink_w(F_BOLD, 16, label), iw = 17, gap = 8;
    float sx = x + (w - (iw + gap + lw)) / 2;
    gfx_icon(icon, sx + iw / 2, y + h / 2, iw, c);
    gfx_text_vc(F_BOLD, 16, sx + iw + gap, y + h / 2, c, label);
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
    float by = top + COVER - 52, gap = 8, bw0 = 150, bw = (RW - bw0 - 2 * gap) / 2;
    button(RX, by, bw0, 52, IC_PLAY, resume ? "Resume" : "Play", true, focused && b->focus == F_PLAY, F_PLAY, -1, 0);
    button(RX + bw0 + gap, by, bw, 52, IC_BACK, "From start", false, focused && b->focus == F_START, F_START, -1, 0);
    /* download */
    float frac = 0;
    DlState ds = dl_state(b->id, &frac);
    char dl_label[32];
    int dl_icon = IC_DOWNLOAD;
    uint32_t dl_text = 0;
    float dl_fill = -1;
    switch (ds) {
    case DL_NONE: str_copy(dl_label, sizeof dl_label, "Download"); break;
    case DL_QUEUED: str_copy(dl_label, sizeof dl_label, "Queued"); dl_icon = IC_CLOCK; break;
    case DL_ACTIVE: snprintf(dl_label, sizeof dl_label, "%d%%", (int)(frac * 100)); dl_icon = IC_PAUSE; dl_fill = frac; break;
    case DL_PAUSED: snprintf(dl_label, sizeof dl_label, "Paused"); dl_fill = frac; break;
    case DL_FAILED: str_copy(dl_label, sizeof dl_label, "Retry"); dl_text = C_ERROR; break;
    case DL_DONE:
        if (b->confirm_delete) { str_copy(dl_label, sizeof dl_label, "Delete?"); dl_icon = IC_TRASH; dl_text = C_ERROR; }
        else { str_copy(dl_label, sizeof dl_label, "Saved"); dl_icon = IC_CHECK; dl_text = C_ACCENT; }
        break;
    }
    button(RX + bw0 + bw + 2 * gap, by, bw, 52, dl_icon, dl_label, false, focused && b->focus == F_DOWNLOAD, F_DOWNLOAD, dl_fill, dl_text);
    if (ds == DL_FAILED || ds == DL_DONE || ds == DL_ACTIVE || ds == DL_PAUSED) {
        DlInfo list[128];
        int n = dl_list(list, 128);
        for (int i = 0; i < n; i++) {
            if (strcmp(list[i].id, b->id)) continue;
            char sz[32], tot[32], msg[128];
            snprintf(sz, sizeof sz, "%.0f MB", list[i].done_bytes / 1048576.0);
            snprintf(tot, sizeof tot, "%.0f MB", list[i].total_bytes / 1048576.0);
            if (ds == DL_FAILED) snprintf(msg, sizeof msg, "Download failed: %s", list[i].error);
            else if (ds == DL_DONE) snprintf(msg, sizeof msg, "Downloaded \xC2\xB7 %s on the memory card", tot);
            else snprintf(msg, sizeof msg, "%s of %s", sz, tot);
            gfx_text_right(F_REG, 14, RX + RW, by + 52 + 12, ds == DL_FAILED ? C_ERROR : C_SUB, msg);
        }
    }

    /* description */
    float dy = desc_top();
    gfx_rect(CX, dy - 22, CW, 1, C_LINE);
    mutex_lock(&b->lock);
    if (b->state == 1 && b->d.description && b->d.description[0]) {
        clip_content(dy - 4);
        gfx_wrap(F_REG, 15, CX, dy - b->desc.y, CW, 22, 400, C_SUB, b->d.description);
        clip_off();
        if (focused && b->focus == F_DESC) gfx_rect(CX - 10, dy - 4, 3, CONTENT_BOTTOM - dy - 8, C_ACCENT);
    } else if (b->state == 0) {
        gfx_text(F_REG, 15, CX, dy, C_DIM, "Loading details\xE2\x80\xA6");
    } else {
        gfx_text(F_REG, 15, CX, dy, C_DIM, "No description");
    }
    mutex_unlock(&b->lock);
    if (focused) {
        hint(G_DPAD, b->focus == F_DESC ? "Scroll" : "Move");
        if (b->focus == F_PLAY) hint(g_ok(), resume ? "Resume" : "Play");
        else if (b->focus == F_START) hint(g_ok(), "Play from start");
        else if (b->focus == F_DOWNLOAD)
            hint(g_ok(), ds == DL_NONE ? "Download" : ds == DL_ACTIVE || ds == DL_QUEUED ? "Pause download" :
                         ds == DL_DONE ? (b->confirm_delete ? "Delete" : "Delete download") : "Resume download");
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

View *view_book_new(const BookSummary *s) {
    if (!s) return NULL;
    View *v = calloc(1, sizeof *v);
    Book *b = calloc(1, sizeof *b);
    if (!v || !b) { free(v); free(b); return NULL; }
    b->copy = *s;
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
