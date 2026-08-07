#include "platform/secure_mem.h"

#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
