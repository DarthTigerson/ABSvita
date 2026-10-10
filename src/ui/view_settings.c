/* Settings: account, playback defaults, library refresh, about. */
#include <stdio.h>
#include <stdlib.h>

#include "abs_client.h"
#include "libmodel.h"
#include "player.h"
#include "strutil.h"
#include "views.h"

#define ROW_H 64.0f
#define TOP 80.0f

enum { S_SERVER, S_LOGOUT, S_SKIP_BACK, S_SKIP_FWD, S_SPEED, S_SYNC, S_REFRESH, S_ABOUT, S_COUNT };

typedef struct {
    int focus;
    bool confirm_logout;
    Scroll sc;
} Settings;

static Settings *g_set;

static int next_sync(int s) {
    static const int v[] = {10, 15, 30, 60};
    for (int i = 0; i < 4; i++) if (s < v[i]) return v[i];
    return v[0];
}

static void activate(Settings *st, int i) {
    if (i != S_LOGOUT) st->confirm_logout = false;
    switch (i) {
    case S_LOGOUT:
        if (!st->confirm_logout) { st->confirm_logout = true; return; }
        absc_logout();
        app_logged_out();
        return;
    case S_SKIP_BACK: g_cfg.skip_back = cfg_next_skip(g_cfg.skip_back); break;
    case S_SKIP_FWD: g_cfg.skip_fwd = cfg_next_skip(g_cfg.skip_fwd); break;
    case S_SPEED: player_set_speed(cfg_next_speed(g_cfg.speed)); break;
    case S_SYNC: g_cfg.sync_interval = next_sync(g_cfg.sync_interval); break;
    case S_REFRESH: lib_refresh(); app_toast("Refreshing your library\xE2\x80\xA6"); return;
    default: return;
    }
    app_save_config();
}

static void on_tap(int i, int b) {
    (void)b;
    if (!g_set) return;
    g_set->focus = i;
    activate(g_set, i);
}

static void label(int i, char *name, char *val, int n) {
    switch (i) {
    case S_SERVER: snprintf(name, n, "Server"); snprintf(val, n, "%s", g_cfg.server); break;
    case S_LOGOUT: snprintf(name, n, g_set && g_set->confirm_logout ? "Press again to log out" : "Log out");
                   snprintf(val, n, "%s", g_cfg.username); break;
    case S_SKIP_BACK: snprintf(name, n, "Skip back"); snprintf(val, n, "%d s", g_cfg.skip_back); break;
    case S_SKIP_FWD: snprintf(name, n, "Skip forward"); snprintf(val, n, "%d s", g_cfg.skip_fwd); break;
    case S_SPEED: snprintf(name, n, "Playback speed"); snprintf(val, n, "%g\xC3\x97", g_cfg.speed); break;
    case S_SYNC: snprintf(name, n, "Sync progress every"); snprintf(val, n, "%d s", g_cfg.sync_interval); break;
    case S_REFRESH: snprintf(name, n, "Refresh library"); snprintf(val, n, "%d books", lib_count()); break;
    case S_ABOUT: snprintf(name, n, "About"); snprintf(val, n, "ABSvita %s", APP_VERSION); break;
    }
}

static const int kIcon[S_COUNT] = {IC_SERVER, IC_USER, IC_BACK, IC_FWD, IC_SPEED, IC_CLOCK, IC_DOWNLOAD, IC_BOOK};

static void settings_update(View *v, const Input *in, float dt) {
    Settings *st = v->data;
    scroll_step(&st->sc, dt);
    if (in->repeat & B_UP && st->focus > 0) { st->focus--; st->confirm_logout = false; }
    if (in->repeat & B_DOWN && st->focus + 1 < S_COUNT) { st->focus++; st->confirm_logout = false; }
    if (in->repeat & B_LEFT || in->down & B_BACK) { st->confirm_logout = false; app_focus_sidebar(); return; }
    if (in->down & B_OK) activate(st, st->focus);
    float max = S_COUNT * ROW_H - (CONTENT_BOTTOM - TOP) + 8;
    scroll_show(&st->sc, st->focus * ROW_H, (st->focus + 1) * ROW_H, CONTENT_BOTTOM - TOP, max > 0 ? max : 0);
}

static void settings_draw(View *v, bool focused) {
    Settings *st = v->data;
    g_set = st;
    gfx_text(F_BOLD, 30, CX, 26, C_TEXT, "Settings");
    clip_content(TOP - 6);
    for (int i = 0; i < S_COUNT; i++) {
        float y = TOP + i * ROW_H - st->sc.y, h = ROW_H - 8;
        bool foc = focused && st->focus == i;
        gfx_rrect(CX, y, CW, h, 12, foc ? C_PANEL2 : C_PANEL);
        if (foc) draw_focus_ring(CX, y, CW, h, 12);
        char name[96], val[200], fit[200];
        label(i, name, val, sizeof val);
        bool danger = i == S_LOGOUT && st->confirm_logout;
        gfx_icon(kIcon[i], CX + 32, y + h / 2, 22, danger ? C_ERROR : C_SUB);
        gfx_text_vc(F_BOLD, 18, CX + 60, y + h / 2, danger ? C_ERROR : C_TEXT, name);
        float nw = gfx_text_w(F_BOLD, 18, name);
        gfx_fit(F_REG, 16, val, CW - 60 - nw - 48, fit, sizeof fit);
        gfx_text_right_vc(F_REG, 16, CX + CW - 20, y + h / 2, C_SUB, fit);
        hit_add(CX, y, CW, h, on_tap, i, 0);
    }
    clip_off();
    if (focused) {
        hint(G_DPAD, "Move");
        if (st->focus != S_SERVER && st->focus != S_ABOUT) hint(g_ok(), st->focus == S_LOGOUT ? "Log out" : "Change");
        hint(g_back(), "Back");
    }
}

static void settings_destroy(View *v) {
    if (g_set == v->data) g_set = NULL;
    free(v->data);
    free(v);
}

View *view_settings_new(void) {
    View *v = calloc(1, sizeof *v);
    Settings *s = calloc(1, sizeof *s);
    if (!v || !s) { free(v); free(s); return NULL; }
    v->update = settings_update;
    v->draw = settings_draw;
    v->destroy = settings_destroy;
    v->data = s;
    return v;
}
