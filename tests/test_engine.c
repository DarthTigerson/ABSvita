/* Audio engine against the mock server with a null sink that consumes audio
 * 20x faster than real time. Skipped unless MOCK_ABS is set. */
#include <stdlib.h>

#include "abs_client.h"
#include "engine.h"
#include "osal.h"
#include "test.h"

#define FAST 20.0

static struct { int rate, ch; double frames; int opens; } g_sink;

static int s_open(void *ud, int rate, int ch) { (void)ud; g_sink.rate = rate; g_sink.ch = ch; g_sink.opens++; return 0; }
static int s_write(void *ud, const int16_t *pcm, int frames) {
    (void)ud; (void)pcm;
    g_sink.frames += frames;
    sleep_ms((int)(frames * 1000.0 / g_sink.rate / FAST));
    return frames;
}
static void s_close(void *ud) { (void)ud; }

static PlayableBook g_pb;

static void load_book(const char *item) {
    PlaySession s;
    if (absc_play(item, "Test", &s) != ABS_OK) { memset(&g_pb, 0, sizeof g_pb); return; }
    free(g_pb.tracks);
    free(g_pb.sources);
    g_pb.ntracks = s.book.ntracks;
    g_pb.duration = s.book.duration;
    g_pb.tracks = malloc(sizeof(Track) * s.book.ntracks);
    g_pb.sources = calloc(s.book.ntracks, sizeof(TrackSource));
    memcpy(g_pb.tracks, s.book.tracks, sizeof(Track) * s.book.ntracks);
    for (int i = 0; i < s.book.ntracks; i++) {
        absc_url(s.book.tracks[i].content_url, g_pb.sources[i].url, sizeof g_pb.sources[i].url);
        g_pb.sources[i].size = s.book.tracks[i].size;
        snprintf(g_pb.sources[i].ext, sizeof g_pb.sources[i].ext, "%s", s.book.tracks[i].ext);
        snprintf(g_pb.sources[i].codec, sizeof g_pb.sources[i].codec, "%s", s.book.tracks[i].codec);
    }
    book_detail_free(&s.book);
}

/* Waits until the position reaches t (book seconds) or the timeout (wall ms). */
static double wait_pos(double t, int timeout_ms) {
    uint64_t end = now_ms() + timeout_ms;
    while (now_ms() < end && engine_position() < t && engine_state() != ENG_ERROR && engine_state() != ENG_ENDED)
        sleep_ms(5);
    return engine_position();
}

TEST(crosses_track_boundary) {
    load_book("li-mp3");
    CHECK_EQ_INT(g_pb.ntracks, 2);
    engine_set_speed(1.0f);
    engine_load(&g_pb, 0, true);
    double p = wait_pos(90, 15000);
    CHECK(p >= 90 && p < 92);
    CHECK(engine_state() == ENG_PLAYING || engine_state() == ENG_BUFFERING);
    /* book time grows ~20x faster than wall time here */
    uint64_t w0 = now_ms();
    double p0 = engine_position();
    sleep_ms(500);
    double rate = (engine_position() - p0) / ((now_ms() - w0) / 1000.0);
    CHECK_NEAR(rate, FAST, 4);
    engine_pause();
    sleep_ms(100);
    CHECK_EQ_INT(engine_state(), ENG_PAUSED);
    double held = engine_position();
    sleep_ms(200);
    CHECK_NEAR(engine_position(), held, 0.05);
    /* play out the second file: the book ends at the sum of both tracks */
    engine_play();
    uint64_t end = now_ms() + 10000;
    while (now_ms() < end && engine_state() != ENG_ENDED) sleep_ms(10);
    CHECK_EQ_INT(engine_state(), ENG_ENDED);
    CHECK_NEAR(engine_position(), g_pb.duration, 0.6);
}

TEST(double_speed) {
    load_book("li-fast");
    engine_set_speed(2.0f);
    engine_load(&g_pb, 10, true);
    wait_pos(12, 5000);
    uint64_t w0 = now_ms();
    double p0 = engine_position();
    sleep_ms(500);
    double rate = (engine_position() - p0) / ((now_ms() - w0) / 1000.0);
    CHECK_NEAR(rate, FAST * 2, 8);
    engine_set_speed(1.0f);
    sleep_ms(200);
    w0 = now_ms();
    p0 = engine_position();
    sleep_ms(500);
    rate = (engine_position() - p0) / ((now_ms() - w0) / 1000.0);
    CHECK_NEAR(rate, FAST, 4);
}

TEST(seek_and_end) {
    load_book("li-long");
    engine_set_speed(1.0f);
    engine_load(&g_pb, 0, true);
    wait_pos(1, 8000);
    engine_seek(g_pb.duration - 20);
    sleep_ms(50);
    CHECK_NEAR(engine_position(), g_pb.duration - 20, 1.0);
    uint64_t end = now_ms() + 10000;
    while (now_ms() < end && engine_state() != ENG_ENDED) sleep_ms(10);
    CHECK_EQ_INT(engine_state(), ENG_ENDED);
    CHECK_NEAR(engine_position(), g_pb.duration, 0.6);
    engine_play(); /* from the end: restarts at 0 */
    double p = wait_pos(2, 5000);
    CHECK(p >= 2 && p < 10);
    engine_stop();
    sleep_ms(50);
    CHECK_EQ_INT(engine_state(), ENG_IDLE);
}

void suite_engine(void) {
    if (!getenv("MOCK_ABS")) { printf("  (skipped: MOCK_ABS not set)\n"); return; }
    AudioSink sink = {s_open, s_write, s_close, NULL};
    engine_init(&sink, absc_stream_auth);
    RUN(crosses_track_boundary);
    RUN(double_speed);
    RUN(seek_and_end);
    engine_exit();
    free(g_pb.tracks);
    free(g_pb.sources);
}
