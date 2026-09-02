#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "drivers/ata.h"
#include "fs/fat12.h"
#include "fs/fat32.h"
#include "fs/fs.h"
#include "fs/storage.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_ENTRY_COUNT 3U
#define HOST_DIR_ENTRY_COUNT 2U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint32_t fake_log_count;
static uint32_t fake_video_count;
static uint32_t fake_storage_writes;
static uint32_t fake_storage_deletes;
static uint32_t fake_storage_renames;
static uint8_t fake_system_available;
static uint8_t fake_stream_active;
static uint8_t fake_directory_sector[FS_DIR_SECTOR_SIZE];
static uint16_t fake_fat12_fat_storage[256];
static fat12_dir_entry_t fake_fat12_root[16];
static fat12_fs_t fake_fat12;
static fat32_fs_t fake_fat32;
static uint8_t fake_fat12_enabled;
static const uint8_t fake_file_data[] = "hello from storage";

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
    printf("ZCOV_BEGIN|case=host:storage:fs|value=0x%08X\n", coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:storage:fs|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:storage:fs|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "fs-host: falhou: %s\n", expression);
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

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
    fake_video_count++;
}

static void fill_long_entry(storage_long_dir_entry_t* entry, const char* name,
                            const char* short_name, uint32_t size,
                            uint8_t directory) {
    kmemset(entry, 0, sizeof(*entry));
    set_text(entry->name, sizeof(entry->name), name);
    set_text(entry->short_name, sizeof(entry->short_name), short_name);
    entry->size = size;
    entry->cluster = directory ? 9U : 0U;
    entry->attributes = directory ? 0x10U :
                                   FS_ATTRIBUTE_ARCHIVE;
    entry->is_directory = directory;
}

int storage_find_system_volume(storage_volume_t* out_volume) {
    if (!out_volume) return ERR_NULL;
    if (!fake_system_available) return ERR_UNAVAILABLE;
    kmemset(out_volume, 0, sizeof(*out_volume));
    set_text(out_volume->id, sizeof(out_volume->id), "system");
    set_text(out_volume->label, sizeof(out_volume->label), "ZEHPRO");
    out_volume->fs_type = STORAGE_FS_FAT32;
    out_volume->state = STORAGE_VOLUME_MOUNTED;
    out_volume->mounted = 1U;
    out_volume->bytes_per_sector = STORAGE_SECTOR_SIZE;
    out_volume->sectors_per_cluster = 2U;
    out_volume->sector_count = 1000U;
    out_volume->generation = 7U;
    out_volume->role = STORAGE_VOLUME_ROLE_SYSTEM;
    return OK;
}

int storage_list_dir_long(const char* id, const char* path,
                          storage_long_dir_entry_t* entries,
                          uint32_t capacity, uint32_t* out_count) {
    if (!id || !path || !entries || !out_count) return ERR_NULL;
    if (strcmp(id, "system") != 0) return ERR_NOT_FOUND;
    if (capacity < HOST_ENTRY_COUNT) return ERR_OVERFLOW;
    if (strcmp(path, "") == 0) {
        fill_long_entry(&entries[0], "config.ini", "CONFIG.INI", 10U, 0U);
        fill_long_entry(&entries[1], "docs", "DOCS", 0U, 1U);
        fill_long_entry(&entries[2], "readme.txt", "README.TXT", 18U, 0U);
        *out_count = HOST_ENTRY_COUNT;
        return OK;
    }
    if (strcmp(path, "docs") == 0) {
        if (capacity < HOST_DIR_ENTRY_COUNT) return ERR_OVERFLOW;
        fill_long_entry(&entries[0], "guide.md", "GUIDE.MD", 9U, 0U);
        fill_long_entry(&entries[1], "manual.txt", "MANUAL.TXT", 11U, 0U);
        *out_count = HOST_DIR_ENTRY_COUNT;
        return OK;
    }
    return ERR_NOT_FOUND;
}

