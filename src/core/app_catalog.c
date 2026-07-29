#include "core/app_catalog.h"
#include "core/app_loader.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/string.h"
#include "fs/fs.h"

#define APP_CATALOG_DIRECTORY_ATTRIBUTE 0x10U
#define APP_CATALOG_EXTENSION_SIZE      4U
#define APP_CATALOG_VERSION_PARTS       3U

typedef struct {
    char alias[APP_CATALOG_ALIAS_SIZE];
    uint32_t size;
} app_catalog_source_t;

static app_catalog_entry_t catalog_entries[APP_CATALOG_MAX_ENTRIES];
static app_catalog_source_t catalog_sources[APP_CATALOG_MAX_SOURCES];
static app_package_info_t catalog_installed[APP_CATALOG_MAX_ENTRIES];
static app_catalog_status_t catalog_status;
static uint32_t catalog_source_count = 0;
static uint32_t catalog_installed_count = 0;
static int catalog_initialized = 0;
static int catalog_ready = 0;
static int catalog_read_error = 0;

static void app_catalog_copy_text(char* destination, uint32_t capacity,
                                  const char* source) {
    uint32_t length = source ? kstrlen(source) : 0;

    if (!destination || capacity == 0) return;
    if (length >= capacity) length = capacity - 1U;
    if (length > 0) kmemcpy(destination, source, length);
    destination[length] = '\0';
}

static int app_catalog_reason_priority(app_catalog_reason_t reason) {
    if (reason == APP_CATALOG_REASON_FILESYSTEM_UNAVAILABLE ||
        reason == APP_CATALOG_REASON_LOADER_UNAVAILABLE ||
        reason == APP_CATALOG_REASON_PACKAGE_SERVICE_UNAVAILABLE) return 4;
    if (reason == APP_CATALOG_REASON_READ_ERROR) return 3;
    if (reason == APP_CATALOG_REASON_SOURCE_LIMIT ||
        reason == APP_CATALOG_REASON_ENTRY_LIMIT) return 2;
    if (reason == APP_CATALOG_REASON_PACKAGE_INVALID ||
        reason == APP_CATALOG_REASON_ALIAS_MISMATCH) return 1;
    return 0;
}

static void app_catalog_record_problem(app_catalog_reason_t reason) {
    if (app_catalog_reason_priority(reason) >
        app_catalog_reason_priority(catalog_status.reason)) {
        catalog_status.reason = reason;
    }
}

static int app_catalog_is_source_name(const char* name) {
    uint32_t length;
    uint32_t base_length;

    if (!name) return 0;
    length = kstrlen(name);
    if (length <= APP_CATALOG_EXTENSION_SIZE ||
        length >= APP_CATALOG_ALIAS_SIZE) return 0;
    base_length = length - APP_CATALOG_EXTENSION_SIZE;
    if (base_length == 0 || base_length >= APP_PACKAGE_ID_SIZE) return 0;
    return name[base_length] == '.' &&
           name[base_length + 1U] == 'Z' &&
           name[base_length + 2U] == 'P' &&
           name[base_length + 3U] == 'K';
}

static void app_catalog_insert_source(const char* alias, uint32_t size) {
    uint32_t position = 0;
    uint32_t limit = catalog_source_count;

    while (position < limit &&
           kstrcmp(catalog_sources[position].alias, alias) < 0) position++;
    if (catalog_source_count >= APP_CATALOG_MAX_SOURCES) {
        catalog_status.source_overflow = 1U;
        app_catalog_record_problem(APP_CATALOG_REASON_SOURCE_LIMIT);
        if (position >= APP_CATALOG_MAX_SOURCES) return;
        limit = APP_CATALOG_MAX_SOURCES - 1U;
    } else {
        catalog_source_count++;
    }
    while (limit > position) {
        catalog_sources[limit] = catalog_sources[limit - 1U];
        limit--;
    }
    app_catalog_copy_text(catalog_sources[position].alias,
                          APP_CATALOG_ALIAS_SIZE, alias);
    catalog_sources[position].size = size;
}

