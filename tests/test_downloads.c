/* Offline downloads against tools/mock_abs.py (skipped unless MOCK_ABS is set). */
#include <stdlib.h>
#include <sys/stat.h>

#include "abs_client.h"
#include "downloads.h"
#include "engine.h"
#include "http.h"
#include "osal.h"
#include "player.h"
#include "test.h"
#include "worker.h"

#define DL_DIR "build-host/dltest"

static const char *g_media = "tests/media";
static int64_t g_free = INT64_MAX;
static int64_t fake_free(const char *dir) { (void)dir; return g_free; }

static void post(const char *path) {
    char url[256];
    snprintf(url, sizeof url, "%s%s", getenv("MOCK_ABS"), path);
    HttpResp r;
    if (http_request("POST", url, NULL, "{}", 2, 0, &r) == 0) http_resp_free(&r);
}

static long file_len(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 ? (long)st.st_size : -1;
}

static int same_file(const char *a, const char *b) {
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    int same = fa && fb;
    while (same) {
        int ca = fgetc(fa), cb = fgetc(fb);
        if (ca != cb) same = 0;
        if (ca == EOF || cb == EOF) break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return same;
}

static DlState wait_state(const char *id, int timeout_ms) {
    uint64_t end = now_ms() + timeout_ms;
    DlState s;
    while ((s = dl_state(id, NULL)) == DL_QUEUED || s == DL_ACTIVE) {
        if (now_ms() > end) break;
        sleep_ms(10);
    }
    return s;
}

static void media_path(const char *name, char *out, size_t n) { snprintf(out, n, "%s/%s", g_media, name); }

TEST(download_two_file_book) {
    CHECK_EQ_INT(dl_start("li-mp3"), 0);
    CHECK(dl_state("li-mp3", NULL) == DL_QUEUED || dl_state("li-mp3", NULL) == DL_ACTIVE);
    CHECK_EQ_INT(wait_state("li-mp3", 10000), DL_DONE);
    CHECK(dl_is_done("li-mp3"));
    char got[256], want[256];
    dl_track_path("li-mp3", 0, ".mp3", got, sizeof got);
    media_path("part1.mp3", want, sizeof want);
    CHECK(same_file(got, want));
    dl_track_path("li-mp3", 1, ".mp3", got, sizeof got);
    media_path("part2.mp3", want, sizeof want);
    CHECK(same_file(got, want));
    BookDetail b;
    CHECK_EQ_INT(dl_load_meta("li-mp3", &b), 0);
    CHECK_EQ_INT(b.ntracks, 2);
    CHECK_STR(b.title, "Two Parts");
    book_detail_free(&b);
    DlInfo list[8];
    int n = dl_list(list, 8);
    CHECK_EQ_INT(n, 1);
    CHECK_EQ_INT(list[0].done_bytes, list[0].total_bytes);
    CHECK(dl_used_bytes() >= list[0].total_bytes);
}

TEST(download_survives_dropped_connection) {
    post("/__test/drop?after=200000");
    CHECK_EQ_INT(dl_start("li-fast"), 0);
    CHECK_EQ_INT(wait_state("li-fast", 15000), DL_DONE);
    char got[256], want[256];
    dl_track_path("li-fast", 0, ".m4a", got, sizeof got);
    media_path("faststart.m4a", want, sizeof want);
    CHECK(same_file(got, want));
}

TEST(pause_resume_delete) {
    post("/__test/reset");
    CHECK_EQ_INT(dl_start("li-long"), 0);
    float frac = 0;
    uint64_t end = now_ms() + 10000;
    while (now_ms() < end && dl_state("li-long", &frac) != DL_DONE && frac < 0.02f) sleep_ms(1);
    dl_pause("li-long");
    CHECK_EQ_INT(wait_state("li-long", 5000), DL_PAUSED);
    char part[256], path[256];
    dl_track_path("li-long", 0, ".m4b", path, sizeof path);
    snprintf(part, sizeof part, "%s.part", path);
    long had = file_len(part);
    CHECK(had > 0);
    /* resuming asks only for the rest of the file */
    post("/__test/reset");
    CHECK_EQ_INT(dl_start("li-long"), 0);
    end = now_ms() + 10000;
    while (now_ms() < end && file_len(part) <= had && dl_state("li-long", NULL) != DL_DONE) sleep_ms(5);
    dl_pause("li-long");
    wait_state("li-long", 5000);
    char url[256];
    snprintf(url, sizeof url, "%s/__test/log", getenv("MOCK_ABS"));
    HttpResp r;
    CHECK_EQ_INT(http_request("GET", url, NULL, NULL, 0, 0, &r), 0);
    char want[64];
    snprintf(want, sizeof want, "bytes=%ld-", had);
    CHECK(r.data && strstr(r.data, want) != NULL);
    http_resp_free(&r);
    CHECK_EQ_INT(dl_delete("li-long"), 0);
    CHECK_EQ_INT(dl_state("li-long", NULL), DL_NONE);
    CHECK(file_len(DL_DIR "/li-long") < 0);
}

TEST(not_enough_space) {
    g_free = 10 << 20; /* 10 MB free, the book needs ~1 MB + 50 MB margin */
    CHECK_EQ_INT(dl_start("li-fast-2"), 0); /* unknown id: fails in the job */
    wait_state("li-fast-2", 5000);
    dl_delete("li-fast-2");
    dl_delete("li-fast");
    CHECK_EQ_INT(dl_start("li-fast"), 0);
    CHECK_EQ_INT(wait_state("li-fast", 5000), DL_FAILED);
    DlInfo list[8];
    int n = dl_list(list, 8), found = 0;
    for (int i = 0; i < n; i++)
        if (!strcmp(list[i].id, "li-fast")) { found = 1; CHECK(strstr(list[i].error, "space") != NULL); }
    CHECK(found);
    g_free = INT64_MAX;
    CHECK_EQ_INT(dl_start("li-fast"), 0);
    CHECK_EQ_INT(wait_state("li-fast", 10000), DL_DONE);
}

TEST(rescan_keeps_state) {
    dl_exit();
    dl_init(DL_DIR, fake_free);
    CHECK(dl_is_done("li-mp3"));
    CHECK(dl_is_done("li-fast"));
}

static int s_open(void *ud, int rate, int ch) { (void)ud; (void)rate; (void)ch; return 0; }
static int s_write(void *ud, const int16_t *pcm, int frames) { (void)ud; (void)pcm; sleep_ms(1); return frames; }
static void s_close(void *ud) { (void)ud; }

TEST(plays_offline_from_memory_card) {
    static Config cfg;
    static ProgressQueue pq;
    extern Config *test_abs_cfg;
    cfg_defaults(&cfg);
    player_init(&cfg, &pq, NULL, "Test");
    /* the server is gone */
    char saved[160];
    snprintf(saved, sizeof saved, "%s", test_abs_cfg->server);
    snprintf(test_abs_cfg->server, sizeof test_abs_cfg->server, "http://127.0.0.1:9");
    BookSummary b = {.id = "li-mp3"};
    player_open(&b, -1);
    uint64_t end = now_ms() + 5000;
    while (now_ms() < end && !(player_has_book() && engine_state() == ENG_PLAYING)) { player_tick(0.016); sleep_ms(5); }
    CHECK(player_has_book());
    CHECK_EQ_INT(engine_state(), ENG_PLAYING);
    CHECK(player_position() > 0);
    player_exit();
    snprintf(test_abs_cfg->server, sizeof test_abs_cfg->server, "%s", saved);
}

void suite_downloads(void) {
    if (!getenv("MOCK_ABS")) { printf("  (skipped: MOCK_ABS not set)\n"); return; }
    if (getenv("MOCK_MEDIA")) g_media = getenv("MOCK_MEDIA");
    system("rm -rf " DL_DIR);
    if (!g_api) g_api = worker_create("api", 128);
    if (!g_sync) g_sync = worker_create("sync", 128);
    dl_init(DL_DIR, fake_free);
    RUN(download_two_file_book);
    RUN(download_survives_dropped_connection);
    RUN(pause_resume_delete);
    RUN(not_enough_space);
    RUN(rescan_keeps_state);
    AudioSink sink = {s_open, s_write, s_close, NULL};
    engine_init(&sink, absc_stream_auth);
    RUN(plays_offline_from_memory_card);
    engine_exit();
    dl_exit();
}
