#include "color.h"

#include <math.h>

void rgb_to_hsl(Rgb c, float *h, float *s, float *l) {
    float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
    *l = (mx + mn) / 2;
    if (mx == mn) { *h = 0; *s = 0; return; }
    float d = mx - mn;
    *s = *l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
    if (mx == r) *h = (g - b) / d + (g < b ? 6 : 0);
    else if (mx == g) *h = (b - r) / d + 2;
    else *h = (r - g) / d + 4;
    *h /= 6;
}

static float hue2rgb(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.f / 6) return p + (q - p) * 6 * t;
    if (t < 0.5f) return q;
    if (t < 2.f / 3) return p + (q - p) * (2.f / 3 - t) * 6;
    return p;
}

Rgb hsl_to_rgb(float h, float s, float l) {
    float r, g, b;
    if (s == 0) { r = g = b = l; }
    else {
        float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
        float p = 2 * l - q;
        r = hue2rgb(p, q, h + 1.f / 3);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1.f / 3);
    }
    Rgb o = {(uint8_t)lroundf(r * 255), (uint8_t)lroundf(g * 255), (uint8_t)lroundf(b * 255)};
    return o;
}

Rgb accent_from_rgba(const uint8_t *px, int w, int h) {
    double sr = 0, sg = 0, sb = 0, sw = 0;
    for (int i = 0; i < w * h; i++) {
        Rgb c = {px[i * 4], px[i * 4 + 1], px[i * 4 + 2]};
        float hh, s, l;
        rgb_to_hsl(c, &hh, &s, &l);
        /* favour saturated mid-tones */
        double wgt = s * s * (1.0 - fabs(l - 0.5) * 1.6);
        if (wgt <= 0) continue;
        sr += c.r * wgt; sg += c.g * wgt; sb += c.b * wgt; sw += wgt;
    }
    if (sw < w * h * 0.004) { Rgb amber = {247, 168, 55}; return amber; }
    Rgb avg = {(uint8_t)(sr / sw), (uint8_t)(sg / sw), (uint8_t)(sb / sw)};
    float hh, s, l;
    rgb_to_hsl(avg, &hh, &s, &l);
    if (s < 0.55f) s = 0.55f;
    if (l < 0.45f) l = 0.45f;
    if (l > 0.70f) l = 0.70f;
    return hsl_to_rgb(hh, s, l);
}

Rgb average_rgba(const uint8_t *px, int w, int h, float darken) {
    double r = 0, g = 0, b = 0;
    int n = w * h;
    for (int i = 0; i < n; i++) { r += px[i * 4]; g += px[i * 4 + 1]; b += px[i * 4 + 2]; }
    if (!n) n = 1;
    float k = 1 - darken;
    Rgb o = {(uint8_t)(r / n * k), (uint8_t)(g / n * k), (uint8_t)(b / n * k)};
    return o;
}
