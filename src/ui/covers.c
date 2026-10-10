#include "covers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <vita2d.h>

#include "abs_client.h"
#include "color.h"
#include "gfx.h"
#include "image.h"
#include "osal.h"
#include "strutil.h"
#include "worker.h"

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define SIDE 256
#define MAX_COVERS 72

typedef enum { CV_EMPTY, CV_QUEUED, CV_DECODED, CV_READY, CV_FAILED } CoverState;

typedef struct {
    char id[40];
    volatile CoverState state;
    int gen;
    uint8_t *rgba;       /* SIDE*SIDE*4 while CV_DECODED */
    vita2d_texture *tex;
    uint32_t accent, backdrop;
    uint64_t used;
} Cover;

static Cover g_c[MAX_COVERS];
static Mutex g_lock;
static char g_dir[200];
static uint64_t g_frame;

typedef struct { int slot, gen; char id[40]; } Job;

static uint32_t to_c(Rgb c) { return RGBA(c.r, c.g, c.b, 255); }

static void job(void *ud) {
    Job *j = ud;
    char path[260];
    snprintf(path, sizeof path, "%s/%s.jpg", g_dir, j->id);
    uint8_t *data = NULL;
    uint32_t len = 0;
    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        len = (uint32_t)ftell(f);
        fseek(f, 0, SEEK_SET);
        data = malloc(len);
        if (data && fread(data, 1, len, f) != len) { free(data); data = NULL; }
        fclose(f);
    }
    if (!data && absc_fetch_cover(j->id, 320, &data, &len) == ABS_OK && data) {
        FILE *o = fopen(path, "wb");
        if (o) { fwrite(data, 1, len, o); fclose(o); }
    }
    uint8_t *sq = NULL;
    Rgb accent = {245, 165, 36}, back = {30, 30, 36};
    if (data) {
        int w, h, comps;
        uint8_t *px = stbi_load_from_memory(data, (int)len, &w, &h, &comps, 4);
        if (px) {
            sq = malloc(SIDE * SIDE * 4);
            if (sq) {
                img_crop_resize(px, w, h, 4, sq, SIDE);
                accent = accent_from_rgba(sq, SIDE, SIDE);
                back = average_rgba(sq, SIDE, SIDE, 0.35f);
            }
            stbi_image_free(px);
        } else {
            remove(path); /* corrupt cache entry */
        }
        free(data);
    }
    mutex_lock(&g_lock);
    Cover *c = &g_c[j->slot];
    if (c->gen == j->gen && c->state == CV_QUEUED) {
        c->rgba = sq;
        c->accent = to_c(accent);
        c->backdrop = to_c(back);
        c->state = sq ? CV_DECODED : CV_FAILED;
        sq = NULL;
    }
    mutex_unlock(&g_lock);
    free(sq);
    free(j);
}

void covers_init(const char *cache_dir) {
    mutex_init(&g_lock);
    str_copy(g_dir, sizeof g_dir, cache_dir);
    mkdir(g_dir, 0777);
}

void covers_exit(void) {
    for (int i = 0; i < MAX_COVERS; i++) {
        if (g_c[i].tex) vita2d_free_texture(g_c[i].tex);
        free(g_c[i].rgba);
    }
}

static Cover *lookup(const char *id, bool request) {
    if (!id || !id[0]) return NULL;
    for (int i = 0; i < MAX_COVERS; i++)
        if (g_c[i].state != CV_EMPTY && !strcmp(g_c[i].id, id)) {
            g_c[i].used = g_frame;
            return &g_c[i];
        }
    if (!request) return NULL;
    /* a free slot, else the least recently used one not drawn this frame */
    int slot = -1;
    uint64_t oldest = UINT64_MAX;
    mutex_lock(&g_lock);
    for (int i = 0; i < MAX_COVERS; i++) {
        if (g_c[i].state == CV_EMPTY) { slot = i; break; }
        if (g_c[i].state != CV_QUEUED && g_c[i].used + 1 < g_frame && g_c[i].used < oldest) { oldest = g_c[i].used; slot = i; }
    }
    if (slot < 0) { mutex_unlock(&g_lock); return NULL; }
    Cover *c = &g_c[slot];
    if (c->tex) { vita2d_wait_rendering_done(); vita2d_free_texture(c->tex); c->tex = NULL; }
    free(c->rgba);
    c->rgba = NULL;
    str_copy(c->id, sizeof c->id, id);
    c->gen++;
    c->state = CV_QUEUED;
    c->used = g_frame;
    c->accent = C_ACCENT;
    c->backdrop = RGBA(30, 30, 36, 255);
    mutex_unlock(&g_lock);
    Job *j = calloc(1, sizeof *j);
    if (j) {
        j->slot = slot;
        j->gen = c->gen;
        str_copy(j->id, sizeof j->id, id);
        if (!worker_post(g_covers, job, j)) { free(j); c->state = CV_EMPTY; return NULL; }
    }
    return c;
}

void covers_tick(void) {
    g_frame++;
    int uploads = 0;
    for (int i = 0; i < MAX_COVERS && uploads < 2; i++) {
        Cover *c = &g_c[i];
        if (c->state != CV_DECODED) continue;
        mutex_lock(&g_lock);
        uint8_t *px = c->rgba;
        c->rgba = NULL;
        mutex_unlock(&g_lock);
        vita2d_texture *t = px ? vita2d_create_empty_texture(SIDE, SIDE) : NULL;
        if (t) {
            uint8_t *dst = vita2d_texture_get_datap(t);
            unsigned stride = vita2d_texture_get_stride(t);
            for (int y = 0; y < SIDE; y++) memcpy(dst + y * stride, px + y * SIDE * 4, SIDE * 4);
            vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
        }
        free(px);
        c->tex = t;
        c->state = t ? CV_READY : CV_FAILED;
        uploads++;
    }
}

void covers_draw(const char *id, float x, float y, float size, float alpha) {
    Cover *c = lookup(id, true);
    if (c && c->state == CV_READY && c->tex) {
        vita2d_draw_texture_tint_scale(c->tex, (int)x, (int)y, size / SIDE, size / SIDE, gfx_alpha(0xFFFFFFFF, alpha));
        return;
    }
    gfx_rect(x, y, size, size, gfx_alpha(C_PANEL2, alpha));
    gfx_icon(IC_BOOK, x + size / 2, y + size / 2, (int)(size * 0.32f), gfx_alpha(C_DIM, alpha));
}

uint32_t covers_accent(const char *id, uint32_t fallback) {
    Cover *c = lookup(id, false);
    return c && (c->state == CV_READY || c->state == CV_DECODED) ? c->accent : fallback;
}

uint32_t covers_backdrop(const char *id, uint32_t fallback) {
    Cover *c = lookup(id, false);
    return c && (c->state == CV_READY || c->state == CV_DECODED) ? c->backdrop : fallback;
}
