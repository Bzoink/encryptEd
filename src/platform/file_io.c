#if !defined(_WIN32)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#endif

#include "platform/file_io.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#endif

#include "platform/random.h"
#include "platform/secure_mem.h"

static txx_status file_seek_start(FILE *file)
{
#if defined(_WIN32)
    return _fseeki64(file, 0, SEEK_SET) == 0 ? TXX_OK : TXX_ERR_IO;
#else
    return fseeko(file, 0, SEEK_SET) == 0 ? TXX_OK : TXX_ERR_IO;
#endif
}

static txx_status file_length(FILE *file, uint64_t *length)
{
#if defined(_WIN32)
    __int64 pos;

    if (_fseeki64(file, 0, SEEK_END) != 0) {
        return TXX_ERR_IO;
    }
    pos = _ftelli64(file);
    if (pos < 0) {
        return TXX_ERR_IO;
    }
    *length = (uint64_t)pos;
    return TXX_OK;
#else
    off_t pos;

    if (fseeko(file, 0, SEEK_END) != 0) {
        return TXX_ERR_IO;
    }
    pos = ftello(file);
    if (pos < 0) {
        return TXX_ERR_IO;
    }
    *length = (uint64_t)pos;
    return TXX_OK;
#endif
}

static int make_suffix(char suffix[17])
{
    uint8_t random[8];
    static const char hex[] = "0123456789abcdef";
    size_t i;

    if (txx_random_bytes(random, sizeof(random)) != 0) {
        return -1;
    }
    for (i = 0; i < sizeof(random); ++i) {
        suffix[i * 2U + 0U] = hex[(random[i] >> 4) & 0x0F];
        suffix[i * 2U + 1U] = hex[random[i] & 0x0F];
    }
    suffix[16] = '\0';
    txx_secure_zero(random, sizeof(random));
    return 0;
}

#if defined(_WIN32)
static wchar_t *wide_from_utf8(const char *path)
{
    int count;
    wchar_t *wide;

    if (path == NULL) {
        return NULL;
    }
    count = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (count <= 0) {
        return NULL;
    }
    wide = (wchar_t *)malloc((size_t)count * sizeof(wchar_t));
    if (wide == NULL) {
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, count) != count) {
        free(wide);
        return NULL;
    }
    return wide;
}

static wchar_t *make_temp_path_wide(const wchar_t *path)
{
    char suffix[17];
    size_t path_len;
    size_t suffix_len;
    wchar_t *tmp;
    size_t i;

    if (make_suffix(suffix) != 0) {
        return NULL;
    }
    path_len = wcslen(path);
    suffix_len = strlen(suffix);
    tmp = (wchar_t *)malloc((path_len + 5U + suffix_len + 1U) * sizeof(wchar_t));
    if (tmp == NULL) {
        txx_secure_zero(suffix, sizeof(suffix));
        return NULL;
    }
    memcpy(tmp, path, path_len * sizeof(wchar_t));
    memcpy(tmp + path_len, L".tmp.", 5U * sizeof(wchar_t));
    for (i = 0; i < suffix_len; ++i) {
        tmp[path_len + 5U + i] = (wchar_t)suffix[i];
    }
    tmp[path_len + 5U + suffix_len] = L'\0';
    txx_secure_zero(suffix, sizeof(suffix));
    return tmp;
}
#else
static int split_path(const char *path, char **dir, char **base)
{
    const char *slash;
    size_t dir_len;
    size_t base_len;

    if (path == NULL || dir == NULL || base == NULL) {
        return -1;
    }
    slash = strrchr(path, '/');
    if (slash == NULL) {
        dir_len = 1U;
        base_len = strlen(path);
        if (base_len == 0U) return -1;
        *dir = (char *)malloc(dir_len + 1U);
        *base = (char *)malloc(base_len + 1U);
        if (*dir == NULL || *base == NULL) {
            free(*dir);
            free(*base);
            *dir = NULL;
            *base = NULL;
            return -1;
        }
        (*dir)[0] = '.';
        (*dir)[1] = '\0';
        memcpy(*base, path, base_len + 1U);
        return 0;
    }

    dir_len = (slash == path) ? 1U : (size_t)(slash - path);
    base_len = strlen(slash + 1);
    if (base_len == 0U) return -1;
    *dir = (char *)malloc(dir_len + 1U);
    *base = (char *)malloc(base_len + 1U);
    if (*dir == NULL || *base == NULL) {
        free(*dir);
        free(*base);
        *dir = NULL;
        *base = NULL;
        return -1;
    }
    memcpy(*dir, path, dir_len);
    (*dir)[dir_len] = '\0';
    memcpy(*base, slash + 1, base_len + 1U);
    return 0;
}

static char *make_temp_path(const char *dir, const char *base)
{
    char suffix[17];
    size_t dir_len;
    size_t base_len;
    size_t len;
    char *tmp;

    if (make_suffix(suffix) != 0) {
        return NULL;
    }
    dir_len = strlen(dir);
    base_len = strlen(base);
    len = dir_len + 1U + 1U + base_len + 5U + 16U + 1U;
    tmp = (char *)malloc(len);
    if (tmp == NULL) {
        txx_secure_zero(suffix, sizeof(suffix));
        return NULL;
    }
    snprintf(tmp, len, "%s/.%s.tmp.%s", dir, base, suffix);
    txx_secure_zero(suffix, sizeof(suffix));
    return tmp;
}

