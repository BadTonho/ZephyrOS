#include <stdint.h>
#include <stdio.h>

#include "core/app_package.h"
#include "core/app_catalog.h"
#include "core/app_remote.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/string.h"
#include "core/video.h"
#include "drivers/mouse.h"
#include "process/process.h"
#include "ui/desktop.h"
#include "ui/display.h"
#include "ui/gui.h"
#include "ui/appstore_test.h"
#include "ui/taskbar.h"
#include "ui/wm.h"

#define HOST_COVERAGE_CAPACITY 2048U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static app_package_status_t fixture_package_status;
static int fixture_package_status_result;
static int fixture_provenance_available;
static int fixture_installed_trust;
static desktop_mode_t fixture_desktop_mode;
static app_catalog_entry_t fixture_catalog_entries[APP_CATALOG_MAX_ENTRIES];
static app_catalog_status_t fixture_catalog_status;
static uint32_t fixture_catalog_count;
static int fixture_catalog_ready;
static int fixture_catalog_refresh_result;
static int fixture_package_ready;
static int fixture_package_operation_result;
static int fixture_package_verify_result;
static int fixture_package_run_result;
static int fixture_plan_result;
static int fixture_remote_enabled;
static app_remote_entry_t fixture_remote_entries[APP_REMOTE_MAX_ENTRIES];
static uint32_t fixture_remote_count;
static int fixture_remote_operation_result;
static process_t fixture_worker_process;

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
    printf("ZCOV_BEGIN|case=host:ui:appstore|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:ui:appstore|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:ui:appstore|value=0x%08X\n",
           (uint32_t)result);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

int app_package_compare_versions(const char* left, const char* right,
                                 int* comparison_out) {
    if (!left || !right || !comparison_out) return ERR_NULL;
    *comparison_out = kstrcmp(left, right);
    return OK;
}

int app_package_get_status(app_package_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    if (fixture_package_status_result != OK) return fixture_package_status_result;
    *status_out = fixture_package_status;
    return OK;
}

int app_remote_is_provenance_available(void) {
    return fixture_provenance_available;
}

int app_remote_get_installed_trust(const char* id, const char* version) {
    (void)id;
    (void)version;
    return fixture_installed_trust;
}

int app_catalog_refresh(void) {
    return fixture_catalog_refresh_result;
}

int app_catalog_is_ready(void) {
    return fixture_catalog_ready;
}

int app_catalog_get_status(app_catalog_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    *status_out = fixture_catalog_status;
    return OK;
}

int app_catalog_get_count(uint32_t* count_out) {
    if (!count_out) return ERR_NULL;
    *count_out = fixture_catalog_count;
    return OK;
}

int app_catalog_get_entry(uint32_t index, app_catalog_entry_t* entry_out) {
    if (!entry_out) return ERR_NULL;
    if (index >= fixture_catalog_count) return ERR_NOT_FOUND;
    *entry_out = fixture_catalog_entries[index];
    return OK;
}

static void fixture_fill_plan(app_package_plan_t* plan_out) {
    kmemset(plan_out, 0, sizeof(*plan_out));
    plan_out->entry_count = 1U;
    plan_out->target_index = 0U;
    kmemcpy(plan_out->entries[0].id, "CORE", 5U);
    kmemcpy(plan_out->entries[0].alias, "core", 5U);
    kmemcpy(plan_out->entries[0].from_version, "1.0", 4U);
    kmemcpy(plan_out->entries[0].to_version, "2.0", 4U);
    plan_out->entries[0].action = APP_PACKAGE_PLAN_ACTION_UPDATE;
}

int app_catalog_build_install_plan(const char* id_or_alias,
                                   app_package_plan_t* plan_out) {
    (void)id_or_alias;
    if (!plan_out) return ERR_NULL;
    fixture_fill_plan(plan_out);
    return fixture_plan_result;
}

