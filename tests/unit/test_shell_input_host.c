#include <stdint.h>
#include <stdio.h>

#include "apps/shell_input.h"
#include "apps/shell.h"
#include "core/keyboard.h"
#include "core/log.h"
#include "core/string.h"
#include "core/video.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_OUTPUT_CAPACITY 2048U
#define SHELL_INPUT_SCANCODE_A 0x1EU
#define SHELL_INPUT_SCANCODE_B 0x30U
#define SHELL_INPUT_SCANCODE_C 0x2EU
#define SHELL_INPUT_SCANCODE_CTRL 0x1DU
#define SHELL_INPUT_SCANCODE_CTRL_RELEASE 0x9DU
#define SHELL_INPUT_SCANCODE_ENTER 0x1CU
#define SHELL_INPUT_SCANCODE_EXTENDED 0xE0U
#define SHELL_INPUT_SCANCODE_HOME 0x47U
#define SHELL_INPUT_SCANCODE_PAGE_UP 0x49U
#define SHELL_INPUT_SCANCODE_END 0x4FU
#define SHELL_INPUT_SCANCODE_UP 0x48U
#define SHELL_INPUT_SCANCODE_DOWN 0x50U
#define SHELL_INPUT_SCANCODE_BACKSPACE 0x0EU
#define SHELL_INPUT_SCANCODE_LEFT_SHIFT 0x2AU
#define SHELL_INPUT_SCANCODE_LEFT_SHIFT_RELEASE 0xAAU

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static char video_output[HOST_OUTPUT_CAPACITY];
static uint32_t video_output_length;
static uint8_t terminal_active;
static uint8_t terminal_hosted;
static uint8_t terminal_scrolled;
static uint32_t terminal_begin_calls;
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
    printf("ZCOV_BEGIN|case=host:shell:input|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:input|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:input|value=0x%08X\n",
           (uint32_t)result);
}

static void output_reset(void) {
    video_output_length = 0U;
    video_output[0] = '\0';
}

static void output_append_char(char value) {
    if (video_output_length + 1U >= HOST_OUTPUT_CAPACITY) return;
    video_output[video_output_length++] = value;
    video_output[video_output_length] = '\0';
}

static void output_append(const char* text) {
    if (!text) return;
    while (*text) output_append_char(*text++);
}

static void reset_terminal(void) {
    terminal_active = 0U;
    terminal_hosted = 0U;
    terminal_scrolled = 0U;
    terminal_begin_calls = 0U;
    output_reset();
}

void video_print(const char* text, uint8_t color) {
    (void)color;
    output_append(text);
}

void video_put_char(char value, uint8_t color) {
    (void)color;
    output_append_char(value);
}

void video_backspace(void) {
    output_append_char('\b');
}

int video_terminal_is_active(void) {
    return terminal_active;
}

int video_terminal_is_hosted(void) {
    return terminal_hosted;
}

void video_terminal_begin(void) {
    terminal_active = 1U;
    terminal_begin_calls++;
}

int video_terminal_scroll(int lines) {
    terminal_scrolled = lines > 0 ? 1U : 0U;
    return 1;
}

void video_terminal_scroll_home(void) {
    terminal_scrolled = 1U;
}

void video_terminal_scroll_end(void) {
    terminal_scrolled = 0U;
}

int video_terminal_is_scrolled(void) {
    return terminal_scrolled;
}

char keyboard_scancode_to_ascii_shifted(uint8_t scancode, uint8_t shifted) {
    if (scancode == SHELL_INPUT_SCANCODE_A) return shifted ? 'A' : 'a';
    if (scancode == SHELL_INPUT_SCANCODE_B) return shifted ? 'B' : 'b';
    if (scancode == SHELL_INPUT_SCANCODE_C) return shifted ? 'C' : 'c';
    return 0;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
    log_calls++;
}

static int check_prompt_and_resume(void) {
    reset_terminal();
    terminal_hosted = 1U;
    shell_input_init();
    shell_input_print_prompt(0U);
    shell_input_print_prompt(0U);
    if (kstrcmp(video_output, SHELL_PROMPT) != 0) return 1;
    if (terminal_begin_calls != 2U) return 2;
    terminal_active = 0U;
    terminal_hosted = 0U;
    shell_input_resume_terminal(1U);
    if (terminal_begin_calls != 3U) return 3;
    terminal_active = 1U;
    shell_input_resume_terminal(1U);
    if (terminal_begin_calls != 3U) return 4;
    return 0;
}

