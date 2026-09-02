#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/video.h"
#include "drivers/ata.h"
#include "fs/fat12.h"

#define HOST_SECTOR_COUNT 64U
#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_FAT_SECTOR 1U
#define HOST_ROOT_SECTOR 2U
#define HOST_DATA_SECTOR 3U
#define HOST_DIR_CLUSTER 3U
#define HOST_NEST_CLUSTER 4U
#define HOST_NEW_CLUSTER 5U

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
    printf("ZCOV_BEGIN|case=host:storage:fat12|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:storage:fat12|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:storage:fat12|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "fat12-host: falhou: %s\n", expression);
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

static void write_name(uint8_t* entry, const char* name, const char* ext) {
    memset(entry, ' ', 11U);
    for (uint32_t index = 0U; index < 8U && name && name[index]; index++) {
        entry[index] = (uint8_t)name[index];
    }
    for (uint32_t index = 0U; index < 3U && ext && ext[index]; index++) {
        entry[8U + index] = (uint8_t)ext[index];
    }
}

static void write_entry(uint8_t* sector, uint32_t index, const char* name,
                        const char* ext, uint8_t attributes, uint16_t cluster,
                        uint32_t size) {
    uint8_t* entry = sector + index * 32U;

    memset(entry, 0, 32U);
    write_name(entry, name, ext);
    entry[11U] = attributes;
    write_u16(entry, 26U, cluster);
    write_u32(entry, 28U, size);
}

static void set_fat(uint16_t cluster, uint16_t value) {
    uint32_t offset = cluster + cluster / 2U;
    uint16_t current = (uint16_t)disk_image[HOST_FAT_SECTOR][offset] |
                       ((uint16_t)disk_image[HOST_FAT_SECTOR][offset + 1U] << 8U);

    if (cluster & 1U) current = (current & 0x000FU) | (uint16_t)(value << 4U);
    else current = (current & 0xF000U) | value;
    disk_image[HOST_FAT_SECTOR][offset] = (uint8_t)current;
    disk_image[HOST_FAT_SECTOR][offset + 1U] = (uint8_t)(current >> 8U);
}

static void setup_disk(void) {
    fat12_bpb_t bpb;

    memset(disk_image, 0, sizeof(disk_image));
    memset(&bpb, 0, sizeof(bpb));
    bpb.boot_jump[0] = 0xEBU;
    bpb.boot_jump[2] = 0x90U;
    memcpy(bpb.oem, "HOSTFAT ", 8U);
    bpb.bytes_per_sector = 512U;
    bpb.sectors_per_cluster = 1U;
    bpb.reserved_sectors = 1U;
    bpb.num_fats = 1U;
    bpb.root_entries = 16U;
    bpb.total_sectors = 32U;
    bpb.media_type = 0xF8U;
    bpb.sectors_per_fat = 1U;
    bpb.sectors_per_track = 1U;
    bpb.num_heads = 1U;
    bpb.drive_number = 0x80U;
    bpb.boot_signature = 0x29U;
    bpb.volume_id = 0x12345678U;
    memcpy(bpb.volume_label, "HOSTVOL    ", 11U);
    memcpy(bpb.filesystem, "FAT12   ", 8U);
    memcpy(disk_image[0], &bpb, sizeof(bpb));
    disk_image[0][510U] = 0x55U;
    disk_image[0][511U] = 0xAAU;

    disk_image[HOST_FAT_SECTOR][0] = 0xF8U;
    disk_image[HOST_FAT_SECTOR][1] = 0xFFU;
    disk_image[HOST_FAT_SECTOR][2] = 0xFFU;
    set_fat(2U, FAT12_CLUSTER_END);
    set_fat(HOST_DIR_CLUSTER, FAT12_CLUSTER_END);
    set_fat(HOST_NEST_CLUSTER, FAT12_CLUSTER_END);

    write_entry(disk_image[HOST_ROOT_SECTOR], 0U, "HELLO", "TXT", 0x20U,
                2U, 5U);
    write_entry(disk_image[HOST_ROOT_SECTOR], 1U, "DIR", "", 0x10U,
                HOST_DIR_CLUSTER, 0U);
    write_entry(disk_image[HOST_ROOT_SECTOR], 2U, "HOSTVOL", "", 0x08U,
                0U, 0U);
    disk_image[HOST_ROOT_SECTOR][3U * 32U] = 0U;

    write_entry(disk_image[HOST_DATA_SECTOR + 1U], 0U, ".", "", 0x10U,
                HOST_DIR_CLUSTER, 0U);
    write_entry(disk_image[HOST_DATA_SECTOR + 1U], 1U, "..", "", 0x10U,
                0U, 0U);
    write_entry(disk_image[HOST_DATA_SECTOR + 1U], 2U, "NEST", "TXT", 0x20U,
                HOST_NEST_CLUSTER, 4U);
    disk_image[HOST_DATA_SECTOR + 1U][3U * 32U] = 0U;
    memcpy(disk_image[HOST_DATA_SECTOR], "hello", 5U);
    memcpy(disk_image[HOST_DATA_SECTOR + 2U], "nest", 4U);

    memset(&fake_ata, 0, sizeof(fake_ata));
    fake_ata.slot = 0U;
    fake_ata.sectors = HOST_SECTOR_COUNT;
    fake_ata.present = 1;
    memcpy(fake_ata.model, "host-fat12", 11U);
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
    fat12_fs_t* filesystem;
    uint8_t buffer[32];
    uint8_t payload[] = {'n', 'e', 'w'};
    uint8_t replacement[] = {'o', 'k'};
    char name[13];
    uint32_t size;
    uint8_t attributes;

    setup_disk();
    coverage_active = 1U;
    EXPECT(fat12_get_free_clusters() == 0U);
    EXPECT(fat12_get_file_count() == 0);
    EXPECT(fat12_stream_is_active() == 0);
    EXPECT(fat12_init() == OK);
    filesystem = fat12_get_fs();
    EXPECT(filesystem != 0 && filesystem->initialized != 0);
    EXPECT(filesystem->data_start == HOST_DATA_SECTOR);
    EXPECT(fat12_get_file_count() == 2);
    EXPECT(fat12_get_file_info(0, name, &size, &attributes) == OK);
    EXPECT(strcmp(name, "HELLO.TXT") == 0 && size == 5U && attributes == 0x20U);
    EXPECT(fat12_get_file_info(9, name, &size, &attributes) != OK);
    EXPECT(fat12_list_dir() == 2);
    EXPECT(fat12_get_free_clusters() > 20U);

    EXPECT(fat12_read_file("HELLO.TXT", buffer, sizeof(buffer)) == 5);
    EXPECT(memcmp(buffer, "hello", 5U) == 0);
    EXPECT(fat12_read_file("HELLO.TXT", buffer, 2U) == 2);
    EXPECT(fat12_read_file_at("DIR/NEST.TXT", buffer, sizeof(buffer)) == 4);
    EXPECT(memcmp(buffer, "nest", 4U) == 0);
    EXPECT(fat12_read_file_range_at("DIR/NEST.TXT", 1U, buffer, 2U) == 2);
    EXPECT(memcmp(buffer, "es", 2U) == 0);
    EXPECT(fat12_read_file_range_at("HELLO.TXT", 99U, buffer, 2U) == 0);
    EXPECT(fat12_resolve_path("DIR") == HOST_DIR_CLUSTER);
    EXPECT(fat12_resolve_path("missing") == 0xFFFFU);
    EXPECT(fat12_get_file_count_at(HOST_DIR_CLUSTER) >= 3);
    EXPECT(fat12_get_file_info_at(HOST_DIR_CLUSTER, 2, name, &size,
                                  &attributes) == OK);
    EXPECT(strcmp(name, "NEST.TXT") == 0 && size == 4U);
    EXPECT(fat12_get_root_file_info("HELLO.TXT", &size, &attributes) == OK);
    EXPECT(size == 5U && attributes == 0x20U);
    EXPECT(fat12_get_root_file_info("BAD", &size, &attributes) == ERR_INVALID);

    EXPECT(fat12_atomic_write_root("NEW.TXT", payload, sizeof(payload),
                                   0x20U, 0) == OK);
    EXPECT(fat12_read_file("NEW.TXT", buffer, sizeof(buffer)) == 3);
    EXPECT(memcmp(buffer, payload, sizeof(payload)) == 0);
    EXPECT(fat12_atomic_write_root("NEW.TXT", replacement, sizeof(replacement),
                                   0x20U, 1) == OK);
    EXPECT(fat12_read_file("NEW.TXT", buffer, sizeof(buffer)) == 2);
    EXPECT(fat12_atomic_delete_root("NEW.TXT") == OK);
    EXPECT(fat12_atomic_delete_root("NEW.TXT") == ERR_NOT_FOUND);
    EXPECT(fat12_atomic_write_root("BAD", payload, sizeof(payload), 0x20U, 0) ==
           ERR_INVALID);
    EXPECT(fat12_atomic_write_root("BIG.TXT", payload, 0U, 0x20U, 0) ==
           ERR_OVERFLOW);

    EXPECT(fat12_atomic_write_file_in_dir(HOST_DIR_CLUSTER, "INNER.TXT",
                                          payload, sizeof(payload), 0x20U, 0) ==
           OK);
    EXPECT(fat12_read_file_at("DIR/INNER.TXT", buffer, sizeof(buffer)) == 3);
    EXPECT(fat12_rename_file_in_dir(HOST_DIR_CLUSTER, "INNER.TXT", "REN.TXT") ==
           OK);
    EXPECT(fat12_read_file_at("DIR/REN.TXT", buffer, sizeof(buffer)) == 3);
    EXPECT(fat12_atomic_delete_file_in_dir(HOST_DIR_CLUSTER, "REN.TXT") == OK);
    EXPECT(fat12_delete_file_in_dir(HOST_DIR_CLUSTER, "missing") != OK);

    EXPECT(fat12_stream_begin_root("STREAM.TXT", sizeof(payload), 0x20U) == OK);
    EXPECT(fat12_stream_is_active() == 1);
    EXPECT(fat12_stream_write_root(payload, 2U) == OK);
    EXPECT(fat12_stream_finish_root() == ERR_INVALID);
    EXPECT(fat12_stream_write_root(payload + 2U, 1U) == OK);
    EXPECT(fat12_stream_finish_root() == OK);
    EXPECT(fat12_read_file("STREAM.TXT", buffer, sizeof(buffer)) == 3);
    EXPECT(fat12_atomic_delete_root("STREAM.TXT") == OK);
    EXPECT(fat12_stream_begin_root("ABORT.TXT", 1U, 0x20U) == OK);
    EXPECT(fat12_stream_write_root(payload, 1U) == OK);
    EXPECT(fat12_stream_abort_root() == OK);
    EXPECT(fat12_stream_is_active() == 0);
    EXPECT(fat12_stream_abort_root() == OK);
    EXPECT(fat12_stream_begin_root("BAD", 1U, 0x20U) == ERR_INVALID);
    EXPECT(fat12_stream_begin_root("BIG.TXT", 0U, 0x20U) == ERR_OVERFLOW);

    EXPECT(fat12_get_file_count() == 2);
    EXPECT(fake_read_ops > 0U);
    EXPECT(fake_write_ops > 0U);
    EXPECT(fake_video_calls > 0U);
    EXPECT(fake_log_count > 0U);
    coverage_active = 0U;
    coverage_emit(0);
    puts("fat12-host: PASS");
    return 0;
}
