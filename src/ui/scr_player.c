/* Now Playing: cover, chapter and book progress, transport controls, speed,
 * sleep timer, chapter list and screen off. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <vita2d.h>

#include "covers.h"
#include "engine.h"
#include "player.h"
#include "strutil.h"
#include "sys.h"
#include "timeline.h"
#include "views.h"

#define COVER 316.0f
#define COVER_X 56.0f
#define COVER_Y 84.0f
#define RX 420.0f
#define RW 484.0f
#define BAR_Y 276.0f
#define CTRL_Y 392.0f
#define PILL_Y 450.0f
#define PILL_H 42.0f

enum { K_BACK, K_PREV, K_PLAY, K_NEXT, K_FWD };
enum { P_SPEED, P_SLEEP, P_CHAPTERS, P_SCREEN };

static struct {
    int row, col[2];
    bool chapters;
    int ch_focus;
    Scroll ch_sc;
    bool screen_off;
} S;

void player_screen_enter(void) {
    S.row = 0;
    S.col[0] = K_PLAY;
    S.chapters = false;
}

static void open_chapters(void) {
    const BookDetail *b = player_book();
    if (!b || b->nchapters <= 0) { app_toast("This book has no chapters"); return; }
    S.chapters = true;
    S.ch_focus = player_chapter_index();
    if (S.ch_focus < 0) S.ch_focus = 0;
    S.ch_sc.y = S.ch_sc.target = S.ch_focus * 52.0f - 120 > 0 ? S.ch_focus * 52.0f - 120 : 0;
}

static void screen_off(void) {
    S.screen_off = true;
    sys_screen(false);
}

static void do_control(int k) {
    switch (k) {
    case K_BACK: player_skip(-g_cfg.skip_back); break;
    case K_PREV: player_chapter(-1); break;
    case K_PLAY: player_toggle(); break;
    case K_NEXT: player_chapter(1); break;
    case K_FWD: player_skip(g_cfg.skip_fwd); break;
    }
}

static void do_pill(int p) {
    switch (p) {
    case P_SPEED: player_cycle_speed(); app_save_config(); break;
    case P_SLEEP: player_cycle_sleep(); break;
    case P_CHAPTERS: open_chapters(); break;
    case P_SCREEN: screen_off(); break;
    }
}

static void on_tap(int row, int col) {
    S.row = row;
    S.col[row] = col;
    if (row == 0) do_control(col);
    else do_pill(col);
}

static void on_tap_back(int a, int b) { (void)a; (void)b; app_close_player(); }

/* chapter bounds containing t (whole book when there are no chapters) */
static void chapter_span(double t, double *start, double *end, int *index) {
    const BookDetail *b = player_book();
    int ci = b ? tl_chapter_at(b->chapters, b->nchapters, t) : -1;
    *index = ci;
    if (ci >= 0) { *start = b->chapters[ci].start; *end = b->chapters[ci].end; }
    else { *start = 0; *end = player_duration(); }
}

static void on_tap_bar(int a, int b) {
    (void)a; (void)b;
    double s, e;
    int ci;
    chapter_span(player_position(), &s, &e, &ci);
    float f = (app_tap_x() - RX) / RW;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    player_jump(s + (e - s) * f);
}

static void on_tap_chapter(int i, int b) {
    (void)b;
    const BookDetail *bk = player_book();
    if (!bk || i >= bk->nchapters) return;
    player_jump(bk->chapters[i].start);
    S.chapters = false;
}

