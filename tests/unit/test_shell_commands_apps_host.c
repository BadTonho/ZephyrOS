#include <stdint.h>
#include <stdio.h>

#include "apps/editor.h"
#include "apps/guitest.h"
#include "apps/mediaplayer.h"
#include "apps/shell_checks.h"
#include "apps/shell_runtime.h"
#include "core/app_builtin.h"
#include "core/app_loader.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/string.h"
#include "core/video.h"
#include "drivers/vesa.h"
#include "fs/fs.h"
#include "ui/desktop.h"
#include "ui/display.h"
#include "ui/filemanager.h"
#include "ui/icons.h"
#include "ui/settings.h"
#include "ui/taskbar.h"
#include "ui/updater.h"
#include "ui/wm.h"

#define HOST_COVERAGE_CAPACITY 1024U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_APP_PID 41U

int shell_commands_apps_host_test_contracts(void);

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

static uint8_t fixture_fs_type = FS_TYPE_FAT32;
static desktop_mode_t fixture_desktop_mode = DESKTOP_MODE_SIMPLE;
static int fixture_recovery_enabled = 1;
static int fixture_wm_active;
static int fixture_desktop_active;
static int fixture_calls_app;
static int fixture_calls_scene;
static int fixture_calls_display;
static int fixture_calls_media;
static int fixture_calls_runtime;

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
    printf("ZCOV_BEGIN|case=host:shell:commands-apps|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:commands-apps|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:commands-apps|value=0x%08X\n",
           (uint32_t)result);
}

