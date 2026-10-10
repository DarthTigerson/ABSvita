#include <stdlib.h>

#include "color.h"
#include "library.h"
#include "mp3.h"
#include "image.h"
#include "test.h"
#include "url.h"

TEST(url_join_and_valid) {
    char b[128];
    url_join("http://192.168.1.10:13378/audiobookshelf/", "/api/me", b, sizeof b);
    CHECK_STR(b, "http://192.168.1.10:13378/audiobookshelf/api/me");
    url_join("http://h:1", "login", b, sizeof b);
    CHECK_STR(b, "http://h:1/login");
    CHECK(url_join("http://h", "x", b, 5) < 0);
    CHECK(url_valid("http://192.168.1.10:13378/audiobookshelf"));
    CHECK(url_valid("http://abs.local"));
    CHECK(!url_valid("https://abs.local"));
    CHECK(!url_valid("http://:80"));
    CHECK(!url_valid("http://host:99999"));
    CHECK(!url_valid("http://ho st"));
    CHECK(!url_valid("192.168.1.1"));
    char n[128] = "  192.168.1.10:13378/audiobookshelf/ ";
    url_normalize(n, sizeof n);
    CHECK_STR(n, "http://192.168.1.10:13378/audiobookshelf");
    url_encode("a b&c/é", b, sizeof b);
    CHECK_STR(b, "a%20b%26c%2F%C3%A9");
}
TEST(accent_colour) {
    uint8_t px[16 * 16 * 4];
    for (int i = 0; i < 256; i++) { px[i * 4] = 20; px[i * 4 + 1] = 40; px[i * 4 + 2] = 200; px[i * 4 + 3] = 255; }
    Rgb a = accent_from_rgba(px, 16, 16);
    CHECK(a.b > a.r && a.b > a.g);
    float h, s, l;
    rgb_to_hsl(a, &h, &s, &l);
    CHECK(l >= 0.44f && l <= 0.71f);
    for (int i = 0; i < 256; i++) { px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = 128; }
    a = accent_from_rgba(px, 16, 16);
    CHECK_EQ_INT(a.r, 247); /* grey cover -> default amber */
    Rgb red = hsl_to_rgb(0, 1, 0.5f);
    CHECK_EQ_INT(red.r, 255); CHECK_EQ_INT(red.g, 0);
}
TEST(crop_resize) {
    uint8_t big[4 * 2 * 3] = {10, 10, 10, 10, 10, 10, 200, 0, 0, 100, 0, 0, 10, 10, 10, 10, 10, 10, 200, 0, 0, 100, 0, 0};
    uint8_t out[1 * 4];
    img_crop_resize(big, 4, 2, 3, out, 1); /* centre square = columns 1..2 */
    CHECK_EQ_INT(out[0], 105);
    CHECK_EQ_INT(out[3], 255);
}
TEST(library_sort_filter) {
    BookSummary b[4] = {
        {.id = "1", .title = "The Hobbit", .author = "Tolkien", .added_at = 5},
        {.id = "2", .title = "Dune", .author = "Herbert", .added_at = 9},
        {.id = "3", .title = "an Echo", .author = "", .added_at = 1},
        {.id = "4", .title = "Circe", .author = "Miller", .series = "Greek", .added_at = 7},
    };
    int idx[4] = {0, 1, 2, 3}, out[4];
    lib_sort(b, idx, 4, SORT_TITLE);
    CHECK_EQ_INT(idx[0], 3); CHECK_EQ_INT(idx[1], 1); CHECK_EQ_INT(idx[2], 2); CHECK_EQ_INT(idx[3], 0);
    lib_sort(b, idx, 4, SORT_AUTHOR);
    CHECK_EQ_INT(idx[0], 1); CHECK_EQ_INT(idx[3], 2);
    lib_sort(b, idx, 4, SORT_RECENT);
    CHECK_EQ_INT(idx[0], 1); CHECK_EQ_INT(idx[1], 3);
    CHECK_EQ_INT(lib_filter(b, idx, 4, "TOLK", out), 1);
    CHECK_EQ_INT(out[0], 0);
    CHECK_EQ_INT(lib_filter(b, idx, 4, "greek", out), 1);
    CHECK_EQ_INT(lib_filter(b, idx, 4, "", out), 4);
    CHECK_EQ_INT(lib_find(b, 4, "3"), 2);
    CHECK_EQ_INT(lib_find(b, 4, "x"), -1);
}
TEST(mp3_headers) {
    uint8_t h[4] = {0xFF, 0xFB, 0x50, 0xC4}; /* MPEG1 L3 64 kbps 44.1k mono */
    Mp3Frame f;
    CHECK_EQ_INT(mp3_parse_header(h, &f), 0);
    CHECK_EQ_INT(f.version, 1); CHECK_EQ_INT(f.bitrate, 64); CHECK_EQ_INT(f.sample_rate, 44100);
    CHECK_EQ_INT(f.channels, 1); CHECK_EQ_INT(f.frame_bytes, 208);
    uint8_t bad[4] = {0xFF, 0xFB, 0xF0, 0};
    CHECK(mp3_parse_header(bad, &f) != 0);
    uint8_t id3[10] = {'I', 'D', '3', 4, 0, 0, 0, 0, 0x02, 0x01};
    CHECK_EQ_INT(mp3_id3_size(id3, 10), 257 + 10);
}
static void probe_file(const char *p, int vbr) {
    Reader r; FileReaderCtx fc;
    if (reader_file_open(&r, &fc, p, 0, 0) != 0) { printf("  (fixture %s missing, skipped)\n", p); return; }
    Mp3Info info;
    CHECK_EQ_INT(mp3_probe(&r, &info), 0);
    CHECK_EQ_INT(info.sample_rate, 44100);
    CHECK_NEAR(info.duration, 12.0, 0.15);
    CHECK(info.audio_start > 0); /* ID3 tag or Xing frame skipped */
    if (vbr) CHECK(info.has_toc);
    uint64_t mid = mp3_offset_for_time(&info, 6.0);
    CHECK(mid > info.audio_start + info.audio_bytes * 0.4 && mid < info.audio_start + info.audio_bytes * 0.6);
    CHECK_EQ_INT(mp3_offset_for_time(&info, -1), info.audio_start);
    CHECK_EQ_INT(mp3_offset_for_time(&info, 99), info.audio_start + info.audio_bytes);
    reader_file_close(&fc);
}
TEST(mp3_probe_fixtures) {
    probe_file("tests/fixtures/media/cbr.mp3", 0);
    probe_file("tests/fixtures/media/vbr.mp3", 1);
}

void suite_utils(void) {
    RUN(url_join_and_valid);
    RUN(accent_colour);
    RUN(crop_resize);
    RUN(library_sort_filter);
    RUN(mp3_headers);
    RUN(mp3_probe_fixtures);
}
