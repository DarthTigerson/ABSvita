/* Offline downloads to <dir>/<itemId>/ :
 *   meta.json   book detail (abs_parse_item format)
 *   NN<ext>     track files (".part" while incomplete)
 *   .done       written when every track is complete
 * One background thread downloads one book at a time. Thread-safe. */
#ifndef ABSVITA_DOWNLOADS_H
#define ABSVITA_DOWNLOADS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "models.h"

typedef enum { DL_NONE, DL_QUEUED, DL_ACTIVE, DL_PAUSED, DL_FAILED, DL_DONE } DlState;

typedef struct {
    char id[40];
    char title[160];
    char author[96];
    double duration;
    DlState state;
    uint64_t done_bytes, total_bytes;
    char error[64];
} DlInfo;

/* free_space: bytes free on the device holding dir (NULL: don't check). Scans dir. */
void dl_init(const char *dir, int64_t (*free_space)(const char *dir));
void dl_exit(void);
/* Queues the book (or resumes a paused/failed one). 0 ok. */
int dl_start(const char *id);
/* Stops after the current chunk; partial files are kept. */
void dl_pause(const char *id);
/* Stops if needed and removes the files. */
int dl_delete(const char *id);
DlState dl_state(const char *id, float *frac);
bool dl_is_done(const char *id);
bool dl_busy(void);                 /* something is queued or downloading */
int dl_list(DlInfo *out, int max);  /* in the order they were added */
int dl_load_meta(const char *id, BookDetail *out);
void dl_track_path(const char *id, int i, const char *ext, char *out, size_t n);
uint64_t dl_used_bytes(void);
int64_t dl_free_bytes(void);        /* -1 when unknown */

#endif
