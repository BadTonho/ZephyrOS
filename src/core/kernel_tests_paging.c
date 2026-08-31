#include "kernel_tests.h"

#include "core/app_api.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/syscall.h"
#include "core/timer.h"
#include "memory/paging.h"
#include "memory/vma.h"
#include "process/process.h"

#define KERNEL_TEST_PAGING_TAG "TST4"
#define KERNEL_TEST_PAGING_CODE_CAPACITY 512U
#define KERNEL_TEST_PAGING_DATA_CAPACITY 128U
#define KERNEL_TEST_PAGING_TIMEOUT_TICKS 250U
#define KERNEL_TEST_PAGING_FAILURE_CAPACITY 16U

static uint8_t paging_fixture_code[KERNEL_TEST_PAGING_CODE_CAPACITY];
static uint8_t paging_fixture_data[KERNEL_TEST_PAGING_DATA_CAPACITY];

static void paging_emit_mov(uint8_t* code, uint32_t* offset, uint8_t reg,
                            uint32_t value) {
    code[(*offset)++] = (uint8_t)(0xB8U + reg);
    code[(*offset)++] = (uint8_t)(value & 0xFFU);
    code[(*offset)++] = (uint8_t)((value >> 8U) & 0xFFU);
    code[(*offset)++] = (uint8_t)((value >> 16U) & 0xFFU);
    code[(*offset)++] = (uint8_t)((value >> 24U) & 0xFFU);
}

static void paging_emit_load_ebx(uint8_t* code, uint32_t* offset,
                                 uint32_t address) {
    code[(*offset)++] = 0x8BU;
    code[(*offset)++] = 0x1DU;
    code[(*offset)++] = (uint8_t)(address & 0xFFU);
    code[(*offset)++] = (uint8_t)((address >> 8U) & 0xFFU);
    code[(*offset)++] = (uint8_t)((address >> 16U) & 0xFFU);
    code[(*offset)++] = (uint8_t)((address >> 24U) & 0xFFU);
}

static void paging_emit_compare_eax(uint8_t* code, uint32_t* offset,
                                    uint32_t value) {
    code[(*offset)++] = 0x3DU;
    code[(*offset)++] = (uint8_t)(value & 0xFFU);
    code[(*offset)++] = (uint8_t)((value >> 8U) & 0xFFU);
    code[(*offset)++] = (uint8_t)((value >> 16U) & 0xFFU);
    code[(*offset)++] = (uint8_t)((value >> 24U) & 0xFFU);
}

static uint32_t paging_emit_jne(uint8_t* code, uint32_t* offset) {
    uint32_t patch = *offset + 2U;

    code[(*offset)++] = 0x0FU;
    code[(*offset)++] = 0x85U;
    code[(*offset)++] = 0U;
    code[(*offset)++] = 0U;
    code[(*offset)++] = 0U;
    code[(*offset)++] = 0U;
    return patch;
}

static void paging_emit_syscall(uint8_t* code, uint32_t* offset) {
    code[(*offset)++] = 0xCDU;
    code[(*offset)++] = 0x80U;
}

static void paging_emit_expected_mmap(uint32_t* offset,
                                      uint32_t length,
                                      uint32_t protection,
                                      uint32_t flags,
                                      uint32_t expected,
                                      uint32_t* failure_jumps,
                                      uint32_t* failure_count) {
    paging_emit_mov(paging_fixture_code, offset, 0U, APP_SYSCALL_MMAP);
    paging_emit_mov(paging_fixture_code, offset, 3U, length);
    paging_emit_mov(paging_fixture_code, offset, 1U, protection);
    paging_emit_mov(paging_fixture_code, offset, 2U, flags);
    paging_emit_mov(paging_fixture_code, offset, 6U, USER_DATA_BASE);
    paging_emit_syscall(paging_fixture_code, offset);
    paging_emit_compare_eax(paging_fixture_code, offset, expected);
    failure_jumps[(*failure_count)++] =
        paging_emit_jne(paging_fixture_code, offset);
}

static void paging_emit_expected_munmap(uint32_t* offset, uint32_t address,
                                        uint32_t length, uint32_t expected,
                                        uint32_t* failure_jumps,
                                        uint32_t* failure_count) {
    paging_emit_mov(paging_fixture_code, offset, 0U, APP_SYSCALL_MUNMAP);
    paging_emit_mov(paging_fixture_code, offset, 3U, address);
    paging_emit_mov(paging_fixture_code, offset, 1U, length);
    paging_emit_syscall(paging_fixture_code, offset);
    paging_emit_compare_eax(paging_fixture_code, offset, expected);
    failure_jumps[(*failure_count)++] =
        paging_emit_jne(paging_fixture_code, offset);
}

