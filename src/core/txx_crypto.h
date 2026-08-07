#ifndef TXX_CRYPTO_H
#define TXX_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#include "core/txx_status.h"

#define TXX_KEY_LEN 32U
#define TXX_SALT_LEN 16U
#define TXX_NONCE_LEN 24U
#define TXX_TAG_LEN 16U

typedef struct txx_kdf_params {
    uint32_t memory_blocks;
    uint32_t passes;
    uint32_t lanes;
} txx_kdf_params;

txx_kdf_params txx_kdf_default_params(void);
txx_status txx_derive_key(uint8_t key[TXX_KEY_LEN],
                          const uint8_t *password,
                          size_t password_len,
                          const uint8_t salt[TXX_SALT_LEN],
                          txx_kdf_params params);
void txx_aead_lock(uint8_t *ciphertext,
                   uint8_t tag[TXX_TAG_LEN],
                   const uint8_t key[TXX_KEY_LEN],
                   const uint8_t nonce[TXX_NONCE_LEN],
                   const uint8_t *associated_data,
                   size_t associated_data_len,
                   const uint8_t *plaintext,
                   size_t plaintext_len);
txx_status txx_aead_unlock(uint8_t *plaintext,
                           const uint8_t tag[TXX_TAG_LEN],
                           const uint8_t key[TXX_KEY_LEN],
                           const uint8_t nonce[TXX_NONCE_LEN],
                           const uint8_t *associated_data,
                           size_t associated_data_len,
                           const uint8_t *ciphertext,
                           size_t ciphertext_len);

#endif