int app_catalog_build_update_plan(const char* id_or_alias,
                                  int allow_downgrade,
                                  app_package_plan_t* plan_out) {
    (void)id_or_alias;
    (void)allow_downgrade;
    if (!plan_out) return ERR_NULL;
    fixture_fill_plan(plan_out);
    return fixture_plan_result;
}

int app_package_is_ready(void) {
    return fixture_package_ready;
}

int app_package_verify_file(const char* path, app_package_info_t* info_out) {
    (void)path;
    if (!info_out) return ERR_NULL;
    kmemset(info_out, 0, sizeof(*info_out));
    kmemcpy(info_out->id, "CORE", 5U);
    kmemcpy(info_out->name, "Core", 5U);
    kmemcpy(info_out->version, "1.0", 4U);
    return fixture_package_verify_result;
}

static int fixture_package_action(app_package_action_result_t* result_out) {
    if (!result_out) return ERR_NULL;
    kmemset(result_out, 0, sizeof(*result_out));
    if (fixture_package_operation_result != OK) {
        result_out->reason = APP_PACKAGE_ACTION_REASON_PACKAGE_INVALID;
    }
    return fixture_package_operation_result;
}

int app_package_preflight_plan(const app_package_plan_t* plan,
                               app_package_action_result_t* result_out) {
    if (!plan) return ERR_NULL;
    return fixture_package_action(result_out);
}

int app_package_apply_plan_confirmed(const app_package_plan_t* plan,
                                     app_package_action_result_t* result_out) {
    if (!plan) return ERR_NULL;
    return fixture_package_action(result_out);
}

int app_package_preflight_remove(const char* id,
                                 app_package_action_result_t* result_out) {
    (void)id;
    return fixture_package_action(result_out);
}

int app_package_remove_confirmed(const char* id,
                                 app_package_action_result_t* result_out) {
    (void)id;
    return fixture_package_action(result_out);
}

int app_package_preflight_rollback(const char* id,
                                   app_package_action_result_t* result_out) {
    (void)id;
    return fixture_package_action(result_out);
}

int app_package_rollback_confirmed(const char* id,
                                   app_package_action_result_t* result_out) {
    (void)id;
    return fixture_package_action(result_out);
}

int app_package_run_installed(const char* id, const app_launch_info_t* launch,
                              uint32_t* pid_out,
                              app_package_action_result_t* result_out) {
    (void)id;
    (void)launch;
    if (!pid_out) return ERR_NULL;
    *pid_out = 42U;
    return fixture_package_action(result_out) == OK ?
           fixture_package_run_result : fixture_package_operation_result;
}

int app_remote_get_status(app_remote_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->state = fixture_remote_enabled ? APP_REMOTE_STATE_READY :
                        APP_REMOTE_STATE_DISABLED;
    status_out->enabled = fixture_remote_enabled;
    status_out->network_ready = 1U;
    status_out->catalog_available = 1U;
    status_out->cache_state = APP_REMOTE_CACHE_VALID;
    return OK;
}

int app_remote_get_count(uint32_t* count_out) {
    if (!count_out) return ERR_NULL;
    *count_out = fixture_remote_count;
    return OK;
}

int app_remote_get_entry(uint32_t index, app_remote_entry_t* entry_out) {
    if (!entry_out) return ERR_NULL;
    if (index >= fixture_remote_count) return ERR_NOT_FOUND;
    *entry_out = fixture_remote_entries[index];
    return OK;
}

int app_remote_refresh_provenance(void) {
    return OK;
}

int app_remote_enable(void) {
    if (fixture_remote_operation_result == OK) fixture_remote_enabled = 1;
    return fixture_remote_operation_result;
}

int app_remote_disable(void) {
    if (fixture_remote_operation_result == OK) fixture_remote_enabled = 0;
    return fixture_remote_operation_result;
}

int app_remote_check(const char* catalog_url,
                     const app_remote_options_t* options,
                     app_remote_result_t* result_out) {
    (void)catalog_url;
    (void)options;
    if (!result_out) return ERR_NULL;
    kmemset(result_out, 0, sizeof(*result_out));
    return fixture_remote_operation_result;
}

