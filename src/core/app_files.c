#include "core/app_files.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "fs/fs.h"
#include "process/process.h"

#define APP_FILE_SLOT_MASK 0xFFU
#define APP_FILE_GENERATION_MASK 0x00FFFFFFU
#define APP_FILE_NO_SLOT -1

typedef struct {
    int used;
    uint32_t generation;
    uint32_t owner_pid;
    uint32_t mode;
    uint32_t offset;
    char path[FS_MAX_PATH];
} app_file_entry_t;

static app_file_entry_t app_file_entries[APP_FILE_HANDLE_COUNT];
static uint32_t app_file_generations[APP_FILE_HANDLE_COUNT];
static int app_files_ready;

static int app_files_validate_mode(uint32_t mode) {
    if (mode == 0 || (mode & ~APP_FILE_MODE_READ_WRITE) != 0) {
        LOG_ERROR("APP_FILES", "Modo de arquivo invalido");
        return ERR_INVALID;
    }
    return OK;
}

static int app_files_validate_path(const char* path) {
    uint32_t length;

    if (!path) {
        LOG_ERROR("APP_FILES", "Caminho de arquivo nulo");
        return ERR_NULL;
    }

    length = kstrlen(path);
    if (length == 0) {
        LOG_ERROR("APP_FILES", "Caminho de arquivo vazio");
        return ERR_INVALID;
    }
    if (length >= FS_MAX_PATH) {
        LOG_ERROR("APP_FILES", "Caminho de arquivo excede o limite");
        return ERR_OVERFLOW;
    }

    return OK;
}

static int app_files_find_free_slot(void) {
    for (uint32_t i = 0; i < APP_FILE_HANDLE_COUNT; i++) {
        if (!app_file_entries[i].used) return (int)i;
    }

    LOG_WARN("APP_FILES", "Tabela de handles de arquivo cheia");
    return APP_FILE_NO_SLOT;
}

static int app_files_get_entry(app_handle_t handle, app_file_entry_t** entry) {
    uint32_t slot;
    uint32_t generation;
    process_t* current;

    if (!entry || handle == APP_HANDLE_INVALID) {
        LOG_ERROR("APP_FILES", "Handle de arquivo invalido");
        return ERR_INVALID;
    }

    slot = handle & APP_FILE_SLOT_MASK;
    generation = (handle >> 8) & APP_FILE_GENERATION_MASK;
    if (slot == 0 || slot > APP_FILE_HANDLE_COUNT || generation == 0) {
        LOG_ERROR("APP_FILES", "Handle fora dos limites");
        return ERR_INVALID;
    }

    *entry = &app_file_entries[slot - 1];
    if (!(*entry)->used || (*entry)->generation != generation) {
        LOG_WARN("APP_FILES", "Handle expirado ou ja fechado");
        return ERR_INVALID;
    }

    current = process_get_current();
    if (!current) {
        LOG_ERROR("APP_FILES", "Handle usado sem processo atual");
        return ERR_STATE;
    }
    if ((*entry)->owner_pid != current->pid) {
        LOG_WARN("APP_FILES", "Processo tentou usar handle de outro PID");
        return ERR_STATE;
    }

    return OK;
}

static app_handle_t app_files_make_handle(uint32_t slot) {
    return (app_handle_t)((app_file_generations[slot] << 8) | (slot + 1));
}

int app_files_init(void) {
    LOG_INFO("APP_FILES", "Inicializando tabela de handles de arquivo");

    if (app_files_ready) {
        LOG_WARN("APP_FILES", "Tabela de handles ja estava inicializada");
        return OK;
    }

    kmemset(app_file_entries, 0, sizeof(app_file_entries));
    kmemset(app_file_generations, 0, sizeof(app_file_generations));
    app_files_ready = 1;
    LOG_INFO("APP_FILES", "Tabela de handles de arquivo inicializada");
    return OK;
}

int app_files_is_ready(void) {
    return app_files_ready;
}

