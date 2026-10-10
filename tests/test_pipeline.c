/* End-to-end audio path on the host: MP4 demux -> Helix AAC -> sonic. */
#include <stdlib.h>

#include "aacstream.h"
#include "sonic.h"
#include "test.h"

/* rising zero crossings per second on channel ch */
static double freq_of(const int16_t *pcm, int frames, int nch, int ch, int rate) {
    int crossings = 0;
    for (int i = 1; i < frames; i++)
        if (pcm[(i - 1) * nch + ch] < 0 && pcm[i * nch + ch] >= 0) crossings++;
    return frames ? crossings * (double)rate / frames : 0;
}

typedef struct { int16_t *pcm; int frames, cap, ch, rate; } Pcm;
static void pcm_add(Pcm *p, const int16_t *s, int frames, int ch) {
    if (p->frames + frames > p->cap) { p->cap = (p->frames + frames) * 2; p->pcm = realloc(p->pcm, sizeof(int16_t) * p->cap * ch); }
    memcpy(p->pcm + p->frames * ch, s, sizeof(int16_t) * frames * ch);
    p->frames += frames;
    p->ch = ch;
}

static int decode_all(const char *path, Pcm *out, double seek, double max_seconds) {
    Reader r; FileReaderCtx fc;
    if (reader_file_open(&r, &fc, path, 0, 0) != 0) return -1;
    static AacStream s;
    if (aacs_open(&s, &r, &r) != 0) { reader_file_close(&fc); return -2; }
    double actual = 0;
    if (seek > 0) aacs_seek(&s, seek, &actual);
    int16_t pcm[AACS_MAX_PCM];
    int frames, ch, rate, rc;
    memset(out, 0, sizeof *out);
    while ((rc = aacs_decode(&s, pcm, &frames, &ch, &rate)) == 0) {
        pcm_add(out, pcm, frames, ch);
        out->rate = rate;
        if (max_seconds > 0 && out->frames >= max_seconds * rate) break;
    }
    aacs_close(&s);
    reader_file_close(&fc);
    return rc < 0 ? -3 : 0;
}

/* A tables reader that fails every period-th read (a flaky network under the block cache). */
typedef struct { Reader inner; int n, period, failures; } Flaky;
static int flaky_read(void *ctx, uint64_t off, void *buf, uint32_t n) {
    Flaky *f = ctx;
    if (++f->n % f->period == 0) { f->failures++; return -1; }
    return f->inner.read_at(f->inner.ctx, off, buf, n);
}

TEST(aac_survives_flaky_tables) {
    const char *path = "tests/fixtures/media/aac_lc.m4b";
    Pcm clean;
    if (decode_all(path, &clean, 0, 0) == -1) { printf("  (fixture missing, skipped)\n"); return; }
    Reader media, inner;
    FileReaderCtx fm, ft;
    reader_file_open(&media, &fm, path, 0, 0);
    reader_file_open(&inner, &ft, path, 0, 0);
    Flaky fl = {inner, 0, 1 << 30, 0};
    Reader tables = {&fl, flaky_read, inner.size};
    static AacStream s;
    CHECK_EQ_INT(aacs_open(&s, &media, &tables), 0);
    fl.period = 2; /* from now on every other table read fails */
    int16_t pcm[AACS_MAX_PCM];
    int frames, ch, rate, rc, retries = 0;
    long total = 0;
    for (;;) {
        rc = aacs_decode(&s, pcm, &frames, &ch, &rate);
        if (rc == -2) { retries++; continue; } /* retryable: same frame next call */
        if (rc != 0) break;
        if (memcmp(pcm, clean.pcm + total * ch, sizeof(int16_t) * frames * ch) != 0) { CHECK(!"pcm differs"); break; }
        total += frames;
    }
    CHECK_EQ_INT(rc, 1); /* reached the end, not an error */
    CHECK_EQ_INT(total, clean.frames);
    CHECK(fl.failures > 0);
    CHECK(retries > 0);
    aacs_close(&s);
    reader_file_close(&fm);
    reader_file_close(&ft);
    free(clean.pcm);
}

