/* Every icon's ink is centred in its cell (the 3DS build drew several off-centre). */
#include "icons.h"
#include "test.h"

static void ink_centre(const uint8_t *a, int cell, double *cx, double *cy) {
    int x0 = cell, y0 = cell, x1 = -1, y1 = -1;
    for (int y = 0; y < cell; y++)
        for (int x = 0; x < cell; x++)
            if (a[y * cell + x] > 127) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    *cx = (x0 + x1 + 1) / 2.0;
    *cy = (y0 + y1 + 1) / 2.0;
}

TEST(icons_centred) {
    static uint8_t a[64 * 64];
    for (int id = 0; id < IC_COUNT; id++) {
        icons_raster(id, a, 64, 64);
        double cx, cy;
        ink_centre(a, 64, &cx, &cy);
        double want_x = 32 + icons_optical_dx(id) * 64;
        if (fabs(cx - want_x) > 0.75 || fabs(cy - 32) > 0.75) printf("  icon %d centre (%.2f, %.2f)\n", id, cx, cy);
        CHECK_NEAR(cx, want_x, 0.75);
        CHECK_NEAR(cy, 32, 0.75);
    }
}

void suite_icons(void) { RUN(icons_centred); }
