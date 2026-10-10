#include "sys.h"

#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/kernel/processmgr.h>
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
    return 0;
}

void sys_exit(void) {
    sceNetCtlTerm();
    sceNetTerm();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    free(g_net_mem);
    sceAppUtilShutdown();
}

void sys_tick(bool playing) {
    if (playing) sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
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
const char *sys_model(void) { return "PS Vita"; }
int sys_language(void) { return g_lang; }
