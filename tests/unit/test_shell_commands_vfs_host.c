#include <stdint.h>
#include <stdio.h>

#include "apps/shell_command_utils.h"
#include "apps/shell_pipeline.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/video.h"

void shell_dispatch_cmd_grep(const char* arguments);
void shell_dispatch_cmd_pipetest(const char* arguments);

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_OUTPUT_CAPACITY 4096U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static char video_output[HOST_OUTPUT_CAPACITY];
static uint32_t video_output_length;
static char pipeline_output[HOST_OUTPUT_CAPACITY];
static uint32_t pipeline_output_length;
static const uint8_t* pipeline_input;
static uint32_t pipeline_input_length;
static uint32_t pipeline_input_offset;
static int pipeline_read_status;
static uint32_t pipeline_write_calls;
static uint32_t pipeline_write_fail_call;
static int pipeline_self_test_status;
static uint32_t log_calls;

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
    printf("ZCOV_BEGIN|case=host:shell:commands-vfs|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:commands-vfs|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:commands-vfs|value=0x%08X\n",
           (uint32_t)result);
}

static void output_reset(void) {
    video_output_length = 0U;
    video_output[0] = '\0';
    pipeline_output_length = 0U;
    pipeline_output[0] = '\0';
}

static void output_append(char* output, uint32_t* length, const char* text) {
    if (!text) return;
    while (*text && *length + 1U < HOST_OUTPUT_CAPACITY) {
        output[(*length)++] = *text++;
    }
    output[*length] = '\0';
}

static void pipeline_set_input(const char* text) {
    pipeline_input = (const uint8_t*)text;
    pipeline_input_length = kstrlen(text);
    pipeline_input_offset = 0U;
    pipeline_read_status = OK;
}

void video_print(const char* text, uint8_t color) {
    (void)color;
    output_append(video_output, &video_output_length, text);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
    log_calls++;
}

int shell_pipeline_read(void* buffer, uint32_t size, uint32_t* bytes_read) {
    uint32_t remaining;
    uint32_t count;

    if (!bytes_read || (size > 0U && !buffer)) return ERR_NULL;
    *bytes_read = 0U;
    if (pipeline_read_status != OK) return pipeline_read_status;
    if (pipeline_input_offset >= pipeline_input_length) return OK;
    remaining = pipeline_input_length - pipeline_input_offset;
    count = remaining < size ? remaining : size;
    kmemcpy(buffer, pipeline_input + pipeline_input_offset, count);
    pipeline_input_offset += count;
    *bytes_read = count;
    return OK;
}

int shell_pipeline_write(const char* text, uint8_t color) {
    (void)color;
    pipeline_write_calls++;
    if (pipeline_write_fail_call &&
        pipeline_write_calls == pipeline_write_fail_call) {
        return ERR_DISK;
    }
    if (!text) return ERR_NULL;
    output_append(pipeline_output, &pipeline_output_length, text);
    return OK;
}

int shell_pipeline_self_test(void) {
    return pipeline_self_test_status;
}

static int check_grep_matches_fragmented_input(void) {
    static const char input[] =
        "ignored-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
        "first BeTa\nmissing\nlast beta";

    output_reset();
    pipeline_set_input(input);
    pipeline_write_calls = 0U;
    pipeline_write_fail_call = 0U;
    shell_dispatch_cmd_grep("beta");
    if (kstrcmp(pipeline_output, "first BeTa\nlast beta\n") != 0) return 1;
    if (video_output_length != 0U) return 2;
    if (pipeline_write_calls != 4U) return 3;
    return 0;
}

static int check_grep_rejections(void) {
    char long_line[257];

    output_reset();
    shell_dispatch_cmd_grep(NULL);
    if (kstrcmp(video_output, "Uso: grep <texto>\n") != 0) return 1;
    output_reset();
    shell_dispatch_cmd_grep("beta extra");
    if (kstrcmp(video_output, "Uso: grep <texto>\n") != 0) return 2;
    output_reset();
    shell_dispatch_cmd_grep(
        "12345678901234567890123456789012");
    if (kstrcmp(video_output, "Uso: grep <texto>\n") != 0) return 3;
    output_reset();
    pipeline_set_input("");
    pipeline_read_status = ERR_UNAVAILABLE;
    shell_dispatch_cmd_grep("beta");
    if (kstrcmp(video_output,
               "Erro: grep sem entrada de pipeline.\n") != 0) return 4;
    output_reset();
    pipeline_set_input("beta\n");
    pipeline_write_calls = 0U;
    pipeline_write_fail_call = 1U;
    shell_dispatch_cmd_grep("beta");
    if (kstrcmp(video_output,
               "Erro: grep nao conseguiu escrever a saida.\n") != 0) return 5;
    for (uint32_t index = 0U; index < sizeof(long_line) - 1U; index++) {
        long_line[index] = 'a';
    }
    long_line[sizeof(long_line) - 1U] = '\0';
    output_reset();
    pipeline_set_input(long_line);
    pipeline_write_fail_call = 0U;
    shell_dispatch_cmd_grep("a");
    if (kstrcmp(video_output,
               "Erro: linha excede o limite do grep.\n") != 0) return 6;
    return 0;
}

static int check_pipetest_results(void) {
    output_reset();
    shell_dispatch_cmd_pipetest("unexpected");
    if (kstrcmp(video_output, "Uso: pipetest\n") != 0) return 1;
    output_reset();
    pipeline_self_test_status = OK;
    shell_dispatch_cmd_pipetest("");
    if (kstrcmp(video_output, "PipeTest: OK\n") != 0) return 2;
    output_reset();
    pipeline_self_test_status = ERR_STATE;
    shell_dispatch_cmd_pipetest("\t");
    if (kstrcmp(video_output, "PipeTest: ERRO codigo=7\n") != 0) return 3;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    log_calls = 0U;
    result = check_grep_matches_fragmented_input();
    if (result == 0) result = check_grep_rejections();
    if (result == 0) result = check_pipetest_results();
    coverage_active = 0U;
    if (result == 0 && log_calls == 0U) result = 99;
    coverage_emit(result);
    return result;
}
