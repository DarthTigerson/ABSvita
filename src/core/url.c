#include "url.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

int url_join(const char *base, const char *path, char *out, size_t n) {
    size_t bl = strlen(base);
    while (bl && base[bl - 1] == '/') bl--;
    while (*path == '/') path++;
    int k = snprintf(out, n, "%.*s/%s", (int)bl, base, path);
    return (k < 0 || (size_t)k >= n) ? -1 : k;
}

int url_valid(const char *s) {
    if (strncmp(s, "http://", 7) != 0) return 0;
    const char *h = s + 7;
    size_t hl = strcspn(h, ":/");
    if (hl == 0) return 0;
    for (size_t i = 0; i < hl; i++)
        if (!isalnum((unsigned char)h[i]) && h[i] != '.' && h[i] != '-') return 0;
    if (h[hl] == ':') {
        const char *p = h + hl + 1;
        int digits = 0;
        long port = 0;
        while (isdigit((unsigned char)*p)) { port = port * 10 + (*p - '0'); p++; digits++; }
        if (!digits || port < 1 || port > 65535) return 0;
        if (*p && *p != '/') return 0;
    }
    for (const char *p = s; *p; p++)
        if ((unsigned char)*p <= ' ') return 0;
    return 1;
}

void url_normalize(char *s, size_t n) {
    char tmp[512];
    const char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    size_t l = strlen(p);
    while (l && (isspace((unsigned char)p[l - 1]) || p[l - 1] == '/')) l--;
    if (strncmp(p, "http://", 7) != 0 && strncmp(p, "https://", 8) != 0 && l) snprintf(tmp, sizeof tmp, "http://%.*s", (int)l, p);
    else snprintf(tmp, sizeof tmp, "%.*s", (int)l, p);
    snprintf(s, n, "%s", tmp);
}

int url_encode(const char *in, char *out, size_t n) {
    static const char hex[] = "0123456789ABCDEF";
    size_t w = 0;
    for (; *in; in++) {
        unsigned char c = (unsigned char)*in;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            if (w + 1 >= n) return -1;
            out[w++] = (char)c;
        } else {
            if (w + 3 >= n) return -1;
            out[w++] = '%';
            out[w++] = hex[c >> 4];
            out[w++] = hex[c & 15];
        }
    }
    out[w] = 0;
    return (int)w;
}
