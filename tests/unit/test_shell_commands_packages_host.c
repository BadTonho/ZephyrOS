#include <stdint.h>
#include <stdio.h>

#include "apps/shell.h"
#include "apps/shell_job.h"
#include "apps/shell_runtime.h"
#include "core/app_catalog.h"
#include "core/app_loader.h"
#include "core/app_package.h"
#include "core/app_remote.h"
#include "core/errors.h"
#include "core/keyboard.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/recovery.h"
#include "core/string.h"
#include "core/update.h"
#include "core/update_remote.h"
#include "core/update_remote_runtime.h"
#include "core/update_remote_system.h"
#include "core/update_runtime.h"
#include "core/update_system.h"
#include "core/update_system_slots.h"
#include "core/video.h"
#include "fs/fs.h"
#include "process/process.h"

#define HOST_COVERAGE_CAPACITY 1024U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

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

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:shell:commands-packages|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:commands-packages|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:commands-packages|value=0x%08X\n",
           (uint32_t)result);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void video_print(const char* str, uint8_t color) {
    (void)str;
    (void)color;
}

void keyboard_process_events(void) {}

int ipc_receive(ipc_msg_t* msg) {
    (void)msg;
    return 0;
}

uint8_t fs_get_type(void) {
    return FS_TYPE_FAT32;
}

static recovery_component_t fixture_recovery = {
    "app-store", RECOVERY_STATE_READY, 0U, OK, "ready"
};

const recovery_component_t* recovery_get(recovery_component_id_t component) {
    (void)component;
    return &fixture_recovery;
}

const char* recovery_state_name(recovery_state_t state) {
    return state == RECOVERY_STATE_READY ? "READY" : "UNKNOWN";
}

int recovery_mark_degraded(recovery_component_id_t component, int error_code,
                           const char* message) {
    (void)component;
    (void)error_code;
    (void)message;
    return OK;
}

int recovery_mark_ready(recovery_component_id_t component) {
    (void)component;
    return OK;
}

uint8_t shell_diagnostics_health_state_color(recovery_state_t state) {
    (void)state;
    return 0x0AU;
}

void shell_hosted_present_progress(void) {}

void shell_handle_app_request(uint32_t request) {
    (void)request;
}

static uint8_t fixture_job_active;
static uint8_t fixture_job_cancelled;
static uint32_t fixture_job_generation = 7U;

int shell_job_is_active(void) {
    return fixture_job_active;
}

int shell_job_generation_matches(uint32_t generation) {
    (void)generation;
    return 1;
}

void shell_job_pump_events(void) {}

int shell_job_cancel_requested(void) {
    return fixture_job_cancelled;
}

void shell_job_set_phase(shell_job_context_t* context, const char* phase) {
    (void)context;
    (void)phase;
}

int shell_job_start(const shell_job_definition_t* definition,
                    const char* arguments) {
    (void)definition;
    (void)arguments;
    fixture_job_active = 1U;
    return OK;
}

uint32_t shell_job_get_generation(void) {
    return fixture_job_generation;
}

static void fixture_fill_version(update_version_t* version) {
    version->major = 1U;
    version->minor = 2U;
    version->patch = 3U;
}

static void fixture_fill_update_action(update_action_result_t* action) {
    kmemset(action, 0, sizeof(*action));
    action->reason = UPDATE_ACTION_IO;
    fixture_fill_version(&action->from_version);
    fixture_fill_version(&action->to_version);
    action->from_epoch = 10U;
    action->to_epoch = 11U;
    action->entry_count = 1U;
    action->completed_entries = 0U;
}

int update_verify_file(const char* path, update_verification_t* result_out) {
    (void)path;
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = ZUPD_REASON_FORMAT;
    result_out->entry_count = 1U;
    fixture_fill_version(&result_out->base_version);
    fixture_fill_version(&result_out->target_version);
    return ERR_INVALID;
}

