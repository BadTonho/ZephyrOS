#include "fs/storage.h"
#include "fs/block_cache.h"
#include "fs/fs.h"
#include "drivers/ata.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/spinlock.h"
#include "core/string.h"

#define STORAGE_MBR_SIGNATURE_OFFSET 510U
#define STORAGE_MBR_TABLE_OFFSET 446U
#define STORAGE_MBR_ENTRY_SIZE 16U
#define STORAGE_MBR_ENTRY_COUNT 4U
#define STORAGE_MBR_BOOT_FLAG_OFFSET 0U
#define STORAGE_MBR_TYPE_OFFSET 4U
#define STORAGE_MBR_START_LBA_OFFSET 8U
#define STORAGE_MBR_SECTOR_COUNT_OFFSET 12U
#define STORAGE_FAT12_MAX_CLUSTERS 4085U
/* A imagem do sistema usa um FAT32 compacto de 64 MiB. */
#define STORAGE_FAT32_MIN_CLUSTERS (STORAGE_FAT12_MAX_CLUSTERS + 1U)
#define STORAGE_FAT12_END 0x0FF8U
#define STORAGE_FAT12_BAD 0x0FF7U
#define STORAGE_FAT32_FREE 0x00000000U
#define STORAGE_FAT32_END 0x0FFFFFF8U
#define STORAGE_FAT32_BAD 0x0FFFFFF7U
#define STORAGE_DIR_ENTRY_SIZE 32U
#define STORAGE_MAX_LFN_ENTRIES ((STORAGE_LONG_NAME_SIZE + 12U) / 13U)
#define STORAGE_ATTR_DIRECTORY 0x10U
#define STORAGE_ATTR_VOLUME 0x08U
#define STORAGE_ATTR_LFN 0x0FU
#define STORAGE_FIRST_DATA_CLUSTER 2U
#define STORAGE_MAX_SECTORS_PER_CLUSTER 128U
#define STORAGE_MAX_FAT_COPIES 4U
#define STORAGE_FAT32_ENTRY_SIZE 4U
#define STORAGE_MBR_BOOTABLE_FLAG 0x80U
#define STORAGE_PARTITION_FAT12 0x01U
#define STORAGE_PARTITION_FAT32_CHS 0x0BU
#define STORAGE_PARTITION_FAT32_LBA 0x0CU
#define STORAGE_BPB_BYTES_PER_SECTOR 11U
#define STORAGE_BPB_SECTORS_PER_CLUSTER 13U
#define STORAGE_BPB_RESERVED_SECTORS 14U
#define STORAGE_BPB_FAT_COUNT 16U
#define STORAGE_BPB_ROOT_ENTRIES 17U
#define STORAGE_BPB_TOTAL16 19U
#define STORAGE_BPB_SECTORS_PER_FAT16 22U
#define STORAGE_BPB_TOTAL32 32U
#define STORAGE_BPB_SECTORS_PER_FAT32 36U
#define STORAGE_BPB_LABEL_FAT12 43U
#define STORAGE_BPB_ROOT_CLUSTER 44U
#define STORAGE_BPB_LABEL_FAT32 71U
#define STORAGE_DIR_ATTRIBUTE_OFFSET 11U
#define STORAGE_DIR_CLUSTER_HIGH_OFFSET 20U
#define STORAGE_DIR_CLUSTER_LOW_OFFSET 26U
#define STORAGE_DIR_SIZE_OFFSET 28U
#define STORAGE_SLOT_WRITER_METADATA_INTERVAL 64U

typedef struct {
    uint8_t active;
    uint8_t volume_index;
    storage_fs_type_t fs_type;
    uint8_t sectors_per_cluster;
    uint8_t fat_count;
    uint16_t root_entries;
    uint32_t total_sectors;
    uint32_t fat_start;
    uint32_t sectors_per_fat;
    uint32_t root_start;
    uint32_t root_sectors;
    uint32_t data_start;
    uint32_t total_clusters;
    uint32_t root_cluster;
} storage_mount_t;

typedef struct {
    uint8_t name[11];
    uint8_t attributes;
    uint32_t first_cluster;
    uint32_t size;
} storage_raw_entry_t;

typedef struct {
    char name[STORAGE_LONG_NAME_SIZE];
    char short_name[STORAGE_NAME_SIZE];
    uint8_t attributes;
    uint32_t first_cluster;
    uint32_t size;
    uint32_t entry_offset;
    uint32_t lfn_offset;
    uint8_t lfn_count;
    uint32_t lfn_offsets[STORAGE_MAX_LFN_ENTRIES];
} storage_long_raw_entry_t;

typedef struct {
    uint16_t units[STORAGE_LONG_NAME_SIZE];
    uint8_t active;
    uint8_t expected_sequence;
    uint8_t checksum;
    uint16_t highest_unit;
    uint32_t offset;
    uint8_t offset_count;
    uint32_t offsets[STORAGE_MAX_LFN_ENTRIES];
} storage_lfn_state_t;

typedef int (*storage_long_entry_visitor_t)(
    const storage_long_raw_entry_t* entry, void* context);

typedef void (*storage_entry_visitor_t)(const storage_raw_entry_t* entry,
                                        void* context);

static storage_disk_t storage_disks[STORAGE_MAX_DISKS];
static storage_volume_t storage_volumes[STORAGE_MAX_VOLUMES];
static storage_mount_t storage_mounts[STORAGE_MAX_MOUNTS];
static uint8_t storage_disk_count;
static uint8_t storage_volume_count;
static uint8_t storage_mounted_count;
static uint8_t storage_initialized;
static int storage_last_error;
static uint32_t storage_refresh_epoch;
static spinlock_t storage_registry_lock;
static spinlock_t storage_operation_lock;
static storage_long_dir_entry_t storage_long_cursor_entries[
    STORAGE_MAX_DIR_ENTRIES];
static uint32_t storage_fat32_allocation_hint[STORAGE_MAX_VOLUMES];
typedef struct {
    uint8_t active;
    char volume_id[STORAGE_ID_SIZE];
    char path[STORAGE_MAX_PATH];
    uint32_t expected_size;
    uint32_t written_size;
    uint8_t attributes;
    uint8_t* buffer;
} storage_stream_state_t;
static storage_stream_state_t storage_stream_state;

typedef struct {
    uint8_t active;
    char volume_id[STORAGE_ID_SIZE];
    char target_path[STORAGE_MAX_PATH];
    char temp_path[STORAGE_MAX_PATH];
    uint32_t expected_size;
    uint32_t received_size;
    uint32_t flushed_size;
    uint8_t attributes;
    uint8_t volume_index;
    uint32_t entry_offset;
    uint32_t first_cluster;
    uint32_t last_cluster;
    uint32_t cluster_bytes;
    uint32_t buffered_size;
    uint8_t buffer[STORAGE_SLOT_WRITE_BUFFER_SIZE];
} storage_slot_writer_state_t;

static storage_slot_writer_state_t storage_slot_writer_state;

static int storage_cluster_is_end(const storage_mount_t* mount,
                                  uint32_t cluster);
static int storage_cluster_is_bad(const storage_mount_t* mount,
                                  uint32_t cluster);

