/* Home: Continue listening (large cards with time left) and Recently added. */
#include <stdio.h>
#include <stdlib.h>

#include "covers.h"
#include "libmodel.h"
#include "strutil.h"
#include "views.h"

#define CONT_COVER 150.0f
#define CONT_W 170.0f
#define RECENT_COVER 112.0f
#define RECENT_W 130.0f

typedef struct {
    int row;           /* 0 continue, 1 recent */
    int col[2];
    Scroll sx[2];
} Home;

static Home *g_home;

static void rows(int *nc, int *nr, const int **c, const int **r) {
    *c = lib_continue(nc);
    *r = lib_recent(nr);
}

static void on_tap(int row, int col) {
    if (!g_home) return;
    g_home->row = row;
    g_home->col[row] = col;
    int nc, nr;
    const int *c, *r;
    rows(&nc, &nr, &c, &r);
    if (row == 0 && col < nc) app_play(lib_book(c[col]), -1);
    if (row == 1 && col < nr) app_open_book(r[col]);
}

static void home_update(View *v, const Input *in, float dt) {
    Home *h = v->data;
    int nc, nr;
    const int *c, *r;
    rows(&nc, &nr, &c, &r);
    int n[2] = {nc, nr};
    if (!nc) h->row = 1;
    scroll_step(&h->sx[0], dt);
    scroll_step(&h->sx[1], dt);
    int *col = &h->col[h->row];
    if (*col >= n[h->row]) *col = n[h->row] ? n[h->row] - 1 : 0;
    if (in->repeat & B_LEFT) { if (*col == 0) { app_focus_sidebar(); return; } (*col)--; }
    if (in->repeat & B_RIGHT && *col + 1 < n[h->row]) (*col)++;
    if (in->repeat & B_UP && h->row == 1 && nc) h->row = 0;
    if (in->repeat & B_DOWN && h->row == 0 && nr) h->row = 1;
    if (in->down & B_BACK) app_focus_sidebar();
    if (lib_state() == LIB_ERROR && !nc && !nr && in->down & B_OK) { lib_refresh(); return; }
    col = &h->col[h->row];
    if (h->row == 0 && nc && *col < nc) {
        if (in->down & (B_OK | B_SQUARE)) app_play(lib_book(c[*col]), -1);
    } else if (h->row == 1 && nr && *col < nr) {
        if (in->down & B_OK) app_open_book(r[*col]);
        if (in->down & B_SQUARE) app_play(lib_book(r[*col]), -1);
    }
    for (int k = 0; k < 2; k++) {
        float w = k == 0 ? CONT_W : RECENT_W;
        float max = n[k] * w - CW;
        scroll_show(&h->sx[k], h->col[k] * w - (h->col[k] ? 40 : 0), (h->col[k] + 1) * w + 40, CW, max > 0 ? max : 0);
    }
}

static void section_title(float y, const char *t) { gfx_text(F_BOLD, 21, CX, y, C_TEXT, t); }

static void home_draw(View *v, bool focused) {
    Home *h = v->data;
    g_home = h;
    int nc, nr;
    const int *c, *r;
    rows(&nc, &nr, &c, &r);
    float cx = CX + CW / 2, cy = CONTENT_BOTTOM / 2;
    if (!nc && !nr) {
        LibState st = lib_state();
        if (st == LIB_ERROR) {
            gfx_icon(IC_CLOUD_OFF, cx, cy - 34, 40, C_ERROR);
            gfx_text_center(F_BOLD, 19, cx, cy + 14, C_TEXT, lib_error());
            gfx_text_center(F_REG, 16, cx, cy + 44, C_SUB, "Check your Wi-Fi, then try again");
            if (focused) hint(g_ok(), "Try again");
        } else if (st == LIB_READY) {
            gfx_text_center(F_BOLD, 20, cx, cy - 12, C_TEXT, "Your library is empty");
            gfx_text_center(F_REG, 16, cx, cy + 18, C_SUB, "Add books on your Audiobookshelf server");
        } else {
            draw_spinner(cx, cy - 20, 18, C_ACCENT);
            gfx_text_center(F_REG, 17, cx, cy + 26, C_SUB, "Loading your library\xE2\x80\xA6");
        }
        return;
    }
    float y = 26;
    gfx_text(F_BOLD, 30, CX, y, C_TEXT, "Home");
    y = 80;
    clip_content(0);
    if (nc) {
        section_title(y, "Continue listening");
        y += 34;
        for (int i = 0; i < nc; i++) {
            float x = CX + i * CONT_W - h->sx[0].y;
            if (x > SCREEN_W || x + CONT_W < CX - 40) continue;
            const BookSummary *b = lib_book(c[i]);
            bool foc = focused && h->row == 0 && h->col[0] == i;
            covers_draw(b->id, x, y, CONT_COVER, 1);
            gfx_rect(x, y + CONT_COVER - 5, CONT_COVER, 5, gfx_alpha(0xFF000000, 0.6f));
            gfx_rect(x, y + CONT_COVER - 5, CONT_COVER * b->progress, 5, C_ACCENT);
            if (foc) {
                draw_focus_ring(x, y, CONT_COVER, CONT_COVER, 2);
                gfx_circle(x + CONT_COVER / 2, y + CONT_COVER / 2, 28, gfx_alpha(0xFF000000, 0.6f));
                gfx_icon(IC_PLAY, x + CONT_COVER / 2, y + CONT_COVER / 2, 26, C_TEXT);
            }
            char line[200], left[32];
            gfx_fit(F_BOLD, 15, b->title, CONT_COVER, line, sizeof line);
            gfx_text(F_BOLD, 15, x, y + CONT_COVER + 10, C_TEXT, line);
            double rem = b->duration - b->current_time;
            fmt_duration(rem > 0 ? rem : 0, left, sizeof left);
            snprintf(line, sizeof line, "%s left", left);
            gfx_text(F_REG, 14, x, y + CONT_COVER + 32, C_SUB, line);
            hit_add(x, y, CONT_COVER, CONT_COVER + 50, on_tap, 0, i);
        }
        y += CONT_COVER + 70;
    }
    if (nr) {
        section_title(y, "Recently added");
        y += 34;
        for (int i = 0; i < nr; i++) {
            float x = CX + i * RECENT_W - h->sx[1].y;
            if (x > SCREEN_W || x + RECENT_W < CX - 40) continue;
            const BookSummary *b = lib_book(r[i]);
            draw_book_cell(b, x, y, RECENT_COVER, focused && h->row == 1 && h->col[1] == i);
            hit_add(x, y, RECENT_COVER, RECENT_COVER + 50, on_tap, 1, i);
        }
    }
    clip_off();
    if (focused) {
        hint(G_DPAD, "Move");
        if (h->row == 0) {
            hint(g_ok(), "Resume");
        } else {
            hint(g_ok(), "Details");
            hint(G_SQUARE, "Play");
        }
    }
}

static void home_destroy(View *v) {
    if (g_home == v->data) g_home = NULL;
    free(v->data);
    free(v);
}

View *view_home_new(void) {
    View *v = calloc(1, sizeof *v);
    Home *h = calloc(1, sizeof *h);
    if (!v || !h) { free(v); free(h); return NULL; }
    v->update = home_update;
    v->draw = home_draw;
    v->destroy = home_destroy;
    v->data = h;
    return v;
}