int update_get_capabilities(update_capabilities_t* capabilities_out) {
    kmemset(capabilities_out, 0, sizeof(*capabilities_out));
    capabilities_out->verifier_ready = 1U;
    capabilities_out->local_file_available = 1U;
    capabilities_out->apply_available = 1U;
    capabilities_out->history_available = 1U;
    capabilities_out->persistent_state_ready = 1U;
    return OK;
}

int update_get_status(update_status_t* status_out) {
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->state_store = UPDATE_STORE_EMPTY;
    status_out->current_files = UPDATE_STORE_VALID;
    status_out->history_store = UPDATE_STORE_EMPTY;
    fixture_fill_version(&status_out->build_version);
    fixture_fill_version(&status_out->installed_version);
    status_out->capabilities.verifier_ready = 1U;
    status_out->capabilities.local_file_available = 1U;
    status_out->capabilities.apply_available = 1U;
    status_out->capabilities.history_available = 1U;
    return OK;
}

int update_get_history_count(uint32_t* count_out) {
    *count_out = 1U;
    return OK;
}

int update_get_history_entry(uint32_t newest_index,
                             update_history_entry_t* entry_out) {
    (void)newest_index;
    kmemset(entry_out, 0, sizeof(*entry_out));
    entry_out->sequence = 1U;
    entry_out->operation = UPDATE_HISTORY_OPERATION_APPLY;
    entry_out->outcome = UPDATE_HISTORY_OUTCOME_SUCCESS;
    entry_out->action_reason = UPDATE_ACTION_NONE;
    entry_out->entry_count = 1U;
    entry_out->completed_entries = 1U;
    return OK;
}

int update_apply_file(const char* path, const update_action_options_t* options,
                      update_action_result_t* result_out) {
    (void)path;
    (void)options;
    fixture_fill_update_action(result_out);
    return ERR_UNAVAILABLE;
}

int update_rollback(const update_action_options_t* options,
                    update_action_result_t* result_out) {
    (void)options;
    fixture_fill_update_action(result_out);
    result_out->reason = UPDATE_ACTION_NO_ROLLBACK;
    return ERR_UNAVAILABLE;
}

int update_test_fail_after(uint16_t completed_entries) {
    (void)completed_entries;
    return OK;
}

const char* update_action_reason_name(update_action_reason_t reason) {
    return reason == UPDATE_ACTION_NONE ? "NONE" : "IO";
}

const char* update_store_state_name(update_store_state_t state) {
    return state == UPDATE_STORE_EMPTY ? "EMPTY" : "VALID";
}

const char* update_history_operation_name(update_history_operation_t operation) {
    return operation == UPDATE_HISTORY_OPERATION_APPLY ? "APPLY" : "NONE";
}

const char* update_history_outcome_name(update_history_outcome_t outcome) {
    return outcome == UPDATE_HISTORY_OUTCOME_SUCCESS ? "SUCCESS" : "NONE";
}

const char* zupd_reason_name(zupd_reason_t reason) {
    return reason == ZUPD_REASON_FORMAT ? "FORMAT" : "NONE";
}

static void fixture_fill_remote_status(update_remote_status_t* status) {
    kmemset(status, 0, sizeof(*status));
    status->state = UPDATE_REMOTE_STATE_UNAVAILABLE;
    status->reason = UPDATE_REMOTE_REASON_NETWORK;
    status->cache_store = UPDATE_REMOTE_STORE_EMPTY;
    status->enabled = 1U;
    status->network_ready = 0U;
}

int update_remote_get_status(update_remote_status_t* status_out) {
    fixture_fill_remote_status(status_out);
    return OK;
}

int update_remote_enable(void) { return OK; }
int update_remote_disable(void) { return OK; }

static int fixture_remote_result(update_remote_result_t* result_out) {
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = UPDATE_REMOTE_REASON_NETWORK;
    result_out->http_status = 503U;
    return ERR_UNAVAILABLE;
}

int update_remote_check(const char* manifest_url,
                        const update_remote_options_t* options,
                        update_remote_result_t* result_out) {
    (void)manifest_url;
    (void)options;
    return fixture_remote_result(result_out);
}

