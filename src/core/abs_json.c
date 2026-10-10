#include "abs_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "strutil.h"

/* ---- helpers --------------------------------------------------------------- */

static const cJSON *path(const cJSON *o, const char *a, const char *b, const char *c) {
    if (o && a) o = cJSON_GetObjectItemCaseSensitive(o, a);
    if (o && b) o = cJSON_GetObjectItemCaseSensitive(o, b);
    if (o && c) o = cJSON_GetObjectItemCaseSensitive(o, c);
    return o;
}
static const char *jstr(const cJSON *o) { return cJSON_IsString(o) ? o->valuestring : NULL; }
static double jnum(const cJSON *o, double def) { return cJSON_IsNumber(o) ? o->valuedouble : def; }
static void jcopy(char *dst, size_t n, const cJSON *o) { str_copy(dst, n, jstr(o)); }
/* ids may be numbers in some ABS versions (ino) */
static void jid(char *dst, size_t n, const cJSON *o) {
    if (cJSON_IsNumber(o)) snprintf(dst, n, "%.0f", o->valuedouble);
    else jcopy(dst, n, o);
}

static void author_from(char *dst, size_t n, const cJSON *meta) {
    const char *a = jstr(path(meta, "authorName", NULL, NULL));
    if (a) { str_copy(dst, n, a); return; }
    const cJSON *arr = path(meta, "authors", NULL, NULL);
    dst[0] = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        const char *nm = jstr(path(it, "name", NULL, NULL));
        if (!nm) continue;
        size_t l = strlen(dst);
        if (l && l + 2 < n) { strcat(dst, ", "); l += 2; }
        str_copy(dst + l, n - l, nm);
    }
}

/* ---- parsers --------------------------------------------------------------- */

int abs_parse_login(const char *json, char *access, size_t an, char *refresh, size_t rn, char *user_id, size_t un) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    const cJSON *user = path(root, "user", NULL, NULL);
    const char *acc = jstr(path(user, "accessToken", NULL, NULL));
    if (!acc) acc = jstr(path(user, "token", NULL, NULL));
    int rc = -1;
    if (acc) {
        str_copy(access, an, acc);
        jcopy(refresh, rn, path(user, "refreshToken", NULL, NULL));
        jcopy(user_id, un, path(user, "id", NULL, NULL));
        rc = 0;
    }
    cJSON_Delete(root);
    return rc;
}

int abs_parse_libraries(const char *json, char *book_lib_id, size_t n) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    int rc = -1;
    const cJSON *it;
    cJSON_ArrayForEach(it, path(root, "libraries", NULL, NULL)) {
        const char *mt = jstr(path(it, "mediaType", NULL, NULL));
        if (mt && strcmp(mt, "book") == 0) {
            jcopy(book_lib_id, n, path(it, "id", NULL, NULL));
            rc = book_lib_id[0] ? 0 : -1;
            break;
        }
    }
    cJSON_Delete(root);
    return rc;
}

int abs_parse_items_page(const char *json, BookSummary *out, int max, int *count, int *total) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    *total = (int)jnum(path(root, "total", NULL, NULL), 0);
    const cJSON *it;
    cJSON_ArrayForEach(it, path(root, "results", NULL, NULL)) {
        if (*count >= max) break;
        BookSummary *b = &out[*count];
        memset(b, 0, sizeof *b);
        jcopy(b->id, sizeof b->id, path(it, "id", NULL, NULL));
        if (!b->id[0]) continue;
        const cJSON *media = path(it, "media", NULL, NULL);
        const cJSON *meta = path(media, "metadata", NULL, NULL);
        jcopy(b->title, sizeof b->title, path(meta, "title", NULL, NULL));
        author_from(b->author, sizeof b->author, meta);
        jcopy(b->series, sizeof b->series, path(meta, "seriesName", NULL, NULL));
        b->duration = jnum(path(media, "duration", NULL, NULL), 0);
        b->added_at = (uint64_t)jnum(path(it, "addedAt", NULL, NULL), 0);
        (*count)++;
    }
    cJSON_Delete(root);
    return 0;
}

