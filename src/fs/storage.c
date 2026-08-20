#include "fs/storage.h"
#include "fs/fs.h"
#include "drivers/ata.h"
#include "core/errors.h"
#include "core/log.h"
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
#define STORAGE_FAT32_MIN_CLUSTERS 65525U
#define STORAGE_FAT12_END 0x0FF8U
#define STORAGE_FAT12_BAD 0x0FF7U
#define STORAGE_FAT32_END 0x0FFFFFF8U
#define STORAGE_FAT32_BAD 0x0FFFFFF7U
#define STORAGE_DIR_ENTRY_SIZE 32U
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

int storage_init(void) {
    char boot_disk_id[STORAGE_DISK_ID_SIZE];
    uint32_t block_count = 0U;
    int block_result;

    LOG_INFO("FS", "Inicializando inventario de armazenamento");
    spinlock_init(&storage_registry_lock);
    spinlock_init(&storage_operation_lock);
    kmemset(storage_disks, 0, sizeof(storage_disks));
    kmemset(storage_volumes, 0, sizeof(storage_volumes));
    kmemset(storage_mounts, 0, sizeof(storage_mounts));
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
        if (raw_result == OK) continue;
        if (sector[STORAGE_MBR_SIGNATURE_OFFSET] == 0x55U &&
            sector[STORAGE_MBR_SIGNATURE_OFFSET + 1U] == 0xAAU) {
            storage_scan_mbr(block.id, block.sector_count, sector);
        } else {
            storage_disks[storage_disk_count - 1U].last_error = ERR_INVALID;
            storage_log_volume(
                LOG_LEVEL_WARN,
                storage_disks[storage_disk_count - 1U].id,
                "sem BPB FAT ou assinatura MBR valida");
        }
    }

    storage_initialized = 1;
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
    int result;

    if (!storage_initialized) {
        LOG_ERROR("FS", "Atualizacao de storage antes da inicializacao");
        return ERR_STATE;
    }
    LOG_INFO("FS", "Atualizando inventario de armazenamento");
    storage_refresh_epoch++;
    if (!storage_refresh_epoch) storage_refresh_epoch = 1U;
    result = storage_init();
    for (uint8_t index = 0U; index < storage_volume_count; index++) {
        storage_volumes[index].generation += storage_refresh_epoch;
    }
    return result;
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
    uint8_t wanted[11];
    storage_raw_entry_t entry;
    uint8_t found;
} storage_find_context_t;

static void storage_find_visitor(const storage_raw_entry_t* entry,
                                 void* context) {
    storage_find_context_t* find = (storage_find_context_t*)context;

    if (find->found) return;
    for (uint32_t index = 0; index < 11U; index++) {
        if (entry->name[index] != find->wanted[index]) return;
    }
    find->entry = *entry;
    find->found = 1;
}

static int storage_find_entry(const storage_volume_t* volume,
                              const storage_mount_t* mount,
                              uint32_t directory_cluster, uint8_t fixed_root,
                              const char* name, storage_raw_entry_t* output) {
    storage_find_context_t context;
    int result;

    kmemset(&context, 0, sizeof(context));
    result = storage_name_to_fat(name, context.wanted);
    if (result != OK) return result;
    result = storage_walk_directory(volume, mount, directory_cluster,
                                    fixed_root, storage_find_visitor, &context);
    if (result != OK) return result;
    if (!context.found) {
        LOG_WARN("FS", "Entrada 8.3 nao encontrada");
        return ERR_NOT_FOUND;
    }
    if (output) *output = context.entry;
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
        char component[STORAGE_NAME_SIZE];
        storage_raw_entry_t entry;
        int result = storage_next_component(&cursor, component,
                                            sizeof(component));

        if (result != OK) return result;
        if (!component[0]) break;
        if (component[0] == '.' && component[1] == '\0') continue;
        if (component[0] == '.' && component[1] == '.' &&
            component[2] == '\0') return ERR_INVALID;
        result = storage_find_entry(volume, mount, cluster, fixed_root,
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
        storage_copy_text(filename, STORAGE_NAME_SIZE, path);
        return OK;
    }
    for (int index = 0; index < separator; index++) directory[index] = path[index];
    directory[separator] = '\0';
    storage_copy_text(filename, STORAGE_NAME_SIZE, path + separator + 1);
    return OK;
}

static int storage_find_file(const storage_volume_t* volume,
                             const storage_mount_t* mount, const char* path,
                             storage_raw_entry_t* out_entry) {
    char directory[STORAGE_MAX_PATH];
    char filename[STORAGE_NAME_SIZE];
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
    result = storage_find_entry(volume, mount, cluster, fixed_root,
                                filename, out_entry);
    if (result != OK) return result;
    if (out_entry->attributes & STORAGE_ATTR_DIRECTORY) return ERR_INVALID;
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
    volume->read_only = 1;
    volume->state = STORAGE_VOLUME_MOUNTED;
    volume->last_error = OK;
    volume->generation++;
    storage_mounted_count++;
    spinlock_release(&storage_registry_lock);
    storage_log_volume(LOG_LEVEL_INFO, volume->id,
                       "montado em modo somente-leitura");
    spinlock_release(&storage_operation_lock);
    return OK;
}

int storage_unmount(const char* id) {
    storage_mount_t* mount;
    storage_volume_t* volume;
    int index;

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
    if (max_size > STORAGE_MAX_VIEW_BYTES) {
        LOG_ERROR("FS", "Faixa de visualizacao excede o limite");
        return ERR_OVERFLOW;
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
