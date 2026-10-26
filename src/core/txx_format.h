#ifndef TXX_FORMAT_H
#define TXX_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#include "core/txx_status.h"

#define TXX_HEADER_SIZE 96U
#define TXX_MAGIC_SIZE 8U

#define TXX_VERSION_1 1U
#define TXX_FLAG_COMPRESSED 0x00000001U

#define TXX_CRYPTO_XCHACHA20_POLY1305 1U
#define TXX_KDF_ARGON2ID 1U
#define TXX_COMPRESS_NONE 0U
#define TXX_COMPRESS_MICROZ_DEFLATE 1U

#define TXX_MAX_KDF_MEMORY_BLOCKS 262144U
#define TXX_MAX_KDF_LANES 8U
/* Argon2id RFC 9106 recommends >= 8*lanes blocks; we additionally enforce */
/* a defense-in-depth range of 256..262144 blocks, 2..10 passes, and       */
/* 1..8 lanes with memory_blocks >= 8*lanes for untrusted headers.          */
#define TXX_MIN_KDF_MEMORY_BLOCKS 256U
#define TXX_MIN_KDF_PASSES 2U
#define TXX_MAX_KDF_PASSES 10U
#define TXX_MAX_TEXT_BYTES (128ULL * 1024ULL * 1024ULL)
#define TXX_FILE_OVERHEAD_BYTES 56ULL
#define TXX_MAX_FILE_BYTES (TXX_MAX_TEXT_BYTES + TXX_HEADER_SIZE + TXX_FILE_OVERHEAD_BYTES)

extern const uint8_t TXX_MAGIC[TXX_MAGIC_SIZE];

typedef struct txx_header {
    uint16_t version;
    uint16_t header_len;
    uint32_t flags;
    uint16_t crypto_alg;
    uint16_t kdf_alg;
    uint16_t compress_alg;
    uint16_t reserved_alg;
    uint32_t kdf_memory_blocks;
    uint32_t kdf_passes;
    uint32_t kdf_lanes;
    uint32_t salt_len;
    uint32_t nonce_len;
    uint32_t tag_len;
    uint64_t plaintext_len;
    uint64_t payload_len;
    uint64_t ciphertext_len;
} txx_header;

void txx_header_init(txx_header *header);
void txx_header_encode(uint8_t out[TXX_HEADER_SIZE], const txx_header *header);
txx_status txx_header_decode(const uint8_t data[TXX_HEADER_SIZE], txx_header *header);
txx_status txx_header_validate_supported(const txx_header *header);

#endif
