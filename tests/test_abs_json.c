#include <stdlib.h>

#include "abs_json.h"
#include "test.h"

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc(n + 1);
    size_t got = fread(s, 1, n, f);
    s[got] = 0;
    fclose(f);
    return s;
}

TEST(login) {
    char *j = slurp("tests/fixtures/login.json");
    char a[64], r[64], u[48];
    CHECK_EQ_INT(abs_parse_login(j, a, sizeof a, r, sizeof r, u, sizeof u), 0);
    CHECK_STR(a, "acc.123");
    CHECK_STR(r, "ref.456");
    CHECK_STR(u, "usr_1");
    CHECK(abs_parse_login("{\"user\":{}}", a, sizeof a, r, sizeof r, u, sizeof u) != 0);
    CHECK(abs_parse_login("not json", a, sizeof a, r, sizeof r, u, sizeof u) != 0);
    free(j);
}
TEST(libraries_picks_book) {
    char *j = slurp("tests/fixtures/libraries.json");
    char id[40];
    CHECK_EQ_INT(abs_parse_libraries(j, id, sizeof id), 0);
    CHECK_STR(id, "lib_1");
    CHECK(abs_parse_libraries("{\"libraries\":[]}", id, sizeof id) != 0);
    free(j);
}
TEST(items_page_with_nulls) {
    char *j = slurp("tests/fixtures/items_page.json");
    BookSummary b[4];
    int count = 0, total = 0;
    CHECK_EQ_INT(abs_parse_items_page(j, b, 4, &count, &total), 0);
    CHECK_EQ_INT(count, 3);
    CHECK_EQ_INT(total, 684);
    CHECK_STR(b[0].id, "li_a");
    CHECK_STR(b[0].title, "Dune");
    CHECK_STR(b[0].author, "Frank Herbert");
    CHECK_STR(b[0].series, "Dune #1");
    CHECK_NEAR(b[0].duration, 75600.5, 1e-6);
    CHECK_EQ_INT(b[0].added_at, 1700000000000ULL);
    CHECK_STR(b[1].title, "L\xE2\x80\x99\xC3\x89tranger");
    CHECK_STR(b[1].author, "");
    CHECK_STR(b[1].series, "");
    CHECK_STR(b[2].title, "");
    /* respects max */
    count = 2;
    CHECK_EQ_INT(abs_parse_items_page(j, b, 3, &count, &total), 0);
    CHECK_EQ_INT(count, 3);
    free(j);
}
TEST(me_progress_merge) {
    char *j = slurp("tests/fixtures/me.json");
    BookSummary b[2] = {{.id = "li_a"}, {.id = "li_c"}};
    CHECK_EQ_INT(abs_parse_me_progress(j, b, 2), 0);
    CHECK_NEAR(b[0].current_time, 18900, 1e-6);
    CHECK_NEAR(b[0].progress, 0.25, 1e-6);
    CHECK(!b[0].finished);
    CHECK_EQ_INT(b[0].progress_updated, 1760000000000ULL);
    CHECK(b[1].finished);
    /* also works on the login payload (progress nested under user) */
    char *l = slurp("tests/fixtures/login.json");
    BookSummary c[1] = {{.id = "li_b"}};
    CHECK_EQ_INT(abs_parse_me_progress(l, c, 1), 0);
    CHECK_NEAR(c[0].current_time, 1800.5, 1e-6);
    free(j);
    free(l);
}
TEST(in_progress_ids) {
    char *j = slurp("tests/fixtures/in_progress.json");
    char ids[4][40];
    CHECK_EQ_INT(abs_parse_in_progress(j, ids, 4), 2);
    CHECK_STR(ids[0], "li_b");
    CHECK_STR(ids[1], "li_a");
    CHECK_EQ_INT(abs_parse_in_progress(j, ids, 1), 1);
    free(j);
}
TEST(item_expanded) {
    char *j = slurp("tests/fixtures/item_expanded.json");
    BookDetail d;
    CHECK_EQ_INT(abs_parse_item(j, &d), 0);
    CHECK_STR(d.id, "li_a");
    CHECK_STR(d.title, "Dune");
    CHECK_STR(d.author, "Frank Herbert");
    CHECK_STR(d.narrator, "Scott Brick, Others");
    CHECK_STR(d.description, "Set on the desert planet & more.");
    CHECK_EQ_INT(d.nchapters, 2);
    CHECK_NEAR(d.chapters[1].start, 600.5, 1e-9);
    CHECK_STR(d.chapters[1].title, "Book One");
    CHECK_EQ_INT(d.ntracks, 1);
    CHECK_STR(d.tracks[0].content_url, "/api/items/li_a/file/9001");
    CHECK_STR(d.tracks[0].codec, "aac");
    CHECK_STR(d.tracks[0].ext, ".m4b");
    CHECK_STR(d.tracks[0].ino, "9001");
    CHECK_EQ_INT(d.tracks[0].size, 645000000ULL);
    book_detail_free(&d);
    free(j);
}
TEST(play_session_multi_mp3) {
    char *j = slurp("tests/fixtures/play.json");
    PlaySession s;
    CHECK_EQ_INT(abs_parse_play(j, &s), 0);
    CHECK_STR(s.id, "play_77");
    CHECK_NEAR(s.current_time, 123.75, 1e-9);
    CHECK_STR(s.book.id, "li_m");
    CHECK_STR(s.book.title, "Multi Part");
    CHECK_STR(s.book.author, "Some One");
    CHECK_STR(s.book.description, "Desc");
    CHECK_EQ_INT(s.book.ntracks, 3);
    CHECK_NEAR(s.book.tracks[2].start_offset, 200.25, 1e-9);
    CHECK_STR(s.book.tracks[2].ext, ".mp3"); /* metadata null: derived from title */
    /* no chapters -> one per track */
    CHECK_EQ_INT(s.book.nchapters, 3);
    CHECK_STR(s.book.chapters[1].title, "02");
    CHECK_NEAR(s.book.chapters[1].end, 200.25, 1e-9);
    book_detail_free(&s.book);
    free(j);
}
TEST(bodies) {
    char b[512];
    CHECK(abs_build_login_body("th\"o", "p\\w", b, sizeof b) > 0);
    CHECK_STR(b, "{\"username\":\"th\\\"o\",\"password\":\"p\\\\w\"}");
    CHECK(abs_build_sync_body(12.5, 15, 100, b, sizeof b) > 0);
    CHECK_STR(b, "{\"currentTime\":12.5,\"timeListened\":15,\"duration\":100}");
    CHECK(abs_build_progress_body(99.25, 100, 1, b, sizeof b) > 0);
    CHECK_STR(b, "{\"currentTime\":99.25,\"duration\":100,\"progress\":0.9925,\"isFinished\":true}");
    CHECK(abs_build_play_body("dev1", "New 2DS XL", "1.0.0", b, sizeof b) > 0);
    CHECK(strstr(b, "\"forceDirectPlay\":true") != NULL);
    CHECK(strstr(b, "\"deviceId\":\"dev1\"") != NULL);
    CHECK(strstr(b, "audio/mp4") != NULL);
    CHECK(abs_build_sync_body(1, 1, 1, b, 5) < 0);
    CHECK(abs_build_local_session_body("li_a", "dev1", 50.5, 100, 12, 1760000000123ULL, b, sizeof b) > 0);
    CHECK(strstr(b, "\"id\":\"play_local_li_a_1760000000123\"") != NULL);
    CHECK(strstr(b, "\"libraryItemId\":\"li_a\"") != NULL);
    CHECK(strstr(b, "\"currentTime\":50.5") != NULL);
    CHECK(strstr(b, "\"updatedAt\":1760000000123") != NULL);
}

