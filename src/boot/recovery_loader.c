#include "types.h"
#include "core/crypto.h"
#include "core/update_system.h"
#include "core/update_system_slots.h"
#include "core/update_trust.h"
#include "recovery_layout.h"
#include "recovery_menu.h"

#define RECOVERY_SECTOR_SIZE 512U
#define RECOVERY_MBR_PARTITIONS 4U
#define RECOVERY_MBR_PARTITION_OFFSET 446U
#define RECOVERY_FAT32_TYPE_LBA 0x0CU
#define RECOVERY_FAT32_TYPE_CHS 0x0BU
#define RECOVERY_FAT32_EOC 0x0FFFFFF8U
#define RECOVERY_FAT32_MAX_HOPS 32768U
#define RECOVERY_FAT32_MIN_CLUSTERS 4086U
#define RECOVERY_FAT32_MAX_SECTORS_PER_CLUSTER 128U
#define RECOVERY_FAT32_FAT_COUNT 2U
#define RECOVERY_FAT32_DIRECTORY_ATTRIBUTE 0x10U
#define RECOVERY_FAT32_VOLUME_ATTRIBUTE 0x08U
#define RECOVERY_FIND_ERROR (-1)
#define RECOVERY_FIND_NOT_FOUND 0
#define RECOVERY_FIND_FOUND 1
#define RECOVERY_FILE_NAME_SIZE 11U
#define RECOVERY_STATE_SLOT_OFFSET 32U
#define RECOVERY_STATE_SLOT_SIZE 176U
#define RECOVERY_STATE_RESERVED_OFFSET 384U
#define RECOVERY_STATE_HASH_OFFSET 480U
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
#define RECOVERY_VESA_AVAILABLE_OFFSET 11U
#define RECOVERY_LEGACY_KERNEL_LBA 64U
#define RECOVERY_LEGACY_KERNEL_SECTORS \
    ((RECOVERY_LEGACY_KERNEL_SIZE + RECOVERY_SECTOR_SIZE - 1U) / RECOVERY_SECTOR_SIZE)

typedef struct {
    uint32_t partition_lba;
    uint32_t partition_end_lba;
    uint32_t fat_lba;
    uint32_t data_lba;
    uint32_t root_cluster;
    uint32_t sectors_per_cluster;
    uint32_t sectors_per_fat;
    uint32_t cluster_count;
} recovery_fat32_t;

typedef struct {
    uint32_t cluster;
    uint32_t size;
} recovery_file_t;

typedef struct {
    const recovery_fat32_t* fs;
    uint32_t cluster;
    uint32_t cluster_offset;
    uint32_t remaining;
    uint32_t hops;
} recovery_file_reader_t;

typedef struct {
    uint32_t sequence;
    uint32_t attempt_sequence;
    uint16_t version;
    uint16_t reason;
    uint8_t active;
    uint8_t pending;
    uint8_t flags;
    uint8_t previous;
    uint8_t attempt;
    uint8_t boot_state;
    uint8_t valid;
    recovery_file_t file;
    uint8_t raw[UPDATE_SYSTEM_SLOT_CONTROL_SIZE];
} recovery_state_t;

typedef struct {
    recovery_state_t first;
    recovery_state_t second;
    recovery_state_t* selected;
    recovery_state_t* alternate;
    const char* alternate_name;
    uint8_t trusted;
    uint8_t alternate_control;
} recovery_controls_t;

typedef struct {
    recovery_fat32_t fs;
    recovery_controls_t controls;
    uint32_t mmap;
    uint32_t vesa;
    update_system_slots_reason_t reason;
    const char* diagnostic;
    uint8_t journal_clean;
    uint8_t forced_legacy;
    uint8_t retry_disabled;
    uint8_t invalid_slot;
    uint8_t automatic_timeout;
} recovery_boot_context_t;

static uint8_t recovery_sector[RECOVERY_SECTOR_SIZE];
static uint8_t recovery_fat_sector[RECOVERY_SECTOR_SIZE];
static uint8_t recovery_header[UPDATE_SYSTEM_HEADER_SIZE];
static uint8_t recovery_signed_header[UPDATE_SYSTEM_HEADER_SIZE];
static uint32_t recovery_fat_cached_lba;
static uint8_t recovery_fat_cache_valid;

static const char recovery_state_names[2][12] = {
    "ZSI0    STA", "ZSI1    STA"
};
static const char recovery_journal_names[2][12] = {
    "ZSI0    JRN", "ZSI1    JRN"
};
static const char recovery_slot_names[2][12] = {
    "ZSA0    ZSY", "ZSB0    ZSY"
};

extern int recovery_bios_read_sector(uint32_t lba, void* output);
extern int recovery_bios_write_sector(uint32_t lba, const void* input);
extern void recovery_boot_kernel_entry(uint32_t mmap, uint32_t vesa);

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

static void recovery_clear(void* value, uint32_t size) {
    uint8_t* output = (uint8_t*)value;
    if (!output) return;
    for (uint32_t index = 0U; index < size; index++) output[index] = 0U;
}

static int recovery_read_sector(uint32_t lba, void* output) {
    if (!output) return 0;
    return recovery_bios_read_sector(lba, output);
}

static int recovery_write_sector(uint32_t lba, const void* input) {
    if (!input) return 0;
    return recovery_bios_write_sector(lba, input);
}

static void recovery_message(const char* message) {
    recovery_console_print(message);
}

static int recovery_fat32_open(recovery_fat32_t* fs) {
    if (!fs || !recovery_read_sector(0U, recovery_sector) ||
        recovery_sector[510U] != 0x55U || recovery_sector[511U] != 0xAAU)
        return 0;
    for (uint32_t index = 0U; index < RECOVERY_MBR_PARTITIONS; index++) {
        uint8_t* partition = recovery_sector + RECOVERY_MBR_PARTITION_OFFSET + index * 16U;
        uint32_t partition_sectors;
        uint32_t reserved;
        uint32_t fat_sectors;
        uint32_t total_sectors;
        uint32_t data_sectors;
        uint32_t fat_entries_needed;
        if (partition[4] != RECOVERY_FAT32_TYPE_LBA && partition[4] != RECOVERY_FAT32_TYPE_CHS) continue;
        fs->partition_lba = recovery_u32(partition + 8U);
        partition_sectors = recovery_u32(partition + 12U);
        if (!fs->partition_lba || !partition_sectors ||
            fs->partition_lba > 0xFFFFFFFFU - partition_sectors)
            continue;
        fs->partition_end_lba = fs->partition_lba + partition_sectors;
        if (!recovery_read_sector(fs->partition_lba, recovery_sector) ||
            recovery_sector[510U] != 0x55U || recovery_sector[511U] != 0xAAU ||
            recovery_u16(recovery_sector + 11U) != RECOVERY_SECTOR_SIZE ||
            !recovery_equal(recovery_sector + 71U,
                            (const uint8_t*)"ZEPHYROS   ", 11U) ||
            !recovery_equal(recovery_sector + 82U,
                            (const uint8_t*)"FAT32   ", 8U) ||
            recovery_u32(recovery_sector + 28U) != fs->partition_lba)
            return 0;
        fs->sectors_per_cluster = recovery_sector[13U];
        fs->sectors_per_fat = recovery_u32(recovery_sector + 36U);
        fs->root_cluster = recovery_u32(recovery_sector + 44U);
        reserved = recovery_u16(recovery_sector + 14U);
        total_sectors = recovery_u32(recovery_sector + 32U);
        if (!fs->sectors_per_cluster ||
            fs->sectors_per_cluster > RECOVERY_FAT32_MAX_SECTORS_PER_CLUSTER ||
            (fs->sectors_per_cluster & (fs->sectors_per_cluster - 1U)) ||
            !reserved || recovery_sector[16U] != RECOVERY_FAT32_FAT_COUNT ||
            !fs->sectors_per_fat || total_sectors != partition_sectors ||
            reserved >= total_sectors ||
            fs->sectors_per_fat > (total_sectors - reserved) /
                RECOVERY_FAT32_FAT_COUNT)
            return 0;
        fat_sectors = RECOVERY_FAT32_FAT_COUNT * fs->sectors_per_fat;
        if (reserved + fat_sectors >= total_sectors) return 0;
        data_sectors = total_sectors - reserved - fat_sectors;
        fs->cluster_count = data_sectors / fs->sectors_per_cluster;
        fat_entries_needed = fs->cluster_count / 128U +
            ((fs->cluster_count % 128U) + 129U) / 128U;
        if (fs->cluster_count < RECOVERY_FAT32_MIN_CLUSTERS ||
            fs->cluster_count >= RECOVERY_FAT32_EOC - 2U ||
            fs->sectors_per_fat < fat_entries_needed ||
            fs->root_cluster < 2U ||
            fs->root_cluster >= fs->cluster_count + 2U)
            return 0;
        fs->fat_lba = fs->partition_lba + reserved;
        fs->data_lba = fs->fat_lba + fat_sectors;
        if (fs->data_lba >= fs->partition_end_lba) return 0;
        return 1;
    }
    return 0;
}

