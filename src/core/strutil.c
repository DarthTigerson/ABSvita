#include "strutil.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void str_copy(char *dst, size_t n, const char *src) {
    if (!n) return;
    if (!src) { dst[0] = 0; return; }
    size_t len = strlen(src);
    if (len >= n) {
        len = n - 1;
        /* back up over continuation bytes, then drop the partial lead byte */
        while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) len--;
    }
    memcpy(dst, src, len);
    dst[len] = 0;
}

void fmt_time(double seconds, char *out, size_t n) {
    if (seconds < 0) seconds = 0;
    long s = (long)seconds;
    long h = s / 3600, m = (s / 60) % 60, sec = s % 60;
    if (h) snprintf(out, n, "%ld:%02ld:%02ld", h, m, sec);
    else snprintf(out, n, "%ld:%02ld", m, sec);
}

void fmt_duration(double seconds, char *out, size_t n) {
    if (seconds < 0) seconds = 0;
    long s = (long)(seconds + 0.5);
    long h = s / 3600, m = (s / 60) % 60;
    if (h) snprintf(out, n, "%ldh %ldm", h, m);
    else if (m) snprintf(out, n, "%ldm", m);
    else snprintf(out, n, "%lds", s);
}

int str_icontains(const char *hay, const char *needle) {
    if (!needle || !*needle) return 1;
    if (!hay) return 0;
    size_t nl = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < nl && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == nl) return 1;
    }
    return 0;
}

static const struct { const char *name; const char *rep; } kEntities[] = {
    {"&nbsp;", " "}, {"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&apos;", "'"},
    {"&lt;", "<"}, {"&gt;", ">"}, {"&rsquo;", "\xE2\x80\x99"}, {"&lsquo;", "\xE2\x80\x98"},
    {"&ldquo;", "\xE2\x80\x9C"}, {"&rdquo;", "\xE2\x80\x9D"}, {"&mdash;", "\xE2\x80\x94"}, {"&ndash;", "\xE2\x80\x93"},
    {"&hellip;", "\xE2\x80\xA6"},
};

void str_strip_html(char *s) {
    char *w = s;
    const char *r = s;
    while (*r) {
        if (*r == '<') {
            const char *e = strchr(r, '>');
            if (!e) break;
            /* block-level breaks become newlines */
            int is_break = (r[1] == 'b' && r[2] == 'r') || (r[1] == '/' && (r[2] == 'p' || r[2] == 'd' || r[2] == 'l'));
            if (is_break && w > s && w[-1] != '\n') *w++ = '\n';
            r = e + 1;
            continue;
        }
        if (*r == '&') {
            int done = 0;
            for (size_t i = 0; i < sizeof kEntities / sizeof kEntities[0]; i++) {
                size_t l = strlen(kEntities[i].name);
                if (strncmp(r, kEntities[i].name, l) == 0) {
                    size_t rl = strlen(kEntities[i].rep);
                    if (rl <= l) { memcpy(w, kEntities[i].rep, rl); w += rl; r += l; done = 1; }
                    break;
                }
            }
            if (done) continue;
        }
        *w++ = *r++;
    }
    /* trim trailing whitespace/newlines */
    while (w > s && (w[-1] == '\n' || w[-1] == ' ')) w--;
    *w = 0;
}
