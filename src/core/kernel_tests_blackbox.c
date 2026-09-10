#include "kernel_tests.h"

#include "apps/shell.h"
#include "core/log.h"
#include "core/timer.h"
#include "process/process.h"
#include "video_test.h"

#define KERNEL_TESTS_BLACKBOX_TIMEOUT_TICKS 2500U
#define KERNEL_TESTS_BLACKBOX_TEXT_CAPACITY VIDEO_TEST_TEXT_CAPACITY
#define KRN6_REQUIRED_COUNT 15U
#define SEC6_REQUIRED_COUNT 7U

static char blackbox_text[KERNEL_TESTS_BLACKBOX_TEXT_CAPACITY];
static uint8_t blackbox_krn6_seen[KRN6_REQUIRED_COUNT];
static uint8_t blackbox_sec6_seen[SEC6_REQUIRED_COUNT];

static uint32_t blackbox_length(const char* text) {
    uint32_t length = 0U;

    if (!text) return 0U;
    while (text[length]) length++;
    return length;
}

static int blackbox_equals(const char* value, uint32_t length,
                           const char* expected) {
    uint32_t expected_length = blackbox_length(expected);

    if (!value || !expected || length != expected_length) return 0;
    for (uint32_t index = 0U; index < length; index++) {
        if (value[index] != expected[index]) return 0;
    }
    return 1;
}

static int blackbox_contains(const char* text, const char* needle) {
    uint32_t text_length = blackbox_length(text);
    uint32_t needle_length = blackbox_length(needle);

    if (!text || !needle || needle_length == 0U || needle_length > text_length) {
        return 0;
    }
    for (uint32_t start = 0U; start + needle_length <= text_length; start++) {
        uint32_t index = 0U;

        while (index < needle_length && text[start + index] == needle[index]) {
            index++;
        }
        if (index == needle_length) return 1;
    }
    return 0;
}

static int blackbox_is_krn6_case(const char* case_id, uint32_t case_length) {
    static const char case_name[] = "qemu:tst5:krn6-diagnostics";

    return blackbox_equals(case_id, case_length, case_name);
}

static int blackbox_is_sec6_case(const char* case_id, uint32_t case_length) {
    static const char case_name[] = "qemu:tst5:sec6-diagnostics";

    return blackbox_equals(case_id, case_length, case_name);
}

static int blackbox_requires_prompt(const char* case_id,
                                    uint32_t case_length) {
    static const char reboot_case[] = "qemu:tst5:reboot";
    static const char poweroff_case[] = "qemu:tst5:poweroff";

    return !blackbox_equals(case_id, case_length, reboot_case) &&
           !blackbox_equals(case_id, case_length, poweroff_case);
}

static void blackbox_reset_krn6_observation(void) {
    for (uint32_t index = 0U; index < KRN6_REQUIRED_COUNT; index++) {
        blackbox_krn6_seen[index] = 0U;
    }
}

static void blackbox_reset_sec6_observation(void) {
    for (uint32_t index = 0U; index < SEC6_REQUIRED_COUNT; index++) {
        blackbox_sec6_seen[index] = 0U;
    }
}

