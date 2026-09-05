#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/keyboard.h"
#include "core/log.h"
#include "core/power.h"
#include "core/recovery.h"
#include "core/string.h"
#include "core/update.h"
#include "core/update_remote.h"
#include "core/update_remote_runtime.h"
#include "core/update_remote_system.h"
#include "core/update_system_slots.h"
#include "core/video.h"
#include "fs/fs.h"
#include "process/process.h"
#include "ui/desktop.h"
#include "ui/gui.h"
#include "ui/wm.h"
#include "ui/updater_test.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static int fixture_cached_path_result;
static int fixture_slots_status_result;
static update_system_slots_status_t fixture_slots_status;
static char fixture_cached_path[FS_MAX_PATH];
static ipc_msg_t fixture_message;
static int fixture_message_available;
static char fixture_keyboard_ascii;
static desktop_mode_t fixture_desktop_mode;
static process_t fixture_process;
static const wm_hosted_app_t* fixture_hosted_app;

void updater_host_fixture_set_desktop_mode(int mode) {
    fixture_desktop_mode = (desktop_mode_t)mode;
}

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
    printf("ZCOV_BEGIN|case=host:ui:updater|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:ui:updater|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:ui:updater|value=0x%08X\n",
           (uint32_t)result);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void updater_host_fixture_set_message(int type, uint32_t data1) {
    fixture_message.type = (ipc_msg_type_t)type;
    fixture_message.data1 = data1;
    fixture_message.data2 = 0U;
    fixture_message_available = 1;
}

void updater_host_fixture_clear_message(void) {
    fixture_message_available = 0;
}

void updater_host_fixture_set_cached_path(int result, const char* path) {
    fixture_cached_path_result = result;
    kmemset(fixture_cached_path, 0, sizeof(fixture_cached_path));
    if (path) {
        uint32_t index = 0U;

        while (index + 1U < sizeof(fixture_cached_path) && path[index]) {
            fixture_cached_path[index] = path[index];
            index++;
        }
    }
}

void updater_host_fixture_set_slots(int result, uint8_t pending_slot,
                                    uint32_t sequence) {
    fixture_slots_status_result = result;
    kmemset(&fixture_slots_status, 0, sizeof(fixture_slots_status));
    fixture_slots_status.pending_slot = pending_slot;
    fixture_slots_status.sequence = sequence;
}

void updater_host_fixture_set_keyboard_ascii(char value) {
    fixture_keyboard_ascii = value;
}

int fs_get_file_count(void) {
    return 3;
}

int fs_get_file_info(int index, char* name_out, uint32_t* size_out,
                     uint8_t* attr_out) {
    static const char* names[3] = {"beta.zup", "skip.txt", "folder.zup"};
    static const uint32_t sizes[3] = {20U, 5U, 8U};
    static const uint8_t attributes[3] = {0U, 0U, 0x10U};

    if (index < 0 || index >= 3 || !name_out || !size_out || !attr_out) {
        return ERR_INVALID;
    }
    kmemset(name_out, 0, 13U);
    kmemcpy(name_out, names[index], kstrlen(names[index]) + 1U);
    *size_out = sizes[index];
    *attr_out = attributes[index];
    return OK;
}

int fs_get_root_file_info(const char* filename, uint32_t* size_out,
                          uint8_t* attr_out) {
    (void)filename;
    if (size_out) *size_out = 77U;
    if (attr_out) *attr_out = 0U;
    return OK;
}

int update_is_ready(void) {
    return 1;
}

int update_get_status(update_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->state_store = UPDATE_STORE_VALID;
    status_out->current_files = UPDATE_STORE_VALID;
    status_out->history_store = UPDATE_STORE_EMPTY;
    status_out->build_version.major = 1U;
    status_out->build_version.minor = 2U;
    status_out->build_version.patch = 3U;
    status_out->installed_version = status_out->build_version;
    status_out->capabilities.verifier_ready = 1U;
    status_out->capabilities.local_file_available = 1U;
    status_out->capabilities.apply_available = 1U;
    status_out->capabilities.rollback_available = 1U;
    return OK;
}

int update_get_history_count(uint32_t* count_out) {
    if (!count_out) return ERR_NULL;
    *count_out = 0U;
    return OK;
}

int update_get_history_entry(uint32_t index, update_history_entry_t* entry_out) {
    (void)index;
    if (!entry_out) return ERR_NULL;
    kmemset(entry_out, 0, sizeof(*entry_out));
    return ERR_NOT_FOUND;
}

