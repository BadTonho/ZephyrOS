#include "kernel_tests.h"

#include "core/log.h"
#include "core/memory.h"
#include "fs/block.h"
#include "fs/block_cache.h"
#include "fs/devfs.h"
#include "fs/file_index.h"
#include "fs/procfs.h"
#include "fs/storage.h"
#include "fs/sysfs.h"
#include "fs/vfs.h"

#define KERNEL_TEST_STORAGE_TAG "TST4"

static int storage_check_heap(const char* phase) {
    memory_heap_stats_t stats;

    memory_get_heap_stats(&stats);
    if (stats.valid) return OK;
    LOG_ERROR(KERNEL_TEST_STORAGE_TAG, phase);
    LOG_ERROR_CODE(KERNEL_TEST_STORAGE_TAG, ERR_STATE, phase);
    return ERR_STATE;
}

static int storage_check_devfs(void) {
    devfs_test_result_t result;

    if (devfs_self_test(&result) != OK ||
        !result.registry || !result.null_device || !result.zero_device ||
        !result.tty_device || !result.speaker_device ||
        !result.block_device || !result.permissions || !result.cleanup ||
        !result.invariants || result.passed != result.total) {
        LOG_ERROR(KERNEL_TEST_STORAGE_TAG,
                  "fase=storage-devfs resultado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_STORAGE_TAG, ERR_STATE,
                       "fase=storage-devfs resultado invalido");
        return ERR_STATE;
    }
    return OK;
}

static int storage_check_procfs(void) {
    procfs_test_result_t result;

    if (procfs_self_test(&result) != OK ||
        !result.registry || !result.lookup || !result.listing ||
        !result.read || !result.permissions || !result.seek_eof ||
        !result.callback_errors || !result.cleanup || !result.invariants ||
        !result.global_nodes || !result.process_listing ||
        !result.process_nodes || !result.format || !result.generation ||
        !result.control_nodes || !result.control_read ||
        !result.control_write || !result.control_rollback ||
        !result.control_privilege || !result.control_reset ||
        result.passed != result.total) {
        LOG_ERROR(KERNEL_TEST_STORAGE_TAG,
                  "fase=storage-procfs resultado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_STORAGE_TAG, ERR_STATE,
                       "fase=storage-procfs resultado invalido");
        return ERR_STATE;
    }
    return OK;
}

static int storage_check_sysfs_flag(uint8_t value, const char* phase) {
    if (value) return OK;
    LOG_ERROR(KERNEL_TEST_STORAGE_TAG, phase);
    LOG_ERROR_CODE(KERNEL_TEST_STORAGE_TAG, ERR_STATE, phase);
    return ERR_STATE;
}

static int storage_check_equal(uint32_t actual, uint32_t expected,
                               const char* phase) {
    if (actual == expected) return OK;
    LOG_ERROR(KERNEL_TEST_STORAGE_TAG, phase);
    LOG_ERROR_CODE(KERNEL_TEST_STORAGE_TAG, ERR_STATE, phase);
    return ERR_STATE;
}

