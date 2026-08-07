#include "core/txx_crypto.h"

#include <limits.h>
#include <stdlib.h>

#include "monocypher.h"
#include "platform/secure_mem.h"

txx_kdf_params txx_kdf_default_params(void)
{
    txx_kdf_params params;
    params.memory_blocks = 65536U; /* 64MB */
    params.passes = 3U;
    params.lanes = 1U;
    return params;
}

txx_status txx_derive_key(uint8_t key[TXX_KEY_LEN],
                          const uint8_t *password,
                          size_t password_len,
                          const uint8_t salt[TXX_SALT_LEN],
                          txx_kdf_params params)
{
    crypto_argon2_config config;
    crypto_argon2_inputs inputs;
    void *work_area;
    size_t work_area_len;

    if (key == NULL || (password == NULL && password_len != 0) || salt == NULL) {
        return TXX_ERR_ARG;
    }
    if (password_len > UINT32_MAX || params.memory_blocks < 8U * params.lanes || params.passes == 0U ||
        params.lanes == 0U) {
        return TXX_ERR_ARG;
    }
    work_area_len = (size_t)params.memory_blocks * 1024U;
    work_area = malloc(work_area_len);
    if (work_area == NULL) {
        return TXX_ERR_ALLOC;
    }

    config.algorithm = CRYPTO_ARGON2_ID;
    config.nb_blocks = params.memory_blocks;
    config.nb_passes = params.passes;
    config.nb_lanes = params.lanes;

    inputs.pass = password;
    inputs.pass_size = (uint32_t)password_len;
    inputs.salt = salt;
    inputs.salt_size = TXX_SALT_LEN;

    crypto_argon2(key, TXX_KEY_LEN, work_area, config, inputs, crypto_argon2_no_extras);
    txx_secure_zero(work_area, work_area_len);
    free(work_area);
    return TXX_OK;
}

void txx_aead_lock(uint8_t *ciphertext,
                   uint8_t tag[TXX_TAG_LEN],
                   const uint8_t key[TXX_KEY_LEN],
                   const uint8_t nonce[TXX_NONCE_LEN],
                   const uint8_t *associated_data,
                   size_t associated_data_len,
                   const uint8_t *plaintext,
                   size_t plaintext_len)
{
    crypto_aead_lock(ciphertext, tag, key, nonce, associated_data, associated_data_len, plaintext, plaintext_len);
}

txx_status txx_aead_unlock(uint8_t *plaintext,
                           const uint8_t tag[TXX_TAG_LEN],
                           const uint8_t key[TXX_KEY_LEN],
                           const uint8_t nonce[TXX_NONCE_LEN],
                           const uint8_t *associated_data,
                           size_t associated_data_len,
                           const uint8_t *ciphertext,
                           size_t ciphertext_len)
{
    if (crypto_aead_unlock(plaintext, tag, key, nonce, associated_data, associated_data_len, ciphertext,
                           ciphertext_len) != 0) {
        return TXX_ERR_AUTH;
    }
    return TXX_OK;
}