/* Book time <-> (track, local time) mapping and chapter navigation. */
#ifndef ABSVITA_TIMELINE_H
#define ABSVITA_TIMELINE_H
#include "models.h"

/* Track index containing book time t (clamped to [0, duration]); *local is
 * the time inside that track. A time exactly on a boundary picks the later
 * track. Returns -1 if there are no tracks. */
int tl_locate(const Track *tracks, int n, double t, double *local);
double tl_duration(const Track *tracks, int n);

/* Chapter index containing t, or -1 when there are no chapters. */
int tl_chapter_at(const Chapter *ch, int n, double t);
/* Target time for "next chapter" (-1 if already in the last chapter). */
double tl_next_chapter(const Chapter *ch, int n, double t);
/* Target time for "previous chapter": the start of the current chapter when
 * more than 3 s into it, otherwise the start of the previous one. */
double tl_prev_chapter(const Chapter *ch, int n, double t);

#endif
