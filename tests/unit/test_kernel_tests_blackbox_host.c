#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/log.h"
#include "kernel_tests.h"
#include "video_test.h"

#define HOST_COVERAGE_CAPACITY 96U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_TEXT_CAPACITY 256U
#define HOST_CASE_COUNT 9U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static char terminal_text[HOST_TEXT_CAPACITY];
static const char* expected_marker;
static uint32_t snapshot_count;
static uint32_t progress_count;
static uint32_t yield_count;
static uint32_t report_count;
static uint32_t fake_ticks;

const kernel_tests_runtime_t* kernel_tests_active_runtime;

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

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

uint32_t timer_get_ticks(void) {
    return fake_ticks;
}

void process_yield(void) {
    yield_count++;
}

int video_test_copy_terminal(char* output, uint32_t capacity,
                             video_test_terminal_info_t* info) {
    uint32_t length;

    if (!output || !info || capacity == 0U) return ERR_INVALID;
    snapshot_count++;
    if (snapshot_count >= 3U && expected_marker) {
        snprintf(terminal_text, sizeof(terminal_text),
                 "prompt %s", expected_marker);
    }
    length = (uint32_t)strlen(terminal_text);
    if (length >= capacity) return ERR_OVERFLOW;
    memcpy(output, terminal_text, length + 1U);
    info->generation = snapshot_count;
    info->line_count = 1U;
    info->cursor_x = length;
    info->active = 1U;
    info->hosted = 1U;
    info->truncated = 0U;
    return OK;
}

static int fake_progress(void* context) {
    (void)context;
    progress_count++;
    return OK;
}

static void fake_report(void* context, const char* phase, int result) {
    (void)context;
    (void)phase;
    (void)result;
    report_count++;
}

int kernel_tests_progress(const kernel_tests_runtime_t* runtime) {
    if (!runtime || !runtime->progress) return ERR_NULL;
    return runtime->progress(runtime->context);
}

int kernel_tests_report_phase(const kernel_tests_runtime_t* runtime,
                              const char* phase, int result) {
    if (!runtime || !runtime->report_phase || !phase) return ERR_NULL;
    runtime->report_phase(runtime->context, phase, result);
    return result;
}

static int run_case(const kernel_tests_runtime_t* runtime,
                    const char* case_id, const char* marker) {
    int result;

    expected_marker = marker;
    snapshot_count = 0U;
    progress_count = 0U;
    yield_count = 0U;
    report_count = 0U;
    fake_ticks = 1U;
    strcpy(terminal_text, "prompt");
    result = kernel_tests_run_tst5_blackbox(runtime, case_id,
                                             (uint32_t)strlen(case_id));
    if (result != OK || snapshot_count != 3U || progress_count != 1U ||
        yield_count != 1U || report_count != 4U) return 0;
    return 1;
}

static int check_valid_cases(void) {
    static const char* cases[HOST_CASE_COUNT][2] = {
        {"qemu:tst5:shell", "tst5-shell"},
        {"qemu:tst5:input", "tst5-input"},
        {"qemu:tst5:apps", "tst5-apps"},
        {"qemu:tst5:processes", "tst5-processes"},
        {"qemu:tst5:storage", "tst5-storage"},
        {"qemu:tst5:network", "tst5-network"},
        {"qemu:tst5:update-recovery", "tst5-update"},
        {"qemu:tst5:reboot", "tst5-reboot"},
        {"qemu:tst5:poweroff", "tst5-poweroff"}
    };
    kernel_tests_runtime_t runtime;

    runtime.progress = fake_progress;
    runtime.context = 0;
    runtime.report_phase = fake_report;
    for (uint32_t index = 0U; index < HOST_CASE_COUNT; index++) {
        if (!run_case(&runtime, cases[index][0], cases[index][1])) {
            return 10 + (int)index;
        }
    }
    return 0;
}

static int check_invalid_case(void) {
    kernel_tests_runtime_t runtime;

    runtime.progress = fake_progress;
    runtime.context = 0;
    runtime.report_phase = fake_report;
    expected_marker = 0;
    snapshot_count = 0U;
    if (kernel_tests_run_tst5_blackbox(&runtime, "qemu:tst5:invalid",
                                       16U) != ERR_NOT_FOUND ||
        snapshot_count != 0U || report_count == 0U) return 30;
    return 0;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:tst5:blackbox|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:tst5:blackbox|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:tst5:blackbox|value=0x%08X\n",
           (uint32_t)result);
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_valid_cases();
    if (!result) result = check_invalid_case();
    if (!result && kernel_tests_progress(0) != ERR_NULL) result = 40;
    if (!result && kernel_tests_report_phase(0, "phase", OK) != ERR_NULL) {
        result = 41;
    }
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
