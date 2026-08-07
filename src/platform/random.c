#include "platform/random.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/random.h>
#endif
#endif

int txx_random_bytes(uint8_t *out, size_t len)
{
    size_t offset = 0;

    if (out == NULL && len != 0) {
        return -1;
    }

#if defined(_WIN32)
    if (len > ULONG_MAX) {
        return -1;
    }
    return BCryptGenRandom(NULL, out, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -1;
#else
#if defined(__linux__)
    while (offset < len) {
        ssize_t got = getrandom(out + offset, len - offset, 0);
        if (got > 0) {
            offset += (size_t)got;
        } else if (got < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    if (offset == len) {
        return 0;
    }
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > 256U) {
            chunk = 256U;
        }
        if (getentropy(out + offset, chunk) == 0) {
            offset += chunk;
        } else if (errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    if (offset == len) {
        return 0;
    }
#endif

    {
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) {
            return -1;
        }
        while (offset < len) {
            ssize_t got = read(fd, out + offset, len - offset);
            if (got > 0) {
                offset += (size_t)got;
            } else if (got < 0 && errno == EINTR) {
                continue;
            } else {
                close(fd);
                return -1;
            }
        }
        close(fd);
    }
    return 0;
#endif
}