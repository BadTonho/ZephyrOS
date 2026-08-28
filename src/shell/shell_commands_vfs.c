#include "apps/shell_command_utils.h"
#include "apps/shell_pipeline.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/video.h"

#define SHELL_GREP_PATTERN_SIZE 32U
#define SHELL_GREP_CHUNK_SIZE 128U
#define SHELL_GREP_LINE_SIZE 256U

static int shell_grep_contains(const char* line, uint32_t line_length,
                               const char* pattern, uint32_t pattern_length) {
    if (pattern_length > line_length) return 0;
    for (uint32_t start = 0U; start + pattern_length <= line_length; start++) {
        uint32_t index;

        for (index = 0U; index < pattern_length; index++) {
            char line_char = line[start + index];
            char pattern_char = pattern[index];

            if (line_char >= 'A' && line_char <= 'Z') {
                line_char = (char)(line_char - 'A' + 'a');
            }
            if (pattern_char >= 'A' && pattern_char <= 'Z') {
                pattern_char = (char)(pattern_char - 'A' + 'a');
            }
            if (line_char != pattern_char) break;
        }
        if (index == pattern_length) return 1;
    }
    return 0;
}

static int shell_grep_emit(const char* line, uint32_t length,
                           const char* pattern, uint32_t pattern_length) {
    int result;

    if (!shell_grep_contains(line, length, pattern, pattern_length)) return OK;
    result = shell_pipeline_write(line, 0x07);
    if (result != OK) return result;
    return shell_pipeline_write("\n", 0x07);
}

static void cmd_grep(const char* arguments) {
    char pattern[SHELL_GREP_PATTERN_SIZE];
    char line[SHELL_GREP_LINE_SIZE];
    uint8_t input[SHELL_GREP_CHUNK_SIZE];
    uint32_t pattern_length;
    uint32_t line_length = 0U;
    uint32_t bytes_read;
    int result;

    result = shell_command_read_single_arg(arguments, pattern,
                                           sizeof(pattern));
    if (result != OK) {
        video_print("Uso: grep <texto>\n", 0x0C);
        return;
    }
    pattern_length = kstrlen(pattern);
    while (1) {
        result = shell_pipeline_read(input, sizeof(input), &bytes_read);
        if (result != OK) {
            video_print("Erro: grep sem entrada de pipeline.\n", 0x0C);
            return;
        }
        if (!bytes_read) break;
        for (uint32_t index = 0U; index < bytes_read; index++) {
            if (input[index] == '\n') {
                line[line_length] = '\0';
                result = shell_grep_emit(line, line_length, pattern,
                                         pattern_length);
                if (result != OK) {
                    video_print("Erro: grep nao conseguiu escrever a saida.\n",
                                0x0C);
                    return;
                }
                line_length = 0U;
            } else {
                if (line_length + 1U >= sizeof(line)) {
                    LOG_WARN("SHELL", "Linha excedeu o limite do grep");
                    video_print("Erro: linha excede o limite do grep.\n", 0x0C);
                    return;
                }
                line[line_length++] = (char)input[index];
            }
        }
    }
    if (line_length) {
        line[line_length] = '\0';
        result = shell_grep_emit(line, line_length, pattern, pattern_length);
        if (result != OK) {
            video_print("Erro: grep nao conseguiu escrever a saida.\n", 0x0C);
        }
    }
}

static void cmd_pipetest(const char* arguments) {
    int result;

    if (!shell_command_args_equal(arguments, "")) {
        video_print("Uso: pipetest\n", 0x0C);
        return;
    }
    result = shell_pipeline_self_test();
    video_print("PipeTest: ", result == OK ? 0x0A : 0x0C);
    video_print(result == OK ? "OK" : "ERRO", result == OK ? 0x0A : 0x0C);
    if (result != OK) {
        video_print(" codigo=", 0x0C);
        shell_command_print_num((uint32_t)result);
    }
    video_print("\n", result == OK ? 0x0A : 0x0C);
}

void shell_dispatch_cmd_grep(const char* arguments) {
    cmd_grep(arguments);
}

void shell_dispatch_cmd_pipetest(const char* arguments) {
    cmd_pipetest(arguments);
}
