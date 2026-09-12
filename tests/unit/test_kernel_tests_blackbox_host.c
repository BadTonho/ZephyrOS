#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "apps/shell.h"
#include "core/log.h"
#include "kernel_tests.h"
#include "video_test.h"

#define HOST_COVERAGE_CAPACITY 96U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_TEXT_CAPACITY 768U
#define HOST_CASE_COUNT 14U

typedef enum {
    HOST_TERMINAL_NORMAL,
    HOST_TERMINAL_KRN6,
    HOST_TERMINAL_SEC6,
    HOST_TERMINAL_HW6,
    HOST_TERMINAL_INCOMPLETE,
    HOST_TERMINAL_ABSENT
} host_terminal_mode_t;

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
static host_terminal_mode_t terminal_mode;

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
    fake_ticks++;
}

int video_test_copy_terminal(char* output, uint32_t capacity,
                             video_test_terminal_info_t* info) {
    uint32_t length;

    if (!output || !info || capacity == 0U) return ERR_INVALID;
    snapshot_count++;
    if (snapshot_count >= 3U && expected_marker) {
        if (terminal_mode == HOST_TERMINAL_KRN6) {
            snprintf(terminal_text, sizeof(terminal_text),
                     "Resumo do health:\n"
                     "Health check: OK\n"
                     "SchedCheck:\n"
                     "  contabilidade_idle OK\n"
                     "MemCheck:\n"
                     "  memoria_detalhada OK\n"
                     "Autoteste de Bottom-Half (fila privada):\n"
                     "Resultado: OK\n"
                     "Autoteste de esperas (canal privado):\n"
                     "Resultado: OK\n"
                     "Autoteste da workqueue (fixture privada):\n"
                     "Resultado: OK\n"
                     "Metricas K1 (desde reset):\n"
                     "ZOMBIE=0\n"
                     "RegCheck: OK\n"
                     "Processos ativos:\n"
                     "Total: 4 processos\n"
                     SHELL_PROMPT "%s\n"
                     SHELL_PROMPT, expected_marker);
        } else if (terminal_mode == HOST_TERMINAL_SEC6) {
            snprintf(terminal_text, sizeof(terminal_text),
                     "AppCheck compacto:\n"
                     "MemCheck:\n"
                     "SchedCheck:\n"
                     "Health check: OK\n"
                     "PROC5 introspeccao: OK\n"
                     "VFS:\n"
                     SHELL_PROMPT "%s", expected_marker);
        } else if (terminal_mode == HOST_TERMINAL_HW6) {
            snprintf(terminal_text, sizeof(terminal_text),
                     "Health check: OK\n"
                     "RegCheck: OK\n"
                     "Dispositivos detectados:\n"
                     "Dispositivo (sysfs):\n"
                     "Varredura PCI concluida; inventario atualizado.\n"
                     "ACPI tables:\n"
                     "Rede:\n"
                     "USB:\n"
                     "Energia:\n"
                     SHELL_PROMPT "%s\n"
                     SHELL_PROMPT, expected_marker);
        } else if (terminal_mode == HOST_TERMINAL_INCOMPLETE) {
        snprintf(terminal_text, sizeof(terminal_text),
                 "SchedCheck:\n"
                 SHELL_PROMPT "%s\n"
                 SHELL_PROMPT, expected_marker);
        } else if (terminal_mode == HOST_TERMINAL_ABSENT) {
            strcpy(terminal_text, "prompt");
        } else {
            snprintf(terminal_text, sizeof(terminal_text),
                     SHELL_PROMPT "%s", expected_marker);
        }
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
                    const char* case_id, const char* marker,
                    host_terminal_mode_t mode, int expected_result) {
    int result;

    expected_marker = marker;
    terminal_mode = mode;
    snapshot_count = 0U;
    progress_count = 0U;
    yield_count = 0U;
    report_count = 0U;
    fake_ticks = 1U;
    strcpy(terminal_text, "prompt");
    result = kernel_tests_run_tst5_blackbox(runtime, case_id,
                                             (uint32_t)strlen(case_id));
    if (result != expected_result || report_count != 4U) return 0;
    if (expected_result == OK) {
        return snapshot_count == 3U && progress_count == 1U &&
               yield_count == 1U;
    }
    if (mode == HOST_TERMINAL_INCOMPLETE) {
        return snapshot_count == 3U && progress_count == 1U &&
               yield_count == 1U;
    }
    return mode == HOST_TERMINAL_ABSENT && fake_ticks > 2500U &&
           yield_count >= 2500U;
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
        {"qemu:tst5:poweroff", "tst5-poweroff"},
        {"qemu:tst5:krn6-diagnostics", "krn6-diagnostics"},
        {"qemu:tst5:sec6-simple", "sec6-simple"},
        {"qemu:tst5:sec6-classic", "sec6-classic"},
        {"qemu:tst5:sec6-diagnostics", "sec6-diagnostics"},
        {"qemu:tst5:hw6-diagnostics", "hw6-diagnostics"}
    };
    kernel_tests_runtime_t runtime;

    runtime.progress = fake_progress;
    runtime.context = 0;
    runtime.report_phase = fake_report;
    for (uint32_t index = 0U; index < HOST_CASE_COUNT; index++) {
        host_terminal_mode_t mode = index == HOST_CASE_COUNT - 1U ?
                                    HOST_TERMINAL_HW6 :
                                    index == HOST_CASE_COUNT - 2U ?
                                    HOST_TERMINAL_SEC6 :
                                    index == 9U ? HOST_TERMINAL_KRN6 :
                                    HOST_TERMINAL_NORMAL;
        if (!run_case(&runtime, cases[index][0], cases[index][1], mode, OK)) {
            return 10 + (int)index;
        }
    }
    return 0;
}

static int check_krn6_observer_failures(void) {
    kernel_tests_runtime_t runtime;

    runtime.progress = fake_progress;
    runtime.context = 0;
    runtime.report_phase = fake_report;
    if (!run_case(&runtime, "qemu:tst5:krn6-diagnostics",
                  "krn6-diagnostics", HOST_TERMINAL_INCOMPLETE,
                  ERR_STATE)) return 31;
    if (!run_case(&runtime, "qemu:tst5:krn6-diagnostics",
                  "krn6-diagnostics", HOST_TERMINAL_ABSENT,
                  ERR_TIMEOUT)) return 32;
    return 0;
}

static int check_hw6_observer_failures(void) {
    kernel_tests_runtime_t runtime;

    runtime.progress = fake_progress;
    runtime.context = 0;
    runtime.report_phase = fake_report;
    if (!run_case(&runtime, "qemu:tst5:hw6-diagnostics",
                  "hw6-diagnostics", HOST_TERMINAL_INCOMPLETE,
                  ERR_STATE)) return 33;
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
    if (!result) result = check_krn6_observer_failures();
    if (!result) result = check_hw6_observer_failures();
    if (!result) result = check_invalid_case();
    if (!result && kernel_tests_progress(0) != ERR_NULL) result = 40;
    if (!result && kernel_tests_report_phase(0, "phase", OK) != ERR_NULL) {
        result = 41;
    }
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