static void app_catalog_collect_sources(void) {
    int count = fs_get_file_count_at("");

    for (int index = 0; index < count; index++) {
        char name[APP_CATALOG_ALIAS_SIZE];
        uint32_t size = 0;
        uint8_t attributes = 0;
        int result = fs_get_file_info_at("", index, name, &size, &attributes);

        if (result != OK) {
            catalog_read_error = 1;
            app_catalog_record_problem(APP_CATALOG_REASON_READ_ERROR);
            LOG_WARN("APPSTORE", "Falha ao consultar entrada do diretorio raiz");
            continue;
        }
        if ((attributes & (APP_CATALOG_DIRECTORY_ATTRIBUTE |
                           FS_ATTRIBUTE_HIDDEN | FS_ATTRIBUTE_SYSTEM)) != 0 ||
            !app_catalog_is_source_name(name)) continue;
        app_catalog_insert_source(name, size);
    }
    catalog_status.source_count = catalog_source_count;
}

static void app_catalog_insert_installed(const app_package_info_t* info) {
    uint32_t position = 0;

    if (catalog_installed_count >= APP_CATALOG_MAX_ENTRIES) {
        catalog_status.entry_overflow = 1U;
        app_catalog_record_problem(APP_CATALOG_REASON_ENTRY_LIMIT);
        LOG_WARN("APPSTORE", "Pacotes instalados excedem limite do catalogo");
        return;
    }
    while (position < catalog_installed_count &&
           kstrcmp(catalog_installed[position].id, info->id) < 0) position++;
    for (uint32_t index = catalog_installed_count; index > position; index--) {
        catalog_installed[index] = catalog_installed[index - 1U];
    }
    catalog_installed[position] = *info;
    catalog_installed_count++;
}

static void app_catalog_collect_installed(void) {
    int count = app_package_get_installed_count();

    catalog_status.installed_count = count > 0 ? (uint32_t)count : 0U;
    for (int index = 0; index < count; index++) {
        app_package_info_t info;

        if (app_package_get_installed_info(index, &info) != OK) {
            catalog_read_error = 1;
            app_catalog_record_problem(APP_CATALOG_REASON_READ_ERROR);
            LOG_WARN("APPSTORE", "Falha ao consultar pacote instalado");
            continue;
        }
        app_catalog_insert_installed(&info);
    }
}

static const app_package_info_t* app_catalog_find_installed(const char* id) {
    for (uint32_t index = 0; index < catalog_installed_count; index++) {
        if (kstrcmp(catalog_installed[index].id, id) == 0) {
            return &catalog_installed[index];
        }
    }
    return 0;
}

static int app_catalog_alias_matches(const char* alias, const char* id) {
    uint32_t id_length;

    if (!alias || !id) return 0;
    id_length = kstrlen(id);
    if (kstrlen(alias) != id_length + APP_CATALOG_EXTENSION_SIZE) return 0;
    for (uint32_t index = 0; index < id_length; index++) {
        if (alias[index] != id[index]) return 0;
    }
    return alias[id_length] == '.' && alias[id_length + 1U] == 'Z' &&
           alias[id_length + 2U] == 'P' && alias[id_length + 3U] == 'K';
}

static int app_catalog_compare_decimal(const char** left_cursor,
                                       const char** right_cursor) {
    const char* left = *left_cursor;
    const char* right = *right_cursor;
    const char* left_end = left;
    const char* right_end = right;

    while (*left_end && *left_end != '.') left_end++;
    while (*right_end && *right_end != '.') right_end++;
    while (left + 1 < left_end && *left == '0') left++;
    while (right + 1 < right_end && *right == '0') right++;
    if ((left_end - left) != (right_end - right)) {
        return (left_end - left) > (right_end - right) ? 1 : -1;
    }
    while (left < left_end) {
        if (*left != *right) return *left > *right ? 1 : -1;
        left++;
        right++;
    }
    *left_cursor = *left_end == '.' ? left_end + 1 : left_end;
    *right_cursor = *right_end == '.' ? right_end + 1 : right_end;
    return 0;
}

static int app_catalog_compare_versions(const char* left, const char* right) {
    for (uint32_t part = 0; part < APP_CATALOG_VERSION_PARTS; part++) {
        int comparison = app_catalog_compare_decimal(&left, &right);
        if (comparison != 0) return comparison;
    }
    return 0;
}

static uint32_t app_catalog_missing_dependencies(
    const app_package_info_t* source) {
    uint32_t missing = 0;

    for (uint32_t index = 0; index < source->dependency_count; index++) {
        if (!app_catalog_find_installed(source->dependencies[index])) {
            missing |= 1U << index;
        }
    }
    return missing;
}

