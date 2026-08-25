#include "types.h"
#include "core/crypto.h"
#include "core/update_system.h"
#include "core/update_system_slots.h"
#include "core/update_trust.h"
#include "recovery_layout.h"

#define RECOVERY_ATA_DATA 0x1F0U
#define RECOVERY_ATA_SECTORS 0x1F2U
#define RECOVERY_ATA_LBA0 0x1F3U
#define RECOVERY_ATA_LBA1 0x1F4U
#define RECOVERY_ATA_LBA2 0x1F5U
#define RECOVERY_ATA_DRIVE 0x1F6U
#define RECOVERY_ATA_STATUS 0x1F7U
#define RECOVERY_ATA_COMMAND 0x1F7U
#define RECOVERY_ATA_READ 0x20U
#define RECOVERY_ATA_WRITE 0x30U
#define RECOVERY_ATA_FLUSH 0xE7U
#define RECOVERY_ATA_TIMEOUT 1000000U
#define RECOVERY_SECTOR_SIZE 512U
#define RECOVERY_MBR_PARTITIONS 4U
#define RECOVERY_MBR_PARTITION_OFFSET 446U
#define RECOVERY_FAT32_TYPE_LBA 0x0CU
#define RECOVERY_FAT32_TYPE_CHS 0x0BU
#define RECOVERY_FAT32_EOC 0x0FFFFFF8U
#define RECOVERY_FAT32_MAX_HOPS 32768U
#define RECOVERY_FILE_NAME_SIZE 11U
#define RECOVERY_STATE_SLOT_OFFSET 32U
#define RECOVERY_STATE_SLOT_SIZE 176U
#define RECOVERY_ZSYS_IMAGE_SIZE_OFFSET 24U
#define RECOVERY_ZSYS_VERSION_OFFSET 4U
#define RECOVERY_ZSYS_HEADER_SIZE_OFFSET 6U
#define RECOVERY_ZSYS_ARCH_OFFSET 8U
#define RECOVERY_ZSYS_FLAGS_OFFSET 10U
#define RECOVERY_ZSYS_TOTAL_SIZE_OFFSET 12U
#define RECOVERY_ZSYS_PAYLOAD_OFFSET 16U
#define RECOVERY_ZSYS_PAYLOAD_SIZE_OFFSET 20U
#define RECOVERY_ZSYS_IMAGE_HASH_OFFSET 28U
#define RECOVERY_ZSYS_COMPONENTS_OFFSET 346U
#define RECOVERY_ZSYS_COMPONENT_SIZE 64U
#define RECOVERY_ZSYS_SIGNATURE_OFFSET 812U
#define RECOVERY_ZSYS_SIGNATURE_SIZE 64U
#define RECOVERY_ZSYS_KEY_ID_OFFSET 796U
#define RECOVERY_ZSYS_SIGNATURE_SIZE_OFFSET 876U
#define RECOVERY_ZSYS_SIGNATURE_FIELD_OFFSET 880U
#define RECOVERY_ZSYS_RESERVED_OFFSET 884U
#define RECOVERY_ZSYS_COMPONENT_BOOT 1U
#define RECOVERY_ZSYS_COMPONENT_STAGE2 2U
#define RECOVERY_ZSYS_COMPONENT_KERNEL 3U
#define RECOVERY_KERNEL_OFFSET 0x00100000U
#define RECOVERY_KERNEL_LIMIT 0x00800000U
#define RECOVERY_VGA_TEXT ((volatile uint16_t*)0xB8000U)
#define RECOVERY_VGA_WIDTH 80U
#define RECOVERY_VGA_ATTRIBUTE 0x0F00U

typedef struct {
    uint32_t partition_lba;
    uint32_t fat_lba;
    uint32_t data_lba;
    uint32_t root_cluster;
    uint32_t sectors_per_cluster;
    uint32_t sectors_per_fat;
} recovery_fat32_t;

typedef struct {
    uint32_t cluster;
    uint32_t size;
} recovery_file_t;

typedef struct {
    uint32_t sequence;
    uint8_t active;
    uint8_t pending;
    uint8_t valid;
    recovery_file_t file;
    uint8_t raw[UPDATE_SYSTEM_SLOT_CONTROL_SIZE];
} recovery_state_t;

static uint8_t recovery_sector[RECOVERY_SECTOR_SIZE];
static uint8_t recovery_header[UPDATE_SYSTEM_HEADER_SIZE];
static uint8_t recovery_signed_header[UPDATE_SYSTEM_HEADER_SIZE];
static uint32_t recovery_vga_cursor;
static const uint8_t* recovery_vesa;

