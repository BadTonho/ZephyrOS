#include "ui/updater.h"
#include "core/errors.h"
#include "core/keyboard.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/string.h"
#include "core/update.h"
#include "core/update_remote.h"
#include "core/update_remote_config.h"
#include "core/video.h"
#include "drivers/vesa.h"
#include "fs/fs.h"
#include "process/process.h"
#include "ui/desktop.h"
#include "ui/gui.h"
#include "ui/taskbar.h"
#include "ui/wm.h"

#define UPDATER_MAX_PACKAGES 16
#define UPDATER_PACKAGE_NAME_SIZE 13
#define UPDATER_FS_DIRECTORY 0x10U
#define UPDATER_CLASSIC_LIST_X 3
#define UPDATER_CLASSIC_LIST_Y 6
#define UPDATER_CLASSIC_LIST_WIDTH 28
#define UPDATER_MODERN_MIN_WIDTH 640
#define UPDATER_MODERN_MIN_HEIGHT 420
#define UPDATER_MODERN_DEFAULT_WIDTH 780
#define UPDATER_MODERN_DEFAULT_HEIGHT 560
#define UPDATER_MODERN_MARGIN 12
#define UPDATER_MODERN_ROW_HEIGHT 24
#define UPDATER_SCANCODE_ESC 0x01U
#define UPDATER_SCANCODE_TAB 0x0FU
#define UPDATER_SCANCODE_ENTER 0x1CU
#define UPDATER_SCANCODE_A 0x1EU
#define UPDATER_SCANCODE_B 0x30U
#define UPDATER_SCANCODE_C 0x2EU
#define UPDATER_SCANCODE_D 0x20U
#define UPDATER_SCANCODE_H 0x23U
#define UPDATER_SCANCODE_V 0x2FU
#define UPDATER_SCANCODE_X 0x2DU
#define UPDATER_SCANCODE_F5 0x3FU
#define UPDATER_SCANCODE_UP 0x48U
#define UPDATER_SCANCODE_DOWN 0x50U
#define UPDATER_SCANCODE_F12 0x58U

typedef enum {
    UPDATER_TAB_PACKAGES = 0,
    UPDATER_TAB_STATUS,
    UPDATER_TAB_HISTORY,
    UPDATER_TAB_REMOTE,
    UPDATER_TAB_COUNT
} updater_tab_t;

typedef enum {
    UPDATER_CONFIRM_NONE = 0,
    UPDATER_CONFIRM_APPLY,
    UPDATER_CONFIRM_ROLLBACK,
    UPDATER_CONFIRM_REMOTE_FETCH,
    UPDATER_CONFIRM_REMOTE_CLEAR
} updater_confirm_t;

typedef enum {
    UPDATER_RESULT_NONE = 0,
    UPDATER_RESULT_VERIFY,
    UPDATER_RESULT_APPLY,
    UPDATER_RESULT_ROLLBACK,
    UPDATER_RESULT_REMOTE
} updater_result_t;

typedef enum {
    UPDATER_REMOTE_JOB_NONE = 0,
    UPDATER_REMOTE_JOB_CHECK,
    UPDATER_REMOTE_JOB_FETCH
} updater_remote_job_t;

typedef struct {
    char name[UPDATER_PACKAGE_NAME_SIZE];
    uint32_t size;
} updater_package_t;

static updater_package_t updater_packages[UPDATER_MAX_PACKAGES];
static int updater_package_count = 0;
static int updater_package_overflow = 0;
static int updater_selected = 0;
static int updater_scroll = 0;
static int updater_active = 0;
static int updater_hosted = 0;
static int updater_initialized = 0;
static updater_mode_t updater_mode = UPDATER_MODE_CLASSIC;
static updater_tab_t updater_tab = UPDATER_TAB_PACKAGES;
static updater_confirm_t updater_confirm = UPDATER_CONFIRM_NONE;
static updater_result_t updater_result_kind = UPDATER_RESULT_NONE;
static update_status_t updater_status;
static int updater_status_ready = 0;
static update_verification_t updater_verification;
static update_action_result_t updater_action;
static updater_result_t updater_completed_action_kind = UPDATER_RESULT_NONE;
static update_action_result_t updater_completed_action;
static update_remote_status_t updater_remote_status;
static update_remote_result_t updater_remote_result;
static int updater_remote_status_ready = 0;
static int updater_last_result = OK;
static int updater_gui_x = 0;
static int updater_gui_y = 0;
static int updater_gui_width = UPDATER_MODERN_DEFAULT_WIDTH;
static int updater_gui_height = UPDATER_MODERN_DEFAULT_HEIGHT;
static volatile updater_remote_job_t updater_remote_job =
    UPDATER_REMOTE_JOB_NONE;
static volatile updater_remote_job_t updater_remote_job_running =
    UPDATER_REMOTE_JOB_NONE;
static volatile uint8_t updater_remote_job_busy = 0U;
static volatile uint8_t updater_remote_cancel_requested = 0U;
static uint8_t updater_remote_job_confirm = 0U;
static uint32_t updater_remote_progress_bytes = 0U;
static update_remote_state_t updater_remote_progress_state =
    UPDATE_REMOTE_STATE_DISABLED;

static void updater_hosted_draw(int x, int y, int width, int height);
static void updater_hosted_key(uint8_t scancode);
static int updater_hosted_mouse(mouse_event_t* event, int x, int y,
                                int width, int height);
static void updater_hosted_close(void);
static void updater_remote_worker_main(void);

static const wm_hosted_app_t updater_hosted_app = {
    WM_APP_UPDATER, "ZephyrOS System Updater", "Updater",
    UPDATER_MODERN_MIN_WIDTH, UPDATER_MODERN_MIN_HEIGHT,
    UPDATER_MODERN_DEFAULT_WIDTH, UPDATER_MODERN_DEFAULT_HEIGHT,
    WM_KEY_REDRAW_WINDOW_MANAGER,
    updater_hosted_draw, updater_hosted_key, updater_hosted_mouse,
    updater_hosted_close
};

static int updater_is_zup_name(const char* name) {
    uint32_t length;

    if (!name) return 0;
    length = kstrlen(name);
    if (length < 5U) return 0;
    return name[length - 4U] == '.' &&
           (name[length - 3U] == 'Z' || name[length - 3U] == 'z') &&
           (name[length - 2U] == 'U' || name[length - 2U] == 'u') &&
           (name[length - 1U] == 'P' || name[length - 1U] == 'p');
}

static void updater_copy_name(char* output, const char* input) {
    uint32_t index = 0;

    while (input[index] && index + 1U < UPDATER_PACKAGE_NAME_SIZE) {
        char value = input[index];

        if (value >= 'a' && value <= 'z') value -= 'a' - 'A';
        output[index] = value;
        index++;
    }
    output[index] = '\0';
}

static void updater_sort_packages(void) {
    for (int index = 1; index < updater_package_count; index++) {
        updater_package_t item = updater_packages[index];
        int position = index;

        while (position > 0 &&
               kstrcmp(updater_packages[position - 1].name,
                       item.name) > 0) {
            updater_packages[position] = updater_packages[position - 1];
            position--;
        }
        updater_packages[position] = item;
    }
}

static void updater_refresh_packages(void) {
    int file_count = fs_get_file_count();

    updater_package_count = 0;
    updater_package_overflow = 0;
    updater_selected = 0;
    updater_scroll = 0;
    updater_confirm = UPDATER_CONFIRM_NONE;
    updater_result_kind = UPDATER_RESULT_NONE;
    for (int index = 0; index < file_count; index++) {
        char name[UPDATER_PACKAGE_NAME_SIZE];
        uint32_t size = 0;
        uint8_t attributes = 0;

        if (fs_get_file_info(index, name, &size, &attributes) != OK) {
            LOG_WARN("UPDATER", "Arquivo raiz nao pode ser enumerado");
            continue;
        }
        if ((attributes & (UPDATER_FS_DIRECTORY |
                           FS_ATTRIBUTE_HIDDEN |
                           FS_ATTRIBUTE_SYSTEM)) ||
            !updater_is_zup_name(name)) {
            continue;
        }
        if (updater_package_count >= UPDATER_MAX_PACKAGES) {
            updater_package_overflow = 1;
            continue;
        }
        updater_copy_name(
            updater_packages[updater_package_count].name, name);
        updater_packages[updater_package_count].size = size;
        updater_package_count++;
    }
    {
        char remote_alias[UPDATER_PACKAGE_NAME_SIZE];

        if (update_remote_get_cached_alias(
                remote_alias, sizeof(remote_alias)) == OK) {
            int duplicate = 0;

            for (int index = 0; index < updater_package_count; index++) {
                if (kstrcmp(updater_packages[index].name,
                            remote_alias) == 0) duplicate = 1;
            }
            if (!duplicate && updater_package_count < UPDATER_MAX_PACKAGES) {
                uint32_t remote_size = 0U;

                updater_copy_name(
                    updater_packages[updater_package_count].name,
                    remote_alias);
                fs_get_root_file_info(
                    remote_alias, &remote_size, 0);
                updater_packages[updater_package_count].size = remote_size;
                updater_package_count++;
            } else if (!duplicate) {
                updater_package_overflow = 1;
            }
        }
    }
    updater_sort_packages();
}

