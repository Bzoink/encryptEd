#include "core/txx_compress.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "microz.h"

static txx_status copy_payload(const uint8_t *input, size_t input_len, txx_buffer *payload)
{
    txx_status status = txx_buffer_alloc(payload, input_len);
    if (status != TXX_OK) {
        return status;
    }
    if (input_len != 0) {
        memcpy(payload->data, input, input_len);
    }
    return TXX_OK;
}

txx_status txx_compress_encode(const uint8_t *input,
                               size_t input_len,
                               uint32_t threshold,
                               uint32_t min_savings,
                               txx_buffer *payload,
                               int *is_compressed)
{
    uint32_t bound;
    uint32_t compressed_len;
    txx_buffer compressed;
    txx_status status;

    if ((input == NULL && input_len != 0) || payload == NULL || is_compressed == NULL) {
        return TXX_ERR_ARG;
    }
    if (input_len > UINT32_MAX) {
        return TXX_ERR_OVERFLOW;
    }

    txx_buffer_init(payload);
    *is_compressed = 0;

    if (input_len < threshold || input_len == 0) {
        return copy_payload(input, input_len, payload);
    }

    bound = microz_compress_bound((uint32_t)input_len);
    status = txx_buffer_alloc(&compressed, bound);
    if (status != TXX_OK) {
        return status;
    }

    compressed_len = bound;
    if (microz_compress_block(input, (uint32_t)input_len, compressed.data, &compressed_len) != MICROZ_OK) {
        txx_buffer_secure_free(&compressed);
        return TXX_ERR_COMPRESS;
    }
    compressed.len = compressed_len;

    if ((size_t)compressed_len + (size_t)min_savings < input_len) {
        *payload = compressed;
        *is_compressed = 1;
        return TXX_OK;
    }

    txx_buffer_secure_free(&compressed);
    return copy_payload(input, input_len, payload);
}

txx_status txx_compress_decode(const uint8_t *payload,
                               size_t payload_len,
                               size_t plaintext_len,
                               int is_compressed,
                               txx_buffer *plaintext)
{
    txx_status status;
    uint32_t output_len;

    if ((payload == NULL && payload_len != 0) || plaintext == NULL) {
        return TXX_ERR_ARG;
    }
    if (payload_len > UINT32_MAX || plaintext_len > UINT32_MAX) {
        return TXX_ERR_OVERFLOW;
    }

    txx_buffer_init(plaintext);
    if (!is_compressed) {
        if (payload_len != plaintext_len) {
            return TXX_ERR_FORMAT;
        }
        return copy_payload(payload, payload_len, plaintext);
    }

    status = txx_buffer_alloc(plaintext, plaintext_len);
    if (status != TXX_OK) {
        return status;
    }

    output_len = (uint32_t)plaintext_len;
    if (microz_decompress_block(payload, (uint32_t)payload_len, plaintext->data, &output_len) != MICROZ_OK ||
        output_len != plaintext_len) {
        txx_buffer_secure_free(plaintext);
        return TXX_ERR_DECOMPRESS;
    }

    return TXX_OK;
}
