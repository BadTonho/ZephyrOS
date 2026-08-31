#include "kernel_tests.h"

#include "core/log.h"
#include "core/memory.h"
#include "memory/paging.h"
#include "memory/slab.h"

#define KERNEL_TEST_TAG "TST4"

const kernel_tests_runtime_t* kernel_tests_active_runtime;

int kernel_tests_progress(const kernel_tests_runtime_t* runtime) {
    if (!runtime || !runtime->progress) return OK;
    return runtime->progress(runtime->context);
}

int kernel_tests_phase_result(const char* phase, int result) {
    if (!phase) {
        LOG_ERROR(KERNEL_TEST_TAG, "fase nula");
        LOG_ERROR_CODE(KERNEL_TEST_TAG, ERR_NULL, "fase nula");
        return ERR_NULL;
    }
    if (result != OK) {
        LOG_ERROR_CODE(KERNEL_TEST_TAG, result, phase);
    } else {
        LOG_INFO(KERNEL_TEST_TAG, phase);
    }
    return result;
}

int kernel_tests_report_phase(const kernel_tests_runtime_t* runtime,
                              const char* phase, int result) {
    int reported = kernel_tests_phase_result(phase, result);

    if (runtime && runtime->report_phase) {
        runtime->report_phase(runtime->context, phase, reported);
    }
    return reported;
}

typedef struct {
    memory_heap_stats_t heap;
    memory_pmm_stats_t pmm;
    memory_detailed_stats_t detailed;
    kmem_slab_stats_t slab;
} kernel_tests_memory_snapshot_t;

static int kernel_tests_check_snapshot(
    const kernel_tests_memory_snapshot_t* snapshot) {
    uint32_t zone_sum = 0U;

    if (!snapshot || !snapshot->heap.initialized || !snapshot->heap.valid ||
        !snapshot->pmm.initialized || !snapshot->detailed.initialized ||
        !snapshot->detailed.valid || !snapshot->slab.initialized ||
        !snapshot->slab.valid) {
        LOG_ERROR(KERNEL_TEST_TAG, "fase=memory-snapshot estado invalido");
        return ERR_STATE;
    }
    if (snapshot->heap.free_bytes > snapshot->heap.total_bytes ||
        snapshot->heap.used_bytes >
            snapshot->heap.total_bytes - snapshot->heap.free_bytes ||
        snapshot->heap.used_bytes + snapshot->heap.free_bytes !=
            snapshot->heap.total_bytes ||
        snapshot->heap.largest_free_block > snapshot->heap.free_bytes ||
        snapshot->heap.fragmentation_percent > 100U) {
        LOG_ERROR(KERNEL_TEST_TAG, "fase=heap invariante invalida");
        return ERR_STATE;
    }
    for (memory_zone_t zone = MEMORY_ZONE_KERNEL;
         zone < MEMORY_ZONE_COUNT; zone++) {
        zone_sum += snapshot->detailed.zone_pages[zone];
    }
    if (zone_sum != snapshot->detailed.total_pages) {
        LOG_ERROR(KERNEL_TEST_TAG, "fase=pmm soma-das-zonas invalida");
        return ERR_STATE;
    }
    if (snapshot->detailed.zone_pages[MEMORY_ZONE_FREE] !=
        memory_get_free_pages()) {
        LOG_ERROR(KERNEL_TEST_TAG, "fase=pmm paginas-livres divergentes");
        return ERR_STATE;
    }
    if (snapshot->detailed.largest_free_run >
            snapshot->detailed.zone_pages[MEMORY_ZONE_FREE] ||
        snapshot->detailed.free_runs > snapshot->detailed.total_pages ||
        snapshot->detailed.fragmentation_percent > 100U) {
        LOG_ERROR(KERNEL_TEST_TAG, "fase=pmm fragmentacao invalida");
        return ERR_STATE;
    }
    if (snapshot->pmm.owned_pages > snapshot->detailed.total_pages -
            snapshot->detailed.zone_pages[MEMORY_ZONE_FREE] ||
        snapshot->slab.caches > KMEM_CACHE_MAX ||
        snapshot->slab.slabs > KMEM_SLAB_MAX ||
        snapshot->slab.active_objects > snapshot->slab.capacity) {
        LOG_ERROR(KERNEL_TEST_TAG, "fase=slab invariante invalida");
        return ERR_STATE;
    }
    return OK;
}

static int kernel_tests_capture_memory(
    kernel_tests_memory_snapshot_t* snapshot) {
    int result;

    if (!snapshot) {
        LOG_ERROR(KERNEL_TEST_TAG, "fase=memory-snapshot destino nulo");
        return ERR_NULL;
    }
    memory_get_heap_stats(&snapshot->heap);
    memory_get_pmm_stats(&snapshot->pmm);
    result = memory_get_detailed_stats(&snapshot->detailed);
    kmem_cache_get_stats(&snapshot->slab);
    if (result != OK) {
        LOG_ERROR(KERNEL_TEST_TAG, "fase=memory-detailed consulta falhou");
        return result;
    }
    return kernel_tests_check_snapshot(snapshot);
}

