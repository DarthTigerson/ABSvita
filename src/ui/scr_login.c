/* Login: server, username, password (each opens the system keyboard) and Connect. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "abs_client.h"
#include "ime.h"
#include "osal.h"
#include "strutil.h"
#include "url.h"
#include "views.h"
#include "worker.h"

enum { F_SERVER, F_USER, F_PASS, F_CONNECT, F_COUNT };

#define CARD_X 470.0f
#define CARD_W 440.0f
#define ROW_H 64.0f
#define TOP 118.0f

static struct {
    int focus;
    int editing;           /* field the keyboard is open for, or -1 */
    char server[160], user[64], pass[128];
    volatile int busy;     /* connecting */
    volatile int result;   /* 0 none, 1 ok, <0 error */
    char error[128], detail[96];
} L;

typedef struct { char server[160], user[64], pass[128]; } LoginJob;

static void login_job(void *ud) {
    LoginJob *j = ud;
    int rc = absc_status(j->server);
    if (rc == ABS_OK) {
        str_copy(g_cfg.server, sizeof g_cfg.server, j->server);
        g_cfg.library_id[0] = 0;
        rc = absc_login(j->user, j->pass);
    }
    if (rc == ABS_OK) {
        L.result = 1;
    } else {
        str_copy(L.error, sizeof L.error,
                 rc == ABS_ERR_AUTH ? "Wrong username or password" :
                 rc == ABS_ERR_NET ? "Can't reach the server" : "That doesn't look like an Audiobookshelf server");
        str_copy(L.detail, sizeof L.detail, absc_last_detail());
        L.result = -1;
    }
    memset(j->pass, 0, sizeof j->pass);
    free(j);
    L.busy = 0;
}

void login_enter(const char *message) {
    memset(&L, 0, sizeof L);
    L.editing = -1;
    str_copy(L.server, sizeof L.server, g_cfg.server[0] && strcmp(g_cfg.server, CFG_DEFAULT_SERVER) ? g_cfg.server : "");
    str_copy(L.user, sizeof L.user, g_cfg.username);
    if (message) str_copy(L.error, sizeof L.error, message);
    L.focus = !L.server[0] ? F_SERVER : !L.user[0] ? F_USER : F_PASS;
}

static void connect(void) {
    if (L.busy) return;
    url_normalize(L.server, sizeof L.server);
    if (!url_valid(L.server)) {
        str_copy(L.error, sizeof L.error, "Enter the server address, e.g. http://192.168.1.10:13378");
        L.detail[0] = 0;
        L.focus = F_SERVER;
        return;
    }
    if (!L.user[0]) { str_copy(L.error, sizeof L.error, "Enter your username"); L.detail[0] = 0; L.focus = F_USER; return; }
    LoginJob *j = calloc(1, sizeof *j);
    if (!j) return;
    str_copy(j->server, sizeof j->server, L.server);
    str_copy(j->user, sizeof j->user, L.user);
    str_copy(j->pass, sizeof j->pass, L.pass);
    L.error[0] = L.detail[0] = 0;
    L.result = 0;
    L.busy = 1;
    if (!worker_post(g_api, login_job, j)) { free(j); L.busy = 0; }
}

static void edit(int f) {
    static const char *titles[] = {"Server address", "Username", "Password"};
    const char *init = f == F_SERVER ? (L.server[0] ? L.server : "http://") : f == F_USER ? L.user : "";
    if (ime_open(titles[f], init, f == F_SERVER ? 150 : 60, f == F_SERVER ? IME_URL : f == F_PASS ? IME_PASSWORD : IME_TEXT))
        L.editing = f;
}

static void activate(int f) {
    L.focus = f;
    if (f == F_CONNECT) connect();
    else edit(f);
}

static void on_tap(int f, int b) { (void)b; if (!L.busy && L.editing < 0) activate(f); }

void login_update(const Input *in, float dt) {
    (void)dt;
    if (L.result == 1) {
        L.result = 0;
        memset(L.pass, 0, sizeof L.pass);
        str_copy(g_cfg.username, sizeof g_cfg.username, L.user);
        app_save_config();
        app_logged_in();
        return;
    }
    if (L.editing >= 0) {
        char out[200];
        int r = ime_poll(out, sizeof out);
        if (r == 0) return;
        if (r == 1) {
            if (L.editing == F_SERVER) str_copy(L.server, sizeof L.server, out);
            else if (L.editing == F_USER) str_copy(L.user, sizeof L.user, out);
            else str_copy(L.pass, sizeof L.pass, out);
            L.focus = L.editing + 1; /* move on to the next field */
        }
        L.editing = -1;
        return;
    }
    if (L.busy) return;
    if (in->repeat & B_UP && L.focus > 0) L.focus--;
    if (in->repeat & B_DOWN && L.focus + 1 < F_COUNT) L.focus++;
    if (in->down & B_OK) activate(L.focus);
    if (in->down & B_START) connect();
}

