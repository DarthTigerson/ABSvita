#include "app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <vita2d.h>

#include "abs_client.h"
#include "covers.h"
#include "engine.h"
#include "libmodel.h"
#include "player.h"
#include "strutil.h"
#include "sys.h"
#include "views.h"

typedef enum { MODE_LOGIN, MODE_MAIN, MODE_PLAYER } Mode;

#define MAX_STACK 8
#define MAX_HINTS 8
#define MAX_HITS 160

static struct {
    Mode mode;
    Section section;
    bool sidebar_focus;
    int sb_focus;            /* 0..SEC_COUNT-1, SEC_COUNT = mini player */
    View *stack[MAX_STACK];
    int depth;
    char toast[128];
    float toast_t;
    struct { Glyph g; const char *label; } hints[MAX_HINTS];
    int nhints;
    float sb_anim;           /* animated sidebar highlight position */
} A;

static struct { float x, y, w, h; HitFn fn; int a, b; } g_hits[MAX_HITS], g_prev_hits[MAX_HITS];
static int g_nhits, g_nprev;
static float g_tap_x, g_tap_y;
float app_tap_x(void) { return g_tap_x; }
float app_tap_y(void) { return g_tap_y; }

static const char *kSectionName[SEC_COUNT] = {"Home", "Library", "Series", "Authors", "Search", "Settings"};
static const int kSectionIcon[SEC_COUNT] = {IC_HOME, IC_BOOK, IC_SERIES, IC_USER, IC_SEARCH, IC_GEAR};

/* ---- helpers ------------------------------------------------------------------- */

Glyph g_ok(void) { return sys_enter_is_cross() ? G_CROSS : G_CIRCLE; }
Glyph g_back(void) { return sys_enter_is_cross() ? G_CIRCLE : G_CROSS; }

void clip_content(float top) {
    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(SIDEBAR_W, (int)top, SCREEN_W, CONTENT_BOTTOM);
}

void clip_off(void) { vita2d_disable_clipping(); }

void hint(Glyph g, const char *label) {
    if (A.nhints < MAX_HINTS) { A.hints[A.nhints].g = g; A.hints[A.nhints].label = label; A.nhints++; }
}

void hit_add(float x, float y, float w, float h, HitFn fn, int a, int b) {
    if (g_nhits < MAX_HITS) g_hits[g_nhits++] = (typeof(g_hits[0])){x, y, w, h, fn, a, b};
}

void app_toast(const char *msg) {
    str_copy(A.toast, sizeof A.toast, msg);
    A.toast_t = 2.5f;
}

void scroll_step(Scroll *s, float dt) {
    float k = 1 - expf(-dt * 14);
    s->y += (s->target - s->y) * k;
    if (fabsf(s->target - s->y) < 0.5f) s->y = s->target;
}

void scroll_show(Scroll *s, float top, float bottom, float view_h, float max) {
    if (top < s->target) s->target = top;
    if (bottom > s->target + view_h) s->target = bottom - view_h;
    if (s->target > max) s->target = max;
    if (s->target < 0) s->target = 0;
}

void scroll_drag(Scroll *s, float dy, float max) {
    s->target -= dy;
    if (s->target > max) s->target = max;
    if (s->target < 0) s->target = 0;
    s->y = s->target;
}

void draw_progress_bar(float x, float y, float w, float h, float frac, uint32_t fill, uint32_t track) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    gfx_rrect(x, y, w, h, h / 2, track);
    if (frac > 0) gfx_rrect(x, y, fmaxf(h, w * frac), h, h / 2, fill);
}

void draw_focus_ring(float x, float y, float w, float h, float r) {
    gfx_ring(x, y, w, h, r, 3, C_ACCENT);
}

void draw_spinner(float cx, float cy, float r, uint32_t c) {
    static float t;
    t += 0.12f;
    for (int i = 0; i < 8; i++) {
        float a = t + i * (float)M_PI / 4;
        gfx_circle(cx + cosf(a) * r, cy + sinf(a) * r, r * 0.18f, gfx_alpha(c, 0.25f + i / 10.0f));
    }
}

