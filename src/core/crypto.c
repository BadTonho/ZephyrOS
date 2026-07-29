#include "core/crypto.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"

#define SHA256_BLOCK_SIZE 64U
#define SHA256_WORD_COUNT 64U

static const uint32_t sha256_constants[SHA256_WORD_COUNT] = {
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
    0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
    0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
    0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
    0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
    0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
    0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
    0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
    0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
    0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
    0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
    0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
    0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
    0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
    0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
    0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U
};

static uint32_t sha256_rotate_right(uint32_t value, uint32_t count) {
    return (value >> count) | (value << (32U - count));
}

static uint32_t sha256_load_be(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static void sha256_store_be(uint8_t* output, uint32_t value) {
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static void sha256_expand_words(uint32_t words[SHA256_WORD_COUNT],
                                const uint8_t block[SHA256_BLOCK_SIZE]) {
    for (uint32_t index = 0; index < 16U; index++) {
        words[index] = sha256_load_be(block + index * 4U);
    }
    for (uint32_t index = 16U; index < SHA256_WORD_COUNT; index++) {
        uint32_t first = words[index - 15U];
        uint32_t second = words[index - 2U];
        uint32_t sigma0 = sha256_rotate_right(first, 7U) ^
                          sha256_rotate_right(first, 18U) ^ (first >> 3U);
        uint32_t sigma1 = sha256_rotate_right(second, 17U) ^
                          sha256_rotate_right(second, 19U) ^ (second >> 10U);
        words[index] = words[index - 16U] + sigma0 +
                       words[index - 7U] + sigma1;
    }
}

static void sha256_compress(crypto_sha256_ctx_t* ctx,
                            const uint8_t block[SHA256_BLOCK_SIZE]) {
    uint32_t words[SHA256_WORD_COUNT];
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    sha256_expand_words(words, block);
    for (uint32_t index = 0; index < SHA256_WORD_COUNT; index++) {
        uint32_t sigma1 = sha256_rotate_right(e, 6U) ^
                          sha256_rotate_right(e, 11U) ^
                          sha256_rotate_right(e, 25U);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + sigma1 + choice +
                         sha256_constants[index] + words[index];
        uint32_t sigma0 = sha256_rotate_right(a, 2U) ^
                          sha256_rotate_right(a, 13U) ^
                          sha256_rotate_right(a, 22U);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = sigma0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
    kmemset(words, 0, sizeof(words));
}

int crypto_sha256_init(crypto_sha256_ctx_t* ctx) {
    if (!ctx) {
        LOG_ERROR("CRYPTO", "Contexto SHA-256 nulo");
        return ERR_NULL;
    }
    ctx->state[0] = 0x6A09E667U;
    ctx->state[1] = 0xBB67AE85U;
    ctx->state[2] = 0x3C6EF372U;
    ctx->state[3] = 0xA54FF53AU;
    ctx->state[4] = 0x510E527FU;
    ctx->state[5] = 0x9B05688CU;
    ctx->state[6] = 0x1F83D9ABU;
    ctx->state[7] = 0x5BE0CD19U;
    ctx->total_size = 0;
    ctx->buffer_size = 0;
    kmemset(ctx->buffer, 0, sizeof(ctx->buffer));
    return OK;
}

static void sha256_copy_input(crypto_sha256_ctx_t* ctx,
                              const uint8_t** data, uint32_t* size) {
    uint32_t available = SHA256_BLOCK_SIZE - ctx->buffer_size;
    uint32_t copied = *size < available ? *size : available;

    kmemcpy(ctx->buffer + ctx->buffer_size, *data, copied);
    ctx->buffer_size += copied;
    *data += copied;
    *size -= copied;
}

int crypto_sha256_update(crypto_sha256_ctx_t* ctx, const uint8_t* data,
                         uint32_t size) {
    uint32_t original_size = size;

    if (!ctx || (!data && size > 0U)) {
        LOG_ERROR("CRYPTO", "Argumento nulo no SHA-256 incremental");
        return ERR_NULL;
    }
    while (size > 0U) {
        sha256_copy_input(ctx, &data, &size);
        if (ctx->buffer_size == SHA256_BLOCK_SIZE) {
            sha256_compress(ctx, ctx->buffer);
            ctx->buffer_size = 0;
            kmemset(ctx->buffer, 0, sizeof(ctx->buffer));
        }
    }
    ctx->total_size += original_size;
    return OK;
}

static void sha256_append_length(crypto_sha256_ctx_t* ctx,
                                 uint64_t bit_length) {
    for (uint32_t index = 0; index < 8U; index++) {
        ctx->buffer[63U - index] = (uint8_t)(bit_length >> (index * 8U));
    }
}

int crypto_sha256_final(crypto_sha256_ctx_t* ctx,
                        uint8_t hash[CRYPTO_SHA256_SIZE]) {
    uint64_t bit_length;

    if (!ctx || !hash) {
        LOG_ERROR("CRYPTO", "Argumento nulo ao finalizar SHA-256");
        return ERR_NULL;
    }
    bit_length = ctx->total_size * 8U;
    ctx->buffer[ctx->buffer_size++] = 0x80U;
    if (ctx->buffer_size > 56U) {
        while (ctx->buffer_size < SHA256_BLOCK_SIZE) {
            ctx->buffer[ctx->buffer_size++] = 0;
        }
        sha256_compress(ctx, ctx->buffer);
        ctx->buffer_size = 0;
        kmemset(ctx->buffer, 0, sizeof(ctx->buffer));
    }
    while (ctx->buffer_size < 56U) {
        ctx->buffer[ctx->buffer_size++] = 0;
    }
    sha256_append_length(ctx, bit_length);
    sha256_compress(ctx, ctx->buffer);
    for (uint32_t index = 0; index < 8U; index++) {
        sha256_store_be(hash + index * 4U, ctx->state[index]);
    }
    kmemset(ctx, 0, sizeof(*ctx));
    return OK;
}

int crypto_sha256(const uint8_t* data, uint32_t size,
                  uint8_t hash[CRYPTO_SHA256_SIZE]) {
    crypto_sha256_ctx_t ctx;
    int result;

    if (!hash || (!data && size > 0U)) {
        LOG_ERROR("CRYPTO", "Argumento nulo no SHA-256");
        return ERR_NULL;
    }
    result = crypto_sha256_init(&ctx);
    if (result != OK) return result;
    result = crypto_sha256_update(&ctx, data, size);
    if (result != OK) return result;
    return crypto_sha256_final(&ctx, hash);
}

int crypto_equal(const uint8_t* first, const uint8_t* second, uint32_t size) {
    uint8_t difference = 0;

    if (!first || !second) {
        LOG_ERROR("CRYPTO", "Comparacao criptografica com ponteiro nulo");
        return 0;
    }
    for (uint32_t index = 0; index < size; index++) {
        difference |= first[index] ^ second[index];
    }
    return difference == 0;
}

static int crypto_test_sha256(void) {
    static const uint8_t message[] = {'a', 'b', 'c'};
    static const uint8_t expected[CRYPTO_SHA256_SIZE] = {
        0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
        0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
        0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
        0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
    };
    uint8_t actual[CRYPTO_SHA256_SIZE];

    if (crypto_sha256(message, sizeof(message), actual) != OK ||
        !crypto_equal(actual, expected, sizeof(expected))) {
        LOG_ERROR("CRYPTO", "Autoteste SHA-256 falhou");
        return ERR_INVALID;
    }
    return OK;
}

static int crypto_test_sha512(void) {
    static const uint8_t message[] = {'a', 'b', 'c'};
    static const uint8_t expected[CRYPTO_SHA512_SIZE] = {
        0xDD, 0xAF, 0x35, 0xA1, 0x93, 0x61, 0x7A, 0xBA,
        0xCC, 0x41, 0x73, 0x49, 0xAE, 0x20, 0x41, 0x31,
        0x12, 0xE6, 0xFA, 0x4E, 0x89, 0xA9, 0x7E, 0xA2,
        0x0A, 0x9E, 0xEE, 0xE6, 0x4B, 0x55, 0xD3, 0x9A,
        0x21, 0x92, 0x99, 0x2A, 0x27, 0x4F, 0xC1, 0xA8,
        0x36, 0xBA, 0x3C, 0x23, 0xA3, 0xFE, 0xEB, 0xBD,
        0x45, 0x4D, 0x44, 0x23, 0x64, 0x3C, 0xE8, 0x0E,
        0x2A, 0x9A, 0xC9, 0x4F, 0xA5, 0x4C, 0xA4, 0x9F
    };
    uint8_t actual[CRYPTO_SHA512_SIZE];

    if (crypto_sha512_digest(message, sizeof(message), actual) != OK ||
        !crypto_equal(actual, expected, sizeof(expected))) {
        LOG_ERROR("CRYPTO", "Autoteste SHA-512 falhou");
        return ERR_INVALID;
    }
    return OK;
}

static int crypto_test_ed25519_vector(
    const uint8_t public_key[CRYPTO_ED25519_PUBLIC_KEY_SIZE],
    const uint8_t signature[CRYPTO_ED25519_SIGNATURE_SIZE],
    const uint8_t* message, uint32_t message_size) {
    crypto_ed25519_verify_ctx_t ctx;
    int result = crypto_ed25519_verify_init(&ctx, signature, public_key);

    if (result != OK) return result;
    result = crypto_ed25519_verify_update(&ctx, message, message_size);
    if (result != OK) return result;
    return crypto_ed25519_verify_final(&ctx);
}

static int crypto_test_ed25519(void) {
    static const uint8_t public_key1[CRYPTO_ED25519_PUBLIC_KEY_SIZE] = {
        0xD7, 0x5A, 0x98, 0x01, 0x82, 0xB1, 0x0A, 0xB7,
        0xD5, 0x4B, 0xFE, 0xD3, 0xC9, 0x64, 0x07, 0x3A,
        0x0E, 0xE1, 0x72, 0xF3, 0xDA, 0xA6, 0x23, 0x25,
        0xAF, 0x02, 0x1A, 0x68, 0xF7, 0x07, 0x51, 0x1A
    };
    static const uint8_t signature1[CRYPTO_ED25519_SIGNATURE_SIZE] = {
        0xE5, 0x56, 0x43, 0x00, 0xC3, 0x60, 0xAC, 0x72,
        0x90, 0x86, 0xE2, 0xCC, 0x80, 0x6E, 0x82, 0x8A,
        0x84, 0x87, 0x7F, 0x1E, 0xB8, 0xE5, 0xD9, 0x74,
        0xD8, 0x73, 0xE0, 0x65, 0x22, 0x49, 0x01, 0x55,
        0x5F, 0xB8, 0x82, 0x15, 0x90, 0xA3, 0x3B, 0xAC,
        0xC6, 0x1E, 0x39, 0x70, 0x1C, 0xF9, 0xB4, 0x6B,
        0xD2, 0x5B, 0xF5, 0xF0, 0x59, 0x5B, 0xBE, 0x24,
        0x65, 0x51, 0x41, 0x43, 0x8E, 0x7A, 0x10, 0x0B
    };
    static const uint8_t public_key2[CRYPTO_ED25519_PUBLIC_KEY_SIZE] = {
        0x3D, 0x40, 0x17, 0xC3, 0xE8, 0x43, 0x89, 0x5A,
        0x92, 0xB7, 0x0A, 0xA7, 0x4D, 0x1B, 0x7E, 0xBC,
        0x9C, 0x98, 0x2C, 0xCF, 0x2E, 0xC4, 0x96, 0x8C,
        0xC0, 0xCD, 0x55, 0xF1, 0x2A, 0xF4, 0x66, 0x0C
    };
    static const uint8_t signature2[CRYPTO_ED25519_SIGNATURE_SIZE] = {
        0x92, 0xA0, 0x09, 0xA9, 0xF0, 0xD4, 0xCA, 0xB8,
        0x72, 0x0E, 0x82, 0x0B, 0x5F, 0x64, 0x25, 0x40,
        0xA2, 0xB2, 0x7B, 0x54, 0x16, 0x50, 0x3F, 0x8F,
        0xB3, 0x76, 0x22, 0x23, 0xEB, 0xDB, 0x69, 0xDA,
        0x08, 0x5A, 0xC1, 0xE4, 0x3E, 0x15, 0x99, 0x6E,
        0x45, 0x8F, 0x36, 0x13, 0xD0, 0xF1, 0x1D, 0x8C,
        0x38, 0x7B, 0x2E, 0xAE, 0xB4, 0x30, 0x2A, 0xEE,
        0xB0, 0x0D, 0x29, 0x16, 0x12, 0xBB, 0x0C, 0x00
    };
    static const uint8_t message2[] = {0x72};

    if (crypto_test_ed25519_vector(
            public_key1, signature1, 0, 0) != OK ||
        crypto_test_ed25519_vector(
            public_key2, signature2, message2, sizeof(message2)) != OK) {
        LOG_ERROR("CRYPTO", "Autoteste Ed25519 RFC 8032 falhou");
        return ERR_INVALID;
    }
    return OK;
}

int crypto_self_test(void) {
    LOG_INFO("CRYPTO", "Iniciando autotestes criptograficos");

    if (crypto_test_sha256() != OK ||
        crypto_test_sha512() != OK ||
        crypto_test_ed25519() != OK) {
        LOG_ERROR("CRYPTO", "Autotestes criptograficos falharam");
        return ERR_INVALID;
    }
    LOG_INFO("CRYPTO", "Autotestes criptograficos concluidos com sucesso");
    return OK;
}