int update_verify_file(const char* path, update_verification_t* result_out) {
    (void)path;
    if (result_out) {
        kmemset(result_out, 0, sizeof(*result_out));
        result_out->reason = ZUPD_REASON_FORMAT;
    }
    return ERR_INVALID;
}

int update_apply_file(const char* path, const update_action_options_t* options,
                      update_action_result_t* result_out) {
    (void)path;
    (void)options;
    if (result_out) {
        kmemset(result_out, 0, sizeof(*result_out));
        result_out->reason = UPDATE_ACTION_VERIFY;
    }
    return ERR_INVALID;
}

int update_rollback(const update_action_options_t* options,
                    update_action_result_t* result_out) {
    (void)options;
    if (result_out) {
        kmemset(result_out, 0, sizeof(*result_out));
        result_out->reason = UPDATE_ACTION_NO_ROLLBACK;
    }
    return ERR_UNAVAILABLE;
}

const char* update_store_state_name(update_store_state_t state) {
    (void)state;
    return "VALID";
}

const char* update_history_operation_name(update_history_operation_t operation) {
    (void)operation;
    return "NONE";
}

const char* update_history_outcome_name(update_history_outcome_t outcome) {
    (void)outcome;
    return "NONE";
}

int update_remote_get_status(update_remote_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->state = UPDATE_REMOTE_STATE_READY;
    status_out->cache_store = UPDATE_REMOTE_STORE_VALID;
    status_out->initialized = 1U;
    status_out->enabled = 1U;
    status_out->network_ready = 1U;
    status_out->manifest_cached = 1U;
    status_out->package_cached = 1U;
    status_out->candidate.base_version.major = 1U;
    status_out->candidate.target_version.major = 2U;
    kmemcpy(status_out->cached_alias, "CACHE.ZUP", 10U);
    kmemcpy(status_out->candidate.package_path, "CACHE.ZUP", 10U);
    return OK;
}

int update_remote_get_cached_alias(char* alias_out, uint32_t capacity) {
    if (!alias_out || capacity < 10U) return ERR_INVALID;
    kmemset(alias_out, 0, capacity);
    kmemcpy(alias_out, "cache.zup", 10U);
    return OK;
}

int update_remote_enable(void) { return OK; }
int update_remote_disable(void) { return OK; }

int update_remote_check(const char* manifest_url,
                        const update_remote_options_t* options,
                        update_remote_result_t* result_out) {
    (void)manifest_url;
    (void)options;
    if (result_out) {
        kmemset(result_out, 0, sizeof(*result_out));
        result_out->reason = UPDATE_REMOTE_REASON_NETWORK;
    }
    return ERR_UNAVAILABLE;
}

int update_remote_fetch(const char* manifest_url,
                        const update_remote_options_t* options,
                        update_remote_result_t* result_out) {
    return update_remote_check(manifest_url, options, result_out);
}

int update_remote_clear(const update_remote_options_t* options,
                        update_remote_result_t* result_out) {
    (void)options;
    if (result_out) kmemset(result_out, 0, sizeof(*result_out));
    return ERR_UNAVAILABLE;
}

const char* update_remote_state_name(update_remote_state_t state) {
    (void)state;
    return "READY";
}

const char* update_remote_reason_name(update_remote_reason_t reason) {
    (void)reason;
    return "NONE";
}

const char* update_remote_store_name(update_remote_store_t state) {
    (void)state;
    return "VALID";
}

int update_runtime_get_status(update_runtime_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->state_valid = 1U;
    status_out->rollback_available = 1U;
    status_out->capabilities.apply_available = 1U;
    return OK;
}

int update_runtime_get_cache(update_runtime_cache_t* cache_out) {
    if (!cache_out) return ERR_NULL;
    kmemset(cache_out, 0, sizeof(*cache_out));
    cache_out->valid = 1U;
    cache_out->full_package = 1U;
    kmemcpy(cache_out->manifest_alias, "RUNTIME.MNF", 12U);
    return OK;
}

int update_runtime_apply_cached(const update_runtime_action_options_t* options,
                               update_runtime_action_result_t* result_out) {
    (void)options;
    if (result_out) kmemset(result_out, 0, sizeof(*result_out));
    return ERR_UNAVAILABLE;
}

int update_runtime_rollback(const update_runtime_action_options_t* options,
                            update_runtime_action_result_t* result_out) {
    return update_runtime_apply_cached(options, result_out);
}

