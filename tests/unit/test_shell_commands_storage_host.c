#include <stdint.h>
#include <stdio.h>

#include "apps/shell_command_utils.h"
#include "apps/shell_job.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/video.h"
#include "fs/block.h"
#include "fs/block_cache.h"
#include "fs/file_index.h"
#include "fs/storage.h"
#include "fs/vfs.h"

void shell_dispatch_cmd_index(const char* arguments);
void shell_dispatch_cmd_search(const char* arguments);
void shell_dispatch_cmd_storage(const char* arguments);
void shell_dispatch_cmd_blkstat(const char* arguments);
void shell_dispatch_cmd_cachestat(const char* arguments);
void shell_dispatch_cmd_cache(const char* arguments);
void shell_dispatch_cmd_sync(const char* arguments);

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_OUTPUT_CAPACITY 4096U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static char video_output[HOST_OUTPUT_CAPACITY];
static uint32_t video_output_length;

static int fixture_block_stats_result;
static int fixture_block_count_result;
static int fixture_block_at_result;
static uint32_t fixture_block_count;
static block_queue_stats_t fixture_block_stats;
static block_device_t fixture_block_devices[2];
static int fixture_cache_stats_result;
static int fixture_durability_result;
static int fixture_cache_clear_result;
static block_cache_stats_t fixture_cache_stats;
static block_durability_status_t fixture_durability;
static int fixture_sync_result;
static int fixture_storage_status_result;
static int fixture_storage_disk_at_result;
static int fixture_storage_volume_at_result;
static int fixture_storage_find_disk_result;
static int fixture_storage_find_volume_result;
static int fixture_storage_system_volume_result;
static int fixture_storage_check_result;
static int fixture_mount_result;
static int fixture_unmount_result;
static storage_status_t fixture_storage_status;
static storage_disk_t fixture_storage_disks[2];
static storage_volume_t fixture_storage_volumes[2];
static int fixture_index_rebuild_result;
static int fixture_index_cancel_result;
static int fixture_index_validate_result;
static int fixture_index_self_test_result;
static int fixture_index_status_result;
static int fixture_index_search_result;
static file_index_status_t fixture_index_status;
static file_index_search_status_t fixture_search_status;
static file_index_result_t fixture_search_results[2];
static int fixture_job_start_result;
static int fixture_job_generation_matches_result;
static int fixture_job_cancel_calls;
static int fixture_job_set_phase_calls;
static int fixture_job_set_progress_calls;
static const shell_job_definition_t* fixture_job_definition;

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
    printf("ZCOV_BEGIN|case=host:shell:commands-storage|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:commands-storage|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:commands-storage|value=0x%08X\n",
           (uint32_t)result);
}

static void fixture_reset(void) {
    fixture_block_stats_result = ERR_UNAVAILABLE;
    fixture_block_count_result = OK;
    fixture_block_at_result = ERR_NOT_FOUND;
    fixture_block_count = 0U;
    fixture_cache_stats_result = ERR_UNAVAILABLE;
    fixture_durability_result = ERR_UNAVAILABLE;
    fixture_cache_clear_result = ERR_UNAVAILABLE;
    fixture_sync_result = ERR_UNAVAILABLE;
    fixture_storage_status_result = ERR_UNAVAILABLE;
    fixture_storage_disk_at_result = ERR_NOT_FOUND;
    fixture_storage_volume_at_result = ERR_NOT_FOUND;
    fixture_storage_find_disk_result = ERR_NOT_FOUND;
    fixture_storage_find_volume_result = ERR_NOT_FOUND;
    fixture_storage_system_volume_result = ERR_NOT_FOUND;
    fixture_storage_check_result = ERR_UNAVAILABLE;
    fixture_mount_result = ERR_UNAVAILABLE;
    fixture_unmount_result = ERR_UNAVAILABLE;
    fixture_index_rebuild_result = ERR_UNAVAILABLE;
    fixture_index_cancel_result = OK;
    fixture_index_validate_result = ERR_UNAVAILABLE;
    fixture_index_self_test_result = ERR_UNAVAILABLE;
    fixture_index_status_result = OK;
    fixture_index_search_result = ERR_UNAVAILABLE;
    fixture_job_start_result = ERR_UNAVAILABLE;
    fixture_job_generation_matches_result = 1;
    fixture_job_cancel_calls = 0;
    fixture_job_set_phase_calls = 0;
    fixture_job_set_progress_calls = 0;
    fixture_job_definition = 0;
    kmemset(&fixture_block_stats, 0, sizeof(fixture_block_stats));
    kmemset(fixture_block_devices, 0, sizeof(fixture_block_devices));
    kmemset(&fixture_cache_stats, 0, sizeof(fixture_cache_stats));
    kmemset(&fixture_durability, 0, sizeof(fixture_durability));
    kmemset(&fixture_storage_status, 0, sizeof(fixture_storage_status));
    kmemset(fixture_storage_disks, 0, sizeof(fixture_storage_disks));
    kmemset(fixture_storage_volumes, 0, sizeof(fixture_storage_volumes));
    kmemset(&fixture_index_status, 0, sizeof(fixture_index_status));
    kmemset(&fixture_search_status, 0, sizeof(fixture_search_status));
    kmemset(fixture_search_results, 0, sizeof(fixture_search_results));
}