static int check_history(void) {
    shell_input_reset();
    if (shell_input_handle_key(SHELL_INPUT_SCANCODE_A, 1U, 0U) !=
        SHELL_INPUT_EVENT_NONE) return 1;
    if (shell_input_handle_key(SHELL_INPUT_SCANCODE_ENTER, 1U, 0U) !=
        SHELL_INPUT_EVENT_COMMAND_READY) return 2;
    shell_input_reset();
    shell_input_handle_key(SHELL_INPUT_SCANCODE_B, 1U, 0U);
    if (shell_input_handle_key(SHELL_INPUT_SCANCODE_ENTER, 1U, 0U) !=
        SHELL_INPUT_EVENT_COMMAND_READY) return 3;
    shell_input_reset();
    shell_input_handle_key(SHELL_INPUT_SCANCODE_EXTENDED, 1U, 0U);
    shell_input_handle_key(SHELL_INPUT_SCANCODE_UP, 1U, 0U);
    if (kstrcmp(shell_input_get_buffer(), "b") != 0) return 4;
    shell_input_handle_key(SHELL_INPUT_SCANCODE_EXTENDED, 1U, 0U);
    shell_input_handle_key(SHELL_INPUT_SCANCODE_UP, 1U, 0U);
    if (kstrcmp(shell_input_get_buffer(), "a") != 0) return 5;
    shell_input_handle_key(SHELL_INPUT_SCANCODE_EXTENDED, 1U, 0U);
    shell_input_handle_key(SHELL_INPUT_SCANCODE_DOWN, 1U, 0U);
    if (kstrcmp(shell_input_get_buffer(), "b") != 0) return 6;
    shell_input_handle_key(SHELL_INPUT_SCANCODE_EXTENDED, 1U, 0U);
    shell_input_handle_key(SHELL_INPUT_SCANCODE_DOWN, 1U, 0U);
    if (kstrcmp(shell_input_get_buffer(), "") != 0) return 7;
    return 0;
}

static int check_scrolling_and_editing(void) {
    shell_input_reset();
    terminal_scrolled = 0U;
    shell_input_handle_key(SHELL_INPUT_SCANCODE_EXTENDED, 1U, 0U);
    shell_input_handle_key(SHELL_INPUT_SCANCODE_PAGE_UP, 1U, 0U);
    if (!terminal_scrolled) return 1;
    shell_input_handle_key(SHELL_INPUT_SCANCODE_EXTENDED, 1U, 0U);
    shell_input_handle_key(SHELL_INPUT_SCANCODE_HOME, 1U, 0U);
    if (!terminal_scrolled) return 2;
    shell_input_handle_key(SHELL_INPUT_SCANCODE_EXTENDED, 1U, 0U);
    shell_input_handle_key(SHELL_INPUT_SCANCODE_END, 1U, 0U);
    if (terminal_scrolled) return 3;
    shell_input_handle_key(SHELL_INPUT_SCANCODE_A, 1U, 0U);
    shell_input_handle_key(SHELL_INPUT_SCANCODE_B, 1U, 0U);
    shell_input_handle_key(SHELL_INPUT_SCANCODE_BACKSPACE, 1U, 0U);
    if (kstrcmp(shell_input_get_buffer(), "a") != 0) return 4;
    return 0;
}

static int check_cancel_block_and_limits(void) {
    shell_input_reset();
    shell_input_handle_key(SHELL_INPUT_SCANCODE_A, 1U, 1U);
    if (kstrcmp(shell_input_get_buffer(), "") != 0) return 1;
    shell_input_handle_key(SHELL_INPUT_SCANCODE_LEFT_SHIFT, 1U, 0U);
    shell_input_handle_key(SHELL_INPUT_SCANCODE_A, 1U, 0U);
    shell_input_handle_key(SHELL_INPUT_SCANCODE_LEFT_SHIFT_RELEASE, 1U, 0U);
    if (kstrcmp(shell_input_get_buffer(), "A") != 0) return 2;
    shell_input_reset_modifiers();
    shell_input_handle_key(SHELL_INPUT_SCANCODE_CTRL, 1U, 0U);
    if (shell_input_handle_key(SHELL_INPUT_SCANCODE_C, 1U, 0U) !=
        SHELL_INPUT_EVENT_CANCELLED) return 3;
    if (kstrcmp(shell_input_get_buffer(), "") != 0) return 4;
    shell_input_handle_key(SHELL_INPUT_SCANCODE_CTRL_RELEASE, 1U, 0U);
    shell_input_handle_key(SHELL_INPUT_SCANCODE_EXTENDED, 1U, 0U);
    shell_input_cancel_extended();
    shell_input_handle_key(SHELL_INPUT_SCANCODE_A, 1U, 0U);
    for (uint32_t index = 1U; index < SHELL_BUFFER_SIZE; index++) {
        shell_input_handle_key(SHELL_INPUT_SCANCODE_A, 1U, 0U);
    }
    if (kstrlen(shell_input_get_buffer()) != SHELL_BUFFER_SIZE - 1U) return 5;
    if (!log_calls) return 6;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_prompt_and_resume();
    if (result == 0) result = check_history();
    if (result == 0) result = check_scrolling_and_editing();
    if (result == 0) result = check_cancel_block_and_limits();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
