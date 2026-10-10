/* Cover art: fetched and decoded on a worker (JPEG -> 256 px square RGBA plus an
 * accent colour), uploaded to textures on the UI thread, cached on the memory card. */
#ifndef ABSVITA_COVERS_H
#define ABSVITA_COVERS_H
#include <stdint.h>

void covers_init(const char *cache_dir);
void covers_exit(void);
/* Uploads finished decodes (a couple per frame). */
void covers_tick(void);
/* Draws the cover of book id (or a placeholder while it loads). */
void covers_draw(const char *id, float x, float y, float size, float alpha);
/* Accent colour of the cover (fallback until it is known). */
uint32_t covers_accent(const char *id, uint32_t fallback);
/* Average (darkened) colour for backdrops. */
uint32_t covers_backdrop(const char *id, uint32_t fallback);

#endif
