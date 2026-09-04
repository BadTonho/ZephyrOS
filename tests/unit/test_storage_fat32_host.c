#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "drivers/ata.h"
#include "fs/block.h"
#include "fs/block_cache.h"
#include "fs/fs.h"
#include "fs/storage.h"

#define HOST_SECTOR_COUNT 5000U
#define HOST_PARTITION_START 1U
#define HOST_PARTITION_SECTORS 4999U
#define HOST_RESERVED_SECTORS 32U
#define HOST_FAT_COUNT 2U
#define HOST_SECTORS_PER_FAT 40U
#define HOST_DATA_START (HOST_RESERVED_SECTORS + HOST_FAT_COUNT * HOST_SECTORS_PER_FAT)
#define HOST_ROOT_CLUSTER 2U
#define HOST_FILE_CLUSTER 3U
#define HOST_DIRECTORY_CLUSTER 4U
#define HOST_NESTED_CLUSTER 5U
#define HOST_BAD_CLUSTER 6U
#define HOST_FAT32_END 0x0FFFFFF8U
#define HOST_FAT32_BAD 0x0FFFFFF7U
#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U

static uint8_t disk_image[HOST_SECTOR_COUNT][STORAGE_SECTOR_SIZE];
static uint8_t host_allocation[STORAGE_SLOT_WRITE_BUFFER_SIZE];
static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint32_t fake_read_ops;
static uint32_t fake_write_ops;
static uint32_t fake_log_count;
static block_device_t fake_block;
static ata_device_t fake_ata;

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

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:storage:storage-fat32|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:storage:storage-fat32|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:storage:storage-fat32|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "storage-fat32-host: falhou: %s\n", expression);
        (void)fflush(stderr);
        __builtin_trap();
    }
}

#define EXPECT(expression) expect_true((expression), #expression)

static void write_u16(uint8_t* destination, uint32_t offset,
                      uint16_t value) {
    destination[offset] = (uint8_t)value;
    destination[offset + 1U] = (uint8_t)(value >> 8U);
}

static void write_u32(uint8_t* destination, uint32_t offset,
                      uint32_t value) {
    destination[offset] = (uint8_t)value;
    destination[offset + 1U] = (uint8_t)(value >> 8U);
    destination[offset + 2U] = (uint8_t)(value >> 16U);
    destination[offset + 3U] = (uint8_t)(value >> 24U);
}

static uint32_t cluster_lba(uint32_t cluster) {
    return HOST_PARTITION_START + HOST_DATA_START +
           cluster - HOST_ROOT_CLUSTER;
}

static void write_entry(uint8_t* sector, uint32_t index, const char* name,
                        const char* extension, uint8_t attributes,
                        uint32_t cluster, uint32_t size) {
    uint8_t* entry = sector + index * 32U;

    memset(entry, ' ', 11U);
    memset(entry + 11U, 0, 21U);
    for (uint32_t offset = 0U; offset < 8U && name && name[offset]; offset++) {
        entry[offset] = (uint8_t)name[offset];
    }
    for (uint32_t offset = 0U; offset < 3U && extension && extension[offset];
         offset++) {
        entry[8U + offset] = (uint8_t)extension[offset];
    }
    entry[11U] = attributes;
    write_u16(entry, 20U, (uint16_t)(cluster >> 16U));
    write_u16(entry, 26U, (uint16_t)cluster);
    write_u32(entry, 28U, size);
}

static void set_fat(uint32_t cluster, uint32_t value) {
    for (uint32_t copy = 0U; copy < HOST_FAT_COUNT; copy++) {
        uint8_t* fat = disk_image[HOST_PARTITION_START + HOST_RESERVED_SECTORS +
                                   copy * HOST_SECTORS_PER_FAT];
        write_u32(fat, cluster * 4U, value);
    }
}

