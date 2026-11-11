#include "search/tre_alloc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "platform/secure_mem.h"

/* The header keeps the payload 16-byte aligned. */
typedef struct { size_t size; size_t pad; } block_header;

static size_t g_limit = 64u * 1024u * 1024u;
static size_t g_live = 0;

size_t txx_tre_set_limit(size_t bytes)
{
    size_t previous = g_limit;
    g_limit = bytes;
    return previous;
}

size_t txx_tre_live_bytes(void) { return g_live; }

void *txx_tre_malloc(size_t size)
{
    if (size > SIZE_MAX - sizeof(block_header)) return NULL;
    const size_t total = size + sizeof(block_header);
    if (total > g_limit || g_live > g_limit - total) return NULL;
    block_header *header = (block_header *)malloc(total);
    if (header == NULL) return NULL;
    header->size = size;
    header->pad = 0;
    g_live += total;
    return header + 1;
}

void *txx_tre_calloc(size_t count, size_t size)
{
    if (size != 0 && count > SIZE_MAX / size) return NULL;
    void *p = txx_tre_malloc(count * size);
    if (p != NULL) memset(p, 0, count * size);
    return p;
}

void txx_tre_free(void *ptr)
{
    if (ptr == NULL) return;
    block_header *header = (block_header *)ptr - 1;
    const size_t total = header->size + sizeof(block_header);
    txx_secure_zero(header, total);
    g_live -= total;
    free(header);
}

void *txx_tre_realloc(void *ptr, size_t size)
{
    if (ptr == NULL) return txx_tre_malloc(size);
    if (size == 0) { txx_tre_free(ptr); return NULL; }
    block_header *header = (block_header *)ptr - 1;
    void *next = txx_tre_malloc(size);
    if (next == NULL) return NULL;
    memcpy(next, ptr, header->size < size ? header->size : size);
    txx_tre_free(ptr);
    return next;
}
