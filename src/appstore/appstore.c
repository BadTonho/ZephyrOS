#include "ui/appstore.h"
#include "ui/appstore_test.h"
#include "core/app_catalog.h"
#include "core/app_loader.h"
#include "core/app_package.h"
#include "core/app_remote.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/string.h"
#include "core/video.h"
#include "core/wait.h"
#include "process/process.h"
#include "ui/desktop.h"
#include "ui/display.h"
#include "ui/gui.h"
#include "ui/taskbar.h"
#include "ui/wm.h"

#define APPSTORE_CLASSIC_MIN_WIDTH 640
#define APPSTORE_CLASSIC_MIN_HEIGHT 420
#define APPSTORE_CLASSIC_DEFAULT_WIDTH 780
#define APPSTORE_CLASSIC_DEFAULT_HEIGHT 560
#define APPSTORE_CLASSIC_MARGIN 12
#define APPSTORE_CLASSIC_ROW_HEIGHT 26
#define APPSTORE_SIMPLE_LIST_WIDTH 28
#define APPSTORE_TEXT_SIZE 96
#define APPSTORE_BUTTON_WIDTH 112
#define APPSTORE_BUTTON_HEIGHT 28
#define APPSTORE_JOB_NONE 0
#define APPSTORE_SCANCODE_ESC 0x01U
#define APPSTORE_SCANCODE_TAB 0x0FU
#define APPSTORE_SCANCODE_ENTER 0x1CU
#define APPSTORE_SCANCODE_A 0x1EU
#define APPSTORE_SCANCODE_B 0x30U
#define APPSTORE_SCANCODE_D 0x20U
#define APPSTORE_SCANCODE_E 0x12U
#define APPSTORE_SCANCODE_I 0x17U
#define APPSTORE_SCANCODE_R 0x13U
#define APPSTORE_SCANCODE_U 0x16U
#define APPSTORE_SCANCODE_V 0x2FU
#define APPSTORE_SCANCODE_F5 0x3FU
#define APPSTORE_SCANCODE_F12 0x58U
#define APPSTORE_SCANCODE_UP 0x48U
#define APPSTORE_SCANCODE_DOWN 0x50U

typedef enum {
    APPSTORE_TAB_CATALOG = 0,
    APPSTORE_TAB_INSTALLED,
    APPSTORE_TAB_DETAILS,
    APPSTORE_TAB_REMOTE,
    APPSTORE_TAB_COUNT
} appstore_tab_t;

typedef enum {
    APPSTORE_CONFIRM_NONE = 0,
    APPSTORE_CONFIRM_INSTALL,
    APPSTORE_CONFIRM_REMOVE,
    APPSTORE_CONFIRM_UPDATE,
    APPSTORE_CONFIRM_ROLLBACK,
    APPSTORE_CONFIRM_REMOTE_FETCH,
    APPSTORE_CONFIRM_REMOTE_INSTALL,
    APPSTORE_CONFIRM_REMOTE_UPDATE
} appstore_confirm_t;

typedef enum {
    APPSTORE_RESULT_NONE = 0,
    APPSTORE_RESULT_REFRESH,
    APPSTORE_RESULT_VERIFY,
    APPSTORE_RESULT_INSTALL,
    APPSTORE_RESULT_REMOVE,
    APPSTORE_RESULT_RUN,
    APPSTORE_RESULT_UPDATE,
    APPSTORE_RESULT_ROLLBACK,
    APPSTORE_RESULT_REMOTE
} appstore_result_t;

typedef enum {
    APPSTORE_JOB_REFRESH = 1,
    APPSTORE_JOB_VERIFY,
    APPSTORE_JOB_PREFLIGHT_INSTALL,
    APPSTORE_JOB_INSTALL,
    APPSTORE_JOB_PREFLIGHT_UPDATE,
    APPSTORE_JOB_UPDATE,
    APPSTORE_JOB_PREFLIGHT_REMOVE,
    APPSTORE_JOB_REMOVE,
    APPSTORE_JOB_PREFLIGHT_ROLLBACK,
    APPSTORE_JOB_ROLLBACK,
    APPSTORE_JOB_RUN,
    APPSTORE_JOB_REMOTE_ENABLE,
    APPSTORE_JOB_REMOTE_CHECK,
    APPSTORE_JOB_REMOTE_PREFLIGHT_FETCH,
    APPSTORE_JOB_REMOTE_FETCH,
    APPSTORE_JOB_REMOTE_PREFLIGHT_INSTALL,
    APPSTORE_JOB_REMOTE_INSTALL,
    APPSTORE_JOB_REMOTE_PREFLIGHT_UPDATE,
    APPSTORE_JOB_REMOTE_UPDATE
} appstore_job_t;

typedef struct {
    appstore_job_t type;
    uint32_t generation;
    char key[APP_CATALOG_ALIAS_SIZE];
} appstore_pending_job_t;

static app_catalog_entry_t appstore_entries[APP_CATALOG_MAX_ENTRIES];
static uint32_t appstore_entry_count;
static app_catalog_status_t appstore_status;
static app_package_action_result_t appstore_action;
static app_package_info_t appstore_verification;
static app_remote_entry_t appstore_remote_entries[APP_REMOTE_MAX_ENTRIES];
static app_remote_status_t appstore_remote_status;
static app_remote_result_t appstore_remote_result;
static app_remote_options_t appstore_remote_options;
static uint32_t appstore_remote_count;
static appstore_pending_job_t appstore_job;
static appstore_pending_job_t appstore_running_job;
static volatile appstore_job_t appstore_queued_type = APPSTORE_JOB_NONE;
static appstore_tab_t appstore_tab;
static appstore_confirm_t appstore_confirm;
static appstore_result_t appstore_result_kind;
static appstore_mode_t appstore_mode;
static int appstore_active;
static int appstore_hosted;
static int appstore_initialized;
static int appstore_busy;
static int appstore_last_result;
static int appstore_selected;
static int appstore_scroll;
static int appstore_gui_x;
static int appstore_gui_y;
static int appstore_gui_width = APPSTORE_CLASSIC_DEFAULT_WIDTH;
static int appstore_gui_height = APPSTORE_CLASSIC_DEFAULT_HEIGHT;
static uint32_t appstore_generation;
static uint32_t appstore_last_pid;
static char appstore_result_text[APPSTORE_TEXT_SIZE];
static char appstore_confirm_key[APP_CATALOG_ALIAS_SIZE];
static uint32_t appstore_confirm_generation;
static process_t* appstore_worker_process;

static void appstore_hosted_draw(int x, int y, int width, int height);
static void appstore_hosted_key(uint8_t scancode);
static int appstore_hosted_mouse(mouse_event_t* event, int x, int y,
                                 int width, int height);
static void appstore_hosted_close(void);
static void appstore_worker_main(void);
static void appstore_gui_draw_details(int x, int y);

static void appstore_gui_draw_surface(int x, int y, int width, int height,
                                      uint32_t background, uint32_t border) {
    if (width <= 0 || height <= 0) return;
    gui_draw_rounded_rect((uint32_t)x, (uint32_t)y, (uint32_t)width,
                          (uint32_t)height,
                          display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                          background);
    gui_draw_flat_border((uint32_t)x, (uint32_t)y, (uint32_t)width,
                         (uint32_t)height, border);
}

static const wm_hosted_app_t appstore_hosted_app = {
    WM_APP_APPSTORE, "ZephyrOS App Store", "App Store",
    APPSTORE_CLASSIC_MIN_WIDTH, APPSTORE_CLASSIC_MIN_HEIGHT,
    APPSTORE_CLASSIC_DEFAULT_WIDTH, APPSTORE_CLASSIC_DEFAULT_HEIGHT,
    WM_KEY_REDRAW_WINDOW_MANAGER,
    appstore_hosted_draw, appstore_hosted_key, appstore_hosted_mouse,
    appstore_hosted_close
};

static void appstore_copy_text(char* output, uint32_t size,
                               const char* input) {
    uint32_t index = 0;

    if (!output || size == 0U) return;
    if (input) {
        while (input[index] && index + 1U < size) {
            output[index] = input[index];
            index++;
        }
    }
    output[index] = '\0';
}

static void appstore_append_text(char* output, uint32_t size,
                                 const char* text) {
    uint32_t length;
    uint32_t index = 0;

    if (!output || !text || size == 0U) return;
    length = kstrlen(output);
    while (text[index] && length + index + 1U < size) {
        output[length + index] = text[index];
        index++;
    }
    output[length + index] = '\0';
}

