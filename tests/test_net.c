/* Network pipeline tests against tools/mock_abs.py. Skipped unless MOCK_ABS
 * (the server's base URL) is set; `make -f Makefile.host nettest` does that. */
#include <stdlib.h>

#include "abs_client.h"
#include "http.h"
#include "httpsrc.h"
#include "stream.h"
#include "test.h"

static Config g_cfg;
static const char *g_media = "tests/media";

static void post(const char *path) {
    char url[256];
    snprintf(url, sizeof url, "%s%s", getenv("MOCK_ABS"), path);
    HttpResp r;
    if (http_request("POST", url, NULL, "{}", 2, 0, &r) == 0) http_resp_free(&r);
}

static uint8_t *slurp(const char *name, long *len) {
    char path[256];
    snprintf(path, sizeof path, "%s/%s", g_media, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*len);
    if (b && fread(b, 1, *len, f) != (size_t)*len) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static void source_for(const char *item, int track, TrackSource *src) {
    PlaySession s;
    memset(src, 0, sizeof *src);
    if (absc_play(item, "Test", &s) != ABS_OK) return;
    const Track *t = &s.book.tracks[track];
    absc_url(t->content_url, src->url, sizeof src->url);
    src->size = t->size;
    snprintf(src->ext, sizeof src->ext, "%s", t->ext);
    snprintf(src->codec, sizeof src->codec, "%s", t->codec);
    book_detail_free(&s.book);
}

TEST(login_and_play) {
    CHECK_EQ_INT(absc_status(g_cfg.server), ABS_OK);
    CHECK_EQ_INT(absc_login("root", "nope"), ABS_ERR_AUTH);
    CHECK_EQ_INT(absc_login("root", "pw"), ABS_OK);
    CHECK(g_cfg.refresh[0] != 0);
    PlaySession s;
    CHECK_EQ_INT(absc_play("li-mp3", "Test", &s), ABS_OK);
    CHECK_EQ_INT(s.book.ntracks, 2);
    CHECK_STR(s.book.tracks[1].ext, ".mp3");
    CHECK_NEAR(s.book.tracks[1].start_offset, s.book.tracks[0].duration, 0.01);
    book_detail_free(&s.book);
    BookSummary *books = NULL;
    int n = 0;
    CHECK_EQ_INT(absc_fetch_library(&books, &n, NULL), ABS_OK);
    CHECK(n >= 3);
    CHECK_STR(books[0].series, "Earthsea #1");
    free(books);
}

TEST(token_refresh) {
    post("/__test/expire");
    BookDetail d;
    CHECK_EQ_INT(absc_fetch_item("li-fast", &d), ABS_OK);
    CHECK_EQ_INT(d.ntracks, 1);
    book_detail_free(&d);
}

static int read_all(HttpSrc *hs, uint8_t *out, long len) {
    Reader r = httpsrc_reader(hs);
    long got = 0;
    while (got < len) {
        int k = r.read_at(r.ctx, got, out + got, 65536);
        if (k <= 0) return -1;
        got += k;
    }
    return r.read_at(r.ctx, got, out, 16) == 0 ? 0 : -2;
}

TEST(ring_source) {
    long len = 0;
    uint8_t *want = slurp("faststart.m4a", &len);
    CHECK(want != NULL);
    if (!want) return;
    TrackSource src;
    source_for("li-fast", 0, &src);
    uint8_t *got = malloc(len);
    HttpSrc *hs = httpsrc_open(src.url, src.size, absc_stream_auth);
    CHECK_EQ_INT(read_all(hs, got, len), 0);
    CHECK(memcmp(got, want, len) == 0);
    httpsrc_close(hs);

    /* the server cuts the connection part-way: the source reconnects and resumes */
    post("/__test/drop?after=300000");
    memset(got, 0, len);
    hs = httpsrc_open(src.url, src.size, absc_stream_auth);
    CHECK_EQ_INT(read_all(hs, got, len), 0);
    CHECK(memcmp(got, want, len) == 0);
    httpsrc_close(hs);

    /* a stale token mid-session: streams refresh it themselves */
    post("/__test/expire");
    hs = httpsrc_open(src.url, src.size, absc_stream_auth);
    CHECK_EQ_INT(read_all(hs, got, len), 0);
    CHECK(memcmp(got, want, len) == 0);
    httpsrc_close(hs);
    free(got);
    free(want);
}

/* Decodes until EOF; returns seconds of audio. */
static double decode_all(Stream *st) {
    static int16_t pcm[STREAM_MAX_FRAMES * 2];
    double secs = 0;
    for (;;) {
        int ch, rate;
        int n = stream_read(st, pcm, STREAM_MAX_FRAMES, &ch, &rate);
        if (n == STREAM_EOF) break;
        if (n == STREAM_IO) continue;
        if (n < 0) return -1;
        secs += (double)n / rate;
    }
    return secs;
}

/* Zero crossings per second over `seconds` of decoded audio (= 2 x tone frequency). */
static double tone_hz(Stream *st, double seconds, double skip) {
    static int16_t pcm[STREAM_MAX_FRAMES * 2];
    long crossings = 0, frames = 0, skipped = 0;
    int prev = 0, rate = 44100, ch = 1;
    while (frames < (long)(seconds * rate)) {
        int n = stream_read(st, pcm, STREAM_MAX_FRAMES, &ch, &rate);
        if (n == STREAM_IO) continue;
        if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            if (skipped < (long)(skip * rate)) { skipped++; continue; }
            int v = pcm[i * ch];
            if ((v >= 0) != (prev >= 0)) crossings++;
            prev = v;
            frames++;
        }
    }
    return frames ? crossings / 2.0 / ((double)frames / rate) : 0;
}

