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

int app_remote_get_status(app_remote_status_t* status_out) {
    if (!status_out) return ERR_NULL;
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->enabled = 1U;
    status_out->network_ready = 1U;
    status_out->catalog_available = 1U;
    status_out->cache_state = APP_REMOTE_CACHE_VALID;
    return OK;
}

int app_remote_get_count(uint32_t* count_out) {
    if (!count_out) return ERR_NULL;
    *count_out = 0U;
    return OK;
}

int app_remote_get_entry(uint32_t index, app_remote_entry_t* entry_out) {
    (void)index;
    (void)entry_out;
    return ERR_NOT_FOUND;
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
