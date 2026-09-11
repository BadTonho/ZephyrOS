#ifndef UPDATE_TRUST_H
#define UPDATE_TRUST_H

#include "types.h"

/* Gerado por tools/updater.py; contem apenas material publico. */
#define UPDATE_TRUST_PUBLIC_KEY_HEX "3dd0697e7a167218300dfe96f100d562caa760b804818d2dabf09c10b1daeecd"
#define UPDATE_TRUST_KEY_ID_HEX "dfecfa35485fde8ad61707947e5ae85b"

#define UPDATE_TRUST_VALID_FROM_EPOCH 0U
#define UPDATE_TRUST_VALID_UNTIL_EPOCH 4294967294U
#define UPDATE_TRUST_REVOKED_KEY_COUNT 1U

static const uint8_t UPDATE_TRUST_PUBLIC_KEY[32] = {
    0x3DU, 0xD0U, 0x69U, 0x7EU, 0x7AU, 0x16U, 0x72U, 0x18U, 0x30U, 0x0DU, 0xFEU, 0x96U, 0xF1U, 0x00U, 0xD5U, 0x62U, 0xCAU, 0xA7U, 0x60U, 0xB8U, 0x04U, 0x81U, 0x8DU, 0x2DU, 0xABU, 0xF0U, 0x9CU, 0x10U, 0xB1U, 0xDAU, 0xEEU, 0xCDU
};

static const uint8_t UPDATE_TRUST_KEY_ID[16] = {
    0xDFU, 0xECU, 0xFAU, 0x35U, 0x48U, 0x5FU, 0xDEU, 0x8AU, 0xD6U, 0x17U, 0x07U, 0x94U, 0x7EU, 0x5AU, 0xE8U, 0x5BU
};

static const uint8_t UPDATE_TRUST_REVOKED_KEY_IDS[1U][16] = {
    {0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U},
};

static inline uint8_t update_trust_key_allowed(
    const uint8_t* key_id, uint32_t target_epoch) {
    uint8_t different;
    if (!key_id || target_epoch > UPDATE_TRUST_VALID_UNTIL_EPOCH) return 0U;
#if UPDATE_TRUST_VALID_FROM_EPOCH != 0U
    if (target_epoch < UPDATE_TRUST_VALID_FROM_EPOCH) return 0U;
#endif
    different = 0U;
    for (uint32_t index = 0U; index < 16U; index++) {
        different |= (uint8_t)(key_id[index] ^ UPDATE_TRUST_KEY_ID[index]);
    }
    if (different) return 0U;
    for (uint32_t key = 0U; key < UPDATE_TRUST_REVOKED_KEY_COUNT; key++) {
        different = 0U;
        for (uint32_t index = 0U; index < 16U; index++) {
            different |= (uint8_t)(key_id[index] ^
                UPDATE_TRUST_REVOKED_KEY_IDS[key][index]);
        }
        if (!different) return 0U;
    }
    return 1U;
}

#endif