void player_screen_update(const Input *in, float dt) {
    if (S.screen_off) {
        if (in->any || in->touching) { S.screen_off = false; sys_screen(true); }
        return;
    }
    if (player_phase() == PL_FAILED || (!player_book() && player_phase() != PL_OPENING)) {
        if (in->down & (B_BACK | B_OK | B_START)) app_close_player();
        return;
    }
    if (S.chapters) {
        const BookDetail *b = player_book();
        int n = b ? b->nchapters : 0;
        scroll_step(&S.ch_sc, dt);
        if (in->repeat & B_UP && S.ch_focus > 0) S.ch_focus--;
        if (in->repeat & B_DOWN && S.ch_focus + 1 < n) S.ch_focus++;
        if (in->repeat & B_L) S.ch_focus = S.ch_focus > 6 ? S.ch_focus - 6 : 0;
        if (in->repeat & B_R) S.ch_focus = S.ch_focus + 6 < n ? S.ch_focus + 6 : n - 1;
        if (in->down & B_OK && n) { player_jump(b->chapters[S.ch_focus].start); S.chapters = false; }
        if (in->down & (B_BACK | B_TRIANGLE)) S.chapters = false;
        float max = n * 52.0f - 320;
        if (in->dragging) scroll_drag(&S.ch_sc, in->drag_dy, max > 0 ? max : 0);
        else scroll_show(&S.ch_sc, S.ch_focus * 52.0f - 52, (S.ch_focus + 2) * 52.0f, 320, max > 0 ? max : 0);
        return;
    }
    if (in->down & (B_BACK | B_START)) { app_close_player(); return; }
    if (in->down & B_SQUARE) player_toggle();
    if (in->repeat & B_L) player_skip(-g_cfg.skip_back);
    if (in->repeat & B_R) player_skip(g_cfg.skip_fwd);
    if (in->down & B_TRIANGLE) open_chapters();
    if (in->down & B_SELECT) screen_off();
    int n = S.row == 0 ? 5 : 4;
    if (in->repeat & B_LEFT && S.col[S.row] > 0) S.col[S.row]--;
    if (in->repeat & B_RIGHT && S.col[S.row] + 1 < n) S.col[S.row]++;
    if (in->repeat & B_DOWN && S.row == 0) { S.row = 1; S.col[1] = S.col[0] * 4 / 5; }
    if (in->repeat & B_UP && S.row == 1) { S.row = 0; S.col[0] = S.col[1] * 5 / 4 + (S.col[1] >= 2); if (S.col[0] > 4) S.col[0] = 4; }
    if (in->down & B_OK) {
        if (S.row == 0) do_control(S.col[0]);
        else do_pill(S.col[1]);
    }
}

static void control(int k, float cx, float r, int icon, const char *num) {
    bool foc = S.row == 0 && S.col[0] == k && !S.chapters;
    bool primary = k == K_PLAY;
    gfx_circle(cx, CTRL_Y, r, primary ? C_ACCENT : gfx_alpha(C_PANEL2, 0.9f));
    if (foc) gfx_ring(cx - r, CTRL_Y - r, 2 * r, 2 * r, r, 3, primary ? C_TEXT : C_ACCENT);
    uint32_t c = primary ? C_ON_ACCENT : C_TEXT;
    gfx_icon(icon, cx, CTRL_Y, primary ? 34 : (num ? 36 : 24), c);
    if (num) gfx_text_center(F_BOLD, 11, cx, CTRL_Y + 1.5f, c, num);
    hit_add(cx - r, CTRL_Y - r, 2 * r, 2 * r, on_tap, 0, k);
}

static void pill(int p, float x, float w, int icon, const char *label, bool active) {
    bool foc = S.row == 1 && S.col[1] == p && !S.chapters;
    gfx_rrect(x, PILL_Y, w, PILL_H, PILL_H / 2, active ? gfx_alpha(C_ACCENT, 0.22f) : gfx_alpha(C_PANEL2, 0.9f));
    if (foc) draw_focus_ring(x, PILL_Y, w, PILL_H, PILL_H / 2);
    uint32_t c = active ? C_ACCENT : C_TEXT;
    float iw = 18, gap = 8, lw = gfx_ink_w(F_BOLD, 15, label);
    float sx = x + (w - (iw + gap + lw)) / 2;
    gfx_icon(icon, sx + iw / 2, PILL_Y + PILL_H / 2, iw, c);
    gfx_text_vc(F_BOLD, 15, sx + iw + gap, PILL_Y + PILL_H / 2, c, label);
    hit_add(x, PILL_Y, w, PILL_H, on_tap, 1, p);
}