static void output_reset(void) {
    video_output_length = 0U;
    video_output[0] = '\0';
}

static void output_append(const char* text) {
    if (!text) return;
    while (*text && video_output_length + 1U < HOST_OUTPUT_CAPACITY) {
        video_output[video_output_length++] = *text++;
    }
    video_output[video_output_length] = '\0';
}

static int expect_text(const char* expected) {
    if (kstrcmp(video_output, expected) == 0) return 0;
    fprintf(stderr, "storage-commands-host: saida inesperada: %s\n",
            video_output);
    return 1;
}

static int expect_contains(const char* expected) {
    uint32_t expected_length;
    uint32_t output_length;

    if (!expected) return 1;
    expected_length = kstrlen(expected);
    output_length = kstrlen(video_output);
    if (!expected_length || expected_length > output_length) return 1;
    for (uint32_t offset = 0U;
         offset + expected_length <= output_length; offset++) {
        uint32_t index = 0U;
        while (index < expected_length &&
               video_output[offset + index] == expected[index]) index++;
        if (index == expected_length) return 0;
    }
    fprintf(stderr, "storage-commands-host: trecho ausente: %s\n",
            expected);
    return 1;
}

static int expect_absent(const char* expected) {
    uint32_t expected_length;
    uint32_t output_length;

    if (!expected) return 1;
    expected_length = kstrlen(expected);
    output_length = kstrlen(video_output);
    if (!expected_length || expected_length > output_length) return 0;
    for (uint32_t offset = 0U;
         offset + expected_length <= output_length; offset++) {
        uint32_t index = 0U;
        while (index < expected_length &&
               video_output[offset + index] == expected[index]) index++;
        if (index == expected_length) {
            fprintf(stderr, "storage-commands-host: trecho inesperado: %s\n",
                    expected);
            return 1;
        }
    }
    return 0;
}

void video_print(const char* text, uint8_t color) {
    (void)color;
    output_append(text);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

int block_get_stats(block_queue_stats_t* out_stats) {
    if (!out_stats) return ERR_NULL;
    *out_stats = fixture_block_stats;
    return fixture_block_stats_result;
}

int block_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = fixture_block_count;
    return fixture_block_count_result;
}

int block_get_at(uint32_t index, block_device_t* out_device) {
    if (!out_device) return ERR_NULL;
    if (fixture_block_at_result != OK || index >= 2U) {
        kmemset(out_device, 0, sizeof(*out_device));
        return fixture_block_at_result;
    }
    *out_device = fixture_block_devices[index];
    return OK;
}

int block_cache_get_stats(block_cache_stats_t* out_stats) {
    if (!out_stats) return ERR_NULL;
    *out_stats = fixture_cache_stats;
    return fixture_cache_stats_result;
}

int block_cache_get_durability_status(
    block_durability_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_durability;
    return fixture_durability_result;
}

int block_cache_clear(void) { return fixture_cache_clear_result; }
int vfs_sync(void) { return fixture_sync_result; }

int storage_get_status(storage_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_storage_status;
    return fixture_storage_status_result;
}

int storage_get_disk_at(uint8_t index, storage_disk_t* out_disk) {
    if (!out_disk) return ERR_NULL;
    if (fixture_storage_disk_at_result != OK || index >= 2U) {
        kmemset(out_disk, 0, sizeof(*out_disk));
        return fixture_storage_disk_at_result;
    }
    *out_disk = fixture_storage_disks[index];
    return OK;
}

int storage_find_disk(const char* id, storage_disk_t* out_disk) {
    if (!out_disk) return ERR_NULL;
    if (fixture_storage_find_disk_result != OK || !id) {
        kmemset(out_disk, 0, sizeof(*out_disk));
        return fixture_storage_find_disk_result;
    }
    for (uint32_t index = 0U; index < 2U; index++) {
        if (kstrcmp(id, fixture_storage_disks[index].id) == 0) {
            *out_disk = fixture_storage_disks[index];
            return OK;
        }
    }
    kmemset(out_disk, 0, sizeof(*out_disk));
    return ERR_NOT_FOUND;
}

int storage_get_volume_at(uint8_t index, storage_volume_t* out_volume) {
    if (!out_volume) return ERR_NULL;
    if (fixture_storage_volume_at_result != OK || index >= 2U) {
        kmemset(out_volume, 0, sizeof(*out_volume));
        return fixture_storage_volume_at_result;
    }
    *out_volume = fixture_storage_volumes[index];
    return OK;
}

int storage_find_volume(const char* id, storage_volume_t* out_volume) {
    if (!out_volume) return ERR_NULL;
    if (fixture_storage_find_volume_result != OK || !id) {
        kmemset(out_volume, 0, sizeof(*out_volume));
        return fixture_storage_find_volume_result;
    }
    for (uint32_t index = 0U; index < 2U; index++) {
        if (kstrcmp(id, fixture_storage_volumes[index].id) == 0) {
            *out_volume = fixture_storage_volumes[index];
            return OK;
        }
    }
    kmemset(out_volume, 0, sizeof(*out_volume));
    return ERR_NOT_FOUND;
}

