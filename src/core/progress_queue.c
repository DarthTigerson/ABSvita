#include "progress_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "strutil.h"

void pq_put(ProgressQueue *q, const QEntry *e) {
    for (int i = 0; i < q->n; i++) {
        if (strcmp(q->e[i].item_id, e->item_id) == 0) {
            if (e->updated_ms >= q->e[i].updated_ms) q->e[i] = *e;
            return;
        }
    }
    if (q->n == PQ_MAX) {
        int oldest = 0;
        for (int i = 1; i < q->n; i++)
            if (q->e[i].updated_ms < q->e[oldest].updated_ms) oldest = i;
        q->e[oldest] = *e;
        return;
    }
    q->e[q->n++] = *e;
}

const QEntry *pq_get(const ProgressQueue *q, const char *item_id) {
    for (int i = 0; i < q->n; i++)
        if (strcmp(q->e[i].item_id, item_id) == 0) return &q->e[i];
    return NULL;
}

void pq_mark_synced(ProgressQueue *q, const char *item_id, uint64_t updated_ms) {
    for (int i = 0; i < q->n; i++)
        if (strcmp(q->e[i].item_id, item_id) == 0 && q->e[i].updated_ms <= updated_ms) q->e[i].pending = false;
}

static char *slurp(const char *path, long max) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = NULL;
    if (n > 0 && n < max && (s = malloc(n + 1))) {
        if (fread(s, 1, n, f) != (size_t)n) { free(s); s = NULL; }
        else s[n] = 0;
    }
    fclose(f);
    return s;
}

int pq_load(ProgressQueue *q, const char *path) {
    memset(q, 0, sizeof *q);
    char *s = slurp(path, 1 << 20);
    if (!s) return -1;
    cJSON *root = cJSON_Parse(s);
    free(s);
    if (!cJSON_IsArray(root)) { cJSON_Delete(root); return -1; }
    const cJSON *it;
    cJSON_ArrayForEach(it, root) {
        QEntry e = {0};
        const cJSON *id = cJSON_GetObjectItem(it, "id");
        if (!cJSON_IsString(id)) continue;
        str_copy(e.item_id, sizeof e.item_id, id->valuestring);
        e.current_time = cJSON_GetNumberValue(cJSON_GetObjectItem(it, "t"));
        e.duration = cJSON_GetNumberValue(cJSON_GetObjectItem(it, "d"));
        e.updated_ms = (uint64_t)cJSON_GetNumberValue(cJSON_GetObjectItem(it, "u"));
        e.finished = cJSON_IsTrue(cJSON_GetObjectItem(it, "f"));
        e.pending = cJSON_IsTrue(cJSON_GetObjectItem(it, "p"));
        if (e.current_time != e.current_time) e.current_time = 0; /* NaN from missing */
        if (e.duration != e.duration) e.duration = 0;
        pq_put(q, &e);
    }
    cJSON_Delete(root);
    return 0;
}

int pq_save(const ProgressQueue *q, const char *path) {
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < q->n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", q->e[i].item_id);
        cJSON_AddNumberToObject(o, "t", q->e[i].current_time);
        cJSON_AddNumberToObject(o, "d", q->e[i].duration);
        cJSON_AddNumberToObject(o, "u", (double)q->e[i].updated_ms);
        cJSON_AddBoolToObject(o, "f", q->e[i].finished);
        cJSON_AddBoolToObject(o, "p", q->e[i].pending);
        cJSON_AddItemToArray(root, o);
    }
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!s) return -1;
    /* write to a temp file then rename, so a power cut never leaves half a file */
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    int rc = -1;
    if (f) {
        size_t l = strlen(s);
        rc = fwrite(s, 1, l, f) == l ? 0 : -1;
        fclose(f);
        if (rc == 0) { remove(path); rc = rename(tmp, path); }
    }
    free(s);
    return rc;
}