void draw_book_cell(const BookSummary *b, float x, float y, float cover, bool focused) {
    covers_draw(b->id, x, y, cover, 1);
    if (b->finished) {
        gfx_circle(x + cover - 16, y + 16, 12, C_ACCENT);
        gfx_icon(IC_CHECK, x + cover - 16, y + 16, 14, C_ON_ACCENT);
    } else if (b->progress > 0.005f) {
        gfx_rect(x, y + cover - 5, cover, 5, gfx_alpha(0xFF000000, 0.6f));
        gfx_rect(x, y + cover - 5, cover * b->progress, 5, C_ACCENT);
    }
    if (focused) draw_focus_ring(x, y, cover, cover, 2);
    char line[200];
    gfx_fit(F_BOLD, 15, b->title, cover, line, sizeof line);
    gfx_text(F_BOLD, 15, x, y + cover + 10, focused ? C_TEXT : gfx_mix(C_TEXT, C_SUB, 0.3f), line);
    gfx_fit(F_REG, 14, b->author, cover, line, sizeof line);
    gfx_text(F_REG, 14, x, y + cover + 32, C_SUB, line);
}

/* ---- navigation --------------------------------------------------------------- */

static void clear_stack(void) {
    while (A.depth) {
        View *v = A.stack[--A.depth];
        if (v->destroy) v->destroy(v);
    }
}

void app_push(View *v) {
    if (!v) return;
    if (A.depth == MAX_STACK) { app_pop(); }
    A.stack[A.depth++] = v;
    A.sidebar_focus = false;
}

void app_pop(void) {
    if (A.depth <= 1) { app_focus_sidebar(); return; }
    View *v = A.stack[--A.depth];
    if (v->destroy) v->destroy(v);
    View *top = A.stack[A.depth - 1];
    if (top->resume) top->resume(top);
}

void app_focus_sidebar(void) {
    A.sidebar_focus = true;
    A.sb_focus = A.section;
}

static View *section_root(Section s) {
    switch (s) {
    case SEC_HOME: return view_home_new();
    case SEC_LIBRARY: return view_library_new();
    case SEC_SERIES: return view_groups_new(GROUP_SERIES);
    case SEC_AUTHORS: return view_groups_new(GROUP_AUTHOR);
    case SEC_SEARCH: return view_search_new(false);
    case SEC_SETTINGS: return view_settings_new();
    default: return view_home_new();
    }
}

void app_set_section(Section s, bool focus_content) {
    if (s != A.section || A.depth == 0) {
        clear_stack();
        A.section = s;
        app_push(section_root(s));
    } else {
        while (A.depth > 1) app_pop();
    }
    A.sb_focus = s;
    A.sidebar_focus = !focus_content;
}

void app_open_book(int lib_index) { app_push(view_book_new(lib_index)); }

void app_play(const BookSummary *b, double start) {
    player_open(b, start);
    app_open_player();
}

void app_open_player(void) {
    A.mode = MODE_PLAYER;
    player_screen_enter();
}

void app_close_player(void) { A.mode = MODE_MAIN; }

void app_logged_out(void) {
    player_exit();
    clear_stack();
    lib_clear();
    A.mode = MODE_LOGIN;
    login_enter(NULL);
}

void app_logged_in(void) {
    A.mode = MODE_MAIN;
    lib_refresh();
    player_flush_queue();
    app_set_section(SEC_HOME, true);
}

void app_init(void) {
    memset(&A, 0, sizeof A);
    A.sb_anim = SEC_HOME;
    if (absc_have_session()) app_logged_in();
    else { A.mode = MODE_LOGIN; login_enter(NULL); }
}

/* ---- sidebar -------------------------------------------------------------------- */

#define SB_Y0 84
#define SB_ITEM_H 48
#define SB_GAP 6
#define MINI_Y 404
#define MINI_H 92

static float sb_item_y(int i) { return SB_Y0 + i * (SB_ITEM_H + SB_GAP); }

static void on_sidebar_tap(int i, int b) {
    (void)b;
    if (i == SEC_COUNT) { app_open_player(); return; }
    app_set_section((Section)i, true);
}

static void sidebar_update(const Input *in) {
    int n = SEC_COUNT + (player_book() ? 1 : 0);
    if (in->repeat & B_UP) A.sb_focus = (A.sb_focus + n - 1) % n;
    if (in->repeat & B_DOWN) A.sb_focus = (A.sb_focus + 1) % n;
    if (in->repeat & (B_UP | B_DOWN) && A.sb_focus < SEC_COUNT && A.sb_focus != (int)A.section) {
        /* moving through the sidebar previews the section */
        clear_stack();
        A.section = (Section)A.sb_focus;
        app_push(section_root(A.section));
        A.sidebar_focus = true;
    }
    if (in->down & (B_OK | B_RIGHT)) {
        if (A.sb_focus == SEC_COUNT) app_open_player();
        else app_set_section((Section)A.sb_focus, true);
    }
}

