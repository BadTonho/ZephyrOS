#include "ui/appstore.h"
#include "core/app_catalog.h"
#include "core/app_loader.h"
#include "core/app_package.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/string.h"
#include "core/video.h"
#include "process/process.h"
#include "ui/desktop.h"
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
#define APPSTORE_SCANCODE_I 0x17U
#define APPSTORE_SCANCODE_R 0x13U
#define APPSTORE_SCANCODE_V 0x2FU
#define APPSTORE_SCANCODE_F5 0x3FU
#define APPSTORE_SCANCODE_UP 0x48U
#define APPSTORE_SCANCODE_DOWN 0x50U

typedef enum {
    APPSTORE_TAB_CATALOG = 0,
    APPSTORE_TAB_INSTALLED,
    APPSTORE_TAB_DETAILS,
    APPSTORE_TAB_COUNT
} appstore_tab_t;

typedef enum {
    APPSTORE_CONFIRM_NONE = 0,
    APPSTORE_CONFIRM_INSTALL,
    APPSTORE_CONFIRM_REMOVE
} appstore_confirm_t;

typedef enum {
    APPSTORE_RESULT_NONE = 0,
    APPSTORE_RESULT_REFRESH,
    APPSTORE_RESULT_VERIFY,
    APPSTORE_RESULT_INSTALL,
    APPSTORE_RESULT_REMOVE,
    APPSTORE_RESULT_RUN,
    APPSTORE_RESULT_UPDATE_UNAVAILABLE
} appstore_result_t;