static void appstore_u32_text(uint32_t value, char* output,
                              uint32_t size) {
    char reversed[11];
    uint32_t count = 0;
    uint32_t index = 0;

    if (!output || size == 0U) return;
    if (value == 0U) {
        appstore_copy_text(output, size, "0");
        return;
    }
    while (value && count < sizeof(reversed)) {
        reversed[count++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    while (count && index + 1U < size) output[index++] = reversed[--count];
    output[index] = '\0';
}

static void appstore_dependencies_text(const app_package_info_t* info,
                                       char* output, uint32_t size) {
    if (!info || info->dependency_count == 0U) {
        appstore_copy_text(output, size, "nenhuma");
        return;
    }
    output[0] = '\0';
    for (uint32_t index = 0; index < info->dependency_count; index++) {
        if (index) appstore_append_text(output, size, ", ");
        appstore_append_text(output, size, info->dependencies[index]);
    }
}

static void appstore_blockers_text(char* output, uint32_t size) {
    if (appstore_action.blocker_count == 0U) {
        appstore_copy_text(output, size, "nenhum");
        return;
    }
    output[0] = '\0';
    for (uint32_t index = 0; index < appstore_action.blocker_count; index++) {
        if (index) appstore_append_text(output, size, ", ");
        appstore_append_text(output, size, appstore_action.blocker_ids[index]);
    }
    if (appstore_action.blocker_overflow) appstore_append_text(output, size, ", ...");
}

static int appstore_plan_is_downgrade(void) {
    const app_package_plan_t* plan = &appstore_action.plan;
    const app_package_plan_entry_t* target;
    int comparison = 0;

    if (plan->entry_count == 0U || plan->target_index >= plan->entry_count) {
        return 0;
    }
    target = &plan->entries[plan->target_index];
    if (target->action != APP_PACKAGE_PLAN_ACTION_UPDATE ||
        !target->from_version[0] || !target->to_version[0]) return 0;
    return app_package_compare_versions(target->to_version, target->from_version,
                                        &comparison) == OK && comparison < 0;
}

static uint32_t appstore_visible_count(void) {
    uint32_t count = 0;

    if (appstore_tab == APPSTORE_TAB_REMOTE) return appstore_remote_count;
    for (uint32_t index = 0; index < appstore_entry_count; index++) {
        if (appstore_tab == APPSTORE_TAB_CATALOG &&
            appstore_entries[index].has_source) count++;
        if (appstore_tab == APPSTORE_TAB_INSTALLED &&
            appstore_entries[index].has_installed) count++;
    }
    return count;
}

static int appstore_visible_index(uint32_t visible_index) {
    uint32_t current = 0;

    if (appstore_tab == APPSTORE_TAB_REMOTE) {
        return visible_index < appstore_remote_count ? (int)visible_index : -1;
    }
    for (uint32_t index = 0; index < appstore_entry_count; index++) {
        int visible = appstore_tab == APPSTORE_TAB_CATALOG ?
                      appstore_entries[index].has_source :
                      appstore_entries[index].has_installed;

        if (!visible) continue;
        if (current == visible_index) return (int)index;
        current++;
    }
    return -1;
}

static app_catalog_entry_t* appstore_selected_entry(void) {
    if (appstore_tab == APPSTORE_TAB_REMOTE) return 0;
    if (appstore_selected < 0 ||
        (uint32_t)appstore_selected >= appstore_entry_count) return 0;
    return &appstore_entries[appstore_selected];
}

static app_remote_entry_t* appstore_selected_remote_entry(void) {
    if (appstore_tab != APPSTORE_TAB_REMOTE || appstore_selected < 0 ||
        (uint32_t)appstore_selected >= appstore_remote_count) return 0;
    return &appstore_remote_entries[appstore_selected];
}

static const app_package_info_t* appstore_entry_info(
    const app_catalog_entry_t* entry) {
    if (!entry) return 0;
    return entry->has_source ? &entry->source : &entry->installed;
}

static const char* appstore_entry_label(const app_catalog_entry_t* entry) {
    const app_package_info_t* info = appstore_entry_info(entry);

    if (info && info->id[0]) return info->id;
    if (entry && entry->alias[0]) return entry->alias;
    return "N/D";
}

static void appstore_selected_key(appstore_job_t type, char* output,
                                  uint32_t size) {
    app_catalog_entry_t* entry = appstore_selected_entry();
    const app_package_info_t* info = appstore_entry_info(entry);

    if (appstore_tab == APPSTORE_TAB_REMOTE) {
        app_remote_entry_t* remote = appstore_selected_remote_entry();

        appstore_copy_text(output, size, remote ? remote->info.id : "");
        return;
    }

    if (!entry || !info) {
        appstore_copy_text(output, size, "");
        return;
    }
    if ((type == APPSTORE_JOB_VERIFY ||
         type == APPSTORE_JOB_PREFLIGHT_INSTALL ||
         type == APPSTORE_JOB_INSTALL ||
         type == APPSTORE_JOB_PREFLIGHT_UPDATE ||
         type == APPSTORE_JOB_UPDATE) && entry->alias[0]) {
        appstore_copy_text(output, size, entry->alias);
        return;
    }
    appstore_copy_text(output, size, info->id);
}

static void appstore_clear_context(void) {
    appstore_confirm = APPSTORE_CONFIRM_NONE;
    appstore_confirm_key[0] = '\0';
    appstore_confirm_generation = 0U;
}

static void appstore_set_result(appstore_result_t kind, int result,
                                const char* text) {
    appstore_result_kind = kind;
    appstore_last_result = result;
    appstore_copy_text(appstore_result_text, sizeof(appstore_result_text),
                       text);
}

static void appstore_keep_selection_visible(void) {
    int visible = (appstore_gui_height - 198) / APPSTORE_CLASSIC_ROW_HEIGHT;
    uint32_t count = appstore_visible_count();
    int selected_visible = -1;

    if (visible < 1) visible = 1;
    for (uint32_t index = 0; index < count; index++) {
        if (appstore_visible_index(index) == appstore_selected) {
            selected_visible = (int)index;
            break;
        }
    }
    if (selected_visible < 0) appstore_scroll = 0;
    else if (selected_visible < appstore_scroll) appstore_scroll = selected_visible;
    else if (selected_visible >= appstore_scroll + visible) {
        appstore_scroll = selected_visible - visible + 1;
    }
}

static void appstore_select_first_visible(void) {
    int index = appstore_visible_index(0U);

    appstore_selected = index;
    appstore_scroll = 0;
}

static int appstore_restore_selection(const char* alias, const char* id) {
    if (appstore_tab == APPSTORE_TAB_REMOTE) return 0;
    for (uint32_t index = 0; index < appstore_entry_count; index++) {
        app_catalog_entry_t* entry = &appstore_entries[index];
        int visible = appstore_tab == APPSTORE_TAB_CATALOG ?
                      entry->has_source : entry->has_installed;

        if (!visible) continue;
        if ((alias && alias[0] && kstrcmp(entry->alias, alias) == 0) ||
            (id && id[0] &&
             ((entry->has_source && kstrcmp(entry->source.id, id) == 0) ||
              (entry->has_installed &&
               kstrcmp(entry->installed.id, id) == 0)))) {
            appstore_selected = (int)index;
            appstore_keep_selection_visible();
            return 1;
        }
    }
    return 0;
}

static void appstore_change_selection(int direction) {
    uint32_t count = appstore_visible_count();
    int current = -1;

    if (count == 0U) return;
    for (uint32_t index = 0; index < count; index++) {
        if (appstore_visible_index(index) == appstore_selected) {
            current = (int)index;
            break;
        }
    }
    if (current < 0) current = 0;
    else if (direction < 0) current = current ? current - 1 : (int)count - 1;
    else current = current + 1 < (int)count ? current + 1 : 0;
    appstore_selected = appstore_visible_index((uint32_t)current);
    appstore_generation++;
    appstore_clear_context();
    appstore_keep_selection_visible();
}

static void appstore_refresh_snapshot(void) {
    app_catalog_entry_t* selected_entry = appstore_selected_entry();
    const app_package_info_t* selected_info =
        appstore_entry_info(selected_entry);
    char selected_alias[APP_CATALOG_ALIAS_SIZE];
    char selected_id[APP_PACKAGE_ID_SIZE];
    uint32_t count = 0;

    appstore_copy_text(selected_alias, sizeof(selected_alias),
                       selected_entry ? selected_entry->alias : "");
    appstore_copy_text(selected_id, sizeof(selected_id),
                       selected_info ? selected_info->id : "");
    appstore_entry_count = 0U;
    kmemset(&appstore_status, 0, sizeof(appstore_status));
    if (app_catalog_refresh() != OK ||
        app_catalog_get_status(&appstore_status) != OK ||
        app_catalog_get_count(&count) != OK) {
        LOG_ERROR("APPSTORE", "Falha ao atualizar catalogo da interface");
        appstore_set_result(APPSTORE_RESULT_REFRESH, ERR_UNAVAILABLE,
                            "Catalogo indisponivel");
        return;
    }
    for (uint32_t index = 0; index < count &&
         index < APP_CATALOG_MAX_ENTRIES; index++) {
        if (app_catalog_get_entry(index, &appstore_entries[index]) == OK) {
            appstore_entry_count++;
        }
    }
    if (!appstore_restore_selection(selected_alias, selected_id)) {
        appstore_select_first_visible();
    }
    (void)app_remote_refresh_provenance();
    appstore_set_result(APPSTORE_RESULT_REFRESH, OK, "Catalogo atualizado");
}

static void appstore_refresh_remote_snapshot(void) {
    app_remote_entry_t* selected = appstore_selected_remote_entry();
    char selected_id[APP_PACKAGE_ID_SIZE];
    uint32_t count = 0U;

    appstore_copy_text(selected_id, sizeof(selected_id),
                       selected ? selected->info.id : "");
    appstore_remote_count = 0U;
    kmemset(&appstore_remote_status, 0, sizeof(appstore_remote_status));
    if (app_remote_get_status(&appstore_remote_status) != OK) {
        LOG_ERROR("APPSTORE", "Status remoto indisponivel para a interface");
        return;
    }
    if (app_remote_get_count(&count) != OK) {
        if (appstore_tab == APPSTORE_TAB_REMOTE) appstore_select_first_visible();
        return;
    }
    for (uint32_t index = 0; index < count &&
         index < APP_REMOTE_MAX_ENTRIES; index++) {
        if (app_remote_get_entry(index,
                                 &appstore_remote_entries[index]) == OK) {
            appstore_remote_count++;
        }
    }
    appstore_selected = -1;
    for (uint32_t index = 0; index < appstore_remote_count; index++) {
        if (selected_id[0] && kstrcmp(
                appstore_remote_entries[index].info.id, selected_id) == 0) {
            appstore_selected = (int)index;
            break;
        }
    }
    if (appstore_selected < 0) appstore_select_first_visible();
    appstore_keep_selection_visible();
}

static void appstore_queue_job(appstore_job_t type) {
    uint32_t woken = 0U;

    if (appstore_busy || appstore_queued_type != APPSTORE_JOB_NONE) {
        LOG_WARN("APPSTORE", "Operacao da App Store ja esta pendente");
        return;
    }
    appstore_job.type = type;
    appstore_job.generation = appstore_generation;
    appstore_selected_key(type, appstore_job.key, sizeof(appstore_job.key));
    if (!appstore_job.key[0] && type != APPSTORE_JOB_REFRESH &&
        type != APPSTORE_JOB_REMOTE_ENABLE &&
        type != APPSTORE_JOB_REMOTE_CHECK) {
        LOG_WARN("APPSTORE", "Operacao solicitada sem item selecionado");
        appstore_job.type = APPSTORE_JOB_NONE;
        appstore_queued_type = APPSTORE_JOB_NONE;
        appstore_set_result(APPSTORE_RESULT_NONE, ERR_NOT_FOUND,
                            "Nenhum aplicativo selecionado");
        return;
    }
    appstore_queued_type = type;
    appstore_busy = 1;
    if (appstore_worker_process &&
        process_wake_channel(&appstore_worker_process->ipc_wait_channel,
                             WAIT_WAKE_ONE, WAIT_REASON_EVENT,
                             &woken) != OK) {
        LOG_WARN("APPSTORE", "Falha ao acordar worker da App Store");
    }
    appstore_draw();
}

static void appstore_request_refresh(void) {
    appstore_generation++;
    appstore_clear_context();
    appstore_queue_job(appstore_tab == APPSTORE_TAB_REMOTE ?
                       APPSTORE_JOB_REMOTE_CHECK : APPSTORE_JOB_REFRESH);
}

static void appstore_request_remote_enable(void) {
    appstore_queue_job(APPSTORE_JOB_REMOTE_ENABLE);
}

static void appstore_request_remote_fetch(void) {
    if (!appstore_selected_remote_entry() ||
        !appstore_remote_status.enabled ||
        !appstore_remote_status.network_ready ||
        !appstore_remote_status.catalog_available) {
        appstore_set_result(APPSTORE_RESULT_REMOTE, ERR_NOT_FOUND,
                            "Download remoto indisponivel");
        return;
    }
    appstore_queue_job(APPSTORE_JOB_REMOTE_PREFLIGHT_FETCH);
}

static int appstore_can(const app_catalog_entry_t* entry,
                        app_catalog_capability_t capability) {
    return entry && (entry->capabilities & (uint32_t)capability) != 0U;
}

static void appstore_request_verify(void) {
    app_catalog_entry_t* entry = appstore_selected_entry();

    if (!appstore_can(entry, APP_CATALOG_CAPABILITY_VERIFY)) {
        appstore_set_result(APPSTORE_RESULT_VERIFY, ERR_STATE,
                            "Verificacao indisponivel para o item");
        return;
    }
    appstore_queue_job(APPSTORE_JOB_VERIFY);
}

static void appstore_request_install(void) {
    app_catalog_entry_t* entry = appstore_selected_entry();
    app_remote_entry_t* remote = appstore_selected_remote_entry();

    if (appstore_tab == APPSTORE_TAB_REMOTE) {
        if (!appstore_remote_status.enabled || !remote ||
            !remote->cached || remote->installed) {
            appstore_set_result(APPSTORE_RESULT_REMOTE, ERR_STATE,
                                "Instalacao remota requer plano em cache");
            return;
        }
        appstore_queue_job(APPSTORE_JOB_REMOTE_PREFLIGHT_INSTALL);
        return;
    }

    if (!appstore_can(entry, APP_CATALOG_CAPABILITY_INSTALL)) {
        appstore_set_result(APPSTORE_RESULT_INSTALL, ERR_STATE,
                            "Instalacao indisponivel para o item");
        return;
    }
    appstore_queue_job(APPSTORE_JOB_PREFLIGHT_INSTALL);
}

static void appstore_request_remove(void) {
    app_catalog_entry_t* entry = appstore_selected_entry();

    if (!appstore_can(entry, APP_CATALOG_CAPABILITY_REMOVE)) {
        appstore_set_result(APPSTORE_RESULT_REMOVE, ERR_STATE,
                            "Remocao indisponivel para o item");
        return;
    }
    appstore_queue_job(APPSTORE_JOB_PREFLIGHT_REMOVE);
}

static void appstore_request_update(void) {
    app_catalog_entry_t* entry = appstore_selected_entry();
    app_remote_entry_t* remote = appstore_selected_remote_entry();

    if (appstore_tab == APPSTORE_TAB_REMOTE) {
        if (!appstore_remote_status.enabled || !remote ||
            !remote->cached || !remote->installed ||
            (remote->state != APP_REMOTE_ENTRY_UPDATE_AVAILABLE &&
             remote->state != APP_REMOTE_ENTRY_DOWNGRADE)) {
            appstore_set_result(APPSTORE_RESULT_REMOTE, ERR_STATE,
                                "Atualizacao remota requer plano em cache");
            return;
        }
        appstore_queue_job(APPSTORE_JOB_REMOTE_PREFLIGHT_UPDATE);
        return;
    }

    if (!appstore_can(entry, APP_CATALOG_CAPABILITY_UPDATE)) {
        appstore_set_result(APPSTORE_RESULT_UPDATE, ERR_STATE,
                            "Atualizacao indisponivel para o item");
        return;
    }
    appstore_queue_job(APPSTORE_JOB_PREFLIGHT_UPDATE);
}

static void appstore_request_rollback(void) {
    app_catalog_entry_t* entry = appstore_selected_entry();
    app_remote_entry_t* remote = appstore_selected_remote_entry();

    if (appstore_tab == APPSTORE_TAB_REMOTE) {
        if (!remote || !remote->installed) {
            appstore_set_result(APPSTORE_RESULT_ROLLBACK, ERR_STATE,
                                "Rollback indisponivel para o item");
            return;
        }
        appstore_queue_job(APPSTORE_JOB_PREFLIGHT_ROLLBACK);
        return;
    }
    if (!entry || !entry->has_installed) {
        appstore_set_result(APPSTORE_RESULT_ROLLBACK, ERR_STATE,
                            "Rollback indisponivel para o item");
        return;
    }
    appstore_queue_job(APPSTORE_JOB_PREFLIGHT_ROLLBACK);
}

static void appstore_request_run(void) {
    app_catalog_entry_t* entry = appstore_selected_entry();
    app_remote_entry_t* remote = appstore_selected_remote_entry();

    if (appstore_tab == APPSTORE_TAB_REMOTE) {
        if (!remote || !remote->installed) {
            appstore_set_result(APPSTORE_RESULT_RUN, ERR_STATE,
                                "Aplicativo remoto ainda nao esta instalado");
            return;
        }
        appstore_queue_job(APPSTORE_JOB_RUN);
        return;
    }

    if (!appstore_can(entry, APP_CATALOG_CAPABILITY_RUN)) {
        appstore_set_result(APPSTORE_RESULT_RUN, ERR_STATE,
                            "Execucao indisponivel para o item");
        return;
    }
    appstore_queue_job(APPSTORE_JOB_RUN);
}

static void appstore_confirm_action(void) {
    appstore_job_t job;
    char selected_key[APP_CATALOG_ALIAS_SIZE];

    if (appstore_confirm == APPSTORE_CONFIRM_NONE ||
        appstore_confirm_generation != appstore_generation) {
        appstore_clear_context();
        return;
    }
    if (appstore_confirm == APPSTORE_CONFIRM_REMOTE_FETCH) {
        job = APPSTORE_JOB_REMOTE_FETCH;
    } else if (appstore_confirm == APPSTORE_CONFIRM_REMOTE_INSTALL) {
        job = APPSTORE_JOB_REMOTE_INSTALL;
    } else if (appstore_confirm == APPSTORE_CONFIRM_REMOTE_UPDATE) {
        job = APPSTORE_JOB_REMOTE_UPDATE;
    } else if (appstore_confirm == APPSTORE_CONFIRM_INSTALL) {
        job = APPSTORE_JOB_INSTALL;
    } else if (appstore_confirm == APPSTORE_CONFIRM_REMOVE) {
        job = APPSTORE_JOB_REMOVE;
    } else if (appstore_confirm == APPSTORE_CONFIRM_UPDATE) {
        job = APPSTORE_JOB_UPDATE;
    } else {
        job = APPSTORE_JOB_ROLLBACK;
    }
    appstore_selected_key(job, selected_key, sizeof(selected_key));
    if (kstrcmp(selected_key, appstore_confirm_key) != 0) {
        LOG_WARN("APPSTORE", "Confirmacao descartada por selecao divergente");
        appstore_clear_context();
        return;
    }
    appstore_clear_context();
    appstore_queue_job(job);
}

static void appstore_worker_verify(const char* alias) {
    int result = app_package_verify_file(alias, &appstore_verification);

    kmemset(&appstore_action, 0, sizeof(appstore_action));
    appstore_set_result(APPSTORE_RESULT_VERIFY, result,
                        result == OK ? "Pacote verificado" :
                        "Verificacao bloqueada");
}

static int appstore_worker_build_plan(const char* key, int update) {
    app_package_plan_t* plan = &appstore_action.plan;
    int result;

    kmemset(&appstore_action, 0, sizeof(appstore_action));
    result = update ? app_catalog_build_update_plan(key, 1, plan) :
                      app_catalog_build_install_plan(key, plan);
    if (result != OK) {
        appstore_action.reason = plan->reason ? plan->reason :
                                 APP_PACKAGE_ACTION_REASON_PLAN_INCOMPLETE;
    }
    return result;
}

static void appstore_worker_preflight_install(const char* alias) {
    int result = appstore_worker_build_plan(alias, 0);

    if (result == OK) {
        result = app_package_preflight_plan(&appstore_action.plan,
                                            &appstore_action);
    }

    appstore_set_result(APPSTORE_RESULT_INSTALL, result,
                        result == OK ? "Confirmar instalacao" :
                        "Preflight de instalacao bloqueado");
}

static void appstore_worker_preflight_update(const char* key) {
    int result = appstore_worker_build_plan(key, 1);

    if (result == OK) {
        result = app_package_preflight_plan(&appstore_action.plan,
                                            &appstore_action);
    }
    appstore_set_result(APPSTORE_RESULT_UPDATE, result,
                        result == OK ? "Confirmar atualizacao" :
                        "Preflight de atualizacao bloqueado");
}

static void appstore_worker_preflight_remove(const char* id) {
    int result = app_package_preflight_remove(id, &appstore_action);

    appstore_set_result(APPSTORE_RESULT_REMOVE, result,
                        result == OK ? "Confirmar remocao" :
                        "Preflight de remocao bloqueado");
}

static void appstore_worker_install(const char* alias) {
    int result = appstore_worker_build_plan(alias, 0);
    appstore_result_t result_kind = APPSTORE_RESULT_INSTALL;
    char result_text[APPSTORE_TEXT_SIZE];

    if (result == OK) {
        result = app_package_apply_plan_confirmed(&appstore_action.plan,
                                                  &appstore_action);
    }
    appstore_copy_text(result_text, sizeof(result_text),
                       result == OK ? "Aplicativo instalado" :
                       "Instalacao bloqueada");
    appstore_refresh_snapshot();
    appstore_set_result(result_kind, result, result_text);
}

static void appstore_worker_update(const char* key) {
    int result = appstore_worker_build_plan(key, 1);
    char result_text[APPSTORE_TEXT_SIZE];

    if (result == OK) {
        result = app_package_apply_plan_confirmed(&appstore_action.plan,
                                                  &appstore_action);
    }
    appstore_copy_text(result_text, sizeof(result_text),
                       result == OK ? "Aplicativo atualizado" :
                       "Atualizacao bloqueada");
    appstore_refresh_snapshot();
    appstore_set_result(APPSTORE_RESULT_UPDATE, result, result_text);
}

static void appstore_worker_remove(const char* id) {
    int result = app_package_remove_confirmed(id, &appstore_action);
    appstore_result_t result_kind = APPSTORE_RESULT_REMOVE;
    char result_text[APPSTORE_TEXT_SIZE];

    appstore_copy_text(result_text, sizeof(result_text),
                       result == OK ? "Aplicativo removido" :
                       "Remocao bloqueada");
    appstore_refresh_snapshot();
    appstore_set_result(result_kind, result, result_text);
}

static void appstore_worker_preflight_rollback(const char* id) {
    int result = app_package_preflight_rollback(id, &appstore_action);

    appstore_set_result(APPSTORE_RESULT_ROLLBACK, result,
                        result == OK ? "Confirmar rollback" :
                        "Preflight de rollback bloqueado");
}

static void appstore_worker_rollback(const char* id) {
    int result = app_package_rollback_confirmed(id, &appstore_action);
    char result_text[APPSTORE_TEXT_SIZE];

    appstore_copy_text(result_text, sizeof(result_text),
                       result == OK ? "Rollback concluido" :
                       "Rollback bloqueado");
    appstore_refresh_snapshot();
    if (appstore_tab == APPSTORE_TAB_REMOTE) {
        appstore_refresh_remote_snapshot();
    }
    appstore_set_result(APPSTORE_RESULT_ROLLBACK, result, result_text);
}

static void appstore_worker_run(const char* id) {
    int result = app_package_run_installed(id, 0, &appstore_last_pid,
                                           &appstore_action);

    appstore_set_result(APPSTORE_RESULT_RUN, result,
                        result == OK ? "Aplicativo iniciado" :
                        "Execucao bloqueada");
    if (result == OK && appstore_hosted) {
        result = wm_close_hosted_app(WM_APP_APPSTORE);
        if (result != OK) {
            LOG_WARN("APPSTORE", "ZAPP iniciou, mas a janela nao fechou");
            return;
        }
        LOG_INFO("APPSTORE", "ZAPP iniciado; F12 cancela e devolve foco ao Shell");
    }
}

static void appstore_worker_remote_enable(void) {
    app_remote_status_t status;
    int enable_requested = 0;
    int result;

    result = app_remote_get_status(&status);
    if (result == OK) {
        enable_requested = !status.enabled;
        result = enable_requested ? app_remote_enable() : app_remote_disable();
    }
    appstore_refresh_remote_snapshot();
    appstore_set_result(APPSTORE_RESULT_REMOTE, result,
                        result != OK ? "Controle remoto bloqueado" :
                        enable_requested ? "Repositorio remoto habilitado" :
                                           "Repositorio remoto desabilitado");
}

static void appstore_worker_remote_check(void) {
    int result;

    kmemset(&appstore_remote_options, 0, sizeof(appstore_remote_options));
    result = app_remote_check(0, &appstore_remote_options,
                              &appstore_remote_result);
    appstore_refresh_remote_snapshot();
    appstore_set_result(APPSTORE_RESULT_REMOTE, result,
                        result == OK ? "Catalogo remoto autenticado" :
                        "Consulta remota bloqueada");
}

static void appstore_worker_remote_fetch(const char* id, int confirmed) {
    int result;

    kmemset(&appstore_remote_options, 0, sizeof(appstore_remote_options));
    result = app_remote_fetch(id, 0, confirmed, &appstore_remote_options,
                              &appstore_remote_result);
    kmemset(&appstore_action, 0, sizeof(appstore_action));
    appstore_action.plan = appstore_remote_result.plan;
    appstore_refresh_remote_snapshot();
    appstore_set_result(APPSTORE_RESULT_REMOTE, result,
                        result == OK ?
                        (confirmed ? "Plano remoto em cache" :
                                     "Confirmar download remoto") :
                        "Download remoto bloqueado");
}

static void appstore_worker_remote_apply(const char* id, int update,
                                         int confirmed) {
    int result;

    kmemset(&appstore_remote_options, 0, sizeof(appstore_remote_options));
    appstore_remote_options.allow_downgrade = update ? 1U : 0U;
    result = app_remote_apply_cached(
        id, update, confirmed, &appstore_remote_options,
        &appstore_remote_result);
    appstore_action = appstore_remote_result.package_action;
    if (!appstore_action.plan.entry_count) {
        appstore_action.plan = appstore_remote_result.plan;
    }
    if (confirmed) appstore_refresh_snapshot();
    appstore_refresh_remote_snapshot();
    appstore_set_result(APPSTORE_RESULT_REMOTE, result,
                        result == OK ?
                        (confirmed ? "Plano remoto aplicado" :
                                     "Confirmar aplicacao remota") :
                        "Aplicacao remota bloqueada");
}

static void appstore_worker_finish(void) {
    appstore_job_t type = appstore_running_job.type;
    int stale = appstore_running_job.generation != appstore_generation;

    if (!stale && appstore_last_result == OK &&
        type == APPSTORE_JOB_PREFLIGHT_INSTALL) {
        appstore_confirm = APPSTORE_CONFIRM_INSTALL;
        appstore_copy_text(appstore_confirm_key, sizeof(appstore_confirm_key),
                           appstore_running_job.key);
        appstore_confirm_generation = appstore_generation;
    } else if (!stale && appstore_last_result == OK &&
               type == APPSTORE_JOB_PREFLIGHT_REMOVE) {
        appstore_confirm = APPSTORE_CONFIRM_REMOVE;
        appstore_copy_text(appstore_confirm_key, sizeof(appstore_confirm_key),
                           appstore_running_job.key);
        appstore_confirm_generation = appstore_generation;
    } else if (!stale && appstore_last_result == OK &&
               type == APPSTORE_JOB_PREFLIGHT_UPDATE) {
        appstore_confirm = APPSTORE_CONFIRM_UPDATE;
        appstore_copy_text(appstore_confirm_key, sizeof(appstore_confirm_key),
                           appstore_running_job.key);
        appstore_confirm_generation = appstore_generation;
    } else if (!stale && appstore_last_result == OK &&
               type == APPSTORE_JOB_PREFLIGHT_ROLLBACK) {
        appstore_confirm = APPSTORE_CONFIRM_ROLLBACK;
        appstore_copy_text(appstore_confirm_key, sizeof(appstore_confirm_key),
                           appstore_running_job.key);
        appstore_confirm_generation = appstore_generation;
    } else if (!stale && appstore_last_result == OK &&
               type == APPSTORE_JOB_REMOTE_PREFLIGHT_FETCH) {
        appstore_confirm = APPSTORE_CONFIRM_REMOTE_FETCH;
        appstore_copy_text(appstore_confirm_key, sizeof(appstore_confirm_key),
                           appstore_running_job.key);
        appstore_confirm_generation = appstore_generation;
    } else if (!stale && appstore_last_result == OK &&
               type == APPSTORE_JOB_REMOTE_PREFLIGHT_INSTALL) {
        appstore_confirm = APPSTORE_CONFIRM_REMOTE_INSTALL;
        appstore_copy_text(appstore_confirm_key, sizeof(appstore_confirm_key),
                           appstore_running_job.key);
        appstore_confirm_generation = appstore_generation;
    } else if (!stale && appstore_last_result == OK &&
               type == APPSTORE_JOB_REMOTE_PREFLIGHT_UPDATE) {
        appstore_confirm = APPSTORE_CONFIRM_REMOTE_UPDATE;
        appstore_copy_text(appstore_confirm_key, sizeof(appstore_confirm_key),
                           appstore_running_job.key);
        appstore_confirm_generation = appstore_generation;
    }
    appstore_busy = 0;
    appstore_running_job.type = APPSTORE_JOB_NONE;
    appstore_draw();
}

static void appstore_worker_main(void) {
    while (1) {
        if (appstore_queued_type == APPSTORE_JOB_NONE) {
            wait_reason_t wait_reason = WAIT_REASON_NONE;

            if (ipc_wait(WAIT_TIMEOUT_INFINITE, &wait_reason) != OK) {
                LOG_WARN("APPSTORE", "Falha na espera do worker da App Store");
                process_yield();
            }
            continue;
        }
        appstore_running_job = appstore_job;
        appstore_queued_type = APPSTORE_JOB_NONE;
        appstore_job.type = APPSTORE_JOB_NONE;
        if (appstore_running_job.type == APPSTORE_JOB_REFRESH) {
            appstore_refresh_snapshot();
        } else if (appstore_running_job.type == APPSTORE_JOB_VERIFY) {
            appstore_worker_verify(appstore_running_job.key);
        } else if (appstore_running_job.type == APPSTORE_JOB_PREFLIGHT_INSTALL) {
            appstore_worker_preflight_install(appstore_running_job.key);
        } else if (appstore_running_job.type == APPSTORE_JOB_INSTALL) {
            appstore_worker_install(appstore_running_job.key);
        } else if (appstore_running_job.type == APPSTORE_JOB_PREFLIGHT_UPDATE) {
            appstore_worker_preflight_update(appstore_running_job.key);
        } else if (appstore_running_job.type == APPSTORE_JOB_UPDATE) {
            appstore_worker_update(appstore_running_job.key);
        } else if (appstore_running_job.type == APPSTORE_JOB_PREFLIGHT_REMOVE) {
            appstore_worker_preflight_remove(appstore_running_job.key);
        } else if (appstore_running_job.type == APPSTORE_JOB_REMOVE) {
            appstore_worker_remove(appstore_running_job.key);
        } else if (appstore_running_job.type == APPSTORE_JOB_PREFLIGHT_ROLLBACK) {
            appstore_worker_preflight_rollback(appstore_running_job.key);
        } else if (appstore_running_job.type == APPSTORE_JOB_ROLLBACK) {
            appstore_worker_rollback(appstore_running_job.key);
        } else if (appstore_running_job.type == APPSTORE_JOB_RUN) {
            appstore_worker_run(appstore_running_job.key);
        } else if (appstore_running_job.type == APPSTORE_JOB_REMOTE_ENABLE) {
            appstore_worker_remote_enable();
        } else if (appstore_running_job.type == APPSTORE_JOB_REMOTE_CHECK) {
            appstore_worker_remote_check();
        } else if (appstore_running_job.type ==
                   APPSTORE_JOB_REMOTE_PREFLIGHT_FETCH) {
            appstore_worker_remote_fetch(appstore_running_job.key, 0);
        } else if (appstore_running_job.type == APPSTORE_JOB_REMOTE_FETCH) {
            appstore_worker_remote_fetch(appstore_running_job.key, 1);
        } else if (appstore_running_job.type ==
                   APPSTORE_JOB_REMOTE_PREFLIGHT_INSTALL) {
            appstore_worker_remote_apply(appstore_running_job.key, 0, 0);
        } else if (appstore_running_job.type == APPSTORE_JOB_REMOTE_INSTALL) {
            appstore_worker_remote_apply(appstore_running_job.key, 0, 1);
        } else if (appstore_running_job.type ==
                   APPSTORE_JOB_REMOTE_PREFLIGHT_UPDATE) {
            appstore_worker_remote_apply(appstore_running_job.key, 1, 0);
        } else if (appstore_running_job.type == APPSTORE_JOB_REMOTE_UPDATE) {
            appstore_worker_remote_apply(appstore_running_job.key, 1, 1);
        } else {
            appstore_set_result(APPSTORE_RESULT_NONE, ERR_INVALID,
                                "Worker recebeu operacao invalida");
            LOG_ERROR("APPSTORE", "Worker recebeu operacao invalida");
        }
        appstore_worker_finish();
        process_yield();
    }
}

static uint8_t appstore_entry_color(const app_catalog_entry_t* entry) {
    if (!entry) return 0x08;
    if (entry->state == APP_CATALOG_STATE_INVALID) return 0x0C;
    if (entry->state == APP_CATALOG_STATE_BLOCKED) return 0x0E;
    return 0x0A;
}

static uint32_t appstore_gui_entry_color(const app_catalog_entry_t* entry) {
    if (!entry) return GUI_MODERN_COLOR_BORDER_INACTIVE;
    if (entry->state == APP_CATALOG_STATE_INVALID) return 0x00D65A5AU;
    if (entry->state == APP_CATALOG_STATE_BLOCKED) return 0x00E0A850U;
    return GUI_MODERN_COLOR_TEXT;
}

static const char* appstore_entry_trust(const app_catalog_entry_t* entry,
                                        const app_package_info_t* info) {
    if (entry && entry->installed.id[0]) {
        if (!app_remote_is_provenance_available()) return "N/D";
        if (info && app_remote_get_installed_trust(
                entry->installed.id, entry->installed.version)) {
            return "REMOTO / AUTENTICADO (TESTE)";
        }
    }
    return entry && entry->has_source ? "LOCAL / NAO ASSINADO" : "N/D";
}

static void appstore_simple_draw_entries(void) {
    uint32_t count = appstore_visible_count();
    int max_rows = SCREEN_ROWS - 10;

    video_draw_box(1, 4, APPSTORE_SIMPLE_LIST_WIDTH, SCREEN_ROWS - 8, 0x08);
    video_print_at(3, 4, appstore_tab == APPSTORE_TAB_CATALOG ?
                   " Catalogo local " : " Instalados ", 0x0B);
    if (count == 0U) {
        video_print_at(3, 7, "(vazio)", 0x08);
        return;
    }
    for (int row = 0; row < max_rows; row++) {
        int index = appstore_visible_index((uint32_t)(appstore_scroll + row));
        app_catalog_entry_t* entry;
        int color;

        if (index < 0) break;
        entry = &appstore_entries[index];
        color = index == appstore_selected ? 0x1F : appstore_entry_color(entry);
        if (index == appstore_selected) {
            video_fill_rect(2, 6 + row, APPSTORE_SIMPLE_LIST_WIDTH - 2,
                            1, ' ', 0x1F);
        }
        video_print_at(3, 6 + row, appstore_entry_label(entry), color);
    }
}

static void appstore_simple_draw_details(void) {
    app_catalog_entry_t* entry = appstore_selected_entry();
    const app_package_info_t* info = appstore_entry_info(entry);
    char blockers[APPSTORE_TEXT_SIZE];
    int x = APPSTORE_SIMPLE_LIST_WIDTH + 3;

    if (!entry || !info) {
        video_print_at(x, 6, "Nenhum aplicativo selecionado", 0x08);
        return;
    }
    video_print_at(x, 6, info->name, 0x0B);
    video_print_at(x, 8, "ID: ", 0x07);
    video_print(info->id, 0x0A);
    video_print("  Estado: ", 0x07);
    video_print(app_catalog_state_name(entry->state), appstore_entry_color(entry));
    video_print_at(x, 10, "Fonte: ", 0x07);
    video_print(entry->has_source ? entry->alias : "N/D", 0x07);
    video_print("  Versao: ", 0x07);
    video_print(entry->has_source ? entry->source.version :
                entry->installed.version, 0x07);
    video_print_at(x, 12, "Motivo: ", 0x07);
    video_print(app_catalog_reason_name(entry->reason),
                entry->reason == APP_CATALOG_REASON_NONE ? 0x0A : 0x0E);
    video_print_at(x, 14, "Confianca: ", 0x07);
    video_print(appstore_entry_trust(entry, info), 0x0E);
    video_print_at(x, 16, "Dependencias: ", 0x07);
    if (info->dependency_count == 0U) video_print("nenhuma", 0x08);
    for (uint32_t index = 0; index < info->dependency_count; index++) {
        if (index) video_print(", ", 0x07);
        video_print(info->dependencies[index], 0x0E);
    }
    appstore_blockers_text(blockers, sizeof(blockers));
    video_print_at(x, 18, "Acao: ", 0x07);
    video_print(app_package_action_reason_name(appstore_action.reason),
                appstore_action.reason == APP_PACKAGE_ACTION_REASON_NONE ?
                0x08 : 0x0E);
    video_print_at(x, 19, "Bloqueadores: ", 0x07);
    video_print(blockers, appstore_action.blocker_count ? 0x0E : 0x08);
    video_print_at(x, 21, appstore_result_text,
                   appstore_last_result == OK ? 0x0A : 0x0C);
}

static void appstore_simple_draw_confirmation(void) {
    const char* question;
    char plan[APPSTORE_TEXT_SIZE];

    if (appstore_confirm == APPSTORE_CONFIRM_NONE) return;
    question = appstore_confirm == APPSTORE_CONFIRM_UPDATE &&
               appstore_plan_is_downgrade() ?
               "Confirmar downgrade diagnostico?" :
               appstore_confirm == APPSTORE_CONFIRM_REMOTE_FETCH ?
               "Baixar e publicar plano autenticado?" :
               appstore_confirm == APPSTORE_CONFIRM_REMOTE_INSTALL ?
               "Instalar plano remoto em cache?" :
               appstore_confirm == APPSTORE_CONFIRM_REMOTE_UPDATE &&
               appstore_plan_is_downgrade() ?
               "Confirmar downgrade remoto diagnostico?" :
               appstore_confirm == APPSTORE_CONFIRM_REMOTE_UPDATE ?
               "Atualizar pelo plano remoto em cache?" :
               appstore_confirm == APPSTORE_CONFIRM_INSTALL ?
               "Confirmar instalacao?" :
               appstore_confirm == APPSTORE_CONFIRM_REMOVE ?
               "Confirmar remocao?" :
               appstore_confirm == APPSTORE_CONFIRM_UPDATE ?
               "Confirmar atualizacao?" : "Confirmar rollback?";
    plan[0] = '\0';
    for (uint32_t index = 0; index < appstore_action.plan.entry_count;
         index++) {
        if (index) appstore_append_text(plan, sizeof(plan), " -> ");
        appstore_append_text(plan, sizeof(plan),
                             appstore_action.plan.entries[index].id);
    }
    video_fill_rect(22, SCREEN_ROWS - 9, 58, 6, ' ', 0x1E);
    video_draw_box(22, SCREEN_ROWS - 9, 58, 6, 0x0E);
    video_print_at(24, SCREEN_ROWS - 8, question, 0x1E);
    video_print_at(24, SCREEN_ROWS - 6, plan[0] ? plan : "Item selecionado",
                   0x1E);
    video_print_at(24, SCREEN_ROWS - 4, "Enter confirma | Esc cancela", 0x1E);
}

static void appstore_draw_simple(void) {
    static const char* names[3] = {
        " Catalogo ", " Instalados ", " Detalhes "
    };

    video_begin_update();
    video_fill_rect(0, 0, SCREEN_COLS, SCREEN_ROWS, ' ', 0x07);
    video_fill_rect(0, 0, SCREEN_COLS, 1, ' ', 0x1F);
    video_print_at(2, 0, "ZephyrOS App Store", 0x1F);
    for (int index = 0; index < 3; index++) {
        video_print_at(2 + index * 15, 2, names[index],
                       (int)appstore_tab == index ? 0x1F : 0x07);
    }
    if (appstore_tab == APPSTORE_TAB_DETAILS) appstore_simple_draw_details();
    else {
        appstore_simple_draw_entries();
        appstore_simple_draw_details();
    }
    video_print_at(2, SCREEN_ROWS - 2,
                   appstore_busy ? "Operacao em andamento..." :
                   "Tab Setas F5 V I U A R B Enter/Esc",
                   0x70);
    appstore_simple_draw_confirmation();
    taskbar_draw();
    video_end_update();
}

static void appstore_gui_draw_tabs(int x, int y) {
    static const char* names[APPSTORE_TAB_COUNT] = {
        "Catalogo", "Instalados", "Detalhes", "Remoto"
    };

    for (int index = 0; index < APPSTORE_TAB_COUNT; index++) {
        gui_draw_modern_button((uint32_t)(x + index * 124), (uint32_t)y,
                               116, APPSTORE_BUTTON_HEIGHT, names[index],
                               (int)appstore_tab == index ? GUI_BUTTON_STATE_PRESSED :
                                                       GUI_BUTTON_STATE_NORMAL);
    }
}

static void appstore_gui_draw_entries(int x, int y, int width, int height) {
    uint32_t count = appstore_visible_count();
    int list_width = 230;
    int visible = (height - 54) / APPSTORE_CLASSIC_ROW_HEIGHT;

    appstore_gui_draw_surface(x, y, list_width, height,
                              GUI_MODERN_COLOR_WINDOW,
                              GUI_MODERN_COLOR_BORDER_INACTIVE);
    if (count == 0U) {
        gui_draw_text((uint32_t)(x + 14), (uint32_t)(y + 16), "Nenhum item",
                      GUI_MODERN_COLOR_BORDER_INACTIVE);
    }
    for (int row = 0; row < visible; row++) {
        int index = appstore_visible_index((uint32_t)(appstore_scroll + row));
        app_catalog_entry_t* entry;
        int row_y = y + 12 + row * APPSTORE_CLASSIC_ROW_HEIGHT;

        if (index < 0) break;
        if (appstore_tab == APPSTORE_TAB_REMOTE) {
            app_remote_entry_t* remote = &appstore_remote_entries[index];
            uint32_t color = remote->state == APP_REMOTE_ENTRY_BLOCKED ?
                             0x00D65A5AU :
                             remote->state == APP_REMOTE_ENTRY_UPDATE_AVAILABLE ?
                             0x00E0A850U : GUI_MODERN_COLOR_TEXT;

            if (index == appstore_selected) {
                appstore_gui_draw_surface(x + 6, row_y - 4,
                                          list_width - 12, 22,
                                          GUI_MODERN_COLOR_HOVER,
                                          GUI_MODERN_COLOR_ACCENT);
            }
            gui_draw_text((uint32_t)(x + 14), (uint32_t)row_y,
                          remote->info.id, color);
            continue;
        }
        entry = &appstore_entries[index];
        if (index == appstore_selected) {
            appstore_gui_draw_surface(x + 6, row_y - 4, list_width - 12, 22,
                                      GUI_MODERN_COLOR_HOVER,
                                      GUI_MODERN_COLOR_ACCENT);
        }
        gui_draw_text((uint32_t)(x + 14), (uint32_t)row_y,
                      appstore_entry_label(entry),
                      appstore_gui_entry_color(entry));
    }
    appstore_gui_draw_surface(x + list_width + 12, y,
                              width - list_width - 12, height,
                              GUI_MODERN_COLOR_WINDOW,
                              GUI_MODERN_COLOR_BORDER_INACTIVE);
    appstore_gui_draw_details(x + list_width + 30, y + 18);
}

static void appstore_gui_line(int x, int y, const char* label,
                              const char* value, uint32_t color) {
    gui_draw_text((uint32_t)x, (uint32_t)y, label, GUI_MODERN_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 142), (uint32_t)y, value, color);
}

static void appstore_gui_draw_remote_details(int x, int y) {
    app_remote_entry_t* entry = appstore_selected_remote_entry();
    char dependencies[APPSTORE_TEXT_SIZE];
    char size[16];
    const char* installed_version;
    const char* authenticated_label = appstore_gui_width >= 700 ?
        "REMOTO / AUTENTICADO (TESTE)" : "AUTENTICADO (TESTE)";
    uint32_t result_color = appstore_last_result == OK ?
                            0x005FBF7FU : 0x00D65A5AU;

    if (!entry) {
        gui_draw_text((uint32_t)x, (uint32_t)y,
                      appstore_remote_status.enabled ?
                      "Consulte o catalogo remoto com F5" :
                      "Repositorio remoto desabilitado; pressione E",
                      GUI_MODERN_COLOR_BORDER_INACTIVE);
        return;
    }
    installed_version = entry->installed_version[0] ?
                        entry->installed_version : "N/D";
    appstore_u32_text(entry->package_size, size, sizeof(size));
    appstore_dependencies_text(&entry->info, dependencies,
                               sizeof(dependencies));
    gui_draw_text((uint32_t)x, (uint32_t)y, entry->info.name,
                  GUI_MODERN_COLOR_TEXT);
    appstore_gui_line(x, y + 24, "ID", entry->info.id,
                      GUI_MODERN_COLOR_TEXT);
    appstore_gui_line(x, y + 44, "Versao remota", entry->info.version,
                      GUI_MODERN_COLOR_TEXT);
    appstore_gui_line(x, y + 64, "Instalado", installed_version,
                      GUI_MODERN_COLOR_TEXT);
    appstore_gui_line(x, y + 84, "Estado",
                      app_remote_entry_state_name(entry->state),
                      entry->state == APP_REMOTE_ENTRY_BLOCKED ?
                      0x00D65A5AU : GUI_MODERN_COLOR_TEXT);
    gui_draw_text((uint32_t)x, (uint32_t)(y + 104), "Confianca",
                  GUI_MODERN_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 110), (uint32_t)(y + 104),
                  authenticated_label, 0x005FBF7FU);
    appstore_gui_line(x, y + 124, "Cache", entry->cached ? "SIM" : "NAO",
                      entry->cached ? 0x005FBF7FU :
                      GUI_MODERN_COLOR_BORDER_INACTIVE);
    appstore_gui_line(x, y + 144, "Tamanho", size,
                      GUI_MODERN_COLOR_TEXT);
    appstore_gui_line(x, y + 164, "Dependencias", dependencies,
                      GUI_MODERN_COLOR_TEXT);
    appstore_gui_line(x, y + 184, "Resultado",
                      app_remote_reason_name(appstore_remote_result.reason),
                      appstore_remote_result.reason == APP_REMOTE_REASON_NONE ?
                      GUI_MODERN_COLOR_BORDER_INACTIVE : 0x00E0A850U);
    gui_draw_text((uint32_t)x, (uint32_t)(y + 204), "Procedencia",
                  GUI_MODERN_COLOR_TEXT);
    gui_draw_text(
        (uint32_t)(x + 110), (uint32_t)(y + 204),
        !entry->installed ? "N/A" :
        app_remote_get_installed_trust(entry->info.id, installed_version) ?
        authenticated_label : "N/D",
        entry->installed && app_remote_get_installed_trust(
            entry->info.id, installed_version) ? 0x005FBF7FU :
            GUI_MODERN_COLOR_BORDER_INACTIVE);
    gui_draw_text((uint32_t)x, (uint32_t)(y + 240), appstore_result_text,
                  result_color);
}