static txx_status fsync_directory(const char *dir)
{
    int flags = O_RDONLY | O_CLOEXEC;
    int fd;

#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
    fd = open(dir, flags);
    if (fd < 0) {
        return TXX_OK;
    }
    if (fsync(fd) != 0 && errno != EINVAL) {
        close(fd);
        return TXX_ERR_IO;
    }
    close(fd);
    return TXX_OK;
}
#endif

txx_status txx_read_file_limited(const char *path, uint64_t max_bytes, txx_buffer *buffer)
{
    FILE *file;
    uint64_t length;
    size_t read_len;
    txx_status status;

    if (path == NULL || buffer == NULL) {
        return TXX_ERR_ARG;
    }
    txx_buffer_init(buffer);

#if defined(_WIN32)
    {
        wchar_t *wide = wide_from_utf8(path);
        if (wide == NULL) {
            return TXX_ERR_IO;
        }
        file = _wfopen(wide, L"rb");
        free(wide);
    }
#else
    file = fopen(path, "rb");
#endif
    if (file == NULL) {
        return TXX_ERR_IO;
    }
    status = file_length(file, &length);
    if (status != TXX_OK) {
        fclose(file);
        return status;
    }
    if (length > max_bytes) {
        fclose(file);
        return TXX_ERR_UNSUPPORTED;
    }
    if (length > (uint64_t)SIZE_MAX) {
        fclose(file);
        return TXX_ERR_OVERFLOW;
    }
    status = file_seek_start(file);
    if (status != TXX_OK) {
        fclose(file);
        return status;
    }

    status = txx_buffer_alloc(buffer, (size_t)length);
    if (status != TXX_OK) {
        fclose(file);
        return status;
    }
    read_len = buffer->len == 0U ? 0U : fread(buffer->data, 1, buffer->len, file);
    fclose(file);
    if (read_len != buffer->len) {
        txx_buffer_free(buffer);
        return TXX_ERR_IO;
    }
    return TXX_OK;
}

txx_status txx_write_file_atomic(const char *path, const uint8_t *data, size_t len)
{
#if defined(_WIN32)
    wchar_t *wide_path = NULL;
    wchar_t *tmp_path = NULL;
    txx_status status = TXX_ERR_IO;
    int attempt;

    if (path == NULL || (data == NULL && len != 0U)) {
        return TXX_ERR_ARG;
    }
    wide_path = wide_from_utf8(path);
    if (wide_path == NULL) {
        return TXX_ERR_IO;
    }

    for (attempt = 0; attempt < 8; ++attempt) {
        HANDLE file;
        const uint8_t *p;
        size_t remaining;

        tmp_path = make_temp_path_wide(wide_path);
        if (tmp_path == NULL) {
            goto cleanup;
        }
        file = CreateFileW(tmp_path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            free(tmp_path);
            tmp_path = NULL;
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
                continue;
            }
            goto cleanup;
        }

        p = data;
        remaining = len;
        while (remaining != 0U) {
            DWORD chunk = remaining > 0x40000000UL ? 0x40000000UL : (DWORD)remaining;
            DWORD written = 0;
            if (!WriteFile(file, p, chunk, &written, NULL) || written != chunk) {
                CloseHandle(file);
                DeleteFileW(tmp_path);
                goto cleanup;
            }
            p += chunk;
            remaining -= written;
        }
        if (!FlushFileBuffers(file)) {
            CloseHandle(file);
            DeleteFileW(tmp_path);
            goto cleanup;
        }
        if (!CloseHandle(file)) {
            DeleteFileW(tmp_path);
            goto cleanup;
        }
        if (!MoveFileExW(tmp_path, wide_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(tmp_path);
            goto cleanup;
        }
        status = TXX_OK;
        goto cleanup;
    }

cleanup:
    free(tmp_path);
    free(wide_path);
    return status;
#else
    char *dir = NULL;
    char *base = NULL;
    char *tmp_path = NULL;
    int fd = -1;
    int tmp_created = 0;
    int attempt;
    txx_status status = TXX_ERR_IO;

    if (path == NULL || (data == NULL && len != 0U)) {
        return TXX_ERR_ARG;
    }
    if (split_path(path, &dir, &base) != 0) {
        goto cleanup;
    }

    for (attempt = 0; attempt < 8; ++attempt) {
        tmp_path = make_temp_path(dir, base);
        if (tmp_path == NULL) {
            goto cleanup;
        }
        fd = open(tmp_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
        if (fd >= 0) {
            tmp_created = 1;
            break;
        }
        if (errno != EEXIST) {
            goto cleanup;
        }
        free(tmp_path);
        tmp_path = NULL;
    }
    if (fd < 0) {
        goto cleanup;
    }

    {
        const uint8_t *p = data;
        size_t remaining = len;
        while (remaining != 0U) {
            ssize_t written = write(fd, p, remaining);
            if (written < 0) {
                if (errno == EINTR) continue;
                goto cleanup;
            }
            if (written == 0) {
                goto cleanup;
            }
            p += (size_t)written;
            remaining -= (size_t)written;
        }
    }
    if (fsync(fd) != 0 && errno != EINVAL) {
        goto cleanup;
    }
    if (close(fd) != 0) {
        fd = -1;
        goto cleanup;
    }
    fd = -1;
    if (rename(tmp_path, path) != 0) {
        goto cleanup;
    }
    tmp_created = 0;
    status = fsync_directory(dir);

cleanup:
    if (fd >= 0) {
        close(fd);
    }
    if (tmp_created && tmp_path != NULL) {
        unlink(tmp_path);
    }
    free(tmp_path);
    free(dir);
    free(base);
    return status;
#endif
}