static void setup_disk(void) {
    uint8_t* mbr;
    uint8_t* bpb;
    uint8_t* fsinfo;
    uint8_t* root;
    uint8_t* nested;

    memset(disk_image, 0, sizeof(disk_image));
    mbr = disk_image[0];
    mbr[446U] = 0x80U;
    mbr[450U] = 0x0CU;
    write_u32(mbr, 454U, HOST_PARTITION_START);
    write_u32(mbr, 458U, HOST_PARTITION_SECTORS);
    mbr[510U] = 0x55U;
    mbr[511U] = 0xAAU;

    bpb = disk_image[HOST_PARTITION_START];
    bpb[0] = 0xEBU;
    bpb[2] = 0x90U;
    memcpy(bpb + 3U, "HOSTFAT", 7U);
    write_u16(bpb, 11U, STORAGE_SECTOR_SIZE);
    bpb[13U] = 1U;
    write_u16(bpb, 14U, HOST_RESERVED_SECTORS);
    bpb[16U] = HOST_FAT_COUNT;
    write_u16(bpb, 17U, 0U);
    write_u16(bpb, 19U, 0U);
    bpb[21U] = 0xF8U;
    write_u16(bpb, 22U, 0U);
    write_u32(bpb, 32U, HOST_PARTITION_SECTORS);
    write_u32(bpb, 36U, HOST_SECTORS_PER_FAT);
    write_u32(bpb, 44U, HOST_ROOT_CLUSTER);
    write_u16(bpb, 48U, 1U);
    write_u16(bpb, 50U, 6U);
    memcpy(bpb + 71U, "HOSTVOL    ", 11U);
    memcpy(bpb + 82U, "FAT32   ", 8U);
    bpb[510U] = 0x55U;
    bpb[511U] = 0xAAU;
    memcpy(disk_image[HOST_PARTITION_START + 6U], bpb, STORAGE_SECTOR_SIZE);

    fsinfo = disk_image[HOST_PARTITION_START + 1U];
    write_u32(fsinfo, 0U, 0x41615252U);
    write_u32(fsinfo, 484U, 0x61417272U);
    write_u32(fsinfo, 508U, 0xAA550000U);
    memcpy(disk_image[HOST_PARTITION_START + 7U], fsinfo,
           STORAGE_SECTOR_SIZE);

    set_fat(0U, 0x0FFFFFF8U);
    set_fat(1U, 0x0FFFFFFFU);
    set_fat(HOST_ROOT_CLUSTER, HOST_FAT32_END);
    set_fat(HOST_FILE_CLUSTER, HOST_FAT32_END);
    set_fat(HOST_DIRECTORY_CLUSTER, HOST_FAT32_END);
    set_fat(HOST_NESTED_CLUSTER, HOST_FAT32_END);
    set_fat(HOST_BAD_CLUSTER, HOST_FAT32_END);

    root = disk_image[cluster_lba(HOST_ROOT_CLUSTER)];
    write_entry(root, 0U, "HELLO", "TXT", 0x20U, HOST_FILE_CLUSTER, 5U);
    write_entry(root, 1U, "DIR", "", 0x10U, HOST_DIRECTORY_CLUSTER, 0U);
    root[2U * 32U] = 0U;
    nested = disk_image[cluster_lba(HOST_DIRECTORY_CLUSTER)];
    write_entry(nested, 0U, ".", "", 0x10U, HOST_DIRECTORY_CLUSTER, 0U);
    write_entry(nested, 1U, "..", "", 0x10U, HOST_ROOT_CLUSTER, 0U);
    write_entry(nested, 2U, "NEST", "TXT", 0x20U, HOST_NESTED_CLUSTER, 4U);
    nested[3U * 32U] = 0U;
    memcpy(disk_image[cluster_lba(HOST_FILE_CLUSTER)], "hello", 5U);
    memcpy(disk_image[cluster_lba(HOST_NESTED_CLUSTER)], "nest", 4U);

    memset(&fake_block, 0, sizeof(fake_block));
    memcpy(fake_block.id, "ata0", 5U);
    memcpy(fake_block.model, "host-fat32-storage", 19U);
    fake_block.provider = BLOCK_PROVIDER_ATA;
    fake_block.sector_count = HOST_SECTOR_COUNT;
    fake_block.sector_size = STORAGE_SECTOR_SIZE;
    fake_block.online = 1U;
    fake_block.max_transfer_sectors = BLOCK_MAX_TRANSFER_SECTORS;
    fake_block.capabilities = BLOCK_DEVICE_CAPABILITIES_SUPPORTED;

    memset(&fake_ata, 0, sizeof(fake_ata));
    fake_ata.slot = 0U;
    fake_ata.sectors = HOST_SECTOR_COUNT;
    fake_ata.present = 1;
    fake_read_ops = 0U;
    fake_write_ops = 0U;
    fake_log_count = 0U;
    coverage_count = 0U;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
    fake_log_count++;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
    fake_log_count++;
}