static const uint8_t recovery_font[26][5] = {
    {0x1EU,0x05U,0x05U,0x1EU,0x00U},{0x1FU,0x15U,0x15U,0x0AU,0x00U},
    {0x0EU,0x11U,0x11U,0x0AU,0x00U},{0x1FU,0x11U,0x11U,0x0EU,0x00U},
    {0x1FU,0x15U,0x15U,0x11U,0x00U},{0x1FU,0x05U,0x05U,0x01U,0x00U},
    {0x0EU,0x11U,0x15U,0x1DU,0x00U},{0x1FU,0x04U,0x04U,0x1FU,0x00U},
    {0x11U,0x1FU,0x11U,0x00U,0x00U},{0x08U,0x10U,0x10U,0x0FU,0x00U},
    {0x1FU,0x04U,0x0AU,0x11U,0x00U},{0x1FU,0x10U,0x10U,0x10U,0x00U},
    {0x1FU,0x02U,0x04U,0x02U,0x1FU},{0x1FU,0x02U,0x04U,0x1FU,0x00U},
    {0x0EU,0x11U,0x11U,0x0EU,0x00U},{0x1FU,0x05U,0x05U,0x02U,0x00U},
    {0x0EU,0x11U,0x19U,0x1EU,0x00U},{0x1FU,0x05U,0x0DU,0x12U,0x00U},
    {0x12U,0x15U,0x15U,0x09U,0x00U},{0x01U,0x1FU,0x01U,0x00U,0x00U},
    {0x0FU,0x10U,0x10U,0x0FU,0x00U},{0x07U,0x08U,0x10U,0x08U,0x07U},
    {0x1FU,0x08U,0x04U,0x08U,0x1FU},{0x1BU,0x04U,0x04U,0x1BU,0x00U},
    {0x03U,0x04U,0x18U,0x04U,0x03U},{0x19U,0x15U,0x13U,0x00U,0x00U},
};

static inline void recovery_out8(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %w1" : : "a"(value), "d"(port));
}

static inline uint8_t recovery_in8(uint16_t port) {
    uint8_t value;
    asm volatile("inb %w1, %0" : "=a"(value) : "d"(port));
    return value;
}

static inline uint16_t recovery_in16(uint16_t port) {
    uint16_t value;
    asm volatile("inw %w1, %0" : "=a"(value) : "d"(port));
    return value;
}

static inline void recovery_out16(uint16_t port, uint16_t value) {
    asm volatile("outw %0, %w1" : : "a"(value), "d"(port));
}

static uint16_t recovery_u16(const uint8_t* value) {
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8U);
}

static uint32_t recovery_u32(const uint8_t* value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8U) |
           ((uint32_t)value[2] << 16U) | ((uint32_t)value[3] << 24U);
}

static int recovery_equal(const uint8_t* first, const uint8_t* second,
                          uint32_t size) {
    uint8_t difference = 0U;
    for (uint32_t index = 0U; index < size; index++) difference |= first[index] ^ second[index];
    return difference == 0U;
}

static int recovery_zero(const uint8_t* value, uint32_t size) {
    uint8_t combined = 0U;
    if (!value) return 0;
    for (uint32_t index = 0U; index < size; index++) combined |= value[index];
    return combined == 0U;
}

static int recovery_wait(uint8_t require_drq) {
    for (uint32_t attempt = 0U; attempt < RECOVERY_ATA_TIMEOUT; attempt++) {
        uint8_t status = recovery_in8(RECOVERY_ATA_STATUS);
        if (status & 0x01U || status & 0x20U) return 0;
        if (!(status & 0x80U) && (!require_drq || (status & 0x08U))) return 1;
    }
    return 0;
}

static int recovery_read_sector(uint32_t lba, void* output) {
    uint16_t* words = (uint16_t*)output;
    if (!output || lba & 0xF0000000U || !recovery_wait(0U)) return 0;
    recovery_out8(RECOVERY_ATA_DRIVE, (uint8_t)(0xE0U | ((lba >> 24U) & 0x0FU)));
    recovery_out8(RECOVERY_ATA_SECTORS, 1U);
    recovery_out8(RECOVERY_ATA_LBA0, (uint8_t)lba);
    recovery_out8(RECOVERY_ATA_LBA1, (uint8_t)(lba >> 8U));
    recovery_out8(RECOVERY_ATA_LBA2, (uint8_t)(lba >> 16U));
    recovery_out8(RECOVERY_ATA_COMMAND, RECOVERY_ATA_READ);
    if (!recovery_wait(1U)) return 0;
    for (uint32_t index = 0U; index < 256U; index++) words[index] = recovery_in16(RECOVERY_ATA_DATA);
    return 1;
}

static int recovery_write_sector(uint32_t lba, const void* input) {
    const uint16_t* words = (const uint16_t*)input;
    if (!input || lba & 0xF0000000U || !recovery_wait(0U)) return 0;
    recovery_out8(RECOVERY_ATA_DRIVE, (uint8_t)(0xE0U | ((lba >> 24U) & 0x0FU)));
    recovery_out8(RECOVERY_ATA_SECTORS, 1U);
    recovery_out8(RECOVERY_ATA_LBA0, (uint8_t)lba);
    recovery_out8(RECOVERY_ATA_LBA1, (uint8_t)(lba >> 8U));
    recovery_out8(RECOVERY_ATA_LBA2, (uint8_t)(lba >> 16U));
    recovery_out8(RECOVERY_ATA_COMMAND, RECOVERY_ATA_WRITE);
    if (!recovery_wait(1U)) return 0;
    for (uint32_t index = 0U; index < 256U; index++)
        recovery_out16(RECOVERY_ATA_DATA, words[index]);
    if (!recovery_wait(0U)) return 0;
    recovery_out8(RECOVERY_ATA_COMMAND, RECOVERY_ATA_FLUSH);
    return recovery_wait(0U);
}