static int recovery_cluster_lba(const recovery_fat32_t* fs, uint32_t cluster,
                                uint32_t sector, uint32_t* lba_out) {
    uint32_t relative;
    if (!fs || !lba_out || cluster < 2U ||
        cluster >= fs->cluster_count + 2U ||
        sector >= fs->sectors_per_cluster ||
        cluster - 2U > (fs->partition_end_lba - fs->data_lba) /
            fs->sectors_per_cluster)
        return 0;
    relative = (cluster - 2U) * fs->sectors_per_cluster + sector;
    if (relative >= fs->partition_end_lba - fs->data_lba) return 0;
    *lba_out = fs->data_lba + relative;
    return 1;
}

static uint32_t recovery_next_cluster(const recovery_fat32_t* fs, uint32_t cluster) {
    uint32_t offset;
    uint32_t lba;
    if (!fs || cluster < 2U || cluster >= fs->cluster_count + 2U ||
        cluster > 0x3FFFFFFFU)
        return 0U;
    offset = cluster * 4U;
    lba = fs->fat_lba + offset / RECOVERY_SECTOR_SIZE;
    if (lba < fs->fat_lba || lba >= fs->fat_lba + fs->sectors_per_fat)
        return 0U;
    if ((!recovery_fat_cache_valid || recovery_fat_cached_lba != lba) &&
        !recovery_read_sector(lba, recovery_fat_sector)) return 0U;
    recovery_fat_cached_lba = lba;
    recovery_fat_cache_valid = 1U;
    return recovery_u32(recovery_fat_sector + offset % RECOVERY_SECTOR_SIZE) &
           0x0FFFFFFFU;
}

static int recovery_reader_init(recovery_file_reader_t* reader,
                                const recovery_fat32_t* fs,
                                const recovery_file_t* file,
                                uint32_t offset) {
    uint32_t cluster_bytes;
    if (!reader || !fs || !file || offset > file->size) return 0;
    cluster_bytes = fs->sectors_per_cluster * RECOVERY_SECTOR_SIZE;
    if (!cluster_bytes || file->cluster < 2U ||
        file->cluster >= fs->cluster_count + 2U)
        return 0;
    reader->fs = fs;
    reader->cluster = file->cluster;
    reader->cluster_offset = offset;
    reader->remaining = file->size - offset;
    reader->hops = 0U;
    while (reader->cluster_offset >= cluster_bytes && reader->remaining) {
        if (reader->hops++ >= RECOVERY_FAT32_MAX_HOPS) return 0;
        reader->cluster = recovery_next_cluster(fs, reader->cluster);
        if (reader->cluster < 2U ||
            reader->cluster >= fs->cluster_count + 2U)
            return 0;
        reader->cluster_offset -= cluster_bytes;
    }
    return 1;
}

static int recovery_reader_read(recovery_file_reader_t* reader, void* output,
                                uint32_t size) {
    uint8_t* destination = (uint8_t*)output;
    uint32_t cluster_bytes;
    if (!reader || !reader->fs || !output || size > reader->remaining) return 0;
    cluster_bytes = reader->fs->sectors_per_cluster * RECOVERY_SECTOR_SIZE;
    if (reader->cluster_offset == cluster_bytes && reader->remaining) {
        if (reader->hops++ >= RECOVERY_FAT32_MAX_HOPS) return 0;
        reader->cluster = recovery_next_cluster(reader->fs, reader->cluster);
        reader->cluster_offset = 0U;
        if (reader->cluster < 2U ||
            reader->cluster >= reader->fs->cluster_count + 2U)
            return 0;
    }
    while (size) {
        uint32_t sector_index = reader->cluster_offset / RECOVERY_SECTOR_SIZE;
        uint32_t sector_offset = reader->cluster_offset % RECOVERY_SECTOR_SIZE;
        uint32_t amount = RECOVERY_SECTOR_SIZE - sector_offset;
        uint32_t lba;
        if (amount > size) amount = size;
        if (!recovery_cluster_lba(reader->fs, reader->cluster, sector_index,
                                  &lba) ||
            !recovery_read_sector(lba, recovery_sector))
            return 0;
        for (uint32_t index = 0U; index < amount; index++)
            destination[index] = recovery_sector[sector_offset + index];
        destination += amount;
        size -= amount;
        reader->remaining -= amount;
        reader->cluster_offset += amount;
        if (reader->cluster_offset == cluster_bytes && reader->remaining && size) {
            if (reader->hops++ >= RECOVERY_FAT32_MAX_HOPS) return 0;
            reader->cluster = recovery_next_cluster(reader->fs, reader->cluster);
            reader->cluster_offset = 0U;
            if (reader->cluster < 2U ||
                reader->cluster >= reader->fs->cluster_count + 2U)
                return 0;
        }
    }
    return 1;
}

static int recovery_find_file_status(
    const recovery_fat32_t* fs, const char name[RECOVERY_FILE_NAME_SIZE],
    recovery_file_t* file) {
    uint32_t cluster;
    uint32_t hops = 0U;
    if (!fs || !name || !file) return RECOVERY_FIND_ERROR;
    cluster = fs->root_cluster;
    while (cluster >= 2U && cluster < fs->cluster_count + 2U &&
           hops++ < RECOVERY_FAT32_MAX_HOPS) {
        for (uint32_t sector = 0U; sector < fs->sectors_per_cluster; sector++) {
            uint32_t lba;
            if (!recovery_cluster_lba(fs, cluster, sector, &lba) ||
                !recovery_read_sector(lba, recovery_sector))
                return RECOVERY_FIND_ERROR;
            for (uint32_t entry = 0U; entry < 16U; entry++) {
                uint8_t* raw = recovery_sector + entry * 32U;
                if (!raw[0]) return RECOVERY_FIND_NOT_FOUND;
                if (raw[0] == 0xE5U || raw[11] == 0x0FU ||
                    (raw[11] & RECOVERY_FAT32_VOLUME_ATTRIBUTE))
                    continue;
                if (!recovery_equal(raw, (const uint8_t*)name, RECOVERY_FILE_NAME_SIZE)) continue;
                if (raw[11] & RECOVERY_FAT32_DIRECTORY_ATTRIBUTE)
                    return RECOVERY_FIND_ERROR;
                file->cluster = ((uint32_t)recovery_u16(raw + 20U) << 16U) | recovery_u16(raw + 26U);
                file->size = recovery_u32(raw + 28U);
                return RECOVERY_FIND_FOUND;
            }
        }
        cluster = recovery_next_cluster(fs, cluster);
        if (!cluster) return RECOVERY_FIND_ERROR;
        if (cluster >= RECOVERY_FAT32_EOC) return RECOVERY_FIND_NOT_FOUND;
    }
    return cluster >= RECOVERY_FAT32_EOC ? RECOVERY_FIND_NOT_FOUND :
                                           RECOVERY_FIND_ERROR;
}

static int recovery_find_file(const recovery_fat32_t* fs,
                              const char name[RECOVERY_FILE_NAME_SIZE],
                              recovery_file_t* file) {
    return recovery_find_file_status(fs, name, file) == RECOVERY_FIND_FOUND;
}

static int recovery_file_contains_cluster(const recovery_fat32_t* fs,
                                          const recovery_file_t* file,
                                          uint32_t target) {
    uint32_t cluster;
    uint32_t cluster_bytes;
    uint32_t count;
    if (!fs || !file || !file->size) return 0;
    if (file->size > UPDATE_SYSTEM_SLOT_MAX_FILE_SIZE) return -1;
    cluster_bytes = fs->sectors_per_cluster * RECOVERY_SECTOR_SIZE;
    count = file->size / cluster_bytes +
            ((file->size % cluster_bytes) != 0U);
    cluster = file->cluster;
    for (uint32_t index = 0U; index < count; index++) {
        if (cluster < 2U || cluster >= fs->cluster_count + 2U) return -1;
        if (cluster == target) return 1;
        if (index + 1U == count) return 0;
        cluster = recovery_next_cluster(fs, cluster);
    }
    return 0;
}