int storage_read_file_range(const char* id, const char* path,
                            uint32_t offset, uint8_t* buffer,
                            uint32_t max_size, uint32_t* out_read) {
    uint32_t available;
    uint32_t amount;

    if (!id || !path || !out_read || (max_size && !buffer)) return ERR_NULL;
    if (strcmp(id, "system") != 0 ||
        (strcmp(path, "readme.txt") != 0 && strcmp(path, "docs/guide.md") != 0)) {
        return ERR_NOT_FOUND;
    }
    if (offset >= sizeof(fake_file_data) - 1U) {
        *out_read = 0U;
        return OK;
    }
    available = (uint32_t)(sizeof(fake_file_data) - 1U) - offset;
    amount = available < max_size ? available : max_size;
    if (amount) kmemcpy(buffer, fake_file_data + offset, amount);
    *out_read = amount;
    return OK;
}

int storage_get_file_info(const char* id, const char* path,
                          uint32_t* out_size, uint8_t* out_attributes) {
    if (!id || !path) return ERR_NULL;
    if (strcmp(id, "system") != 0 || strcmp(path, "readme.txt") != 0) {
        return ERR_NOT_FOUND;
    }
    if (out_size) *out_size = sizeof(fake_file_data) - 1U;
    if (out_attributes) *out_attributes = FS_ATTRIBUTE_ARCHIVE;
    return OK;
}

int storage_get_free_space(const char* id, uint32_t* out_free_sectors,
                           uint32_t* out_free_clusters) {
    if (!id || !out_free_sectors || !out_free_clusters) return ERR_NULL;
    if (strcmp(id, "system") != 0) return ERR_NOT_FOUND;
    *out_free_sectors = 777U;
    *out_free_clusters = 388U;
    return OK;
}

int storage_write_file(const char* id, const char* path,
                       const uint8_t* data, uint32_t size,
                       uint8_t attributes) {
    if (!id || !path || (size && !data)) return ERR_NULL;
    (void)attributes;
    fake_storage_writes++;
    return OK;
}

int storage_delete_file(const char* id, const char* path) {
    if (!id || !path) return ERR_NULL;
    fake_storage_deletes++;
    return OK;
}

int storage_create_dir(const char* id, const char* path) {
    if (!id || !path) return ERR_NULL;
    fake_storage_writes++;
    return OK;
}

int storage_rename_file(const char* id, const char* path,
                        const char* new_name) {
    if (!id || !path || !new_name) return ERR_NULL;
    fake_storage_renames++;
    return OK;
}

int storage_atomic_write_file(const char* id, const char* path,
                              const uint8_t* data, uint32_t size,
                              uint8_t attributes, storage_atomic_mode_t mode) {
    if (!id || !path || !data || !size) return ERR_NULL;
    if (mode != STORAGE_ATOMIC_CREATE_OR_REPLACE &&
        mode != STORAGE_ATOMIC_REPLACE_ONLY) return ERR_INVALID;
    (void)attributes;
    fake_storage_writes++;
    return OK;
}

int storage_stream_begin(const char* id, const char* path,
                         uint32_t expected_size, uint8_t attributes) {
    if (!id || !path) return ERR_NULL;
    if (!expected_size) return ERR_OVERFLOW;
    if (fake_stream_active) return ERR_STATE;
    (void)attributes;
    fake_stream_active = 1U;
    return OK;
}

int storage_stream_write(const uint8_t* data, uint32_t size) {
    if (!data && size) return ERR_NULL;
    if (!fake_stream_active) return ERR_STATE;
    return OK;
}

int storage_stream_finish(void) {
    if (!fake_stream_active) return ERR_STATE;
    fake_stream_active = 0U;
    return OK;
}

int storage_stream_abort(void) {
    if (!fake_stream_active) return ERR_STATE;
    fake_stream_active = 0U;
    return OK;
}

int storage_stream_is_active(void) {
    return fake_stream_active ? 1 : 0;
}

int storage_is_stream_active(void) {
    return fake_stream_active ? 1 : 0;
}

int storage_get_path_info(const char* id, const char* path,
                          uint32_t* out_size, uint8_t* out_attributes,
                          uint8_t* out_is_directory) {
    if (!id || !path || !out_size || !out_attributes || !out_is_directory) {
        return ERR_NULL;
    }
    *out_size = 0U;
    *out_attributes = 0U;
    *out_is_directory = 0U;
    return ERR_NOT_FOUND;
}

static int fake_fat_unavailable(void) {
    return ERR_UNAVAILABLE;
}

