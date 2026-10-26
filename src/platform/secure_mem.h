#ifndef TXX_SECURE_MEM_H
#define TXX_SECURE_MEM_H

#include <stddef.h>

#include "core/txx_status.h"

/* Overwrite memory so the compiler cannot elide the store. */
void txx_secure_zero(void *ptr, size_t len);

/* Best-effort: keep a range out of swap and out of core dumps.
 *
 * Linux: mlock() plus madvise(MADV_DONTDUMP); Windows: VirtualLock().
 * The range is rounded outwards to whole pages. Locking can fail when the
 * process exceeds RLIMIT_MEMLOCK (Unix) or its working-set quota (Windows),
 * so callers must treat TXX_OK as "locked" and anything else as "not locked"
 * rather than as a fatal error.
 */
txx_status txx_secure_lock(void *ptr, size_t len);

/* Undo txx_secure_lock(). Safe to call on a range that was never locked. */
void txx_secure_unlock(void *ptr, size_t len);

#endif