static void draw_chapters(void) {
    const BookDetail *b = player_book();
    if (!b) return;
    gfx_rect(0, 0, SCREEN_W, CONTENT_BOTTOM, gfx_alpha(0xFF000000, 0.6f));
    float x = 170, y = 34, w = 620, h = 436;
    gfx_rrect(x, y, w, h, 18, C_PANEL);
    gfx_text(F_BOLD, 22, x + 24, y + 22, C_TEXT, "Chapters");
    char t[48];
    snprintf(t, sizeof t, "%d", b->nchapters);
    gfx_text_right(F_REG, 16, x + w - 24, y + 26, C_SUB, t);
    float top = y + 70;
    int cur = player_chapter_index();
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle((int)x, (int)top - 4, (int)(x + w), (int)(y + h - 12));
    for (int i = 0; i < b->nchapters; i++) {
        float ry = top + i * 52 - S.ch_sc.y;
        if (ry < top - 52 || ry > y + h) continue;
        bool foc = i == S.ch_focus;
        if (foc) gfx_rrect(x + 12, ry, w - 24, 46, 10, C_PANEL2);
        if (i == cur) gfx_rrect(x + 12, ry + 12, 4, 22, 2, C_ACCENT);
        char num[8], tm[24], title[160];
        snprintf(num, sizeof num, "%d", i + 1);
        gfx_text_center(F_BOLD, 15, x + 46, ry + 23, i == cur ? C_ACCENT : C_DIM, num);
        fmt_time(b->chapters[i].start, tm, sizeof tm);
        float tw = gfx_text_w(F_REG, 15, tm);
        gfx_fit(i == cur ? F_BOLD : F_REG, 17, b->chapters[i].title, w - 120 - tw - 30, title, sizeof title);
        gfx_text_vc(i == cur ? F_BOLD : F_REG, 17, x + 76, ry + 23, i == cur ? C_ACCENT : C_TEXT, title);
        gfx_text_right_vc(F_REG, 15, x + w - 30, ry + 23, C_SUB, tm);
        hit_add(x + 12, ry, w - 24, 46, on_tap_chapter, i, 0);
    }
    vita2d_disable_clipping();
    hint(G_DPAD, "Move");
    hint(G_L, "Page");
    hint(g_ok(), "Play chapter");
    hint(g_back(), "Close");
}

