#include <stdint.h>
#include <stdio.h>

#include "apps/shell_command_utils.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/video.h"
#include "drivers/mouse.h"
#include "fs/vfs.h"

void shell_dispatch_cmd_pwd(const char* arguments);
void shell_dispatch_cmd_cd(const char* arguments);
void shell_dispatch_cmd_mouse(const char* arguments);
void shell_dispatch_cmd_log(const char* arguments);

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_OUTPUT_CAPACITY 4096U
#define HOST_PATH_CAPACITY 256U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static char video_output[HOST_OUTPUT_CAPACITY];
static uint32_t video_output_length;
static char fixture_cwd[HOST_PATH_CAPACITY];
static char fixture_last_path[HOST_PATH_CAPACITY];
static mouse_status_t fixture_mouse_status;
static int fixture_getcwd_result;
static int fixture_chdir_result;
static int fixture_mouse_status_result;
static int fixture_mouse_speed_result;
static int fixture_mouse_primary_result;
static int fixture_mouse_acceleration_result;
static uint32_t fixture_chdir_calls;
static uint32_t fixture_speed_calls;
static uint32_t fixture_primary_calls;
static uint32_t fixture_acceleration_calls;
static log_stats_t fixture_log_stats;
static log_record_t fixture_log_records[2];
static log_self_test_result_t fixture_log_test;
static int fixture_log_stats_result;
static int fixture_log_copy_result;
static int fixture_log_set_console_result;
static int fixture_log_set_buffer_result;
static int fixture_log_test_result;
static uint32_t fixture_log_copy_count;
static uint32_t fixture_log_clear_calls;
static uint32_t fixture_log_console_calls;
static uint32_t fixture_log_buffer_calls;
static log_level_t fixture_log_console_level;
static log_level_t fixture_log_buffer_level;

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
    printf("ZCOV_BEGIN|case=host:shell:diagnostics|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:diagnostics|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:diagnostics|value=0x%08X\n",
           (uint32_t)result);
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

static void fixture_reset(void) {
    output_reset();
    copy_text(fixture_cwd, sizeof(fixture_cwd), "/home/test");
    fixture_last_path[0] = '\0';
    fixture_getcwd_result = OK;
    fixture_chdir_result = OK;
    fixture_mouse_status_result = OK;
    fixture_mouse_speed_result = OK;
    fixture_mouse_primary_result = OK;
    fixture_mouse_acceleration_result = OK;
    fixture_chdir_calls = 0U;
    fixture_speed_calls = 0U;
    fixture_primary_calls = 0U;
    fixture_acceleration_calls = 0U;
    fixture_log_stats_result = OK;
    fixture_log_copy_result = OK;
    fixture_log_set_console_result = OK;
    fixture_log_set_buffer_result = OK;
    fixture_log_test_result = OK;
    fixture_log_copy_count = 0U;
    fixture_log_clear_calls = 0U;
    fixture_log_console_calls = 0U;
    fixture_log_buffer_calls = 0U;
    fixture_log_console_level = LOG_LEVEL_INFO;
    fixture_log_buffer_level = LOG_LEVEL_DEBUG;
    kmemset(&fixture_log_stats, 0, sizeof(fixture_log_stats));
    kmemset(fixture_log_records, 0, sizeof(fixture_log_records));
    kmemset(&fixture_log_test, 0, sizeof(fixture_log_test));
    fixture_log_stats.capacity = LOG_RECORD_CAPACITY;
    fixture_log_stats.occupancy = 2U;
    fixture_log_stats.console_level = LOG_LEVEL_INFO;
    fixture_log_stats.buffer_level = LOG_LEVEL_DEBUG;
    fixture_log_stats.next_sequence = 12U;
    fixture_log_stats.overwritten_records = 3U;
    fixture_log_stats.grouped_events = 4U;
    fixture_log_stats.truncated_events = 5U;
    fixture_log_stats.dropped_events = 6U;
    fixture_log_stats.clear_count = 7U;
    fixture_log_copy_count = 2U;
    fixture_log_records[0].sequence = 10U;
    fixture_log_records[0].first_tick = 100U;
    fixture_log_records[0].last_tick = 110U;
    fixture_log_records[0].level = LOG_LEVEL_ERROR;
    fixture_log_records[0].error_code = -4;
    fixture_log_records[0].occurrences = 2U;
    fixture_log_records[0].flags = LOG_RECORD_FLAG_HAS_ERROR_CODE;
    copy_text(fixture_log_records[0].module, sizeof(fixture_log_records[0].module),
              "TEST");
    copy_text(fixture_log_records[0].message,
              sizeof(fixture_log_records[0].message), "falha");
    fixture_log_records[1] = fixture_log_records[0];
    fixture_log_records[1].sequence = 11U;
    fixture_log_records[1].level = LOG_LEVEL_DEBUG;
    fixture_log_records[1].flags = 0U;
    fixture_log_test.passed = 8U;
    fixture_log_test.failed = 0U;
    fixture_log_test.order_and_metadata = 1U;
    fixture_log_test.wrap_and_overwrite = 1U;
    fixture_log_test.repetition_grouping = 1U;
    fixture_log_test.safe_truncation = 1U;
    fixture_log_test.optional_error_code = 1U;
    fixture_log_test.clear_behavior = 1U;
    fixture_log_test.text_serialization = 1U;
    fixture_log_test.level_filtering = 1U;
    kmemset(&fixture_mouse_status, 0, sizeof(fixture_mouse_status));
    fixture_mouse_status.initialized = 1U;
    fixture_mouse_status.x = 12;
    fixture_mouse_status.y = 34;
    fixture_mouse_status.config.speed = 3U;
    fixture_mouse_status.config.primary_button = MOUSE_PRIMARY_LEFT;
}

