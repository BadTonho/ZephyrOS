#ifndef KERNEL_TESTS_H
#define KERNEL_TESTS_H

#include "core/errors.h"

typedef int (*kernel_tests_progress_fn)(void* context);

typedef struct {
    kernel_tests_progress_fn progress;
    void* context;
} kernel_tests_runtime_t;

extern const kernel_tests_runtime_t* kernel_tests_active_runtime;

int kernel_tests_run_memory_slab(void);
int kernel_tests_run_memory_slab_with_runtime(
    const kernel_tests_runtime_t* runtime);
int kernel_tests_run_paging_vma(const kernel_tests_runtime_t* runtime);
int kernel_tests_run_execution(const kernel_tests_runtime_t* runtime);
int kernel_tests_run_storage_vfs(const kernel_tests_runtime_t* runtime);
int kernel_tests_run_network(const kernel_tests_runtime_t* runtime);
int kernel_tests_run_platform(const kernel_tests_runtime_t* runtime);
int kernel_tests_progress(const kernel_tests_runtime_t* runtime);
int kernel_tests_phase_result(const char* phase, int result);

#endif