TEST(aac_lc_full_decode) {
    Pcm p;
    int rc = decode_all("tests/fixtures/media/aac_lc.m4b", &p, 0, 0);
    if (rc == -1) { printf("  (fixture missing, skipped)\n"); return; }
    CHECK_EQ_INT(rc, 0);
    CHECK_EQ_INT(p.rate, 44100);
    CHECK_EQ_INT(p.ch, 2);
    CHECK_NEAR(p.frames / 44100.0, 30.0, 0.1);
    /* skip the first 0.5 s (encoder priming) */
    int off = 22050;
    CHECK_NEAR(freq_of(p.pcm + off * 2, p.frames - off, 2, 0, 44100), 440, 5);
    CHECK_NEAR(freq_of(p.pcm + off * 2, p.frames - off, 2, 1, 44100), 660, 5);
    free(p.pcm);
}

TEST(aac_seek_then_decode) {
    Pcm p;
    int rc = decode_all("tests/fixtures/media/aac_lc.m4b", &p, 20.0, 1.0);
    if (rc == -1) return;
    CHECK_EQ_INT(rc, 0);
    CHECK(p.frames >= 44100);
    int off = 4096; /* decoder warm-up after flush */
    CHECK_NEAR(freq_of(p.pcm + off * 2, p.frames - off, 2, 0, 44100), 440, 10);
    free(p.pcm);
}

TEST(aac_mono_22k) {
    Pcm p;
    int rc = decode_all("tests/fixtures/media/aac_mono.m4a", &p, 0, 0);
    if (rc == -1) return;
    CHECK_EQ_INT(rc, 0);
    CHECK_EQ_INT(p.ch, 1);
    CHECK_EQ_INT(p.rate, 22050);
    CHECK_NEAR(p.frames / 22050.0, 12.0, 0.1);
    CHECK_NEAR(freq_of(p.pcm + 11025, p.frames - 11025, 1, 0, 22050), 440, 5);
    free(p.pcm);
}

TEST(he_aac_sbr) {
    Pcm p;
    int rc = decode_all("tests/fixtures/media/he_aac.m4a", &p, 0, 0);
    if (rc == -1) { printf("  (HE-AAC fixture missing, skipped)\n"); return; }
    CHECK_EQ_INT(rc, 0);
    CHECK_EQ_INT(p.rate, 44100); /* SBR doubles the 22.05 kHz core */
    CHECK_NEAR(p.frames / 44100.0, 12.0, 0.2);
    CHECK_NEAR(freq_of(p.pcm + 44100 * 2, p.frames - 44100, p.ch, 0, 44100), 440, 10);
    free(p.pcm);
}

TEST(sonic_two_x_keeps_pitch) {
    Pcm p;
    if (decode_all("tests/fixtures/media/aac_lc.m4b", &p, 0, 10.0) != 0) return;
    sonicStream st = sonicCreateStream(44100, 2);
    sonicSetSpeed(st, 2.0f);
    Pcm o = {0};
    int16_t buf[4096 * 2];
    for (int i = 0; i < p.frames; i += 1024) {
        int n = p.frames - i < 1024 ? p.frames - i : 1024;
        sonicWriteShortToStream(st, p.pcm + i * 2, n);
        int k;
        while ((k = sonicReadShortFromStream(st, buf, 4096)) > 0) pcm_add(&o, buf, k, 2);
    }
    sonicFlushStream(st);
    int k;
    while ((k = sonicReadShortFromStream(st, buf, 4096)) > 0) pcm_add(&o, buf, k, 2);
    sonicDestroyStream(st);
    CHECK_NEAR((double)o.frames / p.frames, 0.5, 0.02);
    CHECK_NEAR(freq_of(o.pcm + 11025 * 2, o.frames - 11025, 2, 0, 44100), 440, 8);
    CHECK_NEAR(freq_of(o.pcm + 11025 * 2, o.frames - 11025, 2, 1, 44100), 660, 8);
    free(p.pcm);
    free(o.pcm);
}

void suite_pipeline(void) {
    RUN(aac_lc_full_decode);
    RUN(aac_survives_flaky_tables);
    RUN(aac_seek_then_decode);
    RUN(aac_mono_22k);
    RUN(he_aac_sbr);
    RUN(sonic_two_x_keeps_pitch);
}