static void app_catalog_classify(app_catalog_entry_t* entry) {
    int comparison;

    entry->capabilities = APP_CATALOG_CAPABILITY_VERIFY;
    if (entry->has_installed) {
        entry->capabilities |= APP_CATALOG_CAPABILITY_RUN |
                               APP_CATALOG_CAPABILITY_REMOVE;
    }
    entry->missing_dependency_mask =
        app_catalog_missing_dependencies(&entry->source);
    if (entry->missing_dependency_mask != 0) {
        entry->state = APP_CATALOG_STATE_BLOCKED;
        entry->reason = APP_CATALOG_REASON_DEPENDENCY_MISSING;
        return;
    }
    if (!entry->has_installed) {
        entry->state = APP_CATALOG_STATE_AVAILABLE;
        entry->capabilities |= APP_CATALOG_CAPABILITY_INSTALL;
        return;
    }
    comparison = app_catalog_compare_versions(entry->source.version,
                                              entry->installed.version);
    if (comparison > 0) {
        entry->state = APP_CATALOG_STATE_UPDATE_AVAILABLE;
        entry->capabilities |= APP_CATALOG_CAPABILITY_UPDATE;
    } else if (comparison < 0) {
        entry->state = APP_CATALOG_STATE_DOWNGRADE;
    } else {
        entry->state = APP_CATALOG_STATE_SAME_VERSION;
    }
}

static int app_catalog_append(const app_catalog_entry_t* entry) {
    if (catalog_status.entry_count >= APP_CATALOG_MAX_ENTRIES) {
        catalog_status.entry_overflow = 1U;
        app_catalog_record_problem(APP_CATALOG_REASON_ENTRY_LIMIT);
        LOG_WARN("APPSTORE", "Catalogo excedeu o limite de entradas");
        return ERR_OVERFLOW;
    }
    catalog_entries[catalog_status.entry_count++] = *entry;
    return OK;
}

static void app_catalog_add_source(const app_catalog_source_t* source) {
    app_catalog_entry_t entry;
    const app_package_info_t* installed;
    int result;

    kmemset(&entry, 0, sizeof(entry));
    entry.has_source = 1U;
    entry.source_size = source->size;
    entry.state = APP_CATALOG_STATE_INVALID;
    entry.capabilities = APP_CATALOG_CAPABILITY_VERIFY;
    app_catalog_copy_text(entry.alias, APP_CATALOG_ALIAS_SIZE, source->alias);
    result = app_package_verify_file(source->alias, &entry.source);
    if (result != OK) {
        entry.reason = result == ERR_NOT_FOUND ?
            APP_CATALOG_REASON_READ_ERROR :
            APP_CATALOG_REASON_PACKAGE_INVALID;
        catalog_status.invalid_source_count++;
        app_catalog_record_problem(entry.reason);
        app_catalog_append(&entry);
        return;
    }
    if (!app_catalog_alias_matches(source->alias, entry.source.id)) {
        entry.reason = APP_CATALOG_REASON_ALIAS_MISMATCH;
        entry.capabilities = APP_CATALOG_CAPABILITY_VERIFY;
        catalog_status.invalid_source_count++;
        app_catalog_record_problem(entry.reason);
        app_catalog_append(&entry);
        return;
    }
    catalog_status.valid_source_count++;
    installed = app_catalog_find_installed(entry.source.id);
    if (installed) {
        entry.installed = *installed;
        entry.has_installed = 1U;
    }
    app_catalog_classify(&entry);
    app_catalog_append(&entry);
}

static int app_catalog_has_id(const char* id) {
    for (uint32_t index = 0; index < catalog_status.entry_count; index++) {
        const app_catalog_entry_t* entry = &catalog_entries[index];
        if (entry->has_source && kstrcmp(entry->source.id, id) == 0) return 1;
        if (entry->has_installed &&
            kstrcmp(entry->installed.id, id) == 0) return 1;
    }
    return 0;
}

static void app_catalog_append_installed(void) {
    for (uint32_t index = 0; index < catalog_installed_count; index++) {
        app_catalog_entry_t entry;

        kmemset(&entry, 0, sizeof(entry));
        entry.installed = catalog_installed[index];
        if (app_catalog_has_id(entry.installed.id)) continue;
        entry.has_installed = 1U;
        entry.state = APP_CATALOG_STATE_INSTALLED;
        entry.reason = APP_CATALOG_REASON_NONE;
        entry.capabilities = APP_CATALOG_CAPABILITY_RUN |
                             APP_CATALOG_CAPABILITY_REMOVE;
        app_catalog_append(&entry);
    }
}

static const char* app_catalog_entry_key(const app_catalog_entry_t* entry) {
    if (entry->alias[0]) return entry->alias;
    return entry->installed.id;
}