int fat12_init(void) {
    fake_fat12.initialized = fake_fat12_enabled ? 1 : 0;
    return fake_fat12_enabled ? OK : ERR_NOT_FOUND;
}
fat12_fs_t* fat12_get_fs(void) { return &fake_fat12; }
uint32_t fat12_get_free_clusters(void) {
    return fake_fat12_enabled ? 5U : 0U;
}
int fat32_init(void) { return OK; }
fat32_fs_t* fat32_get_fs(void) { return &fake_fat32; }
uint32_t fat32_get_free_clusters(void) { return 44U; }

int fat12_read_file(const char* path, uint8_t* buffer, uint32_t size) {
    (void)path; (void)buffer; (void)size; return fake_fat_unavailable();
}
int fat12_write_file(const char* path, const uint8_t* data, uint32_t size) {
    (void)path; (void)data; return size ? (int)size : fake_fat_unavailable();
}
int fat12_delete_file(const char* path) { (void)path; return fake_fat_unavailable(); }
int fat12_list_dir(void) { return fake_fat_unavailable(); }
int fat12_get_file_count(void) { return fake_fat_unavailable(); }
int fat12_get_file_info(int index, char* name, uint32_t* size, uint8_t* attr) {
    (void)index; (void)name; (void)size; (void)attr; return fake_fat_unavailable();
}
int fat12_read_file_at(const char* path, uint8_t* b, uint32_t s) {
    (void)path; (void)b; (void)s; return fake_fat_unavailable();
}
int fat12_read_file_range_at(const char* path, uint32_t o, uint8_t* b, uint32_t s) {
    (void)path; (void)o; (void)b; (void)s; return fake_fat_unavailable();
}
int fat12_get_file_count_at(uint16_t cluster) { (void)cluster; return fake_fat_unavailable(); }
int fat12_get_file_info_at(uint16_t c, int i, char* n, uint32_t* s, uint8_t* a) {
    (void)c; (void)i; (void)n; (void)s; (void)a; return fake_fat_unavailable();
}
uint16_t fat12_resolve_path(const char* path) {
    return path && strcmp(path, "docs") == 0 ? 2U : 0xFFFFU;
}
int fat12_create_dir_entry(uint16_t c, const char* n, uint8_t a) {
    (void)c; (void)n; (void)a; return fake_fat_unavailable();
}
int fat12_write_file_in_dir(uint16_t c, const char* n, const uint8_t* d, uint32_t s) {
    (void)c; (void)n; (void)d; return s ? (int)s : fake_fat_unavailable();
}
int fat12_delete_file_in_dir(uint16_t c, const char* n) {
    (void)c; (void)n; return fake_fat_unavailable();
}
int fat12_rename_file_in_dir(uint16_t c, const char* o, const char* n) {
    (void)c; (void)o; (void)n; return fake_fat_unavailable();
}
int fat12_get_root_file_info(const char* n, uint32_t* s, uint8_t* a) {
    (void)n; (void)s; (void)a; return fake_fat_unavailable();
}
int fat12_atomic_write_root(const char* n, const uint8_t* d, uint32_t s,
                            uint8_t a, int r) {
    (void)n; (void)d; (void)s; (void)a; (void)r; return fake_fat_unavailable();
}
int fat12_atomic_delete_root(const char* n) { (void)n; return fake_fat_unavailable(); }
int fat12_atomic_write_file_in_dir(uint16_t c, const char* n, const uint8_t* d,
                                   uint32_t s, uint8_t a, int r) {
    (void)c; (void)n; (void)d; (void)s; (void)a; (void)r; return fake_fat_unavailable();
}
int fat12_atomic_delete_file_in_dir(uint16_t c, const char* n) {
    (void)c; (void)n; return fake_fat_unavailable();
}
int fat12_stream_begin_root(const char* n, uint32_t s, uint8_t a) {
    (void)n; (void)s; (void)a; return fake_fat_unavailable();
}
int fat12_stream_write_root(const uint8_t* d, uint32_t s) {
    (void)d; (void)s; return fake_fat_unavailable();
}
int fat12_stream_finish_root(void) { return fake_fat_unavailable(); }
int fat12_stream_abort_root(void) { return fake_fat_unavailable(); }
int fat12_stream_is_active(void) { return 0; }

