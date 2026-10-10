/* Procedural icons as signed distance fields, rasterised once at startup
 * into an A8 atlas (anti-aliased at any scale). Unit space: [-1, 1], y down. */
#include <math.h>
#include <stdint.h>

#include "icons.h"

typedef struct { float x, y; } V;

static float len(V a) { return sqrtf(a.x * a.x + a.y * a.y); }
static V sub(V a, V b) { return (V){a.x - b.x, a.y - b.y}; }
static float dot(V a, V b) { return a.x * b.x + a.y * b.y; }
static float fminf2(float a, float b) { return a < b ? a : b; }
static float fmaxf2(float a, float b) { return a > b ? a : b; }

static float sd_circle(V p, V c, float r) { return len(sub(p, c)) - r; }
static float sd_seg(V p, V a, V b, float w) {
    V pa = sub(p, a), ba = sub(b, a);
    float h = dot(pa, ba) / dot(ba, ba);
    h = h < 0 ? 0 : h > 1 ? 1 : h;
    V q = {pa.x - ba.x * h, pa.y - ba.y * h};
    return len(q) - w;
}
static float sd_box(V p, V c, V half, float r) {
    V d = {fabsf(p.x - c.x) - half.x + r, fabsf(p.y - c.y) - half.y + r};
    V m = {fmaxf2(d.x, 0), fmaxf2(d.y, 0)};
    return len(m) + fminf2(fmaxf2(d.x, d.y), 0) - r;
}
/* convex polygon, vertices in either winding */
static float sd_poly(V p, const V *v, int n) {
    float d = dot(sub(p, v[0]), sub(p, v[0]));
    float s = 1;
    for (int i = 0, j = n - 1; i < n; j = i, i++) {
        V e = sub(v[j], v[i]), w = sub(p, v[i]);
        float h = dot(w, e) / dot(e, e);
        h = h < 0 ? 0 : h > 1 ? 1 : h;
        V b = {w.x - e.x * h, w.y - e.y * h};
        d = fminf2(d, dot(b, b));
        int c1 = p.y >= v[i].y, c2 = p.y < v[j].y, c3 = e.x * w.y > e.y * w.x;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) s = -s;
    }
    return s * sqrtf(d);
}
/* ring arc from angle a0 to a1 (radians, a0 < a1), thickness w, round caps */
static float sd_arc(V p, V c, float r, float w, float a0, float a1) {
    V q = sub(p, c);
    float a = atan2f(q.y, q.x);
    while (a < a0) a += 6.2831853f;
    if (a <= a1) return fabsf(len(q) - r) - w;
    V e0 = {c.x + r * cosf(a0), c.y + r * sinf(a0)}, e1 = {c.x + r * cosf(a1), c.y + r * sinf(a1)};
    return fminf2(len(sub(p, e0)), len(sub(p, e1))) - w;
}
static float uni(float a, float b) { return fminf2(a, b); }
static float diff(float a, float b) { return fmaxf2(a, -b); }
static float outline(float d, float w) { return fabsf(d) - w; }

static const float PI = 3.14159265f;

