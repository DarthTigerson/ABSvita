#include <stdlib.h>

#include "config.h"
#include "policy.h"
#include "progress_queue.h"
#include "test.h"

TEST(sync_interval_only_while_playing) {
    SyncPolicy p;
    sync_init(&p, 15);
    int fired = 0;
    for (int i = 0; i < 14; i++) fired += sync_tick(&p, 1.0, true);
    CHECK_EQ_INT(fired, 0);
    for (int i = 0; i < 100; i++) fired += sync_tick(&p, 1.0, false);
    CHECK_EQ_INT(fired, 0);
    CHECK(sync_tick(&p, 1.0, true));
    CHECK_NEAR(sync_take_listened(&p), 15, 1e-9);
    CHECK(!sync_tick(&p, 1.0, true));
    CHECK_NEAR(sync_take_listened(&p), 1, 1e-9);
    CHECK_NEAR(sync_take_listened(&p), 0, 1e-9);
    sync_init(&p, 0);
    CHECK_NEAR(p.interval, 15, 1e-9);
}
TEST(sleep_minutes_fade_and_stop) {
    SleepTimer s;
    sleep_set(&s, SLEEP_MINUTES, 1, 0);
    CHECK_EQ_INT(sleep_tick(&s, 50, 0), SLEEP_NONE);
    CHECK_NEAR(sleep_volume(&s, 0, 1), 1.0, 1e-6);
    CHECK_EQ_INT(sleep_tick(&s, 7.5, 0), SLEEP_FADING);
    CHECK_NEAR(sleep_volume(&s, 0, 1), 0.5, 1e-6);
    CHECK_EQ_INT(sleep_tick(&s, 3, 0), SLEEP_STOP);
    CHECK_EQ_INT(s.mode, SLEEP_OFF);
    CHECK_EQ_INT(sleep_tick(&s, 3, 0), SLEEP_NONE);
}
TEST(sleep_end_of_chapter) {
    SleepTimer s;
    sleep_set(&s, SLEEP_END_CHAPTER, 0, 600);
    CHECK_EQ_INT(sleep_tick(&s, 1, 500), SLEEP_NONE);
    CHECK_EQ_INT(sleep_tick(&s, 1, 597), SLEEP_FADING);
    CHECK_NEAR(sleep_volume(&s, 597.5, 1), 0.5, 1e-6);
    CHECK_NEAR(sleep_volume(&s, 595, 2), 0.5, 1e-6); /* 5 book s at 2x = 2.5 wall s */
    CHECK_EQ_INT(sleep_tick(&s, 1, 600), SLEEP_STOP);
    sleep_set(&s, SLEEP_OFF, 0, 0);
    CHECK_NEAR(sleep_volume(&s, 0, 1), 1, 1e-9);
}
TEST(queue_newer_wins_and_eviction) {
    ProgressQueue q = {0};
    QEntry a = {.item_id = "a", .current_time = 10, .updated_ms = 100, .pending = true};
    pq_put(&q, &a);
    QEntry older = {.item_id = "a", .current_time = 5, .updated_ms = 50};
    pq_put(&q, &older);
    CHECK_NEAR(pq_get(&q, "a")->current_time, 10, 1e-9);
    QEntry newer = {.item_id = "a", .current_time = 20, .updated_ms = 200, .pending = true};
    pq_put(&q, &newer);
    CHECK_EQ_INT(q.n, 1);
    CHECK_NEAR(pq_get(&q, "a")->current_time, 20, 1e-9);
    pq_mark_synced(&q, "a", 150); /* an older confirmation must not clear a newer entry */
    CHECK(pq_get(&q, "a")->pending);
    pq_mark_synced(&q, "a", 200);
    CHECK(!pq_get(&q, "a")->pending);
    for (int i = 0; i < PQ_MAX + 5; i++) {
        QEntry e = {.updated_ms = 1000 + i};
        snprintf(e.item_id, sizeof e.item_id, "b%d", i);
        pq_put(&q, &e);
    }
    CHECK_EQ_INT(q.n, PQ_MAX);
    CHECK(pq_get(&q, "a") == NULL);           /* oldest evicted */
    CHECK(pq_get(&q, "b68") != NULL);
    CHECK(pq_get(&q, "missing") == NULL);
}
TEST(queue_save_load_and_corrupt) {
    const char *p = "build-host/pq_test.json";
    ProgressQueue q = {0}, r;
    QEntry a = {.item_id = "li_\xC3\xA9", .current_time = 12.5, .duration = 99, .updated_ms = 1760000000123ULL, .finished = true, .pending = true};
    pq_put(&q, &a);
    CHECK_EQ_INT(pq_save(&q, p), 0);
    CHECK_EQ_INT(pq_load(&r, p), 0);
    CHECK_EQ_INT(r.n, 1);
    CHECK_STR(r.e[0].item_id, "li_\xC3\xA9");
    CHECK_NEAR(r.e[0].current_time, 12.5, 1e-9);
    CHECK_EQ_INT(r.e[0].updated_ms, 1760000000123ULL);
    CHECK(r.e[0].finished && r.e[0].pending);
    FILE *f = fopen(p, "wb"); fputs("{garbage", f); fclose(f);
    CHECK_EQ_INT(pq_load(&r, p), -1);
    CHECK_EQ_INT(r.n, 0);
    CHECK_EQ_INT(pq_load(&r, "build-host/nope.json"), -1);
}
TEST(config_roundtrip_and_clamp) {
    const char *p = "build-host/cfg_test.json";
    Config c, d;
    cfg_defaults(&c);
    CHECK_STR(c.server, CFG_DEFAULT_SERVER);
    CHECK_EQ_INT(c.skip_back, 15);
    strcpy(c.username, "thomas");
    strcpy(c.refresh, "rt.xyz");
    c.speed = 1.5f;
    c.skip_fwd = 60;
    c.clock_offset_ms = -7200000;
    c.clock_known = true;
    CHECK_EQ_INT(cfg_save(&c, p), 0);
    CHECK_EQ_INT(cfg_load(&d, p), 0);
    CHECK_STR(d.username, "thomas");
    CHECK_STR(d.refresh, "rt.xyz");
    CHECK_NEAR(d.speed, 1.5, 1e-6);
    CHECK_EQ_INT(d.skip_fwd, 60);
    CHECK(d.clock_known);
    CHECK_EQ_INT(d.clock_offset_ms, -7200000);
    FILE *f = fopen(p, "wb"); fputs("{\"speed\":9,\"skipBack\":7,\"syncInterval\":1}", f); fclose(f);
    CHECK_EQ_INT(cfg_load(&d, p), 0);
    CHECK_NEAR(d.speed, 2.0, 1e-6);
    CHECK_EQ_INT(d.skip_back, 15);
    CHECK_EQ_INT(d.sync_interval, 10);
    CHECK_EQ_INT(cfg_load(&d, "build-host/none.json"), -1);
    CHECK_STR(d.server, CFG_DEFAULT_SERVER);
    CHECK_NEAR(cfg_next_speed(1.0f), 1.25, 1e-6);
    CHECK_NEAR(cfg_next_speed(2.0f), 0.75, 1e-6);
    CHECK_EQ_INT(cfg_next_skip(30), 60);
    CHECK_EQ_INT(cfg_next_skip(60), 10);
}