static int recovery_control_cluster_safe(const recovery_fat32_t* fs,
                                         const recovery_state_t* selected,
                                         const recovery_state_t* alternate) {
    recovery_file_t file;
    uint32_t next;
    if (!fs || !selected || !alternate ||
        selected->file.cluster == alternate->file.cluster)
        return 0;
    next = recovery_next_cluster(fs, selected->file.cluster);
    if (next < RECOVERY_FAT32_EOC) return 0;
    next = recovery_next_cluster(fs, alternate->file.cluster);
    if (next < RECOVERY_FAT32_EOC) return 0;
    for (uint32_t index = 0U; index < 2U; index++) {
        int status = recovery_find_file_status(
            fs, recovery_slot_names[index], &file);
        int contains;
        if (status == RECOVERY_FIND_ERROR) return 0;
        if (status == RECOVERY_FIND_NOT_FOUND) continue;
        contains = recovery_file_contains_cluster(
            fs, &file, alternate->file.cluster);
        if (contains != 0) return 0;
    }
    return 1;
}

static int recovery_read_file(const recovery_fat32_t* fs, const recovery_file_t* file,
                              uint32_t offset, void* output, uint32_t size) {
    recovery_file_reader_t reader;
    return recovery_reader_init(&reader, fs, file, offset) &&
           recovery_reader_read(&reader, output, size);
}

static int recovery_identifier_valid(const uint8_t* value, uint32_t size) {
    uint32_t end = 0U;
    if (!value || !size) return 0;
    while (end < size && value[end]) {
        uint8_t item = value[end];
        if (!((item >= 'A' && item <= 'Z') ||
              (item >= 'a' && item <= 'z') ||
              (item >= '0' && item <= '9') || item == '.' || item == '_' ||
              item == '-'))
            return 0;
        end++;
    }
    if (!end || end == size) return 0;
    return recovery_zero(value + end + 1U, size - end - 1U);
}

static int recovery_fixed_text_equal(const uint8_t* value, uint32_t size,
                                     const char* expected) {
    uint32_t index = 0U;
    if (!value || !size || !expected) return 0;
    while (index < size && expected[index]) {
        if (value[index] != (uint8_t)expected[index]) return 0;
        index++;
    }
    return index < size && !expected[index] && !value[index] &&
           recovery_zero(value + index + 1U, size - index - 1U);
}

static int recovery_target_exceeds_base(const uint8_t* header,
                                        const uint8_t* base) {
    for (uint32_t index = 0U; index < 3U; index++) {
        uint16_t target = recovery_u16(
            header + UPDATE_SYSTEM_TARGET_VERSION_OFFSET + index * 2U);
        uint16_t source = recovery_u16(base + index * 2U);
        if (target != source) return target > source;
    }
    return recovery_u32(header + UPDATE_SYSTEM_TARGET_EPOCH_OFFSET) >
           recovery_u32(base + 6U);
}

static int recovery_header_policy_valid(const uint8_t* header) {
    uint16_t base_count;
    uint16_t flags;
    uint8_t route;
    uint8_t checkpoint_count;
    if (!header ||
        !recovery_fixed_text_equal(header + UPDATE_SYSTEM_CHANNEL_OFFSET,
                                   UPDATE_SYSTEM_CHANNEL_SIZE, "stable") ||
        !recovery_identifier_valid(header + UPDATE_SYSTEM_RELEASE_ID_OFFSET,
                                   UPDATE_SYSTEM_IDENTIFIER_SIZE) ||
        !recovery_identifier_valid(header + UPDATE_SYSTEM_RELEASE_TAG_OFFSET,
                                   UPDATE_SYSTEM_IDENTIFIER_SIZE))
        return 0;
    base_count = recovery_u16(header + UPDATE_SYSTEM_BASE_COUNT_OFFSET);
    if (!base_count || base_count > UPDATE_SYSTEM_MAX_BASES ||
        recovery_u16(header + UPDATE_SYSTEM_BASE_ENTRY_SIZE_OFFSET) !=
            UPDATE_SYSTEM_BASE_ENTRY_SIZE)
        return 0;
    for (uint32_t index = 0U; index < base_count; index++) {
        const uint8_t* base = header + UPDATE_SYSTEM_BASES_OFFSET +
                              index * UPDATE_SYSTEM_BASE_ENTRY_SIZE;
        if (!recovery_zero(base + 10U, 2U) ||
            !recovery_target_exceeds_base(header, base))
            return 0;
    }
    if (!recovery_zero(
            header + UPDATE_SYSTEM_BASES_OFFSET +
                base_count * UPDATE_SYSTEM_BASE_ENTRY_SIZE,
            UPDATE_SYSTEM_MIN_UPDATER_OFFSET -
                (UPDATE_SYSTEM_BASES_OFFSET +
                 base_count * UPDATE_SYSTEM_BASE_ENTRY_SIZE)) ||
        !recovery_zero(header + UPDATE_SYSTEM_MIN_UPDATER_OFFSET + 10U, 2U))
        return 0;
    flags = recovery_u16(header + RECOVERY_ZSYS_FLAGS_OFFSET);
    route = header[UPDATE_SYSTEM_ROUTE_OFFSET];
    checkpoint_count = header[UPDATE_SYSTEM_CHECKPOINT_COUNT_OFFSET];
    if (flags != UPDATE_SYSTEM_FLAG_REQUIRES_REBOOT ||
        recovery_u32(header + UPDATE_SYSTEM_BOOT_ABI_OFFSET) != 1U ||
        recovery_u32(header + UPDATE_SYSTEM_SCHEMA_FROM_OFFSET) >
            recovery_u32(header + UPDATE_SYSTEM_SCHEMA_TO_OFFSET) ||
        (route != UPDATE_SYSTEM_ROUTE_DIRECT &&
         route != UPDATE_SYSTEM_ROUTE_CHECKPOINT) ||
        checkpoint_count > UPDATE_SYSTEM_MAX_CHECKPOINTS ||
        (route == UPDATE_SYSTEM_ROUTE_DIRECT && checkpoint_count))
        return 0;
    if (recovery_u16(header + UPDATE_SYSTEM_CHECKPOINT_ENTRY_SIZE_OFFSET) !=
        UPDATE_SYSTEM_CHECKPOINT_SIZE)
        return 0;
    for (uint32_t index = 0U; index < checkpoint_count; index++) {
        if (!recovery_identifier_valid(
                header + UPDATE_SYSTEM_CHECKPOINTS_OFFSET +
                    index * UPDATE_SYSTEM_CHECKPOINT_SIZE,
                UPDATE_SYSTEM_CHECKPOINT_SIZE))
            return 0;
    }
    return recovery_zero(
        header + UPDATE_SYSTEM_CHECKPOINTS_OFFSET +
            checkpoint_count * UPDATE_SYSTEM_CHECKPOINT_SIZE,
        UPDATE_SYSTEM_KEY_ID_OFFSET -
            (UPDATE_SYSTEM_CHECKPOINTS_OFFSET +
             checkpoint_count * UPDATE_SYSTEM_CHECKPOINT_SIZE));
}