static void recovery_message(const char* message) {
    uint32_t framebuffer = recovery_vesa ? recovery_u32(recovery_vesa) : 0U;
    uint16_t width = recovery_vesa ? recovery_u16(recovery_vesa + 4U) : 0U;
    uint16_t height = recovery_vesa ? recovery_u16(recovery_vesa + 6U) : 0U;
    uint16_t pitch = recovery_vesa ? recovery_u16(recovery_vesa + 8U) : 0U;
    if (!message) return;
    while (*message && recovery_vga_cursor < RECOVERY_VGA_WIDTH * 25U) {
        if (*message == '\n') {
            recovery_vga_cursor = ((recovery_vga_cursor / RECOVERY_VGA_WIDTH) + 1U) * RECOVERY_VGA_WIDTH;
        } else {
            uint32_t x = (recovery_vga_cursor % RECOVERY_VGA_WIDTH) * 12U;
            uint32_t y = (recovery_vga_cursor / RECOVERY_VGA_WIDTH) * 16U;
            if (framebuffer && recovery_vesa[10U] == 32U && recovery_vesa[11U] &&
                x + 10U <= width && y + 14U <= height) {
                const uint8_t* glyph = *message >= 'A' && *message <= 'Z' ?
                    recovery_font[*message - 'A'] : 0;
                if (glyph) for (uint32_t row = 0U; row < 5U; row++) for (uint32_t column = 0U; column < 5U; column++) {
                    if (glyph[row] & (1U << (4U - column))) {
                        uint32_t* pixel = (uint32_t*)(framebuffer + (y + row * 2U) * pitch + (x + column * 2U) * 4U);
                        pixel[0] = 0x00FFFFFFU; pixel[1] = 0x00FFFFFFU;
                        pixel[pitch / 4U] = 0x00FFFFFFU; pixel[pitch / 4U + 1U] = 0x00FFFFFFU;
                    }
                }
            } else RECOVERY_VGA_TEXT[recovery_vga_cursor] = RECOVERY_VGA_ATTRIBUTE | (uint8_t)*message;
            recovery_vga_cursor++;
        }
        message++;
    }
}

static int recovery_fat32_open(recovery_fat32_t* fs) {
    if (!fs || !recovery_read_sector(0U, recovery_sector)) return 0;
    for (uint32_t index = 0U; index < RECOVERY_MBR_PARTITIONS; index++) {
        uint8_t* partition = recovery_sector + RECOVERY_MBR_PARTITION_OFFSET + index * 16U;
        if (partition[4] != RECOVERY_FAT32_TYPE_LBA && partition[4] != RECOVERY_FAT32_TYPE_CHS) continue;
        fs->partition_lba = recovery_u32(partition + 8U);
        if (!recovery_read_sector(fs->partition_lba, recovery_sector) ||
            recovery_u16(recovery_sector + 11U) != RECOVERY_SECTOR_SIZE) return 0;
        fs->sectors_per_cluster = recovery_sector[13U];
        fs->sectors_per_fat = recovery_u32(recovery_sector + 36U);
        fs->root_cluster = recovery_u32(recovery_sector + 44U);
        if (!fs->sectors_per_cluster || !fs->sectors_per_fat || fs->root_cluster < 2U) return 0;
        fs->fat_lba = fs->partition_lba + recovery_u16(recovery_sector + 14U);
        fs->data_lba = fs->fat_lba + recovery_sector[16U] * fs->sectors_per_fat;
        return 1;
    }
    return 0;
}

static uint32_t recovery_next_cluster(const recovery_fat32_t* fs, uint32_t cluster) {
    uint32_t offset = cluster * 4U;
    if (!recovery_read_sector(fs->fat_lba + offset / RECOVERY_SECTOR_SIZE, recovery_sector)) return 0U;
    return recovery_u32(recovery_sector + offset % RECOVERY_SECTOR_SIZE) & 0x0FFFFFFFU;
}

static int recovery_find_file(const recovery_fat32_t* fs, const char name[RECOVERY_FILE_NAME_SIZE], recovery_file_t* file) {
    uint32_t cluster = fs->root_cluster;
    uint32_t hops = 0U;
    while (cluster >= 2U && cluster < RECOVERY_FAT32_EOC && hops++ < RECOVERY_FAT32_MAX_HOPS) {
        for (uint32_t sector = 0U; sector < fs->sectors_per_cluster; sector++) {
            if (!recovery_read_sector(fs->data_lba + (cluster - 2U) * fs->sectors_per_cluster + sector, recovery_sector)) return 0;
            for (uint32_t entry = 0U; entry < 16U; entry++) {
                uint8_t* raw = recovery_sector + entry * 32U;
                if (!raw[0]) return 0;
                if (raw[0] == 0xE5U || raw[11] == 0x0FU || (raw[11] & 0x08U)) continue;
                if (!recovery_equal(raw, (const uint8_t*)name, RECOVERY_FILE_NAME_SIZE)) continue;
                file->cluster = ((uint32_t)recovery_u16(raw + 20U) << 16U) | recovery_u16(raw + 26U);
                file->size = recovery_u32(raw + 28U);
                return file->cluster >= 2U && file->size > 0U;
            }
        }
        cluster = recovery_next_cluster(fs, cluster);
    }
    return 0;
}