void player_screen_draw(void) {
    const BookDetail *b = player_book();
    uint32_t back = b ? covers_backdrop(b->id, RGBA(30, 30, 36, 255)) : RGBA(30, 30, 36, 255);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BG);
    gfx_vgrad(0, 0, SCREEN_W, CONTENT_BOTTOM, back, C_BG);

    /* top bar */
    gfx_icon(IC_CHEVRON_LEFT, 30, 34, 20, C_SUB);
    gfx_text_vc(F_BOLD, 15, 46, 34, C_SUB, "Now Playing");
    hit_add(0, 0, 200, 64, on_tap_back, 0, 0);
    const char *st = player_status();
    if (st) {
        float w = gfx_ink_w(F_BOLD, 14, st) + 32;
        gfx_rrect(SCREEN_W - 24 - w, 18, w, 32, 16, gfx_alpha(0xFF000000, 0.45f));
        gfx_text_center(F_BOLD, 14, SCREEN_W - 24 - w / 2, 34, player_phase() == PL_FAILED ? C_ERROR : C_ACCENT, st);
    }

    if (!b) {
        float cx = SCREEN_W / 2, cy = CONTENT_BOTTOM / 2;
        if (player_phase() == PL_OPENING) {
            draw_spinner(cx, cy - 16, 20, C_ACCENT);
            gfx_text_center(F_REG, 17, cx, cy + 34, C_SUB, "Opening\xE2\x80\xA6");
        } else {
            gfx_icon(IC_CLOUD_OFF, cx, cy - 30, 40, C_ERROR);
            gfx_text_center(F_BOLD, 19, cx, cy + 18, C_TEXT, player_error()[0] ? player_error() : "Nothing playing");
        }
        hint(g_back(), "Back");
        return;
    }

    covers_draw(b->id, COVER_X, COVER_Y, COVER, 1);
    double pos = player_position(), cs, ce;
    int ci;
    chapter_span(pos, &cs, &ce, &ci);
    char line[256], fit[256];
    if (ci >= 0) snprintf(line, sizeof line, "CHAPTER %d OF %d", ci + 1, b->nchapters);
    else snprintf(line, sizeof line, "NOW PLAYING");
    gfx_text(F_BOLD, 13, RX, COVER_Y + 4, C_ACCENT, line);
    int tl = gfx_wrap(F_BOLD, 28, RX, COVER_Y + 30, RW, 36, 2, C_TEXT, b->title);
    float y = COVER_Y + 30 + tl * 36 + 2;
    gfx_fit(F_REG, 18, b->author, RW, fit, sizeof fit);
    gfx_text(F_REG, 18, RX, y, C_SUB, fit);
    if (ci >= 0) {
        gfx_fit(F_REG, 16, b->chapters[ci].title, RW, fit, sizeof fit);
        gfx_text(F_REG, 16, RX, BAR_Y - 30, C_TEXT, fit);
    }
    /* chapter progress */
    double span = ce - cs > 0 ? ce - cs : 1;
    float frac = (float)((pos - cs) / span);
    draw_progress_bar(RX, BAR_Y, RW, 6, frac, C_ACCENT, gfx_alpha(C_TEXT, 0.18f));
    gfx_circle(RX + RW * (frac < 0 ? 0 : frac > 1 ? 1 : frac), BAR_Y + 3, 8, C_TEXT);
    hit_add(RX - 8, BAR_Y - 18, RW + 16, 42, on_tap_bar, 0, 0);
    char a[32], r[32];
    fmt_time(pos - cs, a, sizeof a);
    fmt_time(ce - pos, r + 1, sizeof r - 1);
    r[0] = '-';
    gfx_text(F_REG, 15, RX, BAR_Y + 18, C_SUB, a);
    gfx_text_right(F_REG, 15, RX + RW, BAR_Y + 18, C_SUB, r);
    char bt[32], bd[32], left[32];
    fmt_time(pos, bt, sizeof bt);
    fmt_time(b->duration, bd, sizeof bd);
    fmt_duration(b->duration - pos > 0 ? b->duration - pos : 0, left, sizeof left);
    snprintf(line, sizeof line, "%s of %s \xC2\xB7 %s left", bt, bd, left);
    gfx_text(F_REG, 14, RX, BAR_Y + 46, C_DIM, line);

    /* transport */
    float cx = RX + RW / 2;
    char nb[8], nf[8];
    snprintf(nb, sizeof nb, "%d", g_cfg.skip_back);
    snprintf(nf, sizeof nf, "%d", g_cfg.skip_fwd);
    control(K_BACK, cx - 192, 30, IC_BACK, nb);
    control(K_PREV, cx - 96, 30, IC_PREV, NULL);
    control(K_PLAY, cx, 40, player_playing() ? IC_PAUSE : IC_PLAY, NULL);
    control(K_NEXT, cx + 96, 30, IC_NEXT, NULL);
    control(K_FWD, cx + 192, 30, IC_FWD, nf);

    /* options */
    float pw = (RW - 3 * 12) / 4;
    char sp[16], sl[32];
    snprintf(sp, sizeof sp, "%g\xC3\x97", player_speed());
    const SleepTimer *slp = player_sleep();
    player_sleep_label(sl, sizeof sl);
    if (slp->mode == SLEEP_END_CHAPTER) str_copy(sl, sizeof sl, "Chapter end");
    pill(P_SPEED, RX, pw, IC_SPEED, sp, player_speed() < 0.99f || player_speed() > 1.01f);
    pill(P_SLEEP, RX + (pw + 12), pw, IC_MOON, slp->mode == SLEEP_OFF ? "Sleep" : sl, slp->mode != SLEEP_OFF);
    pill(P_CHAPTERS, RX + 2 * (pw + 12), pw, IC_LIST, "Chapters", false);
    pill(P_SCREEN, RX + 3 * (pw + 12), pw, IC_SCREEN, "Screen off", false);

    if (S.chapters) {
        draw_chapters();
        return;
    }
    hint(G_SQUARE, player_playing() ? "Pause" : "Play");
    hint(G_L, "Skip");
    hint(G_TRIANGLE, "Chapters");
    hint(G_SELECT, "Screen off");
    hint(g_back(), "Back");
}
