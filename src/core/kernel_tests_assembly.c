#include "kernel_tests.h"

#include "../drivers/idt_internal.h"
#include "core/errors.h"
#include "core/log.h"

#define KERNEL_TEST_ASSEMBLY_TAG "TST7"
#define KERNEL_TEST_EXCEPTION_COUNT 32U
#define KERNEL_TEST_IRQ_COUNT 16U
#define KERNEL_TEST_SYSCALL_VECTOR 128U

#define KERNEL_TEST_INT_NOERR(value) \
    __asm__ volatile("int $" #value : : : "cc", "memory")
#define KERNEL_TEST_INT_ERR(value) \
    __asm__ volatile("pushl %%eax\n\tpushfl\n\txorl %%eax, %%eax\n\tmovw %%cs, %%ax\n\tpushl %%eax\n\tpushl $1f\n\tpushl $0\n\tjmp isr" #value "\n1:\n\tpopl %%eax" \
                     : : : "cc", "memory")

static int kernel_tests_trigger_vector(uint8_t vector) {
    switch (vector) {
        case 0: KERNEL_TEST_INT_NOERR(0); break;
        case 1: KERNEL_TEST_INT_NOERR(1); break;
        case 2: KERNEL_TEST_INT_NOERR(2); break;
        case 3: KERNEL_TEST_INT_NOERR(3); break;
        case 4: KERNEL_TEST_INT_NOERR(4); break;
        case 5: KERNEL_TEST_INT_NOERR(5); break;
        case 6: KERNEL_TEST_INT_NOERR(6); break;
        case 7: KERNEL_TEST_INT_NOERR(7); break;
        case 8: KERNEL_TEST_INT_ERR(8); break;
        case 9: KERNEL_TEST_INT_NOERR(9); break;
        case 10: KERNEL_TEST_INT_ERR(10); break;
        case 11: KERNEL_TEST_INT_ERR(11); break;
        case 12: KERNEL_TEST_INT_ERR(12); break;
        case 13: KERNEL_TEST_INT_ERR(13); break;
        case 14: KERNEL_TEST_INT_ERR(14); break;
        case 15: KERNEL_TEST_INT_NOERR(15); break;
        case 16: KERNEL_TEST_INT_NOERR(16); break;
        case 17: KERNEL_TEST_INT_ERR(17); break;
        case 18: KERNEL_TEST_INT_NOERR(18); break;
        case 19: KERNEL_TEST_INT_NOERR(19); break;
        case 20: KERNEL_TEST_INT_NOERR(20); break;
        case 21: KERNEL_TEST_INT_NOERR(21); break;
        case 22: KERNEL_TEST_INT_NOERR(22); break;
        case 23: KERNEL_TEST_INT_NOERR(23); break;
        case 24: KERNEL_TEST_INT_NOERR(24); break;
        case 25: KERNEL_TEST_INT_NOERR(25); break;
        case 26: KERNEL_TEST_INT_NOERR(26); break;
        case 27: KERNEL_TEST_INT_NOERR(27); break;
        case 28: KERNEL_TEST_INT_NOERR(28); break;
        case 29: KERNEL_TEST_INT_NOERR(29); break;
        case 30: KERNEL_TEST_INT_ERR(30); break;
        case 31: KERNEL_TEST_INT_NOERR(31); break;
        case 32: KERNEL_TEST_INT_NOERR(32); break;
        case 33: KERNEL_TEST_INT_NOERR(33); break;
        case 34: KERNEL_TEST_INT_NOERR(34); break;
        case 35: KERNEL_TEST_INT_NOERR(35); break;
        case 36: KERNEL_TEST_INT_NOERR(36); break;
        case 37: KERNEL_TEST_INT_NOERR(37); break;
        case 38: KERNEL_TEST_INT_NOERR(38); break;
        case 39: KERNEL_TEST_INT_NOERR(39); break;
        case 40: KERNEL_TEST_INT_NOERR(40); break;
        case 41: KERNEL_TEST_INT_NOERR(41); break;
        case 42: KERNEL_TEST_INT_NOERR(42); break;
        case 43: KERNEL_TEST_INT_NOERR(43); break;
        case 44: KERNEL_TEST_INT_NOERR(44); break;
        case 45: KERNEL_TEST_INT_NOERR(45); break;
        case 46: KERNEL_TEST_INT_NOERR(46); break;
        case 47: KERNEL_TEST_INT_NOERR(47); break;
        case KERNEL_TEST_SYSCALL_VECTOR:
            KERNEL_TEST_INT_NOERR(128);
            break;
        default:
            LOG_ERROR(KERNEL_TEST_ASSEMBLY_TAG,
                      "fase=assembly-vector vetor invalido");
            return ERR_INVALID;
    }
    return OK;
}

static int kernel_tests_validate_vectors(void) {
    for (uint32_t vector = 0U; vector < KERNEL_TEST_EXCEPTION_COUNT;
         vector++) {
        if (idt_test_probe_get_count((uint8_t)vector) == 0U) {
            LOG_ERROR(KERNEL_TEST_ASSEMBLY_TAG,
                      "fase=assembly-vectors excecao nao observada");
            return ERR_STATE;
        }
    }
    for (uint32_t irq = 0U; irq < KERNEL_TEST_IRQ_COUNT; irq++) {
        if (idt_test_probe_get_count((uint8_t)(32U + irq)) == 0U) {
            LOG_ERROR(KERNEL_TEST_ASSEMBLY_TAG,
                      "fase=assembly-vectors IRQ nao observada");
            return ERR_STATE;
        }
    }
    if (idt_test_probe_get_count(KERNEL_TEST_SYSCALL_VECTOR) == 0U) {
        LOG_ERROR(KERNEL_TEST_ASSEMBLY_TAG,
                  "fase=assembly-vectors syscall nao observada");
        return ERR_STATE;
    }
    return OK;
}

int kernel_tests_run_assembly(const kernel_tests_runtime_t* runtime) {
    int result;
    int cleanup_result;

    LOG_INFO(KERNEL_TEST_ASSEMBLY_TAG, "fase=assembly-preconditions inicio");
    result = kernel_tests_progress(runtime);
    if (result != OK) return result;
    result = idt_test_probe_begin();
    if (result != OK) {
        return kernel_tests_phase_result("fase=assembly-probe-begin", result);
    }
    LOG_INFO(KERNEL_TEST_ASSEMBLY_TAG,
             "fase=assembly-probe-begin concluida");
    LOG_INFO(KERNEL_TEST_ASSEMBLY_TAG,
             "fase=assembly-exceptions inicio");
    for (uint32_t vector = 0U;
         vector < KERNEL_TEST_EXCEPTION_COUNT; vector++) {
        result = kernel_tests_trigger_vector((uint8_t)vector);
        if (result != OK) break;
    }
    if (result == OK) {
        LOG_INFO(KERNEL_TEST_ASSEMBLY_TAG,
                 "fase=assembly-exceptions concluida");
    }
    if (result == OK) {
        LOG_INFO(KERNEL_TEST_ASSEMBLY_TAG,
                 "fase=assembly-irqs inicio");
        for (uint32_t irq = 0U; irq < KERNEL_TEST_IRQ_COUNT; irq++) {
            result = kernel_tests_trigger_vector((uint8_t)(32U + irq));
            if (result != OK) break;
        }
    }
    if (result == OK) {
        LOG_INFO(KERNEL_TEST_ASSEMBLY_TAG,
                 "fase=assembly-irqs concluida");
        LOG_INFO(KERNEL_TEST_ASSEMBLY_TAG,
                 "fase=assembly-syscall inicio");
    }
    if (result == OK) result = kernel_tests_trigger_vector(
        KERNEL_TEST_SYSCALL_VECTOR);
    if (result == OK) {
        LOG_INFO(KERNEL_TEST_ASSEMBLY_TAG,
                 "fase=assembly-syscall concluida");
    }
    if (result == OK) result = kernel_tests_validate_vectors();
    cleanup_result = idt_test_probe_end();
    if (result != OK) {
        LOG_ERROR(KERNEL_TEST_ASSEMBLY_TAG,
                  "fase=assembly-vectors resultado invalido");
        return kernel_tests_phase_result("fase=assembly-vectors", result);
    }
    if (cleanup_result != OK) {
        LOG_ERROR(KERNEL_TEST_ASSEMBLY_TAG,
                  "fase=assembly-cleanup falhou");
        return kernel_tests_phase_result("fase=assembly-cleanup",
                                         cleanup_result);
    }
    result = kernel_tests_progress(runtime);
    if (result != OK) return result;
    return kernel_tests_phase_result("fase=assembly-postconditions", OK);
}
