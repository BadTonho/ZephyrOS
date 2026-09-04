#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/video.h"
#include "apps/shell_dispatch.h"
#include "apps/shell_pipeline.h"

#define HOST_COVERAGE_CAPACITY 2048U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_OUTPUT_CAPACITY 256U
#define SHELL_COMMAND_MAX_LENGTH 31U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static char video_output[HOST_OUTPUT_CAPACITY];
static uint32_t video_output_length;
static uint32_t log_calls;
static uint32_t echo_calls;
static uint32_t handler_calls;
static char handler_arguments[HOST_OUTPUT_CAPACITY];
static char echo_arguments[HOST_OUTPUT_CAPACITY];

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
    printf("ZCOV_BEGIN|case=host:shell:dispatch|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:dispatch|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:dispatch|value=0x%08X\n",
           (uint32_t)result);
}

static void output_reset(void) {
    video_output_length = 0U;
    video_output[0] = '\0';
}

static void output_append(const char* text) {
    while (*text && video_output_length + 1U < HOST_OUTPUT_CAPACITY) {
        video_output[video_output_length++] = *text++;
    }
    video_output[video_output_length] = '\0';
}

static int output_equals(const char* expected) {
    return kstrcmp(video_output, expected) == 0;
}

static void copy_echo_arguments(const char* arguments) {
    uint32_t index = 0U;

    echo_calls++;
    handler_calls++;
    while (arguments[index] && index + 1U < HOST_OUTPUT_CAPACITY) {
        handler_arguments[index] = arguments[index];
        index++;
    }
    handler_arguments[index] = '\0';
    index = 0U;
    while (arguments[index] && index + 1U < HOST_OUTPUT_CAPACITY) {
        echo_arguments[index] = arguments[index];
        index++;
    }
    echo_arguments[index] = '\0';
}

void video_print(const char* text, uint8_t color) {
    (void)color;
    output_append(text);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
    log_calls++;
}

int shell_pipeline_try_execute(const char* input, uint8_t* handled) {
    (void)input;
    *handled = 0U;
    return OK;
}

static void record_handler_call(const char* arguments) {
    uint32_t index = 0U;

    handler_calls++;
    while (arguments[index] && index + 1U < HOST_OUTPUT_CAPACITY) {
        handler_arguments[index] = arguments[index];
        index++;
    }
    handler_arguments[index] = '\0';
}

#define SHELL_STUB(name) \
    void name(const char* arguments) { record_handler_call(arguments); }