int app_files_open(const char* path, uint32_t mode, app_handle_t* handle) {
    process_t* current;
    int result;
    int slot;

    if (!app_files_ready) {
        LOG_ERROR("APP_FILES", "Tabela usada antes da inicializacao");
        return ERR_STATE;
    }
    if (!handle) {
        LOG_ERROR("APP_FILES", "Destino de handle nulo");
        return ERR_NULL;
    }

    result = app_files_validate_mode(mode);
    if (result != OK) return result;
    result = app_files_validate_path(path);
    if (result != OK) return result;
    if (fs_get_type() == FS_TYPE_NONE) {
        LOG_WARN("APP_FILES", "Abertura rejeitada sem filesystem montado");
        return ERR_UNAVAILABLE;
    }

    current = process_get_current();
    if (!current) {
        LOG_ERROR("APP_FILES", "Abertura sem processo atual");
        return ERR_STATE;
    }

    if (mode & APP_FILE_MODE_READ) {
        uint32_t available = 0;
        result = fs_read_file_range_at(path, 0, 0, 0, &available);
        if (result != OK) {
            LOG_WARN("APP_FILES", "Arquivo nao existe ou nao pode ser lido");
            return result;
        }
    }

    slot = app_files_find_free_slot();
    if (slot < 0) return ERR_UNAVAILABLE;

    app_file_entries[slot].used = 1;
    app_file_entries[slot].generation =
        (app_file_generations[slot] + 1) & APP_FILE_GENERATION_MASK;
    if (app_file_entries[slot].generation == 0) {
        app_file_entries[slot].generation = 1;
    }
    app_file_generations[slot] = app_file_entries[slot].generation;
    app_file_entries[slot].owner_pid = current->pid;
    app_file_entries[slot].mode = mode;
    app_file_entries[slot].offset = 0;
    kmemcpy(app_file_entries[slot].path, path, kstrlen(path) + 1);
    *handle = app_files_make_handle((uint32_t)slot);
    return OK;
}

int app_files_read(app_handle_t handle, uint8_t* buffer,
                   uint32_t size, uint32_t* bytes_read) {
    app_file_entry_t* entry;
    int result;
    uint32_t count = 0;

    if (!app_files_ready) {
        LOG_ERROR("APP_FILES", "Leitura antes da inicializacao");
        return ERR_STATE;
    }
    if (!bytes_read) {
        LOG_ERROR("APP_FILES", "Destino de leitura nulo");
        return ERR_NULL;
    }
    *bytes_read = 0;
    if (size > APP_API_MAX_FILE_IO_SIZE) {
        LOG_ERROR("APP_FILES", "Leitura excede o limite da API");
        return ERR_OVERFLOW;
    }
    if (size > 0 && !buffer) {
        LOG_ERROR("APP_FILES", "Buffer de leitura nulo");
        return ERR_NULL;
    }

    result = app_files_get_entry(handle, &entry);
    if (result != OK) return result;
    if (!(entry->mode & APP_FILE_MODE_READ)) {
        LOG_WARN("APP_FILES", "Handle sem permissao de leitura");
        return ERR_UNAVAILABLE;
    }

    result = fs_read_file_range_at(entry->path, entry->offset,
                                   buffer, size, &count);
    if (result != OK) return result;
    if (entry->offset > 0xFFFFFFFFU - count) {
        LOG_ERROR("APP_FILES", "Offset de leitura estourou");
        return ERR_OVERFLOW;
    }

    entry->offset += count;
    *bytes_read = count;
    return OK;
}

int app_files_write(app_handle_t handle, const uint8_t* buffer,
                    uint32_t size, uint32_t* bytes_written) {
    app_file_entry_t* entry;
    int result;

    if (!app_files_ready) {
        LOG_ERROR("APP_FILES", "Escrita antes da inicializacao");
        return ERR_STATE;
    }
    if (!bytes_written) {
        LOG_ERROR("APP_FILES", "Destino de escrita nulo");
        return ERR_NULL;
    }
    *bytes_written = 0;
    if (size > APP_API_MAX_FILE_IO_SIZE) {
        LOG_ERROR("APP_FILES", "Escrita excede o limite da API");
        return ERR_OVERFLOW;
    }
    if (size > 0 && !buffer) {
        LOG_ERROR("APP_FILES", "Buffer de escrita nulo");
        return ERR_NULL;
    }

    result = app_files_get_entry(handle, &entry);
    if (result != OK) return result;
    if (!(entry->mode & APP_FILE_MODE_WRITE)) {
        LOG_WARN("APP_FILES", "Handle sem permissao de escrita");
        return ERR_UNAVAILABLE;
    }

    result = fs_write_file_at(entry->path, buffer, size);
    if (result != OK) return result;

    entry->offset = 0;
    *bytes_written = size;
    return OK;
}

int app_files_close(app_handle_t handle) {
    app_file_entry_t* entry;
    int result;

    if (!app_files_ready) {
        LOG_ERROR("APP_FILES", "Fechamento antes da inicializacao");
        return ERR_STATE;
    }
    result = app_files_get_entry(handle, &entry);

    if (result != OK) return result;
    kmemset(entry, 0, sizeof(app_file_entry_t));
    return OK;
}
