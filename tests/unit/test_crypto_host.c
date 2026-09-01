#include <stdint.h>
#include <stdio.h>

#include "core/crypto.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "drivers/serial.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

static void __attribute__((no_instrument_function)) coverage_record(
    void* function) {
    uintptr_t address = (uintptr_t)function;

    if (!coverage_active || !address) return;
    for (uint32_t index = 0U; index < coverage_count; index++) {
        if (coverage_addresses[index] == address) return;
    }
    if (coverage_count < HOST_COVERAGE_CAPACITY) {
        coverage_addresses[coverage_count++] = address;
    }
}

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(
    void* function, void* caller) {
    (void)caller;
    coverage_record(function);
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(
    void* function, void* caller) {
    (void)function;
    (void)caller;
}

static int __attribute__((no_instrument_function)) bytes_equal(
    const uint8_t* first, const uint8_t* second, uint32_t size) {
    for (uint32_t index = 0U; index < size; index++) {
        if (first[index] != second[index]) return 0;
    }
    return 1;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:core:crypto|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:crypto|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:crypto|value=0x%08X\n",
           (uint32_t)result);
}

uint32_t timer_get_ticks(void) {
    static uint32_t ticks = 100U;
    return ticks++;
}

uint32_t timer_get_frequency(void) {
    return 1000U;
}

uint8_t serial_is_ready(void) {
    return 1;
}