static void appstore_gui_draw_details(int x, int y) {
    app_catalog_entry_t* entry = appstore_selected_entry();
    const app_package_info_t* info = appstore_entry_info(entry);
    char dependencies[APPSTORE_TEXT_SIZE];
    char blockers[APPSTORE_TEXT_SIZE];
    char size[16];
    uint32_t result_color;

    if (appstore_tab == APPSTORE_TAB_REMOTE) {
        appstore_gui_draw_remote_details(x, y);
        return;
    }
    if (!entry || !info) {
        gui_draw_text((uint32_t)x, (uint32_t)y, "Nenhum aplicativo selecionado",
                      GUI_MODERN_COLOR_BORDER_INACTIVE);
        return;
    }
    appstore_u32_text(entry->source_size, size, sizeof(size));
    appstore_dependencies_text(info, dependencies, sizeof(dependencies));
    appstore_blockers_text(blockers, sizeof(blockers));
    result_color = appstore_last_result == OK ? 0x005FBF7FU : 0x00D65A5AU;
    gui_draw_text((uint32_t)x, (uint32_t)y, info->name, GUI_MODERN_COLOR_TEXT);
    appstore_gui_line(x, y + 32, "ID", info->id, GUI_MODERN_COLOR_TEXT);
    appstore_gui_line(x, y + 56, "Fonte", entry->has_source ?
                      entry->alias : "N/D", GUI_MODERN_COLOR_TEXT);
    appstore_gui_line(x, y + 80, "Versao fonte", entry->source.version[0] ?
                      entry->source.version : "N/D", GUI_MODERN_COLOR_TEXT);
    appstore_gui_line(x, y + 104, "Instalado", entry->installed.version[0] ?
                      entry->installed.version : "N/D", GUI_MODERN_COLOR_TEXT);
    appstore_gui_line(x, y + 128, "Estado", app_catalog_state_name(entry->state),
                      appstore_gui_entry_color(entry));
    appstore_gui_line(x, y + 152, "Motivo", app_catalog_reason_name(entry->reason),
                      entry->reason == APP_CATALOG_REASON_NONE ?
                      GUI_MODERN_COLOR_TEXT : 0x00E0A850U);
    appstore_gui_line(x, y + 176, "Confianca",
                      appstore_entry_trust(entry, info), 0x00E0A850U);
    appstore_gui_line(x, y + 200, "Tamanho", size, GUI_MODERN_COLOR_TEXT);
    appstore_gui_line(x, y + 224, "Dependencias", dependencies,
                      GUI_MODERN_COLOR_TEXT);
    appstore_gui_line(x, y + 248, "Acao",
                      app_package_action_reason_name(appstore_action.reason),
                      appstore_action.reason == APP_PACKAGE_ACTION_REASON_NONE ?
                      GUI_MODERN_COLOR_BORDER_INACTIVE : 0x00E0A850U);
    appstore_gui_line(x, y + 272, "Bloqueadores", blockers,
                      appstore_action.blocker_count ? 0x00E0A850U :
                      GUI_MODERN_COLOR_BORDER_INACTIVE);
    gui_draw_text((uint32_t)x, (uint32_t)(y + 308), appstore_result_text,
                  result_color);
}