static int storage_check_sysfs(void) {
    sysfs_test_result_t result;
    int code;

    code = sysfs_self_test(&result);
    if (code != OK) {
        LOG_ERROR_CODE(KERNEL_TEST_STORAGE_TAG, code,
                       "fase=storage-sysfs autoteste falhou");
        (void)storage_check_sysfs_flag(result.registry,
                                       "fase=storage-sysfs registry");
        (void)storage_check_sysfs_flag(result.lookup,
                                       "fase=storage-sysfs lookup");
        (void)storage_check_sysfs_flag(result.listing,
                                       "fase=storage-sysfs listing");
        (void)storage_check_sysfs_flag(result.read,
                                       "fase=storage-sysfs read");
        (void)storage_check_sysfs_flag(result.permissions,
                                       "fase=storage-sysfs permissions");
        (void)storage_check_sysfs_flag(result.seek_eof,
                                       "fase=storage-sysfs seek-eof");
        (void)storage_check_sysfs_flag(result.format,
                                       "fase=storage-sysfs format");
        (void)storage_check_sysfs_flag(result.cleanup,
                                       "fase=storage-sysfs cleanup");
        (void)storage_check_sysfs_flag(result.inventories,
                                       "fase=storage-sysfs inventories");
        (void)storage_check_sysfs_flag(result.power,
                                       "fase=storage-sysfs power");
        if (result.passed != result.total) {
            LOG_ERROR(KERNEL_TEST_STORAGE_TAG,
                      "fase=storage-sysfs contador inconsistente");
        }
        return code;
    }
    if (storage_check_sysfs_flag(result.registry,
                                 "fase=storage-sysfs registry") != OK) {
        return ERR_STATE;
    }
    if (storage_check_sysfs_flag(result.lookup,
                                 "fase=storage-sysfs lookup") != OK) {
        return ERR_STATE;
    }
    if (storage_check_sysfs_flag(result.listing,
                                 "fase=storage-sysfs listing") != OK) {
        return ERR_STATE;
    }
    if (storage_check_sysfs_flag(result.read,
                                 "fase=storage-sysfs read") != OK) {
        return ERR_STATE;
    }
    if (storage_check_sysfs_flag(result.permissions,
                                 "fase=storage-sysfs permissions") != OK) {
        return ERR_STATE;
    }
    if (storage_check_sysfs_flag(result.seek_eof,
                                 "fase=storage-sysfs seek-eof") != OK) {
        return ERR_STATE;
    }
    if (storage_check_sysfs_flag(result.format,
                                 "fase=storage-sysfs format") != OK) {
        return ERR_STATE;
    }
    if (storage_check_sysfs_flag(result.cleanup,
                                 "fase=storage-sysfs cleanup") != OK) {
        return ERR_STATE;
    }
    if (storage_check_sysfs_flag(result.inventories,
                                 "fase=storage-sysfs inventories") != OK) {
        return ERR_STATE;
    }
    if (storage_check_sysfs_flag(result.power,
                                 "fase=storage-sysfs power") != OK) {
        return ERR_STATE;
    }
    if (result.passed != result.total) {
        LOG_ERROR(KERNEL_TEST_STORAGE_TAG,
                  "fase=storage-sysfs contador inconsistente");
        LOG_ERROR_CODE(KERNEL_TEST_STORAGE_TAG, ERR_STATE,
                       "fase=storage-sysfs contador inconsistente");
        return ERR_STATE;
    }
    return OK;
}

static int storage_check_vfs(void) {
    vfs_test_result_t result;

    if (vfs_self_test(&result) != OK ||
        !result.stdio || !result.lifecycle || !result.sequential_read ||
        !result.seek || !result.permissions || !result.eof ||
        !result.limits || !result.table_capacity ||
        !result.closed_descriptor || !result.isolation || !result.cleanup ||
        !result.normalization || !result.lookup || !result.cwd ||
        !result.mount_busy || !result.invariants || !result.directory_listing ||
        !result.devices || !result.ioctl || !result.pipes || !result.procfs ||
        !result.sysfs || result.passed != result.total) {
        LOG_ERROR(KERNEL_TEST_STORAGE_TAG,
                  "fase=storage-vfs resultado invalido");
        LOG_ERROR_CODE(KERNEL_TEST_STORAGE_TAG, ERR_STATE,
                       "fase=storage-vfs resultado invalido");
        return ERR_STATE;
    }
    return OK;
}

