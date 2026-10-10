#include "gfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vita2d.h>

#include "font_metrics.h"

/* ---- fonts ------------------------------------------------------------------- */

typedef struct { int size; vita2d_font *f; } SizedFont;
#define MAX_SIZES 16
static SizedFont g_fonts[2][MAX_SIZES];
static void *g_font_data[2];
static unsigned g_font_len[2];

static void *slurp(const char *path, unsigned *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *b = malloc(n);
    if (b && fread(b, 1, n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    *len = (unsigned)n;
    return b;
}

/* vita2d caches each glyph at the first size it is drawn and scales it for
 * other sizes, so every size gets its own font object. */
static vita2d_font *font(FontW w, int size) {
    SizedFont *fs = g_fonts[w];
    for (int i = 0; i < MAX_SIZES; i++) {
        if (fs[i].size == size) return fs[i].f;
        if (!fs[i].f) {
            fs[i].f = vita2d_load_font_mem(g_font_data[w], g_font_len[w]);
            fs[i].size = size;
            return fs[i].f;
        }
    }
    return fs[MAX_SIZES - 1].f;
}

static float cap_px(FontW w, int size) {
    return (w == F_BOLD ? FM700_CAP / (float)FM700_UPM : FM500_CAP / (float)FM500_UPM) * size;
}

static float bearing(FontW w, int size, unsigned char c, int right) {
    if (c < 32 || c > 126) return 0;
    const short *t = w == F_BOLD ? (right ? fm700_rsb : fm700_lsb) : (right ? fm500_rsb : fm500_lsb);
    float upm = w == F_BOLD ? FM700_UPM : FM500_UPM;
    return t[c - 32] * size / upm;
}

static unsigned char last_char(const char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ')) n--;
    return n ? (unsigned char)s[n - 1] : 0;
}

/* ---- icons ---------------------------------------------------------------------- */

typedef struct { int id, size; vita2d_texture *t; } IconTex;
#define MAX_ICON_TEX 160
static IconTex g_icons[MAX_ICON_TEX];
static int g_nicons;

static vita2d_texture *icon_tex(int id, int size) {
    for (int i = 0; i < g_nicons; i++)
        if (g_icons[i].id == id && g_icons[i].size == size) return g_icons[i].t;
    if (g_nicons == MAX_ICON_TEX) return NULL;
    uint8_t *a = malloc(size * size);
    if (!a) return NULL;
    icons_raster(id, a, size, size);
    vita2d_texture *t = vita2d_create_empty_texture(size, size);
    if (t) {
        uint32_t *px = vita2d_texture_get_datap(t);
        unsigned stride = vita2d_texture_get_stride(t) / 4;
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) px[y * stride + x] = (uint32_t)a[y * size + x] << 24 | 0xFFFFFF;
        g_icons[g_nicons++] = (IconTex){id, size, t};
    }
    free(a);
    return t;
}

/* ---- setup ---------------------------------------------------------------------- */

int gfx_init(const char *font_dir) {
    char path[256];
    snprintf(path, sizeof path, "%s/Manrope-500.ttf", font_dir);
    g_font_data[F_REG] = slurp(path, &g_font_len[F_REG]);
    snprintf(path, sizeof path, "%s/Manrope-700.ttf", font_dir);
    g_font_data[F_BOLD] = slurp(path, &g_font_len[F_BOLD]);
    return g_font_data[0] && g_font_data[1] ? 0 : -1;
}

void gfx_exit(void) {
    for (int w = 0; w < 2; w++)
        for (int i = 0; i < MAX_SIZES; i++)
            if (g_fonts[w][i].f) vita2d_free_font(g_fonts[w][i].f);
    for (int i = 0; i < g_nicons; i++) vita2d_free_texture(g_icons[i].t);
}

uint32_t gfx_alpha(uint32_t c, float a) {
    uint32_t al = (uint32_t)((c >> 24) * (a < 0 ? 0 : a > 1 ? 1 : a));
    return (c & 0xFFFFFF) | al << 24;
}

uint32_t gfx_mix(uint32_t a, uint32_t b, float t) {
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        float x = (a >> sh & 0xFF) * (1 - t) + (b >> sh & 0xFF) * t;
        out |= (uint32_t)(x + 0.5f) << sh;
    }
    return out;
}

