/* Random-access reader over HTTP Range requests with an LRU cache of 64 KB
 * blocks. Used for MP4 sample tables, which are read on demand instead of
 * downloading the whole moov first. Keeps one connection alive. Single user. */
#ifndef ABSVITA_BLOCKCACHE_H
#define ABSVITA_BLOCKCACHE_H
#include <stdint.h>

#include "http.h"
#include "reader.h"

#define BC_BLOCK (64u * 1024)

typedef struct BlockCache BlockCache;

BlockCache *bc_open(const char *url, uint64_t size, AuthFn auth, int nblocks);
Reader bc_reader(BlockCache *c);
/* Makes the current and future fetches fail with -3 until cleared. */
void bc_interrupt(BlockCache *c, int on);
void bc_close(BlockCache *c);
/* For tests and logs: range requests made and bytes fetched. */
void bc_stats(const BlockCache *c, int *requests, uint64_t *bytes);

#endif
