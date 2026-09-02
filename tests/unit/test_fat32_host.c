#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/video.h"
#include "drivers/ata.h"
#include "fs/fat32.h"

#define HOST_SECTOR_COUNT 5000U
#define HOST_FAT_START 1U
#define HOST_SECTORS_PER_FAT 40U
#define HOST_DATA_START 41U
#define HOST_ROOT_CLUSTER 2U
#define HOST_FILE_CLUSTER 3U
#define HOST_DIRECTORY_CLUSTER 4U
#define HOST_NESTED_CLUSTER 5U
#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U

static uint8_t disk_image[HOST_SECTOR_COUNT][512U];
static uint8_t host_heap[2U * 1024U * 1024U];
static uint32_t host_heap_offset;
static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint32_t fake_read_ops;
static uint32_t fake_write_ops;
static uint32_t fake_log_count;
static uint32_t fake_video_calls;
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
    printf("ZCOV_BEGIN|case=host:storage:fat32|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:storage:fat32|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:storage:fat32|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "fat32-host: falhou: %s\n", expression);
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
    uint32_t offset = cluster * 4U;
    uint8_t* destination = disk_image[HOST_FAT_START] + offset;

    write_u32(destination, 0U, value);
}

static uint32_t cluster_lba(uint32_t cluster) {
    return HOST_DATA_START + cluster - HOST_ROOT_CLUSTER;
}

static void setup_disk(void) {
    fat32_bpb_t bpb;

    memset(disk_image, 0, sizeof(disk_image));
    memset(&bpb, 0, sizeof(bpb));
    bpb.boot_jump[0] = 0xEBU;
    bpb.boot_jump[2] = 0x90U;
    memcpy(bpb.oem, "HOSTFAT32", 8U);
    bpb.bytes_per_sector = 512U;
    bpb.sectors_per_cluster = 1U;
    bpb.reserved_sectors = HOST_FAT_START;
    bpb.num_fats = 1U;
    bpb.total_sectors_large = HOST_SECTOR_COUNT;
    bpb.media_type = 0xF8U;
    bpb.sectors_per_fat = HOST_SECTORS_PER_FAT;
    bpb.root_cluster = HOST_ROOT_CLUSTER;
    bpb.fs_info_sector = 0xFFFFU;
    bpb.backup_boot_sector = 6U;
    bpb.boot_signature = 0x29U;
    memcpy(bpb.volume_label, "HOSTVOL    ", 11U);
    memcpy(bpb.filesystem, "FAT32   ", 8U);
    memcpy(disk_image[0], &bpb, sizeof(bpb));
    disk_image[0][510U] = 0x55U;
    disk_image[0][511U] = 0xAAU;

    set_fat(0U, 0x0FFFFFF8U);
    set_fat(1U, 0x0FFFFFFFU);
    set_fat(HOST_ROOT_CLUSTER, FAT32_CLUSTER_END);
    set_fat(HOST_FILE_CLUSTER, FAT32_CLUSTER_END);
    set_fat(HOST_DIRECTORY_CLUSTER, FAT32_CLUSTER_END);
    set_fat(HOST_NESTED_CLUSTER, FAT32_CLUSTER_END);

    write_entry(disk_image[cluster_lba(HOST_ROOT_CLUSTER)], 0U, "HELLO",
                "TXT", 0x20U, HOST_FILE_CLUSTER, 5U);
    write_entry(disk_image[cluster_lba(HOST_ROOT_CLUSTER)], 1U, "DIR", "",
                0x10U, HOST_DIRECTORY_CLUSTER, 0U);
    disk_image[cluster_lba(HOST_ROOT_CLUSTER)][2U * 32U] = 0U;
    write_entry(disk_image[cluster_lba(HOST_DIRECTORY_CLUSTER)], 0U, ".", "",
                0x10U, HOST_DIRECTORY_CLUSTER, 0U);
    write_entry(disk_image[cluster_lba(HOST_DIRECTORY_CLUSTER)], 1U, "..", "",
                0x10U, HOST_ROOT_CLUSTER, 0U);
    write_entry(disk_image[cluster_lba(HOST_DIRECTORY_CLUSTER)], 2U, "NEST",
                "TXT", 0x20U, HOST_NESTED_CLUSTER, 4U);
    disk_image[cluster_lba(HOST_DIRECTORY_CLUSTER)][3U * 32U] = 0U;
    memcpy(disk_image[cluster_lba(HOST_FILE_CLUSTER)], "hello", 5U);
    memcpy(disk_image[cluster_lba(HOST_NESTED_CLUSTER)], "nest", 4U);

    memset(&fake_ata, 0, sizeof(fake_ata));
    fake_ata.slot = 0U;
    fake_ata.sectors = HOST_SECTOR_COUNT;
    fake_ata.present = 1;
    memcpy(fake_ata.model, "host-fat32", 11U);
    host_heap_offset = 0U;
    fake_read_ops = 0U;
    fake_write_ops = 0U;
    fake_log_count = 0U;
    fake_video_calls = 0U;
}

