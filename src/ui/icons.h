/* Icon ids + rasteriser (no vita2d dependency, so it builds on the host). */
#ifndef ABSVITA_ICONS_H
#define ABSVITA_ICONS_H
#include <stdint.h>

typedef enum {
    IC_PLAY, IC_PAUSE, IC_BACK, IC_FWD, IC_PREV, IC_NEXT, IC_LIST, IC_MOON,
    IC_SPEED, IC_DOWNLOAD, IC_CHECK, IC_TRASH, IC_GEAR, IC_SEARCH, IC_SORT, IC_WIFI,
    IC_BOOK, IC_HEADPHONES, IC_CLOSE, IC_SCREEN, IC_CHEVRON, IC_USER, IC_SERVER, IC_LOCK,
    IC_CLOUD_OFF, IC_CLOCK, IC_CHEVRON_LEFT, IC_HOME, IC_SERIES, IC_PS_CIRCLE, IC_PS_SQUARE, IC_PS_TRIANGLE, IC_PS_CROSS, IC_COUNT
} IconId;

/* Rasterises icon id into an A8 cell x cell region of dst, centred on its ink
 * bounding box (the play triangle gets a small optical nudge toward its point). */
void icons_raster(int id, uint8_t *dst, int stride, int cell);
/* Horizontal optical offset applied to an icon, as a fraction of the cell. */
float icons_optical_dx(int id);

#endif