static uint16_t storage_read_u16(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t storage_read_u32(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void storage_copy_text(char* destination, uint32_t capacity,
                              const char* source) {
    uint32_t index = 0;

    if (!destination || capacity == 0) return;
    if (!source) source = "";
    while (source[index] && index + 1U < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static char storage_lower(char value) {
    if (value >= 'A' && value <= 'Z') return (char)(value + ('a' - 'A'));
    return value;
}

static int storage_text_equal(const char* left, const char* right) {
    if (!left || !right) return 0;
    while (*left && *right) {
        if (storage_lower(*left) != storage_lower(*right)) return 0;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static void storage_append_text(char* destination, uint32_t capacity,
                                const char* source) {
    uint32_t offset = 0U;

    if (!destination || !capacity) return;
    while (destination[offset] && offset + 1U < capacity) offset++;
    while (source && *source && offset + 1U < capacity) {
        destination[offset++] = *source++;
    }
    destination[offset] = '\0';
}

static void storage_build_ata_id(uint8_t slot, char* id) {
    if (!id) return;
    id[0] = 'a';
    id[1] = 't';
    id[2] = 'a';
    id[3] = (char)('0' + slot);
    id[4] = '\0';
}

static void storage_build_volume_id(const char* disk_id, uint8_t partition,
                                    char* id) {
    storage_copy_text(id, STORAGE_ID_SIZE, disk_id);
    if (partition == 0U) storage_append_text(id, STORAGE_ID_SIZE, "raw");
    else {
        storage_append_text(id, STORAGE_ID_SIZE, "p");
        if (partition < 10U) {
            char number[2] = { (char)('0' + partition), '\0' };
            storage_append_text(id, STORAGE_ID_SIZE, number);
        }
    }
}

static void storage_log_volume(log_level_t level, const char* id,
                               const char* detail) {
    char message[128];
    uint32_t offset = 0;

    message[offset++] = '[';
    while (id && *id && offset + 1U < sizeof(message)) {
        message[offset++] = *id++;
    }
    if (offset + 2U < sizeof(message)) {
        message[offset++] = ']';
        message[offset++] = ' ';
    }
    while (detail && *detail && offset + 1U < sizeof(message)) {
        message[offset++] = *detail++;
    }
    message[offset] = '\0';
    log_print(level, "FS", message);
}

static int storage_volume_index(const char* id) {
    if (!id) return -1;
    for (uint8_t index = 0; index < storage_volume_count; index++) {
        if (storage_text_equal(storage_volumes[index].id, id)) return index;
    }
    return -1;
}

static int storage_disk_index(const char* id) {
    if (!id) return -1;
    for (uint8_t index = 0; index < storage_disk_count; index++) {
        if (storage_text_equal(storage_disks[index].id, id)) return index;
    }
    if (storage_text_equal(id, "ata-primary") && storage_disk_count > 0) {
        return 0;
    }
    return -1;
}

static storage_mount_t* storage_mount_for_volume(uint8_t volume_index) {
    for (uint8_t index = 0; index < STORAGE_MAX_MOUNTS; index++) {
        if (storage_mounts[index].active &&
            storage_mounts[index].volume_index == volume_index) {
            return &storage_mounts[index];
        }
    }
    return 0;
}

static storage_mount_t* storage_free_mount(void) {
    for (uint8_t index = 0; index < STORAGE_MAX_MOUNTS; index++) {
        if (!storage_mounts[index].active) return &storage_mounts[index];
    }
    return 0;
}

static int storage_read_relative(const storage_volume_t* volume,
                                 uint32_t relative_lba, uint8_t count,
                                 uint8_t* buffer) {
    uint32_t absolute_lba;

    if (!volume || !buffer || count == 0) {
        LOG_ERROR("FS", "Leitura de volume com argumento invalido");
        return ERR_NULL;
    }
    if (relative_lba >= volume->sector_count ||
        count > volume->sector_count - relative_lba) {
        storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                           "leitura fora dos limites");
        return ERR_DISK;
    }
    if (volume->start_lba > 0xFFFFFFFFU - relative_lba) {
        storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                           "overflow no endereco de leitura");
        return ERR_OVERFLOW;
    }
    absolute_lba = volume->start_lba + relative_lba;
    return block_read(volume->disk_id, absolute_lba, count, buffer);
}

static void storage_copy_label(char* label, const uint8_t* source) {
    int end = 10;

    while (end >= 0 && source[end] == ' ') end--;
    for (int index = 0; index <= end; index++) label[index] = source[index];
    label[end + 1] = '\0';
}

static int storage_configure_fat_type(storage_volume_t* volume,
                                      const uint8_t* sector,
                                      storage_mount_t* mount,
                                      uint32_t clusters,
                                      uint32_t sectors_per_fat,
                                      uint16_t root_entries) {
    if (!volume || !sector || !mount) {
        LOG_ERROR("FS", "Argumento nulo ao classificar FAT");
        return ERR_NULL;
    }
    if (clusters < STORAGE_FAT12_MAX_CLUSTERS) {
        uint32_t fat_bytes = sectors_per_fat * STORAGE_SECTOR_SIZE;
        uint32_t required = ((clusters + STORAGE_FIRST_DATA_CLUSTER) * 3U +
                             1U) / 2U;

        if (!root_entries ||
            storage_read_u16(sector + STORAGE_BPB_SECTORS_PER_FAT16) == 0 ||
            fat_bytes < required) return ERR_INVALID;
        mount->fs_type = STORAGE_FS_FAT12;
        storage_copy_label(volume->label, sector + STORAGE_BPB_LABEL_FAT12);
        return OK;
    }
    if (clusters < STORAGE_FAT32_MIN_CLUSTERS) return ERR_UNAVAILABLE;

    uint32_t root_cluster = storage_read_u32(
        sector + STORAGE_BPB_ROOT_CLUSTER);
    uint32_t fat_entries;

    if (sectors_per_fat >
        0xFFFFFFFFU / (STORAGE_SECTOR_SIZE /
                       STORAGE_FAT32_ENTRY_SIZE)) return ERR_OVERFLOW;
    fat_entries = sectors_per_fat *
                  (STORAGE_SECTOR_SIZE / STORAGE_FAT32_ENTRY_SIZE);
    if (root_entries ||
        storage_read_u16(sector + STORAGE_BPB_SECTORS_PER_FAT16) != 0 ||
        root_cluster < STORAGE_FIRST_DATA_CLUSTER ||
        root_cluster >= clusters + STORAGE_FIRST_DATA_CLUSTER ||
        fat_entries < clusters + STORAGE_FIRST_DATA_CLUSTER) {
        return ERR_INVALID;
    }
    mount->fs_type = STORAGE_FS_FAT32;
    mount->root_cluster = root_cluster;
    storage_copy_label(volume->label, sector + STORAGE_BPB_LABEL_FAT32);
    return OK;
}

static int storage_parse_bpb(storage_volume_t* volume, const uint8_t* sector,
                             storage_mount_t* mount) {
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved;
    uint8_t fat_count;
    uint16_t root_entries;
    uint32_t total_sectors;
    uint32_t sectors_per_fat;
    uint32_t root_sectors;
    uint32_t data_start;
    uint32_t clusters;
    int result;

    if (!volume || !sector || !mount) {
        LOG_ERROR("FS", "Argumento nulo ao validar BPB");
        return ERR_NULL;
    }
    if (sector[STORAGE_MBR_SIGNATURE_OFFSET] != 0x55 ||
        sector[STORAGE_MBR_SIGNATURE_OFFSET + 1U] != 0xAA) {
        return ERR_INVALID;
    }
    bytes_per_sector = storage_read_u16(
        sector + STORAGE_BPB_BYTES_PER_SECTOR);
    sectors_per_cluster = sector[STORAGE_BPB_SECTORS_PER_CLUSTER];
    reserved = storage_read_u16(sector + STORAGE_BPB_RESERVED_SECTORS);
    fat_count = sector[STORAGE_BPB_FAT_COUNT];
    root_entries = storage_read_u16(sector + STORAGE_BPB_ROOT_ENTRIES);
    total_sectors = storage_read_u16(sector + STORAGE_BPB_TOTAL16);
    if (!total_sectors) {
        total_sectors = storage_read_u32(sector + STORAGE_BPB_TOTAL32);
    }
    sectors_per_fat = storage_read_u16(
        sector + STORAGE_BPB_SECTORS_PER_FAT16);
    if (!sectors_per_fat) {
        sectors_per_fat = storage_read_u32(
            sector + STORAGE_BPB_SECTORS_PER_FAT32);
    }

    if (bytes_per_sector != STORAGE_SECTOR_SIZE || !sectors_per_cluster ||
        sectors_per_cluster > STORAGE_MAX_SECTORS_PER_CLUSTER ||
        (sectors_per_cluster & (sectors_per_cluster - 1U)) != 0 ||
        !reserved || !fat_count || fat_count > STORAGE_MAX_FAT_COPIES ||
        !sectors_per_fat ||
        !total_sectors || total_sectors > volume->sector_count) {
        return ERR_INVALID;
    }
    root_sectors = ((uint32_t)root_entries * STORAGE_DIR_ENTRY_SIZE +
                    STORAGE_SECTOR_SIZE - 1U) / STORAGE_SECTOR_SIZE;
    if (sectors_per_fat > (0xFFFFFFFFU - reserved - root_sectors) / fat_count) {
        return ERR_OVERFLOW;
    }
    data_start = reserved + fat_count * sectors_per_fat + root_sectors;
    if (data_start >= total_sectors) return ERR_INVALID;
    clusters = (total_sectors - data_start) / sectors_per_cluster;
    if (!clusters) return ERR_INVALID;

    kmemset(mount, 0, sizeof(*mount));
    mount->sectors_per_cluster = sectors_per_cluster;
    mount->fat_count = fat_count;
    mount->root_entries = root_entries;
    mount->total_sectors = total_sectors;
    mount->fat_start = reserved;
    mount->sectors_per_fat = sectors_per_fat;
    mount->root_start = reserved + fat_count * sectors_per_fat;
    mount->root_sectors = root_sectors;
    mount->data_start = data_start;
    mount->total_clusters = clusters;

    result = storage_configure_fat_type(volume, sector, mount, clusters,
                                        sectors_per_fat, root_entries);
    if (result != OK) return result;

    volume->fs_type = mount->fs_type;
    volume->bytes_per_sector = bytes_per_sector;
    volume->sectors_per_cluster = sectors_per_cluster;
    return OK;
}

static int storage_probe_volume(storage_volume_t* volume,
                                storage_mount_t* mount) {
    uint8_t sector[STORAGE_SECTOR_SIZE];
    uint32_t root_lba;
    int result;

    result = storage_read_relative(volume, 0, 1, sector);
    if (result != OK) return result;
    result = storage_parse_bpb(volume, sector, mount);
    if (result != OK) return result;
    root_lba = mount->fs_type == STORAGE_FS_FAT12 ? mount->root_start :
               mount->data_start +
               (mount->root_cluster - STORAGE_FIRST_DATA_CLUSTER) *
               mount->sectors_per_cluster;
    return storage_read_relative(volume, root_lba, 1, sector);
}

static storage_volume_t* storage_add_volume(const char* disk_id,
                                            uint8_t partition,
                                            storage_layout_t layout,
                                            uint8_t partition_type,
                                            uint32_t start_lba,
                                            uint32_t sector_count) {
    storage_volume_t* volume;

    if (storage_volume_count >= STORAGE_MAX_VOLUMES) {
        LOG_ERROR("FS", "Limite de volumes atingido");
        storage_last_error = ERR_OVERFLOW;
        return 0;
    }
    volume = &storage_volumes[storage_volume_count++];
    kmemset(volume, 0, sizeof(*volume));
    if (!disk_id) {
        LOG_ERROR("FS", "ID de bloco nulo no volume");
        storage_volume_count--;
        return 0;
    }
    storage_build_volume_id(disk_id, partition, volume->id);
    storage_copy_text(volume->disk_id, STORAGE_DISK_ID_SIZE, disk_id);
    volume->partition_index = partition;
    volume->partition_type = partition_type;
    volume->layout = layout;
    volume->start_lba = start_lba;
    volume->sector_count = sector_count;
    volume->read_only = 1;
    volume->state = STORAGE_VOLUME_DETECTED;
    return volume;
}

static int storage_is_extended_type(uint8_t type) {
    return type == 0x05U || type == 0x0FU || type == 0x85U || type == 0xEEU;
}

static int storage_is_supported_partition_type(uint8_t type) {
    return type == STORAGE_PARTITION_FAT12 ||
           type == STORAGE_PARTITION_FAT32_CHS ||
           type == STORAGE_PARTITION_FAT32_LBA;
}

static void storage_mark_volume_error(storage_volume_t* volume, int error,
                                      storage_volume_state_t state,
                                      const char* message) {
    if (!volume) return;
    volume->last_error = error;
    volume->state = state;
    storage_log_volume(state == STORAGE_VOLUME_INVALID ? LOG_LEVEL_ERROR :
                       LOG_LEVEL_WARN, volume->id, message);
}

static void storage_probe_candidate(storage_volume_t* volume) {
    storage_mount_t probe;
    int result;

    if (!volume || volume->state == STORAGE_VOLUME_INVALID) return;
    if (volume->layout == STORAGE_LAYOUT_MBR &&
        (storage_is_extended_type(volume->partition_type) ||
         !storage_is_supported_partition_type(volume->partition_type))) {
        storage_mark_volume_error(volume, ERR_UNAVAILABLE,
                                  STORAGE_VOLUME_UNSUPPORTED,
                                  "tipo de particao nao suportado");
        return;
    }
    result = storage_probe_volume(volume, &probe);
    if (result == OK) {
        volume->last_error = OK;
        return;
    }
    storage_mark_volume_error(volume, result,
                              result == ERR_UNAVAILABLE ?
                              STORAGE_VOLUME_UNSUPPORTED :
                              STORAGE_VOLUME_INVALID,
                              result == ERR_UNAVAILABLE ?
                              "filesystem nao suportado" : "BPB invalido");
}

static void storage_mark_overlaps(uint8_t first_index) {
    uint8_t overlaps[STORAGE_MBR_ENTRY_COUNT];
    uint8_t candidate_count = storage_volume_count - first_index;

    kmemset(overlaps, 0, sizeof(overlaps));
    for (uint8_t left = first_index; left < storage_volume_count; left++) {
        storage_volume_t* a = &storage_volumes[left];

        if (a->layout != STORAGE_LAYOUT_MBR ||
            a->state == STORAGE_VOLUME_INVALID) continue;
        for (uint8_t right = left + 1U; right < storage_volume_count; right++) {
            storage_volume_t* b = &storage_volumes[right];
            uint32_t a_end = a->start_lba + a->sector_count;
            uint32_t b_end;

             if (!storage_text_equal(b->disk_id, a->disk_id) ||
                b->layout != STORAGE_LAYOUT_MBR ||
                b->state == STORAGE_VOLUME_INVALID) continue;
            b_end = b->start_lba + b->sector_count;
            if (a->start_lba < b_end && b->start_lba < a_end) {
                overlaps[left - first_index] = 1;
                overlaps[right - first_index] = 1;
            }
        }
    }
    for (uint8_t index = 0; index < candidate_count; index++) {
        if (!overlaps[index]) continue;
        storage_mark_volume_error(&storage_volumes[first_index + index],
                                  ERR_INVALID, STORAGE_VOLUME_INVALID,
                                  "particao sobreposta");
    }
}

static void storage_scan_mbr(const char* disk_id, uint32_t disk_sectors,
                             const uint8_t* sector) {
    uint8_t first_index = storage_volume_count;

    for (uint8_t index = 0; index < STORAGE_MBR_ENTRY_COUNT; index++) {
        const uint8_t* entry = sector + STORAGE_MBR_TABLE_OFFSET +
                               index * STORAGE_MBR_ENTRY_SIZE;
        uint8_t boot_flag = entry[STORAGE_MBR_BOOT_FLAG_OFFSET];
        uint8_t type = entry[STORAGE_MBR_TYPE_OFFSET];
        uint32_t start_lba = storage_read_u32(
            entry + STORAGE_MBR_START_LBA_OFFSET);
        uint32_t sector_count = storage_read_u32(
            entry + STORAGE_MBR_SECTOR_COUNT_OFFSET);
        storage_volume_t* volume;

        if (!type && !start_lba && !sector_count) continue;
        volume = storage_add_volume(disk_id, index + 1U, STORAGE_LAYOUT_MBR,
                                    type, start_lba, sector_count);
        if (!volume) return;
        if ((boot_flag != 0 && boot_flag != STORAGE_MBR_BOOTABLE_FLAG) ||
            !type || !start_lba ||
            !sector_count || start_lba >= disk_sectors ||
            sector_count > disk_sectors - start_lba) {
            storage_mark_volume_error(volume, ERR_INVALID,
                                      STORAGE_VOLUME_INVALID,
                                      "entrada MBR fora dos limites");
        }
    }
    storage_mark_overlaps(first_index);
    for (uint8_t index = first_index; index < storage_volume_count; index++) {
        storage_probe_candidate(&storage_volumes[index]);
    }
}

static int storage_register_raw(const char* disk_id, uint32_t disk_sectors,
                                const uint8_t* sector,
                                const char* boot_disk_id) {
    storage_volume_t candidate;
    storage_mount_t probe;
    storage_volume_t* volume;
    int result;

    kmemset(&candidate, 0, sizeof(candidate));
    storage_build_volume_id(disk_id, 0U, candidate.id);
    storage_copy_text(candidate.disk_id, STORAGE_DISK_ID_SIZE, disk_id);
    candidate.sector_count = disk_sectors;
    result = storage_parse_bpb(&candidate, sector, &probe);
    if (result != OK) return result;
    volume = storage_add_volume(disk_id, 0U, STORAGE_LAYOUT_RAW, 0, 0,
                                disk_sectors);
    if (!volume) {
        LOG_ERROR("FS", "Falha ao registrar volume superfloppy");
        return ERR_OVERFLOW;
    }
    volume->fs_type = candidate.fs_type;
    volume->bytes_per_sector = candidate.bytes_per_sector;
    volume->sectors_per_cluster = candidate.sectors_per_cluster;
    storage_copy_text(volume->label, STORAGE_LABEL_SIZE, candidate.label);
    volume->last_error = OK;

    if (disk_id && boot_disk_id &&
        storage_text_equal(disk_id, boot_disk_id) &&
        fs_get_type() != FS_TYPE_NONE) {
        storage_mount_t* mount = storage_free_mount();

        if (!mount) {
            LOG_ERROR("FS", "Sem slot para registrar montagem de boot");
            return ERR_OVERFLOW;
        }
        *mount = probe;
        mount->active = 1;
        mount->volume_index = (uint8_t)(volume - storage_volumes);
        volume->boot = 1;
        volume->role = STORAGE_VOLUME_ROLE_BOOT;
        volume->pinned = 1;
        volume->read_only = 0;
        volume->mounted = 1;
        volume->generation = 1;
        volume->state = STORAGE_VOLUME_MOUNTED;
        storage_mounted_count++;
    }
    return OK;
}

static int storage_add_disk(const block_device_t* block) {
    storage_disk_t* disk;

    if (!block) {
        LOG_ERROR("FS", "Dispositivo de bloco nulo no inventario");
        return ERR_NULL;
    }
    if (storage_disk_count >= STORAGE_MAX_DISKS) {
        LOG_ERROR("FS", "Limite de discos do inventario atingido");
        return ERR_OVERFLOW;
    }
    disk = &storage_disks[storage_disk_count++];
    kmemset(disk, 0, sizeof(*disk));
    storage_copy_text(disk->id, STORAGE_DISK_ID_SIZE, block->id);
    storage_copy_text(disk->model, STORAGE_MODEL_SIZE, block->model);
    disk->kind = block->provider == BLOCK_PROVIDER_USB_MSC ?
                 STORAGE_DISK_USB_MSC : STORAGE_DISK_ATA;
    disk->sector_size = block->sector_size;
    disk->read_only = block->read_only;
    if (disk->kind == STORAGE_DISK_ATA && block->id[3] >= '0' &&
        block->id[3] <= '9') disk->slot = (uint8_t)(block->id[3] - '0');
    disk->sector_count = block->sector_count;
    disk->read_ops = block->read_ops;
    disk->write_ops = block->write_ops;
    disk->last_error = block->last_error;
    return OK;
}

static void storage_boot_disk_id(char* id, uint32_t capacity) {
    ata_device_t* boot_device;

    if (!id || !capacity) return;
    kmemset(id, 0, capacity);
    boot_device = ata_get_device();
    if (boot_device) storage_build_ata_id(boot_device->slot, id);
}

static int storage_auto_mount_system(void) {
    int candidate = -1;
    uint8_t candidates = 0;

    for (uint8_t index = 0; index < storage_volume_count; index++) {
        storage_volume_t* volume = &storage_volumes[index];
        if (volume->fs_type != STORAGE_FS_FAT32 ||
            !storage_text_equal(volume->label, "ZEPHYROS") ||
            volume->state != STORAGE_VOLUME_DETECTED) continue;
        candidate = index;
        candidates++;
    }
    if (!candidates) return OK;
    if (candidates != 1U) {
        LOG_ERROR("FS", "Volume FAT32 ZEPHYROS ambiguo; montagem recusada");
        storage_last_error = ERR_INVALID;
        return ERR_INVALID;
    }

    storage_volume_t* volume = &storage_volumes[candidate];
    storage_mount_t probe;
    storage_mount_t* mount = storage_free_mount();
    block_device_t block;
    int result = storage_probe_volume(volume, &probe);

    if (result != OK) {
        storage_mark_volume_error(volume, result, STORAGE_VOLUME_INVALID,
                                  "volume do sistema rejeitado");
        return result;
    }
    if (!mount) {
        storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                           "sem slot para montagem automatica");
        return ERR_OVERFLOW;
    }
    result = block_find(volume->disk_id, &block);
    if (result != OK) {
        storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                           "disco do sistema indisponivel");
        return result;
    }
    *mount = probe;
    mount->active = 1;
    mount->volume_index = (uint8_t)candidate;
    volume->role = STORAGE_VOLUME_ROLE_SYSTEM;
    volume->pinned = 1;
    volume->mounted = 1;
    volume->read_only = block.read_only ? 1U : 0U;
    volume->generation = 1U;
    volume->state = STORAGE_VOLUME_MOUNTED;
    volume->last_error = OK;
    storage_mounted_count++;
    storage_log_volume(LOG_LEVEL_INFO, volume->id,
                       volume->read_only ?
                       "volume do sistema montado somente-leitura" :
                       "volume do sistema montado gravavel");
    return OK;
}

int storage_init(void) {
    char boot_disk_id[STORAGE_DISK_ID_SIZE];
    uint32_t block_count = 0U;
    int cache_result;
    int block_result;

    LOG_INFO("FS", "Inicializando inventario de armazenamento");
    cache_result = block_cache_clear();
    if (cache_result != OK) {
        LOG_ERROR("FS", "Cache de blocos ocupado durante inventario");
        return cache_result;
    }
    spinlock_init(&storage_registry_lock);
    spinlock_init(&storage_operation_lock);
    kmemset(storage_disks, 0, sizeof(storage_disks));
    kmemset(storage_volumes, 0, sizeof(storage_volumes));
    kmemset(storage_mounts, 0, sizeof(storage_mounts));
    kmemset(storage_fat32_allocation_hint, 0,
            sizeof(storage_fat32_allocation_hint));
    storage_disk_count = 0;
    storage_volume_count = 0;
    storage_mounted_count = 0;
    storage_last_error = OK;
    storage_initialized = 0;
    storage_boot_disk_id(boot_disk_id, sizeof(boot_disk_id));
    block_result = block_get_count(&block_count);
    if (block_result != OK) {
        storage_initialized = 1;
        storage_last_error = block_result;
        LOG_ERROR("FS", "Camada de bloco indisponivel");
        return block_result;
    }
    if (!block_count) {
        storage_initialized = 1;
        storage_last_error = ERR_NOT_FOUND;
        LOG_ERROR("FS", "Nenhum dispositivo de bloco para inventariar");
        return ERR_NOT_FOUND;
    }

    for (uint32_t index = 0U; index < block_count; index++) {
        block_device_t block;
        uint8_t sector[STORAGE_SECTOR_SIZE];
        int raw_result;

        if (block_get_at(index, &block) != OK) {
            LOG_WARN("FS", "Dispositivo de bloco ausente durante o inventario");
            continue;
        }
        if (storage_add_disk(&block) != OK) {
            storage_last_error = ERR_OVERFLOW;
            continue;
        }
        if (block_read(block.id, 0U, 1U, sector) != OK) {
            storage_disks[storage_disk_count - 1U].last_error = ERR_DISK;
            storage_last_error = ERR_DISK;
            storage_log_volume(
                LOG_LEVEL_ERROR,
                storage_disks[storage_disk_count - 1U].id,
                "falha ao ler primeiro setor");
            continue;
        }
        raw_result = storage_register_raw(block.id, block.sector_count,
                                          sector, boot_disk_id);
        if (sector[STORAGE_MBR_SIGNATURE_OFFSET] == 0x55U &&
            sector[STORAGE_MBR_SIGNATURE_OFFSET + 1U] == 0xAAU) {
            storage_scan_mbr(block.id, block.sector_count, sector);
        } else if (raw_result != OK) {
            storage_disks[storage_disk_count - 1U].last_error = ERR_INVALID;
            storage_log_volume(
                LOG_LEVEL_WARN,
                storage_disks[storage_disk_count - 1U].id,
                "sem BPB FAT ou assinatura MBR valida");
        }
    }

    storage_initialized = 1;
    storage_auto_mount_system();
    if (!storage_disk_count) {
        storage_last_error = ERR_NOT_FOUND;
        LOG_ERROR("FS", "Nenhum disco inventariavel apos a leitura de bloco");
        return ERR_NOT_FOUND;
    }
    if (storage_last_error != OK) {
        LOG_ERROR("FS", "Inventario inicializado parcialmente");
        return storage_last_error;
    }
    LOG_INFO("FS", "Inventario de armazenamento inicializado com sucesso");
    return OK;
}

int storage_refresh(void) {
    char mounted_ids[STORAGE_MAX_MOUNTS][STORAGE_ID_SIZE];
    uint8_t mounted_before = 0U;
    int result;

    if (!storage_initialized) {
        LOG_ERROR("FS", "Atualizacao de storage antes da inicializacao");
        return ERR_STATE;
    }
    result = storage_sync_all();
    if (result != OK) {
        LOG_ERROR("FS", "Atualizacao recusada por falha de sincronizacao");
        return result;
    }
    LOG_INFO("FS", "Atualizando inventario de armazenamento");
    for (uint8_t index = 0U; index < storage_volume_count &&
         mounted_before < STORAGE_MAX_MOUNTS; index++) {
        if (!storage_volumes[index].mounted) continue;
        storage_copy_text(mounted_ids[mounted_before], STORAGE_ID_SIZE,
                          storage_volumes[index].id);
        mounted_before++;
    }
    storage_refresh_epoch++;
    if (!storage_refresh_epoch) storage_refresh_epoch = 1U;
    result = storage_init();
    if (result == OK) {
        for (uint8_t index = 0U; index < mounted_before; index++) {
            storage_volume_t volume;
            int restore_result = storage_find_volume(mounted_ids[index],
                                                     &volume);

            if (restore_result == OK && !volume.mounted) {
                restore_result = storage_mount(mounted_ids[index]);
            }
            if (restore_result != OK) {
                storage_log_volume(LOG_LEVEL_WARN, mounted_ids[index],
                                   "montagem anterior nao foi restaurada");
                result = restore_result;
            }
        }
    }
    for (uint8_t index = 0U; index < storage_volume_count; index++) {
        storage_volumes[index].generation += storage_refresh_epoch;
    }
    return result;
}

int storage_sync_volume(const char* id) {
    storage_volume_t volume;
    int result;

    if (!id) {
        LOG_ERROR("FS", "ID nulo na sincronizacao de volume");
        return ERR_NULL;
    }
    if (!storage_initialized) {
        LOG_ERROR("FS", "Sincronizacao de volume antes da inicializacao");
        return ERR_STATE;
    }
    result = storage_find_volume(id, &volume);
    if (result != OK) {
        LOG_ERROR("FS", "Volume nao encontrado na sincronizacao");
        return result;
    }
    result = block_cache_sync_device(volume.disk_id);
    if (result != OK) LOG_ERROR("FS", "Sync do volume falhou");
    return result;
}

int storage_sync_all(void) {
    if (!storage_initialized) {
        LOG_ERROR("FS", "Sincronizacao global antes da inicializacao");
        return ERR_STATE;
    }
    {
        int result = block_cache_sync_all();

        if (result != OK) LOG_ERROR("FS", "Sync global de storage falhou");
        return result;
    }
}

static int storage_read_fat_bytes(const storage_volume_t* volume,
                                  const storage_mount_t* mount,
                                  uint32_t offset, uint8_t* output,
                                  uint32_t size) {
    uint8_t sector[STORAGE_SECTOR_SIZE];
    uint32_t copied = 0;

    while (copied < size) {
        uint32_t current = offset + copied;
        uint32_t sector_index = current / STORAGE_SECTOR_SIZE;
        uint32_t sector_offset = current % STORAGE_SECTOR_SIZE;
        uint32_t amount = STORAGE_SECTOR_SIZE - sector_offset;
        int result;

        if (sector_index >= mount->sectors_per_fat) {
            storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                               "leitura excedeu a FAT");
            LOG_ERROR("FS", "Offset fora da FAT adicional");
            return ERR_DISK;
        }
        if (amount > size - copied) amount = size - copied;
        result = storage_read_relative(volume, mount->fat_start + sector_index,
                                       1, sector);
        if (result != OK) return result;
        kmemcpy(output + copied, sector + sector_offset, amount);
        copied += amount;
    }
    return OK;
}

static int storage_write_relative(const storage_volume_t* volume,
                                  uint32_t relative_lba,
                                  const uint8_t* buffer) {
    uint32_t absolute_lba;

    if (!volume || !buffer) {
        LOG_ERROR("FS", "Argumento invalido na escrita de volume");
        return ERR_NULL;
    }
    if (volume->read_only || relative_lba >= volume->sector_count) {
        storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                           volume->read_only ?
                           "volume somente-leitura" :
                           "escrita fora dos limites");
        return volume->read_only ? ERR_UNAVAILABLE : ERR_DISK;
    }
    if (volume->start_lba > 0xFFFFFFFFU - relative_lba) {
        storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                           "overflow no endereco de escrita");
        return ERR_OVERFLOW;
    }
    absolute_lba = volume->start_lba + relative_lba;
    return block_write(volume->disk_id, absolute_lba, 1, buffer);
}

static int storage_read_fat32_entry(const storage_volume_t* volume,
                                    const storage_mount_t* mount,
                                    uint32_t cluster, uint32_t* out_value) {
    uint8_t bytes[4];
    int result;

    if (!volume || !mount || !out_value) {
        LOG_ERROR("FS", "Argumento nulo na leitura de FAT32");
        return ERR_NULL;
    }
    if (mount->fs_type != STORAGE_FS_FAT32 ||
        cluster < STORAGE_FIRST_DATA_CLUSTER ||
        cluster >= mount->total_clusters + STORAGE_FIRST_DATA_CLUSTER) {
        return ERR_INVALID;
    }
    result = storage_read_fat_bytes(volume, mount,
                                    cluster * STORAGE_FAT32_ENTRY_SIZE,
                                    bytes, sizeof(bytes));
    if (result == OK) *out_value = storage_read_u32(bytes) & 0x0FFFFFFFU;
    return result;
}

static int storage_write_fat32_entry(const storage_volume_t* volume,
                                     const storage_mount_t* mount,
                                     uint32_t cluster, uint32_t value) {
    uint8_t sector[STORAGE_SECTOR_SIZE];
    uint32_t offset;

    if (!volume || !mount) {
        LOG_ERROR("FS", "Argumento nulo na escrita de FAT32");
        return ERR_NULL;
    }
    if (mount->fs_type != STORAGE_FS_FAT32 || volume->read_only ||
        cluster < STORAGE_FIRST_DATA_CLUSTER ||
        cluster >= mount->total_clusters + STORAGE_FIRST_DATA_CLUSTER) {
        storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                           "entrada FAT32 nao pode ser escrita");
        return volume->read_only ? ERR_UNAVAILABLE : ERR_INVALID;
    }
    offset = cluster * STORAGE_FAT32_ENTRY_SIZE;
    for (uint8_t copy = 0; copy < mount->fat_count; copy++) {
        int result = storage_read_relative(
            volume, mount->fat_start + copy * mount->sectors_per_fat +
                offset / STORAGE_SECTOR_SIZE, 1, sector);
        if (result != OK) return result;
        uint32_t sector_offset = offset % STORAGE_SECTOR_SIZE;
        uint32_t old = storage_read_u32(sector + sector_offset);
        uint32_t next = (old & 0xF0000000U) | (value & 0x0FFFFFFFU);
        sector[sector_offset] = (uint8_t)next;
        sector[sector_offset + 1U] = (uint8_t)(next >> 8);
        sector[sector_offset + 2U] = (uint8_t)(next >> 16);
        sector[sector_offset + 3U] = (uint8_t)(next >> 24);
        result = storage_write_relative(
            volume, mount->fat_start + copy * mount->sectors_per_fat +
                offset / STORAGE_SECTOR_SIZE, sector);
        if (result != OK) return result;
    }
    return OK;
}

