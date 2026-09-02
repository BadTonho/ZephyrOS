#include <stdint.h>
#include <stdio.h>

#include "apps/shell_command_utils.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/video.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_OUTPUT_CAPACITY 256U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static char video_output[HOST_OUTPUT_CAPACITY];
static uint32_t video_output_length;
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
    printf("ZCOV_BEGIN|case=host:shell:command-utils|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:command-utils|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:command-utils|value=0x%08X\n",
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

static int check_text_and_numbers(void) {
    char text[] = "Zephyr Os 123!";

    shell_command_uppercase(text);
    if (kstrcmp(text, "ZEPHYR OS 123!") != 0) return 1;
    if (shell_command_parse_number("12345rest") != 12345U) return 2;
    if (shell_command_parse_number("") != 0U) return 3;
    if (shell_command_parse_number("4294967295") != UINT32_MAX) return 4;
    if (shell_command_parse_number("4294967296") != 0U) return 5;
    return 0;
}

static int check_matching(void) {
    const char* remainder;

    if (!shell_command_args_equal("status \t", "status")) return 1;
    if (shell_command_args_equal("status extra", "status")) return 2;
    if (shell_command_args_equal(NULL, "status")) return 3;
    if (shell_command_args_equal("status", NULL)) return 4;
    remainder = shell_command_match_subcommand("add\titem", "add");
    if (!remainder || kstrcmp(remainder, "item") != 0) return 5;
    remainder = shell_command_match_subcommand("status", "status");
    if (!remainder || *remainder != '\0') return 6;
    if (shell_command_match_subcommand("adder", "add")) return 7;
    if (shell_command_match_subcommand(NULL, "add")) return 8;
    if (shell_command_match_subcommand("add", NULL)) return 9;
    return 0;
}

static int check_tokens(void) {
    const char* cursor = " \talpha beta";
    const char* empty = "";
    char first[8];
    char second[8];
    char small[4];

    if (shell_command_read_token(&cursor, first, sizeof(first)) != OK) return 1;
    if (kstrcmp(first, "alpha") != 0) return 2;
    if (shell_command_read_token(&cursor, second, sizeof(second)) != OK) return 3;
    if (kstrcmp(second, "beta") != 0) return 4;
    if (shell_command_read_token(&cursor, second, sizeof(second)) != ERR_INVALID) return 5;
    if (shell_command_read_token(&empty, second, sizeof(second)) != ERR_INVALID) return 6;
    if (shell_command_read_token(NULL, second, sizeof(second)) != ERR_NULL) return 7;
    cursor = "abcd";
    if (shell_command_read_token(&cursor, small, sizeof(small)) != ERR_OVERFLOW) return 8;
    if (shell_command_read_token(&cursor, second, 0U) != ERR_NULL) return 9;
    return 0;
}

static int check_single_argument(void) {
    char output[8];
    char small[4];

    if (shell_command_read_single_arg(" \tvalue\t", output,
                                      sizeof(output)) != OK) return 1;
    if (kstrcmp(output, "value") != 0) return 2;
    if (shell_command_read_single_arg("value extra", output,
                                      sizeof(output)) != ERR_INVALID) return 3;
    if (shell_command_read_single_arg("", output, sizeof(output)) != ERR_INVALID) return 4;
    if (shell_command_read_single_arg(NULL, output, sizeof(output)) != ERR_NULL) return 5;
    if (shell_command_read_single_arg("value", NULL, sizeof(output)) != ERR_NULL) return 6;
    if (shell_command_read_single_arg("value", output, 0U) != ERR_NULL) return 7;
    if (shell_command_read_single_arg("abcd", small, sizeof(small)) != ERR_OVERFLOW) return 8;
    return 0;
}

static int check_multiple_arguments(void) {
    char first[8];
    char second[8];
    char third[8];
    char fourth[8];

    if (shell_command_read_two_args("one\ttwo", first, sizeof(first),
                                    second, sizeof(second)) != OK) return 1;
    if (kstrcmp(first, "one") != 0 || kstrcmp(second, "two") != 0) return 2;
    if (shell_command_read_two_args("one", first, sizeof(first),
                                    second, sizeof(second)) != ERR_INVALID) return 3;
    if (shell_command_read_two_args("one two three", first, sizeof(first),
                                    second, sizeof(second)) != ERR_INVALID) return 4;
    if (shell_command_read_two_args(NULL, first, sizeof(first),
                                    second, sizeof(second)) != ERR_NULL) return 5;
    if (shell_command_read_two_args("one two", first, 0U,
                                    second, sizeof(second)) != ERR_NULL) return 6;
    if (shell_command_read_four_args("one two three four", first, sizeof(first),
                                     second, sizeof(second), third, sizeof(third),
                                     fourth, sizeof(fourth)) != OK) return 7;
    if (kstrcmp(first, "one") != 0 || kstrcmp(second, "two") != 0 ||
        kstrcmp(third, "three") != 0 || kstrcmp(fourth, "four") != 0) return 8;
    if (shell_command_read_four_args("one two three", first, sizeof(first),
                                     second, sizeof(second), third, sizeof(third),
                                     fourth, sizeof(fourth)) != ERR_INVALID) return 9;
    if (shell_command_read_four_args("one two three four five", first, sizeof(first),
                                     second, sizeof(second), third, sizeof(third),
                                     fourth, sizeof(fourth)) != ERR_INVALID) return 10;
    if (shell_command_read_four_args("one two three four", NULL, sizeof(first),
                                     second, sizeof(second), third, sizeof(third),
                                     fourth, sizeof(fourth)) != ERR_NULL) return 11;
    return 0;
}

static int check_formatting(void) {
    output_reset();
    shell_command_print_num(0U);
    if (kstrcmp(video_output, "0") != 0) return 1;
    output_reset();
    shell_command_print_num(UINT32_MAX);
    if (kstrcmp(video_output, "4294967295") != 0) return 2;
    output_reset();
    shell_command_print_hex(0xBEEFU, 4U);
    if (kstrcmp(video_output, "BEEF") != 0) return 3;
    output_reset();
    shell_command_print_hex(0xAU, 8U);
    if (kstrcmp(video_output, "0000000A") != 0) return 4;
    output_reset();
    shell_command_print_hex(0xAU, 0U);
    if (kstrcmp(video_output, "") != 0) return 5;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_text_and_numbers();
    if (result == 0) result = check_matching();
    if (result == 0) result = check_tokens();
    if (result == 0) result = check_single_argument();
    if (result == 0) result = check_multiple_arguments();
    if (result == 0) result = check_formatting();
    coverage_active = 0U;
    if (result == 0 && log_calls == 0U) result = 99;
    coverage_emit(result);
    return result;
}