static void mini_player_draw(bool focused) {
    const BookDetail *b = player_book();
    if (!b) return;
    float x = 12, y = MINI_Y, w = SIDEBAR_W - 24, h = MINI_H;
    gfx_rrect(x, y, w, h, 12, C_PANEL2);
    if (focused) draw_focus_ring(x, y, w, h, 12);
    covers_draw(b->id, x + 12, y + 12, 52, 1);
    char line[160];
    float tx = x + 76, tw = w - 76 - 12;
    gfx_fit(F_BOLD, 14, b->title, tw, line, sizeof line);
    gfx_text(F_BOLD, 14, tx, y + 16, C_TEXT, line);
    const char *st = player_status();
    gfx_fit(F_REG, 13, st ? st : b->author, tw, line, sizeof line);
    gfx_text(F_REG, 13, tx, y + 37, st ? C_ACCENT : C_SUB, line);
    gfx_icon(player_playing() ? IC_PAUSE : IC_PLAY, tx + 9, y + 62, 16, C_TEXT);
    char t[32];
    fmt_time(player_position(), t, sizeof t);
    gfx_text_vc(F_REG, 13, tx + 24, y + 62, C_SUB, t);
    double d = player_duration();
    draw_progress_bar(x + 12, y + h - 12, w - 24, 4, d > 0 ? (float)(player_position() / d) : 0, C_ACCENT, C_LINE);
    hit_add(x, y, w, h, on_sidebar_tap, SEC_COUNT, 0);
}

static void sidebar_draw(float dt) {
    gfx_rect(0, 0, SIDEBAR_W, CONTENT_BOTTOM, C_PANEL);
    gfx_icon(IC_BOOK, 36, 44, 28, C_ACCENT);
    gfx_text_vc(F_BOLD, 24, 60, 44, C_TEXT, "ABSvita");
    float target = A.section;
    A.sb_anim += (target - A.sb_anim) * (1 - expf(-dt * 16));
    float hy = SB_Y0 + A.sb_anim * (SB_ITEM_H + SB_GAP);
    gfx_rrect(12, hy, SIDEBAR_W - 24, SB_ITEM_H, 12, C_PANEL2);
    gfx_rrect(12, hy + 12, 4, SB_ITEM_H - 24, 2, C_ACCENT);
    for (int i = 0; i < SEC_COUNT; i++) {
        float y = sb_item_y(i), cy = y + SB_ITEM_H / 2;
        bool sel = i == (int)A.section, foc = A.sidebar_focus && A.sb_focus == i;
        uint32_t c = sel || foc ? C_TEXT : C_SUB;
        gfx_icon(kSectionIcon[i], 46, cy, 22, sel ? C_ACCENT : c);
        gfx_text_vc(F_BOLD, 18, 72, cy, c, kSectionName[i]);
        if (foc) draw_focus_ring(12, y, SIDEBAR_W - 24, SB_ITEM_H, 12);
        hit_add(0, y, SIDEBAR_W, SB_ITEM_H, on_sidebar_tap, i, 0);
    }
    mini_player_draw(A.sidebar_focus && A.sb_focus == SEC_COUNT);
}

/* ---- hint bar and toast ------------------------------------------------------------ */

static void hintbar_draw(void) {
    gfx_rect(0, CONTENT_BOTTOM, SCREEN_W, HINT_H, RGBA(10, 11, 14, 255));
    gfx_rect(0, CONTENT_BOTTOM, SCREEN_W, 1, C_LINE);
    float x = SCREEN_W - 20, cy = CONTENT_BOTTOM + HINT_H / 2;
    /* right-aligned, in the order added */
    for (int i = A.nhints - 1; i >= 0; i--) {
        float lw = gfx_ink_w(F_REG, 15, A.hints[i].label);
        float gw = gfx_glyph_w(A.hints[i].g, 24);
        x -= lw;
        gfx_text_vc(F_REG, 15, x, cy, C_SUB, A.hints[i].label);
        x -= 8 + gw;
        gfx_glyph(A.hints[i].g, x + gw / 2, cy, 24);
        x -= 22;
    }
    /* left: connection and battery */
    char b[16];
    snprintf(b, sizeof b, "%d%%%s", sys_battery(), sys_charging() ? "+" : "");
    gfx_icon(sys_wifi_connected() ? IC_WIFI : IC_CLOUD_OFF, 30, cy, 18, sys_wifi_connected() ? C_SUB : C_ERROR);
    gfx_text_vc(F_REG, 15, 48, cy, C_SUB, b);
}