void shell_commands_apps_host_set_environment(
    uint8_t fs_type, desktop_mode_t desktop_mode, int recovery_enabled) {
    fixture_fs_type = fs_type;
    fixture_desktop_mode = desktop_mode;
    fixture_recovery_enabled = recovery_enabled;
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

void video_print_at(int x, int y, const char* str, uint8_t color) {
    (void)x;
    (void)y;
    (void)str;
    (void)color;
}

void video_begin_update(void) {}
void video_end_update(void) {}
void video_set_cursor(int x, int y) {
    (void)x;
    (void)y;
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

void shell_command_uppercase(char* text) {
    if (!text) return;
    for (uint32_t index = 0U; text[index]; index++) {
        if (text[index] >= 'a' && text[index] <= 'z') {
            text[index] = (char)(text[index] - ('a' - 'A'));
        }
    }
}

void shell_command_print_num(uint32_t value) {
    (void)value;
}

uint8_t fs_get_type(void) {
    return fixture_fs_type;
}

int fs_get_root_file_info(const char* filename, uint32_t* size_out,
                          uint8_t* attributes_out) {
    (void)filename;
    if (size_out) *size_out = 16U;
    if (attributes_out) *attributes_out = 0U;
    return OK;
}

int recovery_is_available(recovery_component_id_t component) {
    if (component == RECOVERY_COMPONENT_FILESYSTEM) {
        return fixture_fs_type != FS_TYPE_NONE;
    }
    return fixture_recovery_enabled;
}

int recovery_is_enabled(recovery_component_id_t component) {
    (void)component;
    return fixture_recovery_enabled;
}

void shell_runtime_resume_terminal(void) {
    fixture_calls_runtime++;
}

void shell_runtime_suspend_terminal(void) {
    fixture_calls_runtime++;
}

void shell_runtime_suspend_terminal_for_scene(void) {
    fixture_calls_runtime++;
}

int shell_runtime_prepare_filemanager(void) {
    fixture_calls_runtime++;
    return fixture_recovery_enabled ? OK : ERR_UNAVAILABLE;
}

void shell_checks_run_app_inputtest(uint8_t use_tty) {
    (void)use_tty;
    fixture_calls_app++;
}

int app_loader_build_launch_info(const char* text, app_launch_info_t* launch) {
    if (!launch || !text) return ERR_NULL;
    kmemset(launch, 0, sizeof(*launch));
    launch->abi_version = APP_LAUNCH_ABI_VERSION;
    launch->raw_length = kstrlen(text);
    return OK;
}

int app_loader_run_file_with_launch(const char* path,
                                    const app_launch_info_t* launch,
                                    uint32_t* pid_out) {
    if (!path || !launch || !pid_out) return ERR_NULL;
    *pid_out = HOST_APP_PID;
    fixture_calls_app++;
    return fixture_recovery_enabled ? OK : ERR_UNAVAILABLE;
}

int app_builtin_run_argtest(const app_launch_info_t* launch, uint32_t* pid_out) {
    if (!launch || !pid_out) return ERR_NULL;
    *pid_out = HOST_APP_PID;
    fixture_calls_app++;
    return OK;
}

int app_builtin_run_outputtest(uint32_t exit_code, uint32_t* pid_out) {
    (void)exit_code;
    if (!pid_out) return ERR_NULL;
    *pid_out = HOST_APP_PID;
    fixture_calls_app++;
    return OK;
}

int app_builtin_run_pathtest(uint32_t* pid_out) {
    if (!pid_out) return ERR_NULL;
    *pid_out = HOST_APP_PID;
    fixture_calls_app++;
    return OK;
}

int app_builtin_run_devtest(uint32_t* pid_out) {
    if (!pid_out) return ERR_NULL;
    *pid_out = HOST_APP_PID;
    fixture_calls_app++;
    return OK;
}

int icons_get_desktop_bitmap_status(icon_desktop_id_t id) {
    return id == ICON_DESKTOP_TASKMGR ? ERR_UNAVAILABLE : OK;
}

desktop_mode_t desktop_get_mode(void) {
    return fixture_desktop_mode;
}

int desktop_set_mode(desktop_mode_t mode) {
    if (mode != DESKTOP_MODE_SIMPLE && mode != DESKTOP_MODE_CLASSIC) {
        return ERR_INVALID;
    }
    fixture_desktop_mode = mode;
    return OK;
}

int desktop_is_active(void) {
    return fixture_desktop_active;
}

void desktop_set_active(int active) {
    fixture_desktop_active = active;
}

void desktop_draw(void) {
    fixture_calls_scene++;
}

int wm_is_active(void) {
    return fixture_wm_active;
}

void wm_set_active(int active) {
    fixture_wm_active = active;
    fixture_calls_scene++;
}

int guitest_open_calls;

void guitest_open(void) {
    guitest_open_calls++;
    fixture_calls_scene++;
}

void guitest_open_modern(void) {
    guitest_open_calls++;
    fixture_calls_scene++;
}

static vesa_mode_t fixture_vesa_mode = {
    1024U, 768U, 32U, 4096U, NULL, 1U
};

vesa_mode_t* vesa_get_mode(void) {
    fixture_calls_display++;
    return &fixture_vesa_mode;
}

int vesa_has_backbuffer(void) {
    fixture_calls_display++;
    return 1;
}

static void fixture_fill_metrics(display_metrics_t* metrics) {
    kmemset(metrics, 0, sizeof(*metrics));
    metrics->scale = DISPLAY_SCALE_NORMAL;
    metrics->factor_numerator = 1U;
    metrics->factor_denominator = 1U;
    metrics->font_width = 8U;
    metrics->font_height = 16U;
    metrics->spacing = 2U;
    metrics->taskbar_height = 24U;
    metrics->taskbar_side_width = 24U;
    metrics->button_min_width = 80U;
    metrics->button_min_height = 24U;
    metrics->icon_size = 32U;
    metrics->title_bar_height = 18U;
    metrics->row_height = 20U;
    metrics->min_width = 640U;
    metrics->min_height = 480U;
    metrics->available = 1U;
}

int display_get_metrics(display_metrics_t* metrics) {
    if (!metrics) return ERR_NULL;
    fixture_calls_display++;
    fixture_fill_metrics(metrics);
    return OK;
}

int display_parse_scale(const char* name, display_scale_t* scale) {
    if (!name || !scale) return ERR_NULL;
    if (kstrcmp(name, "normal") == 0) {
        *scale = DISPLAY_SCALE_NORMAL;
        return OK;
    }
    if (kstrcmp(name, "pequena") == 0) {
        *scale = DISPLAY_SCALE_SMALL;
        return OK;
    }
    if (kstrcmp(name, "grande") == 0) {
        *scale = DISPLAY_SCALE_LARGE;
        return OK;
    }
    return ERR_INVALID;
}

const char* display_scale_name(display_scale_t scale) {
    if (scale == DISPLAY_SCALE_SMALL) return "pequena";
    if (scale == DISPLAY_SCALE_LARGE) return "grande";
    return "normal";
}

int display_apply_scale(display_scale_t scale) {
    if (scale >= DISPLAY_SCALE_COUNT) return ERR_INVALID;
    fixture_calls_display++;
    return OK;
}

int taskbar_get_work_area(tb_rect_t* area) {
    if (!area) return 0;
    area->x = 0;
    area->y = 0;
    area->width = 1024;
    area->height = 744;
    return 1;
}

void taskbar_draw_config_menu(void) {
    fixture_calls_scene++;
}

void guitest_close(void) {}
int guitest_is_active(void) { return 0; }
void guitest_draw(void) {}
void guitest_handle_key(uint8_t scancode) { (void)scancode; }
void guitest_handle_mouse(mouse_event_t* event) { (void)event; }

void editor_run(void) {
    fixture_calls_scene++;
}

void editor_run_file(const char* filename) {
    (void)filename;
    fixture_calls_scene++;
}

int fm_run_result = OK;

void fm_run(void) {
    fixture_calls_scene++;
}

int taskmgr_open_gui(void) {
    fixture_calls_scene++;
    return ERR_UNAVAILABLE;
}

void taskmgr_run(void) {
    fixture_calls_scene++;
}

void settings_open(void) {
    fixture_calls_scene++;
}

int updater_open(void) {
    fixture_calls_scene++;
    return OK;
}

int mp_play_audio(const char* filename) {
    if (!filename) return ERR_NULL;
    fixture_calls_media++;
    return OK;
}

int mp_play_image(const char* filename) {
    if (!filename) return ERR_NULL;
    fixture_calls_media++;
    return OK;
}

void mp_stop(void) {
    fixture_calls_media++;
}

static int fixture_validate_calls(void) {
    int failures = 0;

    if (fixture_calls_app == 0) failures++;
    if (fixture_calls_scene == 0) failures++;
    if (fixture_calls_display == 0) failures++;
    if (fixture_calls_media == 0) failures++;
    if (fixture_calls_runtime == 0) failures++;
    if (guitest_open_calls == 0) failures++;
    return failures;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = shell_commands_apps_host_test_contracts();
    result += fixture_validate_calls();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != 0) {
        fprintf(stderr, "shell commands apps host test failed: %d\n", result);
        return 1;
    }
    return 0;
}