int update_remote_fetch(const char* manifest_url,
                        const update_remote_options_t* options,
                        update_remote_result_t* result_out) {
    (void)manifest_url;
    (void)options;
    return fixture_remote_result(result_out);
}

int update_remote_release_check(const char* tag,
                                const update_remote_options_t* options,
                                update_remote_result_t* result_out) {
    (void)tag;
    (void)options;
    return fixture_remote_result(result_out);
}

int update_remote_release_fetch(const char* tag,
                                const update_remote_options_t* options,
                                update_remote_result_t* result_out) {
    (void)tag;
    (void)options;
    return fixture_remote_result(result_out);
}

int update_remote_clear(const update_remote_options_t* options,
                        update_remote_result_t* result_out) {
    (void)options;
    kmemset(result_out, 0, sizeof(*result_out));
    return OK;
}

const char* update_remote_state_name(update_remote_state_t state) {
    return state == UPDATE_REMOTE_STATE_UNAVAILABLE ? "UNAVAILABLE" : "READY";
}

const char* update_remote_reason_name(update_remote_reason_t reason) {
    return reason == UPDATE_REMOTE_REASON_NETWORK ? "NETWORK" : "NONE";
}

const char* update_remote_store_name(update_remote_store_t state) {
    return state == UPDATE_REMOTE_STORE_EMPTY ? "EMPTY" : "VALID";
}

int update_remote_get_cached_alias(char* alias_out, uint32_t capacity) {
    (void)alias_out;
    (void)capacity;
    return ERR_UNAVAILABLE;
}

int update_remote_capability_available(void) { return 0; }

static void fixture_fill_runtime_remote_result(
    update_remote_runtime_result_t* result_out) {
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = UPDATE_REMOTE_RUNTIME_REASON_NETWORK;
    result_out->http_status = 503U;
}

int update_remote_runtime_check(
    const char* tag, const update_remote_options_t* options,
    update_remote_runtime_result_t* result_out) {
    (void)tag;
    (void)options;
    fixture_fill_runtime_remote_result(result_out);
    return ERR_UNAVAILABLE;
}

int update_remote_runtime_fetch(
    const char* tag, update_remote_runtime_fetch_mode_t mode,
    const update_remote_options_t* options,
    update_remote_runtime_result_t* result_out) {
    (void)tag;
    (void)mode;
    (void)options;
    fixture_fill_runtime_remote_result(result_out);
    return ERR_UNAVAILABLE;
}

int update_remote_runtime_clear(const update_remote_options_t* options,
                                update_remote_runtime_result_t* result_out) {
    (void)options;
    fixture_fill_runtime_remote_result(result_out);
    return ERR_UNAVAILABLE;
}

int update_remote_runtime_get_status(
    update_remote_runtime_status_t* status_out) {
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->state = UPDATE_REMOTE_RUNTIME_STATE_UNAVAILABLE;
    status_out->reason = UPDATE_REMOTE_RUNTIME_REASON_NETWORK;
    return OK;
}

const char* update_remote_runtime_state_name(
    update_remote_runtime_state_t state) {
    return state == UPDATE_REMOTE_RUNTIME_STATE_UNAVAILABLE ?
           "UNAVAILABLE" : "READY";
}

const char* update_remote_runtime_reason_name(
    update_remote_runtime_reason_t reason) {
    return reason == UPDATE_REMOTE_RUNTIME_REASON_NETWORK ? "NETWORK" : "NONE";
}

int update_runtime_get_status(update_runtime_status_t* status_out) {
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->state_valid = 1U;
    status_out->capabilities.apply_available = 1U;
    status_out->capabilities.cache_available = 1U;
    return OK;
}

int update_runtime_get_cache(update_runtime_cache_t* cache_out) {
    kmemset(cache_out, 0, sizeof(*cache_out));
    return OK;
}