static int contains_text(const char* text) {
    uint32_t text_length;
    uint32_t output_length;

    if (!text) return 0;
    text_length = kstrlen(text);
    output_length = kstrlen(video_output);
    if (!text_length || text_length > output_length) return 0;
    for (uint32_t offset = 0U;
         offset + text_length <= output_length; offset++) {
        uint32_t index = 0U;

        while (index < text_length &&
               video_output[offset + index] == text[index]) index++;
        if (index == text_length) return 1;
    }
    return 0;
}

static int expect_text(const char* text) {
    if (kstrcmp(video_output, text) == 0) return 0;
    fprintf(stderr, "diagnostics-host: saida inesperada: %s\n", video_output);
    return 1;
}

static int expect_contains(const char* text) {
    if (contains_text(text)) return 0;
    fprintf(stderr, "diagnostics-host: trecho ausente: %s\n", text);
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

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
}

int log_get_stats(log_stats_t* stats) {
    if (!stats) return ERR_NULL;
    if (fixture_log_stats_result != OK) return fixture_log_stats_result;
    *stats = fixture_log_stats;
    return OK;
}

int log_copy_recent(log_record_t* records, uint32_t max_records,
                    uint32_t* count) {
    if (!records || !count) return ERR_NULL;
    if (fixture_log_copy_result != OK) return fixture_log_copy_result;
    if (max_records < fixture_log_copy_count) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < fixture_log_copy_count; index++) {
        records[index] = fixture_log_records[index];
    }
    *count = fixture_log_copy_count;
    return OK;
}

void log_clear_buffer(void) {
    fixture_log_clear_calls++;
}

int log_set_console_level(log_level_t level) {
    fixture_log_console_calls++;
    if (fixture_log_set_console_result == OK) {
        fixture_log_console_level = level;
    }
    return fixture_log_set_console_result;
}

int log_set_buffer_level(log_level_t level) {
    fixture_log_buffer_calls++;
    if (fixture_log_set_buffer_result == OK) {
        fixture_log_buffer_level = level;
    }
    return fixture_log_set_buffer_result;
}

int log_self_test(log_self_test_result_t* result) {
    if (!result) return ERR_NULL;
    *result = fixture_log_test;
    return fixture_log_test_result;
}

const char* log_level_str(log_level_t level) {
    if (level == LOG_LEVEL_ERROR) return "ERROR";
    if (level == LOG_LEVEL_WARN) return "WARN";
    if (level == LOG_LEVEL_INFO) return "INFO";
    if (level == LOG_LEVEL_DEBUG) return "DEBUG";
    return "INVALID";
}

