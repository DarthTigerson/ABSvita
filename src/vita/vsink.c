#include "vsink.h"

#include <psp2/audioout.h>
#include <string.h>

#include "dbg.h"

#define GRAIN 1024

static struct {
    int port;
    int ch;
    int fill;
    int16_t buf[GRAIN * 2];
} S = {-1, 0, 0, {0}};

static int s_open(void *ud, int rate, int ch) {
    (void)ud;
    if (S.port >= 0) sceAudioOutReleasePort(S.port);
    S.fill = 0;
    S.ch = ch;
    S.port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, GRAIN, rate,
                                 ch == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);
    if (S.port < 0) {
        dbg("audio: open %d Hz x%d failed %08x", rate, ch, (unsigned)S.port);
        return -1;
    }
    int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
    sceAudioOutSetVolume(S.port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
    dbg("audio: %d Hz x%d", rate, ch);
    return 0;
}

/* Collects frames into whole grains; each full grain blocks until the hardware takes it. */
static int s_write(void *ud, const int16_t *pcm, int frames) {
    (void)ud;
    if (S.port < 0) return -1;
    int done = 0;
    while (done < frames) {
        int k = GRAIN - S.fill;
        if (k > frames - done) k = frames - done;
        memcpy(S.buf + S.fill * S.ch, pcm + done * S.ch, (size_t)k * S.ch * sizeof(int16_t));
        S.fill += k;
        done += k;
        if (S.fill == GRAIN) {
            sceAudioOutOutput(S.port, S.buf);
            S.fill = 0;
        }
    }
    return frames;
}

static void s_close(void *ud) {
    (void)ud;
    if (S.port < 0) return;
    sceAudioOutOutput(S.port, NULL); /* wait for the last grain */
    sceAudioOutReleasePort(S.port);
    S.port = -1;
    S.fill = 0;
}

const AudioSink vita_sink = {s_open, s_write, s_close, NULL};
