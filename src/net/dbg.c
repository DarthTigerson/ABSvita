#include "dbg.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "osal.h"

static Mutex g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_path[256];

void dbg_init(const char *path) {
    mutex_lock(&g_lock);
    snprintf(g_path, sizeof g_path, "%s", path ? path : "");
    mutex_unlock(&g_lock);
}

void dbg(const char *fmt, ...) {
    mutex_lock(&g_lock);
    FILE *f = g_path[0] ? fopen(g_path, "a") : stderr;
    if (f && f != stderr && ftell(f) > 256 * 1024) {
        char old[264];
        fclose(f);
        snprintf(old, sizeof old, "%s.old", g_path);
        remove(old);
        rename(g_path, old);
        f = fopen(g_path, "a");
    }
    if (f) {
        fprintf(f, "[%llu] ", (unsigned long long)(now_ms() % 100000000ULL));
        va_list ap;
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fputc('\n', f);
        if (f != stderr) fclose(f);
    }
    mutex_unlock(&g_lock);
}