int abs_parse_me_progress(const char *json, BookSummary *books, int n) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    const cJSON *arr = path(root, "mediaProgress", NULL, NULL);
    if (!arr) arr = path(root, "user", "mediaProgress", NULL);
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        const char *id = jstr(path(it, "libraryItemId", NULL, NULL));
        if (!id || cJSON_IsString(path(it, "episodeId", NULL, NULL))) continue;
        for (int i = 0; i < n; i++) {
            if (strcmp(books[i].id, id) != 0) continue;
            books[i].current_time = jnum(path(it, "currentTime", NULL, NULL), 0);
            books[i].progress = (float)jnum(path(it, "progress", NULL, NULL), 0);
            books[i].finished = cJSON_IsTrue(path(it, "isFinished", NULL, NULL));
            books[i].progress_updated = (uint64_t)jnum(path(it, "lastUpdate", NULL, NULL), 0);
            break;
        }
    }
    cJSON_Delete(root);
    return 0;
}

int abs_parse_in_progress(const char *json, char ids[][40], int max) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return 0;
    int n = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, path(root, "libraryItems", NULL, NULL)) {
        if (n >= max) break;
        jcopy(ids[n], 40, path(it, "id", NULL, NULL));
        if (ids[n][0]) n++;
    }
    cJSON_Delete(root);
    return n;
}

static void ext_from_name(char *ext, size_t n, const char *name) {
    const char *dot = name ? strrchr(name, '.') : NULL;
    str_copy(ext, n, dot ? dot : "");
}

static int parse_tracks(const cJSON *arr, BookDetail *d) {
    int n = cJSON_GetArraySize(arr);
    if (n <= 0) return 0;
    d->tracks = calloc(n, sizeof(Track));
    if (!d->tracks) return -1;
    const cJSON *it;
    double offset = 0;
    cJSON_ArrayForEach(it, arr) {
        Track *t = &d->tracks[d->ntracks];
        t->index = (int)jnum(path(it, "index", NULL, NULL), d->ntracks + 1);
        jid(t->ino, sizeof t->ino, path(it, "ino", NULL, NULL));
        jcopy(t->content_url, sizeof t->content_url, path(it, "contentUrl", NULL, NULL));
        jcopy(t->codec, sizeof t->codec, path(it, "codec", NULL, NULL));
        jcopy(t->title, sizeof t->title, path(it, "title", NULL, NULL));
        if (!t->title[0]) jcopy(t->title, sizeof t->title, path(it, "metadata", "filename", NULL));
        t->duration = jnum(path(it, "duration", NULL, NULL), 0);
        t->start_offset = jnum(path(it, "startOffset", NULL, NULL), offset);
        offset = t->start_offset + t->duration;
        const cJSON *meta = path(it, "metadata", NULL, NULL);
        const char *ext = jstr(path(meta, "ext", NULL, NULL));
        if (ext) str_copy(t->ext, sizeof t->ext, ext);
        else {
            const char *fn = jstr(path(meta, "filename", NULL, NULL));
            if (!fn) fn = jstr(path(it, "title", NULL, NULL));
            ext_from_name(t->ext, sizeof t->ext, fn);
        }
        t->size = (uint64_t)jnum(path(meta, "size", NULL, NULL), 0);
        d->ntracks++;
    }
    return 0;
}

static int parse_chapters(const cJSON *arr, BookDetail *d) {
    int n = cJSON_GetArraySize(arr);
    if (n <= 0) return 0;
    d->chapters = calloc(n, sizeof(Chapter));
    if (!d->chapters) return -1;
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        Chapter *c = &d->chapters[d->nchapters++];
        c->start = jnum(path(it, "start", NULL, NULL), 0);
        c->end = jnum(path(it, "end", NULL, NULL), c->start);
        jcopy(c->title, sizeof c->title, path(it, "title", NULL, NULL));
    }
    return 0;
}

static void set_description(BookDetail *d, const char *html) {
    if (!html) return;
    d->description = strdup(html);
    if (d->description) str_strip_html(d->description);
}

