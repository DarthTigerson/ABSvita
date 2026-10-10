/* Plain data types shared by the core, platform and UI layers. */
#ifndef ABSVITA_MODELS_H
#define ABSVITA_MODELS_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    double start, end;
    char title[96];
} Chapter;

typedef struct {
    int index;
    char ino[40];
    char content_url[160]; /* "/api/items/<id>/file/<ino>" */
    char codec[16];        /* "aac", "mp3", ... */
    char ext[8];           /* ".m4b", ".mp3", ... */
    char title[96];        /* usually the file name */
    double start_offset;   /* book time at which this track starts */
    double duration;
    uint64_t size;
} Track;

typedef struct {
    char id[40];
    char title[160];
    char author[96];
    char series[96];
    double duration;
    uint64_t added_at;
    /* progress (merged from /api/me) */
    float progress;
    double current_time;
    bool finished;
    uint64_t progress_updated; /* ms since epoch */
} BookSummary;

typedef struct {
    char id[40];
    char title[160];
    char author[96];
    char narrator[96];
    char *description; /* malloc'd, may be NULL */
    double duration;
    int ntracks;
    Track *tracks; /* malloc'd */
    int nchapters;
    Chapter *chapters; /* malloc'd */
} BookDetail;

typedef struct {
    char id[48];
    double current_time;
    BookDetail book;
} PlaySession;

void book_detail_free(BookDetail *b);

#endif