/* Logging out writes a shorter config over a longer one; it must still parse
 * (the emulator showed stale tails), and no temp file is left behind. */
TEST(config_shrinks_cleanly) {
    const char *p = "build-host/cfg_shrink.json";
    Config c, d;
    cfg_defaults(&c);
    memset(c.refresh, 'x', sizeof c.refresh - 1);
    c.refresh[sizeof c.refresh - 1] = 0;
    CHECK_EQ_INT(cfg_save(&c, p), 0);
    FILE *f = fopen("build-host/cfg_shrink.json.tmp", "wb");
    fputs("{\"stale\": \"temp file from a crash, longer than the next save\", \"pad\": \"....................\"}", f);
    fclose(f);
    c.refresh[0] = 0;
    strcpy(c.username, "u");
    CHECK_EQ_INT(cfg_save(&c, p), 0);
    CHECK_EQ_INT(cfg_load(&d, p), 0);
    CHECK_STR(d.refresh, "");
    CHECK_STR(d.username, "u");
    f = fopen("build-host/cfg_shrink.json.tmp", "rb");
    CHECK(f == NULL);
    if (f) fclose(f);
}

void suite_policy(void) {
    RUN(config_shrinks_cleanly);
    RUN(sync_interval_only_while_playing);
    RUN(sleep_minutes_fade_and_stop);
    RUN(sleep_end_of_chapter);
    RUN(queue_newer_wins_and_eviction);
    RUN(queue_save_load_and_corrupt);
    RUN(config_roundtrip_and_clamp);
}
