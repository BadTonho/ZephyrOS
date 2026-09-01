#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "fs/file_index.h"
#include "fs/fs.h"
#include "fs/storage.h"

#define HOST_VOLUME_COUNT 2U
#define HOST_ALLOC_COUNT 2U
#define HOST_ALLOC_SIZE (256U * 1024U)
#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t allocation_used[HOST_ALLOC_COUNT];
static uint8_t allocation_storage[HOST_ALLOC_COUNT][HOST_ALLOC_SIZE];
static storage_volume_t fake_volumes[HOST_VOLUME_COUNT];
static storage_status_t fake_storage_status;
static uint32_t fake_ticks = 100U;
static uint32_t fake_log_count;
static char fake_last_log[128];

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
    printf("ZCOV_BEGIN|case=host:storage:file-index|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:storage:file-index|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:storage:file-index|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "file-index-host: falhou: %s\n", expression);
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
    set_text(fake_last_log, sizeof(fake_last_log), message);
    fake_log_count++;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    set_text(fake_last_log, sizeof(fake_last_log), message);
    fake_log_count++;
}

uint32_t timer_get_ticks(void) {
    return fake_ticks++;
}

void* kmalloc(uint32_t size) {
    if (!size || size > HOST_ALLOC_SIZE) return 0;
    for (uint32_t index = 0U; index < HOST_ALLOC_COUNT; index++) {
        if (!allocation_used[index]) {
            allocation_used[index] = 1U;
            memset(allocation_storage[index], 0, HOST_ALLOC_SIZE);
            return allocation_storage[index];
        }
    }
    return 0;
}

void kfree(void* pointer) {
    if (!pointer) return;
    for (uint32_t index = 0U; index < HOST_ALLOC_COUNT; index++) {
        if (pointer == allocation_storage[index]) {
            allocation_used[index] = 0U;
            return;
        }
    }
}

static void fill_volume(uint32_t index, const char* id, uint8_t boot,
                        uint32_t generation) {
    memset(&fake_volumes[index], 0, sizeof(fake_volumes[index]));
    set_text(fake_volumes[index].id, sizeof(fake_volumes[index].id), id);
    fake_volumes[index].fs_type = STORAGE_FS_FAT32;
    fake_volumes[index].state = STORAGE_VOLUME_MOUNTED;
    fake_volumes[index].mounted = 1U;
    fake_volumes[index].boot = boot;
    fake_volumes[index].generation = generation;
    fake_volumes[index].role = boot ? STORAGE_VOLUME_ROLE_BOOT :
                                STORAGE_VOLUME_ROLE_NONE;
}

static void setup_storage(void) {
    memset(&fake_storage_status, 0, sizeof(fake_storage_status));
    fake_storage_status.initialized = 1U;
    fake_storage_status.volume_count = HOST_VOLUME_COUNT;
    fake_storage_status.mounted_count = HOST_VOLUME_COUNT;
    fill_volume(0U, "data", 0U, 21U);
    fill_volume(1U, "boot", 1U, 22U);
}

int storage_get_status(storage_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fake_storage_status;
    return OK;
}

int storage_get_mounted_at(uint8_t index, storage_volume_t* out_volume) {
    if (!out_volume) return ERR_NULL;
    if (index >= HOST_VOLUME_COUNT || !fake_volumes[index].mounted) {
        return ERR_NOT_FOUND;
    }
    *out_volume = fake_volumes[index];
    return OK;
}

int storage_find_volume(const char* id, storage_volume_t* out_volume) {
    if (!id || !out_volume) return ERR_NULL;
    for (uint32_t index = 0U; index < HOST_VOLUME_COUNT; index++) {
        if (strcmp(id, fake_volumes[index].id) == 0) {
            *out_volume = fake_volumes[index];
            return OK;
        }
    }
    return ERR_NOT_FOUND;
}

static void fill_storage_entry(storage_dir_entry_t* entry, const char* name,
                               uint32_t size, uint32_t cluster,
                               uint8_t directory) {
    memset(entry, 0, sizeof(*entry));
    set_text(entry->name, sizeof(entry->name), name);
    entry->size = size;
    entry->cluster = cluster;
    entry->attributes = directory ? 0x10U : 0U;
    entry->is_directory = directory;
}

int storage_dir_cursor_open(const char* id, const char* path,
                            storage_dir_cursor_t* cursor) {
    if (!id || !path || !cursor) return ERR_NULL;
    if (strcmp(id, "data") != 0 || path[0] != '\0') return ERR_NOT_FOUND;
    memset(cursor, 0, sizeof(*cursor));
    set_text(cursor->volume_id, sizeof(cursor->volume_id), id);
    cursor->volume_generation = fake_volumes[0].generation;
    cursor->directory_cluster = 2U;
    cursor->current_cluster = 2U;
    cursor->fs_type = STORAGE_FS_FAT32;
    cursor->active = 1U;
    return OK;
}

