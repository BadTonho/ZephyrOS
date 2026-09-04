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

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_OUTPUT_CAPACITY 4096U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static char video_output[HOST_OUTPUT_CAPACITY];
static uint32_t video_output_length;

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
    kmemset(out_stats, 0, sizeof(*out_stats));
    return ERR_UNAVAILABLE;
}

int block_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = 0U;
    return OK;
}

int block_get_at(uint32_t index, block_device_t* out_device) {
    (void)index;
    if (!out_device) return ERR_NULL;
    kmemset(out_device, 0, sizeof(*out_device));
    return ERR_NOT_FOUND;
}

int block_cache_get_stats(block_cache_stats_t* out_stats) {
    if (!out_stats) return ERR_NULL;
    kmemset(out_stats, 0, sizeof(*out_stats));
    return ERR_UNAVAILABLE;
}

int block_cache_get_durability_status(
    block_durability_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    kmemset(out_status, 0, sizeof(*out_status));
    out_status->state = BLOCK_DURABILITY_DEGRADED;
    return ERR_UNAVAILABLE;
}

int block_cache_clear(void) { return ERR_UNAVAILABLE; }
int vfs_sync(void) { return ERR_UNAVAILABLE; }

int storage_get_status(storage_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    kmemset(out_status, 0, sizeof(*out_status));
    return ERR_UNAVAILABLE;
}

int storage_get_disk_at(uint8_t index, storage_disk_t* out_disk) {
    (void)index;
    if (!out_disk) return ERR_NULL;
    kmemset(out_disk, 0, sizeof(*out_disk));
    return ERR_NOT_FOUND;
}

int storage_find_disk(const char* id, storage_disk_t* out_disk) {
    (void)id;
    if (!out_disk) return ERR_NULL;
    kmemset(out_disk, 0, sizeof(*out_disk));
    return ERR_NOT_FOUND;
}

int storage_get_volume_at(uint8_t index, storage_volume_t* out_volume) {
    (void)index;
    if (!out_volume) return ERR_NULL;
    kmemset(out_volume, 0, sizeof(*out_volume));
    return ERR_NOT_FOUND;
}

int storage_find_volume(const char* id, storage_volume_t* out_volume) {
    (void)id;
    if (!out_volume) return ERR_NULL;
    kmemset(out_volume, 0, sizeof(*out_volume));
    return ERR_NOT_FOUND;
}

int storage_find_system_volume(storage_volume_t* out_volume) {
    if (!out_volume) return ERR_NULL;
    kmemset(out_volume, 0, sizeof(*out_volume));
    return ERR_NOT_FOUND;
}

int storage_check(const char* id) {
    (void)id;
    return ERR_UNAVAILABLE;
}

const char* storage_fs_name(storage_fs_type_t type) {
    (void)type;
    return "NONE";
}

const char* storage_volume_state_name(storage_volume_state_t state) {
    (void)state;
    return "UNAVAILABLE";
}

int vfs_mount_volume(const char* volume_id) {
    (void)volume_id;
    return ERR_UNAVAILABLE;
}

int vfs_unmount_volume(const char* volume_id) {
    (void)volume_id;
    return ERR_UNAVAILABLE;
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
    (void)value;
    (void)digits;
}

int file_index_rebuild(void) { return ERR_UNAVAILABLE; }
int file_index_cancel(void) { return OK; }
int file_index_validate_state(void) { return ERR_UNAVAILABLE; }
int file_index_self_test(void) { return ERR_UNAVAILABLE; }

int file_index_get_status(file_index_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    kmemset(out_status, 0, sizeof(*out_status));
    out_status->state = FILE_INDEX_STATE_EMPTY;
    return OK;
}

int file_index_search(const char* query, file_index_result_t* results,
                      uint32_t capacity,
                      file_index_search_status_t* out_status) {
    (void)query;
    (void)results;
    (void)capacity;
    (void)out_status;
    return ERR_UNAVAILABLE;
}

const char* file_index_state_name(file_index_state_t state) {
    (void)state;
    return "EMPTY";
}

int shell_job_start(const shell_job_definition_t* definition,
                    const char* arguments) {
    (void)definition;
    (void)arguments;
    return ERR_UNAVAILABLE;
}

int shell_job_generation_matches(uint32_t generation) {
    (void)generation;
    return 1;
}

void shell_job_set_phase(shell_job_context_t* context, const char* phase) {
    (void)context;
    (void)phase;
}

void shell_job_set_progress(shell_job_context_t* context, uint32_t completed,
                            uint32_t total) {
    (void)context;
    (void)completed;
    (void)total;
}

static int check_index_rejections(void) {
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
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