void* kmalloc(uint32_t size) {
    if (!size || size > sizeof(host_allocation)) return 0;
    memset(host_allocation, 0, sizeof(host_allocation));
    return host_allocation;
}

void kfree(void* pointer) {
    if (pointer == host_allocation) memset(host_allocation, 0,
                                           sizeof(host_allocation));
}

uint8_t fs_get_type(void) { return FS_TYPE_NONE; }

ata_device_t* ata_get_device(void) { return &fake_ata; }

int block_cache_clear(void) { return OK; }
int block_cache_sync_device(const char* id) { return id ? OK : ERR_NULL; }
int block_cache_sync_all_until(uint32_t deadline_tick) {
    return deadline_tick ? OK : ERR_TIMEOUT;
}
int block_cache_invalidate_device(const char* device_id) {
    return device_id ? OK : ERR_NULL;
}
uint32_t block_cache_get_pending_count(void) { return 0U; }
int block_cache_validate_state(void) { return OK; }

int block_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = 1U;
    return OK;
}

int block_get_at(uint32_t index, block_device_t* out_device) {
    if (!out_device) return ERR_NULL;
    if (index != 0U) return ERR_NOT_FOUND;
    *out_device = fake_block;
    out_device->read_ops = fake_read_ops;
    out_device->write_ops = fake_write_ops;
    return OK;
}

int block_find(const char* id, block_device_t* out_device) {
    if (!id || !out_device) return ERR_NULL;
    if (strcmp(id, fake_block.id) != 0) return ERR_NOT_FOUND;
    *out_device = fake_block;
    out_device->read_ops = fake_read_ops;
    out_device->write_ops = fake_write_ops;
    return OK;
}

int block_read(const char* id, uint32_t lba, uint8_t count, uint8_t* buffer) {
    if (!id || !buffer || !count) return ERR_NULL;
    if (strcmp(id, fake_block.id) != 0 || lba >= HOST_SECTOR_COUNT ||
        count > HOST_SECTOR_COUNT - lba) return ERR_DISK;
    memcpy(buffer, disk_image[lba], (uint32_t)count * STORAGE_SECTOR_SIZE);
    fake_read_ops += count;
    return OK;
}

int block_write(const char* id, uint32_t lba, uint8_t count,
                const uint8_t* buffer) {
    if (!id || !buffer || !count) return ERR_NULL;
    if (strcmp(id, fake_block.id) != 0 || lba >= HOST_SECTOR_COUNT ||
        count > HOST_SECTOR_COUNT - lba) return ERR_DISK;
    memcpy(disk_image[lba], buffer, (uint32_t)count * STORAGE_SECTOR_SIZE);
    fake_write_ops += count;
    return OK;
}

static void fill_payload(uint8_t* payload, uint32_t size) {
    for (uint32_t index = 0U; index < size; index++) {
        payload[index] = (uint8_t)(index * 13U + 7U);
    }
}

