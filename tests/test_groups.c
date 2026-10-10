#include <stdlib.h>

#include "groups.h"
#include "test.h"

TEST(series_parsing) {
    char name[96];
    double seq;
    series_split("Discworld #3", name, sizeof name, &seq);
    CHECK_STR(name, "Discworld"); CHECK_NEAR(seq, 3, 1e-9);
    series_split("The Expanse #1.5", name, sizeof name, &seq);
    CHECK_STR(name, "The Expanse"); CHECK_NEAR(seq, 1.5, 1e-9);
    series_split("Earthsea", name, sizeof name, &seq);
    CHECK_STR(name, "Earthsea"); CHECK_NEAR(seq, -1, 1e-9);
    series_split("Book #One", name, sizeof name, &seq); /* not a number: part of the name */
    CHECK_STR(name, "Book #One"); CHECK_NEAR(seq, -1, 1e-9);
}

TEST(grouping) {
    BookSummary b[5] = {
        {.id = "1", .title = "Mort", .author = "Terry Pratchett", .series = "Discworld #4"},
        {.id = "2", .title = "The Colour of Magic", .author = "Terry Pratchett", .series = "Discworld #1"},
        {.id = "3", .title = "Good Omens", .author = "Terry Pratchett, Neil Gaiman", .series = ""},
        {.id = "4", .title = "Dune", .author = "Frank Herbert", .series = "Dune #1, Sci-Fi Classics #2"},
        {.id = "5", .title = "Extra", .author = "", .series = "Discworld"},
    };
    Group *g;
    int n = groups_build(b, 5, GROUP_SERIES, &g);
    CHECK_EQ_INT(n, 3);
    CHECK_STR(g[0].name, "Discworld");
    CHECK_EQ_INT(g[0].count, 3);
    CHECK_EQ_INT(g[0].books[0], 1); /* #1 */
    CHECK_EQ_INT(g[0].books[1], 0); /* #4 */
    CHECK_EQ_INT(g[0].books[2], 4); /* unnumbered last */
    CHECK_STR(g[1].name, "Dune");
    CHECK_STR(g[2].name, "Sci-Fi Classics");
    CHECK_NEAR(g[2].seq[0], 2, 1e-9);
    groups_free(g, n);
    n = groups_build(b, 5, GROUP_AUTHOR, &g);
    CHECK_EQ_INT(n, 3);
    CHECK_STR(g[0].name, "Frank Herbert");
    CHECK_STR(g[1].name, "Neil Gaiman");
    CHECK_STR(g[2].name, "Terry Pratchett");
    CHECK_EQ_INT(g[2].count, 3);
    CHECK_EQ_INT(g[2].books[0], 1); /* "The Colour of Magic" sorts as "Colour" */
    CHECK_EQ_INT(g[2].books[1], 2);
    CHECK(g[2].seq == NULL);
    groups_free(g, n);
}

void suite_groups(void) {
    RUN(series_parsing);
    RUN(grouping);
}
