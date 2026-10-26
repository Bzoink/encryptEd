#include "core/txx_codec.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "core/txx_compress.h"
#include "core/txx_format.h"
#include "core/utf8.h"
#include "platform/random.h"
#include "platform/secure_mem.h"

static int add_overflow_size(size_t a, size_t b, size_t *result)
{
    if (a > SIZE_MAX - b) {
        return 1;
    }
    *result = a + b;
    return 0;
}

static txx_status txx_file_size(const txx_header *header, size_t *size)
{
    size_t total = TXX_HEADER_SIZE;

    if (add_overflow_size(total, (size_t)header->salt_len, &total) ||
        add_overflow_size(total, (size_t)header->nonce_len, &total) ||
        add_overflow_size(total, (size_t)header->tag_len, &total) ||
        add_overflow_size(total, (size_t)header->ciphertext_len, &total)) {
        return TXX_ERR_OVERFLOW;
    }
    *size = total;
    return TXX_OK;
}

static size_t cstr_len(const char *text)
{
    return text == NULL ? 0U : strlen(text);
}

void txx_codec_default_options(txx_codec_options *options)
{
    if (options != NULL) {
        options->compression_threshold = TXX_COMPRESS_DEFAULT_THRESHOLD;
        options->compression_min_savings = TXX_COMPRESS_DEFAULT_MIN_SAVINGS;
        options->kdf = txx_kdf_default_params();
    }
}

txx_status txx_encode_text(const uint8_t *plaintext,
                           size_t plaintext_len,
                           const char *password,
                           const txx_codec_options *options,
                           txx_buffer *encoded)
{
    txx_codec_options local_options;
    txx_header header;
    txx_buffer payload;
    uint8_t header_bytes[TXX_HEADER_SIZE];
    uint8_t key[TXX_KEY_LEN];
    uint8_t salt[TXX_SALT_LEN];
    uint8_t nonce[TXX_NONCE_LEN];
    uint8_t tag[TXX_TAG_LEN];
    uint8_t zero = 0;
    size_t total_len;
    size_t offset;
    size_t password_len;
    int is_compressed;
    txx_status status;

    if ((plaintext == NULL && plaintext_len != 0) || password == NULL || encoded == NULL) {
        return TXX_ERR_ARG;
    }
    if (plaintext_len > UINT64_MAX || plaintext_len > (size_t)TXX_MAX_TEXT_BYTES) {
        return TXX_ERR_OVERFLOW;
    }

    txx_buffer_init(encoded);
    txx_buffer_init(&payload);
    memset(key, 0, sizeof(key));
    memset(tag, 0, sizeof(tag));

    if (!txx_utf8_validate(plaintext, plaintext_len)) {
        return TXX_ERR_UTF8;
    }

    if (options == NULL) {
        txx_codec_default_options(&local_options);
        options = &local_options;
    }
    if (options->kdf.memory_blocks > TXX_MAX_KDF_MEMORY_BLOCKS ||
        options->kdf.memory_blocks < TXX_MIN_KDF_MEMORY_BLOCKS ||
        options->kdf.passes < TXX_MIN_KDF_PASSES ||
        options->kdf.passes > TXX_MAX_KDF_PASSES ||
        options->kdf.lanes < 1U ||
        options->kdf.lanes > TXX_MAX_KDF_LANES ||
        options->kdf.lanes > options->kdf.memory_blocks / 8U) {
        return TXX_ERR_UNSUPPORTED;
    }

    status = txx_compress_encode(plaintext, plaintext_len, options->compression_threshold,
                                 options->compression_min_savings, &payload, &is_compressed);
    if (status != TXX_OK) {
        return status;
    }

    if (txx_random_bytes(salt, sizeof(salt)) != 0 || txx_random_bytes(nonce, sizeof(nonce)) != 0) {
        txx_buffer_secure_free(&payload);
        return TXX_ERR_RANDOM;
    }

    txx_header_init(&header);
    header.flags = is_compressed ? TXX_FLAG_COMPRESSED : 0U;
    header.compress_alg = is_compressed ? TXX_COMPRESS_MICROZ_DEFLATE : TXX_COMPRESS_NONE;
    header.kdf_memory_blocks = options->kdf.memory_blocks;
    header.kdf_passes = options->kdf.passes;
    header.kdf_lanes = options->kdf.lanes;
    header.salt_len = TXX_SALT_LEN;
    header.nonce_len = TXX_NONCE_LEN;
    header.tag_len = TXX_TAG_LEN;
    header.plaintext_len = (uint64_t)plaintext_len;
    header.payload_len = (uint64_t)payload.len;
    header.ciphertext_len = (uint64_t)payload.len;
    txx_header_encode(header_bytes, &header);

    status = txx_file_size(&header, &total_len);
    if (status != TXX_OK) {
        txx_buffer_secure_free(&payload);
        return status;
    }
    status = txx_buffer_alloc(encoded, total_len);
    if (status != TXX_OK) {
        txx_buffer_secure_free(&payload);
        return status;
    }

    password_len = cstr_len(password);
    status = txx_derive_key(key, (const uint8_t *)password, password_len, salt, options->kdf);
    if (status != TXX_OK) {
        txx_buffer_secure_free(&payload);
        txx_buffer_secure_free(encoded);
        return status;
    }

    memcpy(encoded->data, header_bytes, TXX_HEADER_SIZE);
    offset = TXX_HEADER_SIZE;
    memcpy(encoded->data + offset, salt, sizeof(salt));
    offset += sizeof(salt);
    memcpy(encoded->data + offset, nonce, sizeof(nonce));
    offset += sizeof(nonce);

    txx_aead_lock(encoded->data + offset + TXX_TAG_LEN, tag, key, nonce, header_bytes, sizeof(header_bytes),
                  payload.len == 0 ? &zero : payload.data, payload.len);
    memcpy(encoded->data + offset, tag, sizeof(tag));

    txx_secure_zero(key, sizeof(key));
    txx_secure_zero(tag, sizeof(tag));
    txx_buffer_secure_free(&payload);
    return TXX_OK;
}