int app_remote_fetch(const char* id, const char* catalog_url, int confirmed,
                     const app_remote_options_t* options,
                     app_remote_result_t* result_out) {
    (void)id;
    (void)catalog_url;
    (void)confirmed;
    (void)options;
    if (!result_out) return ERR_NULL;
    kmemset(result_out, 0, sizeof(*result_out));
    fixture_fill_plan(&result_out->plan);
    return fixture_remote_operation_result;
}

int app_remote_apply_cached(const char* id, int update, int confirmed,
                            const app_remote_options_t* options,
                            app_remote_result_t* result_out) {
    (void)id;
    (void)update;
    (void)confirmed;
    (void)options;
    if (!result_out) return ERR_NULL;
    kmemset(result_out, 0, sizeof(*result_out));
    fixture_fill_plan(&result_out->plan);
    return fixture_remote_operation_result;
}

void app_remote_request_cancel(void) {}

int process_wake_channel(wait_channel_t* channel, wait_wake_mode_t mode,
                         wait_reason_t reason, uint32_t* out_woken) {
    (void)channel;
    (void)mode;
    (void)reason;
    if (out_woken) *out_woken = 0U;
    return OK;
}

int recovery_is_enabled(recovery_component_id_t component) {
    (void)component;
    return 1;
}

int recovery_mark_disabled(recovery_component_id_t component, int error,
                           const char* message) {
    (void)component;
    (void)error;
    (void)message;
    return OK;
}

process_t* process_create(const char* name, void (*entry_point)()) {
    (void)name;
    (void)entry_point;
    return &fixture_worker_process;
}

void process_yield(void) {}

int ipc_wait(uint32_t timeout_ticks, wait_reason_t* out_reason) {
    (void)timeout_ticks;
    if (out_reason) *out_reason = WAIT_REASON_TIMEOUT;
    return ERR_TIMEOUT;
}

const char* app_catalog_state_name(app_catalog_state_t state) {
    static const char* names[] = {
        "disponivel", "instalado", "atualizacao", "mesma-versao",
        "downgrade", "bloqueado", "invalido"
    };

    return state < APP_CATALOG_STATE_INVALID + 1 ? names[state] : "desconhecido";
}

const char* app_catalog_reason_name(app_catalog_reason_t reason) {
    (void)reason;
    return "nenhum";
}

const char* app_package_action_reason_name(
    app_package_action_reason_t reason) {
    (void)reason;
    return "nenhum";
}

const char* app_remote_entry_state_name(app_remote_entry_state_t state) {
    (void)state;
    return "disponivel";
}

const char* app_remote_reason_name(app_remote_reason_t reason) {
    (void)reason;
    return "nenhum";
}

void video_begin_update(void) {}
void video_end_update(void) {}
void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}
void video_print_at(int x, int y, const char* text, uint8_t color) {
    (void)x;
    (void)y;
    (void)text;
    (void)color;
}
void video_fill_rect(int x, int y, int width, int height, char value,
                     uint8_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)value;
    (void)color;
}
void video_draw_box(int x, int y, int width, int height, uint8_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)color;
}

void gui_draw_text(uint32_t x, uint32_t y, const char* text, uint32_t color) {
    (void)x;
    (void)y;
    (void)text;
    (void)color;
}
void gui_draw_scaled_text(uint32_t x, uint32_t y, const char* text,
                          uint32_t color) {
    gui_draw_text(x, y, text, color);
}
void gui_draw_rounded_rect(uint32_t x, uint32_t y, uint32_t width,
                           uint32_t height, uint32_t radius, uint32_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)radius;
    (void)color;
}
void gui_draw_flat_border(uint32_t x, uint32_t y, uint32_t width,
                          uint32_t height, uint32_t color) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)color;
}
void gui_draw_modern_button(uint32_t x, uint32_t y, uint32_t width,
                            uint32_t height, const char* text,
                            gui_button_state_t state) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)text;
    (void)state;
}

