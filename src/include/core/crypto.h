#ifndef CRYPTO_H
#define CRYPTO_H

#include "types.h"

#define CRYPTO_SHA256_SIZE 32U
#define CRYPTO_SHA512_SIZE 64U
#define CRYPTO_ED25519_PUBLIC_KEY_SIZE 32U
#define CRYPTO_ED25519_SIGNATURE_SIZE 64U

typedef struct {
    uint32_t state[8];
    uint64_t total_size;
    uint8_t buffer[64];
    uint32_t buffer_size;
} crypto_sha256_ctx_t;

typedef struct {
    uint64_t hash[8];
    uint64_t input[16];
    uint64_t input_size[2];
    size_t input_idx;
} crypto_sha512_ctx_t;

typedef struct {
    crypto_sha512_ctx_t hash;
    uint8_t signature[CRYPTO_ED25519_SIGNATURE_SIZE];
    uint8_t public_key[CRYPTO_ED25519_PUBLIC_KEY_SIZE];
    uint8_t active;
} crypto_ed25519_verify_ctx_t;

int crypto_sha256_init(crypto_sha256_ctx_t* ctx);
int crypto_sha256_update(crypto_sha256_ctx_t* ctx, const uint8_t* data,
                         uint32_t size);
int crypto_sha256_final(crypto_sha256_ctx_t* ctx,
                        uint8_t hash[CRYPTO_SHA256_SIZE]);
int crypto_sha256(const uint8_t* data, uint32_t size,
                  uint8_t hash[CRYPTO_SHA256_SIZE]);
int crypto_sha512_digest(const uint8_t* data, uint32_t size,
                         uint8_t hash[CRYPTO_SHA512_SIZE]);
int crypto_equal(const uint8_t* first, const uint8_t* second, uint32_t size);

int crypto_ed25519_verify_init(
    crypto_ed25519_verify_ctx_t* ctx,
    const uint8_t signature[CRYPTO_ED25519_SIGNATURE_SIZE],
    const uint8_t public_key[CRYPTO_ED25519_PUBLIC_KEY_SIZE]);
int crypto_ed25519_verify_update(crypto_ed25519_verify_ctx_t* ctx,
                                 const uint8_t* data, uint32_t size);
int crypto_ed25519_verify_final(crypto_ed25519_verify_ctx_t* ctx);
int crypto_self_test(void);

#endif