static void paging_emit_exit(uint32_t* offset, uint32_t code) {
    paging_emit_mov(paging_fixture_code, offset, 3U, code);
    paging_emit_mov(paging_fixture_code, offset, 0U,
                    APP_SYSCALL_PROCESS_EXIT);
    paging_emit_syscall(paging_fixture_code, offset);
    paging_fixture_code[(*offset)++] = 0xF4U;
}

static uint32_t paging_build_fixture(void) {
    uint32_t failure_jumps[KERNEL_TEST_PAGING_FAILURE_CAPACITY];
    uint32_t failure_count = 0U;
    uint32_t offset = 0U;
    uint32_t failure_offset;

    kmemset(paging_fixture_code, 0, sizeof(paging_fixture_code));
    kmemset(paging_fixture_data, 0, sizeof(paging_fixture_data));

    paging_emit_expected_mmap(
        &offset, PAGE_SIZE, APP_MMAP_PROT_READ | APP_MMAP_PROT_WRITE,
        APP_MMAP_FLAG_ANONYMOUS, OK, failure_jumps, &failure_count);
    paging_emit_load_ebx(paging_fixture_code, &offset, USER_DATA_BASE);
    paging_fixture_code[offset++] = 0x89U;
    paging_fixture_code[offset++] = 0x1DU;
    paging_fixture_code[offset++] = (uint8_t)(USER_DATA_BASE & 0xFFU);
    paging_fixture_code[offset++] = (uint8_t)((USER_DATA_BASE >> 8U) & 0xFFU);
    paging_fixture_code[offset++] = (uint8_t)((USER_DATA_BASE >> 16U) & 0xFFU);
    paging_fixture_code[offset++] = (uint8_t)((USER_DATA_BASE >> 24U) & 0xFFU);
    paging_fixture_code[offset++] = 0xC6U;
    paging_fixture_code[offset++] = 0x03U;
    paging_fixture_code[offset++] = 0x5AU;
    paging_fixture_code[offset++] = 0x8AU;
    paging_fixture_code[offset++] = 0x03U;
    paging_fixture_code[offset++] = 0x3CU;
    paging_fixture_code[offset++] = 0x5AU;
    failure_jumps[failure_count++] =
        paging_emit_jne(paging_fixture_code, &offset);
    paging_emit_load_ebx(paging_fixture_code, &offset, USER_DATA_BASE);
    paging_emit_mov(paging_fixture_code, &offset, 0U, APP_SYSCALL_MUNMAP);
    paging_emit_mov(paging_fixture_code, &offset, 1U, PAGE_SIZE);
    paging_emit_syscall(paging_fixture_code, &offset);
    paging_emit_compare_eax(paging_fixture_code, &offset, OK);
    failure_jumps[failure_count++] =
        paging_emit_jne(paging_fixture_code, &offset);

    paging_emit_expected_mmap(
        &offset, 0U, APP_MMAP_PROT_READ, APP_MMAP_FLAG_ANONYMOUS,
        ERR_INVALID, failure_jumps, &failure_count);
    paging_emit_expected_mmap(
        &offset, 0xFFFFF001U, APP_MMAP_PROT_READ, APP_MMAP_FLAG_ANONYMOUS,
        ERR_OVERFLOW, failure_jumps, &failure_count);
    paging_emit_expected_mmap(
        &offset, PAGE_SIZE, APP_MMAP_PROT_READ,
        APP_MMAP_FLAG_SHARED | APP_MMAP_FLAG_ANONYMOUS, ERR_UNAVAILABLE,
        failure_jumps, &failure_count);
    paging_emit_expected_munmap(&offset, 0U, PAGE_SIZE, ERR_INVALID,
                                failure_jumps, &failure_count);
    paging_emit_expected_munmap(&offset, USER_CODE_BASE, 0U, ERR_INVALID,
                                failure_jumps, &failure_count);
    paging_emit_expected_munmap(&offset, USER_CODE_BASE, PAGE_SIZE,
                                ERR_INVALID, failure_jumps, &failure_count);

    paging_emit_exit(&offset, APP_EXIT_SUCCESS);
    failure_offset = offset;
    paging_emit_exit(&offset, ERR_STATE);

    for (uint32_t index = 0U; index < failure_count; index++) {
        int32_t relative = (int32_t)failure_offset -
                           (int32_t)(failure_jumps[index] + 4U);
        uint32_t patch = failure_jumps[index];

        paging_fixture_code[patch] = (uint8_t)(relative & 0xFF);
        paging_fixture_code[patch + 1U] = (uint8_t)((relative >> 8) & 0xFF);
        paging_fixture_code[patch + 2U] = (uint8_t)((relative >> 16) & 0xFF);
        paging_fixture_code[patch + 3U] = (uint8_t)((relative >> 24) & 0xFF);
    }
    return offset;
}