SHELL_STUB(shell_dispatch_cmd_job)
SHELL_STUB(shell_dispatch_cmd_help)
SHELL_STUB(shell_dispatch_cmd_clear)
SHELL_STUB(shell_dispatch_cmd_ls)
SHELL_STUB(shell_dispatch_cmd_cat)
void shell_dispatch_cmd_echo(const char* arguments) {
    copy_echo_arguments(arguments);
}
SHELL_STUB(shell_dispatch_cmd_mem)
SHELL_STUB(shell_dispatch_cmd_procs)
SHELL_STUB(shell_dispatch_cmd_stack)
SHELL_STUB(shell_dispatch_cmd_threads)
SHELL_STUB(shell_dispatch_cmd_threadtest)
SHELL_STUB(shell_dispatch_cmd_uptime)
SHELL_STUB(shell_dispatch_cmd_health)
SHELL_STUB(shell_dispatch_cmd_log)
SHELL_STUB(shell_dispatch_cmd_irqstat)
SHELL_STUB(shell_dispatch_cmd_timer)
SHELL_STUB(shell_dispatch_cmd_clock)
SHELL_STUB(shell_dispatch_cmd_tls)
SHELL_STUB(shell_dispatch_cmd_wait)
SHELL_STUB(shell_dispatch_cmd_wqinfo)
SHELL_STUB(shell_dispatch_cmd_workq)
SHELL_STUB(shell_dispatch_cmd_kill)
SHELL_STUB(shell_dispatch_cmd_sigtest)
SHELL_STUB(shell_dispatch_cmd_vfs)
SHELL_STUB(shell_dispatch_cmd_devcheck)
SHELL_STUB(shell_dispatch_cmd_proccheck)
SHELL_STUB(shell_dispatch_cmd_mount)
SHELL_STUB(shell_dispatch_cmd_pwd)
SHELL_STUB(shell_dispatch_cmd_cd)
SHELL_STUB(shell_dispatch_cmd_devices)
SHELL_STUB(shell_dispatch_cmd_device_info)
SHELL_STUB(shell_dispatch_cmd_device_scan)
SHELL_STUB(shell_dispatch_cmd_usb)
SHELL_STUB(shell_dispatch_cmd_net)
SHELL_STUB(shell_dispatch_cmd_skbstat)
SHELL_STUB(shell_dispatch_cmd_sockstat)
SHELL_STUB(shell_dispatch_cmd_selecttest)
SHELL_STUB(shell_dispatch_cmd_netstat)
SHELL_STUB(shell_dispatch_cmd_route)
SHELL_STUB(shell_dispatch_cmd_wifi)
SHELL_STUB(shell_dispatch_cmd_ping)
SHELL_STUB(shell_dispatch_cmd_nslookup)
SHELL_STUB(shell_dispatch_cmd_http)
SHELL_STUB(shell_dispatch_cmd_acpi)
SHELL_STUB(shell_dispatch_cmd_power)
SHELL_STUB(shell_dispatch_cmd_kmetrics)
SHELL_STUB(shell_dispatch_cmd_cpu_usage)
SHELL_STUB(shell_dispatch_cmd_memcheck)
SHELL_STUB(shell_dispatch_cmd_slabinfo)
SHELL_STUB(shell_dispatch_cmd_slabtest)
SHELL_STUB(shell_dispatch_cmd_pagefault)
SHELL_STUB(shell_dispatch_cmd_vmamap)
SHELL_STUB(shell_dispatch_cmd_schedcheck)
SHELL_STUB(shell_dispatch_cmd_q2check)
SHELL_STUB(shell_dispatch_cmd_regcheck)
SHELL_STUB(shell_dispatch_cmd_appcheck)
SHELL_STUB(shell_dispatch_cmd_blkcheck)
SHELL_STUB(shell_dispatch_cmd_pkg)
SHELL_STUB(shell_dispatch_cmd_store)
SHELL_STUB(shell_dispatch_cmd_update)
SHELL_STUB(shell_dispatch_cmd_pkgcheck)
SHELL_STUB(shell_dispatch_cmd_app)
SHELL_STUB(shell_dispatch_cmd_usertest)
SHELL_STUB(shell_dispatch_cmd_beep)
SHELL_STUB(shell_dispatch_cmd_melody)
SHELL_STUB(shell_dispatch_cmd_desktop)
SHELL_STUB(shell_dispatch_cmd_guimode)
SHELL_STUB(shell_dispatch_cmd_display)
SHELL_STUB(shell_dispatch_cmd_explorer)
SHELL_STUB(shell_dispatch_cmd_reboot)
SHELL_STUB(shell_dispatch_cmd_shutdown)
SHELL_STUB(shell_dispatch_cmd_poweroff)
SHELL_STUB(shell_dispatch_cmd_guitest)
SHELL_STUB(shell_dispatch_cmd_taskmgr)
SHELL_STUB(shell_dispatch_cmd_taskcfg)
SHELL_STUB(shell_dispatch_cmd_settings)
SHELL_STUB(shell_dispatch_cmd_updater)
SHELL_STUB(shell_dispatch_cmd_wm)
SHELL_STUB(shell_dispatch_cmd_play)
SHELL_STUB(shell_dispatch_cmd_view)
SHELL_STUB(shell_dispatch_cmd_icons)
SHELL_STUB(shell_dispatch_cmd_stop)
SHELL_STUB(shell_dispatch_cmd_compress)
SHELL_STUB(shell_dispatch_cmd_stats)
SHELL_STUB(shell_dispatch_cmd_edit)
SHELL_STUB(shell_dispatch_cmd_storage)
SHELL_STUB(shell_dispatch_cmd_blkstat)
SHELL_STUB(shell_dispatch_cmd_cachestat)
SHELL_STUB(shell_dispatch_cmd_cache)
SHELL_STUB(shell_dispatch_cmd_sync)
SHELL_STUB(shell_dispatch_cmd_index)
SHELL_STUB(shell_dispatch_cmd_search)
SHELL_STUB(shell_dispatch_cmd_mouse)
SHELL_STUB(shell_dispatch_cmd_grep)
SHELL_STUB(shell_dispatch_cmd_pipetest)