static int recovery_state_slot_record_valid(const uint8_t* raw) {
    uint32_t size;
    if (!raw || raw[0] > UPDATE_SYSTEM_SLOT_FILE_VALID || raw[1]) return 0;
    if (raw[0] == UPDATE_SYSTEM_SLOT_FILE_EMPTY)
        return recovery_zero(raw + 1U, RECOVERY_STATE_SLOT_SIZE - 1U);
    size = recovery_u32(raw + 12U);
    return size > UPDATE_SYSTEM_HEADER_SIZE &&
           size <= UPDATE_SYSTEM_SLOT_MAX_FILE_SIZE &&
           !recovery_zero(raw + 16U, CRYPTO_SHA256_SIZE) &&
           recovery_identifier_valid(raw + 48U, 64U) &&
           recovery_identifier_valid(raw + 112U, 64U);
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
    state->version = version;
    state->active = state->raw[12U];
    state->pending = state->raw[13U];
    state->flags = state->raw[14U];
    state->previous = version == 2U ? state->raw[15U] : state->active;
    state->attempt = version == 2U ? state->raw[16U] : UPDATE_SYSTEM_SLOT_NONE;
    state->boot_state = version == 2U ? state->raw[17U] :
        UPDATE_SYSTEM_SLOTS_BOOT_NONE;
    state->reason = version == 2U ? recovery_u16(state->raw + 18U) :
        UPDATE_SYSTEM_SLOTS_REASON_NONE;
    state->attempt_sequence = version == 2U ? recovery_u32(state->raw + 20U) : 0U;
    state->file = file;
    state->valid = state->sequence != 0U && state->active < 2U &&
        (state->pending == UPDATE_SYSTEM_SLOT_NONE || state->pending < 2U) &&
        state->pending != state->active &&
        recovery_state_slot_record_valid(state->raw + RECOVERY_STATE_SLOT_OFFSET) &&
        recovery_state_slot_record_valid(state->raw + RECOVERY_STATE_SLOT_OFFSET +
                                          RECOVERY_STATE_SLOT_SIZE) &&
        recovery_zero(state->raw + RECOVERY_STATE_RESERVED_OFFSET,
                      RECOVERY_STATE_HASH_OFFSET - RECOVERY_STATE_RESERVED_OFFSET) &&
        state->raw[RECOVERY_STATE_SLOT_OFFSET +
                   state->active * RECOVERY_STATE_SLOT_SIZE] ==
            UPDATE_SYSTEM_SLOT_FILE_VALID &&
        (state->pending == UPDATE_SYSTEM_SLOT_NONE ||
         state->raw[RECOVERY_STATE_SLOT_OFFSET +
                    state->pending * RECOVERY_STATE_SLOT_SIZE] ==
             UPDATE_SYSTEM_SLOT_FILE_VALID);
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
                                    uint8_t slot,
                                    recovery_state_t* verified_out) {
    uint8_t raw[UPDATE_SYSTEM_SLOT_CONTROL_SIZE];
    uint8_t hash[CRYPTO_SHA256_SIZE];
    recovery_state_t verified;
    uint32_t cluster_lba;

    uint32_t attempt_sequence;
    if (!selected || !target || !target_name || !verified_out || slot >= 2U ||
        selected->sequence == 0xFFFFFFFFU ||
        selected->attempt_sequence == 0xFFFFFFFFU ||
        target->file.cluster < 2U) return 0;
    for (uint32_t index = 0U; index < sizeof(raw); index++) raw[index] = selected->raw[index];
    recovery_state_write_u16(raw + 4U, 2U);
    recovery_state_write_u32(raw + 8U, selected->sequence + 1U);
    raw[15U] = selected->active;
    raw[16U] = slot;
    raw[17U] = UPDATE_SYSTEM_SLOTS_BOOT_ATTEMPTED;
    recovery_state_write_u16(raw + 18U, UPDATE_SYSTEM_SLOTS_REASON_NONE);
    attempt_sequence = selected->attempt_sequence;
    recovery_state_write_u32(raw + 20U, attempt_sequence + 1U);
    if (crypto_sha256(raw, 480U, hash) != 0) return 0;
    for (uint32_t index = 0U; index < sizeof(hash); index++) raw[480U + index] = hash[index];
    if (!recovery_cluster_lba(fs, target->file.cluster, 0U, &cluster_lba) ||
        !recovery_write_sector(cluster_lba, raw) ||
        !recovery_load_state(fs, target_name, &verified) ||
        !recovery_equal(verified.raw, raw, sizeof(raw))) return 0;
    *verified_out = verified;
    return 1;
}

static int recovery_mark_attempt_failed(const recovery_fat32_t* fs,
                                        const recovery_state_t* selected,
                                        const recovery_state_t* target,
                                        const char target_name[RECOVERY_FILE_NAME_SIZE],
                                        uint8_t slot,
                                        update_system_slots_reason_t reason,
                                        recovery_state_t* verified_out) {
    uint8_t raw[UPDATE_SYSTEM_SLOT_CONTROL_SIZE];
    uint8_t hash[CRYPTO_SHA256_SIZE];
    recovery_state_t verified;
    uint32_t cluster_lba;

    if (!selected || !target || !target_name || !verified_out || slot >= 2U ||
        reason == UPDATE_SYSTEM_SLOTS_REASON_NONE ||
        reason > UPDATE_SYSTEM_SLOTS_REASON_UNSUPPORTED ||
        selected->sequence == 0xFFFFFFFFU || target->file.cluster < 2U) return 0;
    for (uint32_t index = 0U; index < sizeof(raw); index++) raw[index] = selected->raw[index];
    recovery_state_write_u16(raw + 4U, 2U);
    recovery_state_write_u32(raw + 8U, selected->sequence + 1U);
    if (selected->version == 2U &&
        selected->boot_state != UPDATE_SYSTEM_SLOTS_BOOT_NONE) {
        raw[15U] = selected->previous;
        raw[16U] = selected->attempt;
    } else {
        raw[15U] = selected->active;
        raw[16U] = slot;
        recovery_state_write_u32(raw + 20U, 1U);
    }
    if (raw[16U] >= 2U) return 0;
    if (!recovery_u32(raw + 20U)) recovery_state_write_u32(raw + 20U, 1U);
    raw[13U] = UPDATE_SYSTEM_SLOT_NONE;
    raw[17U] = UPDATE_SYSTEM_SLOTS_BOOT_FAILED;
    recovery_state_write_u16(raw + 18U, (uint16_t)reason);
    if (crypto_sha256(raw, 480U, hash) != 0) return 0;
    for (uint32_t index = 0U; index < sizeof(hash); index++) raw[480U + index] = hash[index];
    if (!recovery_cluster_lba(fs, target->file.cluster, 0U, &cluster_lba) ||
        !recovery_write_sector(cluster_lba, raw) ||
        !recovery_load_state(fs, target_name, &verified) ||
        !recovery_equal(verified.raw, raw, sizeof(raw))) return 0;
    *verified_out = verified;
    return 1;
}

static int recovery_hash_file(const recovery_fat32_t* fs, const recovery_file_t* file,
                              uint8_t output[CRYPTO_SHA256_SIZE]) {
    crypto_sha256_ctx_t hash;
    recovery_file_reader_t reader;
    uint32_t offset = 0U;
    if (crypto_sha256_init(&hash) != 0 ||
        !recovery_reader_init(&reader, fs, file, 0U)) return 0;
    while (offset < file->size) {
        uint32_t amount = file->size - offset;
        if (amount > sizeof(recovery_sector)) amount = sizeof(recovery_sector);
        if (!recovery_reader_read(&reader, recovery_sector, amount) ||
            crypto_sha256_update(&hash, recovery_sector, amount) != 0) return 0;
        offset += amount;
    }
    return crypto_sha256_final(&hash, output) == 0;
}

static int recovery_hash_range(const recovery_fat32_t* fs, const recovery_file_t* file,
                               uint32_t offset, uint32_t size,
                               uint8_t output[CRYPTO_SHA256_SIZE]) {
    crypto_sha256_ctx_t hash;
    recovery_file_reader_t reader;
    uint32_t consumed = 0U;
    if (crypto_sha256_init(&hash) != 0 ||
        !recovery_reader_init(&reader, fs, file, offset)) return 0;
    while (consumed < size) {
        uint32_t amount = size - consumed;
        if (amount > sizeof(recovery_sector)) amount = sizeof(recovery_sector);
        if (!recovery_reader_read(&reader, recovery_sector, amount) ||
            crypto_sha256_update(&hash, recovery_sector, amount) != 0) return 0;
        consumed += amount;
    }
    return crypto_sha256_final(&hash, output) == 0;
}

