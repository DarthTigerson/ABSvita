#include "input.h"

#include <math.h>
#include <stdlib.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/touch.h>
#include <string.h>

#define REPEAT_DELAY_US 320000
#define REPEAT_RATE_US 70000
#define STICK_DEAD 56
#define TAP_SLOP 18.0f

static bool g_cross = true;
static uint32_t g_prev;
static uint64_t g_since[32], g_next[32];
static struct { bool down; float x, y, sx, sy; uint64_t t0; bool moved; } T;

#ifdef ABSVITA_AUTOPILOT
/* Test builds only: scripted presses from ux0:data/ABSvita/autopilot.txt, one
 * "<wait ms> <button>" per line (button: up down left right ok back square
 * triangle l r start select none). Each step is logged so screenshots can be timed. */
#include <stdio.h>
#include "dbg.h"
static struct { int n, i; int wait[256]; uint32_t btn[256]; uint64_t next, release; uint32_t cur; } AP;
static void autopilot_load(void) {
    FILE *f = fopen("ux0:data/ABSvita/autopilot.txt", "r");
    if (!f) return;
    char name[32];
    int w;
    static const struct { const char *n; uint32_t b; } map[] = {
        {"up", B_UP}, {"down", B_DOWN}, {"left", B_LEFT}, {"right", B_RIGHT}, {"ok", B_OK}, {"back", B_BACK},
        {"square", B_SQUARE}, {"triangle", B_TRIANGLE}, {"l", B_L}, {"r", B_R}, {"start", B_START},
        {"select", B_SELECT}, {"none", 0}};
    while (AP.n < 256 && fscanf(f, "%d %31s", &w, name) == 2) {
        AP.wait[AP.n] = w;
        AP.btn[AP.n] = 0;
        for (unsigned k = 0; k < sizeof map / sizeof map[0]; k++) if (!strcmp(map[k].n, name)) AP.btn[AP.n] = map[k].b;
        AP.n++;
    }
    fclose(f);
    dbg("autopilot: %d steps", AP.n);
}
static uint32_t autopilot(uint64_t now) {
    if (AP.i > AP.n) return 0;
    if (!AP.next) AP.next = now + 1000000;
    if (AP.cur && now >= AP.release) AP.cur = 0;
    if (!AP.cur && AP.i < AP.n && now >= AP.next + (uint64_t)AP.wait[AP.i] * 1000) {
        AP.cur = AP.btn[AP.i];
        AP.release = now + 100000;
        AP.next = now + 100000;
        dbg("autopilot: step %d", AP.i);
        AP.i++;
        if (AP.i == AP.n) dbg("autopilot: done");
    }
    return AP.cur;
}
#endif

void input_init(bool enter_is_cross) {
#ifdef ABSVITA_AUTOPILOT
    autopilot_load();
#endif
    g_cross = enter_is_cross;
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
}

void input_poll(Input *in) {
    memset(in, 0, sizeof *in);
    SceCtrlData pad;
    memset(&pad, 0, sizeof pad);
    sceCtrlPeekBufferPositive(0, &pad, 1);
    uint32_t b = 0, p = pad.buttons;
    if (p & SCE_CTRL_UP) b |= B_UP;
    if (p & SCE_CTRL_DOWN) b |= B_DOWN;
    if (p & SCE_CTRL_LEFT) b |= B_LEFT;
    if (p & SCE_CTRL_RIGHT) b |= B_RIGHT;
    if (p & SCE_CTRL_CROSS) b |= g_cross ? B_OK : B_BACK;
    if (p & SCE_CTRL_CIRCLE) b |= g_cross ? B_BACK : B_OK;
    if (p & SCE_CTRL_SQUARE) b |= B_SQUARE;
    if (p & SCE_CTRL_TRIANGLE) b |= B_TRIANGLE;
    if (p & SCE_CTRL_LTRIGGER) b |= B_L;
    if (p & SCE_CTRL_RTRIGGER) b |= B_R;
    if (p & SCE_CTRL_START) b |= B_START;
    if (p & SCE_CTRL_SELECT) b |= B_SELECT;
    int lx = pad.lx - 128, ly = pad.ly - 128;
    if (abs(lx) > STICK_DEAD || abs(ly) > STICK_DEAD) {
        if (abs(lx) > abs(ly)) b |= lx < 0 ? B_LEFT : B_RIGHT;
        else b |= ly < 0 ? B_UP : B_DOWN;
    }
    uint64_t now = sceKernelGetProcessTimeWide();
#ifdef ABSVITA_AUTOPILOT
    b |= autopilot(now);
#endif
    in->held = b;
    in->down = b & ~g_prev;
    in->repeat = in->down;
    for (int i = 0; i < 12; i++) {
        uint32_t m = 1u << i;
        if (in->down & m) { g_since[i] = now; g_next[i] = now + REPEAT_DELAY_US; }
        else if ((b & m) && (m & (B_UP | B_DOWN | B_LEFT | B_RIGHT | B_L | B_R)) && now >= g_next[i]) {
            in->repeat |= m;
            g_next[i] = now + REPEAT_RATE_US;
        }
    }
    in->any = in->down != 0;
    g_prev = b;

    SceTouchData td;
    memset(&td, 0, sizeof td);
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1);
    if (td.reportNum > 0) {
        float x = td.report[0].x / 2.0f, y = td.report[0].y / 2.0f;
        if (!T.down) {
            T.down = true;
            T.sx = T.x = x;
            T.sy = T.y = y;
            T.t0 = now;
            T.moved = false;
        }
        if (!T.moved && hypotf(x - T.sx, y - T.sy) > TAP_SLOP) T.moved = true;
        if (T.moved) {
            in->dragging = true;
            in->drag_dx = x - T.x;
            in->drag_dy = y - T.y;
        }
        T.x = x;
        T.y = y;
        in->touching = true;
        in->tx = x;
        in->ty = y;
        in->start_x = T.sx;
        in->start_y = T.sy;
    } else if (T.down) {
        T.down = false;
        if (!T.moved && now - T.t0 < 500000) {
            in->tap = true;
            in->tap_x = T.sx;
            in->tap_y = T.sy;
        }
        in->any = true;
    }
    if (in->touching && !T.moved && now - T.t0 < 20000) in->any = true;
}
