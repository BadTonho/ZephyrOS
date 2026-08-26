#ifndef RECOVERY_CHAIN_H
#define RECOVERY_CHAIN_H

#include "types.h"

#define RECOVERY_CHAIN_HANDOFF_ADDRESS 0x00002B00U
#define RECOVERY_CHAIN_HANDOFF_SIZE 64U
#define RECOVERY_CHAIN_HANDOFF_VERSION 1U
#define RECOVERY_CHAIN_BOOT_ADDRESS 0x00007C00U
#define RECOVERY_CHAIN_BOOT_SIZE 512U
#define RECOVERY_CHAIN_STAGE2_ADDRESS 0x00005000U
#define RECOVERY_CHAIN_STAGE2_LIMIT 0x00005F00U
#define RECOVERY_CHAIN_KERNEL_ADDRESS 0x00100000U
#define RECOVERY_CHAIN_KERNEL_LIMIT 0x00800000U

typedef struct {
    uint8_t magic[4];
    uint16_t version;
    uint16_t size;
    uint32_t boot_abi;
    uint32_t boot_address;
    uint32_t boot_size;
    uint32_t stage2_address;
    uint32_t stage2_size;
    uint32_t kernel_address;
    uint32_t kernel_size;
    uint32_t mmap_address;
    uint32_t vesa_address;
    uint32_t boot_handoff_address;
    uint32_t checksum;
    uint32_t saved_esp;
    uint8_t reserved[8];
} recovery_chain_handoff_t;

#endif
