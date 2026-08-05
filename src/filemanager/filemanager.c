#include "ui/filemanager.h"
#include "apps/shell.h"
#include "core/video.h"
#include "process/process.h"
#include "core/keyboard.h"
#include "fs/fs.h"
#include "fs/storage.h"
#include "fs/file_index.h"
#include "core/memory.h"
#include "core/string.h"
#include "ui/taskbar.h"
#include "ui/desktop.h"
#include "ui/wm.h"
#include "ui/icons.h"
#include "ui/gui.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "drivers/font.h"
#include "drivers/mouse.h"
#include "drivers/vesa.h"
#include "ui/display.h"

/* As primitivas GUI deste arquivo sao usadas pelo Explorer Classic. */
#define gui_draw_text gui_draw_scaled_text

#define FM_CLASSIC_VISIBLE_ROWS 17
#define FM_CLASSIC_MARGIN ((int)display_scale_px(24))
#define FM_CLASSIC_MIN_WIDTH 660
#define FM_CLASSIC_MIN_HEIGHT 360
#define FM_CLASSIC_SIDE_WIDTH ((int)display_scale_px(184))
#define FM_CLASSIC_CONTENT_OFFSET ((int)display_scale_px(30))
#define FM_CLASSIC_INSET ((int)display_scale_px(8))
#define FM_CLASSIC_PANE_GAP ((int)display_scale_px(8))
#define FM_CLASSIC_TOOLBAR_HEIGHT ((int)display_scale_px(28))
#define FM_CLASSIC_ADDRESS_HEIGHT ((int)display_scale_px(28))
#define FM_CLASSIC_HEADER_HEIGHT ((int)display_scale_px(28))
#define FM_CLASSIC_STATUS_HEIGHT ((int)display_scale_px(28))
#define FM_CLASSIC_ROW_HEIGHT ((int)display_scale_px(28))
#define FM_CLASSIC_ROW_GAP ((int)display_scale_px(2))
#define FM_CLASSIC_MAX_TEXT 256
#define FM_CLASSIC_SIDE_ITEM_COUNT 8
#define FM_CLASSIC_SIDE_HEADER_OFFSET ((int)display_scale_px(34))
#define FM_CLASSIC_CONTENT_FIXED_HEIGHT \
    (FM_CLASSIC_CONTENT_OFFSET + FM_CLASSIC_TOOLBAR_HEIGHT + \
     FM_CLASSIC_PANE_GAP + FM_CLASSIC_ADDRESS_HEIGHT + \
     FM_CLASSIC_PANE_GAP + FM_CLASSIC_STATUS_HEIGHT + \
     FM_CLASSIC_PANE_GAP + (int)display_scale_px(8))
#define FM_CLASSIC_DEFAULT_WIDTH 720
#define FM_CLASSIC_DEFAULT_HEIGHT 500
#define FM_CLASSIC_SEARCH_BUTTON_WIDTH ((int)display_scale_px(112))
#define FM_SEARCH_REDRAW_STEP 8U
#define FM_SEARCH_PATH_SIZE \
    (FM_MAX_PATH + FM_VOLUME_ID_LEN + FM_NAME_LEN + 4)
#define FM_CLASSIC_WARNING_COLOR 0x00FF6B6BU
#define FM_SCANCODE_CTRL_MAKE 0x1DU
#define FM_SCANCODE_CTRL_BREAK 0x9DU
#define FM_SCANCODE_F 0x21U
#define FM_SEARCH_SCANCODE_ESCAPE 0x01U
#define FM_SEARCH_SCANCODE_BACKSPACE 0x0EU
#define FM_SEARCH_SCANCODE_ENTER 0x1CU
#define FM_SEARCH_SCANCODE_HOME 0x47U
#define FM_SEARCH_SCANCODE_UP 0x48U
#define FM_SEARCH_SCANCODE_PAGE_UP 0x49U
#define FM_SEARCH_SCANCODE_END 0x4FU
#define FM_SEARCH_SCANCODE_DOWN 0x50U
#define FM_SEARCH_SCANCODE_PAGE_DOWN 0x51U
#define FM_DIRECTORY_ATTRIBUTE 0x10U
#define FM_FAT_BASE_LENGTH 8U
#define FM_FAT_EXTENSION_LENGTH 3U

static fm_state_t state;
static char input_buffer[32];
static int input_pos = 0;
static int input_mode = 0;
static int rename_mode = 0;
static int confirm_delete = 0;
static int create_dir_mode = 0;
static int create_file_mode = 0;
static int fm_help_mode = 0;
static int fm_fallback_logged = 0;
static char old_name[FM_NAME_LEN];
static int fm_hosted = 0;
static int fm_hosted_x = 0;
static int fm_hosted_y = 0;
static int fm_hosted_width = 0;
static int fm_hosted_height = 0;
/* Resultados globais ficam em BSS; a pilha dos processos tem 4 KiB. */
static file_index_result_t fm_search_results[FILE_INDEX_MAX_RESULTS];
static file_index_search_status_t fm_search_status;
static file_index_status_t fm_search_index_status;
static char fm_search_query[FILE_INDEX_QUERY_SIZE];
static int fm_search_active = 0;
static int fm_search_input = 0;
static uint32_t fm_search_pos = 0;
static int fm_search_selected = 0;
static int fm_search_scroll = 0;
static int fm_search_last_clicked = -1;
static uint32_t fm_search_event_generation = 0;
static file_index_state_t fm_search_index_state =
    FILE_INDEX_STATE_UNINITIALIZED;


static const char* side_pane_names[] = {
    "Este Computador",
    "Lixeira",
    "Desktop",
    "Documentos",
    "Downloads",
    "Imagens",
    "Musica",
    "Videos"
};
static const char* side_pane_paths[] = {
    "",
    "/.trash",
    "/Desktop",
    "/Documentos",
    "/Downloads",
    "/Imagens",
    "/Musica",
    "/Videos"
};
static const int side_pane_count = FM_CLASSIC_SIDE_ITEM_COUNT;

static void fm_refresh_files(void);
static void fm_draw_all(void);
static void fm_draw_simple_all(void);
static void fm_draw_classic_all(void);
static void fm_draw_address_bar(void);
static void fm_draw_file_list(void);
static void fm_draw_status_bar(void);
static void fm_draw_help(void);
static void fm_draw_create_file(void);
static void fm_draw_rename_file(void);
static void fm_draw_confirm_delete(void);
static void fm_draw_view_file(void);
static int fm_classic_get_layout(int* x, int* y, int* width, int* height);
static int fm_classic_get_content_height(int height);
static int fm_side_items_for_height(int height);
static int fm_visible_side_items(void);
static int fm_visible_rows(void);
static void fm_select_mode(void);
static void fm_redraw_file_view(void);
static void fm_classic_draw_input_dialog(void);
static void fm_classic_draw_help(void);
static void fm_classic_draw_view_file(void);
static void fm_hosted_draw(int x, int y, int width, int height);
static int fm_hosted_mouse(mouse_event_t* event, int x, int y,
                           int width, int height);
static void fm_hosted_close(void);
static void fm_select_virtual_root(void);
static int fm_select_boot_source(const char* path);
static int fm_select_mounted_source(const char* id);
static void fm_validate_storage_source(void);
static void fm_record_history(void);
static void fm_search_begin(void);
static void fm_search_close(void);
static void fm_search_refresh(void);
static int fm_search_activate_selected(void);

static const wm_hosted_app_t fm_hosted_app = {
    WM_APP_EXPLORER, "ZephyrOS Explorer", "Explorer",
    FM_CLASSIC_MIN_WIDTH + WM_HOSTED_FRAME_MAX_WIDTH,
    FM_CLASSIC_MIN_HEIGHT + WM_HOSTED_FRAME_MAX_HEIGHT,
    FM_CLASSIC_DEFAULT_WIDTH + WM_HOSTED_FRAME_MAX_WIDTH,
    FM_CLASSIC_DEFAULT_HEIGHT + WM_HOSTED_FRAME_MAX_HEIGHT,
    WM_KEY_REDRAW_WINDOW_MANAGER,
    fm_hosted_draw, fm_handle_key, fm_hosted_mouse, fm_hosted_close
};

static void int_to_str(uint32_t num, char* buf) {
    int i = 0;
    if (num == 0) { buf[i++] = '0'; }
    else {
        char tmp[16];
        int j = 0;
        while (num > 0) { tmp[j++] = '0' + (num % 10); num /= 10; }
        while (j > 0) { buf[i++] = tmp[--j]; }
    }
    buf[i] = '\0';
}