static int blackbox_validate_krn6_output(const char* text, int final_snapshot) {
    static const char* required[] = {
        "Resumo do health:",
        "Health check:",
        "SchedCheck:",
        "contabilidade_idle OK",
        "MemCheck:",
        "memoria_detalhada OK",
        "Autoteste de Bottom-Half (fila privada):",
        "Autoteste de esperas (canal privado):",
        "Autoteste da workqueue (fixture privada):",
        "Resultado: OK",
        "Metricas K1 (desde reset):",
        "RegCheck: OK",
        "Processos ativos:",
        "Total:",
        "ZOMBIE=0"
    };

    for (uint32_t index = 0U;
         index < sizeof(required) / sizeof(required[0]); index++) {
        if (blackbox_contains(text, required[index])) {
            blackbox_krn6_seen[index] = 1U;
        }
    }
    if (!final_snapshot) return 1;
    for (uint32_t index = 0U; index < KRN6_REQUIRED_COUNT; index++) {
        if (!blackbox_krn6_seen[index]) return 0;
    }
    if (blackbox_contains(text, "RegCheck: ERRO")) {
        return 0;
    }
    if (blackbox_contains(text, "Health check: ERRO")) {
        return 0;
    }
    if (blackbox_contains(text, "UNKNOWN")) {
        return 0;
    }
    if (blackbox_contains(text, "resultado ERRO") ||
        blackbox_contains(text, "Resultado: ERRO")) {
        return 0;
    }
    if (blackbox_contains(text, "processo ring 3")) {
        return 0;
    }
    if (blackbox_contains(text, "zumbi pendente")) {
        return 0;
    }
    return 1;
}

static int blackbox_validate_sec6_output(const char* text,
                                         int final_snapshot) {
    static const char* required[] = {
        "AppCheck compacto:",
        "MemCheck:",
        "SchedCheck:",
        "Health check:",
        "PROC5 introspeccao: OK",
        "VFS:",
        SHELL_PROMPT
    };

    for (uint32_t index = 0U;
         index < sizeof(required) / sizeof(required[0]); index++) {
        if (blackbox_contains(text, required[index])) {
            blackbox_sec6_seen[index] = 1U;
        }
    }
    if (!final_snapshot) return 1;
    for (uint32_t index = 0U; index < SEC6_REQUIRED_COUNT; index++) {
        if (!blackbox_sec6_seen[index]) return 0;
    }
    if (blackbox_contains(text, "resultado=ERRO") ||
        blackbox_contains(text, "resultado ERRO") ||
        blackbox_contains(text, "PROC5 introspeccao: ERRO")) {
        return 0;
    }
    return 1;
}

static const char* blackbox_marker(const char* case_id, uint32_t case_length) {
    static const char shell_case[] = "qemu:tst5:shell";
    static const char input_case[] = "qemu:tst5:input";
    static const char apps_case[] = "qemu:tst5:apps";
    static const char processes_case[] = "qemu:tst5:processes";
    static const char storage_case[] = "qemu:tst5:storage";
    static const char network_case[] = "qemu:tst5:network";
    static const char update_case[] = "qemu:tst5:update-recovery";
    static const char reboot_case[] = "qemu:tst5:reboot";
    static const char poweroff_case[] = "qemu:tst5:poweroff";
    static const char krn6_case[] = "qemu:tst5:krn6-diagnostics";
    static const char sec6_simple_case[] = "qemu:tst5:sec6-simple";
    static const char sec6_classic_case[] = "qemu:tst5:sec6-classic";
    static const char sec6_diagnostics_case[] = "qemu:tst5:sec6-diagnostics";

    if (blackbox_equals(case_id, case_length, shell_case)) return "tst5-shell";
    if (blackbox_equals(case_id, case_length, input_case)) return "tst5-input";
    if (blackbox_equals(case_id, case_length, apps_case)) return "tst5-apps";
    if (blackbox_equals(case_id, case_length, processes_case)) {
        return "tst5-processes";
    }
    if (blackbox_equals(case_id, case_length, storage_case)) {
        return "tst5-storage";
    }
    if (blackbox_equals(case_id, case_length, network_case)) {
        return "tst5-network";
    }
    if (blackbox_equals(case_id, case_length, update_case)) {
        return "tst5-update";
    }
    if (blackbox_equals(case_id, case_length, reboot_case)) {
        return "tst5-reboot";
    }
    if (blackbox_equals(case_id, case_length, poweroff_case)) {
        return "tst5-poweroff";
    }
    if (blackbox_equals(case_id, case_length, krn6_case)) {
        return "krn6-diagnostics";
    }
    if (blackbox_equals(case_id, case_length, sec6_simple_case)) {
        return "sec6-simple";
    }
    if (blackbox_equals(case_id, case_length, sec6_classic_case)) {
        return "sec6-classic";
    }
    if (blackbox_equals(case_id, case_length, sec6_diagnostics_case)) {
        return "sec6-diagnostics";
    }
    return 0;
}