static int storage_allocate_fat32_cluster(const storage_volume_t* volume,
                                          storage_mount_t* mount,
                                          uint32_t* out_cluster) {
    int volume_index;
    uint32_t start;
    uint32_t count = mount->total_clusters;

    if (!volume || !mount || !out_cluster) {
        LOG_ERROR("FS", "Argumento nulo na reserva de cluster");
        return ERR_NULL;
    }
    volume_index = storage_volume_index(volume->id);
    start = volume_index >= 0 &&
            storage_fat32_allocation_hint[volume_index] ?
            storage_fat32_allocation_hint[volume_index] : mount->root_cluster;
    if (start < STORAGE_FIRST_DATA_CLUSTER ||
        start >= mount->total_clusters + STORAGE_FIRST_DATA_CLUSTER) {
        start = STORAGE_FIRST_DATA_CLUSTER;
    }
    for (uint32_t pass = 0; pass < 2U; pass++) {
        uint32_t first = pass ? STORAGE_FIRST_DATA_CLUSTER : start;
        uint32_t last = pass ? start : mount->total_clusters + STORAGE_FIRST_DATA_CLUSTER;
        for (uint32_t cluster = first; cluster < last && count; cluster++, count--) {
            uint32_t value;
            int result = storage_read_fat32_entry(volume, mount, cluster, &value);
            if (result != OK) return result;
            if (value == 0U) {
                result = storage_write_fat32_entry(volume, mount, cluster,
                                                   STORAGE_FAT32_END);
                if (result == OK) {
                    *out_cluster = cluster;
                    if (volume_index >= 0) {
                        storage_fat32_allocation_hint[volume_index] =
                            cluster + 1U < mount->total_clusters +
                            STORAGE_FIRST_DATA_CLUSTER ? cluster + 1U :
                            STORAGE_FIRST_DATA_CLUSTER;
                    }
                }
                return result;
            }
        }
    }
    storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                       "espaco insuficiente na FAT32");
    return ERR_OVERFLOW;
}

static int storage_release_fat32_chain(const storage_volume_t* volume,
                                       const storage_mount_t* mount,
                                       uint32_t first_cluster) {
    uint32_t cluster = first_cluster;

    if (!first_cluster) return OK;
    for (uint32_t step = 0; step < STORAGE_MAX_CHAIN_STEPS; step++) {
        uint32_t next;
        int result = storage_read_fat32_entry(volume, mount, cluster, &next);
        if (result != OK) return result;
        result = storage_write_fat32_entry(volume, mount, cluster,
                                           STORAGE_FAT32_FREE);
        if (result != OK) return result;
        if (storage_cluster_is_end(mount, next)) return OK;
        if (storage_cluster_is_bad(mount, next) ||
            next < STORAGE_FIRST_DATA_CLUSTER) return ERR_DISK;
        cluster = next;
    }
    return ERR_OVERFLOW;
}

static int storage_write_cluster(const storage_volume_t* volume,
                                 const storage_mount_t* mount,
                                 uint32_t cluster, const uint8_t* data,
                                 uint32_t size) {
    uint8_t sector[STORAGE_SECTOR_SIZE];
    uint32_t base;

    if (!volume || !mount || !data || size > mount->sectors_per_cluster *
        STORAGE_SECTOR_SIZE) {
        LOG_ERROR("FS", "Argumento invalido na escrita de cluster");
        return ERR_NULL;
    }
    base = mount->data_start + (cluster - STORAGE_FIRST_DATA_CLUSTER) *
           mount->sectors_per_cluster;
    for (uint32_t index = 0; index < mount->sectors_per_cluster; index++) {
        kmemset(sector, 0, sizeof(sector));
        if (index * STORAGE_SECTOR_SIZE < size) {
            uint32_t amount = size - index * STORAGE_SECTOR_SIZE;
            if (amount > STORAGE_SECTOR_SIZE) amount = STORAGE_SECTOR_SIZE;
            kmemcpy(sector, data + index * STORAGE_SECTOR_SIZE, amount);
        }
        int result = storage_write_relative(volume, base + index, sector);
        if (result != OK) return result;
    }
    return OK;
}

static void storage_build_directory_marker(uint8_t entry[STORAGE_DIR_ENTRY_SIZE],
                                           const char* name,
                                           uint32_t cluster) {
    kmemset(entry, 0, STORAGE_DIR_ENTRY_SIZE);
    kmemcpy(entry, name, 11U);
    entry[STORAGE_DIR_ATTRIBUTE_OFFSET] = STORAGE_ATTR_DIRECTORY;
    entry[STORAGE_DIR_CLUSTER_HIGH_OFFSET] = (uint8_t)(cluster >> 16);
    entry[STORAGE_DIR_CLUSTER_HIGH_OFFSET + 1U] =
        (uint8_t)(cluster >> 24);
    entry[STORAGE_DIR_CLUSTER_LOW_OFFSET] = (uint8_t)cluster;
    entry[STORAGE_DIR_CLUSTER_LOW_OFFSET + 1U] = (uint8_t)(cluster >> 8);
}

static int storage_next_cluster(const storage_volume_t* volume,
                                const storage_mount_t* mount,
                                uint32_t cluster, uint32_t* out_next) {
    uint8_t bytes[4];
    uint32_t offset;
    int result;

    if (!out_next) {
        LOG_ERROR("FS", "Destino nulo ao percorrer cadeia FAT");
        return ERR_NULL;
    }
    if (cluster < STORAGE_FIRST_DATA_CLUSTER ||
        cluster >= mount->total_clusters + STORAGE_FIRST_DATA_CLUSTER) {
        return ERR_INVALID;
    }
    if (mount->fs_type == STORAGE_FS_FAT12) {
        uint16_t value;

        offset = cluster + cluster / 2U;
        result = storage_read_fat_bytes(volume, mount, offset, bytes, 2);
        if (result != OK) return result;
        value = storage_read_u16(bytes);
        *out_next = cluster & 1U ? value >> 4 : value & 0x0FFFU;
        return OK;
    }
    if (cluster > 0xFFFFFFFFU / STORAGE_FAT32_ENTRY_SIZE) {
        return ERR_OVERFLOW;
    }
    offset = cluster * STORAGE_FAT32_ENTRY_SIZE;
    result = storage_read_fat_bytes(volume, mount, offset, bytes,
                                    STORAGE_FAT32_ENTRY_SIZE);
    if (result != OK) return result;
    *out_next = storage_read_u32(bytes) & 0x0FFFFFFFU;
    return OK;
}

static int storage_cluster_is_end(const storage_mount_t* mount,
                                  uint32_t cluster) {
    if (mount->fs_type == STORAGE_FS_FAT12) return cluster >= STORAGE_FAT12_END;
    return cluster >= STORAGE_FAT32_END;
}

static int storage_cluster_is_bad(const storage_mount_t* mount,
                                  uint32_t cluster) {
    if (mount->fs_type == STORAGE_FS_FAT12) return cluster == STORAGE_FAT12_BAD;
    return cluster == STORAGE_FAT32_BAD;
}

int storage_get_free_space(const char* id, uint32_t* out_free_sectors,
                           uint32_t* out_free_clusters) {
    storage_volume_t* volume;
    storage_mount_t* mount;
    uint32_t free_clusters = 0U;
    uint32_t free_sectors;
    int index;
    int result = OK;

    if (!id || !out_free_sectors || !out_free_clusters) {
        LOG_ERROR("FS", "Argumento nulo na consulta de espaco livre");
        return ERR_NULL;
    }
    spinlock_acquire(&storage_operation_lock);
    index = storage_initialized ? storage_volume_index(id) : -1;
    if (index < 0 || !storage_volumes[index].mounted) {
        result = index < 0 ? ERR_NOT_FOUND : ERR_STATE;
    }
    volume = index >= 0 ? &storage_volumes[index] : 0;
    mount = index >= 0 ? storage_mount_for_volume((uint8_t)index) : 0;
    if (result == OK && !mount) result = ERR_STATE;
    if (result == OK && mount->fs_type != STORAGE_FS_FAT12 &&
        mount->fs_type != STORAGE_FS_FAT32) result = ERR_UNAVAILABLE;
    if (result == OK) {
        for (uint32_t cluster = STORAGE_FIRST_DATA_CLUSTER;
             cluster < mount->total_clusters + STORAGE_FIRST_DATA_CLUSTER;
             cluster++) {
            uint32_t value;

            result = storage_next_cluster(volume, mount, cluster, &value);
            if (result != OK) break;
            if (value == 0U) free_clusters++;
        }
    }
    if (result == OK) {
        if (free_clusters > 0xFFFFFFFFU / mount->sectors_per_cluster) {
            result = ERR_OVERFLOW;
        } else {
            free_sectors = free_clusters * mount->sectors_per_cluster;
            *out_free_clusters = free_clusters;
            *out_free_sectors = free_sectors;
        }
    }
    spinlock_release(&storage_operation_lock);
    if (result != OK) {
        storage_log_volume(LOG_LEVEL_ERROR, id,
                           "falha ao consultar espaco livre");
        return result;
    }
    return OK;
}

static void storage_parse_raw_entry(const storage_mount_t* mount,
                                    const uint8_t* source,
                                    storage_raw_entry_t* entry) {
    kmemcpy(entry->name, source, sizeof(entry->name));
    entry->attributes = source[STORAGE_DIR_ATTRIBUTE_OFFSET];
    entry->first_cluster = storage_read_u16(
        source + STORAGE_DIR_CLUSTER_LOW_OFFSET);
    if (mount->fs_type == STORAGE_FS_FAT32) {
        entry->first_cluster |= (uint32_t)storage_read_u16(
            source + STORAGE_DIR_CLUSTER_HIGH_OFFSET) << 16;
    }
    entry->size = storage_read_u32(source + STORAGE_DIR_SIZE_OFFSET);
}

static uint8_t storage_lfn_checksum(const uint8_t alias[11]) {
    uint8_t checksum = 0;

    for (uint32_t index = 0; index < 11U; index++) {
        checksum = (uint8_t)(((checksum & 1U) << 7) +
                             (checksum >> 1) + alias[index]);
    }
    return checksum;
}

static void storage_lfn_reset(storage_lfn_state_t* state) {
    if (!state) return;
    kmemset(state, 0, sizeof(*state));
}

static void storage_lfn_copy_units(storage_lfn_state_t* state,
                                   const uint8_t* source) {
    static const uint8_t positions[] = { 1U, 14U, 28U };
    static const uint8_t counts[] = { 5U, 6U, 2U };
    uint32_t output = ((source[0] & 0x1FU) - 1U) * 13U;

    for (uint32_t group = 0; group < 3U; group++) {
        for (uint32_t index = 0; index < counts[group]; index++) {
            uint32_t target = output++;
            if (target < STORAGE_LONG_NAME_SIZE) {
                state->units[target] = storage_read_u16(
                    source + positions[group] + index * 2U);
                if (target + 1U > state->highest_unit) {
                    state->highest_unit = (uint16_t)(target + 1U);
                }
            }
        }
    }
}

static int storage_lfn_append_utf8(char* output, uint32_t capacity,
                                   uint32_t* offset, uint32_t codepoint) {
    uint8_t bytes[4];
    uint32_t count;

    if (!output || !offset || !capacity || codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
        return ERR_INVALID;
    }
    if (codepoint <= 0x7FU) {
        bytes[0] = (uint8_t)codepoint;
        count = 1U;
    } else if (codepoint <= 0x7FFU) {
        bytes[0] = (uint8_t)(0xC0U | (codepoint >> 6));
        bytes[1] = (uint8_t)(0x80U | (codepoint & 0x3FU));
        count = 2U;
    } else if (codepoint <= 0xFFFFU) {
        bytes[0] = (uint8_t)(0xE0U | (codepoint >> 12));
        bytes[1] = (uint8_t)(0x80U | ((codepoint >> 6) & 0x3FU));
        bytes[2] = (uint8_t)(0x80U | (codepoint & 0x3FU));
        count = 3U;
    } else {
        bytes[0] = (uint8_t)(0xF0U | (codepoint >> 18));
        bytes[1] = (uint8_t)(0x80U | ((codepoint >> 12) & 0x3FU));
        bytes[2] = (uint8_t)(0x80U | ((codepoint >> 6) & 0x3FU));
        bytes[3] = (uint8_t)(0x80U | (codepoint & 0x3FU));
        count = 4U;
    }
    if (*offset + count + 1U > capacity) return ERR_OVERFLOW;
    for (uint32_t index = 0; index < count; index++) {
        output[(*offset)++] = (char)bytes[index];
    }
    output[*offset] = '\0';
    return OK;
}

static int storage_lfn_to_utf8(const storage_lfn_state_t* state,
                               char* output, uint32_t capacity) {
    uint32_t offset = 0U;

    if (!state || !output || capacity < 2U) return ERR_NULL;
    output[0] = '\0';
    for (uint32_t index = 0; index < state->highest_unit; index++) {
        uint32_t codepoint = state->units[index];
        if (codepoint == 0U || codepoint == 0xFFFFU) break;
        if (codepoint >= 0xD800U && codepoint <= 0xDBFFU) {
            uint32_t next = index + 1U;
            if (next >= state->highest_unit ||
                state->units[next] < 0xDC00U ||
                state->units[next] > 0xDFFFU) return ERR_INVALID;
            codepoint = 0x10000U + ((codepoint - 0xD800U) << 10) +
                        (state->units[next] - 0xDC00U);
            index = next;
        }
        if (storage_lfn_append_utf8(output, capacity, &offset, codepoint) != OK) {
            return ERR_INVALID;
        }
    }
    return offset ? OK : ERR_INVALID;
}

static int storage_utf8_to_utf16(const char* input, uint16_t* units,
                                 uint32_t capacity, uint32_t* out_count) {
    uint32_t offset = 0U;
    uint32_t index = 0U;

    if (!input || !units || !out_count) return ERR_NULL;
    while (input[index]) {
        uint32_t codepoint;
        uint8_t first = (uint8_t)input[index++];
        uint32_t extra;

        if (first < 0x80U) {
            codepoint = first;
            extra = 0U;
        } else if (first >= 0xC2U && first <= 0xDFU) {
            codepoint = first & 0x1FU;
            extra = 1U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            codepoint = first & 0x0FU;
            extra = 2U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            codepoint = first & 0x07U;
            extra = 3U;
        } else {
            return ERR_INVALID;
        }
        for (uint32_t count = 0; count < extra; count++) {
            uint8_t next = (uint8_t)input[index++];
            if ((next & 0xC0U) != 0x80U) return ERR_INVALID;
            codepoint = (codepoint << 6) | (next & 0x3FU);
        }
        if ((codepoint > 0x10FFFFU) ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) return ERR_INVALID;
        if (codepoint <= 0xFFFFU) {
            if (offset >= capacity) return ERR_OVERFLOW;
            units[offset++] = (uint16_t)codepoint;
        } else {
            if (offset + 1U >= capacity) return ERR_OVERFLOW;
            codepoint -= 0x10000U;
            units[offset++] = (uint16_t)(0xD800U | (codepoint >> 10));
            units[offset++] = (uint16_t)(0xDC00U | (codepoint & 0x3FFU));
        }
    }
    *out_count = offset;
    return OK;
}

static void storage_build_lfn_entry(uint8_t output[STORAGE_DIR_ENTRY_SIZE],
                                    uint8_t sequence,
                                    const uint16_t* units, uint32_t count,
                                    uint8_t checksum) {
    static const uint8_t positions[] = { 1U, 14U, 28U };
    static const uint8_t lengths[] = { 5U, 6U, 2U };
    uint32_t offset = ((uint32_t)(sequence & 0x1FU) - 1U) * 13U;

    kmemset(output, 0xFF, STORAGE_DIR_ENTRY_SIZE);
    output[0] = sequence;
    output[11] = STORAGE_ATTR_LFN;
    output[13] = checksum;
    for (uint32_t group = 0; group < 3U; group++) {
        for (uint32_t index = 0; index < lengths[group]; index++) {
            uint16_t value = 0xFFFFU;
            uint32_t unit = offset++;
            if (unit < count) value = units[unit];
            else if (unit == count) value = 0U;
            output[positions[group] + index * 2U] = (uint8_t)value;
            output[positions[group] + index * 2U + 1U] =
                (uint8_t)(value >> 8);
        }
    }
}

static void storage_long_parse_entry(const storage_mount_t* mount,
                                     const uint8_t* source, uint32_t offset,
                                     const storage_lfn_state_t* lfn,
                                     storage_long_raw_entry_t* entry) {
    storage_raw_entry_t raw;

    storage_parse_raw_entry(mount, source, &raw);
    kmemset(entry, 0, sizeof(*entry));
    storage_copy_text(entry->short_name, STORAGE_NAME_SIZE, "");
    for (uint32_t index = 0; index < 8U && raw.name[index] != ' '; index++) {
        uint32_t length = kstrlen(entry->short_name);
        if (length + 1U < STORAGE_NAME_SIZE) {
            entry->short_name[length] = (char)raw.name[index];
            entry->short_name[length + 1U] = '\0';
        }
    }
    if (raw.name[8] != ' ') {
        uint32_t length = kstrlen(entry->short_name);
        if (length + 1U < STORAGE_NAME_SIZE) {
            entry->short_name[length++] = '.';
            entry->short_name[length] = '\0';
        }
        for (uint32_t index = 8U; index < 11U && raw.name[index] != ' '; index++) {
            length = kstrlen(entry->short_name);
            if (length + 1U < STORAGE_NAME_SIZE) {
                entry->short_name[length] = (char)raw.name[index];
                entry->short_name[length + 1U] = '\0';
            }
        }
    }
    if (!lfn || storage_lfn_to_utf8(lfn, entry->name,
                                    STORAGE_LONG_NAME_SIZE) != OK) {
        storage_copy_text(entry->name, STORAGE_LONG_NAME_SIZE,
                          entry->short_name);
    }
    entry->attributes = raw.attributes;
    entry->first_cluster = raw.first_cluster;
    entry->size = raw.size;
    entry->entry_offset = offset;
    entry->lfn_offset = lfn && lfn->active ? lfn->offset : offset;
    if (lfn && lfn->active) {
        entry->lfn_count = lfn->offset_count;
        kmemcpy(entry->lfn_offsets, lfn->offsets,
                sizeof(entry->lfn_offsets));
    }
}

static int storage_visit_sector(const storage_mount_t* mount,
                                const uint8_t* sector,
                                storage_entry_visitor_t visitor,
                                void* context, int* out_end) {
    if (!visitor || !out_end) {
        LOG_ERROR("FS", "Visitante nulo ao ler diretorio FAT");
        return ERR_NULL;
    }
    for (uint32_t offset = 0; offset < STORAGE_SECTOR_SIZE;
         offset += STORAGE_DIR_ENTRY_SIZE) {
        const uint8_t* source = sector + offset;
        storage_raw_entry_t entry;

        if (source[0] == 0x00U) {
            *out_end = 1;
            return OK;
        }
        if (source[0] == 0xE5U ||
            source[STORAGE_DIR_ATTRIBUTE_OFFSET] == STORAGE_ATTR_LFN ||
            (source[STORAGE_DIR_ATTRIBUTE_OFFSET] & STORAGE_ATTR_VOLUME)) {
            continue;
        }
        storage_parse_raw_entry(mount, source, &entry);
        visitor(&entry, context);
    }
    return OK;
}