int vfs_getcwd(char* path, uint32_t capacity) {
    if (!path || !capacity) return ERR_NULL;
    if (fixture_getcwd_result != OK) return fixture_getcwd_result;
    if (kstrlen(fixture_cwd) + 1U > capacity) return ERR_OVERFLOW;
    copy_text(path, capacity, fixture_cwd);
    return OK;
}

int vfs_chdir(const char* path) {
    if (!path) return ERR_NULL;
    fixture_chdir_calls++;
    copy_text(fixture_last_path, sizeof(fixture_last_path), path);
    return fixture_chdir_result;
}

int mouse_get_status(mouse_status_t* status) {
    if (!status) return ERR_NULL;
    if (fixture_mouse_status_result != OK) return fixture_mouse_status_result;
    *status = fixture_mouse_status;
    return OK;
}

int mouse_set_speed(uint8_t speed) {
    fixture_speed_calls++;
    if (fixture_mouse_speed_result == OK) fixture_mouse_status.config.speed = speed;
    return fixture_mouse_speed_result;
}

int mouse_set_primary_button(mouse_primary_button_t primary_button) {
    fixture_primary_calls++;
    if (fixture_mouse_primary_result == OK) {
        fixture_mouse_status.config.primary_button = primary_button;
    }
    return fixture_mouse_primary_result;
}

int mouse_set_acceleration(int enabled) {
    fixture_acceleration_calls++;
    if (fixture_mouse_acceleration_result == OK) {
        fixture_mouse_status.config.acceleration_enabled = enabled ? 1U : 0U;
    }
    return fixture_mouse_acceleration_result;
}

static int test_pwd(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_pwd("");
    failures += expect_text("/home/test\n");
    fixture_reset();
    shell_dispatch_cmd_pwd("extra");
    failures += expect_text("Uso: pwd\n");
    fixture_reset();
    fixture_getcwd_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_pwd(0);
    failures += expect_text("Erro: diretorio atual indisponivel.\n");
    return failures;
}

