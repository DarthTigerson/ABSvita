/* Append-only debug log (ux0:data/ABSvita/debug.log on the Vita; stderr on the host
 * unless a path is set). Rotated at 256 KB. Thread-safe. */
#ifndef ABSVITA_DBG_H
#define ABSVITA_DBG_H
void dbg_init(const char *path);
void dbg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#endif