static int fixture_runtime_action(update_runtime_action_result_t* result_out) {
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = UPDATE_RUNTIME_REASON_IO;
    return ERR_UNAVAILABLE;
}

int update_runtime_apply_cached(
    const update_runtime_action_options_t* options,
    update_runtime_action_result_t* result_out) {
    (void)options;
    return fixture_runtime_action(result_out);
}

int update_runtime_rollback(
    const update_runtime_action_options_t* options,
    update_runtime_action_result_t* result_out) {
    (void)options;
    return fixture_runtime_action(result_out);
}

int update_runtime_verify_file(const char* path,
                               update_runtime_verification_t* result_out) {
    (void)path;
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = UPDATE_RUNTIME_REASON_FORMAT;
    return ERR_INVALID;
}

int update_runtime_verify_file_for_manifest(
    const char* path, const update_runtime_manifest_t* manifest,
    update_runtime_verification_t* result_out) {
    (void)manifest;
    return update_runtime_verify_file(path, result_out);
}

int update_runtime_test_fail_after(uint16_t completed_entries) {
    (void)completed_entries;
    return OK;
}

const char* update_runtime_reason_name(update_runtime_reason_t reason) {
    return reason == UPDATE_RUNTIME_REASON_FORMAT ? "FORMAT" : "IO";
}

int update_remote_system_fetch(
    const char* tag, const update_remote_options_t* options,
    update_remote_system_result_t* result_out) {
    (void)tag;
    (void)options;
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = UPDATE_REMOTE_SYSTEM_REASON_NETWORK;
    return ERR_UNAVAILABLE;
}

int update_remote_system_clear(
    const update_remote_options_t* options,
    update_remote_system_result_t* result_out) {
    (void)options;
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = UPDATE_REMOTE_SYSTEM_REASON_NETWORK;
    return ERR_UNAVAILABLE;
}

int update_remote_system_get_status(
    update_remote_system_status_t* status_out) {
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->state = UPDATE_REMOTE_SYSTEM_STATE_UNAVAILABLE;
    status_out->reason = UPDATE_REMOTE_SYSTEM_REASON_NETWORK;
    return OK;
}

int update_remote_system_get_cached_path(char* path_out, uint32_t capacity) {
    (void)path_out;
    (void)capacity;
    return ERR_UNAVAILABLE;
}

const char* update_remote_system_state_name(update_remote_system_state_t state) {
    return state == UPDATE_REMOTE_SYSTEM_STATE_UNAVAILABLE ?
           "UNAVAILABLE" : "READY";
}

const char* update_remote_system_reason_name(
    update_remote_system_reason_t reason) {
    return reason == UPDATE_REMOTE_SYSTEM_REASON_NETWORK ? "NETWORK" : "NONE";
}

int update_system_verify_file(const char* path,
                              update_system_verification_t* result_out) {
    (void)path;
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = UPDATE_SYSTEM_REASON_FORMAT;
    return ERR_INVALID;
}

int update_system_check_tag(const char* tag,
                            const update_remote_options_t* options,
                            update_system_verification_t* result_out) {
    (void)tag;
    (void)options;
    return update_system_verify_file(0, result_out);
}

const char* update_system_reason_name(update_system_reason_t reason) {
    return reason == UPDATE_SYSTEM_REASON_FORMAT ? "FORMAT" : "NONE";
}

int update_system_slots_get_status(update_system_slots_status_t* status_out) {
    kmemset(status_out, 0, sizeof(*status_out));
    return ERR_UNAVAILABLE;
}

int update_system_slots_stage_file(
    const char* path, const update_system_slots_action_options_t* options,
    update_system_slots_action_result_t* result_out) {
    (void)path;
    (void)options;
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_UNSUPPORTED;
    return ERR_UNAVAILABLE;
}

int update_system_slots_cancel_pending(
    const update_system_slots_action_options_t* options,
    update_system_slots_action_result_t* result_out) {
    (void)options;
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_UNSUPPORTED;
    return ERR_UNAVAILABLE;
}