static int test_cd(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_cd("");
    if (fixture_chdir_calls != 1U || kstrcmp(fixture_last_path, "/") != 0) {
        fprintf(stderr, "diagnostics-host: cd padrao nao chamou raiz\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_cd("/var/log");
    if (fixture_chdir_calls != 1U ||
        kstrcmp(fixture_last_path, "/var/log") != 0 || video_output[0]) {
        fprintf(stderr, "diagnostics-host: cd valido inesperado\n");
        failures++;
    }
    fixture_reset();
    fixture_chdir_result = ERR_NOT_FOUND;
    shell_dispatch_cmd_cd("missing");
    failures += expect_text("Erro: cd recusado (codigo 4).\n");
    return failures;
}

static int test_mouse(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_mouse("");
    failures += expect_contains("Mouse PS/2:\n");
    failures += expect_contains("Posicao: 12,34\n");
    fixture_reset();
    fixture_mouse_status.initialized = 0U;
    fixture_mouse_status.config.acceleration_enabled = 1U;
    fixture_mouse_status.config.primary_button = MOUSE_PRIMARY_RIGHT;
    fixture_mouse_status.wheel_supported = 1U;
    shell_dispatch_cmd_mouse("speed 7");
    if (fixture_speed_calls != 1U || fixture_mouse_status.config.speed != 7U) {
        fprintf(stderr, "diagnostics-host: speed valido nao aplicado\n");
        failures++;
    }
    failures += expect_contains("Preferencia do mouse aplicada em RAM.\n");
    fixture_reset();
    shell_dispatch_cmd_mouse("primary right");
    if (fixture_primary_calls != 1U ||
        fixture_mouse_status.config.primary_button != MOUSE_PRIMARY_RIGHT) {
        fprintf(stderr, "diagnostics-host: primary right nao aplicado\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_mouse("primary left");
    if (fixture_primary_calls != 1U ||
        fixture_mouse_status.config.primary_button != MOUSE_PRIMARY_LEFT) {
        fprintf(stderr, "diagnostics-host: primary left nao aplicado\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_mouse("acceleration on");
    if (fixture_acceleration_calls != 1U ||
        !fixture_mouse_status.config.acceleration_enabled) {
        fprintf(stderr, "diagnostics-host: acceleration on nao aplicado\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_mouse("acceleration off");
    if (fixture_acceleration_calls != 1U ||
        fixture_mouse_status.config.acceleration_enabled) {
        fprintf(stderr, "diagnostics-host: acceleration off nao aplicado\n");
        failures++;
    }
    fixture_reset();
    shell_dispatch_cmd_mouse("speed 0");
    failures += expect_contains("preferencia invalida; estado preservado");
    fixture_reset();
    shell_dispatch_cmd_mouse("primary middle");
    failures += expect_contains("preferencia invalida; estado preservado");
    fixture_reset();
    shell_dispatch_cmd_mouse("unknown value");
    failures += expect_contains("preferencia invalida; estado preservado");
    fixture_reset();
    shell_dispatch_cmd_mouse("speed 4 extra");
    failures += expect_text("Uso: mouse | mouse speed <1-10> | mouse primary <left|right> | mouse acceleration <on|off>\n");
    fixture_reset();
    fixture_mouse_speed_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_mouse("speed 4");
    failures += expect_contains("driver de mouse indisponivel");
    fixture_reset();
    fixture_mouse_status_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_mouse(0);
    failures += expect_text("Erro: status do mouse indisponivel.\n");
    return failures;
}

static int test_log(void) {
    int failures = 0;

    fixture_reset();
    shell_dispatch_cmd_log("status");
    failures += expect_contains("Log circular: 2/32 registros\n");
    failures += expect_contains("Niveis do log: console=info buffer=debug\n");
    fixture_reset();
    fixture_log_stats_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_log("");
    failures += expect_text("Erro: estatisticas do log indisponiveis.\n");
    fixture_reset();
    shell_dispatch_cmd_log("clear");
    if (fixture_log_clear_calls != 1U ||
        expect_text("Log circular limpo.\n")) failures++;
    fixture_reset();
    fixture_log_copy_count = 0U;
    shell_dispatch_cmd_log("tail");
    failures += expect_text("Log vazio.\n");
    fixture_reset();
    shell_dispatch_cmd_log("tail 2");
    failures += expect_contains("seq=10 ticks=100..110 [ERROR] [TEST] falha");
    failures += expect_contains("erro=-4");
    failures += expect_contains("seq=11 ticks=100..110 [DEBUG] [TEST] falha");
    fixture_reset();
    fixture_log_copy_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_log("tail 1");
    failures += expect_text("Erro: historico do log indisponivel.\n");
    fixture_reset();
    shell_dispatch_cmd_log("tail 33");
    failures += expect_contains("Uso: log [status|tail [1-16]|clear|level|check]");
    fixture_reset();
    shell_dispatch_cmd_log("level");
    failures += expect_contains("Niveis do log: console=info buffer=debug\n");
    fixture_reset();
    shell_dispatch_cmd_log("level console debug");
    if (fixture_log_console_calls != 1U ||
        fixture_log_console_level != LOG_LEVEL_DEBUG) {
        fprintf(stderr, "diagnostics-host: nivel console nao aplicado\n");
        failures++;
    }
    failures += expect_contains("Nivel console alterado para debug.\n");
    fixture_reset();
    fixture_log_set_buffer_result = ERR_INVALID;
    shell_dispatch_cmd_log("level buffer error");
    failures += expect_contains("Erro: o buffer deve ser tao detalhado quanto o console.\n");
    fixture_reset();
    shell_dispatch_cmd_log("level invalid info");
    failures += expect_contains("Uso: log [status|tail [1-16]|clear|level|check]");
    fixture_reset();
    shell_dispatch_cmd_log("check");
    failures += expect_contains("Resultado: OK (8 aprovados, 0 falhos)\n");
    fixture_reset();
    fixture_log_test_result = ERR_STATE;
    fixture_log_test.failed = 1U;
    shell_dispatch_cmd_log("check");
    failures += expect_contains("Resultado: ERRO (8 aprovados, 1 falhos)\n");
    fixture_reset();
    shell_dispatch_cmd_log("unknown");
    failures += expect_contains("Uso: log [status|tail [1-16]|clear|level|check]");
    return failures;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = test_pwd() + test_cd() + test_mouse() + test_log();
    coverage_active = 0U;
    coverage_emit(result);
    return result ? 1 : 0;
}