static void appstore_gui_draw_confirmation(int x, int y, int width, int height) {
    int dialog_x;
    int dialog_y;
    const char* question;
    char transition[APPSTORE_TEXT_SIZE];

    if (appstore_confirm == APPSTORE_CONFIRM_NONE) return;
    dialog_x = x + (width - 420) / 2;
    dialog_y = y + (height - 170) / 2;
    question = appstore_confirm == APPSTORE_CONFIRM_UPDATE &&
               appstore_plan_is_downgrade() ?
               "Confirmar downgrade diagnostico?" :
               appstore_confirm == APPSTORE_CONFIRM_REMOTE_FETCH ?
               "Baixar e publicar plano autenticado?" :
               appstore_confirm == APPSTORE_CONFIRM_REMOTE_INSTALL ?
               "Instalar plano remoto em cache?" :
               appstore_confirm == APPSTORE_CONFIRM_REMOTE_UPDATE &&
               appstore_plan_is_downgrade() ?
               "Confirmar downgrade remoto diagnostico?" :
               appstore_confirm == APPSTORE_CONFIRM_REMOTE_UPDATE ?
               "Atualizar pelo plano remoto em cache?" :
               appstore_confirm == APPSTORE_CONFIRM_INSTALL ?
               "Confirmar instalacao?" :
               appstore_confirm == APPSTORE_CONFIRM_REMOVE ?
               "Confirmar remocao?" :
               appstore_confirm == APPSTORE_CONFIRM_UPDATE ?
               "Confirmar atualizacao local?" : "Confirmar rollback?";
    appstore_gui_draw_surface(dialog_x, dialog_y, 420, 170,
                              GUI_MODERN_COLOR_WINDOW, GUI_MODERN_COLOR_ACCENT);
    gui_draw_text((uint32_t)(dialog_x + 24), (uint32_t)(dialog_y + 28), question,
                  GUI_MODERN_COLOR_TEXT);
    if (appstore_action.plan.entry_count > 0U) {
        char plan[APPSTORE_TEXT_SIZE];

        plan[0] = '\0';
        for (uint32_t index = 0; index < appstore_action.plan.entry_count;
             index++) {
            if (index) appstore_append_text(plan, sizeof(plan), " -> ");
            appstore_append_text(plan, sizeof(plan),
                                 appstore_action.plan.entries[index].id);
        }
        gui_draw_text((uint32_t)(dialog_x + 24), (uint32_t)(dialog_y + 54),
                      plan, GUI_MODERN_COLOR_TEXT);
        transition[0] = '\0';
        if (appstore_action.plan.target_index <
            appstore_action.plan.entry_count) {
            const app_package_plan_entry_t* target =
                &appstore_action.plan.entries[
                    appstore_action.plan.target_index];

            appstore_append_text(transition, sizeof(transition), "Alvo ");
            appstore_append_text(transition, sizeof(transition), target->id);
            appstore_append_text(transition, sizeof(transition), ": ");
            appstore_append_text(transition, sizeof(transition),
                                 target->from_version[0] ?
                                 target->from_version : "novo");
            appstore_append_text(transition, sizeof(transition), " -> ");
            appstore_append_text(transition, sizeof(transition),
                                 target->to_version);
            gui_draw_text((uint32_t)(dialog_x + 24),
                          (uint32_t)(dialog_y + 78), transition,
                          GUI_MODERN_COLOR_TEXT);
        }
    }
    gui_draw_modern_button((uint32_t)(dialog_x + 82), (uint32_t)(dialog_y + 116),
                           APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT,
                           "Confirmar", GUI_BUTTON_STATE_PRESSED);
    gui_draw_modern_button((uint32_t)(dialog_x + 226), (uint32_t)(dialog_y + 116),
                           APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT,
                           "Cancelar", GUI_BUTTON_STATE_NORMAL);
}