const char* update_system_slots_state_name(update_system_slots_state_t state) {
    return state == UPDATE_SYSTEM_SLOTS_STATE_READY ? "READY" : "EMPTY";
}

const char* update_system_slots_boot_state_name(
    update_system_slots_boot_state_t state) {
    return state == UPDATE_SYSTEM_SLOTS_BOOT_ATTEMPTED ? "ATTEMPTED" : "NONE";
}

const char* update_system_slots_journal_phase_name(
    update_system_slots_journal_phase_t phase) {
    return phase == UPDATE_SYSTEM_SLOTS_JOURNAL_NONE ? "NONE" : "PREPARED";
}

const char* update_system_slot_file_state_name(
    update_system_slot_file_state_t state) {
    return state == UPDATE_SYSTEM_SLOT_FILE_VALID ? "VALID" : "EMPTY";
}

const char* update_system_slots_reason_name(
    update_system_slots_reason_t reason) {
    return reason == UPDATE_SYSTEM_SLOTS_REASON_UNSUPPORTED ?
           "UNSUPPORTED" : "NONE";
}

static void fixture_fill_package_info(app_package_info_t* info) {
    kmemset(info, 0, sizeof(*info));
    kmemcpy(info->id, "TESTAPP", 8U);
    kmemcpy(info->name, "Test App", 9U);
    kmemcpy(info->version, "1.0", 4U);
}

int app_package_get_installed_count(void) { return 1; }

int app_package_get_installed_info(int index, app_package_info_t* info_out) {
    (void)index;
    fixture_fill_package_info(info_out);
    return OK;
}

int app_package_get_installed_info_by_id(const char* id,
                                         app_package_info_t* info_out) {
    (void)id;
    fixture_fill_package_info(info_out);
    return OK;
}

int app_package_verify_file(const char* path, app_package_info_t* info_out) {
    (void)path;
    fixture_fill_package_info(info_out);
    return OK;
}

int app_package_install_file(const char* path, app_package_info_t* info_out) {
    (void)path;
    fixture_fill_package_info(info_out);
    return OK;
}

int app_package_remove(const char* id) {
    (void)id;
    return OK;
}

int app_package_run_diagnostics(app_package_diagnostic_t* diagnostic_out) {
    kmemset(diagnostic_out, 1, sizeof(*diagnostic_out));
    return OK;
}

int app_package_get_status(app_package_status_t* status_out) {
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->transaction_supported = 1U;
    status_out->rollback_available = 1U;
    status_out->history_available = 1U;
    status_out->rollback_count = 1U;
    kmemcpy(status_out->rollback_id, "TESTAPP", 8U);
    kmemcpy(status_out->rollback_version, "0.9", 4U);
    status_out->last_history.sequence = 1U;
    status_out->last_history.operation = APP_PACKAGE_HISTORY_OPERATION_UPDATE;
    kmemcpy(status_out->last_history.id, "TESTAPP", 8U);
    return OK;
}

int app_package_get_history_count(uint32_t* count_out) {
    *count_out = 1U;
    return OK;
}

int app_package_get_history_entry(uint32_t newest_index,
                                  app_package_history_entry_t* entry_out) {
    (void)newest_index;
    kmemset(entry_out, 0, sizeof(*entry_out));
    entry_out->sequence = 1U;
    entry_out->operation = APP_PACKAGE_HISTORY_OPERATION_UPDATE;
    entry_out->outcome = APP_PACKAGE_HISTORY_OUTCOME_SUCCESS;
    kmemcpy(entry_out->id, "TESTAPP", 8U);
    return OK;
}

int app_package_test_fail_after(uint16_t completed_files) {
    (void)completed_files;
    return OK;
}

int app_package_is_mutation_active(void) { return 0; }

int app_package_preflight_plan(const app_package_plan_t* plan,
                               app_package_action_result_t* result_out) {
    (void)plan;
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = APP_PACKAGE_ACTION_REASON_NONE;
    return OK;
}

