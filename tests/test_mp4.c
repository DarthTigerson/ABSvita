#include <stdlib.h>

#include "mp4.h"
#include "mp4build.h"
#include "test.h"

TEST(find_moov_after_64bit_mdat) {
    Buf b = build_synth_mp4(50, 0, 1024);
    Reader r; MemReaderCtx c; reader_mem(&r, &c, b.d, b.n);
    uint64_t off = 0, size = 0;
    CHECK_EQ_INT(mp4_find_moov(&r, &off, &size), 0);
    CHECK(off > 36);
    CHECK_EQ_INT(off + size, b.n);
    CHECK(memcmp(b.d + off + 4, "moov", 4) == 0);
    free(b.d);
}

TEST(find_moov_missing) {
    uint8_t d[16] = {0, 0, 0, 16, 'f', 't', 'y', 'p'};
    Reader r; MemReaderCtx c; reader_mem(&r, &c, d, sizeof d);
    uint64_t off, size;
    CHECK(mp4_find_moov(&r, &off, &size) != 0);
    /* garbage size smaller than a header must not loop forever */
    uint8_t g[16] = {0, 0, 0, 3, 'x', 'x', 'x', 'x'};
    reader_mem(&r, &c, g, sizeof g);
    CHECK(mp4_find_moov(&r, &off, &size) != 0);
}

TEST(open_parses_audio_track) {
    Buf b = build_synth_mp4(50, 0, 1024);
    Reader r; MemReaderCtx c; reader_mem(&r, &c, b.d, b.n);
    static Mp4 m;
    CHECK_EQ_INT(mp4_open(&m, &r), 0);
    CHECK_EQ_INT(m.codec, MP4_AAC);
    CHECK_EQ_INT(m.asc_len, 2);
    CHECK_EQ_INT(m.asc[0], 0x12);
    CHECK_EQ_INT(m.asc[1], 0x10);
    CHECK_EQ_INT(m.timescale, 44100);
    CHECK_EQ_INT(m.nsamples, 50);
    CHECK_EQ_INT(m.channels, 2);
    CHECK_NEAR(mp4_duration(&m), 50 * 1024 / 44100.0, 1e-6);
    free(b.d);
}

static void check_sequential(uint32_t n, int co64) {
    Buf b = build_synth_mp4(n, co64, 1024);
    Reader r; MemReaderCtx c; reader_mem(&r, &c, b.d, b.n);
    static Mp4 m;
    CHECK_EQ_INT(mp4_open(&m, &r), 0);
    uint64_t off; uint32_t size; int bad = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (mp4_next(&m, &off, &size) != 0) { bad++; break; }
        if (off != synth_offset(&b, i) || size != synth_size(i) || b.d[off] != (uint8_t)i) bad++;
    }
    CHECK_EQ_INT(bad, 0);
    CHECK_EQ_INT(mp4_next(&m, &off, &size), 1);
    free(b.d);
}
TEST(sequential_stco) { check_sequential(40, 0); }
TEST(sequential_co64) { check_sequential(40, 1); }

TEST(seek_middle_and_end) {
    Buf b = build_synth_mp4(40, 0, 1024);
    Reader r; MemReaderCtx c; reader_mem(&r, &c, b.d, b.n);
    static Mp4 m;
    CHECK_EQ_INT(mp4_open(&m, &r), 0);
    double actual;
    uint64_t off; uint32_t size;
    /* sample 7 starts at 7*1024/44100 = 0.16254 s; ask a bit later */
    CHECK_EQ_INT(mp4_seek(&m, 0.17, &actual), 0);
    CHECK_NEAR(actual, 7 * 1024 / 44100.0, 1e-6);
    CHECK_EQ_INT(mp4_next(&m, &off, &size), 0);
    CHECK_EQ_INT(off, synth_offset(&b, 7));
    CHECK_EQ_INT(mp4_next(&m, &off, &size), 0);
    CHECK_EQ_INT(off, synth_offset(&b, 8));
    /* seek inside the first stsc run (3 spc), sample 4 = chunk 2 index 1 */
    CHECK_EQ_INT(mp4_seek(&m, 4 * 1024 / 44100.0 + 1e-4, &actual), 0);
    CHECK_EQ_INT(mp4_next(&m, &off, &size), 0);
    CHECK_EQ_INT(off, synth_offset(&b, 4));
    /* seek past the end clamps to the last sample */
    CHECK_EQ_INT(mp4_seek(&m, 999, &actual), 0);
    CHECK_EQ_INT(mp4_next(&m, &off, &size), 0);
    CHECK_EQ_INT(off, synth_offset(&b, 39));
    CHECK_EQ_INT(mp4_next(&m, &off, &size), 1);
    /* negative -> start */
    CHECK_EQ_INT(mp4_seek(&m, -3, &actual), 0);
    CHECK_NEAR(actual, 0, 1e-9);
    CHECK_EQ_INT(mp4_next(&m, &off, &size), 0);
    CHECK_EQ_INT(off, synth_offset(&b, 0));
    free(b.d);
}

