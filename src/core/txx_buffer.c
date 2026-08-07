#include "core/txx_buffer.h"

#include <stdlib.h>

#include "platform/secure_mem.h"

void txx_buffer_init(txx_buffer *buffer)
{
    if (buffer != NULL) {
        buffer->data = NULL;
        buffer->len = 0;
    }
}

txx_status txx_buffer_alloc(txx_buffer *buffer, size_t len)
{
    if (buffer == NULL) {
        return TXX_ERR_ARG;
    }

    buffer->data = NULL;
    buffer->len = 0;
    if (len == 0) {
        return TXX_OK;
    }

    buffer->data = (uint8_t *)malloc(len);
    if (buffer->data == NULL) {
        return TXX_ERR_ALLOC;
    }
    buffer->len = len;
    return TXX_OK;
}

void txx_buffer_free(txx_buffer *buffer)
{
    if (buffer != NULL) {
        free(buffer->data);
        buffer->data = NULL;
        buffer->len = 0;
    }
}

void txx_buffer_secure_free(txx_buffer *buffer)
{
    if (buffer != NULL) {
        txx_secure_zero(buffer->data, buffer->len);
        txx_buffer_free(buffer);
    }
}