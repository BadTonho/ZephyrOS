#ifndef APP_API_H
#define APP_API_H

#include "types.h"

#define APP_API_VERSION_MAJOR 0
#define APP_API_VERSION_MINOR 1
#define APP_API_MAX_TEXT_SIZE 1024
#define APP_API_MAX_FILE_IO_SIZE 4096
#define APP_API_TICKS_PER_SECOND 50
#define APP_HANDLE_INVALID 0

#define APP_FILE_MODE_READ       1
#define APP_FILE_MODE_WRITE      2
#define APP_FILE_MODE_READ_WRITE 3

#define APP_MESSAGE_KEYBOARD    1
#define APP_MESSAGE_APP_REQUEST 2

typedef uint32_t app_handle_t;

typedef struct {
    uint32_t major;
    uint32_t minor;
} app_api_version_t;

typedef struct {
    uint32_t ticks;
    uint32_t seconds;
} app_uptime_info_t;

typedef struct {
    uint32_t total_bytes;
    uint32_t used_bytes;
    uint32_t free_bytes;
    uint32_t total_pages;
    uint32_t free_pages;
} app_memory_info_t;

typedef struct {
    uint32_t type;
    uint32_t data1;
    uint32_t data2;
} app_message_t;

int app_api_init(void);
int app_api_is_ready(void);
int app_api_get_version(app_api_version_t* version);
int app_api_console_write(const char* text, uint32_t size);
int app_api_get_uptime(app_uptime_info_t* info);
int app_api_get_memory_info(app_memory_info_t* info);
int app_api_file_open(const char* path, uint32_t mode, app_handle_t* handle);
int app_api_file_read(app_handle_t handle, uint8_t* buffer,
                      uint32_t size, uint32_t* bytes_read);
int app_api_file_write(app_handle_t handle, const uint8_t* buffer,
                       uint32_t size, uint32_t* bytes_written);
int app_api_file_close(app_handle_t handle);
int app_api_message_send(uint32_t pid, const app_message_t* message);
int app_api_message_receive(app_message_t* message);
int app_api_file_is_ready(void);
int app_api_ipc_is_ready(void);

#endif
