#ifndef TXX_CODEC_H
#define TXX_CODEC_H

#include <stddef.h>
#include <stdint.h>

#include "core/txx_buffer.h"
#include "core/txx_crypto.h"

typedef struct txx_codec_options {
    uint32_t compression_threshold;
    uint32_t compression_min_savings;
    txx_kdf_params kdf;
} txx_codec_options;

void txx_codec_default_options(txx_codec_options *options);
txx_status txx_encode_text(const uint8_t *plaintext,
                           size_t plaintext_len,
                           const char *password,
                           const txx_codec_options *options,
                           txx_buffer *encoded);
txx_status txx_decode_text(const uint8_t *encoded,
                           size_t encoded_len,
                           const char *password,
                           txx_buffer *plaintext);

#endif