static int storage_check_inventory(const storage_status_t* before,
                                   const vfs_status_t* vfs_before,
                                   const block_queue_stats_t* block_before,
                                   const block_cache_stats_t* cache_before,
                                   const file_index_status_t* index_before) {
    storage_status_t storage_after;
    vfs_status_t vfs_after;
    block_queue_stats_t block_after;
    block_cache_stats_t cache_after;
    file_index_status_t index_after;
    int result;

    result = storage_get_status(&storage_after);
    if (result != OK) {
        LOG_ERROR_CODE(KERNEL_TEST_STORAGE_TAG, result,
                       "fase=storage-status apos teste");
        return result;
    }
    result = vfs_get_status(&vfs_after);
    if (result != OK) {
        LOG_ERROR_CODE(KERNEL_TEST_STORAGE_TAG, result,
                       "fase=vfs-status apos teste");
        return result;
    }
    result = block_get_stats(&block_after);
    if (result != OK) {
        LOG_ERROR_CODE(KERNEL_TEST_STORAGE_TAG, result,
                       "fase=block-status apos teste");
        return result;
    }
    result = block_cache_get_stats(&cache_after);
    if (result != OK) {
        LOG_ERROR_CODE(KERNEL_TEST_STORAGE_TAG, result,
                       "fase=cache-status apos teste");
        return result;
    }
    result = file_index_get_status(&index_after);
    if (result != OK) {
        LOG_ERROR_CODE(KERNEL_TEST_STORAGE_TAG, result,
                       "fase=file-index-status apos teste");
        return result;
    }
    if (storage_check_equal(storage_after.initialized, before->initialized,
                            "fase=storage-inventory storage initialized") != OK ||
        storage_check_equal(storage_after.disk_count, before->disk_count,
                            "fase=storage-inventory disk count") != OK ||
        storage_check_equal(storage_after.volume_count, before->volume_count,
                            "fase=storage-inventory volume count") != OK ||
        storage_check_equal(storage_after.mounted_count,
                            before->mounted_count,
                            "fase=storage-inventory mounted count") != OK ||
        storage_check_equal(vfs_after.global_files_used,
                            vfs_before->global_files_used,
                            "fase=storage-inventory global files") != OK ||
        storage_check_equal(vfs_after.descriptors_open,
                            vfs_before->descriptors_open,
                            "fase=storage-inventory descriptors") != OK ||
        storage_check_equal(vfs_after.mounts_active, vfs_before->mounts_active,
                            "fase=storage-inventory mounts") != OK ||
        storage_check_equal(vfs_after.devices_active,
                            vfs_before->devices_active,
                            "fase=storage-inventory devices") != OK ||
        storage_check_equal(vfs_after.pipes_active, vfs_before->pipes_active,
                            "fase=storage-inventory pipes") != OK ||
        storage_check_equal(block_after.queue_depth, block_before->queue_depth,
                            "fase=storage-inventory block queue") != OK ||
        storage_check_equal(block_after.in_flight, block_before->in_flight,
                            "fase=storage-inventory block in-flight") != OK ||
        storage_check_equal(cache_after.reading_entries,
                            cache_before->reading_entries,
                            "fase=storage-inventory cache reading") != OK ||
        storage_check_equal(cache_after.dirty_entries,
                            cache_before->dirty_entries,
                            "fase=storage-inventory cache dirty") != OK ||
        storage_check_equal(cache_after.writeback_entries,
                            cache_before->writeback_entries,
                            "fase=storage-inventory cache writeback") != OK ||
        storage_check_equal(cache_after.pinned_entries,
                            cache_before->pinned_entries,
                            "fase=storage-inventory cache pinned") != OK ||
        storage_check_equal(index_after.state, index_before->state,
                            "fase=storage-inventory index state") != OK ||
        storage_check_equal(index_after.active_entries,
                            index_before->active_entries,
                            "fase=storage-inventory index entries") != OK ||
        storage_check_equal(index_after.source_count, index_before->source_count,
                            "fase=storage-inventory index sources") != OK ||
        storage_check_equal(index_after.partial, index_before->partial,
                            "fase=storage-inventory index partial") != OK ||
        storage_check_equal(index_after.stale, index_before->stale,
                            "fase=storage-inventory index stale") != OK ||
        vfs_validate_state() != OK || block_validate_state() != OK ||
        block_cache_validate_state() != OK || file_index_validate_state() != OK) {
        LOG_ERROR(KERNEL_TEST_STORAGE_TAG,
                  "fase=storage-inventory estado residual");
        LOG_ERROR_CODE(KERNEL_TEST_STORAGE_TAG, ERR_STATE,
                       "fase=storage-inventory estado residual");
        return ERR_STATE;
    }
    return OK;
}

