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

#define HOST_SECTOR_COUNT 1000U
#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_PARTITION_START 1U
#define HOST_PARTITION_SECTORS 999U
#define HOST_BPB_SECTOR 1U
#define HOST_FAT_START 2U
#define HOST_ROOT_START 6U
#define HOST_DATA_START 7U
#define HOST_FILE_CLUSTER 2U
#define HOST_DIRECTORY_CLUSTER 3U
#define HOST_NESTED_CLUSTER 4U

static uint8_t disk_image[HOST_SECTOR_COUNT][STORAGE_SECTOR_SIZE];
static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint32_t fake_read_ops;
static uint32_t fake_write_ops;
static uint32_t fake_log_count;
static uint8_t fake_cache_busy;
static uint8_t fake_cache_sync_failed;
static uint8_t host_allocation[STORAGE_SECTOR_SIZE];
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
    printf("ZCOV_BEGIN|case=host:storage:storage|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:storage:storage|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:storage:storage|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "storage-host: falhou: %s\n", expression);
        (void)fflush(stderr);
        __builtin_trap();
    }
}

#define EXPECT(expression) expect_true((expression), #expression)

static void set_text(char* destination, uint32_t capacity,
                     const char* source) {
    if (!destination || !capacity) return;
    if (!source) source = "";
    while (*source && capacity > 1U) {
        *destination++ = *source++;
        capacity--;
    }
    *destination = '\0';
}

static void write_u16(uint8_t* sector, uint32_t offset, uint16_t value) {
    sector[offset] = (uint8_t)value;
    sector[offset + 1U] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t* sector, uint32_t offset, uint32_t value) {
    sector[offset] = (uint8_t)value;
    sector[offset + 1U] = (uint8_t)(value >> 8);
    sector[offset + 2U] = (uint8_t)(value >> 16);
    sector[offset + 3U] = (uint8_t)(value >> 24);
}

static void write_short_name(uint8_t* entry, const char* name,
                             const char* extension) {
    memset(entry, ' ', 11U);
    for (uint32_t index = 0U; index < 8U && name && name[index]; index++) {
        entry[index] = (uint8_t)name[index];
    }
    for (uint32_t index = 0U; index < 3U && extension && extension[index];
         index++) {
        entry[8U + index] = (uint8_t)extension[index];
    }
}

static void write_directory_entry(uint8_t* sector, uint32_t index,
                                  const char* name, const char* extension,
                                  uint8_t attributes, uint32_t cluster,
                                  uint32_t size) {
    uint8_t* entry = sector + index * 32U;

    memset(entry, 0, 32U);
    write_short_name(entry, name, extension);
    entry[11] = attributes;
    write_u16(entry, 26U, (uint16_t)cluster);
    write_u32(entry, 28U, size);
}

