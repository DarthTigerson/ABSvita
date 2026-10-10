#include "image.h"

void img_crop_resize(const uint8_t *src, int sw, int sh, int comps, uint8_t *dst, int out_sz) {
    int side = sw < sh ? sw : sh;
    int ox = (sw - side) / 2, oy = (sh - side) / 2;
    for (int y = 0; y < out_sz; y++) {
        int y0 = oy + y * side / out_sz, y1 = oy + (y + 1) * side / out_sz;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < out_sz; x++) {
            int x0 = ox + x * side / out_sz, x1 = ox + (x + 1) * side / out_sz;
            if (x1 <= x0) x1 = x0 + 1;
            unsigned acc[4] = {0, 0, 0, 0}, n = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) {
                    const uint8_t *p = src + (yy * sw + xx) * comps;
                    acc[0] += p[0];
                    acc[1] += comps >= 3 ? p[1] : p[0];
                    acc[2] += comps >= 3 ? p[2] : p[0];
                    acc[3] += comps == 4 ? p[3] : (comps == 2 ? p[1] : 255);
                    n++;
                }
            uint8_t *d = dst + (y * out_sz + x) * 4;
            for (int c = 0; c < 4; c++) d[c] = (uint8_t)(acc[c] / n);
        }
    }
}