int main(void) {
    storage_status_t status;
    storage_volume_t volume;
    uint8_t payload[600U];
    uint8_t buffer[640U];
    uint32_t bytes_read;
    uint32_t size;
    uint8_t attributes;

    setup_disk();
    coverage_active = 1U;
    disk_image[HOST_PARTITION_START][510U] = 0U;
    EXPECT(storage_init() == OK);
    EXPECT(storage_find_volume("ata0p1", &volume) == OK);
    EXPECT(volume.state == STORAGE_VOLUME_INVALID);
    disk_image[HOST_PARTITION_START][510U] = 0x55U;
    EXPECT(storage_refresh() == OK);
    EXPECT(storage_get_status(&status) == OK);
    EXPECT(status.initialized == 1U && status.volume_count == 1U);
    EXPECT(storage_mount("ata0p1") == OK);
    EXPECT(storage_find_volume("ata0p1", &volume) == OK);
    EXPECT(volume.read_only == 0U && volume.mounted == 1U);

    EXPECT(storage_check("ata0p1") == OK);
    disk_image[HOST_PARTITION_START + 6U][100U] ^= 1U;
    EXPECT(storage_check("ata0p1") == ERR_INVALID);
    disk_image[HOST_PARTITION_START + 6U][100U] ^= 1U;
    set_fat(HOST_NESTED_CLUSTER, HOST_FAT32_BAD);
    EXPECT(storage_check("ata0p1") == ERR_DISK);
    set_fat(HOST_NESTED_CLUSTER, HOST_FAT32_END);
    EXPECT(storage_check("ata0p1") == OK);

    fill_payload(payload, sizeof(payload));
    EXPECT(storage_write_file("ata0p1", "LONG-FILE.TXT", payload,
                               sizeof(payload), 0x20U) == OK);
    EXPECT(storage_get_file_info("ata0p1", "LONG-FILE.TXT", &size,
                                 &attributes) == OK);
    EXPECT(size == sizeof(payload) && attributes == 0x20U);
    EXPECT(storage_read_file_range("ata0p1", "LONG-FILE.TXT", 0U, buffer,
                                   sizeof(buffer), &bytes_read) == OK);
    EXPECT(bytes_read == sizeof(payload));
    EXPECT(memcmp(buffer, payload, sizeof(payload)) == 0);
    EXPECT(storage_delete_file("ata0p1", "LONG-FILE.TXT") == OK);
    EXPECT(storage_get_file_info("ata0p1", "LONG-FILE.TXT", &size,
                                 &attributes) == ERR_NOT_FOUND);

    EXPECT(storage_write_file("ata0p1", "HELLO.TXT", payload, 4U,
                               0x20U) == OK);
    EXPECT(storage_read_file_range("ata0p1", "HELLO.TXT", 0U, buffer,
                                   sizeof(buffer), &bytes_read) == OK);
    EXPECT(bytes_read == 4U && memcmp(buffer, payload, 4U) == 0);
    EXPECT(storage_delete_file("ata0p1", "HELLO.TXT") == OK);

    EXPECT(storage_transaction_writer_begin("ata0p1", "TARGET.TXT",
                                             "TEMP.TMP", sizeof(payload),
                                             0x20U) == OK);
    EXPECT(storage_transaction_writer_is_active() == 1);
    EXPECT(storage_transaction_writer_write(payload, sizeof(payload)) == OK);
    EXPECT(storage_transaction_writer_finish() == OK);
    EXPECT(storage_transaction_writer_is_active() == 0);
    EXPECT(storage_read_file_range("ata0p1", "TARGET.TXT", 0U, buffer,
                                   sizeof(buffer), &bytes_read) == OK);
    EXPECT(bytes_read == sizeof(payload));
    EXPECT(memcmp(buffer, payload, sizeof(payload)) == 0);
    EXPECT(storage_delete_file("ata0p1", "TARGET.TXT") == OK);

    EXPECT(storage_slot_writer_begin("ata0p1", "SLOT.TXT", 3U,
                                     0x20U) == OK);
    EXPECT(storage_slot_writer_write(payload, 3U) == OK);
    EXPECT(storage_slot_writer_finish() == OK);
    EXPECT(storage_read_file_range("ata0p1", "SLOT.TXT", 0U, buffer,
                                   sizeof(buffer), &bytes_read) == OK);
    EXPECT(bytes_read == 3U && memcmp(buffer, payload, 3U) == 0);
    EXPECT(storage_delete_file("ata0p1", "SLOT.TXT") == OK);

    EXPECT(storage_slot_writer_begin("ata0p1", "CANCEL.TXT", 3U,
                                     0x20U) == OK);
    EXPECT(storage_slot_writer_write(payload, 3U) == OK);
    EXPECT(storage_slot_writer_abort() == OK);
    EXPECT(storage_slot_writer_is_active() == 0);

    EXPECT(storage_transaction_writer_begin("ata0p1", "ABORT.TXT",
                                             "ABORT.TMP", 3U, 0x20U) == OK);
    EXPECT(storage_transaction_writer_write(payload, 3U) == OK);
    EXPECT(storage_transaction_writer_abort() == OK);
    EXPECT(storage_transaction_writer_is_active() == 0);
    EXPECT(storage_get_file_info("ata0p1", "ABORT.TMP", &size,
                                 &attributes) == ERR_NOT_FOUND);
    EXPECT(storage_check("ata0p1") == OK);
    EXPECT(fake_read_ops > 0U && fake_write_ops > 0U);
    EXPECT(fake_log_count > 0U);

    coverage_active = 0U;
    coverage_emit(0);
    puts("storage-fat32-host: PASS");
    return 0;
}