/* ---- shapes ----------------------------------------------------------------------- */

#define CORNER_SEGS 8

static int perimeter(float x, float y, float w, float h, float r, float *px, float *py) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    const float cx[4] = {x + w - r, x + w - r, x + r, x + r};
    const float cy[4] = {y + r, y + h - r, y + h - r, y + r};
    const float a0[4] = {-M_PI / 2, 0, M_PI / 2, M_PI};
    int n = 0;
    for (int c = 0; c < 4; c++)
        for (int i = 0; i <= CORNER_SEGS; i++) {
            float a = a0[c] + (float)M_PI / 2 * i / CORNER_SEGS;
            px[n] = cx[c] + r * cosf(a);
            py[n] = cy[c] + r * sinf(a);
            n++;
        }
    return n;
}

static vita2d_color_vertex *verts(int n) {
    return vita2d_pool_memalign(n * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
}

void gfx_rect(float x, float y, float w, float h, uint32_t c) {
    vita2d_draw_rectangle(x, y, w, h, c);
}

void gfx_rrect(float x, float y, float w, float h, float r, uint32_t c) {
    if (r < 0.5f) { gfx_rect(x, y, w, h, c); return; }
    float px[4 * (CORNER_SEGS + 1)], py[4 * (CORNER_SEGS + 1)];
    int n = perimeter(x, y, w, h, r, px, py);
    vita2d_color_vertex *v = verts(n * 3);
    if (!v) return;
    float mx = x + w / 2, my = y + h / 2;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        v[i * 3 + 0] = (vita2d_color_vertex){mx, my, 0.5f, c};
        v[i * 3 + 1] = (vita2d_color_vertex){px[i], py[i], 0.5f, c};
        v[i * 3 + 2] = (vita2d_color_vertex){px[j], py[j], 0.5f, c};
    }
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, n * 3);
}

void gfx_ring(float x, float y, float w, float h, float r, float t, uint32_t c) {
    float ox[4 * (CORNER_SEGS + 1)], oy[4 * (CORNER_SEGS + 1)];
    float ix[4 * (CORNER_SEGS + 1)], iy[4 * (CORNER_SEGS + 1)];
    int n = perimeter(x - t, y - t, w + 2 * t, h + 2 * t, r + t, ox, oy);
    perimeter(x, y, w, h, r, ix, iy);
    vita2d_color_vertex *v = verts(n * 6);
    if (!v) return;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        v[i * 6 + 0] = (vita2d_color_vertex){ox[i], oy[i], 0.5f, c};
        v[i * 6 + 1] = (vita2d_color_vertex){ox[j], oy[j], 0.5f, c};
        v[i * 6 + 2] = (vita2d_color_vertex){ix[i], iy[i], 0.5f, c};
        v[i * 6 + 3] = (vita2d_color_vertex){ix[i], iy[i], 0.5f, c};
        v[i * 6 + 4] = (vita2d_color_vertex){ox[j], oy[j], 0.5f, c};
        v[i * 6 + 5] = (vita2d_color_vertex){ix[j], iy[j], 0.5f, c};
    }
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, n * 6);
}

void gfx_circle(float cx, float cy, float r, uint32_t c) { gfx_rrect(cx - r, cy - r, 2 * r, 2 * r, r, c); }