int storage_dir_cursor_next(storage_dir_cursor_t* cursor,
                            storage_dir_entry_t* out_entry,
                            uint8_t* out_found, uint8_t* out_done) {
    if (!cursor || !out_entry || !out_found || !out_done) return ERR_NULL;
    *out_found = 0U;
    if (!cursor->active) {
        *out_done = 1U;
        return OK;
    }
    if (cursor->entry_index == 0U && cursor->directory_cluster == 2U) {
        fill_storage_entry(out_entry, "docs", 0U, 9U, 1U);
        cursor->entry_index++;
        *out_found = 1U;
        *out_done = 0U;
        return OK;
    }
    if (cursor->entry_index == 1U && cursor->directory_cluster == 2U) {
        fill_storage_entry(out_entry, "readme.txt", 12U, 0U, 0U);
        cursor->entry_index++;
        *out_found = 1U;
        *out_done = 0U;
        return OK;
    }
    if (cursor->entry_index == 0U && cursor->directory_cluster == 9U) {
        fill_storage_entry(out_entry, "guide.md", 8U, 0U, 0U);
        cursor->entry_index++;
        *out_found = 1U;
        *out_done = 0U;
        return OK;
    }
    cursor->active = 0U;
    *out_done = 1U;
    return OK;
}

uint32_t fs_get_generation(void) {
    return 31U;
}

int fs_dir_cursor_open(const char* path, fs_dir_cursor_t* cursor) {
    if (!path || !cursor) return ERR_NULL;
    if (strcmp(path, "legacy:/") != 0) return ERR_NOT_FOUND;
    memset(cursor, 0, sizeof(*cursor));
    cursor->generation = fs_get_generation();
    cursor->directory_cluster = 2U;
    cursor->current_cluster = 2U;
    cursor->fs_type = FS_TYPE_FAT32;
    cursor->active = 1U;
    return OK;
}

int fs_dir_cursor_next(fs_dir_cursor_t* cursor, fs_dir_entry_t* out_entry,
                       uint8_t* out_found, uint8_t* out_done) {
    if (!cursor || !out_entry || !out_found || !out_done) return ERR_NULL;
    *out_found = 0U;
    if (!cursor->active) {
        *out_done = 1U;
        return OK;
    }
    if (cursor->entry_index == 0U && cursor->directory_cluster == 2U) {
        memset(out_entry, 0, sizeof(*out_entry));
        set_text(out_entry->name, sizeof(out_entry->name), "bootdir");
        out_entry->cluster = 7U;
        out_entry->attributes = 0x10U;
        out_entry->is_directory = 1U;
        cursor->entry_index++;
        *out_found = 1U;
        *out_done = 0U;
        return OK;
    }
    if (cursor->entry_index == 0U && cursor->directory_cluster == 7U) {
        memset(out_entry, 0, sizeof(*out_entry));
        set_text(out_entry->name, sizeof(out_entry->name), "boot.cfg");
        out_entry->size = 4U;
        cursor->entry_index++;
        *out_found = 1U;
        *out_done = 0U;
        return OK;
    }
    cursor->active = 0U;
    *out_done = 1U;
    return OK;
}

static int poll_until_ready(void) {
    file_index_status_t status;

    for (uint32_t index = 0U; index < 128U; index++) {
        uint32_t steps = 0U;
        int result = file_index_poll(1U, &steps);

        if (result != OK) {
            fprintf(stderr, "file-index-host: poll error=%d steps=%lu\n",
                    result, (unsigned long)steps);
            return result;
        }
        if (file_index_get_status(&status) != OK) {
            fprintf(stderr, "file-index-host: status error\n");
            return ERR_STATE;
        }
        if (status.state == FILE_INDEX_STATE_FAILED) {
            fprintf(stderr, "file-index-host: failed error=%d partial=%u source=%lu log=%s\n",
                    status.last_error, (unsigned)status.partial,
                    (unsigned long)status.sources_completed, fake_last_log);
            return status.last_error;
        }
        if (status.state == FILE_INDEX_STATE_READY) return OK;
    }
    fprintf(stderr, "file-index-host: timeout state=%d entries=%lu candidate=%lu source=%lu frame=%lu partial=%u error=%d\n",
            status.state, (unsigned long)status.active_entries,
            (unsigned long)status.candidate_entries,
            (unsigned long)status.source_count,
            (unsigned long)status.sources_completed,
            (unsigned)status.partial, status.last_error);
    return ERR_TIMEOUT;
}