const char* update_runtime_reason_name(update_runtime_reason_t reason) {
    (void)reason;
    return "NONE";
}

int update_remote_runtime_get_status(
    update_remote_runtime_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->state = UPDATE_REMOTE_RUNTIME_STATE_READY;
    status_out->initialized = 1U;
    status_out->enabled = 1U;
    status_out->network_ready = 1U;
    return OK;
}

int update_remote_runtime_get_cache(update_runtime_cache_t* cache_out) {
    return update_runtime_get_cache(cache_out);
}

int update_remote_runtime_check(
    const char* tag, const update_remote_options_t* options,
    update_remote_runtime_result_t* result_out) {
    (void)tag;
    (void)options;
    if (result_out) kmemset(result_out, 0, sizeof(*result_out));
    return ERR_UNAVAILABLE;
}

int update_remote_runtime_fetch(
    const char* tag, update_remote_runtime_fetch_mode_t mode,
    const update_remote_options_t* options,
    update_remote_runtime_result_t* result_out) {
    (void)mode;
    return update_remote_runtime_check(tag, options, result_out);
}

int update_remote_runtime_clear(const update_remote_options_t* options,
                                update_remote_runtime_result_t* result_out) {
    (void)options;
    if (result_out) kmemset(result_out, 0, sizeof(*result_out));
    return ERR_UNAVAILABLE;
}

const char* update_remote_runtime_state_name(
    update_remote_runtime_state_t state) {
    (void)state;
    return "READY";
}

const char* update_remote_runtime_reason_name(
    update_remote_runtime_reason_t reason) {
    (void)reason;
    return "NONE";
}

int update_remote_system_get_status(update_remote_system_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->state = UPDATE_REMOTE_SYSTEM_STATE_READY;
    status_out->initialized = 1U;
    status_out->volume_ready = 1U;
    status_out->cache_slot = 0U;
    kmemcpy(status_out->tag, "stable", 7U);
    return OK;
}

int update_remote_system_fetch(
    const char* tag, const update_remote_options_t* options,
    update_remote_system_result_t* result_out) {
    (void)tag;
    (void)options;
    if (result_out) kmemset(result_out, 0, sizeof(*result_out));
    return ERR_UNAVAILABLE;
}

int update_remote_system_clear(const update_remote_options_t* options,
                               update_remote_system_result_t* result_out) {
    (void)options;
    if (result_out) kmemset(result_out, 0, sizeof(*result_out));
    return ERR_UNAVAILABLE;
}

const char* update_remote_system_state_name(
    update_remote_system_state_t state) {
    (void)state;
    return "READY";
}

const char* update_remote_system_reason_name(
    update_remote_system_reason_t reason) {
    (void)reason;
    return "NONE";
}

int update_system_verify_file(const char* path,
                              update_system_verification_t* result_out) {
    (void)path;
    if (result_out) kmemset(result_out, 0, sizeof(*result_out));
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
    (void)reason;
    return "NONE";
}

const char* update_system_slots_state_name(update_system_slots_state_t state) {
    (void)state;
    return "READY";
}

const char* update_system_slots_boot_state_name(
    update_system_slots_boot_state_t state) {
    (void)state;
    return "NONE";
}

const char* update_system_slots_journal_phase_name(
    update_system_slots_journal_phase_t phase) {
    (void)phase;
    return "NONE";
}

const char* update_system_slot_file_state_name(
    update_system_slot_file_state_t state) {
    (void)state;
    return "VALID";
}

const char* update_system_slots_reason_name(
    update_system_slots_reason_t reason) {
    (void)reason;
    return "NONE";
}

int update_system_slots_stage_file(
    const char* path, const update_system_slots_action_options_t* options,
    update_system_slots_action_result_t* result_out) {
    (void)path;
    (void)options;
    if (result_out) kmemset(result_out, 0, sizeof(*result_out));
    return ERR_UNAVAILABLE;
}

int update_system_slots_cancel_pending(
    const update_system_slots_action_options_t* options,
    update_system_slots_action_result_t* result_out) {
    (void)options;
    if (result_out) kmemset(result_out, 0, sizeof(*result_out));
    return ERR_UNAVAILABLE;
}

int update_system_slots_reboot_preflight(
    update_system_slots_action_result_t* result_out) {
    if (result_out) kmemset(result_out, 0, sizeof(*result_out));
    return ERR_UNAVAILABLE;
}

int recovery_is_enabled(recovery_component_id_t component) {
    (void)component;
    return 1;
}

