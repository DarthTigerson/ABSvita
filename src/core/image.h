/* Image helpers for cover art (pure C). */
#ifndef ABSVITA_IMAGE_H
#define ABSVITA_IMAGE_H
#include <stdint.h>

/* Box resize + centre crop to a square of size out_sz. src has comps bytes per
 * pixel (1..4); dst is out_sz*out_sz RGBA8. */
void img_crop_resize(const uint8_t *src, int sw, int sh, int comps, uint8_t *dst, int out_sz);

#endif