static int str_len(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void str_copy(char* dst, const char* src) {
    int i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int fm_join_path(char* dst, const char* base, const char* name) {
    if (!dst || !base || !name) return ERR_NULL;

    int base_len = str_len(base);
    int name_len = str_len(name);
    int separator = (base_len > 0) ? 1 : 0;
    if (base_len + separator + name_len >= FM_MAX_PATH) {
        LOG_ERROR("FM", "Caminho excede o limite do File Manager");
        return ERR_OVERFLOW;
    }

    int pos = 0;
    for (int i = 0; i < base_len; i++) dst[pos++] = base[i];
    if (separator) dst[pos++] = '/';
    for (int i = 0; i < name_len; i++) dst[pos++] = name[i];
    dst[pos] = '\0';
    return OK;
}

static int str_equal(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static char fm_upper_ascii(char value) {
    if (value >= 'a' && value <= 'z') return (char)(value - ('a' - 'A'));
    return value;
}

static void fm_fat_display_name(const char* name, char* output) {
    uint32_t input = 0;
    uint32_t length = 0;
    uint32_t base = 0;
    uint32_t extension = 0;

    while (name[input] && name[input] != '.' &&
           base < FM_FAT_BASE_LENGTH) {
        output[length++] = fm_upper_ascii(name[input++]);
        base++;
    }
    if (name[input] == '.') {
        input++;
        output[length++] = '.';
        while (name[input] && extension < FM_FAT_EXTENSION_LENGTH) {
            output[length++] = fm_upper_ascii(name[input++]);
            extension++;
        }
    }
    output[length] = '\0';
}

static int fm_rename_target_conflicts(const char* target) {
    char current[FM_NAME_LEN];

    for (int index = 0; index < state.file_count; index++) {
        if (index == state.selected) continue;
        fm_fat_display_name(state.files[index].name, current);
        if (str_equal(current, target)) return 1;
    }
    return 0;
}

static int fm_rename_selected_file(void) {
    char source_name[FM_NAME_LEN];
    char target_name[FM_NAME_LEN];
    int result;

    if (state.selected < 0 || state.selected >= state.file_count ||
        state.files[state.selected].is_dir || input_pos <= 0) {
        LOG_ERROR("FM", "Selecao invalida para renomear arquivo");
        return ERR_INVALID;
    }

    fm_fat_display_name(old_name, source_name);
    fm_fat_display_name(input_buffer, target_name);
    if (!target_name[0]) {
        LOG_ERROR("FM", "Nome de destino vazio na renomeacao");
        return ERR_INVALID;
    }
    if (str_equal(source_name, target_name)) return OK;
    if (fm_rename_target_conflicts(target_name)) {
        LOG_ERROR("FM", "Ja existe um item com o nome de destino");
        return ERR_STATE;
    }
    result = fs_rename_file_in_dir(
        state.current_path, source_name, target_name);
    if (result != OK) LOG_ERROR("FM", "Renomeacao do arquivo nao foi concluida");
    return result;
}

static int fm_boot_directory_exists(const char* name) {
    int count = fs_get_file_count_at("");
    char entry_name[FM_NAME_LEN];
    char canonical_name[FM_NAME_LEN];

    fm_fat_display_name(name, canonical_name);

    for (int index = 0; index < count; index++) {
        uint8_t attributes = 0;

        if (fs_get_file_info_at("", index, entry_name, 0, &attributes) == OK &&
            (attributes & FM_DIRECTORY_ATTRIBUTE) &&
            str_equal(entry_name, canonical_name)) {
            return 1;
        }
    }
    return 0;
}

static void fm_ensure_boot_directory(const char* name) {
    if (fm_boot_directory_exists(name)) return;
    if (fs_create_dir_entry("", name, FM_DIRECTORY_ATTRIBUTE) != OK) {
        LOG_ERROR("FM", "Falha ao criar diretorio padrao do Explorer");
    }
}

static void fm_store_history_source(int index) {
    if (index < 0 || index >= FM_MAX_HISTORY) return;
    str_copy(state.history_volume[index], state.storage_volume_id);
    state.history_virtual[index] = state.storage_virtual;
    state.history_boot[index] = state.storage_boot;
}

static void fm_restore_history_source(int index) {
    if (index < 0 || index >= FM_MAX_HISTORY) return;
    str_copy(state.storage_volume_id, state.history_volume[index]);
    state.storage_virtual = state.history_virtual[index];
    state.storage_boot = state.history_boot[index];
    state.storage_read_only = state.storage_virtual || !state.storage_boot;
    if (!state.storage_virtual && state.storage_volume_id[0]) {
        storage_volume_t volume;

        if (storage_find_volume(state.storage_volume_id, &volume) == OK &&
            volume.mounted) {
            state.storage_generation = volume.generation;
            state.storage_read_only = volume.read_only;
            return;
        }
        state.storage_virtual = 1;
        state.storage_boot = 0;
        state.storage_volume_id[0] = '\0';
        state.current_path[0] = '\0';
    }
}

static void fm_select_virtual_root(void) {
    state.storage_virtual = 1;
    state.storage_boot = 0;
    state.storage_read_only = 1;
    state.storage_generation = 0;
    state.storage_volume_id[0] = '\0';
    state.current_path[0] = '\0';
    state.selected = 0;
    state.scroll_offset = 0;
}

static int fm_select_boot_source(const char* path) {
    storage_status_t status;

    state.storage_virtual = 0;
    state.storage_boot = 1;
    state.storage_read_only = 0;
    state.storage_generation = 0;
    state.storage_volume_id[0] = '\0';
    if (storage_get_status(&status) == OK) {
        for (uint8_t index = 0; index < status.mounted_count; index++) {
            storage_volume_t volume;

            if (storage_get_mounted_at(index, &volume) == OK && volume.boot) {
                str_copy(state.storage_volume_id, volume.id);
                state.storage_generation = volume.generation;
                break;
            }
        }
    }
    str_copy(state.current_path, path ? path : "");
    state.selected = 0;
    state.scroll_offset = 0;
    return OK;
}

static int fm_select_mounted_source(const char* id) {
    storage_volume_t volume;
    int result = ERR_NOT_FOUND;

    if (!id) {
        LOG_ERROR("FM", "ID nulo ao selecionar volume montado");
        return ERR_NULL;
    }
    if (str_equal(id, "C:")) {
        storage_status_t status;

        if (storage_get_status(&status) == OK) {
            for (uint8_t index = 0; index < status.mounted_count; index++) {
                if (storage_get_mounted_at(index, &volume) == OK &&
                    volume.boot) {
                    result = OK;
                    break;
                }
            }
        }
    } else {
        result = storage_find_volume(id, &volume);
        if (result == OK && !volume.mounted) result = ERR_STATE;
    }

    if (result != OK) {
        LOG_ERROR("FM", "Volume montado nao encontrado no Explorer");
        return result;
    }
    state.storage_virtual = 0;
    state.storage_boot = volume.boot;
    state.storage_read_only = volume.read_only;
    state.storage_generation = volume.generation;
    str_copy(state.storage_volume_id, volume.id);
    state.current_path[0] = '\0';
    state.selected = 0;
    state.scroll_offset = 0;
    return OK;
}

static void fm_search_apply_index_status(const file_index_status_t* status) {
    if (!status) return;
    fm_search_index_status = *status;
    fm_search_status.partial = status->partial;
    fm_search_status.stale = status->stale;
    fm_search_status.building =
        status->state == FILE_INDEX_STATE_BUILDING;
    fm_search_status.cancelled =
        status->state == FILE_INDEX_STATE_CANCELLED;
    fm_search_status.last_error = status->last_error;
    fm_search_event_generation = status->event_generation;
    fm_search_index_state = status->state;
}

static void fm_search_refresh(void) {
    file_index_status_t status;
    int result;

    kmemset(&fm_search_status, 0, sizeof(fm_search_status));
    kmemset(&fm_search_index_status, 0, sizeof(fm_search_index_status));
    fm_search_last_clicked = -1;
    if (file_index_get_status(&status) == OK) {
        fm_search_apply_index_status(&status);
    }
    if (!fm_search_query[0]) {
        fm_search_selected = 0;
        fm_search_scroll = 0;
        return;
    }
    result = file_index_search(fm_search_query, fm_search_results,
                               FILE_INDEX_MAX_RESULTS, &fm_search_status);
    if (result != OK) {
        int search_error = result;

        kmemset(&fm_search_status, 0, sizeof(fm_search_status));
        fm_search_status.last_error = search_error;
        if (file_index_get_status(&status) == OK) {
            fm_search_apply_index_status(&status);
            if (fm_search_status.last_error == OK) {
                fm_search_status.last_error = search_error;
            }
        }
    }
    if (fm_search_selected >= (int)fm_search_status.returned_matches) {
        fm_search_selected = (int)fm_search_status.returned_matches - 1;
    }
    if (fm_search_selected < 0) fm_search_selected = 0;
    if (fm_search_scroll > fm_search_selected) {
        fm_search_scroll = fm_search_selected;
    }
}

static void fm_search_begin(void) {
    state.address_mode = 0;
    state.address_pos = 0;
    fm_search_active = 1;
    fm_search_input = 1;
    fm_search_pos = 0;
    fm_search_query[0] = '\0';
    fm_search_selected = 0;
    fm_search_scroll = 0;
    fm_search_last_clicked = -1;
    state.focus_pane = 1;
    fm_search_refresh();
}

static void fm_search_close(void) {
    fm_search_active = 0;
    fm_search_input = 0;
    fm_search_last_clicked = -1;
}

static int fm_search_scroll_selection(int delta) {
    int count = (int)fm_search_status.returned_matches;
    int previous = fm_search_selected;
    int rows = fm_visible_rows();

    if (count <= 0 || !delta) return 0;
    while (delta > 0 && fm_search_selected > 0) {
        fm_search_selected--;
        delta--;
    }
    while (delta < 0 && fm_search_selected < count - 1) {
        fm_search_selected++;
        delta++;
    }
    if (fm_search_selected < fm_search_scroll) {
        fm_search_scroll = fm_search_selected;
    } else if (fm_search_selected >= fm_search_scroll + rows) {
        fm_search_scroll = fm_search_selected - rows + 1;
    }
    if (previous == fm_search_selected) return 0;
    fm_search_last_clicked = -1;
    return 1;
}

static int fm_search_activate_selected(void) {
    file_index_result_t* result;
    storage_volume_t volume;
    char target[FM_MAX_PATH];

    if (fm_search_selected < 0 ||
        fm_search_selected >= (int)fm_search_status.returned_matches) {
        LOG_ERROR("FM", "Resultado inexistente na ativacao da pesquisa");
        return ERR_NOT_FOUND;
    }
    result = &fm_search_results[fm_search_selected];
    if (storage_find_volume(result->entry.volume_id, &volume) != OK ||
        !volume.mounted ||
        volume.generation != result->entry.volume_generation) {
        LOG_WARN("FM", "Resultado do indice aponta para volume ausente");
        fm_search_refresh();
        return ERR_STATE;
    }
    if (result->entry.is_directory) {
        if (fm_join_path(target, result->entry.parent_path,
                         result->entry.name) != OK) {
            LOG_ERROR("FM", "Caminho indexado excede o limite do Explorer");
            return ERR_OVERFLOW;
        }
    } else {
        str_copy(target, result->entry.parent_path);
    }
    if (fm_select_mounted_source(result->entry.volume_id) != OK) {
        LOG_ERROR("FM", "Fonte do resultado nao pode ser selecionada");
        return ERR_NOT_FOUND;
    }
    str_copy(state.current_path, target);
    fm_search_close();
    fm_record_history();
    fm_refresh_files();
    if (!result->entry.is_directory) {
        for (int index = 0; index < state.file_count; index++) {
            if (str_equal(state.files[index].name, result->entry.name)) {
                state.selected = index;
                if (state.selected >= fm_visible_rows()) {
                    state.scroll_offset = state.selected - fm_visible_rows() + 1;
                }
                break;
            }
        }
    }
    return OK;
}

static void fm_validate_storage_source(void) {
    storage_volume_t volume;

    if (state.mode != FM_MODE_CLASSIC || state.storage_virtual ||
        state.storage_boot) return;
    if (storage_find_volume(state.storage_volume_id, &volume) == OK &&
        volume.mounted && volume.generation == state.storage_generation) {
        return;
    }
    LOG_WARN("FM", "Montagem mudou; retornando a Este Computador");
    input_mode = 0;
    rename_mode = 0;
    confirm_delete = 0;
    create_dir_mode = 0;
    create_file_mode = 0;
    fm_select_virtual_root();
    state.history_count = 0;
    state.history_pos = 0;
    fm_record_history();
    fm_refresh_files();
}

static void fm_record_history(void) {
    if (state.history_count > 0 && state.history_pos >= 0 &&
        str_equal(state.history[state.history_pos], state.current_path) &&
        str_equal(state.history_volume[state.history_pos],
                  state.storage_volume_id) &&
        state.history_virtual[state.history_pos] == state.storage_virtual &&
        state.history_boot[state.history_pos] == state.storage_boot) return;
    if (state.history_pos + 1 < state.history_count) {
        state.history_count = state.history_pos + 1;
    }
    if (state.history_count >= FM_MAX_HISTORY) {
        for (int i = 0; i < FM_MAX_HISTORY - 1; i++) {
            str_copy(state.history[i], state.history[i + 1]);
            str_copy(state.history_volume[i], state.history_volume[i + 1]);
            state.history_virtual[i] = state.history_virtual[i + 1];
            state.history_boot[i] = state.history_boot[i + 1];
        }
        state.history_count = FM_MAX_HISTORY - 1;
    }
    str_copy(state.history[state.history_count], state.current_path);
    fm_store_history_source(state.history_count);
    state.history_count++;
    state.history_pos = state.history_count - 1;
}

static void fm_navigate_to(const char* path) {
    str_copy(state.current_path, path);
    state.selected = 0;
    state.scroll_offset = 0;
    fm_record_history();
    fm_refresh_files();
    fm_draw_all();
}

static void fm_copy_normalized_path(char* destination, const char* source) {
    uint32_t offset = 0;

    while (source && *source && offset + 1U < FM_MAX_PATH) {
        destination[offset++] = *source == '\\' ? '/' : *source;
        source++;
    }
    destination[offset] = '\0';
}

static int fm_navigate_address_value(const char* address) {
    int colon = -1;
    uint32_t length;

    if (!address) {
        LOG_ERROR("FM", "Endereco nulo no Explorer");
        return ERR_NULL;
    }
    if (state.mode != FM_MODE_CLASSIC) {
        fm_navigate_to(address);
        return OK;
    }
    length = (uint32_t)str_len(address);
    for (uint32_t index = 0; index < length; index++) {
        if (address[index] == ':') {
            colon = (int)index;
            break;
        }
    }
    if (colon < 0) {
        fm_navigate_to(address);
        return OK;
    }
    if (colon == 1 && (address[0] == 'C' || address[0] == 'c')) {
        fm_select_boot_source("");
    } else {
        char id[FM_VOLUME_ID_LEN];
        storage_volume_t volume;

        if (colon <= 0 || colon >= FM_VOLUME_ID_LEN) {
            LOG_ERROR("FM", "Prefixo de volume invalido no endereco");
            return ERR_INVALID;
        }
        for (int index = 0; index < colon; index++) id[index] = address[index];
        id[colon] = '\0';
        if (storage_find_volume(id, &volume) != OK || !volume.mounted) {
            LOG_ERROR("FM", "Volume do endereco nao esta montado");
            return ERR_NOT_FOUND;
        }
        state.storage_virtual = 0;
        state.storage_boot = volume.boot;
        state.storage_read_only = volume.read_only;
        state.storage_generation = volume.generation;
        str_copy(state.storage_volume_id, volume.id);
    }
    fm_copy_normalized_path(state.current_path, address + colon + 1);
    state.selected = 0;
    state.scroll_offset = 0;
    fm_record_history();
    fm_refresh_files();
    fm_draw_all();
    return OK;
}

static void fm_go_up(void) {
    if (state.storage_virtual) return;
    if (state.current_path[0] == '\0') {
        if (state.mode == FM_MODE_CLASSIC) {
            fm_select_virtual_root();
            fm_record_history();
            fm_refresh_files();
            fm_draw_all();
        }
        return;
    }

    int len = str_len(state.current_path);
    if (len > 0 && state.current_path[len - 1] == '/') {
        state.current_path[len - 1] = '\0';
        len--;
    }

    int last_slash = -1;
    for (int i = 0; i < len; i++) {
        if (state.current_path[i] == '/') last_slash = i;
    }

    if (last_slash < 0) {
        state.current_path[0] = '\0';
    } else if (last_slash == 0) {
        state.current_path[0] = '/';
        state.current_path[1] = '\0';
    } else {
        state.current_path[last_slash] = '\0';
    }

    state.selected = 0;
    state.scroll_offset = 0;
    fm_record_history();
    fm_refresh_files();
    fm_draw_all();
}

static void fm_go_back(void) {
    if (state.history_pos > 0) {
        state.history_pos--;
        str_copy(state.current_path, state.history[state.history_pos]);
        fm_restore_history_source(state.history_pos);
        state.selected = 0;
        state.scroll_offset = 0;
        fm_refresh_files();
        fm_draw_all();
    }
}

static void fm_go_forward(void) {
    if (state.history_pos < state.history_count - 1) {
        state.history_pos++;
        str_copy(state.current_path, state.history[state.history_pos]);
        fm_restore_history_source(state.history_pos);
        state.selected = 0;
        state.scroll_offset = 0;
        fm_refresh_files();
        fm_draw_all();
    }
}

static int fm_display_font_width(void) {
    display_metrics_t metrics;

    if (display_get_metrics(&metrics) != OK) return FONT_WIDTH;
    return metrics.font_width;
}

static int fm_display_font_height(void) {
    display_metrics_t metrics;

    if (display_get_metrics(&metrics) != OK) return FONT_HEIGHT;
    return metrics.font_height;
}

static int fm_display_icon_scale(void) {
    display_metrics_t metrics;

    if (display_get_metrics(&metrics) != OK) return 2;
    return metrics.scale == DISPLAY_SCALE_LARGE ? 3 : 2;
}

static int fm_classic_get_layout(int* x, int* y, int* width, int* height) {
    vesa_mode_t* mode = vesa_get_mode();
    tb_rect_t work_area;

    if (!x || !y || !width || !height) return 0;
    if (fm_hosted) {
        *x = fm_hosted_x;
        *y = fm_hosted_y;
        *width = fm_hosted_width;
        *height = fm_hosted_height;
        return *width > 0 && *height > 0;
    }
    if (!mode || !mode->initialized || !vesa_has_backbuffer()) return 0;

    work_area.x = 0;
    work_area.y = 0;
    work_area.width = mode->width;
    work_area.height = mode->height;
    taskbar_get_work_area(&work_area);
    *x = work_area.x + FM_CLASSIC_MARGIN;
    *y = work_area.y + FM_CLASSIC_MARGIN;
    *width = work_area.width - (FM_CLASSIC_MARGIN * 2);
    *height = work_area.height - (FM_CLASSIC_MARGIN * 2);

    return *width >= FM_CLASSIC_MIN_WIDTH &&
           *height >= FM_CLASSIC_MIN_HEIGHT;
}

static void fm_select_mode(void) {
    fm_mode_t next_mode = FM_MODE_SIMPLE;
    int wants_classic = desktop_get_mode() == DESKTOP_MODE_CLASSIC;
    int x;
    int y;
    int width;
    int height;

    if (wants_classic &&
        fm_classic_get_layout(&x, &y, &width, &height)) {
        next_mode = FM_MODE_CLASSIC;
    }

    if (wants_classic && next_mode == FM_MODE_SIMPLE &&
        !fm_fallback_logged) {
        LOG_WARN("FM", "VESA/backbuffer indisponivel; Explorer Simple ativo");
        fm_fallback_logged = 1;
    }
    if (next_mode == FM_MODE_CLASSIC) fm_fallback_logged = 0;

    if (state.mode == next_mode) return;

    state.mode = next_mode;
    if (state.mode == FM_MODE_CLASSIC) {
        fm_select_virtual_root();
        state.history_count = 0;
        state.history_pos = 0;
        fm_record_history();
        fm_refresh_files();
        LOG_INFO("FM", "Explorer usando modo Classic");
    } else {
        fm_search_close();
        fm_select_boot_source("");
        state.history_count = 0;
        state.history_pos = 0;
        fm_record_history();
        fm_refresh_files();
        LOG_WARN("FM", "Explorer usando modo Simple como fallback");
    }
}

static int fm_classic_get_content_height(int height) {
    return height - FM_CLASSIC_CONTENT_FIXED_HEIGHT;
}

static int fm_side_items_for_height(int height) {
    int visible = 0;

    for (int i = 0; i < side_pane_count; i++) {
        int row_bottom = FM_CLASSIC_SIDE_HEADER_OFFSET +
                         i * (FM_CLASSIC_ROW_HEIGHT + FM_CLASSIC_ROW_GAP) +
                         FM_CLASSIC_ROW_HEIGHT;

        if (row_bottom > height) break;
        visible++;
    }
    return visible;
}

static int fm_visible_side_items(void) {
    int x;
    int y;
    int width;
    int height;

    if (state.mode != FM_MODE_CLASSIC ||
        !fm_classic_get_layout(&x, &y, &width, &height)) {
        return side_pane_count;
    }
    return fm_side_items_for_height(fm_classic_get_content_height(height));
}

static int fm_visible_rows(void) {
    int x;
    int y;
    int width;
    int height;
    int content_height;
    int rows;

    if (state.mode != FM_MODE_CLASSIC ||
        !fm_classic_get_layout(&x, &y, &width, &height)) {
        return FM_CLASSIC_VISIBLE_ROWS;
    }

    content_height = fm_classic_get_content_height(height);
    rows = (content_height - FM_CLASSIC_SIDE_HEADER_OFFSET) /
           (FM_CLASSIC_ROW_HEIGHT + FM_CLASSIC_ROW_GAP);
    return rows > 0 ? rows : 1;
}

static void fm_copy_display_text(char* dst, int capacity, const char* text,
                                 int max_chars) {
    int i = 0;

    if (!dst || capacity <= 0) return;
    if (!text) text = "";
    if (max_chars >= capacity) max_chars = capacity - 1;

    while (text[i] && i < max_chars) {
        dst[i] = text[i];
        i++;
    }
    dst[i] = '\0';
}

static void fm_draw_text_limited(int x, int y, int width, const char* text,
                                 uint32_t color) {
    char clipped[FM_CLASSIC_MAX_TEXT];
    int max_chars = (width - (int)display_scale_px(8)) /
                    fm_display_font_width();

    if (max_chars < 1) return;
    if (max_chars >= FM_CLASSIC_MAX_TEXT) max_chars = FM_CLASSIC_MAX_TEXT - 1;
    fm_copy_display_text(clipped, FM_CLASSIC_MAX_TEXT, text, max_chars);
    gui_draw_text((uint32_t)x, (uint32_t)y, clipped, color);
}

static void fm_build_display_path(char* path, int capacity) {
    int pos = 0;
    int start = state.current_path[0] == '/' ? 1 : 0;

    if (!path || capacity < 4) return;
    if (state.storage_virtual) {
        fm_copy_display_text(path, capacity, "Este Computador", capacity - 1);
        return;
    }
    if (state.storage_boot) {
        path[pos++] = 'C';
    } else {
        for (int i = 0; state.storage_volume_id[i] && pos < capacity - 3; i++) {
            path[pos++] = state.storage_volume_id[i];
        }
    }
    path[pos++] = ':';
    path[pos++] = '\\';

    for (int i = start; state.current_path[i] && pos < capacity - 1; i++) {
        path[pos++] = state.current_path[i] == '/' ? '\\' :
                      state.current_path[i];
    }
    path[pos] = '\0';
}

static void fm_draw_title_bar(void) {
    video_fill_rect(0, 0, SCREEN_COLS, 1, ' ', FM_TITLE_BAR_COLOR);
    video_print_at((SCREEN_COLS - 20) / 2, 0, " ZephyrOS Explorer ", FM_TITLE_BAR_COLOR);
}

static void fm_draw_menu_bar(void) {
    video_fill_rect(0, 1, SCREEN_COLS, 1, ' ', 0x70);
    video_print_at(1, 1, "F1=Ajuda F3=Ver F5=Atualizar F7=Novo F8=Excluir Esc=Sair", 0x70);
}

static void fm_draw_address_bar(void) {
    if (state.mode == FM_MODE_CLASSIC) {
        fm_draw_all();
        return;
    }

    video_fill_rect(0, 2, SCREEN_COLS, 1, ' ', FM_ADDRESS_COLOR);
    video_print_at(1, 2, ">", FM_ADDRESS_COLOR);

    if (state.address_mode) {
        video_fill_rect(3, 2, SCREEN_COLS - 4, 1, ' ', FM_ADDRESS_INPUT_COLOR);
        video_print_at(3, 2, state.address_buffer, FM_ADDRESS_INPUT_COLOR);
    } else {
        char display_path[78];
        display_path[0] = 'C';
        display_path[1] = ':';

        if (state.current_path[0] == '\0') {
            display_path[2] = '\\';
            display_path[3] = '\0';
        } else {
            display_path[2] = '\\';
            int di = 3;
            int start_idx = (state.current_path[0] == '/') ? 1 : 0;
            for (int i = start_idx; state.current_path[i] && di < SCREEN_COLS - 4; i++) {
                if (state.current_path[i] == '/') display_path[di++] = '\\';
                else display_path[di++] = state.current_path[i];
            }
            display_path[di] = '\0';
        }

        int plen = str_len(display_path);
        video_fill_rect(3, 2, SCREEN_COLS - 4, 1, ' ', FM_ADDRESS_COLOR);
        video_print_at(3, 2, display_path, FM_ADDRESS_COLOR);

        if (state.current_path[0] != '\0') {
            video_print_at(3 + plen, 2, "\\", FM_ADDRESS_COLOR);
        }
    }
}


static void fm_draw_side_pane(void) {
    video_fill_rect(0, 3, 20, SCREEN_ROWS - 5, ' ', 0x07);
    video_draw_vline(20, 3, SCREEN_ROWS - 5, 0xB3, 0x07);
    
    video_print_at(2, 4, "Acesso Rapido", 0x0F);
    
    for (int i = 0; i < side_pane_count; i++) {
        uint8_t color = 0x07;
        if (state.focus_pane == 0 && state.side_selected == i) {
            color = FM_SELECTED_COLOR;
            video_fill_rect(0, 6 + i, 20, 1, ' ', color);
        }
        
        char buf[20];
        int d = 0;
        for (int j = 0; side_pane_names[i][j] && d < 18; j++) buf[d++] = side_pane_names[i][j];
        buf[d] = '\0';
        
        video_print_at(1, 6 + i, buf, color);
    }
}

static void fm_draw_column_headers(void) {
    video_fill_rect(21, 3, SCREEN_COLS - 21, 1, ' ', 0x07);
    video_print_at(23, 3, "Nome", 0x0F);
    video_print_at(43, 3, "Tamanho", 0x0F);
    video_print_at(56, 3, "Tipo", 0x0F);
    video_draw_hline(21, 4, SCREEN_COLS - 21, 0xC4, 0x07);
}

static void fm_draw_separator_bottom(void) {
    video_draw_hline(0, SCREEN_ROWS - 3, SCREEN_COLS, 0xC4, 0x07);
}

static void fm_draw_status_bar(void) {
    if (state.mode == FM_MODE_CLASSIC) {
        fm_draw_all();
        return;
    }

    video_fill_rect(0, SCREEN_ROWS - 2, SCREEN_COLS, 1, ' ', FM_STATUS_COLOR);

    if (state.file_count == 0) {
        video_print_at(2, SCREEN_ROWS - 2, "Nenhum arquivo encontrado", FM_STATUS_COLOR);
        return;
    }

    video_print_at(2, SCREEN_ROWS - 2, "Arquivo: ", FM_STATUS_COLOR);

    if (state.selected >= 0 && state.selected < state.file_count) {
        fm_file_entry_t* f = &state.files[state.selected];
        video_print_at(11, SCREEN_ROWS - 2, f->name, FM_STATUS_COLOR);

        if (f->is_dir) {
            video_print_at(24, SCREEN_ROWS - 2, "| Pasta", FM_STATUS_COLOR);
        } else {
            video_print_at(24, SCREEN_ROWS - 2, "| Arquivo | ", FM_STATUS_COLOR);
            char buf[16];
            int_to_str(f->size, buf);
            video_print_at(36, SCREEN_ROWS - 2, buf, FM_STATUS_COLOR);
            int blen = str_len(buf);
            video_print_at(36 + blen, SCREEN_ROWS - 2, " bytes", FM_STATUS_COLOR);
        }
    }

    video_print_at(60, SCREEN_ROWS - 2, "Sel: ", FM_STATUS_COLOR);
    char buf[8];
    int_to_str(state.selected + 1, buf);
    video_print_at(65, SCREEN_ROWS - 2, buf, FM_STATUS_COLOR);
    int blen = str_len(buf);
    video_print_at(65 + blen, SCREEN_ROWS - 2, "/", FM_STATUS_COLOR);
    int_to_str(state.file_count, buf);
    video_print_at(66 + blen, SCREEN_ROWS - 2, buf, FM_STATUS_COLOR);
}

static void fm_refresh_files(void) {
    if (state.mode == FM_MODE_CLASSIC && state.storage_virtual) {
        storage_status_t status;

        state.file_count = 0;
        if (storage_get_status(&status) == OK) {
            for (uint8_t index = 0;
                 index < status.mounted_count && state.file_count < FM_MAX_FILES;
                 index++) {
                storage_volume_t volume;
                fm_file_entry_t* entry;

                if (storage_get_mounted_at(index, &volume) != OK) continue;
                entry = &state.files[state.file_count++];
                str_copy(entry->name, volume.boot ? "C:" : volume.id);
                entry->size = volume.sector_count;
                entry->attributes = 0x10;
                entry->is_dir = 1;
            }
        }
    } else if (state.mode == FM_MODE_CLASSIC && !state.storage_boot) {
        storage_volume_t volume;
        storage_dir_entry_t entries[STORAGE_MAX_DIR_ENTRIES];
        uint32_t count = 0;
        int result = storage_find_volume(state.storage_volume_id, &volume);

        if (result != OK || !volume.mounted ||
            volume.generation != state.storage_generation) {
            LOG_WARN("FM", "Montagem mudou; retornando a Este Computador");
            fm_select_virtual_root();
            state.history_count = 0;
            state.history_pos = 0;
            fm_record_history();
            fm_refresh_files();
            return;
        }
        result = storage_list_dir(state.storage_volume_id, state.current_path,
                                  entries, STORAGE_MAX_DIR_ENTRIES, &count);
        state.file_count = result == OK ? (int)count : 0;
        for (int i = 0; i < state.file_count; i++) {
            str_copy(state.files[i].name, entries[i].name);
            state.files[i].size = entries[i].size;
            state.files[i].attributes = entries[i].attributes;
            state.files[i].is_dir = entries[i].is_directory;
        }
    } else {
        state.file_count = fs_get_file_count_at(state.current_path);
        if (state.file_count > FM_MAX_FILES) state.file_count = FM_MAX_FILES;
        for (int i = 0; i < state.file_count; i++) {
            uint8_t attr;
            fs_get_file_info_at(state.current_path, i, state.files[i].name,
                                &state.files[i].size, &attr);
            state.files[i].attributes = attr;
            state.files[i].is_dir = (attr & 0x10) ? 1 : 0;
        }
    }

    if (state.selected >= state.file_count) {
        state.selected = state.file_count - 1;
    }
    if (state.selected < 0) {
        state.selected = 0;
    }
}

static void fm_draw_file_list(void) {
    if (state.mode == FM_MODE_CLASSIC) {
        fm_draw_all();
        return;
    }

    int visible_start = state.scroll_offset;
    int visible_count = 17;

    for (int i = 0; i < visible_count; i++) {
        int row = 5 + i;
        int file_idx = visible_start + i;

        video_fill_rect(21, row, SCREEN_COLS - 21, 1, ' ', 0x07);

        if (file_idx < state.file_count) {
            fm_file_entry_t* f = &state.files[file_idx];
            icon_entry_t* icon = icons_get_fm(f->is_dir ? ICON_FM_FOLDER : ICON_FM_FILE);
            uint8_t name_color = icon->color;

            if (file_idx == state.selected) {
                name_color = FM_SELECTED_COLOR;
                video_fill_rect(21, row, SCREEN_COLS - 21, 1, ' ', state.focus_pane == 1 ? FM_SELECTED_COLOR : 0x70);
            }

            video_put_char_at(icon->ch, name_color, 23, row);
            video_put_char_at(icon->ch, name_color, 24, row);

            char display_name[FM_NAME_LEN + 4];
            int d = 0;

            if (f->is_dir) {
                display_name[d++] = '[';
                for (int j = 0; f->name[j] && d < 10; j++) {
                    display_name[d++] = f->name[j];
                }
                display_name[d++] = ']';
                while (d < 12) display_name[d++] = ' ';
            } else {
                for (int j = 0; f->name[j] && d < 12; j++) {
                    display_name[d++] = f->name[j];
                }
                while (d < 12) display_name[d++] = ' ';
            }
            display_name[d] = '\0';

            video_print_at(26, row, display_name, name_color);

            if (!f->is_dir) {
                char buf[12];
                int_to_str(f->size, buf);
                video_print_at(43, row, buf, file_idx == state.selected && state.focus_pane == 1 ? FM_SELECTED_COLOR : (file_idx == state.selected ? 0x70 : FM_SIZE_COLOR));
                video_print_at(56, row, "Arquivo", file_idx == state.selected && state.focus_pane == 1 ? FM_SELECTED_COLOR : (file_idx == state.selected ? 0x70 : 0x08));
            } else {
                video_print_at(56, row, "Pasta", file_idx == state.selected && state.focus_pane == 1 ? FM_SELECTED_COLOR : (file_idx == state.selected ? 0x70 : 0x0B));
            }
        }
    }
}

static void fm_classic_draw_toolbar(int x, int y, int width) {
    gui_draw_rounded_rect((uint32_t)x, (uint32_t)y, (uint32_t)width,
                          FM_CLASSIC_TOOLBAR_HEIGHT,
                          display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                          GUI_MODERN_COLOR_WINDOW);
    gui_draw_flat_border((uint32_t)x, (uint32_t)y, (uint32_t)width,
                         FM_CLASSIC_TOOLBAR_HEIGHT,
                         GUI_MODERN_COLOR_BORDER_INACTIVE);
    fm_draw_text_limited(x + FM_CLASSIC_INSET,
                         y + (int)display_scale_px(6),
                         width - (int)display_scale_px(16),
                         (state.storage_virtual || state.storage_read_only) ?
                         "Ctrl+F Pesquisar  F1 Ajuda  F3 Ver  F5 Atualizar  Somente leitura  Alt+F4 Sair" :
                         "Ctrl+F Pesquisar  F1 Ajuda  F2 Renomear  F3 Ver  F5 Atualizar  F6 Pasta  F7 Arquivo  F8 Excluir",
                         GUI_MODERN_COLOR_TEXT);
}

static void fm_classic_draw_address(int x, int y, int width) {
    char display_path[FM_MAX_PATH];
    int search_x = x + width - FM_CLASSIC_SEARCH_BUTTON_WIDTH;
    uint32_t background = state.address_mode || fm_search_input ?
                          GUI_MODERN_COLOR_HOVER : GUI_MODERN_COLOR_WINDOW;
    uint32_t foreground = GUI_MODERN_COLOR_TEXT;

    gui_draw_rounded_rect((uint32_t)x, (uint32_t)y, (uint32_t)width,
                          FM_CLASSIC_ADDRESS_HEIGHT,
                          display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                          background);
    gui_draw_flat_border((uint32_t)x, (uint32_t)y, (uint32_t)width,
                         FM_CLASSIC_ADDRESS_HEIGHT,
                         (state.address_mode || fm_search_input) ?
                         GUI_MODERN_COLOR_ACCENT :
                         GUI_MODERN_COLOR_BORDER_INACTIVE);
    fm_build_display_path(display_path, FM_MAX_PATH);
    if (state.address_mode) {
        fm_copy_display_text(display_path, FM_MAX_PATH,
                             state.address_buffer, FM_MAX_PATH - 1);
    } else if (fm_search_active) {
        fm_copy_display_text(display_path, FM_MAX_PATH,
                             fm_search_query, FILE_INDEX_QUERY_SIZE - 1U);
    }

    gui_draw_text((uint32_t)(x + FM_CLASSIC_INSET),
                  (uint32_t)(y + (int)display_scale_px(6)),
                  fm_search_active ? "Pesquisar:" : "Endereco:", foreground);
    fm_draw_text_limited(x + (int)display_scale_px(92),
                         y + (int)display_scale_px(6),
                         search_x - x - (int)display_scale_px(100),
                         display_path, foreground);
    gui_draw_rounded_rect((uint32_t)search_x, (uint32_t)y,
                          (uint32_t)FM_CLASSIC_SEARCH_BUTTON_WIDTH,
                          FM_CLASSIC_ADDRESS_HEIGHT,
                          display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                          fm_search_active ? GUI_MODERN_COLOR_ACCENT :
                                             GUI_MODERN_COLOR_HOVER);
    gui_draw_flat_border((uint32_t)search_x, (uint32_t)y,
                         (uint32_t)FM_CLASSIC_SEARCH_BUTTON_WIDTH,
                         FM_CLASSIC_ADDRESS_HEIGHT,
                         GUI_MODERN_COLOR_BORDER_INACTIVE);
    gui_draw_text((uint32_t)(search_x + (int)display_scale_px(12)),
                  (uint32_t)(y + (int)display_scale_px(6)),
                  "Pesquisar", GUI_MODERN_COLOR_TEXT);
}

static void fm_classic_draw_side(int x, int y, int width, int height) {
    int visible_items = fm_side_items_for_height(height);

    gui_draw_rounded_rect((uint32_t)x, (uint32_t)y, (uint32_t)width,
                          (uint32_t)height,
                          display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                          GUI_MODERN_COLOR_WINDOW);
    gui_draw_flat_border((uint32_t)x, (uint32_t)y, (uint32_t)width,
                         (uint32_t)height, GUI_MODERN_COLOR_BORDER_INACTIVE);
    gui_draw_text((uint32_t)(x + FM_CLASSIC_INSET),
                  (uint32_t)(y + (int)display_scale_px(8)),
                  "Acesso Rapido", GUI_MODERN_COLOR_ACCENT);

    for (int i = 0; i < visible_items; i++) {
        int row_y = y + FM_CLASSIC_SIDE_HEADER_OFFSET +
                    i * (FM_CLASSIC_ROW_HEIGHT + FM_CLASSIC_ROW_GAP);
        int selected = state.focus_pane == 0 && state.side_selected == i;
        uint32_t foreground = GUI_MODERN_COLOR_TEXT;

        if (selected) {
            gui_draw_rounded_rect((uint32_t)(x + (int)display_scale_px(4)),
                                  (uint32_t)row_y,
                                  (uint32_t)(width - (int)display_scale_px(8)),
                                  FM_CLASSIC_ROW_HEIGHT,
                                  display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                                  GUI_MODERN_COLOR_HOVER);
            gui_draw_flat_border((uint32_t)(x + (int)display_scale_px(4)),
                                 (uint32_t)row_y,
                                 (uint32_t)(width - (int)display_scale_px(8)),
                                 FM_CLASSIC_ROW_HEIGHT,
                                 GUI_MODERN_COLOR_ACCENT);
        }
        fm_draw_text_limited(x + (int)display_scale_px(14),
                             row_y + (int)display_scale_px(6),
                             width - (int)display_scale_px(24),
                             side_pane_names[i], foreground);
    }
}

static void fm_search_build_location(const file_index_entry_t* entry,
                                     char* output) {
    int offset = 0;

    while (entry->volume_id[offset] && offset + 1 < FM_SEARCH_PATH_SIZE) {
        output[offset] = entry->volume_id[offset];
        offset++;
    }
    if (offset + 2 < FM_SEARCH_PATH_SIZE) {
        output[offset++] = ':';
        output[offset++] = '/';
    }
    for (int index = 0; entry->parent_path[index] &&
         offset + 1 < FM_SEARCH_PATH_SIZE; index++) {
        output[offset++] = entry->parent_path[index];
    }
    output[offset] = '\0';
}

static void fm_classic_draw_search_list(int x, int y, int width, int height) {
    int rows = fm_visible_rows();
    int header_y = y + (int)display_scale_px(4);
    int list_y = header_y + FM_CLASSIC_HEADER_HEIGHT +
                 (int)display_scale_px(4);
    int name_x = x + (int)display_scale_px(20);
    int path_x = x + width / 3;
    int type_x = x + (width * 4) / 5;

    gui_draw_rounded_rect((uint32_t)x, (uint32_t)y, (uint32_t)width,
                          (uint32_t)height,
                          display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                          GUI_MODERN_COLOR_WINDOW);
    gui_draw_flat_border((uint32_t)x, (uint32_t)y, (uint32_t)width,
                         (uint32_t)height, GUI_MODERN_COLOR_BORDER_INACTIVE);
    gui_draw_rounded_rect((uint32_t)(x + (int)display_scale_px(4)),
                          (uint32_t)header_y,
                          (uint32_t)(width - (int)display_scale_px(8)),
                          FM_CLASSIC_HEADER_HEIGHT,
                          display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                          GUI_MODERN_COLOR_HOVER);
    gui_draw_text((uint32_t)name_x,
                  (uint32_t)(header_y + (int)display_scale_px(6)),
                  "Nome", GUI_MODERN_COLOR_TEXT);
    gui_draw_text((uint32_t)path_x,
                  (uint32_t)(header_y + (int)display_scale_px(6)),
                  "Volume e caminho", GUI_MODERN_COLOR_TEXT);
    gui_draw_text((uint32_t)type_x,
                  (uint32_t)(header_y + (int)display_scale_px(6)),
                  "Tipo", GUI_MODERN_COLOR_TEXT);

    for (int row = 0; row < rows; row++) {
        int result_index = fm_search_scroll + row;
        int row_y = list_y + row *
                    (FM_CLASSIC_ROW_HEIGHT + FM_CLASSIC_ROW_GAP);
        file_index_result_t* result;
        icon_entry_t* icon;
        char location[FM_SEARCH_PATH_SIZE];
        uint32_t foreground;

        if (result_index >= (int)fm_search_status.returned_matches) break;
        result = &fm_search_results[result_index];
        foreground = result->availability == FILE_INDEX_RESULT_AVAILABLE ?
                     GUI_MODERN_COLOR_TEXT : FM_CLASSIC_WARNING_COLOR;
        if (result_index == fm_search_selected) {
            gui_draw_rounded_rect((uint32_t)(x + (int)display_scale_px(4)),
                                  (uint32_t)row_y,
                                  (uint32_t)(width - (int)display_scale_px(8)),
                                  FM_CLASSIC_ROW_HEIGHT,
                                  display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                                  GUI_MODERN_COLOR_HOVER);
            gui_draw_flat_border((uint32_t)(x + (int)display_scale_px(4)),
                                 (uint32_t)row_y,
                                 (uint32_t)(width - (int)display_scale_px(8)),
                                 FM_CLASSIC_ROW_HEIGHT,
                                 GUI_MODERN_COLOR_ACCENT);
        }
        icon = icons_get_fm(result->entry.is_directory ?
                            ICON_FM_FOLDER : ICON_FM_FILE);
        if (icon) {
            vesa_color_t color;
            color.raw = foreground;
            vesa_draw_char(x + (int)display_scale_px(10),
                           row_y + (int)display_scale_px(4), icon->ch,
                           color, fm_display_icon_scale());
        }
        fm_draw_text_limited(name_x, row_y + (int)display_scale_px(6),
                             path_x - name_x - (int)display_scale_px(8),
                             result->entry.name, foreground);
        fm_search_build_location(&result->entry, location);
        fm_draw_text_limited(path_x, row_y + (int)display_scale_px(6),
                             type_x - path_x - (int)display_scale_px(8),
                             location, foreground);
        gui_draw_text((uint32_t)type_x,
                      (uint32_t)(row_y + (int)display_scale_px(6)),
                      result->entry.is_directory ? "Pasta" : "Arquivo",
                      foreground);
    }
}

static void fm_classic_draw_list(int x, int y, int width, int height) {
    int rows = fm_visible_rows();
    int start = state.scroll_offset;
    int header_y = y + (int)display_scale_px(4);
    int list_y = header_y + FM_CLASSIC_HEADER_HEIGHT +
                 (int)display_scale_px(4);
    int name_x = x + (int)display_scale_px(20);
    int size_x = x + width / 2;
    int type_x = x + (width * 3) / 4;

    if (fm_search_active) {
        fm_classic_draw_search_list(x, y, width, height);
        return;
    }

    gui_draw_rounded_rect((uint32_t)x, (uint32_t)y, (uint32_t)width,
                          (uint32_t)height,
                          display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                          GUI_MODERN_COLOR_WINDOW);
    gui_draw_flat_border((uint32_t)x, (uint32_t)y, (uint32_t)width,
                         (uint32_t)height, GUI_MODERN_COLOR_BORDER_INACTIVE);
    gui_draw_rounded_rect((uint32_t)(x + (int)display_scale_px(4)),
                          (uint32_t)header_y,
                          (uint32_t)(width - (int)display_scale_px(8)),
                          FM_CLASSIC_HEADER_HEIGHT,
                          display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                          GUI_MODERN_COLOR_HOVER);
    gui_draw_text((uint32_t)name_x,
                  (uint32_t)(header_y + (int)display_scale_px(6)),
                  "Nome", GUI_MODERN_COLOR_TEXT);
    gui_draw_text((uint32_t)size_x,
                  (uint32_t)(header_y + (int)display_scale_px(6)),
                  "Tamanho", GUI_MODERN_COLOR_TEXT);
    gui_draw_text((uint32_t)type_x,
                  (uint32_t)(header_y + (int)display_scale_px(6)),
                  "Tipo", GUI_MODERN_COLOR_TEXT);

    for (int row = 0; row < rows; row++) {
        int file_index = start + row;
        int row_y = list_y + row * (FM_CLASSIC_ROW_HEIGHT + FM_CLASSIC_ROW_GAP);
        fm_file_entry_t* file;
        icon_entry_t* icon;
        uint32_t foreground;
        char name[FM_NAME_LEN + 4];
        char size[16];

        if (file_index >= state.file_count) break;

        file = &state.files[file_index];
        icon = icons_get_fm(file->is_dir ? ICON_FM_FOLDER : ICON_FM_FILE);
        int selected = file_index == state.selected;
        int active = selected && state.focus_pane == 1;
        foreground = GUI_MODERN_COLOR_TEXT;

        if (active) {
            gui_draw_rounded_rect((uint32_t)(x + (int)display_scale_px(4)),
                                  (uint32_t)row_y,
                                  (uint32_t)(width - (int)display_scale_px(8)),
                                  FM_CLASSIC_ROW_HEIGHT,
                                  display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                                  GUI_MODERN_COLOR_HOVER);
            gui_draw_flat_border((uint32_t)(x + (int)display_scale_px(4)),
                                 (uint32_t)row_y,
                                 (uint32_t)(width - (int)display_scale_px(8)),
                                 FM_CLASSIC_ROW_HEIGHT,
                                 GUI_MODERN_COLOR_ACCENT);
        }
        if (icon) {
            vesa_color_t icon_color;
            icon_color.raw = foreground;
            vesa_draw_char(x + (int)display_scale_px(10),
                           row_y + (int)display_scale_px(4),
                           icon->ch, icon_color,
                           fm_display_icon_scale());
        }

        if (file->is_dir) {
            int name_length = str_len(file->name);
            if (name_length > FM_NAME_LEN) name_length = FM_NAME_LEN;
            name[0] = '[';
            for (int j = 0; j < name_length; j++) {
                name[j + 1] = file->name[j];
            }
            name[name_length + 1] = ']';
            name[name_length + 2] = '\0';
        } else {
            fm_copy_display_text(name, FM_NAME_LEN + 4,
                                 file->name, FM_NAME_LEN + 3);
        }
        fm_draw_text_limited(name_x, row_y + (int)display_scale_px(6),
                             size_x - name_x - (int)display_scale_px(8),
                             name, foreground);

        if (file->is_dir) {
            gui_draw_text((uint32_t)size_x,
                          (uint32_t)(row_y + (int)display_scale_px(6)),
                          "-", foreground);
            gui_draw_text((uint32_t)type_x,
                          (uint32_t)(row_y + (int)display_scale_px(6)),
                          state.storage_virtual ? "Volume" : "Pasta",
                          foreground);
        } else {
            int_to_str(file->size, size);
            fm_draw_text_limited(
                size_x, row_y + (int)display_scale_px(6),
                type_x - size_x - (int)display_scale_px(8),
                size, foreground);
            gui_draw_text((uint32_t)type_x,
                          (uint32_t)(row_y + (int)display_scale_px(6)),
                          "Arquivo", foreground);
        }
    }
}

static void fm_classic_draw_search_status(int x, int y, int width) {
    const char* warning = 0;
    char selection[24];
    char progress[16];

    if (fm_search_input) {
        gui_draw_text((uint32_t)(x + FM_CLASSIC_INSET),
                      (uint32_t)(y + (int)display_scale_px(6)),
                      "Digite o termo e pressione Enter",
                      GUI_MODERN_COLOR_TEXT);
    } else if (!fm_search_status.returned_matches) {
        gui_draw_text((uint32_t)(x + FM_CLASSIC_INSET),
                      (uint32_t)(y + (int)display_scale_px(6)),
                      "Nenhum resultado global",
                      GUI_MODERN_COLOR_BORDER_INACTIVE);
    } else {
        gui_draw_text((uint32_t)(x + FM_CLASSIC_INSET),
                      (uint32_t)(y + (int)display_scale_px(6)),
                      "Resultado:", GUI_MODERN_COLOR_TEXT);
        fm_draw_text_limited(
            x + (int)display_scale_px(92), y + (int)display_scale_px(6),
            width - (int)display_scale_px(330),
            fm_search_results[fm_search_selected].entry.name,
            GUI_MODERN_COLOR_TEXT);
    }
    if (fm_search_status.last_error != OK) warning = "Erro no indice";
    else if (fm_search_status.cancelled) warning = "Cancelado";
    else if (fm_search_status.volume_missing) warning = "Volume ausente";
    else if (fm_search_status.result_stale) warning = "Resultado obsoleto";
    else if (fm_search_status.stale) warning = "Desatualizado";
    else if (fm_search_status.building) warning = "Construindo";
    else if (fm_search_status.partial) warning = "Parcial";
    if (warning) {
        gui_draw_text((uint32_t)(x + width - (int)display_scale_px(160)),
                      (uint32_t)(y + (int)display_scale_px(6)), warning,
                      FM_CLASSIC_WARNING_COLOR);
    }
    if (fm_search_status.building) {
        int_to_str(fm_search_index_status.candidate_entries, progress);
        gui_draw_text((uint32_t)(x + width - (int)display_scale_px(300)),
                      (uint32_t)(y + (int)display_scale_px(6)),
                      "Indexados:", GUI_MODERN_COLOR_TEXT);
        fm_draw_text_limited(x + width - (int)display_scale_px(220),
                             y + (int)display_scale_px(6),
                             (int)display_scale_px(52), progress,
                             GUI_MODERN_COLOR_ACCENT);
    }
    int_to_str(fm_search_status.returned_matches, selection);
    fm_draw_text_limited(x + width - (int)display_scale_px(70),
                         y + (int)display_scale_px(6),
                         (int)display_scale_px(60), selection,
                         GUI_MODERN_COLOR_ACCENT);
}

static void fm_classic_draw_status(int x, int y, int width) {
    char selection[24];
    char size[16];

    gui_draw_rounded_rect((uint32_t)x, (uint32_t)y, (uint32_t)width,
                          FM_CLASSIC_STATUS_HEIGHT,
                          display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                          GUI_MODERN_COLOR_WINDOW);
    gui_draw_flat_border((uint32_t)x, (uint32_t)y, (uint32_t)width,
                         FM_CLASSIC_STATUS_HEIGHT,
                         GUI_MODERN_COLOR_BORDER_INACTIVE);
    if (fm_search_active) {
        fm_classic_draw_search_status(x, y, width);
        return;
    }
    if ((state.storage_virtual || state.storage_read_only) &&
        state.file_count == 0) {
        gui_draw_text((uint32_t)(x + width - (int)display_scale_px(170)),
                      (uint32_t)(y + (int)display_scale_px(6)),
                      "Somente leitura", GUI_MODERN_COLOR_ACCENT);
    }
    if (state.file_count == 0) {
        gui_draw_text((uint32_t)(x + FM_CLASSIC_INSET),
                      (uint32_t)(y + (int)display_scale_px(6)),
                      "Nenhum arquivo encontrado", GUI_MODERN_COLOR_BORDER_INACTIVE);
        return;
    }

    gui_draw_text((uint32_t)(x + FM_CLASSIC_INSET),
                  (uint32_t)(y + (int)display_scale_px(6)),
                  "Arquivo:", GUI_MODERN_COLOR_TEXT);
    fm_draw_text_limited(
        x + (int)display_scale_px(82),
        y + (int)display_scale_px(6),
        width - (int)display_scale_px(260),
        state.files[state.selected].name, GUI_MODERN_COLOR_TEXT);

    if (state.storage_virtual || state.storage_read_only) {
        gui_draw_text((uint32_t)(x + width - (int)display_scale_px(170)),
                      (uint32_t)(y + (int)display_scale_px(6)),
                      "Somente leitura", GUI_MODERN_COLOR_ACCENT);
    } else if (state.files[state.selected].is_dir) {
        gui_draw_text((uint32_t)(x + width - (int)display_scale_px(170)),
                      (uint32_t)(y + (int)display_scale_px(6)),
                      "Pasta", GUI_MODERN_COLOR_TEXT);
    } else {
        int_to_str(state.files[state.selected].size, size);
        fm_draw_text_limited(
            x + width - (int)display_scale_px(170),
            y + (int)display_scale_px(6), (int)display_scale_px(80),
            size, GUI_MODERN_COLOR_TEXT);
        gui_draw_text((uint32_t)(x + width - (int)display_scale_px(82)),
                      (uint32_t)(y + (int)display_scale_px(6)),
                      "bytes", GUI_MODERN_COLOR_TEXT);
    }

    int_to_str(state.selected + 1, selection);
    gui_draw_text((uint32_t)(x + width - (int)display_scale_px(40)),
                  (uint32_t)(y + (int)display_scale_px(6)),
                  selection, GUI_MODERN_COLOR_ACCENT);
}

static void fm_draw_classic_all(void) {
    int x;
    int y;
    int width;
    int height;
    int content_x;
    int content_y;
    int content_width;
    int content_height;
    int list_x;
    int list_width;
    int visible_side_items;
    vesa_color_t background;

    if (!fm_classic_get_layout(&x, &y, &width, &height)) {
        LOG_WARN("FM", "Layout Classic indisponivel; usando modo Simple");
        state.mode = FM_MODE_SIMPLE;
        fm_select_boot_source("");
        state.history_count = 0;
        state.history_pos = 0;
        fm_record_history();
        fm_refresh_files();
        fm_draw_simple_all();
        return;
    }
    fm_validate_storage_source();

    if (fm_help_mode) {
        fm_classic_draw_help();
        return;
    }
    if (input_mode == 2) {
        fm_classic_draw_view_file();
        return;
    }

    if (!fm_hosted) {
        mouse_invalidate_cursor();
        background.raw = GUI_MODERN_COLOR_BG;
        vesa_clear(background);
        gui_draw_scaled_window_frame((uint32_t)x, (uint32_t)y,
                                     (uint32_t)width, (uint32_t)height,
                                     "ZephyrOS Explorer", 1);
    }

    content_x = x + FM_CLASSIC_INSET;
    content_y = y + FM_CLASSIC_CONTENT_OFFSET;
    content_width = width - (FM_CLASSIC_INSET * 2);
    fm_classic_draw_toolbar(content_x, content_y, content_width);
    content_y += FM_CLASSIC_TOOLBAR_HEIGHT + FM_CLASSIC_PANE_GAP;
    fm_classic_draw_address(content_x, content_y, content_width);
    content_y += FM_CLASSIC_ADDRESS_HEIGHT + FM_CLASSIC_PANE_GAP;
    content_height = fm_classic_get_content_height(height);
    visible_side_items = fm_side_items_for_height(content_height);
    if (visible_side_items == 0) {
        state.side_selected = 0;
        if (state.focus_pane == 0) state.focus_pane = 1;
    } else if (state.side_selected >= visible_side_items) {
        state.side_selected = visible_side_items - 1;
    }

    fm_classic_draw_side(content_x, content_y, FM_CLASSIC_SIDE_WIDTH,
                        content_height);
    list_x = content_x + FM_CLASSIC_SIDE_WIDTH + FM_CLASSIC_PANE_GAP;
    list_width = content_width - FM_CLASSIC_SIDE_WIDTH - FM_CLASSIC_PANE_GAP;
    fm_classic_draw_list(list_x, content_y, list_width, content_height);
    fm_classic_draw_status(content_x,
                          content_y + content_height + FM_CLASSIC_PANE_GAP,
                          content_width);
    if (input_mode == 1) fm_classic_draw_input_dialog();
    if (!fm_hosted) taskbar_draw();
}

static void fm_classic_draw_input_dialog(void) {
    int x;
    int y;
    int width;
    int height;
    int dialog_width;
    int dialog_height = (int)display_scale_px(128);
    int dialog_x;
    int dialog_y;
    const char* title = "Entrada";
    const char* prompt = "Nome:";

    if (!fm_classic_get_layout(&x, &y, &width, &height)) return;

    dialog_width = width > (int)display_scale_px(520) ?
                   (int)display_scale_px(520) :
                   width - (int)display_scale_px(32);
    if (dialog_width < (int)display_scale_px(260)) {
        dialog_width = (int)display_scale_px(260);
    }
    dialog_x = x + (width - dialog_width) / 2;
    dialog_y = y + (height - dialog_height) / 2;

    if (confirm_delete) {
        title = "Confirmacao";
        prompt = "Excluir o item selecionado? (S/N)";
    } else if (rename_mode) {
        title = "Renomear arquivo";
        prompt = "Renomear para:";
    } else if (create_dir_mode) {
        title = "Nova pasta";
        prompt = "Nome da pasta:";
    } else if (create_file_mode) {
        title = "Novo arquivo";
        prompt = "Nome do arquivo:";
    }

    gui_draw_rounded_rect((uint32_t)dialog_x, (uint32_t)dialog_y,
                          (uint32_t)dialog_width, (uint32_t)dialog_height,
                          display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                          GUI_MODERN_COLOR_WINDOW);
    gui_draw_flat_border((uint32_t)dialog_x, (uint32_t)dialog_y,
                         (uint32_t)dialog_width, (uint32_t)dialog_height,
                         GUI_MODERN_COLOR_ACCENT);
    gui_draw_text((uint32_t)(dialog_x + (int)display_scale_px(16)),
                  (uint32_t)(dialog_y + (int)display_scale_px(14)), title,
                  GUI_MODERN_COLOR_TEXT);
    gui_draw_text((uint32_t)(dialog_x + (int)display_scale_px(16)),
                  (uint32_t)(dialog_y + (int)display_scale_px(42)),
                  prompt, GUI_MODERN_COLOR_TEXT);

    if (confirm_delete) return;

    gui_draw_rounded_rect((uint32_t)(dialog_x + (int)display_scale_px(16)),
                          (uint32_t)(dialog_y + (int)display_scale_px(66)),
                          (uint32_t)(dialog_width - (int)display_scale_px(32)),
                          (uint32_t)display_scale_px(30),
                          display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                          GUI_MODERN_COLOR_HOVER);
    gui_draw_flat_border((uint32_t)(dialog_x + (int)display_scale_px(16)),
                         (uint32_t)(dialog_y + (int)display_scale_px(66)),
                         (uint32_t)(dialog_width - (int)display_scale_px(32)),
                         (uint32_t)display_scale_px(30),
                         GUI_MODERN_COLOR_ACCENT);
    fm_draw_text_limited(dialog_x + (int)display_scale_px(24),
                         dialog_y + (int)display_scale_px(72),
                         dialog_width - (int)display_scale_px(48),
                         input_buffer, GUI_MODERN_COLOR_TEXT);
}

static void fm_classic_draw_page_surface(int x, int y, int width, int height,
                                         const char* title) {
    gui_draw_rounded_rect((uint32_t)x, (uint32_t)y, (uint32_t)width,
                          (uint32_t)height,
                          display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                          GUI_MODERN_COLOR_WINDOW);
    gui_draw_flat_border((uint32_t)x, (uint32_t)y, (uint32_t)width,
                         (uint32_t)height, GUI_MODERN_COLOR_BORDER_INACTIVE);
    gui_draw_rounded_rect((uint32_t)(x + FM_CLASSIC_INSET),
                          (uint32_t)(y + FM_CLASSIC_INSET),
                          (uint32_t)(width - FM_CLASSIC_INSET * 2),
                          (uint32_t)display_scale_px(24),
                          display_scale_px(GUI_MODERN_BUTTON_RADIUS_BASE),
                          GUI_MODERN_COLOR_HOVER);
    gui_draw_text((uint32_t)(x + FM_CLASSIC_INSET * 2),
                  (uint32_t)(y + (int)display_scale_px(12)), title,
                  GUI_MODERN_COLOR_TEXT);
}

static void fm_classic_draw_help(void) {
    int x;
    int y;
    int width;
    int height;
    const char* lines[] = {
        "Setas       Navegar na lista",
        "Enter       Abrir pasta/arquivo",
        "Backspace   Subir um nivel",
        "Alt+<-/->   Voltar/avancar",
        "F2          Renomear arquivo",
        "F3          Visualizar conteudo",
        "F5          Atualizar lista",
        "F6          Criar nova pasta",
        "F7          Criar novo arquivo",
        "F8          Excluir arquivo",
        "Ctrl+L      Digitar caminho",
        "Esc         Voltar ao Explorer"
    };
    int line_count = sizeof(lines) / sizeof(lines[0]);
    int row_height = (int)display_scale_px(24);
    int top_offset = (int)display_scale_px(54);
    int bottom_offset = (int)display_scale_px(42);
    int available_rows;
    int columns;
    int rows_per_column;

    if (!fm_classic_get_layout(&x, &y, &width, &height)) return;
    if (!fm_hosted) {
        vesa_color_t background;

        mouse_invalidate_cursor();
        background.raw = GUI_MODERN_COLOR_BG;
        vesa_clear(background);
    }
    fm_classic_draw_page_surface(x, y, width, height, "Ajuda do Explorer");
    available_rows = (height - top_offset - bottom_offset) / row_height;
    columns = available_rows < line_count ? 2 : 1;
    rows_per_column = (line_count + columns - 1) / columns;
    for (int i = 0; i < line_count; i++) {
        int column = i / rows_per_column;
        int row = i % rows_per_column;
        int line_x = x + (int)display_scale_px(24) +
                     column * (width / columns);
        int line_y = y + top_offset + row * row_height;

        gui_draw_text((uint32_t)line_x, (uint32_t)line_y,
                      lines[i], GUI_MODERN_COLOR_TEXT);
    }
    gui_draw_text((uint32_t)(x + (int)display_scale_px(24)),
                  (uint32_t)(y + height - (int)display_scale_px(34)),
                  "Pressione Esc para voltar.", GUI_MODERN_COLOR_ACCENT);
    if (!fm_hosted) taskbar_draw();
}

static void fm_classic_draw_view_file(void) {
    int x;
    int y;
    int width;
    int height;
    int text_width;
    int max_rows;
    int line_pos = 0;
    int row = 0;
    uint8_t* buffer;
    char line[FM_CLASSIC_MAX_TEXT];
    char file_path[FM_MAX_PATH];
    fm_file_entry_t* file;

    if (!fm_classic_get_layout(&x, &y, &width, &height)) return;
    if (!fm_hosted) {
        vesa_color_t background;

        mouse_invalidate_cursor();
        background.raw = GUI_MODERN_COLOR_BG;
        vesa_clear(background);
    }
    fm_classic_draw_page_surface(x, y, width, height, "Visualizando arquivo");

    if (state.selected < 0 || state.selected >= state.file_count) {
        if (!fm_hosted) taskbar_draw();
        return;
    }
    file = &state.files[state.selected];
    if (file->is_dir) {
        gui_draw_text((uint32_t)(x + (int)display_scale_px(24)),
                      (uint32_t)(y + (int)display_scale_px(54)),
                      "Nao e possivel visualizar pastas.", GUI_MODERN_COLOR_TEXT);
        if (!fm_hosted) taskbar_draw();
        return;
    }

    buffer = (uint8_t*)kmalloc(4096);
    if (!buffer) {
        LOG_ERROR("FM", "Falha ao alocar buffer da visualizacao");
        gui_draw_text((uint32_t)(x + (int)display_scale_px(24)),
                      (uint32_t)(y + (int)display_scale_px(54)),
                      "Erro: sem memoria!", 0x00C00000);
        if (!fm_hosted) taskbar_draw();
        return;
    }

    if (fm_join_path(file_path, state.current_path, file->name) != OK) {
        gui_draw_text((uint32_t)(x + (int)display_scale_px(24)),
                      (uint32_t)(y + (int)display_scale_px(54)),
                      "Erro: caminho muito longo.", 0x00C00000);
        kfree(buffer);
        buffer = 0;
        if (!fm_hosted) taskbar_draw();
        return;
    }

    int bytes;
    if (state.storage_boot) {
        bytes = fs_read_file_at(file_path, buffer, STORAGE_MAX_VIEW_BYTES);
    } else {
        uint32_t bytes_read = 0;
        int read_result = storage_read_file_range(
            state.storage_volume_id, file_path, 0, buffer,
            STORAGE_MAX_VIEW_BYTES, &bytes_read);
        bytes = read_result == OK ? (int)bytes_read : -1;
    }
    if (bytes < 0) {
        LOG_ERROR("FM", "Falha ao ler arquivo na visualizacao");
        gui_draw_text((uint32_t)(x + (int)display_scale_px(24)),
                      (uint32_t)(y + (int)display_scale_px(54)),
                      "Erro ao ler o arquivo.", 0x00C00000);
        kfree(buffer);
        buffer = 0;
        if (!fm_hosted) taskbar_draw();
        return;
    }
    if (bytes == 0) {
        gui_draw_text((uint32_t)(x + (int)display_scale_px(24)),
                      (uint32_t)(y + (int)display_scale_px(54)),
                      "(arquivo vazio)", GUI_MODERN_COLOR_TEXT);
        kfree(buffer);
        buffer = 0;
        if (!fm_hosted) taskbar_draw();
        return;
    }

    text_width = (width - (int)display_scale_px(48)) /
                 fm_display_font_width();
    if (text_width >= FM_CLASSIC_MAX_TEXT) text_width = FM_CLASSIC_MAX_TEXT - 1;
    max_rows = (height - (int)display_scale_px(92)) /
               fm_display_font_height();
    line[0] = '\0';
    for (int i = 0; i < bytes && row < max_rows; i++) {
        char c = (char)buffer[i];
        if (c == '\r') continue;
        if (c == '\n' || line_pos >= text_width) {
            line[line_pos] = '\0';
            gui_draw_text((uint32_t)(x + (int)display_scale_px(24)),
                          (uint32_t)(y + (int)display_scale_px(52) +
                                     row * fm_display_font_height()),
                          line, GUI_MODERN_COLOR_TEXT);
            row++;
            line_pos = 0;
            if (c == '\n') continue;
        }
        if (c == '\t') c = ' ';
        if (c >= 32 && c <= 126 && line_pos < text_width) {
            line[line_pos++] = c;
        }
    }
    if (row < max_rows && line_pos > 0) {
        line[line_pos] = '\0';
        gui_draw_text((uint32_t)(x + (int)display_scale_px(24)),
                      (uint32_t)(y + (int)display_scale_px(52) +
                                 row * fm_display_font_height()),
                      line, GUI_MODERN_COLOR_TEXT);
    }
    gui_draw_text((uint32_t)(x + (int)display_scale_px(24)),
                  (uint32_t)(y + height - (int)display_scale_px(34)),
                  "Pressione Esc para voltar.", GUI_MODERN_COLOR_ACCENT);
    kfree(buffer);
    buffer = 0;
    if (!fm_hosted) taskbar_draw();
}

static void fm_draw_help(void) {
    if (state.mode == FM_MODE_CLASSIC) {
        fm_help_mode = 1;
        fm_draw_all();
        return;
    }

    video_clear();
    video_draw_box(10, 3, 60, 19, 0x07);
    video_fill_rect(11, 4, 58, 17, ' ', 0x07);

    video_print_at(28, 4, "ZephyrOS Explorer - Ajuda", 0x0F);
    video_draw_hline(11, 5, 58, 0xC4, 0x07);

    video_print_at(13, 7,  "Setas      Navegar na lista", 0x07);
    video_print_at(13, 8,  "Enter      Abrir pasta/arquivo", 0x07);
    video_print_at(13, 9,  "Backspace  Subir um nivel", 0x07);
    video_print_at(13, 10, "Alt+<-     Voltar", 0x07);
    video_print_at(13, 11, "Alt+->     Avancar", 0x07);
    video_print_at(13, 12, "F2         Renomear arquivo", 0x07);
    video_print_at(13, 13, "F3         Visualizar conteudo", 0x07);
    video_print_at(13, 14, "F5         Atualizar lista", 0x07);
    video_print_at(13, 15, "F7         Criar novo arquivo", 0x07);
    video_print_at(13, 16, "F8         Excluir arquivo", 0x07);
    video_print_at(13, 17, "Ctrl+L     Digitar caminho", 0x07);
    video_print_at(13, 18, "Esc        Sair do Explorer", 0x07);

    video_draw_hline(11, 20, 58, 0xC4, 0x07);
    video_print_at(22, 21, "Pressione Esc para voltar...", 0x08);
    taskbar_draw();
}


static void fm_draw_create_dir(void) {
    if (state.mode == FM_MODE_CLASSIC) {
        fm_draw_all();
        return;
    }

    video_fill_rect(10, 10, 60, 3, ' ', 0x17);
    video_draw_box(10, 10, 60, 3, 0x17);
    video_print_at(12, 11, "Nova pasta: ", 0x17);
}

static void fm_draw_create_file(void) {
    if (state.mode == FM_MODE_CLASSIC) {
        fm_draw_all();
        return;
    }

    video_fill_rect(10, 10, 60, 3, ' ', 0x17);
    video_draw_box(10, 10, 60, 3, 0x17);
    video_print_at(12, 11, "Novo arquivo: ", 0x17);
}

static void fm_draw_rename_file(void) {
    if (state.mode == FM_MODE_CLASSIC) {
        fm_draw_all();
        return;
    }

    video_fill_rect(10, 10, 60, 3, ' ', 0x17);
    video_draw_box(10, 10, 60, 3, 0x17);
    video_print_at(12, 11, "Renomear para: ", 0x17);
}

static void fm_draw_confirm_delete(void) {
    if (state.mode == FM_MODE_CLASSIC) {
        fm_draw_all();
        return;
    }

    video_fill_rect(15, 10, 50, 3, ' ', 0x47);
    video_draw_box(15, 10, 50, 3, 0x47);
    video_print_at(17, 11, "Excluir? (S/N)", 0x47);
}

static void fm_draw_view_file(void) {
    if (state.mode == FM_MODE_CLASSIC) {
        input_mode = 2;
        if (fm_hosted) {
            fm_draw_all();
            return;
        }
        vesa_frame_begin();
        fm_classic_draw_view_file();
        vesa_frame_end();
        return;
    }

    video_clear();
    video_fill_rect(0, 0, SCREEN_COLS, 1, ' ', 0x1F);
    video_print_at((SCREEN_COLS - 22) / 2, 0, " Visualizando Arquivo ", 0x1F);

    if (state.selected < 0 || state.selected >= state.file_count) {
        taskbar_draw();
        return;
    }

    fm_file_entry_t* f = &state.files[state.selected];
    if (f->is_dir) {
        video_print_at(2, 2, "Nao e possivel visualizar pastas.", 0x0C);
        video_print_at(2, 4, "Pressione Esc para voltar.", 0x08);
        taskbar_draw();
        return;
    }

    uint8_t* buffer = (uint8_t*)kmalloc(4096);
    if (!buffer) {
        video_print_at(2, 2, "Erro: sem memoria!", 0x0C);
        taskbar_draw();
        return;
    }

    char file_path[FM_MAX_PATH];
    if (fm_join_path(file_path, state.current_path, f->name) != OK) {
        video_print_at(2, 2, "Erro: caminho muito longo.", 0x0C);
        kfree(buffer);
        buffer = 0;
        taskbar_draw();
        return;
    }

    int bytes = fs_read_file_at(file_path, buffer, 4095);
    if (bytes < 0) {
        video_print_at(2, 2, "Erro ao ler arquivo: ", 0x0C);
        video_print_at(22, 2, f->name, 0x0C);
        video_print_at(2, 4, "Pressione Esc para voltar.", 0x08);
        kfree(buffer);
        buffer = 0;
        taskbar_draw();
        return;
    }

    if (bytes == 0) {
        video_print_at(2, 2, "(arquivo vazio)", 0x08);
        video_print_at(2, 4, "Pressione Esc para voltar.", 0x08);
        kfree(buffer);
        buffer = 0;
        taskbar_draw();
        return;
    }

    buffer[bytes] = '\0';
    int row = 2;
    int col = 0;
    for (int i = 0; i < bytes && row < SCREEN_ROWS - 2; i++) {
        if (buffer[i] == '\n') {
            col = 0;
            row++;
        } else if (buffer[i] == '\r') {
            col = 0;
        } else if (buffer[i] == '\t') {
            col = (col + 8) & ~7;
            if (col >= SCREEN_COLS - 2) { col = 0; row++; }
        } else {
            video_put_char_at(buffer[i], 0x07, col, row);
            col++;
            if (col >= SCREEN_COLS - 2) {
                col = 0;
                row++;
            }
        }
    }

    video_fill_rect(0, SCREEN_ROWS - 2, SCREEN_COLS, 1, ' ', 0x70);
    video_print_at(2, SCREEN_ROWS - 2, "Pressione Esc para voltar", 0x70);

    kfree(buffer);
    buffer = 0;
    taskbar_draw();
}

static void fm_draw_simple_all(void) {
    video_clear();
    fm_draw_title_bar();
    fm_draw_menu_bar();
    fm_draw_address_bar();
    fm_draw_side_pane();
    fm_draw_column_headers();
    fm_draw_separator_bottom();
    fm_draw_status_bar();
    fm_draw_file_list();
    taskbar_draw();
}

static void fm_draw_all(void) {
    if (fm_hosted) {
        wm_request_hosted_redraw(WM_APP_EXPLORER);
        return;
    }
    fm_select_mode();

    if (state.mode == FM_MODE_CLASSIC) {
        vesa_frame_begin();
        fm_draw_classic_all();
        vesa_frame_end();
        return;
    }

    fm_draw_simple_all();
}

void fm_draw(void) {
    fm_draw_all();
}

void fm_update(void) {
    file_index_status_t status;
    uint32_t event_delta;

    if (!state.running || state.mode != FM_MODE_CLASSIC ||
        !fm_search_active) return;
    if (file_index_get_status(&status) != OK ||
        status.event_generation == fm_search_event_generation) return;
    event_delta = status.event_generation - fm_search_event_generation;
    if (status.state == fm_search_index_state &&
        event_delta < FM_SEARCH_REDRAW_STEP) return;
    fm_search_refresh();
    fm_draw_all();
}

static int fm_scroll_file_selection(int delta) {
    int rows;
    int previous = state.selected;

    if (state.file_count <= 0 || delta == 0) return 0;
    while (delta > 0 && state.selected > 0) {
        state.selected--;
        delta--;
    }
    while (delta < 0 && state.selected < state.file_count - 1) {
        state.selected++;
        delta++;
    }
    if (state.selected == previous) return 0;

    rows = fm_visible_rows();
    if (state.selected < state.scroll_offset) {
        state.scroll_offset = state.selected;
    } else if (state.selected >= state.scroll_offset + rows) {
        state.scroll_offset = state.selected - rows + 1;
    }
    return 1;
}

static int fm_search_button_pressed(const mouse_event_t* event, int x, int y,
                                    int width) {
    int address_x = x + FM_CLASSIC_INSET;
    int address_y = y + FM_CLASSIC_CONTENT_OFFSET +
                    FM_CLASSIC_TOOLBAR_HEIGHT + FM_CLASSIC_PANE_GAP;
    int address_width = width - (FM_CLASSIC_INSET * 2);

    if (event->x < address_x + address_width -
                       FM_CLASSIC_SEARCH_BUTTON_WIDTH ||
        event->x >= address_x + address_width || event->y < address_y ||
        event->y >= address_y + FM_CLASSIC_ADDRESS_HEIGHT) return 0;
    fm_search_begin();
    return 1;
}

static void fm_mouse_select_item(int item_index) {
    state.focus_pane = 1;
    if (!fm_search_active) {
        state.selected = item_index;
        return;
    }
    if (fm_search_last_clicked == item_index) {
        fm_search_selected = item_index;
        fm_search_activate_selected();
        return;
    }
    fm_search_selected = item_index;
    fm_search_last_clicked = item_index;
    fm_search_input = 0;
}

static int fm_hosted_mouse(mouse_event_t* event, int x, int y,
                           int width, int height) {
    int content_x;
    int content_y;
    int content_width;
    int content_height;
    int list_x;
    int list_width;
    int list_y;
    int panel_y;
    int panel_height;

    if (!event) {
        LOG_ERROR("FM", "Evento de mouse nulo no Explorer hospedado");
        return 0;
    }
    if (input_mode != 0 || fm_help_mode) return 0;

    fm_hosted_x = x;
    fm_hosted_y = y;
    fm_hosted_width = width;
    fm_hosted_height = height;
    if (!fm_classic_get_layout(&content_x, &content_y,
                              &content_width, &content_height)) return 0;

    list_x = content_x + FM_CLASSIC_INSET + FM_CLASSIC_SIDE_WIDTH +
             FM_CLASSIC_PANE_GAP;
    list_width = content_width - (FM_CLASSIC_INSET * 2) -
                 FM_CLASSIC_SIDE_WIDTH - FM_CLASSIC_PANE_GAP;
    panel_y = content_y + FM_CLASSIC_CONTENT_OFFSET +
              FM_CLASSIC_TOOLBAR_HEIGHT + FM_CLASSIC_PANE_GAP +
              FM_CLASSIC_ADDRESS_HEIGHT + FM_CLASSIC_PANE_GAP;
    panel_height = fm_classic_get_content_height(content_height);
    list_y = panel_y +
             (int)display_scale_px(4) + FM_CLASSIC_HEADER_HEIGHT +
             (int)display_scale_px(4);

    if (event->event == MOUSE_EVENT_WHEEL && event->wheel != 0) {
        if (event->x < list_x || event->x >= list_x + list_width ||
            event->y < list_y ||
            event->y >= list_y + fm_visible_rows() *
                        (FM_CLASSIC_ROW_HEIGHT + FM_CLASSIC_ROW_GAP)) {
            return 0;
        }
        return fm_search_active ?
               fm_search_scroll_selection(event->wheel) :
               fm_scroll_file_selection(event->wheel);
    }
    if (event->event != MOUSE_EVENT_PRESS ||
        !(event->changed & MOUSE_BTN_LEFT)) return 0;

    if (fm_search_button_pressed(event, content_x, content_y,
                                 content_width)) return 1;

    if (event->x >= content_x + FM_CLASSIC_INSET &&
        event->x < content_x + FM_CLASSIC_INSET + FM_CLASSIC_SIDE_WIDTH &&
        event->y >= panel_y + FM_CLASSIC_SIDE_HEADER_OFFSET) {
        int relative_y = event->y -
                         (panel_y + FM_CLASSIC_SIDE_HEADER_OFFSET);
        int step = FM_CLASSIC_ROW_HEIGHT + FM_CLASSIC_ROW_GAP;
        int row = relative_y / step;

        if (relative_y % step < FM_CLASSIC_ROW_HEIGHT &&
            row >= 0 && row < fm_side_items_for_height(panel_height)) {
            if (fm_search_active) fm_search_close();
            state.focus_pane = 0;
            state.side_selected = row;
            return 1;
        }
    }
    if (event->x >= list_x && event->x < list_x + list_width &&
        event->y >= list_y) {
        int relative_y = event->y - list_y;
        int step = FM_CLASSIC_ROW_HEIGHT + FM_CLASSIC_ROW_GAP;
        int row = relative_y / step;
        int file_index = (fm_search_active ? fm_search_scroll :
                          state.scroll_offset) + row;
        int item_count = fm_search_active ?
                         (int)fm_search_status.returned_matches :
                         state.file_count;

        if (relative_y % step < FM_CLASSIC_ROW_HEIGHT &&
            row >= 0 && row < fm_visible_rows() &&
            file_index >= 0 && file_index < item_count) {
            fm_mouse_select_item(file_index);
            return 1;
        }
    }
    return 0;
}

static void fm_hosted_draw(int x, int y, int width, int height) {
    fm_hosted_x = x;
    fm_hosted_y = y;
    fm_hosted_width = width;
    fm_hosted_height = height;
    fm_draw_classic_all();
}

static void fm_hosted_close(void) {
    fm_hosted = 0;
    state.running = 0;
    fm_help_mode = 0;
    input_mode = 0;
    fm_search_close();
}

static void fm_redraw_file_view(void) {
    if (state.mode == FM_MODE_CLASSIC) {
        fm_draw_all();
        return;
    }

    fm_draw_file_list();
    fm_draw_status_bar();
}

void fm_init(void) {
    state.selected = 0;
    state.scroll_offset = 0;
    state.view_mode = 0;
    state.running = 1;

    fm_ensure_boot_directory(".TRASH");
    fm_ensure_boot_directory("DESKTOP");
    fm_ensure_boot_directory("DOCUMENTOS");
    fm_ensure_boot_directory("DOWNLOADS");
    fm_ensure_boot_directory("IMAGENS");
    fm_ensure_boot_directory("MUSICA");
    fm_ensure_boot_directory("VIDEOS");

    fm_select_boot_source("");
    state.history_count = 0;
    state.history_pos = 0;
    state.address_mode = 0;
    state.address_pos = 0;
    input_pos = 0;
    input_mode = 0;
    rename_mode = 0;
    confirm_delete = 0;
    fm_help_mode = 0;
    state.mode = FM_MODE_SIMPLE;
    state.focus_pane = 1;
    state.side_selected = 0;
    fm_fallback_logged = 0;
    fm_search_close();
    fm_search_query[0] = '\0';
    fm_search_event_generation = 0;
    fm_search_index_state = FILE_INDEX_STATE_UNINITIALIZED;
    old_name[0] = '\0';

    fm_record_history();
    fm_refresh_files();
}


void fm_open(void) {
    if (!recovery_is_enabled(RECOVERY_COMPONENT_FILEMANAGER)) {
        LOG_WARN("FM", "File Manager indisponivel; abertura ignorada");
        state.running = 0;
        return;
    }

    if (desktop_get_mode() == DESKTOP_MODE_CLASSIC && fm_hosted) {
        wm_set_active(1);
        wm_register_hosted_app(&fm_hosted_app);
        return;
    }

    fm_init();
    if (desktop_get_mode() == DESKTOP_MODE_CLASSIC) {
        wm_set_active(1);
        fm_hosted = 1;
        state.mode = FM_MODE_CLASSIC;
        fm_select_virtual_root();
        state.history_count = 0;
        state.history_pos = 0;
        fm_record_history();
        fm_refresh_files();
        if (wm_register_hosted_app(&fm_hosted_app) == OK) return;
        fm_hosted = 0;
        state.mode = FM_MODE_SIMPLE;
        fm_select_boot_source("");
        state.history_count = 0;
        state.history_pos = 0;
        fm_record_history();
        fm_refresh_files();
        wm_set_active(0);
        desktop_set_active(0);
        LOG_WARN("FM", "Workspace indisponivel; usando Explorer Simple");
    }
    taskbar_add_app(TB_APP_EXPLORER, "Explorer");
    fm_draw_all();
}

void fm_close(void) {
    if (fm_hosted) {
        wm_close_hosted_app(WM_APP_EXPLORER);
        return;
    }
    fm_search_close();
    state.running = 0;
    taskbar_remove_app(TB_APP_EXPLORER);
    desktop_set_active(1);
    desktop_draw();
}

int fm_is_running(void) {
    return state.running;
}

fm_mode_t fm_get_mode(void) {
    return state.mode;
}

static const char scancode_table[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/',0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

static int alt_pressed = 0;
static int ctrl_pressed = 0;

static int fm_block_read_only_mutation(uint8_t scancode) {
    int mutation_key = scancode == 0x3CU || scancode == 0x40U ||
                       scancode == 0x41U || scancode == 0x42U ||
                       scancode == 0x43U || scancode == 0x44U ||
                       scancode == 0x53U || scancode == 0x57U;

    if (!mutation_key || state.mode != FM_MODE_CLASSIC ||
        (!state.storage_virtual && !state.storage_read_only)) return 0;
    LOG_WARN("FM", "Mutacao recusada em volume somente-leitura");
    fm_draw_all();
    return 1;
}

static void fm_search_move_page(int direction) {
    int rows = fm_visible_rows();
    int count = (int)fm_search_status.returned_matches;

    fm_search_selected += direction * rows;
    if (fm_search_selected < 0) fm_search_selected = 0;
    if (fm_search_selected >= count) fm_search_selected = count - 1;
    if (fm_search_selected < 0) fm_search_selected = 0;
    if (fm_search_selected < fm_search_scroll) {
        fm_search_scroll = fm_search_selected;
    } else if (fm_search_selected >= fm_search_scroll + rows) {
        fm_search_scroll = fm_search_selected - rows + 1;
    }
    fm_search_last_clicked = -1;
}

static int fm_search_handle_key(uint8_t scancode) {
    char character = scancode_table[scancode];

    if (scancode == FM_SEARCH_SCANCODE_ESCAPE) {
        fm_search_close();
        fm_draw_all();
        return 1;
    }
    if (scancode == FM_SEARCH_SCANCODE_BACKSPACE && fm_search_input) {
        if (fm_search_pos > 0) {
            fm_search_query[--fm_search_pos] = '\0';
            fm_search_refresh();
            fm_draw_all();
        }
        return 1;
    }
    if (scancode == FM_SEARCH_SCANCODE_ENTER) {
        if (fm_search_input) {
            if (fm_search_pos > 0) {
                fm_search_input = 0;
                fm_search_refresh();
            }
        } else {
            fm_search_activate_selected();
        }
        fm_draw_all();
        return 1;
    }
    if (scancode == FM_SEARCH_SCANCODE_UP) {
        fm_search_scroll_selection(1);
        fm_draw_all();
        return 1;
    }
    if (scancode == FM_SEARCH_SCANCODE_DOWN) {
        fm_search_scroll_selection(-1);
        fm_draw_all();
        return 1;
    }
    if (scancode == FM_SEARCH_SCANCODE_PAGE_UP ||
        scancode == FM_SEARCH_SCANCODE_PAGE_DOWN) {
        fm_search_move_page(scancode == FM_SEARCH_SCANCODE_PAGE_UP ? -1 : 1);
        fm_draw_all();
        return 1;
    }
    if (scancode == FM_SEARCH_SCANCODE_HOME ||
        scancode == FM_SEARCH_SCANCODE_END) {
        fm_search_selected = scancode == FM_SEARCH_SCANCODE_HOME ? 0 :
            (int)fm_search_status.returned_matches - 1;
        if (fm_search_selected < 0) fm_search_selected = 0;
        fm_search_scroll = scancode == FM_SEARCH_SCANCODE_HOME ? 0 :
            fm_search_selected - fm_visible_rows() + 1;
        if (fm_search_scroll < 0) fm_search_scroll = 0;
        fm_search_last_clicked = -1;
        fm_draw_all();
        return 1;
    }
    if (fm_search_input && character && !ctrl_pressed &&
        fm_search_pos + 1 < FILE_INDEX_QUERY_SIZE) {
        fm_search_query[fm_search_pos++] = character;
        fm_search_query[fm_search_pos] = '\0';
        fm_search_refresh();
        fm_draw_all();
        return 1;
    }
    return 1;
}

void fm_handle_key(uint8_t scancode) {
    int taskbar_result;

    taskbar_result = 0;
    if (!fm_hosted && taskbar_handle_config_key(scancode)) return;
    if (!fm_hosted) taskbar_result = taskbar_handle_key(scancode);
    if (!fm_hosted && taskbar_result) {
        if (taskbar_result == 2) {
            fm_close();
            shell_handle_app_request(IPC_APP_OPEN_SHELL);
        } else if (taskbar_result == 3) {
            fm_close();
            shell_handle_app_request(IPC_APP_OPEN_EXPLORER);
        } else if (taskbar_result == 4) {
            fm_close();
            shell_handle_app_request(IPC_APP_OPEN_TASKMANAGER_GUI);
        } else if (taskbar_result == 7) {
            fm_close();
            shell_handle_app_request(IPC_APP_OPEN_DESKTOP);
        } else if (taskbar_result == 8) {
            fm_close();
            shell_handle_app_request(IPC_APP_OPEN_SETTINGS);
        } else if (taskbar_result == TB_ACTION_UPDATER) {
            fm_close();
            shell_handle_app_request(IPC_APP_OPEN_UPDATER);
        } else if (taskbar_result == TB_ACTION_APPSTORE) {
            fm_close();
            shell_handle_app_request(IPC_APP_OPEN_APP_STORE);
        }
        return;
    }

    if (scancode == 0x38) {
        alt_pressed = 1;
        return;
    }
    if (scancode == 0x38 + 0x80) {
        alt_pressed = 0;
        return;
    }

    if (scancode == FM_SCANCODE_CTRL_MAKE) {
        ctrl_pressed = 1;
        return;
    }
    if (scancode == FM_SCANCODE_CTRL_BREAK) {
        ctrl_pressed = 0;
        return;
    }

    if (scancode & 0x80) return;

    if (state.mode == FM_MODE_CLASSIC && ctrl_pressed &&
        scancode == FM_SCANCODE_F && !input_mode && !fm_help_mode) {
        fm_search_begin();
        fm_draw_all();
        return;
    }
    if (state.mode == FM_MODE_CLASSIC && fm_search_active) {
        fm_search_handle_key(scancode);
        return;
    }

    if (fm_block_read_only_mutation(scancode)) return;

    if (state.mode == FM_MODE_CLASSIC && input_mode == 1 &&
        (state.storage_virtual || state.storage_read_only)) {
        input_mode = 0;
        rename_mode = 0;
        create_dir_mode = 0;
        create_file_mode = 0;
        confirm_delete = 0;
        LOG_WARN("FM", "Mutacao cancelada apos mudanca de montagem");
        fm_draw_all();
        return;
    }

    if (fm_help_mode) {
        if (scancode == 0x01) {
            fm_help_mode = 0;
            fm_draw_all();
        }
        return;
    }
    if (input_mode == 2) {
        if (scancode == 0x01) {
            input_mode = 0;
            fm_draw_all();
        }
        return;
    }

    // --- Modo de endereco (Ctrl+L) ---
    if (state.address_mode) {
        char c = scancode_table[scancode];

        if (scancode == 0x0E) { // Backspace
            if (state.address_pos > 0) {
                state.address_pos--;
                state.address_buffer[state.address_pos] = '\0';
                int cx = 3 + state.address_pos;
                if (state.mode == FM_MODE_CLASSIC) {
                    fm_draw_all();
                } else {
                    video_put_char_at(' ', FM_ADDRESS_INPUT_COLOR, cx, 2);
                }
            }
            return;
        }
        if (scancode == 0x1C) { // Enter -> navegar para o path digitado
            state.address_buffer[state.address_pos] = '\0';
            state.address_mode = 0;
            if (state.address_pos > 0) {
                if (fm_navigate_address_value(state.address_buffer) != OK) {
                    LOG_ERROR("FM", "Navegacao por endereco recusada");
                    fm_draw_all();
                }
            } else {
                fm_draw_all();
            }
            return;
        }
        if (scancode == 0x01) { // Esc -> cancelar
            state.address_mode = 0;
            fm_draw_all();
            return;
        }
        if (c && state.address_pos < FM_MAX_PATH - 1) {
            state.address_buffer[state.address_pos++] = c;
            state.address_buffer[state.address_pos] = '\0';
            int cx = 3 + state.address_pos - 1;
            if (state.mode == FM_MODE_CLASSIC) {
                fm_draw_all();
            } else {
                video_put_char_at(c, FM_ADDRESS_INPUT_COLOR, cx, 2);
            }
        }
        return;
    }

    // --- Modo de input (criar pasta, criar arquivo, renomear, confirmar delete) ---
    if (input_mode) {
        char c = scancode_table[scancode];

        if (scancode == 0x01) { // Esc -> cancelar
            input_mode = 0;
            rename_mode = 0;
            confirm_delete = 0;
            create_dir_mode = 0;
            create_file_mode = 0;
            fm_draw_all();
            return;
        }

        if (scancode == 0x1C) { // Enter -> confirmar
            input_buffer[input_pos] = '\0';

            if (confirm_delete) {
                // S ou s = confirmar exclusao
                if (input_buffer[0] == 'S' || input_buffer[0] == 's') {
                    if (state.selected >= 0 && state.selected < state.file_count) {
                        fs_delete_file_in_dir(state.current_path, state.files[state.selected].name);
                        if (state.selected > 0) state.selected--;
                        fm_refresh_files();
                    }
                }
                confirm_delete = 0;
                input_mode = 0;
                fm_draw_all();
                return;
            }

            if (rename_mode) {
                fm_rename_selected_file();
                fm_refresh_files();
                rename_mode = 0;
                input_mode = 0;
                fm_draw_all();
                return;
            }

            if (create_dir_mode) {
                if (input_pos > 0) {
                    fs_create_dir_entry(state.current_path, input_buffer, 0x10);
                    fm_refresh_files();
                }
                create_dir_mode = 0;
                input_mode = 0;
                fm_draw_all();
                return;
            }

            if (create_file_mode) {
                if (input_pos > 0) {
                    uint8_t empty_data[1] = {0};
                    fs_write_file_in_dir(state.current_path, input_buffer, empty_data, 0);
                    fm_refresh_files();
                }
                create_file_mode = 0;
                input_mode = 0;
                fm_draw_all();
                return;
            }

            input_mode = 0;
            fm_draw_all();
            return;
        }

        if (scancode == 0x0E) { // Backspace
            if (input_pos > 0) {
                input_pos--;
                input_buffer[input_pos] = '\0';
                int cx = 12 + input_pos; // Default fallback
                if (create_dir_mode) cx = 24 + input_pos;
                else if (create_file_mode) cx = 26 + input_pos;
                else if (rename_mode) cx = 27 + input_pos;
                if (state.mode == FM_MODE_CLASSIC) {
                    fm_draw_all();
                } else {
                    video_put_char_at(' ', 0x17, cx, 11);
                }
            }
            return;
        }

        if (c && input_pos < 31) {
            input_buffer[input_pos++] = c;
            input_buffer[input_pos] = '\0';
            int cx = 12 + input_pos - 1; // Default fallback
            if (create_dir_mode) cx = 24 + input_pos - 1;
            else if (create_file_mode) cx = 26 + input_pos - 1;
            else if (rename_mode) cx = 27 + input_pos - 1;
            if (state.mode == FM_MODE_CLASSIC) {
                fm_draw_all();
            } else {
                video_put_char_at(c, 0x17, cx, 11);
            }
        }
        return;
    }

    // --- Teclas de navegacao / atalhos gerais ---
    if (alt_pressed) {
        if (scancode == 0x4B) { // Alt+Esquerda = Voltar
            alt_pressed = 0;
            fm_go_back();
            return;
        }
        if (scancode == 0x4D) { // Alt+Direita = Avancar
            alt_pressed = 0;
            fm_go_forward();
            return;
        }
    }

    if (scancode == 0x0F) { // TAB
        state.focus_pane = 1 - state.focus_pane;
        fm_draw_all();
        return;
    }

    if (scancode == 0x48) { // UP
        if (state.focus_pane == 0) {
            if (state.side_selected > 0) state.side_selected--;
        } else {
            if (state.selected > 0) {
                state.selected--;
                if (state.selected < state.scroll_offset) {
                    state.scroll_offset = state.selected;
                }
            }
        }
        fm_draw_all();
        return;
    }

    if (scancode == 0x50) { // DOWN
        if (state.focus_pane == 0) {
            if (state.side_selected < fm_visible_side_items() - 1) {
                state.side_selected++;
            }
        } else {
            if (state.selected < state.file_count - 1) {
                state.selected++;
                if (state.selected >= state.scroll_offset + fm_visible_rows()) {
                    state.scroll_offset++;
                }
            }
        }
        fm_draw_all();
        return;
    }

    if (scancode == 0x1C) { // ENTER
        if (state.focus_pane == 0) {
            int visible_items = fm_visible_side_items();

            if (visible_items <= 0) {
                state.focus_pane = 1;
                fm_draw_all();
                return;
            }
            if (state.side_selected >= visible_items) {
                state.side_selected = visible_items - 1;
            }
            state.focus_pane = 1;
            if (state.mode == FM_MODE_CLASSIC && state.side_selected == 0) {
                fm_select_virtual_root();
            } else {
                fm_select_boot_source(side_pane_paths[state.side_selected]);
            }
            fm_record_history();
            fm_refresh_files();
            fm_draw_all();
            return;
        }
        if (state.file_count == 0) return;
        if (state.mode == FM_MODE_CLASSIC && state.storage_virtual) {
            if (fm_select_mounted_source(state.files[state.selected].name) == OK) {
                fm_record_history();
                fm_refresh_files();
            } else {
                fm_select_virtual_root();
                fm_refresh_files();
            }
            fm_draw_all();
            return;
        }
        fm_file_entry_t* f = &state.files[state.selected];
        if (f->is_dir) {
            char new_path[FM_MAX_PATH];
            if (fm_join_path(new_path, state.current_path, f->name) == OK) {
                fm_navigate_to(new_path);
            }
        } else {
            input_mode = 2;
            if (state.mode == FM_MODE_CLASSIC) {
                fm_draw_all();
            } else {
                video_clear();
                fm_draw_view_file();
            }
        }
        return;
    }

    if (scancode == 0x0E) { // Backspace = subir nivel
        fm_go_up();
        return;
    }

    if (scancode == 0x3B) { // F1 = Ajuda
        fm_draw_help();
        return;
    }

    if (scancode == 0x3D) { // F3 = Ver arquivo
        input_mode = 2;
        fm_draw_view_file();
        return;
    }

    if (scancode == 0x3E) { // F4 = Atualizar... alias para F5
        fm_refresh_files();
        fm_draw_all();
        return;
    }

    if (scancode == 0x3F) { // F5 = Atualizar
        fm_refresh_files();
        fm_draw_all();
        return;
    }

    if (scancode == 0x40) { // F6 = Nova Pasta
        create_dir_mode = 1;
        create_file_mode = 0;
        rename_mode = 0;
        confirm_delete = 0;
        input_mode = 1;
        input_pos = 0;
        input_buffer[0] = '\0';
        fm_draw_all();
        if (state.mode == FM_MODE_SIMPLE) fm_draw_create_dir();
        return;
    }

    if (scancode == 0x41) { // F7 = Novo Arquivo
        create_file_mode = 1;
        create_dir_mode = 0;
        rename_mode = 0;
        confirm_delete = 0;
        input_mode = 1;
        input_pos = 0;
        input_buffer[0] = '\0';
        fm_draw_all();
        if (state.mode == FM_MODE_SIMPLE) fm_draw_create_file();
        return;
    }

    if (scancode == 0x42) { // F8 = Excluir (confirmar)
        if (state.selected >= 0 && state.selected < state.file_count) {
            confirm_delete = 1;
            input_mode = 1;
            input_pos = 0;
            input_buffer[0] = '\0';
            fm_draw_all();
            if (state.mode == FM_MODE_SIMPLE) fm_draw_confirm_delete();
        }
        return;
    }

    if (scancode == 0x3C) { // F2 = Renomear
        if (state.selected >= 0 && state.selected < state.file_count &&
            !state.files[state.selected].is_dir) {
            input_mode = 1;
            rename_mode = 1;
            create_dir_mode = 0;
            create_file_mode = 0;
            confirm_delete = 0;
            input_pos = 0;
            input_buffer[0] = '\0';

            int i = 0;
            for (; state.files[state.selected].name[i]; i++) {
                old_name[i] = state.files[state.selected].name[i];
            }
            old_name[i] = '\0';

            input_pos = 0;
            for (i = 0; state.files[state.selected].name[i]; i++) {
                input_buffer[input_pos++] = state.files[state.selected].name[i];
            }
            input_buffer[input_pos] = '\0';
            fm_draw_all();
            if (state.mode == FM_MODE_SIMPLE) {
                fm_draw_rename_file();
                video_print_at(27, 11, state.files[state.selected].name, 0x17);
            }
        } else {
            LOG_WARN("FM", "Renomeacao de pasta nao e suportada");
        }
        return;
    }

    if (scancode == 0x43) { // F9 = Copiar
        if (state.file_count > 0 && !state.files[state.selected].is_dir) {
            state.clipboard_op = 1;
            str_copy(state.clipboard_dir, state.current_path);
            str_copy(state.clipboard_name, state.files[state.selected].name);
            state.clipboard_size = state.files[state.selected].size;
            fm_join_path(state.clipboard_path, state.current_path, state.files[state.selected].name);
        }
        return;
    }

    if (scancode == 0x44) { // F10 = Recortar
        if (state.file_count > 0 && !state.files[state.selected].is_dir) {
            state.clipboard_op = 2;
            str_copy(state.clipboard_dir, state.current_path);
            str_copy(state.clipboard_name, state.files[state.selected].name);
            state.clipboard_size = state.files[state.selected].size;
            fm_join_path(state.clipboard_path, state.current_path, state.files[state.selected].name);
        }
        return;
    }

    if (scancode == 0x57) { // F11 = Colar
        if (state.clipboard_op > 0 && state.clipboard_size > 0) {
            uint8_t* buf = kmalloc(state.clipboard_size + 1);
            if (buf) {
                if (fs_read_file_at(state.clipboard_path, buf,
                                    state.clipboard_size) >= 0) {
                    fs_write_file_in_dir(state.current_path, state.clipboard_name, buf, state.clipboard_size);
                    if (state.clipboard_op == 2) {
                        fs_delete_file_in_dir(state.clipboard_dir, state.clipboard_name);
                        state.clipboard_op = 0;
                    }
                }
                kfree(buf);
                buf = 0;
                fm_refresh_files();
                fm_draw_all();
            }
        }
        return;
    }

    if (scancode == 0x53) { // DELETE = Mover para Lixeira
        if (state.file_count > 0 && !state.files[state.selected].is_dir) {
            char trash_path[] = "/.trash";
            fs_create_dir_entry("", ".trash", 0x10);
            uint32_t size = state.files[state.selected].size;
            uint8_t* buf = kmalloc(size + 1);
            if (buf) {
                if (fs_read_file_at(state.current_path, buf, size) >= 0) {
                    fs_write_file_in_dir(trash_path, state.files[state.selected].name, buf, size);
                    fs_delete_file_in_dir(state.current_path, state.files[state.selected].name);
                }
                kfree(buf);
                buf = 0;
                fm_refresh_files();
                fm_draw_all();
            }
        }
        return;
    }

    if (scancode == 0x26) { // L = Modo endereco (Ctrl+L era so uma ideia)
        state.address_mode = 1;
        state.address_pos = 0;
        state.address_buffer[0] = '\0';
        fm_draw_address_bar();
        return;
    }

    if (scancode == 0x01) {
        if (state.mode == FM_MODE_SIMPLE) fm_close();
        return;
    }

    if (state.file_count == 0) return;

    if (scancode == 0x49) { // Page Up
        int rows = fm_visible_rows();
        state.selected -= rows;
        if (state.selected < 0) state.selected = 0;
        if (state.selected < state.scroll_offset) {
            state.scroll_offset = state.selected;
        }
        fm_redraw_file_view();
        return;
    }

    if (scancode == 0x51) { // Page Down
        int rows = fm_visible_rows();
        state.selected += rows;
        if (state.selected >= state.file_count) {
            state.selected = state.file_count - 1;
        }
        if (state.selected >= state.scroll_offset + rows) {
            state.scroll_offset = state.selected - rows + 1;
        }
        fm_redraw_file_view();
        return;
    }

    if (scancode == 0x47) { // Home
        state.selected = 0;
        state.scroll_offset = 0;
        fm_redraw_file_view();
        return;
    }

    if (scancode == 0x4F) { // End
        state.selected = state.file_count - 1;
        int rows = fm_visible_rows();
        if (state.selected >= rows) {
            state.scroll_offset = state.selected - rows + 1;
        }
        fm_redraw_file_view();
        return;
    }
}


void fm_run(void) {
    fm_open();
    if (fm_hosted) return;
    ipc_msg_t msg;
    while (state.running) {
        if (ipc_receive(&msg)) {
            if (msg.type == IPC_MSG_KEYBOARD) {
                fm_handle_key((uint8_t)msg.data1);
            } else if (msg.type == IPC_MSG_APP_REQUEST) {
                fm_close();
                shell_handle_app_request(msg.data1);
            }
        } else {
            process_yield();
        }
    }
}