static int blackbox_snapshot(char* text, video_test_terminal_info_t* info) {
    return video_test_copy_terminal(text, KERNEL_TESTS_BLACKBOX_TEXT_CAPACITY,
                                    info);
}

static int blackbox_wait_for_marker(const kernel_tests_runtime_t* runtime,
                                    const char* marker,
                                    uint32_t initial_generation,
                                    int validate_krn6,
                                    int validate_sec6,
                                    int require_prompt) {
    video_test_terminal_info_t info;
    uint32_t start = timer_get_ticks();

    while (timer_get_ticks() - start < KERNEL_TESTS_BLACKBOX_TIMEOUT_TICKS) {
        if (blackbox_snapshot(blackbox_text, &info) != OK) {
            LOG_ERROR("TST5", "Observer de terminal indisponivel");
            return ERR_STATE;
        }
        if (validate_krn6) {
            blackbox_validate_krn6_output(blackbox_text, 0);
        }
        if (validate_sec6) {
            blackbox_validate_sec6_output(blackbox_text, 0);
        }
        if (info.active && info.generation > initial_generation &&
            blackbox_contains(blackbox_text, marker)) {
            if (require_prompt && !blackbox_contains(blackbox_text,
                                                      SHELL_PROMPT)) {
                LOG_ERROR("TST5", "Prompt nao retornou apos caso black-box");
                return ERR_STATE;
            }
            if (validate_krn6 &&
                !blackbox_validate_krn6_output(blackbox_text, 1)) {
                LOG_ERROR("TST5", "Diagnosticos KRN6 incompletos");
                return ERR_STATE;
            }
            if (validate_sec6 &&
                !blackbox_validate_sec6_output(blackbox_text, 1)) {
                LOG_ERROR("TST5", "Diagnosticos SEC6 incompletos");
                return ERR_STATE;
            }
            return OK;
        }
        if (kernel_tests_progress(runtime) != OK) {
            LOG_ERROR("TST5", "Progresso do observer falhou");
            return ERR_STATE;
        }
        process_yield();
    }
    LOG_ERROR("TST5", "Observer de terminal excedeu o prazo");
    return ERR_TIMEOUT;
}

static int blackbox_report(const kernel_tests_runtime_t* runtime,
                           const char* phase, int result) {
    return kernel_tests_report_phase(runtime, phase, result);
}

int kernel_tests_run_tst5_blackbox(const kernel_tests_runtime_t* runtime,
                                   const char* case_id,
                                   uint32_t case_length) {
    video_test_terminal_info_t before;
    const char* marker;
    int validate_krn6;
    int validate_sec6;
    int require_prompt;
    int result;

    marker = blackbox_marker(case_id, case_length);
    validate_krn6 = blackbox_is_krn6_case(case_id, case_length);
    validate_sec6 = blackbox_is_sec6_case(case_id, case_length);
    require_prompt = blackbox_requires_prompt(case_id, case_length);
    if (validate_krn6) blackbox_reset_krn6_observation();
    if (validate_sec6) blackbox_reset_sec6_observation();
    result = blackbox_report(runtime, "case-selection", marker ? OK :
                             ERR_NOT_FOUND);
    if (result != OK) return result;
    result = blackbox_snapshot(blackbox_text, &before);
    result = blackbox_report(runtime, "terminal-snapshot", result);
    if (result != OK) return result;
    result = blackbox_report(runtime, "input-path", OK);
    if (result != OK) return result;
    result = blackbox_wait_for_marker(runtime, marker, before.generation,
                                      validate_krn6, validate_sec6,
                                      require_prompt);
    return blackbox_report(runtime, "terminal-observer", result);
}