static void toast_draw(float dt) {
    if (A.toast_t <= 0) return;
    A.toast_t -= dt;
    float a = A.toast_t < 0.3f ? A.toast_t / 0.3f : 1;
    float w = gfx_ink_w(F_BOLD, 16, A.toast) + 48, h = 44;
    float x = (SCREEN_W - w) / 2, y = CONTENT_BOTTOM - h - 16;
    gfx_rrect(x, y, w, h, h / 2, gfx_alpha(C_PANEL2, a * 0.97f));
    gfx_text_center(F_BOLD, 16, x + w / 2, y + h / 2, gfx_alpha(C_TEXT, a), A.toast);
}

/* ---- frame ---------------------------------------------------------------------------- */

static float g_dt;

void app_update(const Input *in, float dt) {
    g_dt = dt;
    lib_tick();
    covers_tick();
    player_tick(dt);
    /* keep Continue listening current while a book plays */
    static float note_t;
    if (player_book() && (note_t += dt) > 5) {
        note_t = 0;
        lib_note_progress(player_book_id(), player_position(), player_duration());
    }
    A.nhints = 0;

    /* touch: taps hit the rectangles drawn last frame (topmost first) */
    memcpy(g_prev_hits, g_hits, sizeof(g_hits[0]) * g_nhits);
    g_nprev = g_nhits;
    g_nhits = 0;
    Input local = *in;
    if (in->tap) {
        g_tap_x = in->tap_x;
        g_tap_y = in->tap_y;
        for (int i = g_nprev - 1; i >= 0; i--) {
            typeof(g_hits[0]) *h = &g_prev_hits[i];
            if (in->tap_x >= h->x && in->tap_x < h->x + h->w && in->tap_y >= h->y && in->tap_y < h->y + h->h) {
                h->fn(h->a, h->b);
                local.tap = false;
                break;
            }
        }
    }

    switch (A.mode) {
    case MODE_LOGIN: login_update(&local, dt); return;
    case MODE_PLAYER: player_screen_update(&local, dt); return;
    case MODE_MAIN: break;
    }

    /* global shortcuts */
    if (in->down & B_START) { if (player_book() || player_phase() == PL_OPENING) app_open_player(); return; }
    if (in->down & B_SQUARE && player_book() && A.sidebar_focus) { player_toggle(); return; }
    if (in->repeat & B_L) { app_set_section((Section)((A.section + SEC_COUNT - 1) % SEC_COUNT), !A.sidebar_focus); return; }
    if (in->repeat & B_R) { app_set_section((Section)((A.section + 1) % SEC_COUNT), !A.sidebar_focus); return; }
    if (in->down & B_TRIANGLE && A.section != SEC_SEARCH) {
        clear_stack();
        A.section = SEC_SEARCH;
        app_push(view_search_new(true));
        return;
    }

    if (A.sidebar_focus) sidebar_update(&local);
    else if (A.depth) {
        View *v = A.stack[A.depth - 1];
        if (v->update) v->update(v, &local, dt);
    }
}

void app_draw(void) {
    float dt = g_dt;
    if (A.mode == MODE_LOGIN) {
        login_draw();
    } else if (A.mode == MODE_PLAYER) {
        player_screen_draw();
    } else {
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BG);
        if (A.depth) {
            View *v = A.stack[A.depth - 1];
            if (v->draw) v->draw(v, !A.sidebar_focus);
        }
        sidebar_draw(dt);
        if (A.sidebar_focus) {
            hint(G_DPAD, "Move");
            hint(g_ok(), "Open");
            if (player_book()) hint(G_SQUARE, player_playing() ? "Pause" : "Play");
            hint(G_TRIANGLE, "Search");
            if (player_book()) hint(G_START, "Player");
        }
    }
    hintbar_draw();
    toast_draw(dt);
}