static update_system_slots_reason_t recovery_verify_header(
    const recovery_fat32_t* fs, const recovery_file_t* file,
    const uint8_t expected_hash[CRYPTO_SHA256_SIZE], uint32_t* image_size_out) {
    uint8_t actual_hash[CRYPTO_SHA256_SIZE];
    uint32_t image_size;
    if (!fs || !file || !expected_hash || !image_size_out)
        return UPDATE_SYSTEM_SLOTS_REASON_FORMAT;
    if (!recovery_read_file(fs, file, 0U, recovery_header,
                            sizeof(recovery_header)))
        return UPDATE_SYSTEM_SLOTS_REASON_IO;
    if (!recovery_equal(recovery_header, (const uint8_t*)"ZSYS", 4U) ||
        recovery_u16(recovery_header + RECOVERY_ZSYS_VERSION_OFFSET) != 1U ||
        recovery_u16(recovery_header + RECOVERY_ZSYS_HEADER_SIZE_OFFSET) !=
            UPDATE_SYSTEM_HEADER_SIZE ||
        recovery_u32(recovery_header + RECOVERY_ZSYS_PAYLOAD_OFFSET) !=
            UPDATE_SYSTEM_HEADER_SIZE ||
        recovery_u32(recovery_header + RECOVERY_ZSYS_SIGNATURE_SIZE_OFFSET) !=
            RECOVERY_ZSYS_SIGNATURE_SIZE ||
        recovery_u32(recovery_header + RECOVERY_ZSYS_SIGNATURE_FIELD_OFFSET) !=
            RECOVERY_ZSYS_SIGNATURE_OFFSET ||
        !recovery_zero(recovery_header + RECOVERY_ZSYS_RESERVED_OFFSET,
                       UPDATE_SYSTEM_HEADER_SIZE - RECOVERY_ZSYS_RESERVED_OFFSET))
        return UPDATE_SYSTEM_SLOTS_REASON_FORMAT;
    if (recovery_u16(recovery_header + RECOVERY_ZSYS_ARCH_OFFSET) !=
        UPDATE_SYSTEM_ARCH_I386)
        return UPDATE_SYSTEM_SLOTS_REASON_FORMAT;
    image_size = recovery_u32(recovery_header + RECOVERY_ZSYS_IMAGE_SIZE_OFFSET);
    if (!image_size || image_size > UPDATE_SYSTEM_MAX_IMAGE_SIZE ||
        (image_size % RECOVERY_SECTOR_SIZE) != 0U ||
        recovery_u32(recovery_header + RECOVERY_ZSYS_PAYLOAD_SIZE_OFFSET) !=
            image_size ||
        recovery_u32(recovery_header + RECOVERY_ZSYS_TOTAL_SIZE_OFFSET) !=
            file->size ||
        file->size != UPDATE_SYSTEM_HEADER_SIZE + image_size)
        return UPDATE_SYSTEM_SLOTS_REASON_SIZE;
    if (!recovery_header_policy_valid(recovery_header))
        return UPDATE_SYSTEM_SLOTS_REASON_FORMAT;
    if (!recovery_equal(recovery_header + RECOVERY_ZSYS_KEY_ID_OFFSET,
                        UPDATE_TRUST_KEY_ID, 16U))
        return UPDATE_SYSTEM_SLOTS_REASON_SIGNATURE;
    if (!recovery_hash_file(fs, file, actual_hash))
        return UPDATE_SYSTEM_SLOTS_REASON_IO;
    if (!recovery_equal(actual_hash, expected_hash, CRYPTO_SHA256_SIZE))
        return UPDATE_SYSTEM_SLOTS_REASON_HASH;
    *image_size_out = image_size;
    return UPDATE_SYSTEM_SLOTS_REASON_NONE;
}

static update_system_slots_reason_t recovery_verify_signed_image(
    const recovery_fat32_t* fs, const recovery_file_t* file,
    uint32_t image_size) {
    crypto_ed25519_verify_ctx_t signature;
    crypto_sha256_ctx_t image_hash;
    recovery_file_reader_t reader;
    uint8_t actual_hash[CRYPTO_SHA256_SIZE];
    uint32_t offset = 0U;
    static const uint8_t domain[] = "ZEPHYROS-SYSTEM-IMAGE-V1\0";
    for (uint32_t index = 0U; index < sizeof(recovery_header); index++)
        recovery_signed_header[index] = recovery_header[index];
    for (uint32_t index = 0U; index < RECOVERY_ZSYS_SIGNATURE_SIZE; index++)
        recovery_signed_header[RECOVERY_ZSYS_SIGNATURE_OFFSET + index] = 0U;
    if (crypto_ed25519_verify_init(
            &signature, recovery_header + RECOVERY_ZSYS_SIGNATURE_OFFSET,
            UPDATE_TRUST_PUBLIC_KEY) != 0 ||
        crypto_ed25519_verify_update(&signature, domain,
                                     sizeof(domain) - 1U) != 0 ||
        crypto_ed25519_verify_update(&signature, recovery_signed_header,
                                     sizeof(recovery_signed_header)) != 0)
        return UPDATE_SYSTEM_SLOTS_REASON_SIGNATURE;
    if (crypto_sha256_init(&image_hash) != 0 ||
        !recovery_reader_init(&reader, fs, file, UPDATE_SYSTEM_HEADER_SIZE))
        return UPDATE_SYSTEM_SLOTS_REASON_IO;
    while (offset < image_size) {
        uint32_t amount = image_size - offset;
        if (amount > sizeof(recovery_sector)) amount = sizeof(recovery_sector);
        if (!recovery_reader_read(&reader, recovery_sector, amount))
            return UPDATE_SYSTEM_SLOTS_REASON_IO;
        if (crypto_ed25519_verify_update(&signature, recovery_sector,
                                         amount) != 0)
            return UPDATE_SYSTEM_SLOTS_REASON_SIGNATURE;
        if (crypto_sha256_update(&image_hash, recovery_sector, amount) != 0)
            return UPDATE_SYSTEM_SLOTS_REASON_HASH;
        offset += amount;
    }
    if (crypto_ed25519_verify_final(&signature) != 0)
        return UPDATE_SYSTEM_SLOTS_REASON_SIGNATURE;
    if (crypto_sha256_final(&image_hash, actual_hash) != 0 ||
        !recovery_equal(actual_hash,
                        recovery_header + RECOVERY_ZSYS_IMAGE_HASH_OFFSET,
                        CRYPTO_SHA256_SIZE))
        return UPDATE_SYSTEM_SLOTS_REASON_HASH;
    return UPDATE_SYSTEM_SLOTS_REASON_NONE;
}

static update_system_slots_reason_t recovery_verify_components(
    const recovery_fat32_t* fs, const recovery_file_t* file,
    uint32_t image_size, uint32_t* kernel_offset, uint32_t* kernel_size) {
    uint32_t previous_end = 0U;
    if (!kernel_offset || !kernel_size ||
        recovery_u16(recovery_header + UPDATE_SYSTEM_COMPONENT_COUNT_OFFSET) !=
            UPDATE_SYSTEM_COMPONENT_COUNT ||
        recovery_u16(recovery_header +
                     UPDATE_SYSTEM_COMPONENT_ENTRY_SIZE_OFFSET) !=
            RECOVERY_ZSYS_COMPONENT_SIZE)
        return UPDATE_SYSTEM_SLOTS_REASON_FORMAT;
    for (uint32_t component = 0U;
         component < UPDATE_SYSTEM_COMPONENT_COUNT; component++) {
        uint8_t* entry = recovery_header + RECOVERY_ZSYS_COMPONENTS_OFFSET +
                         component * RECOVERY_ZSYS_COMPONENT_SIZE;
        uint8_t actual_hash[CRYPTO_SHA256_SIZE];
        uint16_t kind = recovery_u16(entry);
        uint32_t offset = recovery_u32(entry + 4U);
        uint32_t size = recovery_u32(entry + 8U);
        if (kind != component + RECOVERY_ZSYS_COMPONENT_BOOT ||
            recovery_u16(entry + 2U) != 0U || offset != previous_end ||
            !recovery_zero(entry + 44U, RECOVERY_ZSYS_COMPONENT_SIZE - 44U))
            return UPDATE_SYSTEM_SLOTS_REASON_FORMAT;
        if (!size || offset >= image_size || size > image_size - offset)
            return UPDATE_SYSTEM_SLOTS_REASON_SIZE;
        if ((component == 0U && (offset || size != RECOVERY_SECTOR_SIZE)) ||
            (component == 1U &&
             (offset != RECOVERY_SECTOR_SIZE ||
              (size % RECOVERY_SECTOR_SIZE) != 0U)))
            return UPDATE_SYSTEM_SLOTS_REASON_SIZE;
        if (!recovery_hash_range(fs, file, UPDATE_SYSTEM_HEADER_SIZE + offset,
                                 size, actual_hash))
            return UPDATE_SYSTEM_SLOTS_REASON_IO;
        if (!recovery_equal(actual_hash, entry + 12U, CRYPTO_SHA256_SIZE))
            return UPDATE_SYSTEM_SLOTS_REASON_HASH;
        if (kind == RECOVERY_ZSYS_COMPONENT_KERNEL) {
            *kernel_offset = offset;
            *kernel_size = size;
        }
        previous_end = offset + size;
    }
    if (!*kernel_size ||
        *kernel_size > RECOVERY_KERNEL_LIMIT - RECOVERY_KERNEL_OFFSET)
        return UPDATE_SYSTEM_SLOTS_REASON_SIZE;
    return UPDATE_SYSTEM_SLOTS_REASON_NONE;
}

static update_system_slots_reason_t recovery_verify_package(
    const recovery_fat32_t* fs, const recovery_file_t* file,
    const uint8_t expected_hash[CRYPTO_SHA256_SIZE], uint32_t* kernel_offset,
    uint32_t* kernel_size) {
    update_system_slots_reason_t reason;
    uint32_t image_size = 0U;
    if (!kernel_offset || !kernel_size)
        return UPDATE_SYSTEM_SLOTS_REASON_FORMAT;
    *kernel_offset = 0U;
    *kernel_size = 0U;
    reason = recovery_verify_header(fs, file, expected_hash, &image_size);
    if (reason != UPDATE_SYSTEM_SLOTS_REASON_NONE) return reason;
    reason = recovery_verify_signed_image(fs, file, image_size);
    if (reason != UPDATE_SYSTEM_SLOTS_REASON_NONE) return reason;
    return recovery_verify_components(fs, file, image_size, kernel_offset,
                                      kernel_size);
}