static void updater_refresh_status(void) {
    updater_status_ready = update_get_status(&updater_status) == OK;
    updater_remote_status_ready =
        update_remote_get_status(&updater_remote_status) == OK;
}

static void updater_refresh_component(void) {
    if (!update_is_ready()) {
        recovery_mark_disabled(
            RECOVERY_COMPONENT_SYSTEM_UPDATER, ERR_STATE,
            "System Updater requer servico Update");
    } else if (!updater_status_ready ||
               updater_status.state_store == UPDATE_STORE_INVALID ||
               updater_status.current_files == UPDATE_STORE_INVALID ||
               updater_status.history_store == UPDATE_STORE_INVALID ||
               !updater_status.capabilities.local_file_available) {
        recovery_mark_degraded(
            RECOVERY_COMPONENT_SYSTEM_UPDATER, ERR_UNAVAILABLE,
            "System Updater opera com diagnostico parcial");
    } else {
        recovery_mark_ready(RECOVERY_COMPONENT_SYSTEM_UPDATER);
    }
}

static void updater_refresh_all(void) {
    updater_refresh_packages();
    updater_refresh_status();
    updater_refresh_component();
}

static void updater_u32_text(uint32_t value, char* output) {
    char reversed[11];
    int count = 0;
    int index = 0;

    if (value == 0U) {
        output[0] = '0';
        output[1] = '\0';
        return;
    }
    while (value && count < 10) {
        reversed[count++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    while (count > 0) output[index++] = reversed[--count];
    output[index] = '\0';
}

static void updater_text_append(char* output, uint32_t size,
                                const char* text) {
    uint32_t length = kstrlen(output);
    uint32_t index = 0;

    while (text[index] && length + 1U < size) {
        output[length++] = text[index++];
    }
    output[length] = '\0';
}

static void updater_text_append_u32(char* output, uint32_t size,
                                    uint32_t value) {
    char number[11];

    updater_u32_text(value, number);
    updater_text_append(output, size, number);
}

static void updater_text_append_version(
    char* output, uint32_t size,
    const update_version_t* version, uint32_t epoch) {
    updater_text_append_u32(output, size, version->major);
    updater_text_append(output, size, ".");
    updater_text_append_u32(output, size, version->minor);
    updater_text_append(output, size, ".");
    updater_text_append_u32(output, size, version->patch);
    updater_text_append(output, size, "/e");
    updater_text_append_u32(output, size, epoch);
}

static void updater_version_text(
    char* output, uint32_t size,
    const update_version_t* version, uint32_t epoch) {
    output[0] = '\0';
    updater_text_append_u32(output, size, version->major);
    updater_text_append(output, size, ".");
    updater_text_append_u32(output, size, version->minor);
    updater_text_append(output, size, ".");
    updater_text_append_u32(output, size, version->patch);
    updater_text_append(output, size, " epoch=");
    updater_text_append_u32(output, size, epoch);
}

static void updater_history_detail_text(
    char* output, uint32_t size,
    const update_history_entry_t* entry) {
    output[0] = '\0';
    updater_text_append_version(
        output, size, &entry->from_version, entry->from_epoch);
    updater_text_append(output, size, " -> ");
    updater_text_append_version(
        output, size, &entry->to_version, entry->to_epoch);
    updater_text_append(output, size, " ");
    updater_text_append_u32(output, size, entry->completed_entries);
    updater_text_append(output, size, "/");
    updater_text_append_u32(output, size, entry->entry_count);
    if (entry->action_reason != UPDATE_ACTION_NONE) {
        updater_text_append(output, size, " ");
        updater_text_append(
            output, size, update_action_reason_name(entry->action_reason));
    }
    if (entry->verification_reason != ZUPD_REASON_NONE) {
        updater_text_append(output, size, "/");
        updater_text_append(
            output, size, zupd_reason_name(entry->verification_reason));
    }
    if (entry->package_alias[0]) {
        updater_text_append(output, size, " ");
        updater_text_append(output, size, entry->package_alias);
    }
}

static void updater_classic_print_version(
    const char* label, const update_version_t* version, uint32_t epoch) {
    char text[64];

    updater_version_text(text, sizeof(text), version, epoch);
    video_print(label, 0x07);
    video_print(text, 0x0F);
    video_print("\n", 0x07);
}

static int updater_cancel_check(void* context) {
    ipc_msg_t message;

    (void)context;
    keyboard_process_events();
    while (ipc_receive(&message)) {
        if (message.type == IPC_MSG_KEYBOARD &&
            (message.data1 == UPDATER_SCANCODE_ESC ||
             message.data1 == UPDATER_SCANCODE_F12)) {
            return 1;
        }
    }
    return 0;
}

static int updater_remote_job_cancel_check(void* context) {
    update_remote_status_t status;

    (void)context;
    if (update_remote_get_status(&status) == OK) {
        if (status.bytes_received != updater_remote_progress_bytes ||
            status.state != updater_remote_progress_state) {
            updater_remote_status = status;
            updater_remote_status_ready = 1;
            updater_remote_progress_bytes = status.bytes_received;
            updater_remote_progress_state = status.state;
            updater_draw();
        }
    }
    return updater_remote_cancel_requested ? 1 : 0;
}

static const char* updater_selected_package(void) {
    if (updater_selected < 0 ||
        updater_selected >= updater_package_count) return 0;
    return updater_packages[updater_selected].name;
}

static void updater_verify_selected(void) {
    const char* path = updater_selected_package();

    if (!path) {
        LOG_WARN("UPDATER", "Verificacao solicitada sem pacote");
        return;
    }
    kmemset(&updater_verification, 0, sizeof(updater_verification));
    updater_last_result =
        update_verify_file(path, &updater_verification);
    updater_result_kind = UPDATER_RESULT_VERIFY;
    updater_confirm = UPDATER_CONFIRM_NONE;
    updater_refresh_status();
    updater_refresh_component();
}

static void updater_preflight_apply(void) {
    update_action_options_t options;
    const char* path = updater_selected_package();

    if (!path) {
        LOG_WARN("UPDATER", "Aplicacao solicitada sem pacote");
        return;
    }
    kmemset(&options, 0, sizeof(options));
    options.dry_run = 1;
    options.cancel_check = updater_cancel_check;
    kmemset(&updater_action, 0, sizeof(updater_action));
    updater_last_result =
        update_apply_file(path, &options, &updater_action);
    updater_result_kind = UPDATER_RESULT_APPLY;
    updater_confirm = updater_last_result == OK ?
        UPDATER_CONFIRM_APPLY : UPDATER_CONFIRM_NONE;
    updater_refresh_status();
    updater_refresh_component();
}

static void updater_preflight_rollback(void) {
    update_action_options_t options;

    kmemset(&options, 0, sizeof(options));
    options.dry_run = 1;
    options.cancel_check = updater_cancel_check;
    kmemset(&updater_action, 0, sizeof(updater_action));
    updater_last_result =
        update_rollback(&options, &updater_action);
    updater_result_kind = UPDATER_RESULT_ROLLBACK;
    updater_confirm = updater_last_result == OK ?
        UPDATER_CONFIRM_ROLLBACK : UPDATER_CONFIRM_NONE;
    updater_refresh_status();
    updater_refresh_component();
}

static void updater_remote_toggle(void) {
    int result;

    if (!updater_remote_status_ready) return;
    result = updater_remote_status.enabled ?
             update_remote_disable() : update_remote_enable();
    if (result != OK) {
        LOG_ERROR("UPDATER", "Falha ao alternar Update remoto");
    }
    updater_last_result = result;
    updater_result_kind = UPDATER_RESULT_REMOTE;
    kmemset(&updater_remote_result, 0, sizeof(updater_remote_result));
    updater_refresh_status();
    if (result != OK && updater_remote_status_ready) {
        updater_remote_result.reason = updater_remote_status.reason;
    }
    updater_refresh_component();
}

static void updater_remote_run_check(int request_confirmation) {
    update_remote_options_t options;
    int result;

    kmemset(&options, 0, sizeof(options));
    options.dry_run = 1U;
    options.cancel_check = updater_remote_job_cancel_check;
    kmemset(&updater_remote_result, 0, sizeof(updater_remote_result));
    result = update_remote_check(0, &options, &updater_remote_result);
    updater_last_result = result;
    updater_result_kind = UPDATER_RESULT_REMOTE;
    updater_confirm = result == OK && request_confirmation ?
        UPDATER_CONFIRM_REMOTE_FETCH : UPDATER_CONFIRM_NONE;
    updater_refresh_status();
    updater_refresh_component();
}

static void updater_remote_clear_preflight(void) {
    update_remote_options_t options;

    kmemset(&options, 0, sizeof(options));
    options.dry_run = 1U;
    kmemset(&updater_remote_result, 0, sizeof(updater_remote_result));
    updater_last_result =
        update_remote_clear(&options, &updater_remote_result);
    updater_result_kind = UPDATER_RESULT_REMOTE;
    updater_confirm = updater_last_result == OK ?
        UPDATER_CONFIRM_REMOTE_CLEAR : UPDATER_CONFIRM_NONE;
    updater_refresh_status();
    updater_refresh_component();
}

static void updater_remote_confirm_fetch(void) {
    update_remote_options_t options;

    kmemset(&options, 0, sizeof(options));
    options.cancel_check = updater_remote_job_cancel_check;
    kmemset(&updater_remote_result, 0, sizeof(updater_remote_result));
    updater_last_result =
        update_remote_fetch(0, &options, &updater_remote_result);
    updater_result_kind = UPDATER_RESULT_REMOTE;
}

static int updater_remote_start_job(updater_remote_job_t job,
                                    int request_confirmation) {
    if (job == UPDATER_REMOTE_JOB_NONE) {
        LOG_ERROR("UPDATER", "Job remoto invalido");
        return ERR_INVALID;
    }
    if (updater_remote_job_busy ||
        updater_remote_job != UPDATER_REMOTE_JOB_NONE) {
        LOG_WARN("UPDATER", "Operacao remota da interface ja esta ativa");
        return ERR_STATE;
    }
    updater_remote_cancel_requested = 0U;
    updater_remote_job_confirm = request_confirmation ? 1U : 0U;
    updater_remote_progress_bytes = 0U;
    updater_remote_progress_state = UPDATE_REMOTE_STATE_DISABLED;
    updater_remote_job_busy = 1U;
    updater_remote_job = job;
    updater_confirm = UPDATER_CONFIRM_NONE;
    updater_result_kind = UPDATER_RESULT_REMOTE;
    kmemset(&updater_remote_result, 0, sizeof(updater_remote_result));
    updater_draw();
    return OK;
}

static void updater_remote_finish_cache_refresh(void) {
    updater_result_t result_kind = updater_result_kind;
    int last_result = updater_last_result;
    update_remote_result_t remote_result = updater_remote_result;

    updater_refresh_packages();
    updater_result_kind = result_kind;
    updater_last_result = last_result;
    updater_remote_result = remote_result;
    updater_confirm = UPDATER_CONFIRM_NONE;
}

static void updater_remote_worker_main(void) {
    while (1) {
        updater_remote_job_t job = updater_remote_job;

        if (job == UPDATER_REMOTE_JOB_NONE) {
            process_block(1U);
            continue;
        }
        updater_remote_job_running = job;
        updater_remote_job = UPDATER_REMOTE_JOB_NONE;
        if (job == UPDATER_REMOTE_JOB_CHECK) {
            updater_remote_run_check(updater_remote_job_confirm);
        } else if (job == UPDATER_REMOTE_JOB_FETCH) {
            updater_remote_confirm_fetch();
            updater_remote_finish_cache_refresh();
        } else {
            updater_last_result = ERR_INVALID;
            LOG_ERROR("UPDATER", "Worker recebeu job remoto desconhecido");
        }
        updater_remote_job_running = UPDATER_REMOTE_JOB_NONE;
        updater_remote_job_busy = 0U;
        updater_remote_cancel_requested = 0U;
        updater_refresh_status();
        updater_refresh_component();
        updater_draw();
        process_yield();
    }
}

static void updater_remote_confirm_clear(void) {
    update_remote_options_t options;

    kmemset(&options, 0, sizeof(options));
    kmemset(&updater_remote_result, 0, sizeof(updater_remote_result));
    updater_last_result =
        update_remote_clear(&options, &updater_remote_result);
    updater_result_kind = UPDATER_RESULT_REMOTE;
}

static void updater_confirm_action(void) {
    update_action_options_t options;
    updater_confirm_t confirmation = updater_confirm;

    kmemset(&options, 0, sizeof(options));
    options.cancel_check = updater_cancel_check;
    updater_confirm = UPDATER_CONFIRM_NONE;
    kmemset(&updater_action, 0, sizeof(updater_action));
    if (confirmation == UPDATER_CONFIRM_APPLY) {
        const char* path = updater_selected_package();

        if (!path) return;
        updater_last_result =
            update_apply_file(path, &options, &updater_action);
        updater_result_kind = UPDATER_RESULT_APPLY;
        if (updater_last_result == OK ||
            updater_action.recovery_pending) {
            updater_completed_action_kind = UPDATER_RESULT_APPLY;
            updater_completed_action = updater_action;
        }
    } else if (confirmation == UPDATER_CONFIRM_ROLLBACK) {
        updater_last_result =
            update_rollback(&options, &updater_action);
        updater_result_kind = UPDATER_RESULT_ROLLBACK;
        if (updater_last_result == OK ||
            updater_action.recovery_pending) {
            updater_completed_action_kind = UPDATER_RESULT_ROLLBACK;
            updater_completed_action = updater_action;
        }
    } else if (confirmation == UPDATER_CONFIRM_REMOTE_FETCH) {
        (void)updater_remote_start_job(
            UPDATER_REMOTE_JOB_FETCH, 0);
        return;
    } else if (confirmation == UPDATER_CONFIRM_REMOTE_CLEAR) {
        updater_remote_confirm_clear();
    }
    if (confirmation == UPDATER_CONFIRM_REMOTE_CLEAR) {
        updater_remote_finish_cache_refresh();
    }
    updater_refresh_status();
    updater_refresh_component();
}

static void updater_classic_draw_tabs(void) {
    static const char* names[UPDATER_TAB_COUNT] = {
        " Pacotes ", " Estado ", " Historico ", " Remoto "
    };
    int x = 2;

    for (int index = 0; index < UPDATER_TAB_COUNT; index++) {
        uint8_t color = updater_tab == index ? 0x1F : 0x07;

        video_print_at(x, 2, names[index], color);
        x += (int)kstrlen(names[index]) + 1;
    }
}

static void updater_classic_draw_packages(void) {
    video_draw_box(1, 4, UPDATER_CLASSIC_LIST_WIDTH,
                   SCREEN_ROWS - 8, 0x08);
    video_print_at(3, 4, " Pacotes locais ", 0x0B);
    if (updater_package_count == 0) {
        video_print_at(UPDATER_CLASSIC_LIST_X,
                       UPDATER_CLASSIC_LIST_Y,
                       "Nenhum *.ZUP", 0x08);
    }
    for (int index = 0; index < updater_package_count; index++) {
        uint8_t color = index == updater_selected ? 0x1F : 0x07;

        if (index == updater_selected) {
            video_fill_rect(2, UPDATER_CLASSIC_LIST_Y + index,
                            UPDATER_CLASSIC_LIST_WIDTH - 2,
                            1, ' ', color);
        }
        video_print_at(UPDATER_CLASSIC_LIST_X,
                       UPDATER_CLASSIC_LIST_Y + index,
                       updater_packages[index].name, color);
    }
    if (updater_package_overflow) {
        video_print_at(3, SCREEN_ROWS - 5,
                       "Limite de 16 pacotes atingido", 0x0E);
    }
}

static void updater_classic_draw_result(void) {
    int x = UPDATER_CLASSIC_LIST_WIDTH + 3;

    video_set_cursor(x, 6);
    if (updater_completed_action_kind != UPDATER_RESULT_NONE) {
        video_print(
            updater_completed_action_kind == UPDATER_RESULT_APPLY ?
            "Aplicacao concluida: " : "Rollback concluido: ", 0x07);
        video_print(
            update_action_reason_name(updater_completed_action.reason),
            updater_completed_action.recovery_pending ? 0x0E : 0x0A);
        video_print("\n", 0x07);
        if (updater_completed_action.recovery_pending) {
            video_print("Recuperacao pendente: reinicie.\n", 0x0E);
        } else if (updater_completed_action.reboot_required) {
            video_print(
                "Reboot necessario para recarregar os BMPs.\n", 0x0E);
        }
        if (updater_result_kind == updater_completed_action_kind) return;
        video_print("\nResultado posterior:\n", 0x08);
    }
    if (updater_result_kind == UPDATER_RESULT_NONE) {
        video_print("Selecione um pacote e pressione V ou A.\n", 0x08);
        return;
    }
    if (updater_result_kind == UPDATER_RESULT_VERIFY) {
        video_print("Verificacao: ", 0x07);
        video_print(updater_last_result == OK ? "NONE" :
                    zupd_reason_name(updater_verification.reason),
                    updater_last_result == OK ? 0x0A : 0x0C);
        video_print("\n", 0x07);
        if (updater_verification.entry_count) {
            updater_classic_print_version(
                "Base: ", &updater_verification.base_version,
                updater_verification.base_epoch);
            updater_classic_print_version(
                "Alvo: ", &updater_verification.target_version,
                updater_verification.target_epoch);
        }
        video_print("Nenhuma gravacao foi realizada.\n", 0x0A);
        return;
    }
    video_print("Acao: ", 0x07);
    video_print(update_action_reason_name(updater_action.reason),
                updater_last_result == OK ? 0x0A : 0x0C);
    video_print("\n", 0x07);
    if (updater_action.entry_count) {
        updater_classic_print_version(
            "Origem: ", &updater_action.from_version,
            updater_action.from_epoch);
        updater_classic_print_version(
            "Destino: ", &updater_action.to_version,
            updater_action.to_epoch);
    }
    if (updater_action.recovery_pending) {
        video_print("Recuperacao pendente: reinicie.\n", 0x0E);
    } else if (updater_action.reboot_required) {
        video_print("Reboot necessario para recarregar os BMPs.\n", 0x0E);
    }
}

static void updater_classic_draw_status(void) {
    video_set_cursor(3, 6);
    if (!updater_status_ready) {
        video_print("Status de Update indisponivel.\n", 0x0C);
        return;
    }
    updater_classic_print_version(
        "Build: ", &updater_status.build_version,
        updater_status.build_epoch);
    updater_classic_print_version(
        "Instalado: ", &updater_status.installed_version,
        updater_status.installed_epoch);
    if (updater_status.capabilities.rollback_available) {
        updater_classic_print_version(
            "Rollback: ", &updater_status.rollback_version,
            updater_status.rollback_epoch);
    } else {
        video_print("Rollback: indisponivel\n", 0x08);
    }
    video_print("Estado: ", 0x07);
    video_print(updater_status.state_store == UPDATE_STORE_EMPTY ?
                "BASELINE" :
                update_store_state_name(updater_status.state_store),
                updater_status.state_store == UPDATE_STORE_INVALID ?
                0x0C : 0x0A);
    video_print("\nArquivos atuais: ", 0x07);
    video_print(update_store_state_name(updater_status.current_files),
                updater_status.current_files == UPDATE_STORE_INVALID ?
                0x0C : 0x0A);
    video_print("\nHistorico: ", 0x07);
    video_print(update_store_state_name(updater_status.history_store),
                updater_status.history_store == UPDATE_STORE_INVALID ?
                0x0C : 0x0A);
    video_print("\nJournal: ", 0x07);
    video_print(updater_status.transaction_pending ?
                "PENDING" : "CLEAN",
                updater_status.transaction_pending ? 0x0E : 0x0A);
    video_print("\nAplicacao: ", 0x07);
    video_print(updater_status.capabilities.apply_available ?
                "READY" : "DISABLED",
                updater_status.capabilities.apply_available ? 0x0A : 0x08);
    video_print("  Rollback: ", 0x07);
    video_print(updater_status.capabilities.rollback_available ?
                "READY" : "DISABLED",
                updater_status.capabilities.rollback_available ? 0x0A : 0x08);
    video_print("\nRemoto: ", 0x07);
    if (!updater_remote_status_ready ||
        !updater_remote_status.enabled) {
        video_print("DISABLED\n", 0x08);
    } else if (!updater_remote_status.network_ready ||
               updater_remote_status.state == UPDATE_REMOTE_STATE_FAILED) {
        video_print("DEGRADED\n", 0x0E);
    } else {
        video_print("READY\n", 0x0A);
    }
}

static void updater_classic_draw_history(void) {
    uint32_t count = 0;

    video_set_cursor(3, 6);
    if (update_get_history_count(&count) != OK) {
        video_print("Historico indisponivel ou corrompido.\n", 0x0C);
        return;
    }
    if (count == 0U) {
        video_print("Nenhuma operacao registrada.\n", 0x08);
        return;
    }
    for (uint32_t index = 0; index < count; index++) {
        update_history_entry_t entry;
        char number[11];
        char detail[96];

        if (update_get_history_entry(index, &entry) != OK) break;
        updater_u32_text(entry.sequence, number);
        video_print("#", 0x07);
        video_print(number, 0x07);
        video_print(" ", 0x07);
        video_print(update_history_operation_name(entry.operation), 0x0B);
        video_print(" ", 0x07);
        video_print(update_history_outcome_name(entry.outcome),
                    (entry.outcome == UPDATE_HISTORY_OUTCOME_SUCCESS ||
                     entry.outcome == UPDATE_HISTORY_OUTCOME_RECOVERED) ?
                    0x0A : 0x0C);
        video_print("\n    ", 0x07);
        updater_history_detail_text(detail, sizeof(detail), &entry);
        video_print(detail, 0x08);
        video_print("\n", 0x07);
    }
}

static const char* updater_remote_active_job_name(void) {
    updater_remote_job_t job = updater_remote_job_running !=
                               UPDATER_REMOTE_JOB_NONE ?
                               updater_remote_job_running :
                               updater_remote_job;

    if (job == UPDATER_REMOTE_JOB_CHECK) return "CONSULTANDO";
    if (job == UPDATER_REMOTE_JOB_FETCH) return "BAIXANDO";
    return "AGUARDANDO";
}

static void updater_classic_draw_remote(void) {
    video_set_cursor(3, 6);
    if (!updater_remote_status_ready) {
        video_print("Servico remoto indisponivel.\n", 0x0C);
        return;
    }
    video_print("Sessao: ", 0x07);
    video_print(updater_remote_status.enabled ?
                "HABILITADA" : "DESABILITADA",
                updater_remote_status.enabled ? 0x0A : 0x08);
    video_print("  Rede: ", 0x07);
    video_print(updater_remote_status.network_ready ?
                "READY" : "UNAVAILABLE",
                updater_remote_status.network_ready ? 0x0A : 0x0E);
    video_print("\nEstado: ", 0x07);
    video_print(update_remote_state_name(updater_remote_status.state),
                updater_remote_status.state ==
                    UPDATE_REMOTE_STATE_FAILED ? 0x0C : 0x0B);
    video_print("  Motivo: ", 0x07);
    video_print(update_remote_reason_name(updater_remote_status.reason),
                updater_remote_status.reason ==
                    UPDATE_REMOTE_REASON_NONE ? 0x0A : 0x0E);
    video_print("\nCanal: ", 0x07);
    video_print(UPDATE_REMOTE_CHANNEL_NAME, 0x0B);
    video_print("\nURL: ", 0x07);
    video_print(updater_remote_status.manifest_url, 0x0F);
    video_print("\nCache: ", 0x07);
    video_print(update_remote_store_name(
                    updater_remote_status.cache_store),
                updater_remote_status.cache_store ==
                    UPDATE_REMOTE_STORE_INVALID ? 0x0C : 0x0A);
    if (updater_remote_status.package_cached) {
        video_print("  ", 0x07);
        video_print(updater_remote_status.cached_alias, 0x0B);
    }
    video_print("\nProgresso: ", 0x07);
    {
        char progress[48];

        progress[0] = '\0';
        updater_text_append_u32(
            progress, sizeof(progress),
            updater_remote_status.bytes_received);
        updater_text_append(progress, sizeof(progress), "/");
        updater_text_append_u32(
            progress, sizeof(progress),
            updater_remote_status.total_bytes);
        updater_text_append(progress, sizeof(progress), " retry=");
        updater_text_append_u32(
            progress, sizeof(progress),
            updater_remote_status.retry_count);
        video_print(progress, 0x0F);
    }
    video_print("\n", 0x07);
    if (updater_remote_status.manifest_cached) {
        updater_classic_print_version(
            "Base: ", &updater_remote_status.candidate.base_version,
            updater_remote_status.candidate.base_epoch);
        updater_classic_print_version(
            "Alvo: ", &updater_remote_status.candidate.target_version,
            updater_remote_status.candidate.target_epoch);
        video_print("Pacote: ", 0x07);
        video_print(updater_remote_status.candidate.package_path, 0x0F);
        video_print(" bytes=", 0x08);
        {
            char size[11];

            updater_u32_text(
                updater_remote_status.candidate.package_size, size);
            video_print(size, 0x08);
        }
        video_print("\n", 0x07);
    }
    if (updater_result_kind == UPDATER_RESULT_REMOTE) {
        video_print("Ultimo resultado: ", 0x07);
        video_print(update_remote_reason_name(
                        updater_remote_result.reason),
                    updater_last_result == OK ? 0x0A : 0x0C);
        video_print("\n", 0x07);
    }
    if (updater_remote_job_busy) {
        video_print("Operacao: ", 0x07);
        video_print(updater_remote_active_job_name(), 0x0E);
        video_print("  Esc/F12 cancela\n", 0x0E);
    }
}

static void updater_classic_draw_confirmation(void) {
    if (updater_confirm == UPDATER_CONFIRM_NONE) return;
    video_fill_rect(30, SCREEN_ROWS - 9, 68, 5, ' ', 0x1E);
    video_draw_box(30, SCREEN_ROWS - 9, 68, 5, 0x0E);
    {
        const char* question =
            updater_confirm == UPDATER_CONFIRM_APPLY ?
            "Confirmar aplicacao autenticada?" :
            updater_confirm == UPDATER_CONFIRM_ROLLBACK ?
            "Confirmar rollback validado?" :
            updater_confirm == UPDATER_CONFIRM_REMOTE_FETCH ?
            "Confirmar download remoto autenticado?" :
            "Confirmar limpeza do cache remoto?";

        video_print_at(33, SCREEN_ROWS - 8, question, 0x1E);
    }
    video_print_at(33, SCREEN_ROWS - 6,
                   "Enter confirma | Esc cancela", 0x1E);
}

static void updater_draw_classic(void) {
    video_begin_update();
    video_fill_rect(0, 0, SCREEN_COLS, SCREEN_ROWS, ' ', 0x07);
    video_fill_rect(0, 0, SCREEN_COLS, 1, ' ', 0x1F);
    video_print_at(2, 0, "ZephyrOS System Updater", 0x1F);
    updater_classic_draw_tabs();
    if (updater_tab == UPDATER_TAB_PACKAGES) {
        updater_classic_draw_packages();
        updater_classic_draw_result();
    } else if (updater_tab == UPDATER_TAB_STATUS) {
        updater_classic_draw_status();
    } else if (updater_tab == UPDATER_TAB_HISTORY) {
        updater_classic_draw_history();
    } else {
        updater_classic_draw_remote();
    }
    if (updater_tab == UPDATER_TAB_REMOTE) {
        video_print_at(2, SCREEN_ROWS - 2,
                       "H=habilitar C=consultar D=baixar X=limpar "
                       "F5=atualizar Esc=fechar", 0x70);
    } else {
        video_print_at(2, SCREEN_ROWS - 2,
                       "Tab=aba Setas=selecionar F5=atualizar V=verificar "
                       "A=aplicar B=rollback Esc=fechar", 0x70);
    }
    updater_classic_draw_confirmation();
    taskbar_draw();
    video_end_update();
}

static void updater_gui_line(int x, int y, const char* label,
                             const char* value, uint32_t color) {
    gui_draw_text((uint32_t)x, (uint32_t)y, label, GUI_COLOR_TEXT);
    gui_draw_text((uint32_t)(x + 150), (uint32_t)y, value, color);
}

static void updater_gui_draw_tabs(int x, int y) {
    static const char* names[UPDATER_TAB_COUNT] = {
        "Pacotes", "Estado", "Historico", "Remoto"
    };

    for (int index = 0; index < UPDATER_TAB_COUNT; index++) {
        gui_draw_button((uint32_t)(x + index * 116), (uint32_t)y,
                        108, 28, names[index], updater_tab == index);
    }
}

static void updater_gui_draw_packages(int x, int y, int width, int height) {
    int list_width = 220;
    int visible = (height - 94) / UPDATER_MODERN_ROW_HEIGHT;
    int result_y = y + 52;

    gui_draw_panel((uint32_t)x, (uint32_t)y, (uint32_t)list_width,
                   (uint32_t)(height - 54), GUI_COLOR_BG, 0);
    for (int row = 0; row < visible; row++) {
        int index = updater_scroll + row;
        int row_y = y + 10 + row * UPDATER_MODERN_ROW_HEIGHT;

        if (index >= updater_package_count) break;
        if (index == updater_selected) {
            gui_draw_panel((uint32_t)(x + 6), (uint32_t)(row_y - 3),
                           (uint32_t)(list_width - 12), 22,
                           GUI_COLOR_TITLE_BG, 1);
        }
        gui_draw_text((uint32_t)(x + 12), (uint32_t)row_y,
                      updater_packages[index].name,
                      index == updater_selected ?
                      GUI_COLOR_TEXT_W : GUI_COLOR_TEXT);
    }
    gui_draw_panel((uint32_t)(x + list_width + 10), (uint32_t)y,
                   (uint32_t)(width - list_width - 10),
                   (uint32_t)(height - 54), GUI_COLOR_BG, 0);
    gui_draw_text((uint32_t)(x + list_width + 26), (uint32_t)(y + 16),
                  updater_selected_package() ?
                  updater_selected_package() : "Nenhum pacote",
                  GUI_COLOR_TEXT);
    if (updater_completed_action_kind != UPDATER_RESULT_NONE) {
        gui_draw_text(
            (uint32_t)(x + list_width + 26), (uint32_t)result_y,
            updater_completed_action_kind == UPDATER_RESULT_APPLY ?
            "Aplicacao concluida: NONE" : "Rollback concluido: NONE",
            updater_completed_action.recovery_pending ?
            0x00808000U : 0x00008000U);
        result_y += 28;
        gui_draw_text(
            (uint32_t)(x + list_width + 26), (uint32_t)result_y,
            updater_completed_action.recovery_pending ?
            "Recuperacao pendente: reinicie." :
            "Reboot necessario para recarregar os BMPs.",
            0x00808000U);
        result_y += 36;
    }
    if (updater_result_kind == UPDATER_RESULT_VERIFY) {
        gui_draw_text((uint32_t)(x + list_width + 26),
                      (uint32_t)result_y,
                      updater_last_result == OK ?
                      "Verificacao: NONE" :
                      zupd_reason_name(updater_verification.reason),
                      updater_last_result == OK ?
                      0x00008000U : 0x00800000U);
    } else if (updater_result_kind != UPDATER_RESULT_NONE &&
               updater_result_kind != updater_completed_action_kind) {
        gui_draw_text((uint32_t)(x + list_width + 26),
                      (uint32_t)result_y,
                      update_action_reason_name(updater_action.reason),
                      updater_last_result == OK ?
                      0x00008000U : 0x00800000U);
    }
    if (updater_package_overflow) {
        gui_draw_text((uint32_t)(x + 12), (uint32_t)(y + height - 72),
                      "Limite de 16 pacotes", 0x00808000U);
    }
}

static void updater_gui_draw_status(int x, int y) {
    char value[80];

    if (!updater_status_ready) {
        gui_draw_text((uint32_t)x, (uint32_t)y,
                      "Status de Update indisponivel", 0x00800000U);
        return;
    }
    updater_version_text(value, sizeof(value),
                         &updater_status.build_version,
                         updater_status.build_epoch);
    updater_gui_line(x, y, "Build", value, GUI_COLOR_TEXT);
    updater_version_text(value, sizeof(value),
                         &updater_status.installed_version,
                         updater_status.installed_epoch);
    updater_gui_line(x, y + 30, "Instalado", value, GUI_COLOR_TEXT);
    if (updater_status.capabilities.rollback_available) {
        updater_version_text(value, sizeof(value),
                             &updater_status.rollback_version,
                             updater_status.rollback_epoch);
    } else {
        value[0] = '\0';
        updater_text_append(value, sizeof(value), "DISABLED");
    }
    updater_gui_line(x, y + 60, "Rollback", value,
                     updater_status.capabilities.rollback_available ?
                     GUI_COLOR_TEXT : GUI_COLOR_BORDER_D);
    updater_gui_line(
        x, y + 90, "Estado",
        updater_status.state_store == UPDATE_STORE_EMPTY ?
        "BASELINE" : update_store_state_name(updater_status.state_store),
        updater_status.state_store == UPDATE_STORE_INVALID ?
        0x00800000U : 0x00008000U);
    updater_gui_line(
        x, y + 120, "Arquivos",
        update_store_state_name(updater_status.current_files),
        updater_status.current_files == UPDATE_STORE_INVALID ?
        0x00800000U : 0x00008000U);
    updater_gui_line(
        x, y + 150, "Historico",
        update_store_state_name(updater_status.history_store),
        updater_status.history_store == UPDATE_STORE_INVALID ?
        0x00800000U : 0x00008000U);
    updater_gui_line(
        x, y + 180, "Journal",
        updater_status.transaction_pending ? "PENDING" : "CLEAN",
        updater_status.transaction_pending ?
        0x00808000U : 0x00008000U);
    updater_gui_line(
        x, y + 210, "Aplicacao",
        updater_status.capabilities.apply_available ? "READY" : "DISABLED",
        updater_status.capabilities.apply_available ?
        0x00008000U : GUI_COLOR_BORDER_D);
    updater_gui_line(
        x, y + 240, "Rollback cap.",
        updater_status.capabilities.rollback_available ? "READY" : "DISABLED",
        updater_status.capabilities.rollback_available ?
        0x00008000U : GUI_COLOR_BORDER_D);
    if (!updater_remote_status_ready ||
        !updater_remote_status.enabled) {
        updater_gui_line(x, y + 270, "Remoto", "DISABLED",
                         GUI_COLOR_BORDER_D);
    } else if (!updater_remote_status.network_ready ||
               updater_remote_status.state == UPDATE_REMOTE_STATE_FAILED) {
        updater_gui_line(x, y + 270, "Remoto", "DEGRADED",
                         0x00808000U);
    } else {
        updater_gui_line(x, y + 270, "Remoto", "READY",
                         0x00008000U);
    }
}

static void updater_gui_draw_history(int x, int y) {
    uint32_t count = 0;

    if (update_get_history_count(&count) != OK) {
        gui_draw_text((uint32_t)x, (uint32_t)y,
                      "Historico indisponivel ou corrompido",
                      0x00800000U);
        return;
    }
    if (count == 0U) {
        gui_draw_text((uint32_t)x, (uint32_t)y,
                      "Nenhuma operacao registrada", GUI_COLOR_BORDER_D);
        return;
    }
    for (uint32_t index = 0; index < count; index++) {
        update_history_entry_t entry;
        char line[96];
        char detail[96];
        int row_y = y + (int)index * 36;

        if (update_get_history_entry(index, &entry) != OK) break;
        line[0] = '#';
        line[1] = '\0';
        updater_text_append_u32(line, sizeof(line), entry.sequence);
        updater_text_append(line, sizeof(line), " ");
        updater_text_append(
            line, sizeof(line),
            update_history_operation_name(entry.operation));
        updater_text_append(line, sizeof(line), " ");
        updater_text_append(
            line, sizeof(line),
            update_history_outcome_name(entry.outcome));
        gui_draw_text((uint32_t)x,
                      (uint32_t)row_y,
                      line,
                      (entry.outcome == UPDATE_HISTORY_OUTCOME_SUCCESS ||
                       entry.outcome == UPDATE_HISTORY_OUTCOME_RECOVERED) ?
                      0x00008000U : 0x00800000U);
        updater_history_detail_text(detail, sizeof(detail), &entry);
        gui_draw_text((uint32_t)(x + 16), (uint32_t)(row_y + 16),
                      detail, GUI_COLOR_BORDER_D);
    }
}

static void updater_gui_draw_remote(int x, int y) {
    char value[UPDATE_REMOTE_URL_SIZE];

    if (!updater_remote_status_ready) {
        gui_draw_text((uint32_t)x, (uint32_t)y,
                      "Servico remoto indisponivel", 0x00800000U);
        return;
    }
    updater_gui_line(
        x, y, "Sessao",
        updater_remote_status.enabled ? "HABILITADA" : "DESABILITADA",
        updater_remote_status.enabled ? 0x00008000U : GUI_COLOR_BORDER_D);
    updater_gui_line(
        x, y + 30, "Rede",
        updater_remote_status.network_ready ? "READY" : "UNAVAILABLE",
        updater_remote_status.network_ready ? 0x00008000U : 0x00808000U);
    updater_gui_line(
        x, y + 60, "Estado",
        update_remote_state_name(updater_remote_status.state),
        updater_remote_status.state == UPDATE_REMOTE_STATE_FAILED ?
        0x00800000U : GUI_COLOR_TEXT);
    updater_gui_line(
        x, y + 90, "Motivo",
        update_remote_reason_name(updater_remote_status.reason),
        updater_remote_status.reason == UPDATE_REMOTE_REASON_NONE ?
        0x00008000U : 0x00808000U);
    updater_gui_line(
        x, y + 120, "Cache",
        update_remote_store_name(updater_remote_status.cache_store),
        updater_remote_status.cache_store == UPDATE_REMOTE_STORE_INVALID ?
        0x00800000U : 0x00008000U);
    updater_gui_line(
        x, y + 150, "Alias",
        updater_remote_status.package_cached ?
        updater_remote_status.cached_alias : "NENHUM",
        updater_remote_status.package_cached ?
        GUI_COLOR_TEXT : GUI_COLOR_BORDER_D);
    value[0] = '\0';
    updater_text_append_u32(
        value, sizeof(value), updater_remote_status.bytes_received);
    updater_text_append(value, sizeof(value), "/");
    updater_text_append_u32(
        value, sizeof(value), updater_remote_status.total_bytes);
    updater_text_append(value, sizeof(value), " retry=");
    updater_text_append_u32(
        value, sizeof(value), updater_remote_status.retry_count);
    if (updater_remote_status.manifest_cached) {
        updater_text_append(value, sizeof(value), " gen=");
        updater_text_append_u32(
            value, sizeof(value),
            updater_remote_status.candidate.generation);
    }
    updater_gui_line(x, y + 180, "Progresso", value, GUI_COLOR_TEXT);
    if (updater_remote_status.manifest_cached) {
        updater_version_text(
            value, sizeof(value),
            &updater_remote_status.candidate.base_version,
            updater_remote_status.candidate.base_epoch);
        updater_gui_line(x, y + 210, "Base", value, GUI_COLOR_TEXT);
        updater_version_text(
            value, sizeof(value),
            &updater_remote_status.candidate.target_version,
            updater_remote_status.candidate.target_epoch);
        updater_gui_line(x, y + 240, "Alvo", value, GUI_COLOR_TEXT);
        value[0] = '\0';
        updater_text_append(
            value, sizeof(value),
            updater_remote_status.candidate.package_path);
        updater_text_append(value, sizeof(value), " ");
        updater_text_append_u32(
            value, sizeof(value),
            updater_remote_status.candidate.package_size);
        updater_text_append(value, sizeof(value), " bytes");
        updater_gui_line(
            x, y + 270, "Pacote", value, GUI_COLOR_TEXT);
    }
    updater_gui_line(x, y + 300, "Canal",
                     UPDATE_REMOTE_CHANNEL_NAME, GUI_COLOR_TEXT);
    if (updater_remote_job_busy) {
        updater_gui_line(
            x, y + 330, "Operacao",
            updater_remote_active_job_name(), 0x00808000U);
        updater_gui_line(
            x, y + 360, "Cancelar",
            "Esc ou F12", 0x00808000U);
    }
}

static void updater_gui_draw_confirmation(
    int x, int y, int width, int height) {
    int dialog_x;
    int dialog_y;
    const char* question;

    if (updater_confirm == UPDATER_CONFIRM_NONE) return;
    dialog_x = x + (width - 420) / 2;
    dialog_y = y + (height - 150) / 2;
    gui_draw_panel((uint32_t)dialog_x, (uint32_t)dialog_y,
                   420, 150, GUI_COLOR_BG, 0);
    question = updater_confirm == UPDATER_CONFIRM_APPLY ?
               "Confirmar aplicacao autenticada?" :
               updater_confirm == UPDATER_CONFIRM_ROLLBACK ?
               "Confirmar rollback validado?" :
               updater_confirm == UPDATER_CONFIRM_REMOTE_FETCH ?
               "Confirmar download remoto?" :
               "Confirmar limpeza do cache?";
    gui_draw_text(
        (uint32_t)(dialog_x + 24), (uint32_t)(dialog_y + 28),
        question, GUI_COLOR_TEXT);
    gui_draw_button((uint32_t)(dialog_x + 82),
                    (uint32_t)(dialog_y + 92),
                    112, 30, "Confirmar", 1);
    gui_draw_button((uint32_t)(dialog_x + 226),
                    (uint32_t)(dialog_y + 92),
                    112, 30, "Cancelar", 0);
}

static void updater_draw_modern(int x, int y, int width, int height) {
    int content_y = y + 48;
    int content_height = height - 104;

    gui_draw_panel((uint32_t)x, (uint32_t)y, (uint32_t)width,
                   (uint32_t)height, GUI_COLOR_BG, 0);
    updater_gui_draw_tabs(x + UPDATER_MODERN_MARGIN,
                          y + UPDATER_MODERN_MARGIN);
    if (updater_tab == UPDATER_TAB_PACKAGES) {
        updater_gui_draw_packages(
            x + UPDATER_MODERN_MARGIN, content_y,
            width - 2 * UPDATER_MODERN_MARGIN, content_height);
    } else if (updater_tab == UPDATER_TAB_STATUS) {
        updater_gui_draw_status(x + 32, content_y + 16);
    } else if (updater_tab == UPDATER_TAB_HISTORY) {
        updater_gui_draw_history(x + 32, content_y + 16);
    } else {
        updater_gui_draw_remote(x + 32, content_y + 16);
    }
    if (updater_tab == UPDATER_TAB_REMOTE) {
        gui_draw_button((uint32_t)(x + 16), (uint32_t)(y + height - 44),
                        112, 28,
                        updater_remote_status.enabled ?
                        "Desabilitar" : "Habilitar", 0);
        gui_draw_button((uint32_t)(x + 140), (uint32_t)(y + height - 44),
                        112, 28, "Consultar", 0);
        gui_draw_button((uint32_t)(x + 264), (uint32_t)(y + height - 44),
                        112, 28, "Baixar", 0);
        gui_draw_button((uint32_t)(x + 388), (uint32_t)(y + height - 44),
                        112, 28, "Limpar", 0);
    } else {
        gui_draw_button((uint32_t)(x + 16), (uint32_t)(y + height - 44),
                        112, 28, "Atualizar", 0);
        gui_draw_button((uint32_t)(x + 140), (uint32_t)(y + height - 44),
                        112, 28, "Verificar", 0);
        gui_draw_button((uint32_t)(x + 264), (uint32_t)(y + height - 44),
                        112, 28, "Aplicar", 0);
        gui_draw_button((uint32_t)(x + 388), (uint32_t)(y + height - 44),
                        112, 28, "Rollback", 0);
    }
    updater_gui_draw_confirmation(x, y, width, height);
}

static void updater_keep_selection_visible(void) {
    int visible = (updater_gui_height - 198) / UPDATER_MODERN_ROW_HEIGHT;

    if (visible < 1) visible = 1;
    if (updater_selected < updater_scroll) {
        updater_scroll = updater_selected;
    } else if (updater_selected >= updater_scroll + visible) {
        updater_scroll = updater_selected - visible + 1;
    }
}

static void updater_change_selection(int direction) {
    if (updater_package_count == 0) return;
    updater_selected += direction;
    if (updater_selected < 0) {
        updater_selected = updater_package_count - 1;
    } else if (updater_selected >= updater_package_count) {
        updater_selected = 0;
    }
    updater_keep_selection_visible();
    updater_confirm = UPDATER_CONFIRM_NONE;
    updater_result_kind = UPDATER_RESULT_NONE;
    updater_completed_action_kind = UPDATER_RESULT_NONE;
}

int updater_init(void) {
    process_t* worker;

    LOG_INFO("UPDATER", "Inicializando System Updater");
    updater_active = 0;
    updater_hosted = 0;
    updater_initialized = 0;
    updater_tab = UPDATER_TAB_PACKAGES;
    updater_confirm = UPDATER_CONFIRM_NONE;
    updater_result_kind = UPDATER_RESULT_NONE;
    updater_completed_action_kind = UPDATER_RESULT_NONE;
    kmemset(&updater_completed_action, 0,
            sizeof(updater_completed_action));
    updater_remote_job = UPDATER_REMOTE_JOB_NONE;
    updater_remote_job_running = UPDATER_REMOTE_JOB_NONE;
    updater_remote_job_busy = 0U;
    updater_remote_cancel_requested = 0U;
    if (!update_is_ready()) {
        recovery_mark_disabled(
            RECOVERY_COMPONENT_SYSTEM_UPDATER, ERR_STATE,
            "Servico Update indisponivel para a interface");
        LOG_ERROR("UPDATER", "Servico Update nao inicializado");
        return ERR_STATE;
    }
    worker = process_create(
        "Updater Worker", updater_remote_worker_main);
    if (!worker) {
        recovery_mark_disabled(
            RECOVERY_COMPONENT_SYSTEM_UPDATER, ERR_MEM,
            "Worker cooperativo do System Updater indisponivel");
        LOG_ERROR("UPDATER", "Falha ao criar worker cooperativo");
        return ERR_MEM;
    }
    updater_initialized = 1;
    updater_refresh_all();
    LOG_INFO("UPDATER", "System Updater inicializado com sucesso");
    return OK;
}

int updater_open(void) {
    if (!updater_initialized ||
        !recovery_is_enabled(RECOVERY_COMPONENT_SYSTEM_UPDATER)) {
        LOG_ERROR("UPDATER", "System Updater indisponivel");
        return ERR_STATE;
    }
    if (updater_active && updater_hosted) {
        wm_set_active(1);
        return wm_register_hosted_app(&updater_hosted_app);
    }
    updater_active = 1;
    updater_tab = UPDATER_TAB_PACKAGES;
    updater_confirm = UPDATER_CONFIRM_NONE;
    updater_result_kind = UPDATER_RESULT_NONE;
    updater_completed_action_kind = UPDATER_RESULT_NONE;
    kmemset(&updater_completed_action, 0,
            sizeof(updater_completed_action));
    updater_refresh_all();
    updater_mode = UPDATER_MODE_CLASSIC;
    if (desktop_get_mode() == DESKTOP_MODE_MODERN) {
        wm_set_active(1);
        updater_hosted = 1;
        updater_mode = UPDATER_MODE_MODERN;
        if (wm_register_hosted_app(&updater_hosted_app) == OK) {
            LOG_INFO("UPDATER", "System Updater moderno aberto");
            return OK;
        }
        updater_hosted = 0;
        updater_mode = UPDATER_MODE_CLASSIC;
        wm_set_active(0);
        LOG_WARN("UPDATER", "Workspace moderno indisponivel; usando TUI");
    }
    desktop_set_active(0);
    updater_draw_classic();
    LOG_INFO("UPDATER", "System Updater classico aberto");
    return OK;
}

void updater_close(void) {
    if (!updater_active) return;
    if (updater_remote_job_busy) {
        updater_remote_cancel_requested = 1U;
        return;
    }
    if (updater_hosted) {
        (void)wm_close_hosted_app(WM_APP_UPDATER);
        return;
    }
    updater_active = 0;
    updater_confirm = UPDATER_CONFIRM_NONE;
    desktop_set_active(1);
    desktop_draw();
    LOG_INFO("UPDATER", "System Updater fechado");
}

void updater_draw(void) {
    if (!updater_active) return;
    if (updater_hosted) {
        wm_request_hosted_redraw(WM_APP_UPDATER);
        return;
    }
    updater_draw_classic();
}

void updater_handle_key(uint8_t scancode) {
    if (!updater_active || (scancode & 0x80U)) return;
    if (updater_remote_job_busy) {
        if (scancode == UPDATER_SCANCODE_ESC ||
            scancode == UPDATER_SCANCODE_F12) {
            updater_remote_cancel_requested = 1U;
            updater_draw();
        }
        return;
    }
    if (updater_confirm != UPDATER_CONFIRM_NONE) {
        if (scancode == UPDATER_SCANCODE_ENTER) updater_confirm_action();
        if (scancode == UPDATER_SCANCODE_ESC ||
            scancode == UPDATER_SCANCODE_F12) {
            updater_confirm = UPDATER_CONFIRM_NONE;
        }
        updater_draw();
        return;
    }
    if (scancode == UPDATER_SCANCODE_ESC) {
        if (updater_mode == UPDATER_MODE_CLASSIC) updater_close();
        return;
    }
    if (scancode == UPDATER_SCANCODE_TAB) {
        updater_tab = (updater_tab_t)((updater_tab + 1) %
                                      UPDATER_TAB_COUNT);
        updater_confirm = UPDATER_CONFIRM_NONE;
    } else if (scancode == UPDATER_SCANCODE_F5) {
        updater_completed_action_kind = UPDATER_RESULT_NONE;
        updater_refresh_all();
    } else if (scancode == UPDATER_SCANCODE_UP &&
               updater_tab == UPDATER_TAB_PACKAGES) {
        updater_change_selection(-1);
    } else if (scancode == UPDATER_SCANCODE_DOWN &&
               updater_tab == UPDATER_TAB_PACKAGES) {
        updater_change_selection(1);
    } else if (scancode == UPDATER_SCANCODE_V &&
               updater_tab == UPDATER_TAB_PACKAGES) {
        updater_verify_selected();
    } else if (scancode == UPDATER_SCANCODE_A &&
               updater_tab == UPDATER_TAB_PACKAGES) {
        updater_preflight_apply();
    } else if (scancode == UPDATER_SCANCODE_B &&
               updater_tab != UPDATER_TAB_REMOTE) {
        updater_preflight_rollback();
    } else if (updater_tab == UPDATER_TAB_REMOTE &&
               scancode == UPDATER_SCANCODE_H) {
        updater_remote_toggle();
    } else if (updater_tab == UPDATER_TAB_REMOTE &&
               scancode == UPDATER_SCANCODE_C) {
        (void)updater_remote_start_job(
            UPDATER_REMOTE_JOB_CHECK, 0);
    } else if (updater_tab == UPDATER_TAB_REMOTE &&
               scancode == UPDATER_SCANCODE_D) {
        (void)updater_remote_start_job(
            UPDATER_REMOTE_JOB_CHECK, 1);
    } else if (updater_tab == UPDATER_TAB_REMOTE &&
               scancode == UPDATER_SCANCODE_X) {
        updater_remote_clear_preflight();
    }
    updater_draw();
}

static int updater_point_in(int px, int py, int x, int y,
                            int width, int height) {
    return px >= x && px < x + width && py >= y && py < y + height;
}

static int updater_handle_confirmation_click(int px, int py) {
    int dialog_x = updater_gui_x + (updater_gui_width - 420) / 2;
    int dialog_y = updater_gui_y + (updater_gui_height - 150) / 2;

    if (updater_point_in(px, py, dialog_x + 82, dialog_y + 92,
                         112, 30)) {
        updater_confirm_action();
    } else if (updater_point_in(px, py, dialog_x + 226,
                                dialog_y + 92, 112, 30)) {
        updater_confirm = UPDATER_CONFIRM_NONE;
    }
    return 1;
}

int updater_handle_mouse(mouse_event_t* event) {
    int px;
    int py;
    int bottom;

    if (!event || !updater_active || !updater_hosted) return 0;
    if (event->event != MOUSE_EVENT_PRESS ||
        !(event->changed & MOUSE_BTN_LEFT)) return 1;
    if (updater_remote_job_busy) return 1;
    px = event->x;
    py = event->y;
    if (updater_confirm != UPDATER_CONFIRM_NONE) {
        return updater_handle_confirmation_click(px, py);
    }
    for (int index = 0; index < UPDATER_TAB_COUNT; index++) {
        if (updater_point_in(
                px, py, updater_gui_x + UPDATER_MODERN_MARGIN + index * 116,
                updater_gui_y + UPDATER_MODERN_MARGIN, 108, 28)) {
            updater_tab = (updater_tab_t)index;
            updater_confirm = UPDATER_CONFIRM_NONE;
            return 1;
        }
    }
    if (updater_tab == UPDATER_TAB_PACKAGES &&
        updater_point_in(px, py, updater_gui_x + UPDATER_MODERN_MARGIN,
                         updater_gui_y + 48, 220,
                         updater_gui_height - 158)) {
        int row = (py - (updater_gui_y + 58)) /
                  UPDATER_MODERN_ROW_HEIGHT;
        int index = updater_scroll + row;

        if (row >= 0 && index < updater_package_count) {
            updater_selected = index;
            updater_confirm = UPDATER_CONFIRM_NONE;
            updater_result_kind = UPDATER_RESULT_NONE;
            updater_completed_action_kind = UPDATER_RESULT_NONE;
        }
        return 1;
    }
    bottom = updater_gui_y + updater_gui_height - 44;
    if (updater_tab == UPDATER_TAB_REMOTE) {
        if (updater_point_in(
                px, py, updater_gui_x + 16, bottom, 112, 28)) {
            updater_remote_toggle();
        } else if (updater_point_in(
                       px, py, updater_gui_x + 140, bottom, 112, 28)) {
            (void)updater_remote_start_job(
                UPDATER_REMOTE_JOB_CHECK, 0);
        } else if (updater_point_in(
                       px, py, updater_gui_x + 264, bottom, 112, 28)) {
            (void)updater_remote_start_job(
                UPDATER_REMOTE_JOB_CHECK, 1);
        } else if (updater_point_in(
                       px, py, updater_gui_x + 388, bottom, 112, 28)) {
            updater_remote_clear_preflight();
        }
        return 1;
    }
    if (updater_point_in(px, py, updater_gui_x + 16, bottom, 112, 28)) {
        updater_completed_action_kind = UPDATER_RESULT_NONE;
        updater_refresh_all();
    } else if (updater_point_in(
                   px, py, updater_gui_x + 140, bottom, 112, 28)) {
        updater_verify_selected();
    } else if (updater_point_in(
                   px, py, updater_gui_x + 264, bottom, 112, 28)) {
        updater_preflight_apply();
    } else if (updater_point_in(
                   px, py, updater_gui_x + 388, bottom, 112, 28)) {
        updater_preflight_rollback();
    }
    return 1;
}

int updater_is_open(void) {
    return updater_active;
}

updater_mode_t updater_get_mode(void) {
    return updater_mode;
}

static void updater_hosted_draw(int x, int y, int width, int height) {
    updater_gui_x = x;
    updater_gui_y = y;
    updater_gui_width = width;
    updater_gui_height = height;
    updater_draw_modern(x, y, width, height);
}

static void updater_hosted_key(uint8_t scancode) {
    updater_handle_key(scancode);
}

static int updater_hosted_mouse(mouse_event_t* event, int x, int y,
                                int width, int height) {
    updater_gui_x = x;
    updater_gui_y = y;
    updater_gui_width = width;
    updater_gui_height = height;
    return updater_handle_mouse(event);
}

static void updater_hosted_close(void) {
    if (updater_remote_job_busy) {
        updater_remote_cancel_requested = 1U;
    }
    updater_hosted = 0;
    updater_active = 0;
    updater_confirm = UPDATER_CONFIRM_NONE;
    LOG_INFO("UPDATER", "System Updater moderno fechado");
}
