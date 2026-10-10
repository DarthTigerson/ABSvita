/* Audiobookshelf (v2.31) JSON <-> plain C structs. All parsers tolerate
 * missing or null fields and return 0 on success, -1 on malformed JSON. */
#ifndef ABSVITA_ABS_JSON_H
#define ABSVITA_ABS_JSON_H
#include <stddef.h>
#include <stdint.h>

#include "models.h"

int abs_parse_login(const char *json, char *access, size_t an, char *refresh, size_t rn, char *user_id, size_t un);
/* id of the first library with mediaType "book" */
int abs_parse_libraries(const char *json, char *book_lib_id, size_t n);
/* Appends up to max-*count summaries to out; *total gets the server total. */
int abs_parse_items_page(const char *json, BookSummary *out, int max, int *count, int *total);
/* Merges mediaProgress[] (from /api/me or the login user) into books by id. */
int abs_parse_me_progress(const char *json, BookSummary *books, int n);
/* Ids from /api/me/items-in-progress, most recent first. Returns count. */
int abs_parse_in_progress(const char *json, char ids[][40], int max);
int abs_parse_item(const char *json, BookDetail *out);
/* -2: a valid session for a book with no audio tracks */
int abs_parse_play(const char *json, PlaySession *out);
/* When a book has no chapters, uses one chapter per track (titled from the file name). */
void abs_chapters_from_tracks(BookDetail *b);

/* Serialises a book in the /api/items/:id?expanded=1 shape, so abs_parse_item
 * reads it back (used for downloads/<id>/meta.json). malloc'd, NULL on error. */
char *abs_detail_to_json(const BookDetail *b);

/* Request bodies. Return the length written (or -1 if it didn't fit). */
int abs_build_login_body(const char *user, const char *pass, char *out, size_t n);
int abs_build_play_body(const char *device_id, const char *model, const char *version, char *out, size_t n);
int abs_build_sync_body(double current_time, double listened, double duration, char *out, size_t n);
int abs_build_progress_body(double current_time, double duration, int finished, char *out, size_t n);
/* POST /api/session/local body for progress made offline (httpc has no PATCH).
 * The server only applies it when updated_ms is newer than its own progress. */
int abs_build_local_session_body(const char *item_id, const char *device_id, double current_time, double duration,
                                 double listened, uint64_t updated_ms, char *out, size_t n);

#endif
