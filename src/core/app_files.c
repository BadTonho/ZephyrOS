#include "core/app_files.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/poll.h"
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

int app_files_poll(pollfd_t* fds, uint32_t count, uint32_t timeout_ticks,
                   uint32_t* out_ready) {
    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Poll antes da inicializacao");
        return ERR_STATE;
    }
    return vfs_poll(fds, count, timeout_ticks, out_ready);
}

int app_files_select(uint32_t nfds, fd_set_t* readfds, fd_set_t* writefds,
                     fd_set_t* exceptfds, uint32_t timeout_ticks,
                     uint32_t* out_ready) {
    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Select antes da inicializacao");
        return ERR_STATE;
    }
    return vfs_select(nfds, readfds, writefds, exceptfds,
                      timeout_ticks, out_ready);
}

int app_files_close(app_handle_t handle) {
    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Fechamento antes da inicializacao");
        return ERR_STATE;
    }
    return vfs_close((int32_t)handle);
}

int app_files_fsync(app_handle_t handle) {
    int result;

    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Fsync antes da inicializacao");
        return ERR_STATE;
    }
    result = vfs_fsync((int32_t)handle);
    if (result != OK) LOG_ERROR("APP_FILES", "Fsync VFS falhou");
    return result;
}

int app_files_sync(void) {
    int result;

    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Sync antes da inicializacao");
        return ERR_STATE;
    }
    result = vfs_sync();
    if (result != OK) LOG_ERROR("APP_FILES", "Sync VFS falhou");
    return result;
}

int app_files_lseek(app_handle_t handle, int32_t offset, uint32_t whence,
                    uint32_t* position) {
    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Seek antes da inicializacao");
        return ERR_STATE;
    }
    return vfs_lseek((int32_t)handle, offset, whence, position);
}

int app_files_ioctl(app_handle_t handle, uint32_t request, void* argument) {
    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Ioctl antes da inicializacao");
        return ERR_STATE;
    }
    return vfs_ioctl((int32_t)handle, request, argument);
}

int app_files_pipe(app_handle_t fds[2]) {
    int32_t raw_fds[2];
    int result;

    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Pipe antes da inicializacao");
        return ERR_STATE;
    }
    if (!fds) {
        LOG_ERROR("APP_FILES", "Destino de pipe nulo");
        return ERR_NULL;
    }
    fds[0] = APP_HANDLE_INVALID;
    fds[1] = APP_HANDLE_INVALID;
    result = vfs_pipe(raw_fds);
    if (result != OK) {
        LOG_ERROR("APP_FILES", "Falha ao criar pipe VFS");
        return result;
    }
    fds[0] = (app_handle_t)raw_fds[0];
    fds[1] = (app_handle_t)raw_fds[1];
    return OK;
}

int app_files_chdir(const char* path) {
    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Chdir antes da inicializacao");
        return ERR_STATE;
    }
    return vfs_chdir(path);
}

int app_files_getcwd(char* path, uint32_t capacity) {
    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Getcwd antes da inicializacao");
        return ERR_STATE;
    }
    return vfs_getcwd(path, capacity);
}

int app_files_close_owner(uint32_t pid) {
    if (!app_files_is_ready()) {
        LOG_ERROR("APP_FILES", "Limpeza antes da inicializacao");
        return ERR_STATE;
    }
    return vfs_close_owner(pid);
}
