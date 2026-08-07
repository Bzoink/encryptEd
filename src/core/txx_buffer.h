#ifndef TXX_BUFFER_H
#define TXX_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#include "core/txx_status.h"

typedef struct txx_buffer {
    uint8_t *data;
    size_t len;
} txx_buffer;

void txx_buffer_init(txx_buffer *buffer);
txx_status txx_buffer_alloc(txx_buffer *buffer, size_t len);
void txx_buffer_free(txx_buffer *buffer);
void txx_buffer_secure_free(txx_buffer *buffer);

#endif