int storage_find_system_volume(storage_volume_t* out_volume) {
    if (!out_volume) return ERR_NULL;
    if (fixture_storage_system_volume_result != OK) {
        kmemset(out_volume, 0, sizeof(*out_volume));
        return fixture_storage_system_volume_result;
    }
    *out_volume = fixture_storage_volumes[0];
    return OK;
}

int storage_check(const char* id) {
    (void)id;
    return fixture_storage_check_result;
}

const char* storage_fs_name(storage_fs_type_t type) {
    if (type == STORAGE_FS_FAT12) return "FAT12";
    if (type == STORAGE_FS_FAT32) return "FAT32";
    return "NONE";
}

const char* storage_volume_state_name(storage_volume_state_t state) {
    if (state == STORAGE_VOLUME_DETECTED) return "DETECTED";
    if (state == STORAGE_VOLUME_INVALID) return "INVALID";
    if (state == STORAGE_VOLUME_UNSUPPORTED) return "UNSUPPORTED";
    return "MOUNTED";
}

int vfs_mount_volume(const char* volume_id) {
    (void)volume_id;
    return fixture_mount_result;
}

int vfs_unmount_volume(const char* volume_id) {
    (void)volume_id;
    return fixture_unmount_result;
}

int shell_command_args_equal(const char* args, const char* expected) {
    if (!args || !expected) return 0;
    while (*args == ' ' || *args == '\t') args++;
    while (*expected == ' ' || *expected == '\t') expected++;
    if (kstrcmp(args, expected) != 0) return 0;
    return 1;
}