void gfx_vgrad(float x, float y, float w, float h, uint32_t top, uint32_t bottom) {
    vita2d_color_vertex *v = verts(6);
    if (!v) return;
    v[0] = (vita2d_color_vertex){x, y, 0.5f, top};
    v[1] = (vita2d_color_vertex){x + w, y, 0.5f, top};
    v[2] = (vita2d_color_vertex){x, y + h, 0.5f, bottom};
    v[3] = (vita2d_color_vertex){x + w, y, 0.5f, top};
    v[4] = (vita2d_color_vertex){x + w, y + h, 0.5f, bottom};
    v[5] = (vita2d_color_vertex){x, y + h, 0.5f, bottom};
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, 6);
}

/* ---- text -------------------------------------------------------------------------- */

int gfx_text_w(FontW w, int size, const char *s) {
    if (!s || !*s) return 0;
    vita2d_font *f = font(w, size);
    return f ? vita2d_font_text_width(f, size, s) : 0;
}

float gfx_ink_w(FontW w, int size, const char *s) {
    if (!s || !*s) return 0;
    return gfx_text_w(w, size, s) - bearing(w, size, (unsigned char)s[0], 0) - bearing(w, size, last_char(s), 1);
}

int gfx_text(FontW w, int size, float x, float y, uint32_t c, const char *s) {
    if (!s || !*s) return 0;
    vita2d_font *f = font(w, size);
    if (!f) return 0;
    int baseline = (int)lroundf(y + cap_px(w, size));
    vita2d_font_draw_text(f, (int)lroundf(x), baseline, c, size, s);
    return vita2d_font_text_width(f, size, s);
}

void gfx_text_center(FontW w, int size, float cx, float cy, uint32_t c, const char *s) {
    if (!s || !*s) return;
    float ink = gfx_ink_w(w, size, s);
    float x = cx - ink / 2 - bearing(w, size, (unsigned char)s[0], 0);
    gfx_text(w, size, x, cy - cap_px(w, size) / 2, c, s);
}

void gfx_text_vc(FontW w, int size, float x, float cy, uint32_t c, const char *s) {
    gfx_text(w, size, x - bearing(w, size, s && *s ? (unsigned char)s[0] : 0, 0), cy - cap_px(w, size) / 2, c, s);
}

void gfx_text_right(FontW w, int size, float x, float y, uint32_t c, const char *s) {
    if (!s || !*s) return;
    float adv = gfx_text_w(w, size, s);
    gfx_text(w, size, x - adv + bearing(w, size, last_char(s), 1), y, c, s);
}

void gfx_text_right_vc(FontW w, int size, float x, float cy, uint32_t c, const char *s) {
    gfx_text_right(w, size, x, cy - cap_px(w, size) / 2, c, s);
}

static int utf8_prev(const char *s, int i) {
    do i--; while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80);
    return i;
}

void gfx_fit(FontW w, int size, const char *s, float max_w, char *out, int n) {
    snprintf(out, n, "%s", s ? s : "");
    if (gfx_text_w(w, size, out) <= max_w) return;
    int len = (int)strlen(out);
    while (len > 0) {
        len = utf8_prev(out, len);
        while (len > 0 && out[len - 1] == ' ') len--;
        if (len + 4 > n) continue;
        memcpy(out + len, "\xE2\x80\xA6", 4);
        if (gfx_text_w(w, size, out) <= max_w) return;
    }
    out[0] = 0;
}

/* Next line of s starting at *pos that fits max_w; returns its byte length. */
static int next_line(FontW w, int size, const char *s, int pos, float max_w, char *line, int n) {
    int best = -1, i = pos;
    while (s[i] == ' ') i++;
    int start = i;
    for (;;) {
        while (s[i] && s[i] != ' ' && s[i] != '\n') i++;
        int len = i - start;
        if (len >= n) len = n - 1;
        memcpy(line, s + start, len);
        line[len] = 0;
        if (gfx_text_w(w, size, line) > max_w && best >= 0) break;
        best = i;
        if (!s[i] || s[i] == '\n') break;
        i++;
    }
    int len = best - start;
    if (len >= n) len = n - 1;
    memcpy(line, s + start, len);
    line[len] = 0;
    return best - pos;
}

