#include "aacstream.h"

#include <stdlib.h>
#include <string.h>

#include "aacdec.h"

static int setup_decoder(AacStream *s) {
    int aot = 2, rate = (int)s->mp4.sample_rate, ch = (int)s->mp4.channels;
    if (s->mp4.asc_len >= 2) aac_parse_asc(s->mp4.asc, s->mp4.asc_len, &aot, &rate, &ch);
    if (aot != 2 && aot != 5 && aot != 29) return -1; /* LC, SBR, PS(decoded as SBR) */
    if (ch < 1 || ch > 2) ch = s->mp4.channels >= 1 && s->mp4.channels <= 2 ? (int)s->mp4.channels : 2;
    AACFrameInfo fi;
    memset(&fi, 0, sizeof fi);
    fi.nChans = ch;
    fi.sampRateCore = rate;
    fi.profile = AAC_PROFILE_LC;
    return AACSetRawBlockParams(s->dec, 0, &fi) == 0 ? 0 : -1;
}

int aacs_open(AacStream *s, const Reader *media, const Reader *tables) {
    return aacs_open_at(s, media, tables, 0, 0);
}

int aacs_open_at(AacStream *s, const Reader *media, const Reader *tables, uint64_t moov_off, uint64_t moov_size) {
    memset(s, 0, sizeof *s);
    s->media = media;
    int rc = moov_size ? mp4_open_at(&s->mp4, tables, moov_off, moov_size) : mp4_open(&s->mp4, tables);
    if (rc != 0 || s->mp4.codec != MP4_AAC) return -1;
    s->dec = AACInitDecoder();
    if (!s->dec) return -1;
    s->frame_cap = 8192;
    s->frame = malloc(s->frame_cap);
    if (!s->frame || setup_decoder(s) != 0) { aacs_close(s); return -1; }
    return 0;
}

int aacs_seek(AacStream *s, double t, double *actual) {
    if (mp4_seek(&s->mp4, t, actual) != 0) return -1;
    AACFlushCodec(s->dec);
    return 0;
}

int aacs_decode(AacStream *s, int16_t *pcm, int *frames, int *channels, int *rate) {
    uint64_t off;
    uint32_t size;
    Mp4Cursor mark = mp4_tell(&s->mp4);
    int rc = mp4_next(&s->mp4, &off, &size);
    if (rc != 0) return rc > 0 ? 1 : -1;
    if (size > s->frame_cap) {
        if (size > 1 << 20) return -1;
        uint8_t *n = realloc(s->frame, size);
        if (!n) return -1;
        s->frame = n;
        s->frame_cap = size;
    }
    int rr = reader_read_full(s->media, off, s->frame, size);
    if (rr != 0) {
        mp4_restore(&s->mp4, &mark); /* the same frame is retried on the next call */
        return rr == -3 ? -3 : -2;
    }
    unsigned char *p = s->frame;
    int left = (int)size;
    int err = AACDecode(s->dec, &p, &left, pcm);
    AACFrameInfo fi;
    if (err == 0) {
        AACGetLastFrameInfo(s->dec, &fi);
        s->out_rate = fi.sampRateOut;
        s->out_ch = fi.nChans;
        s->errors = 0;
        *frames = fi.nChans ? fi.outputSamps / fi.nChans : 0;
    } else {
        /* bad frame: emit one frame of silence to keep the timeline intact */
        if (++s->errors > 50) return -1;
        int ch = s->out_ch ? s->out_ch : (int)s->mp4.channels;
        if (ch < 1 || ch > 2) ch = 2;
        int n = s->out_rate && s->mp4.sample_rate && s->out_rate > (int)s->mp4.sample_rate ? 2048 : 1024;
        memset(pcm, 0, (size_t)n * ch * sizeof(int16_t));
        s->out_ch = ch;
        if (!s->out_rate) s->out_rate = (int)s->mp4.sample_rate;
        *frames = n;
    }
    *channels = s->out_ch;
    *rate = s->out_rate;
    return 0;
}

void aacs_close(AacStream *s) {
    if (s->dec) AACFreeDecoder(s->dec);
    free(s->frame);
    s->dec = NULL;
    s->frame = NULL;
}
