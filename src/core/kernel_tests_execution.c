#include "kernel_tests.h"

#include "core/log.h"
#include "core/wait.h"
#include "core/workqueue.h"
#include "process/process.h"
#include "process/signal.h"
#include "process/thread.h"

#define KERNEL_TEST_EXECUTION_TAG "TST4"

static int execution_check_ipc(void) {
    ipc_msg_t message;
    ipc_msg_t received;
    uint32_t pending_before = ipc_get_pending_count();
    uint32_t pid = process_get_current_pid();

    if (!pid || pending_before != 0U) {
        LOG_ERROR(KERNEL_TEST_EXECUTION_TAG,
                  "fase=execution-ipc precondicao invalida");
        LOG_ERROR_CODE(KERNEL_TEST_EXECUTION_TAG, ERR_STATE,
                       "fase=execution-ipc precondicao invalida");
        return ERR_STATE;
    }
    message.type = IPC_MSG_APP_REQUEST;
    message.data1 = IPC_APP_OPEN_SETTINGS;
    message.data2 = 0x54U;
    if (!ipc_send(pid, &message) || !ipc_current_has_pending() ||
        !ipc_receive(&received) || received.type != message.type ||
        received.data1 != message.data1 || received.data2 != message.data2 ||
        ipc_current_has_pending() || ipc_get_pending_count() != 0U) {
        LOG_ERROR(KERNEL_TEST_EXECUTION_TAG,
                  "fase=execution-ipc mensagem invalida");
        LOG_ERROR_CODE(KERNEL_TEST_EXECUTION_TAG, ERR_STATE,
                       "fase=execution-ipc mensagem invalida");
        return ERR_STATE;
    }
    if (ipc_send(MAX_PROCESSES + 1U, &message) ||
        ipc_send(pid, 0) || ipc_receive(0)) {
        LOG_ERROR(KERNEL_TEST_EXECUTION_TAG,
                  "fase=execution-ipc entradas invalidas");
        LOG_ERROR_CODE(KERNEL_TEST_EXECUTION_TAG, ERR_STATE,
                       "fase=execution-ipc entradas invalidas");
        return ERR_STATE;
    }
    message.type = IPC_MSG_NONE;
    if (ipc_send(pid, &message)) {
        LOG_ERROR(KERNEL_TEST_EXECUTION_TAG,
                  "fase=execution-ipc tipo invalido aceito");
        LOG_ERROR_CODE(KERNEL_TEST_EXECUTION_TAG, ERR_STATE,
                       "fase=execution-ipc tipo invalido aceito");
        return ERR_STATE;
    }
    return OK;
}

static int execution_check_signal(void) {
    process_signal_self_test_t result;

    if (process_signal_self_test(&result) != OK || !result.lifecycle ||
        !result.actions || !result.blocking || !result.coalescing ||
        !result.fatal_rules || !result.child_notification ||
        !result.frame_rules || !result.invariants ||
        process_signal_validate_state() != OK) {
        LOG_ERROR(KERNEL_TEST_EXECUTION_TAG,
                  "fase=execution-signals resultado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_EXECUTION_TAG, ERR_STATE,
                       "fase=execution-signals resultado invalido");
        return ERR_STATE;
    }
    return OK;
}

static int execution_check_wait(void) {
    wait_self_test_result_t result;

    if (wait_self_test(&result) != OK || result.failed != 0U ||
        !result.channel_lifecycle || !result.condition_signal ||
        !result.availability || !result.accounting || !result.reasons ||
        !result.limits || !result.reset || !result.fifo ||
        !result.wake_all || !result.lost_wakeup ||
        !result.condition_recheck || !result.process_thread ||
        !result.interrupt_context || !result.invariants ||
        wait_validate_state() != OK) {
        LOG_ERROR(KERNEL_TEST_EXECUTION_TAG,
                  "fase=execution-wait resultado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_EXECUTION_TAG, ERR_STATE,
                       "fase=execution-wait resultado invalido");
        return ERR_STATE;
    }
    return OK;
}

