#include "core/app_files.h"
#include "core/errors.h"
#include "core/log.h"
#include "fs/vfs.h"

static int app_files_ready;

int app_files_init(void) {
    int result;

    LOG_INFO("APP_FILES", "Inicializando fachada de arquivos VFS");
    if (app_files_ready) {
        LOG_WARN("APP_FILES", "Fachada de arquivos ja estava inicializada");
        return OK;
    }
    result = vfs_init();
    if (result != OK) {
        LOG_ERROR("APP_FILES", "Falha ao inicializar VFS");
        return result;
    }
    app_files_ready = 1;
    LOG_INFO("APP_FILES", "Fachada de arquivos VFS inicializada");
    return OK;
}

int app_files_is_ready(void) {
    return app_files_ready && vfs_is_ready();
}

int app_files_open(const char* path, uint32_t mode, app_handle_t* handle) {
    int32_t fd = VFS_FD_INVALID;
    int result;

    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Abertura antes da inicializacao");
        return ERR_STATE;
    }
    if (!handle) {
        LOG_ERROR("APP_FILES", "Destino de descritor nulo");
        return ERR_NULL;
    }
    result = vfs_open(path, mode, &fd);
    if (result != OK) return result;
    *handle = (app_handle_t)fd;
    return OK;
}

int app_files_read(app_handle_t handle, uint8_t* buffer,
                   uint32_t size, uint32_t* bytes_read) {
    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Leitura antes da inicializacao");
        return ERR_STATE;
    }
    return vfs_read((int32_t)handle, buffer, size, bytes_read);
}

int app_files_write(app_handle_t handle, const uint8_t* buffer,
                    uint32_t size, uint32_t* bytes_written) {
    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Escrita antes da inicializacao");
        return ERR_STATE;
    }
    return vfs_write((int32_t)handle, buffer, size, bytes_written);
}

int app_files_close(app_handle_t handle) {
    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Fechamento antes da inicializacao");
        return ERR_STATE;
    }
    return vfs_close((int32_t)handle);
}

int app_files_lseek(app_handle_t handle, int32_t offset, uint32_t whence,
                    uint32_t* position) {
    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Seek antes da inicializacao");
        return ERR_STATE;
    }
    return vfs_lseek((int32_t)handle, offset, whence, position);
}

int app_files_close_owner(uint32_t pid) {
    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Limpeza antes da inicializacao");
        return ERR_STATE;
    }
    return vfs_close_owner(pid);
}
