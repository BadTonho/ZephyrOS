#ifndef APP_FILES_H
#define APP_FILES_H

#include "types.h"
#include "core/app_api.h"

#define APP_FILE_HANDLE_COUNT 32

int app_files_init(void);
int app_files_is_ready(void);
int app_files_open(const char* path, uint32_t mode, app_handle_t* handle);
int app_files_read(app_handle_t handle, uint8_t* buffer,
                   uint32_t size, uint32_t* bytes_read);
int app_files_write(app_handle_t handle, const uint8_t* buffer,
                    uint32_t size, uint32_t* bytes_written);
int app_files_poll(pollfd_t* fds, uint32_t count, uint32_t timeout_ticks,
                   uint32_t* out_ready);
int app_files_select(uint32_t nfds, fd_set_t* readfds, fd_set_t* writefds,
                     fd_set_t* exceptfds, uint32_t timeout_ticks,
                     uint32_t* out_ready);
int app_files_close(app_handle_t handle);
int app_files_fsync(app_handle_t handle);
int app_files_sync(void);
int app_files_close_owner(uint32_t pid);
int app_files_lseek(app_handle_t handle, int32_t offset, uint32_t whence,
                    uint32_t* position);
int app_files_ioctl(app_handle_t handle, uint32_t request, void* argument);
int app_files_pipe(app_handle_t fds[2]);
int app_files_chdir(const char* path);
int app_files_getcwd(char* path, uint32_t capacity);

#endif