typedef enum {
    APPSTORE_JOB_REFRESH = 1,
    APPSTORE_JOB_VERIFY,
    APPSTORE_JOB_PREFLIGHT_INSTALL,
    APPSTORE_JOB_INSTALL,
    APPSTORE_JOB_PREFLIGHT_REMOVE,
    APPSTORE_JOB_REMOVE,
    APPSTORE_JOB_RUN
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

static void appstore_hosted_draw(int x, int y, int width, int height);
static void appstore_hosted_key(uint8_t scancode);
static int appstore_hosted_mouse(mouse_event_t* event, int x, int y,
                                 int width, int height);
static void appstore_hosted_close(void);
static void appstore_worker_main(void);
static void appstore_gui_draw_details(int x, int y);

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

static uint32_t appstore_visible_count(void) {
    uint32_t count = 0;

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
    if (appstore_selected < 0 ||
        (uint32_t)appstore_selected >= appstore_entry_count) return 0;
    return &appstore_entries[appstore_selected];
}

static const app_package_info_t* appstore_entry_info(
    const app_catalog_entry_t* entry) {
    if (!entry) return 0;
    return entry->has_source ? &entry->source : &entry->installed;
}

static void appstore_selected_key(appstore_job_t type, char* output,
                                  uint32_t size) {
    app_catalog_entry_t* entry = appstore_selected_entry();
    const app_package_info_t* info = appstore_entry_info(entry);

    if (!entry || !info) {
        appstore_copy_text(output, size, "");
        return;
    }
    if ((type == APPSTORE_JOB_VERIFY ||
         type == APPSTORE_JOB_PREFLIGHT_INSTALL ||
         type == APPSTORE_JOB_INSTALL) && entry->alias[0]) {
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
    uint32_t count = 0;

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
    appstore_select_first_visible();
    appstore_set_result(APPSTORE_RESULT_REFRESH, OK, "Catalogo atualizado");
}

static void appstore_queue_job(appstore_job_t type) {
    if (appstore_busy || appstore_queued_type != APPSTORE_JOB_NONE) {
        LOG_WARN("APPSTORE", "Operacao da App Store ja esta pendente");
        return;
    }
    appstore_job.type = type;
    appstore_job.generation = appstore_generation;
    appstore_selected_key(type, appstore_job.key, sizeof(appstore_job.key));
    if (!appstore_job.key[0] && type != APPSTORE_JOB_REFRESH) {
        LOG_WARN("APPSTORE", "Operacao solicitada sem item selecionado");
        appstore_job.type = APPSTORE_JOB_NONE;
        appstore_queued_type = APPSTORE_JOB_NONE;
        appstore_set_result(APPSTORE_RESULT_NONE, ERR_NOT_FOUND,
                            "Nenhum aplicativo selecionado");
        return;
    }
    appstore_queued_type = type;
    appstore_busy = 1;
    appstore_draw();
}

static void appstore_request_refresh(void) {
    appstore_generation++;
    appstore_clear_context();
    appstore_queue_job(APPSTORE_JOB_REFRESH);
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

static void appstore_request_run(void) {
    app_catalog_entry_t* entry = appstore_selected_entry();

    if (!appstore_can(entry, APP_CATALOG_CAPABILITY_RUN)) {
        appstore_set_result(APPSTORE_RESULT_RUN, ERR_STATE,
                            "Execucao indisponivel para o item");
        return;
    }
    appstore_queue_job(APPSTORE_JOB_RUN);
}

static void appstore_confirm_action(void) {
    appstore_job_t job = appstore_confirm == APPSTORE_CONFIRM_INSTALL ?
                         APPSTORE_JOB_INSTALL : APPSTORE_JOB_REMOVE;
    char selected_key[APP_CATALOG_ALIAS_SIZE];

    if (appstore_confirm == APPSTORE_CONFIRM_NONE ||
        appstore_confirm_generation != appstore_generation) {
        appstore_clear_context();
        return;
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

static void appstore_worker_preflight_install(const char* alias) {
    int result = app_package_preflight_install(alias, &appstore_action);

    appstore_set_result(APPSTORE_RESULT_INSTALL, result,
                        result == OK ? "Confirmar instalacao" :
                        "Preflight de instalacao bloqueado");
}

static void appstore_worker_preflight_remove(const char* id) {
    int result = app_package_preflight_remove(id, &appstore_action);

    appstore_set_result(APPSTORE_RESULT_REMOVE, result,
                        result == OK ? "Confirmar remocao" :
                        "Preflight de remocao bloqueado");
}

static void appstore_worker_install(const char* alias) {
    int result = app_package_install_confirmed(alias, &appstore_action);
    appstore_result_t result_kind = APPSTORE_RESULT_INSTALL;
    char result_text[APPSTORE_TEXT_SIZE];

    appstore_copy_text(result_text, sizeof(result_text),
                       result == OK ? "Aplicativo instalado" :
                       "Instalacao bloqueada");
    appstore_refresh_snapshot();
    appstore_set_result(result_kind, result, result_text);
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

static void appstore_worker_run(const char* id) {
    int result = app_package_run_installed(id, 0, &appstore_last_pid,
                                           &appstore_action);

    appstore_set_result(APPSTORE_RESULT_RUN, result,
                        result == OK ? "Aplicativo iniciado" :
                        "Execucao bloqueada");
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
    }
    appstore_busy = 0;
    appstore_running_job.type = APPSTORE_JOB_NONE;
    appstore_draw();
}

static void appstore_worker_main(void) {
    while (1) {
        if (appstore_queued_type == APPSTORE_JOB_NONE) {
            process_block(1U);
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
        } else if (appstore_running_job.type == APPSTORE_JOB_PREFLIGHT_REMOVE) {
            appstore_worker_preflight_remove(appstore_running_job.key);
        } else if (appstore_running_job.type == APPSTORE_JOB_REMOVE) {
            appstore_worker_remove(appstore_running_job.key);
        } else if (appstore_running_job.type == APPSTORE_JOB_RUN) {
            appstore_worker_run(appstore_running_job.key);
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
        const app_package_info_t* info;
        int color;

        if (index < 0) break;
        entry = &appstore_entries[index];
        info = appstore_entry_info(entry);
        color = index == appstore_selected ? 0x1F : appstore_entry_color(entry);
        if (index == appstore_selected) {
            video_fill_rect(2, 6 + row, APPSTORE_SIMPLE_LIST_WIDTH - 2,
                            1, ' ', 0x1F);
        }
        video_print_at(3, 6 + row, info ? info->id : "N/D", color);
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
    video_print_at(x, 14, "Confianca: LOCAL / NAO ASSINADO", 0x0E);
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

    if (appstore_confirm == APPSTORE_CONFIRM_NONE) return;
    question = appstore_confirm == APPSTORE_CONFIRM_INSTALL ?
               "Confirmar instalacao?" : "Confirmar remocao?";
    video_fill_rect(28, SCREEN_ROWS - 8, 48, 5, ' ', 0x1E);
    video_draw_box(28, SCREEN_ROWS - 8, 48, 5, 0x0E);
    video_print_at(30, SCREEN_ROWS - 7, question, 0x1E);
    video_print_at(30, SCREEN_ROWS - 5, "Enter confirma | Esc cancela", 0x1E);
}

static void appstore_draw_simple(void) {
    static const char* names[APPSTORE_TAB_COUNT] = {
        " Catalogo ", " Instalados ", " Detalhes "
    };

    video_begin_update();
    video_fill_rect(0, 0, SCREEN_COLS, SCREEN_ROWS, ' ', 0x07);
    video_fill_rect(0, 0, SCREEN_COLS, 1, ' ', 0x1F);
    video_print_at(2, 0, "ZephyrOS App Store", 0x1F);
    for (int index = 0; index < APPSTORE_TAB_COUNT; index++) {
        video_print_at(2 + index * 15, 2, names[index],
                       appstore_tab == index ? 0x1F : 0x07);
    }
    if (appstore_tab == APPSTORE_TAB_DETAILS) appstore_simple_draw_details();
    else {
        appstore_simple_draw_entries();
        appstore_simple_draw_details();
    }
    video_print_at(2, SCREEN_ROWS - 2,
                   appstore_busy ? "Operacao em andamento..." :
                   "Tab Setas F5 V=verificar I=instalar A=abrir R=remover Esc=fechar",
                   0x70);
    appstore_simple_draw_confirmation();
    taskbar_draw();
    video_end_update();
}

static void appstore_gui_draw_tabs(int x, int y) {
    static const char* names[APPSTORE_TAB_COUNT] = {
        "Catalogo", "Instalados", "Detalhes"
    };

    for (int index = 0; index < APPSTORE_TAB_COUNT; index++) {
        gui_draw_modern_button((uint32_t)(x + index * 124), (uint32_t)y,
                               116, APPSTORE_BUTTON_HEIGHT, names[index],
                               appstore_tab == index ? GUI_BUTTON_STATE_PRESSED :
                                                       GUI_BUTTON_STATE_NORMAL);
    }
}

static void appstore_gui_draw_entries(int x, int y, int width, int height) {
    uint32_t count = appstore_visible_count();
    int list_width = 230;
    int visible = (height - 54) / APPSTORE_CLASSIC_ROW_HEIGHT;

    gui_draw_rounded_rect((uint32_t)x, (uint32_t)y, (uint32_t)list_width,
                          (uint32_t)height, GUI_MODERN_BUTTON_RADIUS_BASE,
                          GUI_MODERN_COLOR_WINDOW);
    gui_draw_flat_border((uint32_t)x, (uint32_t)y, (uint32_t)list_width,
                         (uint32_t)height, GUI_MODERN_COLOR_BORDER_INACTIVE);
    if (count == 0U) {
        gui_draw_text((uint32_t)(x + 14), (uint32_t)(y + 16), "Nenhum item",
                      GUI_MODERN_COLOR_BORDER_INACTIVE);
    }
    for (int row = 0; row < visible; row++) {
        int index = appstore_visible_index((uint32_t)(appstore_scroll + row));
        app_catalog_entry_t* entry;
        const app_package_info_t* info;
        int row_y = y + 12 + row * APPSTORE_CLASSIC_ROW_HEIGHT;

        if (index < 0) break;
        entry = &appstore_entries[index];
        info = appstore_entry_info(entry);
        if (index == appstore_selected) {
            gui_draw_rounded_rect((uint32_t)(x + 6), (uint32_t)(row_y - 4),
                                  (uint32_t)(list_width - 12), 22,
                                  GUI_MODERN_BUTTON_RADIUS_BASE,
                                  GUI_MODERN_COLOR_HOVER);
        }
        gui_draw_text((uint32_t)(x + 14), (uint32_t)row_y,
                      info ? info->id : "N/D", appstore_gui_entry_color(entry));
    }
    gui_draw_rounded_rect((uint32_t)(x + list_width + 12), (uint32_t)y,
                          (uint32_t)(width - list_width - 12), (uint32_t)height,
                          GUI_MODERN_BUTTON_RADIUS_BASE, GUI_MODERN_COLOR_WINDOW);
    gui_draw_flat_border((uint32_t)(x + list_width + 12), (uint32_t)y,
                         (uint32_t)(width - list_width - 12), (uint32_t)height,
                         GUI_MODERN_COLOR_BORDER_INACTIVE);
    appstore_gui_draw_details(x + list_width + 30, y + 18);
}

static void appstore_gui_line(int x, int y, const char* label,
                              const char* value, uint32_t color) {
    gui_draw_text((uint32_t)x, (uint32_t)y, label, GUI_MODERN_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 142), (uint32_t)y, value, color);
}

static void appstore_gui_draw_details(int x, int y) {
    app_catalog_entry_t* entry = appstore_selected_entry();
    const app_package_info_t* info = appstore_entry_info(entry);
    char dependencies[APPSTORE_TEXT_SIZE];
    char blockers[APPSTORE_TEXT_SIZE];
    char size[16];
    uint32_t result_color;

    if (!entry || !info) {
        gui_draw_text((uint32_t)x, (uint32_t)y, "Nenhum aplicativo selecionado",
                      GUI_MODERN_COLOR_BORDER_INACTIVE);
        return;
    }
    appstore_u32_text(entry->source_size, size, sizeof(size));
    appstore_dependencies_text(info, dependencies, sizeof(dependencies));
    appstore_blockers_text(blockers, sizeof(blockers));
    result_color = appstore_result_kind == APPSTORE_RESULT_UPDATE_UNAVAILABLE ?
                   0x00E0A850U :
                   appstore_last_result == OK ? 0x005FBF7FU : 0x00D65A5AU;
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
    appstore_gui_line(x, y + 176, "Confianca", entry->has_source ?
                      "LOCAL / NAO ASSINADO" : "N/D", 0x00E0A850U);
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

    if (appstore_confirm == APPSTORE_CONFIRM_NONE) return;
    dialog_x = x + (width - 420) / 2;
    dialog_y = y + (height - 150) / 2;
    question = appstore_confirm == APPSTORE_CONFIRM_INSTALL ?
               "Confirmar instalacao?" : "Confirmar remocao?";
    gui_draw_rounded_rect((uint32_t)dialog_x, (uint32_t)dialog_y, 420, 150,
                          GUI_MODERN_BUTTON_RADIUS_BASE, GUI_MODERN_COLOR_WINDOW);
    gui_draw_flat_border((uint32_t)dialog_x, (uint32_t)dialog_y, 420, 150,
                         GUI_MODERN_COLOR_ACCENT);
    gui_draw_text((uint32_t)(dialog_x + 24), (uint32_t)(dialog_y + 28), question,
                  GUI_MODERN_COLOR_TEXT);
    gui_draw_modern_button((uint32_t)(dialog_x + 82), (uint32_t)(dialog_y + 92),
                           APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT,
                           "Confirmar", GUI_BUTTON_STATE_PRESSED);
    gui_draw_modern_button((uint32_t)(dialog_x + 226), (uint32_t)(dialog_y + 92),
                           APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT,
                           "Cancelar", GUI_BUTTON_STATE_NORMAL);
}

static void appstore_gui_draw_disabled_button(int x, int y,
                                              const char* text) {
    gui_draw_rounded_rect((uint32_t)x, (uint32_t)y, APPSTORE_BUTTON_WIDTH,
                          APPSTORE_BUTTON_HEIGHT, GUI_MODERN_BUTTON_RADIUS_BASE,
                          GUI_MODERN_COLOR_WINDOW);
    gui_draw_flat_border((uint32_t)x, (uint32_t)y, APPSTORE_BUTTON_WIDTH,
                         APPSTORE_BUTTON_HEIGHT, GUI_MODERN_COLOR_BORDER_INACTIVE);
    gui_draw_text((uint32_t)(x + 10), (uint32_t)(y + 9), text,
                  GUI_MODERN_COLOR_BORDER_INACTIVE);
}

static void appstore_draw_classic(int x, int y, int width, int height) {
    int content_y = y + 50;
    int content_height = height - 108;
    app_catalog_entry_t* entry = appstore_selected_entry();

    gui_draw_rounded_rect((uint32_t)x, (uint32_t)y, (uint32_t)width,
                          (uint32_t)height, GUI_MODERN_BUTTON_RADIUS_BASE,
                          GUI_MODERN_COLOR_BG);
    appstore_gui_draw_tabs(x + APPSTORE_CLASSIC_MARGIN,
                           y + APPSTORE_CLASSIC_MARGIN);
    if (appstore_tab == APPSTORE_TAB_DETAILS) {
        appstore_gui_draw_details(x + 32, content_y + 18);
    } else {
        appstore_gui_draw_entries(x + APPSTORE_CLASSIC_MARGIN, content_y,
                                  width - 2 * APPSTORE_CLASSIC_MARGIN,
                                  content_height);
    }
    appstore_gui_draw_disabled_button(x + 16, y + height - 44,
                                      "Atualizar AS4");
    gui_draw_modern_button((uint32_t)(x + 140), (uint32_t)(y + height - 44),
                           APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT,
                           "Verificar", appstore_can(entry, APP_CATALOG_CAPABILITY_VERIFY) ?
                           GUI_BUTTON_STATE_NORMAL : GUI_BUTTON_STATE_HOVER);
    gui_draw_modern_button((uint32_t)(x + 264), (uint32_t)(y + height - 44),
                           APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT,
                           "Instalar", appstore_can(entry, APP_CATALOG_CAPABILITY_INSTALL) ?
                           GUI_BUTTON_STATE_NORMAL : GUI_BUTTON_STATE_HOVER);
    gui_draw_modern_button((uint32_t)(x + 388), (uint32_t)(y + height - 44),
                           APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT,
                           "Abrir", appstore_can(entry, APP_CATALOG_CAPABILITY_RUN) ?
                           GUI_BUTTON_STATE_NORMAL : GUI_BUTTON_STATE_HOVER);
    gui_draw_modern_button((uint32_t)(x + 512), (uint32_t)(y + height - 44),
                           APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT,
                           "Remover", appstore_can(entry, APP_CATALOG_CAPABILITY_REMOVE) ?
                           GUI_BUTTON_STATE_NORMAL : GUI_BUTTON_STATE_HOVER);
    appstore_gui_draw_confirmation(x, y, width, height);
}

int appstore_init(void) {
    process_t* worker;

    LOG_INFO("APPSTORE", "Inicializando interface da App Store");
    appstore_initialized = 0;
    appstore_active = 0;
    appstore_hosted = 0;
    appstore_tab = APPSTORE_TAB_CATALOG;
    appstore_mode = APPSTORE_MODE_SIMPLE;
    appstore_job.type = APPSTORE_JOB_NONE;
    appstore_running_job.type = APPSTORE_JOB_NONE;
    appstore_queued_type = APPSTORE_JOB_NONE;
    appstore_generation = 1U;
    appstore_clear_context();
    if (!app_catalog_is_ready() || !app_package_is_ready()) {
        recovery_mark_disabled(RECOVERY_COMPONENT_APP_STORE, ERR_STATE,
                               "Catalogo ou servico PKG indisponivel para a interface");
        LOG_ERROR("APPSTORE", "Servicos da App Store indisponiveis");
        return ERR_STATE;
    }
    worker = process_create("App Store Worker", appstore_worker_main);
    if (!worker) {
        recovery_mark_disabled(RECOVERY_COMPONENT_APP_STORE, ERR_MEM,
                               "Worker cooperativo da App Store indisponivel");
        LOG_ERROR("APPSTORE", "Falha ao criar worker cooperativo");
        return ERR_MEM;
    }
    appstore_initialized = 1;
    appstore_refresh_snapshot();
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
    appstore_request_refresh();
    appstore_mode = APPSTORE_MODE_SIMPLE;
    if (desktop_get_mode() == DESKTOP_MODE_CLASSIC) {
        wm_set_active(1);
        appstore_hosted = 1;
        appstore_mode = APPSTORE_MODE_CLASSIC;
        if (wm_register_hosted_app(&appstore_hosted_app) == OK) {
            LOG_INFO("APPSTORE", "App Store Classic aberta");
            return OK;
        }
        appstore_hosted = 0;
        appstore_mode = APPSTORE_MODE_SIMPLE;
        wm_set_active(0);
        LOG_WARN("APPSTORE", "Workspace Classic indisponivel; usando Simple");
    }
    desktop_set_active(0);
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
    if (appstore_busy) return;
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
        appstore_tab = (appstore_tab_t)((appstore_tab + 1) % APPSTORE_TAB_COUNT);
        appstore_generation++;
        appstore_clear_context();
        appstore_select_first_visible();
    } else if (scancode == APPSTORE_SCANCODE_F5) appstore_request_refresh();
    else if (scancode == APPSTORE_SCANCODE_UP &&
             appstore_tab != APPSTORE_TAB_DETAILS) appstore_change_selection(-1);
    else if (scancode == APPSTORE_SCANCODE_DOWN &&
             appstore_tab != APPSTORE_TAB_DETAILS) appstore_change_selection(1);
    else if (scancode == APPSTORE_SCANCODE_V) appstore_request_verify();
    else if (scancode == APPSTORE_SCANCODE_I) appstore_request_install();
    else if (scancode == APPSTORE_SCANCODE_A) appstore_request_run();
    else if (scancode == APPSTORE_SCANCODE_R) appstore_request_remove();
    appstore_draw();
}

static int appstore_point_in(int px, int py, int x, int y,
                             int width, int height) {
    return px >= x && px < x + width && py >= y && py < y + height;
}

static int appstore_handle_confirmation_click(int px, int py) {
    int dialog_x = appstore_gui_x + (appstore_gui_width - 420) / 2;
    int dialog_y = appstore_gui_y + (appstore_gui_height - 150) / 2;

    if (appstore_point_in(px, py, dialog_x + 82, dialog_y + 92,
                          APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT)) {
        appstore_confirm_action();
    } else if (appstore_point_in(px, py, dialog_x + 226, dialog_y + 92,
                                 APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT)) {
        appstore_clear_context();
    }
    return 1;
}

int appstore_handle_mouse(mouse_event_t* event) {
    int bottom;

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
            appstore_select_first_visible();
            appstore_draw();
            return 1;
        }
    }
    if (appstore_tab != APPSTORE_TAB_DETAILS &&
        appstore_point_in(event->x, event->y,
                          appstore_gui_x + APPSTORE_CLASSIC_MARGIN,
                          appstore_gui_y + 50, 230,
                          appstore_gui_height - 158)) {
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
    bottom = appstore_gui_y + appstore_gui_height - 44;
    if (appstore_point_in(event->x, event->y, appstore_gui_x + 16, bottom,
                          APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT)) {
        appstore_set_result(APPSTORE_RESULT_UPDATE_UNAVAILABLE, ERR_STATE,
                            "Atualizacao local pertence ao AS4");
    } else if (appstore_point_in(event->x, event->y, appstore_gui_x + 140, bottom,
                                 APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT)) {
        appstore_request_verify();
    } else if (appstore_point_in(event->x, event->y, appstore_gui_x + 264, bottom,
                                 APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT)) {
        appstore_request_install();
    } else if (appstore_point_in(event->x, event->y, appstore_gui_x + 388, bottom,
                                 APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT)) {
        appstore_request_run();
    } else if (appstore_point_in(event->x, event->y, appstore_gui_x + 512, bottom,
                                 APPSTORE_BUTTON_WIDTH, APPSTORE_BUTTON_HEIGHT)) {
        appstore_request_remove();
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
