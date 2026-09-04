#include <stdint.h>
#include <stdio.h>

#include "apps/shell_command_utils.h"
#include "apps/shell_pipeline.h"
#include "apps/shell_runtime.h"
#include "core/app_builtin.h"
#include "core/app_loader.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/power.h"
#include "core/string.h"
#include "core/video.h"
#include "drivers/speaker.h"
#include "fs/vfs.h"
#include "memory/compress.h"
#include "process/process.h"
#include "process/thread.h"

void shell_dispatch_cmd_help(const char* arguments);
void shell_dispatch_cmd_clear(const char* arguments);
void shell_dispatch_cmd_ls(const char* arguments);
void shell_dispatch_cmd_cat(const char* arguments);
void shell_dispatch_cmd_echo(const char* arguments);
void shell_dispatch_cmd_mem(const char* arguments);
void shell_dispatch_cmd_procs(const char* arguments);
void shell_dispatch_cmd_stack(const char* arguments);
void shell_dispatch_cmd_threads(const char* arguments);
void shell_dispatch_cmd_threadtest(const char* arguments);
void shell_dispatch_cmd_uptime(const char* arguments);
void shell_dispatch_cmd_beep(const char* arguments);
void shell_dispatch_cmd_melody(const char* arguments);
void shell_dispatch_cmd_reboot(const char* arguments);
void shell_dispatch_cmd_shutdown(const char* arguments);
void shell_dispatch_cmd_poweroff(const char* arguments);
void shell_dispatch_cmd_compress(const char* arguments);
void shell_dispatch_cmd_stats(const char* arguments);
const char* shell_core_builtin_app_name(shell_builtin_app_t app);
int shell_core_handle_loader_result(const app_loader_result_t* result);

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_OUTPUT_CAPACITY 16384U
#define HOST_CAT_BUFFER_SIZE 4096U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static char video_output[HOST_OUTPUT_CAPACITY];
static char pipeline_output[HOST_OUTPUT_CAPACITY];
static uint32_t video_output_length;
static uint32_t pipeline_output_length;
static uint8_t pipeline_active;
static uint8_t hosted_visible;
static uint32_t terminal_clear_calls;
static uint32_t taskbar_draw_calls;
static uint32_t video_begin_calls;
static uint32_t video_end_calls;
static uint32_t shell_finish_calls;
static uint32_t beep_calls;
static uint32_t melody_calls;
static uint32_t reboot_calls;
static uint32_t shutdown_calls;
static uint32_t system_reboot_calls;
static uint32_t threadtest_calls;
static int app_echo_result;
static int app_mem_result;
static int app_uptime_result;
static uint32_t next_loader_pid;
static int power_result;
static int system_reboot_result;
static int shutdown_result;
static uint8_t compression_enabled;
static compress_stats_t compression_stats;
static uint8_t cat_buffer[HOST_CAT_BUFFER_SIZE];
static process_t fake_process;
static thread_t fake_thread;

process_t* processes[MAX_PROCESSES];
uint32_t process_count;

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
    printf("ZCOV_BEGIN|case=host:shell:commands-core|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:commands-core|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:commands-core|value=0x%08X\n",
           (uint32_t)result);
}

static void output_append(char* output, uint32_t* length, const char* text) {
    if (!text) return;
    while (*text && *length + 1U < HOST_OUTPUT_CAPACITY) {
        output[(*length)++] = *text++;
    }
    output[*length] = '\0';
}

static void output_reset(void) {
    video_output_length = 0U;
    pipeline_output_length = 0U;
    video_output[0] = '\0';
    pipeline_output[0] = '\0';
}

static void copy_text(char* output, uint32_t capacity, const char* input) {
    uint32_t index = 0U;

    if (!output || !capacity) return;
    if (!input) {
        output[0] = '\0';
        return;
    }
    while (input[index] && index + 1U < capacity) {
        output[index] = input[index];
        index++;
    }
    output[index] = '\0';
}

static int output_contains(const char* output, const char* needle) {
    uint32_t output_length = kstrlen(output);
    uint32_t needle_length = kstrlen(needle);

    if (!needle_length) return 1;
    if (needle_length > output_length) return 0;
    for (uint32_t offset = 0U; offset + needle_length <= output_length;
         offset++) {
        uint32_t index = 0U;
        while (index < needle_length &&
               output[offset + index] == needle[index]) index++;
        if (index == needle_length) return 1;
    }
    return 0;
}