static int recovery_read_file(const recovery_fat32_t* fs, const recovery_file_t* file,
                              uint32_t offset, void* output, uint32_t size) {
    uint32_t cluster_bytes = fs->sectors_per_cluster * RECOVERY_SECTOR_SIZE;
    uint32_t cluster = file->cluster;
    uint32_t hops = 0U;
    uint8_t* destination = (uint8_t*)output;
    if (!output || offset > file->size || size > file->size - offset) return 0;
    while (offset >= cluster_bytes) {
        if (hops++ >= RECOVERY_FAT32_MAX_HOPS) return 0;
        cluster = recovery_next_cluster(fs, cluster);
        if (cluster < 2U || cluster >= RECOVERY_FAT32_EOC) return 0;
        offset -= cluster_bytes;
    }
    while (size) {
        uint32_t sector_index = offset / RECOVERY_SECTOR_SIZE;
        uint32_t sector_offset = offset % RECOVERY_SECTOR_SIZE;
        uint32_t amount = RECOVERY_SECTOR_SIZE - sector_offset;
        if (amount > size) amount = size;
        if (!recovery_read_sector(fs->data_lba + (cluster - 2U) * fs->sectors_per_cluster + sector_index, recovery_sector)) return 0;
        for (uint32_t index = 0U; index < amount; index++) destination[index] = recovery_sector[sector_offset + index];
        destination += amount; size -= amount; offset += amount;
        if (offset == cluster_bytes && size) {
            if (hops++ >= RECOVERY_FAT32_MAX_HOPS) return 0;
            cluster = recovery_next_cluster(fs, cluster);
            offset = 0U;
            if (cluster < 2U || cluster >= RECOVERY_FAT32_EOC) return 0;
        }
    }
    return 1;
}

static int recovery_load_state(const recovery_fat32_t* fs, const char name[RECOVERY_FILE_NAME_SIZE], recovery_state_t* state) {
    recovery_file_t file;
    uint8_t hash[CRYPTO_SHA256_SIZE];
    uint16_t version;
    if (!recovery_find_file(fs, name, &file) || file.size != UPDATE_SYSTEM_SLOT_CONTROL_SIZE ||
        !recovery_read_file(fs, &file, 0U, state->raw, sizeof(state->raw))) return 0;
    version = recovery_u16(state->raw + 4U);
    if (!recovery_equal(state->raw, (const uint8_t*)"ZSI1", 4U) ||
        (version != 1U && version != 2U) || recovery_u16(state->raw + 6U) != 512U ||
        crypto_sha256(state->raw, 480U, hash) != 0 || !recovery_equal(hash, state->raw + 480U, 32U)) return 0;
    state->sequence = recovery_u32(state->raw + 8U);
    state->active = state->raw[12U];
    state->pending = state->raw[13U];
    state->file = file;
    state->valid = state->sequence != 0U && state->active < 2U &&
        (state->pending == UPDATE_SYSTEM_SLOT_NONE || state->pending < 2U) &&
        state->pending != state->active;
    if (state->valid && version == 1U) {
        state->valid = recovery_zero(state->raw + 15U, 17U);
    }
    if (state->valid && version == 2U) {
        uint8_t boot_state = state->raw[17U];
        uint8_t attempt = state->raw[16U];
        state->valid = (state->raw[14U] & ~1U) == 0U && state->raw[15U] < 2U &&
            (attempt == UPDATE_SYSTEM_SLOT_NONE || attempt < 2U) &&
            boot_state <= UPDATE_SYSTEM_SLOTS_BOOT_FAILED &&
            recovery_u16(state->raw + 18U) <= UPDATE_SYSTEM_SLOTS_REASON_UNSUPPORTED &&
            recovery_zero(state->raw + 24U, 8U) &&
            ((boot_state == UPDATE_SYSTEM_SLOTS_BOOT_NONE &&
              attempt == UPDATE_SYSTEM_SLOT_NONE && recovery_u32(state->raw + 20U) == 0U) ||
             (boot_state != UPDATE_SYSTEM_SLOTS_BOOT_NONE && attempt < 2U &&
              state->raw[15U] < 2U && recovery_u32(state->raw + 20U) != 0U));
    }
    return state->valid;
}

static int recovery_locate_control(const recovery_fat32_t* fs,
                                   const char name[RECOVERY_FILE_NAME_SIZE],
                                   recovery_state_t* state) {
    if (!fs || !name || !state || !recovery_find_file(fs, name, &state->file) ||
        state->file.size != UPDATE_SYSTEM_SLOT_CONTROL_SIZE) return 0;
    return 1;
}

static void recovery_state_write_u16(uint8_t* output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
}