int kernel_tests_run_storage_vfs(const kernel_tests_runtime_t* runtime) {
    storage_status_t storage_before;
    vfs_status_t vfs_before;
    block_queue_stats_t block_before;
    block_cache_stats_t cache_before;
    file_index_status_t index_before;
    int result;

    LOG_INFO(KERNEL_TEST_STORAGE_TAG,
             "fase=storage-vfs-preconditions inicio");
    if (!vfs_is_ready() || storage_get_status(&storage_before) != OK ||
        vfs_get_status(&vfs_before) != OK ||
        block_get_stats(&block_before) != OK ||
        block_cache_get_stats(&cache_before) != OK ||
        file_index_get_status(&index_before) != OK) {
        return kernel_tests_phase_result("fase=storage-vfs-preconditions",
                                         ERR_STATE);
    }
    result = kernel_tests_progress(runtime);
    if (result != OK) return result;
    LOG_INFO(KERNEL_TEST_STORAGE_TAG, "fase=storage-block inicio");
    result = block_validate_state();
    if (result == OK) result = block_self_test();
    if (result != OK) {
        return kernel_tests_phase_result("fase=storage-block", result);
    }
    result = storage_check_heap("fase=storage-block heap invalido");
    if (result != OK) return result;
    result = block_cache_validate_state();
    if (result != OK) {
        return kernel_tests_phase_result("fase=storage-block-cache", result);
    }
    LOG_INFO(KERNEL_TEST_STORAGE_TAG, "fase=storage-file-index inicio");
    result = file_index_validate_state();
    if (result == OK) result = file_index_self_test();
    if (result != OK) {
        return kernel_tests_phase_result("fase=storage-file-index", result);
    }
    result = storage_check_heap("fase=storage-file-index heap invalido");
    if (result != OK) return result;
    LOG_INFO(KERNEL_TEST_STORAGE_TAG, "fase=storage-devfs inicio");
    result = storage_check_devfs();
    if (result != OK) {
        return kernel_tests_phase_result("fase=storage-devfs", result);
    }
    result = storage_check_heap("fase=storage-devfs heap invalido");
    if (result != OK) return result;
    result = kernel_tests_progress(runtime);
    if (result != OK) return result;
    LOG_INFO(KERNEL_TEST_STORAGE_TAG, "fase=storage-procfs inicio");
    result = storage_check_procfs();
    if (result != OK) {
        return kernel_tests_phase_result("fase=storage-procfs", result);
    }
    result = kernel_tests_progress(runtime);
    if (result != OK) return result;
    LOG_INFO(KERNEL_TEST_STORAGE_TAG, "fase=storage-sysfs inicio");
    result = storage_check_sysfs();
    if (result != OK) {
        return kernel_tests_phase_result("fase=storage-sysfs", result);
    }
    result = kernel_tests_progress(runtime);
    if (result != OK) return result;
    LOG_INFO(KERNEL_TEST_STORAGE_TAG, "fase=storage-vfs inicio");
    result = storage_check_vfs();
    if (result != OK) {
        return kernel_tests_phase_result("fase=storage-vfs", result);
    }
    result = kernel_tests_progress(runtime);
    if (result != OK) return result;
    result = block_cache_clear();
    if (result != OK) {
        return kernel_tests_phase_result("fase=storage-cache-cleanup", result);
    }
    result = storage_check_inventory(&storage_before, &vfs_before,
                                     &block_before, &cache_before,
                                     &index_before);
    if (result != OK) {
        return kernel_tests_phase_result("fase=storage-vfs-postconditions",
                                         result);
    }
    return kernel_tests_phase_result("fase=storage-vfs-postconditions", OK);
}
