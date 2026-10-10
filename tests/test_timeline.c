#include "test.h"
#include "timeline.h"

static Track T3[3] = {
    {.index = 1, .start_offset = 0, .duration = 100},
    {.index = 2, .start_offset = 100, .duration = 50},
    {.index = 3, .start_offset = 150, .duration = 25},
};
static Chapter C3[3] = {{0, 60, "One"}, {60, 120, "Two"}, {120, 175, "Three"}};

TEST(locate_single_track) {
    double l;
    CHECK_EQ_INT(tl_locate(T3, 1, 42.5, &l), 0);
    CHECK_NEAR(l, 42.5, 1e-9);
}
TEST(locate_boundary_picks_next) {
    double l;
    CHECK_EQ_INT(tl_locate(T3, 3, 100.0, &l), 1);
    CHECK_NEAR(l, 0.0, 1e-9);
    CHECK_EQ_INT(tl_locate(T3, 3, 149.999, &l), 1);
    CHECK_NEAR(l, 49.999, 1e-6);
}
TEST(locate_clamps) {
    double l;
    CHECK_EQ_INT(tl_locate(T3, 3, -5, &l), 0);
    CHECK_NEAR(l, 0, 1e-9);
    CHECK_EQ_INT(tl_locate(T3, 3, 1000, &l), 2);
    CHECK_NEAR(l, 25, 1e-9);
    CHECK_EQ_INT(tl_locate(T3, 3, 175, &l), 2);
    CHECK_NEAR(l, 25, 1e-9);
    CHECK_EQ_INT(tl_locate(T3, 0, 5, &l), -1);
    CHECK_NEAR(tl_duration(T3, 3), 175, 1e-9);
}
TEST(chapter_at) {
    CHECK_EQ_INT(tl_chapter_at(C3, 3, 0), 0);
    CHECK_EQ_INT(tl_chapter_at(C3, 3, 59.9), 0);
    CHECK_EQ_INT(tl_chapter_at(C3, 3, 60), 1);
    CHECK_EQ_INT(tl_chapter_at(C3, 3, 175), 2);
    CHECK_EQ_INT(tl_chapter_at(C3, 3, 500), 2);
    CHECK_EQ_INT(tl_chapter_at(C3, 3, -1), 0);
    CHECK_EQ_INT(tl_chapter_at(C3, 0, 10), -1);
}
TEST(next_prev_chapter) {
    CHECK_NEAR(tl_next_chapter(C3, 3, 10), 60, 1e-9);
    CHECK_NEAR(tl_next_chapter(C3, 3, 130), -1, 1e-9);
    CHECK_NEAR(tl_prev_chapter(C3, 3, 70), 60, 1e-9);  /* 10 s in -> restart chapter */
    CHECK_NEAR(tl_prev_chapter(C3, 3, 62), 0, 1e-9);   /* 2 s in -> previous chapter */
    CHECK_NEAR(tl_prev_chapter(C3, 3, 1), 0, 1e-9);    /* first chapter -> 0 */
    CHECK_NEAR(tl_prev_chapter(C3, 0, 50), 0, 1e-9);
    CHECK_NEAR(tl_next_chapter(C3, 0, 50), -1, 1e-9);
}

void suite_timeline(void) {
    RUN(locate_single_track);
    RUN(locate_boundary_picks_next);
    RUN(locate_clamps);
    RUN(chapter_at);
    RUN(next_prev_chapter);
}