static int kernel_tests_same_memory_state(
    const kernel_tests_memory_snapshot_t* before,
    const kernel_tests_memory_snapshot_t* after) {
    if (!before || !after) {
        LOG_ERROR(KERNEL_TEST_TAG, "fase=postconditions snapshot nulo");
        return ERR_NULL;
    }
    if (before->heap.total_bytes != after->heap.total_bytes ||
        before->heap.used_bytes != after->heap.used_bytes ||
        before->heap.free_bytes != after->heap.free_bytes ||
        before->heap.allocated_blocks != after->heap.allocated_blocks ||
        before->heap.free_blocks != after->heap.free_blocks ||
        before->heap.largest_free_block != after->heap.largest_free_block ||
        before->heap.fragmentation_percent !=
            after->heap.fragmentation_percent ||
        before->heap.allocation_failures != after->heap.allocation_failures ||
        before->heap.invalid_frees != after->heap.invalid_frees ||
        before->heap.double_frees != after->heap.double_frees ||
        before->pmm.owned_pages != after->pmm.owned_pages ||
        before->pmm.allocation_failures != after->pmm.allocation_failures ||
        before->pmm.invalid_frees != after->pmm.invalid_frees ||
        before->detailed.total_pages != after->detailed.total_pages ||
        before->detailed.free_runs != after->detailed.free_runs ||
        before->detailed.largest_free_run != after->detailed.largest_free_run ||
        before->detailed.isolated_free_pages !=
            after->detailed.isolated_free_pages ||
        before->detailed.fragmentation_percent !=
            after->detailed.fragmentation_percent ||
        before->slab.caches != after->slab.caches ||
        before->slab.slabs != after->slab.slabs ||
        before->slab.pages != after->slab.pages ||
        before->slab.active_objects != after->slab.active_objects ||
        before->slab.capacity != after->slab.capacity ||
        before->slab.allocation_failures !=
            after->slab.allocation_failures ||
        before->slab.invalid_frees != after->slab.invalid_frees ||
        before->slab.double_frees != after->slab.double_frees) {
        LOG_ERROR(KERNEL_TEST_TAG, "fase=postconditions estado alterado");
        return ERR_STATE;
    }
    for (memory_zone_t zone = MEMORY_ZONE_KERNEL;
         zone < MEMORY_ZONE_COUNT; zone++) {
        if (before->detailed.zone_pages[zone] !=
            after->detailed.zone_pages[zone]) {
            LOG_ERROR(KERNEL_TEST_TAG, "fase=postconditions zonas alteradas");
            return ERR_STATE;
        }
    }
    return OK;
}

int kernel_tests_run_memory_slab_with_runtime(
    const kernel_tests_runtime_t* runtime) {
    kernel_tests_memory_snapshot_t before;
    kernel_tests_memory_snapshot_t after;
    int result;

    LOG_INFO(KERNEL_TEST_TAG, "fase=preconditions inicio");
    if (!paging_is_ready()) {
        LOG_ERROR(KERNEL_TEST_TAG, "fase=preconditions codigo=ERR_STATE");
        return ERR_STATE;
    }
    result = kernel_tests_progress(runtime);
    if (result != OK) return result;
    result = kernel_tests_capture_memory(&before);
    if (result != OK) {
        LOG_ERROR_CODE(KERNEL_TEST_TAG, result, "fase=memory-before falhou");
        return result;
    }
    LOG_INFO(KERNEL_TEST_TAG, "fase=memory-before PASS");

    result = kmem_cache_validate();
    if (result != OK) {
        LOG_ERROR_CODE(KERNEL_TEST_TAG, result, "fase=slab-before falhou");
        return result;
    }
    LOG_INFO(KERNEL_TEST_TAG, "fase=slab-self-test inicio");
    kernel_tests_active_runtime = runtime;
    result = kmem_cache_self_test();
    kernel_tests_active_runtime = 0;
    if (result != OK) {
        LOG_ERROR_CODE(KERNEL_TEST_TAG, result, "fase=slab-self-test falhou");
        return result;
    }
    LOG_INFO(KERNEL_TEST_TAG, "fase=slab-self-test PASS");
    result = kernel_tests_progress(runtime);
    if (result != OK) return result;

    result = kernel_tests_capture_memory(&after);
    if (result != OK) {
        LOG_ERROR_CODE(KERNEL_TEST_TAG, result, "fase=memory-after falhou");
        return result;
    }
    result = kmem_cache_validate();
    if (result != OK) {
        LOG_ERROR_CODE(KERNEL_TEST_TAG, result, "fase=slab-after falhou");
        return result;
    }
    if (kernel_tests_same_memory_state(&before, &after) != OK) {
        LOG_ERROR_CODE(KERNEL_TEST_TAG, ERR_STATE,
                       "fase=postconditions falhou");
        return ERR_STATE;
    }
    LOG_INFO(KERNEL_TEST_TAG, "fase=postconditions PASS");
    return OK;
}

int kernel_tests_run_memory_slab(void) {
    return kernel_tests_run_memory_slab_with_runtime(0);
}