static void fixture_reset(void) {
    output_reset();
    pipeline_active = 0U;
    hosted_visible = 0U;
    terminal_clear_calls = 0U;
    taskbar_draw_calls = 0U;
    video_begin_calls = 0U;
    video_end_calls = 0U;
    shell_finish_calls = 0U;
    beep_calls = 0U;
    melody_calls = 0U;
    reboot_calls = 0U;
    shutdown_calls = 0U;
    system_reboot_calls = 0U;
    threadtest_calls = 0U;
    app_echo_result = ERR_UNAVAILABLE;
    app_mem_result = ERR_UNAVAILABLE;
    app_uptime_result = ERR_UNAVAILABLE;
    next_loader_pid = 41U;
    power_result = OK;
    system_reboot_result = OK;
    shutdown_result = OK;
    compression_enabled = 0U;
    kmemset(&compression_stats, 0, sizeof(compression_stats));
    kmemset(cat_buffer, 0, sizeof(cat_buffer));
    kmemset(&fake_process, 0, sizeof(fake_process));
    kmemset(&fake_thread, 0, sizeof(fake_thread));
    fake_process.pid = 7U;
    fake_process.state = PROCESS_STATE_READY;
    copy_text(fake_process.name, PROCESS_NAME_LENGTH, "host-proc");
    fake_thread.id = 1U;
    fake_thread.state = THREAD_RUNNING;
    copy_text(fake_thread.name, THREAD_NAME_LENGTH, "host-thread");
    processes[0] = &fake_process;
    for (uint32_t index = 1U; index < MAX_PROCESSES; index++) {
        processes[index] = 0;
    }
    process_count = 1U;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void video_print(const char* text, uint8_t color) {
    (void)color;
    output_append(video_output, &video_output_length, text);
}

void video_begin_update(void) {
    video_begin_calls++;
}

void video_end_update(void) {
    video_end_calls++;
}

void video_terminal_clear(void) {
    terminal_clear_calls++;
}

int shell_runtime_is_hosted_visible(void) {
    return hosted_visible;
}

void taskbar_draw(void) {
    taskbar_draw_calls++;
}

int shell_pipeline_is_active(void) {
    return pipeline_active;
}

int shell_pipeline_write(const char* text, uint8_t color) {
    (void)color;
    if (!text) return ERR_NULL;
    output_append(pipeline_output, &pipeline_output_length, text);
    return OK;
}

void shell_pipeline_print_num(uint32_t value) {
    shell_command_print_num(value);
}

void shell_runtime_finish_command(void) {
    shell_finish_calls++;
}

void* kmalloc(uint32_t size) {
    if (size > sizeof(cat_buffer)) return 0;
    return cat_buffer;
}

void kfree(void* ptr) {
    (void)ptr;
}

uint32_t memory_get_total(void) {
    return 32U * 1024U * 1024U;
}

uint32_t memory_get_free(void) {
    return 20U * 1024U * 1024U;
}

uint32_t memory_get_used(void) {
    return 12U * 1024U * 1024U;
}

void memory_get_heap_stats(memory_heap_stats_t* stats) {
    if (!stats) return;
    kmemset(stats, 0, sizeof(*stats));
    stats->used_bytes = 4096U;
    stats->free_bytes = 8192U;
    stats->fragmentation_percent = 3U;
}

int memory_get_detailed_stats(memory_detailed_stats_t* stats) {
    if (!stats) return ERR_NULL;
    kmemset(stats, 0, sizeof(*stats));
    stats->total_pages = 8192U;
    stats->zone_pages[MEMORY_ZONE_KERNEL] = 512U;
    stats->zone_pages[MEMORY_ZONE_HEAP] = 1024U;
    stats->zone_pages[MEMORY_ZONE_SLAB] = 128U;
    stats->zone_pages[MEMORY_ZONE_PROCESS] = 256U;
    stats->zone_pages[MEMORY_ZONE_BUFFER] = 128U;
    stats->zone_pages[MEMORY_ZONE_FREE] = 6144U;
    stats->free_runs = 4U;
    stats->largest_free_run = 2048U;
    stats->initialized = 1U;
    stats->valid = 1U;
    return OK;
}

int vfs_getcwd(char* path, uint32_t capacity) {
    if (!path || !capacity) return ERR_NULL;
    if (capacity < 2U) return ERR_OVERFLOW;
    path[0] = '/';
    path[1] = '\0';
    return OK;
}

int vfs_list_dir(const char* path, vfs_dir_entry_t* entries,
                uint32_t capacity, uint32_t* count) {
    if (!path || !entries || !count) return ERR_NULL;
    if (!capacity) return ERR_OVERFLOW;
    if (kstrcmp(path, "missing") == 0) return ERR_NOT_FOUND;
    copy_text(entries[0].name, sizeof(entries[0].name), "README.TXT");
    entries[0].type = VFS_NODE_REGULAR;
    entries[0].size = 12U;
    *count = 1U;
    return OK;
}

int vfs_open(const char* path, uint32_t mode, int32_t* fd_out) {
    (void)mode;
    if (!path || !fd_out) return ERR_NULL;
    if (kstrcmp(path, "README.TXT") != 0) return ERR_NOT_FOUND;
    *fd_out = 3;
    return OK;
}

int vfs_read(int32_t fd, void* buffer, uint32_t size, uint32_t* bytes_read) {
    static const char content[] = "hello\n";
    uint32_t length = sizeof(content) - 1U;

    if (!buffer || !bytes_read) return ERR_NULL;
    if (fd != 3) return ERR_INVALID;
    if (size < length) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < length; index++) {
        ((uint8_t*)buffer)[index] = (uint8_t)content[index];
    }
    *bytes_read = length;
    return OK;
}