static void appstore_gui_draw_disabled_button(int x, int y,
                                              const char* text) {
    appstore_gui_draw_surface(x, y, APPSTORE_BUTTON_WIDTH,
                              APPSTORE_BUTTON_HEIGHT, GUI_MODERN_COLOR_WINDOW,
                              GUI_MODERN_COLOR_BORDER_INACTIVE);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 9), text,
                  GUI_MODERN_COLOR_BORDER_INACTIVE);
}

static void appstore_gui_draw_action_button(int x, int y, const char* text,
                                            int enabled) {
    if (!enabled) {
        appstore_gui_draw_disabled_button(x, y, text);
        return;
    }
    gui_draw_modern_button((uint32_t)x, (uint32_t)y, APPSTORE_BUTTON_WIDTH,
                           APPSTORE_BUTTON_HEIGHT, text,
                           GUI_BUTTON_STATE_HOVER);
}

static int appstore_action_buttons_per_row(int width) {
    return width >= 760 ? 6 : 3;
}

static void appstore_action_button_position(int x, int y, int width,
                                            int height, int index,
                                            int* button_x, int* button_y) {
    int per_row = appstore_action_buttons_per_row(width);
    int rows = per_row == 6 ? 1 : 2;

    if (button_x) *button_x = x + 16 + (index % per_row) * 124;
    if (button_y) *button_y = y + height - 44 -
                                (rows - 1 - index / per_row) * 32;
}

