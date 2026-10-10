#ifndef ABSVITA_STRUTIL_H
#define ABSVITA_STRUTIL_H
#include <stddef.h>

/* Copies src into dst (size n), truncating on a UTF-8 character boundary.
 * NULL src yields "". Always NUL-terminates when n > 0. */
void str_copy(char *dst, size_t n, const char *src);
/* "1:02:03" for >= 1 h, else "2:03". Negative values clamp to 0. */
void fmt_time(double seconds, char *out, size_t n);
/* "12h 5m" / "45m" / "30s" style duration for book cards. */
void fmt_duration(double seconds, char *out, size_t n);
/* Case-insensitive (ASCII) substring search. */
int str_icontains(const char *hay, const char *needle);
/* Removes HTML tags and decodes a few entities in place (ABS descriptions are HTML). */
void str_strip_html(char *s);

#endif
