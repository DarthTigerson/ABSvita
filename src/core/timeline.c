#include "timeline.h"

#include <stdlib.h>

double tl_duration(const Track *tracks, int n) {
    if (n <= 0) return 0;
    return tracks[n - 1].start_offset + tracks[n - 1].duration;
}

int tl_locate(const Track *tracks, int n, double t, double *local) {
    if (n <= 0) return -1;
    if (t < 0) t = 0;
    int i = n - 1;
    for (int k = 0; k < n; k++) {
        if (t < tracks[k].start_offset + tracks[k].duration) { i = k; break; }
    }
    double l = t - tracks[i].start_offset;
    if (l < 0) l = 0;
    if (l > tracks[i].duration) l = tracks[i].duration;
    if (local) *local = l;
    return i;
}

int tl_chapter_at(const Chapter *ch, int n, double t) {
    if (n <= 0) return -1;
    for (int i = n - 1; i >= 0; i--)
        if (t >= ch[i].start) return i;
    return 0;
}

double tl_next_chapter(const Chapter *ch, int n, double t) {
    int i = tl_chapter_at(ch, n, t);
    if (i < 0 || i + 1 >= n) return -1;
    return ch[i + 1].start;
}

double tl_prev_chapter(const Chapter *ch, int n, double t) {
    int i = tl_chapter_at(ch, n, t);
    if (i < 0) return 0;
    if (t - ch[i].start > 3.0 || i == 0) return ch[i].start;
    return ch[i - 1].start;
}

void book_detail_free(BookDetail *b) {
    if (!b) return;
    free(b->description);
    free(b->tracks);
    free(b->chapters);
    b->description = NULL;
    b->tracks = NULL;
    b->chapters = NULL;
    b->ntracks = b->nchapters = 0;
}