static void field(int f, const char *label, int icon, const char *value, bool secret) {
    float y = TOP + f * ROW_H, h = ROW_H - 10;
    bool foc = L.focus == f;
    gfx_rrect(CARD_X, y, CARD_W, h, 12, foc ? C_PANEL2 : C_PANEL);
    if (foc) draw_focus_ring(CARD_X, y, CARD_W, h, 12);
    gfx_icon(icon, CARD_X + 30, y + h / 2, 20, foc ? C_ACCENT : C_SUB);
    gfx_text(F_BOLD, 12, CARD_X + 56, y + 11, C_SUB, label);
    char shown[200], fit[200];
    if (secret) {
        size_t n = strlen(value);
        if (n > 40) n = 40;
        for (size_t i = 0; i < n; i++) shown[i] = '*';
        shown[n] = 0;
    } else {
        str_copy(shown, sizeof shown, value);
    }
    const char *ph = f == F_SERVER ? "http://192.168.1.10:13378" : f == F_USER ? "Username" : "Password";
    gfx_fit(F_REG, 17, shown[0] ? shown : ph, CARD_W - 76, fit, sizeof fit);
    gfx_text(F_REG, 17, CARD_X + 56, y + 29, shown[0] ? C_TEXT : C_DIM, fit);
    hit_add(CARD_X, y, CARD_W, h, on_tap, f, 0);
}

void login_draw(void) {
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BG);
    gfx_vgrad(0, 0, SCREEN_W, CONTENT_BOTTOM, RGBA(40, 30, 14, 255), C_BG);
    /* brand */
    float bx = 230, by = 200;
    gfx_circle(bx, by, 62, C_ACCENT);
    gfx_icon(IC_BOOK, bx, by, 58, C_ON_ACCENT);
    gfx_text_center(F_BOLD, 40, bx, by + 104, C_TEXT, "ABSvita");
    gfx_text_center(F_REG, 17, bx, by + 140, C_SUB, "Audiobookshelf for PS Vita");

    gfx_text(F_BOLD, 26, CARD_X, 62, C_TEXT, "Sign in");
    gfx_text(F_REG, 16, CARD_X + gfx_text_w(F_BOLD, 26, "Sign in") + 12, 70, C_SUB, "to your server");
    field(F_SERVER, "SERVER", IC_SERVER, L.server, false);
    field(F_USER, "USERNAME", IC_USER, L.user, false);
    field(F_PASS, "PASSWORD", IC_LOCK, L.pass, true);

    float y = TOP + F_CONNECT * ROW_H + 4, h = 54;
    bool foc = L.focus == F_CONNECT;
    gfx_rrect(CARD_X, y, CARD_W, h, h / 2, L.busy ? C_PANEL2 : C_ACCENT);
    if (foc && !L.busy) gfx_ring(CARD_X, y, CARD_W, h, h / 2, 3, C_TEXT);
    if (L.busy) {
        draw_spinner(CARD_X + CARD_W / 2 - 64, y + h / 2, 9, C_ACCENT);
        gfx_text_center(F_BOLD, 18, CARD_X + CARD_W / 2 + 14, y + h / 2, C_TEXT, "Connecting\xE2\x80\xA6");
    } else {
        gfx_text_center(F_BOLD, 18, CARD_X + CARD_W / 2, y + h / 2, C_ON_ACCENT, "Connect");
    }
    hit_add(CARD_X, y, CARD_W, h, on_tap, F_CONNECT, 0);
    if (L.error[0]) {
        gfx_wrap(F_BOLD, 15, CARD_X, y + h + 18, CARD_W, 21, 2, C_ERROR, L.error);
        if (L.detail[0]) gfx_text(F_REG, 13, CARD_X, y + h + 62, C_DIM, L.detail);
    }
    hint(G_DPAD, "Move");
    hint(g_ok(), L.focus == F_CONNECT ? "Connect" : "Edit");
    hint(G_START, "Connect");
}