static int check_unknown_command(void) {
    const char* expected = "Comando nao encontrado: missing\n"
                           "Digite 'help' para ver os comandos.\n";

    output_reset();
    if (shell_dispatch_execute("missing arg") != OK) return 1;
    if (!output_equals(expected)) return 2;
    return 0;
}

static int check_input_normalization(void) {
    const char* expected = "Comando nao encontrado: mystery\n"
                           "Digite 'help' para ver os comandos.\n";

    output_reset();
    if (shell_dispatch_execute("\x1B \t mystery value") != OK) return 1;
    if (!output_equals(expected)) return 2;
    output_reset();
    if (shell_dispatch_execute("   \t\r\n") != OK) return 3;
    if (video_output_length != 0U) return 4;
    return 0;
}

static int check_command_limit(void) {
    char input[48];
    uint32_t index;

    for (index = 0U; index < 40U; index++) input[index] = 'x';
    input[index++] = ' ';
    input[index] = '\0';
    output_reset();
    if (shell_dispatch_execute(input) != OK) return 1;
    if (kstrcmp(video_output,
                "Comando nao encontrado: xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n"
                "Digite 'help' para ver os comandos.\n") != 0) return 2;
    return 0;
}

static int check_known_command_and_null(void) {
    output_reset();
    echo_calls = 0U;
    echo_arguments[0] = '\0';
    if (shell_dispatch_execute("echo hello world") != OK) return 1;
    if (echo_calls != 1U || kstrcmp(echo_arguments, "hello world") != 0) {
        return 2;
    }
    if (video_output_length != 0U) return 3;
    if (shell_dispatch_execute(NULL) != ERR_NULL) return 4;
    if (log_calls == 0U) return 5;
    return 0;
}

static int check_registered_commands(void) {
    static const char* commands[] = {
        "job", "help", "clear", "ls", "cat", "echo", "grep",
        "pipetest", "mem", "procs", "stack", "threads", "threadtest",
        "uptime", "health", "log", "irqstat", "timer", "clock", "tls",
        "wait", "wqinfo", "workq", "kill", "sigtest", "vfs", "devcheck",
        "proccheck", "mount", "pwd", "cd", "devices", "device-info",
        "device-scan", "usb", "net", "skbstat", "sockstat", "selecttest",
        "netstat", "route", "wifi", "ping", "nslookup", "http", "acpi",
        "power", "kmetrics", "cpu", "memcheck", "slabinfo", "slabtest",
        "pagefault", "vmamap", "schedcheck", "q2check", "regcheck",
        "appcheck", "blkcheck", "pkg", "store", "update", "pkgcheck", "app",
        "usertest", "beep", "melody", "desktop", "guimode", "display",
        "explorer", "reboot", "shutdown", "poweroff", "guitest", "taskmgr",
        "taskcfg", "settings", "updater", "wm", "play", "view", "icons",
        "stop", "compress", "stats", "edit", "storage", "blkstat", "cachestat",
        "cache", "sync", "index", "search", "mouse"
    };
    char input[64];
    uint32_t command_count =
        sizeof(commands) / sizeof(commands[0]);

    for (uint32_t index = 0U; index < command_count; index++) {
        uint32_t input_length = 0U;
        const char* command = commands[index];

        while (command[input_length] && input_length + 1U < sizeof(input)) {
            input[input_length] = command[input_length];
            input_length++;
        }
        input[input_length++] = ' ';
        input[input_length++] = 'p';
        input[input_length++] = 'r';
        input[input_length++] = 'o';
        input[input_length++] = 'b';
        input[input_length++] = 'e';
        input[input_length] = '\0';

        handler_calls = 0U;
        handler_arguments[0] = '\0';
        echo_calls = 0U;
        if (shell_dispatch_execute(input) != OK) return 1;
        if (handler_calls != 1U ||
            kstrcmp(handler_arguments, "probe") != 0) return 2;
        if (kstrcmp(command, "echo") == 0 && echo_calls != 1U) return 3;
    }
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    log_calls = 0U;
    result = check_unknown_command();
    if (result == 0) result = check_input_normalization();
    if (result == 0) result = check_command_limit();
    if (result == 0) result = check_known_command_and_null();
    if (result == 0) result = check_registered_commands();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
