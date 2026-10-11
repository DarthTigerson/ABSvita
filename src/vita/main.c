/* ABSvita entry point: Vita setup, the frame loop, shutdown. */
#include <psp2/io/devctl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <vita2d.h>

#include "abs_client.h"
#include "app.h"
#include "covers.h"
#include "dbg.h"
#include "downloads.h"
#include "engine.h"
#include "http.h"
#include "ime.h"
#include "input.h"
#include "libmodel.h"
#include "player.h"
#include "progress_queue.h"
#include "strutil.h"
#include "sys.h"
#include "vsink.h"
#include "worker.h"

#define DATA "ux0:data/ABSvita"

/* VitaSDK: a bigger heap for covers, rings and decoders */
int _newlib_heap_size_user = 192 * 1024 * 1024;

Config g_cfg;
static ProgressQueue g_pq;

const char *app_data_dir(void) { return DATA; }

void app_save_config(void) {
    bool known;
    long long off = http_clock_offset(&known);
    if (known) { g_cfg.clock_offset_ms = off; g_cfg.clock_known = true; }
    cfg_save(&g_cfg, DATA "/config.json");
}

/* free space on ux0: (sceIoDevctl 0x3001, as VitaShell does) */
static int64_t ux0_free(const char *dir) {
    (void)dir;
    struct { uint64_t max_size, free_size; uint32_t cluster_size; void *unk; } info;
    memset(&info, 0, sizeof info);
    if (sceIoDevctl("ux0:", 0x3001, NULL, 0, &info, sizeof info) < 0) return -1;
    return (int64_t)info.free_size;
}

static void save_queue(void) { pq_save(&g_pq, DATA "/progress_queue.json"); }

static void make_device_id(void) {
    if (g_cfg.device_id[0]) return;
    SceRtcTick t;
    sceRtcGetCurrentTick(&t);
    unsigned long long x = t.tick ^ 0x9E3779B97F4A7C15ULL;
    x ^= x >> 31;
    x *= 0xBF58476D1CE4E5B9ULL;
    snprintf(g_cfg.device_id, sizeof g_cfg.device_id, "absvita-%016llx", x);
}

int main(void) {
    mkdir("ux0:data", 0777);
    mkdir(DATA, 0777);
    dbg_init(DATA "/debug.log");
    sys_init();
    dbg("start: ABSvita %s", APP_VERSION);

    cfg_defaults(&g_cfg);
    cfg_load(&g_cfg, DATA "/config.json");
    make_device_id();
    if (g_cfg.clock_known) http_set_clock_offset(g_cfg.clock_offset_ms);
    pq_load(&g_pq, DATA "/progress_queue.json");
    app_save_config();

    http_init();
    absc_init(&g_cfg, app_save_config);
    g_api = worker_create("api", 256);
    g_sync = worker_create("sync", 128);
    g_covers = worker_create("covers", 256);
    if (engine_init(&vita_sink, absc_stream_auth) != 0) dbg("engine_init failed");
    dl_init(DATA "/books", ux0_free);
    player_init(&g_cfg, &g_pq, save_queue, sys_model());
    lib_init();

    vita2d_init_advanced_with_msaa(4 * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);
    vita2d_set_clear_color(C_BG);
    vita2d_set_vblank_wait(1);
    input_init(sys_enter_is_cross());
    if (gfx_init("app0:assets") != 0) dbg("fonts missing");
    covers_init(DATA "/covers");
    app_init();

    uint64_t last = sceKernelGetProcessTimeWide();
    while (!sys_quit_requested()) {
        uint64_t now = sceKernelGetProcessTimeWide();
        float dt = (now - last) / 1e6f;
        last = now;
        if (dt > 0.25f) dt = 0.25f; /* after suspend or a long frame */
        Input in;
        input_poll(&in);
        if (ime_active()) {
            Input none;
            memset(&none, 0, sizeof none);
            app_update(&none, dt); /* views poll the keyboard themselves */
        } else {
            app_update(&in, dt);
        }
        bool busy = player_playing() || dl_busy(); /* stay awake while listening or downloading */
        if (!sys_visible()) {
            /* nothing to see: skip drawing and let the CPU idle */
            sys_tick(busy, in.any || in.touching);
            sceKernelDelayThread(50 * 1000);
            continue;
        }
        vita2d_start_drawing();
        vita2d_clear_screen();
        app_draw();
        vita2d_end_drawing();
        vita2d_common_dialog_update();
        vita2d_swap_buffers();
        sys_tick(busy, in.any || in.touching);
    }

    /* the system asked us to close: save the position and go */
    dbg("quit requested");
    player_exit();
    engine_exit();
    vita2d_fini();
    sys_exit();
    return 0;
}
