#include "mp3.h"

#include <string.h>

uint32_t mp3_id3_size(const uint8_t *b, uint32_t n) {
    if (n < 10 || memcmp(b, "ID3", 3) != 0) return 0;
    if ((b[6] | b[7] | b[8] | b[9]) & 0x80) return 0;
    uint32_t sz = (uint32_t)b[6] << 21 | (uint32_t)b[7] << 14 | (uint32_t)b[8] << 7 | b[9];
    sz += 10;
    if (b[5] & 0x10) sz += 10; /* footer */
    return sz;
}

static const int kBr[2][16] = {
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}, /* MPEG1 L3 */
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},     /* MPEG2/2.5 L3 */
};
static const int kSr[3][3] = {{44100, 48000, 32000}, {22050, 24000, 16000}, {11025, 12000, 8000}};

int mp3_parse_header(const uint8_t *h, Mp3Frame *f) {
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return -1;
    int ver = (h[1] >> 3) & 3; /* 0: 2.5, 2: 2, 3: 1 */
    int layer = (h[1] >> 1) & 3;
    if (ver == 1 || layer != 1) return -1; /* only layer III */
    int bri = h[2] >> 4, sri = (h[2] >> 2) & 3, pad = (h[2] >> 1) & 1;
    if (bri == 0 || bri == 15 || sri == 3) return -1;
    f->version = ver == 3 ? 1 : ver == 2 ? 2 : 25;
    f->layer = 3;
    f->bitrate = kBr[f->version == 1 ? 0 : 1][bri];
    f->sample_rate = kSr[f->version == 1 ? 0 : f->version == 2 ? 1 : 2][sri];
    f->channels = ((h[3] >> 6) == 3) ? 1 : 2;
    f->samples = f->version == 1 ? 1152 : 576;
    f->frame_bytes = (f->samples / 8) * f->bitrate * 1000 / f->sample_rate + pad;
    return 0;
}

int mp3_probe(const Reader *r, Mp3Info *info) {
    memset(info, 0, sizeof *info);
    uint8_t buf[4096];
    uint32_t n = r->size < sizeof buf ? (uint32_t)r->size : sizeof buf;
    if (reader_read_full(r, 0, buf, 10 < n ? 10 : n) != 0) return -1;
    uint64_t start = mp3_id3_size(buf, 10);
    /* find the first valid frame header (confirm with the following one) */
    for (int attempt = 0; attempt < 16; attempt++) {
        if (start >= r->size) return -1;
        uint32_t want = r->size - start < sizeof buf ? (uint32_t)(r->size - start) : sizeof buf;
        if (reader_read_full(r, start, buf, want) != 0) return -1;
        for (uint32_t i = 0; i + 4 <= want; i++) {
            Mp3Frame f, g;
            if (mp3_parse_header(buf + i, &f) != 0) continue;
            uint32_t next = i + f.frame_bytes;
            if (next + 4 <= want && mp3_parse_header(buf + next, &g) != 0) continue;
            uint64_t fstart = start + i;
            info->sample_rate = f.sample_rate;
            info->channels = f.channels;
            info->bitrate = f.bitrate;
            info->audio_start = fstart;
            info->audio_bytes = r->size - fstart;
            /* Xing / Info header inside the first frame */
            int side = f.version == 1 ? (f.channels == 1 ? 17 : 32) : (f.channels == 1 ? 9 : 17);
            const uint8_t *x = buf + i + 4 + side;
            if (i + 4 + side + 120 <= want && (!memcmp(x, "Xing", 4) || !memcmp(x, "Info", 4))) {
                uint32_t flags = rd_be32(x + 4);
                const uint8_t *p = x + 8;
                uint32_t frames = 0, bytes = 0;
                if (flags & 1) { frames = rd_be32(p); p += 4; }
                if (flags & 2) { bytes = rd_be32(p); p += 4; }
                if (flags & 4) { memcpy(info->toc, p, 100); info->has_toc = true; }
                if (frames) info->duration = (double)frames * f.samples / f.sample_rate;
                info->audio_start = fstart + f.frame_bytes;
                info->audio_bytes = bytes > (uint32_t)f.frame_bytes ? bytes - f.frame_bytes : r->size - info->audio_start;
                if (info->audio_start + info->audio_bytes > r->size) info->audio_bytes = r->size - info->audio_start;
                if (frames && info->duration > 0) info->bitrate = (int)(info->audio_bytes * 8 / info->duration / 1000);
            }
            if (info->duration <= 0 && info->bitrate > 0) info->duration = info->audio_bytes * 8.0 / (info->bitrate * 1000.0);
            return 0;
        }
        start += want > 3 ? want - 3 : want;
    }
    return -1;
}

uint64_t mp3_offset_for_time(const Mp3Info *info, double t) {
    if (t <= 0 || info->duration <= 0) return info->audio_start;
    double frac = t / info->duration;
    if (frac >= 1) return info->audio_start + info->audio_bytes;
    double pos;
    if (info->has_toc) {
        double pct = frac * 100;
        int a = (int)pct;
        if (a > 99) a = 99;
        double fa = info->toc[a];
        double fb = a < 99 ? info->toc[a + 1] : 256;
        pos = (fa + (fb - fa) * (pct - a)) / 256.0;
    } else {
        pos = frac;
    }
    return info->audio_start + (uint64_t)(pos * info->audio_bytes);
}