static int appstore_rollback_is_available(const app_catalog_entry_t* entry) {
    app_package_status_t status;
    const app_package_info_t* info = appstore_entry_info(entry);

    if (!entry || !entry->has_installed || !info ||
        app_package_get_status(&status) != OK) return 0;
    for (uint32_t index = 0; index < status.rollback_count; index++) {
        if (kstrcmp(status.rollbacks[index].id, info->id) == 0) return 1;
    }
    return 0;
}

static int appstore_remote_rollback_is_available(
    const app_remote_entry_t* entry) {
    app_package_status_t status;

    if (!entry || !entry->installed ||
        app_package_get_status(&status) != OK) return 0;
    for (uint32_t index = 0; index < status.rollback_count; index++) {
        if (kstrcmp(status.rollbacks[index].id, entry->info.id) == 0) return 1;
    }
    return 0;
}

static int appstore_remote_buttons_per_row(int width) {
    return width >= 780 ? 7 : 4;
}

static void appstore_remote_button_position(int x, int y, int width,
                                            int height, int index,
                                            int* button_x, int* button_y,
                                            int* button_width) {
    int per_row = appstore_remote_buttons_per_row(width);
    int rows = per_row == 7 ? 1 : 2;
    int step = (width - 24) / per_row;

    if (button_x) *button_x = x + 12 + (index % per_row) * step;
    if (button_y) *button_y = y + height - 44 -
                                (rows - 1 - index / per_row) * 32;
    if (button_width) *button_width = step - 8;
}

static void appstore_draw_classic_remote(int x, int y, int width,
                                         int height) {
    const char* labels[] = {
        appstore_remote_status.enabled ? "Desabilitar" : "Habilitar",
        "Consultar", "Baixar", "Instalar",
        "Atualizar", "Abrir", "Reverter"
    };
    app_remote_entry_t* entry = appstore_selected_remote_entry();
    int rows = appstore_remote_buttons_per_row(width) == 7 ? 1 : 2;
    int content_y = y + 50;
    int content_height = height - (rows == 1 ? 108 : 138);
    int enabled[] = {
        1,
        appstore_remote_status.enabled &&
            appstore_remote_status.network_ready,
        entry && appstore_remote_status.enabled &&
            appstore_remote_status.network_ready &&
            appstore_remote_status.catalog_available &&
            appstore_remote_status.cache_state != APP_REMOTE_CACHE_INVALID,
        entry && appstore_remote_status.enabled && entry->cached &&
            !entry->installed,
        entry && appstore_remote_status.enabled && entry->cached &&
            entry->installed &&
            (entry->state == APP_REMOTE_ENTRY_UPDATE_AVAILABLE ||
             entry->state == APP_REMOTE_ENTRY_DOWNGRADE),
        entry && entry->installed,
        appstore_remote_rollback_is_available(entry)
    };

    appstore_gui_draw_surface(x, y, width, height, GUI_MODERN_COLOR_BG,
                              GUI_MODERN_COLOR_BORDER_INACTIVE);
    appstore_gui_draw_tabs(x + APPSTORE_CLASSIC_MARGIN,
                           y + APPSTORE_CLASSIC_MARGIN);
    appstore_gui_draw_entries(x + APPSTORE_CLASSIC_MARGIN, content_y,
                              width - 2 * APPSTORE_CLASSIC_MARGIN,
                              content_height);
    for (int index = 0; index < 7; index++) {
        int button_x;
        int button_y;
        int button_width;

        appstore_remote_button_position(x, y, width, height, index,
                                        &button_x, &button_y, &button_width);
        if (enabled[index]) {
            gui_draw_modern_button((uint32_t)button_x, (uint32_t)button_y,
                                   (uint32_t)button_width,
                                   APPSTORE_BUTTON_HEIGHT, labels[index],
                                   GUI_BUTTON_STATE_HOVER);
        } else {
            appstore_gui_draw_surface(button_x, button_y, button_width,
                                      APPSTORE_BUTTON_HEIGHT,
                                      GUI_MODERN_COLOR_WINDOW,
                                      GUI_MODERN_COLOR_BORDER_INACTIVE);
            gui_draw_text((uint32_t)(button_x + 8),
                          (uint32_t)(button_y + 9), labels[index],
                          GUI_MODERN_COLOR_BORDER_INACTIVE);
        }
    }
    appstore_gui_draw_confirmation(x, y, width, height);
}

