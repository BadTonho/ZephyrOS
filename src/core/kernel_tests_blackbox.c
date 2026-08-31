#include "kernel_tests.h"

#include "core/log.h"
#include "core/timer.h"
#include "process/process.h"
#include "video_test.h"

#define KERNEL_TESTS_BLACKBOX_TIMEOUT_TICKS 2500U
#define KERNEL_TESTS_BLACKBOX_TEXT_CAPACITY VIDEO_TEST_TEXT_CAPACITY

static char blackbox_text[KERNEL_TESTS_BLACKBOX_TEXT_CAPACITY];

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
    return 0;
}

static int blackbox_snapshot(char* text, video_test_terminal_info_t* info) {
    return video_test_copy_terminal(text, KERNEL_TESTS_BLACKBOX_TEXT_CAPACITY,
                                    info);
}

static int blackbox_wait_for_marker(const kernel_tests_runtime_t* runtime,
                                    const char* marker,
                                    uint32_t initial_generation) {
    video_test_terminal_info_t info;
    uint32_t start = timer_get_ticks();

    while (timer_get_ticks() - start < KERNEL_TESTS_BLACKBOX_TIMEOUT_TICKS) {
        if (blackbox_snapshot(blackbox_text, &info) != OK) {
            LOG_ERROR("TST5", "Observer de terminal indisponivel");
            return ERR_STATE;
        }
        if (info.active && info.generation > initial_generation &&
            blackbox_contains(blackbox_text, marker)) {
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
    int result;

    marker = blackbox_marker(case_id, case_length);
    result = blackbox_report(runtime, "case-selection", marker ? OK :
                             ERR_NOT_FOUND);
    if (result != OK) return result;
    result = blackbox_snapshot(blackbox_text, &before);
    result = blackbox_report(runtime, "terminal-snapshot", result);
    if (result != OK) return result;
    result = blackbox_report(runtime, "input-path", OK);
    if (result != OK) return result;
    result = blackbox_wait_for_marker(runtime, marker, before.generation);
    return blackbox_report(runtime, "terminal-observer", result);
}