static void recovery_state_write_u32(uint8_t* output, uint32_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

static int recovery_publish_attempt(const recovery_fat32_t* fs,
                                    const recovery_state_t* selected,
                                    const recovery_state_t* target,
                                    const char target_name[RECOVERY_FILE_NAME_SIZE],
                                    uint8_t slot, uint32_t* state_sequence_out,
                                    uint32_t* attempt_sequence_out) {
    uint8_t raw[UPDATE_SYSTEM_SLOT_CONTROL_SIZE];
    uint8_t hash[CRYPTO_SHA256_SIZE];
    recovery_state_t verified;
    uint32_t cluster_lba;

    uint32_t attempt_sequence;
    if (!selected || !target || !target_name || !state_sequence_out ||
        !attempt_sequence_out || slot >= 2U ||
        selected->sequence == 0xFFFFFFFFU || target->file.cluster < 2U) return 0;
    for (uint32_t index = 0U; index < sizeof(raw); index++) raw[index] = selected->raw[index];
    recovery_state_write_u16(raw + 4U, 2U);
    recovery_state_write_u32(raw + 8U, selected->sequence + 1U);
    raw[15U] = selected->active;
    raw[16U] = slot;
    raw[17U] = UPDATE_SYSTEM_SLOTS_BOOT_ATTEMPTED;
    recovery_state_write_u16(raw + 18U, UPDATE_SYSTEM_SLOTS_REASON_NONE);
    attempt_sequence = recovery_u32(selected->raw + 20U);
    if (attempt_sequence == 0xFFFFFFFFU) return 0;
    recovery_state_write_u32(raw + 20U, attempt_sequence + 1U);
    if (crypto_sha256(raw, 480U, hash) != 0) return 0;
    for (uint32_t index = 0U; index < sizeof(hash); index++) raw[480U + index] = hash[index];
    cluster_lba = fs->data_lba + (target->file.cluster - 2U) * fs->sectors_per_cluster;
    if (!recovery_write_sector(cluster_lba, raw) ||
        !recovery_load_state(fs, target_name, &verified) ||
        verified.sequence != recovery_u32(raw + 8U)) return 0;
    *state_sequence_out = verified.sequence;
    *attempt_sequence_out = recovery_u32(raw + 20U);
    return 1;
}

static int recovery_mark_attempt_failed(const recovery_fat32_t* fs,
                                        const recovery_state_t* selected,
                                        const recovery_state_t* target,
                                        const char target_name[RECOVERY_FILE_NAME_SIZE]) {
    uint8_t raw[UPDATE_SYSTEM_SLOT_CONTROL_SIZE];
    uint8_t hash[CRYPTO_SHA256_SIZE];
    recovery_state_t verified;
    uint32_t cluster_lba;

    if (!selected || !target || !target_name || selected->sequence == 0xFFFFFFFFU ||
        target->file.cluster < 2U) return 0;
    for (uint32_t index = 0U; index < sizeof(raw); index++) raw[index] = selected->raw[index];
    recovery_state_write_u16(raw + 4U, 2U);
    recovery_state_write_u32(raw + 8U, selected->sequence + 1U);
    if (raw[15U] >= 2U) raw[15U] = raw[12U];
    if (raw[16U] >= 2U) raw[16U] = raw[13U];
    if (raw[16U] >= 2U) return 0;
    if (recovery_u32(raw + 20U) == 0U) recovery_state_write_u32(raw + 20U, 1U);
    raw[13U] = UPDATE_SYSTEM_SLOT_NONE;
    raw[17U] = UPDATE_SYSTEM_SLOTS_BOOT_FAILED;
    recovery_state_write_u16(raw + 18U, UPDATE_SYSTEM_SLOTS_REASON_BOOT_FAILED);
    if (crypto_sha256(raw, 480U, hash) != 0) return 0;
    for (uint32_t index = 0U; index < sizeof(hash); index++) raw[480U + index] = hash[index];
    cluster_lba = fs->data_lba + (target->file.cluster - 2U) * fs->sectors_per_cluster;
    return recovery_write_sector(cluster_lba, raw) &&
           recovery_load_state(fs, target_name, &verified) &&
           verified.sequence == recovery_u32(raw + 8U) &&
           verified.raw[17U] == UPDATE_SYSTEM_SLOTS_BOOT_FAILED;
}

static int recovery_hash_file(const recovery_fat32_t* fs, const recovery_file_t* file,
                              uint8_t output[CRYPTO_SHA256_SIZE]) {
    crypto_sha256_ctx_t hash;
    uint32_t offset = 0U;
    if (crypto_sha256_init(&hash) != 0) return 0;
    while (offset < file->size) {
        uint32_t amount = file->size - offset;
        if (amount > sizeof(recovery_sector)) amount = sizeof(recovery_sector);
        if (!recovery_read_file(fs, file, offset, recovery_sector, amount) ||
            crypto_sha256_update(&hash, recovery_sector, amount) != 0) return 0;
        offset += amount;
    }
    return crypto_sha256_final(&hash, output) == 0;
}

static int recovery_hash_range(const recovery_fat32_t* fs, const recovery_file_t* file,
                               uint32_t offset, uint32_t size,
                               uint8_t output[CRYPTO_SHA256_SIZE]) {
    crypto_sha256_ctx_t hash;
    uint32_t consumed = 0U;
    if (crypto_sha256_init(&hash) != 0) return 0;
    while (consumed < size) {
        uint32_t amount = size - consumed;
        if (amount > sizeof(recovery_sector)) amount = sizeof(recovery_sector);
        if (!recovery_read_file(fs, file, offset + consumed, recovery_sector, amount) ||
            crypto_sha256_update(&hash, recovery_sector, amount) != 0) return 0;
        consumed += amount;
    }
    return crypto_sha256_final(&hash, output) == 0;
}

static int recovery_verify_package(const recovery_fat32_t* fs, const recovery_file_t* file,
                                   const uint8_t expected_hash[32], uint32_t* kernel_offset,
                                   uint32_t* kernel_size) {
    crypto_ed25519_verify_ctx_t signature;
    crypto_sha256_ctx_t image_hash;
    uint8_t actual_hash[32];
    uint8_t actual_image_hash[32];
    uint32_t image_size;
    uint32_t offset = 0U;
    uint32_t previous_end = 0U;
    uint8_t found_kernel = 0U;
    static const uint8_t domain[] = "ZEPHYROS-SYSTEM-IMAGE-V1\0";
    if (!recovery_read_file(fs, file, 0U, recovery_header, sizeof(recovery_header)) ||
        !recovery_equal(recovery_header, (const uint8_t*)"ZSYS", 4U) ||
        recovery_u16(recovery_header + RECOVERY_ZSYS_VERSION_OFFSET) != 1U ||
        recovery_u16(recovery_header + RECOVERY_ZSYS_HEADER_SIZE_OFFSET) != UPDATE_SYSTEM_HEADER_SIZE ||
        recovery_u16(recovery_header + RECOVERY_ZSYS_ARCH_OFFSET) != 1U ||
        (recovery_u16(recovery_header + RECOVERY_ZSYS_FLAGS_OFFSET) & ~3U) != 0U ||
        recovery_u32(recovery_header + RECOVERY_ZSYS_PAYLOAD_OFFSET) != UPDATE_SYSTEM_HEADER_SIZE ||
        recovery_u32(recovery_header + RECOVERY_ZSYS_PAYLOAD_SIZE_OFFSET) != recovery_u32(recovery_header + RECOVERY_ZSYS_IMAGE_SIZE_OFFSET) ||
        recovery_u32(recovery_header + RECOVERY_ZSYS_TOTAL_SIZE_OFFSET) != file->size ||
        file->size != UPDATE_SYSTEM_HEADER_SIZE + recovery_u32(recovery_header + RECOVERY_ZSYS_IMAGE_SIZE_OFFSET) ||
        recovery_u32(recovery_header + RECOVERY_ZSYS_SIGNATURE_SIZE_OFFSET) != RECOVERY_ZSYS_SIGNATURE_SIZE ||
        recovery_u32(recovery_header + RECOVERY_ZSYS_SIGNATURE_FIELD_OFFSET) != RECOVERY_ZSYS_SIGNATURE_OFFSET ||
        !recovery_zero(recovery_header + RECOVERY_ZSYS_RESERVED_OFFSET,
                       UPDATE_SYSTEM_HEADER_SIZE - RECOVERY_ZSYS_RESERVED_OFFSET) ||
        !recovery_equal(recovery_header + RECOVERY_ZSYS_KEY_ID_OFFSET, UPDATE_TRUST_KEY_ID, 16U)) return 0;
    image_size = recovery_u32(recovery_header + RECOVERY_ZSYS_IMAGE_SIZE_OFFSET);
    if (!image_size || image_size > UPDATE_SYSTEM_MAX_IMAGE_SIZE ||
        !recovery_hash_file(fs, file, actual_hash) || !recovery_equal(actual_hash, expected_hash, 32U)) return 0;
    for (uint32_t index = 0U; index < sizeof(recovery_header); index++) recovery_signed_header[index] = recovery_header[index];
    for (uint32_t index = 0U; index < RECOVERY_ZSYS_SIGNATURE_SIZE; index++) recovery_signed_header[RECOVERY_ZSYS_SIGNATURE_OFFSET + index] = 0U;
    if (crypto_ed25519_verify_init(&signature, recovery_header + RECOVERY_ZSYS_SIGNATURE_OFFSET, UPDATE_TRUST_PUBLIC_KEY) != 0 ||
        crypto_ed25519_verify_update(&signature, domain, sizeof(domain) - 1U) != 0 ||
        crypto_ed25519_verify_update(&signature, recovery_signed_header, sizeof(recovery_signed_header)) != 0 ||
        crypto_sha256_init(&image_hash) != 0) return 0;
    while (offset < image_size) {
        uint32_t amount = image_size - offset;
        if (amount > sizeof(recovery_sector)) amount = sizeof(recovery_sector);
        if (!recovery_read_file(fs, file, UPDATE_SYSTEM_HEADER_SIZE + offset, recovery_sector, amount) ||
            crypto_ed25519_verify_update(&signature, recovery_sector, amount) != 0 ||
            crypto_sha256_update(&image_hash, recovery_sector, amount) != 0) return 0;
        offset += amount;
    }
    if (crypto_ed25519_verify_final(&signature) != 0 || crypto_sha256_final(&image_hash, actual_image_hash) != 0 ||
        !recovery_equal(actual_image_hash, recovery_header + RECOVERY_ZSYS_IMAGE_HASH_OFFSET, 32U)) return 0;
    if (recovery_u16(recovery_header + 342U) != 3U ||
        recovery_u16(recovery_header + 344U) != RECOVERY_ZSYS_COMPONENT_SIZE) return 0;
    for (uint32_t component = 0U; component < 3U; component++) {
        uint8_t* entry = recovery_header + RECOVERY_ZSYS_COMPONENTS_OFFSET + component * RECOVERY_ZSYS_COMPONENT_SIZE;
        uint8_t component_hash[CRYPTO_SHA256_SIZE];
        uint16_t kind = recovery_u16(entry);
        uint32_t component_offset = recovery_u32(entry + 4U);
        uint32_t component_size = recovery_u32(entry + 8U);
        if (kind != component + RECOVERY_ZSYS_COMPONENT_BOOT ||
            kind < RECOVERY_ZSYS_COMPONENT_BOOT || kind > RECOVERY_ZSYS_COMPONENT_KERNEL ||
            recovery_u16(entry + 2U) != 0U || !component_size ||
            component_offset > image_size || component_size > image_size - component_offset ||
            component_offset != previous_end || !recovery_zero(entry + 44U,
                RECOVERY_ZSYS_COMPONENT_SIZE - 44U)) return 0;
        if ((component == 0U && (component_offset != 0U || component_size != 512U)) ||
            (component == 1U && (component_offset != 512U ||
                                 (component_size % RECOVERY_SECTOR_SIZE) != 0U))) return 0;
        if (!recovery_hash_range(fs, file, UPDATE_SYSTEM_HEADER_SIZE + component_offset,
                                 component_size, component_hash) ||
            !recovery_equal(component_hash, entry + 12U, CRYPTO_SHA256_SIZE)) return 0;
        if (kind == RECOVERY_ZSYS_COMPONENT_KERNEL) { *kernel_offset = component_offset; *kernel_size = component_size; found_kernel = 1U; }
        previous_end = component_offset + component_size;
    }
    return found_kernel && previous_end == image_size &&
           *kernel_size <= RECOVERY_KERNEL_LIMIT - RECOVERY_KERNEL_OFFSET;
}

static void recovery_boot_kernel(uint32_t mmap, uint32_t vesa) {
    uint32_t kernel = RECOVERY_KERNEL_OFFSET;
    /* A entrada legada recebe estes ponteiros em ESI/EDI antes de montar a
     * propria pilha C; preservar essa ABI evita tocar no kernel de fallback. */
    asm volatile(
        "movl %0, %%esi\n\t"
        "movl %1, %%edi\n\t"
        "call *%2"
        :
        : "r"(mmap), "r"(vesa), "r"(kernel)
        : "esi", "edi", "memory");
}

static int recovery_boot_legacy(uint32_t mmap, uint32_t vesa, uint32_t lba,
                                uint32_t sectors, uint32_t bytes) {
    uint8_t* destination = (uint8_t*)RECOVERY_KERNEL_OFFSET;
    crypto_sha256_ctx_t hash;
    uint8_t actual_hash[CRYPTO_SHA256_SIZE];
    if (!lba || !sectors || bytes != RECOVERY_LEGACY_KERNEL_SIZE ||
        sectors != (bytes + RECOVERY_SECTOR_SIZE - 1U) / RECOVERY_SECTOR_SIZE ||
        crypto_sha256_init(&hash) != 0) {
        recovery_message("LEGACY METADATA FAIL\n");
        return 0;
    }
    for (uint32_t index = 0U; index < sectors; index++) {
        uint8_t* sector = destination + index * RECOVERY_SECTOR_SIZE;
        uint32_t amount = bytes - index * RECOVERY_SECTOR_SIZE;
        if (amount > RECOVERY_SECTOR_SIZE) amount = RECOVERY_SECTOR_SIZE;
        if (!recovery_read_sector(lba + index, sector) ||
            crypto_sha256_update(&hash, sector, amount) != 0) {
            recovery_message("LEGACY ATA READ FAIL\n");
            return 0;
        }
    }
    if (crypto_sha256_final(&hash, actual_hash) != 0 ||
        !recovery_equal(actual_hash, recovery_legacy_kernel_sha256, CRYPTO_SHA256_SIZE)) {
        recovery_message("LEGACY SHA FAIL\n");
        return 0;
    }
    recovery_message("LEGACY KERNEL START\n");
    recovery_boot_kernel(mmap, vesa);
    recovery_message("LEGACY KERNEL RETURN\n");
    return 0;
}

void recovery_loader_main(uint32_t mmap, uint32_t vesa, uint32_t legacy_lba,
                          uint32_t legacy_sectors, uint32_t legacy_bytes) {
    recovery_fat32_t fs;
    recovery_file_t slot;
    recovery_state_t first;
    recovery_state_t second;
    recovery_state_t* selected;
    recovery_state_t* alternate;
    update_system_boot_handoff_t* handoff =
        (update_system_boot_handoff_t*)UPDATE_SYSTEM_BOOT_HANDOFF_ADDRESS;
    uint8_t slot_index;
    int first_valid;
    int second_valid;
    uint32_t kernel_offset = 0U;
    uint32_t kernel_size = 0U;
    static const char state_a[] = "ZSI0    STA";
    static const char state_b[] = "ZSI1    STA";
    static const char slots[2][12] = { "ZSA0    ZSY", "ZSB0    ZSY" };

    recovery_vesa = (const uint8_t*)vesa;

    for (uint32_t index = 0U; index < UPDATE_SYSTEM_BOOT_HANDOFF_SIZE; index++) {
        ((uint8_t*)handoff)[index] = 0U;
    }

    recovery_message("ZEPHYROS RECOVERY LOADER\n");
    if (!recovery_fat32_open(&fs)) {
        recovery_message("FAT STATE LEGACY KERNEL\n");
        recovery_boot_legacy(mmap, vesa, legacy_lba, legacy_sectors, legacy_bytes);
        return;
    }
    first_valid = recovery_load_state(&fs, state_a, &first);
    second_valid = recovery_load_state(&fs, state_b, &second);
    if (!first_valid && !second_valid) {
        recovery_message("NO VALID STATE LEGACY KERNEL\n");
        recovery_boot_legacy(mmap, vesa, legacy_lba, legacy_sectors, legacy_bytes);
        return;
    }
    if (!first_valid && !recovery_locate_control(&fs, state_a, &first)) {
        recovery_message("STATE COPY LEGACY KERNEL\n");
        recovery_boot_legacy(mmap, vesa, legacy_lba, legacy_sectors, legacy_bytes);
        return;
    }
    if (!second_valid && !recovery_locate_control(&fs, state_b, &second)) {
        recovery_message("STATE COPY LEGACY KERNEL\n");
        recovery_boot_legacy(mmap, vesa, legacy_lba, legacy_sectors, legacy_bytes);
        return;
    }
    if (first_valid && second_valid && first.sequence == second.sequence &&
        !recovery_equal(first.raw, second.raw, UPDATE_SYSTEM_SLOT_CONTROL_SIZE)) {
        recovery_message("DIVERGENT STATE LEGACY KERNEL\n");
        recovery_boot_legacy(mmap, vesa, legacy_lba, legacy_sectors, legacy_bytes);
        return;
    }
    selected = first_valid && (!second_valid || first.sequence >= second.sequence) ? &first : &second;
    alternate = selected == &first ? &second : &first;
    if (selected->raw[17U] == UPDATE_SYSTEM_SLOTS_BOOT_ATTEMPTED) {
        const char* alternate_name = alternate == &first ? state_a : state_b;
        recovery_mark_attempt_failed(&fs, selected, alternate, alternate_name);
        recovery_message("UNCONFIRMED ATTEMPT LEGACY KERNEL\n");
        recovery_boot_legacy(mmap, vesa, legacy_lba, legacy_sectors, legacy_bytes);
        return;
    }
    if (recovery_find_file(&fs, "ZSI0    JRN", &slot) ||
        recovery_find_file(&fs, "ZSI1    JRN", &slot)) {
        recovery_message("PENDING JOURNAL LEGACY KERNEL\n");
        recovery_boot_legacy(mmap, vesa, legacy_lba, legacy_sectors, legacy_bytes);
        return;
    }
    if (selected->pending != UPDATE_SYSTEM_SLOT_NONE) slot_index = selected->pending;
    else slot_index = selected->active;
    if (!recovery_find_file(&fs, slots[slot_index], &slot) ||
        slot.size != recovery_u32(selected->raw + RECOVERY_STATE_SLOT_OFFSET + slot_index * RECOVERY_STATE_SLOT_SIZE + 12U) ||
        !recovery_verify_package(&fs, &slot, selected->raw + RECOVERY_STATE_SLOT_OFFSET + slot_index * RECOVERY_STATE_SLOT_SIZE + 16U,
                                 &kernel_offset, &kernel_size) ||
        !recovery_read_file(&fs, &slot, UPDATE_SYSTEM_HEADER_SIZE + kernel_offset,
                            (void*)RECOVERY_KERNEL_OFFSET, kernel_size)) {
        if (selected->pending != UPDATE_SYSTEM_SLOT_NONE) {
            const char* alternate_name = alternate == &first ? state_a : state_b;
            recovery_mark_attempt_failed(&fs, selected, alternate, alternate_name);
        }
        recovery_message("SLOT VALIDATION LEGACY KERNEL\n");
        recovery_boot_legacy(mmap, vesa, legacy_lba, legacy_sectors, legacy_bytes);
        return;
    }
    if (selected->pending != UPDATE_SYSTEM_SLOT_NONE) {
        uint32_t state_sequence;
        uint32_t attempt_sequence;
        const char* alternate_name = alternate == &first ? state_a : state_b;
        if (!recovery_publish_attempt(&fs, selected, alternate, alternate_name,
                                      slot_index, &state_sequence, &attempt_sequence)) {
            recovery_message("ATTEMPT WRITE LEGACY KERNEL\n");
            recovery_boot_legacy(mmap, vesa, legacy_lba, legacy_sectors, legacy_bytes);
            return;
        }
        handoff->magic[0] = 'Z'; handoff->magic[1] = 'S';
        handoff->magic[2] = 'B'; handoff->magic[3] = 'H';
        handoff->version = 1U;
        handoff->size = UPDATE_SYSTEM_BOOT_HANDOFF_SIZE;
        handoff->state_sequence = state_sequence;
        handoff->attempt_sequence = attempt_sequence;
        handoff->boot_slot = slot_index;
        handoff->previous_slot = selected->active;
        handoff->boot_state = UPDATE_SYSTEM_SLOTS_BOOT_ATTEMPTED;
    }
    recovery_boot_kernel(mmap, vesa);
}