txx_status txx_decode_text(const uint8_t *encoded,
                           size_t encoded_len,
                           const char *password,
                           txx_buffer *plaintext)
{
    txx_header header;
    txx_kdf_params kdf;
    txx_buffer payload;
    const uint8_t *salt;
    const uint8_t *nonce;
    const uint8_t *tag;
    const uint8_t *ciphertext;
    uint8_t *payload_plain;
    uint8_t key[TXX_KEY_LEN];
    uint8_t zero = 0;
    size_t expected_len;
    size_t offset;
    size_t password_len;
    txx_status status;

    if ((encoded == NULL && encoded_len != 0) || password == NULL || plaintext == NULL) {
        return TXX_ERR_ARG;
    }
    if (encoded_len < TXX_HEADER_SIZE) {
        return TXX_ERR_FORMAT;
    }

    txx_buffer_init(plaintext);
    txx_buffer_init(&payload);
    memset(key, 0, sizeof(key));

    status = txx_header_decode(encoded, &header);
    if (status != TXX_OK) {
        return status;
    }
    status = txx_header_validate_supported(&header);
    if (status != TXX_OK) {
        return status;
    }
    if (header.plaintext_len > SIZE_MAX || header.payload_len > SIZE_MAX || header.ciphertext_len > SIZE_MAX) {
        return TXX_ERR_OVERFLOW;
    }

    status = txx_file_size(&header, &expected_len);
    if (status != TXX_OK) {
        return status;
    }
    if (encoded_len != expected_len) {
        return TXX_ERR_FORMAT;
    }

    offset = TXX_HEADER_SIZE;
    salt = encoded + offset;
    offset += header.salt_len;
    nonce = encoded + offset;
    offset += header.nonce_len;
    tag = encoded + offset;
    offset += header.tag_len;
    ciphertext = encoded + offset;

    status = txx_buffer_alloc(&payload, (size_t)header.payload_len);
    if (status != TXX_OK) {
        return status;
    }

    kdf.memory_blocks = header.kdf_memory_blocks;
    kdf.passes = header.kdf_passes;
    kdf.lanes = header.kdf_lanes;
    password_len = cstr_len(password);
    status = txx_derive_key(key, (const uint8_t *)password, password_len, salt, kdf);
    if (status != TXX_OK) {
        txx_buffer_secure_free(&payload);
        return status;
    }

    payload_plain = payload.len == 0 ? &zero : payload.data;
    status = txx_aead_unlock(payload_plain, tag, key, nonce, encoded, TXX_HEADER_SIZE, ciphertext, payload.len);
    txx_secure_zero(key, sizeof(key));
    if (status != TXX_OK) {
        txx_buffer_secure_free(&payload);
        return status;
    }

    status = txx_compress_decode(payload.data, payload.len, (size_t)header.plaintext_len,
                                 (header.flags & TXX_FLAG_COMPRESSED) != 0, plaintext);
    txx_buffer_secure_free(&payload);
    if (status != TXX_OK) {
        return status;
    }

    if (!txx_utf8_validate(plaintext->data, plaintext->len)) {
        txx_buffer_secure_free(plaintext);
        return TXX_ERR_UTF8;
    }

    return TXX_OK;
}