int fat32_read_file(const char* path, uint8_t* buffer, uint32_t size) {
    (void)path; (void)buffer; (void)size; return fake_fat_unavailable();
}
int fat32_write_file(const char* path, const uint8_t* data, uint32_t size) {
    (void)path; (void)data; return size ? (int)size : fake_fat_unavailable();
}
int fat32_delete_file(const char* path) { (void)path; return fake_fat_unavailable(); }
int fat32_list_dir(void) { return fake_fat_unavailable(); }
int fat32_get_file_count(void) { return fake_fat_unavailable(); }
int fat32_get_file_info(int i, char* n, uint32_t* s, uint8_t* a) {
    (void)i; (void)n; (void)s; (void)a; return fake_fat_unavailable();
}
int fat32_read_file_at(const char* path, uint8_t* b, uint32_t s) {
    (void)path; (void)b; (void)s; return fake_fat_unavailable();
}
int fat32_read_file_range_at(const char* path, uint32_t o, uint8_t* b, uint32_t s) {
    (void)path; (void)o; (void)b; (void)s; return fake_fat_unavailable();
}
int fat32_get_file_count_at(uint32_t cluster) { (void)cluster; return 2; }
int fat32_get_file_info_at(uint32_t c, int i, char* n, uint32_t* s, uint8_t* a) {
    (void)c;
    if (i != 0 || !n || !s || !a) return ERR_NULL;
    set_text(n, 13U, "LEGACY.TXT"); *s = 4U; *a = FS_ATTRIBUTE_ARCHIVE; return OK;
}
uint32_t fat32_resolve_path(const char* path) { (void)path; return 2U; }
int fat32_create_dir_entry(uint32_t c, const char* n, uint8_t a) {
    (void)c; (void)n; (void)a; return OK;
}
int fat32_write_file_in_dir(uint32_t c, const char* n, const uint8_t* d, uint32_t s) {
    (void)c; (void)n; (void)d; return s ? (int)s : OK;
}
int fat32_delete_file_in_dir(uint32_t c, const char* n) {
    (void)c; (void)n; return OK;
}

int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    if (!buffer || count != 1U) return ERR_NULL;
    if ((fake_fat12_enabled && lba == fake_fat12.data_start) ||
        (!fake_fat12_enabled && lba == fake_fat32.data_start)) {
        kmemcpy(buffer, fake_directory_sector, FS_DIR_SECTOR_SIZE);
        return OK;
    }
    if (lba == fake_fat32.fat_start) {
        kmemset(buffer, 0, FS_DIR_SECTOR_SIZE);
        buffer[8] = 0xF8U;
        buffer[9] = 0xFFU;
        buffer[10] = 0xFFU;
        buffer[11] = 0x0FU;
        return OK;
    }
    return ERR_DISK;
}

static void make_legacy_directory_sector(void) {
    fat32_dir_entry_t* entry;

    kmemset(fake_directory_sector, 0, sizeof(fake_directory_sector));
    entry = (fat32_dir_entry_t*)fake_directory_sector;
    kmemset(entry, 0, sizeof(*entry));
    memset(entry->name, ' ', sizeof(entry->name));
    memset(entry->ext, ' ', sizeof(entry->ext));
    entry->name[0] = '.';
    entry->attributes = 0x10U;
    entry->cluster_low = 2U;
    entry++;
    kmemset(entry, 0, sizeof(*entry));
    memset(entry->name, ' ', sizeof(entry->name));
    memset(entry->ext, ' ', sizeof(entry->ext));
    entry->name[0] = '.';
    entry->name[1] = '.';
    entry->attributes = 0x10U;
    entry->cluster_low = 2U;
    entry++;
    kmemset(entry, 0, sizeof(*entry));
    memset(entry->name, ' ', sizeof(entry->name));
    memset(entry->ext, ' ', sizeof(entry->ext));
    memcpy(entry->name, "README", 6U);
    memcpy(entry->ext, "TXT", 3U);
    entry->attributes = FS_ATTRIBUTE_ARCHIVE;
    entry->cluster_low = 3U;
    entry->file_size = 5U;
    for (uint32_t index = 3U;
         index < FS_DIR_SECTOR_SIZE / sizeof(fat32_dir_entry_t); index++) {
        entry = (fat32_dir_entry_t*)(fake_directory_sector +
                                     index * sizeof(fat32_dir_entry_t));
        kmemset(entry, 0, sizeof(*entry));
        entry->name[0] = 0xE5U;
    }
}