static float icon_sdf(int id, V p) {
    const float W = 0.085f; /* stroke half-width */
    switch (id) {
    case IC_PLAY: {
        V t[3] = {{-0.32f, -0.52f}, {0.56f, 0}, {-0.32f, 0.52f}};
        return sd_poly(p, t, 3) - 0.05f;
    }
    case IC_PAUSE:
        return uni(sd_box(p, (V){-0.26f, 0}, (V){0.13f, 0.5f}, 0.06f), sd_box(p, (V){0.26f, 0}, (V){0.13f, 0.5f}, 0.06f));
    case IC_BACK:
    case IC_FWD: {
        V q = id == IC_FWD ? (V){-p.x, p.y} : p;
        float d = sd_arc(q, (V){0, 0.04f}, 0.62f, W, -PI * 0.15f, PI * 1.5f);
        /* arrow head at the arc start (top), pointing left */
        V t[3] = {{-0.05f, -0.82f}, {-0.05f, -0.32f}, {-0.38f, -0.57f}};
        return uni(d, sd_poly(q, t, 3) - 0.02f);
    }
    case IC_PREV:
    case IC_NEXT: {
        V q = id == IC_NEXT ? (V){-p.x, p.y} : p;
        V t[3] = {{0.5f, -0.48f}, {0.5f, 0.48f}, {-0.25f, 0}};
        return uni(sd_poly(q, t, 3) - 0.04f, sd_box(q, (V){-0.42f, 0}, (V){0.09f, 0.48f}, 0.05f));
    }
    case IC_LIST: {
        float d = 1e9f;
        for (int i = -1; i <= 1; i++) {
            d = uni(d, sd_seg(p, (V){-0.2f, i * 0.42f}, (V){0.62f, i * 0.42f}, W));
            d = uni(d, sd_circle(p, (V){-0.55f, i * 0.42f}, 0.1f));
        }
        return d;
    }
    case IC_MOON:
        return diff(sd_circle(p, (V){0, 0}, 0.62f), sd_circle(p, (V){0.34f, -0.26f}, 0.5f));
    case IC_SPEED: {
        float d = sd_arc(p, (V){0, 0.12f}, 0.62f, W, PI * 0.8f, PI * 2.2f);
        d = uni(d, sd_seg(p, (V){0, 0.12f}, (V){0.36f, -0.22f}, W));
        return uni(d, sd_circle(p, (V){0, 0.12f}, 0.13f));
    }
    case IC_DOWNLOAD: {
        float d = sd_seg(p, (V){0, -0.62f}, (V){0, 0.25f}, W);
        d = uni(d, sd_seg(p, (V){-0.34f, -0.07f}, (V){0, 0.27f}, W));
        d = uni(d, sd_seg(p, (V){0.34f, -0.07f}, (V){0, 0.27f}, W));
        return uni(d, sd_seg(p, (V){-0.55f, 0.6f}, (V){0.55f, 0.6f}, W));
    }
    case IC_CHECK:
        return uni(sd_seg(p, (V){-0.55f, 0.02f}, (V){-0.16f, 0.42f}, W * 1.2f), sd_seg(p, (V){-0.16f, 0.42f}, (V){0.6f, -0.42f}, W * 1.2f));
    case IC_TRASH: {
        float d = outline(sd_box(p, (V){0, 0.18f}, (V){0.38f, 0.48f}, 0.08f), W * 0.8f);
        d = diff(d, sd_box(p, (V){0, -0.4f}, (V){0.5f, 0.1f}, 0));
        d = uni(d, sd_seg(p, (V){-0.6f, -0.42f}, (V){0.6f, -0.42f}, W));
        d = uni(d, sd_seg(p, (V){-0.18f, -0.62f}, (V){0.18f, -0.62f}, W));
        d = uni(d, sd_seg(p, (V){-0.13f, 0.0f}, (V){-0.13f, 0.4f}, W * 0.7f));
        return uni(d, sd_seg(p, (V){0.13f, 0.0f}, (V){0.13f, 0.4f}, W * 0.7f));
    }
    case IC_GEAR: {
        float d = sd_circle(p, (V){0, 0}, 0.5f);
        for (int i = 0; i < 8; i++) {
            float a = i * PI / 4;
            d = uni(d, sd_circle(p, (V){0.6f * cosf(a), 0.6f * sinf(a)}, 0.15f));
        }
        return diff(d, sd_circle(p, (V){0, 0}, 0.2f));
    }
    case IC_SEARCH:
        return uni(outline(sd_circle(p, (V){-0.12f, -0.12f}, 0.4f), W), sd_seg(p, (V){0.2f, 0.2f}, (V){0.6f, 0.6f}, W * 1.3f));
    case IC_SORT:
        return uni(uni(sd_seg(p, (V){-0.6f, -0.45f}, (V){0.6f, -0.45f}, W), sd_seg(p, (V){-0.6f, 0}, (V){0.3f, 0}, W)),
                   sd_seg(p, (V){-0.6f, 0.45f}, (V){0.0f, 0.45f}, W));
    case IC_WIFI: {
        float d = sd_circle(p, (V){0, 0.52f}, 0.12f);
        for (int i = 1; i <= 3; i++) d = uni(d, sd_arc(p, (V){0, 0.52f}, 0.3f * i, W * 0.9f, PI * 1.25f, PI * 1.75f));
        return d;
    }
    case IC_BOOK: {
        V l[4] = {{-0.04f, -0.42f}, {-0.66f, -0.56f}, {-0.66f, 0.46f}, {-0.04f, 0.58f}};
        V r[4] = {{0.04f, -0.42f}, {0.66f, -0.56f}, {0.66f, 0.46f}, {0.04f, 0.58f}};
        return uni(sd_poly(p, l, 4), sd_poly(p, r, 4)) - 0.02f;
    }
    case IC_HEADPHONES: {
        float d = sd_arc(p, (V){0, 0.1f}, 0.56f, W, PI, PI * 2);
        d = uni(d, sd_box(p, (V){-0.5f, 0.32f}, (V){0.12f, 0.24f}, 0.08f));
        return uni(d, sd_box(p, (V){0.5f, 0.32f}, (V){0.12f, 0.24f}, 0.08f));
    }
    case IC_CLOSE:
        return uni(sd_seg(p, (V){-0.45f, -0.45f}, (V){0.45f, 0.45f}, W * 1.1f), sd_seg(p, (V){0.45f, -0.45f}, (V){-0.45f, 0.45f}, W * 1.1f));
    case IC_SCREEN: {
        float d = outline(sd_box(p, (V){0, -0.05f}, (V){0.62f, 0.42f}, 0.1f), W * 0.8f);
        d = uni(d, sd_seg(p, (V){-0.25f, 0.6f}, (V){0.25f, 0.6f}, W));
        return uni(diff(d, sd_seg(p, (V){-0.7f, -0.6f}, (V){0.7f, 0.5f}, W * 2.4f)), sd_seg(p, (V){-0.7f, -0.6f}, (V){0.7f, 0.5f}, W * 0.8f));
    }
    case IC_CHEVRON:
        return uni(sd_seg(p, (V){-0.2f, -0.45f}, (V){0.25f, 0}, W * 1.1f), sd_seg(p, (V){0.25f, 0}, (V){-0.2f, 0.45f}, W * 1.1f));
    case IC_CHEVRON_LEFT:
        return uni(sd_seg(p, (V){0.2f, -0.45f}, (V){-0.25f, 0}, W * 1.1f), sd_seg(p, (V){-0.25f, 0}, (V){0.2f, 0.45f}, W * 1.1f));
    case IC_USER: {
        float body = diff(sd_circle(p, (V){0, 0.75f}, 0.6f), sd_box(p, (V){0, 1.2f}, (V){1, 0.45f}, 0));
        return uni(sd_circle(p, (V){0, -0.3f}, 0.3f), body);
    }
    case IC_SERVER: {
        float d = outline(sd_box(p, (V){0, -0.3f}, (V){0.6f, 0.24f}, 0.08f), W * 0.8f);
        d = uni(d, outline(sd_box(p, (V){0, 0.3f}, (V){0.6f, 0.24f}, 0.08f), W * 0.8f));
        d = uni(d, sd_circle(p, (V){0.32f, -0.3f}, 0.08f));
        return uni(d, sd_circle(p, (V){0.32f, 0.3f}, 0.08f));
    }
    case IC_LOCK: {
        float d = sd_box(p, (V){0, 0.25f}, (V){0.5f, 0.38f}, 0.1f);
        return uni(d, sd_arc(p, (V){0, -0.15f}, 0.3f, W, PI, PI * 2));
    }
    case IC_CLOUD_OFF: {
        float c = uni(uni(sd_circle(p, (V){-0.28f, 0.12f}, 0.3f), sd_circle(p, (V){0.12f, -0.05f}, 0.4f)),
                      sd_box(p, (V){0, 0.27f}, (V){0.55f, 0.16f}, 0.15f));
        float d = outline(c, W * 0.8f);
        d = diff(d, sd_seg(p, (V){-0.7f, -0.6f}, (V){0.7f, 0.65f}, W * 2.4f));
        return uni(d, sd_seg(p, (V){-0.7f, -0.6f}, (V){0.7f, 0.65f}, W * 0.8f));
    }
    case IC_CLOCK: {
        float d = outline(sd_circle(p, (V){0, 0}, 0.62f), W * 0.9f);
        d = uni(d, sd_seg(p, (V){0, 0}, (V){0, -0.36f}, W * 0.9f));
        return uni(d, sd_seg(p, (V){0, 0}, (V){0.26f, 0.12f}, W * 0.9f));
    }
    case IC_SERIES: {
        float d = sd_box(p, (V){-0.42f, 0.08f}, (V){0.14f, 0.5f}, 0.04f);
        d = uni(d, sd_box(p, (V){-0.04f, 0.0f}, (V){0.14f, 0.58f}, 0.04f));
        V lean[4] = {{0.2f, -0.36f}, {0.44f, -0.44f}, {0.72f, 0.5f}, {0.48f, 0.58f}};
        return uni(d, sd_poly(p, lean, 4) - 0.02f);
    }
    case IC_PS_CIRCLE:
        return outline(sd_circle(p, (V){0, 0}, 0.5f), W);
    case IC_PS_SQUARE:
        return outline(sd_box(p, (V){0, 0}, (V){0.46f, 0.46f}, 0.04f), W);
    case IC_PS_TRIANGLE: {
        V t[3] = {{0, -0.52f}, {0.56f, 0.44f}, {-0.56f, 0.44f}};
        return outline(sd_poly(p, t, 3), W);
    }
    case IC_PS_CROSS:
        return uni(sd_seg(p, (V){-0.48f, -0.48f}, (V){0.48f, 0.48f}, W), sd_seg(p, (V){0.48f, -0.48f}, (V){-0.48f, 0.48f}, W));
    case IC_HOME: {
        V roof[3] = {{-0.66f, -0.02f}, {0, -0.62f}, {0.66f, -0.02f}};
        float d = uni(sd_poly(p, roof, 3), sd_box(p, (V){0, 0.3f}, (V){0.46f, 0.34f}, 0.04f));
        return diff(d, sd_box(p, (V){0, 0.42f}, (V){0.13f, 0.22f}, 0.03f));
    }
    }
    return 1;
}

