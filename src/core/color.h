#ifndef ABSVITA_COLOR_H
#define ABSVITA_COLOR_H
#include <stdint.h>

typedef struct { uint8_t r, g, b; } Rgb;

/* Accent colour for a cover: saturation-weighted average, then lightness
 * clamped to [0.45, 0.70] and saturation lifted so it reads on dark UI.
 * px is RGBA8 (r,g,b,a byte order). Falls back to amber for grey covers. */
Rgb accent_from_rgba(const uint8_t *px, int w, int h);
/* Average colour (for backdrops), darkened by factor 0..1. */
Rgb average_rgba(const uint8_t *px, int w, int h, float darken);
void rgb_to_hsl(Rgb c, float *h, float *s, float *l);
Rgb hsl_to_rgb(float h, float s, float l);

#endif