uint32_t display_scale_px(uint32_t value) {
    return value;
}

void taskbar_draw(void) {}
desktop_mode_t desktop_get_mode(void) {
    return fixture_desktop_mode;
}
void desktop_set_active(int active) {
    (void)active;
}
void desktop_draw(void) {}
void wm_set_active(int active) {
    (void)active;
}
int wm_register_hosted_app(const wm_hosted_app_t* app) {
    (void)app;
    return OK;
}
int wm_close_hosted_app(wm_app_type_t app_type) {
    (void)app_type;
    return OK;
}
void wm_request_hosted_redraw(wm_app_type_t app_type) {
    (void)app_type;
}

int main(void) {
    int result;

    kmemset(&fixture_catalog_status, 0, sizeof(fixture_catalog_status));
    kmemset(fixture_catalog_entries, 0, sizeof(fixture_catalog_entries));
    fixture_catalog_count = 2U;
    fixture_catalog_ready = 1;
    fixture_catalog_refresh_result = OK;
    fixture_catalog_status.source_count = 2U;
    fixture_catalog_status.valid_source_count = 2U;
    fixture_catalog_status.entry_count = 2U;
    kmemcpy(fixture_catalog_entries[0].alias, "core", 5U);
    kmemcpy(fixture_catalog_entries[0].source.id, "CORE", 5U);
    kmemcpy(fixture_catalog_entries[0].source.name, "Core", 5U);
    kmemcpy(fixture_catalog_entries[0].source.version, "1.0", 4U);
    fixture_catalog_entries[0].has_source = 1U;
    fixture_catalog_entries[0].state = APP_CATALOG_STATE_AVAILABLE;
    fixture_catalog_entries[0].capabilities = APP_CATALOG_CAPABILITY_VERIFY |
        APP_CATALOG_CAPABILITY_INSTALL | APP_CATALOG_CAPABILITY_RUN |
        APP_CATALOG_CAPABILITY_REMOVE | APP_CATALOG_CAPABILITY_UPDATE;
    kmemcpy(fixture_catalog_entries[1].alias, "tool", 5U);
    kmemcpy(fixture_catalog_entries[1].source.id, "TOOL", 5U);
    kmemcpy(fixture_catalog_entries[1].source.name, "Tool", 5U);
    kmemcpy(fixture_catalog_entries[1].source.version, "2.0", 4U);
    kmemcpy(fixture_catalog_entries[1].installed.id, "TOOL", 5U);
    kmemcpy(fixture_catalog_entries[1].installed.version, "1.0", 4U);
    fixture_catalog_entries[1].has_source = 1U;
    fixture_catalog_entries[1].has_installed = 1U;
    fixture_catalog_entries[1].state = APP_CATALOG_STATE_UPDATE_AVAILABLE;
    fixture_catalog_entries[1].capabilities = fixture_catalog_entries[0].capabilities;
    kmemset(fixture_remote_entries, 0, sizeof(fixture_remote_entries));
    fixture_remote_count = 1U;
    fixture_remote_enabled = 1;
    fixture_remote_entries[0].info = fixture_catalog_entries[0].source;
    fixture_remote_entries[0].cached = 1U;
    fixture_remote_entries[0].installed = 1U;
    fixture_remote_entries[0].state = APP_REMOTE_ENTRY_UPDATE_AVAILABLE;
    fixture_package_ready = 1;
    fixture_package_operation_result = OK;
    fixture_package_verify_result = OK;
    fixture_package_run_result = OK;
    fixture_plan_result = OK;
    fixture_remote_operation_result = OK;
    fixture_package_status_result = OK;
    fixture_provenance_available = 0;
    fixture_installed_trust = 0;
    fixture_desktop_mode = DESKTOP_MODE_SIMPLE;
    coverage_active = 1U;
    result = appstore_host_test_contracts();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != OK) {
        printf("appstore-host: FAIL code=%d\n", result);
        return 1;
    }
    printf("appstore-host: PASS\n");
    return 0;
}
