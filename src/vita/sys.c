#include "sys.h"

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/power.h>
#include <psp2/sysmodule.h>
#include <stdlib.h>
#include <string.h>

#include "dbg.h"

static bool g_cross = true, g_screen = true;
static int g_lang = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
static void *g_net_mem;
static volatile bool g_awake, g_fg = true;
static volatile unsigned g_last_active; /* seconds; 32 bits so the tick thread never reads it torn */

/* ATTRIBUTE 0x80000 turns off system suspend, so standby when idle ourselves */
#define IDLE_SUSPEND_S (10 * 60)

static unsigned now_s(void) { return (unsigned)(sceKernelGetProcessTimeWide() / 1000000); }

static int power_cb(int id, int count, int info, void *common) {
    (void)id; (void)count; (void)common;
    dbg("power event %08x", (unsigned)info);
    return 0;
}

/* Ticks from its own thread: the frame loop may stall while the display is off. */
static int tick_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    SceUID cb = sceKernelCreateCallback("power_cb", 0, power_cb, NULL);
    if (cb >= 0) scePowerRegisterCallback(cb);
    for (;;) {
        if (g_awake) {
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
        } else if (now_s() - g_last_active > IDLE_SUSPEND_S) {
            dbg("idle: suspend");
            g_last_active = now_s();
            scePowerRequestSuspend();
        }
        sceKernelDelayThreadCB(10 * 1000 * 1000);
    }
    return 0;
}

int sys_init(void) {
    /* headroom for decoding and drawing */
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    memset(&init, 0, sizeof init);
    memset(&boot, 0, sizeof boot);
    sceAppUtilInit(&init, &boot);
    int v = 0;
    if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &v) == 0) g_cross = v == SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
    if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &v) == 0) g_lang = v;
    SceCommonDialogConfigParam cfg;
    sceCommonDialogConfigParamInit(&cfg);
    cfg.language = g_lang;
    cfg.enterButtonAssign = g_cross ? SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS : SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE;
    sceCommonDialogSetConfigParam(&cfg);

    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    SceNetInitParam np;
    int size = 1 << 20;
    g_net_mem = malloc(size);
    np.memory = g_net_mem;
    np.size = size;
    np.flags = 0;
    int r = sceNetInit(&np);
    if (r < 0 && r != (int)SCE_NET_ERROR_EBUSY) dbg("sceNetInit %08x", (unsigned)r);
    r = sceNetCtlInit();
    if (r < 0) dbg("sceNetCtlInit %08x", (unsigned)r);

    /* the BGM port keeps our audio running with the display off (as ElevenMPV does) */
    r = sceAppMgrAcquireBgmPort();
    if (r < 0) dbg("sceAppMgrAcquireBgmPort %08x", (unsigned)r);
    g_last_active = now_s();
    SceUID th = sceKernelCreateThread("power_tick", tick_thread, 0x10000100, 0x4000, 0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    return 0;
}

void sys_exit(void) {
    sceAppMgrReleaseBgmPort();
    sceNetCtlTerm();
    sceNetTerm();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    free(g_net_mem);
    sceAppUtilShutdown();
}

/* SceAppMgr's application event queue (missing from the SDK in our image) */
typedef struct { int event; SceUID app_id; char param[56]; } AppMgrEvent;
int sceAppMgrReceiveEventNum(int *num);
int sceAppMgrReceiveEvent(AppMgrEvent *ev);

static volatile bool g_quit;

/* ON_ACTIVATE, ON_DEACTIVATE and ON_RESUME arrive on both queues; older headers lack the names */
static void on_event(int e) {
    /* closing from LiveArea: with ATTRIBUTE 0x8 the system waits for us to go,
     * and ignoring it leaves the shell hung */
    if (e == 0x20000001) g_quit = true;
    if (e == 0x10000002) g_fg = false;
    if (e == 0x10000001 || e == 0x10000003) {
        g_fg = true;
        g_screen = true;
    }
}

static void poll_events(void) {
    /* leaving for LiveArea or another app, and the PS button or a wake from
     * standby turning the display back on behind our back */
    int n = 0;
    if (sceAppMgrReceiveEventNum(&n) == 0) {
        while (n-- > 0) {
            AppMgrEvent ev;
            if (sceAppMgrReceiveEvent(&ev) < 0) break;
            dbg("appmgr event %08x", (unsigned)ev.event);
            on_event(ev.event);
        }
    }
    SceAppMgrAppState st;
    memset(&st, 0, sizeof st);
    if (_sceAppMgrGetAppState(&st, sizeof st, 0x03570011) < 0) return;
    while (st.systemEventNum-- > 0) {
        SceAppMgrSystemEvent ev;
        if (sceAppMgrReceiveSystemEvent(&ev) < 0) break;
        dbg("system event %08x", (unsigned)ev.systemEvent);
        on_event(ev.systemEvent);
    }
}

void sys_tick(bool busy, bool input) {
    g_awake = busy;
    if (busy || input) g_last_active = now_s();
    /* events are IPC to the shell: ten polls a second is plenty */
    static SceUInt64 next;
    SceUInt64 now = sceKernelGetProcessTimeWide();
    if (now < next) return;
    next = now + 100 * 1000;
    poll_events();
}

bool sys_enter_is_cross(void) { return g_cross; }
int sys_battery(void) { int p = scePowerGetBatteryLifePercent(); return p < 0 ? 100 : p; }
bool sys_charging(void) { return scePowerIsBatteryCharging() > 0; }

bool sys_wifi_connected(void) {
    int state = 0;
    return sceNetCtlInetGetState(&state) == 0 && state == SCE_NETCTL_STATE_CONNECTED;
}

void sys_screen(bool on) {
    if (on == g_screen) return;
    g_screen = on;
    if (on) scePowerRequestDisplayOn();
    else scePowerRequestDisplayOff();
}

bool sys_screen_on(void) { return g_screen; }
bool sys_quit_requested(void) { return g_quit; }
bool sys_visible(void) { return g_screen && g_fg; }
const char *sys_model(void) { return "PS Vita"; }
int sys_language(void) { return g_lang; }