static int storage_walk_directory(const storage_volume_t* volume,
                                  const storage_mount_t* mount,
                                  uint32_t cluster, uint8_t fixed_root,
                                  storage_entry_visitor_t visitor,
                                  void* context) {
    uint8_t sector[STORAGE_SECTOR_SIZE];
    int end = 0;

    if (!volume || !mount || !visitor) {
        LOG_ERROR("FS", "Argumento nulo ao percorrer diretorio");
        return ERR_NULL;
    }

    if (fixed_root) {
        for (uint32_t index = 0; index < mount->root_sectors && !end; index++) {
            int result = storage_read_relative(volume, mount->root_start + index,
                                               1, sector);
            if (result != OK) return result;
            result = storage_visit_sector(mount, sector, visitor, context, &end);
            if (result != OK) return result;
        }
        return OK;
    }

    for (uint32_t step = 0; step < STORAGE_MAX_CHAIN_STEPS; step++) {
        uint32_t relative_lba;
        uint32_t next;

        if (cluster < STORAGE_FIRST_DATA_CLUSTER ||
            cluster >= mount->total_clusters + STORAGE_FIRST_DATA_CLUSTER) {
            return ERR_INVALID;
        }
        relative_lba = mount->data_start +
                       (cluster - STORAGE_FIRST_DATA_CLUSTER) *
                       mount->sectors_per_cluster;
        for (uint32_t sector_index = 0;
             sector_index < mount->sectors_per_cluster && !end;
             sector_index++) {
            int result = storage_read_relative(volume,
                                               relative_lba + sector_index,
                                               1, sector);
            if (result != OK) return result;
            result = storage_visit_sector(mount, sector, visitor, context, &end);
            if (result != OK) return result;
        }
        if (end) return OK;
        if (storage_next_cluster(volume, mount, cluster, &next) != OK) {
            return ERR_DISK;
        }
        if (storage_cluster_is_bad(mount, next)) return ERR_DISK;
        if (storage_cluster_is_end(mount, next)) return OK;
        cluster = next;
    }
    return ERR_OVERFLOW;
}

static int storage_visit_long_sector(const storage_mount_t* mount,
                                     const uint8_t* sector, uint32_t base_offset,
                                     storage_lfn_state_t* lfn,
                                     storage_long_entry_visitor_t visitor,
                                     void* context, int* out_end) {
    if (!mount || !sector || !lfn || !visitor || !out_end) {
        LOG_ERROR("FS", "Argumento invalido no leitor LFN");
        return ERR_NULL;
    }
    for (uint32_t offset = 0; offset < STORAGE_SECTOR_SIZE;
         offset += STORAGE_DIR_ENTRY_SIZE) {
        const uint8_t* source = sector + offset;
        uint8_t attributes = source[STORAGE_DIR_ATTRIBUTE_OFFSET];

        if (source[0] == 0x00U) {
            *out_end = 1;
            return OK;
        }
        if (source[0] == 0xE5U) {
            storage_lfn_reset(lfn);
            continue;
        }
        if (attributes == STORAGE_ATTR_LFN) {
            uint8_t sequence = source[0] & 0x1FU;
            if (!sequence || sequence > STORAGE_MAX_LFN_ENTRIES ||
                (!(source[0] & 0x40U) &&
                 (!lfn->active || lfn->expected_sequence != sequence ||
                  lfn->checksum != source[13]))) {
                storage_lfn_reset(lfn);
                LOG_ERROR("FS", "Sequencia LFN FAT32 invalida");
                return ERR_INVALID;
            }
            if (source[0] & 0x40U) {
                storage_lfn_reset(lfn);
                lfn->active = 1U;
                lfn->expected_sequence = sequence;
                lfn->checksum = source[13];
                lfn->offset = base_offset + offset;
            }
            if (lfn->offset_count >= STORAGE_MAX_LFN_ENTRIES) {
                storage_lfn_reset(lfn);
                LOG_ERROR("FS", "Quantidade de entradas LFN FAT32 excedida");
                return ERR_OVERFLOW;
            }
            lfn->offsets[lfn->offset_count++] = base_offset + offset;
            storage_lfn_copy_units(lfn, source);
            if (lfn->expected_sequence > 0U) lfn->expected_sequence--;
            continue;
        }
        if (attributes & STORAGE_ATTR_VOLUME) {
            storage_lfn_reset(lfn);
            continue;
        }
        storage_long_raw_entry_t entry;
        storage_lfn_state_t valid_lfn = *lfn;
        uint8_t alias[11];
        kmemcpy(alias, source, sizeof(alias));
        if (!valid_lfn.active || valid_lfn.expected_sequence != 0U ||
            valid_lfn.checksum != storage_lfn_checksum(alias)) {
            if (valid_lfn.active) {
                LOG_ERROR("FS", "Checksum LFN FAT32 invalido");
                return ERR_INVALID;
            }
            storage_lfn_reset(&valid_lfn);
        }
        if (valid_lfn.active) {
            char validated_name[STORAGE_LONG_NAME_SIZE];
            if (storage_lfn_to_utf8(&valid_lfn, validated_name,
                                    sizeof(validated_name)) != OK) {
                LOG_ERROR("FS", "Conteudo LFN FAT32 invalido");
                return ERR_INVALID;
            }
        }
        storage_long_parse_entry(mount, source, base_offset + offset,
                                 &valid_lfn, &entry);
        storage_lfn_reset(lfn);
        int result = visitor(&entry, context);
        if (result != OK) return result;
    }
    return OK;
}

static int storage_walk_directory_long(
    const storage_volume_t* volume, const storage_mount_t* mount,
    uint32_t cluster, uint8_t fixed_root,
    storage_long_entry_visitor_t visitor, void* context) {
    uint8_t sector[STORAGE_SECTOR_SIZE];
    storage_lfn_state_t lfn;
    int end = 0;

    if (!volume || !mount || !visitor) {
        LOG_ERROR("FS", "Argumento nulo ao percorrer diretorio LFN");
        return ERR_NULL;
    }
    storage_lfn_reset(&lfn);
    if (fixed_root) {
        for (uint32_t index = 0; index < mount->root_sectors && !end; index++) {
            int result = storage_read_relative(volume, mount->root_start + index,
                                               1, sector);
            if (result != OK) return result;
            result = storage_visit_long_sector(
                mount, sector,
                (volume->start_lba + mount->root_start + index) *
                    STORAGE_SECTOR_SIZE,
                &lfn, visitor, context, &end);
            if (result != OK) return result;
        }
        return OK;
    }
    for (uint32_t step = 0; step < STORAGE_MAX_CHAIN_STEPS && !end; step++) {
        uint32_t relative_lba;
        uint32_t next;
        if (cluster < STORAGE_FIRST_DATA_CLUSTER ||
            cluster >= mount->total_clusters + STORAGE_FIRST_DATA_CLUSTER) {
            return ERR_INVALID;
        }
        relative_lba = mount->data_start +
                       (cluster - STORAGE_FIRST_DATA_CLUSTER) *
                       mount->sectors_per_cluster;
        for (uint32_t sector_index = 0;
             sector_index < mount->sectors_per_cluster && !end; sector_index++) {
            int result = storage_read_relative(volume,
                                               relative_lba + sector_index,
                                               1, sector);
            if (result != OK) return result;
            result = storage_visit_long_sector(
                mount, sector,
                (volume->start_lba + relative_lba + sector_index) *
                    STORAGE_SECTOR_SIZE,
                &lfn, visitor, context, &end);
            if (result != OK) return result;
        }
        if (end) return OK;
        if (storage_next_cluster(volume, mount, cluster, &next) != OK) {
            return ERR_DISK;
        }
        if (storage_cluster_is_bad(mount, next)) return ERR_DISK;
        if (storage_cluster_is_end(mount, next)) return OK;
        cluster = next;
    }
    return end ? OK : ERR_OVERFLOW;
}

static char storage_upper(char value) {
    if (value >= 'a' && value <= 'z') return (char)(value - ('a' - 'A'));
    return value;
}

static int storage_name_to_fat(const char* name, uint8_t output[11]) {
    uint32_t base = 0;
    uint32_t extension = 0;
    int in_extension = 0;

    if (!name || !*name || !output) {
        LOG_ERROR("FS", "Nome 8.3 invalido ou destino nulo");
        return ERR_INVALID;
    }
    for (uint32_t index = 0; index < 11U; index++) output[index] = ' ';
    while (*name) {
        char value = *name++;

        if (value == '.') {
            if (in_extension || base == 0) return ERR_INVALID;
            in_extension = 1;
            continue;
        }
        if (value == '/' || value == '\\' || value == ' ') return ERR_INVALID;
        if (!in_extension) {
            if (base >= 8U) return ERR_OVERFLOW;
            output[base++] = (uint8_t)storage_upper(value);
        } else {
            if (extension >= 3U) return ERR_OVERFLOW;
            output[8U + extension++] = (uint8_t)storage_upper(value);
        }
    }
    return base ? OK : ERR_INVALID;
}

typedef struct {
    uint8_t alias[11];
    uint8_t found;
} storage_alias_context_t;

static int storage_alias_visitor(const storage_long_raw_entry_t* entry,
                                 void* context) {
    storage_alias_context_t* aliases = (storage_alias_context_t*)context;
    uint8_t current[11];

    if (!entry || !aliases) return ERR_NULL;
    if (storage_name_to_fat(entry->short_name, current) == OK) {
        for (uint32_t index = 0; index < sizeof(current); index++) {
            if (current[index] != aliases->alias[index]) return OK;
        }
        aliases->found = 1U;
    }
    return OK;
}

static int storage_alias_is_used(const storage_volume_t* volume,
                                 const storage_mount_t* mount,
                                 uint32_t cluster, uint8_t fixed_root,
                                 const uint8_t alias[11]) {
    storage_alias_context_t context;

    kmemset(&context, 0, sizeof(context));
    kmemcpy(context.alias, alias, sizeof(context.alias));
    if (storage_walk_directory_long(volume, mount, cluster, fixed_root,
                                    storage_alias_visitor, &context) != OK) {
        return -1;
    }
    return context.found ? 1 : 0;
}

static int storage_generate_alias(const storage_volume_t* volume,
                                  const storage_mount_t* mount,
                                  uint32_t cluster, uint8_t fixed_root,
                                  const char* name, uint8_t alias[11]) {
    uint8_t candidate[11];
    char stem[64];
    char extension[4];
    uint32_t stem_length = 0U;
    uint32_t extension_length = 0U;
    const char* dot = 0;

    if (!name || !alias) return ERR_NULL;
    if (storage_name_to_fat(name, candidate) == OK &&
        storage_alias_is_used(volume, mount, cluster, fixed_root, candidate) == 0) {
        kmemcpy(alias, candidate, sizeof(candidate));
        return OK;
    }
    for (const char* cursor = name; *cursor; cursor++) {
        if (*cursor == '.') dot = cursor;
    }
    for (const char* cursor = name; *cursor && cursor != dot &&
         stem_length + 1U < sizeof(stem); cursor++) {
        char value = storage_upper(*cursor);
        if (value >= 'A' && value <= 'Z') stem[stem_length++] = value;
        else if (value >= '0' && value <= '9') stem[stem_length++] = value;
    }
    if (!stem_length) {
        stem[0] = 'F'; stem[1] = 'I'; stem[2] = 'L'; stem[3] = 'E';
        stem_length = 4U;
    }
    stem[stem_length] = '\0';
    if (dot) {
        for (const char* cursor = dot + 1; *cursor &&
             extension_length < sizeof(extension) - 1U; cursor++) {
            char value = storage_upper(*cursor);
            if ((value >= 'A' && value <= 'Z') ||
                (value >= '0' && value <= '9')) {
                extension[extension_length++] = value;
            }
        }
    }
    extension[extension_length] = '\0';
    for (uint32_t number = 1U; number < 10000U; number++) {
        char digits[6];
        char suffix[8];
        uint32_t value = number;
        uint32_t digit_count = 0U;
        uint32_t suffix_length;
        kmemset(candidate, ' ', sizeof(candidate));
        suffix[0] = '~';
        do {
            digits[digit_count++] = (char)('0' + (value % 10U));
            value /= 10U;
        } while (value && digit_count < sizeof(digits));
        suffix_length = digit_count + 1U;
        for (uint32_t index = 0; index < digit_count; index++) {
            suffix[suffix_length - index - 1U] = digits[index];
        }
        if (suffix_length > sizeof(suffix)) continue;
        uint32_t prefix = 8U - suffix_length;
        if (prefix > stem_length) prefix = stem_length;
        for (uint32_t index = 0; index < prefix; index++) candidate[index] = (uint8_t)stem[index];
        for (uint32_t index = 0; index < suffix_length; index++) {
            candidate[prefix + index] = (uint8_t)suffix[index];
        }
        for (uint32_t index = 0; index < extension_length; index++) {
            candidate[8U + index] = (uint8_t)extension[index];
        }
        if (storage_alias_is_used(volume, mount, cluster, fixed_root,
                                  candidate) == 0) {
            kmemcpy(alias, candidate, sizeof(candidate));
            return OK;
        }
    }
    return ERR_OVERFLOW;
}

typedef struct {
    char wanted[STORAGE_LONG_NAME_SIZE];
    storage_long_raw_entry_t entry;
    uint8_t found;
} storage_long_find_context_t;

static int storage_find_long_visitor(const storage_long_raw_entry_t* entry,
                                     void* context) {
    storage_long_find_context_t* find = (storage_long_find_context_t*)context;

    if (!entry || !find) return ERR_NULL;
    if (!find->found &&
        (storage_text_equal(entry->name, find->wanted) ||
         storage_text_equal(entry->short_name, find->wanted))) {
        find->entry = *entry;
        find->found = 1U;
    }
    return OK;
}

static int storage_find_entry_long(const storage_volume_t* volume,
                                   const storage_mount_t* mount,
                                   uint32_t directory_cluster,
                                   uint8_t fixed_root, const char* name,
                                   storage_long_raw_entry_t* output) {
    storage_long_find_context_t context;
    int result;

    if (!volume || !mount || !name || !output) {
        LOG_ERROR("FS", "Argumento nulo na busca LFN");
        return ERR_NULL;
    }
    if (kstrlen(name) >= STORAGE_LONG_NAME_SIZE) return ERR_OVERFLOW;
    kmemset(&context, 0, sizeof(context));
    storage_copy_text(context.wanted, STORAGE_LONG_NAME_SIZE, name);
    result = storage_walk_directory_long(volume, mount, directory_cluster,
                                         fixed_root, storage_find_long_visitor,
                                         &context);
    if (result != OK) return result;
    if (!context.found) return ERR_NOT_FOUND;
    *output = context.entry;
    return OK;
}

static int storage_next_component(const char** cursor, char* component,
                                  uint32_t capacity) {
    const char* input;
    uint32_t length = 0;

    if (!cursor || !*cursor || !component || capacity < 2U) {
        LOG_ERROR("FS", "Destino invalido ao separar caminho");
        return ERR_NULL;
    }
    input = *cursor;
    while (*input == '/' || *input == '\\') input++;
    if (!*input) {
        component[0] = '\0';
        *cursor = input;
        return OK;
    }
    while (*input && *input != '/' && *input != '\\') {
        if (length + 1U >= capacity) return ERR_OVERFLOW;
        component[length++] = *input++;
    }
    component[length] = '\0';
    *cursor = input;
    return OK;
}

static int storage_resolve_directory(const storage_volume_t* volume,
                                     const storage_mount_t* mount,
                                     const char* path, uint32_t* out_cluster,
                                     uint8_t* out_fixed_root) {
    const char* cursor = path ? path : "";
    uint32_t cluster;
    uint8_t fixed_root;

    if (!volume || !mount || !out_cluster || !out_fixed_root) {
        LOG_ERROR("FS", "Argumento nulo ao resolver diretorio");
        return ERR_NULL;
    }
    cluster = mount->fs_type == STORAGE_FS_FAT32 ? mount->root_cluster : 0;
    fixed_root = mount->fs_type == STORAGE_FS_FAT12;

    while (1) {
        char component[STORAGE_LONG_NAME_SIZE];
        storage_long_raw_entry_t entry;
        int result = storage_next_component(&cursor, component,
                                            sizeof(component));

        if (result != OK) return result;
        if (!component[0]) break;
        if (component[0] == '.' && component[1] == '\0') continue;
        if (component[0] == '.' && component[1] == '.' &&
            component[2] == '\0') return ERR_INVALID;
        result = storage_find_entry_long(volume, mount, cluster, fixed_root,
                                         component, &entry);
        if (result != OK) return result;
        if (!(entry.attributes & STORAGE_ATTR_DIRECTORY) ||
            entry.first_cluster < STORAGE_FIRST_DATA_CLUSTER) {
            return ERR_INVALID;
        }
        cluster = entry.first_cluster;
        fixed_root = 0;
    }
    *out_cluster = cluster;
    *out_fixed_root = fixed_root;
    return OK;
}

static void storage_entry_name(const storage_raw_entry_t* entry,
                               char name[STORAGE_NAME_SIZE]) {
    uint32_t offset = 0;

    for (uint32_t index = 0; index < 8U && entry->name[index] != ' '; index++) {
        name[offset++] = (char)entry->name[index];
    }
    if (entry->name[8] != ' ') {
        name[offset++] = '.';
        for (uint32_t index = 8U; index < 11U && entry->name[index] != ' ';
             index++) {
            name[offset++] = (char)entry->name[index];
        }
    }
    name[offset] = '\0';
}

typedef struct {
    storage_dir_entry_t* entries;
    uint32_t capacity;
    uint32_t count;
} storage_list_context_t;

static void storage_list_visitor(const storage_raw_entry_t* entry,
                                 void* context) {
    storage_list_context_t* list = (storage_list_context_t*)context;
    storage_dir_entry_t* output;
    char name[STORAGE_NAME_SIZE];

    storage_entry_name(entry, name);
    if ((name[0] == '.' && name[1] == '\0') ||
        (name[0] == '.' && name[1] == '.' && name[2] == '\0')) return;
    if (list->count >= list->capacity) return;
    output = &list->entries[list->count++];
    kmemset(output, 0, sizeof(*output));
    storage_copy_text(output->name, STORAGE_NAME_SIZE, name);
    output->size = entry->size;
    output->cluster = entry->first_cluster;
    output->attributes = entry->attributes;
    output->is_directory = (entry->attributes & STORAGE_ATTR_DIRECTORY) ? 1 : 0;
}

static int storage_split_file_path(const char* path, char* directory,
                                   char* filename) {
    uint32_t length;
    int separator = -1;

    if (!path || !directory || !filename) {
        LOG_ERROR("FS", "Argumento nulo ao separar arquivo");
        return ERR_NULL;
    }
    length = kstrlen(path);
    if (!length || length >= STORAGE_MAX_PATH) return ERR_INVALID;
    for (uint32_t index = 0; index < length; index++) {
        if (path[index] == '/' || path[index] == '\\') separator = index;
    }
    if ((uint32_t)(separator + 1) >= length) return ERR_INVALID;
    if (separator < 0) {
        directory[0] = '\0';
        storage_copy_text(filename, STORAGE_LONG_NAME_SIZE, path);
        return OK;
    }
    for (int index = 0; index < separator; index++) directory[index] = path[index];
    directory[separator] = '\0';
    storage_copy_text(filename, STORAGE_LONG_NAME_SIZE, path + separator + 1);
    return OK;
}

static int storage_find_file(const storage_volume_t* volume,
                             const storage_mount_t* mount, const char* path,
                             storage_raw_entry_t* out_entry) {
    char directory[STORAGE_MAX_PATH];
    char filename[STORAGE_LONG_NAME_SIZE];
    storage_long_raw_entry_t long_entry;
    uint32_t cluster;
    uint8_t fixed_root;
    int result;

    if (!volume || !mount || !path || !out_entry) {
        LOG_ERROR("FS", "Argumento nulo ao localizar arquivo");
        return ERR_NULL;
    }

    result = storage_split_file_path(path, directory, filename);
    if (result != OK) return result;
    result = storage_resolve_directory(volume, mount, directory,
                                       &cluster, &fixed_root);
    if (result != OK) return result;
    result = storage_find_entry_long(volume, mount, cluster, fixed_root,
                                     filename, &long_entry);
    if (result != OK) return result;
    if (long_entry.attributes & STORAGE_ATTR_DIRECTORY) return ERR_INVALID;
    if (storage_name_to_fat(long_entry.short_name, out_entry->name) != OK) {
        return ERR_INVALID;
    }
    out_entry->attributes = long_entry.attributes;
    out_entry->first_cluster = long_entry.first_cluster;
    out_entry->size = long_entry.size;
    return OK;
}