uint32_t serial_write_text(const char* text, uint32_t length) {
    (void)text;
    return length;
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void video_newline(void) {
}

int main(void) {
    static const uint8_t abc[] = {'a', 'b', 'c'};
    static const uint8_t abc_hash[CRYPTO_SHA256_SIZE] = {
        0xBAU, 0x78U, 0x16U, 0xBFU, 0x8FU, 0x01U, 0xCFU, 0xEAU,
        0x41U, 0x41U, 0x40U, 0xDEU, 0x5DU, 0xAEU, 0x22U, 0x23U,
        0xB0U, 0x03U, 0x61U, 0xA3U, 0x96U, 0x17U, 0x7AU, 0x9CU,
        0xB4U, 0x10U, 0xFFU, 0x61U, 0xF2U, 0x00U, 0x15U, 0xADU
    };
    static const uint8_t empty_hash[CRYPTO_SHA256_SIZE] = {
        0xE3U, 0xB0U, 0xC4U, 0x42U, 0x98U, 0xFCU, 0x1CU, 0x14U,
        0x9AU, 0xFBU, 0xF4U, 0xC8U, 0x99U, 0x6FU, 0xB9U, 0x24U,
        0x27U, 0xAEU, 0x41U, 0xE4U, 0x64U, 0x9BU, 0x93U, 0x4CU,
        0xA4U, 0x95U, 0x99U, 0x1BU, 0x78U, 0x52U, 0xB8U, 0x55U
    };
    static const uint8_t abc_sha512[CRYPTO_SHA512_SIZE] = {
        0xDDU, 0xAFU, 0x35U, 0xA1U, 0x93U, 0x61U, 0x7AU, 0xBAU,
        0xCCU, 0x41U, 0x73U, 0x49U, 0xAEU, 0x20U, 0x41U, 0x31U,
        0x12U, 0xE6U, 0xFAU, 0x4EU, 0x89U, 0xA9U, 0x7EU, 0xA2U,
        0x0AU, 0x9EU, 0xEEU, 0xE6U, 0x4BU, 0x55U, 0xD3U, 0x9AU,
        0x21U, 0x92U, 0x99U, 0x2AU, 0x27U, 0x4FU, 0xC1U, 0xA8U,
        0x36U, 0xBAU, 0x3CU, 0x23U, 0xA3U, 0xFEU, 0xEBU, 0xBDU,
        0x45U, 0x4DU, 0x44U, 0x23U, 0x64U, 0x3CU, 0xE8U, 0x0EU,
        0x2AU, 0x9AU, 0xC9U, 0x4FU, 0xA5U, 0x4CU, 0xA4U, 0x9FU
    };
    crypto_sha256_ctx_t context;
    crypto_ed25519_verify_ctx_t verify_context;
    uint8_t hash[CRYPTO_SHA512_SIZE];
    uint8_t different[CRYPTO_SHA256_SIZE];
    static const uint8_t public_key[CRYPTO_ED25519_PUBLIC_KEY_SIZE] = {
        0xD7U, 0x5AU, 0x98U, 0x01U, 0x82U, 0xB1U, 0x0AU, 0xB7U,
        0xD5U, 0x4BU, 0xFEU, 0xD3U, 0xC9U, 0x64U, 0x07U, 0x3AU,
        0x0EU, 0xE1U, 0x72U, 0xF3U, 0xDAU, 0xA6U, 0x23U, 0x25U,
        0xAFU, 0x02U, 0x1AU, 0x68U, 0xF7U, 0x07U, 0x51U, 0x1AU
    };
    static const uint8_t valid_signature[CRYPTO_ED25519_SIGNATURE_SIZE] = {
        0xE5U, 0x56U, 0x43U, 0x00U, 0xC3U, 0x60U, 0xACU, 0x72U,
        0x90U, 0x86U, 0xE2U, 0xCCU, 0x80U, 0x6EU, 0x82U, 0x8AU,
        0x84U, 0x87U, 0x7FU, 0x1EU, 0xB8U, 0xE5U, 0xD9U, 0x74U,
        0xD8U, 0x73U, 0xE0U, 0x65U, 0x22U, 0x49U, 0x01U, 0x55U,
        0x5FU, 0xB8U, 0x82U, 0x15U, 0x90U, 0xA3U, 0x3BU, 0xACU,
        0xC6U, 0x1EU, 0x39U, 0x70U, 0x1CU, 0xF9U, 0xB4U, 0x6BU,
        0xD2U, 0x5BU, 0xF5U, 0xF0U, 0x59U, 0x5BU, 0xBEU, 0x24U,
        0x65U, 0x51U, 0x41U, 0x43U, 0x8EU, 0x7AU, 0x10U, 0x0BU
    };
    uint8_t signature[CRYPTO_ED25519_SIGNATURE_SIZE];
    log_stats_t log_stats;
    int result = 0;

    coverage_active = 1U;
    kmemcpy(signature, valid_signature, sizeof(signature));
    signature[0] ^= 1U;
    log_init();
    if (crypto_sha256_init(NULL) != ERR_NULL) result = 1;
    if (!result && crypto_sha256_update(NULL, abc, sizeof(abc)) != ERR_NULL) {
        result = 2;
    }
    if (!result && crypto_sha256_final(NULL, hash) != ERR_NULL) result = 3;
    if (!result && crypto_sha256(abc, sizeof(abc), hash) != OK) result = 4;
    if (!result && !bytes_equal(hash, abc_hash, CRYPTO_SHA256_SIZE)) {
        result = 5;
    }
    if (!result && crypto_sha256(NULL, 0U, hash) != OK) result = 6;
    if (!result && !bytes_equal(hash, empty_hash, CRYPTO_SHA256_SIZE)) {
        result = 7;
    }
    if (!result && crypto_sha256_update(&context, NULL, 1U) != ERR_NULL) {
        result = 8;
    }
    if (!result && crypto_sha256_init(&context) != OK) result = 9;
    if (!result && crypto_sha256_update(&context, abc, 1U) != OK) result = 10;
    if (!result && crypto_sha256_update(&context, abc + 1U, 2U) != OK) {
        result = 11;
    }
    if (!result && crypto_sha256_final(&context, hash) != OK) result = 12;
    if (!result && !bytes_equal(hash, abc_hash, CRYPTO_SHA256_SIZE)) {
        result = 13;
    }
    if (!result && crypto_sha512_digest(abc, sizeof(abc), hash) != OK) {
        result = 14;
    }
    if (!result && !bytes_equal(hash, abc_sha512, CRYPTO_SHA512_SIZE)) {
        result = 15;
    }
    if (!result && crypto_sha256(NULL, sizeof(abc), hash) != ERR_NULL) {
        result = 16;
    }
    if (!result && crypto_sha256(abc, sizeof(abc), NULL) != ERR_NULL) {
        result = 17;
    }
    if (!result && !crypto_equal(abc_hash, abc_hash, CRYPTO_SHA256_SIZE)) {
        result = 18;
    }
    kmemcpy(different, abc_hash, sizeof(different));
    different[0] ^= 1U;
    if (!result && crypto_equal(abc_hash, different, CRYPTO_SHA256_SIZE)) {
        result = 19;
    }
    if (!result && crypto_equal(NULL, abc_hash, CRYPTO_SHA256_SIZE)) {
        result = 20;
    }
    if (!result && crypto_ed25519_verify_init(NULL, signature, public_key) !=
                      ERR_NULL) result = 21;
    if (!result && crypto_ed25519_verify_init(&verify_context, NULL,
                                               public_key) != ERR_NULL) {
        result = 22;
    }
    if (!result && crypto_ed25519_verify_init(&verify_context, signature,
                                               public_key) != OK) result = 23;
    if (!result && crypto_ed25519_verify_update(&verify_context, NULL, 1U) !=
                      ERR_NULL) result = 24;
    if (!result && crypto_ed25519_verify_update(&verify_context, abc,
                                                 sizeof(abc)) != OK) {
        result = 25;
    }
    if (!result && crypto_ed25519_verify_final(&verify_context) != ERR_INVALID) {
        result = 26;
    }
    if (!result && crypto_self_test() != OK) result = 27;
    if (!result && log_get_stats(&log_stats) != OK) result = 28;
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
