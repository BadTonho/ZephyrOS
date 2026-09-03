#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>

#include "core/panic.h"
#include "core/test_protocol.h"
#include "core/video.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_OUTPUT_CAPACITY 4096U
#define HOST_REASON_CAPACITY 128U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static char video_output[HOST_OUTPUT_CAPACITY];
static uint32_t video_output_length;
static char protocol_reason[HOST_REASON_CAPACITY];
static uint32_t protocol_calls;
static uint32_t video_clear_calls;
static uint32_t video_color_calls;
static uint32_t video_put_char_calls;
static uint32_t video_flush_calls;
static jmp_buf panic_jump;
static uint8_t panic_jump_active;

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
    printf("ZCOV_BEGIN|case=host:kernel:panic|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:kernel:panic|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:kernel:panic|value=0x%08X\n",
           (uint32_t)result);
}

static void output_append(const char* text) {
    if (!text) return;
    while (*text && video_output_length + 1U < HOST_OUTPUT_CAPACITY) {
        video_output[video_output_length++] = *text++;
    }
    video_output[video_output_length] = '\0';
}

static void output_reset(void) {
    video_output_length = 0U;
    video_output[0] = '\0';
}

static int output_contains(const char* expected) {
    uint32_t expected_length = 0U;

    while (expected[expected_length]) expected_length++;
    if (!expected_length || expected_length > video_output_length) return 0;
    for (uint32_t start = 0U;
         start + expected_length <= video_output_length; start++) {
        uint32_t index;

        for (index = 0U; index < expected_length; index++) {
            if (video_output[start + index] != expected[index]) break;
        }
        if (index == expected_length) return 1;
    }
    return 0;
}

static void reason_set(const char* reason) {
    uint32_t index = 0U;

    while (reason && reason[index] && index + 1U < HOST_REASON_CAPACITY) {
        protocol_reason[index] = reason[index];
        index++;
    }
    protocol_reason[index] = '\0';
}

static int reason_equals(const char* expected) {
    uint32_t index = 0U;

    while (expected[index] && protocol_reason[index] == expected[index]) index++;
    return expected[index] == '\0' && protocol_reason[index] == '\0';
}

void test_protocol_panic(const char* reason) {
    protocol_calls++;
    reason_set(reason);
}

void video_clear(void) {
    video_clear_calls++;
}

void video_set_color(uint8_t fg, uint8_t bg) {
    (void)fg;
    (void)bg;
    video_color_calls++;
}

void video_print(const char* text, uint8_t color) {
    (void)color;
    output_append(text);
}

void video_put_char(char character, uint8_t color) {
    (void)color;
    video_put_char_calls++;
    if (video_output_length + 1U < HOST_OUTPUT_CAPACITY) {
        video_output[video_output_length++] = character;
        video_output[video_output_length] = '\0';
    }
}

void video_flush_updates(void) {
    video_flush_calls++;
}

void panic_halt(void) {
    if (panic_jump_active) longjmp(panic_jump, 1);
}

static int panic_call(const char* message) {
    panic_jump_active = 1U;
    if (setjmp(panic_jump) == 0) {
        panic(message);
        panic_jump_active = 0U;
        return 1;
    }
    panic_jump_active = 0U;
    return 0;
}

static int panic_memory_call(const char* message, uint32_t mmap_entries,
                             uint32_t total_memory, uint32_t free_memory,
                             uint32_t free_pages) {
    panic_jump_active = 1U;
    if (setjmp(panic_jump) == 0) {
        panic_memory(message, mmap_entries, total_memory, free_memory,
                     free_pages);
        panic_jump_active = 0U;
        return 1;
    }
    panic_jump_active = 0U;
    return 0;
}

static int check_panic_paths(void) {
    output_reset();
    protocol_calls = 0U;
    video_clear_calls = 0U;
    video_color_calls = 0U;
    video_put_char_calls = 0U;
    video_flush_calls = 0U;
    if (panic_call(0) != 0) return 1;
    if (protocol_calls != 1U || reason_equals("ERR_STATE") == 0 ||
        output_contains("KERNEL PANIC") == 0 ||
        output_contains("Mensagem de panic ausente") == 0) {
        return 2;
    }
    if (video_clear_calls != 1U || video_flush_calls != 1U ||
        video_color_calls == 0U) return 3;
    output_reset();
    if (panic_call("disk failure") != 0) return 4;
    if (protocol_calls != 2U || reason_equals("disk failure") == 0 ||
        output_contains("disk failure") == 0) return 5;
    return 0;
}

static int check_memory_panic_paths(void) {
    output_reset();
    if (panic_memory_call(0U, UINT32_MAX, UINT32_MAX, 0U,
                          UINT32_MAX) != 0) return 1;
    if (protocol_calls != 3U || reason_equals("ERR_MEM") == 0 ||
        output_contains("Detalhe de memoria ausente") == 0 ||
        output_contains("4294967295") == 0) return 2;
    if (video_put_char_calls == 0U) return 3;
    output_reset();
    if (panic_memory_call("oom", 1U, 2U, 3U, 4U) != 0) return 4;
    if (protocol_calls != 4U || reason_equals("oom") == 0 ||
        output_contains("oom") == 0 ||
        output_contains("Entradas E820") == 0) return 5;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_panic_paths();
    if (result == 0) result = check_memory_panic_paths();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