process_t* process_create_with_stack_size(const char* name,
                                          void (*entry_point)(),
                                          uint32_t stack_size) {
    (void)name;
    (void)entry_point;
    (void)stack_size;
    kmemset(&fixture_process, 0, sizeof(fixture_process));
    return &fixture_process;
}

int process_wake_channel(wait_channel_t* channel, wait_wake_mode_t mode,
                         wait_reason_t reason, uint32_t* out_woken) {
    (void)channel;
    (void)mode;
    (void)reason;
    if (out_woken) *out_woken = 1U;
    return OK;
}

void process_yield(void) {
}

int ipc_wait(uint32_t timeout_ticks, wait_reason_t* out_reason) {
    (void)timeout_ticks;
    if (out_reason) *out_reason = WAIT_REASON_EVENT;
    return OK;
}

desktop_mode_t desktop_get_mode(void) {
    return fixture_desktop_mode;
}

void desktop_set_active(int active) {
    (void)active;
}

void desktop_draw(void) {
}

void wm_set_active(int active) {
    (void)active;
}

int wm_register_hosted_app(const wm_hosted_app_t* app) {
    fixture_hosted_app = app;
    return OK;
}

int wm_close_hosted_app(wm_app_type_t app_type) {
    (void)app_type;
    if (fixture_hosted_app && fixture_hosted_app->on_close) {
        fixture_hosted_app->on_close();
    }
    return OK;
}

void wm_request_hosted_redraw(wm_app_type_t app_type) {
    (void)app_type;
}

int power_reboot(void) {
    return OK;
}

void video_print(const char* str, uint8_t color) {
    (void)str;
    (void)color;
}

void video_begin_update(void) {
}

void video_end_update(void) {
}

void video_set_cursor(int x, int y) {
    (void)x;
    (void)y;
}

void video_print_at(int x, int y, const char* str, uint8_t color) {
    (void)x;
    (void)y;
    (void)str;
    (void)color;
}

void video_fill_rect(int x, int y, int w, int h, char c, uint8_t color) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)c;
    (void)color;
}

void video_draw_box(int x, int y, int w, int h, uint8_t color) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
}

void gui_draw_text(uint32_t x, uint32_t y, const char* text, uint32_t color) {
    (void)x;
    (void)y;
    (void)text;
    (void)color;
}

void gui_draw_panel(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint32_t background, int pressed) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)background;
    (void)pressed;
}

void gui_draw_button(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     const char* text, int pressed) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)text;
    (void)pressed;
}

void taskbar_draw(void) {
}

int recovery_mark_disabled(recovery_component_id_t component,
                           int error_code, const char* message) {
    (void)component;
    (void)error_code;
    (void)message;
    return OK;
}

int recovery_mark_ready(recovery_component_id_t component) {
    (void)component;
    return OK;
}

int recovery_mark_degraded(recovery_component_id_t component,
                           int error_code, const char* message) {
    (void)component;
    (void)error_code;
    (void)message;
    return OK;
}

void keyboard_process_events(void) {
}

char keyboard_scancode_to_ascii_shifted(uint8_t scancode, uint8_t shifted) {
    (void)scancode;
    (void)shifted;
    return fixture_keyboard_ascii;
}

int ipc_receive(ipc_msg_t* message) {
    if (!message || !fixture_message_available) return 0;
    *message = fixture_message;
    fixture_message_available = 0;
    return 1;
}

int update_remote_system_get_cached_path(char* path_out, uint32_t capacity) {
    if (fixture_cached_path_result == OK && path_out && capacity) {
        uint32_t index = 0U;

        while (index + 1U < capacity && fixture_cached_path[index]) {
            path_out[index] = fixture_cached_path[index];
            index++;
        }
        path_out[index] = '\0';
    }
    return fixture_cached_path_result;
}

int update_system_slots_get_status(update_system_slots_status_t* status_out) {
    if (fixture_slots_status_result == OK && status_out) {
        *status_out = fixture_slots_status;
    }
    return fixture_slots_status_result;
}

const char* update_action_reason_name(update_action_reason_t reason) {
    return reason == UPDATE_ACTION_NONE ? "NONE" : "ACTION";
}

const char* zupd_reason_name(zupd_reason_t reason) {
    return reason == ZUPD_REASON_NONE ? "NONE" : "VERIFY";
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = updater_host_test_contracts();
    coverage_active = 0U;
    if (result != 0) printf("UPDATER_HOST_FAIL:%d\n", result);
    coverage_emit(result);
    return result == 0 ? 0 : 1;
}