int app_package_apply_plan_confirmed(const app_package_plan_t* plan,
                                     app_package_action_result_t* result_out) {
    return app_package_preflight_plan(plan, result_out);
}

int app_package_preflight_remove(const char* id,
                                 app_package_action_result_t* result_out) {
    (void)id;
    kmemset(result_out, 0, sizeof(*result_out));
    return OK;
}

int app_package_remove_confirmed(const char* id,
                                 app_package_action_result_t* result_out) {
    return app_package_preflight_remove(id, result_out);
}

int app_package_preflight_rollback(const char* id,
                                   app_package_action_result_t* result_out) {
    return app_package_preflight_remove(id, result_out);
}

int app_package_rollback_confirmed(const char* id,
                                   app_package_action_result_t* result_out) {
    return app_package_preflight_remove(id, result_out);
}

int app_package_run_installed(const char* id, const app_launch_info_t* launch,
                              uint32_t* pid_out,
                              app_package_action_result_t* result_out) {
    (void)id;
    (void)launch;
    *pid_out = 42U;
    kmemset(result_out, 0, sizeof(*result_out));
    return OK;
}

const char* app_package_action_reason_name(
    app_package_action_reason_t reason) {
    return reason == APP_PACKAGE_ACTION_REASON_NONE ? "NONE" : "ERROR";
}

const char* app_package_plan_action_name(app_package_plan_action_t action) {
    return action == APP_PACKAGE_PLAN_ACTION_UPDATE ? "UPDATE" : "INSTALL";
}

const char* app_package_history_operation_name(
    app_package_history_operation_t operation) {
    return operation == APP_PACKAGE_HISTORY_OPERATION_UPDATE ? "UPDATE" : "NONE";
}

const char* app_package_history_outcome_name(
    app_package_history_outcome_t outcome) {
    return outcome == APP_PACKAGE_HISTORY_OUTCOME_SUCCESS ? "SUCCESS" : "NONE";
}

static void fixture_fill_catalog_entry(app_catalog_entry_t* entry_out) {
    kmemset(entry_out, 0, sizeof(*entry_out));
    kmemcpy(entry_out->alias, "TESTAPP.ZPK", 12U);
    fixture_fill_package_info(&entry_out->source);
    fixture_fill_package_info(&entry_out->installed);
    entry_out->source_size = 128U;
    entry_out->state = APP_CATALOG_STATE_UPDATE_AVAILABLE;
    entry_out->has_source = 1U;
    entry_out->has_installed = 1U;
}

int app_catalog_refresh(void) { return OK; }

int app_catalog_get_status(app_catalog_status_t* status_out) {
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->source_count = 1U;
    status_out->valid_source_count = 1U;
    status_out->installed_count = 1U;
    status_out->entry_count = 1U;
    return OK;
}

int app_catalog_get_count(uint32_t* count_out) {
    *count_out = 1U;
    return OK;
}

int app_catalog_get_entry(uint32_t index, app_catalog_entry_t* entry_out) {
    (void)index;
    fixture_fill_catalog_entry(entry_out);
    return OK;
}

int app_catalog_find_entry(const char* id_or_alias,
                           app_catalog_entry_t* entry_out) {
    (void)id_or_alias;
    fixture_fill_catalog_entry(entry_out);
    return OK;
}

int app_catalog_build_install_plan(const char* id_or_alias,
                                   app_package_plan_t* plan_out) {
    (void)id_or_alias;
    kmemset(plan_out, 0, sizeof(*plan_out));
    plan_out->entry_count = 1U;
    plan_out->entries[0].action = APP_PACKAGE_PLAN_ACTION_INSTALL;
    kmemcpy(plan_out->entries[0].id, "TESTAPP", 8U);
    return OK;
}

int app_catalog_build_update_plan(const char* id_or_alias, int allow_downgrade,
                                  app_package_plan_t* plan_out) {
    (void)allow_downgrade;
    return app_catalog_build_install_plan(id_or_alias, plan_out);
}

