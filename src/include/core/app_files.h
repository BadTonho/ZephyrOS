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
int app_files_close(app_handle_t handle);
int app_files_close_owner(uint32_t pid);

#endif
