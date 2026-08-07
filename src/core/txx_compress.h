#ifndef TXX_COMPRESS_H
#define TXX_COMPRESS_H

#include <stddef.h>
#include <stdint.h>

#include "core/txx_buffer.h"

#define TXX_COMPRESS_DEFAULT_THRESHOLD 512U
#define TXX_COMPRESS_DEFAULT_MIN_SAVINGS 8U

txx_status txx_compress_encode(const uint8_t *input,
                               size_t input_len,
                               uint32_t threshold,
                               uint32_t min_savings,
                               txx_buffer *payload,
                               int *is_compressed);
txx_status txx_compress_decode(const uint8_t *payload,
                               size_t payload_len,
                               size_t plaintext_len,
                               int is_compressed,
                               txx_buffer *plaintext);

#endif