static int storage_find_file_long(const storage_volume_t* volume,
                                  const storage_mount_t* mount,
                                  const char* path,
                                  storage_long_raw_entry_t* out_entry,
                                  uint32_t* out_directory_cluster,
                                  uint8_t* out_fixed_root) {
    char directory[STORAGE_MAX_PATH];
    char filename[STORAGE_LONG_NAME_SIZE];
    uint32_t cluster;
    uint8_t fixed_root;
    int result;

    if (!volume || !mount || !path || !out_entry ||
        !out_directory_cluster || !out_fixed_root) {
        LOG_ERROR("FS", "Argumento nulo na localizacao LFN");
        return ERR_NULL;
    }
    result = storage_split_file_path(path, directory, filename);
    if (result != OK) return result;
    result = storage_resolve_directory(volume, mount, directory,
                                       &cluster, &fixed_root);
    if (result != OK) return result;
    result = storage_find_entry_long(volume, mount, cluster, fixed_root,
                                     filename, out_entry);
    if (result != OK) return result;
    if (out_entry->attributes & STORAGE_ATTR_DIRECTORY) return ERR_INVALID;
    *out_directory_cluster = cluster;
    *out_fixed_root = fixed_root;
    return OK;
}

static int storage_read_entry_range(const storage_volume_t* volume,
                                    const storage_mount_t* mount,
                                    const storage_raw_entry_t* entry,
                                    uint32_t offset, uint8_t* buffer,
                                    uint32_t max_size, uint32_t* out_read) {
    uint8_t sector[STORAGE_SECTOR_SIZE];
    uint32_t cluster;
    uint32_t cluster_bytes;
    uint32_t wanted;
    uint32_t copied = 0;
    uint32_t skip = offset;

    if (!volume || !mount || !entry || !out_read || (max_size && !buffer)) {
        LOG_ERROR("FS", "Argumento nulo na leitura por faixa");
        return ERR_NULL;
    }
    cluster = entry->first_cluster;
    cluster_bytes = mount->sectors_per_cluster * STORAGE_SECTOR_SIZE;

    *out_read = 0;
    if (offset >= entry->size || !max_size) return OK;
    wanted = entry->size - offset;
    if (wanted > max_size) wanted = max_size;
    if (!entry->size) return OK;
    if (cluster < STORAGE_FIRST_DATA_CLUSTER) return ERR_INVALID;

    for (uint32_t step = 0; step < STORAGE_MAX_CHAIN_STEPS; step++) {
        uint32_t relative_lba;
        uint32_t next;

        if (cluster < STORAGE_FIRST_DATA_CLUSTER ||
            cluster >= mount->total_clusters + STORAGE_FIRST_DATA_CLUSTER) {
            return ERR_INVALID;
        }
        if (skip >= cluster_bytes) {
            skip -= cluster_bytes;
        } else {
            relative_lba = mount->data_start +
                           (cluster - STORAGE_FIRST_DATA_CLUSTER) *
                           mount->sectors_per_cluster;
            for (uint32_t sector_index = 0;
                 sector_index < mount->sectors_per_cluster && copied < wanted;
                 sector_index++) {
                uint32_t sector_skip = skip >= STORAGE_SECTOR_SIZE ?
                                       STORAGE_SECTOR_SIZE : skip;
                uint32_t amount;
                int result;

                skip -= sector_skip;
                if (sector_skip == STORAGE_SECTOR_SIZE) continue;
                result = storage_read_relative(volume,
                                               relative_lba + sector_index,
                                               1, sector);
                if (result != OK) return result;
                amount = STORAGE_SECTOR_SIZE - sector_skip;
                if (amount > wanted - copied) amount = wanted - copied;
                kmemcpy(buffer + copied, sector + sector_skip, amount);
                copied += amount;
            }
            if (copied >= wanted) {
                *out_read = copied;
                return OK;
            }
        }
        if (storage_next_cluster(volume, mount, cluster, &next) != OK) {
            return ERR_DISK;
        }
        if (storage_cluster_is_bad(mount, next)) return ERR_DISK;
        if (storage_cluster_is_end(mount, next)) break;
        cluster = next;
    }
    *out_read = copied;
    return copied == wanted ? OK : ERR_OVERFLOW;
}

static int storage_find_free_directory_slots(const storage_volume_t* volume,
                                             storage_mount_t* mount,
                                             uint32_t directory_cluster,
                                             uint32_t required,
                                             uint32_t* out_offset) {
    uint8_t sector[STORAGE_SECTOR_SIZE];
    uint32_t run_start = 0U;
    uint32_t run_count = 0U;
    uint32_t cluster = directory_cluster;
    uint32_t last = directory_cluster;

    if (!volume || !mount || !required || !out_offset) {
        LOG_ERROR("FS", "Argumento invalido ao reservar entradas FAT32");
        return ERR_NULL;
    }
    for (uint32_t step = 0; step < STORAGE_MAX_CHAIN_STEPS; step++) {
        uint32_t relative_lba;
        uint32_t next;

        if (cluster < STORAGE_FIRST_DATA_CLUSTER ||
            cluster >= mount->total_clusters + STORAGE_FIRST_DATA_CLUSTER) {
            return ERR_INVALID;
        }
        last = cluster;
        relative_lba = mount->data_start +
                       (cluster - STORAGE_FIRST_DATA_CLUSTER) *
                       mount->sectors_per_cluster;
        for (uint32_t sector_index = 0;
             sector_index < mount->sectors_per_cluster; sector_index++) {
            int result = storage_read_relative(volume,
                                               relative_lba + sector_index,
                                               1, sector);
            if (result != OK) return result;
            for (uint32_t offset = 0; offset < STORAGE_SECTOR_SIZE;
                 offset += STORAGE_DIR_ENTRY_SIZE) {
                uint32_t absolute = (volume->start_lba + relative_lba +
                                     sector_index) * STORAGE_SECTOR_SIZE + offset;
                if (sector[offset] == 0x00U || sector[offset] == 0xE5U) {
                    if (!run_count) run_start = absolute;
                    run_count++;
                    if (run_count >= required) {
                        *out_offset = run_start;
                        return OK;
                    }
                } else {
                    run_count = 0U;
                }
            }
        }
        if (storage_next_cluster(volume, mount, cluster, &next) != OK) {
            return ERR_DISK;
        }
        if (storage_cluster_is_end(mount, next)) break;
        if (storage_cluster_is_bad(mount, next)) return ERR_DISK;
        cluster = next;
    }

    uint32_t entries_per_cluster = mount->sectors_per_cluster *
                                   (STORAGE_SECTOR_SIZE /
                                    STORAGE_DIR_ENTRY_SIZE);
    uint32_t clusters_needed = (required + entries_per_cluster - 1U) /
                               entries_per_cluster;
    uint32_t first_new = 0U;
    uint32_t previous_new = last;
    uint32_t orphan_cluster = 0U;
    int result = OK;

    for (uint32_t created = 0; created < clusters_needed; created++) {
        uint32_t new_cluster;
        result = storage_allocate_fat32_cluster(volume, mount, &new_cluster);
        if (result != OK) break;
        orphan_cluster = new_cluster;
        if (!first_new) first_new = new_cluster;
        result = storage_write_fat32_entry(volume, mount, previous_new,
                                           new_cluster);
        if (result != OK) break;
        orphan_cluster = 0U;
        previous_new = new_cluster;
        for (uint32_t index = 0; index < mount->sectors_per_cluster; index++) {
            uint8_t empty[STORAGE_SECTOR_SIZE];
            kmemset(empty, 0, sizeof(empty));
            result = storage_write_relative(
                volume, mount->data_start +
                    (new_cluster - STORAGE_FIRST_DATA_CLUSTER) *
                    mount->sectors_per_cluster + index, empty);
            if (result != OK) break;
        }
        if (result != OK) break;
    }
    if (result != OK) {
        storage_write_fat32_entry(volume, mount, last, STORAGE_FAT32_END);
        if (first_new) storage_release_fat32_chain(volume, mount, first_new);
        if (orphan_cluster && orphan_cluster != first_new) {
            storage_write_fat32_entry(
                volume, mount, orphan_cluster, STORAGE_FAT32_FREE);
        }
        return result;
    }
    *out_offset = (volume->start_lba + mount->data_start +
                   (first_new - STORAGE_FIRST_DATA_CLUSTER) *
                   mount->sectors_per_cluster) * STORAGE_SECTOR_SIZE;
    return OK;
}

static int storage_write_directory_entry(const storage_volume_t* volume,
                                         uint32_t absolute_offset,
                                         const uint8_t entry[STORAGE_DIR_ENTRY_SIZE]) {
    uint8_t sector[STORAGE_SECTOR_SIZE];
    uint32_t absolute_lba = absolute_offset / STORAGE_SECTOR_SIZE;
    uint32_t offset = absolute_offset % STORAGE_SECTOR_SIZE;
    int result;

    if (!volume || !entry || offset + STORAGE_DIR_ENTRY_SIZE >
        STORAGE_SECTOR_SIZE || absolute_lba < volume->start_lba) {
        LOG_ERROR("FS", "Entrada de diretorio FAT32 invalida");
        return ERR_INVALID;
    }
    result = storage_read_relative(volume,
                                   absolute_lba - volume->start_lba,
                                   1, sector);
    if (result != OK) return result;
    kmemcpy(sector + offset, entry, STORAGE_DIR_ENTRY_SIZE);
    return storage_write_relative(volume, absolute_lba - volume->start_lba,
                                  sector);
}

static int storage_mark_directory_entries_deleted(
    const storage_volume_t* volume, const storage_long_raw_entry_t* source) {
    uint8_t entry[STORAGE_DIR_ENTRY_SIZE];

    if (!volume || !source) return ERR_NULL;
    kmemset(entry, 0xE5, sizeof(entry));
    for (uint32_t index = 0U; index < source->lfn_count; index++) {
        int result = storage_write_directory_entry(
            volume, source->lfn_offsets[index], entry);
        if (result != OK) return result;
    }
    return storage_write_directory_entry(volume, source->entry_offset, entry);
}

static int storage_get_mounted_fat32(const char* id,
                                     storage_volume_t** out_volume,
                                     storage_mount_t** out_mount,
                                     int* out_index) {
    int index;

    if (!id || !out_volume || !out_mount || !out_index) {
        LOG_ERROR("FS", "Argumento nulo ao obter montagem FAT32");
        return ERR_NULL;
    }
    index = storage_initialized ? storage_volume_index(id) : -1;
    if (index < 0) return storage_initialized ? ERR_NOT_FOUND : ERR_STATE;
    if (!storage_volumes[index].mounted) return ERR_STATE;
    if (storage_volumes[index].fs_type != STORAGE_FS_FAT32) return ERR_UNAVAILABLE;
    *out_volume = &storage_volumes[index];
    *out_mount = storage_mount_for_volume((uint8_t)index);
    *out_index = index;
    if (!*out_mount) return ERR_STATE;
    if ((*out_volume)->read_only) return ERR_UNAVAILABLE;
    return OK;
}

static int storage_publish_fat32_entry(
    const storage_volume_t* volume, const storage_long_raw_entry_t* old_entry,
    uint32_t slot_start, const char* name, const uint8_t alias[11],
    uint32_t first_cluster, uint32_t size, uint8_t attributes,
    uint32_t slot_count) {
    uint16_t units[STORAGE_LONG_NAME_SIZE];
    uint32_t unit_count;
    uint8_t short_entry[STORAGE_DIR_ENTRY_SIZE];
    uint8_t checksum;
    int result;

    if (!volume || !name || !alias || !slot_count) {
        LOG_ERROR("FS", "Argumento nulo na publicacao FAT32");
        return ERR_NULL;
    }
    result = storage_utf8_to_utf16(name, units, STORAGE_LONG_NAME_SIZE,
                                   &unit_count);
    if (result != OK) return result;
    uint32_t lfn_count = (unit_count + 12U) / 13U;
    if (lfn_count + 1U != slot_count) return ERR_INVALID;
    if (old_entry) {
        result = storage_mark_directory_entries_deleted(volume, old_entry);
        if (result != OK) return result;
    }
    checksum = storage_lfn_checksum(alias);
    for (uint32_t index = 0; index < lfn_count; index++) {
        uint8_t entry[STORAGE_DIR_ENTRY_SIZE];
        uint8_t sequence = (uint8_t)(lfn_count - index);
        if (sequence == lfn_count) sequence |= 0x40U;
        storage_build_lfn_entry(entry, sequence, units, unit_count, checksum);
        result = storage_write_directory_entry(
            volume, slot_start + index * STORAGE_DIR_ENTRY_SIZE, entry);
        if (result != OK) return result;
    }
    kmemset(short_entry, 0, sizeof(short_entry));
    kmemcpy(short_entry, alias, 11U);
    short_entry[11] = attributes;
    short_entry[20] = (uint8_t)(first_cluster >> 16);
    short_entry[21] = (uint8_t)(first_cluster >> 24);
    short_entry[26] = (uint8_t)first_cluster;
    short_entry[27] = (uint8_t)(first_cluster >> 8);
    short_entry[28] = (uint8_t)size;
    short_entry[29] = (uint8_t)(size >> 8);
    short_entry[30] = (uint8_t)(size >> 16);
    short_entry[31] = (uint8_t)(size >> 24);
    return storage_write_directory_entry(
        volume, slot_start + lfn_count * STORAGE_DIR_ENTRY_SIZE, short_entry);
}

static int storage_write_fat32_file_unlocked(const char* id, const char* path,
                                             const uint8_t* data, uint32_t size,
                                             uint8_t attributes,
                                             storage_atomic_mode_t mode) {
    storage_volume_t* volume;
    storage_mount_t* mount;
    storage_long_raw_entry_t old_entry;
    char directory[STORAGE_MAX_PATH];
    char filename[STORAGE_LONG_NAME_SIZE];
    uint32_t directory_cluster;
    uint8_t fixed_root;
    uint32_t slot_start;
    uint32_t cluster_bytes;
    uint32_t needed;
    uint32_t first_cluster = 0U;
    uint32_t previous_cluster = 0U;
    uint32_t orphan_cluster = 0U;
    uint8_t alias[11];
    int index;
    int result;

    if (!id || !path || (size && !data)) {
        LOG_ERROR("FS", "Argumento invalido na escrita FAT32");
        return ERR_NULL;
    }
    result = storage_get_mounted_fat32(id, &volume, &mount, &index);
    if (result != OK) return result;
    (void)index;
    result = storage_split_file_path(path, directory, filename);
    if (result != OK) return result;
    result = storage_resolve_directory(volume, mount, directory,
                                       &directory_cluster, &fixed_root);
    if (result != OK) return result;
    result = storage_find_entry_long(volume, mount, directory_cluster,
                                     fixed_root, filename, &old_entry);
    uint8_t replacing = result == OK ? 1U : 0U;
    if (result != OK && result != ERR_NOT_FOUND) return result;
    if (replacing && (old_entry.attributes & STORAGE_ATTR_DIRECTORY)) {
        return ERR_INVALID;
    }
    if (mode == STORAGE_ATOMIC_REPLACE_ONLY && !replacing) return ERR_NOT_FOUND;
    if (replacing) {
        result = storage_name_to_fat(old_entry.short_name, alias);
        if (result != OK) return result;
    } else {
        result = storage_generate_alias(volume, mount, directory_cluster,
                                        fixed_root, filename, alias);
        if (result != OK) return result;
    }
    cluster_bytes = mount->sectors_per_cluster * STORAGE_SECTOR_SIZE;
    if (size > 0U && size > 0xFFFFFFFFU - cluster_bytes + 1U) {
        result = ERR_OVERFLOW;
        goto rollback_clusters;
    }
    needed = size ? (size + cluster_bytes - 1U) / cluster_bytes : 0U;
    if (attributes & STORAGE_ATTR_DIRECTORY) needed = 1U;
    for (uint32_t count = 0; count < needed; count++) {
        uint32_t cluster;
        result = storage_allocate_fat32_cluster(volume, mount, &cluster);
        if (result != OK) goto rollback_clusters;
        orphan_cluster = cluster;
        if (!first_cluster) first_cluster = cluster;
        if (previous_cluster) {
            result = storage_write_fat32_entry(volume, mount,
                                               previous_cluster, cluster);
            if (result != OK) goto rollback_clusters;
        }
        orphan_cluster = 0U;
        previous_cluster = cluster;
        if (!(attributes & STORAGE_ATTR_DIRECTORY)) {
            uint32_t offset = count * cluster_bytes;
            uint32_t amount = size - offset;
            if (amount > cluster_bytes) amount = cluster_bytes;
            result = storage_write_cluster(volume, mount, cluster,
                                           data + offset, amount);
            if (result != OK) goto rollback_clusters;
        } else {
            uint8_t empty[STORAGE_SECTOR_SIZE];
            kmemset(empty, 0, sizeof(empty));
            for (uint32_t sector = 0; sector < mount->sectors_per_cluster;
                 sector++) {
                result = storage_write_relative(
                    volume, mount->data_start +
                    (cluster - STORAGE_FIRST_DATA_CLUSTER) *
                    mount->sectors_per_cluster + sector, empty);
                if (result != OK) goto rollback_clusters;
            }
        }
    }
    if (attributes & STORAGE_ATTR_DIRECTORY) {
        /* O marcador de diretorio vazio fica consistente antes da entrada. */
        uint8_t dot[STORAGE_DIR_ENTRY_SIZE];
        uint8_t dot_dot[STORAGE_DIR_ENTRY_SIZE];
        uint32_t parent_cluster = fixed_root ? mount->root_cluster :
                                  directory_cluster;

        storage_build_directory_marker(dot, ".          ", first_cluster);
        result = storage_write_directory_entry(
            volume, (volume->start_lba + mount->data_start +
                     (first_cluster - STORAGE_FIRST_DATA_CLUSTER) *
                     mount->sectors_per_cluster) * STORAGE_SECTOR_SIZE, dot);
        if (result != OK) goto rollback_clusters;
        storage_build_directory_marker(dot_dot, "..         ", parent_cluster);
        result = storage_write_directory_entry(
            volume, (volume->start_lba + mount->data_start +
                     (first_cluster - STORAGE_FIRST_DATA_CLUSTER) *
                     mount->sectors_per_cluster) * STORAGE_SECTOR_SIZE +
                    STORAGE_DIR_ENTRY_SIZE, dot_dot);
        if (result != OK) goto rollback_clusters;
    }
    uint16_t units[STORAGE_LONG_NAME_SIZE];
    uint32_t unit_count;
    result = storage_utf8_to_utf16(filename, units, STORAGE_LONG_NAME_SIZE,
                                   &unit_count);
    if (result != OK) goto rollback_clusters;
    uint32_t required_slots = (unit_count + 12U) / 13U + 1U;
    result = storage_find_free_directory_slots(
        volume, mount, directory_cluster, required_slots, &slot_start);
    if (result == OK) {
        /* A entrada antiga permanece intacta ate a nova estar publicada. */
        result = storage_publish_fat32_entry(
            volume, 0, slot_start, filename, alias, first_cluster,
            size, attributes, required_slots);
    }
    if (result == OK && replacing) {
        result = storage_mark_directory_entries_deleted(volume, &old_entry);
    }
    if (result != OK) goto rollback_clusters;
    if (replacing && old_entry.first_cluster &&
        old_entry.first_cluster != first_cluster) {
        result = storage_release_fat32_chain(volume, mount,
                                              old_entry.first_cluster);
        if (result != OK) {
            storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                               "arquivo publicado, mas cadeia antiga nao foi liberada");
            return result;
        }
    }
    return OK;

rollback_clusters:
    if (first_cluster) storage_release_fat32_chain(volume, mount, first_cluster);
    if (orphan_cluster && orphan_cluster != first_cluster) {
        storage_write_fat32_entry(volume, mount, orphan_cluster,
                                  STORAGE_FAT32_FREE);
    }
    storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                       "transacao de escrita FAT32 revertida");
    return result;
}

static void storage_refresh_disk(storage_disk_t* disk) {
    block_device_t block;

    if (!disk || block_find(disk->id, &block) != OK) return;
    disk->read_ops = block.read_ops;
    disk->write_ops = block.write_ops;
    if (block.last_error != OK || disk->last_error == OK) {
        disk->last_error = block.last_error;
    }
}

