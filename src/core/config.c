#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "strutil.h"

void cfg_defaults(Config *c) {
    memset(c, 0, sizeof *c);
    str_copy(c->server, sizeof c->server, CFG_DEFAULT_SERVER);
    c->skip_back = 15;
    c->skip_fwd = 30;
    c->speed = 1.0f;
    c->sync_interval = 15;
    c->screen_off = true;
}

static const int kSkips[] = {10, 15, 30, 60};
static const float kSpeeds[] = {0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};

static int clamp_skip(int s) {
    for (size_t i = 0; i < 4; i++) if (kSkips[i] == s) return s;
    return 15;
}

float cfg_next_speed(float s) {
    for (size_t i = 0; i < 6; i++)
        if (s < kSpeeds[i] - 0.01f) return kSpeeds[i];
    return kSpeeds[0];
}

int cfg_next_skip(int s) {
    for (size_t i = 0; i < 4; i++)
        if (kSkips[i] > s) return kSkips[i];
    return kSkips[0];
}

static void get_str(const cJSON *o, const char *k, char *dst, size_t n) {
    const cJSON *v = cJSON_GetObjectItem(o, k);
    if (cJSON_IsString(v)) str_copy(dst, n, v->valuestring);
}

int cfg_load(Config *c, const char *path) {
    cfg_defaults(c);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    cJSON *o = cJSON_Parse(buf);
    if (!cJSON_IsObject(o)) { cJSON_Delete(o); return -1; }
    get_str(o, "server", c->server, sizeof c->server);
    get_str(o, "username", c->username, sizeof c->username);
    get_str(o, "refresh", c->refresh, sizeof c->refresh);
    get_str(o, "userId", c->user_id, sizeof c->user_id);
    get_str(o, "deviceId", c->device_id, sizeof c->device_id);
    get_str(o, "libraryId", c->library_id, sizeof c->library_id);
    const cJSON *v;
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "skipBack"))) c->skip_back = clamp_skip(v->valueint);
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "skipFwd"))) c->skip_fwd = clamp_skip(v->valueint);
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "speed"))) {
        float s = (float)v->valuedouble;
        c->speed = s < 0.75f ? 0.75f : s > 2.0f ? 2.0f : s;
    }
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "syncInterval"))) {
        int s = v->valueint;
        c->sync_interval = s < 10 ? 10 : s > 60 ? 60 : s;
    }
    if (cJSON_IsBool(v = cJSON_GetObjectItem(o, "screenOff"))) c->screen_off = cJSON_IsTrue(v);
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "clockOffset"))) { c->clock_offset_ms = (long long)v->valuedouble; c->clock_known = true; }
    cJSON_Delete(o);
    return 0;
}

int cfg_save(const Config *c, const char *path) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "server", c->server);
    cJSON_AddStringToObject(o, "username", c->username);
    cJSON_AddStringToObject(o, "refresh", c->refresh);
    cJSON_AddStringToObject(o, "userId", c->user_id);
    cJSON_AddStringToObject(o, "deviceId", c->device_id);
    cJSON_AddStringToObject(o, "libraryId", c->library_id);
    cJSON_AddNumberToObject(o, "skipBack", c->skip_back);
    cJSON_AddNumberToObject(o, "skipFwd", c->skip_fwd);
    cJSON_AddNumberToObject(o, "speed", c->speed);
    cJSON_AddNumberToObject(o, "syncInterval", c->sync_interval);
    cJSON_AddBoolToObject(o, "screenOff", c->screen_off);
    if (c->clock_known) cJSON_AddNumberToObject(o, "clockOffset", (double)c->clock_offset_ms);
    char *s = cJSON_Print(o);
    cJSON_Delete(o);
    if (!s) return -1;
    /* temp file then rename: never a half-written or stale-tailed config (it holds the login) */
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    remove(tmp);
    FILE *f = fopen(tmp, "wb");
    int rc = -1;
    if (f) {
        rc = fputs(s, f) >= 0 ? 0 : -1;
        if (fclose(f) != 0) rc = -1;
        if (rc == 0) { remove(path); rc = rename(tmp, path); }
        else remove(tmp);
    }
    free(s);
    return rc;
}