int gfx_wrap(FontW w, int size, float x, float y, float max_w, float line_h, int max_lines, uint32_t c, const char *s) {
    if (!s) return 0;
    char line[512];
    int pos = 0, lines = 0, slen = (int)strlen(s);
    while (pos < slen && lines < max_lines) {
        int used = next_line(w, size, s, pos, max_w, line, sizeof line);
        if (used <= 0) break;
        pos += used;
        if (s[pos] == '\n') pos++;
        if (lines == max_lines - 1 && pos < slen) {
            char rest[1100];
            snprintf(rest, sizeof rest, "%s %.540s", line, s + pos);
            gfx_fit(w, size, rest, max_w, line, sizeof line);
        }
        gfx_text(w, size, x, y + lines * line_h, c, line);
        lines++;
    }
    return lines;
}

int gfx_wrap_count(FontW w, int size, float max_w, const char *s) {
    if (!s) return 0;
    char line[512];
    int pos = 0, lines = 0, slen = (int)strlen(s);
    while (pos < slen && lines < 999) {
        int used = next_line(w, size, s, pos, max_w, line, sizeof line);
        if (used <= 0) break;
        pos += used;
        if (s[pos] == '\n') pos++;
        lines++;
    }
    return lines;
}

/* ---- icons and glyphs ----------------------------------------------------------- */

void gfx_icon(int id, float cx, float cy, float size, uint32_t c) {
    int px = (int)lroundf(size);
    vita2d_texture *t = icon_tex(id, px);
    if (!t) return;
    float x = (float)(int)lroundf(cx - px / 2.0f), y = (float)(int)lroundf(cy - px / 2.0f);
    vita2d_draw_texture_tint(t, x, y, c);
}

static const uint32_t kGlyphColour[4] = {
    RGBA(124, 178, 232, 255), /* cross: blue */
    RGBA(255, 102, 102, 255), /* circle: red */
    RGBA(232, 140, 200, 255), /* square: pink */
    RGBA(64, 226, 160, 255),  /* triangle: green */
};

static const char *pill_label(Glyph g) {
    switch (g) {
    case G_L: return "L";
    case G_R: return "R";
    case G_START: return "START";
    case G_SELECT: return "SELECT";
    default: return "";
    }
}

float gfx_glyph_w(Glyph g, float size) {
    if (g <= G_TRIANGLE || g == G_DPAD) return size;
    int fs = (int)(size * 0.5f);
    float w = gfx_ink_w(F_BOLD, fs, pill_label(g)) + size * 0.7f;
    return w < size * 1.3f ? size * 1.3f : w;
}

float gfx_glyph(Glyph g, float cx, float cy, float size) {
    float r = size / 2;
    if (g <= G_TRIANGLE) {
        static const int ic[4] = {IC_PS_CROSS, IC_PS_CIRCLE, IC_PS_SQUARE, IC_PS_TRIANGLE};
        gfx_circle(cx, cy, r, C_PANEL2);
        gfx_icon(ic[g], cx, cy, size * 0.62f, kGlyphColour[g]);
        return size;
    }
    if (g == G_DPAD) {
        float a = size * 0.26f, b = size * 0.5f;
        gfx_rrect(cx - a / 2, cy - b, a, 2 * b, 2, C_SUB);
        gfx_rrect(cx - b, cy - a / 2, 2 * b, a, 2, C_SUB);
        return size;
    }
    float w = gfx_glyph_w(g, size);
    gfx_rrect(cx - w / 2, cy - r * 0.8f, w, size * 0.8f, r * 0.8f, C_PANEL2);
    gfx_text_center(F_BOLD, (int)(size * 0.5f), cx, cy, C_SUB, pill_label(g));
    return w;
}