int storage_get_status(storage_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("FS", "Destino nulo na consulta de status");
        return ERR_NULL;
    }
    spinlock_acquire(&storage_registry_lock);
    out_status->initialized = storage_initialized;
    out_status->disk_count = storage_disk_count;
    out_status->volume_count = storage_volume_count;
    out_status->mounted_count = storage_mounted_count;
    out_status->last_error = storage_last_error;
    spinlock_release(&storage_registry_lock);
    return OK;
}

int storage_get_disk_at(uint8_t index, storage_disk_t* out_disk) {
    if (!out_disk) {
        LOG_ERROR("FS", "Destino nulo na consulta de disco");
        return ERR_NULL;
    }
    spinlock_acquire(&storage_registry_lock);
    if (!storage_initialized || index >= storage_disk_count) {
        LOG_WARN("FS", "Indice de disco indisponivel");
        spinlock_release(&storage_registry_lock);
        return storage_initialized ? ERR_INVALID : ERR_STATE;
    }
    *out_disk = storage_disks[index];
    spinlock_release(&storage_registry_lock);
    storage_refresh_disk(out_disk);
    return OK;
}

int storage_find_disk(const char* id, storage_disk_t* out_disk) {
    int index;

    if (!id || !out_disk) {
        LOG_ERROR("FS", "Argumento nulo na busca de disco");
        return ERR_NULL;
    }
    spinlock_acquire(&storage_registry_lock);
    index = storage_initialized ? storage_disk_index(id) : -1;
    if (index >= 0) *out_disk = storage_disks[index];
    spinlock_release(&storage_registry_lock);
    if (index < 0) return storage_initialized ? ERR_NOT_FOUND : ERR_STATE;
    storage_refresh_disk(out_disk);
    return OK;
}

int storage_get_volume_at(uint8_t index, storage_volume_t* out_volume) {
    if (!out_volume) {
        LOG_ERROR("FS", "Destino nulo na consulta de volume");
        return ERR_NULL;
    }
    spinlock_acquire(&storage_registry_lock);
    if (!storage_initialized || index >= storage_volume_count) {
        LOG_WARN("FS", "Indice de volume indisponivel");
        spinlock_release(&storage_registry_lock);
        return storage_initialized ? ERR_INVALID : ERR_STATE;
    }
    *out_volume = storage_volumes[index];
    spinlock_release(&storage_registry_lock);
    return OK;
}

int storage_get_mounted_at(uint8_t index, storage_volume_t* out_volume) {
    uint8_t mounted_index = 0;

    if (!out_volume) {
        LOG_ERROR("FS", "Destino nulo na consulta de montagem");
        return ERR_NULL;
    }
    spinlock_acquire(&storage_registry_lock);
    if (!storage_initialized) {
        LOG_WARN("FS", "Consulta de montagem antes da inicializacao");
        spinlock_release(&storage_registry_lock);
        return ERR_STATE;
    }
    for (uint8_t volume_index = 0; volume_index < storage_volume_count;
         volume_index++) {
        if (!storage_volumes[volume_index].mounted) continue;
        if (mounted_index++ == index) {
            *out_volume = storage_volumes[volume_index];
            spinlock_release(&storage_registry_lock);
            return OK;
        }
    }
    spinlock_release(&storage_registry_lock);
    LOG_WARN("FS", "Indice de montagem nao encontrado");
    return ERR_NOT_FOUND;
}

int storage_find_volume(const char* id, storage_volume_t* out_volume) {
    int index;

    if (!id || !out_volume) {
        LOG_ERROR("FS", "Argumento nulo na busca de volume");
        return ERR_NULL;
    }
    spinlock_acquire(&storage_registry_lock);
    index = storage_initialized ? storage_volume_index(id) : -1;
    if (index >= 0) *out_volume = storage_volumes[index];
    spinlock_release(&storage_registry_lock);
    if (index < 0) return storage_initialized ? ERR_NOT_FOUND : ERR_STATE;
    return OK;
}

int storage_mount(const char* id) {
    storage_mount_t probe;
    storage_mount_t* mount;
    storage_volume_t* volume;
    uint32_t writes_before = 0U;
    uint32_t writes_after = 0U;
    block_device_t block;
    int index;
    int result;

    if (!id) {
        LOG_ERROR("FS", "ID nulo na montagem");
        return ERR_NULL;
    }
    spinlock_acquire(&storage_operation_lock);
    index = storage_initialized ? storage_volume_index(id) : -1;
    if (index < 0) {
        LOG_ERROR("FS", "Volume solicitado para mount nao encontrado");
        spinlock_release(&storage_operation_lock);
        return storage_initialized ? ERR_NOT_FOUND : ERR_STATE;
    }
    volume = &storage_volumes[index];
    if (volume->mounted || storage_mounted_count >= STORAGE_MAX_MOUNTS) {
        result = volume->mounted ? ERR_STATE : ERR_OVERFLOW;
        storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                           volume->mounted ? "volume ja montado" :
                           "limite de montagens atingido");
        spinlock_release(&storage_operation_lock);
        return result;
    }
    if (volume->state == STORAGE_VOLUME_INVALID ||
        volume->state == STORAGE_VOLUME_UNSUPPORTED) {
        result = volume->last_error ? volume->last_error : ERR_UNAVAILABLE;
        storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                           "estado nao permite montagem");
        spinlock_release(&storage_operation_lock);
        return result;
    }
    result = block_find(volume->disk_id, &block);
    if (result != OK) {
        storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                           "provedor de bloco indisponivel antes do mount");
        spinlock_release(&storage_operation_lock);
        return result;
    }
    writes_before = block.write_ops;
    result = storage_probe_volume(volume, &probe);
    if (block_find(volume->disk_id, &block) != OK) result = ERR_STATE;
    else writes_after = block.write_ops;
    if (result == OK && writes_before != writes_after) result = ERR_STATE;
    if (result != OK) {
        storage_mark_volume_error(volume, result,
                                  result == ERR_UNAVAILABLE ?
                                  STORAGE_VOLUME_UNSUPPORTED :
                                  STORAGE_VOLUME_INVALID,
                                  "montagem recusada pela validacao");
        spinlock_release(&storage_operation_lock);
        return result;
    }
    mount = storage_free_mount();
    if (!mount) {
        storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                           "nenhum slot de montagem livre");
        spinlock_release(&storage_operation_lock);
        return ERR_OVERFLOW;
    }
    *mount = probe;
    mount->active = 1;
    mount->volume_index = index;
    spinlock_acquire(&storage_registry_lock);
    volume->mounted = 1;
    volume->read_only = (block.read_only ||
                         volume->fs_type != STORAGE_FS_FAT32) ? 1U : 0U;
    volume->state = STORAGE_VOLUME_MOUNTED;
    volume->last_error = OK;
    volume->generation++;
    storage_mounted_count++;
    spinlock_release(&storage_registry_lock);
    storage_log_volume(LOG_LEVEL_INFO, volume->id,
                       volume->read_only ?
                       "montado em modo somente-leitura" :
                       "montado em modo gravavel");
    spinlock_release(&storage_operation_lock);
    return OK;
}

int storage_unmount(const char* id) {
    storage_mount_t* mount;
    storage_volume_t* volume;
    int index;
    int result;

    if (!id) {
        LOG_ERROR("FS", "ID nulo na desmontagem");
        return ERR_NULL;
    }
    spinlock_acquire(&storage_operation_lock);
    index = storage_initialized ? storage_volume_index(id) : -1;
    if (index < 0) {
        LOG_ERROR("FS", "Volume solicitado para unmount nao encontrado");
        spinlock_release(&storage_operation_lock);
        return storage_initialized ? ERR_NOT_FOUND : ERR_STATE;
    }
    volume = &storage_volumes[index];
    if (!volume->mounted || volume->pinned || volume->boot) {
        storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                           volume->boot ? "volume de boot nao pode ser desmontado" :
                           "volume nao esta montado");
        spinlock_release(&storage_operation_lock);
        return ERR_STATE;
    }
    mount = storage_mount_for_volume(index);
    if (!mount) {
        storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                           "registro interno de montagem ausente");
        spinlock_release(&storage_operation_lock);
        return ERR_STATE;
    }
    result = storage_sync_volume(volume->id);
    if (result != OK) {
        storage_log_volume(LOG_LEVEL_WARN, volume->id,
                           "desmontagem recusada por falha de sincronizacao");
        spinlock_release(&storage_operation_lock);
        return result;
    }
    result = block_cache_invalidate_device(volume->disk_id);
    if (result != OK) {
        storage_log_volume(LOG_LEVEL_WARN, volume->id,
                           "desmontagem recusada por cache ocupado");
        spinlock_release(&storage_operation_lock);
        return result;
    }
    spinlock_acquire(&storage_registry_lock);
    kmemset(mount, 0, sizeof(*mount));
    volume->mounted = 0;
    volume->state = STORAGE_VOLUME_DETECTED;
    volume->generation++;
    storage_mounted_count--;
    spinlock_release(&storage_registry_lock);
    storage_log_volume(LOG_LEVEL_INFO, volume->id, "volume desmontado");
    spinlock_release(&storage_operation_lock);
    return OK;
}

int storage_list_dir(const char* id, const char* path,
                     storage_dir_entry_t* entries, uint32_t capacity,
                     uint32_t* out_count) {
    storage_list_context_t list;
    storage_mount_t* mount;
    storage_volume_t* volume;
    uint32_t cluster;
    uint8_t fixed_root;
    int index;
    int result;

    if (!id || !path || !entries || !out_count) {
        LOG_ERROR("FS", "Argumento nulo na listagem de diretorio");
        return ERR_NULL;
    }
    if (!capacity || capacity > STORAGE_MAX_DIR_ENTRIES) {
        LOG_ERROR("FS", "Capacidade invalida na listagem de diretorio");
        return ERR_INVALID;
    }
    if (kstrlen(path) >= STORAGE_MAX_PATH) {
        LOG_ERROR("FS", "Caminho excede limite na listagem");
        return ERR_OVERFLOW;
    }
    *out_count = 0;
    spinlock_acquire(&storage_operation_lock);
    index = storage_initialized ? storage_volume_index(id) : -1;
    if (index < 0 || !storage_volumes[index].mounted) {
        storage_log_volume(LOG_LEVEL_ERROR, id,
                           index < 0 ? "volume nao encontrado" :
                           "volume nao esta montado");
        spinlock_release(&storage_operation_lock);
        return index < 0 ? ERR_NOT_FOUND : ERR_STATE;
    }
    volume = &storage_volumes[index];
    mount = storage_mount_for_volume(index);
    if (!mount) {
        storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                           "registro interno de montagem ausente");
        spinlock_release(&storage_operation_lock);
        return ERR_STATE;
    }
    result = storage_resolve_directory(volume, mount, path,
                                       &cluster, &fixed_root);
    if (result == OK) {
        list.entries = entries;
        list.capacity = capacity;
        list.count = 0;
        result = storage_walk_directory(volume, mount, cluster, fixed_root,
                                        storage_list_visitor, &list);
        *out_count = list.count;
    }
    if (result != OK) storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                                         "falha ao listar diretorio");
    spinlock_release(&storage_operation_lock);
    return result;
}

