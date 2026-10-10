/* Drawing layer over vita2d: theme colours, fonts (one vita2d font per size),
 * ink-centred text, rounded shapes, icons and PlayStation button glyphs. */
#ifndef ABSVITA_GFX_H
#define ABSVITA_GFX_H
#include <stdbool.h>
#include <stdint.h>

#include "icons.h"

#define SCREEN_W 960
#define SCREEN_H 544

/* colours are 0xAABBGGRR (vita2d) */
#define RGBA(r, g, b, a) ((uint32_t)(((a) & 0xFF) << 24 | ((b) & 0xFF) << 16 | ((g) & 0xFF) << 8 | ((r) & 0xFF)))
#define C_BG RGBA(15, 17, 21, 255)
#define C_PANEL RGBA(26, 29, 36, 255)
#define C_PANEL2 RGBA(36, 40, 50, 255)
#define C_LINE RGBA(52, 57, 70, 255)
#define C_TEXT RGBA(242, 242, 245, 255)
#define C_SUB RGBA(154, 160, 174, 255)
#define C_DIM RGBA(104, 110, 124, 255)
#define C_ACCENT RGBA(245, 165, 36, 255)
#define C_ON_ACCENT RGBA(24, 18, 8, 255)
#define C_ERROR RGBA(255, 107, 107, 255)

typedef enum { F_REG, F_BOLD } FontW;

int gfx_init(const char *font_dir);
void gfx_exit(void);
uint32_t gfx_alpha(uint32_t c, float a);
uint32_t gfx_mix(uint32_t a, uint32_t b, float t);

void gfx_rect(float x, float y, float w, float h, uint32_t c);
void gfx_rrect(float x, float y, float w, float h, float r, uint32_t c);
/* ring of width t around (outside) the rounded rect */
void gfx_ring(float x, float y, float w, float h, float r, float t, uint32_t c);
void gfx_circle(float cx, float cy, float r, uint32_t c);
/* vertical gradient */
void gfx_vgrad(float x, float y, float w, float h, uint32_t top, uint32_t bottom);

/* Advance width as drawn. */
int gfx_text_w(FontW w, int size, const char *s);
/* Width of the ink (glyph outlines) of s. */
float gfx_ink_w(FontW w, int size, const char *s);
/* Draws with the top of capital letters at y. Returns the advance width. */
int gfx_text(FontW w, int size, float x, float y, uint32_t c, const char *s);
/* Ink-centred on (cx, cy): horizontally on the glyph outlines, vertically on the cap height. */
void gfx_text_center(FontW w, int size, float cx, float cy, uint32_t c, const char *s);
/* Left-aligned, vertically centred on the cap height at cy. */
void gfx_text_vc(FontW w, int size, float x, float cy, uint32_t c, const char *s);
/* Right-aligned (ink) at x, cap top at y. */
void gfx_text_right(FontW w, int size, float x, float y, uint32_t c, const char *s);
/* Right-aligned (ink) at x, vertically centred on the cap height at cy. */
void gfx_text_right_vc(FontW w, int size, float x, float cy, uint32_t c, const char *s);
/* Truncates s with an ellipsis to fit max_w. */
void gfx_fit(FontW w, int size, const char *s, float max_w, char *out, int n);
/* Word-wraps into at most max_lines lines (the last one ellipsised). Returns lines drawn.
 * line_h: distance between cap tops. */
int gfx_wrap(FontW w, int size, float x, float y, float max_w, float line_h, int max_lines, uint32_t c, const char *s);
/* Lines s would wrap into. */
int gfx_wrap_count(FontW w, int size, float max_w, const char *s);

/* Icon centred on (cx, cy), box size px. */
void gfx_icon(int id, float cx, float cy, float size, uint32_t c);

typedef enum { G_CROSS, G_CIRCLE, G_SQUARE, G_TRIANGLE, G_L, G_R, G_START, G_SELECT, G_DPAD } Glyph;
/* Button glyph centred on (cx, cy); returns its width. */
float gfx_glyph(Glyph g, float cx, float cy, float size);
float gfx_glyph_w(Glyph g, float size);

#endif
