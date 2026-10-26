#include "platform/secure_mem.h"

#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

void txx_secure_zero(void *ptr, size_t len)
{
    if (ptr == 0) {
        return;
    }

#if defined(_WIN32)
    SecureZeroMemory(ptr, len);
#else
    {
        static void *(*volatile secure_memset)(void *, int, size_t) = memset;
        secure_memset(ptr, 0, len);
    }
#endif
}

#if defined(_WIN32)

txx_status txx_secure_lock(void *ptr, size_t len)
{
    if (ptr == 0 || len == 0) {
        return TXX_OK;
    }
    return VirtualLock(ptr, len) ? TXX_OK : TXX_ERR_IO;
}

void txx_secure_unlock(void *ptr, size_t len)
{
    if (ptr != 0 && len != 0) {
        (void)VirtualUnlock(ptr, len);
    }
}

#else

/* Round [ptr, ptr + len) outwards to whole pages. */
static int page_range(void *ptr, size_t len, void **start, size_t *span)
{
    const long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) {
        return 0;
    }
    const uintptr_t mask = (uintptr_t)page - 1;
    const uintptr_t first = (uintptr_t)ptr & ~mask;
    const uintptr_t last = ((uintptr_t)ptr + len + mask) & ~mask;
    *start = (void *)first;
    *span = (size_t)(last - first);
    return *span != 0;
}

txx_status txx_secure_lock(void *ptr, size_t len)
{
    if (ptr == 0 || len == 0) {
        return TXX_OK;
    }

    void *start = 0;
    size_t span = 0;
    if (!page_range(ptr, len, &start, &span)) {
        return TXX_ERR_ARG;
    }
    if (mlock(start, span) != 0) {
        return TXX_ERR_IO;
    }

#if defined(__linux__) && defined(MADV_DONTDUMP)
    /* Keep the pages out of core dumps even if the core limit is raised. */
    (void)madvise(start, span, MADV_DONTDUMP);
#endif
    return TXX_OK;
}

void txx_secure_unlock(void *ptr, size_t len)
{
    if (ptr == 0 || len == 0) {
        return;
    }
    void *start = 0;
    size_t span = 0;
    if (page_range(ptr, len, &start, &span)) {
        (void)munlock(start, span);
    }
}

#endif