void* kmalloc(uint32_t size) {
    uint32_t aligned_size;

    if (!size) return 0;
    aligned_size = (size + 7U) & ~7U;
    if (aligned_size > sizeof(host_heap) - host_heap_offset) return 0;
    {
        void* result = host_heap + host_heap_offset;
        host_heap_offset += aligned_size;
        memset(result, 0, size);
        return result;
    }
}

void kfree(void* pointer) {
    (void)pointer;
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
    fake_video_calls++;
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

ata_device_t* ata_get_device(void) {
    return &fake_ata;
}

int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    if (!buffer || !count || lba >= HOST_SECTOR_COUNT ||
        count > HOST_SECTOR_COUNT - lba) return ERR_DISK;
    memcpy(buffer, disk_image[lba], (uint32_t)count * 512U);
    fake_read_ops += count;
    return OK;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer) {
    if (!buffer || !count || lba >= HOST_SECTOR_COUNT ||
        count > HOST_SECTOR_COUNT - lba) return ERR_DISK;
    memcpy(disk_image[lba], buffer, (uint32_t)count * 512U);
    fake_write_ops += count;
    return OK;
}

int main(void) {
    fat32_fs_t* filesystem;
    uint8_t buffer[32];
    uint8_t payload[] = {'n', 'e', 'w'};
    char name[13];
    uint32_t size;
    uint8_t attributes;

    setup_disk();
    coverage_active = 1U;
    EXPECT(fat32_get_free_clusters() == 0U);
    EXPECT(fat32_get_file_count() == 0);
    EXPECT(fat32_init() == OK);
    filesystem = fat32_get_fs();
    EXPECT(filesystem != 0 && filesystem->initialized != 0);
    EXPECT(filesystem->root_cluster == HOST_ROOT_CLUSTER);
    EXPECT(filesystem->total_clusters > 4085U);
    EXPECT(fat32_get_file_count() == 2);
    EXPECT(fat32_get_file_info(0, name, &size, &attributes) == OK);
    EXPECT(strcmp(name, "HELLO.TXT") == 0 && size == 5U && attributes == 0x20U);
    EXPECT(fat32_get_file_info_at(HOST_DIRECTORY_CLUSTER, 2U, name, &size,
                                  &attributes) == OK);
    EXPECT(strcmp(name, "NEST.TXT") == 0 && size == 4U);
    EXPECT(fat32_get_file_count_at(HOST_DIRECTORY_CLUSTER) == 3);
    EXPECT(fat32_resolve_path("") == HOST_ROOT_CLUSTER);
    EXPECT(fat32_resolve_path("DIR") == HOST_DIRECTORY_CLUSTER);
    EXPECT(fat32_resolve_path("missing") == 0U);
    EXPECT(fat32_read_file_at("HELLO.TXT", buffer, sizeof(buffer)) == 5);
    EXPECT(memcmp(buffer, "hello", 5U) == 0);
    EXPECT(fat32_read_file_at("DIR/NEST.TXT", buffer, sizeof(buffer)) == 4);
    EXPECT(memcmp(buffer, "nest", 4U) == 0);
    EXPECT(fat32_read_file_range_at("DIR/NEST.TXT", 1U, buffer, 2U) == 2);
    EXPECT(memcmp(buffer, "es", 2U) == 0);
    EXPECT(fat32_read_file_range_at("HELLO.TXT", 99U, buffer, 2U) == 0);

    EXPECT(fat32_create_dir_entry(HOST_ROOT_CLUSTER, "SUB", 0x10U) == 0);
    EXPECT(fat32_write_file_in_dir(HOST_ROOT_CLUSTER, "NEW.TXT", payload,
                                   sizeof(payload)) == 3);
    EXPECT(fat32_read_file_at("NEW.TXT", buffer, sizeof(buffer)) == 3);
    EXPECT(memcmp(buffer, payload, sizeof(payload)) == 0);
    EXPECT(fat32_delete_file_in_dir(HOST_ROOT_CLUSTER, "NEW.TXT") == 0);
    EXPECT(fat32_write_file("PUBLIC  TXT", payload, sizeof(payload)) == 3);
    EXPECT(fat32_read_file("PUBLIC  TXT", buffer, sizeof(buffer)) == 3);
    EXPECT(memcmp(buffer, payload, sizeof(payload)) == 0);
    EXPECT(fat32_delete_file("PUBLIC  TXT") == 0);
    EXPECT(fat32_delete_file("HELLO   TXT") == 0);
    EXPECT(fat32_get_free_clusters() > 4000U);
    EXPECT(fat32_list_dir() >= 2);
    EXPECT(fake_read_ops > 0U);
    EXPECT(fake_write_ops > 0U);
    EXPECT(fake_video_calls > 0U);
    EXPECT(fake_log_count > 0U);
    coverage_active = 0U;
    coverage_emit(0);
    puts("fat32-host: PASS");
    return 0;
}