static void setup_disk(void) {
    uint8_t* mbr = disk_image[0];
    uint8_t* bpb = disk_image[HOST_BPB_SECTOR];
    uint8_t* root = disk_image[HOST_ROOT_START];
    uint8_t* nested = disk_image[HOST_DATA_START + 1U];

    memset(disk_image, 0, sizeof(disk_image));
    mbr[446U] = 0x80U;
    mbr[450U] = 0x01U;
    write_u32(mbr, 454U, HOST_PARTITION_START);
    write_u32(mbr, 458U, HOST_PARTITION_SECTORS);
    mbr[510U] = 0x55U;
    mbr[511U] = 0xAAU;

    bpb[0] = 0xEBU;
    bpb[2] = 0x90U;
    write_u16(bpb, 11U, STORAGE_SECTOR_SIZE);
    bpb[13U] = 1U;
    write_u16(bpb, 14U, 1U);
    bpb[16U] = 1U;
    write_u16(bpb, 17U, 16U);
    write_u16(bpb, 19U, HOST_PARTITION_SECTORS);
    bpb[21U] = 0xF8U;
    write_u16(bpb, 22U, 4U);
    memcpy(bpb + 43U, "TESTVOL    ", 11U);
    bpb[510U] = 0x55U;
    bpb[511U] = 0xAAU;

    disk_image[HOST_FAT_START][3] = 0xFFU;
    disk_image[HOST_FAT_START][4] = 0xFFU;
    disk_image[HOST_FAT_START][5] = 0xFFU;
    disk_image[HOST_FAT_START][6] = 0xFFU;
    disk_image[HOST_FAT_START][7] = 0x0FU;

    write_directory_entry(root, 0U, "HELLO", "TXT", 0x20U,
                          HOST_FILE_CLUSTER, 5U);
    write_directory_entry(root, 1U, "DIR", "", 0x10U,
                          HOST_DIRECTORY_CLUSTER, 0U);
    root[2U * 32U] = 0xE5U;
    root[3U * 32U] = 'L';
    root[3U * 32U + 11U] = 0x0FU;
    root[4U * 32U] = 'V';
    root[4U * 32U + 11U] = 0x08U;
    root[5U * 32U] = 0U;

    write_directory_entry(nested, 0U, ".", "", 0x10U,
                          HOST_DIRECTORY_CLUSTER, 0U);
    write_directory_entry(nested, 1U, "..", "", 0x10U, 0U, 0U);
    write_directory_entry(nested, 2U, "NEST", "TXT", 0x20U,
                          HOST_NESTED_CLUSTER, 4U);
    nested[3U * 32U] = 0U;

    memcpy(disk_image[HOST_DATA_START] + 0U, "hello", 5U);
    memcpy(disk_image[HOST_DATA_START + 2U] + 0U, "nest", 4U);

    memset(&fake_block, 0, sizeof(fake_block));
    set_text(fake_block.id, sizeof(fake_block.id), "ata0");
    set_text(fake_block.model, sizeof(fake_block.model), "host-disk");
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

uint8_t fs_get_type(void) {
    return FS_TYPE_NONE;
}

ata_device_t* ata_get_device(void) {
    return &fake_ata;
}

int block_cache_clear(void) {
    return fake_cache_busy ? ERR_STATE : OK;
}

int block_cache_sync_device(const char* id) {
    if (!id) return ERR_NULL;
    return fake_cache_sync_failed ? ERR_TIMEOUT : OK;
}

int block_cache_sync_all_until(uint32_t deadline_tick) {
    return fake_cache_sync_failed || deadline_tick == 0U ? ERR_TIMEOUT : OK;
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
    fake_block.read_ops = fake_read_ops;
    fake_block.write_ops = fake_write_ops;
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

int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    return block_read("ata0", lba, count, buffer);
}

static void expect_entry_names(storage_dir_entry_t* entries, uint32_t count) {
    EXPECT(count == 2U);
    EXPECT(strcmp(entries[0].name, "HELLO.TXT") == 0);
    EXPECT(strcmp(entries[1].name, "DIR") == 0);
    EXPECT(entries[1].is_directory == 1U);
}

int main(void) {
    storage_status_t status;
    storage_disk_t disk;
    storage_volume_t volume;
    storage_dir_entry_t entries[4];
    storage_long_dir_entry_t long_entries[4];
    storage_dir_cursor_t cursor;
    storage_long_dir_cursor_t long_cursor;
    storage_dir_entry_t entry;
    uint32_t count;
    uint32_t bytes_read;
    uint32_t free_sectors;
    uint32_t free_clusters;
    uint32_t size;
    uint8_t attributes;
    uint8_t found;
    uint8_t done;
    uint8_t buffer[16];

    setup_disk();
    coverage_active = 1U;
    EXPECT(storage_get_status(&status) == OK);
    EXPECT(status.initialized == 0U);
    EXPECT(storage_get_status(0) == ERR_NULL);
    EXPECT(storage_get_disk_at(0U, &disk) == ERR_STATE);
    EXPECT(storage_find_volume("ata0p1", &volume) == ERR_STATE);
    EXPECT(storage_list_dir(0, "", entries, 1U, &count) == ERR_NULL);
    EXPECT(storage_mount(0) == ERR_NULL);
    EXPECT(storage_sync_all_until(1U) == ERR_STATE);

    fake_cache_busy = 1U;
    EXPECT(storage_init() == ERR_STATE);
    fake_cache_busy = 0U;
    EXPECT(storage_init() == OK);
    EXPECT(storage_get_status(&status) == OK);
    EXPECT(status.initialized == 1U);
    EXPECT(status.disk_count == 1U);
    EXPECT(status.volume_count == 1U);
    EXPECT(status.mounted_count == 0U);
    EXPECT(storage_get_disk_at(0U, &disk) == OK);
    EXPECT(strcmp(disk.id, "ata0") == 0);
    EXPECT(storage_find_disk("ata-primary", &disk) == OK);
    EXPECT(storage_get_disk_at(1U, &disk) == ERR_INVALID);
    EXPECT(storage_get_volume_at(0U, &volume) == OK);
    EXPECT(strcmp(volume.id, "ata0p1") == 0);
    EXPECT(volume.fs_type == STORAGE_FS_FAT12);
    EXPECT(storage_get_mounted_at(0U, &volume) == ERR_NOT_FOUND);
    EXPECT(storage_find_volume("missing", &volume) == ERR_NOT_FOUND);
    EXPECT(storage_mount("missing") == ERR_NOT_FOUND);

    EXPECT(storage_mount("ata0p1") == OK);
    EXPECT(storage_mount("ata0p1") == ERR_STATE);
    EXPECT(storage_get_mounted_at(0U, &volume) == OK);
    EXPECT(volume.mounted == 1U);
    EXPECT(volume.read_only == 1U);
    EXPECT(storage_find_volume("ATA0P1", &volume) == OK);
    EXPECT(storage_sync_volume("ata0p1") == OK);
    EXPECT(storage_sync_volume(0) == ERR_NULL);
    EXPECT(storage_sync_all_until(0U) == ERR_TIMEOUT);
    EXPECT(storage_sync_all() == OK);

    EXPECT(storage_list_dir("ata0p1", "", entries, 4U, &count) == OK);
    expect_entry_names(entries, count);
    EXPECT(storage_list_dir("ata0p1", "DIR", entries, 4U, &count) == OK);
    EXPECT(count == 1U);
    EXPECT(strcmp(entries[0].name, "NEST.TXT") == 0);
    EXPECT(storage_list_dir("ata0p1", "", entries, 0U, &count) == ERR_INVALID);
    EXPECT(storage_list_dir("ata0p1", "", entries, 1U, &count) == OK);
    EXPECT(count == 1U);
    EXPECT(storage_list_dir("ata0p1", "missing", entries, 4U, &count) ==
           ERR_NOT_FOUND);

    EXPECT(storage_list_dir_long("ata0p1", "", long_entries, 4U, &count) == OK);
    EXPECT(count == 2U);
    EXPECT(strcmp(long_entries[0].short_name, "HELLO.TXT") == 0);
    EXPECT(strcmp(long_entries[1].name, "DIR") == 0);
    EXPECT(storage_list_dir_long("ata0p1", "DIR", long_entries, 4U,
                                 &count) == OK);
    EXPECT(count == 1U);
    EXPECT(strcmp(long_entries[0].name, "NEST.TXT") == 0);

    EXPECT(storage_dir_cursor_open("ata0p1", "", &cursor) == OK);
    EXPECT(storage_dir_cursor_next(&cursor, &entry, &found, &done) == OK);
    EXPECT(found == 1U && strcmp(entry.name, "HELLO.TXT") == 0);
    EXPECT(storage_dir_cursor_next(&cursor, &entry, &found, &done) == OK);
    EXPECT(found == 1U && strcmp(entry.name, "DIR") == 0);
    EXPECT(storage_dir_cursor_next(&cursor, &entry, &found, &done) == OK);
    EXPECT(found == 0U && done == 1U);
    EXPECT(storage_dir_cursor_open_long("ata0p1", "", &long_cursor) == OK);
    EXPECT(storage_dir_cursor_next_long(&long_cursor, &long_entries[0],
                                        &found, &done) == OK);
    EXPECT(found == 1U && strcmp(long_entries[0].short_name, "HELLO.TXT") == 0);
    EXPECT(storage_dir_cursor_next_long(&long_cursor, &long_entries[0],
                                        &found, &done) == OK);
    EXPECT(found == 1U && strcmp(long_entries[0].short_name, "DIR") == 0);
    EXPECT(storage_dir_cursor_next_long(&long_cursor, &long_entries[0],
                                        &found, &done) == OK);
    EXPECT(found == 0U && done == 1U);
    EXPECT(storage_dir_cursor_next(0, &entry, &found, &done) == ERR_NULL);

    EXPECT(storage_read_file_range("ata0p1", "HELLO.TXT", 0U, buffer,
                                   sizeof(buffer), &bytes_read) == OK);
    EXPECT(bytes_read == 5U && memcmp(buffer, "hello", 5U) == 0);
    EXPECT(storage_read_file_range("ata0p1", "HELLO.TXT", 2U, buffer, 2U,
                                   &bytes_read) == OK);
    EXPECT(bytes_read == 2U && memcmp(buffer, "ll", 2U) == 0);
    EXPECT(storage_read_file_range("ata0p1", "DIR/NEST.TXT", 0U, buffer,
                                   sizeof(buffer), &bytes_read) == OK);
    EXPECT(bytes_read == 4U && memcmp(buffer, "nest", 4U) == 0);
    EXPECT(storage_read_file_range("ata0p1", "HELLO.TXT", 99U, buffer,
                                   sizeof(buffer), &bytes_read) == OK);
    EXPECT(bytes_read == 0U);
    EXPECT(storage_read_file_range("ata0p1", "DIR", 0U, buffer,
                                   sizeof(buffer), &bytes_read) == ERR_INVALID);
    EXPECT(storage_read_file_range("ata0p1", "HELLO.TXT", 0U, 0,
                                   sizeof(buffer), &bytes_read) == ERR_NULL);
    EXPECT(storage_get_file_info("ata0p1", "HELLO.TXT", &size, &attributes) == OK);
    EXPECT(size == 5U && attributes == 0x20U);
    EXPECT(storage_get_file_info("ata0p1", "missing", &size, &attributes) ==
           ERR_NOT_FOUND);
    EXPECT(storage_get_path_info("ata0p1", "", &size, &attributes,
                                 &found) == OK);
    EXPECT(found == 1U && attributes == 0x10U);
    EXPECT(storage_get_path_info("ata0p1", "DIR", &size, &attributes,
                                 &found) == OK);
    EXPECT(found == 1U);
    EXPECT(storage_get_path_info("ata0p1", "HELLO.TXT", &size, &attributes,
                                 &found) == OK);
    EXPECT(found == 0U && size == 5U);
    EXPECT(storage_get_path_info("ata0p1", "missing", &size, &attributes,
                                 &found) == ERR_NOT_FOUND);

    EXPECT(storage_get_free_space("ata0p1", &free_sectors,
                                  &free_clusters) == OK);
    EXPECT(free_clusters > 900U);
    EXPECT(free_sectors == free_clusters);
    EXPECT(storage_get_free_space("missing", &free_sectors,
                                  &free_clusters) == ERR_NOT_FOUND);
    EXPECT(storage_get_free_space(0, &free_sectors, &free_clusters) == ERR_NULL);
    EXPECT(storage_check("ata0p1") == ERR_UNAVAILABLE);
    EXPECT(storage_check(0) == ERR_NULL);

    EXPECT(storage_write_file("ata0p1", "NEW.TXT", buffer, 2U, 0U) ==
           ERR_UNAVAILABLE);
    EXPECT(storage_atomic_write_file("ata0p1", "NEW.TXT", buffer, 2U,
                                      0U, STORAGE_ATOMIC_CREATE_OR_REPLACE) ==
           ERR_UNAVAILABLE);
    EXPECT(storage_delete_file("ata0p1", "HELLO.TXT") == ERR_UNAVAILABLE);
    EXPECT(storage_create_dir("ata0p1", "NEW") == ERR_UNAVAILABLE);
    EXPECT(storage_rename_file("ata0p1", "HELLO.TXT", "NEW.TXT") ==
           ERR_UNAVAILABLE);
    EXPECT(storage_stream_begin("ata0p1", "STREAM", 5U, 0U) ==
           ERR_UNAVAILABLE);
    EXPECT(storage_stream_write(buffer, 1U) == ERR_STATE);
    EXPECT(storage_stream_finish() == ERR_STATE);
    EXPECT(storage_stream_abort() == ERR_STATE);
    EXPECT(storage_stream_is_active() == 0);
    EXPECT(storage_slot_writer_begin("ata0p1", "SLOT", 5U, 0U) ==
           ERR_UNAVAILABLE);
    EXPECT(storage_slot_writer_is_active() == 0);
    EXPECT(storage_transaction_writer_begin("ata0p1", "T", "T.tmp", 5U,
                                             0U) == ERR_UNAVAILABLE);
    EXPECT(storage_transaction_writer_is_active() == 0);

    EXPECT(storage_unmount("ata0p1") == OK);
    EXPECT(storage_unmount("ata0p1") == ERR_STATE);
    EXPECT(storage_unmount_after_sync("ata0p1") == ERR_STATE);
    EXPECT(storage_get_status(&status) == OK);
    EXPECT(status.mounted_count == 0U);
    EXPECT(storage_refresh() == OK);
    EXPECT(storage_get_status(&status) == OK);
    EXPECT(status.initialized == 1U);
    EXPECT(storage_find_volume("ata0p1", &volume) == OK);
    EXPECT(volume.mounted == 0U);
    EXPECT(storage_fs_name(STORAGE_FS_FAT12) != 0);
    EXPECT(storage_fs_name((storage_fs_type_t)99) != 0);
    EXPECT(storage_volume_state_name(STORAGE_VOLUME_MOUNTED) != 0);
    EXPECT(storage_volume_state_name((storage_volume_state_t)99) != 0);
    EXPECT(fake_read_ops > 0U);
    EXPECT(fake_write_ops == 0U);
    EXPECT(fake_log_count > 0U);

    coverage_active = 0U;
    coverage_emit(0);
    puts("storage-host: PASS");
    return 0;
}