int abs_parse_item(const char *json, BookDetail *d) {
    memset(d, 0, sizeof *d);
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    jcopy(d->id, sizeof d->id, path(root, "id", NULL, NULL));
    const cJSON *media = path(root, "media", NULL, NULL);
    const cJSON *meta = path(media, "metadata", NULL, NULL);
    jcopy(d->title, sizeof d->title, path(meta, "title", NULL, NULL));
    author_from(d->author, sizeof d->author, meta);
    jcopy(d->narrator, sizeof d->narrator, path(meta, "narratorName", NULL, NULL));
    set_description(d, jstr(path(meta, "description", NULL, NULL)));
    d->duration = jnum(path(media, "duration", NULL, NULL), 0);
    const cJSON *tracks = path(media, "tracks", NULL, NULL);
    int rc = 0;
    if (cJSON_GetArraySize(tracks) > 0) {
        rc |= parse_tracks(tracks, d);
    } else {
        rc |= parse_tracks(path(media, "audioFiles", NULL, NULL), d);
        for (int i = 0; i < d->ntracks; i++)
            snprintf(d->tracks[i].content_url, sizeof d->tracks[i].content_url, "/api/items/%s/file/%s", d->id, d->tracks[i].ino);
    }
    rc |= parse_chapters(path(media, "chapters", NULL, NULL), d);
    abs_chapters_from_tracks(d);
    cJSON_Delete(root);
    if (rc) book_detail_free(d);
    return rc ? -1 : 0;
}

int abs_parse_play(const char *json, PlaySession *s) {
    memset(s, 0, sizeof *s);
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    BookDetail *d = &s->book;
    jcopy(s->id, sizeof s->id, path(root, "id", NULL, NULL));
    s->current_time = jnum(path(root, "currentTime", NULL, NULL), 0);
    jcopy(d->id, sizeof d->id, path(root, "libraryItemId", NULL, NULL));
    jcopy(d->title, sizeof d->title, path(root, "displayTitle", NULL, NULL));
    jcopy(d->author, sizeof d->author, path(root, "displayAuthor", NULL, NULL));
    if (!d->author[0]) author_from(d->author, sizeof d->author, path(root, "mediaMetadata", NULL, NULL));
    d->duration = jnum(path(root, "duration", NULL, NULL), 0);
    set_description(d, jstr(path(root, "libraryItem", "media", "metadata") ? path(path(root, "libraryItem", "media", "metadata"), "description", NULL, NULL) : NULL));
    jcopy(d->narrator, sizeof d->narrator, path(path(root, "libraryItem", "media", "metadata"), "narratorName", NULL, NULL));
    int rc = parse_tracks(path(root, "audioTracks", NULL, NULL), d);
    rc |= parse_chapters(path(root, "chapters", NULL, NULL), d);
    abs_chapters_from_tracks(d);
    cJSON_Delete(root);
    if (rc || !s->id[0] || d->ntracks == 0) {
        int empty = !rc && s->id[0] && d->ntracks == 0;
        book_detail_free(d);
        return empty ? -2 : -1;
    }
    return 0;
}

void abs_chapters_from_tracks(BookDetail *b) {
    if (b->nchapters > 0 || b->ntracks < 2) return;
    b->chapters = calloc(b->ntracks, sizeof(Chapter));
    if (!b->chapters) return;
    for (int i = 0; i < b->ntracks; i++) {
        Chapter *c = &b->chapters[i];
        c->start = b->tracks[i].start_offset;
        c->end = c->start + b->tracks[i].duration;
        /* title: the track's file name without its extension */
        str_copy(c->title, sizeof c->title, b->tracks[i].title);
        char *dot = strrchr(c->title, '.');
        if (dot && dot != c->title) *dot = 0;
        if (!c->title[0]) snprintf(c->title, sizeof c->title, "Part %d", i + 1);
    }
    b->nchapters = b->ntracks;
}

char *abs_detail_to_json(const BookDetail *b) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", b->id);
    cJSON *media = cJSON_AddObjectToObject(o, "media");
    cJSON *meta = cJSON_AddObjectToObject(media, "metadata");
    cJSON_AddStringToObject(meta, "title", b->title);
    cJSON_AddStringToObject(meta, "authorName", b->author);
    cJSON_AddStringToObject(meta, "narratorName", b->narrator);
    if (b->description) cJSON_AddStringToObject(meta, "description", b->description);
    cJSON_AddNumberToObject(media, "duration", b->duration);
    cJSON *ch = cJSON_AddArrayToObject(media, "chapters");
    for (int i = 0; i < b->nchapters; i++) {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddNumberToObject(c, "start", b->chapters[i].start);
        cJSON_AddNumberToObject(c, "end", b->chapters[i].end);
        cJSON_AddStringToObject(c, "title", b->chapters[i].title);
        cJSON_AddItemToArray(ch, c);
    }
    cJSON *tr = cJSON_AddArrayToObject(media, "tracks");
    for (int i = 0; i < b->ntracks; i++) {
        const Track *t = &b->tracks[i];
        cJSON *c = cJSON_CreateObject();
        cJSON_AddNumberToObject(c, "index", t->index);
        cJSON_AddStringToObject(c, "ino", t->ino);
        cJSON_AddNumberToObject(c, "startOffset", t->start_offset);
        cJSON_AddNumberToObject(c, "duration", t->duration);
        cJSON_AddStringToObject(c, "title", t->title);
        cJSON_AddStringToObject(c, "contentUrl", t->content_url);
        cJSON_AddStringToObject(c, "codec", t->codec);
        cJSON *m = cJSON_AddObjectToObject(c, "metadata");
        cJSON_AddStringToObject(m, "ext", t->ext);
        cJSON_AddNumberToObject(m, "size", (double)t->size);
        cJSON_AddItemToArray(tr, c);
    }
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