float icons_optical_dx(int id) { return id == IC_PLAY ? 0.03f : 0; }

static void raster_at(int id, uint8_t *dst, int stride, int cell, float ox, float oy) {
    float half = cell * 0.5f;
    for (int y = 0; y < cell; y++)
        for (int x = 0; x < cell; x++) {
            V p = {(x + 0.5f - half) / half + ox, (y + 0.5f - half) / half + oy};
            float d = icon_sdf(id, p) * half; /* in pixels */
            float a = 0.5f - d;
            dst[y * stride + x] = (uint8_t)(a <= 0 ? 0 : a >= 1 ? 255 : a * 255);
        }
}

/* Ink bounding box of icon id in unit space, sampled on a fine grid. */
static void ink_box(int id, float *x0, float *y0, float *x1, float *y1) {
    const int N = 256;
    *x0 = *y0 = 1e9f;
    *x1 = *y1 = -1e9f;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            V p = {(x + 0.5f) / N * 2.4f - 1.2f, (y + 0.5f) / N * 2.4f - 1.2f};
            if (icon_sdf(id, p) > 0) continue;
            if (p.x < *x0) *x0 = p.x;
            if (p.x > *x1) *x1 = p.x;
            if (p.y < *y0) *y0 = p.y;
            if (p.y > *y1) *y1 = p.y;
        }
}

void icons_raster(int id, uint8_t *dst, int stride, int cell) {
    float x0, y0, x1, y1;
    ink_box(id, &x0, &y0, &x1, &y1);
    float ox = x1 >= x0 ? (x0 + x1) * 0.5f : 0, oy = y1 >= y0 ? (y0 + y1) * 0.5f : 0;
    ox -= icons_optical_dx(id) * 2.0f; /* one cell is 2 units wide; moving the origin left moves ink right */
    raster_at(id, dst, stride, cell, ox, oy);
}