TEST(tell_restore_retries_same_sample) {
    Buf b = build_synth_mp4(20, 0, 1024);
    Reader r; MemReaderCtx c; reader_mem(&r, &c, b.d, b.n);
    static Mp4 m;
    CHECK_EQ_INT(mp4_open(&m, &r), 0);
    uint64_t off, o2; uint32_t size, s2;
    for (int i = 0; i < 5; i++) mp4_next(&m, &off, &size); /* crosses the chunk 2 boundary */
    Mp4Cursor k = mp4_tell(&m);
    CHECK_EQ_INT(mp4_next(&m, &off, &size), 0);
    mp4_restore(&m, &k);
    CHECK_EQ_INT(mp4_next(&m, &o2, &s2), 0);
    CHECK_EQ_INT(o2, off);
    CHECK_EQ_INT(o2, synth_offset(&b, 5));
    CHECK_EQ_INT(mp4_next(&m, &o2, &s2), 0);
    CHECK_EQ_INT(o2, synth_offset(&b, 6));
    free(b.d);
}

TEST(huge_table_seek_near_end) {
    uint32_t n = 200000;
    Buf b = build_synth_mp4(n, 1, 1024);
    Reader r; MemReaderCtx c; reader_mem(&r, &c, b.d, b.n);
    static Mp4 m;
    CHECK_EQ_INT(mp4_open(&m, &r), 0);
    double actual;
    uint64_t off; uint32_t size;
    double t = (n - 3) * 1024 / 44100.0 + 0.001;
    CHECK_EQ_INT(mp4_seek(&m, t, &actual), 0);
    CHECK_EQ_INT(mp4_next(&m, &off, &size), 0);
    CHECK_EQ_INT(off, synth_offset(&b, n - 3));
    CHECK_EQ_INT(size, synth_size(n - 3));
    free(b.d);
}

TEST(open_at_known_moov_with_offset_reader) {
    /* simulate a moov cache file: only moov bytes, mapped at their original offsets */
    Buf b = build_synth_mp4(30, 0, 1024);
    Reader full; MemReaderCtx fc; reader_mem(&full, &fc, b.d, b.n);
    uint64_t off, size;
    CHECK_EQ_INT(mp4_find_moov(&full, &off, &size), 0);
    const char *p = "build-host/moov_cache.bin";
    FILE *f = fopen(p, "wb"); fwrite(b.d + off, 1, size, f); fclose(f);
    Reader r; FileReaderCtx c;
    CHECK_EQ_INT(reader_file_open(&r, &c, p, off, off + size), 0);
    static Mp4 m;
    CHECK_EQ_INT(mp4_open_at(&m, &r, off, size), 0);
    CHECK_EQ_INT(m.nsamples, 30);
    uint64_t so; uint32_t ss;
    CHECK_EQ_INT(mp4_seek(&m, 10 * 1024 / 44100.0, NULL), 0);
    CHECK_EQ_INT(mp4_next(&m, &so, &ss), 0);
    CHECK_EQ_INT(so, synth_offset(&b, 10));
    reader_file_close(&c);
    free(b.d);
}

TEST(asc_parse) {
    int aot, rate, ch;
    uint8_t lc[2] = {0x12, 0x10};
    CHECK_EQ_INT(aac_parse_asc(lc, 2, &aot, &rate, &ch), 0);
    CHECK_EQ_INT(aot, 2); CHECK_EQ_INT(rate, 44100); CHECK_EQ_INT(ch, 2);
    uint8_t mono22[2] = {0x13, 0x88};
    CHECK_EQ_INT(aac_parse_asc(mono22, 2, &aot, &rate, &ch), 0);
    CHECK_EQ_INT(aot, 2); CHECK_EQ_INT(rate, 22050); CHECK_EQ_INT(ch, 1);
    /* explicit HE-AAC: aot 5, core 22050 (idx 7), ch 2, ext rate 44100 (idx 4) -> reports core aot 2 */
    uint8_t he[4] = {0x2B, 0x92, 0x08, 0x00};
    CHECK_EQ_INT(aac_parse_asc(he, 4, &aot, &rate, &ch), 0);
    CHECK_EQ_INT(aot, 5); CHECK_EQ_INT(rate, 22050); CHECK_EQ_INT(ch, 2);
    CHECK(aac_parse_asc(lc, 1, &aot, &rate, &ch) != 0);
}

/* the real ffmpeg file: moov at the end + a chapter text track */
TEST(real_m4b_fixture) {
    Reader r; FileReaderCtx fc;
    if (reader_file_open(&r, &fc, "tests/fixtures/media/aac_lc.m4b", 0, 0) != 0) { printf("  (fixture missing, skipped)\n"); return; }
    static Mp4 m;
    CHECK_EQ_INT(mp4_open(&m, &r), 0);
    CHECK_EQ_INT(m.codec, MP4_AAC);
    CHECK_NEAR(mp4_duration(&m), 30.0, 0.1);
    CHECK_EQ_INT(m.channels, 2);
    uint64_t off; uint32_t size, total = 0; int n = 0;
    while (mp4_next(&m, &off, &size) == 0) { CHECK(off + size <= r.size); total += size; n++; }
    CHECK_EQ_INT(n, m.nsamples);
    CHECK(total > 300000);
    reader_file_close(&fc);
}

void suite_mp4(void) {
    RUN(find_moov_after_64bit_mdat);
    RUN(find_moov_missing);
    RUN(open_parses_audio_track);
    RUN(sequential_stco);
    RUN(sequential_co64);
    RUN(seek_middle_and_end);
    RUN(tell_restore_retries_same_sample);
    RUN(huge_table_seek_near_end);
    RUN(open_at_known_moov_with_offset_reader);
    RUN(asc_parse);
    RUN(real_m4b_fixture);
}