/* ---- bodies ---------------------------------------------------------------- */

static int emit(cJSON *o, char *out, size_t n) {
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!s) return -1;
    size_t l = strlen(s);
    int rc = -1;
    if (l < n) { memcpy(out, s, l + 1); rc = (int)l; }
    free(s);
    return rc;
}

int abs_build_login_body(const char *user, const char *pass, char *out, size_t n) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "username", user);
    cJSON_AddStringToObject(o, "password", pass);
    return emit(o, out, n);
}

int abs_build_play_body(const char *device_id, const char *model, const char *version, char *out, size_t n) {
    cJSON *o = cJSON_CreateObject();
    cJSON *dev = cJSON_AddObjectToObject(o, "deviceInfo");
    cJSON_AddStringToObject(dev, "deviceId", device_id);
    cJSON_AddStringToObject(dev, "clientName", "ABSvita");
    cJSON_AddStringToObject(dev, "clientVersion", version);
    cJSON_AddStringToObject(dev, "manufacturer", "Nintendo");
    cJSON_AddStringToObject(dev, "model", model);
    cJSON_AddBoolToObject(o, "forceDirectPlay", 1);
    cJSON_AddBoolToObject(o, "forceTranscode", 0);
    cJSON_AddStringToObject(o, "mediaPlayer", "absvita");
    const char *mimes[] = {"audio/mpeg", "audio/mp4", "audio/x-m4b", "audio/aac", "audio/x-m4a"};
    cJSON_AddItemToObject(o, "supportedMimeTypes", cJSON_CreateStringArray(mimes, 5));
    return emit(o, out, n);
}

int abs_build_sync_body(double current_time, double listened, double duration, char *out, size_t n) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "currentTime", current_time);
    cJSON_AddNumberToObject(o, "timeListened", listened);
    cJSON_AddNumberToObject(o, "duration", duration);
    return emit(o, out, n);
}

int abs_build_progress_body(double current_time, double duration, int finished, char *out, size_t n) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "currentTime", current_time);
    cJSON_AddNumberToObject(o, "duration", duration);
    cJSON_AddNumberToObject(o, "progress", duration > 0 ? current_time / duration : 0);
    cJSON_AddBoolToObject(o, "isFinished", finished);
    return emit(o, out, n);
}

int abs_build_local_session_body(const char *item_id, const char *device_id, double current_time, double duration,
                                 double listened, uint64_t updated_ms, char *out, size_t n) {
    char id[96];
    snprintf(id, sizeof id, "play_local_%s_%llu", item_id, (unsigned long long)updated_ms);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", id);
    cJSON_AddStringToObject(o, "libraryItemId", item_id);
    cJSON_AddStringToObject(o, "mediaType", "book");
    cJSON_AddStringToObject(o, "mediaPlayer", "absvita");
    cJSON_AddNumberToObject(o, "playMethod", 3); /* LOCAL */
    cJSON *dev = cJSON_AddObjectToObject(o, "deviceInfo");
    cJSON_AddStringToObject(dev, "deviceId", device_id);
    cJSON_AddStringToObject(dev, "clientName", "ABSvita");
    cJSON_AddNumberToObject(o, "currentTime", current_time);
    cJSON_AddNumberToObject(o, "startTime", current_time);
    cJSON_AddNumberToObject(o, "duration", duration);
    cJSON_AddNumberToObject(o, "timeListening", listened);
    cJSON_AddNumberToObject(o, "startedAt", (double)updated_ms);
    cJSON_AddNumberToObject(o, "updatedAt", (double)updated_ms);
    return emit(o, out, n);
}