static int storage_cursor_is_dot(const char* name) {
    return name && name[0] == '.' &&
           (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

static int storage_cursor_load_sector(const storage_volume_t* volume,
                                      const storage_mount_t* mount,
                                      storage_dir_cursor_t* cursor) {
    uint32_t relative_lba;
    uint32_t next;
    int result;

    if (!volume || !mount || !cursor) {
        LOG_ERROR("FS", "Argumento nulo ao carregar cursor de volume");
        return ERR_NULL;
    }

    if (cursor->fixed_root) {
        if (cursor->sector_index >= mount->root_sectors) {
            cursor->done = 1;
            return OK;
        }
        relative_lba = mount->root_start + cursor->sector_index;
    } else {
        if (cursor->sector_index >= mount->sectors_per_cluster) {
            if (cursor->chain_steps++ >= STORAGE_MAX_CHAIN_STEPS) {
                return ERR_OVERFLOW;
            }
            result = storage_next_cluster(volume, mount,
                                          cursor->current_cluster, &next);
            if (result != OK) return result;
            if (storage_cluster_is_bad(mount, next)) return ERR_DISK;
            if (storage_cluster_is_end(mount, next)) {
                cursor->done = 1;
                return OK;
            }
            cursor->current_cluster = next;
            cursor->sector_index = 0;
            return OK;
        }
        if (cursor->current_cluster < STORAGE_FIRST_DATA_CLUSTER ||
            cursor->current_cluster >=
                mount->total_clusters + STORAGE_FIRST_DATA_CLUSTER) {
            return ERR_INVALID;
        }
        relative_lba = mount->data_start +
                       (cursor->current_cluster - STORAGE_FIRST_DATA_CLUSTER) *
                       mount->sectors_per_cluster + cursor->sector_index;
    }
    result = storage_read_relative(volume, relative_lba, 1, cursor->sector);
    if (result != OK) return result;
    cursor->sector_loaded = 1;
    cursor->entry_index = 0;
    return OK;
}

int storage_dir_cursor_open(const char* id, const char* path,
                            storage_dir_cursor_t* cursor) {
    storage_mount_t* mount;
    storage_volume_t* volume;
    uint32_t cluster;
    uint8_t fixed_root;
    int index;
    int result;

    if (!id || !path || !cursor) {
        LOG_ERROR("FS", "Argumento nulo ao abrir cursor de volume");
        return ERR_NULL;
    }
    if (kstrlen(path) >= STORAGE_MAX_PATH) {
        LOG_ERROR("FS", "Caminho excede limite do cursor de volume");
        return ERR_OVERFLOW;
    }
    spinlock_acquire(&storage_operation_lock);
    index = storage_initialized ? storage_volume_index(id) : -1;
    if (index < 0 || !storage_volumes[index].mounted) {
        spinlock_release(&storage_operation_lock);
        storage_log_volume(LOG_LEVEL_ERROR, id, "cursor sem volume montado");
        return index < 0 ? ERR_NOT_FOUND : ERR_STATE;
    }
    volume = &storage_volumes[index];
    mount = storage_mount_for_volume((uint8_t)index);
    result = mount ? storage_resolve_directory(
                         volume, mount, path, &cluster, &fixed_root) :
                     ERR_STATE;
    if (result == OK) {
        kmemset(cursor, 0, sizeof(*cursor));
        storage_copy_text(cursor->volume_id, STORAGE_ID_SIZE, volume->id);
        cursor->volume_generation = volume->generation;
        cursor->directory_cluster = cluster;
        cursor->current_cluster = cluster;
        cursor->fs_type = mount->fs_type;
        cursor->fixed_root = fixed_root;
        cursor->active = 1;
    }
    spinlock_release(&storage_operation_lock);
    if (result != OK) {
        storage_log_volume(LOG_LEVEL_ERROR, id,
                           "falha ao abrir cursor de diretorio");
    }
    return result;
}

int storage_dir_cursor_next(storage_dir_cursor_t* cursor,
                            storage_dir_entry_t* out_entry,
                            uint8_t* out_found, uint8_t* out_done) {
    storage_mount_t* mount;
    storage_volume_t* volume;
    int index;
    int result = OK;

    if (!cursor || !out_entry || !out_found || !out_done) {
        LOG_ERROR("FS", "Argumento nulo ao avancar cursor de volume");
        return ERR_NULL;
    }
    *out_found = 0;
    *out_done = cursor->done;
    spinlock_acquire(&storage_operation_lock);
    index = storage_initialized ? storage_volume_index(cursor->volume_id) : -1;
    volume = index >= 0 ? &storage_volumes[index] : 0;
    mount = index >= 0 ? storage_mount_for_volume((uint8_t)index) : 0;
    if (!cursor->active || !volume || !volume->mounted || !mount ||
        volume->generation != cursor->volume_generation ||
        mount->fs_type != cursor->fs_type) {
        result = ERR_STATE;
    } else if (!cursor->done && !cursor->sector_loaded) {
        result = storage_cursor_load_sector(volume, mount, cursor);
    }
    if (result != OK || cursor->done || !cursor->sector_loaded) {
        *out_done = cursor->done;
        spinlock_release(&storage_operation_lock);
        if (result != OK) LOG_ERROR("FS", "Cursor de volume ficou invalido");
        return result;
    }
    while (cursor->entry_index <
           STORAGE_SECTOR_SIZE / STORAGE_DIR_ENTRY_SIZE) {
        const uint8_t* source = cursor->sector +
                                cursor->entry_index++ * STORAGE_DIR_ENTRY_SIZE;
        storage_raw_entry_t raw;

        if (source[0] == 0x00U) {
            cursor->done = 1;
            *out_done = 1;
            break;
        }
        if (source[0] == 0xE5U ||
            source[STORAGE_DIR_ATTRIBUTE_OFFSET] == STORAGE_ATTR_LFN ||
            (source[STORAGE_DIR_ATTRIBUTE_OFFSET] & STORAGE_ATTR_VOLUME)) {
            continue;
        }
        storage_parse_raw_entry(mount, source, &raw);
        kmemset(out_entry, 0, sizeof(*out_entry));
        storage_entry_name(&raw, out_entry->name);
        if (storage_cursor_is_dot(out_entry->name)) continue;
        out_entry->size = raw.size;
        out_entry->cluster = raw.first_cluster;
        out_entry->attributes = raw.attributes;
        out_entry->is_directory =
            (raw.attributes & STORAGE_ATTR_DIRECTORY) ? 1U : 0U;
        *out_found = 1;
        break;
    }
    if (!*out_found && !cursor->done) {
        cursor->sector_loaded = 0;
        cursor->sector_index++;
        cursor->entry_index = 0;
    }
    spinlock_release(&storage_operation_lock);
    return OK;
}

typedef struct {
    storage_long_dir_entry_t* entries;
    uint32_t capacity;
    uint32_t count;
} storage_long_list_context_t;

static int storage_long_list_visitor(const storage_long_raw_entry_t* entry,
                                     void* context) {
    storage_long_list_context_t* list =
        (storage_long_list_context_t*)context;
    storage_long_dir_entry_t* output;

    if (!entry || !list) return ERR_NULL;
    if (entry->name[0] == '.' &&
        (entry->name[1] == '\0' ||
         (entry->name[1] == '.' && entry->name[2] == '\0'))) return OK;
    if (list->count >= list->capacity) return OK;
    output = &list->entries[list->count++];
    kmemset(output, 0, sizeof(*output));
    storage_copy_text(output->name, STORAGE_LONG_NAME_SIZE, entry->name);
    storage_copy_text(output->short_name, STORAGE_NAME_SIZE,
                      entry->short_name);
    output->size = entry->size;
    output->cluster = entry->first_cluster;
    output->attributes = entry->attributes;
    output->is_directory = (entry->attributes & STORAGE_ATTR_DIRECTORY) ? 1U : 0U;
    return OK;
}

int storage_list_dir_long(const char* id, const char* path,
                          storage_long_dir_entry_t* entries,
                          uint32_t capacity, uint32_t* out_count) {
    storage_long_list_context_t list;
    storage_mount_t* mount;
    storage_volume_t* volume;
    uint32_t cluster;
    uint8_t fixed_root;
    int index;
    int result;

    if (!id || !path || !entries || !out_count) {
        LOG_ERROR("FS", "Argumento nulo na listagem LFN");
        return ERR_NULL;
    }
    if (!capacity || capacity > STORAGE_MAX_DIR_ENTRIES) {
        LOG_ERROR("FS", "Capacidade invalida na listagem LFN");
        return ERR_INVALID;
    }
    if (kstrlen(path) >= STORAGE_MAX_PATH) {
        LOG_ERROR("FS", "Caminho excede limite na listagem LFN");
        return ERR_OVERFLOW;
    }
    *out_count = 0U;
    spinlock_acquire(&storage_operation_lock);
    index = storage_initialized ? storage_volume_index(id) : -1;
    if (index < 0 || !storage_volumes[index].mounted) {
        spinlock_release(&storage_operation_lock);
        storage_log_volume(LOG_LEVEL_ERROR, id,
                           index < 0 ? "volume nao encontrado" :
                           "volume nao esta montado");
        return index < 0 ? ERR_NOT_FOUND : ERR_STATE;
    }
    volume = &storage_volumes[index];
    mount = storage_mount_for_volume((uint8_t)index);
    if (!mount) {
        spinlock_release(&storage_operation_lock);
        storage_log_volume(LOG_LEVEL_ERROR, id,
                           "registro interno LFN ausente");
        return ERR_STATE;
    }
    result = storage_resolve_directory(volume, mount, path,
                                       &cluster, &fixed_root);
    if (result == OK) {
        list.entries = entries;
        list.capacity = capacity;
        list.count = 0U;
        result = storage_walk_directory_long(
            volume, mount, cluster, fixed_root,
            storage_long_list_visitor, &list);
        *out_count = list.count;
    }
    spinlock_release(&storage_operation_lock);
    if (result != OK) storage_log_volume(LOG_LEVEL_ERROR, id,
                                         "falha ao listar nomes longos");
    return result;
}

int storage_dir_cursor_open_long(const char* id, const char* path,
                                 storage_long_dir_cursor_t* cursor) {
    int result = storage_dir_cursor_open(id, path, cursor);
    if (result == OK) storage_copy_text(cursor->lfn_path, STORAGE_MAX_PATH, path);
    return result;
}

int storage_dir_cursor_next_long(storage_long_dir_cursor_t* cursor,
                                 storage_long_dir_entry_t* out_entry,
                                 uint8_t* out_found, uint8_t* out_done) {
    uint32_t count = 0U;
    int result;

    if (!cursor || !out_entry || !out_found || !out_done) {
        LOG_ERROR("FS", "Argumento nulo no cursor LFN");
        return ERR_NULL;
    }
    *out_found = 0U;
    *out_done = cursor->done;
    if (cursor->done || !cursor->active) return cursor->active ? OK : ERR_STATE;
    result = storage_list_dir_long(cursor->volume_id, cursor->lfn_path,
                                   storage_long_cursor_entries,
                                   STORAGE_MAX_DIR_ENTRIES, &count);
    if (result != OK) return result;
    if (cursor->lfn_result_index >= count) {
        cursor->done = 1U;
        *out_done = 1U;
        return OK;
    }
    *out_entry = storage_long_cursor_entries[cursor->lfn_result_index++];
    *out_found = 1U;
    return OK;
}

int storage_read_file_range(const char* id, const char* path,
                            uint32_t offset, uint8_t* buffer,
                            uint32_t max_size, uint32_t* out_read) {
    storage_raw_entry_t entry;
    storage_mount_t* mount;
    storage_volume_t* volume;
    int index;
    int result;

    if (!id || !path || !out_read || (max_size && !buffer)) {
        LOG_ERROR("FS", "Argumento nulo na leitura de arquivo");
        return ERR_NULL;
    }
    if (kstrlen(path) >= STORAGE_MAX_PATH) {
        LOG_ERROR("FS", "Caminho excede limite na leitura");
        return ERR_OVERFLOW;
    }
    *out_read = 0;
    spinlock_acquire(&storage_operation_lock);
    index = storage_initialized ? storage_volume_index(id) : -1;
    if (index < 0 || !storage_volumes[index].mounted) {
        storage_log_volume(LOG_LEVEL_ERROR, id,
                           index < 0 ? "volume nao encontrado" :
                           "volume nao esta montado");
        spinlock_release(&storage_operation_lock);
        return index < 0 ? ERR_NOT_FOUND : ERR_STATE;
    }
    volume = &storage_volumes[index];
    mount = storage_mount_for_volume(index);
    if (!mount) {
        storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                           "registro interno de montagem ausente");
        spinlock_release(&storage_operation_lock);
        return ERR_STATE;
    }
    result = storage_find_file(volume, mount, path, &entry);
    if (result == OK) {
        result = storage_read_entry_range(volume, mount, &entry, offset,
                                          buffer, max_size, out_read);
    }
    if (result != OK) storage_log_volume(LOG_LEVEL_ERROR, volume->id,
                                         "falha ao ler arquivo");
    spinlock_release(&storage_operation_lock);
    return result;
}

int storage_get_file_info(const char* id, const char* path,
                          uint32_t* out_size, uint8_t* out_attributes) {
    storage_volume_t* volume;
    storage_mount_t* mount;
    storage_raw_entry_t entry;
    int index;
    int result;

    if (!id || !path || !out_size) {
        LOG_ERROR("FS", "Argumento nulo na consulta de arquivo de volume");
        return ERR_NULL;
    }
    spinlock_acquire(&storage_operation_lock);
    index = storage_initialized ? storage_volume_index(id) : -1;
    if (index < 0 || !storage_volumes[index].mounted) {
        spinlock_release(&storage_operation_lock);
        return index < 0 ? ERR_NOT_FOUND : ERR_STATE;
    }
    volume = &storage_volumes[index];
    mount = storage_mount_for_volume((uint8_t)index);
    result = mount ? storage_find_file(volume, mount, path, &entry) : ERR_STATE;
    if (result == OK) {
        *out_size = entry.size;
        if (out_attributes) *out_attributes = entry.attributes;
    }
    spinlock_release(&storage_operation_lock);
    if (result != OK) LOG_ERROR("FS", "Consulta de arquivo de volume falhou");
    return result;
}

int storage_get_path_info(const char* id, const char* path,
                          uint32_t* out_size, uint8_t* out_attributes,
                          uint8_t* out_is_directory) {
    storage_volume_t* volume;
    storage_mount_t* mount;
    storage_raw_entry_t entry;
    uint32_t cluster;
    uint8_t fixed_root;
    int index;
    int result;

    if (!id || !path || !out_size || !out_is_directory) {
        LOG_ERROR("FS", "Argumento nulo na consulta de caminho");
        return ERR_NULL;
    }
    spinlock_acquire(&storage_operation_lock);
    index = storage_initialized ? storage_volume_index(id) : -1;
    volume = index >= 0 ? &storage_volumes[index] : 0;
    mount = index >= 0 ? storage_mount_for_volume((uint8_t)index) : 0;
    if (!volume || !volume->mounted || !mount) {
        result = index < 0 ? ERR_NOT_FOUND : ERR_STATE;
    } else if (!path[0]) {
        *out_size = 0U;
        if (out_attributes) *out_attributes = STORAGE_ATTR_DIRECTORY;
        *out_is_directory = 1U;
        result = OK;
    } else {
        result = storage_find_file(volume, mount, path, &entry);
        if (result == OK) {
            *out_size = entry.size;
            if (out_attributes) *out_attributes = entry.attributes;
            *out_is_directory = 0U;
        } else if (storage_resolve_directory(volume, mount, path,
                                             &cluster, &fixed_root) == OK) {
            *out_size = 0U;
            if (out_attributes) *out_attributes = STORAGE_ATTR_DIRECTORY;
            *out_is_directory = 1U;
            result = OK;
        }
    }
    spinlock_release(&storage_operation_lock);
    if (result != OK) LOG_ERROR("FS", "Consulta de caminho de volume falhou");
    return result;
}

int storage_find_system_volume(storage_volume_t* out_volume) {
    if (!out_volume) {
        LOG_ERROR("FS", "Destino nulo na busca do volume do sistema");
        return ERR_NULL;
    }
    spinlock_acquire(&storage_registry_lock);
    for (uint8_t index = 0; index < storage_volume_count; index++) {
        storage_volume_t* volume = &storage_volumes[index];
        if (volume->role == STORAGE_VOLUME_ROLE_SYSTEM && volume->mounted) {
            *out_volume = *volume;
            spinlock_release(&storage_registry_lock);
            return OK;
        }
    }
    spinlock_release(&storage_registry_lock);
    LOG_WARN("FS", "Volume do sistema FAT32 nao esta montado");
    return storage_initialized ? ERR_NOT_FOUND : ERR_STATE;
}

typedef struct {
    const storage_volume_t* volume;
    const storage_mount_t* mount;
    uint32_t depth;
} storage_check_context_t;

static int storage_buffers_equal(const uint8_t* left, const uint8_t* right,
                                 uint32_t size) {
    if (!left || !right) return 0;
    for (uint32_t index = 0; index < size; index++) {
        if (left[index] != right[index]) return 0;
    }
    return 1;
}

static int storage_check_chain(const storage_volume_t* volume,
                               const storage_mount_t* mount,
                               uint32_t first_cluster, uint32_t size) {
    uint32_t cluster_bytes;
    uint32_t required_clusters = 0U;
    uint32_t cluster;

    if (!volume || !mount) {
        LOG_ERROR("FS", "Contexto nulo na verificacao de cadeia FAT32");
        return ERR_NULL;
    }
    if (!first_cluster) return size ? ERR_INVALID : OK;
    cluster_bytes = mount->sectors_per_cluster * STORAGE_SECTOR_SIZE;
    if (size) {
        required_clusters = size / cluster_bytes;
        if (size % cluster_bytes) required_clusters++;
    }
    cluster = first_cluster;
    for (uint32_t step = 0; step < STORAGE_MAX_CHAIN_STEPS; step++) {
        uint32_t next;

        if (cluster < STORAGE_FIRST_DATA_CLUSTER ||
            cluster >= mount->total_clusters + STORAGE_FIRST_DATA_CLUSTER) {
            LOG_ERROR("FS", "Cadeia FAT32 aponta para cluster invalido");
            return ERR_INVALID;
        }
        if (storage_next_cluster(volume, mount, cluster, &next) != OK) {
            LOG_ERROR("FS", "Falha ao ler cadeia FAT32");
            return ERR_DISK;
        }
        if (storage_cluster_is_bad(mount, next)) {
            LOG_ERROR("FS", "Cadeia FAT32 aponta para cluster ruim");
            return ERR_DISK;
        }
        if (storage_cluster_is_end(mount, next)) {
            if (required_clusters && step + 1U < required_clusters) {
                LOG_ERROR("FS", "Cadeia FAT32 menor que o arquivo");
                return ERR_INVALID;
            }
            return OK;
        }
        cluster = next;
    }
    LOG_ERROR("FS", "Cadeia FAT32 excedeu o limite de passos");
    return ERR_OVERFLOW;
}

static int storage_check_directory_visitor(
    const storage_long_raw_entry_t* entry, void* context) {
    storage_check_context_t* check = (storage_check_context_t*)context;
    storage_check_context_t child;
    int chain_result;

    if (!entry || !check || !check->volume || !check->mount) {
        LOG_ERROR("FS", "Contexto invalido na verificacao de diretorio");
        return ERR_NULL;
    }
    chain_result = storage_check_chain(check->volume, check->mount,
                                       entry->first_cluster, entry->size);
    if (chain_result != OK) {
        LOG_ERROR("FS", "Cadeia de entrada FAT32 inconsistente");
        return chain_result;
    }
    if (!(entry->attributes & STORAGE_ATTR_DIRECTORY) ||
        (entry->name[0] == '.' &&
         (entry->name[1] == '\0' ||
          (entry->name[1] == '.' && entry->name[2] == '\0')))) return OK;
    if (entry->first_cluster < STORAGE_FIRST_DATA_CLUSTER) {
        LOG_ERROR("FS", "Diretorio FAT32 sem cluster valido");
        return ERR_INVALID;
    }
    if (check->depth >= STORAGE_MAX_CHAIN_STEPS) {
        LOG_ERROR("FS", "Profundidade de diretorios FAT32 excedida");
        return ERR_OVERFLOW;
    }
    child = *check;
    child.depth++;
    return storage_walk_directory_long(
        check->volume, check->mount, entry->first_cluster, 0,
        storage_check_directory_visitor, &child);
}

static int storage_check_metadata(const storage_volume_t* volume,
                                  const storage_mount_t* mount) {
    uint8_t boot[STORAGE_SECTOR_SIZE];
    uint8_t backup_boot[STORAGE_SECTOR_SIZE];
    uint8_t fsinfo[STORAGE_SECTOR_SIZE];
    uint8_t backup_fsinfo[STORAGE_SECTOR_SIZE];
    uint16_t fsinfo_sector;
    uint16_t backup_sector;

    if (storage_read_relative(volume, 0, 1, boot) != OK) {
        return ERR_DISK;
    }
    fsinfo_sector = storage_read_u16(boot + 48U);
    backup_sector = storage_read_u16(boot + 50U);
    if (!fsinfo_sector || !backup_sector ||
        fsinfo_sector >= mount->total_sectors ||
        backup_sector >= mount->total_sectors ||
        storage_read_relative(volume, backup_sector, 1, backup_boot) != OK ||
        storage_read_relative(volume, fsinfo_sector, 1, fsinfo) != OK ||
        storage_read_relative(volume, backup_sector + 1U, 1,
                              backup_fsinfo) != OK) {
        return ERR_DISK;
    }
    if (!storage_buffers_equal(boot, backup_boot, STORAGE_SECTOR_SIZE) ||
        !storage_buffers_equal(fsinfo, backup_fsinfo, STORAGE_SECTOR_SIZE) ||
        storage_read_u32(fsinfo) != 0x41615252U ||
        storage_read_u32(fsinfo + 484U) != 0x61417272U ||
        storage_read_u32(fsinfo + 508U) != 0xAA550000U) {
        LOG_ERROR("FS", "BPB, backup ou FSInfo FAT32 divergente");
        return ERR_INVALID;
    }
    (void)mount;
    return OK;
}

int storage_check(const char* id) {
    storage_volume_t* volume;
    storage_mount_t* mount;
    uint8_t first[STORAGE_SECTOR_SIZE];
    uint8_t second[STORAGE_SECTOR_SIZE];
    storage_check_context_t check_context;
    int index;
    int result;

    if (!id) {
        LOG_ERROR("FS", "ID nulo na verificacao de volume");
        return ERR_NULL;
    }
    spinlock_acquire(&storage_operation_lock);
    index = storage_initialized ? storage_volume_index(id) : -1;
    if (index < 0 || !storage_volumes[index].mounted) {
        spinlock_release(&storage_operation_lock);
        LOG_ERROR("FS", "Volume nao encontrado ou desmontado na verificacao");
        return index < 0 ? ERR_NOT_FOUND : ERR_STATE;
    }
    volume = &storage_volumes[index];
    mount = storage_mount_for_volume((uint8_t)index);
    if (!mount || mount->fs_type != STORAGE_FS_FAT32) {
        spinlock_release(&storage_operation_lock);
        LOG_ERROR("FS", "Verificacao exige volume FAT32 montado");
        return ERR_UNAVAILABLE;
    }
    result = storage_check_metadata(volume, mount);
    for (uint32_t sector = 0; sector < mount->sectors_per_fat; sector++) {
        if (result != OK) break;
        result = storage_read_relative(volume, mount->fat_start + sector, 1,
                                       first);
        if (result != OK) break;
        result = storage_read_relative(
            volume, mount->fat_start + mount->sectors_per_fat + sector, 1,
            second);
        if (result != OK) break;
        for (uint32_t byte = 0; byte < STORAGE_SECTOR_SIZE; byte++) {
            if (first[byte] != second[byte]) {
                result = ERR_INVALID;
                break;
            }
        }
        if (result != OK) break;
    }
    if (result == OK) {
        check_context.volume = volume;
        check_context.mount = mount;
        check_context.depth = 0U;
        result = storage_walk_directory_long(
            volume, mount, mount->root_cluster, 0,
            storage_check_directory_visitor, &check_context);
    }
    spinlock_release(&storage_operation_lock);
    if (result != OK) {
        storage_log_volume(LOG_LEVEL_ERROR, id,
                           "verificacao FAT32 encontrou inconsistencias");
        return result;
    }
    storage_log_volume(LOG_LEVEL_INFO, id, "verificacao FAT32 concluida");
    return OK;
}

int storage_write_file(const char* id, const char* path,
                       const uint8_t* data, uint32_t size,
                       uint8_t attributes) {
    return storage_atomic_write_file(id, path, data, size, attributes,
                                     STORAGE_ATOMIC_CREATE_OR_REPLACE);
}

int storage_atomic_write_file(const char* id, const char* path,
                              const uint8_t* data, uint32_t size,
                              uint8_t attributes, storage_atomic_mode_t mode) {
    int result;

    if (!id || !path || (size && !data)) {
        LOG_ERROR("FS", "Argumento nulo na escrita atomica FAT32");
        return ERR_NULL;
    }
    if (mode != STORAGE_ATOMIC_CREATE_OR_REPLACE &&
        mode != STORAGE_ATOMIC_REPLACE_ONLY) {
        LOG_ERROR("FS", "Modo invalido na escrita atomica FAT32");
        return ERR_INVALID;
    }
    spinlock_acquire(&storage_operation_lock);
    result = storage_write_fat32_file_unlocked(id, path, data, size,
                                               attributes, mode);
    spinlock_release(&storage_operation_lock);
    if (result != OK) LOG_ERROR("FS", "Escrita atomica FAT32 falhou");
    return result;
}

int storage_delete_file(const char* id, const char* path) {
    storage_volume_t* volume;
    storage_mount_t* mount;
    storage_long_raw_entry_t entry;
    uint32_t directory_cluster;
    uint8_t fixed_root;
    int index;
    int result;

    if (!id || !path) {
        LOG_ERROR("FS", "Argumento nulo na exclusao FAT32");
        return ERR_NULL;
    }
    spinlock_acquire(&storage_operation_lock);
    result = storage_get_mounted_fat32(id, &volume, &mount, &index);
    if (result == OK) {
        (void)index;
        result = storage_find_file_long(volume, mount, path, &entry,
                                        &directory_cluster, &fixed_root);
        (void)directory_cluster;
        (void)fixed_root;
    }
    if (result == OK) {
        if (entry.attributes & STORAGE_ATTR_DIRECTORY) {
            result = ERR_INVALID;
        } else {
            result = storage_mark_directory_entries_deleted(volume, &entry);
            if (result == OK && entry.first_cluster) {
                result = storage_release_fat32_chain(volume, mount,
                                                     entry.first_cluster);
            }
        }
    }
    spinlock_release(&storage_operation_lock);
    if (result != OK) LOG_ERROR("FS", "Exclusao FAT32 falhou");
    return result;
}

int storage_create_dir(const char* id, const char* path) {
    return storage_atomic_write_file(id, path, 0, 0,
                                     STORAGE_ATTR_DIRECTORY,
                                     STORAGE_ATOMIC_CREATE_OR_REPLACE);
}

int storage_rename_file(const char* id, const char* path,
                        const char* new_name) {
    storage_volume_t* volume;
    storage_mount_t* mount;
    storage_long_raw_entry_t old_entry;
    storage_long_raw_entry_t new_entry;
    char directory[STORAGE_MAX_PATH];
    char old_name[STORAGE_LONG_NAME_SIZE];
    uint32_t directory_cluster;
    uint8_t fixed_root;
    uint8_t alias[11];
    uint16_t units[STORAGE_LONG_NAME_SIZE];
    uint32_t unit_count;
    uint32_t slot_start;
    int index;
    int result;

    if (!id || !path || !new_name) {
        LOG_ERROR("FS", "Argumento nulo na renomeacao FAT32");
        return ERR_NULL;
    }
    if (!new_name[0] || kstrlen(new_name) >= STORAGE_LONG_NAME_SIZE) {
        LOG_ERROR("FS", "Nome de destino FAT32 invalido");
        return ERR_INVALID;
    }
    for (uint32_t name_index = 0; new_name[name_index]; name_index++) {
        if (new_name[name_index] == '/' || new_name[name_index] == '\\') {
            LOG_ERROR("FS", "Nome de destino FAT32 contem separador");
            return ERR_INVALID;
        }
    }
    spinlock_acquire(&storage_operation_lock);
    result = storage_get_mounted_fat32(id, &volume, &mount, &index);
    if (result != OK) goto rename_done;
    (void)index;
    result = storage_split_file_path(path, directory, old_name);
    if (result != OK) goto rename_done;
    result = storage_resolve_directory(volume, mount, directory,
                                       &directory_cluster, &fixed_root);
    if (result != OK) goto rename_done;
    result = storage_find_entry_long(volume, mount, directory_cluster,
                                     fixed_root, old_name, &old_entry);
    if (result != OK) goto rename_done;
    if (storage_find_entry_long(volume, mount, directory_cluster, fixed_root,
                                new_name, &new_entry) == OK) {
        result = ERR_INVALID;
        goto rename_done;
    }
    result = storage_name_to_fat(new_name, alias);
    if (result != OK) {
        result = storage_name_to_fat(old_entry.short_name, alias);
    }
    if (result != OK) goto rename_done;
    result = storage_utf8_to_utf16(new_name, units, STORAGE_LONG_NAME_SIZE,
                                   &unit_count);
    if (result != OK) goto rename_done;
    uint32_t required = (unit_count + 12U) / 13U + 1U;
    result = storage_find_free_directory_slots(
        volume, mount, directory_cluster, required, &slot_start);
    if (result == OK) {
        result = storage_publish_fat32_entry(
            volume, 0, slot_start,
            new_name, alias, old_entry.first_cluster, old_entry.size,
            old_entry.attributes, required);
    }
    if (result == OK) {
        result = storage_mark_directory_entries_deleted(volume, &old_entry);
    }
rename_done:
    spinlock_release(&storage_operation_lock);
    if (result != OK) LOG_ERROR("FS", "Renomeacao FAT32 falhou");
    return result;
}

static void storage_slot_writer_build_entry(
    const storage_slot_writer_state_t* state, uint8_t entry[STORAGE_DIR_ENTRY_SIZE],
    uint32_t size) {
    uint8_t alias[11];

    kmemset(entry, 0, STORAGE_DIR_ENTRY_SIZE);
    if (!state || storage_name_to_fat(state->temp_path, alias) != OK) return;
    kmemcpy(entry, alias, sizeof(alias));
    entry[STORAGE_DIR_ATTRIBUTE_OFFSET] = state->attributes;
    entry[STORAGE_DIR_CLUSTER_HIGH_OFFSET] =
        (uint8_t)(state->first_cluster >> 16U);
    entry[STORAGE_DIR_CLUSTER_HIGH_OFFSET + 1U] =
        (uint8_t)(state->first_cluster >> 24U);
    entry[STORAGE_DIR_CLUSTER_LOW_OFFSET] = (uint8_t)state->first_cluster;
    entry[STORAGE_DIR_CLUSTER_LOW_OFFSET + 1U] =
        (uint8_t)(state->first_cluster >> 8U);
    entry[STORAGE_DIR_SIZE_OFFSET] = (uint8_t)size;
    entry[STORAGE_DIR_SIZE_OFFSET + 1U] = (uint8_t)(size >> 8U);
    entry[STORAGE_DIR_SIZE_OFFSET + 2U] = (uint8_t)(size >> 16U);
    entry[STORAGE_DIR_SIZE_OFFSET + 3U] = (uint8_t)(size >> 24U);
}

static int storage_slot_writer_update_entry_locked(
    const storage_slot_writer_state_t* state, uint32_t size) {
    storage_volume_t* volume;
    uint8_t entry[STORAGE_DIR_ENTRY_SIZE];

    if (!state || !state->active || state->volume_index >= storage_volume_count) {
        LOG_ERROR("FS", "Estado invalido ao atualizar slot FAT32");
        return ERR_STATE;
    }
    volume = &storage_volumes[state->volume_index];
    storage_slot_writer_build_entry(state, entry, size);
    return storage_write_directory_entry(volume, state->entry_offset, entry);
}

static int storage_slot_writer_flush_locked(void) {
    storage_slot_writer_state_t* state = &storage_slot_writer_state;
    storage_volume_t* volume;
    storage_mount_t* mount;
    uint32_t cluster;
    uint8_t linked = 0U;
    int result;

    if (!state->active || !state->buffered_size ||
        state->volume_index >= storage_volume_count) {
        return state->active ? OK : ERR_STATE;
    }
    volume = &storage_volumes[state->volume_index];
    mount = storage_mount_for_volume(state->volume_index);
    if (!mount || mount->fs_type != STORAGE_FS_FAT32) {
        LOG_ERROR("FS", "Montagem FAT32 ausente para slot");
        return ERR_UNAVAILABLE;
    }
    result = storage_allocate_fat32_cluster(volume, mount, &cluster);
    if (result != OK) {
        LOG_ERROR("FS", "Falha ao reservar cluster para slot");
        return result;
    }
    if (state->last_cluster) {
        result = storage_write_fat32_entry(volume, mount,
                                           state->last_cluster, cluster);
        if (result == OK) linked = 1U;
    } else {
        state->first_cluster = cluster;
        result = storage_slot_writer_update_entry_locked(
            state, state->flushed_size);
        if (result != OK) {
            storage_write_fat32_entry(volume, mount, cluster,
                                      STORAGE_FAT32_FREE);
            state->first_cluster = 0U;
            LOG_ERROR("FS", "Falha ao publicar primeiro cluster de slot");
            return result;
        }
    }
    if (result != OK && !linked) {
        storage_write_fat32_entry(volume, mount, cluster,
                                  STORAGE_FAT32_FREE);
        LOG_ERROR("FS", "Falha ao encadear cluster de slot");
        return result;
    }
    if (result == OK) {
        result = storage_write_cluster(volume, mount, cluster,
                                       state->buffer, state->buffered_size);
    }
    if (result != OK) {
        LOG_ERROR("FS", "Falha ao gravar cluster de slot");
        return result;
    }
    state->last_cluster = cluster;
    state->flushed_size += state->buffered_size;
    state->buffered_size = 0U;
    result = OK;
    if (state->cluster_bytes &&
        ((state->flushed_size / state->cluster_bytes) %
         STORAGE_SLOT_WRITER_METADATA_INTERVAL) == 0U) {
        result = storage_slot_writer_update_entry_locked(
            state, state->flushed_size);
    }
    if (result != OK) LOG_ERROR("FS", "Falha ao atualizar tamanho de slot");
    return result;
}

int storage_transaction_writer_begin(const char* id, const char* path,
                                     const char* temporary_path,
                                     uint32_t expected_size,
                                     uint8_t attributes) {
    storage_volume_t* volume;
    storage_mount_t* mount;
    storage_long_raw_entry_t existing;
    char directory[STORAGE_MAX_PATH];
    char filename[STORAGE_LONG_NAME_SIZE];
    uint32_t entry_offset;
    uint32_t cluster_bytes;
    uint8_t entry[STORAGE_DIR_ENTRY_SIZE];
    int index;
    int result;

    if (!id || !path || !temporary_path || !expected_size ||
        expected_size > STORAGE_SLOT_MAX_FILE_SIZE) {
        LOG_ERROR("FS", "Argumento invalido ao iniciar escritor de slot");
        return ERR_INVALID;
    }
    spinlock_acquire(&storage_operation_lock);
    if (storage_slot_writer_state.active) {
        spinlock_release(&storage_operation_lock);
        LOG_ERROR("FS", "Ja existe escritor de slot ativo");
        return ERR_STATE;
    }
    result = storage_get_mounted_fat32(id, &volume, &mount, &index);
    if (result != OK) goto slot_writer_begin_done;
    result = storage_split_file_path(path, directory, filename);
    if (result != OK || directory[0] ||
        storage_name_to_fat(filename, entry) != OK) {
        result = ERR_INVALID;
        goto slot_writer_begin_done;
    }
    if (kstrcmp(filename, temporary_path) == 0 ||
        storage_name_to_fat(temporary_path, entry) != OK) {
        result = ERR_INVALID;
        goto slot_writer_begin_done;
    }
    result = storage_find_entry_long(volume, mount, mount->root_cluster, 0,
                                     temporary_path, &existing);
    if (result == OK) {
        result = ERR_STATE;
        goto slot_writer_begin_done;
    }
    if (result != ERR_NOT_FOUND) goto slot_writer_begin_done;
    result = storage_find_entry_long(volume, mount, mount->root_cluster, 0,
                                     filename, &existing);
    if (result == OK && (existing.attributes & STORAGE_ATTR_DIRECTORY)) {
        result = ERR_INVALID;
        goto slot_writer_begin_done;
    }
    if (result != OK && result != ERR_NOT_FOUND) goto slot_writer_begin_done;
    cluster_bytes = mount->sectors_per_cluster * STORAGE_SECTOR_SIZE;
    if (!cluster_bytes || cluster_bytes > STORAGE_SLOT_WRITE_BUFFER_SIZE) {
        result = ERR_OVERFLOW;
        goto slot_writer_begin_done;
    }
    result = storage_find_free_directory_slots(
        volume, mount, mount->root_cluster, 1U, &entry_offset);
    if (result != OK) goto slot_writer_begin_done;

    kmemset(&storage_slot_writer_state, 0,
            sizeof(storage_slot_writer_state));
    storage_slot_writer_state.active = 1U;
    storage_slot_writer_state.expected_size = expected_size;
    storage_slot_writer_state.attributes = attributes;
    storage_slot_writer_state.volume_index = (uint8_t)index;
    storage_slot_writer_state.entry_offset = entry_offset;
    storage_slot_writer_state.cluster_bytes = cluster_bytes;
    storage_copy_text(storage_slot_writer_state.volume_id,
                      STORAGE_ID_SIZE, id);
    storage_copy_text(storage_slot_writer_state.target_path,
                      STORAGE_MAX_PATH, filename);
    storage_copy_text(storage_slot_writer_state.temp_path,
                      STORAGE_MAX_PATH, temporary_path);
    storage_slot_writer_build_entry(&storage_slot_writer_state, entry, 0U);
    result = storage_write_directory_entry(volume, entry_offset, entry);
    if (result != OK) goto slot_writer_begin_cleanup;
    spinlock_release(&storage_operation_lock);
    return OK;

slot_writer_begin_cleanup:
    kmemset(&storage_slot_writer_state, 0,
            sizeof(storage_slot_writer_state));
slot_writer_begin_done:
    spinlock_release(&storage_operation_lock);
    if (result != OK) LOG_ERROR("FS", "Inicio do escritor de slot falhou");
    return result;
}

int storage_transaction_writer_write(const uint8_t* data, uint32_t size) {
    storage_slot_writer_state_t* state = &storage_slot_writer_state;
    int result = OK;

    if (!data && size) {
        LOG_ERROR("FS", "Dados nulos na escrita do slot");
        return ERR_NULL;
    }
    spinlock_acquire(&storage_operation_lock);
    if (!state->active) {
        spinlock_release(&storage_operation_lock);
        LOG_ERROR("FS", "Escritor de slot nao esta ativo");
        return ERR_STATE;
    }
    if (size > state->expected_size - state->received_size) {
        spinlock_release(&storage_operation_lock);
        LOG_ERROR("FS", "Escrita do slot excedeu o tamanho esperado");
        return ERR_OVERFLOW;
    }
    while (size && result == OK) {
        uint32_t amount = state->cluster_bytes - state->buffered_size;
        if (amount > size) amount = size;
        kmemcpy(state->buffer + state->buffered_size, data, amount);
        state->buffered_size += amount;
        state->received_size += amount;
        data += amount;
        size -= amount;
        if (state->buffered_size == state->cluster_bytes) {
            result = storage_slot_writer_flush_locked();
        }
    }
    spinlock_release(&storage_operation_lock);
    if (result != OK) LOG_ERROR("FS", "Escrita do slot falhou");
    return result;
}

int storage_transaction_writer_finish(void) {
    char volume_id[STORAGE_ID_SIZE];
    char target_path[STORAGE_MAX_PATH];
    char temp_path[STORAGE_MAX_PATH];
    int result;

    spinlock_acquire(&storage_operation_lock);
    if (!storage_slot_writer_state.active) {
        spinlock_release(&storage_operation_lock);
        LOG_ERROR("FS", "Finalizacao sem escritor de slot ativo");
        return ERR_STATE;
    }
    if (storage_slot_writer_state.received_size !=
        storage_slot_writer_state.expected_size) {
        spinlock_release(&storage_operation_lock);
        LOG_ERROR("FS", "Escrita de slot incompleta");
        return ERR_INVALID;
    }
    result = storage_slot_writer_flush_locked();
    if (result == OK) {
        result = storage_slot_writer_update_entry_locked(
            &storage_slot_writer_state,
            storage_slot_writer_state.expected_size);
    }
    storage_copy_text(volume_id, sizeof(volume_id),
                      storage_slot_writer_state.volume_id);
    storage_copy_text(target_path, sizeof(target_path),
                      storage_slot_writer_state.target_path);
    storage_copy_text(temp_path, sizeof(temp_path),
                      storage_slot_writer_state.temp_path);
    kmemset(&storage_slot_writer_state, 0,
            sizeof(storage_slot_writer_state));
    spinlock_release(&storage_operation_lock);
    if (result != OK) {
        LOG_ERROR("FS", "Finalizacao do slot falhou antes da publicacao");
        return result;
    }
    result = storage_delete_file(volume_id, target_path);
    if (result != OK && result != ERR_NOT_FOUND) {
        LOG_ERROR("FS", "Falha ao substituir slot anterior");
        return result;
    }
    result = storage_rename_file(volume_id, temp_path, target_path);
    if (result != OK) LOG_ERROR("FS", "Falha ao publicar slot FAT32");
    return result;
}

int storage_transaction_writer_abort(void) {
    char volume_id[STORAGE_ID_SIZE];
    char temp_path[STORAGE_MAX_PATH];
    int result;

    spinlock_acquire(&storage_operation_lock);
    if (!storage_slot_writer_state.active) {
        spinlock_release(&storage_operation_lock);
        LOG_ERROR("FS", "Cancelamento sem escritor de slot ativo");
        return ERR_STATE;
    }
    storage_copy_text(volume_id, sizeof(volume_id),
                      storage_slot_writer_state.volume_id);
    storage_copy_text(temp_path, sizeof(temp_path),
                      storage_slot_writer_state.temp_path);
    kmemset(&storage_slot_writer_state, 0,
            sizeof(storage_slot_writer_state));
    spinlock_release(&storage_operation_lock);
    result = storage_delete_file(volume_id, temp_path);
    if (result == ERR_NOT_FOUND) result = OK;
    if (result != OK) LOG_ERROR("FS", "Falha ao abortar escritor de slot");
    return result;
}

int storage_transaction_writer_is_active(void) {
    int active;

    spinlock_acquire(&storage_operation_lock);
    active = storage_slot_writer_state.active ? 1 : 0;
    spinlock_release(&storage_operation_lock);
    return active;
}

int storage_slot_writer_begin(const char* id, const char* path,
                              uint32_t expected_size, uint8_t attributes) {
    return storage_transaction_writer_begin(id, path, "ZSTG.ZSY",
                                            expected_size, attributes);
}

int storage_slot_writer_write(const uint8_t* data, uint32_t size) {
    return storage_transaction_writer_write(data, size);
}

int storage_slot_writer_finish(void) {
    return storage_transaction_writer_finish();
}

int storage_slot_writer_abort(void) {
    return storage_transaction_writer_abort();
}

int storage_slot_writer_is_active(void) {
    return storage_transaction_writer_is_active();
}

int storage_stream_begin(const char* id, const char* path,
                         uint32_t expected_size, uint8_t attributes) {
    storage_volume_t* volume;
    storage_mount_t* mount;
    int index;
    int result;

    if (!id || !path) {
        LOG_ERROR("FS", "Argumento nulo no inicio streaming FAT32");
        return ERR_NULL;
    }
    if (!expected_size || expected_size > FS_MAX_STREAM_FILE_SIZE) {
        LOG_ERROR("FS", "Tamanho invalido no inicio streaming FAT32");
        return ERR_OVERFLOW;
    }
    spinlock_acquire(&storage_operation_lock);
    if (storage_stream_state.active) {
        spinlock_release(&storage_operation_lock);
        LOG_ERROR("FS", "Ja existe escrita streaming FAT32 ativa");
        return ERR_STATE;
    }
    result = storage_get_mounted_fat32(id, &volume, &mount, &index);
    (void)volume;
    (void)mount;
    (void)index;
    if (result == OK) {
        kmemset(&storage_stream_state, 0, sizeof(storage_stream_state));
        storage_stream_state.buffer = (uint8_t*)kmalloc(expected_size);
        if (!storage_stream_state.buffer) {
            result = ERR_MEM;
            LOG_ERROR("FS", "Memoria insuficiente para buffer FAT32");
        } else {
            storage_stream_state.active = 1U;
            storage_stream_state.expected_size = expected_size;
            storage_stream_state.attributes = attributes;
            storage_copy_text(storage_stream_state.volume_id,
                              STORAGE_ID_SIZE, id);
            storage_copy_text(storage_stream_state.path,
                              STORAGE_MAX_PATH, path);
        }
    }
    spinlock_release(&storage_operation_lock);
    if (result != OK) LOG_ERROR("FS", "Inicio streaming FAT32 falhou");
    return result;
}

int storage_stream_write(const uint8_t* data, uint32_t size) {
    if (!data && size) {
        LOG_ERROR("FS", "Dados nulos na escrita streaming FAT32");
        return ERR_NULL;
    }
    spinlock_acquire(&storage_operation_lock);
    if (!storage_stream_state.active) {
        spinlock_release(&storage_operation_lock);
        LOG_ERROR("FS", "Streaming FAT32 nao esta ativo");
        return ERR_STATE;
    }
    if (size > storage_stream_state.expected_size -
        storage_stream_state.written_size) {
        spinlock_release(&storage_operation_lock);
        LOG_ERROR("FS", "Streaming FAT32 excedeu o tamanho esperado");
        return ERR_OVERFLOW;
    }
    kmemcpy(storage_stream_state.buffer + storage_stream_state.written_size,
            data, size);
    storage_stream_state.written_size += size;
    spinlock_release(&storage_operation_lock);
    return OK;
}

int storage_stream_finish(void) {
    storage_stream_state_t state;
    int result;

    spinlock_acquire(&storage_operation_lock);
    if (!storage_stream_state.active) {
        spinlock_release(&storage_operation_lock);
        LOG_ERROR("FS", "Finalizacao sem streaming FAT32 ativo");
        return ERR_STATE;
    }
    if (storage_stream_state.written_size !=
        storage_stream_state.expected_size) {
        spinlock_release(&storage_operation_lock);
        LOG_ERROR("FS", "Streaming FAT32 incompleto");
        return ERR_INVALID;
    }
    state = storage_stream_state;
    kmemset(&storage_stream_state, 0, sizeof(storage_stream_state));
    spinlock_release(&storage_operation_lock);
    result = storage_atomic_write_file(
        state.volume_id, state.path, state.buffer, state.expected_size,
        state.attributes, STORAGE_ATOMIC_CREATE_OR_REPLACE);
    kfree(state.buffer);
    state.buffer = 0;
    if (result != OK) LOG_ERROR("FS", "Finalizacao streaming FAT32 falhou");
    return result;
}

int storage_stream_abort(void) {
    spinlock_acquire(&storage_operation_lock);
    if (!storage_stream_state.active) {
        spinlock_release(&storage_operation_lock);
        LOG_ERROR("FS", "Cancelamento sem streaming FAT32 ativo");
        return ERR_STATE;
    }
    kfree(storage_stream_state.buffer);
    storage_stream_state.buffer = 0;
    kmemset(&storage_stream_state, 0, sizeof(storage_stream_state));
    spinlock_release(&storage_operation_lock);
    return OK;
}

int storage_stream_is_active(void) {
    int active;

    spinlock_acquire(&storage_operation_lock);
    active = storage_stream_state.active ? 1 : 0;
    spinlock_release(&storage_operation_lock);
    return active;
}

const char* storage_fs_name(storage_fs_type_t type) {
    if (type == STORAGE_FS_FAT12) return "FAT12";
    if (type == STORAGE_FS_FAT32) return "FAT32";
    return "UNKNOWN";
}

const char* storage_volume_state_name(storage_volume_state_t state) {
    if (state == STORAGE_VOLUME_DETECTED) return "DETECTED";
    if (state == STORAGE_VOLUME_INVALID) return "INVALID";
    if (state == STORAGE_VOLUME_UNSUPPORTED) return "UNSUPPORTED";
    if (state == STORAGE_VOLUME_MOUNTED) return "MOUNTED";
    return "UNKNOWN";
}