static uint32_t find_free_block(void) {
    for (uint32_t index = 0U; index < HOST_ALLOC_COUNT; index++) {
        if (!allocation_used[index]) return index;
    }
    return HOST_ALLOC_COUNT;
}

int main(void) {
    file_index_status_t status;
    file_index_result_t results[FILE_INDEX_MAX_RESULTS];
    file_index_search_status_t search_status;
    uint32_t steps;

    coverage_active = 1U;
    setup_storage();
    EXPECT(file_index_self_test() == OK);
    EXPECT(file_index_get_status(0) == ERR_NULL);
    EXPECT(file_index_get_status(&status) == ERR_STATE);
    EXPECT(file_index_poll(1U, &steps) == ERR_STATE);
    EXPECT(file_index_init() == OK);
    EXPECT(file_index_init() == ERR_STATE);
    EXPECT(file_index_poll(0U, &steps) == ERR_INVALID);
    EXPECT(file_index_poll(1U, 0) == ERR_NULL);
    EXPECT(poll_until_ready() == OK);
    EXPECT(file_index_get_status(&status) == OK);
    EXPECT(status.state == FILE_INDEX_STATE_READY);
    EXPECT(status.active_entries == 5U);
    EXPECT(status.source_count == HOST_VOLUME_COUNT);
    EXPECT(status.partial == 0U);
    EXPECT(file_index_validate_state() == OK);

    EXPECT(file_index_search(0, results, 1U, &search_status) == ERR_NULL);
    EXPECT(file_index_search("", results, 1U, &search_status) == ERR_INVALID);
    EXPECT(file_index_search("readme", results, 0U, &search_status) == ERR_INVALID);
    EXPECT(file_index_search("readme", results, 1U, 0) == ERR_NULL);
    EXPECT(file_index_search("readme", results, 1U, &search_status) == OK);
    EXPECT(search_status.total_matches == 1U);
    EXPECT(search_status.returned_matches == 1U);
    EXPECT(results[0].availability == FILE_INDEX_RESULT_AVAILABLE);
    EXPECT(file_index_search("docs", results, 1U, &search_status) == OK);
    EXPECT(search_status.total_matches == 2U);
    EXPECT(search_status.partial == 1U);
    EXPECT(results[0].entry.is_directory == 1U);
    EXPECT(file_index_state_name(FILE_INDEX_STATE_READY) != 0);
    EXPECT(file_index_state_name((file_index_state_t)99) != 0);

    fake_volumes[0].generation++;
    EXPECT(file_index_search("readme", results, 1U, &search_status) == OK);
    EXPECT(search_status.result_stale == 1U);
    EXPECT(results[0].availability == FILE_INDEX_RESULT_STALE);
    fake_volumes[0].mounted = 0U;
    EXPECT(file_index_search("readme", results, 1U, &search_status) == OK);
    EXPECT(search_status.volume_missing == 1U);
    EXPECT(results[0].availability == FILE_INDEX_RESULT_VOLUME_MISSING);
    fake_volumes[0].mounted = 1U;
    fake_volumes[0].generation = 21U;

    EXPECT(file_index_rebuild() == OK);
    EXPECT(file_index_get_status(&status) == OK);
    EXPECT(status.state == FILE_INDEX_STATE_BUILDING);
    EXPECT(file_index_cancel() == OK);
    EXPECT(file_index_get_status(&status) == OK);
    EXPECT(status.state == FILE_INDEX_STATE_CANCELLED);
    EXPECT(status.automatic_suspended == 1U);
    EXPECT(file_index_cancel() == ERR_STATE);
    EXPECT(file_index_rebuild() == OK);
    EXPECT(poll_until_ready() == OK);

    {
        uint32_t candidate_slot = find_free_block();

        EXPECT(candidate_slot < HOST_ALLOC_COUNT);
        EXPECT(file_index_rebuild() == OK);
        allocation_storage[candidate_slot][0] = 0U;
        EXPECT(file_index_poll(1U, &steps) == OK);
        EXPECT(poll_until_ready() == OK);
        allocation_storage[candidate_slot][0] = 0U;
        EXPECT(file_index_poll(1U, &steps) == OK);
        EXPECT(poll_until_ready() == OK);
    }
    EXPECT(file_index_validate_state() == OK);
    EXPECT(file_index_get_status(&status) == OK);
    EXPECT(status.state == FILE_INDEX_STATE_READY);
    EXPECT(status.candidate_entries == 0U);
    EXPECT(status.memory_bytes > 0U);
    EXPECT(fake_log_count > 0U);

    coverage_active = 0U;
    coverage_emit(0);
    puts("file-index-host: PASS");
    return 0;
}