static void app_catalog_sort_entries(void) {
    for (uint32_t index = 1; index < catalog_status.entry_count; index++) {
        app_catalog_entry_t item = catalog_entries[index];
        uint32_t position = index;

        while (position > 0 &&
               kstrcmp(app_catalog_entry_key(&catalog_entries[position - 1U]),
                       app_catalog_entry_key(&item)) > 0) {
            catalog_entries[position] = catalog_entries[position - 1U];
            position--;
        }
        catalog_entries[position] = item;
    }
}

static int app_catalog_check_dependencies(void) {
    if (fs_get_type() == FS_TYPE_NONE) {
        catalog_status.reason = APP_CATALOG_REASON_FILESYSTEM_UNAVAILABLE;
        recovery_mark_disabled(RECOVERY_COMPONENT_APP_STORE, ERR_UNAVAILABLE,
                               "App Store requer filesystem");
        LOG_ERROR("APPSTORE", "Filesystem indisponivel para o catalogo");
        return ERR_UNAVAILABLE;
    }
    if (!app_loader_is_ready()) {
        catalog_status.reason = APP_CATALOG_REASON_LOADER_UNAVAILABLE;
        recovery_mark_disabled(RECOVERY_COMPONENT_APP_STORE, ERR_UNAVAILABLE,
                               "App Store requer loader ZAPP");
        LOG_ERROR("APPSTORE", "Loader ZAPP indisponivel para o catalogo");
        return ERR_UNAVAILABLE;
    }
    if (!app_package_is_ready()) {
        catalog_status.reason =
            APP_CATALOG_REASON_PACKAGE_SERVICE_UNAVAILABLE;
        recovery_mark_disabled(RECOVERY_COMPONENT_APP_STORE, ERR_UNAVAILABLE,
                               "App Store requer servico PKG");
        LOG_ERROR("APPSTORE", "Servico PKG indisponivel para o catalogo");
        return ERR_UNAVAILABLE;
    }
    return OK;
}

static void app_catalog_refresh_recovery(void) {
    if (catalog_read_error) {
        recovery_mark_degraded(RECOVERY_COMPONENT_APP_STORE, ERR_DISK,
                               "Catalogo App Store possui leitura parcial");
    } else if (catalog_status.source_overflow ||
               catalog_status.entry_overflow) {
        recovery_mark_degraded(RECOVERY_COMPONENT_APP_STORE, ERR_OVERFLOW,
                               "Catalogo App Store atingiu um limite");
    } else if (catalog_status.invalid_source_count > 0) {
        recovery_mark_degraded(RECOVERY_COMPONENT_APP_STORE, ERR_INVALID,
                               "Catalogo App Store possui fontes invalidas");
    } else {
        recovery_mark_ready(RECOVERY_COMPONENT_APP_STORE);
    }
}

int app_catalog_init(void) {
    int result;

    LOG_INFO("APPSTORE", "Inicializando catalogo da App Store");
    catalog_initialized = 1;
    catalog_ready = 0;
    result = app_catalog_refresh();
    if (result != OK) {
        LOG_ERROR("APPSTORE", "Falha ao inicializar catalogo da App Store");
        return result;
    }
    LOG_INFO("APPSTORE", "Catalogo da App Store inicializado com sucesso");
    return OK;
}

int app_catalog_refresh(void) {
    int result;

    if (!catalog_initialized) {
        LOG_ERROR("APPSTORE", "Refresh solicitado antes da inicializacao");
        return ERR_STATE;
    }
    catalog_ready = 0;
    catalog_source_count = 0;
    catalog_installed_count = 0;
    catalog_read_error = 0;
    kmemset(&catalog_status, 0, sizeof(catalog_status));
    kmemset(catalog_entries, 0, sizeof(catalog_entries));
    kmemset(catalog_sources, 0, sizeof(catalog_sources));
    kmemset(catalog_installed, 0, sizeof(catalog_installed));
    result = app_catalog_check_dependencies();
    if (result != OK) return result;
    app_catalog_collect_sources();
    app_catalog_collect_installed();
    for (uint32_t index = 0; index < catalog_source_count; index++) {
        app_catalog_add_source(&catalog_sources[index]);
    }
    app_catalog_append_installed();
    app_catalog_sort_entries();
    catalog_ready = 1;
    app_catalog_refresh_recovery();
    return OK;
}

int app_catalog_is_ready(void) {
    return catalog_ready;
}