static void setup_fat32(void) {
    kmemset(&fake_fat12, 0, sizeof(fake_fat12));
    kmemset(&fake_fat32, 0, sizeof(fake_fat32));
    kmemset(fake_fat12_fat_storage, 0, sizeof(fake_fat12_fat_storage));
    kmemset(fake_fat12_root, 0, sizeof(fake_fat12_root));
    fake_fat12.bpb.bytes_per_sector = STORAGE_SECTOR_SIZE;
    fake_fat12.bpb.sectors_per_cluster = 1U;
    fake_fat12.bpb.total_sectors = 100U;
    fake_fat12.bpb.sectors_per_fat = 1U;
    fake_fat12.bpb.root_entries = 16U;
    fake_fat12.bpb.volume_label[0] = 'F';
    fake_fat12.fat_start = 2U;
    fake_fat12.root_start = 3U;
    fake_fat12.data_start = 20U;
    fake_fat12.fat = fake_fat12_fat_storage;
    fake_fat12.root_dir = fake_fat12_root;
    ((uint8_t*)fake_fat12.fat)[3] = 0xF8U;
    ((uint8_t*)fake_fat12.fat)[4] = 0x0FU;
    fake_fat32.initialized = 1;
    fake_fat32.bpb.bytes_per_sector = STORAGE_SECTOR_SIZE;
    fake_fat32.bpb.sectors_per_cluster = 1U;
    fake_fat32.bpb.total_sectors_large = 1000U;
    fake_fat32.bpb.sectors_per_fat = 1U;
    fake_fat32.bpb.root_cluster = 2U;
    fake_fat32.root_cluster = 2U;
    fake_fat32.fat_start = 4U;
    fake_fat32.data_start = 10U;
    fake_fat32.total_clusters = 100U;
    set_text(fake_fat32.bpb.volume_label, sizeof(fake_fat32.bpb.volume_label),
             "ZEHPRO");
    make_legacy_directory_sector();
}