int vfs_close(int32_t fd) {
    return fd == 3 ? OK : ERR_INVALID;
}

int app_loader_build_launch_info(const char* text, app_launch_info_t* launch) {
    if (!text || !launch) return ERR_NULL;
    kmemset(launch, 0, sizeof(*launch));
    launch->abi_version = APP_LAUNCH_ABI_VERSION;
    return OK;
}

int app_builtin_run_echo(const app_launch_info_t* launch, uint32_t* pid_out) {
    if (!launch || !pid_out) return ERR_NULL;
    if (app_echo_result == OK) *pid_out = next_loader_pid++;
    return app_echo_result;
}

int app_builtin_run_mem(uint32_t* pid_out) {
    if (app_mem_result == OK && pid_out) *pid_out = next_loader_pid++;
    return app_mem_result;
}

int app_builtin_run_uptime(uint32_t* pid_out) {
    if (app_uptime_result == OK && pid_out) *pid_out = next_loader_pid++;
    return app_uptime_result;
}

uint32_t timer_get_ticks(void) {
    return 3725U;
}

int process_stack_get_info(uint32_t pid, process_stack_info_t* output) {
    if (!output) return ERR_NULL;
    if (pid != fake_process.pid) return ERR_NOT_FOUND;
    kmemset(output, 0, sizeof(*output));
    output->pid = pid;
    copy_text(output->name, sizeof(output->name), fake_process.name);
    output->stack_size = 8192U;
    output->bytes_free = 4096U;
    output->minimum_bytes_free = 3072U;
    output->lower_canary_ok = 1U;
    output->upper_canary_ok = 1U;
    return OK;
}

int process_stack_validate_all(process_stack_validation_t* validation) {
    if (!validation) return ERR_NULL;
    kmemset(validation, 0, sizeof(*validation));
    validation->checked = process_count;
    validation->valid = process_count;
    return OK;
}

int process_stack_self_test(void) {
    return OK;
}

uint32_t thread_get_count(void) {
    return 1U;
}

thread_t* thread_get_by_id(uint32_t id) {
    return id == fake_thread.id ? &fake_thread : 0;
}

int thread_run_self_test(void) {
    threadtest_calls++;
    return OK;
}

void speaker_beep(uint32_t frequency, uint32_t duration_ms) {
    if (frequency && duration_ms) beep_calls++;
}

void speaker_play_melody(const uint32_t* frequencies,
                         const uint32_t* durations, int notes) {
    if (frequencies && durations && notes == 8) melody_calls++;
}

int power_reboot(void) {
    reboot_calls++;
    return power_result;
}

int system_reboot(void) {
    system_reboot_calls++;
    return system_reboot_result;
}

int power_shutdown_request(void) {
    shutdown_calls++;
    return shutdown_result;
}

uint8_t compress_is_enabled(void) {
    return compression_enabled;
}

void compress_enable(void) {
    compression_enabled = 1U;
}

void compress_disable(void) {
    compression_enabled = 0U;
}

void compress_print_stats(void) {
    video_print("stats\n", 0x07);
}

static int check_names(void) {
    const char* uptime_name = shell_core_builtin_app_name(SHELL_BUILTIN_APP_UPTIME);
    const char* mem_name = shell_core_builtin_app_name(SHELL_BUILTIN_APP_MEM);
    const char* unknown_name = shell_core_builtin_app_name(SHELL_BUILTIN_APP_NONE);

    if (kstrcmp(uptime_name, "uptime") != 0) return 1;
    if (kstrcmp(mem_name, "mem") != 0) {
        return 2;
    }
    if (kstrcmp(unknown_name, "desconhecido") != 0) return 3;
    return 0;
}

static int check_display_commands(void) {
    fixture_reset();
    shell_dispatch_cmd_help(0);
    if (video_begin_calls != 1U || video_end_calls != 1U ||
        !output_contains(video_output, "Comandos disponiveis")) return 1;
    output_reset();
    shell_dispatch_cmd_clear(0);
    if (terminal_clear_calls != 1U || taskbar_draw_calls != 1U) return 2;
    hosted_visible = 1U;
    shell_dispatch_cmd_clear(0);
    if (taskbar_draw_calls != 1U) return 3;
    return 0;
}