static int execution_check_workqueue(void) {
    workqueue_self_test_result_t result;

    if (workqueue_self_test(&result) != OK || result.failed != 0U ||
        !result.lifecycle || !result.fifo || !result.priority ||
        !result.delayed || !result.rollover || !result.promotion ||
        !result.coalescing || !result.rerun || !result.cancellation ||
        !result.capacity || !result.interrupt_context ||
        !result.invariants || workqueue_validate_state() != OK) {
        LOG_ERROR(KERNEL_TEST_EXECUTION_TAG,
                  "fase=execution-workqueue resultado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_EXECUTION_TAG, ERR_STATE,
                       "fase=execution-workqueue resultado invalido");
        return ERR_STATE;
    }
    return OK;
}

static int execution_check_threads(void) {
    if (thread_get_count() != 0U || thread_run_self_test() != OK ||
        thread_get_count() != 0U) {
        LOG_ERROR(KERNEL_TEST_EXECUTION_TAG,
                  "fase=execution-threads estado residual");
        LOG_ERROR_CODE(KERNEL_TEST_EXECUTION_TAG, ERR_STATE,
                       "fase=execution-threads estado residual");
        return ERR_STATE;
    }
    return OK;
}

static int execution_check_processes(void) {
    process_stack_validation_t stacks;
    scheduler_validation_t scheduler;

    if (process_stack_self_test() != OK ||
        process_stack_validate_all(&stacks) != OK || stacks.corrupted != 0U ||
        scheduler_validate_invariants(&scheduler) != OK ||
        !scheduler.current_valid || !scheduler.idle_valid ||
        !scheduler.pid_table_valid || !scheduler.state_table_valid ||
        !scheduler.stack_table_valid || !scheduler.slab_table_valid ||
        !scheduler.idle_accounting_valid) {
        LOG_ERROR(KERNEL_TEST_EXECUTION_TAG,
                  "fase=execution-processes invariantes invalidas");
        LOG_ERROR_CODE(KERNEL_TEST_EXECUTION_TAG, ERR_STATE,
                       "fase=execution-processes invariantes invalidas");
        return ERR_STATE;
    }
    return OK;
}

int kernel_tests_run_execution(const kernel_tests_runtime_t* runtime) {
    uint32_t process_count_before = process_get_count();
    uint32_t user_count_before = process_get_user_count();
    uint32_t zombie_count_before =
        process_get_state_count(PROCESS_STATE_ZOMBIE);
    int result;

    LOG_INFO(KERNEL_TEST_EXECUTION_TAG,
             "fase=execution-preconditions inicio");
    if (!ipc_is_ready() || !thread_is_ready() || process_get_current() == 0) {
        return kernel_tests_phase_result("fase=execution-preconditions",
                                         ERR_STATE);
    }
    result = kernel_tests_progress(runtime);
    if (result != OK) return result;

    result = execution_check_threads();
    if (result != OK) {
        return kernel_tests_phase_result("fase=execution-threads", result);
    }
    result = execution_check_processes();
    if (result != OK) {
        return kernel_tests_phase_result("fase=execution-processes", result);
    }
    result = execution_check_signal();
    if (result != OK) {
        return kernel_tests_phase_result("fase=execution-signals", result);
    }
    result = execution_check_wait();
    if (result != OK) {
        return kernel_tests_phase_result("fase=execution-wait", result);
    }
    result = execution_check_workqueue();
    if (result != OK) {
        return kernel_tests_phase_result("fase=execution-workqueue", result);
    }
    result = execution_check_ipc();
    if (result != OK) {
        return kernel_tests_phase_result("fase=execution-ipc", result);
    }
    result = kernel_tests_progress(runtime);
    if (result != OK) return result;
    if (process_get_count() != process_count_before ||
        process_get_user_count() != user_count_before ||
        process_get_state_count(PROCESS_STATE_ZOMBIE) != zombie_count_before ||
        thread_get_count() != 0U || ipc_get_pending_count() != 0U ||
        wait_validate_state() != OK || workqueue_validate_state() != OK) {
        return kernel_tests_phase_result("fase=execution-postconditions",
                                         ERR_STATE);
    }
    return kernel_tests_phase_result("fase=execution-postconditions", OK);
}