int main(void) {
    uint8_t buffer[64];
    uint32_t bytes_read = 0U;
    uint32_t size = 0U;
    uint8_t attributes = 0U;
    uint8_t is_directory = 0U;
    uint32_t generation;
    char name[32];
    fs_info_t info;
    fs_dir_cursor_t cursor;
    fs_dir_entry_t entry;
    uint8_t found;
    uint8_t done;
    static const uint8_t payload[] = "payload";

    setup_fat32();
    coverage_active = 1U;
    EXPECT(fs_get_type() == FS_TYPE_NONE);
    EXPECT(fs_has_system_volume() == 0);
    EXPECT(fs_get_info(&info) == ERR_NOT_FOUND);
    EXPECT(fs_read_file(0, buffer, sizeof(buffer)) == ERR_NULL);
    EXPECT(fs_use_system_volume() == ERR_UNAVAILABLE);
    fake_system_available = 1U;

    EXPECT(fs_init() == OK);
    EXPECT(fs_get_type() == FS_TYPE_FAT32);
    EXPECT(fs_has_system_volume() == 0);
    EXPECT(fs_use_system_volume() == OK);
    EXPECT(fs_has_system_volume() == 1);
    generation = fs_get_generation();
    EXPECT(generation > 0U);
    EXPECT(fs_get_info(&info) == OK);
    EXPECT(info.type == FS_TYPE_FAT32);
    EXPECT(info.free_sectors == 777U);
    EXPECT(info.free_clusters == 388U);
    EXPECT(fs_list_dir() == (int)HOST_ENTRY_COUNT);
    EXPECT(fs_get_file_count() == (int)HOST_ENTRY_COUNT);
    EXPECT(fs_get_file_info(1, name, &size, &attributes) == OK);
    EXPECT(strcmp(name, "DOCS") == 0);
    EXPECT(attributes == 0x10U);
    EXPECT(fs_get_file_info(-1, name, &size, &attributes) == ERR_NOT_FOUND);

    EXPECT(fs_read_file("system:/readme.txt", buffer, sizeof(buffer)) ==
           (int)(sizeof(fake_file_data) - 1U));
    EXPECT(memcmp(buffer, fake_file_data, sizeof(fake_file_data) - 1U) == 0);
    EXPECT(fs_read_file_at("system:/readme.txt", buffer, 5U) == 5);
    EXPECT(fs_read_file_range_at("system:/readme.txt", 2U, buffer, 4U,
                                 &bytes_read) == OK);
    EXPECT(bytes_read == 4U);
    EXPECT(fs_get_root_file_info("system:/readme.txt", &size, &attributes) == OK);
    EXPECT(size == sizeof(fake_file_data) - 1U);
    EXPECT(fs_get_file_count_at(0) == (int)HOST_ENTRY_COUNT);
    EXPECT(fs_get_file_count_at("system:/docs") == (int)HOST_DIR_ENTRY_COUNT);
    EXPECT(fs_get_file_info_at("system:/docs", 0, name, &size, &attributes) == OK);
    EXPECT(strcmp(name, "GUIDE.MD") == 0);
    (void)is_directory;

    EXPECT(fs_dir_cursor_open("system:/docs", &cursor) == OK);
    EXPECT(fs_dir_cursor_next(&cursor, &entry, &found, &done) == OK);
    EXPECT(found == 1U && strcmp(entry.name, "GUIDE.MD") == 0);
    EXPECT(fs_dir_cursor_next(&cursor, &entry, &found, &done) == OK);
    EXPECT(found == 1U && strcmp(entry.name, "MANUAL.TXT") == 0);
    EXPECT(fs_dir_cursor_next(&cursor, &entry, &found, &done) == OK);
    EXPECT(found == 0U && done == 1U);
    EXPECT(fs_dir_cursor_open(0, &cursor) == ERR_NULL);
    EXPECT(fs_dir_cursor_open("system:/missing", &cursor) == ERR_NOT_FOUND);

    EXPECT(fs_write_file("system:/new.txt", payload, sizeof(payload) - 1U) == OK);
    EXPECT(fs_write_file_at("system:/new.txt", payload, sizeof(payload) - 1U) == OK);
    EXPECT(fs_write_file_in_dir("system:/docs", "new.txt", payload,
                                sizeof(payload) - 1U) == OK);
    EXPECT(fs_create_dir_entry("system:/docs", "newdir", 0x10U) == OK);
    EXPECT(fs_delete_file("system:/new.txt") == OK);
    EXPECT(fs_delete_file_in_dir("system:/docs", "new.txt") == OK);
    EXPECT(fs_rename_file_in_dir("system:/docs", "old.txt", "new.txt") == OK);
    EXPECT(fs_atomic_write_root("system:/atomic", payload,
                                sizeof(payload) - 1U, FS_ATTRIBUTE_ARCHIVE,
                                FS_ATOMIC_CREATE_OR_REPLACE) == OK);
    EXPECT(fs_atomic_write_file_in_dir("system:/docs", "atomic", payload,
                                       sizeof(payload) - 1U,
                                       FS_ATTRIBUTE_ARCHIVE,
                                       FS_ATOMIC_REPLACE_ONLY) == OK);
    EXPECT(fs_atomic_delete_root("system:/atomic") == OK);
    EXPECT(fs_atomic_delete_file_in_dir("system:/docs", "atomic") == OK);

    EXPECT(fs_stream_begin_root("system:/stream", 10U, FS_ATTRIBUTE_ARCHIVE) == OK);
    EXPECT(fs_stream_is_active() == 1);
    EXPECT(fs_write_file("system:/blocked", payload, 1U) == ERR_STATE);
    EXPECT(fs_stream_write_root(payload, 2U) == OK);
    EXPECT(fs_stream_finish_root() == OK);
    EXPECT(fs_stream_is_active() == 0);
    EXPECT(fs_stream_abort_root() == ERR_STATE);
    EXPECT(fs_stream_begin_root("system:/stream", 10U, 0U) == OK);
    EXPECT(fs_stream_abort_root() == OK);
    EXPECT(fs_stream_begin_root("system:/stream", 0U, 0U) == ERR_OVERFLOW);
    EXPECT(fs_stream_write_root(0, 1U) == ERR_NULL);

    EXPECT(fs_init() == OK);
    EXPECT(fs_has_system_volume() == 0);
    EXPECT(fs_dir_cursor_open("legacy:/", &cursor) == OK);
    EXPECT(fs_dir_cursor_next(&cursor, &entry, &found, &done) == OK);
    EXPECT(found == 1U && strcmp(entry.name, "README.TXT") == 0);
    EXPECT(fs_dir_cursor_next(&cursor, &entry, &found, &done) == OK);
    EXPECT(fs_dir_cursor_next(&cursor, &entry, &found, &done) == OK);
    EXPECT(done == 1U);
    EXPECT(fs_get_file_count_at("legacy:/") == 2);
    EXPECT(fs_get_file_info_at("legacy:/", 0, name, &size, &attributes) == OK);
    EXPECT(strcmp(name, "LEGACY.TXT") == 0);
    EXPECT(fs_write_file("legacy:/legacy.txt", payload, 2U) == 2);
    EXPECT(fs_write_file_at("legacy:/legacy.txt", payload, 2U) == OK);
    EXPECT(fs_write_file_in_dir("legacy:/", "legacy.txt", payload, 2U) == 2);
    EXPECT(fs_create_dir_entry("legacy:/", "legacy", 0U) == OK);
    EXPECT(fs_delete_file("legacy:/legacy.txt") == ERR_UNAVAILABLE);
    EXPECT(fs_delete_file_in_dir("legacy:/", "legacy.txt") == OK);
    EXPECT(fs_rename_file_in_dir("legacy:/", "old", "new") == ERR_UNAVAILABLE);
    EXPECT(fs_atomic_write_root("legacy:/atomic", payload, 2U, 0U,
                                FS_ATOMIC_CREATE_OR_REPLACE) == ERR_UNAVAILABLE);
    EXPECT(fs_atomic_delete_root("legacy:/atomic") == ERR_UNAVAILABLE);
    EXPECT(fs_atomic_write_file_in_dir("legacy:/", "atomic", payload, 2U,
                                       0U, FS_ATOMIC_CREATE_OR_REPLACE) ==
           ERR_UNAVAILABLE);
    EXPECT(fs_atomic_delete_file_in_dir("legacy:/", "atomic") ==
           ERR_UNAVAILABLE);
    EXPECT(fs_stream_begin_root("legacy:/stream", 2U, 0U) == ERR_UNAVAILABLE);
    EXPECT(fs_stream_write_root(payload, 1U) == ERR_UNAVAILABLE);
    EXPECT(fs_stream_finish_root() == ERR_UNAVAILABLE);
    EXPECT(fs_stream_abort_root() == ERR_UNAVAILABLE);
    EXPECT(fs_stream_is_active() == 0);

    fake_fat12_enabled = 1U;
    EXPECT(fs_init() == OK);
    EXPECT(fs_get_type() == FS_TYPE_FAT12);
    EXPECT(fs_has_system_volume() == 0);
    EXPECT(fs_get_info(&info) == OK);
    EXPECT(info.type == FS_TYPE_FAT12);
    EXPECT(info.free_clusters == 5U);
    EXPECT(fs_dir_cursor_open("legacy:/docs", &cursor) == OK);
    EXPECT(fs_dir_cursor_next(&cursor, &entry, &found, &done) == OK);
    EXPECT(found == 1U && strcmp(entry.name, "README.TXT") == 0);
    EXPECT(fs_dir_cursor_next(&cursor, &entry, &found, &done) == OK);
    EXPECT(found == 0U && done == 0U);
    EXPECT(fs_dir_cursor_next(&cursor, &entry, &found, &done) == OK);
    EXPECT(found == 0U && done == 1U);

    EXPECT(fake_storage_writes > 0U);
    EXPECT(fake_storage_deletes > 0U);
    EXPECT(fake_storage_renames > 0U);
    EXPECT(fake_video_count > 0U);
    EXPECT(fake_log_count > 0U);
    coverage_active = 0U;
    coverage_emit(0);
    puts("fs-host: PASS");
    return 0;
}