static void appstore_draw_classic(int x, int y, int width, int height) {
    int content_y = y + 50;
    int content_height = height -
                         (appstore_action_buttons_per_row(width) == 6 ?
                          108 : 138);
    app_catalog_entry_t* entry = appstore_selected_entry();
    static const char* labels[] = {
        "Atualizar", "Verificar", "Instalar", "Abrir", "Remover", "Reverter"
    };
    int enabled[] = {
        appstore_can(entry, APP_CATALOG_CAPABILITY_UPDATE),
        appstore_can(entry, APP_CATALOG_CAPABILITY_VERIFY),
        appstore_can(entry, APP_CATALOG_CAPABILITY_INSTALL),
        appstore_can(entry, APP_CATALOG_CAPABILITY_RUN),
        appstore_can(entry, APP_CATALOG_CAPABILITY_REMOVE),
        appstore_rollback_is_available(entry)
    };

    if (appstore_tab == APPSTORE_TAB_REMOTE) {
        appstore_draw_classic_remote(x, y, width, height);
        return;
    }

    appstore_gui_draw_surface(x, y, width, height, GUI_MODERN_COLOR_BG,
                              GUI_MODERN_COLOR_BORDER_INACTIVE);
    appstore_gui_draw_tabs(x + APPSTORE_CLASSIC_MARGIN,
                           y + APPSTORE_CLASSIC_MARGIN);
    if (appstore_tab == APPSTORE_TAB_DETAILS) {
        appstore_gui_draw_surface(x + APPSTORE_CLASSIC_MARGIN, content_y,
                                  width - 2 * APPSTORE_CLASSIC_MARGIN,
                                  content_height, GUI_MODERN_COLOR_WINDOW,
                                  GUI_MODERN_COLOR_BORDER_INACTIVE);
        appstore_gui_draw_details(x + 32, content_y + 18);
    } else {
        appstore_gui_draw_entries(x + APPSTORE_CLASSIC_MARGIN, content_y,
                                  width - 2 * APPSTORE_CLASSIC_MARGIN,
                                  content_height);
    }
    for (int index = 0; index < 6; index++) {
        int button_x;
        int button_y;

        appstore_action_button_position(x, y, width, height, index,
                                        &button_x, &button_y);
        appstore_gui_draw_action_button(button_x, button_y, labels[index],
                                        enabled[index]);
    }
    appstore_gui_draw_confirmation(x, y, width, height);
}

int appstore_init(void) {
    LOG_INFO("APPSTORE", "Inicializando interface da App Store");
    appstore_initialized = 0;
    appstore_active = 0;
    appstore_hosted = 0;
    appstore_tab = APPSTORE_TAB_CATALOG;
    appstore_mode = APPSTORE_MODE_SIMPLE;
    appstore_job.type = APPSTORE_JOB_NONE;
    appstore_running_job.type = APPSTORE_JOB_NONE;
    appstore_queued_type = APPSTORE_JOB_NONE;
    appstore_worker_process = 0;
    appstore_generation = 1U;
    appstore_clear_context();
    if (!app_catalog_is_ready() || !app_package_is_ready()) {
        recovery_mark_disabled(RECOVERY_COMPONENT_APP_STORE, ERR_STATE,
                               "Catalogo ou servico PKG indisponivel para a interface");
        LOG_ERROR("APPSTORE", "Servicos da App Store indisponiveis");
        return ERR_STATE;
    }
    appstore_worker_process = process_create("App Store Worker",
                                             appstore_worker_main);
    if (!appstore_worker_process) {
        recovery_mark_disabled(RECOVERY_COMPONENT_APP_STORE, ERR_MEM,
                               "Worker cooperativo da App Store indisponivel");
        LOG_ERROR("APPSTORE", "Falha ao criar worker cooperativo");
        return ERR_MEM;
    }
    appstore_initialized = 1;
    appstore_refresh_snapshot();
    appstore_refresh_remote_snapshot();
    LOG_INFO("APPSTORE", "Interface da App Store inicializada com sucesso");
    return OK;
}

int appstore_open(void) {
    if (!appstore_initialized ||
        !recovery_is_enabled(RECOVERY_COMPONENT_APP_STORE)) {
        LOG_ERROR("APPSTORE", "App Store indisponivel");
        return ERR_STATE;
    }
    if (appstore_active && appstore_hosted) {
        wm_set_active(1);
        return wm_register_hosted_app(&appstore_hosted_app);
    }
    appstore_active = 1;
    appstore_tab = APPSTORE_TAB_CATALOG;
    appstore_generation++;
    appstore_clear_context();
    appstore_mode = APPSTORE_MODE_SIMPLE;
    if (desktop_get_mode() == DESKTOP_MODE_CLASSIC) {
        wm_set_active(1);
        appstore_hosted = 1;
        appstore_mode = APPSTORE_MODE_CLASSIC;
        if (wm_register_hosted_app(&appstore_hosted_app) == OK) {
            appstore_request_refresh();
            LOG_INFO("APPSTORE", "App Store Classic aberta");
            return OK;
        }
        appstore_hosted = 0;
        appstore_mode = APPSTORE_MODE_SIMPLE;
        wm_set_active(0);
        LOG_WARN("APPSTORE", "Workspace Classic indisponivel; usando Simple");
    }
    desktop_set_active(0);
    appstore_request_refresh();
    appstore_draw_simple();
    LOG_INFO("APPSTORE", "App Store Simple aberta");
    return OK;
}

void appstore_close(void) {
    if (!appstore_active) return;
    if (appstore_busy) {
        LOG_WARN("APPSTORE", "Fechamento aguardando operacao cooperativa");
        return;
    }
    if (appstore_hosted) {
        (void)wm_close_hosted_app(WM_APP_APPSTORE);
        return;
    }
    appstore_active = 0;
    appstore_clear_context();
    desktop_set_active(1);
    desktop_draw();
    LOG_INFO("APPSTORE", "App Store Simple fechada");
}

void appstore_draw(void) {
    if (!appstore_active) return;
    if (appstore_hosted) {
        wm_request_hosted_redraw(WM_APP_APPSTORE);
        return;
    }
    appstore_draw_simple();
}

void appstore_handle_key(uint8_t scancode) {
    if (!appstore_active || (scancode & 0x80U)) return;
    if (appstore_busy) {
        if (scancode == APPSTORE_SCANCODE_ESC ||
            scancode == APPSTORE_SCANCODE_F12) {
            app_remote_request_cancel();
        }
        return;
    }
    if (appstore_confirm != APPSTORE_CONFIRM_NONE) {
        if (scancode == APPSTORE_SCANCODE_ENTER) appstore_confirm_action();
        else if (scancode == APPSTORE_SCANCODE_ESC) appstore_clear_context();
        appstore_draw();
        return;
    }
    if (scancode == APPSTORE_SCANCODE_ESC) {
        if (appstore_mode == APPSTORE_MODE_SIMPLE) appstore_close();
        return;
    }
    if (scancode == APPSTORE_SCANCODE_TAB) {
        int tab_count = appstore_mode == APPSTORE_MODE_SIMPLE ? 3 :
                        APPSTORE_TAB_COUNT;

        appstore_tab = (appstore_tab_t)((appstore_tab + 1) % tab_count);
        appstore_generation++;
        appstore_clear_context();
        if (appstore_tab != APPSTORE_TAB_DETAILS) appstore_select_first_visible();
        if (appstore_tab == APPSTORE_TAB_REMOTE) {
            appstore_refresh_remote_snapshot();
        }
    } else if (scancode == APPSTORE_SCANCODE_F5) appstore_request_refresh();
    else if (scancode == APPSTORE_SCANCODE_UP &&
             appstore_tab != APPSTORE_TAB_DETAILS) appstore_change_selection(-1);
    else if (scancode == APPSTORE_SCANCODE_DOWN &&
             appstore_tab != APPSTORE_TAB_DETAILS) appstore_change_selection(1);
    else if (scancode == APPSTORE_SCANCODE_V) appstore_request_verify();
    else if (scancode == APPSTORE_SCANCODE_I) appstore_request_install();
    else if (scancode == APPSTORE_SCANCODE_U) appstore_request_update();
    else if (scancode == APPSTORE_SCANCODE_A) appstore_request_run();
    else if (scancode == APPSTORE_SCANCODE_R) appstore_request_remove();
    else if (scancode == APPSTORE_SCANCODE_B) appstore_request_rollback();
    else if (scancode == APPSTORE_SCANCODE_E &&
             appstore_tab == APPSTORE_TAB_REMOTE) {
        appstore_request_remote_enable();
    } else if (scancode == APPSTORE_SCANCODE_D &&
               appstore_tab == APPSTORE_TAB_REMOTE) {
        appstore_request_remote_fetch();
    }
    appstore_draw();
}

static int appstore_point_in(int px, int py, int x, int y,
                             int width, int height) {
    return px >= x && px < x + width && py >= y && py < y + height;
}

static int appstore_handle_confirmation_click(int px, int py) {
    int dialog_x = appstore_gui_x + (appstore_gui_width - 420) / 2;
    int dialog_y = appstore_gui_y + (appstore_gui_height - 170) / 2;

    if (appstore_point_in(px, py, dialog_x + 82, dialog_y + 116,
                          APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT)) {
        appstore_confirm_action();
    } else if (appstore_point_in(px, py, dialog_x + 226, dialog_y + 116,
                                 APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT)) {
        appstore_clear_context();
    }
    return 1;
}

int appstore_handle_mouse(mouse_event_t* event) {
    if (!event || !appstore_active || !appstore_hosted) return 0;
    if (event->event != MOUSE_EVENT_PRESS ||
        !(event->changed & MOUSE_BTN_LEFT)) return 1;
    if (appstore_busy) return 1;
    if (appstore_confirm != APPSTORE_CONFIRM_NONE) {
        return appstore_handle_confirmation_click(event->x, event->y);
    }
    for (int index = 0; index < APPSTORE_TAB_COUNT; index++) {
        if (appstore_point_in(event->x, event->y,
                              appstore_gui_x + APPSTORE_CLASSIC_MARGIN + index * 124,
                              appstore_gui_y + APPSTORE_CLASSIC_MARGIN,
                              116, APPSTORE_BUTTON_HEIGHT)) {
            appstore_tab = (appstore_tab_t)index;
            appstore_generation++;
            appstore_clear_context();
            if (appstore_tab != APPSTORE_TAB_DETAILS) {
                if (appstore_tab == APPSTORE_TAB_REMOTE) {
                    appstore_refresh_remote_snapshot();
                } else {
                    appstore_select_first_visible();
                }
            }
            appstore_draw();
            return 1;
        }
    }
    if (appstore_tab != APPSTORE_TAB_DETAILS &&
        appstore_point_in(event->x, event->y,
                          appstore_gui_x + APPSTORE_CLASSIC_MARGIN,
                          appstore_gui_y + 50, 230,
                          appstore_gui_height -
                          (appstore_tab == APPSTORE_TAB_REMOTE ?
                           (appstore_remote_buttons_per_row(
                                appstore_gui_width) == 7 ? 108 : 138) :
                           (appstore_action_buttons_per_row(
                                appstore_gui_width) == 6 ? 108 : 138)))) {
        int row = (event->y - (appstore_gui_y + 62)) / APPSTORE_CLASSIC_ROW_HEIGHT;
        int index = appstore_visible_index((uint32_t)(appstore_scroll + row));

        if (row >= 0 && index >= 0) {
            appstore_selected = index;
            appstore_generation++;
            appstore_clear_context();
        }
        appstore_draw();
        return 1;
    }
    if (appstore_tab == APPSTORE_TAB_REMOTE) {
        for (int index = 0; index < 7; index++) {
            int button_x;
            int button_y;
            int button_width;

            appstore_remote_button_position(
                appstore_gui_x, appstore_gui_y, appstore_gui_width,
                appstore_gui_height, index, &button_x, &button_y,
                &button_width);
            if (!appstore_point_in(event->x, event->y, button_x, button_y,
                                   button_width,
                                   APPSTORE_BUTTON_HEIGHT)) continue;
            if (index == 0) appstore_request_remote_enable();
            else if (index == 1) appstore_request_refresh();
            else if (index == 2) appstore_request_remote_fetch();
            else if (index == 3) appstore_request_install();
            else if (index == 4) appstore_request_update();
            else if (index == 5) appstore_request_run();
            else appstore_request_rollback();
            break;
        }
        appstore_draw();
        return 1;
    }
    for (int index = 0; index < 6; index++) {
        int button_x;
        int button_y;

        appstore_action_button_position(appstore_gui_x, appstore_gui_y,
                                        appstore_gui_width,
                                        appstore_gui_height, index,
                                        &button_x, &button_y);
        if (!appstore_point_in(event->x, event->y, button_x, button_y,
                               APPSTORE_BUTTON_WIDTH,
                               APPSTORE_BUTTON_HEIGHT)) continue;
        if (index == 0) appstore_request_update();
        else if (index == 1) appstore_request_verify();
        else if (index == 2) appstore_request_install();
        else if (index == 3) appstore_request_run();
        else if (index == 4) appstore_request_remove();
        else appstore_request_rollback();
        break;
    }
    appstore_draw();
    return 1;
}