const char* app_catalog_state_name(app_catalog_state_t state) {
    return state == APP_CATALOG_STATE_UPDATE_AVAILABLE ? "UPDATE" : "AVAILABLE";
}

const char* app_catalog_reason_name(app_catalog_reason_t reason) {
    return reason == APP_CATALOG_REASON_NONE ? "NONE" : "ERROR";
}

int app_remote_get_status(app_remote_status_t* status_out) {
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->state = APP_REMOTE_STATE_UNAVAILABLE;
    status_out->reason = APP_REMOTE_REASON_NETWORK;
    status_out->cache_state = APP_REMOTE_CACHE_EMPTY;
    status_out->enabled = 1U;
    return OK;
}

int app_remote_enable(void) { return OK; }
int app_remote_disable(void) { return OK; }
int app_remote_refresh_provenance(void) { return OK; }
int app_remote_is_provenance_available(void) { return 1; }
int app_remote_get_installed_trust(const char* id, const char* version) {
    (void)id;
    (void)version;
    return 1;
}

int app_remote_get_count(uint32_t* count_out) {
    *count_out = 1U;
    return OK;
}

int app_remote_get_entry(uint32_t index, app_remote_entry_t* entry_out) {
    (void)index;
    kmemset(entry_out, 0, sizeof(*entry_out));
    fixture_fill_package_info(&entry_out->info);
    entry_out->state = APP_REMOTE_ENTRY_AVAILABLE;
    entry_out->cached = 1U;
    return OK;
}

int app_remote_find_entry(const char* id, app_remote_entry_t* entry_out) {
    (void)id;
    return app_remote_get_entry(0U, entry_out);
}

static int fixture_remote_package_result(app_remote_result_t* result_out) {
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = APP_REMOTE_REASON_NETWORK;
    return ERR_UNAVAILABLE;
}

int app_remote_check(const char* catalog_url,
                     const app_remote_options_t* options,
                     app_remote_result_t* result_out) {
    (void)catalog_url;
    (void)options;
    return fixture_remote_package_result(result_out);
}

int app_remote_fetch(const char* id, const char* catalog_url, int confirmed,
                     const app_remote_options_t* options,
                     app_remote_result_t* result_out) {
    (void)id;
    (void)catalog_url;
    (void)confirmed;
    (void)options;
    return fixture_remote_package_result(result_out);
}

int app_remote_apply_cached(const char* id, int update, int confirmed,
                            const app_remote_options_t* options,
                            app_remote_result_t* result_out) {
    (void)id;
    (void)update;
    (void)confirmed;
    (void)options;
    return fixture_remote_package_result(result_out);
}

int app_remote_clear(int confirmed, app_remote_result_t* result_out) {
    (void)confirmed;
    return fixture_remote_package_result(result_out);
}

int app_remote_test_fail_after(uint8_t completed_files) {
    (void)completed_files;
    return OK;
}

void app_remote_request_cancel(void) {}

const char* app_remote_state_name(app_remote_state_t state) {
    return state == APP_REMOTE_STATE_UNAVAILABLE ? "UNAVAILABLE" : "READY";
}

const char* app_remote_reason_name(app_remote_reason_t reason) {
    return reason == APP_REMOTE_REASON_NETWORK ? "NETWORK" : "NONE";
}

const char* app_remote_cache_state_name(app_remote_cache_state_t state) {
    return state == APP_REMOTE_CACHE_EMPTY ? "EMPTY" : "VALID";
}

const char* app_remote_entry_state_name(app_remote_entry_state_t state) {
    return state == APP_REMOTE_ENTRY_AVAILABLE ? "AVAILABLE" : "BLOCKED";
}

int app_loader_build_launch_info(const char* text, app_launch_info_t* launch) {
    (void)text;
    kmemset(launch, 0, sizeof(*launch));
    return OK;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = shell_packages_host_test_contracts();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != OK) {
        printf("shell-commands-packages-host: FAIL code=%d\n", result);
        return 1;
    }
    printf("shell-commands-packages-host: PASS\n");
    return 0;
}
