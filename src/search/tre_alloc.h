#ifndef TXX_TRE_ALLOC_H
#define TXX_TRE_ALLOC_H

#include <stddef.h>

/* Allocator used by the vendored TRE sources (force-included there, so TRE's
 * malloc/calloc/realloc/free resolve to these). Two guarantees:
 *   - every block is zeroed before release, so compiled patterns (which hold
 *     the search text) never linger in freed heap;
 *   - total live bytes are capped, so a pattern whose automaton explodes (for
 *     example nested {n,m} repeats) fails with "out of memory" instead of
 *     exhausting the machine.
 * Not thread-safe: searches run on the UI thread. */
#ifdef __cplusplus
extern "C" {
#endif

void  *txx_tre_malloc(size_t size);
void  *txx_tre_calloc(size_t count, size_t size);
void  *txx_tre_realloc(void *ptr, size_t size);
void   txx_tre_free(void *ptr);

/* Cap on live TRE bytes (default 64 MiB). Returns the previous limit. */
size_t txx_tre_set_limit(size_t bytes);
size_t txx_tre_live_bytes(void);

#ifdef __cplusplus
}
#endif
#endif