int app_catalog_get_status(app_catalog_status_t* status_out) {
    if (!catalog_initialized || !status_out) {
        LOG_ERROR("APPSTORE", "Consulta de status do catalogo invalida");
        return !status_out ? ERR_NULL : ERR_STATE;
    }
    *status_out = catalog_status;
    return OK;
}

int app_catalog_get_count(uint32_t* count_out) {
    if (!count_out) {
        LOG_ERROR("APPSTORE", "Destino nulo na contagem do catalogo");
        return ERR_NULL;
    }
    if (!catalog_ready) {
        LOG_WARN("APPSTORE", "Contagem solicitada com catalogo indisponivel");
        return ERR_UNAVAILABLE;
    }
    *count_out = catalog_status.entry_count;
    return OK;
}

int app_catalog_get_entry(uint32_t index, app_catalog_entry_t* entry_out) {
    if (!entry_out) {
        LOG_ERROR("APPSTORE", "Destino nulo na consulta do catalogo");
        return ERR_NULL;
    }
    if (!catalog_ready) {
        LOG_WARN("APPSTORE", "Entrada solicitada com catalogo indisponivel");
        return ERR_UNAVAILABLE;
    }
    if (index >= catalog_status.entry_count) {
        LOG_WARN("APPSTORE", "Indice do catalogo fora do limite");
        return ERR_NOT_FOUND;
    }
    *entry_out = catalog_entries[index];
    return OK;
}

int app_catalog_find_entry(const char* id_or_alias,
                           app_catalog_entry_t* entry_out) {
    if (!id_or_alias || !entry_out) {
        LOG_ERROR("APPSTORE", "Argumento nulo na busca do catalogo");
        return ERR_NULL;
    }
    if (!catalog_ready) {
        LOG_WARN("APPSTORE", "Busca solicitada com catalogo indisponivel");
        return ERR_UNAVAILABLE;
    }
    for (uint32_t index = 0; index < catalog_status.entry_count; index++) {
        const app_catalog_entry_t* entry = &catalog_entries[index];
        if ((entry->alias[0] && kstrcmp(entry->alias, id_or_alias) == 0) ||
            (entry->has_source &&
             kstrcmp(entry->source.id, id_or_alias) == 0) ||
            (entry->has_installed &&
             kstrcmp(entry->installed.id, id_or_alias) == 0)) {
            *entry_out = *entry;
            return OK;
        }
    }
    LOG_WARN("APPSTORE", "Entrada do catalogo nao encontrada");
    return ERR_NOT_FOUND;
}

const char* app_catalog_state_name(app_catalog_state_t state) {
    switch (state) {
        case APP_CATALOG_STATE_AVAILABLE: return "AVAILABLE";
        case APP_CATALOG_STATE_INSTALLED: return "INSTALLED";
        case APP_CATALOG_STATE_UPDATE_AVAILABLE: return "UPDATE_AVAILABLE";
        case APP_CATALOG_STATE_SAME_VERSION: return "SAME_VERSION";
        case APP_CATALOG_STATE_DOWNGRADE: return "DOWNGRADE";
        case APP_CATALOG_STATE_BLOCKED: return "BLOCKED";
        case APP_CATALOG_STATE_INVALID: return "INVALID";
        default: return "UNKNOWN";
    }
}

const char* app_catalog_reason_name(app_catalog_reason_t reason) {
    switch (reason) {
        case APP_CATALOG_REASON_NONE: return "NONE";
        case APP_CATALOG_REASON_PACKAGE_INVALID: return "PACKAGE_INVALID";
        case APP_CATALOG_REASON_ALIAS_MISMATCH: return "ALIAS_MISMATCH";
        case APP_CATALOG_REASON_DEPENDENCY_MISSING: return "DEPENDENCY_MISSING";
        case APP_CATALOG_REASON_INSUFFICIENT_SPACE: return "INSUFFICIENT_SPACE";
        case APP_CATALOG_REASON_SOURCE_LIMIT: return "SOURCE_LIMIT";
        case APP_CATALOG_REASON_ENTRY_LIMIT: return "ENTRY_LIMIT";
        case APP_CATALOG_REASON_READ_ERROR: return "READ_ERROR";
        case APP_CATALOG_REASON_FILESYSTEM_UNAVAILABLE:
            return "FILESYSTEM_UNAVAILABLE";
        case APP_CATALOG_REASON_LOADER_UNAVAILABLE:
            return "LOADER_UNAVAILABLE";
        case APP_CATALOG_REASON_PACKAGE_SERVICE_UNAVAILABLE:
            return "PACKAGE_SERVICE_UNAVAILABLE";
        default: return "UNKNOWN";
    }
}