static int paging_wait_fixture(const kernel_tests_runtime_t* runtime,
                               uint32_t pid, process_snapshot_t* snapshot) {
    uint32_t start_ticks = timer_get_ticks();
    process_t* process;
    uint32_t result_pid;
    uint32_t faulted;
    uint8_t timed_out = 0U;
    int result;

    while (1) {
        process = process_get_by_pid(pid);
        if (!process) {
            LOG_ERROR(KERNEL_TEST_PAGING_TAG,
                      "fase=paging-vma processo ausente");
            LOG_ERROR_CODE(KERNEL_TEST_PAGING_TAG, ERR_NOT_FOUND,
                           "fase=paging-vma processo ausente");
            return ERR_NOT_FOUND;
        }
        if (process->state == PROCESS_STATE_ZOMBIE) break;
        if (timer_get_ticks() - start_ticks >=
            KERNEL_TEST_PAGING_TIMEOUT_TICKS) {
            timed_out = 1U;
            result = process_cancel_user_test(pid, ERR_TIMEOUT);
            if (result != OK) {
                LOG_ERROR_CODE(KERNEL_TEST_PAGING_TAG, result,
                               "fase=paging-vma cancelamento falhou");
                return result;
            }
            break;
        }
        result = kernel_tests_progress(runtime);
        if (result != OK) {
            LOG_ERROR_CODE(KERNEL_TEST_PAGING_TAG, result,
                           "fase=paging-vma progresso falhou");
            return result;
        }
        process_yield();
    }
    result = process_take_user_test_result(&result_pid, &faulted);
    if (result != OK || result_pid != pid || faulted) {
        LOG_ERROR_CODE(KERNEL_TEST_PAGING_TAG, ERR_STATE,
                       "fase=paging-vma resultado do processo invalido");
        return ERR_STATE;
    }
    if (snapshot) {
        result = process_snapshot_copy(pid, snapshot);
        if (result != OK) {
            LOG_ERROR_CODE(KERNEL_TEST_PAGING_TAG, result,
                           "fase=paging-vma snapshot falhou");
            return result;
        }
    }
    result = process_reap_finished_user();
    if (result != OK) {
        LOG_ERROR_CODE(KERNEL_TEST_PAGING_TAG, result,
                       "fase=paging-vma coleta falhou");
        return result;
    }
    if (timed_out) {
        LOG_ERROR_CODE(KERNEL_TEST_PAGING_TAG, ERR_TIMEOUT,
                       "fase=paging-vma timeout");
        return ERR_TIMEOUT;
    }
    if (!snapshot || snapshot->exit_code != APP_EXIT_SUCCESS) {
        LOG_ERROR_CODE(KERNEL_TEST_PAGING_TAG, ERR_STATE,
                       "fase=paging-vma codigo de saida invalido");
        return ERR_STATE;
    }
    return OK;
}