static int check_file_commands(void) {
    fixture_reset();
    shell_dispatch_cmd_ls(0);
    if (!output_contains(pipeline_output, "README.TXT\n")) return 1;
    output_reset();
    shell_dispatch_cmd_ls("missing");
    if (!output_contains(video_output, "codigo 4")) return 2;
    output_reset();
    shell_dispatch_cmd_cat(0);
    if (!output_contains(video_output, "Uso: cat")) return 3;
    output_reset();
    shell_dispatch_cmd_cat("README.TXT");
    if (!output_contains(pipeline_output, "hello\n")) return 4;
    return 0;
}

static int check_runtime_commands(void) {
    app_loader_result_t loader_result;

    fixture_reset();
    pipeline_active = 1U;
    shell_dispatch_cmd_echo("pipeline");
    if (!output_contains(pipeline_output, "pipeline\n")) return 1;
    pipeline_active = 0U;
    shell_dispatch_cmd_echo("native");
    if (!output_contains(pipeline_output, "native\n")) return 2;
    app_echo_result = OK;
    shell_dispatch_cmd_echo("ring3");
    kmemset(&loader_result, 0, sizeof(loader_result));
    loader_result.pid = 41U;
    loader_result.exit_code = ERR_STATE;
    loader_result.faulted = 1U;
    if (!shell_core_handle_loader_result(&loader_result) ||
        shell_finish_calls != 1U) return 3;
    output_reset();
    app_mem_result = OK;
    shell_dispatch_cmd_mem(0);
    kmemset(&loader_result, 0, sizeof(loader_result));
    loader_result.pid = 42U;
    loader_result.exit_code = ERR_STATE;
    loader_result.start_failed = 1U;
    if (!shell_core_handle_loader_result(&loader_result) ||
        shell_finish_calls != 2U ||
        !output_contains(video_output, "mem ring 3 nao concluiu")) return 4;
    app_mem_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_mem("");
    if (!output_contains(video_output, "Memoria:")) return 5;
    output_reset();
    shell_dispatch_cmd_mem("detailed");
    if (!output_contains(video_output, "Memoria detalhada:")) return 6;
    output_reset();
    shell_dispatch_cmd_mem("bad");
    if (!output_contains(video_output, "Uso: mem")) return 7;
    shell_dispatch_cmd_procs(0);
    shell_dispatch_cmd_threads(0);
    shell_dispatch_cmd_stack("");
    shell_dispatch_cmd_stack("check");
    shell_dispatch_cmd_stack("bad");
    if (!output_contains(video_output, "StackCheck: OK")) return 8;
    return 0;
}

static int check_device_and_power_commands(void) {
    fixture_reset();
    shell_dispatch_cmd_threadtest(0);
    if (threadtest_calls != 1U) return 1;
    shell_dispatch_cmd_uptime(0);
    if (!output_contains(video_output, "Uptime: 0h 1m")) return 2;
    shell_dispatch_cmd_beep(0);
    shell_dispatch_cmd_beep("440 100");
    shell_dispatch_cmd_melody(0);
    if (beep_calls != 2U || melody_calls != 1U) return 3;
    shell_dispatch_cmd_reboot("bad");
    shell_dispatch_cmd_reboot(0);
    if (reboot_calls != 1U) return 4;
    shell_dispatch_cmd_shutdown("bad");
    shell_dispatch_cmd_shutdown("-r now");
    shell_dispatch_cmd_poweroff("bad");
    shell_dispatch_cmd_poweroff(0);
    if (system_reboot_calls != 1U || shutdown_calls != 1U) return 5;
    return 0;
}

static int check_compression_commands(void) {
    fixture_reset();
    shell_dispatch_cmd_compress(0);
    if (!compression_enabled || !output_contains(video_output, "ATIVADA")) {
        return 1;
    }
    output_reset();
    shell_dispatch_cmd_compress(0);
    if (compression_enabled || !output_contains(video_output, "DESATIVADA")) {
        return 2;
    }
    output_reset();
    shell_dispatch_cmd_stats(0);
    if (!output_contains(video_output, "stats")) return 3;
    return 0;
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    if (!result) result = check_names();
    if (!result) result = check_display_commands();
    if (!result) result = check_file_commands();
    if (!result) result = check_runtime_commands();
    if (!result) result = check_device_and_power_commands();
    if (!result) result = check_compression_commands();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        printf("SHELL_COMMANDS_CORE_FAIL:%d\n", result);
        return result;
    }
    printf("SHELL_COMMANDS_CORE_PASS\n");
    return 0;
}