static void recovery_boot_kernel(uint32_t mmap, uint32_t vesa) {
    const uint8_t* vesa_info = (const uint8_t*)vesa;
    recovery_message(
        vesa_info && vesa_info[RECOVERY_VESA_AVAILABLE_OFFSET] ?
            "START KERNEL VESA\n" : "START KERNEL SIMPLE\n");
    recovery_boot_kernel_entry(mmap, vesa);
}

static int recovery_verify_loaded_kernel(
    uint32_t kernel_size, const uint8_t expected_hash[CRYPTO_SHA256_SIZE]) {
    uint8_t actual_hash[CRYPTO_SHA256_SIZE];
    return expected_hash &&
           crypto_sha256((const void*)RECOVERY_KERNEL_OFFSET, kernel_size,
                         actual_hash) == 0 &&
           recovery_equal(actual_hash, expected_hash, CRYPTO_SHA256_SIZE);
}

static int recovery_boot_legacy(uint32_t mmap, uint32_t vesa) {
    uint8_t* destination = (uint8_t*)RECOVERY_KERNEL_OFFSET;
    crypto_sha256_ctx_t hash;
    uint8_t actual_hash[CRYPTO_SHA256_SIZE];
    if (crypto_sha256_init(&hash) != 0) {
        recovery_message("LEGACY HASH INIT FAIL\n");
        return 0;
    }
    for (uint32_t index = 0U; index < RECOVERY_LEGACY_KERNEL_SECTORS; index++) {
        uint8_t* sector = destination + index * RECOVERY_SECTOR_SIZE;
        uint32_t amount = RECOVERY_LEGACY_KERNEL_SIZE - index * RECOVERY_SECTOR_SIZE;
        if (amount > RECOVERY_SECTOR_SIZE) amount = RECOVERY_SECTOR_SIZE;
        if (!recovery_read_sector(RECOVERY_LEGACY_KERNEL_LBA + index, sector) ||
            crypto_sha256_update(&hash, sector, amount) != 0) {
            recovery_message("LEGACY DISK READ FAIL\n");
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

static int recovery_controls_load(const recovery_fat32_t* fs,
                                  recovery_controls_t* controls) {
    int first_valid;
    int second_valid;
    if (!fs || !controls) return 0;
    recovery_clear(controls, sizeof(*controls));
    first_valid = recovery_load_state(fs, recovery_state_names[0],
                                      &controls->first);
    second_valid = recovery_load_state(fs, recovery_state_names[1],
                                       &controls->second);
    if (!first_valid && !second_valid) return 0;
    if (first_valid && second_valid &&
        controls->first.sequence == controls->second.sequence &&
        !recovery_equal(controls->first.raw, controls->second.raw,
                        UPDATE_SYSTEM_SLOT_CONTROL_SIZE))
        return 0;
    controls->selected = first_valid &&
        (!second_valid || controls->first.sequence >= controls->second.sequence) ?
        &controls->first : &controls->second;
    controls->alternate = controls->selected == &controls->first ?
        &controls->second : &controls->first;
    controls->alternate_name = controls->alternate == &controls->first ?
        recovery_state_names[0] : recovery_state_names[1];
    controls->alternate_control =
        (controls->alternate == &controls->first ? first_valid : second_valid) ||
        recovery_locate_control(fs, controls->alternate_name,
                                controls->alternate);
    if (controls->alternate->file.cluster < 2U ||
        controls->alternate->file.size != UPDATE_SYSTEM_SLOT_CONTROL_SIZE ||
        controls->alternate->file.cluster == controls->selected->file.cluster)
        controls->alternate_control = 0U;
    if (controls->alternate_control &&
        !recovery_control_cluster_safe(fs, controls->selected,
                                       controls->alternate))
        controls->alternate_control = 0U;
    controls->trusted = 1U;
    return 1;
}

static const uint8_t* recovery_slot_record(const recovery_state_t* state,
                                           uint8_t slot) {
    if (!state || slot >= 2U) return 0;
    return state->raw + RECOVERY_STATE_SLOT_OFFSET +
           slot * RECOVERY_STATE_SLOT_SIZE;
}

static int recovery_slot_available(const recovery_fat32_t* fs,
                                   const recovery_state_t* state,
                                   uint8_t slot) {
    const uint8_t* record = recovery_slot_record(state, slot);
    recovery_file_t file;
    return record && record[0] == UPDATE_SYSTEM_SLOT_FILE_VALID &&
           recovery_find_file(fs, recovery_slot_names[slot], &file) &&
           file.cluster >= 2U && file.size == recovery_u32(record + 12U);
}

static int recovery_slot_metadata_matches(const uint8_t* record) {
    if (!record) return 0;
    return recovery_u16(record + 2U) ==
               recovery_u16(recovery_header +
                            UPDATE_SYSTEM_TARGET_VERSION_OFFSET) &&
           recovery_u16(record + 4U) ==
               recovery_u16(recovery_header +
                            UPDATE_SYSTEM_TARGET_VERSION_OFFSET + 2U) &&
           recovery_u16(record + 6U) ==
               recovery_u16(recovery_header +
                            UPDATE_SYSTEM_TARGET_VERSION_OFFSET + 4U) &&
           recovery_u32(record + 8U) ==
               recovery_u32(recovery_header + UPDATE_SYSTEM_TARGET_EPOCH_OFFSET) &&
           recovery_equal(record + 48U,
                          recovery_header + UPDATE_SYSTEM_RELEASE_ID_OFFSET,
                          UPDATE_SYSTEM_IDENTIFIER_SIZE) &&
           recovery_equal(record + 112U,
                          recovery_header + UPDATE_SYSTEM_RELEASE_TAG_OFFSET,
                          UPDATE_SYSTEM_IDENTIFIER_SIZE);
}

static update_system_slots_reason_t recovery_prepare_slot(
    recovery_boot_context_t* context, uint8_t slot) {
    const uint8_t* record;
    const uint8_t* kernel_hash;
    recovery_file_t file;
    uint32_t kernel_offset;
    uint32_t kernel_size;
    update_system_slots_reason_t reason;
    if (!context || !context->controls.selected || slot >= 2U)
        return UPDATE_SYSTEM_SLOTS_REASON_FORMAT;
    record = recovery_slot_record(context->controls.selected, slot);
    if (!record || record[0] != UPDATE_SYSTEM_SLOT_FILE_VALID)
        return UPDATE_SYSTEM_SLOTS_REASON_FORMAT;
    if (!recovery_find_file(&context->fs, recovery_slot_names[slot], &file) ||
        file.cluster < 2U)
        return UPDATE_SYSTEM_SLOTS_REASON_IO;
    if (file.size != recovery_u32(record + 12U))
        return UPDATE_SYSTEM_SLOTS_REASON_SIZE;
    recovery_message("VERIFY SLOT\n");
    reason = recovery_verify_package(&context->fs, &file, record + 16U,
                                     &kernel_offset, &kernel_size);
    if (reason != UPDATE_SYSTEM_SLOTS_REASON_NONE) return reason;
    if (!recovery_slot_metadata_matches(record))
        return UPDATE_SYSTEM_SLOTS_REASON_FORMAT;
    recovery_message("LOAD KERNEL\n");
    if (!recovery_read_file(&context->fs, &file,
                            UPDATE_SYSTEM_HEADER_SIZE + kernel_offset,
                            (void*)RECOVERY_KERNEL_OFFSET, kernel_size))
        return UPDATE_SYSTEM_SLOTS_REASON_IO;
    kernel_hash = recovery_header + RECOVERY_ZSYS_COMPONENTS_OFFSET +
                  2U * RECOVERY_ZSYS_COMPONENT_SIZE + 12U;
    if (!recovery_verify_loaded_kernel(kernel_size, kernel_hash))
        return UPDATE_SYSTEM_SLOTS_REASON_HASH;
    recovery_message("KERNEL MEMORY OK\n");
    return UPDATE_SYSTEM_SLOTS_REASON_NONE;
}

static int recovery_context_mark_failed(recovery_boot_context_t* context,
                                        uint8_t slot,
                                        update_system_slots_reason_t reason) {
    recovery_state_t verified;
    if (!context || !context->controls.trusted ||
        !context->controls.alternate_control)
        return 0;
    if (!recovery_mark_attempt_failed(
            &context->fs, context->controls.selected,
            context->controls.alternate, context->controls.alternate_name,
            slot, reason, &verified))
        return 0;
    if (!recovery_controls_load(&context->fs, &context->controls) ||
        !context->controls.selected ||
        !recovery_equal(context->controls.selected->raw, verified.raw,
                        UPDATE_SYSTEM_SLOT_CONTROL_SIZE))
        return 0;
    return 1;
}

static int recovery_context_publish_attempt(recovery_boot_context_t* context,
                                            uint8_t slot) {
    recovery_state_t verified;
    if (!context || !context->controls.trusted ||
        !context->controls.alternate_control)
        return 0;
    if (!recovery_publish_attempt(
            &context->fs, context->controls.selected,
            context->controls.alternate, context->controls.alternate_name,
            slot, &verified))
        return 0;
    if (!recovery_controls_load(&context->fs, &context->controls) ||
        !context->controls.selected ||
        !recovery_equal(context->controls.selected->raw, verified.raw,
                        UPDATE_SYSTEM_SLOT_CONTROL_SIZE))
        return 0;
    return 1;
}

static void recovery_publish_handoff(const recovery_state_t* state) {
    update_system_boot_handoff_t* handoff =
        (update_system_boot_handoff_t*)UPDATE_SYSTEM_BOOT_HANDOFF_ADDRESS;
    if (!state) return;
    handoff->magic[0] = 'Z';
    handoff->magic[1] = 'S';
    handoff->magic[2] = 'B';
    handoff->magic[3] = 'H';
    handoff->version = 1U;
    handoff->size = UPDATE_SYSTEM_BOOT_HANDOFF_SIZE;
    handoff->state_sequence = state->sequence;
    handoff->attempt_sequence = state->attempt_sequence;
    handoff->boot_slot = state->attempt;
    handoff->previous_slot = state->previous;
    handoff->boot_state = UPDATE_SYSTEM_SLOTS_BOOT_ATTEMPTED;
}

static const char* recovery_slot_name(uint8_t slot) {
    if (slot == 0U) return "A";
    if (slot == 1U) return "B";
    return "NONE";
}

static const char* recovery_boot_state_name(uint8_t state) {
    if (state == UPDATE_SYSTEM_SLOTS_BOOT_ATTEMPTED) return "ATTEMPTED";
    if (state == UPDATE_SYSTEM_SLOTS_BOOT_FAILED) return "FAILED";
    return "NONE";
}

static const char* recovery_reason_name(update_system_slots_reason_t reason) {
    if (reason == UPDATE_SYSTEM_SLOTS_REASON_FORMAT) return "FORMAT";
    if (reason == UPDATE_SYSTEM_SLOTS_REASON_SIZE) return "SIZE";
    if (reason == UPDATE_SYSTEM_SLOTS_REASON_HASH) return "HASH";
    if (reason == UPDATE_SYSTEM_SLOTS_REASON_SIGNATURE) return "SIGNATURE";
    if (reason == UPDATE_SYSTEM_SLOTS_REASON_COMPATIBILITY)
        return "COMPATIBILITY";
    if (reason == UPDATE_SYSTEM_SLOTS_REASON_DOWNGRADE) return "DOWNGRADE";
    if (reason == UPDATE_SYSTEM_SLOTS_REASON_STATE) return "STATE";
    if (reason == UPDATE_SYSTEM_SLOTS_REASON_SPACE) return "SPACE";
    if (reason == UPDATE_SYSTEM_SLOTS_REASON_IO) return "IO";
    if (reason == UPDATE_SYSTEM_SLOTS_REASON_JOURNAL) return "JOURNAL";
    if (reason == UPDATE_SYSTEM_SLOTS_REASON_CANCELLED) return "CANCELLED";
    if (reason == UPDATE_SYSTEM_SLOTS_REASON_RECOVERY) return "RECOVERY";
    if (reason == UPDATE_SYSTEM_SLOTS_REASON_BOOT_FAILED) return "BOOT_FAILED";
    if (reason == UPDATE_SYSTEM_SLOTS_REASON_UNSUPPORTED) return "UNSUPPORTED";
    return "NONE";
}

static const char* recovery_slot_state_name(const recovery_boot_context_t* context,
                                            uint8_t slot) {
    const uint8_t* record;
    if (!context || !context->controls.selected || slot >= 2U ||
        context->invalid_slot == slot)
        return "INVALID";
    record = recovery_slot_record(context->controls.selected, slot);
    if (!record || record[0] != UPDATE_SYSTEM_SLOT_FILE_VALID) return "EMPTY";
    return "VALID";
}

static void recovery_menu_slot_version(const recovery_state_t* state,
                                       uint8_t slot, uint16_t* major,
                                       uint16_t* minor, uint16_t* patch) {
    const uint8_t* record = recovery_slot_record(state, slot);
    if (!record) return;
    *major = recovery_u16(record + 2U);
    *minor = recovery_u16(record + 4U);
    *patch = recovery_u16(record + 6U);
}

static void recovery_build_menu_view(recovery_boot_context_t* context,
                                     int allow_continue,
                                     recovery_menu_view_t* view) {
    recovery_state_t* state = context->controls.selected;
    update_system_slots_reason_t reason = context->reason;
    int state_actions = state && context->controls.trusted &&
        context->journal_clean && !context->forced_legacy;
    recovery_clear(view, sizeof(*view));
    view->diagnostic = context->diagnostic ? context->diagnostic : "ESTADO PRONTO";
    view->active = state ? recovery_slot_name(state->active) : "NONE";
    view->pending = state ? recovery_slot_name(state->pending) : "NONE";
    view->previous = state ? recovery_slot_name(state->previous) : "NONE";
    view->attempt = state ? recovery_slot_name(state->attempt) : "NONE";
    view->boot_state = state ? recovery_boot_state_name(state->boot_state) : "NONE";
    if (reason == UPDATE_SYSTEM_SLOTS_REASON_NONE && state)
        reason = (update_system_slots_reason_t)state->reason;
    view->reason = recovery_reason_name(reason);
    view->slot_a_state = recovery_slot_state_name(context, 0U);
    view->slot_b_state = recovery_slot_state_name(context, 1U);
    view->failure_menu = context->automatic_timeout;
    if (!state) return;
    view->sequence = state->sequence;
    view->attempt_sequence = state->attempt_sequence;
    recovery_menu_slot_version(state, 0U, &view->slot_a_major,
                               &view->slot_a_minor, &view->slot_a_patch);
    recovery_menu_slot_version(state, 1U, &view->slot_b_major,
                               &view->slot_b_minor, &view->slot_b_patch);
    view->slot_a_version_available =
        recovery_slot_record(state, 0U)[0] == UPDATE_SYSTEM_SLOT_FILE_VALID;
    view->slot_b_version_available =
        recovery_slot_record(state, 1U)[0] == UPDATE_SYSTEM_SLOT_FILE_VALID;
    view->allow_continue = state_actions && allow_continue &&
        state->boot_state == UPDATE_SYSTEM_SLOTS_BOOT_NONE;
    view->allow_previous = state_actions &&
        state->previous != context->invalid_slot &&
        recovery_slot_available(&context->fs, state, state->previous);
    view->allow_retry = state_actions && !context->retry_disabled &&
        state->version == 2U &&
        state->boot_state == UPDATE_SYSTEM_SLOTS_BOOT_FAILED &&
        state->attempt < 2U && state->attempt != state->active &&
        state->sequence != 0xFFFFFFFFU &&
        state->attempt_sequence != 0xFFFFFFFFU &&
        context->controls.alternate_control &&
        recovery_slot_available(&context->fs, state, state->attempt);
}

static void recovery_restrict_legacy(recovery_boot_context_t* context,
                                     const char* diagnostic,
                                     update_system_slots_reason_t reason) {
    context->forced_legacy = 1U;
    context->diagnostic = diagnostic;
    context->reason = reason;
}

static int recovery_boot_previous(recovery_boot_context_t* context) {
    uint8_t slot = context->controls.selected->previous;
    update_system_slots_reason_t reason = recovery_prepare_slot(context, slot);
    if (reason == UPDATE_SYSTEM_SLOTS_REASON_NONE) {
        recovery_boot_kernel(context->mmap, context->vesa);
        return 1;
    }
    context->diagnostic = "ANTERIOR RECUSADO";
    context->reason = reason;
    context->invalid_slot = slot;
    return 0;
}

static int recovery_retry_candidate(recovery_boot_context_t* context) {
    uint8_t slot = context->controls.selected->attempt;
    update_system_slots_reason_t reason = recovery_prepare_slot(context, slot);
    if (reason != UPDATE_SYSTEM_SLOTS_REASON_NONE) {
        if (!recovery_context_mark_failed(context, slot, reason))
            recovery_restrict_legacy(context, "FALHA GRAVAR MOTIVO",
                                     UPDATE_SYSTEM_SLOTS_REASON_IO);
        else {
            context->diagnostic = "RETRY RECUSADO";
            context->reason = reason;
            context->invalid_slot = slot;
        }
        context->retry_disabled = 1U;
        return 0;
    }
    if (!recovery_context_publish_attempt(context, slot)) {
        recovery_restrict_legacy(context, "FALHA GRAVAR TENTATIVA",
                                 UPDATE_SYSTEM_SLOTS_REASON_IO);
        context->retry_disabled = 1U;
        return 0;
    }
    recovery_publish_handoff(context->controls.selected);
    recovery_boot_kernel(context->mmap, context->vesa);
    return 1;
}

static int recovery_run_menu(recovery_boot_context_t* context,
                             int allow_continue) {
    for (;;) {
        recovery_menu_view_t view;
        recovery_menu_action_t action;
        recovery_build_menu_view(context, allow_continue, &view);
        action = recovery_menu_run(&view);
        if (action == RECOVERY_MENU_ACTION_CONTINUE && view.allow_continue)
            return 1;
        if (action == RECOVERY_MENU_ACTION_PREVIOUS && view.allow_previous) {
            if (recovery_boot_previous(context)) return 0;
            continue;
        }
        if (action == RECOVERY_MENU_ACTION_PREVIOUS_DEFAULT &&
            view.allow_previous) {
            if (recovery_boot_previous(context)) return 0;
            recovery_console_clear();
            recovery_boot_legacy(context->mmap, context->vesa);
            return 0;
        }
        if (action == RECOVERY_MENU_ACTION_RETRY && view.allow_retry) {
            if (!recovery_menu_confirm_retry(&view)) continue;
            if (recovery_retry_candidate(context)) return 0;
            allow_continue = 0;
            continue;
        }
        if (action == RECOVERY_MENU_ACTION_LEGACY) {
            recovery_console_clear();
            recovery_boot_legacy(context->mmap, context->vesa);
            return 0;
        }
    }
}

static int recovery_journal_status(const recovery_fat32_t* fs) {
    recovery_file_t file;
    for (uint32_t index = 0U; index < 2U; index++) {
        int status = recovery_find_file_status(
            fs, recovery_journal_names[index], &file);
        if (status == RECOVERY_FIND_ERROR) return RECOVERY_FIND_ERROR;
        if (status == RECOVERY_FIND_FOUND) return RECOVERY_FIND_FOUND;
    }
    return RECOVERY_FIND_NOT_FOUND;
}

static void recovery_prepare_failure_menu(recovery_boot_context_t* context,
                                          int voluntary) {
    context->automatic_timeout = voluntary ? 0U : 1U;
    recovery_run_menu(context, 0);
}

static int recovery_state_sequence_safe(const recovery_state_t* state) {
    if (!state || state->sequence == 0xFFFFFFFFU) return 0;
    return state->boot_state == UPDATE_SYSTEM_SLOTS_BOOT_NONE ||
           state->attempt_sequence != 0xFFFFFFFFU;
}

static void recovery_clear_handoff(void) {
    recovery_clear((void*)UPDATE_SYSTEM_BOOT_HANDOFF_ADDRESS,
                   UPDATE_SYSTEM_BOOT_HANDOFF_SIZE);
}

void recovery_loader_main(uint32_t mmap, uint32_t vesa) {
    recovery_boot_context_t context;
    recovery_state_t* state;
    update_system_slots_reason_t reason;
    uint8_t slot;
    int journal_status;
    int voluntary;
    recovery_clear(&context, sizeof(context));
    context.mmap = mmap;
    context.vesa = vesa;
    context.journal_clean = 1U;
    context.invalid_slot = UPDATE_SYSTEM_SLOT_NONE;
    context.diagnostic = "ESTADO PRONTO";
    recovery_console_init((uint8_t*)vesa);
    recovery_clear_handoff();
    voluntary = recovery_menu_wait_f8();
    if (!recovery_fat32_open(&context.fs)) {
        recovery_restrict_legacy(&context, "FAT32 INDISPONIVEL",
                                 UPDATE_SYSTEM_SLOTS_REASON_IO);
        recovery_prepare_failure_menu(&context, voluntary);
        return;
    }
    if (!recovery_controls_load(&context.fs, &context.controls)) {
        recovery_restrict_legacy(&context, "ESTADO INVALIDO OU DIVERGENTE",
                                 UPDATE_SYSTEM_SLOTS_REASON_STATE);
        recovery_prepare_failure_menu(&context, voluntary);
        return;
    }
    journal_status = recovery_journal_status(&context.fs);
    context.journal_clean = journal_status == RECOVERY_FIND_NOT_FOUND;
    if (!context.journal_clean) {
        recovery_restrict_legacy(
            &context,
            journal_status == RECOVERY_FIND_FOUND ? "JOURNAL PENDENTE" :
                                                    "FALHA LER JOURNAL",
            journal_status == RECOVERY_FIND_FOUND ?
                UPDATE_SYSTEM_SLOTS_REASON_JOURNAL :
                UPDATE_SYSTEM_SLOTS_REASON_IO);
        recovery_prepare_failure_menu(&context, voluntary);
        return;
    }
    state = context.controls.selected;
    if (state->flags & 1U) {
        recovery_restrict_legacy(&context, "RECUPERACAO PENDENTE",
                                 UPDATE_SYSTEM_SLOTS_REASON_RECOVERY);
        recovery_prepare_failure_menu(&context, voluntary);
        return;
    }
    if (!recovery_state_sequence_safe(state)) {
        recovery_restrict_legacy(&context, "SEQUENCIA ESGOTADA",
                                 UPDATE_SYSTEM_SLOTS_REASON_STATE);
        recovery_prepare_failure_menu(&context, voluntary);
        return;
    }
    if (state->boot_state == UPDATE_SYSTEM_SLOTS_BOOT_ATTEMPTED) {
        slot = state->attempt;
        if (!recovery_context_mark_failed(
                &context, slot, UPDATE_SYSTEM_SLOTS_REASON_BOOT_FAILED))
            recovery_restrict_legacy(&context, "FALHA REGISTRAR BOOT",
                                     UPDATE_SYSTEM_SLOTS_REASON_IO);
        else {
            context.diagnostic = "TENTATIVA INTERROMPIDA";
            context.reason = UPDATE_SYSTEM_SLOTS_REASON_BOOT_FAILED;
        }
        recovery_prepare_failure_menu(&context, voluntary);
        return;
    }
    if (state->boot_state == UPDATE_SYSTEM_SLOTS_BOOT_FAILED) {
        context.diagnostic = "CANDIDATO PRESERVADO";
        context.reason = (update_system_slots_reason_t)state->reason;
        recovery_prepare_failure_menu(&context, voluntary);
        return;
    }
    if (voluntary) {
        context.diagnostic = "MENU SOLICITADO";
        context.automatic_timeout = 0U;
        if (!recovery_run_menu(&context, 1)) return;
    }
    state = context.controls.selected;
    slot = state->pending != UPDATE_SYSTEM_SLOT_NONE ?
        state->pending : state->active;
    reason = recovery_prepare_slot(&context, slot);
    if (reason != UPDATE_SYSTEM_SLOTS_REASON_NONE) {
        context.diagnostic = "SLOT RECUSADO";
        context.reason = reason;
        context.invalid_slot = slot;
        if (state->pending != UPDATE_SYSTEM_SLOT_NONE &&
            !recovery_context_mark_failed(&context, slot, reason))
            recovery_restrict_legacy(&context, "FALHA GRAVAR MOTIVO",
                                     UPDATE_SYSTEM_SLOTS_REASON_IO);
        context.automatic_timeout = voluntary ? 0U : 1U;
        recovery_run_menu(&context, 0);
        return;
    }
    if (state->pending != UPDATE_SYSTEM_SLOT_NONE) {
        if (!recovery_context_publish_attempt(&context, slot)) {
            recovery_restrict_legacy(&context, "FALHA GRAVAR TENTATIVA",
                                     UPDATE_SYSTEM_SLOTS_REASON_IO);
            context.automatic_timeout = voluntary ? 0U : 1U;
            recovery_run_menu(&context, 0);
            return;
        }
        recovery_publish_handoff(context.controls.selected);
    }
    recovery_boot_kernel(mmap, vesa);
}