static int paging_validate_fixture_baseline(const paging_user_stats_t* before,
                                            const page_fault_stats_t* faults,
                                            uint32_t user_count,
                                            uint32_t zombie_count) {
    paging_user_stats_t after;
    page_fault_stats_t after_faults;
    int result;

    paging_get_user_stats(&after);
    result = process_vma_get_page_fault_stats(&after_faults);
    if (result != OK) {
        LOG_ERROR(KERNEL_TEST_PAGING_TAG,
                  "fase=paging-vma estatisticas de fault falharam");
        LOG_ERROR_CODE(KERNEL_TEST_PAGING_TAG, result,
                       "fase=paging-vma estatisticas de fault falharam");
        return result;
    }
    if (after.active_directories != before->active_directories) {
        LOG_ERROR(KERNEL_TEST_PAGING_TAG,
                  "fase=paging-vma diretorios ativos nao restaurados");
        LOG_ERROR_CODE(KERNEL_TEST_PAGING_TAG, ERR_STATE,
                       "fase=paging-vma diretorios ativos nao restaurados");
        return ERR_STATE;
    }
    if (after.active_pages != before->active_pages) {
        LOG_ERROR(KERNEL_TEST_PAGING_TAG,
                  "fase=paging-vma paginas ativas nao restauradas");
        LOG_ERROR_CODE(KERNEL_TEST_PAGING_TAG, ERR_STATE,
                       "fase=paging-vma paginas ativas nao restauradas");
        return ERR_STATE;
    }
    if (process_get_user_count() != user_count) {
        LOG_ERROR(KERNEL_TEST_PAGING_TAG,
                  "fase=paging-vma processos de usuario nao restaurados");
        LOG_ERROR_CODE(KERNEL_TEST_PAGING_TAG, ERR_STATE,
                       "fase=paging-vma processos de usuario nao restaurados");
        return ERR_STATE;
    }
    if (process_get_state_count(PROCESS_STATE_ZOMBIE) != zombie_count) {
        LOG_ERROR(KERNEL_TEST_PAGING_TAG,
                  "fase=paging-vma zombies nao restaurados");
        LOG_ERROR_CODE(KERNEL_TEST_PAGING_TAG, ERR_STATE,
                       "fase=paging-vma zombies nao restaurados");
        return ERR_STATE;
    }
    if (after_faults.handled <= faults->handled) {
        LOG_ERROR(KERNEL_TEST_PAGING_TAG,
                  "fase=paging-vma fault lazy nao foi observado");
        LOG_ERROR_CODE(KERNEL_TEST_PAGING_TAG, ERR_STATE,
                       "fase=paging-vma fault lazy nao foi observado");
        return ERR_STATE;
    }
    if (after_faults.invalid < faults->invalid) {
        LOG_ERROR(KERNEL_TEST_PAGING_TAG,
                  "fase=paging-vma contador de faults invalidos regrediu");
        LOG_ERROR_CODE(KERNEL_TEST_PAGING_TAG, ERR_STATE,
                       "fase=paging-vma contador de faults invalidos regrediu");
        return ERR_STATE;
    }
    return OK;
}

int kernel_tests_run_paging_vma(const kernel_tests_runtime_t* runtime) {
    paging_user_stats_t paging_before;
    page_fault_stats_t faults_before;
    process_snapshot_t finished;
    uint32_t initial_users;
    uint32_t initial_zombies;
    uint32_t pid = 0U;
    uint32_t code_size;
    int result;

    LOG_INFO(KERNEL_TEST_PAGING_TAG, "fase=paging-vma-preconditions inicio");
    result = process_vma_get_page_fault_stats(&faults_before);
    if (!paging_is_ready() || !syscall_user_mode_is_enabled() ||
        result != OK) {
        return kernel_tests_phase_result("fase=paging-vma-preconditions",
                                         ERR_STATE);
    }
    paging_get_user_stats(&paging_before);
    initial_users = process_get_user_count();
    initial_zombies = process_get_state_count(PROCESS_STATE_ZOMBIE);
    code_size = paging_build_fixture();
    if (code_size == 0U || code_size > sizeof(paging_fixture_code)) {
        return kernel_tests_phase_result("fase=paging-vma-fixture-build",
                                         ERR_STATE);
    }
    result = process_create_user_image(
        "TST4-VMA", paging_fixture_code, code_size, paging_fixture_data,
        sizeof(paging_fixture_data), 0U, PAGE_SIZE, 1, &pid);
    if (result != OK) {
        return kernel_tests_phase_result("fase=paging-vma-fixture-create",
                                         result);
    }
    result = paging_wait_fixture(runtime, pid, &finished);
    if (result != OK) {
        return kernel_tests_phase_result("fase=paging-vma-fixture-run",
                                         result);
    }
    result = paging_validate_fixture_baseline(
        &paging_before, &faults_before, initial_users, initial_zombies);
    if (result != OK) {
        return kernel_tests_phase_result("fase=paging-vma-postconditions",
                                         result);
    }
    return kernel_tests_phase_result("fase=paging-vma-postconditions", OK);
}
