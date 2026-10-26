#include "core/txx_format.h"

#include <string.h>

const uint8_t TXX_MAGIC[TXX_MAGIC_SIZE] = { 'T', 'X', 'X', '1', 0x0dU, 0x0aU, 0x1aU, 0x0aU };

static void put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xffU);
    dst[1] = (uint8_t)((value >> 8U) & 0xffU);
}

static void put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xffU);
    dst[1] = (uint8_t)((value >> 8U) & 0xffU);
    dst[2] = (uint8_t)((value >> 16U) & 0xffU);
    dst[3] = (uint8_t)((value >> 24U) & 0xffU);
}

static void put_u64(uint8_t *dst, uint64_t value)
{
    put_u32(dst, (uint32_t)(value & 0xffffffffULL));
    put_u32(dst + 4, (uint32_t)((value >> 32U) & 0xffffffffULL));
}

static uint16_t get_u16(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8U));
}

static uint32_t get_u32(const uint8_t *src)
{
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8U) | ((uint32_t)src[2] << 16U) |
           ((uint32_t)src[3] << 24U);
}

static uint64_t get_u64(const uint8_t *src)
{
    return (uint64_t)get_u32(src) | ((uint64_t)get_u32(src + 4) << 32U);
}

void txx_header_init(txx_header *header)
{
    if (header != NULL) {
        memset(header, 0, sizeof(*header));
        header->version = TXX_VERSION_1;
        header->header_len = TXX_HEADER_SIZE;
        header->crypto_alg = TXX_CRYPTO_XCHACHA20_POLY1305;
        header->kdf_alg = TXX_KDF_ARGON2ID;
    }
}

void txx_header_encode(uint8_t out[TXX_HEADER_SIZE], const txx_header *header)
{
    memset(out, 0, TXX_HEADER_SIZE);
    memcpy(out, TXX_MAGIC, TXX_MAGIC_SIZE);
    put_u16(out + 8, header->version);
    put_u16(out + 10, header->header_len);
    put_u32(out + 12, header->flags);
    put_u16(out + 16, header->crypto_alg);
    put_u16(out + 18, header->kdf_alg);
    put_u16(out + 20, header->compress_alg);
    put_u16(out + 22, header->reserved_alg);
    put_u32(out + 24, header->kdf_memory_blocks);
    put_u32(out + 28, header->kdf_passes);
    put_u32(out + 32, header->kdf_lanes);
    put_u32(out + 36, header->salt_len);
    put_u32(out + 40, header->nonce_len);
    put_u32(out + 44, header->tag_len);
    put_u64(out + 48, header->plaintext_len);
    put_u64(out + 56, header->payload_len);
    put_u64(out + 64, header->ciphertext_len);
}

txx_status txx_header_decode(const uint8_t data[TXX_HEADER_SIZE], txx_header *header)
{
    size_t i;

    if (data == NULL || header == NULL) {
        return TXX_ERR_ARG;
    }
    if (memcmp(data, TXX_MAGIC, TXX_MAGIC_SIZE) != 0) {
        return TXX_ERR_FORMAT;
    }

    memset(header, 0, sizeof(*header));
    header->version = get_u16(data + 8);
    header->header_len = get_u16(data + 10);
    header->flags = get_u32(data + 12);
    header->crypto_alg = get_u16(data + 16);
    header->kdf_alg = get_u16(data + 18);
    header->compress_alg = get_u16(data + 20);
    header->reserved_alg = get_u16(data + 22);
    header->kdf_memory_blocks = get_u32(data + 24);
    header->kdf_passes = get_u32(data + 28);
    header->kdf_lanes = get_u32(data + 32);
    header->salt_len = get_u32(data + 36);
    header->nonce_len = get_u32(data + 40);
    header->tag_len = get_u32(data + 44);
    header->plaintext_len = get_u64(data + 48);
    header->payload_len = get_u64(data + 56);
    header->ciphertext_len = get_u64(data + 64);

    for (i = 72; i < TXX_HEADER_SIZE; ++i) {
        if (data[i] != 0) {
            return TXX_ERR_FORMAT;
        }
    }

    return TXX_OK;
}

txx_status txx_header_validate_supported(const txx_header *header)
{
    if (header == NULL) {
        return TXX_ERR_ARG;
    }
    if (header->version != TXX_VERSION_1 || header->header_len != TXX_HEADER_SIZE) {
        return TXX_ERR_UNSUPPORTED;
    }
    if (header->reserved_alg != 0 || (header->flags & ~TXX_FLAG_COMPRESSED) != 0) {
        return TXX_ERR_UNSUPPORTED;
    }
    if (header->crypto_alg != TXX_CRYPTO_XCHACHA20_POLY1305 || header->kdf_alg != TXX_KDF_ARGON2ID) {
        return TXX_ERR_UNSUPPORTED;
    }
    if ((header->flags & TXX_FLAG_COMPRESSED) != 0) {
        if (header->compress_alg != TXX_COMPRESS_MICROZ_DEFLATE) {
            return TXX_ERR_UNSUPPORTED;
        }
    } else if (header->compress_alg != TXX_COMPRESS_NONE) {
        return TXX_ERR_UNSUPPORTED;
    }
    if (header->kdf_memory_blocks < TXX_DECODE_MIN_KDF_MEMORY_BLOCKS ||
        header->kdf_passes < TXX_MIN_KDF_PASSES ||
        header->kdf_passes > TXX_MAX_KDF_PASSES ||
        header->kdf_lanes < 1U) {
        return TXX_ERR_UNSUPPORTED;
    }
    if (header->kdf_memory_blocks > TXX_MAX_KDF_MEMORY_BLOCKS ||
        header->kdf_lanes > TXX_MAX_KDF_LANES ||
        header->kdf_lanes > header->kdf_memory_blocks / 8U) {
        return TXX_ERR_UNSUPPORTED;
    }
    if (header->salt_len != 16U || header->nonce_len != 24U || header->tag_len != 16U) {
        return TXX_ERR_UNSUPPORTED;
    }
    if (header->payload_len != header->ciphertext_len) {
        return TXX_ERR_FORMAT;
    }
    if ((header->flags & TXX_FLAG_COMPRESSED) == 0 && header->payload_len != header->plaintext_len) {
        return TXX_ERR_FORMAT;
    }
    if (header->payload_len > TXX_MAX_TEXT_BYTES || header->plaintext_len > TXX_MAX_TEXT_BYTES) {
        return TXX_ERR_UNSUPPORTED;
    }

    return TXX_OK;
}