int appstore_is_open(void) {
    return appstore_active;
}

appstore_mode_t appstore_get_mode(void) {
    return appstore_mode;
}

#ifdef ZEPHYROS_HOST_TEST
static int appstore_host_expect(int condition) {
    if (condition) return OK;
    LOG_ERROR("APPSTORE", "Contrato host da App Store falhou");
    return ERR_STATE;
}

static int appstore_host_text_contracts(void) {
    app_package_info_t info;
    char output[APPSTORE_TEXT_SIZE];
    char small_output[5];

    kmemset(&info, 0, sizeof(info));
    appstore_copy_text(output, sizeof(output), 0);
    if (appstore_host_expect(output[0] == '\0') != OK) {
        LOG_ERROR("APPSTORE", "Falha no contrato de texto vazio");
        return ERR_STATE;
    }
    appstore_copy_text(output, sizeof(output), "App Store");
    appstore_append_text(output, sizeof(output), " OK");
    if (appstore_host_expect(kstrcmp(output, "App Store OK") == 0) != OK) {
        return ERR_STATE;
    }
    appstore_copy_text(small_output, sizeof(small_output), "123456");
    if (appstore_host_expect(kstrcmp(small_output, "1234") == 0) != OK) {
        return ERR_STATE;
    }
    appstore_u32_text(0U, output, sizeof(output));
    if (appstore_host_expect(kstrcmp(output, "0") == 0) != OK) {
        return ERR_STATE;
    }
    appstore_u32_text(0xFFFFFFFFU, output, sizeof(output));
    if (appstore_host_expect(kstrcmp(output, "4294967295") == 0) != OK) {
        return ERR_STATE;
    }
    appstore_dependencies_text(&info, output, sizeof(output));
    if (appstore_host_expect(kstrcmp(output, "nenhuma") == 0) != OK) {
        return ERR_STATE;
    }
    info.dependency_count = 2U;
    kmemcpy(info.dependencies[0], "BASE", 5U);
    kmemcpy(info.dependencies[1], "UI", 3U);
    appstore_dependencies_text(&info, output, sizeof(output));
    if (appstore_host_expect(kstrcmp(output, "BASE, UI") == 0) != OK) {
        return ERR_STATE;
    }
    kmemset(&appstore_action, 0, sizeof(appstore_action));
    appstore_blockers_text(output, sizeof(output));
    if (appstore_host_expect(kstrcmp(output, "nenhum") == 0) != OK) {
        return ERR_STATE;
    }
    appstore_action.blocker_count = 2U;
    kmemcpy(appstore_action.blocker_ids[0], "MISS", 5U);
    kmemcpy(appstore_action.blocker_ids[1], "FULL", 5U);
    appstore_action.blocker_overflow = 1U;
    appstore_blockers_text(output, sizeof(output));
    return appstore_host_expect(kstrcmp(output, "MISS, FULL, ...") == 0);
}

static void appstore_host_prepare_entries(void) {
    kmemset(appstore_entries, 0, sizeof(appstore_entries));
    appstore_entry_count = 3U;
    kmemcpy(appstore_entries[0].alias, "core", 5U);
    kmemcpy(appstore_entries[0].source.id, "CORE", 5U);
    appstore_entries[0].has_source = 1U;
    appstore_entries[0].state = APP_CATALOG_STATE_AVAILABLE;
    kmemcpy(appstore_entries[1].alias, "tool", 5U);
    kmemcpy(appstore_entries[1].source.id, "TOOL", 5U);
    kmemcpy(appstore_entries[1].installed.id, "TOOL", 5U);
    appstore_entries[1].has_source = 1U;
    appstore_entries[1].has_installed = 1U;
    appstore_entries[1].state = APP_CATALOG_STATE_UPDATE_AVAILABLE;
    kmemcpy(appstore_entries[2].installed.id, "OLD", 4U);
    appstore_entries[2].has_installed = 1U;
    appstore_entries[2].state = APP_CATALOG_STATE_INSTALLED;
    kmemset(appstore_remote_entries, 0, sizeof(appstore_remote_entries));
    appstore_remote_count = 2U;
    kmemcpy(appstore_remote_entries[0].info.id, "REMOTE", 7U);
    appstore_remote_entries[0].installed = 1U;
    kmemcpy(appstore_remote_entries[1].info.id, "OTHER", 6U);
}

static int appstore_host_selection_contracts(void) {
    char output[APPSTORE_TEXT_SIZE];

    appstore_host_prepare_entries();
    appstore_tab = APPSTORE_TAB_CATALOG;
    appstore_selected = -1;
    appstore_scroll = 0;
    appstore_gui_height = APPSTORE_CLASSIC_DEFAULT_HEIGHT;
    if (appstore_host_expect(appstore_visible_count() == 2U &&
                             appstore_visible_index(0U) == 0 &&
                             appstore_visible_index(1U) == 1 &&
                             appstore_visible_index(2U) == -1) != OK) {
        LOG_ERROR("APPSTORE", "Falha no contrato de selecao inicial");
        return ERR_STATE;
    }
    appstore_select_first_visible();
    if (appstore_host_expect(appstore_selected == 0) != OK) return ERR_STATE;
    if (appstore_host_expect(appstore_restore_selection("tool", 0) == 1 &&
                             appstore_selected == 1) != OK) return ERR_STATE;
    appstore_selected_key(APPSTORE_JOB_VERIFY, output, sizeof(output));
    if (appstore_host_expect(kstrcmp(output, "tool") == 0) != OK) {
        return ERR_STATE;
    }
    appstore_selected_key(APPSTORE_JOB_RUN, output, sizeof(output));
    if (appstore_host_expect(kstrcmp(output, "TOOL") == 0) != OK) {
        return ERR_STATE;
    }
    appstore_change_selection(1);
    if (appstore_host_expect(appstore_selected == 0) != OK) return ERR_STATE;
    appstore_change_selection(-1);
    if (appstore_host_expect(appstore_selected == 1) != OK) return ERR_STATE;
    appstore_tab = APPSTORE_TAB_INSTALLED;
    if (appstore_host_expect(appstore_visible_count() == 2U &&
                             appstore_visible_index(0U) == 1 &&
                             appstore_visible_index(1U) == 2) != OK) {
        return ERR_STATE;
    }
    appstore_tab = APPSTORE_TAB_REMOTE;
    appstore_selected = 0;
    appstore_selected_key(APPSTORE_JOB_RUN, output, sizeof(output));
    if (appstore_host_expect(kstrcmp(output, "REMOTE") == 0 &&
                             appstore_selected_entry() == 0 &&
                             appstore_selected_remote_entry() != 0) != OK) {
        return ERR_STATE;
    }
    appstore_tab = APPSTORE_TAB_CATALOG;
    appstore_selected = -1;
    appstore_selected_key(APPSTORE_JOB_RUN, output, sizeof(output));
    return appstore_host_expect(output[0] == '\0');
}

static int appstore_host_state_contracts(void) {
    app_catalog_entry_t* entry;
    app_package_info_t* info;

    appstore_host_prepare_entries();
    entry = &appstore_entries[1];
    info = (app_package_info_t*)&entry->installed;
    appstore_action.plan.entry_count = 1U;
    appstore_action.plan.target_index = 0U;
    appstore_action.plan.entries[0].action = APP_PACKAGE_PLAN_ACTION_UPDATE;
    kmemcpy(appstore_action.plan.entries[0].from_version, "2.0", 4U);
    kmemcpy(appstore_action.plan.entries[0].to_version, "1.0", 4U);
    if (appstore_host_expect(appstore_plan_is_downgrade() == 1) != OK) {
        LOG_ERROR("APPSTORE", "Falha no contrato de plano de downgrade");
        return ERR_STATE;
    }
    appstore_action.plan.entries[0].action = APP_PACKAGE_PLAN_ACTION_INSTALL;
    if (appstore_host_expect(appstore_plan_is_downgrade() == 0) != OK) {
        return ERR_STATE;
    }
    if (appstore_host_expect(appstore_entry_info(entry) ==
                             &entry->source &&
                             kstrcmp(appstore_entry_label(entry), "TOOL") == 0 &&
                             appstore_entry_color(0) == 0x08U &&
                             appstore_gui_entry_color(0) ==
                                 GUI_MODERN_COLOR_BORDER_INACTIVE) != OK) {
        return ERR_STATE;
    }
    entry->state = APP_CATALOG_STATE_INVALID;
    if (appstore_host_expect(appstore_entry_color(entry) == 0x0CU &&
                             appstore_gui_entry_color(entry) == 0x00D65A5AU) != OK) {
        return ERR_STATE;
    }
    entry->state = APP_CATALOG_STATE_BLOCKED;
    if (appstore_host_expect(appstore_entry_color(entry) == 0x0EU &&
                             appstore_gui_entry_color(entry) == 0x00E0A850U) != OK) {
        return ERR_STATE;
    }
    entry->state = APP_CATALOG_STATE_UPDATE_AVAILABLE;
    if (appstore_host_expect(kstrcmp(appstore_entry_trust(entry, info),
                                     "N/D") == 0) != OK) return ERR_STATE;
    appstore_clear_context();
    appstore_set_result(APPSTORE_RESULT_INSTALL, ERR_INVALID, "rejeitado");
    return appstore_host_expect(appstore_confirm == APPSTORE_CONFIRM_NONE &&
                                appstore_last_result == ERR_INVALID &&
                                kstrcmp(appstore_result_text, "rejeitado") == 0);
}

static int appstore_host_geometry_contracts(void) {
    int x;
    int y;
    int width;

    if (appstore_host_expect(appstore_action_buttons_per_row(760) == 6 &&
                             appstore_action_buttons_per_row(759) == 3 &&
                             appstore_remote_buttons_per_row(780) == 7 &&
                             appstore_remote_buttons_per_row(779) == 4) != OK) {
        LOG_ERROR("APPSTORE", "Falha no contrato de geometria");
        return ERR_STATE;
    }
    appstore_action_button_position(10, 20, 760, 560, 5, &x, &y);
    if (appstore_host_expect(x == 646 && y == 536) != OK) return ERR_STATE;
    appstore_remote_button_position(10, 20, 780, 560, 6, &x, &y, &width);
    if (appstore_host_expect(x == 670 && y == 536 && width == 100) != OK) {
        return ERR_STATE;
    }
    return appstore_host_expect(appstore_point_in(10, 10, 10, 10, 5, 5) &&
                                !appstore_point_in(15, 10, 10, 10, 5, 5) &&
                                !appstore_point_in(10, 15, 10, 10, 5, 5));
}

int appstore_host_test_contracts(void) {
    int result = appstore_host_text_contracts();

    if (result == OK) result = appstore_host_selection_contracts();
    if (result == OK) result = appstore_host_state_contracts();
    if (result == OK) result = appstore_host_geometry_contracts();
    return result;
}
#endif

static void appstore_hosted_draw(int x, int y, int width, int height) {
    appstore_gui_x = x;
    appstore_gui_y = y;
    appstore_gui_width = width;
    appstore_gui_height = height;
    appstore_draw_classic(x, y, width, height);
}

static void appstore_hosted_key(uint8_t scancode) {
    appstore_handle_key(scancode);
}

static int appstore_hosted_mouse(mouse_event_t* event, int x, int y,
                                 int width, int height) {
    appstore_gui_x = x;
    appstore_gui_y = y;
    appstore_gui_width = width;
    appstore_gui_height = height;
    return appstore_handle_mouse(event);
}

static void appstore_hosted_close(void) {
    appstore_hosted = 0;
    appstore_active = 0;
    appstore_clear_context();
    LOG_INFO("APPSTORE", "App Store Classic fechada");
}