void shell_command_print_num(uint32_t value) {
    char buffer[16];
    int length = 0;

    if (!value) {
        output_append("0");
        return;
    }
    while (value && length < 15) {
        buffer[length++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    while (length) {
        char digit[2] = {buffer[--length], '\0'};
        output_append(digit);
    }
}

void shell_command_print_hex(uint32_t value, uint32_t digits) {
    static const char hex[] = "0123456789ABCDEF";

    while (digits) {
        uint32_t shift = (digits - 1U) * 4U;
        char digit[2];

        digit[0] = hex[(value >> shift) & 0x0FU];
        digit[1] = '\0';
        output_append(digit);
        digits--;
    }
}

int file_index_rebuild(void) { return fixture_index_rebuild_result; }

int file_index_cancel(void) {
    fixture_job_cancel_calls++;
    return fixture_index_cancel_result;
}

int file_index_validate_state(void) { return fixture_index_validate_result; }
int file_index_self_test(void) { return fixture_index_self_test_result; }

int file_index_get_status(file_index_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    *out_status = fixture_index_status;
    return fixture_index_status_result;
}

int file_index_search(const char* query, file_index_result_t* results,
                      uint32_t capacity,
                      file_index_search_status_t* out_status) {
    (void)query;
    if (!results || !out_status) return ERR_NULL;
    if (fixture_index_search_result != OK) return fixture_index_search_result;
    *out_status = fixture_search_status;
    if (capacity > 2U) capacity = 2U;
    for (uint32_t index = 0U; index < capacity; index++) {
        results[index] = fixture_search_results[index];
    }
    return OK;
}

const char* file_index_state_name(file_index_state_t state) {
    if (state == FILE_INDEX_STATE_UNINITIALIZED) return "UNINITIALIZED";
    if (state == FILE_INDEX_STATE_BUILDING) return "BUILDING";
    if (state == FILE_INDEX_STATE_READY) return "READY";
    if (state == FILE_INDEX_STATE_CANCELLED) return "CANCELLED";
    if (state == FILE_INDEX_STATE_FAILED) return "FAILED";
    return "EMPTY";
}

int shell_job_start(const shell_job_definition_t* definition,
                    const char* arguments) {
    fixture_job_definition = definition;
    (void)arguments;
    return fixture_job_start_result;
}

int shell_job_generation_matches(uint32_t generation) {
    (void)generation;
    return fixture_job_generation_matches_result;
}

void shell_job_set_phase(shell_job_context_t* context, const char* phase) {
    if (!context || !phase) return;
    fixture_job_set_phase_calls++;
    uint32_t index = 0U;
    while (phase[index] && index + 1U < SHELL_JOB_PHASE_SIZE) {
        context->phase[index] = phase[index];
        index++;
    }
    context->phase[index] = '\0';
}

void shell_job_set_progress(shell_job_context_t* context, uint32_t completed,
                            uint32_t total) {
    if (!context) return;
    fixture_job_set_progress_calls++;
    context->progress = completed;
    context->total = total;
}

static void fixture_copy(char* output, const char* input) {
    kmemcpy(output, input, kstrlen(input) + 1U);
}

static int check_block_commands(void) {
    fixture_reset();
    output_reset();
    shell_dispatch_cmd_blkstat("bad");
    if (expect_text("Uso: blkstat\n")) return 1;

    output_reset();
    shell_dispatch_cmd_blkstat("");
    if (expect_text("Metricas da camada de bloco indisponiveis.\n")) return 2;

    fixture_block_stats_result = OK;
    fixture_block_count_result = ERR_DISK;
    output_reset();
    shell_dispatch_cmd_blkstat("");
    if (expect_text("Metricas da camada de bloco indisponiveis.\n")) return 3;

    fixture_block_count_result = OK;
    fixture_block_at_result = OK;
    fixture_block_count = 2U;
    fixture_block_stats.queue_capacity = 32U;
    fixture_block_stats.queue_depth = 3U;
    fixture_block_stats.peak_depth = 7U;
    fixture_block_stats.in_flight = 2U;
    fixture_block_stats.submitted = 9U;
    fixture_block_stats.completed = 8U;
    fixture_block_stats.failed = 1U;
    fixture_block_stats.cancelled = 2U;
    fixture_block_stats.merged = 3U;
    fixture_block_stats.read_sectors = 10U;
    fixture_block_stats.write_sectors = 11U;
    fixture_block_stats.read_sectors_per_second = 12U;
    fixture_block_stats.write_sectors_per_second = 13U;
    fixture_block_stats.last_error = ERR_DISK;
    fixture_copy(fixture_block_devices[0].id, "ata0");
    fixture_copy(fixture_block_devices[0].model, "ATA fixture");
    fixture_block_devices[0].read_ops = 14U;
    fixture_block_devices[0].write_ops = 15U;
    fixture_block_devices[0].last_error = ERR_TIMEOUT;
    fixture_block_devices[0].read_only = 0U;
    fixture_copy(fixture_block_devices[1].id, "usb0");
    fixture_copy(fixture_block_devices[1].model, "USB fixture");
    fixture_block_devices[1].read_ops = 16U;
    fixture_block_devices[1].write_ops = 0U;
    fixture_block_devices[1].last_error = OK;
    fixture_block_devices[1].read_only = 1U;
    output_reset();
    shell_dispatch_cmd_blkstat("");
    if (expect_contains("Block: fila=3/32 pico=7 voo=2")) return 4;
    if (expect_contains("ata0 modelo=ATA fixture")) return 5;
    if (expect_contains("usb0 modelo=USB fixture")) return 6;
    return 0;
}

static void fixture_set_cache_stats(void) {
    fixture_cache_stats.capacity = 64U;
    fixture_cache_stats.block_size = 512U;
    fixture_cache_stats.memory_bytes = 32768U;
    fixture_cache_stats.entries = 4U;
    fixture_cache_stats.valid_entries = 3U;
    fixture_cache_stats.reading_entries = 1U;
    fixture_cache_stats.dirty_entries = 2U;
    fixture_cache_stats.writeback_entries = 1U;
    fixture_cache_stats.pinned_entries = 1U;
    fixture_cache_stats.hits = 8U;
    fixture_cache_stats.misses = 2U;
    fixture_cache_stats.reads_avoided = 6U;
    fixture_cache_stats.physical_reads = 2U;
    fixture_cache_stats.evictions = 1U;
    fixture_cache_stats.invalidations = 2U;
    fixture_cache_stats.bypasses = 3U;
    fixture_cache_stats.errors = 1U;
    fixture_cache_stats.hit_rate_percent = 80U;
    fixture_cache_stats.last_error = ERR_DISK;
    fixture_cache_stats.dirty_bytes = 1024U;
    fixture_cache_stats.writeback_attempts = 5U;
    fixture_cache_stats.writeback_completed = 4U;
    fixture_cache_stats.writeback_failures = 1U;
    fixture_cache_stats.sync_operations = 3U;
    fixture_cache_stats.flush_operations = 2U;
    fixture_cache_stats.flush_unavailable = 1U;
    fixture_cache_stats.degraded_syncs = 1U;
    fixture_cache_stats.physical_writes = 4U;
}

static int check_cache_commands(void) {
    fixture_reset();
    output_reset();
    shell_dispatch_cmd_cachestat("bad");
    if (expect_text("Uso: cachestat\n")) return 1;

    output_reset();
    shell_dispatch_cmd_cachestat("");
    if (expect_text("Metricas do cache de blocos indisponiveis.\n")) return 2;

    fixture_cache_stats_result = OK;
    fixture_durability_result = OK;
    fixture_set_cache_stats();
    fixture_durability.state = BLOCK_DURABILITY_READY;
    output_reset();
    shell_dispatch_cmd_cachestat("");
    if (expect_contains("Cache: entradas=4/64 validas=3")) return 3;
    if (expect_contains("durabilidade=READY")) return 4;

    fixture_durability.state = BLOCK_DURABILITY_DEGRADED;
    output_reset();
    shell_dispatch_cmd_cachestat("");
    if (expect_contains("durabilidade=DEGRADED")) return 5;

    fixture_durability.state = BLOCK_DURABILITY_ERROR;
    output_reset();
    shell_dispatch_cmd_cachestat("");
    if (expect_contains("durabilidade=ERROR")) return 6;

    fixture_durability_result = ERR_UNAVAILABLE;
    output_reset();
    shell_dispatch_cmd_cachestat("");
    if (expect_contains("Estado: evictions=1")) return 7;
    if (expect_absent("durabilidade=")) return 8;

    output_reset();
    shell_dispatch_cmd_cache("bad");
    if (expect_text("Uso: cache clear\n")) return 9;
    fixture_cache_clear_result = OK;
    output_reset();
    shell_dispatch_cmd_cache("clear");
    if (expect_text("Cache de blocos limpo.\n")) return 10;
    fixture_cache_clear_result = ERR_DISK;
    output_reset();
    shell_dispatch_cmd_cache("clear");
    if (expect_text("Erro: limpeza do cache recusada (codigo 3).\n")) return 11;
    return 0;
}

static int check_sync_commands(void) {
    fixture_reset();
    output_reset();
    shell_dispatch_cmd_sync("extra");
    if (expect_text("Uso: sync\n")) return 1;

    fixture_cache_stats_result = OK;
    fixture_durability_result = OK;
    fixture_set_cache_stats();
    fixture_durability.state = BLOCK_DURABILITY_READY;
    fixture_sync_result = ERR_DISK;
    output_reset();
    shell_dispatch_cmd_sync("");
    if (expect_text("Erro: sync falhou (codigo 3).\n")) return 2;

    fixture_sync_result = OK;
    fixture_cache_stats_result = ERR_UNAVAILABLE;
    output_reset();
    shell_dispatch_cmd_sync("");
    if (expect_text("Erro: metricas de sync indisponiveis.\n")) return 3;

    fixture_cache_stats_result = OK;
    output_reset();
    shell_dispatch_cmd_sync("");
    if (expect_contains("Sync: writeback=4 falhas=1 durabilidade=READY")) return 4;
    return 0;
}

static void fixture_set_storage(void) {
    fixture_storage_status.initialized = 1U;
    fixture_storage_status.disk_count = 2U;
    fixture_storage_status.volume_count = 2U;
    fixture_storage_status.mounted_count = 1U;
    fixture_storage_status_result = OK;
    fixture_storage_disk_at_result = OK;
    fixture_storage_volume_at_result = OK;
    fixture_storage_find_disk_result = OK;
    fixture_storage_find_volume_result = OK;
    fixture_storage_system_volume_result = OK;
    fixture_copy(fixture_storage_disks[0].id, "ata0");
    fixture_copy(fixture_storage_disks[0].model, "ATA disk");
    fixture_storage_disks[0].kind = STORAGE_DISK_ATA;
    fixture_storage_disks[0].sector_size = 512U;
    fixture_storage_disks[0].slot = 1U;
    fixture_storage_disks[0].channel = 0U;
    fixture_storage_disks[0].slave = 0U;
    fixture_storage_disks[0].sector_count = 1000U;
    fixture_storage_disks[0].read_ops = 10U;
    fixture_storage_disks[0].write_ops = 4U;
    fixture_storage_disks[0].last_error = OK;
    fixture_storage_disks[0].read_only = 0U;
    fixture_copy(fixture_storage_disks[1].id, "usb0");
    fixture_copy(fixture_storage_disks[1].model, "USB disk");
    fixture_storage_disks[1].kind = STORAGE_DISK_USB_MSC;
    fixture_storage_disks[1].sector_size = 512U;
    fixture_storage_disks[1].sector_count = 2000U;
    fixture_storage_disks[1].read_ops = 20U;
    fixture_storage_disks[1].write_ops = 0U;
    fixture_storage_disks[1].last_error = ERR_UNAVAILABLE;
    fixture_storage_disks[1].read_only = 1U;
    fixture_copy(fixture_storage_volumes[0].id, "SYS0");
    fixture_copy(fixture_storage_volumes[0].disk_id, "ata0");
    fixture_copy(fixture_storage_volumes[0].label, "SYSTEM");
    fixture_storage_volumes[0].fs_type = STORAGE_FS_FAT32;
    fixture_storage_volumes[0].state = STORAGE_VOLUME_MOUNTED;
    fixture_storage_volumes[0].mounted = 1U;
    fixture_storage_volumes[0].read_only = 0U;
    fixture_storage_volumes[0].bytes_per_sector = 512U;
    fixture_storage_volumes[0].sectors_per_cluster = 8U;
    fixture_storage_volumes[0].start_lba = 2048U;
    fixture_storage_volumes[0].sector_count = 800U;
    fixture_storage_volumes[0].generation = 3U;
    fixture_storage_volumes[0].partition_index = 1U;
    fixture_storage_volumes[0].partition_type = 0x0CU;
    fixture_copy(fixture_storage_volumes[1].id, "LEGACY1");
    fixture_copy(fixture_storage_volumes[1].disk_id, "usb0");
    fixture_storage_volumes[1].fs_type = STORAGE_FS_FAT12;
    fixture_storage_volumes[1].state = STORAGE_VOLUME_DETECTED;
    fixture_storage_volumes[1].mounted = 0U;
    fixture_storage_volumes[1].read_only = 1U;
    fixture_storage_volumes[1].bytes_per_sector = 512U;
    fixture_storage_volumes[1].sectors_per_cluster = 1U;
    fixture_storage_volumes[1].start_lba = 32U;
    fixture_storage_volumes[1].sector_count = 100U;
    fixture_storage_volumes[1].generation = 4U;
    fixture_storage_volumes[1].partition_index = 2U;
    fixture_storage_volumes[1].partition_type = 0x01U;
}

static int check_storage_commands(void) {
    fixture_reset();
    output_reset();
    shell_dispatch_cmd_storage(0);
    if (expect_contains("Uso: storage list")) return 1;

    fixture_set_storage();
    output_reset();
    shell_dispatch_cmd_storage("");
    if (expect_contains("Storage: discos=2 volumes=2 montados=1")) return 2;
    if (expect_contains("Uso: storage list")) return 3;

    output_reset();
    shell_dispatch_cmd_storage("list extra");
    if (expect_contains("Uso: storage list")) return 4;

    output_reset();
    shell_dispatch_cmd_storage("list");
    if (expect_contains("Disco ata0: ATA disk")) return 5;
    if (expect_contains("Provedor: USB MSC somente-leitura")) return 6;
    if (expect_contains("Volume SYS0 (ata0)")) return 7;
    if (expect_contains("FAT32")) return 8;
    if (expect_contains("Tipo: 0C")) return 9;
    if (expect_contains("(sem label)")) return 10;
    if (expect_contains("Volume FAT32 do sistema montado e gravavel.")) return 11;

    fixture_storage_system_volume_result = ERR_NOT_FOUND;
    output_reset();
    shell_dispatch_cmd_storage("list");
    if (expect_contains("Volume FAT32 do sistema nao montado; FAT12 legado em fallback.")) return 12;

    output_reset();
    shell_dispatch_cmd_storage("info ata0");
    if (expect_contains("Disco ata0: ATA disk")) return 13;
    output_reset();
    shell_dispatch_cmd_storage("info SYS0");
    if (expect_contains("Volume SYS0 (ata0)")) return 14;
    if (expect_contains("Disco ata0: ATA disk")) return 15;

    output_reset();
    shell_dispatch_cmd_storage("info ataX");
    if (expect_contains("Erro: disco ou volume nao encontrado.")) return 16;
    if (expect_contains("Mais proximo: ata0")) return 17;
    if (expect_contains("recebido=0x58")) return 18;
    if (expect_contains("catalogo=0x30")) return 19;

    fixture_storage_check_result = OK;
    output_reset();
    shell_dispatch_cmd_storage("check SYS0");
    if (expect_text("Volume FAT32 consistente.\n")) return 20;
    fixture_storage_check_result = ERR_DISK;
    output_reset();
    shell_dispatch_cmd_storage("check SYS0");
    if (expect_text("Erro: verificacao FAT32 recusada.\n")) return 21;

    fixture_mount_result = OK;
    output_reset();
    shell_dispatch_cmd_storage("mount SYS0");
    if (expect_text("Volume montado somente-leitura em RAM.\n")) return 22;
    fixture_unmount_result = ERR_STATE;
    output_reset();
    shell_dispatch_cmd_storage("unmount SYS0");
    if (expect_text("Erro: operacao storage recusada (codigo 7).\n")) return 23;

    output_reset();
    shell_dispatch_cmd_storage("unknown SYS0");
    if (expect_contains("Uso: storage list")) return 24;
    output_reset();
    shell_dispatch_cmd_storage("check");
    if (expect_contains("Uso: storage list")) return 25;
    output_reset();
    shell_dispatch_cmd_storage("info SYS0 extra");
    if (expect_contains("Uso: storage list")) return 26;
    output_reset();
    shell_dispatch_cmd_storage("storage list");
    if (expect_contains("Uso: storage list")) return 27;

    output_reset();
    shell_dispatch_cmd_storage("this-action-is-too-long");
    if (expect_contains("Uso: storage list")) return 28;
    output_reset();
    shell_dispatch_cmd_storage("info id-with-more-than-forty-seven-characters-xxxxxxxx");
    if (expect_contains("Uso: storage list")) return 29;

    fixture_storage_status.initialized = 0U;
    output_reset();
    shell_dispatch_cmd_storage("list");
    if (expect_text("Storage indisponivel.\n")) return 30;
    fixture_storage_status_result = ERR_DISK;
    output_reset();
    shell_dispatch_cmd_storage("list");
    if (expect_text("Storage indisponivel.\n")) return 31;
    return 0;
}

static int check_index_commands(void) {
    fixture_reset();
    fixture_index_status.state = FILE_INDEX_STATE_READY;
    fixture_index_status.active_entries = 8U;
    fixture_index_status.candidate_entries = 9U;
    fixture_index_status.source_count = 2U;
    fixture_index_status.sources_completed = 2U;
    fixture_index_status.directories_scanned = 3U;
    fixture_index_status.scan_steps = 4U;
    fixture_index_status.memory_bytes = 512U;
    fixture_index_status.event_generation = 6U;
    fixture_index_status.partial = 1U;
    fixture_index_status.stale = 1U;
    fixture_index_status.automatic_suspended = 1U;
    fixture_index_status.last_error = ERR_DISK;
    output_reset();
    shell_dispatch_cmd_index("status");
    if (expect_contains("Indice: READY ativos=8 candidato=9 fontes=2")) return 1;
    if (expect_contains("Aviso: indice parcial.")) return 2;
    if (expect_contains("Aviso: indice desatualizado.")) return 3;
    if (expect_contains("Aviso: rebuild automatico suspenso.")) return 4;

    fixture_index_validate_result = OK;
    fixture_index_self_test_result = OK;
    output_reset();
    shell_dispatch_cmd_index("check");
    if (expect_text("Indice e autoteste validos.\n")) return 5;
    fixture_index_validate_result = ERR_STATE;
    output_reset();
    shell_dispatch_cmd_index("check");
    if (expect_text("Operacao do indice falhou; codigo=7\n")) return 6;

    fixture_index_cancel_result = OK;
    output_reset();
    shell_dispatch_cmd_index("cancel");
    if (expect_text("Rebuild cancelado; indice ativo preservado.\n")) return 7;
    fixture_index_status_result = ERR_UNAVAILABLE;
    output_reset();
    shell_dispatch_cmd_index("status");
    if (expect_text("Indice indisponivel.\n")) return 8;
    return 0;
}

static int check_search_commands(void) {
    char long_query[FILE_INDEX_QUERY_SIZE + 1U];

    fixture_reset();
    for (uint32_t index = 0U; index < FILE_INDEX_QUERY_SIZE; index++) {
        long_query[index] = 'x';
    }
    long_query[FILE_INDEX_QUERY_SIZE] = '\0';
    output_reset();
    shell_dispatch_cmd_search(long_query);
    if (expect_text("Uso: search <termo de ate 63 caracteres>\n")) return 1;

    fixture_index_search_result = OK;
    fixture_search_status.total_matches = 3U;
    fixture_search_status.returned_matches = 2U;
    fixture_search_status.partial = 1U;
    fixture_search_status.building = 1U;
    fixture_search_status.cancelled = 1U;
    fixture_search_status.stale = 1U;
    fixture_search_status.volume_missing = 1U;
    fixture_search_status.result_stale = 1U;
    fixture_search_status.last_error = ERR_DISK;
    fixture_copy(fixture_search_results[0].entry.volume_id, "SYS0");
    fixture_copy(fixture_search_results[0].entry.parent_path, "etc");
    fixture_copy(fixture_search_results[0].entry.name, "config");
    fixture_search_results[0].entry.size = 20U;
    fixture_search_results[0].entry.is_directory = 0U;
    fixture_search_results[0].availability = FILE_INDEX_RESULT_AVAILABLE;
    fixture_copy(fixture_search_results[1].entry.volume_id, "USB0");
    fixture_copy(fixture_search_results[1].entry.name, "docs");
    fixture_search_results[1].entry.size = 0U;
    fixture_search_results[1].entry.is_directory = 1U;
    fixture_search_results[1].availability = FILE_INDEX_RESULT_VOLUME_MISSING;
    output_reset();
    shell_dispatch_cmd_search("config");
    if (expect_contains("SYS0:/etc/config  [ARQ] 20 bytes")) return 2;
    if (expect_contains("USB0:/docs  [DIR] 0 bytes  [volume ausente ou desmontado]")) return 3;
    if (expect_contains("Correspondencias: 3 (exibidas 2)")) return 4;
    if (expect_contains("Aviso: resultados parciais.")) return 5;
    if (expect_contains("Aviso: indice em construcao.")) return 6;
    if (expect_contains("Aviso: rebuild cancelado.")) return 7;
    if (expect_contains("Aviso: indice desatualizado.")) return 8;
    if (expect_contains("Aviso: um ou mais volumes estao ausentes.")) return 9;
    if (expect_contains("Aviso: um ou mais resultados estao obsoletos.")) return 10;
    if (expect_contains("Aviso: ultimo erro do indice=3")) return 11;

    fixture_search_results[0].availability = FILE_INDEX_RESULT_STALE;
    fixture_search_status.returned_matches = 1U;
    fixture_search_status.total_matches = 1U;
    output_reset();
    shell_dispatch_cmd_search("old");
    if (expect_contains("[resultado obsoleto]")) return 12;

    fixture_index_search_result = ERR_UNAVAILABLE;
    fixture_index_status_result = OK;
    fixture_index_status.state = FILE_INDEX_STATE_CANCELLED;
    output_reset();
    shell_dispatch_cmd_search("missing");
    if (expect_contains("Pesquisa indisponivel; codigo=9")) return 13;
    if (expect_contains("Aviso: rebuild cancelado e sem indice ativo.")) return 14;
    return 0;
}

static int check_index_job(void) {
    shell_job_context_t context;
    shell_job_step_result_t step_result;

    fixture_reset();
    fixture_index_rebuild_result = OK;
    fixture_index_status_result = OK;
    fixture_index_status.state = FILE_INDEX_STATE_BUILDING;
    fixture_index_status.operation_generation = 12U;
    fixture_index_status.scan_steps = 3U;
    fixture_index_status.candidate_entries = 9U;
    fixture_job_start_result = OK;
    output_reset();
    shell_dispatch_cmd_index("rebuild");
    if (!fixture_job_definition) return 1;
    kmemset(&context, 0, sizeof(context));
    context.generation = 4U;
    step_result = fixture_job_definition->step(&context);
    if (step_result != SHELL_JOB_STEP_PENDING ||
        fixture_job_set_phase_calls != 1 || fixture_job_set_progress_calls != 1) {
        return 2;
    }
    if (kstrcmp(context.phase, "BUILDING") != 0 || context.progress != 3U ||
        context.total != 9U) return 3;

    fixture_index_status.state = FILE_INDEX_STATE_READY;
    fixture_index_status.operation_generation = 12U;
    step_result = fixture_job_definition->step(&context);
    if (step_result != SHELL_JOB_STEP_COMPLETE || context.last_error != OK) return 4;
    if (fixture_job_definition->drain(&context) != SHELL_JOB_STEP_COMPLETE) return 5;

    fixture_index_status.state = FILE_INDEX_STATE_BUILDING;
    if (fixture_job_definition->drain(&context) != SHELL_JOB_STEP_PENDING) return 6;
    context.cancel_requested = 1U;
    fixture_index_cancel_result = OK;
    if (fixture_job_definition->step(&context) != SHELL_JOB_STEP_CANCELLED) return 7;
    fixture_index_cancel_result = ERR_DISK;
    if (fixture_job_definition->cancel(&context) != ERR_DISK) return 8;
    if (fixture_job_definition->step(&context) != SHELL_JOB_STEP_FAILED) return 9;

    context.cancel_requested = 0U;
    fixture_job_generation_matches_result = 0;
    if (fixture_job_definition->step(&context) != SHELL_JOB_STEP_FAILED ||
        context.last_error != ERR_STATE) return 10;
    fixture_job_generation_matches_result = 1;
    fixture_index_status.operation_generation = 13U;
    if (fixture_job_definition->step(&context) != SHELL_JOB_STEP_FAILED ||
        context.last_error != ERR_STATE) return 11;
    fixture_index_status.operation_generation = 12U;
    fixture_index_status.state = FILE_INDEX_STATE_FAILED;
    fixture_index_status.last_error = ERR_DISK;
    if (fixture_job_definition->step(&context) != SHELL_JOB_STEP_FAILED ||
        context.last_error != ERR_DISK) return 12;
    fixture_index_status.state = FILE_INDEX_STATE_CANCELLED;
    fixture_index_status.last_error = OK;
    if (fixture_job_definition->step(&context) != SHELL_JOB_STEP_CANCELLED) return 13;
    if (fixture_job_definition->step(0) != SHELL_JOB_STEP_FAILED) return 14;
    fixture_index_status_result = ERR_UNAVAILABLE;
    if (fixture_job_definition->drain(&context) != SHELL_JOB_STEP_FAILED) return 15;

    fixture_index_status_result = OK;
    output_reset();
    fixture_job_definition->finish(&context, SHELL_JOB_STATE_SUCCEEDED, OK);
    if (expect_text("Indice reconstruido com sucesso.\n")) return 16;
    output_reset();
    fixture_job_definition->finish(&context, SHELL_JOB_STATE_CANCELLED,
                                   ERR_CANCELLED);
    if (expect_text("Rebuild do indice cancelado; indice ativo preservado.\n")) return 17;
    output_reset();
    fixture_job_definition->finish(&context, SHELL_JOB_STATE_FAILED, ERR_DISK);
    if (expect_text("Rebuild do indice falhou; codigo=3\n")) return 18;

    fixture_reset();
    fixture_index_rebuild_result = ERR_DISK;
    output_reset();
    shell_dispatch_cmd_index("rebuild");
    if (expect_text("Operacao do indice falhou; codigo=3\n")) return 19;
    fixture_index_rebuild_result = OK;
    fixture_index_status_result = ERR_STATE;
    output_reset();
    shell_dispatch_cmd_index("rebuild");
    if (expect_text("Status do indice indisponivel.\n")) return 20;
    fixture_index_status_result = OK;
    fixture_job_start_result = ERR_UNAVAILABLE;
    output_reset();
    shell_dispatch_cmd_index("rebuild");
    if (expect_text("Job do indice recusado; codigo=9\n")) return 21;
    return 0;
}

static int check_index_rejections(void) {
    fixture_reset();
    output_reset();
    shell_dispatch_cmd_index(0);
    if (expect_text("Uso: index status | index rebuild | index cancel | "
                    "index check\n")) return 1;

    output_reset();
    shell_dispatch_cmd_index("unknown");
    if (expect_text("Uso: index status | index rebuild | index cancel | "
                    "index check\n")) return 2;

    output_reset();
    shell_dispatch_cmd_index("status extra");
    if (expect_text("Uso: index status | index rebuild | index cancel | "
                    "index check\n")) return 3;

    output_reset();
    shell_dispatch_cmd_index("rebuild");
    if (expect_text("Operacao do indice falhou; codigo=9\n")) return 4;
    return 0;
}

static int check_search_rejections(void) {
    fixture_reset();
    output_reset();
    shell_dispatch_cmd_search(0);
    if (expect_text("Uso: search <termo de ate 63 caracteres>\n")) return 1;

    output_reset();
    shell_dispatch_cmd_search("   ");
    if (expect_text("Uso: search <termo de ate 63 caracteres>\n")) return 2;

    output_reset();
    shell_dispatch_cmd_search("query");
    if (expect_text("Pesquisa indisponivel; codigo=9\n")) return 3;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_index_rejections();
    if (!result) result = check_search_rejections();
    if (!result) result = check_block_commands();
    if (!result) result = check_cache_commands();
    if (!result) result = check_sync_commands();
    if (!result) result = check_storage_commands();
    if (!result) result = check_index_commands();
    if (!result) result = check_search_commands();
    if (!result) result = check_index_job();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