TEST(long_m4b_no_full_moov) {
    TrackSource src;
    source_for("li-long", 0, &src);
    int err = 0;
    Stream *st = stream_open(&src, absc_stream_auth, &err);
    CHECK(st != NULL);
    if (!st) { printf("  stream_open err %d\n", err); return; }
    int req;
    uint64_t bytes;
    stream_table_stats(st, &req, &bytes);
    printf("  open: %d table requests, %llu KB (moov is ~3.6 MB)\n", req, (unsigned long long)(bytes >> 10));
    CHECK(req <= 12);
    CHECK(bytes < (1u << 20));
    /* t = 2.x s plays 220 + 20*2 = 260 Hz */
    CHECK_NEAR(tone_hz(st, 0.5, 2.2), 260, 6);
    double actual = 0;
    CHECK_EQ_INT(stream_seek(st, 10805.2, &actual), 0);
    CHECK_NEAR(actual, 10805.2, 0.1);
    /* t = 10805.x s plays 220 + 20*5 = 320 Hz */
    CHECK_NEAR(tone_hz(st, 0.5, 0.05), 320, 6);
    CHECK_EQ_INT(stream_seek(st, 21500.4, &actual), 0);
    CHECK_NEAR(tone_hz(st, 0.4, 0.05), 220, 6);
    stream_table_stats(st, &req, &bytes);
    printf("  after seeks: %d table requests, %llu KB\n", req, (unsigned long long)(bytes >> 10));
    stream_close(st);
}

TEST(decode_whole_tracks) {
    TrackSource src;
    int err;
    source_for("li-fast", 0, &src);
    Stream *st = stream_open(&src, absc_stream_auth, &err);
    CHECK(st != NULL);
    if (st) { CHECK_NEAR(decode_all(st), 120.0, 0.1); stream_close(st); }
    source_for("li-mp3", 1, &src);
    st = stream_open(&src, absc_stream_auth, &err);
    CHECK(st != NULL);
    if (st) { CHECK_NEAR(decode_all(st), 60.0, 0.1); stream_close(st); }
}

void suite_net(void) {
    const char *base = getenv("MOCK_ABS");
    if (!base) { printf("  (skipped: MOCK_ABS not set)\n"); return; }
    if (getenv("MOCK_MEDIA")) g_media = getenv("MOCK_MEDIA");
    http_init();
    cfg_defaults(&g_cfg);
    snprintf(g_cfg.server, sizeof g_cfg.server, "%s", base);
    snprintf(g_cfg.device_id, sizeof g_cfg.device_id, "test-device");
    absc_init(&g_cfg, NULL);
    RUN(login_and_play);
    RUN(token_refresh);
    RUN(ring_source);
    RUN(long_m4b_no_full_moov);
    RUN(decode_whole_tracks);
}