TEST(detail_roundtrip) {
    char *j = slurp("tests/fixtures/item_expanded.json");
    BookDetail d, e;
    CHECK_EQ_INT(abs_parse_item(j, &d), 0);
    char *out = abs_detail_to_json(&d);
    CHECK(out != NULL);
    CHECK_EQ_INT(abs_parse_item(out, &e), 0);
    CHECK_STR(e.title, d.title);
    CHECK_STR(e.author, d.author);
    CHECK_STR(e.description, d.description);
    CHECK_EQ_INT(e.nchapters, d.nchapters);
    CHECK_EQ_INT(e.ntracks, 1);
    CHECK_STR(e.tracks[0].content_url, d.tracks[0].content_url);
    CHECK_EQ_INT(e.tracks[0].size, d.tracks[0].size);
    CHECK_STR(e.tracks[0].ext, ".m4b");
    book_detail_free(&d);
    book_detail_free(&e);
    free(out);
    free(j);
}

void suite_abs_json(void) {
    RUN(detail_roundtrip);
    RUN(login);
    RUN(libraries_picks_book);
    RUN(items_page_with_nulls);
    RUN(me_progress_merge);
    RUN(in_progress_ids);
    RUN(item_expanded);
    RUN(play_session_multi_mp3);
    RUN(bodies);
}
